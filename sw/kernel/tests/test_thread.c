#include "thread.h"

#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

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
    assert(kernel_thread_publish(low) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(high_first) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(high_second) == KERNEL_THREAD_OK);
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
    assert(kernel_thread_retire_process(1u, &retired) == KERNEL_THREAD_OK);
    assert(retired == 3u);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_NO_RUNNABLE);
    assert(kernel_thread_pool_stats(&stats));
    assert(stats.live_threads == 0u);
    assert(stats.dead_threads == 3u);
    assert(stats.ready_threads == 0u);
    assert(stats.ready_bitmap == 0u);
    assert(kernel_thread_release_process(1u) == KERNEL_THREAD_OK);

    assert(kernel_thread_allocate(2u, 0x10000002u, 0u, 0x00100000u,
                                  0x70001000u, 44u,
                                  KERNEL_THREAD_PRIORITY_NORMAL,
                                  &selected) == KERNEL_THREAD_OK);
    assert(selected->id != old_id);
    assert(selected->slot == 0u);
    assert(kernel_thread_publish(selected) == KERNEL_THREAD_OK);
    assert(kernel_thread_process_runnable(2u));
    assert(kernel_thread_process_count(2u, true) == 1u);
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
            assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
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
            assert(kernel_thread_retire_process(slot, &retired) ==
                   KERNEL_THREAD_OK);
            assert(retired == 1u);
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

    for (uint32_t parity = 0u; parity < 2u; ++parity) {
        kernel_performance_init();
        kernel_thread_pool_init();
        for (uint32_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            uint8_t priority = (uint8_t)(slot * 2u + parity);

            assert(kernel_thread_allocate(
                       1u, 0x10000001u, (uint16_t)slot,
                       0x00100000u + slot * 2u,
                       0x70001000u + slot * KERNEL_THREAD_STACK_STRIDE,
                       priority, priority, &thread) == KERNEL_THREAD_OK);
            assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
        }
        for (uint32_t expected = KERNEL_THREAD_MAX; expected-- != 0u;) {
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
    assert(kernel_thread_publish(low) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(high_first) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(high_second) == KERNEL_THREAD_OK);
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
    assert(kernel_thread_publish(first) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(second) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(third) == KERNEL_THREAD_OK);
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
        assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
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

int main(void)
{
    test_priority_fifo_and_process_retirement();
    test_slot_fifteen_generation_ids_do_not_repeat();
    test_invalid_inputs_do_not_consume_slots();
    test_all_priority_levels_select_highest_first();
    test_guarded_kernel_stack_accounting();
    test_wait_queue_priority_fifo_and_stale_sequence();
    test_bounded_deadline_order_expiry_and_signal_race();
    test_deadline_capacity_matches_thread_capacity();
    puts("thread tests passed");
    return 0;
}
