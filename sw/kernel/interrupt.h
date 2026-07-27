#ifndef ASTRA_KERNEL_INTERRUPT_H
#define ASTRA_KERNEL_INTERRUPT_H

#include "irq.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_INTERRUPT_DEFERRED_DEPTH 32u

typedef enum KernelInterruptDispatchResult {
    KERNEL_INTERRUPT_NONE = 0,
    KERNEL_INTERRUPT_TIMER,
    KERNEL_INTERRUPT_DEVICE,
    KERNEL_INTERRUPT_QUARANTINED,
    KERNEL_INTERRUPT_FATAL
} KernelInterruptDispatchResult;

typedef struct KernelInterruptStats {
    uint32_t pending;
    uint32_t maximum_pending;
    uint32_t queued;
    uint32_t dispatched;
    uint32_t dropped;
} KernelInterruptStats;

bool kernel_interrupt_init(uint32_t cpu_hz);
bool kernel_interrupt_schedule_device_reset(void);
bool kernel_interrupt_device_binding(uint8_t source,
                                     KernelIrqBinding *binding);
KernelInterruptDispatchResult kernel_interrupt_dispatch(
    uint32_t *woken_threads);
bool kernel_interrupt_stats(KernelInterruptStats *stats);

#endif
