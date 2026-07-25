#include "ring.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "vm.h"

#include <stddef.h>

#define RING_GENERATION_MASK 0x00ffffffu

typedef enum KernelRingState {
    KERNEL_RING_FREE = 0,
    KERNEL_RING_OPEN,
    KERNEL_RING_CLOSING
} KernelRingState;

struct KernelRing {
    KernelThreadWaitQueue producer_waiters;
    KernelThreadWaitQueue consumer_waiters;
    KernelArea *area;
    uint32_t owner;
    uint32_t generation;
    uint32_t area_generation;
    uint32_t offset;
    uint32_t total_size;
    uint32_t element_size;
    uint32_t capacity;
    uint32_t producer_position;
    uint32_t consumer_position;
    uint32_t producer_terminal;
    uint32_t consumer_terminal;
    uint16_t producer_references;
    uint16_t consumer_references;
    uint8_t slot;
    uint8_t state;
    uint8_t child_released;
    uint8_t reserved;
};

#if defined(__m68k__)
_Static_assert(sizeof(KernelRing) == 80u,
               "ring record size changed; update the memory budget");
#endif

static KernelRing rings[KERNEL_RING_MAX];
static KernelRingPoolStats pool_stats;
static uint8_t pool_corrupt;

_Static_assert(sizeof(AstraBulkRingHeader) == KERNEL_RING_HEADER_SIZE,
               "bulk-ring header ABI size changed");
_Static_assert(offsetof(AstraBulkRingHeader, producer_position) == 0x20u,
               "bulk-ring producer position moved");
_Static_assert(offsetof(AstraBulkRingHeader, consumer_position) == 0x30u,
               "bulk-ring consumer position moved");

static bool is_power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool valid_ring(const KernelRing *ring)
{
    return ring != NULL && ring >= &rings[0] && ring < &rings[KERNEL_RING_MAX] &&
           ring->slot == (uint8_t)(ring - rings) && ring->generation != 0u &&
           ring->generation <= RING_GENERATION_MASK &&
           ring->state >= KERNEL_RING_OPEN && ring->state <= KERNEL_RING_CLOSING;
}

static void reset_ring(KernelRing *ring, uint8_t slot)
{
    uint32_t generation = ring->generation;

    kernel_bytes_clear(ring, sizeof(*ring));
    ring->generation = generation;
    ring->slot = slot;
    ring->state = KERNEL_RING_FREE;
    ring->child_released = 1u;
    kernel_thread_wait_queue_init(&ring->producer_waiters);
    kernel_thread_wait_queue_init(&ring->consumer_waiters);
}

static uint32_t ring_used(const KernelRing *ring)
{
    return ring->producer_position - ring->consumer_position;
}

static bool valid_endpoint(KernelRingEndpoint endpoint)
{
    return endpoint == KERNEL_RING_ENDPOINT_PRODUCER ||
           endpoint == KERNEL_RING_ENDPOINT_CONSUMER;
}

static uint32_t owner_ring_count(uint32_t owner)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        if (rings[slot].state != KERNEL_RING_FREE && rings[slot].owner == owner)
            ++count;
    }
    return count;
}

static uint32_t area_ring_count(const KernelArea *area)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        if (rings[slot].state != KERNEL_RING_FREE && rings[slot].area == area)
            ++count;
    }
    return count;
}

static bool ranges_overlap(uint32_t first, uint32_t first_size,
                           uint32_t second, uint32_t second_size)
{
    return first < second + second_size && second < first + first_size;
}

static KernelRingStatus release_child(KernelRing *ring)
{
    if (ring->child_released != 0u)
        return KERNEL_RING_OK;
    if (ring->area == NULL ||
        kernel_area_child_release(ring->area) != KERNEL_AREA_OK)
        return KERNEL_RING_CORRUPT;
    ring->child_released = 1u;
    return KERNEL_RING_OK;
}

