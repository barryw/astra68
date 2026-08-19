#include "thread.h"

#include <astra/syscall.h>

#include "allocation.h"
#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static KernelHandle next_test_handle = 0x00000101u;

static void publish_thread(KernelThread *thread)
{
    assert(kernel_thread_attach_handle(thread, next_test_handle) ==
           KERNEL_THREAD_OK);
    next_test_handle += 0x00000100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
}

static void release_process_handles(uint16_t process_slot)
{
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *thread = kernel_thread_at(slot);

        if (thread != NULL && thread->process_slot == process_slot &&
            thread->handle_references != 0u)
            kernel_thread_handle_release(thread, NULL);
    }
}

static void test_priority_fifo_and_process_retirement(void)
{
    KernelThread *low;
    KernelThread *high_first;
    KernelThread *high_second;
    KernelThread *selected;
    KernelThreadPoolStats stats;
    KernelThreadSnapshot snapshot;
    uint32_t retired;
    uint32_t old_id;

    kernel_performance_init();
    kernel_thread_pool_init();
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 11u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &low) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 22u, 20u,
                                  &high_first) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 2u, 0x00100020u,
                                  0x70005000u, 33u, 20u,
                                  &high_second) == KERNEL_THREAD_OK);
    publish_thread(low);
    publish_thread(high_first);
    publish_thread(high_second);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.created_threads == 3u);
    assert(stats.live_threads == 3u);
    assert(stats.ready_threads == 3u);
    assert(stats.ready_bitmap ==
           ((1u << KERNEL_THREAD_PRIORITY_NORMAL) | (1u << 20u)));

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == high_first);
    assert(selected->context.data[2] == 22u);
    assert(kernel_thread_make_ready(selected) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == high_second);
    assert(kernel_thread_snapshot(high_second->slot, &snapshot));
    assert(snapshot.process_id == 0x10000001u);
    assert(snapshot.stack_slot == 2u);
    assert(snapshot.state == KERNEL_THREAD_RUNNING);
    assert(snapshot.base_priority == 20u);
    assert(snapshot.effective_priority == 20u);

    old_id = low->id;
    assert(kernel_thread_retire_process(1u, 8u, &retired) ==
           KERNEL_THREAD_OK);
    assert(retired == 3u);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_NO_RUNNABLE);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.live_threads == 0u);
    assert(stats.dead_threads == 3u);
    assert(stats.ready_threads == 0u);
    assert(stats.ready_bitmap == 0u);
    release_process_handles(1u);
    assert(kernel_thread_release_process(1u) == KERNEL_THREAD_OK);

    assert(kernel_thread_allocate(2u, 0x10000002u, 0u, 0x00100000u,
                                  0x70001000u, 44u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &selected) == KERNEL_THREAD_OK);
    assert(selected->id != old_id);
    assert(selected->slot == 0u);
    publish_thread(selected);
    assert(kernel_thread_process_runnable(2u));
    assert(kernel_thread_process_count(2u, true) == 1u);
}

static void test_record_injection_preserves_pool(void)
{
    KernelAllocationStats allocation_stats;
    KernelThread *thread = (KernelThread *)(uintptr_t)1u;
    KernelThreadPoolStats pool_stats;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, 1u);
    assert(kernel_thread_allocate(
               0u, 0x10000001u, 0u, 0x00100000u, 0x70001000u,
               0u, KERNEL_THREAD_PRIORITY_NORMAL, &thread) ==
           KERNEL_THREAD_NO_SLOT);
    assert(thread == NULL);
    thread = (KernelThread *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_thread_allocate(
               0u, 0x10000001u, 0u, 0x00100000u, 0x70001000u,
               0u, KERNEL_THREAD_PRIORITY_NORMAL, &thread) ==
           KERNEL_THREAD_NO_SLOT);
    assert(thread == NULL);
    assert(kernel_thread_pool_stats(&pool_stats));
    assert(pool_stats.live_threads == 0u);
    assert(pool_stats.ready_threads == 0u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, &allocation_stats));
    assert(allocation_stats.current_units == 0u);
    assert(allocation_stats.injected_failures == 2u);
    assert(kernel_thread_pool_valid());
    assert(kernel_allocation_valid());
}

