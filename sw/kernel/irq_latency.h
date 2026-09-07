#ifndef ASTRA_KERNEL_IRQ_LATENCY_H
#define ASTRA_KERNEL_IRQ_LATENCY_H

#include <stdbool.h>
#include <stdint.h>

/* Ten milliseconds at Vesta's 12.5 MHz architectural cycle clock. */
#define KERNEL_IRQOFF_LATENCY_BUDGET_CYCLES 125000u

typedef struct KernelIrqOffLatencyStats {
    uint32_t samples;
    uint32_t maximum_cycles;
    uint32_t maximum_sample;
    uint32_t nested_entries;
    uint8_t active;
    uint8_t reserved[3];
} KernelIrqOffLatencyStats;

void kernel_irqoff_latency_init(void);
void kernel_irqoff_latency_freeze(void);
void kernel_irqoff_latency_enter(void);
void kernel_irqoff_latency_exit(void);
bool kernel_irqoff_latency_stats(KernelIrqOffLatencyStats *stats);
bool kernel_irqoff_latency_within_budget(
    const KernelIrqOffLatencyStats *stats);

#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
#define KERNEL_IRQOFF_TRACE_SAMPLE_MAX 64u
uint32_t kernel_irqoff_latency_trace_count(void);
uint32_t kernel_irqoff_latency_trace_sample(uint32_t index);
#endif

#endif
