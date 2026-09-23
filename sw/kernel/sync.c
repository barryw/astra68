#include "sync.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "memory.h"

#include <stddef.h>
#include <stdint.h>
#if !defined(__m68k__) && !defined(KERNEL_MEMORY_HOST_TEST)
#include <stdlib.h>
#define KERNEL_SYNC_STANDALONE_HOST 1
#endif

/*
 * Object tables live in their own region above the frame metadata, not beside
 * the kernel image. Raising a limit then costs address space there and moves
 * no address anything else reads -- the trace ring especially, which crash
 * tooling finds at a fixed address. kernel.ld names this region if it fills.
 */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif


struct KernelSyncObject {
    KernelThreadWaitQueue waiters;
    uint32_t owner;
    uint32_t generation;
    uint32_t count;
    uint32_t maximum;
    uint32_t close_result;
    uint64_t deadline;
    uint16_t references;
    uint16_t slot;
    uint16_t timer_position;
    uint8_t type;
    uint8_t state;
};

typedef struct KernelFutexSlot {
    KernelThreadWaitQueue waiters;
    struct KernelFutexSlot *next;
    uint32_t process_id;
    uint32_t address;
    uint32_t resource_owner;
    uint32_t physical;
} KernelFutexSlot;

#define KERNEL_SYNC_TIMER_SLOT_NONE UINT16_MAX
#define SYNC_LEAF_BITS 6u
#define SYNC_LEAF_ENTRIES (1u << SYNC_LEAF_BITS)
#define SYNC_LEAF_COUNT \
    ((KERNEL_SYNC_OBJECT_MAX + SYNC_LEAF_ENTRIES - 1u) / SYNC_LEAF_ENTRIES)
#define SYNC_LEAF_FRAMES \
    ((SYNC_LEAF_ENTRIES * sizeof(KernelSyncObject) + KERNEL_PAGE_SIZE - 1u) / \
     KERNEL_PAGE_SIZE)
#define TIMER_HEAP_LEAF_ENTRIES (KERNEL_PAGE_SIZE / sizeof(uint16_t))
#define TIMER_HEAP_LEAF_COUNT \
    ((KERNEL_SYNC_OBJECT_MAX + TIMER_HEAP_LEAF_ENTRIES - 1u) / \
     TIMER_HEAP_LEAF_ENTRIES)

static KernelSyncObject *object_directory[SYNC_LEAF_COUNT] KERNEL_TABLES;
static uint32_t object_directory_physical[SYNC_LEAF_COUNT] KERNEL_TABLES;
static uint16_t *timer_heap_directory[TIMER_HEAP_LEAF_COUNT] KERNEL_TABLES;
static uint32_t timer_heap_directory_physical[TIMER_HEAP_LEAF_COUNT]
    KERNEL_TABLES;
static KernelFutexSlot *futex_slots;
static uint32_t object_backed_limit;
static uint16_t next_object_slot;
static uint32_t active_object_count;
static uint32_t next_object_generation;
static KernelSyncPoolStats pool_stats;
static uint8_t pool_corrupt;
static uint16_t timer_count;

_Static_assert(sizeof(KernelSyncObject) <= 56u,
               "synchronization object memory budget changed");

static bool discard_sync_metadata(void);

static KernelFutexSlot *futex_slot_find(uint32_t process_id,
                                        uint32_t address)
{
    for (KernelFutexSlot *slot = futex_slots; slot != NULL;
         slot = slot->next) {
        uint32_t waiters = kernel_thread_wait_queue_count(&slot->waiters);

        if (waiters == UINT32_MAX) {
            pool_corrupt = 1u;
            return NULL;
        }
        if (slot->process_id == process_id &&
            slot->address == address)
            return slot;
    }
    return NULL;
}

static KernelFutexSlot *futex_slot_create(uint32_t process_id,
                                          uint32_t resource_owner,
                                          uint32_t address)
{
    KernelFutexSlot *slot;

#if defined(KERNEL_SYNC_STANDALONE_HOST)
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_SYNC_OBJECT,
                                   resource_owner))
        return NULL;
    slot = calloc(1u, sizeof(*slot));
    if (slot == NULL ||
        !kernel_allocation_commit(KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u,
                                  sizeof(*slot), resource_owner)) {
        free(slot);
        return NULL;
    }
#else
    uint32_t physical;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u, 1u,
            KERNEL_FRAME_KERNEL, resource_owner, &physical) !=
        KERNEL_MEMORY_OK)
        return NULL;
    slot = kernel_memory_access(physical, KERNEL_PAGE_SIZE);
    if (slot == NULL) {
        (void)kernel_memory_release(physical, 1u, resource_owner);
        return NULL;
    }
    slot->physical = physical;
#endif
    kernel_thread_wait_queue_init(&slot->waiters);
    slot->process_id = process_id;
    slot->address = address;
    slot->resource_owner = resource_owner;
    slot->next = futex_slots;
    futex_slots = slot;
    return slot;
}

static bool futex_slot_release(KernelFutexSlot *slot)
{
    KernelFutexSlot **link = &futex_slots;

    while (*link != NULL && *link != slot)
        link = &(*link)->next;
    if (*link == NULL || kernel_thread_wait_queue_count(&slot->waiters) != 0u)
        return false;
    *link = slot->next;
#if defined(KERNEL_SYNC_STANDALONE_HOST)
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u,
                                   sizeof(*slot)))
        return false;
    free(slot);
    return true;
#else
    return kernel_memory_release(slot->physical, 1u,
                                 slot->resource_owner) == KERNEL_MEMORY_OK;
#endif
}

static bool valid_type(uint8_t type)
{
    return type == KERNEL_SYNC_EVENT_AUTO ||
           type == KERNEL_SYNC_EVENT_MANUAL ||
           type == KERNEL_SYNC_SEMAPHORE ||
           type == KERNEL_SYNC_TIMER;
}

