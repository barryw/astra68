#include "irq_latency.h"

#include "performance.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    KernelIrqOffLatencyStats stats;

    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 19u);
    kernel_irqoff_latency_init();
    kernel_irqoff_latency_exit();
    kernel_irqoff_latency_enter();
    kernel_irqoff_latency_enter();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(stats.active == 1u);
    assert(stats.nested_entries == 1u);
    kernel_irqoff_latency_exit();
    kernel_irqoff_latency_exit();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(stats.active == 0u);
    assert(stats.samples == 1u);
    assert(stats.maximum_cycles == 19u);
    assert(stats.maximum_sample == 1u);

    kernel_irqoff_latency_enter();
    kernel_performance_test_set_cycles(500u, 37u);
    kernel_irqoff_latency_exit();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(stats.samples == 2u);
    assert(stats.maximum_cycles == 362u);
    assert(stats.maximum_sample == 2u);
    assert(kernel_irqoff_latency_within_budget(&stats));
#if defined(ASTRA_KERNEL_SCHED_TRACE) && ASTRA_KERNEL_SCHED_TRACE
    assert(kernel_irqoff_latency_trace_count() == 2u);
    assert(kernel_irqoff_latency_trace_sample(0u) == 19u);
    assert(kernel_irqoff_latency_trace_sample(1u) == 362u);
    assert(kernel_irqoff_latency_trace_sample(2u) == 0u);
#endif

    kernel_irqoff_latency_enter();
    kernel_irqoff_latency_freeze();
    kernel_irqoff_latency_exit();
    kernel_irqoff_latency_enter();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(stats.active == 0u);
    assert(stats.samples == 2u);
    assert(stats.maximum_cycles == 362u);
    assert(kernel_irqoff_latency_within_budget(&stats));

    kernel_irqoff_latency_init();
    kernel_performance_test_set_cycles(
        0u, KERNEL_IRQOFF_LATENCY_BUDGET_CYCLES + 1u);
    kernel_irqoff_latency_enter();
    kernel_irqoff_latency_exit();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(!kernel_irqoff_latency_within_budget(&stats));
    puts("interrupt-disabled latency tests passed");
    return 0;
}
