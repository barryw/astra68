#include "thread.h"

#include "bytes.h"
#include "generation.h"
#include "performance.h"

#include <stddef.h>

#define THREAD_ID_PREFIX 0x20000000u
#define THREAD_STACK_CANARY 0x5354414bu
#define THREAD_STACK_POISON 0xa5a5a5a5u

#if defined(__m68k__)
extern uint8_t _kernel_thread_stacks_start[];
#else
static _Alignas(4) uint32_t
    host_stack_words[KERNEL_THREAD_MAX]
                    [KERNEL_THREAD_SUPERVISOR_STACK_SIZE / sizeof(uint32_t)];
#endif

static KernelThread threads[KERNEL_THREAD_MAX];
static uint16_t ready_head[KERNEL_THREAD_PRIORITY_LEVELS];
static uint16_t ready_tail[KERNEL_THREAD_PRIORITY_LEVELS];
static uint32_t ready_bitmap;
static uint32_t ready_count;
static KernelThreadPoolStats pool_stats;

static bool valid_thread(const KernelThread *thread);

_Static_assert(offsetof(KernelThread, context) == 0u,
               "thread context must remain the first field");
_Static_assert(offsetof(KernelThread, kernel_stack_top) ==
                   KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET,
               "assembly thread stack offset changed");
_Static_assert(sizeof(KernelThread) <= 160u,
               "thread record exceeds the stable memory budget");
_Static_assert(KERNEL_THREAD_STACK_STRIDE >= KERNEL_THREAD_STACK_SIZE * 2u,
               "thread stacks require an unmapped guard interval");
_Static_assert(KERNEL_THREAD_SUPERVISOR_STACK_SIZE % sizeof(uint32_t) == 0u,
               "supervisor stack must contain whole longwords");

static uint32_t kernel_stack_guard_address(uint16_t slot)
{
#if defined(__m68k__)
    uint32_t arena = (uint32_t)(uintptr_t)_kernel_thread_stacks_start;
#else
    uint32_t arena = KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE;
#endif

    return arena + (uint32_t)slot * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE;
}

static uint32_t *kernel_stack_words(uint16_t slot)
{
#if defined(__m68k__)
    return (uint32_t *)(uintptr_t)(
        kernel_stack_guard_address(slot) +
        KERNEL_THREAD_SUPERVISOR_GUARD_SIZE);
#else
    return host_stack_words[slot];
#endif
}

static void initialize_kernel_stack(KernelThread *thread)
{
    uint32_t *words = kernel_stack_words(thread->slot);
    uint32_t word_count = KERNEL_THREAD_SUPERVISOR_STACK_SIZE /
                          sizeof(uint32_t);

    for (uint32_t index = 0u; index < word_count; ++index)
        words[index] = THREAD_STACK_POISON;
    words[0] = THREAD_STACK_CANARY;
    thread->kernel_stack_base =
        kernel_stack_guard_address(thread->slot) +
        KERNEL_THREAD_SUPERVISOR_GUARD_SIZE;
    thread->kernel_stack_top = thread->kernel_stack_base +
                               KERNEL_THREAD_SUPERVISOR_STACK_SIZE;
    thread->kernel_stack_low_water = thread->kernel_stack_top;
    thread->kernel_stack_entries = 0u;
}

static bool kernel_stack_valid(const KernelThread *thread)
{
    const uint32_t *words;

    if (!valid_thread(thread))
        return false;
    words = kernel_stack_words(thread->slot);
    return words[0] == THREAD_STACK_CANARY &&
           thread->kernel_stack_base ==
               kernel_stack_guard_address(thread->slot) +
                   KERNEL_THREAD_SUPERVISOR_GUARD_SIZE &&
           thread->kernel_stack_top ==
               thread->kernel_stack_base +
                   KERNEL_THREAD_SUPERVISOR_STACK_SIZE &&
           thread->kernel_stack_low_water >= thread->kernel_stack_base &&
           thread->kernel_stack_low_water <= thread->kernel_stack_top;
}

