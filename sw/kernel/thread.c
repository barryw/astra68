#include "thread.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
/* KERNEL_PAGE_SIZE: the guard below a stack is expressed in pages. */
#include "memory.h"
#include "object_cache.h"
#include "performance.h"

#include <stddef.h>

#define THREAD_ID_PREFIX 0x20000000u
#define THREAD_STACK_CANARY 0x5354414bu
#define THREAD_STACK_POISON 0xa5a5a5a5u
#define THREAD_WAIT_REGISTRATION_NONE UINT16_MAX
#define THREAD_WAIT_REGISTRATION_COUNT \
    (KERNEL_THREAD_MAX * KERNEL_THREAD_WAIT_MEMBER_MAX)

typedef struct KernelThreadWaitRegistration {
    KernelThreadWaitQueue *queue;
    uint16_t previous;
    uint16_t next;
} KernelThreadWaitRegistration;

#if defined(__m68k__)
extern uint8_t _kernel_thread_stacks_start[];
#else
static _Alignas(4) uint32_t
    host_stack_words[KERNEL_THREAD_MAX]
                    [KERNEL_THREAD_SUPERVISOR_STACK_SIZE / sizeof(uint32_t)];
#endif

static KernelThread threads[KERNEL_THREAD_MAX];
static KernelObjectCache thread_cache;
static uint32_t thread_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_THREAD_MAX)];
static KernelThreadWaitRegistration
    wait_registrations[KERNEL_THREAD_MAX][KERNEL_THREAD_WAIT_MEMBER_MAX];
static uint16_t ready_head[KERNEL_THREAD_PRIORITY_LEVELS];
static uint16_t ready_tail[KERNEL_THREAD_PRIORITY_LEVELS];
static uint32_t ready_bitmap;
static uint32_t ready_count;
static uint64_t deadline_cycles[KERNEL_THREAD_MAX];
static uint32_t deadline_results[KERNEL_THREAD_MAX];
static uint16_t deadline_heap[KERNEL_THREAD_MAX];
static uint16_t deadline_positions[KERNEL_THREAD_MAX];
static uint16_t deadline_count;
static uint16_t wait_registration_count;
static uint16_t reap_pending_bitmap;
static uint16_t irq_wake_bitmap;
static uint32_t irq_wake_cycles[KERNEL_THREAD_MAX];
static KernelThreadPoolStats pool_stats;
static uint8_t pool_corrupt;

static bool valid_thread(const KernelThread *thread);

static void clear_irq_wake(uint16_t slot)
{
    if (slot >= KERNEL_THREAD_MAX)
        return;
    irq_wake_bitmap &= (uint16_t)~(uint16_t)(1u << slot);
    irq_wake_cycles[slot] = 0u;
}

static void mark_reap_pending(KernelThread *thread)
{
    thread->reap_pending = 1u;
    reap_pending_bitmap |= (uint16_t)(1u << thread->slot);
}

static void clear_reap_pending(KernelThread *thread)
{
    thread->reap_pending = 0u;
    reap_pending_bitmap &= (uint16_t)~(uint16_t)(1u << thread->slot);
}

_Static_assert(offsetof(KernelThread, context) == 0u,
               "thread context must remain the first field");
_Static_assert(offsetof(KernelThread, kernel_stack_top) ==
                   KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET,
               "assembly thread stack offset changed");
#if defined(__m68k__)
/*
 * 180 until the activity arrived; the four bytes it costs are 64 across the
 * whole pool of sixteen, which is what correlating a request across four
 * processes is worth. The assertion is deliberate: this record is multiplied
 * by KERNEL_THREAD_MAX and sits in kernel RAM, so growing it is a decision
 * rather than an accident.
 */
_Static_assert(sizeof(KernelThread) == 184u,
               "thread record size changed; update the memory budget");
_Static_assert(sizeof(KernelThreadWaitRegistration) == 8u,
               "wait registration memory budget changed");
#else
_Static_assert(sizeof(KernelThread) <= 200u,
               "host thread record exceeds the test memory budget");
#endif
_Static_assert(KERNEL_THREAD_MAX <= 16u,
               "reap bitmap cannot represent every thread slot");
_Static_assert(THREAD_WAIT_REGISTRATION_COUNT < UINT16_MAX,
               "wait registration identifiers must fit in 16 bits");
/*
 * The guard is the floor page of a slot's stride, which is never mapped, so an
 * overflow leaves the mapping instead of reaching the thread's stack below it.
 * The stride has to hold that page plus what is committed at creation; the
 * space between them is what growth is allowed to take.
 */
_Static_assert(KERNEL_THREAD_STACK_GUARD_SIZE == KERNEL_PAGE_SIZE,
               "the stack guard is one page");
_Static_assert(KERNEL_THREAD_STACK_STRIDE >=
                   KERNEL_THREAD_STACK_SIZE + KERNEL_THREAD_STACK_GUARD_SIZE,
               "thread stacks require an unmapped guard page");
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

    kernel_words_fill(words, word_count, THREAD_STACK_POISON);
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

static bool deadline_precedes(uint16_t left, uint16_t right)
{
    if (deadline_cycles[left] != deadline_cycles[right])
        return deadline_cycles[left] < deadline_cycles[right];
    return left < right;
}

static void deadline_swap(uint16_t left, uint16_t right)
{
    uint16_t left_slot = deadline_heap[left];
    uint16_t right_slot = deadline_heap[right];

    deadline_heap[left] = right_slot;
    deadline_heap[right] = left_slot;
    deadline_positions[left_slot] = right;
    deadline_positions[right_slot] = left;
}

static void deadline_sift_up(uint16_t position)
{
    while (position != 0u) {
        uint16_t parent = (uint16_t)((position - 1u) >> 1);

        if (!deadline_precedes(deadline_heap[position],
                               deadline_heap[parent]))
            break;
        deadline_swap(position, parent);
        position = parent;
    }
}

static void deadline_sift_down(uint16_t position)
{
    for (;;) {
        uint16_t left = (uint16_t)(position * 2u + 1u);
        uint16_t right = (uint16_t)(left + 1u);
        uint16_t first = position;

        if (left < deadline_count &&
            deadline_precedes(deadline_heap[left], deadline_heap[first]))
            first = left;
        if (right < deadline_count &&
            deadline_precedes(deadline_heap[right], deadline_heap[first]))
            first = right;
        if (first == position)
            return;
        deadline_swap(position, first);
        position = first;
    }
}