static void maybe_free(KernelRing *ring)
{
    if (!valid_ring(ring) || ring->producer_references != 0u ||
        ring->consumer_references != 0u)
        return;
    if (kernel_thread_wait_queue_count(&ring->producer_waiters) != 0u ||
        kernel_thread_wait_queue_count(&ring->consumer_waiters) != 0u ||
        release_child(ring) != KERNEL_RING_OK ||
        pool_stats.active_rings == 0u) {
        pool_corrupt = 1u;
        return;
    }
    if (ring->state == KERNEL_RING_CLOSING) {
        if (pool_stats.closing_rings == 0u) {
            pool_corrupt = 1u;
            return;
        }
        --pool_stats.closing_rings;
    }
    --pool_stats.active_rings;
    reset_ring(ring, ring->slot);
}

static KernelRingStatus wake_queue(KernelThreadWaitQueue *queue,
                                   uint32_t result, uint32_t *total_woken)
{
    uint32_t woken = 0u;

    if (kernel_thread_wake_all(queue, result, &woken) != KERNEL_THREAD_OK)
        return KERNEL_RING_CORRUPT;
    pool_stats.wait_wakeups += woken;
    if (total_woken != NULL)
        *total_woken += woken;
    return KERNEL_RING_OK;
}

static KernelRingStatus fail_ring(KernelRing *ring, uint32_t terminal,
                                  uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (!valid_ring(ring))
        return KERNEL_RING_INVALID_ARGUMENT;
    if (ring->state == KERNEL_RING_CLOSING) {
        if (woken_threads != NULL)
            *woken_threads = 0u;
        return KERNEL_RING_OK;
    }
    ring->state = KERNEL_RING_CLOSING;
    ring->producer_terminal = terminal;
    ring->consumer_terminal = terminal;
    ++pool_stats.closing_rings;
    if (wake_queue(&ring->producer_waiters, terminal, &woken) !=
            KERNEL_RING_OK ||
        wake_queue(&ring->consumer_waiters, terminal, &woken) !=
            KERNEL_RING_OK) {
        pool_corrupt = 1u;
        return KERNEL_RING_CORRUPT;
    }
    if (woken_threads != NULL)
        *woken_threads = woken;
    maybe_free(ring);
    return KERNEL_RING_OK;
}

void kernel_ring_pool_init(void)
{
    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        uint32_t generation = rings[slot].generation;

        reset_ring(&rings[slot], (uint8_t)slot);
        rings[slot].generation = generation == 0u ? 1u : generation;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
}