static void test_slot_fifteen_generation_ids_do_not_repeat(void)
{
    KernelThread *thread;
    uint32_t generation_two_id = 0u;
    uint32_t retired;

    kernel_performance_init();
    kernel_thread_pool_init();
    for (uint32_t generation = 1u; generation <= 3u; ++generation) {
        for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            assert(kernel_thread_allocate(
                       slot, 0x10000001u + slot, slot,
                       0x00100000u + (uint32_t)slot * 2u,
                       0x70001000u +
                           (uint32_t)slot * KERNEL_THREAD_STACK_STRIDE,
                       0u, KERNEL_THREAD_PRIORITY_NORMAL, &thread) ==
                   KERNEL_THREAD_OK);
            publish_thread(thread);
        }
        assert(kernel_thread_at(KERNEL_THREAD_MAX - 1u) != NULL);
        if (generation == 2u)
            generation_two_id =
                kernel_thread_at(KERNEL_THREAD_MAX - 1u)->id;
        else if (generation == 3u) {
            assert(generation_two_id != 0u);
            assert(kernel_thread_at(KERNEL_THREAD_MAX - 1u)->id !=
                   generation_two_id);
        }
        for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            assert(kernel_thread_retire_process(slot, 8u, &retired) ==
                   KERNEL_THREAD_OK);
            assert(retired == 1u);
            release_process_handles(slot);
            assert(kernel_thread_release_process(slot) == KERNEL_THREAD_OK);
        }
    }
}

static void test_invalid_inputs_do_not_consume_slots(void)
{
    KernelThread *thread = (KernelThread *)(uintptr_t)1u;
    KernelThreadPoolStats stats;

    kernel_performance_init();
    kernel_thread_pool_init();
    assert(kernel_thread_allocate(0u, 0u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &thread) == KERNEL_THREAD_INVALID_ARGUMENT);
    assert(kernel_thread_allocate(0u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_LEVELS,
                                  &thread) == KERNEL_THREAD_INVALID_ARGUMENT);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.created_threads == 0u);
    assert(stats.live_threads == 0u);
    assert(stats.ready_threads == 0u);
}

static void test_all_priority_levels_select_highest_first(void)
{
    KernelThread *thread;
    KernelThread *selected;

    /*
     * One thread per other priority level, not one per thread slot. The two
     * were the same number when there were sixteen slots and thirty-two
     * levels; they are not now, and a slot-driven loop asks for priorities
     * that do not exist.
     */
    const uint32_t covered = KERNEL_THREAD_PRIORITY_LEVELS / 2u <
                                     (uint32_t)KERNEL_THREAD_MAX ?
                                 KERNEL_THREAD_PRIORITY_LEVELS / 2u :
                                 (uint32_t)KERNEL_THREAD_MAX;

    for (uint32_t parity = 0u; parity < 2u; ++parity) {
        kernel_performance_init();
        kernel_thread_pool_init();
        for (uint32_t slot = 0u; slot < covered; ++slot) {
            uint8_t priority = (uint8_t)(slot * 2u + parity);

            assert(kernel_thread_allocate(
                       1u, 0x10000001u, (uint16_t)slot,
                       0x00100000u + slot * 2u,
                       0x70001000u + slot * KERNEL_THREAD_STACK_STRIDE,
                       priority, priority, &thread) == KERNEL_THREAD_OK);
            publish_thread(thread);
        }
        for (uint32_t expected = covered; expected-- != 0u;) {
            assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
            assert(selected->effective_priority ==
                   (uint8_t)(expected * 2u + parity));
        }
        assert(kernel_thread_take_next(&selected) ==
               KERNEL_THREAD_NO_RUNNABLE);
    }
}