static KernelSyncObject *object_at(uint32_t slot)
{
    KernelSyncObject *leaf;

    if (slot >= KERNEL_SYNC_OBJECT_MAX)
        return NULL;
    leaf = object_directory[slot >> SYNC_LEAF_BITS];
    return leaf != NULL ? &leaf[slot & (SYNC_LEAF_ENTRIES - 1u)] : NULL;
}

static void *allocate_sync_metadata(uint32_t frames, uint32_t *physical)
{
#if defined(KERNEL_SYNC_STANDALONE_HOST)
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_SYNC_METADATA,
                                   KERNEL_OWNER_CORE))
        return NULL;
    void *memory = calloc(frames, KERNEL_PAGE_SIZE);

    *physical = 0u;
    if (memory == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_SYNC_METADATA,
                               KERNEL_OWNER_CORE);
        return NULL;
    }
    if (!kernel_allocation_commit(
            KERNEL_ALLOCATION_SITE_SYNC_METADATA, frames,
            frames * KERNEL_PAGE_SIZE, KERNEL_OWNER_CORE)) {
        free(memory);
        return NULL;
    }
    return memory;
#else
    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_SYNC_METADATA, frames, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, physical) !=
        KERNEL_MEMORY_OK)
        return NULL;
    void *memory = kernel_memory_access(*physical,
                                        frames * KERNEL_PAGE_SIZE);
    if (memory == NULL) {
        (void)kernel_memory_release(*physical, frames, KERNEL_OWNER_CORE);
        return NULL;
    }
    return memory;
#endif
}

static bool ensure_object_leaf(uint32_t slot)
{
    uint32_t leaf_index = slot >> SYNC_LEAF_BITS;
    KernelSyncObject *leaf;

    if (object_directory[leaf_index] != NULL)
        return true;
    leaf = allocate_sync_metadata(
        SYNC_LEAF_FRAMES, &object_directory_physical[leaf_index]);
    if (leaf == NULL)
        return false;
    object_directory[leaf_index] = leaf;
    for (uint32_t index = 0u; index < SYNC_LEAF_ENTRIES; ++index) {
        uint32_t object_slot = leaf_index * SYNC_LEAF_ENTRIES + index;

        leaf[index].generation = 1u;
        leaf[index].slot = object_slot < KERNEL_SYNC_OBJECT_MAX ?
            (uint16_t)object_slot : UINT16_MAX;
        leaf[index].timer_position = KERNEL_SYNC_TIMER_SLOT_NONE;
        leaf[index].state = KERNEL_SYNC_FREE;
        kernel_thread_wait_queue_init(&leaf[index].waiters);
    }
    uint32_t limit = (leaf_index + 1u) * SYNC_LEAF_ENTRIES;
    if (limit > KERNEL_SYNC_OBJECT_MAX)
        limit = KERNEL_SYNC_OBJECT_MAX;
    if (limit > object_backed_limit)
        object_backed_limit = limit;
    return true;
}

static bool ensure_timer_heap_position(uint32_t position)
{
    uint32_t leaf_index = position / TIMER_HEAP_LEAF_ENTRIES;

    if (timer_heap_directory[leaf_index] != NULL)
        return true;
    timer_heap_directory[leaf_index] = allocate_sync_metadata(
        1u, &timer_heap_directory_physical[leaf_index]);
    return timer_heap_directory[leaf_index] != NULL;
}

static uint16_t timer_heap_get(uint32_t position)
{
    uint16_t *leaf = timer_heap_directory[
        position / TIMER_HEAP_LEAF_ENTRIES];

    return leaf[position % TIMER_HEAP_LEAF_ENTRIES];
}

static void timer_heap_set(uint32_t position, uint16_t slot)
{
    timer_heap_directory[position / TIMER_HEAP_LEAF_ENTRIES]
                        [position % TIMER_HEAP_LEAF_ENTRIES] = slot;
}

static uint16_t timer_slot(const KernelSyncObject *object)
{
    return object->slot;
}

static bool timer_less(uint16_t left, uint16_t right)
{
    const KernelSyncObject *left_object = object_at(left);
    const KernelSyncObject *right_object = object_at(right);

    return left_object->deadline < right_object->deadline ||
           (left_object->deadline == right_object->deadline && left < right);
}

static void timer_heap_swap(uint16_t left, uint16_t right)
{
    uint16_t left_slot = timer_heap_get(left);
    uint16_t right_slot = timer_heap_get(right);

    timer_heap_set(left, right_slot);
    timer_heap_set(right, left_slot);
    object_at(left_slot)->timer_position = right;
    object_at(right_slot)->timer_position = left;
}

static void timer_sift_up(uint16_t position)
{
    while (position != 0u) {
        uint16_t parent = (uint16_t)((position - 1u) / 2u);

        if (!timer_less(timer_heap_get(position), timer_heap_get(parent)))
            break;
        timer_heap_swap(position, parent);
        position = parent;
    }
}

static void timer_sift_down(uint16_t position)
{
    for (;;) {
        uint32_t left = (uint32_t)position * 2u + 1u;
        uint32_t right = left + 1u;
        uint32_t smallest = position;

        if (left < timer_count &&
            timer_less(timer_heap_get(left), timer_heap_get(smallest)))
            smallest = left;
        if (right < timer_count &&
            timer_less(timer_heap_get(right), timer_heap_get(smallest)))
            smallest = right;
        if (smallest == position)
            break;
        timer_heap_swap(position, (uint16_t)smallest);
        position = (uint16_t)smallest;
    }
}