KernelRingStatus kernel_ring_create(uint32_t owner, KernelArea *area,
                                    uint32_t offset, uint32_t element_size,
                                    uint32_t capacity, KernelRing **result)
{
    KernelRing *ring = NULL;
    AstraBulkRingHeader header;
    uint64_t total_size;

    if (owner == 0u || area == NULL || result == NULL ||
        !kernel_area_live(area) ||
        (offset & (KERNEL_RING_OFFSET_ALIGNMENT - 1u)) != 0u ||
        element_size < KERNEL_RING_ELEMENT_SIZE_MIN ||
        element_size > KERNEL_RING_ELEMENT_SIZE_MAX ||
        (element_size & 3u) != 0u ||
        capacity < KERNEL_RING_CAPACITY_MIN ||
        capacity > KERNEL_RING_CAPACITY_MAX || !is_power_of_two(capacity))
        return KERNEL_RING_INVALID_ARGUMENT;
    *result = NULL;
    total_size = (uint64_t)KERNEL_RING_HEADER_SIZE +
                 (uint64_t)element_size * capacity;
    if (total_size > UINT32_MAX || offset > kernel_area_size(area) ||
        total_size > kernel_area_size(area) - offset)
        return KERNEL_RING_INVALID_ARGUMENT;
    if (owner_ring_count(owner) >= KERNEL_RING_OWNER_MAX ||
        area_ring_count(area) >= KERNEL_RING_AREA_MAX) {
        ++pool_stats.quota_failures;
        return KERNEL_RING_QUOTA_EXCEEDED;
    }
    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        if (rings[slot].state != KERNEL_RING_FREE &&
            rings[slot].area == area &&
            ranges_overlap(offset, (uint32_t)total_size, rings[slot].offset,
                           rings[slot].total_size)) {
            ++pool_stats.overlap_failures;
            return KERNEL_RING_OVERLAP;
        }
        if (ring == NULL && rings[slot].state == KERNEL_RING_FREE)
            ring = &rings[slot];
    }
    if (ring == NULL) {
        ++pool_stats.allocation_failures;
        return KERNEL_RING_NO_SLOT;
    }
    if (kernel_area_child_retain(area) != KERNEL_AREA_OK)
        return KERNEL_RING_PEER_DEAD;

    ring->generation = kernel_generation_next_masked(
        ring->generation, RING_GENERATION_MASK);
    ring->area = area;
    ring->owner = owner;
    ring->area_generation = kernel_area_generation(area);
    ring->offset = offset;
    ring->total_size = (uint32_t)total_size;
    ring->element_size = element_size;
    ring->capacity = capacity;
    ring->producer_position = 0u;
    ring->consumer_position = 0u;
    ring->producer_terminal = ASTRA_SYSCALL_OK;
    ring->consumer_terminal = ASTRA_SYSCALL_OK;
    ring->producer_references = 1u;
    ring->consumer_references = 1u;
    ring->state = KERNEL_RING_OPEN;
    ring->child_released = 0u;
    kernel_thread_wait_queue_init(&ring->producer_waiters);
    kernel_thread_wait_queue_init(&ring->consumer_waiters);

    kernel_bytes_clear(&header, sizeof(header));
    header.magic = KERNEL_RING_MAGIC;
    header.version = KERNEL_RING_ABI_VERSION;
    header.header_size = KERNEL_RING_HEADER_SIZE;
    header.element_size = element_size;
    header.capacity = capacity;
    header.data_offset = KERNEL_RING_HEADER_SIZE;
    header.total_size = (uint32_t)total_size;
    header.generation = ring->generation;
    if (kernel_vm_sync_shared_aliases() != KERNEL_VM_OK ||
        kernel_area_write(area, offset, &header, sizeof(header)) !=
            KERNEL_AREA_OK ||
        kernel_vm_sync_shared_aliases() != KERNEL_VM_OK) {
        ring->producer_references = 0u;
        ring->consumer_references = 0u;
        if (release_child(ring) != KERNEL_RING_OK)
            pool_corrupt = 1u;
        reset_ring(ring, ring->slot);
        return KERNEL_RING_CORRUPT;
    }
    ++pool_stats.created_rings;
    ++pool_stats.active_rings;
    if (pool_stats.active_rings > pool_stats.max_active_rings)
        pool_stats.max_active_rings = pool_stats.active_rings;
    *result = ring;
    return KERNEL_RING_OK;
}