static KernelThreadStatus deadline_insert(KernelThread *thread,
                                          uint64_t deadline,
                                          uint32_t timeout_result)
{
    uint16_t position;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_BLOCKED ||
        deadline == KERNEL_THREAD_DEADLINE_NEVER ||
        deadline_positions[thread->slot] != KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_INVALID_STATE;
    if (deadline_count >= KERNEL_THREAD_MAX)
        return KERNEL_THREAD_NO_SLOT;

    position = deadline_count++;
    deadline_cycles[thread->slot] = deadline;
    deadline_results[thread->slot] = timeout_result;
    deadline_heap[position] = thread->slot;
    deadline_positions[thread->slot] = position;
    deadline_sift_up(position);
    ++pool_stats.deadline_waits;
    if (deadline_count > pool_stats.deadline_max_depth)
        pool_stats.deadline_max_depth = deadline_count;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus deadline_remove(KernelThread *thread)
{
    uint16_t position;
    uint16_t replacement;

    if (!valid_thread(thread))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    position = deadline_positions[thread->slot];
    if (position == KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_OK;
    if (position >= deadline_count ||
        deadline_heap[position] != thread->slot)
        return KERNEL_THREAD_CORRUPT;

    --deadline_count;
    replacement = deadline_heap[deadline_count];
    deadline_heap[deadline_count] = KERNEL_THREAD_SLOT_NONE;
    deadline_positions[thread->slot] = KERNEL_THREAD_SLOT_NONE;
    deadline_cycles[thread->slot] = 0u;
    deadline_results[thread->slot] = 0u;
    if (position == deadline_count)
        return KERNEL_THREAD_OK;

    deadline_heap[position] = replacement;
    deadline_positions[replacement] = position;
    if (position != 0u &&
        deadline_precedes(replacement,
                          deadline_heap[(position - 1u) >> 1]))
        deadline_sift_up(position);
    else
        deadline_sift_down(position);
    return KERNEL_THREAD_OK;
}

static bool deadline_heap_valid(void)
{
    uint32_t seen = 0u;

    if (deadline_count > KERNEL_THREAD_MAX)
        return false;
    for (uint16_t position = 0u; position < deadline_count; ++position) {
        uint16_t slot = deadline_heap[position];
        uint16_t left = (uint16_t)(position * 2u + 1u);
        uint16_t right = (uint16_t)(left + 1u);

        if (slot >= KERNEL_THREAD_MAX ||
            (seen & (1u << slot)) != 0u ||
            deadline_positions[slot] != position ||
            !valid_thread(&threads[slot]) ||
            threads[slot].state != KERNEL_THREAD_BLOCKED ||
            threads[slot].wait_member_count == 0u ||
            deadline_cycles[slot] == KERNEL_THREAD_DEADLINE_NEVER)
            return false;
        if (left < deadline_count &&
            deadline_precedes(deadline_heap[left], slot))
            return false;
        if (right < deadline_count &&
            deadline_precedes(deadline_heap[right], slot))
            return false;
        seen |= 1u << slot;
    }
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        if ((deadline_positions[slot] != KERNEL_THREAD_SLOT_NONE) !=
            ((seen & (1u << slot)) != 0u))
            return false;
    }
    return true;
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

static KernelThreadWaitRegistration *registration_at(uint16_t identifier)
{
    uint16_t thread_slot;
    uint16_t member;

    if (identifier >= THREAD_WAIT_REGISTRATION_COUNT)
        return NULL;
    thread_slot = (uint16_t)(identifier / KERNEL_THREAD_WAIT_MEMBER_MAX);
    member = (uint16_t)(identifier % KERNEL_THREAD_WAIT_MEMBER_MAX);
    return &wait_registrations[thread_slot][member];
}

static KernelThread *registration_thread_at(uint16_t identifier)
{
    return identifier < THREAD_WAIT_REGISTRATION_COUNT ?
        &threads[identifier / KERNEL_THREAD_WAIT_MEMBER_MAX] : NULL;
}

static uint16_t registration_member_at(uint16_t identifier)
{
    return identifier < THREAD_WAIT_REGISTRATION_COUNT ?
        (uint16_t)(identifier % KERNEL_THREAD_WAIT_MEMBER_MAX) : UINT16_MAX;
}

static uint16_t registration_identifier(
    const KernelThreadWaitRegistration *registration)
{
    uintptr_t address = (uintptr_t)registration;
    uintptr_t first = (uintptr_t)&wait_registrations[0][0];
    uintptr_t limit = first + sizeof(wait_registrations);
    uintptr_t offset;

    if (registration == NULL || address < first || address >= limit)
        return THREAD_WAIT_REGISTRATION_NONE;
    offset = address - first;
    if (offset % sizeof(*registration) != 0u)
        return THREAD_WAIT_REGISTRATION_NONE;
    offset /= sizeof(*registration);
    return offset < THREAD_WAIT_REGISTRATION_COUNT ? (uint16_t)offset :
                                                     THREAD_WAIT_REGISTRATION_NONE;
}

static KernelThread *registration_thread(
    const KernelThreadWaitRegistration *registration)
{
    uint16_t identifier = registration_identifier(registration);

    return identifier == THREAD_WAIT_REGISTRATION_NONE ? NULL :
        &threads[identifier / KERNEL_THREAD_WAIT_MEMBER_MAX];
}

static uint16_t registration_member(
    const KernelThreadWaitRegistration *registration)
{
    uint16_t identifier = registration_identifier(registration);

    return identifier == THREAD_WAIT_REGISTRATION_NONE ? UINT16_MAX :
        (uint16_t)(identifier % KERNEL_THREAD_WAIT_MEMBER_MAX);
}

static bool valid_wait_queue(const KernelThreadWaitQueue *queue)
{
    uint16_t identifier;
    uint16_t previous = THREAD_WAIT_REGISTRATION_NONE;
    uint16_t traversed = 0u;

    if (queue == NULL || queue->count > KERNEL_THREAD_MAX)
        return false;
    if (queue->count == 0u)
        return queue->head == THREAD_WAIT_REGISTRATION_NONE &&
               queue->tail == THREAD_WAIT_REGISTRATION_NONE;
    if (queue->head >= THREAD_WAIT_REGISTRATION_COUNT ||
        queue->tail >= THREAD_WAIT_REGISTRATION_COUNT)
        return false;

    identifier = queue->head;
    while (identifier != THREAD_WAIT_REGISTRATION_NONE) {
        KernelThreadWaitRegistration *registration;
        KernelThread *thread;
        uint16_t member;

        if (identifier >= THREAD_WAIT_REGISTRATION_COUNT ||
            traversed >= queue->count)
            return false;
        registration = registration_at(identifier);
        thread = registration_thread_at(identifier);
        member = registration_member_at(identifier);
        if (registration == NULL || !valid_thread(thread) ||
            thread->state != KERNEL_THREAD_BLOCKED ||
            thread->wait_member_count == 0u ||
            member >= thread->wait_member_count ||
            registration->queue != queue ||
            registration->previous != previous)
            return false;
        previous = identifier;
        identifier = registration->next;
        ++traversed;
    }
    return traversed == queue->count && previous == queue->tail;
}

static bool wait_row_clear(uint16_t thread_slot)
{
    if (thread_slot >= KERNEL_THREAD_MAX)
        return false;
    for (uint16_t member = 0u; member < KERNEL_THREAD_WAIT_MEMBER_MAX;
         ++member) {
        const KernelThreadWaitRegistration *registration =
            &wait_registrations[thread_slot][member];

        if (registration->queue != NULL ||
            registration->previous != THREAD_WAIT_REGISTRATION_NONE ||
            registration->next != THREAD_WAIT_REGISTRATION_NONE)
            return false;
    }
    return true;
}

static void reset_wait_row(uint16_t thread_slot)
{
    for (uint16_t member = 0u; member < KERNEL_THREAD_WAIT_MEMBER_MAX;
         ++member) {
        KernelThreadWaitRegistration *registration =
            &wait_registrations[thread_slot][member];

        registration->queue = NULL;
        registration->previous = THREAD_WAIT_REGISTRATION_NONE;
        registration->next = THREAD_WAIT_REGISTRATION_NONE;
    }
}

static KernelThreadStatus enqueue_wait_registration(
    KernelThread *thread, uint16_t member, KernelThreadWaitQueue *queue)
{
    KernelThreadWaitRegistration *registration;
    uint16_t previous = THREAD_WAIT_REGISTRATION_NONE;
    uint16_t next;
    uint16_t identifier;

    if (!valid_thread(thread) || !valid_wait_queue(queue) ||
        thread->state != KERNEL_THREAD_BLOCKED ||
        member >= thread->wait_member_count ||
        queue->count >= KERNEL_THREAD_MAX)
        return KERNEL_THREAD_INVALID_STATE;
    registration = &wait_registrations[thread->slot][member];
    identifier = registration_identifier(registration);
    if (identifier == THREAD_WAIT_REGISTRATION_NONE ||
        registration->queue != NULL ||
        registration->previous != THREAD_WAIT_REGISTRATION_NONE ||
        registration->next != THREAD_WAIT_REGISTRATION_NONE)
        return KERNEL_THREAD_INVALID_STATE;

    next = queue->head;
    while (next != THREAD_WAIT_REGISTRATION_NONE) {
        KernelThreadWaitRegistration *queued_registration =
            registration_at(next);
        KernelThread *queued = registration_thread(queued_registration);

        if (queued_registration == NULL || !valid_thread(queued) ||
            queued_registration->queue != queue ||
            queued_registration->previous != previous)
            return KERNEL_THREAD_CORRUPT;
        if (queued->effective_priority < thread->effective_priority)
            break;
        previous = next;
        next = queued_registration->next;
    }

    registration->queue = queue;
    registration->previous = previous;
    registration->next = next;
    if (previous == THREAD_WAIT_REGISTRATION_NONE)
        queue->head = identifier;
    else
        registration_at(previous)->next = identifier;
    if (next == THREAD_WAIT_REGISTRATION_NONE)
        queue->tail = identifier;
    else
        registration_at(next)->previous = identifier;
    ++queue->count;
    ++wait_registration_count;
    if (wait_registration_count > pool_stats.wait_registration_max)
        pool_stats.wait_registration_max = wait_registration_count;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus remove_wait_registration(
    KernelThreadWaitRegistration *registration)
{
    KernelThreadWaitQueue *queue;
    uint16_t identifier;
    uint16_t previous;
    uint16_t next;

    identifier = registration_identifier(registration);
    if (identifier == THREAD_WAIT_REGISTRATION_NONE ||
        registration->queue == NULL)
        return KERNEL_THREAD_INVALID_STATE;
    queue = registration->queue;
    if (registration != registration_at(identifier) || queue->count == 0u ||
        queue->count > KERNEL_THREAD_MAX ||
        queue->head >= THREAD_WAIT_REGISTRATION_COUNT ||
        queue->tail >= THREAD_WAIT_REGISTRATION_COUNT)
        return KERNEL_THREAD_CORRUPT;
    previous = registration->previous;
    next = registration->next;

    if (previous == THREAD_WAIT_REGISTRATION_NONE) {
        if (queue->head != identifier)
            return KERNEL_THREAD_CORRUPT;
        queue->head = next;
    } else {
        KernelThreadWaitRegistration *before = registration_at(previous);

        if (before == NULL || before->queue != queue ||
            before->next != identifier)
            return KERNEL_THREAD_CORRUPT;
        before->next = next;
    }
    if (next == THREAD_WAIT_REGISTRATION_NONE) {
        if (queue->tail != identifier)
            return KERNEL_THREAD_CORRUPT;
        queue->tail = previous;
    } else {
        KernelThreadWaitRegistration *after = registration_at(next);

        if (after == NULL || after->queue != queue ||
            after->previous != identifier)
            return KERNEL_THREAD_CORRUPT;
        after->previous = previous;
    }
    --queue->count;
    if (queue->count == 0u) {
        if (queue->head != THREAD_WAIT_REGISTRATION_NONE ||
            queue->tail != THREAD_WAIT_REGISTRATION_NONE)
            return KERNEL_THREAD_CORRUPT;
    } else if (queue->head >= THREAD_WAIT_REGISTRATION_COUNT ||
               queue->tail >= THREAD_WAIT_REGISTRATION_COUNT) {
        return KERNEL_THREAD_CORRUPT;
    }
    if (wait_registration_count == 0u)
        return KERNEL_THREAD_CORRUPT;
    --wait_registration_count;
    registration->queue = NULL;
    registration->previous = THREAD_WAIT_REGISTRATION_NONE;
    registration->next = THREAD_WAIT_REGISTRATION_NONE;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus withdraw_wait_set(
    KernelThread *thread, KernelThreadWaitQueue *already_advanced,
    bool advance_sequences)
{
    uint16_t member_count;

    if (thread == NULL || thread->state != KERNEL_THREAD_BLOCKED ||
        thread->wait_member_count == 0u ||
        thread->wait_member_count > KERNEL_THREAD_WAIT_MEMBER_MAX)
        return KERNEL_THREAD_INVALID_STATE;
    member_count = thread->wait_member_count;
    for (uint16_t member = 0u; member < member_count; ++member) {
        KernelThreadWaitQueue *queue =
            wait_registrations[thread->slot][member].queue;
        bool seen = queue == already_advanced;

        if (queue == NULL)
            return KERNEL_THREAD_CORRUPT;
        for (uint16_t prior = 0u; prior < member && !seen; ++prior)
            seen = wait_registrations[thread->slot][prior].queue == queue;
        if (!seen) {
            if (!valid_wait_queue(queue))
                return KERNEL_THREAD_CORRUPT;
            if (advance_sequences)
                queue->sequence = kernel_generation_next(queue->sequence);
        }
    }
    for (uint16_t member = 0u; member < member_count; ++member) {
        if (remove_wait_registration(
                &wait_registrations[thread->slot][member]) !=
            KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
    }
    thread->wait_member_count = 0u;
    thread->wait_mode = KERNEL_THREAD_WAIT_NONE;
    thread->wait_reserved = 0u;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus complete_wait(
    KernelThread *thread, KernelThreadWaitRegistration *winner,
    KernelThreadWaitQueue *already_advanced, uint32_t result,
    uint32_t detail, bool write_one_detail)
{
    KernelThreadStatus status;
    uint16_t member = winner == NULL ? UINT16_MAX :
        registration_member(winner);
    uint8_t mode;
    bool cancelled_deadline;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_BLOCKED ||
        thread->wait_member_count == 0u ||
        (winner != NULL && registration_thread(winner) != thread))
        return KERNEL_THREAD_INVALID_STATE;
    mode = thread->wait_mode;
    cancelled_deadline =
        deadline_positions[thread->slot] != KERNEL_THREAD_SLOT_NONE;
    status = deadline_remove(thread);
    if (status != KERNEL_THREAD_OK)
        return status;
    status = withdraw_wait_set(thread, already_advanced, true);
    if (status != KERNEL_THREAD_OK)
        return status;
    if (cancelled_deadline)
        ++pool_stats.deadline_cancellations;
    thread->context.data[0] = result;
    if (mode == KERNEL_THREAD_WAIT_MULTIPLE) {
        thread->context.data[1] = member == UINT16_MAX ?
            ASTRA_WAIT_INDEX_NONE : member;
        thread->context.data[2] = detail;
        ++pool_stats.wait_set_wakeups;
    } else if (write_one_detail) {
        thread->context.data[1] = detail;
    }
    thread->state = KERNEL_THREAD_READY;
    status = enqueue_ready(thread);
    return status == KERNEL_THREAD_OK ? KERNEL_THREAD_OK :
                                       KERNEL_THREAD_CORRUPT;
}

static KernelThreadStatus wake_waiter(
    KernelThreadWaitRegistration *registration,
    KernelThreadWaitQueue *already_advanced, uint32_t result,
    uint32_t detail, bool write_one_detail)
{
    KernelThread *thread = registration_thread(registration);

    if (!valid_thread(thread))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    return complete_wait(thread, registration, already_advanced, result,
                         detail, write_one_detail);
}

static KernelThreadStatus wake_death_waiters(KernelThread *thread,
                                             uint32_t result,
                                             uint32_t *woken_threads)
{
    KernelThreadWaitQueue *queue;
    uint32_t woken = 0u;

    if (!valid_thread(thread))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    queue = &thread->death_waiters;
    if (!valid_wait_queue(queue))
        return KERNEL_THREAD_CORRUPT;
    queue->sequence = kernel_generation_next(queue->sequence);
    while (queue->count != 0u) {
        KernelThreadWaitRegistration *registration;
        KernelThread *waiter;

        registration = registration_at(queue->head);
        waiter = registration_thread_at(queue->head);
        if (registration == NULL || !valid_thread(waiter))
            return KERNEL_THREAD_CORRUPT;
        if (wake_waiter(registration, queue, result,
                        result == ASTRA_SYSCALL_OK ? thread->exit_status : 0u,
                        true) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        ++woken;
    }
    pool_stats.death_wakeups += woken;
    if (woken_threads != NULL)
        *woken_threads = woken;
    return KERNEL_THREAD_OK;
}

void kernel_thread_pool_init(void)
{
    if (!kernel_object_cache_init(
            &thread_cache, threads, sizeof(threads[0]), KERNEL_THREAD_MAX,
            thread_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_THREAD_MAX),
            KERNEL_ALLOCATION_SITE_THREAD_RECORD)) {
        pool_corrupt = 1u;
        return;
    }
    for (uint32_t index = 0u; index < KERNEL_THREAD_MAX; ++index) {
        kernel_bytes_clear(&threads[index], sizeof(threads[index]));
        reset_wait_row((uint16_t)index);
        deadline_cycles[index] = 0u;
        deadline_results[index] = 0u;
        deadline_heap[index] = KERNEL_THREAD_SLOT_NONE;
        deadline_positions[index] = KERNEL_THREAD_SLOT_NONE;
        irq_wake_cycles[index] = 0u;
    }
    for (uint32_t priority = 0u;
         priority < KERNEL_THREAD_PRIORITY_LEVELS; ++priority) {
        ready_head[priority] = KERNEL_THREAD_SLOT_NONE;
        ready_tail[priority] = KERNEL_THREAD_SLOT_NONE;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
    ready_bitmap = 0u;
    ready_count = 0u;
    deadline_count = 0u;
    wait_registration_count = 0u;
    reap_pending_bitmap = 0u;
    irq_wake_bitmap = 0u;
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
    void *raw_candidate;
    uint16_t candidate_slot;
    KernelObjectCacheStatus cache_status;
    uint32_t generation;

    if (process_id == 0u || program_counter == 0u ||
        user_stack < KERNEL_THREAD_STACK_SIZE ||
        (user_stack & 3u) != 0u ||
        priority >= KERNEL_THREAD_PRIORITY_LEVELS || thread == NULL)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    cache_status = kernel_object_cache_claim(
        &thread_cache, process_id, &raw_candidate, &candidate_slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
        return KERNEL_THREAD_NO_SLOT;
    if (cache_status != KERNEL_OBJECT_CACHE_OK ||
        candidate_slot >= KERNEL_THREAD_MAX) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    candidate = raw_candidate;
    if (candidate->occupied != 0u) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }

    generation = kernel_generation_next(candidate->generation);
    kernel_bytes_clear(candidate, sizeof(*candidate));
    candidate->generation = generation;
    candidate->slot = candidate_slot;
    candidate->id = THREAD_ID_PREFIX |
                    ((generation & 0x000fffffu) << 4) |
                    (uint32_t)candidate->slot;
    candidate->process_id = process_id;
    candidate->process_slot = process_slot;
    candidate->stack_slot = stack_slot;
    candidate->user_stack_top = user_stack;
    candidate->user_stack_base = user_stack - KERNEL_THREAD_STACK_SIZE;
    candidate->stack_pages =
        (uint8_t)(KERNEL_THREAD_STACK_SIZE / KERNEL_PAGE_SIZE);
    candidate->ready_previous = KERNEL_THREAD_SLOT_NONE;
    candidate->ready_next = KERNEL_THREAD_SLOT_NONE;
    /* Slot release validates the complete wait row before clearing occupied. */
    candidate->state = KERNEL_THREAD_CREATED;
    candidate->base_priority = priority;
    candidate->effective_priority = priority;
    candidate->occupied = 1u;
    kernel_thread_wait_queue_init(&candidate->death_waiters);
    initialize_kernel_stack(candidate);
    kernel_context_initialize(&candidate->context, program_counter,
                              user_stack);
    candidate->context.data[2] = initial_argument;
    if (!kernel_context_valid(&candidate->context)) {
        candidate->state = KERNEL_THREAD_DEAD;
        candidate->occupied = 0u;
        if (kernel_object_cache_release(&thread_cache, candidate) !=
            KERNEL_OBJECT_CACHE_OK)
            pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    *thread = candidate;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_publish(KernelThread *thread)
{
    KernelThreadStatus status;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED ||
        thread->self_handle == KERNEL_HANDLE_INVALID ||
        thread->handle_references == 0u)
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
    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED ||
        thread->handle_references != 0u ||
        thread->wait_member_count != 0u ||
        kernel_thread_wait_queue_count(&thread->death_waiters) != 0u ||
        !wait_row_clear(thread->slot))
        return KERNEL_THREAD_INVALID_STATE;
    initialize_kernel_stack(thread);
    thread->state = KERNEL_THREAD_DEAD;
    thread->occupied = 0u;
    if (kernel_object_cache_release(&thread_cache, thread) !=
        KERNEL_OBJECT_CACHE_OK) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    ++pool_stats.creation_rollbacks;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_attach_handle(KernelThread *thread,
                                                KernelHandle handle)
{
    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED ||
        handle == KERNEL_HANDLE_INVALID ||
        thread->handle_references != 0u ||
        thread->self_handle != KERNEL_HANDLE_INVALID)
        return KERNEL_THREAD_INVALID_STATE;
    thread->self_handle = handle;
    thread->handle_references = 1u;
    return KERNEL_THREAD_OK;
}

void kernel_thread_handle_release(void *object, void *context)
{
    KernelThread *thread = object;
    uint32_t woken = 0u;

    (void)context;
    if (!valid_thread(thread) || thread->handle_references == 0u) {
        pool_corrupt = 1u;
        return;
    }
    --thread->handle_references;
    if (thread->handle_references != 0u)
        return;
    thread->self_handle = KERNEL_HANDLE_INVALID;
    if (kernel_thread_wait_queue_count(&thread->death_waiters) != 0u) {
        if (wake_death_waiters(thread, ASTRA_SYSCALL_CLOSED, &woken) !=
            KERNEL_THREAD_OK) {
            pool_corrupt = 1u;
            return;
        }
        pool_stats.handle_close_wakeups += woken;
    }
    if (thread->state == KERNEL_THREAD_DEAD)
        mark_reap_pending(thread);
}

KernelThreadStatus kernel_thread_complete(KernelThread *thread,
                                          uint32_t exit_status,
                                          uint32_t terminal_result,
                                          uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_RUNNING ||
        thread->wait_member_count != 0u || thread->stack_released != 0u)
        return KERNEL_THREAD_INVALID_STATE;
    thread->exit_status = exit_status;
    thread->terminal_result = terminal_result;
    thread->state = KERNEL_THREAD_DEAD;
    mark_reap_pending(thread);
    if (wake_death_waiters(thread, terminal_result, &woken) !=
        KERNEL_THREAD_OK)
        return KERNEL_THREAD_CORRUPT;
    if (pool_stats.live_threads == 0u)
        return KERNEL_THREAD_CORRUPT;
    --pool_stats.live_threads;
    ++pool_stats.dead_threads;
    ++pool_stats.thread_exits;
    if (woken_threads != NULL)
        *woken_threads = woken;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_prepare_death_wait(
    KernelThread *target, KernelThread *waiter, KernelThreadWaitSpec *spec,
    bool *ready, uint32_t *wait_result, uint32_t *exit_status)
{
    if (!valid_thread(target) || !valid_thread(waiter) || target == waiter ||
        waiter->state != KERNEL_THREAD_RUNNING || spec == NULL ||
        ready == NULL || wait_result == NULL || exit_status == NULL)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    *ready = false;
    *wait_result = ASTRA_SYSCALL_OK;
    *exit_status = 0u;
    ++pool_stats.death_waits;
    if (target->state == KERNEL_THREAD_DEAD) {
        *ready = true;
        *wait_result = target->terminal_result;
        *exit_status = target->terminal_result == ASTRA_SYSCALL_OK ?
            target->exit_status : 0u;
        return KERNEL_THREAD_OK;
    }
    if (target->state == KERNEL_THREAD_UNUSED ||
        target->state == KERNEL_THREAD_CREATED)
        return KERNEL_THREAD_INVALID_STATE;
    spec->queue = &target->death_waiters;
    spec->sequence = kernel_thread_wait_queue_sequence(spec->queue);
    return spec->sequence == 0u ? KERNEL_THREAD_CORRUPT : KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_commit_death_wait(KernelThread *target)
{
    uint32_t waiters;

    if (!valid_thread(target))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    waiters = kernel_thread_wait_queue_count(&target->death_waiters);
    if (waiters == UINT32_MAX || waiters == 0u)
        return KERNEL_THREAD_INVALID_STATE;
    if (waiters > pool_stats.max_death_waiters)
        pool_stats.max_death_waiters = waiters;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_wait_for_death(
    KernelThread *target, KernelThread *waiter, uint64_t now,
    uint64_t deadline, uint32_t timeout_result, bool *blocked,
    uint32_t *wait_result, uint32_t *exit_status)
{
    KernelThreadWaitSpec spec;
    KernelThreadStatus status;
    bool ready;

    if (blocked == NULL || timeout_result == 0u)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *blocked = false;
    status = kernel_thread_prepare_death_wait(
        target, waiter, &spec, &ready, wait_result, exit_status);
    if (status != KERNEL_THREAD_OK || ready)
        return status;
    waiter->context.data[1] = 0u;
    status = kernel_thread_block_until(
        waiter, spec.queue, spec.sequence, now, deadline, timeout_result);
    if (status != KERNEL_THREAD_OK)
        return status;
    status = kernel_thread_commit_death_wait(target);
    if (status != KERNEL_THREAD_OK)
        return status;
    *blocked = true;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_finish_reap(KernelThread *thread,
                                              bool *released)
{
    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_DEAD ||
        thread->reap_pending == 0u ||
        (reap_pending_bitmap & (uint16_t)(1u << thread->slot)) == 0u ||
        released == NULL ||
        thread->wait_member_count != 0u || !wait_row_clear(thread->slot) ||
        kernel_thread_wait_queue_count(&thread->death_waiters) != 0u)
        return KERNEL_THREAD_INVALID_STATE;
    *released = false;
    thread->stack_released = 1u;
    if (thread->handle_references != 0u) {
        clear_reap_pending(thread);
        return KERNEL_THREAD_OK;
    }
    initialize_kernel_stack(thread);
    clear_reap_pending(thread);
    thread->occupied = 0u;
    if (kernel_object_cache_release(&thread_cache, thread) !=
        KERNEL_OBJECT_CACHE_OK) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    ++pool_stats.reaped_threads;
    *released = true;
    return KERNEL_THREAD_OK;
}

bool kernel_thread_reap_pending(void)
{
    return reap_pending_bitmap != 0u;
}

uint16_t kernel_thread_reap_slots(void)
{
    return reap_pending_bitmap;
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
    if ((irq_wake_bitmap & (uint16_t)(1u << slot)) != 0u) {
        uint32_t elapsed = kernel_performance_cycles_low() -
                           irq_wake_cycles[slot];

        clear_irq_wake(slot);
        if (pool_stats.irq_wake_to_run_samples != UINT32_MAX)
            ++pool_stats.irq_wake_to_run_samples;
        if (elapsed > pool_stats.irq_wake_to_run_max_cycles)
            pool_stats.irq_wake_to_run_max_cycles = elapsed;
    }
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

uint32_t kernel_thread_wait_queue_waiter_count(
    const KernelThreadWaitQueue *queue)
{
    uint16_t identifier;
    uint16_t seen = 0u;
    uint32_t count = 0u;

    if (!valid_wait_queue(queue))
        return UINT32_MAX;
    identifier = queue->head;
    while (identifier != THREAD_WAIT_REGISTRATION_NONE) {
        KernelThreadWaitRegistration *registration =
            registration_at(identifier);
        KernelThread *thread = registration_thread_at(identifier);
        uint16_t bit;

        if (!valid_thread(thread))
            return UINT32_MAX;
        bit = (uint16_t)(1u << thread->slot);
        if ((seen & bit) == 0u) {
            seen |= bit;
            ++count;
        }
        identifier = registration->next;
    }
    return count;
}

static __attribute__((noinline))
KernelThreadStatus block_wait_set_fast(
    KernelThread *thread, const KernelThreadWaitSpec *specs,
    uint32_t member_count, uint64_t now, uint64_t deadline,
    uint32_t timeout_result, KernelThreadWaitMode mode)
{
    KernelThreadStatus status;
    uint32_t linked = 0u;

    if (!valid_thread(thread) || specs == NULL || member_count == 0u ||
        member_count > KERNEL_THREAD_WAIT_MEMBER_MAX ||
        thread->state != KERNEL_THREAD_RUNNING ||
        thread->wait_member_count != 0u || !wait_row_clear(thread->slot) ||
        (mode != KERNEL_THREAD_WAIT_ONE &&
         mode != KERNEL_THREAD_WAIT_MULTIPLE))
        return KERNEL_THREAD_INVALID_STATE;
    if (deadline != KERNEL_THREAD_DEADLINE_NEVER && deadline <= now)
        return KERNEL_THREAD_DEADLINE_EXPIRED;
    for (uint32_t member = 0u; member < member_count; ++member) {
        uint32_t additions = 1u;

        if (!valid_wait_queue(specs[member].queue))
            return KERNEL_THREAD_INVALID_STATE;
        if (specs[member].queue->sequence != specs[member].sequence)
            return KERNEL_THREAD_CONDITION_CHANGED;
        for (uint32_t prior = 0u; prior < member; ++prior) {
            if (specs[prior].queue == specs[member].queue)
                ++additions;
        }
        if (specs[member].queue->count > KERNEL_THREAD_MAX - additions)
            return KERNEL_THREAD_NO_SLOT;
    }
    thread->state = KERNEL_THREAD_BLOCKED;
    thread->wait_member_count = (uint8_t)member_count;
    thread->wait_mode = (uint8_t)mode;
    thread->wait_reserved = 0u;
    status = KERNEL_THREAD_OK;
    for (uint32_t member = 0u; member < member_count; ++member) {
        status = enqueue_wait_registration(
            thread, (uint16_t)member, specs[member].queue);
        if (status != KERNEL_THREAD_OK)
            break;
        ++linked;
    }
    if (status == KERNEL_THREAD_OK && deadline != KERNEL_THREAD_DEADLINE_NEVER)
        status = deadline_insert(thread, deadline, timeout_result);
    if (status != KERNEL_THREAD_OK) {
        for (uint32_t member = 0u; member < linked; ++member) {
            if (remove_wait_registration(
                    &wait_registrations[thread->slot][member]) !=
                KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
        }
        thread->state = KERNEL_THREAD_RUNNING;
        thread->wait_member_count = 0u;
        thread->wait_mode = KERNEL_THREAD_WAIT_NONE;
        thread->wait_reserved = 0u;
        return status;
    }
    if (mode == KERNEL_THREAD_WAIT_MULTIPLE)
        ++pool_stats.wait_set_blocks;
    if (member_count > pool_stats.max_wait_members)
        pool_stats.max_wait_members = member_count;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus block_wait_set_profiled(
    KernelThread *thread, const KernelThreadWaitSpec *specs,
    uint32_t member_count, uint64_t now, uint64_t deadline,
    uint32_t timeout_result, KernelThreadWaitMode mode)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        mode == KERNEL_THREAD_WAIT_MULTIPLE ?
            KERNEL_PERFORMANCE_WAIT_SET_BLOCK :
            KERNEL_PERFORMANCE_WAIT_BLOCK);
    status = block_wait_set_fast(thread, specs, member_count, now, deadline,
                                 timeout_result, mode);
    kernel_performance_end(performance);
    return status;
}

static KernelThreadStatus block_wait_set(
    KernelThread *thread, const KernelThreadWaitSpec *specs,
    uint32_t member_count, uint64_t now, uint64_t deadline,
    uint32_t timeout_result, KernelThreadWaitMode mode)
{
    if (kernel_performance_sampling_enabled == 0u)
        return block_wait_set_fast(thread, specs, member_count, now,
                                   deadline, timeout_result, mode);
    return block_wait_set_profiled(thread, specs, member_count, now,
                                   deadline, timeout_result, mode);
}

KernelThreadStatus kernel_thread_block(KernelThread *thread,
                                       KernelThreadWaitQueue *queue,
                                       uint32_t expected_sequence)
{
    return kernel_thread_block_until(
        thread, queue, expected_sequence, 0u,
        KERNEL_THREAD_DEADLINE_NEVER, 0u);
}

KernelThreadStatus kernel_thread_block_until(
    KernelThread *thread, KernelThreadWaitQueue *queue,
    uint32_t expected_sequence, uint64_t now, uint64_t deadline,
    uint32_t timeout_result)
{
    KernelThreadWaitSpec spec = {queue, expected_sequence};

    return block_wait_set(thread, &spec, 1u, now, deadline, timeout_result,
                          KERNEL_THREAD_WAIT_ONE);
}

KernelThreadStatus kernel_thread_block_wait_set(
    KernelThread *thread, const KernelThreadWaitSpec *specs,
    uint32_t member_count, uint64_t now, uint64_t deadline,
    uint32_t timeout_result)
{
    return block_wait_set(thread, specs, member_count, now, deadline,
                          timeout_result, KERNEL_THREAD_WAIT_MULTIPLE);
}

static __attribute__((noinline))
KernelThreadStatus wake_one_fast(KernelThreadWaitQueue *queue,
                                 uint32_t result,
                                 KernelThread **thread)
{
    KernelThreadWaitRegistration *registration;
    KernelThread *waiter;

    if (thread == NULL || !valid_wait_queue(queue))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    queue->sequence = kernel_generation_next(queue->sequence);
    if (queue->count == 0u)
        return KERNEL_THREAD_NO_RUNNABLE;
    registration = registration_at(queue->head);
    waiter = registration_thread_at(queue->head);
    if (wake_waiter(registration, queue, result, 0u, false) !=
            KERNEL_THREAD_OK)
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
    KernelPerformanceMetric metric = KERNEL_PERFORMANCE_WAKE;

    if (queue != NULL && queue->count != 0u &&
        queue->head < THREAD_WAIT_REGISTRATION_COUNT) {
        KernelThreadWaitRegistration *registration =
            registration_at(queue->head);
        KernelThread *waiter = registration_thread_at(queue->head);

        if (registration != NULL && registration->queue == queue &&
            valid_thread(waiter) &&
            waiter->wait_mode == KERNEL_THREAD_WAIT_MULTIPLE)
            metric = KERNEL_PERFORMANCE_WAIT_SET_WAKE;
    }

    performance = kernel_performance_begin_sampled(metric);
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
                                 uint32_t detail,
                                 bool write_one_detail,
                                 bool irq_wake,
                                 uint32_t *woken_threads)
{
    uint32_t wake_cycle = 0u;
    uint32_t woken = 0u;

    if (!valid_wait_queue(queue))
        return KERNEL_THREAD_INVALID_ARGUMENT;
    if (irq_wake && queue->count != 0u)
        wake_cycle = kernel_performance_cycles_low();
    queue->sequence = kernel_generation_next(queue->sequence);
    while (queue->count != 0u) {
        KernelThreadWaitRegistration *registration =
            registration_at(queue->head);
        KernelThread *waiter;

        if (registration == NULL)
            return KERNEL_THREAD_CORRUPT;
        waiter = registration_thread_at(queue->head);
        if (wake_waiter(registration, queue, result, detail,
                        write_one_detail) !=
                KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        if (irq_wake) {
            irq_wake_cycles[waiter->slot] = wake_cycle;
            irq_wake_bitmap |= (uint16_t)(1u << waiter->slot);
        }
        ++woken;
    }
    if (woken_threads != NULL)
        *woken_threads = woken;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus wake_all_profiled(KernelThreadWaitQueue *queue,
                                     uint32_t result,
                                     uint32_t detail,
                                     bool write_one_detail,
                                     bool irq_wake,
                                     uint32_t *woken_threads)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;
    KernelPerformanceMetric metric = KERNEL_PERFORMANCE_WAKE;

    if (queue != NULL && queue->count != 0u &&
        queue->head < THREAD_WAIT_REGISTRATION_COUNT) {
        KernelThreadWaitRegistration *registration =
            registration_at(queue->head);
        KernelThread *waiter = registration_thread_at(queue->head);

        if (registration != NULL && registration->queue == queue &&
            valid_thread(waiter) &&
            waiter->wait_mode == KERNEL_THREAD_WAIT_MULTIPLE)
            metric = KERNEL_PERFORMANCE_WAIT_SET_WAKE;
    }

    performance = kernel_performance_begin_sampled(metric);
    status = wake_all_fast(queue, result, detail, write_one_detail,
                           irq_wake,
                           woken_threads);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_wake_all(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          uint32_t *woken_threads)
{
    return kernel_thread_wake_all_detail(queue, result, 0u, false,
                                         woken_threads);
}

KernelThreadStatus kernel_thread_wake_all_irq(
    KernelThreadWaitQueue *queue, uint32_t result,
    uint32_t *woken_threads)
{
    if (kernel_performance_sampling_enabled == 0u)
        return wake_all_fast(queue, result, 0u, false, true,
                             woken_threads);
    return wake_all_profiled(queue, result, 0u, false, true,
                             woken_threads);
}

KernelThreadStatus kernel_thread_wake_all_detail(
    KernelThreadWaitQueue *queue, uint32_t result, uint32_t detail,
    bool write_one_detail, uint32_t *woken_threads)
{
    if (kernel_performance_sampling_enabled == 0u)
        return wake_all_fast(queue, result, detail, write_one_detail, false,
                             woken_threads);
    return wake_all_profiled(queue, result, detail, write_one_detail, false,
                             woken_threads);
}

KernelThreadStatus kernel_thread_cancel_wait(KernelThread *thread,
                                             uint32_t result)
{
    KernelThreadStatus status;

    if (!valid_thread(thread) || result == 0u)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    if (thread->state != KERNEL_THREAD_BLOCKED ||
        thread->wait_member_count == 0u)
        return KERNEL_THREAD_INVALID_STATE;
    status = complete_wait(thread, NULL, NULL, result, 0u, false);
    if (status == KERNEL_THREAD_OK)
        ++pool_stats.wait_cancellations;
    return status;
}

static __attribute__((noinline))
KernelThreadStatus expire_deadlines_fast(uint64_t now,
                                         uint32_t *expired_threads,
                                         uint8_t *highest_priority)
{
    uint32_t expired = 0u;
    uint8_t highest = 0u;

    if (!deadline_heap_valid())
        return KERNEL_THREAD_CORRUPT;
    while (deadline_count != 0u) {
        uint16_t slot = deadline_heap[0];
        KernelThread *thread = &threads[slot];
        uint32_t result;

        if (deadline_cycles[slot] > now)
            break;
        if (!valid_thread(thread) ||
            thread->state != KERNEL_THREAD_BLOCKED ||
            thread->wait_member_count == 0u)
            return KERNEL_THREAD_CORRUPT;
        result = deadline_results[slot];
        if (deadline_remove(thread) != KERNEL_THREAD_OK ||
            complete_wait(thread, NULL, NULL, result, 0u, false) !=
            KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        if (expired == 0u || thread->effective_priority > highest)
            highest = thread->effective_priority;
        ++expired;
        ++pool_stats.deadline_expirations;
    }
    if (expired_threads != NULL)
        *expired_threads = expired;
    if (highest_priority != NULL)
        *highest_priority = highest;
    return KERNEL_THREAD_OK;
}

static __attribute__((noinline))
KernelThreadStatus expire_deadlines_profiled(uint64_t now,
                                             uint32_t *expired_threads,
                                             uint8_t *highest_priority)
{
    KernelPerformanceToken performance;
    KernelThreadStatus status;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_DEADLINE_EXPIRE);
    status = expire_deadlines_fast(now, expired_threads, highest_priority);
    kernel_performance_end(performance);
    return status;
}

KernelThreadStatus kernel_thread_expire_deadlines(
    uint64_t now, uint32_t *expired_threads, uint8_t *highest_priority)
{
    if (expired_threads != NULL)
        *expired_threads = 0u;
    if (highest_priority != NULL)
        *highest_priority = 0u;
    if (kernel_performance_sampling_enabled == 0u)
        return expire_deadlines_fast(now, expired_threads,
                                     highest_priority);
    return expire_deadlines_profiled(now, expired_threads,
                                     highest_priority);
}

bool kernel_thread_next_deadline(uint64_t *deadline)
{
    uint16_t slot;

    if (deadline == NULL || deadline_count == 0u)
        return false;
    slot = deadline_heap[0];
    if (slot >= KERNEL_THREAD_MAX || deadline_positions[slot] != 0u)
        return false;
    *deadline = deadline_cycles[slot];
    return true;
}

bool kernel_thread_highest_ready_priority(uint8_t *priority)
{
    if (priority == NULL || ready_bitmap == 0u)
        return false;
    *priority = highest_ready_priority(ready_bitmap);
    return true;
}

KernelThreadStatus kernel_thread_retire_process(uint16_t process_slot,
                                                uint32_t terminal_result,
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
            clear_irq_wake(thread->slot);
        } else if (thread->state == KERNEL_THREAD_BLOCKED) {
            bool cancelled_deadline =
                deadline_positions[thread->slot] !=
                    KERNEL_THREAD_SLOT_NONE;

            if (deadline_remove(thread) != KERNEL_THREAD_OK ||
                withdraw_wait_set(thread, NULL, true) != KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
            if (cancelled_deadline)
                ++pool_stats.deadline_cancellations;
        } else if (thread->state != KERNEL_THREAD_CREATED &&
                   thread->state != KERNEL_THREAD_RUNNING) {
            return KERNEL_THREAD_CORRUPT;
        }
        thread->exit_status = 0u;
        thread->terminal_result = terminal_result;
        thread->state = KERNEL_THREAD_DEAD;
        mark_reap_pending(thread);
        if (wake_death_waiters(thread, terminal_result, NULL) !=
            KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
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
        if (thread->wait_member_count != 0u ||
            !wait_row_clear(thread->slot) ||
            deadline_positions[thread->slot] != KERNEL_THREAD_SLOT_NONE ||
            kernel_thread_wait_queue_count(&thread->death_waiters) != 0u ||
            thread->handle_references != 0u ||
            !kernel_stack_valid(thread))
            return KERNEL_THREAD_CORRUPT;
        initialize_kernel_stack(thread);
        thread->stack_released = 1u;
        clear_reap_pending(thread);
        thread->occupied = 0u;
        if (kernel_object_cache_release(&thread_cache, thread) !=
            KERNEL_OBJECT_CACHE_OK) {
            pool_corrupt = 1u;
            return KERNEL_THREAD_CORRUPT;
        }
        ++pool_stats.reaped_threads;
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
    snapshot->activity = thread->activity;
    snapshot->self_handle = thread->self_handle;
    snapshot->process_slot = thread->process_slot;
    snapshot->stack_slot = thread->stack_slot;
    snapshot->state = thread->state;
    snapshot->base_priority = thread->base_priority;
    snapshot->effective_priority = thread->effective_priority;
    snapshot->occupied = thread->occupied;
    snapshot->waiting = thread->wait_member_count != 0u ? 1u : 0u;
    snapshot->wait_members = thread->wait_member_count;
    snapshot->deadline_waiting =
        thread->occupied != 0u &&
        deadline_positions[thread->slot] != KERNEL_THREAD_SLOT_NONE ?
            1u : 0u;
    snapshot->stack_released = thread->stack_released;
    snapshot->reap_pending = thread->reap_pending;
    snapshot->stack_pages = thread->stack_pages;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    snapshot->exit_status = thread->exit_status;
    snapshot->terminal_result = thread->terminal_result;
    snapshot->handle_references = thread->handle_references;
    snapshot->death_waiters = (uint16_t)
        kernel_thread_wait_queue_count(&thread->death_waiters);
    return true;
}

bool kernel_thread_pool_stats(KernelThreadPoolStats *stats)
{
    uint32_t blocked = 0u;
    uint32_t entries = 0u;
    uint32_t max_used = 0u;
    uint32_t observed_registrations = 0u;
    uint16_t observed_reap_bitmap = 0u;

    if (stats == NULL || pool_corrupt != 0u ||
        !kernel_object_cache_valid(&thread_cache) ||
        !deadline_heap_valid())
        return false;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        const KernelThread *thread = &threads[slot];
        uint32_t used;
        bool claimed = kernel_object_cache_slot_claimed(
            &thread_cache, slot);

        if (thread->occupied == 0u) {
            if (claimed || !wait_row_clear(slot) ||
                (irq_wake_bitmap & (uint16_t)(1u << slot)) != 0u)
                return false;
            continue;
        }
        if (!claimed)
            return false;
        if ((irq_wake_bitmap & (uint16_t)(1u << slot)) != 0u &&
            thread->state != KERNEL_THREAD_READY)
            return false;
        if (thread->reap_pending != 0u)
            observed_reap_bitmap |= (uint16_t)(1u << slot);
        if (!kernel_stack_valid(thread))
            return false;
        if (thread->handle_references > 1u ||
            !valid_wait_queue(&thread->death_waiters) ||
            (thread->state != KERNEL_THREAD_DEAD &&
             thread->stack_released != 0u) ||
            (thread->state == KERNEL_THREAD_DEAD &&
             thread->reap_pending == 0u &&
             thread->stack_released == 0u))
            return false;
        if (thread->state == KERNEL_THREAD_BLOCKED) {
            if (thread->wait_member_count == 0u ||
                thread->wait_member_count > KERNEL_THREAD_WAIT_MEMBER_MAX ||
                (thread->wait_mode != KERNEL_THREAD_WAIT_ONE &&
                 thread->wait_mode != KERNEL_THREAD_WAIT_MULTIPLE))
                return false;
            for (uint16_t member = 0u;
                 member < thread->wait_member_count; ++member) {
                const KernelThreadWaitRegistration *registration =
                    &wait_registrations[slot][member];

                if (registration->queue == NULL ||
                    !valid_wait_queue(registration->queue))
                    return false;
                ++observed_registrations;
            }
            for (uint16_t member = thread->wait_member_count;
                 member < KERNEL_THREAD_WAIT_MEMBER_MAX; ++member) {
                if (wait_registrations[slot][member].queue != NULL)
                    return false;
            }
            ++blocked;
        } else if (thread->wait_member_count != 0u ||
                   thread->wait_mode != KERNEL_THREAD_WAIT_NONE ||
                   !wait_row_clear(slot)) {
            return false;
        }
        entries += thread->kernel_stack_entries;
        used = kernel_stack_observed_used(thread);
        if (used > max_used)
            max_used = used;
    }
    if (observed_reap_bitmap != reap_pending_bitmap ||
        observed_registrations != wait_registration_count)
        return false;
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
    stats->deadline_waits = pool_stats.deadline_waits;
    stats->deadline_expirations = pool_stats.deadline_expirations;
    stats->deadline_cancellations = pool_stats.deadline_cancellations;
    stats->wait_cancellations = pool_stats.wait_cancellations;
    stats->deadline_depth = deadline_count;
    stats->deadline_max_depth = pool_stats.deadline_max_depth;
    stats->thread_exits = pool_stats.thread_exits;
    stats->death_waits = pool_stats.death_waits;
    stats->death_wakeups = pool_stats.death_wakeups;
    stats->handle_close_wakeups = pool_stats.handle_close_wakeups;
    stats->reaped_threads = pool_stats.reaped_threads;
    stats->creation_rollbacks = pool_stats.creation_rollbacks;
    stats->max_death_waiters = pool_stats.max_death_waiters;
    stats->wait_set_blocks = pool_stats.wait_set_blocks;
    stats->wait_set_wakeups = pool_stats.wait_set_wakeups;
    stats->wait_registrations = wait_registration_count;
    stats->wait_registration_max = pool_stats.wait_registration_max;
    stats->max_wait_members = pool_stats.max_wait_members;
    stats->irq_wake_to_run_samples =
        pool_stats.irq_wake_to_run_samples;
    stats->irq_wake_to_run_max_cycles =
        pool_stats.irq_wake_to_run_max_cycles;
    return true;
}

bool kernel_thread_pool_valid(void)
{
    return pool_corrupt == 0u &&
           kernel_object_cache_valid(&thread_cache);
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
