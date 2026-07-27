#include "irq_latency.h"

#include "bytes.h"
#include "performance.h"

#include <stddef.h>

static KernelIrqOffLatencyStats latency_stats;
static uint32_t latency_started;

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

void kernel_irqoff_latency_init(void)
{
    kernel_bytes_clear(&latency_stats, sizeof(latency_stats));
    latency_started = 0u;
}

void kernel_irqoff_latency_enter(void)
{
    if (latency_stats.active != 0u) {
        increment_saturating(&latency_stats.nested_entries);
        return;
    }
    latency_started = kernel_performance_cycles_low();
    latency_stats.active = 1u;
}

void kernel_irqoff_latency_exit(void)
{
    uint32_t elapsed;

    if (latency_stats.active == 0u)
        return;
    elapsed = kernel_performance_cycles_low() - latency_started;
    if (elapsed > latency_stats.maximum_cycles)
        latency_stats.maximum_cycles = elapsed;
    increment_saturating(&latency_stats.samples);
    latency_stats.active = 0u;
}

bool kernel_irqoff_latency_stats(KernelIrqOffLatencyStats *stats)
{
    if (stats == NULL)
        return false;
    kernel_bytes_copy(stats, &latency_stats, sizeof(*stats));
    return true;
}