void kernel_ring_abandon_unpublished(KernelRing *ring)
{
    if (!valid_ring(ring) || ring->state != KERNEL_RING_OPEN ||
        ring->producer_references != 1u || ring->consumer_references != 1u ||
        kernel_thread_wait_queue_count(&ring->producer_waiters) != 0u ||
        kernel_thread_wait_queue_count(&ring->consumer_waiters) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    ring->producer_references = 0u;
    ring->consumer_references = 0u;
    maybe_free(ring);
}

void kernel_ring_handle_release(void *object, void *context)
{
    KernelRing *ring = object;
    KernelRingEndpoint endpoint = (KernelRingEndpoint)(uintptr_t)context;
    uint32_t woken = 0u;

    if (!valid_ring(ring) || !valid_endpoint(endpoint)) {
        pool_corrupt = 1u;
        return;
    }
    if (endpoint == KERNEL_RING_ENDPOINT_PRODUCER) {
        if (ring->producer_references == 0u) {
            pool_corrupt = 1u;
            return;
        }
        --ring->producer_references;
        if (ring->producer_references == 0u &&
            ring->state == KERNEL_RING_OPEN) {
            ring->producer_terminal = ASTRA_SYSCALL_CLOSED;
            ring->consumer_terminal = ASTRA_SYSCALL_PEER_DEAD;
            if (wake_queue(&ring->producer_waiters, ASTRA_SYSCALL_CLOSED,
                           &woken) != KERNEL_RING_OK ||
                wake_queue(&ring->consumer_waiters,
                           ASTRA_SYSCALL_PEER_DEAD, &woken) != KERNEL_RING_OK)
                pool_corrupt = 1u;
            ++pool_stats.peer_closures;
        }
    } else {
        if (ring->consumer_references == 0u) {
            pool_corrupt = 1u;
            return;
        }
        --ring->consumer_references;
        if (ring->consumer_references == 0u &&
            ring->state == KERNEL_RING_OPEN) {
            ring->consumer_terminal = ASTRA_SYSCALL_CLOSED;
            ring->producer_terminal = ASTRA_SYSCALL_PEER_DEAD;
            if (wake_queue(&ring->consumer_waiters, ASTRA_SYSCALL_CLOSED,
                           &woken) != KERNEL_RING_OK ||
                wake_queue(&ring->producer_waiters,
                           ASTRA_SYSCALL_PEER_DEAD, &woken) != KERNEL_RING_OK)
                pool_corrupt = 1u;
            ++pool_stats.peer_closures;
        }
    }
    maybe_free(ring);
}

KernelRingStatus kernel_ring_notify(KernelRing *ring,
                                    KernelRingEndpoint endpoint,
                                    uint32_t position, uint32_t flags,
                                    uint32_t *producer_position,
                                    uint32_t *consumer_position,
                                    uint32_t *woken_threads)
{
    uint32_t woken = 0u;
    uint32_t used;

    if (!valid_ring(ring) || !valid_endpoint(endpoint) ||
        (flags & ~KERNEL_RING_NOTIFY_CORRUPT) != 0u)
        return KERNEL_RING_INVALID_ARGUMENT;
    if (woken_threads != NULL)
        *woken_threads = 0u;
    if (ring->state == KERNEL_RING_CLOSING)
        return ring->producer_terminal == ASTRA_SYSCALL_IO_ERROR ||
                       ring->consumer_terminal == ASTRA_SYSCALL_IO_ERROR ?
                   KERNEL_RING_IO_ERROR : KERNEL_RING_PEER_DEAD;
    if ((flags & KERNEL_RING_NOTIFY_CORRUPT) != 0u) {
        ++pool_stats.corruption_failures;
        if (fail_ring(ring, ASTRA_SYSCALL_IO_ERROR, &woken) != KERNEL_RING_OK)
            return KERNEL_RING_CORRUPT;
        if (woken_threads != NULL)
            *woken_threads = woken;
        return KERNEL_RING_IO_ERROR;
    }

    used = ring_used(ring);
    if (used > ring->capacity)
        return KERNEL_RING_CORRUPT;
    if (endpoint == KERNEL_RING_ENDPOINT_PRODUCER) {
        uint32_t advance = position - ring->producer_position;

        if (ring->producer_references == 0u)
            return KERNEL_RING_CLOSED;
        if (ring->consumer_references == 0u)
            return KERNEL_RING_PEER_DEAD;
        if (advance > ring->capacity - used) {
            ++pool_stats.corruption_failures;
            if (fail_ring(ring, ASTRA_SYSCALL_IO_ERROR, &woken) !=
                KERNEL_RING_OK)
                return KERNEL_RING_CORRUPT;
            if (woken_threads != NULL)
                *woken_threads = woken;
            return KERNEL_RING_IO_ERROR;
        }
        ring->producer_position = position;
        ++pool_stats.producer_notifications;
        if (advance != 0u && ring_used(ring) != 0u &&
            wake_queue(&ring->consumer_waiters, ASTRA_SYSCALL_OK, &woken) !=
                KERNEL_RING_OK)
            return KERNEL_RING_CORRUPT;
    } else {
        uint32_t advance = position - ring->consumer_position;

        if (ring->consumer_references == 0u)
            return KERNEL_RING_CLOSED;
        if (advance > used) {
            ++pool_stats.corruption_failures;
            if (fail_ring(ring, ASTRA_SYSCALL_IO_ERROR, &woken) !=
                KERNEL_RING_OK)
                return KERNEL_RING_CORRUPT;
            if (woken_threads != NULL)
                *woken_threads = woken;
            return KERNEL_RING_IO_ERROR;
        }
        ring->consumer_position = position;
        ++pool_stats.consumer_notifications;
        if (advance != 0u && ring->producer_references != 0u &&
            ring_used(ring) < ring->capacity &&
            wake_queue(&ring->producer_waiters, ASTRA_SYSCALL_OK, &woken) !=
                KERNEL_RING_OK)
            return KERNEL_RING_CORRUPT;
    }
    if (woken_threads != NULL)
        *woken_threads = woken;
    if (producer_position != NULL)
        *producer_position = ring->producer_position;
    if (consumer_position != NULL)
        *consumer_position = ring->consumer_position;
    return KERNEL_RING_OK;
}

KernelRingStatus kernel_ring_prepare_wait(KernelRing *ring,
                                          KernelRingEndpoint endpoint,
                                          KernelThreadWaitSpec *spec)
{
    uint32_t used;

    if (!valid_ring(ring) || !valid_endpoint(endpoint) || spec == NULL)
        return KERNEL_RING_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    if (ring->state == KERNEL_RING_CLOSING)
        return ring->producer_terminal == ASTRA_SYSCALL_IO_ERROR ||
                       ring->consumer_terminal == ASTRA_SYSCALL_IO_ERROR ?
                   KERNEL_RING_IO_ERROR : KERNEL_RING_PEER_DEAD;
    used = ring_used(ring);
    if (used > ring->capacity)
        return KERNEL_RING_CORRUPT;
    if (endpoint == KERNEL_RING_ENDPOINT_PRODUCER) {
        if (ring->producer_references == 0u)
            return KERNEL_RING_CLOSED;
        if (ring->consumer_references == 0u)
            return KERNEL_RING_PEER_DEAD;
        if (used < ring->capacity)
            return KERNEL_RING_OK;
        spec->queue = &ring->producer_waiters;
        ++pool_stats.producer_waits;
    } else {
        if (ring->consumer_references == 0u)
            return KERNEL_RING_CLOSED;
        if (used != 0u)
            return KERNEL_RING_OK;
        if (ring->producer_references == 0u)
            return KERNEL_RING_PEER_DEAD;
        spec->queue = &ring->consumer_waiters;
        ++pool_stats.consumer_waits;
    }
    spec->sequence = kernel_thread_wait_queue_sequence(spec->queue);
    return spec->sequence == 0u ? KERNEL_RING_CORRUPT :
                                 KERNEL_RING_WOULD_BLOCK;
}

KernelRingStatus kernel_ring_commit_wait(KernelRing *ring,
                                         KernelRingEndpoint endpoint)
{
    KernelThreadWaitQueue *queue;
    uint32_t waiters;

    if (!valid_ring(ring) || !valid_endpoint(endpoint))
        return KERNEL_RING_INVALID_ARGUMENT;
    queue = endpoint == KERNEL_RING_ENDPOINT_PRODUCER ?
        &ring->producer_waiters : &ring->consumer_waiters;
    waiters = kernel_thread_wait_queue_count(queue);
    return waiters != 0u && waiters <= KERNEL_THREAD_MAX ? KERNEL_RING_OK :
                                                           KERNEL_RING_INVALID_STATE;
}

KernelRingStatus kernel_ring_process_died(uint32_t process_id,
                                          uint32_t *closed_rings,
                                          uint32_t *woken_threads)
{
    uint32_t closed = 0u;
    uint32_t woken = 0u;

    if (process_id == 0u)
        return KERNEL_RING_INVALID_ARGUMENT;
    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        KernelRing *ring = &rings[slot];
        uint32_t ring_woken = 0u;

        if (ring->state != KERNEL_RING_OPEN ||
            (ring->owner != process_id &&
             kernel_area_creator(ring->area) != process_id))
            continue;
        if (fail_ring(ring, ASTRA_SYSCALL_PEER_DEAD, &ring_woken) !=
            KERNEL_RING_OK)
            return KERNEL_RING_CORRUPT;
        woken += ring_woken;
        ++closed;
    }
    if (closed != 0u)
        ++pool_stats.owner_deaths;
    if (closed_rings != NULL)
        *closed_rings = closed;
    if (woken_threads != NULL)
        *woken_threads = woken;
    return KERNEL_RING_OK;
}

