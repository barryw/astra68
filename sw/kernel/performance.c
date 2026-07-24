#include "performance.h"

#include "bytes.h"
#include "generation.h"
#include "platform.h"

#include <stddef.h>

static KernelPerformanceStats performance_stats;
static uint32_t performance_window_pending;
static uint16_t performance_generation;
static uint8_t performance_window_active;
uint8_t kernel_performance_sampling_enabled;

static const uint32_t performance_budgets[KERNEL_PERFORMANCE_METRIC_COUNT] = {
    [KERNEL_PERFORMANCE_SYSCALL_DISPATCH] =
        KERNEL_PERFORMANCE_BUDGET_SYSCALL_DISPATCH,
    [KERNEL_PERFORMANCE_TIMER_DISPATCH] =
        KERNEL_PERFORMANCE_BUDGET_TIMER_DISPATCH,
    [KERNEL_PERFORMANCE_USER_FAULT] =
        KERNEL_PERFORMANCE_BUDGET_USER_FAULT,
    [KERNEL_PERFORMANCE_SCHEDULER_PICK] =
        KERNEL_PERFORMANCE_BUDGET_SCHEDULER_PICK,
    [KERNEL_PERFORMANCE_SAME_CRP_SWITCH] =
        KERNEL_PERFORMANCE_BUDGET_SAME_CRP_SWITCH,
    [KERNEL_PERFORMANCE_CROSS_CRP_SWITCH] =
        KERNEL_PERFORMANCE_BUDGET_CROSS_CRP_SWITCH,
    [KERNEL_PERFORMANCE_WAIT_BLOCK] =
        KERNEL_PERFORMANCE_BUDGET_WAIT_BLOCK,
    [KERNEL_PERFORMANCE_WAKE] =
        KERNEL_PERFORMANCE_BUDGET_WAKE,
    [KERNEL_PERFORMANCE_DEADLINE_EXPIRE] =
        KERNEL_PERFORMANCE_BUDGET_DEADLINE_EXPIRE,
};

#if defined(KERNEL_PERFORMANCE_HOST_TEST)
static uint32_t host_cycles;
static uint32_t host_cycle_step = 1u;

void kernel_performance_test_set_cycles(uint32_t current, uint32_t step)
{
    host_cycles = current;
    host_cycle_step = step;
}

static uint32_t performance_cycles_low(void)
{
    uint32_t current = host_cycles;

    host_cycles += host_cycle_step;
    return current;
}
#else
static uint32_t performance_cycles_low(void)
{
    return kernel_platform_cpu_cycles_low();
}
#endif

static bool valid_metric(KernelPerformanceMetric metric)
{
    return (uint32_t)metric < KERNEL_PERFORMANCE_METRIC_COUNT;
}

void kernel_performance_init(void)
{
    kernel_bytes_clear(&performance_stats, sizeof(performance_stats));
    performance_window_pending = 0u;
    performance_generation = 1u;
    performance_window_active = 0u;
    for (uint32_t metric = 0u;
         metric < KERNEL_PERFORMANCE_METRIC_COUNT; ++metric)
        performance_stats.metric[metric].budget_cycles =
            performance_budgets[metric];
    kernel_performance_sampling_enabled = 1u;
}

void kernel_performance_freeze(void)
{
    performance_generation = (uint16_t)kernel_generation_next_masked(
        performance_generation, UINT16_MAX);
    kernel_performance_sampling_enabled = 0u;
    performance_window_pending = 0u;
    performance_window_active = 0u;
}

bool kernel_performance_start_window(uint32_t metric_mask)
{
    if (metric_mask == 0u ||
        (metric_mask & ~KERNEL_PERFORMANCE_REQUIRED_MASK) != 0u ||
        kernel_performance_sampling_enabled != 0u)
        return false;
    performance_generation = (uint16_t)kernel_generation_next_masked(
        performance_generation, UINT16_MAX);
    performance_window_pending = metric_mask;
    performance_window_active = 1u;
    kernel_performance_sampling_enabled = 1u;
    return true;
}

