#include "event.h"

#include <stddef.h>

static bool valid_event(const KernelEvent *event)
{
    return event != NULL && event->reserved[0] == 0u &&
           event->reserved[1] == 0u && event->signaled <= 1u &&
           event->closed <= 1u &&
           kernel_thread_wait_queue_count(&event->waiters) != UINT32_MAX;
}

void kernel_event_init(KernelEvent *event, bool initially_signaled)
{
    if (event == NULL)
        return;
    kernel_thread_wait_queue_init(&event->waiters);
    event->signaled = initially_signaled ? 1u : 0u;
    event->closed = 0u;
    event->reserved[0] = 0u;
    event->reserved[1] = 0u;
}

KernelEventStatus kernel_event_wait(KernelEvent *event, KernelThread *thread)
{
    return kernel_event_wait_until(event, thread, 0u,
                                   KERNEL_THREAD_DEADLINE_NEVER, 0u);
}

KernelEventStatus kernel_event_wait_until(
    KernelEvent *event, KernelThread *thread, uint64_t now,
    uint64_t deadline, uint32_t timeout_result)
{
    KernelThreadStatus status;
    uint32_t sequence;

    if (!valid_event(event) || thread == NULL)
        return KERNEL_EVENT_INVALID_ARGUMENT;
    if (event->closed != 0u)
        return KERNEL_EVENT_CLOSED;
    if (event->signaled != 0u) {
        event->signaled = 0u;
        return KERNEL_EVENT_OK;
    }

    sequence = kernel_thread_wait_queue_sequence(&event->waiters);
    if (sequence == 0u)
        return KERNEL_EVENT_CORRUPT;
    status = kernel_thread_block_until(thread, &event->waiters, sequence,
                                       now, deadline, timeout_result);
    if (status == KERNEL_THREAD_OK)
        return KERNEL_EVENT_BLOCKED;
    if (status == KERNEL_THREAD_DEADLINE_EXPIRED)
        return KERNEL_EVENT_TIMED_OUT;
    if (status == KERNEL_THREAD_CONDITION_CHANGED)
        return KERNEL_EVENT_INVALID_STATE;
    return status == KERNEL_THREAD_INVALID_ARGUMENT ||
                   status == KERNEL_THREAD_INVALID_STATE ?
        KERNEL_EVENT_INVALID_STATE : KERNEL_EVENT_CORRUPT;
}

KernelEventStatus kernel_event_signal(KernelEvent *event,
                                      uint32_t wake_result,
                                      KernelThread **woken_thread)
{
    KernelThreadStatus status;

    if (!valid_event(event) || woken_thread == NULL)
        return KERNEL_EVENT_INVALID_ARGUMENT;
    *woken_thread = NULL;
    if (event->closed != 0u)
        return KERNEL_EVENT_CLOSED;
    status = kernel_thread_wake_one(&event->waiters, wake_result,
                                    woken_thread);
    if (status == KERNEL_THREAD_NO_RUNNABLE) {
        event->signaled = 1u;
        return KERNEL_EVENT_OK;
    }
    if (status != KERNEL_THREAD_OK)
        return KERNEL_EVENT_CORRUPT;
    event->signaled = 0u;
    return KERNEL_EVENT_OK;
}

KernelEventStatus kernel_event_close(KernelEvent *event,
                                     uint32_t wake_result,
                                     uint32_t *woken_threads)
{
    if (!valid_event(event))
        return KERNEL_EVENT_INVALID_ARGUMENT;
    if (event->closed != 0u)
        return KERNEL_EVENT_CLOSED;
    event->closed = 1u;
    event->signaled = 0u;
    return kernel_thread_wake_all(&event->waiters, wake_result,
                                  woken_threads) == KERNEL_THREAD_OK ?
        KERNEL_EVENT_OK : KERNEL_EVENT_CORRUPT;
}

uint32_t kernel_event_waiter_count(const KernelEvent *event)
{
    return valid_event(event) ?
        kernel_thread_wait_queue_count(&event->waiters) : UINT32_MAX;
}
