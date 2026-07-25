#include "sync.h"

#include <astra/syscall.h>

#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void initialize_test(void)
{
    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_sync_pool_init();
    assert(kernel_sync_pool_valid());
}

static KernelHandle next_test_handle = 0x00000101u;

static KernelThread *allocate_thread(uint16_t process_slot, uint16_t stack_slot,
                                     uint8_t priority)
{
    KernelThread *thread;

    assert(kernel_thread_allocate(
               process_slot, 0x10000001u + process_slot, stack_slot,
               0x00100000u + (uint32_t)stack_slot * 2u,
               0x70001000u +
                   (uint32_t)stack_slot * KERNEL_THREAD_STACK_STRIDE,
               0u, priority, &thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_attach_handle(thread, next_test_handle) ==
           KERNEL_THREAD_OK);
    next_test_handle += 0x00000100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    return thread;
}

static KernelThread *take_thread(void)
{
    KernelThread *thread;

    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    assert(thread != NULL);
    return thread;
}

static void test_auto_and_manual_event_semantics(void)
{
    KernelSyncObject *automatic;
    KernelSyncObject *manual;
    KernelSyncSnapshot snapshot;
    KernelThread *first;
    KernelThread *second;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(1u, 0u, &automatic) == KERNEL_SYNC_OK);
    first = allocate_thread(0u, 0u, 16u);
    first = take_thread();
    assert(kernel_sync_signal(automatic, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 0u);
    assert(kernel_sync_wait(automatic, first, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(automatic, first, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_signal(automatic, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 1u);
    assert(first->context.data[0] == ASTRA_SYSCALL_OK);
    kernel_sync_handle_release(automatic, NULL);

    kernel_thread_pool_init();
    assert(kernel_sync_create_event(
               2u, KERNEL_SYNC_EVENT_MANUAL_RESET |
                       KERNEL_SYNC_EVENT_INITIALLY_SIGNALED,
               &manual) == KERNEL_SYNC_OK);
    first = allocate_thread(0u, 0u, 16u);
    first = take_thread();
    assert(kernel_sync_wait(manual, first, 0u, 0u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(manual, first, 0u, 0u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_reset(manual) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(manual, first, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    second = allocate_thread(0u, 1u, 17u);
    second = take_thread();
    assert(kernel_sync_wait(manual, second, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_signal(manual, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 2u);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.type == KERNEL_SYNC_EVENT_MANUAL);
    assert(snapshot.count == 1u);
    assert(snapshot.waiters == 0u);
    assert(kernel_sync_signal(manual, 2u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    kernel_sync_handle_release(manual, NULL);
    assert(kernel_sync_pool_valid());
}

static void test_semaphore_atomic_handoff_and_overflow(void)
{
    KernelSyncObject *semaphore;
    KernelSyncSnapshot snapshot;
    KernelThread *first;
    KernelThread *second;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_semaphore(1u, 1u, 2u, &semaphore) ==
           KERNEL_SYNC_OK);
    first = allocate_thread(0u, 0u, 16u);
    first = take_thread();
    assert(kernel_sync_wait(semaphore, first, 0u, 0u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(semaphore, first, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    second = allocate_thread(0u, 1u, 17u);
    second = take_thread();
    assert(kernel_sync_wait(semaphore, second, 10u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);

    assert(kernel_sync_signal(semaphore, 3u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 2u);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.count == 1u);
    assert(snapshot.maximum == 2u);
    assert(snapshot.waiters == 0u);
    assert(kernel_sync_signal(semaphore, 2u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_COUNT_LIMIT);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.count == 1u);
    assert(kernel_sync_signal(semaphore, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 0u);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.count == 2u);
    assert(kernel_sync_reset(semaphore) == KERNEL_SYNC_INVALID_STATE);
    kernel_sync_handle_release(semaphore, NULL);
    assert(kernel_sync_pool_valid());
}

static void test_terminal_races_remove_wait_once(void)
{
    KernelSyncObject *event;
    KernelThreadPoolStats thread_stats;
    KernelThread *thread;
    uint32_t expired;
    uint32_t woken;
    uint8_t highest;

    initialize_test();
    assert(kernel_sync_create_event(1u, 0u, &event) == KERNEL_SYNC_OK);
    thread = allocate_thread(0u, 0u, 16u);
    thread = take_thread();
    assert(kernel_sync_wait(event, thread, 10u, 100u,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_cancel_wait(thread, ASTRA_SYSCALL_CANCELLED) ==
           KERNEL_THREAD_OK);
    assert(thread->context.data[0] == ASTRA_SYSCALL_CANCELLED);
    assert(kernel_thread_cancel_wait(thread, ASTRA_SYSCALL_CANCELLED) ==
           KERNEL_THREAD_INVALID_STATE);
    assert(kernel_thread_expire_deadlines(100u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);

    thread = take_thread();
    assert(kernel_sync_wait(event, thread, 100u, 200u,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_expire_deadlines(200u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_TIMED_OUT);
    assert(kernel_sync_signal(event, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 0u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_TIMED_OUT);

    thread = take_thread();
    assert(kernel_sync_wait(event, thread, 200u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(event, thread, 200u, 300u,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    kernel_sync_handle_release(event, NULL);
    assert(thread->context.data[0] == ASTRA_SYSCALL_CLOSED);
    assert(kernel_thread_expire_deadlines(300u, &expired, &highest) ==
           KERNEL_THREAD_OK);
    assert(expired == 0u);
    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.wait_cancellations == 1u);
    assert(thread_stats.deadline_expirations == 1u);
    assert(thread_stats.deadline_cancellations == 2u);
    assert(thread_stats.deadline_depth == 0u);
    assert(kernel_sync_pool_valid());
}

static void test_owner_death_closes_external_reference(void)
{
    KernelSyncObject *event;
    KernelSyncSnapshot snapshot;
    KernelThread *thread;
    uint32_t closed;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(0x1001u, 0u, &event) == KERNEL_SYNC_OK);
    assert(kernel_sync_retain(event) == KERNEL_SYNC_OK);
    thread = allocate_thread(1u, 0u, 20u);
    thread = take_thread();
    assert(kernel_sync_wait(event, thread, 0u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_owner_died(0x1001u, ASTRA_SYSCALL_PEER_DEAD,
                                  &closed, &woken) == KERNEL_SYNC_OK);
    assert(closed == 1u);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_SYNC_CLOSING);
    assert(snapshot.references == 2u);
    assert(snapshot.close_result == ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_sync_wait(event, thread, 0u, 0u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_CLOSED);
    assert(kernel_sync_terminal_result(event) == ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_sync_owner_died(0x1001u, ASTRA_SYSCALL_PEER_DEAD,
                                  &closed, &woken) == KERNEL_SYNC_OK);
    assert(closed == 0u);
    assert(woken == 0u);
    kernel_sync_handle_release(event, NULL);
    kernel_sync_handle_release(event, NULL);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_SYNC_FREE);
    assert(kernel_sync_pool_valid());
}

static void test_pool_owner_and_waiter_limits(void)
{
    KernelSyncObject *objects[KERNEL_SYNC_OBJECT_MAX];
    KernelSyncObject *extra;
    KernelSyncPoolStats stats;
    KernelSyncSnapshot before;
    KernelSyncSnapshot after;
    KernelThread *thread;

    initialize_test();
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OWNER_MAX; ++slot)
        assert(kernel_sync_create_event(1u, 0u, &objects[slot]) ==
               KERNEL_SYNC_OK);
    assert(kernel_sync_create_event(1u, 0u, &extra) ==
           KERNEL_SYNC_QUOTA_EXCEEDED);
    for (uint32_t owner = 2u; owner <= 4u; ++owner) {
        for (uint32_t index = 0u; index < KERNEL_SYNC_OWNER_MAX; ++index) {
            uint32_t slot = (owner - 1u) * KERNEL_SYNC_OWNER_MAX + index;

            assert(kernel_sync_create_event(owner, 0u, &objects[slot]) ==
                   KERNEL_SYNC_OK);
        }
    }
    assert(kernel_sync_create_event(5u, 0u, &extra) == KERNEL_SYNC_NO_SLOT);
    assert(kernel_sync_snapshot(0u, &before));
    for (uint32_t slot = 0u; slot < KERNEL_SYNC_OBJECT_MAX; ++slot)
        kernel_sync_handle_release(objects[slot], NULL);
    assert(kernel_sync_create_event(1u, 0u, &extra) == KERNEL_SYNC_OK);
    assert(kernel_sync_snapshot(0u, &after));
    assert(after.generation != before.generation);
    kernel_sync_abandon_unpublished(extra);

    kernel_thread_pool_init();
    assert(kernel_sync_create_event(2u, 0u, &extra) == KERNEL_SYNC_OK);
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot)
        (void)allocate_thread(0u, slot, KERNEL_THREAD_PRIORITY_NORMAL);
    for (uint32_t index = 0u; index < KERNEL_THREAD_MAX; ++index) {
        thread = take_thread();
        assert(kernel_sync_wait(extra, thread, 0u,
                                KERNEL_THREAD_DEADLINE_NEVER,
                                ASTRA_SYSCALL_TIMED_OUT) ==
               KERNEL_SYNC_BLOCKED);
    }
    assert(kernel_sync_snapshot(0u, &after));
    assert(after.waiters == KERNEL_SYNC_WAITER_MAX);
    kernel_sync_handle_release(extra, NULL);
    assert(kernel_sync_pool_stats(&stats));
    assert(stats.max_live_objects == KERNEL_SYNC_OBJECT_MAX);
    assert(stats.quota_failures == 1u);
    assert(stats.allocation_failures == 1u);
    assert(stats.publication_rollbacks == 1u);
    assert(stats.max_waiters == KERNEL_SYNC_WAITER_MAX);
    assert(stats.live_objects == 0u);
    assert(stats.closing_objects == 0u);
}

static void test_invalid_creation_and_operations(void)
{
    KernelSyncObject *object = (KernelSyncObject *)(uintptr_t)1u;
    uint32_t closed;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(0u, 0u, &object) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(kernel_sync_create_event(1u, 4u, &object) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(kernel_sync_create_semaphore(1u, 2u, 1u, &object) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(kernel_sync_create_semaphore(1u, 0u, 0u, &object) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(kernel_sync_signal(NULL, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(kernel_sync_owner_died(0u, ASTRA_SYSCALL_PEER_DEAD,
                                  &closed, &woken) ==
           KERNEL_SYNC_INVALID_ARGUMENT);
    assert(!kernel_sync_snapshot(KERNEL_SYNC_OBJECT_MAX, NULL));
    assert(kernel_sync_pool_valid());
}

static void test_wait_set_sync_integration_and_duplicate_handoff(void)
{
    KernelSyncObject *event;
    KernelSyncObject *semaphore;
    KernelSyncSnapshot snapshot;
    KernelThreadWaitSpec specs[2];
    KernelThreadPoolStats thread_stats;
    KernelThread *thread;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(1u, 0u, &event) == KERNEL_SYNC_OK);
    assert(kernel_sync_create_semaphore(1u, 0u, 2u, &semaphore) ==
           KERNEL_SYNC_OK);
    thread = allocate_thread(0u, 0u, KERNEL_THREAD_PRIORITY_NORMAL);
    thread = take_thread();
    assert(kernel_sync_prepare_wait(event, &specs[0]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_prepare_wait(semaphore, &specs[1]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 0u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_sync_commit_wait(event) == KERNEL_SYNC_OK);
    assert(kernel_sync_commit_wait(semaphore) == KERNEL_SYNC_OK);
    assert(kernel_sync_signal(semaphore, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] == 1u);
    assert(thread->context.data[2] == 0u);
    assert(kernel_sync_snapshot(0u, &snapshot));
    assert(snapshot.waiters == 0u);
    assert(kernel_sync_snapshot(1u, &snapshot));
    assert(snapshot.waiters == 0u);
    assert(snapshot.count == 0u);

    /* One semaphore release wakes a thread once despite duplicate members. */
    thread = take_thread();
    assert(kernel_sync_prepare_wait(semaphore, &specs[0]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_prepare_wait(semaphore, &specs[1]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 0u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_sync_commit_wait(semaphore) == KERNEL_SYNC_OK);
    assert(kernel_sync_commit_wait(semaphore) == KERNEL_SYNC_OK);
    assert(kernel_sync_snapshot(1u, &snapshot));
    assert(snapshot.waiters == 2u);
    assert(kernel_sync_signal(semaphore, 1u, ASTRA_SYSCALL_OK, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] == 0u);
    assert(thread->context.data[2] == 0u);
    assert(kernel_sync_snapshot(1u, &snapshot));
    assert(snapshot.waiters == 0u);
    assert(snapshot.count == 0u);
    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.wait_set_blocks == 2u);
    assert(thread_stats.wait_set_wakeups == 2u);
    assert(thread_stats.wait_registrations == 0u);
    assert(kernel_thread_pool_valid());
    assert(kernel_sync_pool_valid());
    kernel_sync_handle_release(event, NULL);
    kernel_sync_handle_release(semaphore, NULL);
}

static void test_wait_set_owner_death_reports_winning_member(void)
{
    KernelSyncObject *first;
    KernelSyncObject *second;
    KernelThreadWaitSpec specs[2];
    KernelThreadPoolStats thread_stats;
    KernelThread *thread;
    uint32_t closed;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(1u, 0u, &first) == KERNEL_SYNC_OK);
    assert(kernel_sync_create_event(2u, 0u, &second) == KERNEL_SYNC_OK);
    thread = allocate_thread(0u, 0u, KERNEL_THREAD_PRIORITY_NORMAL);
    thread = take_thread();
    assert(kernel_sync_prepare_wait(first, &specs[0]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_prepare_wait(second, &specs[1]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 0u, 100u,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_sync_commit_wait(first) == KERNEL_SYNC_OK);
    assert(kernel_sync_commit_wait(second) == KERNEL_SYNC_OK);
    assert(kernel_sync_owner_died(2u, ASTRA_SYSCALL_PEER_DEAD,
                                  &closed, &woken) == KERNEL_SYNC_OK);
    assert(closed == 1u);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_PEER_DEAD);
    assert(thread->context.data[1] == 1u);
    assert(thread->context.data[2] == 0u);
    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.wait_registrations == 0u);
    assert(thread_stats.deadline_depth == 0u);
    assert(thread_stats.deadline_cancellations == 1u);
    assert(kernel_sync_pool_valid());
    kernel_sync_handle_release(first, NULL);
    kernel_sync_handle_release(second, NULL);
}

static void test_timer_heap_order_rearm_and_level_readiness(void)
{
    KernelSyncObject *first;
    KernelSyncObject *second;
    KernelSyncObject *third;
    KernelSyncPoolStats stats;
    KernelSyncSnapshot snapshot;
    KernelThread *thread;
    uint64_t deadline;
    uint32_t expired;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_timer(1u, &first) == KERNEL_SYNC_OK);
    assert(kernel_sync_create_timer(1u, &second) == KERNEL_SYNC_OK);
    assert(kernel_sync_create_timer(1u, &third) == KERNEL_SYNC_OK);
    assert(kernel_sync_timer_set(first, 0u, 300u, &woken) ==
           KERNEL_SYNC_OK);
    assert(woken == 0u);
    assert(kernel_sync_timer_set(second, 0u, 100u, &woken) ==
           KERNEL_SYNC_OK);
    assert(kernel_sync_timer_set(third, 0u, 200u, &woken) ==
           KERNEL_SYNC_OK);
    assert(kernel_sync_next_timer_deadline(&deadline));
    assert(deadline == 100u);
    assert(kernel_sync_snapshot(1u, &snapshot));
    assert(snapshot.type == KERNEL_SYNC_TIMER);
    assert(snapshot.deadline_high == 0u);
    assert(snapshot.deadline_low == 100u);

    thread = allocate_thread(0u, 0u, KERNEL_THREAD_PRIORITY_NORMAL);
    thread = take_thread();
    assert(kernel_sync_wait(second, thread, 0u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_expire_timers(99u, &expired, &woken) ==
           KERNEL_SYNC_OK);
    assert(expired == 0u && woken == 0u);
    assert(kernel_sync_expire_timers(100u, &expired, &woken) ==
           KERNEL_SYNC_OK);
    assert(expired == 1u && woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    thread = take_thread();
    assert(kernel_sync_wait(second, thread, 100u, 100u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);
    assert(kernel_sync_wait(second, thread, 100u, 100u,
                            ASTRA_SYSCALL_TIMED_OUT) == KERNEL_SYNC_OK);

    assert(kernel_sync_timer_set(second, 100u, 400u, &woken) ==
           KERNEL_SYNC_OK);
    assert(kernel_sync_next_timer_deadline(&deadline));
    assert(deadline == 200u);
    assert(kernel_sync_expire_timers(250u, &expired, &woken) ==
           KERNEL_SYNC_OK);
    assert(expired == 1u && woken == 0u);
    assert(kernel_sync_next_timer_deadline(&deadline));
    assert(deadline == 300u);
    assert(kernel_sync_timer_cancel(first, ASTRA_SYSCALL_CANCELLED,
                                    &woken) == KERNEL_SYNC_OK);
    assert(woken == 0u);
    assert(kernel_sync_next_timer_deadline(&deadline));
    assert(deadline == 400u);
    assert(kernel_sync_pool_stats(&stats));
    assert(stats.created_timers == 3u);
    assert(stats.timer_arms == 4u);
    assert(stats.timer_cancellations == 1u);
    assert(stats.timer_expirations == 2u);
    assert(stats.timer_wakeups == 1u);
    assert(stats.armed_timers == 1u);
    assert(stats.max_armed_timers == 3u);

    kernel_sync_handle_release(first, NULL);
    kernel_sync_handle_release(second, NULL);
    kernel_sync_handle_release(third, NULL);
    assert(!kernel_sync_next_timer_deadline(&deadline));
    assert(kernel_sync_pool_valid());
}

static void test_timer_wait_set_cancel_duplicate_and_close(void)
{
    KernelSyncObject *event;
    KernelSyncObject *timer;
    KernelThreadWaitSpec specs[2];
    KernelThreadPoolStats thread_stats;
    KernelThread *thread;
    uint32_t expired;
    uint32_t woken;

    initialize_test();
    assert(kernel_sync_create_event(1u, 0u, &event) == KERNEL_SYNC_OK);
    assert(kernel_sync_create_timer(1u, &timer) == KERNEL_SYNC_OK);
    assert(kernel_sync_timer_set(timer, 0u, 100u, &woken) ==
           KERNEL_SYNC_OK);
    thread = allocate_thread(0u, 0u, KERNEL_THREAD_PRIORITY_NORMAL);
    thread = take_thread();
    assert(kernel_sync_prepare_wait(event, &specs[0]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_prepare_wait(timer, &specs[1]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 0u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_sync_commit_wait(event) == KERNEL_SYNC_OK);
    assert(kernel_sync_commit_wait(timer) == KERNEL_SYNC_OK);
    assert(kernel_sync_timer_cancel(timer, ASTRA_SYSCALL_CANCELLED,
                                    &woken) == KERNEL_SYNC_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_CANCELLED);
    assert(thread->context.data[1] == 1u);
    assert(thread->context.data[2] == 0u);

    thread = take_thread();
    assert(kernel_sync_timer_set(timer, 100u, 200u, &woken) ==
           KERNEL_SYNC_OK);
    assert(kernel_sync_prepare_wait(timer, &specs[0]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_sync_prepare_wait(timer, &specs[1]) ==
           KERNEL_SYNC_BLOCKED);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 100u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_sync_commit_wait(timer) == KERNEL_SYNC_OK);
    assert(kernel_sync_commit_wait(timer) == KERNEL_SYNC_OK);
    assert(kernel_sync_expire_timers(200u, &expired, &woken) ==
           KERNEL_SYNC_OK);
    assert(expired == 1u && woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] == 0u);
    assert(thread->context.data[2] == 0u);

    thread = take_thread();
    assert(kernel_sync_timer_set(timer, 200u, 300u, &woken) ==
           KERNEL_SYNC_OK);
    assert(kernel_sync_wait(timer, thread, 200u,
                            KERNEL_THREAD_DEADLINE_NEVER,
                            ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_SYNC_BLOCKED);
    kernel_sync_handle_release(timer, NULL);
    assert(thread->context.data[0] == ASTRA_SYSCALL_CLOSED);
    assert(kernel_sync_expire_timers(300u, &expired, &woken) ==
           KERNEL_SYNC_OK);
    assert(expired == 0u && woken == 0u);
    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.wait_set_blocks == 2u);
    assert(thread_stats.wait_set_wakeups == 2u);
    assert(thread_stats.wait_registrations == 0u);
    kernel_sync_handle_release(event, NULL);
    assert(kernel_sync_pool_valid());
}

int main(void)
{
    test_auto_and_manual_event_semantics();
    test_semaphore_atomic_handoff_and_overflow();
    test_terminal_races_remove_wait_once();
    test_owner_death_closes_external_reference();
    test_pool_owner_and_waiter_limits();
    test_invalid_creation_and_operations();
    test_wait_set_sync_integration_and_duplicate_handoff();
    test_wait_set_owner_death_reports_winning_member();
    test_timer_heap_order_rearm_and_level_readiness();
    test_timer_wait_set_cancel_duplicate_and_close();
    puts("sync tests passed");
    return 0;
}
