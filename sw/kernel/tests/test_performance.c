#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_bounded_sampling_windows_and_totals(void)
{
    KernelPerformanceStats stats;
    KernelPerformanceToken token;

    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 7u);
    for (uint32_t call = 0u; call < 3u; ++call) {
        token = kernel_performance_begin(
            KERNEL_PERFORMANCE_SCHEDULER_PICK);
        kernel_performance_end(token);
    }
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].calls == 3u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].samples == 3u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].minimum_cycles ==
           7u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].maximum_cycles ==
           7u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].total_cycles_low ==
           21u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].total_cycles_high ==
           0u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].overruns == 0u);

    token = kernel_performance_begin(
        KERNEL_PERFORMANCE_SCHEDULER_PICK);
    assert(token.active != 0u);
    kernel_performance_freeze();
    kernel_performance_end(token);
    token = kernel_performance_begin(KERNEL_PERFORMANCE_SCHEDULER_PICK);
    assert(token.active == 0u);
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].calls == 4u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].samples == 3u);
    assert(!kernel_performance_start_window(0u));
    assert(!kernel_performance_start_window(
        1u << KERNEL_PERFORMANCE_METRIC_COUNT));
    assert(kernel_performance_start_window(
        KERNEL_PERFORMANCE_METRIC_MASK(
            KERNEL_PERFORMANCE_SCHEDULER_PICK) |
        KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_WAKE)));
    assert(!kernel_performance_start_window(
        KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_WAKE)));

    token = kernel_performance_begin(KERNEL_PERFORMANCE_WAIT_BLOCK);
    assert(token.active == 0u);
    token = kernel_performance_begin(
        KERNEL_PERFORMANCE_SCHEDULER_PICK);
    kernel_performance_end(token);
    token = kernel_performance_begin(
        KERNEL_PERFORMANCE_SCHEDULER_PICK);
    assert(token.active == 0u);
    token = kernel_performance_begin(KERNEL_PERFORMANCE_WAKE);
    kernel_performance_end(token);
    assert(kernel_performance_sampling_enabled == 0u);

    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].calls == 5u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].samples == 4u);
    assert(stats.metric[KERNEL_PERFORMANCE_SCHEDULER_PICK].total_cycles_low ==
           28u);
    assert(stats.metric[KERNEL_PERFORMANCE_WAKE].calls == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_WAKE].samples == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_WAIT_BLOCK].calls == 0u);
}

static void test_required_metrics_and_budget_failure(void)
{
    KernelPerformanceMetric failed = KERNEL_PERFORMANCE_METRIC_COUNT;
    KernelPerformanceStats stats;
    KernelPerformanceToken token;

    kernel_performance_init();
    kernel_performance_test_set_cycles(
        0u, KERNEL_PERFORMANCE_BUDGET_WAIT_BLOCK + 1u);
    token = kernel_performance_begin(KERNEL_PERFORMANCE_WAIT_BLOCK);
    kernel_performance_end(token);
    assert(kernel_performance_stats(&stats));
    assert(!kernel_performance_pass(
        &stats, 1u << KERNEL_PERFORMANCE_WAIT_BLOCK, &failed));
    assert(failed == KERNEL_PERFORMANCE_WAIT_BLOCK);
    assert(!kernel_performance_pass(
        &stats, 1u << KERNEL_PERFORMANCE_WAKE, &failed));
    assert(failed == KERNEL_PERFORMANCE_WAKE);

    kernel_performance_init();
    kernel_performance_test_set_cycles(0u, 1u);
    token = kernel_performance_begin(KERNEL_PERFORMANCE_DEADLINE_EXPIRE);
    kernel_performance_end(token);
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_DEADLINE_EXPIRE].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_DEADLINE_EXPIRE);
    assert(stats.metric[KERNEL_PERFORMANCE_THREAD_CREATE].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_THREAD_CREATE);
    assert(stats.metric[KERNEL_PERFORMANCE_THREAD_EXIT].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_THREAD_EXIT);
    assert(stats.metric[KERNEL_PERFORMANCE_THREAD_REAP].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_THREAD_REAP);
    assert(stats.metric[KERNEL_PERFORMANCE_PORT_SEND].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_PORT_SEND);
    assert(stats.metric[KERNEL_PERFORMANCE_PORT_RECEIVE].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_PORT_RECEIVE);
    assert(stats.metric[KERNEL_PERFORMANCE_AREA_CREATE].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_AREA_CREATE);
    assert(stats.metric[KERNEL_PERFORMANCE_AREA_MAP].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_AREA_MAP);
    assert(stats.metric[KERNEL_PERFORMANCE_AREA_UNMAP].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_AREA_UNMAP);
    assert(stats.metric[KERNEL_PERFORMANCE_RING_NOTIFY].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_RING_NOTIFY);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_HARD_IRQ);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE);
    assert(stats.metric[KERNEL_PERFORMANCE_IRQ_READ].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_IRQ_READ);
    assert(stats.metric[KERNEL_PERFORMANCE_IRQ_ACK].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_IRQ_ACK);
    assert(stats.metric[KERNEL_PERFORMANCE_DEVICE_BATCH].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_DEVICE_BATCH);
    assert(stats.metric[KERNEL_PERFORMANCE_MONITOR_COMMAND].budget_cycles ==
           KERNEL_PERFORMANCE_BUDGET_MONITOR_COMMAND);
    assert((KERNEL_PERFORMANCE_REQUIRED_MASK &
            KERNEL_PERFORMANCE_K10_MASK) == 0u);
    assert((KERNEL_PERFORMANCE_VALID_MASK & KERNEL_PERFORMANCE_K10_MASK) ==
           KERNEL_PERFORMANCE_K10_MASK);
    assert(KERNEL_PERFORMANCE_RELEASE_MASK ==
           KERNEL_PERFORMANCE_VALID_MASK);
    assert(kernel_performance_pass(
        &stats, 1u << KERNEL_PERFORMANCE_DEADLINE_EXPIRE, &failed));
}

static void test_conditional_span_selects_one_metric(void)
{
    KernelPerformanceSpan span;
    KernelPerformanceStats stats;

    kernel_performance_init();
    kernel_performance_test_set_cycles(1000u, 23u);
    span = kernel_performance_span_begin();
    kernel_performance_span_end(span, KERNEL_PERFORMANCE_HARD_IRQ_WAKE);
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ].calls == 0u);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ].samples == 0u);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].calls == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].samples == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].maximum_cycles ==
           23u);

    kernel_performance_freeze();
    assert(kernel_performance_start_window(
        KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_HARD_IRQ)));
    span = kernel_performance_span_begin();
    kernel_performance_span_end(span, KERNEL_PERFORMANCE_HARD_IRQ_WAKE);
    assert(kernel_performance_sampling_enabled != 0u);
    span = kernel_performance_span_begin();
    kernel_performance_span_end(span, KERNEL_PERFORMANCE_HARD_IRQ);
    assert(kernel_performance_sampling_enabled == 0u);
}

int main(void)
{
    test_bounded_sampling_windows_and_totals();
    test_required_metrics_and_budget_failure();
    test_conditional_span_selects_one_metric();
    puts("performance tests passed");
    return 0;
}
