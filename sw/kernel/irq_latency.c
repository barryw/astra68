#include "irq_latency.h"

#include <astra/integer.h>

#include "bytes.h"
#include "performance.h"

#include <stddef.h>

static KernelIrqOffLatencyStats latency_stats;
static uint32_t latency_started;
static uint8_t latency_enabled;

#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
static uint32_t latency_trace[KERNEL_IRQOFF_TRACE_SAMPLE_MAX];
static uint32_t latency_trace_count;
#endif

void kernel_irqoff_latency_init(void)
{
    kernel_bytes_clear(&latency_stats, sizeof(latency_stats));
    latency_started = 0u;
    latency_enabled = 1u;
#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
    kernel_bytes_clear(latency_trace, sizeof(latency_trace));
    latency_trace_count = 0u;
#endif
}

void kernel_irqoff_latency_freeze(void)
{
    latency_enabled = 0u;
    latency_stats.active = 0u;
}

void kernel_irqoff_latency_enter(void)
{
    if (latency_enabled == 0u)
        return;
    if (latency_stats.active != 0u) {
        astra_u32_increment_saturating(&latency_stats.nested_entries);
        return;
    }
    latency_started = kernel_performance_cycles_low();
    latency_stats.active = 1u;
}

void kernel_irqoff_latency_exit(void)
{
    uint32_t elapsed;

    if (latency_enabled == 0u || latency_stats.active == 0u)
        return;
    elapsed = kernel_performance_cycles_low() - latency_started;
    astra_u32_increment_saturating(&latency_stats.samples);
    if (elapsed > latency_stats.maximum_cycles) {
        latency_stats.maximum_cycles = elapsed;
        latency_stats.maximum_sample = latency_stats.samples;
    }
#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
    if (latency_trace_count < KERNEL_IRQOFF_TRACE_SAMPLE_MAX)
        latency_trace[latency_trace_count++] = elapsed;
#endif
    latency_stats.active = 0u;
}

bool kernel_irqoff_latency_stats(KernelIrqOffLatencyStats *stats)
{
    if (stats == NULL)
        return false;
    kernel_bytes_copy(stats, &latency_stats, sizeof(*stats));
    return true;
}

bool kernel_irqoff_latency_within_budget(
    const KernelIrqOffLatencyStats *stats)
{
    return stats != NULL && stats->active == 0u && stats->samples != 0u &&
           stats->maximum_sample != 0u &&
           stats->maximum_sample <= stats->samples &&
           stats->maximum_cycles <= KERNEL_IRQOFF_LATENCY_BUDGET_CYCLES;
}

#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
uint32_t kernel_irqoff_latency_trace_count(void)
{
    return latency_trace_count;
}

uint32_t kernel_irqoff_latency_trace_sample(uint32_t index)
{
    return index < latency_trace_count ? latency_trace[index] : 0u;
}
#endif
