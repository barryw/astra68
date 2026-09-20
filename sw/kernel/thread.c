#include "thread.h"

#include <astra/integer.h>
#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "memory.h"
#include "performance.h"
#include "vm.h"

#include <stddef.h>
#if !defined(__m68k__) && !defined(KERNEL_MEMORY_HOST_TEST)
#include <stdlib.h>
#define KERNEL_THREAD_STANDALONE_HOST 1
#endif

#define THREAD_ID_PREFIX 0x20000000u
#define THREAD_ID_VALUE_MASK (THREAD_ID_PREFIX - 1u)
#define THREAD_STACK_CANARY 0x5354414bu
#define THREAD_STACK_POISON 0xa5a5a5a5u
#define THREAD_WAIT_REGISTRATION_NONE UINT32_MAX
#define THREAD_SLOT_LEAF_BITS 8u
#define THREAD_SLOT_LEAF_ENTRIES (1u << THREAD_SLOT_LEAF_BITS)
#define THREAD_SLOT_DIRECTORY_ENTRIES (1u << (16u - THREAD_SLOT_LEAF_BITS))

typedef struct ThreadSlot {
    KernelThread *thread;
} ThreadSlot;

static ThreadSlot *thread_directory[THREAD_SLOT_DIRECTORY_ENTRIES];
static uint32_t thread_directory_physical[THREAD_SLOT_DIRECTORY_ENTRIES];
static uint32_t thread_slots;
static uint16_t next_thread_slot;
static uint32_t next_thread_id;
static uint16_t ready_head[KERNEL_THREAD_PRIORITY_LEVELS];
static uint16_t ready_tail[KERNEL_THREAD_PRIORITY_LEVELS];
static uint32_t ready_bitmap;
static uint32_t ready_count;
static uint16_t *deadline_heap;
static uint32_t deadline_heap_physical;
static uint32_t deadline_heap_owner;
static uint32_t deadline_heap_frames;
static uint32_t deadline_capacity;
static uint32_t deadline_count;
static uint32_t wait_registration_count;
static KernelThreadPoolStats pool_stats;
static uint8_t pool_corrupt;

static bool valid_thread(const KernelThread *thread);

static KernelThread *thread_at_slot(uint16_t slot)
{
    ThreadSlot *leaf = thread_directory[slot >> THREAD_SLOT_LEAF_BITS];

    return leaf != NULL ?
        leaf[slot & (THREAD_SLOT_LEAF_ENTRIES - 1u)].thread :
                          NULL;
}

static ThreadSlot *slot_at(uint16_t slot)
{
    ThreadSlot *leaf = thread_directory[slot >> THREAD_SLOT_LEAF_BITS];

    return leaf != NULL ?
        &leaf[slot & (THREAD_SLOT_LEAF_ENTRIES - 1u)] : NULL;
}

static void clear_irq_wake(uint16_t slot)
{
    KernelThread *thread = thread_at_slot(slot);

    if (thread != NULL) {
        thread->irq_wake_pending = 0u;
        thread->irq_wake_cycles = 0u;
    }
}

static void mark_reap_pending(KernelThread *thread)
{
    thread->reap_pending = 1u;
}

static void clear_reap_pending(KernelThread *thread)
{
    thread->reap_pending = 0u;
}

static bool ensure_thread_leaf(uint16_t slot)
{
    uint32_t directory = slot >> THREAD_SLOT_LEAF_BITS;
    ThreadSlot *leaf;

    if (thread_directory[directory] != NULL)
        return true;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    leaf = calloc(THREAD_SLOT_LEAF_ENTRIES, sizeof(*leaf));
    if (leaf == NULL)
        return false;
    thread_directory_physical[directory] = 0u;
#else
    uint32_t physical;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_MEMORY_GENERIC, 1u, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return false;
    leaf = kernel_memory_access(physical, KERNEL_PAGE_SIZE);
    if (leaf == NULL) {
        (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
        return false;
    }
    thread_directory_physical[directory] = physical;
#endif
    thread_directory[directory] = leaf;
    return true;
}

static bool release_empty_thread_leaf(uint16_t slot)
{
    uint32_t directory = slot >> THREAD_SLOT_LEAF_BITS;
    ThreadSlot *leaf = thread_directory[directory];

    if (leaf == NULL)
        return true;
    for (uint32_t index = 0u; index < THREAD_SLOT_LEAF_ENTRIES; ++index) {
        if (leaf[index].thread != NULL)
            return true;
    }
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    free(leaf);
#else
    if (kernel_memory_release(thread_directory_physical[directory], 1u,
                              KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
        return false;
#endif
    thread_directory[directory] = NULL;
    thread_directory_physical[directory] = 0u;
    return true;
}

static bool allocate_thread_identity(uint32_t *id, uint32_t *generation)
{
    if (id == NULL || generation == NULL)
        return false;
    for (uint32_t attempt = 0u; attempt < THREAD_ID_VALUE_MASK; ++attempt) {
        bool used = false;

        next_thread_id = (next_thread_id % THREAD_ID_VALUE_MASK) + 1u;
        *id = THREAD_ID_PREFIX | next_thread_id;
        for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
            KernelThread *thread = thread_at_slot((uint16_t)slot);

            if (thread != NULL && thread->id == *id) {
                used = true;
                break;
            }
        }
        if (!used) {
            *generation = next_thread_id;
            return true;
        }
    }
    return false;
}

static KernelThread *allocate_thread_record(uint32_t owner)
{
    KernelThread *thread;

#if defined(KERNEL_THREAD_STANDALONE_HOST)
    (void)owner;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_THREAD_RECORD,
                                   owner))
        return NULL;
    thread = calloc(1u, sizeof(*thread));
    if (thread == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_THREAD_RECORD, owner);
        return NULL;
    }
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_THREAD_RECORD, 1u,
                                  sizeof(*thread), owner)) {
        free(thread);
        return NULL;
    }
    thread->record_frames = 0u;
    thread->record_physical = 0u;
#else
    uint32_t frames = ((uint32_t)sizeof(*thread) + KERNEL_PAGE_SIZE - 1u) /
                      KERNEL_PAGE_SIZE;
    uint32_t physical;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_THREAD_RECORD, frames, 1u,
            KERNEL_FRAME_KERNEL, owner, &physical) != KERNEL_MEMORY_OK)
        return NULL;
    thread = kernel_memory_access(physical, frames * KERNEL_PAGE_SIZE);
    if (thread == NULL) {
        (void)kernel_memory_release(physical, frames, owner);
        return NULL;
    }
    thread->record_frames = (uint16_t)frames;
    thread->record_physical = physical;
