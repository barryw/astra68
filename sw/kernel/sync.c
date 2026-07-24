#include "sync.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"

#include <stddef.h>
#include <stdint.h>

struct KernelSyncObject {
    KernelThreadWaitQueue waiters;
    uint32_t owner;
    uint32_t generation;
    uint32_t count;
    uint32_t maximum;
    uint32_t close_result;
    uint16_t references;
    uint8_t type;
    uint8_t state;
};

static KernelSyncObject objects[KERNEL_SYNC_OBJECT_MAX];
static KernelSyncPoolStats pool_stats;
static uint8_t pool_corrupt;

_Static_assert(sizeof(KernelSyncObject) == 36u,
               "synchronization object memory budget changed");

static bool valid_type(uint8_t type)
{
    return type == KERNEL_SYNC_EVENT_AUTO ||
           type == KERNEL_SYNC_EVENT_MANUAL ||
           type == KERNEL_SYNC_SEMAPHORE;
}

static bool valid_object_pointer(const KernelSyncObject *object)
{
    uintptr_t address = (uintptr_t)object;
    uintptr_t first = (uintptr_t)&objects[0];
    uintptr_t limit = (uintptr_t)&objects[KERNEL_SYNC_OBJECT_MAX];

    return object != NULL && address >= first && address < limit &&
           (address - first) % sizeof(objects[0]) == 0u;
}

static bool valid_live_object(const KernelSyncObject *object)
{
    return valid_object_pointer(object) &&
           object->state == KERNEL_SYNC_LIVE &&
           valid_type(object->type) && object->owner != 0u &&
           object->generation != 0u && object->references != 0u &&
           object->close_result == 0u &&
           kernel_thread_wait_queue_count(&object->waiters) <=
               KERNEL_SYNC_WAITER_MAX &&
           ((object->type == KERNEL_SYNC_SEMAPHORE &&
             object->maximum != 0u &&
             object->maximum <= KERNEL_SYNC_SEMAPHORE_COUNT_MAX &&
             object->count <= object->maximum) ||
            (object->type != KERNEL_SYNC_SEMAPHORE &&
             object->maximum == 1u && object->count <= 1u));
}

static uint32_t owner_object_count(uint32_t owner)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        if (objects[slot].state != KERNEL_SYNC_FREE &&
            objects[slot].owner == owner)
            ++count;
    }
    return count;
}

static void update_live_maximum(void)
{
    uint32_t live = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        if (objects[slot].state == KERNEL_SYNC_LIVE)
            ++live;
    }
    if (live > pool_stats.max_live_objects)
        pool_stats.max_live_objects = live;
}

static KernelSyncStatus allocate_object(uint32_t owner, uint8_t type,
                                        KernelSyncObject **object)
{
    if (owner == 0u || !valid_type(type) || object == NULL)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    *object = NULL;
    if (owner_object_count(owner) >= KERNEL_SYNC_OWNER_MAX) {
        ++pool_stats.quota_failures;
        return KERNEL_SYNC_QUOTA_EXCEEDED;
    }
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        KernelSyncObject *candidate = &objects[slot];
        uint32_t generation;

        if (candidate->state != KERNEL_SYNC_FREE)
            continue;
        generation = kernel_generation_next(candidate->generation);
        kernel_bytes_clear(candidate, sizeof(*candidate));
        kernel_thread_wait_queue_init(&candidate->waiters);
        candidate->owner = owner;
        candidate->generation = generation;
        candidate->references = 1u;
        candidate->type = type;
        candidate->state = KERNEL_SYNC_LIVE;
        *object = candidate;
        update_live_maximum();
        return KERNEL_SYNC_OK;
    }
    ++pool_stats.allocation_failures;
    return KERNEL_SYNC_NO_SLOT;
}