static void test_guarded_kernel_stack_accounting(void)
{
    KernelThread *thread;
    KernelThreadPoolStats stats;
    KernelThreadSnapshot snapshot;

    kernel_performance_init();
    kernel_thread_pool_init();
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_snapshot(thread->slot, &snapshot));
    assert(snapshot.kernel_stack_guard ==
           KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE);
    assert(snapshot.kernel_stack_base ==
           snapshot.kernel_stack_guard +
               KERNEL_THREAD_SUPERVISOR_GUARD_SIZE);
    assert(snapshot.kernel_stack_top ==
           snapshot.kernel_stack_base +
               KERNEL_THREAD_SUPERVISOR_STACK_SIZE);
    assert(snapshot.kernel_stack_used == 0u);
    assert(kernel_thread_note_kernel_entry(
               thread, snapshot.kernel_stack_top - 96u) == KERNEL_THREAD_OK);
    assert(kernel_thread_snapshot(thread->slot, &snapshot));
    assert(snapshot.kernel_stack_entries == 1u);
    assert(snapshot.kernel_stack_used == 96u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.kernel_stack_entries == 1u);
    assert(stats.kernel_stack_max_used == 96u);
    assert(stats.kernel_stack_measurements == 0u);
    assert(stats.kernel_stack_scan_words == 0u);
    for (uint32_t query = 0u; query < 128u; ++query)
        assert(kernel_thread_pool_stats(&stats));
    assert(stats.kernel_stack_measurements == 0u);
    assert(stats.kernel_stack_scan_words == 0u);
    assert(kernel_thread_measure_stacks(&stats.kernel_stack_max_used));
    assert(stats.kernel_stack_max_used == 96u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.kernel_stack_measurements == 1u);
    assert(stats.kernel_stack_scan_words != 0u);
    {
        uint32_t measured_words = stats.kernel_stack_scan_words;

        assert(kernel_thread_pool_stats(&stats));
        assert(stats.kernel_stack_scan_words == measured_words);
    }
    assert(kernel_thread_stacks_valid());
    assert(kernel_thread_note_kernel_entry(
               thread, snapshot.kernel_stack_base) == KERNEL_THREAD_CORRUPT);
}