#endif
    thread->resource_owner = owner;
    return thread;
}

static bool release_thread_record(KernelThread *thread)
{
    if (thread == NULL)
        return false;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_THREAD_RECORD,
                                   1u, sizeof(KernelThread)))
        return false;
    free(thread);
    return true;
#else
    return thread->record_frames != 0u &&
           kernel_memory_release(thread->record_physical,
                                 thread->record_frames,
                                 thread->resource_owner) == KERNEL_MEMORY_OK;
#endif
}

_Static_assert(offsetof(KernelThread, context) == 0u,
               "thread context must remain the first field");
_Static_assert(offsetof(KernelThread, kernel_stack_top) ==
                   KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET,
               "assembly thread stack offset changed");
_Static_assert(sizeof(KernelThread) <= KERNEL_PAGE_SIZE,
               "one thread record must fit in one metadata page");
_Static_assert(sizeof(ThreadSlot) * THREAD_SLOT_LEAF_ENTRIES <=
                   KERNEL_PAGE_SIZE,
               "one thread directory leaf must fit in one metadata page");
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
    return KERNEL_THREAD_SUPERVISOR_ARENA_BASE +
           (uint32_t)slot * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE;
}

static uint32_t *kernel_stack_words(const KernelThread *thread)
{
    return thread != NULL ? thread->stack_storage : NULL;
}

static bool allocate_kernel_stack(KernelThread *thread)
{
    if (thread == NULL)
        return false;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    if (!kernel_allocation_attempt(
                                   KERNEL_ALLOCATION_SITE_THREAD_KERNEL_STACK,
                                   thread->resource_owner))
        return false;
    thread->stack_storage = calloc(1u, KERNEL_THREAD_SUPERVISOR_STACK_SIZE);
    if (thread->stack_storage == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_THREAD_KERNEL_STACK,
                               thread->resource_owner);
        return false;
    }
    if (!kernel_allocation_commit(
            KERNEL_ALLOCATION_SITE_THREAD_KERNEL_STACK,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE,
            thread->resource_owner)) {
        free(thread->stack_storage);
        thread->stack_storage = NULL;
        return false;
    }
#else
    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_THREAD_KERNEL_STACK,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE, 1u,
            KERNEL_FRAME_KERNEL, thread->resource_owner,
            &thread->stack_physical) != KERNEL_MEMORY_OK)
        return false;
    thread->stack_storage = kernel_memory_access(
        thread->stack_physical, KERNEL_THREAD_SUPERVISOR_STACK_SIZE);
    if (thread->stack_storage == NULL) {
        (void)kernel_memory_release(
            thread->stack_physical,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE,
            thread->resource_owner);
        thread->stack_physical = 0u;
        return false;
    }
#if defined(__m68k__)
    if (kernel_vm_map_supervisor_stack(thread->slot,
                                       thread->stack_physical) !=
        KERNEL_VM_OK) {
        (void)kernel_memory_release(
            thread->stack_physical,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE,
            thread->resource_owner);
        thread->stack_storage = NULL;
        thread->stack_physical = 0u;
        return false;
    }
#endif
#endif
    return true;
}

static bool release_kernel_stack(KernelThread *thread)
{
    if (thread == NULL || thread->stack_storage == NULL)
        return false;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    free(thread->stack_storage);
    thread->stack_storage = NULL;
    return kernel_allocation_release(
        KERNEL_ALLOCATION_SITE_THREAD_KERNEL_STACK,
        KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE,
        KERNEL_THREAD_SUPERVISOR_STACK_SIZE);
#else
#if defined(__m68k__)
    if (kernel_vm_unmap_supervisor_stack(thread->slot) != KERNEL_VM_OK)
        return false;
#endif
    if (kernel_memory_release(
            thread->stack_physical,
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE,
            thread->resource_owner) != KERNEL_MEMORY_OK)
        return false;
    thread->stack_storage = NULL;
    thread->stack_physical = 0u;
    return true;
#endif
}

static bool initialize_kernel_stack(KernelThread *thread)
{
    uint32_t *words = kernel_stack_words(thread);
    uint32_t word_count = KERNEL_THREAD_SUPERVISOR_STACK_SIZE /
                          sizeof(uint32_t);

    if (words == NULL)
        return false;
    kernel_words_fill(words, word_count, THREAD_STACK_POISON);
    words[0] = THREAD_STACK_CANARY;
    thread->kernel_stack_base =
        kernel_stack_guard_address(thread->slot) +
        KERNEL_THREAD_SUPERVISOR_GUARD_SIZE;
    thread->kernel_stack_top = thread->kernel_stack_base +
                               KERNEL_THREAD_SUPERVISOR_STACK_SIZE;
    thread->kernel_stack_low_water = thread->kernel_stack_top;
    thread->kernel_stack_entries = 0u;
    return true;
}

static bool kernel_stack_valid(const KernelThread *thread)
{
    const uint32_t *words;

    if (!valid_thread(thread))
        return false;
    words = kernel_stack_words(thread);
    return words != NULL && words[0] == THREAD_STACK_CANARY &&
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
    const uint32_t *words = kernel_stack_words(thread);
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
    return thread != NULL && thread_at_slot(thread->slot) == thread &&
           thread->occupied != 0u;
}

static bool deadline_precedes(uint16_t left, uint16_t right)
{
    const KernelThread *left_thread = thread_at_slot(left);
    const KernelThread *right_thread = thread_at_slot(right);

    if (left_thread == NULL || right_thread == NULL)
        return left < right;
    if (left_thread->deadline_cycles != right_thread->deadline_cycles)
        return left_thread->deadline_cycles < right_thread->deadline_cycles;
    return left < right;
}