KernelPerformanceToken kernel_performance_begin_sampled(
    KernelPerformanceMetric metric)
{
    KernelPerformanceToken token = {0u, 0u, 0u, 0u};
    KernelPerformanceMetricStats *stats;
    uint32_t metric_mask;

    if (!valid_metric(metric) || kernel_performance_sampling_enabled == 0u)
        return token;
    metric_mask = KERNEL_PERFORMANCE_METRIC_MASK(metric);
    if (performance_window_active != 0u &&
        (performance_window_pending & metric_mask) == 0u)
        return token;
    stats = &performance_stats.metric[metric];
    if (stats->calls != UINT32_MAX)
        ++stats->calls;
    token.started = performance_cycles_low();
    token.metric = (uint8_t)metric;
    token.active = 1u;
    token.generation = performance_generation;
    return token;
}

void kernel_performance_record(KernelPerformanceMetric metric,
                               uint32_t cycles)
{
    KernelPerformanceMetricStats *stats;
    uint32_t previous_total;

    if (!valid_metric(metric))
        return;
    stats = &performance_stats.metric[metric];
    if (stats->samples != UINT32_MAX)
        ++stats->samples;
    if (stats->samples == 1u || cycles < stats->minimum_cycles)
        stats->minimum_cycles = cycles;
    if (cycles > stats->maximum_cycles)
        stats->maximum_cycles = cycles;
    stats->latest_cycles = cycles;
    previous_total = stats->total_cycles_low;
    stats->total_cycles_low += cycles;
    if (stats->total_cycles_low < previous_total)
        ++stats->total_cycles_high;
    if (cycles > stats->budget_cycles && stats->overruns != UINT32_MAX)
        ++stats->overruns;
}

void kernel_performance_end_sampled(KernelPerformanceToken token)
{
    uint32_t finished;

    if (token.active == 0u || token.generation != performance_generation ||
        token.metric >= KERNEL_PERFORMANCE_METRIC_COUNT)
        return;
    finished = performance_cycles_low();
    kernel_performance_record((KernelPerformanceMetric)token.metric,
                              finished - token.started);
    if (performance_window_active != 0u) {
        performance_window_pending &=
            ~KERNEL_PERFORMANCE_METRIC_MASK(token.metric);
        if (performance_window_pending == 0u) {
            performance_generation =
                (uint16_t)kernel_generation_next_masked(
                    performance_generation, UINT16_MAX);
            performance_window_active = 0u;
            kernel_performance_sampling_enabled = 0u;
        }
    }
}

bool kernel_performance_stats(KernelPerformanceStats *stats)
{
    if (stats == NULL)
        return false;
    kernel_bytes_copy(stats, &performance_stats, sizeof(*stats));
    return true;
}

bool kernel_performance_pass(const KernelPerformanceStats *stats,
                             uint32_t required_mask,
                             KernelPerformanceMetric *failed_metric)
{
    uint32_t valid_mask = KERNEL_PERFORMANCE_REQUIRED_MASK;

    if (stats == NULL || (required_mask & ~valid_mask) != 0u)
        return false;
    for (uint32_t metric = 0u;
         metric < KERNEL_PERFORMANCE_METRIC_COUNT; ++metric) {
        const KernelPerformanceMetricStats *metric_stats =
            &stats->metric[metric];

        if ((required_mask & (1u << metric)) == 0u)
            continue;
        if (metric_stats->samples == 0u ||
            metric_stats->maximum_cycles == 0u ||
            metric_stats->maximum_cycles > metric_stats->budget_cycles ||
            metric_stats->overruns != 0u) {
            if (failed_metric != NULL)
                *failed_metric = (KernelPerformanceMetric)metric;
            return false;
        }
    }
    return true;
}