static void test_wait_queue_priority_fifo_and_stale_sequence(void)
{
    KernelThreadWaitQueue queue;
    KernelThread *low;
    KernelThread *high_first;
    KernelThread *high_second;
    KernelThread *selected;
    uint32_t sequence;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&queue);
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, 8u,
                                  &low) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 0u, 20u,
                                  &high_first) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 2u, 0x00100020u,
                                  0x70005000u, 0u, 20u,
                                  &high_second) == KERNEL_THREAD_OK);
    publish_thread(low);
    publish_thread(high_first);
    publish_thread(high_second);
    sequence = kernel_thread_wait_queue_sequence(&queue);

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == high_first);
    assert(kernel_thread_block(selected, &queue, sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == high_second);
    assert(kernel_thread_block(selected, &queue, sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == low);
    assert(kernel_thread_block(selected, &queue, sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_thread_wait_queue_count(&queue) == 3u);

    assert(kernel_thread_wake_one(&queue, 0x11u, &selected) ==
           KERNEL_THREAD_OK);
    assert(selected == high_first && selected->context.data[0] == 0x11u);
    assert(kernel_thread_wake_one(&queue, 0x22u, &selected) ==
           KERNEL_THREAD_OK);
    assert(selected == high_second && selected->context.data[0] == 0x22u);
    assert(kernel_thread_wake_one(&queue, 0x33u, &selected) ==
           KERNEL_THREAD_OK);
    assert(selected == low && selected->context.data[0] == 0x33u);
    assert(kernel_thread_wait_queue_count(&queue) == 0u);

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == high_first);
    assert(kernel_thread_block(selected, &queue, sequence) ==
           KERNEL_THREAD_CONDITION_CHANGED);
    assert(selected->state == KERNEL_THREAD_RUNNING);
}

static void test_bounded_deadline_order_expiry_and_signal_race(void)
{
    KernelThreadWaitQueue queue;
    KernelThreadPoolStats stats;
    KernelThread *first;
    KernelThread *second;
    KernelThread *third;
    KernelThread *selected;
    uint64_t next_deadline;
    uint32_t expired;
    uint32_t sequence;
    uint8_t highest;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&queue);
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, 20u,
                                  &first) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 0u, 20u,
                                  &second) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 2u, 0x00100020u,
                                  0x70005000u, 0u, 20u,
                                  &third) == KERNEL_THREAD_OK);
    publish_thread(first);
    publish_thread(second);
    publish_thread(third);
    sequence = kernel_thread_wait_queue_sequence(&queue);

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == first);
    assert(kernel_thread_block_until(selected, &queue, sequence, 10u, 10u,
                                     0xdead0000u) ==
           KERNEL_THREAD_DEADLINE_EXPIRED);
    assert(selected->state == KERNEL_THREAD_RUNNING);
    assert(kernel_thread_block_until(selected, &queue, sequence, 10u, 300u,
                                     0xdead0001u) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == second);
    assert(kernel_thread_block_until(selected, &queue, sequence, 10u, 100u,
                                     0xdead0002u) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == third);
    assert(kernel_thread_block_until(selected, &queue, sequence, 10u, 100u,
                                     0xdead0003u) == KERNEL_THREAD_OK);

    assert(kernel_thread_next_deadline(&next_deadline));
    assert(next_deadline == 100u);
    assert(kernel_thread_expire_deadlines(99u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);
    assert(kernel_thread_expire_deadlines(100u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 2u);
    assert(highest == 20u);
    assert(kernel_thread_wait_queue_sequence(&queue) != sequence);
    assert(kernel_thread_wait_queue_count(&queue) == 1u);
    assert(kernel_thread_next_deadline(&next_deadline));
    assert(next_deadline == 300u);

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == second);
    assert(selected->context.data[0] == 0xdead0002u);
    assert(kernel_thread_make_ready(selected) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == third);
    assert(selected->context.data[0] == 0xdead0003u);

    assert(kernel_thread_wake_one(&queue, 0x12345678u, &selected) ==
           KERNEL_THREAD_OK);
    assert(selected == first);
    assert(selected->context.data[0] == 0x12345678u);
    assert(!kernel_thread_next_deadline(&next_deadline));
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.deadline_waits == 3u);
    assert(stats.deadline_expirations == 2u);
    assert(stats.deadline_cancellations == 1u);
    assert(stats.deadline_depth == 0u);
    assert(stats.deadline_max_depth == 3u);
}

static void test_deadline_capacity_matches_thread_capacity(void)
{
    KernelThreadWaitQueue queue;
    KernelThreadPoolStats stats;
    KernelThread *thread;
    KernelThread *selected;
    uint32_t expired;
    uint32_t sequence;
    uint8_t highest;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&queue);
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        assert(kernel_thread_allocate(
                   1u, 0x10000001u, slot,
                   0x00100000u + (uint32_t)slot * 2u,
                   0x70001000u +
                       (uint32_t)slot * KERNEL_THREAD_STACK_STRIDE,
                   0u, KERNEL_THREAD_PRIORITY_NORMAL, &thread) ==
               KERNEL_THREAD_OK);
        publish_thread(thread);
    }
    sequence = kernel_thread_wait_queue_sequence(&queue);
    for (uint32_t count = 0u; count < KERNEL_THREAD_MAX; ++count) {
        assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
        assert(kernel_thread_block_until(
                   selected, &queue, sequence, 0u, 1000u + selected->slot,
                   selected->slot) == KERNEL_THREAD_OK);
    }
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.deadline_depth == KERNEL_THREAD_MAX);
    assert(stats.deadline_max_depth == KERNEL_THREAD_MAX);
    assert(kernel_thread_expire_deadlines(2000u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == KERNEL_THREAD_MAX);
    assert(highest == KERNEL_THREAD_PRIORITY_NORMAL);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.deadline_depth == 0u);
    assert(stats.ready_threads == KERNEL_THREAD_MAX);
}