static bool timer_remove(uint16_t slot)
{
    KernelSyncObject *object = object_at(slot);
    uint16_t position;
    uint16_t last;
    uint16_t moved;

    if (object == NULL)
        return false;
    position = object->timer_position;
    if (position == KERNEL_SYNC_TIMER_SLOT_NONE)
        return true;
    if (position >= timer_count || timer_heap_get(position) != slot ||
        timer_count == 0u)
        return false;
    last = (uint16_t)(timer_count - 1u);
    if (position != last)
        timer_heap_swap(position, last);
    moved = timer_heap_get(position);
    --timer_count;
    object->timer_position = KERNEL_SYNC_TIMER_SLOT_NONE;
    object->deadline = 0u;
    if (position < timer_count) {
        uint16_t parent = position == 0u ? 0u :
            (uint16_t)((position - 1u) / 2u);

        if (position != 0u &&
            timer_less(moved, timer_heap_get(parent)))
            timer_sift_up(position);
        else
            timer_sift_down(position);
    }
    return true;
}

static KernelSyncStatus timer_insert(uint16_t slot, uint64_t deadline)
{
    KernelSyncObject *object = object_at(slot);
    uint16_t position;

    if (object == NULL ||
        object->timer_position != KERNEL_SYNC_TIMER_SLOT_NONE ||
        timer_count >= KERNEL_SYNC_OBJECT_MAX)
        return KERNEL_SYNC_CORRUPT;
    if (!ensure_timer_heap_position(timer_count))
        return KERNEL_SYNC_NO_SLOT;
    position = timer_count++;
    timer_heap_set(position, slot);
    object->timer_position = position;
    object->deadline = deadline;
    timer_sift_up(position);
    if (timer_count > pool_stats.max_armed_timers)
        pool_stats.max_armed_timers = timer_count;
    return KERNEL_SYNC_OK;
}

static bool valid_object_pointer(const KernelSyncObject *object)
{
    return object != NULL && object->slot < KERNEL_SYNC_OBJECT_MAX &&
           object_at(object->slot) == object;
}

static bool valid_live_object(const KernelSyncObject *object)
{
    return valid_object_pointer(object) &&
           object->state == KERNEL_SYNC_LIVE &&
           valid_type(object->type) && object->owner != 0u &&
           object->generation != 0u && object->references != 0u &&
           object->close_result == 0u &&
           kernel_thread_wait_queue_count(&object->waiters) != UINT32_MAX &&
           ((object->type == KERNEL_SYNC_SEMAPHORE &&
             object->maximum != 0u &&
             object->maximum <= KERNEL_SYNC_SEMAPHORE_COUNT_MAX &&
             object->count <= object->maximum) ||
            (object->type != KERNEL_SYNC_SEMAPHORE &&
             object->maximum == 1u && object->count <= 1u));
}

static void update_live_maximum(void)
{
    if (active_object_count > pool_stats.max_live_objects)
        pool_stats.max_live_objects = active_object_count;
}

static KernelSyncStatus allocate_object(uint32_t owner, uint8_t type,
                                        KernelSyncObject **object)
{
    if (owner == 0u || !valid_type(type) || object == NULL)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *object = NULL;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_SYNC_OBJECT,
                                   owner)) {
        ++pool_stats.allocation_failures;
        return KERNEL_SYNC_NO_SLOT;
    }
    KernelSyncObject *candidate = NULL;
    for (uint32_t offset = 0u; offset < KERNEL_SYNC_OBJECT_MAX; ++offset) {
        uint32_t slot = (uint32_t)next_object_slot + offset;

        if (slot >= KERNEL_SYNC_OBJECT_MAX)
            slot -= KERNEL_SYNC_OBJECT_MAX;
        if (!ensure_object_leaf(slot))
            break;
        candidate = object_at(slot);
        if (candidate->state == KERNEL_SYNC_FREE) {
            next_object_slot = slot + 1u == KERNEL_SYNC_OBJECT_MAX ?
                0u : (uint16_t)(slot + 1u);
            break;
        }
        candidate = NULL;
    }
    if (candidate == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_SYNC_OBJECT, owner);
        ++pool_stats.allocation_failures;
        return KERNEL_SYNC_NO_SLOT;
    }
    if (!kernel_allocation_commit(
            KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u, sizeof(*candidate),
            owner)) {
        pool_corrupt = 1u;
        return KERNEL_SYNC_CORRUPT;
    }
    uint32_t generation;
    uint16_t slot = candidate->slot;

    if (candidate->state != KERNEL_SYNC_FREE ||
        candidate->timer_position != KERNEL_SYNC_TIMER_SLOT_NONE) {
        pool_corrupt = 1u;
        return KERNEL_SYNC_CORRUPT;
    }
    generation = kernel_generation_next(next_object_generation);
    next_object_generation = generation;
    kernel_bytes_clear(candidate, sizeof(*candidate));
    kernel_thread_wait_queue_init(&candidate->waiters);
    candidate->owner = owner;
    candidate->generation = generation;
    candidate->references = 1u;
    candidate->slot = slot;
    candidate->timer_position = KERNEL_SYNC_TIMER_SLOT_NONE;
    candidate->type = type;
    candidate->state = KERNEL_SYNC_LIVE;
    ++active_object_count;
    *object = candidate;
    update_live_maximum();
    return KERNEL_SYNC_OK;
}