static void deadline_swap(uint16_t left, uint16_t right)
{
    uint16_t left_slot = deadline_heap[left];
    uint16_t right_slot = deadline_heap[right];

    deadline_heap[left] = right_slot;
    deadline_heap[right] = left_slot;
    thread_at_slot(left_slot)->deadline_position = right;
    thread_at_slot(right_slot)->deadline_position = left;
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

static bool deadline_reserve(uint32_t needed)
{
    uint32_t bytes;
    uint32_t frames;
    uint16_t *replacement;

    if (needed <= deadline_capacity)
        return true;
    if (needed > UINT16_MAX || needed > UINT32_MAX / sizeof(*deadline_heap))
        return false;
    bytes = needed * (uint32_t)sizeof(*deadline_heap);
    frames = (bytes + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
    if (frames < deadline_heap_frames * 2u)
        frames = deadline_heap_frames * 2u;
    if (frames == 0u)
        frames = 1u;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    replacement = realloc(deadline_heap, frames * KERNEL_PAGE_SIZE);
    if (replacement == NULL)
        return false;
#else
    uint32_t physical;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_MEMORY_GENERIC, frames, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return false;
    replacement = kernel_memory_access(physical, frames * KERNEL_PAGE_SIZE);
    if (replacement == NULL) {
        (void)kernel_memory_release(physical, frames, KERNEL_OWNER_CORE);
        return false;
    }
    for (uint32_t index = 0u; index < deadline_count; ++index)
        replacement[index] = deadline_heap[index];
    if (deadline_heap_physical != 0u &&
        kernel_memory_release(deadline_heap_physical, deadline_heap_frames,
                              deadline_heap_owner) != KERNEL_MEMORY_OK) {
        (void)kernel_memory_release(physical, frames, KERNEL_OWNER_CORE);
        return false;
    }
    deadline_heap_physical = physical;
    deadline_heap_owner = KERNEL_OWNER_CORE;
#endif
    deadline_heap = replacement;
    deadline_heap_frames = frames;
    deadline_capacity = frames * KERNEL_PAGE_SIZE /
                        (uint32_t)sizeof(*deadline_heap);
    return true;
}

static bool deadline_release_empty(void)
{
    if (deadline_count != 0u || deadline_heap == NULL)
        return true;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    free(deadline_heap);
#else
    if (kernel_memory_release(deadline_heap_physical,
                              deadline_heap_frames,
                              deadline_heap_owner) != KERNEL_MEMORY_OK)
        return false;
#endif
    deadline_heap = NULL;
    deadline_capacity = 0u;
    deadline_heap_physical = 0u;
    deadline_heap_owner = 0u;
    deadline_heap_frames = 0u;
    return true;
}

static KernelThreadStatus deadline_insert(KernelThread *thread,
                                          uint64_t deadline,
                                          uint32_t timeout_result)
{
    uint16_t position;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_BLOCKED ||
        deadline == KERNEL_THREAD_DEADLINE_NEVER ||
        thread->deadline_position != KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_INVALID_STATE;
    if (!deadline_reserve(deadline_count + 1u))
        return KERNEL_THREAD_NO_SLOT;

    position = (uint16_t)deadline_count++;
    thread->deadline_cycles = deadline;
    thread->deadline_result = timeout_result;
    deadline_heap[position] = thread->slot;
    thread->deadline_position = position;
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
    position = thread->deadline_position;
    if (position == KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_OK;
    if (position >= deadline_count ||
        deadline_heap[position] != thread->slot)
        return KERNEL_THREAD_CORRUPT;

    --deadline_count;
    replacement = deadline_heap[deadline_count];
    deadline_heap[deadline_count] = KERNEL_THREAD_SLOT_NONE;
    thread->deadline_position = KERNEL_THREAD_SLOT_NONE;
    thread->deadline_cycles = 0u;
    thread->deadline_result = 0u;
    if (deadline_count == 0u)
        return deadline_release_empty() ? KERNEL_THREAD_OK :
                                          KERNEL_THREAD_CORRUPT;
    if (position == deadline_count)
        return KERNEL_THREAD_OK;

    deadline_heap[position] = replacement;
    thread_at_slot(replacement)->deadline_position = position;
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
    if (deadline_count > deadline_capacity)
        return false;
    for (uint32_t position = 0u; position < deadline_count; ++position) {
        uint16_t slot = deadline_heap[position];
        uint32_t left = position * 2u + 1u;
        uint32_t right = left + 1u;
        KernelThread *thread = thread_at_slot(slot);

        if (!valid_thread(thread) ||
            thread->deadline_position != position ||
            thread->state != KERNEL_THREAD_BLOCKED ||
            thread->wait_member_count == 0u ||
            thread->deadline_cycles == KERNEL_THREAD_DEADLINE_NEVER)
            return false;
        for (uint32_t prior = 0u; prior < position; ++prior) {
            if (deadline_heap[prior] == slot)
                return false;
        }
        if (left < deadline_count &&
            deadline_precedes(deadline_heap[left], slot))
            return false;
        if (right < deadline_count &&
            deadline_precedes(deadline_heap[right], slot))
            return false;
    }
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL &&
            thread->deadline_position != KERNEL_THREAD_SLOT_NONE &&
            (thread->deadline_position >= deadline_count ||
             deadline_heap[thread->deadline_position] != slot))
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
        thread->suspended != 0u ||
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
        KernelThread *previous = thread_at_slot(tail);

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
        KernelThread *previous_thread = thread_at_slot(previous);

        if (!valid_thread(previous_thread) ||
            previous_thread->ready_next != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        previous_thread->ready_next = next;
    }
    if (next == KERNEL_THREAD_SLOT_NONE) {
        if (ready_tail[priority] != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        ready_tail[priority] = previous;
    } else {
        KernelThread *next_thread = thread_at_slot(next);

        if (!valid_thread(next_thread) ||
            next_thread->ready_previous != thread->slot)
            return KERNEL_THREAD_CORRUPT;
        next_thread->ready_previous = previous;
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

static KernelThreadWaitRegistration *registration_at(uint32_t identifier)
{
    uint32_t thread_slot;
    uint32_t member;
    KernelThread *thread;

    if (identifier == THREAD_WAIT_REGISTRATION_NONE)
        return NULL;
    thread_slot = identifier / KERNEL_THREAD_WAIT_MEMBER_MAX;
    member = identifier % KERNEL_THREAD_WAIT_MEMBER_MAX;
    if (thread_slot >= KERNEL_THREAD_SLOT_NONE)
        return NULL;
    thread = thread_at_slot((uint16_t)thread_slot);
    return valid_thread(thread) ? &thread->wait_registrations[member] : NULL;
}

static KernelThread *registration_thread_at(uint32_t identifier)
{
    KernelThreadWaitRegistration *registration = registration_at(identifier);

    return registration != NULL ?
        thread_at_slot(registration->thread_slot) : NULL;
}

static uint16_t registration_member_at(uint32_t identifier)
{
    KernelThreadWaitRegistration *registration = registration_at(identifier);

    return registration != NULL ? registration->member : UINT16_MAX;
}

static uint32_t registration_identifier(
    const KernelThreadWaitRegistration *registration)
{
    KernelThread *thread;

    if (registration == NULL ||
        registration->thread_slot == KERNEL_THREAD_SLOT_NONE ||
        registration->member >= KERNEL_THREAD_WAIT_MEMBER_MAX)
        return THREAD_WAIT_REGISTRATION_NONE;
    thread = thread_at_slot(registration->thread_slot);
    if (!valid_thread(thread) ||
        &thread->wait_registrations[registration->member] != registration)
        return THREAD_WAIT_REGISTRATION_NONE;
    return (uint32_t)registration->thread_slot *
               KERNEL_THREAD_WAIT_MEMBER_MAX +
           registration->member;
}

static KernelThread *registration_thread(
    const KernelThreadWaitRegistration *registration)
{
    uint32_t identifier = registration_identifier(registration);

    return identifier == THREAD_WAIT_REGISTRATION_NONE ? NULL :
        thread_at_slot(registration->thread_slot);
}

static uint16_t registration_member(
    const KernelThreadWaitRegistration *registration)
{
    uint32_t identifier = registration_identifier(registration);

    return identifier == THREAD_WAIT_REGISTRATION_NONE ? UINT16_MAX :
        registration->member;
}

static bool valid_wait_queue_header(const KernelThreadWaitQueue *queue)
{
    if (queue == NULL || queue->count > wait_registration_count)
        return false;
    if (queue->count == 0u)
        return queue->head == THREAD_WAIT_REGISTRATION_NONE &&
               queue->tail == THREAD_WAIT_REGISTRATION_NONE;
    return registration_at(queue->head) != NULL &&
           registration_at(queue->tail) != NULL;
}

static bool valid_wait_queue(const KernelThreadWaitQueue *queue)
{
    uint32_t identifier;
    uint32_t previous = THREAD_WAIT_REGISTRATION_NONE;
    uint32_t traversed = 0u;

    if (!valid_wait_queue_header(queue))
        return false;
    if (queue->count == 0u)
        return true;

    identifier = queue->head;
    while (identifier != THREAD_WAIT_REGISTRATION_NONE) {
        KernelThreadWaitRegistration *registration;
        KernelThread *thread;
        uint16_t member;

        if (traversed >= queue->count)
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
    KernelThread *thread = thread_at_slot(thread_slot);

    return valid_thread(thread) && thread->wait_registration_count == 0u;
}

static bool wait_row_valid(uint16_t thread_slot)
{
    uint16_t occupied = 0u;

    KernelThread *thread = thread_at_slot(thread_slot);

    if (!valid_thread(thread))
        return false;
    for (uint16_t member = 0u; member < KERNEL_THREAD_WAIT_MEMBER_MAX;
         ++member) {
        const KernelThreadWaitRegistration *registration =
            &thread->wait_registrations[member];

        if (registration->queue == NULL) {
            if (registration->previous != THREAD_WAIT_REGISTRATION_NONE ||
                registration->next != THREAD_WAIT_REGISTRATION_NONE)
                return false;
        } else {
            ++occupied;
        }
    }
    return occupied == thread->wait_registration_count;
}

static void reset_wait_row(KernelThread *thread)
{
    thread->wait_registration_count = 0u;
    for (uint16_t member = 0u; member < KERNEL_THREAD_WAIT_MEMBER_MAX;
         ++member) {
        KernelThreadWaitRegistration *registration =
            &thread->wait_registrations[member];

        registration->queue = NULL;
        registration->previous = THREAD_WAIT_REGISTRATION_NONE;
        registration->next = THREAD_WAIT_REGISTRATION_NONE;
        registration->thread_slot = thread->slot;
        registration->member = member;
    }
}

static KernelThreadStatus enqueue_wait_registration(
    KernelThread *thread, uint16_t member, KernelThreadWaitQueue *queue)
{
    KernelThreadWaitRegistration *registration;
    uint32_t previous = THREAD_WAIT_REGISTRATION_NONE;
    uint32_t next;
    uint32_t identifier;

    if (!valid_thread(thread) || !valid_wait_queue_header(queue) ||
        thread->state != KERNEL_THREAD_BLOCKED ||
        member >= thread->wait_member_count ||
        queue->count == UINT32_MAX ||
        thread->wait_registration_count >= KERNEL_THREAD_WAIT_MEMBER_MAX)
        return KERNEL_THREAD_INVALID_STATE;
    registration = &thread->wait_registrations[member];
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
    ++thread->wait_registration_count;
    if (wait_registration_count > pool_stats.wait_registration_max)
        pool_stats.wait_registration_max = wait_registration_count;
    return KERNEL_THREAD_OK;
}

static KernelThreadStatus remove_wait_registration(
    KernelThreadWaitRegistration *registration)
{
    KernelThreadWaitRegistration *after = NULL;
    KernelThreadWaitRegistration *before = NULL;
    KernelThreadWaitQueue *queue;
    uint32_t identifier;
    uint32_t previous;
    uint32_t next;
    KernelThread *thread;

    identifier = registration_identifier(registration);
    if (identifier == THREAD_WAIT_REGISTRATION_NONE ||
        registration->queue == NULL)
        return KERNEL_THREAD_INVALID_STATE;
    thread = registration_thread(registration);
    if (!valid_thread(thread) || thread->wait_registration_count == 0u)
        return KERNEL_THREAD_CORRUPT;
    queue = registration->queue;
    if (registration != registration_at(identifier) ||
        !valid_wait_queue_header(queue) || queue->count == 0u ||
        wait_registration_count == 0u)
        return KERNEL_THREAD_CORRUPT;
    previous = registration->previous;
    next = registration->next;

    if (previous == THREAD_WAIT_REGISTRATION_NONE) {
        if (queue->head != identifier)
            return KERNEL_THREAD_CORRUPT;
    } else {
        before = registration_at(previous);

        if (before == NULL || before->queue != queue ||
            before->next != identifier)
            return KERNEL_THREAD_CORRUPT;
    }
    if (next == THREAD_WAIT_REGISTRATION_NONE) {
        if (queue->tail != identifier)
            return KERNEL_THREAD_CORRUPT;
    } else {
        after = registration_at(next);

        if (after == NULL || after->queue != queue ||
            after->previous != identifier)
            return KERNEL_THREAD_CORRUPT;
    }

    if (before == NULL)
        queue->head = next;
    else
        before->next = next;
    if (after == NULL)
        queue->tail = previous;
    else
        after->previous = previous;
    --queue->count;
    --wait_registration_count;
    --thread->wait_registration_count;
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
            thread->wait_registrations[member].queue;
        bool seen = queue == already_advanced;

        if (queue == NULL)
            return KERNEL_THREAD_CORRUPT;
        for (uint16_t prior = 0u; prior < member && !seen; ++prior)
            seen = thread->wait_registrations[prior].queue == queue;
        if (!seen) {
            if (!valid_wait_queue_header(queue))
                return KERNEL_THREAD_CORRUPT;
            if (advance_sequences)
                queue->sequence = kernel_generation_next(queue->sequence);
        }
    }
    for (uint16_t member = 0u; member < member_count; ++member) {
        if (remove_wait_registration(
                &thread->wait_registrations[member]) !=
            KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
    }
    thread->wait_member_count = 0u;
    thread->wait_mode = KERNEL_THREAD_WAIT_NONE;
    thread->wait_registration_count = 0u;
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
        thread->deadline_position != KERNEL_THREAD_SLOT_NONE;
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
    if (thread->suspended != 0u)
        return KERNEL_THREAD_OK;
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
    if (!valid_wait_queue_header(queue))
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
    for (uint32_t index = 0u; index < thread_slots; ++index) {
        KernelThread *thread = thread_at_slot((uint16_t)index);

        if (thread == NULL)
            continue;
        (void)release_kernel_stack(thread);
        thread_directory[index >> THREAD_SLOT_LEAF_BITS]
                        [index & (THREAD_SLOT_LEAF_ENTRIES - 1u)].thread =
            NULL;
        (void)release_thread_record(thread);
    }
    for (uint32_t index = 0u; index < THREAD_SLOT_DIRECTORY_ENTRIES;
         ++index) {
        if (thread_directory[index] == NULL)
            continue;
#if defined(KERNEL_THREAD_STANDALONE_HOST)
        free(thread_directory[index]);
#else
        (void)kernel_memory_release(thread_directory_physical[index], 1u,
                                    KERNEL_OWNER_CORE);
#endif
        thread_directory[index] = NULL;
        thread_directory_physical[index] = 0u;
    }
#if defined(KERNEL_THREAD_STANDALONE_HOST)
    free(deadline_heap);
#else
    if (deadline_heap_physical != 0u)
        (void)kernel_memory_release(deadline_heap_physical,
                                    deadline_heap_frames,
                                    deadline_heap_owner);
#endif
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
    deadline_capacity = 0u;
    deadline_heap = NULL;
    deadline_heap_physical = 0u;
    deadline_heap_owner = 0u;
    deadline_heap_frames = 0u;
    wait_registration_count = 0u;
    thread_slots = 0u;
    next_thread_slot = 0u;
    next_thread_id = 0u;
}

KernelThreadStatus kernel_thread_allocate(uint16_t process_slot,
                                          uint32_t process_id,
                                          uint32_t resource_owner,
                                          uint16_t stack_slot,
                                          uint32_t program_counter,
                                          uint32_t user_stack,
                                          uint32_t initial_argument,
                                          uint8_t priority,
                                          KernelThread **thread)
{
    KernelThread *candidate = NULL;
    ThreadSlot *registry_slot;
    uint16_t candidate_slot = KERNEL_THREAD_SLOT_NONE;
    uint32_t generation;

    if (process_id == 0u || program_counter == 0u ||
        user_stack < KERNEL_THREAD_STACK_SIZE ||
        (user_stack & 3u) != 0u ||
        priority >= KERNEL_THREAD_PRIORITY_LEVELS || thread == NULL)
        return KERNEL_THREAD_INVALID_ARGUMENT;
    *thread = NULL;
    for (uint32_t attempt = 0u; attempt < KERNEL_THREAD_SLOT_NONE;
         ++attempt) {
        uint16_t slot = next_thread_slot;

        ++next_thread_slot;
        if (next_thread_slot == KERNEL_THREAD_SLOT_NONE)
            next_thread_slot = 0u;
        if (thread_at_slot(slot) == NULL) {
            candidate_slot = slot;
            break;
        }
    }
    if (candidate_slot == KERNEL_THREAD_SLOT_NONE)
        return KERNEL_THREAD_NO_SLOT;
    if (!ensure_thread_leaf(candidate_slot))
        return KERNEL_THREAD_OUT_OF_MEMORY;
    registry_slot = slot_at(candidate_slot);
    if (registry_slot == NULL || registry_slot->thread != NULL)
        return KERNEL_THREAD_CORRUPT;
    candidate = allocate_thread_record(resource_owner);
    if (candidate == NULL) {
        (void)release_empty_thread_leaf(candidate_slot);
        next_thread_slot = candidate_slot;
        return KERNEL_THREAD_OUT_OF_MEMORY;
    }

    if (!allocate_thread_identity(&candidate->id, &generation)) {
        (void)release_thread_record(candidate);
        (void)release_empty_thread_leaf(candidate_slot);
        next_thread_slot = candidate_slot;
        return KERNEL_THREAD_NO_SLOT;
    }
    candidate->generation = generation;
    candidate->slot = candidate_slot;
    /*
     * Prefix, generation, slot. The slot field covers the physical host-channel
     * aperture, so every system thread has a distinct identifier.
     */
    candidate->process_id = process_id;
    candidate->process_slot = process_slot;
    candidate->stack_slot = stack_slot;
    candidate->user_stack_top = user_stack;
    candidate->user_stack_base = user_stack - KERNEL_THREAD_STACK_SIZE;
    candidate->stack_pages =
        (uint16_t)(KERNEL_THREAD_STACK_SIZE / KERNEL_PAGE_SIZE);
    candidate->ready_previous = KERNEL_THREAD_SLOT_NONE;
    candidate->ready_next = KERNEL_THREAD_SLOT_NONE;
    /* Slot release validates the complete wait row before clearing occupied. */
    candidate->state = KERNEL_THREAD_CREATED;
    candidate->base_priority = priority;
    candidate->effective_priority = priority;
    candidate->occupied = 1u;
    candidate->deadline_position = KERNEL_THREAD_SLOT_NONE;
    reset_wait_row(candidate);
    kernel_thread_wait_queue_init(&candidate->death_waiters);
    if (!allocate_kernel_stack(candidate)) {
        candidate->occupied = 0u;
        (void)release_thread_record(candidate);
        (void)release_empty_thread_leaf(candidate_slot);
        next_thread_slot = candidate_slot;
        return KERNEL_THREAD_OUT_OF_MEMORY;
    }
    registry_slot->thread = candidate;
    if ((uint32_t)candidate_slot + 1u > thread_slots)
        thread_slots = (uint32_t)candidate_slot + 1u;
    if (!initialize_kernel_stack(candidate)) {
        candidate->occupied = 0u;
        registry_slot->thread = NULL;
        (void)release_kernel_stack(candidate);
        (void)release_thread_record(candidate);
        (void)release_empty_thread_leaf(candidate_slot);
        next_thread_slot = candidate_slot;
        return KERNEL_THREAD_CORRUPT;
    }
    kernel_context_initialize(&candidate->context, program_counter,
                              user_stack);
    candidate->context.data[2] = initial_argument;
    if (!kernel_context_valid(&candidate->context)) {
        candidate->state = KERNEL_THREAD_DEAD;
        candidate->occupied = 0u;
        registry_slot->thread = NULL;
        if (!release_kernel_stack(candidate) ||
            !release_thread_record(candidate))
            pool_corrupt = 1u;
        if (!release_empty_thread_leaf(candidate_slot))
            pool_corrupt = 1u;
        next_thread_slot = candidate_slot;
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
    ThreadSlot *registry_slot;
    uint16_t released_slot;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_CREATED ||
        thread->handle_references != 0u ||
        thread->wait_member_count != 0u ||
        kernel_thread_wait_queue_count(&thread->death_waiters) != 0u ||
        !wait_row_clear(thread->slot))
        return KERNEL_THREAD_INVALID_STATE;
    if (!initialize_kernel_stack(thread))
        return KERNEL_THREAD_CORRUPT;
    thread->state = KERNEL_THREAD_DEAD;
    registry_slot = slot_at(thread->slot);
    if (registry_slot == NULL || registry_slot->thread != thread)
        return KERNEL_THREAD_CORRUPT;
    thread->occupied = 0u;
    released_slot = thread->slot;
    registry_slot->thread = NULL;
    if (!release_kernel_stack(thread) || !release_thread_record(thread)) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    if (!release_empty_thread_leaf(released_slot)) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    if (released_slot < next_thread_slot)
        next_thread_slot = released_slot;
    while (thread_slots != 0u &&
           thread_at_slot((uint16_t)(thread_slots - 1u)) == NULL)
        --thread_slots;
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
    ThreadSlot *registry_slot;
    uint16_t released_slot;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_DEAD ||
        thread->reap_pending == 0u ||
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
    if (!initialize_kernel_stack(thread))
        return KERNEL_THREAD_CORRUPT;
    clear_reap_pending(thread);
    registry_slot = slot_at(thread->slot);
    if (registry_slot == NULL || registry_slot->thread != thread)
        return KERNEL_THREAD_CORRUPT;
    thread->occupied = 0u;
    released_slot = thread->slot;
    registry_slot->thread = NULL;
    if (!release_kernel_stack(thread) || !release_thread_record(thread)) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    if (!release_empty_thread_leaf(released_slot)) {
        pool_corrupt = 1u;
        return KERNEL_THREAD_CORRUPT;
    }
    if (released_slot < next_thread_slot)
        next_thread_slot = released_slot;
    while (thread_slots != 0u &&
           thread_at_slot((uint16_t)(thread_slots - 1u)) == NULL)
        --thread_slots;
    ++pool_stats.reaped_threads;
    *released = true;
    return KERNEL_THREAD_OK;
}

bool kernel_thread_reap_pending(void)
{
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL && thread->reap_pending != 0u)
            return true;
    }
    return false;
}

KernelThreadStatus kernel_thread_make_ready(KernelThread *thread)
{
    KernelThreadStatus status;
    uint8_t previous_state;

    if (!valid_thread(thread) || thread->state != KERNEL_THREAD_RUNNING ||
        thread->suspended != 0u)
        return KERNEL_THREAD_INVALID_STATE;
    previous_state = thread->state;
    thread->state = KERNEL_THREAD_READY;
    status = enqueue_ready(thread);
    if (status != KERNEL_THREAD_OK)
        thread->state = previous_state;
    return status;
}

KernelThreadStatus
kernel_thread_set_process_priority(uint16_t process_slot, uint8_t priority)
{
    if (priority >= KERNEL_THREAD_PRIORITY_LEVELS)
        return KERNEL_THREAD_INVALID_ARGUMENT;

    /* Validate the complete process before moving anything between queues. */
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        if (thread->state < KERNEL_THREAD_CREATED ||
            thread->state > KERNEL_THREAD_BLOCKED ||
            thread->base_priority != thread->effective_priority)
            return KERNEL_THREAD_CORRUPT;
    }

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThreadWaitQueue *queues[KERNEL_THREAD_WAIT_MEMBER_MAX];
        KernelThread *thread = thread_at_slot((uint16_t)slot);
        uint8_t members;

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD ||
            thread->base_priority == priority)
            continue;
        if (thread->state == KERNEL_THREAD_READY &&
            thread->suspended == 0u &&
            remove_ready(thread) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        members = thread->state == KERNEL_THREAD_BLOCKED ?
            thread->wait_member_count : 0u;
        for (uint16_t member = 0u; member < members; ++member) {
            queues[member] = thread->wait_registrations[member].queue;
            if (queues[member] == NULL ||
                remove_wait_registration(
                    &thread->wait_registrations[member]) != KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
        }
        thread->base_priority = priority;
        thread->effective_priority = priority;
        if (thread->state == KERNEL_THREAD_READY &&
            thread->suspended == 0u &&
            enqueue_ready(thread) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        for (uint16_t member = 0u; member < members; ++member) {
            if (enqueue_wait_registration(thread, member, queues[member]) !=
                KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
        }
    }
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_suspend_process(uint16_t process_slot)
{
    bool found = false;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        found = true;
        if (thread->suspended != 0u ||
            (thread->state != KERNEL_THREAD_READY &&
             thread->state != KERNEL_THREAD_RUNNING &&
             thread->state != KERNEL_THREAD_BLOCKED))
            return KERNEL_THREAD_INVALID_STATE;
    }
    if (!found)
        return KERNEL_THREAD_INVALID_STATE;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        if (thread->state == KERNEL_THREAD_READY &&
            remove_ready(thread) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        if (thread->state == KERNEL_THREAD_RUNNING)
            thread->state = KERNEL_THREAD_READY;
        thread->suspended = 1u;
        clear_irq_wake(thread->slot);
    }
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_resume_process(uint16_t process_slot)
{
    bool found = false;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        found = true;
        if (thread->suspended == 0u ||
            (thread->state != KERNEL_THREAD_READY &&
             thread->state != KERNEL_THREAD_BLOCKED))
            return KERNEL_THREAD_INVALID_STATE;
    }
    if (!found)
        return KERNEL_THREAD_INVALID_STATE;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL || thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD)
            continue;
        thread->suspended = 0u;
        if (thread->state == KERNEL_THREAD_READY &&
            enqueue_ready(thread) != KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
    }
    return KERNEL_THREAD_OK;
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
    next = thread_at_slot(slot);
    if (!valid_thread(next))
        return KERNEL_THREAD_CORRUPT;
    status = remove_ready(next);
    if (status != KERNEL_THREAD_OK)
        return status;
    next->state = KERNEL_THREAD_RUNNING;
    if (next->irq_wake_pending != 0u) {
        uint32_t elapsed = kernel_performance_cycles_low() -
                           next->irq_wake_cycles;

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
    queue->head = THREAD_WAIT_REGISTRATION_NONE;
    queue->tail = THREAD_WAIT_REGISTRATION_NONE;
    queue->count = 0u;
}

uint32_t kernel_thread_wait_queue_sequence(
    const KernelThreadWaitQueue *queue)
{
    return valid_wait_queue_header(queue) ? queue->sequence : 0u;
}

uint32_t kernel_thread_wait_queue_count(const KernelThreadWaitQueue *queue)
{
    return valid_wait_queue_header(queue) ? queue->count : UINT32_MAX;
}

uint32_t kernel_thread_wait_queue_waiter_count(
    const KernelThreadWaitQueue *queue)
{
    uint32_t identifier;
    uint32_t count = 0u;

    if (!valid_wait_queue(queue))
        return UINT32_MAX;
    identifier = queue->head;
    while (identifier != THREAD_WAIT_REGISTRATION_NONE) {
        KernelThreadWaitRegistration *registration =
            registration_at(identifier);
        KernelThread *thread = registration_thread_at(identifier);
        uint32_t prior = queue->head;
        bool seen = false;

        if (registration == NULL || !valid_thread(thread))
            return UINT32_MAX;
        while (prior != identifier) {
            KernelThreadWaitRegistration *prior_registration =
                registration_at(prior);

            if (prior_registration == NULL)
                return UINT32_MAX;
            if (registration_thread_at(prior) == thread) {
                seen = true;
                break;
            }
            prior = prior_registration->next;
        }
        if (!seen)
            ++count;
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

        if (!valid_wait_queue_header(specs[member].queue))
            return KERNEL_THREAD_INVALID_STATE;
        if (specs[member].queue->sequence != specs[member].sequence)
            return KERNEL_THREAD_CONDITION_CHANGED;
        for (uint32_t prior = 0u; prior < member; ++prior) {
            if (specs[prior].queue == specs[member].queue)
                ++additions;
        }
        if (specs[member].queue->count > UINT32_MAX - additions)
            return KERNEL_THREAD_NO_SLOT;
    }
    thread->state = KERNEL_THREAD_BLOCKED;
    thread->wait_member_count = (uint8_t)member_count;
    thread->wait_mode = (uint8_t)mode;
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
                    &thread->wait_registrations[member]) !=
                KERNEL_THREAD_OK)
                return KERNEL_THREAD_CORRUPT;
        }
        thread->state = KERNEL_THREAD_RUNNING;
        thread->wait_member_count = 0u;
        thread->wait_mode = KERNEL_THREAD_WAIT_NONE;
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

    if (thread == NULL || !valid_wait_queue_header(queue))
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

    if (queue != NULL && queue->count != 0u) {
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

    if (!valid_wait_queue_header(queue))
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
        if (irq_wake && waiter->suspended == 0u) {
            waiter->irq_wake_cycles = wake_cycle;
            waiter->irq_wake_pending = 1u;
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

    if (queue != NULL && queue->count != 0u) {
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
        KernelThread *thread = thread_at_slot(slot);
        uint32_t result;

        if (!valid_thread(thread))
            return KERNEL_THREAD_CORRUPT;
        if (thread->deadline_cycles > now)
            break;
        if (!valid_thread(thread) ||
            thread->state != KERNEL_THREAD_BLOCKED ||
            thread->wait_member_count == 0u)
            return KERNEL_THREAD_CORRUPT;
        result = thread->deadline_result;
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
    KernelThread *thread = thread_at_slot(slot);

    if (!valid_thread(thread) || thread->deadline_position != 0u)
        return false;
    *deadline = thread->deadline_cycles;
    return true;
}

bool kernel_thread_highest_ready_priority(uint8_t *priority)
{
    if (priority == NULL || ready_bitmap == 0u)
        return false;
    *priority = highest_ready_priority(ready_bitmap);
    return true;
}

static KernelThreadStatus retire_process_threads(uint16_t process_slot,
                                                  KernelThread *survivor,
                                                  uint32_t terminal_result,
                                                  bool release_stacks,
                                                  uint32_t *retired_threads)
{
    uint32_t retired = 0u;

    if (survivor != NULL &&
        (!valid_thread(survivor) || survivor->process_slot != process_slot ||
         survivor->state != KERNEL_THREAD_RUNNING))
        return KERNEL_THREAD_INVALID_ARGUMENT;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL ||
            thread->process_slot != process_slot ||
            thread->state == KERNEL_THREAD_DEAD || thread == survivor)
            continue;
        if (thread->state == KERNEL_THREAD_READY &&
            thread->suspended == 0u) {
            KernelThreadStatus status = remove_ready(thread);

            if (status != KERNEL_THREAD_OK)
                return status;
            clear_irq_wake(thread->slot);
        } else if (thread->state == KERNEL_THREAD_BLOCKED) {
            bool cancelled_deadline =
                thread->deadline_position != KERNEL_THREAD_SLOT_NONE;

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
        thread->suspended = 0u;
        if (release_stacks)
            thread->stack_released = 1u;
        mark_reap_pending(thread);
        if (wake_death_waiters(thread, terminal_result, NULL) !=
            KERNEL_THREAD_OK)
            return KERNEL_THREAD_CORRUPT;
        ++retired;
    }
    if ((retired == 0u && survivor == NULL) ||
        retired > pool_stats.live_threads)
        return KERNEL_THREAD_CORRUPT;
    pool_stats.live_threads -= retired;
    pool_stats.dead_threads += retired;
    if (retired_threads != NULL)
        *retired_threads = retired;
    return KERNEL_THREAD_OK;
}

KernelThreadStatus kernel_thread_retire_process(uint16_t process_slot,
                                                uint32_t terminal_result,
                                                uint32_t *retired_threads)
{
    return retire_process_threads(process_slot, NULL, terminal_result, false,
                                  retired_threads);
}

KernelThreadStatus kernel_thread_exec_retire_others(
    uint16_t process_slot, KernelThread *survivor, uint32_t terminal_result,
    uint32_t *retired_threads)
{
    return retire_process_threads(process_slot, survivor, terminal_result,
                                  true, retired_threads);
}

KernelThreadStatus kernel_thread_release_process(uint16_t process_slot)
{
    bool found = false;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);
        ThreadSlot *registry_slot;
        uint16_t released_slot;

        if (thread == NULL ||
            thread->process_slot != process_slot)
            continue;
        if (thread->state != KERNEL_THREAD_DEAD)
            return KERNEL_THREAD_INVALID_STATE;
        if (thread->wait_member_count != 0u ||
            !wait_row_clear(thread->slot) ||
            thread->deadline_position != KERNEL_THREAD_SLOT_NONE ||
            kernel_thread_wait_queue_count(&thread->death_waiters) != 0u ||
            thread->handle_references != 0u ||
            !kernel_stack_valid(thread))
            return KERNEL_THREAD_CORRUPT;
        if (!initialize_kernel_stack(thread))
            return KERNEL_THREAD_CORRUPT;
        thread->stack_released = 1u;
        clear_reap_pending(thread);
        registry_slot = slot_at(thread->slot);
        if (registry_slot == NULL || registry_slot->thread != thread)
            return KERNEL_THREAD_CORRUPT;
        thread->occupied = 0u;
        released_slot = thread->slot;
        registry_slot->thread = NULL;
        if (!release_kernel_stack(thread) ||
            !release_thread_record(thread)) {
            pool_corrupt = 1u;
            return KERNEL_THREAD_CORRUPT;
        }
        if (!release_empty_thread_leaf(released_slot)) {
            pool_corrupt = 1u;
            return KERNEL_THREAD_CORRUPT;
        }
        if (released_slot < next_thread_slot)
            next_thread_slot = released_slot;
        ++pool_stats.reaped_threads;
        found = true;
    }
    while (thread_slots != 0u &&
           thread_at_slot((uint16_t)(thread_slots - 1u)) == NULL)
        --thread_slots;
    return found ? KERNEL_THREAD_OK : KERNEL_THREAD_INVALID_STATE;
}

KernelThread *kernel_thread_at(uint16_t slot)
{
    KernelThread *thread = thread_at_slot(slot);

    return valid_thread(thread) ? thread : NULL;
}

uint32_t kernel_thread_slot_limit(void)
{
    return thread_slots;
}

bool kernel_thread_snapshot(uint32_t slot, KernelThreadSnapshot *snapshot)
{
    const KernelThread *thread;

    if (slot >= thread_slots || snapshot == NULL)
        return false;
    thread = thread_at_slot((uint16_t)slot);
    if (!valid_thread(thread))
        return false;
    snapshot->id = thread->id;
    snapshot->process_id = thread->process_id;
    snapshot->user_stack_base = thread->user_stack_base;
    snapshot->user_stack_top = thread->user_stack_top;
    snapshot->tls_base = thread->tls_base;
    snapshot->tls_pages = thread->tls_pages;
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
    snapshot->runtime_cycles = thread->runtime_cycles;
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
        thread->deadline_position != KERNEL_THREAD_SLOT_NONE ?
            1u : 0u;
    snapshot->stack_released = thread->stack_released;
    snapshot->reap_pending = thread->reap_pending;
    snapshot->stack_pages = thread->stack_pages;
    snapshot->suspended = thread->suspended;
    snapshot->reserved = 0u;
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

    if (stats == NULL || pool_corrupt != 0u || !deadline_heap_valid())
        return false;
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);
        uint32_t used;

        if (thread == NULL)
            continue;
        if (!wait_row_valid(slot))
            return false;
        if (thread->irq_wake_pending != 0u &&
            thread->state != KERNEL_THREAD_READY)
            return false;
        if (!kernel_stack_valid(thread))
            return false;
        if (thread->suspended > 1u ||
            ((thread->state == KERNEL_THREAD_CREATED ||
              thread->state == KERNEL_THREAD_DEAD) &&
             thread->suspended != 0u) ||
            (thread->suspended != 0u &&
             thread->state == KERNEL_THREAD_READY &&
             (thread->ready_previous != KERNEL_THREAD_SLOT_NONE ||
              thread->ready_next != KERNEL_THREAD_SLOT_NONE)) ||
            thread->handle_references > 1u ||
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
                    &thread->wait_registrations[member];

                if (registration->queue == NULL ||
                    !valid_wait_queue(registration->queue))
                    return false;
                ++observed_registrations;
            }
            for (uint16_t member = thread->wait_member_count;
                 member < KERNEL_THREAD_WAIT_MEMBER_MAX; ++member) {
                if (thread->wait_registrations[member].queue != NULL)
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
    if (observed_registrations != wait_registration_count)
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
    return pool_corrupt == 0u && deadline_heap_valid();
}

bool kernel_thread_process_runnable(uint16_t process_slot)
{
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL &&
            thread->process_slot == process_slot &&
            thread->suspended == 0u &&
            (thread->state == KERNEL_THREAD_READY ||
             thread->state == KERNEL_THREAD_RUNNING))
            return true;
    }
    return false;
}

uint32_t kernel_thread_process_count(uint16_t process_slot, bool live_only)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL ||
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

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL && thread->process_slot == process_slot)
            count += thread->run_count;
    }
    return count;
}

uint32_t kernel_thread_process_timer_ticks(uint16_t process_slot)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL && thread->process_slot == process_slot)
            count += thread->timer_ticks;
    }
    return count;
}

uint32_t kernel_thread_process_syscalls(uint16_t process_slot)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL && thread->process_slot == process_slot)
            count += thread->syscall_count;
    }
    return count;
}

KernelThreadState kernel_thread_process_representative_state(
    uint16_t process_slot)
{
    KernelThreadState result = KERNEL_THREAD_UNUSED;

    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        const KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread == NULL ||
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
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);

        if (thread != NULL && !kernel_stack_valid(thread))
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
    for (uint32_t slot = 0u; slot < thread_slots; ++slot) {
        KernelThread *thread = thread_at_slot((uint16_t)slot);
        uint32_t used;
        uint32_t measured_low_water;

        if (thread == NULL)
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