static void test_waitable_death_status_and_deferred_reap(void)
{
    KernelThread *target;
    KernelThread *waiter;
    KernelThread *selected;
    KernelThreadPoolStats stats;
    bool blocked;
    bool released;
    uint32_t exit_status;
    uint32_t wait_result;
    uint32_t woken;

    kernel_performance_init();
    kernel_thread_pool_init();
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, 16u,
                                  &target) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 0u, 20u,
                                  &waiter) == KERNEL_THREAD_OK);
    publish_thread(target);
    publish_thread(waiter);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 10u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT, &blocked, &wait_result,
               &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == target);
    assert(kernel_thread_complete(target, 0x89abcdefu, ASTRA_SYSCALL_OK,
                                  &woken) == KERNEL_THREAD_OK);
    assert(woken == 1u);
    assert(kernel_thread_reap_pending());
    assert(kernel_thread_reap_slots() ==
           (uint16_t)(1u << target->slot));
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(waiter->context.data[0] == ASTRA_SYSCALL_OK);
    assert(waiter->context.data[1] == 0x89abcdefu);

    assert(kernel_thread_wait_for_death(
               target, waiter, 20u, 20u, ASTRA_SYSCALL_TIMED_OUT,
               &blocked, &wait_result, &exit_status) == KERNEL_THREAD_OK);
    assert(!blocked);
    assert(wait_result == ASTRA_SYSCALL_OK);
    assert(exit_status == 0x89abcdefu);
    assert(kernel_thread_finish_reap(target, &released) ==
           KERNEL_THREAD_OK);
    assert(!released);
    assert(target->stack_released == 1u);
    assert(target->reap_pending == 0u);
    assert(!kernel_thread_reap_pending());
    assert(kernel_thread_reap_slots() == 0u);
    kernel_thread_handle_release(target, NULL);
    assert(target->reap_pending == 1u);
    assert(kernel_thread_reap_slots() ==
           (uint16_t)(1u << target->slot));
    assert(kernel_thread_finish_reap(target, &released) ==
           KERNEL_THREAD_OK);
    assert(released);
    assert(kernel_thread_at(target->slot) == NULL);
    assert(!kernel_thread_reap_pending());
    assert(kernel_thread_reap_slots() == 0u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.thread_exits == 1u);
    assert(stats.death_waits == 2u);
    assert(stats.death_wakeups == 1u);
    assert(stats.reaped_threads == 1u);
}

static void test_final_handle_close_wins_without_killing_target(void)
{
    KernelThread *target;
    KernelThread *waiter;
    KernelThread *selected;
    bool blocked;
    bool released;
    uint32_t exit_status;
    uint32_t wait_result;
    uint32_t woken;

    kernel_performance_init();
    kernel_thread_pool_init();
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, 16u,
                                  &target) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 0u, 16u,
                                  &waiter) == KERNEL_THREAD_OK);
    publish_thread(waiter);
    publish_thread(target);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 0u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT, &blocked, &wait_result,
               &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    kernel_thread_handle_release(target, NULL);
    assert(target->state == KERNEL_THREAD_READY);
    assert(target->handle_references == 0u);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == target);
    assert(kernel_thread_complete(target, 7u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_THREAD_OK);
    assert(woken == 0u);
    assert(kernel_thread_finish_reap(target, &released) ==
           KERNEL_THREAD_OK);
    assert(released);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(waiter->context.data[0] == ASTRA_SYSCALL_CLOSED);
    assert(waiter->context.data[1] == 0u);
}

static void allocate_death_race_pair(KernelThread **waiter,
                                     KernelThread **target)
{
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100010u,
                                  0x70001000u, 0u, 16u,
                                  waiter) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100000u,
                                  0x70003000u, 0u, 16u,
                                  target) == KERNEL_THREAD_OK);
    publish_thread(*waiter);
    publish_thread(*target);
}

