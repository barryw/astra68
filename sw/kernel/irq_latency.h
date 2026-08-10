#ifndef ASTRA_KERNEL_IRQ_LATENCY_H
#define ASTRA_KERNEL_IRQ_LATENCY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct KernelIrqOffLatencyStats {
    uint32_t samples;
    uint32_t maximum_cycles;
    uint32_t nested_entries;
    uint8_t active;
    uint8_t reserved[3];
} KernelIrqOffLatencyStats;

void kernel_irqoff_latency_init(void);
void kernel_irqoff_latency_freeze(void);
void kernel_irqoff_latency_enter(void);
void kernel_irqoff_latency_exit(void);
bool kernel_irqoff_latency_stats(KernelIrqOffLatencyStats *stats);

#endif