uint32_t kernel_ring_terminal_result(const KernelRing *ring,
                                     KernelRingEndpoint endpoint)
{
    if (!valid_ring(ring) || !valid_endpoint(endpoint))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return endpoint == KERNEL_RING_ENDPOINT_PRODUCER ?
        ring->producer_terminal : ring->consumer_terminal;
}

bool kernel_ring_snapshot(uint32_t slot, KernelRingSnapshot *snapshot)
{
    const KernelRing *ring;
    uint32_t producer_waiters;
    uint32_t consumer_waiters;

    if (slot >= KERNEL_RING_MAX || snapshot == NULL)
        return false;
    ring = &rings[slot];
    producer_waiters = kernel_thread_wait_queue_count(&ring->producer_waiters);
    consumer_waiters = kernel_thread_wait_queue_count(&ring->consumer_waiters);
    if (producer_waiters > UINT16_MAX || consumer_waiters > UINT16_MAX)
        return false;
    snapshot->owner = ring->owner;
    snapshot->generation = ring->generation;
    snapshot->area_generation = ring->area_generation;
    snapshot->offset = ring->offset;
    snapshot->total_size = ring->total_size;
    snapshot->element_size = ring->element_size;
    snapshot->capacity = ring->capacity;
    snapshot->producer_position = ring->producer_position;
    snapshot->consumer_position = ring->consumer_position;
    snapshot->producer_terminal = ring->producer_terminal;
    snapshot->consumer_terminal = ring->consumer_terminal;
    snapshot->producer_references = ring->producer_references;
    snapshot->consumer_references = ring->consumer_references;
    snapshot->producer_waiters = (uint16_t)producer_waiters;
    snapshot->consumer_waiters = (uint16_t)consumer_waiters;
    snapshot->state = ring->state;
    snapshot->child_released = ring->child_released;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    return true;
}