static void test_death_wait_timeout_close_and_teardown_races(void)
{
    KernelThread *target;
    KernelThread *waiter;
    KernelThread *selected;
    bool blocked;
    uint32_t exit_status;
    uint32_t expired;
    uint32_t retired;
    uint32_t wait_result;
    uint32_t woken;
    uint8_t highest;
    uint64_t next_deadline;

    /* Timeout commits first; later exit cannot wake the waiter again. */
    kernel_performance_init();
    kernel_thread_pool_init();
    allocate_death_race_pair(&waiter, &target);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 0u, 10u, ASTRA_SYSCALL_TIMED_OUT,
               &blocked, &wait_result, &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    assert(kernel_thread_expire_deadlines(10u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 1u);
    assert(highest == 16u);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == target);
    assert(kernel_thread_complete(target, 7u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_THREAD_OK);
    assert(woken == 0u);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(waiter->context.data[0] == ASTRA_SYSCALL_TIMED_OUT);
    assert(waiter->context.data[1] == 0u);

    /* Exit commits first and withdraws the deadline exactly once. */
    kernel_performance_init();
    kernel_thread_pool_init();
    allocate_death_race_pair(&waiter, &target);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 0u, 10u, ASTRA_SYSCALL_TIMED_OUT,
               &blocked, &wait_result, &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == target);
    assert(kernel_thread_complete(target, 0x12345678u, ASTRA_SYSCALL_OK,
                                  &woken) == KERNEL_THREAD_OK);
    assert(woken == 1u);
    assert(kernel_thread_expire_deadlines(10u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);
    assert(!kernel_thread_next_deadline(&next_deadline));
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(waiter->context.data[0] == ASTRA_SYSCALL_OK);
    assert(waiter->context.data[1] == 0x12345678u);

    /* Final handle close wins against the same pending deadline. */
    kernel_performance_init();
    kernel_thread_pool_init();
    allocate_death_race_pair(&waiter, &target);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 0u, 10u, ASTRA_SYSCALL_TIMED_OUT,
               &blocked, &wait_result, &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    kernel_thread_handle_release(target, NULL);
    assert(target->state == KERNEL_THREAD_READY);
    assert(waiter->state == KERNEL_THREAD_READY);
    assert(waiter->context.data[0] == ASTRA_SYSCALL_CLOSED);
    assert(waiter->context.data[1] == 0u);
    assert(kernel_thread_expire_deadlines(10u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);

    /* Whole-process teardown removes both sides regardless of queue order. */
    kernel_performance_init();
    kernel_thread_pool_init();
    allocate_death_race_pair(&waiter, &target);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == waiter);
    assert(kernel_thread_wait_for_death(
               target, waiter, 0u, 10u, ASTRA_SYSCALL_TIMED_OUT,
               &blocked, &wait_result, &exit_status) == KERNEL_THREAD_OK);
    assert(blocked);
    assert(kernel_thread_retire_process(1u, ASTRA_SYSCALL_PEER_DEAD,
                                        &retired) == KERNEL_THREAD_OK);
    assert(retired == 2u);
    assert(waiter->state == KERNEL_THREAD_DEAD);
    assert(target->state == KERNEL_THREAD_DEAD);
    assert(kernel_thread_wait_queue_count(&target->death_waiters) == 0u);
    assert(kernel_thread_expire_deadlines(10u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);
    release_process_handles(1u);
    assert(kernel_thread_release_process(1u) == KERNEL_THREAD_OK);
    assert(kernel_thread_pool_valid());
}

static void test_wait_set_boundary_winner_and_exact_withdrawal(void)
{
    KernelThreadWaitQueue queues[KERNEL_THREAD_WAIT_MEMBER_MAX];
    KernelThreadWaitSpec specs[KERNEL_THREAD_WAIT_MEMBER_MAX];
    KernelThreadPoolStats stats;
    KernelThread *thread;
    KernelThread *selected;
    uint32_t sequences[KERNEL_THREAD_WAIT_MEMBER_MAX];
    uint64_t next_deadline;

    kernel_performance_init();
    kernel_thread_pool_init();
    for (uint32_t member = 0u;
         member < KERNEL_THREAD_WAIT_MEMBER_MAX; ++member) {
        kernel_thread_wait_queue_init(&queues[member]);
        sequences[member] =
            kernel_thread_wait_queue_sequence(&queues[member]);
        specs[member].queue = &queues[member];
        specs[member].sequence = sequences[member];
    }
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &thread) == KERNEL_THREAD_OK);
    publish_thread(thread);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == thread);
    assert(kernel_thread_block_wait_set(
               thread, specs, KERNEL_THREAD_WAIT_MEMBER_MAX, 0u, 100u,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    for (uint32_t member = 0u;
         member < KERNEL_THREAD_WAIT_MEMBER_MAX; ++member)
        assert(kernel_thread_wait_queue_count(&queues[member]) == 1u);
    assert(kernel_thread_next_deadline(&next_deadline));
    assert(next_deadline == 100u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.blocked_threads == 1u);
    assert(stats.wait_set_blocks == 1u);
    assert(stats.wait_registrations == KERNEL_THREAD_WAIT_MEMBER_MAX);
    assert(stats.wait_registration_max == KERNEL_THREAD_WAIT_MEMBER_MAX);
    assert(stats.max_wait_members == KERNEL_THREAD_WAIT_MEMBER_MAX);

    assert(kernel_thread_wake_one(
               &queues[KERNEL_THREAD_WAIT_MEMBER_MAX - 1u],
               ASTRA_SYSCALL_OK, &selected) == KERNEL_THREAD_OK);
    assert(selected == thread);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] ==
           KERNEL_THREAD_WAIT_MEMBER_MAX - 1u);
    assert(thread->context.data[2] == 0u);
    for (uint32_t member = 0u;
         member < KERNEL_THREAD_WAIT_MEMBER_MAX; ++member) {
        assert(kernel_thread_wait_queue_count(&queues[member]) == 0u);
        assert(kernel_thread_wait_queue_sequence(&queues[member]) !=
               sequences[member]);
    }
    assert(!kernel_thread_next_deadline(&next_deadline));
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.blocked_threads == 0u);
    assert(stats.wait_set_wakeups == 1u);
    assert(stats.wait_registrations == 0u);
    assert(stats.deadline_cancellations == 1u);
    assert(kernel_thread_pool_valid());
}