static void free_object(KernelSyncObject *object)
{
    uint32_t generation;

    if (!valid_object_pointer(object) || object->references != 0u ||
        kernel_thread_wait_queue_count(&object->waiters) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    generation = object->generation;
    kernel_bytes_clear(object, sizeof(*object));
    object->generation = generation;
    object->state = KERNEL_SYNC_FREE;
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

void kernel_sync_pool_init(void)
{
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        uint32_t generation = objects[slot].generation;

        kernel_bytes_clear(&objects[slot], sizeof(objects[slot]));
        objects[slot].generation = generation;
        objects[slot].state = KERNEL_SYNC_FREE;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
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

KernelSyncStatus kernel_sync_retain(KernelSyncObject *object)
{
    if (!valid_live_object(object))
        return KERNEL_SYNC_INVALID_ARGUMENT;
    if (object->references == KERNEL_SYNC_REFERENCE_MAX)
        return KERNEL_SYNC_COUNT_LIMIT;
    ++object->references;
    return KERNEL_SYNC_OK;
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

KernelSyncStatus kernel_sync_wait(KernelSyncObject *object,
                                  KernelThread *thread, uint64_t now,
                                  uint64_t deadline,
                                  uint32_t timeout_result)
{
    KernelThreadStatus status;
    uint32_t sequence;
    uint32_t waiters;

    if (!valid_object_pointer(object) || thread == NULL ||
        timeout_result == ASTRA_SYSCALL_OK)
        return KERNEL_SYNC_INVALID_ARGUMENT;
    if (object->state == KERNEL_SYNC_CLOSING)
        return KERNEL_SYNC_CLOSED;
    if (!valid_live_object(object))
        return KERNEL_SYNC_CORRUPT;
    ++pool_stats.wait_calls;

    if (object->count != 0u) {
        if (object->type != KERNEL_SYNC_EVENT_MANUAL)
            --object->count;
        ++pool_stats.immediate_waits;
        return KERNEL_SYNC_OK;
    }
    waiters = kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX)
        return KERNEL_SYNC_CORRUPT;
    if (waiters >= KERNEL_SYNC_WAITER_MAX)
        return KERNEL_SYNC_WAITER_LIMIT;
    sequence = kernel_thread_wait_queue_sequence(&object->waiters);
    if (sequence == 0u)
        return KERNEL_SYNC_CORRUPT;
    status = kernel_thread_block_until(thread, &object->waiters, sequence,
                                       now, deadline, timeout_result);
    if (status == KERNEL_THREAD_DEADLINE_EXPIRED)
        return KERNEL_SYNC_TIMED_OUT;
    if (status != KERNEL_THREAD_OK)
        return status == KERNEL_THREAD_INVALID_ARGUMENT ||
                       status == KERNEL_THREAD_INVALID_STATE ||
                       status == KERNEL_THREAD_CONDITION_CHANGED ?
            KERNEL_SYNC_INVALID_STATE : KERNEL_SYNC_CORRUPT;
    ++pool_stats.blocked_waits;
    ++waiters;
    if (waiters > pool_stats.max_waiters)
        pool_stats.max_waiters = waiters;
    return KERNEL_SYNC_BLOCKED;
}

KernelSyncStatus kernel_sync_signal(KernelSyncObject *object,
                                    uint32_t release_count,
                                    uint32_t wake_result,
                                    uint32_t *woken_threads)
{
    uint32_t waiters;
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
    waiters = kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX)
        return KERNEL_SYNC_CORRUPT;

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
    } else {
        to_wake = release_count < waiters ? release_count : waiters;
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
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        uint32_t object_woken = 0u;
        KernelSyncStatus status;

        if (objects[slot].state != KERNEL_SYNC_LIVE ||
            objects[slot].owner != owner)
            continue;
        status = close_object(&objects[slot], wake_result, &object_woken);
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
    object = &objects[slot];
    waiters = object->state == KERNEL_SYNC_FREE ? 0u :
        kernel_thread_wait_queue_count(&object->waiters);
    if (waiters == UINT32_MAX || waiters > UINT16_MAX)
        return false;
    snapshot->generation = object->generation;
    snapshot->owner = object->owner;
    snapshot->count = object->count;
    snapshot->maximum = object->maximum;
    snapshot->close_result = object->close_result;
    snapshot->references = object->references;
    snapshot->waiters = (uint16_t)waiters;
    snapshot->type = object->type;
    snapshot->state = object->state;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    return true;
}

bool kernel_sync_pool_valid(void)
{
    if (pool_corrupt != 0u)
        return false;
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        const KernelSyncObject *object = &objects[slot];
        uint32_t waiters = object->state == KERNEL_SYNC_FREE ? 0u :
            kernel_thread_wait_queue_count(&object->waiters);

        if (object->state == KERNEL_SYNC_FREE) {
            if (object->owner != 0u || object->count != 0u ||
                object->maximum != 0u || object->close_result != 0u ||
                object->references != 0u || object->type != KERNEL_SYNC_NONE)
                return false;
            continue;
        }
        if (waiters == UINT32_MAX || waiters > KERNEL_SYNC_WAITER_MAX ||
            !valid_type(object->type) || object->owner == 0u ||
            object->generation == 0u || object->references == 0u)
            return false;
        if (object->state == KERNEL_SYNC_LIVE) {
            if (!valid_live_object(object))
                return false;
        } else if (object->state == KERNEL_SYNC_CLOSING) {
            if (object->close_result == 0u || waiters != 0u)
                return false;
        } else {
            return false;
        }
    }
    return true;
}

bool kernel_sync_pool_stats(KernelSyncPoolStats *stats)
{
    if (stats == NULL || !kernel_sync_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    stats->live_objects = 0u;
    stats->closing_objects = 0u;
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot) {
        if (objects[slot].state == KERNEL_SYNC_LIVE)
            ++stats->live_objects;
        else if (objects[slot].state == KERNEL_SYNC_CLOSING)
            ++stats->closing_objects;
    }
    return true;
}