static uint32_t kernel_stack_observed_used(const KernelThread *thread)
{
    return thread->kernel_stack_top - thread->kernel_stack_low_water;
}

static uint32_t kernel_stack_poison_used(const KernelThread *thread)
{
    const uint32_t *words = kernel_stack_words(thread->slot);
    uint32_t word_count = KERNEL_THREAD_SUPERVISOR_STACK_SIZE /
                          sizeof(uint32_t);
    uint32_t first_used = word_count;

    for (uint32_t index = 1u; index < word_count; ++index) {
        if (pool_stats.kernel_stack_scan_words != UINT32_MAX)
            ++pool_stats.kernel_stack_scan_words;
        if (words[index] != THREAD_STACK_POISON) {
            first_used = index;
            break;
        }
    }
    uint32_t poisoned_used = first_used == word_count ? 0u :
        KERNEL_THREAD_SUPERVISOR_STACK_SIZE -
            first_used * sizeof(uint32_t);
    uint32_t observed_used = kernel_stack_observed_used(thread);

    return poisoned_used > observed_used ? poisoned_used : observed_used;
}

static bool valid_thread(const KernelThread *thread)
{
    return thread != NULL && thread->slot < KERNEL_THREAD_MAX &&
           thread == &threads[thread->slot] && thread->occupied != 0u;
}

static uint8_t highest_ready_priority(uint32_t bitmap)
{
#if defined(__m68k__)
    uint32_t first_one;

    __asm__ volatile ("bfffo %1{#0:#0},%0"
                      : "=d" (first_one)
                      : "d" (bitmap));
    return (uint8_t)(31u - first_one);
#else
    uint8_t priority = 0u;

    if ((bitmap & 0xffff0000u) != 0u) {
        bitmap >>= 16;
        priority = 16u;
    }
    if ((bitmap & 0x0000ff00u) != 0u) {
        bitmap >>= 8;
        priority = (uint8_t)(priority + 8u);
    }
    if ((bitmap & 0x000000f0u) != 0u) {
        bitmap >>= 4;
        priority = (uint8_t)(priority + 4u);
    }
    if ((bitmap & 0x0000000cu) != 0u) {
        bitmap >>= 2;
        priority = (uint8_t)(priority + 2u);
    }
    if ((bitmap & 0x00000002u) != 0u)
        ++priority;
    return priority;
#endif
}