static void test_wait_set_duplicate_member_is_deterministic(void)
{
    KernelThreadWaitQueue duplicate;
    KernelThreadWaitQueue other;
    KernelThreadWaitSpec specs[3];
    KernelThreadPoolStats stats;
    KernelThread *thread;
    KernelThread *selected;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&duplicate);
    kernel_thread_wait_queue_init(&other);
    specs[0].queue = &duplicate;
    specs[0].sequence = kernel_thread_wait_queue_sequence(&duplicate);
    specs[1].queue = &other;
    specs[1].sequence = kernel_thread_wait_queue_sequence(&other);
    specs[2] = specs[0];
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &thread) == KERNEL_THREAD_OK);
    publish_thread(thread);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == thread);
    assert(kernel_thread_block_wait_set(
               thread, specs, 3u, 0u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_thread_wait_queue_count(&duplicate) == 2u);
    assert(kernel_thread_wait_queue_waiter_count(&duplicate) == 1u);
    assert(kernel_thread_wait_queue_count(&other) == 1u);

    assert(kernel_thread_wake_one(&duplicate, ASTRA_SYSCALL_OK,
                                  &selected) == KERNEL_THREAD_OK);
    assert(selected == thread);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] == 0u);
    assert(thread->context.data[2] == 0u);
    assert(kernel_thread_wait_queue_count(&duplicate) == 0u);
    assert(kernel_thread_wait_queue_count(&other) == 0u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.wait_registrations == 0u);
    assert(stats.wait_registration_max == 3u);
    assert(kernel_thread_pool_valid());
}

