#include "event.h"

#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static KernelThread *make_running_thread(uint8_t priority)
{
    KernelThread *thread;
    KernelThread *selected;

    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, priority,
                                  &thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == thread);
    return thread;
}

static void test_auto_reset_signal_and_wait(void)
{
    KernelEvent event;
    KernelThread *thread;
    KernelThread *woken = (KernelThread *)(uintptr_t)1u;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_event_init(&event, false);
    thread = make_running_thread(KERNEL_THREAD_PRIORITY_NORMAL);
    assert(kernel_event_signal(&event, 0x1111u, &woken) == KERNEL_EVENT_OK);
    assert(woken == NULL);
    assert(kernel_event_wait(&event, thread) == KERNEL_EVENT_OK);
    assert(thread->state == KERNEL_THREAD_RUNNING);
    assert(kernel_event_wait(&event, thread) == KERNEL_EVENT_BLOCKED);
    assert(thread->state == KERNEL_THREAD_BLOCKED);
    assert(kernel_event_waiter_count(&event) == 1u);
    assert(kernel_event_signal(&event, 0x2222u, &woken) == KERNEL_EVENT_OK);
    assert(woken == thread);
    assert(thread->state == KERNEL_THREAD_READY);
    assert(thread->context.data[0] == 0x2222u);
    assert(kernel_event_waiter_count(&event) == 0u);
}

static void test_close_wakes_waiters_once(void)
{
    KernelEvent event;
    KernelThread *first;
    KernelThread *second;
    KernelThread *selected;
    uint32_t woken = 0u;

    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_event_init(&event, false);
    assert(kernel_thread_allocate(1u, 0x10000001u, 0u, 0x00100000u,
                                  0x70001000u, 0u, 20u,
                                  &first) == KERNEL_THREAD_OK);
    assert(kernel_thread_allocate(1u, 0x10000001u, 1u, 0x00100010u,
                                  0x70003000u, 0u, 12u,
                                  &second) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(first) == KERNEL_THREAD_OK);
    assert(kernel_thread_publish(second) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == first);
    assert(kernel_event_wait(&event, selected) == KERNEL_EVENT_BLOCKED);
    assert(kernel_thread_take_next(&selected) == KERNEL_THREAD_OK);
    assert(selected == second);
    assert(kernel_event_wait(&event, selected) == KERNEL_EVENT_BLOCKED);
    assert(kernel_event_close(&event, 0x3333u, &woken) == KERNEL_EVENT_OK);
    assert(woken == 2u);
    assert(first->state == KERNEL_THREAD_READY);
    assert(second->state == KERNEL_THREAD_READY);
    assert(first->context.data[0] == 0x3333u);
    assert(second->context.data[0] == 0x3333u);
    assert(kernel_event_wait(&event, first) == KERNEL_EVENT_CLOSED);
    assert(kernel_event_close(&event, 0u, NULL) == KERNEL_EVENT_CLOSED);
}

int main(void)
{
    test_auto_reset_signal_and_wait();
    test_close_wakes_waiters_once();
    puts("event tests passed");
    return 0;
}