static void free_object(KernelSyncObject *object)
{
    uint32_t generation;

    if (!valid_object_pointer(object) || object->references != 0u ||
        object->timer_position != KERNEL_SYNC_TIMER_SLOT_NONE ||
        kernel_thread_wait_queue_count(&object->waiters) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    generation = object->generation;
    uint16_t slot = object->slot;
    kernel_bytes_clear(object, sizeof(*object));
    object->generation = generation;
    object->slot = slot;
    object->timer_position = KERNEL_SYNC_TIMER_SLOT_NONE;
    object->state = KERNEL_SYNC_FREE;
    if (active_object_count == 0u ||
        !kernel_allocation_release(KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u,
                                   sizeof(*object))) {
        pool_corrupt = 1u;
        return;
    }
    --active_object_count;
    if (active_object_count == 0u) {
        if (!discard_sync_metadata())
            pool_corrupt = 1u;
    } else if (slot < next_object_slot) {
        next_object_slot = slot;
    }
}

static KernelSyncStatus close_object(KernelSyncObject *object,
                                     uint32_t wake_result,
                                     uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (!valid_object_pointer(object) || wake_result == ASTRA_SYSCALL_OK)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    if (object->state == KERNEL_SYNC_CLOSING) {
        if (woken_threads != NULL)
            *woken_threads = 0u;
        return KERNEL_SYNC_CLOSED;
    }
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;

    if (object->type == KERNEL_SYNC_TIMER &&
        !timer_remove(timer_slot(object)))
        return KERNEL_SYNC_CORRUPT;

    object->state = KERNEL_SYNC_CLOSING;
    object->close_result = wake_result;
    if (kernel_thread_wake_all(&object->waiters, wake_result, &woken) !=
        KERNEL_THREAD_OK)
        return KERNEL_SYNC_CORRUPT;
    ++pool_stats.close_operations;
    pool_stats.close_wakeups += woken;
    if (woken_threads != NULL)
        *woken_threads = woken;
    if (object->references == 0u)
        free_object(object);
    return KERNEL_SYNC_OK;
}

static bool discard_sync_metadata(void)
{
    KernelAllocationStats metadata;
    uint32_t heap_leaves = 0u;
    uint32_t object_leaves = 0u;

    for (uint32_t leaf = 0u; leaf < SYNC_LEAF_COUNT; ++leaf) {
        if (object_directory[leaf] == NULL)
            continue;
        if (leaf != object_leaves)
            return false;
        ++object_leaves;
    }
    for (uint32_t leaf = 0u; leaf < TIMER_HEAP_LEAF_COUNT; ++leaf) {
        if (timer_heap_directory[leaf] == NULL)
            continue;
        if (leaf != heap_leaves)
            return false;
        ++heap_leaves;
    }
    uint32_t frames = object_leaves * SYNC_LEAF_FRAMES + heap_leaves;
    if (!kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_SYNC_METADATA, &metadata) ||
        ((metadata.current_units != 0u || metadata.current_bytes != 0u) &&
         (metadata.current_units != frames ||
          metadata.current_bytes != frames * KERNEL_PAGE_SIZE)))
        return false;
    for (uint32_t leaf = 0u; leaf < object_leaves; ++leaf) {
#if defined(KERNEL_SYNC_STANDALONE_HOST)
        free(object_directory[leaf]);
#else
        if (metadata.current_units != 0u &&
            kernel_memory_release(
                object_directory_physical[leaf], SYNC_LEAF_FRAMES,
                KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            return false;
#endif
        object_directory[leaf] = NULL;
        object_directory_physical[leaf] = 0u;
    }
    for (uint32_t leaf = 0u; leaf < heap_leaves; ++leaf) {
#if defined(KERNEL_SYNC_STANDALONE_HOST)
        free(timer_heap_directory[leaf]);
#else
        if (metadata.current_units != 0u &&
            kernel_memory_release(
                timer_heap_directory_physical[leaf], 1u,
                KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            return false;
#endif
        timer_heap_directory[leaf] = NULL;
        timer_heap_directory_physical[leaf] = 0u;
    }
#if defined(KERNEL_SYNC_STANDALONE_HOST)
    if (metadata.current_units != 0u &&
        !kernel_allocation_release(
            KERNEL_ALLOCATION_SITE_SYNC_METADATA, metadata.current_units,
            metadata.current_bytes))
        return false;
#endif
    object_backed_limit = 0u;
    next_object_slot = 0u;
    return true;
}

void kernel_sync_pool_init(void)
{
    KernelAllocationStats allocations;

    while (futex_slots != NULL) {
        KernelFutexSlot *slot = futex_slots;

        if (kernel_thread_wait_queue_count(&slot->waiters) != 0u) {
            pool_corrupt = 1u;
            return;
        }
        if (!futex_slot_release(slot)) {
            pool_corrupt = 1u;
            return;
        }
    }
    if (!kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_SYNC_OBJECT, &allocations) ||
        ((allocations.current_units != 0u ||
          allocations.current_bytes != 0u) &&
         (allocations.current_units != active_object_count ||
          allocations.current_bytes !=
              active_object_count * sizeof(KernelSyncObject))) ||
        (allocations.current_units != 0u &&
         !kernel_allocation_release(
             KERNEL_ALLOCATION_SITE_SYNC_OBJECT,
             allocations.current_units, allocations.current_bytes)) ||
        !discard_sync_metadata()) {
        pool_corrupt = 1u;
        return;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
    active_object_count = 0u;
    timer_count = 0u;
}

KernelFutexStatus kernel_futex_wait(uint32_t process_id,
                                    uint32_t resource_owner,
                                    uint32_t address,
                                    KernelThread *thread, uint64_t now,
                                    uint64_t deadline,
                                    uint32_t timeout_result)
{
    KernelFutexSlot *slot;
    KernelThreadStatus status;
    uint32_t sequence;

    if (process_id == 0u || address == 0u ||
        (address & (sizeof(uint32_t) - 1u)) != 0u || thread == NULL)
        return KERNEL_FUTEX_INVALID_ARGUMENT;
    slot = futex_slot_find(process_id, address);
    if (slot == NULL)
        slot = futex_slot_create(process_id, resource_owner, address);
    if (slot == NULL)
        return pool_corrupt != 0u ? KERNEL_FUTEX_CORRUPT :
                                   KERNEL_FUTEX_NO_SLOT;
    sequence = kernel_thread_wait_queue_sequence(&slot->waiters);
    status = kernel_thread_block_until(thread, &slot->waiters, sequence, now,
                                       deadline, timeout_result);
    if (status == KERNEL_THREAD_OK)
        return KERNEL_FUTEX_BLOCKED;
    if (kernel_thread_wait_queue_count(&slot->waiters) == 0u &&
        !futex_slot_release(slot))
        return KERNEL_FUTEX_CORRUPT;
    if (status == KERNEL_THREAD_DEADLINE_EXPIRED)
        return KERNEL_FUTEX_TIMED_OUT;
    if (status == KERNEL_THREAD_INVALID_ARGUMENT ||
        status == KERNEL_THREAD_INVALID_STATE)
        return KERNEL_FUTEX_INVALID_ARGUMENT;
    return KERNEL_FUTEX_CORRUPT;
}

KernelFutexStatus kernel_futex_wake(uint32_t process_id, uint32_t address,
                                    uint32_t count, uint32_t result,
                                    uint32_t *woken_threads)
{
    KernelFutexSlot *slot;
    uint32_t woken = 0u;

    if (woken_threads == NULL || process_id == 0u || address == 0u ||
        (address & (sizeof(uint32_t) - 1u)) != 0u || count == 0u)
        return KERNEL_FUTEX_INVALID_ARGUMENT;
    *woken_threads = 0u;
    slot = futex_slot_find(process_id, address);
    if (slot == NULL)
        return pool_corrupt != 0u ? KERNEL_FUTEX_CORRUPT : KERNEL_FUTEX_OK;
    while (woken < count &&
           kernel_thread_wait_queue_count(&slot->waiters) != 0u) {
        KernelThread *thread;
        KernelThreadStatus status = kernel_thread_wake_one(
            &slot->waiters, result, &thread);

        if (status != KERNEL_THREAD_OK || thread == NULL)
            return KERNEL_FUTEX_CORRUPT;
        ++woken;
    }
    if (kernel_thread_wait_queue_count(&slot->waiters) == 0u &&
        !futex_slot_release(slot))
        return KERNEL_FUTEX_CORRUPT;
    *woken_threads = woken;
    return KERNEL_FUTEX_OK;
}

KernelFutexStatus kernel_futex_wake_all_irq(uint32_t process_id,
                                            uint32_t address,
                                            uint32_t result,
                                            uint32_t *woken_threads)
{
    KernelFutexSlot *slot;

    if (woken_threads == NULL || process_id == 0u || address == 0u ||
        (address & (sizeof(uint32_t) - 1u)) != 0u)
        return KERNEL_FUTEX_INVALID_ARGUMENT;
    *woken_threads = 0u;
    slot = futex_slot_find(process_id, address);
    if (slot == NULL)
        return pool_corrupt != 0u ? KERNEL_FUTEX_CORRUPT : KERNEL_FUTEX_OK;
    if (kernel_thread_wake_all_irq(&slot->waiters, result, woken_threads) !=
        KERNEL_THREAD_OK)
        return KERNEL_FUTEX_CORRUPT;
    if (!futex_slot_release(slot))
        return KERNEL_FUTEX_CORRUPT;
    return KERNEL_FUTEX_OK;
}

KernelSyncStatus kernel_sync_create_event(uint32_t owner, uint32_t flags,
                                          KernelSyncObject **object)
{
    KernelSyncStatus status;
    uint8_t type;

    if ((flags & ~KERNEL_SYNC_EVENT_FLAGS) != 0u)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    type = (flags & KERNEL_SYNC_EVENT_MANUAL_RESET) != 0u ?
        KERNEL_SYNC_EVENT_MANUAL : KERNEL_SYNC_EVENT_AUTO;
    status = allocate_object(owner, type, object);
    if (status != KERNEL_SYNC_OK)
        return status;
    (*object)->count =
        (flags & KERNEL_SYNC_EVENT_INITIALLY_SIGNALED) != 0u ? 1u : 0u;
    (*object)->maximum = 1u;
    ++pool_stats.created_events;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_create_semaphore(uint32_t owner,
                                              uint32_t initial_count,
                                              uint32_t maximum_count,
                                              KernelSyncObject **object)
{
    KernelSyncStatus status;

    if (maximum_count == 0u ||
        maximum_count > KERNEL_SYNC_SEMAPHORE_COUNT_MAX ||
        initial_count > maximum_count)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    status = allocate_object(owner, KERNEL_SYNC_SEMAPHORE, object);
    if (status != KERNEL_SYNC_OK)
        return status;
    (*object)->count = initial_count;
    (*object)->maximum = maximum_count;
    ++pool_stats.created_semaphores;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_create_timer(uint32_t owner,
                                          KernelSyncObject **object)
{
    KernelSyncStatus status = allocate_object(
        owner, KERNEL_SYNC_TIMER, object);

    if (status != KERNEL_SYNC_OK)
        return status;
    (*object)->maximum = 1u;
    ++pool_stats.created_timers;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_retain(KernelSyncObject *object)
{
    if (!valid_live_object(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    if (object->references == KERNEL_SYNC_REFERENCE_MAX)
        return KERNEL_SYNC_COUNT_LIMIT;
    ++object->references;
    return KERNEL_SYNC_OK;
}

bool kernel_sync_handle_retain(void *raw_object, void *context)
{
    (void)context;
    return kernel_sync_retain(raw_object) == KERNEL_SYNC_OK;
}

void kernel_sync_handle_release(void *raw_object, void *context)
{
    KernelSyncObject *object = raw_object;
    KernelSyncStatus status;

    (void)context;
    if (!valid_object_pointer(object) || object->references == 0u ||
        (object->state != KERNEL_SYNC_LIVE &&
         object->state != KERNEL_SYNC_CLOSING)) {
        pool_corrupt = 1u;
        return;
    }
    if (object->references > 1u) {
        --object->references;
        return;
    }
    if (object->state == KERNEL_SYNC_LIVE) {
        status = close_object(object, ASTRA_SYSCALL_CLOSED, NULL);
        if (status != KERNEL_SYNC_OK) {
            pool_corrupt = 1u;
            return;
        }
        if (!valid_object_pointer(object) || object->references != 1u ||
            object->state != KERNEL_SYNC_CLOSING) {
            pool_corrupt = 1u;
            return;
        }
        object->references = 0u;
        free_object(object);
    } else {
        object->references = 0u;
        free_object(object);
    }
}

void kernel_sync_abandon_unpublished(KernelSyncObject *object)
{
    if (valid_object_pointer(object) && object->state == KERNEL_SYNC_LIVE &&
        object->references == 1u &&
        kernel_thread_wait_queue_count(&object->waiters) == 0u)
        ++pool_stats.publication_rollbacks;
    kernel_sync_handle_release(object, NULL);
}

KernelSyncStatus kernel_sync_prepare_wait(KernelSyncObject *object,
                                          KernelThreadWaitSpec *spec)
{
    uint32_t waiters;

    if (!valid_object_pointer(object) || spec == NULL)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    ++pool_stats.wait_calls;

    if (object->count != 0u) {
        if (object->type != KERNEL_SYNC_EVENT_MANUAL &&
            object->type != KERNEL_SYNC_TIMER)
            --object->count;
        ++pool_stats.immediate_waits;
        return KERNEL_SYNC_OK;
    }
    waiters = kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX)
        return KERNEL_SYNC_CORRUPT;
    spec->queue = &object->waiters;
    spec->sequence = kernel_thread_wait_queue_sequence(spec->queue);
    if (spec->sequence == 0u)
        return KERNEL_SYNC_CORRUPT;
    return KERNEL_SYNC_BLOCKED;
}

KernelSyncStatus kernel_sync_commit_wait(KernelSyncObject *object)
{
    uint32_t waiters;

    if (!valid_live_object(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    waiters = kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX || waiters == 0u)
        return KERNEL_SYNC_INVALID_STATE;
    ++pool_stats.blocked_waits;
    if (waiters > pool_stats.max_waiters)
        pool_stats.max_waiters = waiters;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_wait(KernelSyncObject *object,
                                  KernelThread *thread, uint64_t now,
                                  uint64_t deadline,
                                  uint32_t timeout_result)
{
    KernelThreadWaitSpec spec;
    KernelThreadStatus status;
    KernelSyncStatus prepare;

    if (thread == NULL || timeout_result == ASTRA_SYSCALL_OK)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    prepare = kernel_sync_prepare_wait(object, &spec);
    if (prepare != KERNEL_SYNC_BLOCKED)
        return prepare;
    status = kernel_thread_block_until(thread, spec.queue, spec.sequence,
                                       now, deadline, timeout_result);
    if (status == KERNEL_THREAD_DEADLINE_EXPIRED)
        return KERNEL_SYNC_TIMED_OUT;
    if (status != KERNEL_THREAD_OK)
        return status == KERNEL_THREAD_INVALID_ARGUMENT ||
                       status == KERNEL_THREAD_INVALID_STATE ||
                       status == KERNEL_THREAD_CONDITION_CHANGED ?
            KERNEL_SYNC_INVALID_STATE : KERNEL_SYNC_CORRUPT;
    if (kernel_sync_commit_wait(object) != KERNEL_SYNC_OK)
        return KERNEL_SYNC_CORRUPT;
    return KERNEL_SYNC_BLOCKED;
}

KernelSyncStatus kernel_sync_signal(KernelSyncObject *object,
                                    uint32_t release_count,
                                    uint32_t wake_result,
                                    uint32_t *woken_threads)
{
    uint32_t waiter_threads;
    uint32_t to_wake;
    uint32_t remainder;
    uint32_t woken = 0u;

    if (woken_threads == NULL || release_count == 0u ||
        wake_result != ASTRA_SYSCALL_OK ||
        !valid_object_pointer(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *woken_threads = 0u;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    ++pool_stats.signal_calls;

    if (object->type == KERNEL_SYNC_EVENT_MANUAL) {
        if (release_count != 1u)
            return KERNEL_SYNC_INVALID_ARGUMENT;
        object->count = 1u;
        if (kernel_thread_wake_all(&object->waiters, wake_result, &woken) !=
            KERNEL_THREAD_OK)
            return KERNEL_SYNC_CORRUPT;
    } else if (object->type == KERNEL_SYNC_EVENT_AUTO) {
        KernelThread *thread = NULL;
        KernelThreadStatus status;

        if (release_count != 1u)
            return KERNEL_SYNC_INVALID_ARGUMENT;
        status = kernel_thread_wake_one(&object->waiters, wake_result,
                                        &thread);
        if (status == KERNEL_THREAD_OK) {
            if (thread == NULL)
                return KERNEL_SYNC_CORRUPT;
            woken = 1u;
            object->count = 0u;
        } else if (status == KERNEL_THREAD_NO_RUNNABLE) {
            object->count = 1u;
        } else {
            return KERNEL_SYNC_CORRUPT;
        }
    } else if (object->type == KERNEL_SYNC_SEMAPHORE) {
        waiter_threads =
            kernel_thread_wait_queue_waiter_count(&object->waiters);
        if (waiter_threads == UINT32_MAX)
            return KERNEL_SYNC_CORRUPT;
        to_wake = release_count < waiter_threads ?
            release_count : waiter_threads;
        remainder = release_count - to_wake;
        if (remainder > object->maximum - object->count)
            return KERNEL_SYNC_COUNT_LIMIT;
        for (uint32_t index = 0u; index < to_wake; ++index) {
            KernelThread *thread = NULL;

            if (kernel_thread_wake_one(&object->waiters, wake_result,
                                       &thread) != KERNEL_THREAD_OK ||
                thread == NULL)
                return KERNEL_SYNC_CORRUPT;
            ++woken;
        }
        object->count += remainder;
    } else {
        return KERNEL_SYNC_INVALID_STATE;
    }
    pool_stats.signal_wakeups += woken;
    *woken_threads = woken;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_reset(KernelSyncObject *object)
{
    if (!valid_object_pointer(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    if (object->type != KERNEL_SYNC_EVENT_AUTO &&
        object->type != KERNEL_SYNC_EVENT_MANUAL)
        return KERNEL_SYNC_INVALID_STATE;
    object->count = 0u;
    ++pool_stats.reset_calls;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_timer_set(KernelSyncObject *object,
                                       uint64_t now, uint64_t deadline,
                                       uint32_t *woken_threads)
{
    KernelSyncStatus insert_status;
    uint32_t woken = 0u;
    uint16_t slot;

    if (woken_threads == NULL || !valid_object_pointer(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *woken_threads = 0u;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    if (object->type != KERNEL_SYNC_TIMER)
        return KERNEL_SYNC_INVALID_STATE;
    slot = timer_slot(object);
    if (!timer_remove(slot))
        return KERNEL_SYNC_CORRUPT;
    object->count = 0u;
    ++pool_stats.timer_arms;
    if (deadline <= now) {
        object->count = 1u;
        if (kernel_thread_wake_all(&object->waiters, ASTRA_SYSCALL_OK,
                                   &woken) != KERNEL_THREAD_OK)
            return KERNEL_SYNC_CORRUPT;
        ++pool_stats.timer_expirations;
        pool_stats.timer_wakeups += woken;
    } else {
        insert_status = timer_insert(slot, deadline);
        if (insert_status != KERNEL_SYNC_OK)
            return insert_status;
    }
    *woken_threads = woken;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_timer_cancel(KernelSyncObject *object,
                                          uint32_t wake_result,
                                          uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (woken_threads == NULL || wake_result == ASTRA_SYSCALL_OK ||
        !valid_object_pointer(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *woken_threads = 0u;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    if (object->type != KERNEL_SYNC_TIMER)
        return KERNEL_SYNC_INVALID_STATE;
    if (!timer_remove(timer_slot(object)))
        return KERNEL_SYNC_CORRUPT;
    object->count = 0u;
    if (kernel_thread_wake_all(&object->waiters, wake_result, &woken) !=
        KERNEL_THREAD_OK)
        return KERNEL_SYNC_CORRUPT;
    ++pool_stats.timer_cancellations;
    *woken_threads = woken;
    return KERNEL_SYNC_OK;
}

KernelSyncStatus kernel_sync_expire_timers(uint64_t now,
                                           uint32_t *expired_timers,
                                           uint32_t *woken_threads)
{
    uint32_t expired = 0u;
    uint32_t woken = 0u;

    if (expired_timers == NULL || woken_threads == NULL)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    while (timer_count != 0u) {
        uint16_t slot = timer_heap_get(0u);
        KernelSyncObject *object = object_at(slot);
        uint32_t object_woken = 0u;

        if (object == NULL || object->deadline > now)
            break;
        if (!valid_live_object(object) ||
            object->type != KERNEL_SYNC_TIMER || object->count != 0u ||
            !timer_remove(slot))
            return KERNEL_SYNC_CORRUPT;
        object->count = 1u;
        if (kernel_thread_wake_all(&object->waiters, ASTRA_SYSCALL_OK,
                                   &object_woken) != KERNEL_THREAD_OK)
            return KERNEL_SYNC_CORRUPT;
        ++expired;
        woken += object_woken;
    }
    pool_stats.timer_expirations += expired;
    pool_stats.timer_wakeups += woken;
    *expired_timers = expired;
    *woken_threads = woken;
    return KERNEL_SYNC_OK;
}

bool kernel_sync_next_timer_deadline(uint64_t *deadline)
{
    if (deadline == NULL || timer_count == 0u)
        return false;
    *deadline = object_at(timer_heap_get(0u))->deadline;
    return true;
}

KernelSyncStatus kernel_sync_owner_died(uint32_t owner,
                                       uint32_t wake_result,
                                       uint32_t *closed_objects,
                                       uint32_t *woken_threads)
{
    uint32_t closed = 0u;
    uint32_t woken = 0u;

    if (owner == 0u || wake_result == ASTRA_SYSCALL_OK ||
        closed_objects == NULL || woken_threads == NULL)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *closed_objects = 0u;
    *woken_threads = 0u;
    for (uint32_t slot = 0u; slot < object_backed_limit; ++slot) {
        uint32_t object_woken = 0u;
        KernelSyncStatus status;
        KernelSyncObject *object = object_at(slot);

        if (object->state != KERNEL_SYNC_LIVE || object->owner != owner)
            continue;
        status = close_object(object, wake_result, &object_woken);
        if (status != KERNEL_SYNC_OK)
            return status;
        ++closed;
        woken += object_woken;
    }
    if (closed != 0u)
        ++pool_stats.owner_deaths;
    *closed_objects = closed;
    *woken_threads = woken;
    return KERNEL_SYNC_OK;
}

uint32_t kernel_sync_terminal_result(const KernelSyncObject *object)
{
    return valid_object_pointer(object) &&
           object->state == KERNEL_SYNC_CLOSING ?
        object->close_result : 0u;
}

bool kernel_sync_snapshot(uint32_t slot, KernelSyncSnapshot *snapshot)
{
    const KernelSyncObject *object;
    uint32_t waiters;

    if (slot >= KERNEL_SYNC_OBJECT_MAX || snapshot == NULL)
        return false;
    object = object_at(slot);
    if (object == NULL) {
        kernel_bytes_clear(snapshot, sizeof(*snapshot));
        snapshot->state = KERNEL_SYNC_FREE;
        return true;
    }
    waiters = object->state == KERNEL_SYNC_FREE ? 0u :
        kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX || waiters > UINT16_MAX)
        return false;
    snapshot->generation = object->generation;
    snapshot->owner = object->owner;
    snapshot->count = object->count;
    snapshot->maximum = object->maximum;
    snapshot->close_result = object->close_result;
    if (object->timer_position != KERNEL_SYNC_TIMER_SLOT_NONE) {
        snapshot->deadline_high = (uint32_t)(object->deadline >> 32);
        snapshot->deadline_low = (uint32_t)object->deadline;
    } else {
        snapshot->deadline_high = 0u;
        snapshot->deadline_low = 0u;
    }
    snapshot->references = object->references;
    snapshot->waiters = (uint16_t)waiters;
    snapshot->type = object->type;
    snapshot->state = object->state;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    return true;
}

bool kernel_sync_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_sync_pool_valid(void)
{
    KernelAllocationStats allocations;
    KernelAllocationStats metadata;
    uint32_t armed = 0u;
    uint32_t closing = 0u;
    uint32_t futex_bytes = 0u;
    uint32_t futex_count = 0u;
    uint32_t heap_leaves = 0u;
    uint32_t live = 0u;
    uint32_t object_leaves = 0u;

    if (!kernel_sync_pool_healthy() ||
        object_backed_limit > KERNEL_SYNC_OBJECT_MAX ||
        active_object_count > object_backed_limit ||
        timer_count > active_object_count)
        return false;
    while (object_leaves < SYNC_LEAF_COUNT &&
           object_directory[object_leaves] != NULL)
        ++object_leaves;
    for (uint32_t leaf = object_leaves; leaf < SYNC_LEAF_COUNT; ++leaf)
        if (object_directory[leaf] != NULL)
            return false;
    uint32_t expected_limit = object_leaves * SYNC_LEAF_ENTRIES;
    if (expected_limit > KERNEL_SYNC_OBJECT_MAX)
        expected_limit = KERNEL_SYNC_OBJECT_MAX;
    if (object_backed_limit != expected_limit)
        return false;
    while (heap_leaves < TIMER_HEAP_LEAF_COUNT &&
           timer_heap_directory[heap_leaves] != NULL)
        ++heap_leaves;
    for (uint32_t leaf = heap_leaves; leaf < TIMER_HEAP_LEAF_COUNT; ++leaf)
        if (timer_heap_directory[leaf] != NULL)
            return false;
    for (const KernelFutexSlot *futex = futex_slots; futex != NULL;
         futex = futex->next) {
        uint32_t waiters = kernel_thread_wait_queue_count(&futex->waiters);

        if (waiters == UINT32_MAX || waiters == 0u)
            return false;
        if (futex->process_id == 0u || futex->resource_owner == 0u ||
            futex->address == 0u ||
            (futex->address & (sizeof(uint32_t) - 1u)) != 0u) {
            return false;
        }
        ++futex_count;
#if defined(KERNEL_SYNC_STANDALONE_HOST)
        futex_bytes += sizeof(*futex);
#else
        futex_bytes += KERNEL_PAGE_SIZE;
#endif
    }
    for (uint32_t position = 0u; position < timer_count; ++position) {
        uint16_t slot = timer_heap_get(position);
        const KernelSyncObject *object = object_at(slot);

        if (object == NULL || object->timer_position != position ||
            object->deadline == 0u || object->state != KERNEL_SYNC_LIVE ||
            object->type != KERNEL_SYNC_TIMER || object->count != 0u)
            return false;
        if (position != 0u &&
            timer_less(slot, timer_heap_get((position - 1u) / 2u)))
            return false;
    }
    for (uint32_t slot = 0u; slot < object_backed_limit; ++slot) {
        const KernelSyncObject *object = object_at(slot);

        if (object == NULL)
            return false;
        uint32_t waiters = object->state == KERNEL_SYNC_FREE ? 0u :
            kernel_thread_wait_queue_count(&object->waiters);
        bool object_armed =
            object->timer_position != KERNEL_SYNC_TIMER_SLOT_NONE;
        bool claimed = object->state != KERNEL_SYNC_FREE;

        if (object->slot != slot || object->generation == 0u ||
            (object_armed &&
             (object->timer_position >= timer_count ||
              timer_heap_get(object->timer_position) != slot)))
            return false;

        if (object->state == KERNEL_SYNC_FREE) {
            if (claimed || object->owner != 0u || object->count != 0u ||
                object->maximum != 0u || object->close_result != 0u ||
                object->references != 0u || object->type != KERNEL_SYNC_NONE ||
                object_armed || object->deadline != 0u)
                return false;
            continue;
        }
        if (!claimed)
            return false;
        if (waiters == UINT32_MAX ||
            !valid_type(object->type) || object->owner == 0u ||
            object->generation == 0u || object->references == 0u)
            return false;
        if (object->state == KERNEL_SYNC_LIVE) {
            ++live;
            if (!valid_live_object(object) ||
                (object->type == KERNEL_SYNC_TIMER && object_armed &&
                 object->count != 0u) ||
                (object->type != KERNEL_SYNC_TIMER &&
                 (object_armed || object->deadline != 0u)))
                return false;
        } else if (object->state == KERNEL_SYNC_CLOSING) {
            ++closing;
            if (object->close_result == 0u || waiters != 0u ||
                object_armed || object->deadline != 0u)
                return false;
        } else {
            return false;
        }
        if (object_armed)
            ++armed;
    }
    uint32_t metadata_frames =
        object_leaves * SYNC_LEAF_FRAMES + heap_leaves;
    return armed == timer_count &&
           live + closing == active_object_count &&
           kernel_allocation_site_stats(
               KERNEL_ALLOCATION_SITE_SYNC_OBJECT, &allocations) &&
           allocations.current_units == active_object_count + futex_count &&
           allocations.current_bytes ==
               active_object_count * sizeof(KernelSyncObject) + futex_bytes &&
           kernel_allocation_site_stats(
               KERNEL_ALLOCATION_SITE_SYNC_METADATA, &metadata) &&
           metadata.current_units == metadata_frames &&
           metadata.current_bytes == metadata_frames * KERNEL_PAGE_SIZE;
}

bool kernel_sync_pool_stats(KernelSyncPoolStats *stats)
{
    if (stats == NULL || !kernel_sync_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    stats->live_objects = 0u;
    stats->closing_objects = 0u;
    stats->armed_timers = timer_count;
    for (uint32_t slot = 0u; slot < object_backed_limit; ++slot) {
        const KernelSyncObject *object = object_at(slot);

        if (object->state == KERNEL_SYNC_LIVE)
            ++stats->live_objects;
        else if (object->state == KERNEL_SYNC_CLOSING)
            ++stats->closing_objects;
    }
    return true;
}
