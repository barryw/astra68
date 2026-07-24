#ifndef ASTRA_KERNEL_EVENT_H
#define ASTRA_KERNEL_EVENT_H

#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum KernelEventStatus {
    KERNEL_EVENT_OK = 0,
    KERNEL_EVENT_BLOCKED,
    KERNEL_EVENT_CLOSED,
    KERNEL_EVENT_INVALID_ARGUMENT,
    KERNEL_EVENT_INVALID_STATE,
    KERNEL_EVENT_CORRUPT
} KernelEventStatus;

typedef struct KernelEvent {
    KernelThreadWaitQueue waiters;
    uint8_t signaled;
    uint8_t closed;
    uint8_t reserved[2];
} KernelEvent;

void kernel_event_init(KernelEvent *event, bool initially_signaled);
KernelEventStatus kernel_event_wait(KernelEvent *event, KernelThread *thread);
KernelEventStatus kernel_event_signal(KernelEvent *event,
                                      uint32_t wake_result,
                                      KernelThread **woken_thread);
KernelEventStatus kernel_event_close(KernelEvent *event,
                                     uint32_t wake_result,
                                     uint32_t *woken_threads);
uint32_t kernel_event_waiter_count(const KernelEvent *event);

#endif