static void test_wait_set_timeout_cancel_and_atomic_admission(void)
{
    KernelThreadWaitQueue first;
    KernelThreadWaitQueue second;
    KernelThreadWaitSpec specs[2];
    KernelThreadPoolStats stats;
    KernelThread *threads[KERNEL_THREAD_MAX];
    KernelThread *selected;
    uint32_t expired;
    uint32_t sequence;
    uint8_t highest;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&first);
    kernel_thread_wait_queue_init(&second);
    specs[0].queue = &first;
    specs[0].sequence = kernel_thread_wait_queue_sequence(&first);
    specs[1].queue = &second;
    specs[1].sequence = kernel_thread_wait_queue_sequence(&second);
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &threads[0]) == KERNEL_THREAD_OK);
    publish_thread(threads[0]);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == threads[0]);
    assert(kernel_thread_block_wait_set(
               selected, specs, 2u, 0u, 10u,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_thread_expire_deadlines(10u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 1u);
    assert(highest == KERNEL_THREAD_PRIORITY_NORMAL);
    assert(threads[0]->context.data[0] == ASTRA_SYSCALL_TIMED_OUT);
    assert(threads[0]->context.data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(threads[0]->context.data[2] == 0u);
    assert(kernel_thread_wait_queue_count(&first) == 0u);
    assert(kernel_thread_wait_queue_count(&second) == 0u);

    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == threads[0]);
    specs[0].sequence = kernel_thread_wait_queue_sequence(&first);
    specs[1].sequence = kernel_thread_wait_queue_sequence(&second);
    assert(kernel_thread_block_wait_set(
               selected, specs, 2u, 10u,
               KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_thread_cancel_wait(selected, ASTRA_SYSCALL_CANCELLED) ==
           KERNEL_THREAD_OK);
    assert(selected->context.data[0] == ASTRA_SYSCALL_CANCELLED);
    assert(selected->context.data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(selected->context.data[2] == 0u);
    assert(kernel_thread_wait_queue_count(&first) == 0u);
    assert(kernel_thread_wait_queue_count(&second) == 0u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.wait_registrations == 0u);
    assert(stats.deadline_expirations == 1u);
    assert(stats.wait_cancellations == 1u);

    /* Admission checks every duplicate before linking any member. */
    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_thread_wait_queue_init(&first);
    sequence = kernel_thread_wait_queue_sequence(&first);
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        assert(kernel_thread_allocate(
                   1u, 0x10000001u, slot,
                   0x00100000u + (uint32_t)slot * 2u,
                   0x70001000u +
                       (uint32_t)slot * KERNEL_THREAD_STACK_STRIDE,
                   0u, KERNEL_THREAD_PRIORITY_NORMAL,
                   &threads[slot]) == KERNEL_THREAD_OK);
        publish_thread(threads[slot]);
    }
    for (uint32_t slot = 0u; slot < KERNEL_THREAD_MAX - 1u; ++slot) {
        assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
        assert(selected == threads[slot]);
        assert(kernel_thread_block(selected, &first, sequence) ==
               KERNEL_THREAD_OK);
    }
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == threads[KERNEL_THREAD_MAX - 1u]);
    specs[0].queue = &first;
    specs[0].sequence = sequence;
    specs[1] = specs[0];
    assert(kernel_thread_block_wait_set(
               selected, specs, 2u, 0u,
               KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_NO_SLOT);
    assert(selected->state == KERNEL_THREAD_RUNNING);
    assert(selected->wait_member_count == 0u);
    assert(kernel_thread_wait_queue_count(&first) ==
           KERNEL_THREAD_MAX - 1u);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.blocked_threads == KERNEL_THREAD_MAX - 1u);
    assert(stats.wait_registrations == KERNEL_THREAD_MAX - 1u);
    assert(kernel_thread_pool_valid());
}

int main(void)
{
    test_record_injection_preserves_pool();
    test_priority_fifo_and_process_retirement();
    test_slot_fifteen_generation_ids_do_not_repeat();
    test_invalid_inputs_do_not_consume_slots();
    test_all_priority_levels_select_highest_first();
    test_guarded_kernel_stack_accounting();
    test_wait_queue_priority_fifo_and_stale_sequence();
    test_bounded_deadline_order_expiry_and_signal_race();
    test_deadline_capacity_matches_thread_capacity();
    test_waitable_death_status_and_deferred_reap();
    test_final_handle_close_wins_without_killing_target();
    test_death_wait_timeout_close_and_teardown_races();
    test_wait_set_boundary_winner_and_exact_withdrawal();
    test_wait_set_duplicate_member_is_deterministic();
    test_wait_set_timeout_cancel_and_atomic_admission();
    puts("thread tests passed");
    return 0;
}
