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

    kernel_irqoff_latency_enter();
    kernel_performance_test_set_cycles(500u, 37u);
    kernel_irqoff_latency_exit();
    assert(kernel_irqoff_latency_stats(&stats));
    assert(stats.samples == 2u);
    assert(stats.maximum_cycles == 362u);
    puts("interrupt-disabled latency tests passed");
    return 0;
}