bool kernel_ring_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_ring_pool_valid(void)
{
    uint32_t active = 0u;
    uint32_t closing = 0u;

    if (!kernel_ring_pool_healthy())
        return false;
    for (uint32_t slot = 0u; slot < KERNEL_RING_MAX; ++slot) {
        const KernelRing *ring = &rings[slot];
        uint32_t producer_waiters =
            kernel_thread_wait_queue_count(&ring->producer_waiters);
        uint32_t consumer_waiters =
            kernel_thread_wait_queue_count(&ring->consumer_waiters);

        if (ring->slot != slot || ring->generation == 0u ||
            ring->generation > RING_GENERATION_MASK ||
            producer_waiters > KERNEL_THREAD_MAX ||
            consumer_waiters > KERNEL_THREAD_MAX)
            return false;
        if (ring->state == KERNEL_RING_FREE) {
            if (ring->area != NULL || ring->owner != 0u ||
                ring->producer_references != 0u ||
                ring->consumer_references != 0u ||
                producer_waiters != 0u || consumer_waiters != 0u ||
                ring->child_released == 0u)
                return false;
            continue;
        }
        if (!valid_ring(ring) || ring->area == NULL || ring->owner == 0u ||
            ring->area_generation == 0u || ring->capacity == 0u ||
            ring->total_size != KERNEL_RING_HEADER_SIZE +
                                    ring->element_size * ring->capacity ||
            ring_used(ring) > ring->capacity)
            return false;
        ++active;
        if (ring->state == KERNEL_RING_OPEN) {
            if (ring->child_released != 0u ||
                (ring->producer_references == 0u &&
                 ring->consumer_references == 0u))
                return false;
        } else {
            if (ring->child_released != 0u ||
                (ring->producer_references == 0u &&
                 ring->consumer_references == 0u))
                return false;
            ++closing;
        }
    }
    return active == pool_stats.active_rings &&
           closing == pool_stats.closing_rings && active <= KERNEL_RING_MAX;
}

bool kernel_ring_pool_stats(KernelRingPoolStats *stats)
{
    if (stats == NULL || !kernel_ring_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    return true;
}

#if defined(KERNEL_RING_HOST_TEST)
bool kernel_ring_test_set_positions(KernelRing *ring,
                                    uint32_t producer_position,
                                    uint32_t consumer_position)
{
    if (!valid_ring(ring) || ring->state != KERNEL_RING_OPEN ||
        producer_position - consumer_position > ring->capacity)
        return false;
    ring->producer_position = producer_position;
    ring->consumer_position = consumer_position;
    return true;
}
#endif