static KernelThreadStatus enqueue_ready(KernelThread *thread)
{
    uint8_t priority;
    uint16_t tail;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_READY ||
        thread->effective_priority >= KERNEL_THREAD_PRIORITY_LEVELS ||
        thread->ready_previous != KERNEL_THREAD_SLOT_NONE ||
        thread->ready_next != KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_INVALID_STATE;

    priority = thread->effective_priority;
    tail = ready_tail[priority];
    if (tail == KERNEL_THREAD_SLOT_NONE) {
        if (ready_head[priority] != KERNEL_THREAD_SLOT_NONE)
            return KERNEL_THREAD_CORRUPT;
        ready_head[priority] = thread->slot;
    } else {
        KernelThread *previous = &threads[tail];

        if (!valid_thread(previous) ||
            previous->state != KERNEL_THREAD_READY ||
            previous->ready_next != KERNEL_THREAD_SLOT_NONE)
            return KERNEL_THREAD_CORRUPT;
        previous->ready_next = thread->slot;
        thread->ready_previous = tail;
    }
    ready_tail[priority] = thread->slot;
    ready_bitmap |= 1u << priority;
    ++ready_count;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus remove_ready(KernelThread *thread)
{
    uint8_t priority;
    uint16_t previous;
    uint16_t next;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_READY ||
        thread->effective_priority >= KERNEL_THREAD_PRIORITY_LEVELS)
        return KERNEL_THREAD_INVALID_STATE;
    priority = thread->effective_priority;
    previous = thread->ready_previous;
    next = thread->ready_next;

    if (previous == KERNEL_THREAD_SLOT_NONE) {
        if (ready_head[priority] != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        ready_head[priority] = next;
    } else {
        if (previous >= KERNEL_THREAD_MAX ||
            threads[previous].ready_next != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        threads[previous].ready_next = next;
    }
    if (next == KERNEL_THREAD_SLOT_NONE) {
        if (ready_tail[priority] != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        ready_tail[priority] = previous;
    } else {
        if (next >= KERNEL_THREAD_MAX ||
            threads[next].ready_previous != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        threads[next].ready_previous = previous;
    }
    thread->ready_previous = KERNEL_THREAD_SLOT_NONE;
    thread->ready_next = KERNEL_THREAD_SLOT_NONE;
    if (ready_count == 0u)
        return KERNEL_THREAD_CORRUPT;
    --ready_count;
    if (ready_head[priority] == KERNEL_THREAD_SLOT_NONE) {
        if (ready_tail[priority] != KERNEL_THREAD_SLOT_NONE)
            return KERNEL_THREAD_CORRUPT;
        ready_bitmap &= ~(1u << priority);
    }
    return KERNEL_THREAD_OK;
}

static bool valid_wait_queue(const KernelThreadWaitQueue *queue)
{
    if (queue == NULL || queue->count > KERNEL_THREAD_MAX)
        return false;
    if (queue->count == 0u)
        return queue->head == KERNEL_THREAD_SLOT_NONE &&
               queue->tail == KERNEL_THREAD_SLOT_NONE;
    return queue->head < KERNEL_THREAD_MAX &&
           queue->tail < KERNEL_THREAD_MAX;
}

static KernelThreadStatus enqueue_wait(KernelThread *thread,
                                       KernelThreadWaitQueue *queue)
{
    uint16_t previous = KERNEL_THREAD_SLOT_NONE;
    uint16_t next;

    if (!valid_thread(thread) || !valid_wait_queue(queue) ||
        thread->state != KERNEL_THREAD_BLOCKED ||
        thread->wait_queue != NULL ||
        thread->wait_previous != KERNEL_THREAD_SLOT_NONE ||
        thread->wait_next != KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_INVALID_STATE;

    next = queue->head;
    while (next != KERNEL_THREAD_SLOT_NONE) {
        KernelThread *queued;

        if (next >= KERNEL_THREAD_MAX)
            return KERNEL_THREAD_CORRUPT;
        queued = &threads[next];
        if (!valid_thread(queued) ||
            queued->state != KERNEL_THREAD_BLOCKED ||
            queued->wait_queue != queue ||
            queued->wait_previous != previous)
            return KERNEL_THREAD_CORRUPT;
        if (queued->effective_priority < thread->effective_priority)
            break;
        previous = next;
        next = queued->wait_next;
    }

    thread->wait_queue = queue;
    thread->wait_previous = previous;
    thread->wait_next = next;
    if (previous == KERNEL_THREAD_SLOT_NONE)
        queue->head = thread->slot;
    else
        threads[previous].wait_next = thread->slot;
    if (next == KERNEL_THREAD_SLOT_NONE)
        queue->tail = thread->slot;
    else
        threads[next].wait_previous = thread->slot;
    ++queue->count;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus remove_wait(KernelThread *thread)
{
    KernelThreadWaitQueue *queue;
    uint16_t previous;
    uint16_t next;

    if (!valid_thread(thread) ||
        thread->state != KERNEL_THREAD_BLOCKED ||
        thread->wait_queue == NULL)
        return KERNEL_THREAD_INVALID_STATE;
    queue = thread->wait_queue;
    if (!valid_wait_queue(queue) || queue->count == 0u)
        return KERNEL_THREAD_CORRUPT;
    previous = thread->wait_previous;
    next = thread->wait_next;

    if (previous == KERNEL_THREAD_SLOT_NONE) {
        if (queue->head != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        queue->head = next;
    } else {
        if (previous >= KERNEL_THREAD_MAX ||
            threads[previous].wait_next != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        threads[previous].wait_next = next;
    }
    if (next == KERNEL_THREAD_SLOT_NONE) {
        if (queue->tail != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        queue->tail = previous;
    } else {
        if (next >= KERNEL_THREAD_MAX ||
            threads[next].wait_previous != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        threads[next].wait_previous = previous;
    }
    --queue->count;
    if (queue->count == 0u) {
        if (queue->head != KERNEL_THREAD_SLOT_NONE ||
            queue->tail != KERNEL_THREAD_SLOT_NONE)
            return KERNEL_THREAD_CORRUPT;
    }
    thread->wait_queue = NULL;
    thread->wait_previous = KERNEL_THREAD_SLOT_NONE;
    thread->wait_next = KERNEL_THREAD_SLOT_NONE;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus wake_waiter(KernelThread *thread, uint32_t result)
{
    KernelThreadStatus status = remove_wait(thread);

    if (status != KERNEL_THREAD_OK)
        return status;
    thread->context.data[0] = result;
    thread->state = KERNEL_THREAD_READY;
    status = enqueue_ready(thread);
    return status == KERNEL_THREAD_OK ? KERNEL_THREAD_OK :
                                       KERNEL_THREAD_CORRUPT;
}

void kernel_thread_pool_init(void)
{
    for (uint32_t index = 0u; index < KERNEL_THREAD_MAX; ++index)
        kernel_bytes_clear(&threads[index], sizeof(threads[index]));
    for (uint32_t priority = 0u;
         priority < KERNEL_THREAD_PRIORITY_LEVELS; ++priority) {
        ready_head[priority] = KERNEL_THREAD_SLOT_NONE;
        ready_tail[priority] = KERNEL_THREAD_SLOT_NONE;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    ready_bitmap = 0u;
    ready_count = 0u;
}

KernelThreadStatus kernel_thread_allocate(uint16_t process_slot,
                                          uint32_t process_id,
                                          uint16_t stack_slot,
                                          uint32_t program_counter,
                                          uint32_t user_stack,
                                          uint32_t initial_argument,
                                          uint8_t priority,
                                          KernelThread **thread)
{
    KernelThread *candidate = NULL;
    uint32_t generation;

    if (process_id == 0u || program_counter == 0u ||
        user_stack < KERNEL_THREAD_STACK_SIZE ||
        (user_stack & 3u) != 0u ||
        priority >= KERNEL_THREAD_PRIORITY_LEVELS || thread == NULL)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if (threads[slot].occupied == 0u) {
            candidate = &threads[slot];
            break;
        }
    }
    if (candidate == NULL)
        return KERNEL_THREAD_NO_SLOT;

    generation = kernel_generation_next(candidate->generation);
    kernel_bytes_clear(candidate, sizeof(*candidate));
    candidate->generation = generation;
    candidate->slot = (uint16_t)(candidate - threads);
    candidate->id = THREAD_ID_PREFIX |
                    ((generation & 0x000fffffu) << 4) |
                    (uint32_t)candidate->slot;
    candidate->process_id = process_id;
    candidate->process_slot = process_slot;
    candidate->stack_slot = stack_slot;
    candidate->user_stack_top = user_stack;
    candidate->user_stack_base = user_stack - KERNEL_THREAD_STACK_SIZE;
    candidate->ready_previous = KERNEL_THREAD_SLOT_NONE;
    candidate->ready_next = KERNEL_THREAD_SLOT_NONE;
    candidate->wait_previous = KERNEL_THREAD_SLOT_NONE;
    candidate->wait_next = KERNEL_THREAD_SLOT_NONE;
    candidate->state = KERNEL_THREAD_CREATED;
    candidate->base_priority = priority;
    candidate->effective_priority = priority;
    candidate->occupied = 1u;
    initialize_kernel_stack(candidate);
    kernel_context_initialize(&candidate->context, program_counter,
                              user_stack);
    candidate->context.data[2] = initial_argument;
    if (!kernel_context_valid(&candidate->context)) {
        candidate->state = KERNEL_THREAD_DEAD;
        candidate->occupied = 0u;
        return KERNEL_THREAD_CORRUPT;
    }
    *thread = candidate;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_publish(KernelThread *thread)
{
    KernelThreadStatus status;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED)
        return KERNEL_THREAD_INVALID_STATE;
    thread->state = KERNEL_THREAD_READY;
    status = enqueue_ready(thread);
    if (status != KERNEL_THREAD_OK) {
        thread->state = KERNEL_THREAD_CREATED;
        return status;
    }
    ++pool_stats.created_threads;
    ++pool_stats.live_threads;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_abort(KernelThread *thread)
{
    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED)
        return KERNEL_THREAD_INVALID_STATE;
    initialize_kernel_stack(thread);
    thread->state = KERNEL_THREAD_DEAD;
    thread->occupied = 0u;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_make_ready(KernelThread *thread)
{
    KernelThreadStatus status;
    uint8_t previous_state;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_RUNNING)
        return KERNEL_THREAD_INVALID_STATE;
    previous_state = thread->state;
    thread->state = KERNEL_THREAD_READY;
    status = enqueue_ready(thread);
    if (status != KERNEL_THREAD_OK)
        thread->state = previous_state;
    return status;
}

static __attribute__((noinline))
KernelThreadStatus take_next_fast(KernelThread **thread)
{
    KernelThread *next;
    KernelThreadStatus status;
    uint8_t priority;
    uint16_t slot;

    if (thread == NULL)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    if (ready_bitmap == 0u)
        return ready_count == 0u ? KERNEL_THREAD_NO_RUNNABLE :
                                   KERNEL_THREAD_CORRUPT;
    priority = highest_ready_priority(ready_bitmap);
    slot = ready_head[priority];
    if (slot >= KERNEL_THREAD_MAX)
        return KERNEL_THREAD_CORRUPT;
    next = &threads[slot];
    status = remove_ready(next);
    if (status != KERNEL_THREAD_OK)
        return status;
    next->state = KERNEL_THREAD_RUNNING;
    *thread = next;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus take_next_profiled(KernelThread **thread)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_SCHEDULER_PICK);
    status = take_next_fast(thread);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_take_next(KernelThread **thread)
{
    if (kernel_performance_sampling_enabled == 0u)
        return take_next_fast(thread);
    return take_next_profiled(thread);
}

void kernel_thread_wait_queue_init(KernelThreadWaitQueue *queue)
{
    if (queue == NULL)
        return;
    queue->sequence = 1u;
    queue->head = KERNEL_THREAD_SLOT_NONE;
    queue->tail = KERNEL_THREAD_SLOT_NONE;
    queue->count = 0u;
    queue->reserved = 0u;
}

uint32_t kernel_thread_wait_queue_sequence(
    const KernelThreadWaitQueue *queue)
{
    return valid_wait_queue(queue) ? queue->sequence : 0u;
}

uint32_t kernel_thread_wait_queue_count(const KernelThreadWaitQueue *queue)
{
    return valid_wait_queue(queue) ? queue->count : UINT32_MAX;
}

static __attribute__((noinline))
KernelThreadStatus block_fast(KernelThread *thread,
                              KernelThreadWaitQueue *queue,
                              uint32_t expected_sequence)
{
    KernelThreadStatus status;

    if (!valid_thread(thread) || !valid_wait_queue(queue) ||
        thread->state != KERNEL_THREAD_RUNNING)
        return KERNEL_THREAD_INVALID_STATE;
    if (queue->sequence != expected_sequence)
        return KERNEL_THREAD_CONDITION_CHANGED;
    thread->state = KERNEL_THREAD_BLOCKED;
    thread->wait_sequence = expected_sequence;
    status = enqueue_wait(thread, queue);
    if (status != KERNEL_THREAD_OK) {
        thread->state = KERNEL_THREAD_RUNNING;
        thread->wait_sequence = 0u;
    }
    return status;
}

static __attribute__((noinline))
KernelThreadStatus block_profiled(KernelThread *thread,
                                  KernelThreadWaitQueue *queue,
                                  uint32_t expected_sequence)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_WAIT_BLOCK);
    status = block_fast(thread, queue, expected_sequence);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_block(KernelThread *thread,
                                       KernelThreadWaitQueue *queue,
                                       uint32_t expected_sequence)
{
    if (kernel_performance_sampling_enabled == 0u)
        return block_fast(thread, queue, expected_sequence);
    return block_profiled(thread, queue, expected_sequence);
}

static __attribute__((noinline))
KernelThreadStatus wake_one_fast(KernelThreadWaitQueue *queue,
                                 uint32_t result,
                                 KernelThread **thread)
{
    KernelThread *waiter;

    if (thread == NULL || !valid_wait_queue(queue))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    queue->sequence = kernel_generation_next(queue->sequence);
    if (queue->count == 0u)
        return KERNEL_THREAD_NO_RUNNABLE;
    waiter = &threads[queue->head];
    if (wake_waiter(waiter, result) != KERNEL_THREAD_OK)
        return KERNEL_THREAD_CORRUPT;
    *thread = waiter;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus wake_one_profiled(KernelThreadWaitQueue *queue,
                                     uint32_t result,
                                     KernelThread **thread)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_WAKE);
    status = wake_one_fast(queue, result, thread);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_wake_one(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          KernelThread **thread)
{
    if (kernel_performance_sampling_enabled == 0u)
        return wake_one_fast(queue, result, thread);
    return wake_one_profiled(queue, result, thread);
}

static __attribute__((noinline))
KernelThreadStatus wake_all_fast(KernelThreadWaitQueue *queue,
                                 uint32_t result,
                                 uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (!valid_wait_queue(queue))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    queue->sequence = kernel_generation_next(queue->sequence);
    while (queue->count != 0u) {
        KernelThread *waiter = &threads[queue->head];

        if (wake_waiter(waiter, result) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        ++woken;
    }
    if (woken_threads != NULL)
        *woken_threads = woken;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus wake_all_profiled(KernelThreadWaitQueue *queue,
                                     uint32_t result,
                                     uint32_t *woken_threads)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_WAKE);
    status = wake_all_fast(queue, result, woken_threads);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_wake_all(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          uint32_t *woken_threads)
{
    if (kernel_performance_sampling_enabled == 0u)
        return wake_all_fast(queue, result, woken_threads);
    return wake_all_profiled(queue, result, woken_threads);
}

KernelThreadStatus kernel_thread_retire_process(uint16_t process_slot,
                                                uint32_t *retired_threads)
{
    uint32_t retired = 0u;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *thread = &threads[slot];

        if (thread->occupied == 0u ||
            thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        if (thread->state == KERNEL_THREAD_READY) {
            KernelThreadStatus status = remove_ready(thread);

            if (status != KERNEL_THREAD_OK)
                return status;
        } else if (thread->state == KERNEL_THREAD_BLOCKED) {
            if (remove_wait(thread) != KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
        } else if (thread->state != KERNEL_THREAD_CREATED &&
                   thread->state != KERNEL_THREAD_RUNNING) {
            return KERNEL_THREAD_CORRUPT;
        }
        thread->state = KERNEL_THREAD_DEAD;
        ++retired;
    }
    if (retired == 0u || retired > pool_stats.live_threads)
        return KERNEL_THREAD_CORRUPT;
    pool_stats.live_threads -= retired;
    pool_stats.dead_threads += retired;
    if (retired_threads != NULL)
        *retired_threads = retired;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_release_process(uint16_t process_slot)
{
    bool found = false;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *thread = &threads[slot];

        if (thread->occupied == 0u ||
            thread->process_slot != process_slot)
            continue;
        if (thread->state != KERNEL_THREAD_DEAD)
            return KERNEL_THREAD_INVALID_STATE;
        if (thread->wait_queue != NULL || !kernel_stack_valid(thread))
            return KERNEL_THREAD_CORRUPT;
        initialize_kernel_stack(thread);
        thread->occupied = 0u;
        found = true;
    }
    return found ? KERNEL_THREAD_OK : KERNEL_THREAD_INVALID_STATE;
}

KernelThread *kernel_thread_at(uint16_t slot)
{
    if (slot >= KERNEL_THREAD_MAX || threads[slot].occupied == 0u)
        return NULL;
    return &threads[slot];
}

bool kernel_thread_snapshot(uint32_t slot, KernelThreadSnapshot *snapshot)
{
    const KernelThread *thread;

    if (slot >= KERNEL_THREAD_MAX || snapshot == NULL)
        return false;
    thread = &threads[slot];
    snapshot->id = thread->id;
    snapshot->process_id = thread->process_id;
    snapshot->user_stack_base = thread->user_stack_base;
    snapshot->user_stack_top = thread->user_stack_top;
    snapshot->kernel_stack_guard = thread->occupied != 0u ?
        thread->kernel_stack_base - KERNEL_THREAD_SUPERVISOR_GUARD_SIZE : 0u;
    snapshot->kernel_stack_base = thread->occupied != 0u ?
        thread->kernel_stack_base : 0u;
    snapshot->kernel_stack_top = thread->occupied != 0u ?
        thread->kernel_stack_top : 0u;
    snapshot->kernel_stack_used = thread->occupied != 0u ?
        kernel_stack_observed_used(thread) : 0u;
    snapshot->kernel_stack_entries = thread->occupied != 0u ?
        thread->kernel_stack_entries : 0u;
    snapshot->timer_ticks = thread->timer_ticks;
    snapshot->run_count = thread->run_count;
    snapshot->syscall_count = thread->syscall_count;
    snapshot->self_handle = thread->self_handle;
    snapshot->process_slot = thread->process_slot;
    snapshot->stack_slot = thread->stack_slot;
    snapshot->state = thread->state;
    snapshot->base_priority = thread->base_priority;
    snapshot->effective_priority = thread->effective_priority;
    snapshot->occupied = thread->occupied;
    snapshot->waiting = thread->wait_queue != NULL ? 1u : 0u;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    snapshot->reserved[2] = 0u;
    return true;
}

bool kernel_thread_pool_stats(KernelThreadPoolStats *stats)
{
    uint32_t blocked = 0u;
    uint32_t entries = 0u;
    uint32_t max_used = 0u;

    if (stats == NULL)
        return false;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        const KernelThread *thread = &threads[slot];
        uint32_t used;

        if (thread->occupied == 0u)
            continue;
        if (!kernel_stack_valid(thread))
            return false;
        if (thread->state == KERNEL_THREAD_BLOCKED)
            ++blocked;
        entries += thread->kernel_stack_entries;
        used = kernel_stack_observed_used(thread);
        if (used > max_used)
            max_used = used;
    }
    stats->created_threads = pool_stats.created_threads;
    stats->live_threads = pool_stats.live_threads;
    stats->dead_threads = pool_stats.dead_threads;
    stats->ready_bitmap = ready_bitmap;
    stats->ready_threads = ready_count;
    stats->blocked_threads = blocked;
    stats->kernel_stack_entries = entries;
    stats->kernel_stack_max_used = max_used;
    stats->kernel_stack_measurements =
        pool_stats.kernel_stack_measurements;
    stats->kernel_stack_scan_words = pool_stats.kernel_stack_scan_words;
    return true;
}

bool kernel_thread_process_runnable(uint16_t process_slot)
{
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        const KernelThread *thread = &threads[slot];

        if (thread->occupied != 0u &&
            thread->process_slot == process_slot &&
            (thread->state == KERNEL_THREAD_READY ||
             thread->state == KERNEL_THREAD_RUNNING))
            return true;
    }
    return false;
}

uint32_t kernel_thread_process_count(uint16_t process_slot, bool live_only)
{
    uint32_t count = 0u;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        const KernelThread *thread = &threads[slot];

        if (thread->occupied == 0u ||
            thread->process_slot != process_slot)
            continue;
        if (!live_only || thread->state != KERNEL_THREAD_DEAD)
            ++count;
    }
    return count;
}

uint32_t kernel_thread_process_run_count(uint16_t process_slot)
{
    uint32_t count = 0u;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if (threads[slot].occupied != 0u &&
            threads[slot].process_slot == process_slot)
            count += threads[slot].run_count;
    }
    return count;
}

uint32_t kernel_thread_process_timer_ticks(uint16_t process_slot)
{
    uint32_t count = 0u;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if (threads[slot].occupied != 0u &&
            threads[slot].process_slot == process_slot)
            count += threads[slot].timer_ticks;
    }
    return count;
}

uint32_t kernel_thread_process_syscalls(uint16_t process_slot)
{
    uint32_t count = 0u;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if (threads[slot].occupied != 0u &&
            threads[slot].process_slot == process_slot)
            count += threads[slot].syscall_count;
    }
    return count;
}

KernelThreadState kernel_thread_process_representative_state(
    uint16_t process_slot)
{
    KernelThreadState result = KERNEL_THREAD_UNUSED;

    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        const KernelThread *thread = &threads[slot];

        if (thread->occupied == 0u ||
            thread->process_slot != process_slot)
            continue;
        if (thread->state == KERNEL_THREAD_RUNNING)
            return KERNEL_THREAD_RUNNING;
        if (thread->state == KERNEL_THREAD_READY)
            result = KERNEL_THREAD_READY;
        else if (result == KERNEL_THREAD_UNUSED)
            result = (KernelThreadState)thread->state;
    }
    return result;
}

KernelThreadStatus kernel_thread_note_kernel_entry(KernelThread *thread,
                                                   uint32_t stack_pointer)
{
    if (!kernel_stack_valid(thread))
        return KERNEL_THREAD_CORRUPT;
    if (stack_pointer < thread->kernel_stack_base + sizeof(uint32_t) ||
        stack_pointer >= thread->kernel_stack_top)
        return KERNEL_THREAD_CORRUPT;
    if (stack_pointer < thread->kernel_stack_low_water)
        thread->kernel_stack_low_water = stack_pointer;
    ++thread->kernel_stack_entries;
    if (thread->kernel_stack_entries == 0u)
        return KERNEL_THREAD_CORRUPT;
    return KERNEL_THREAD_OK;
}

bool kernel_thread_stacks_valid(void)
{
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if (threads[slot].occupied != 0u &&
            !kernel_stack_valid(&threads[slot]))
            return false;
    }
    return true;
}

bool kernel_thread_measure_stacks(uint32_t *maximum_used)
{
    uint32_t maximum = 0u;

    if (maximum_used == NULL)
        return false;
    if (pool_stats.kernel_stack_measurements != UINT32_MAX)
        ++pool_stats.kernel_stack_measurements;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *thread = &threads[slot];
        uint32_t used;
        uint32_t measured_low_water;

        if (thread->occupied == 0u)
            continue;
        if (!kernel_stack_valid(thread))
            return false;
        used = kernel_stack_poison_used(thread);
        if (used > KERNEL_THREAD_SUPERVISOR_STACK_SIZE)
            return false;
        measured_low_water = thread->kernel_stack_top - used;
        if (measured_low_water < thread->kernel_stack_low_water)
            thread->kernel_stack_low_water = measured_low_water;
        if (used > maximum)
            maximum = used;
    }
    *maximum_used = maximum;
    return true;
}
