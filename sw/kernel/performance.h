#ifndef ASTRA_KERNEL_PERFORMANCE_H
#define ASTRA_KERNEL_PERFORMANCE_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_PERFORMANCE_BUDGET_SYSCALL_DISPATCH 50000u
#define KERNEL_PERFORMANCE_BUDGET_TIMER_DISPATCH   50000u
#define KERNEL_PERFORMANCE_BUDGET_USER_FAULT      125000u
#define KERNEL_PERFORMANCE_BUDGET_SCHEDULER_PICK  10000u
#define KERNEL_PERFORMANCE_BUDGET_SAME_CRP_SWITCH 15000u
#define KERNEL_PERFORMANCE_BUDGET_CROSS_CRP_SWITCH 50000u
#define KERNEL_PERFORMANCE_BUDGET_WAIT_BLOCK      15000u
#define KERNEL_PERFORMANCE_BUDGET_WAKE            15000u

typedef enum KernelPerformanceMetric {
    KERNEL_PERFORMANCE_SYSCALL_DISPATCH = 0,
    KERNEL_PERFORMANCE_TIMER_DISPATCH,
    KERNEL_PERFORMANCE_USER_FAULT,
    KERNEL_PERFORMANCE_SCHEDULER_PICK,
    KERNEL_PERFORMANCE_SAME_CRP_SWITCH,
    KERNEL_PERFORMANCE_CROSS_CRP_SWITCH,
    KERNEL_PERFORMANCE_WAIT_BLOCK,
    KERNEL_PERFORMANCE_WAKE,
    KERNEL_PERFORMANCE_METRIC_COUNT
} KernelPerformanceMetric;

#define KERNEL_PERFORMANCE_REQUIRED_MASK \
    ((1u << KERNEL_PERFORMANCE_METRIC_COUNT) - 1u)
#define KERNEL_PERFORMANCE_METRIC_MASK(metric) (1u << (uint32_t)(metric))
#define KERNEL_PERFORMANCE_SOAK_MASK \
    (KERNEL_PERFORMANCE_METRIC_MASK( \
         KERNEL_PERFORMANCE_SYSCALL_DISPATCH) | \
     KERNEL_PERFORMANCE_METRIC_MASK( \
         KERNEL_PERFORMANCE_TIMER_DISPATCH) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_USER_FAULT) | \
     KERNEL_PERFORMANCE_METRIC_MASK( \
         KERNEL_PERFORMANCE_SCHEDULER_PICK) | \
     KERNEL_PERFORMANCE_METRIC_MASK( \
         KERNEL_PERFORMANCE_CROSS_CRP_SWITCH))

typedef struct KernelPerformanceToken {
    uint32_t started;
    uint8_t metric;
    uint8_t active;
    uint16_t generation;
} KernelPerformanceToken;

typedef struct KernelPerformanceMetricStats {
    uint32_t calls;
    uint32_t samples;
    uint32_t minimum_cycles;
    uint32_t maximum_cycles;
    uint32_t latest_cycles;
    uint32_t total_cycles_low;
    uint32_t total_cycles_high;
    uint32_t budget_cycles;
    uint32_t overruns;
} KernelPerformanceMetricStats;

typedef struct KernelPerformanceStats {
    KernelPerformanceMetricStats metric[KERNEL_PERFORMANCE_METRIC_COUNT];
} KernelPerformanceStats;

extern uint8_t kernel_performance_sampling_enabled;

void kernel_performance_init(void);
void kernel_performance_freeze(void);
bool kernel_performance_start_window(uint32_t metric_mask);
KernelPerformanceToken kernel_performance_begin_sampled(
    KernelPerformanceMetric metric);
void kernel_performance_end_sampled(KernelPerformanceToken token);
void kernel_performance_record(KernelPerformanceMetric metric,
                               uint32_t cycles);
bool kernel_performance_stats(KernelPerformanceStats *stats);
bool kernel_performance_pass(const KernelPerformanceStats *stats,
                             uint32_t required_mask,
                             KernelPerformanceMetric *failed_metric);

static inline __attribute__((always_inline))
KernelPerformanceToken kernel_performance_begin(
    KernelPerformanceMetric metric)
{
    KernelPerformanceToken token = {0u, 0u, 0u, 0u};

    if (kernel_performance_sampling_enabled != 0u)
        token = kernel_performance_begin_sampled(metric);
    return token;
}

static inline __attribute__((always_inline))
void kernel_performance_end(KernelPerformanceToken token)
{
    if (token.active != 0u)
        kernel_performance_end_sampled(token);
}

#if defined(KERNEL_PERFORMANCE_HOST_TEST)
void kernel_performance_test_set_cycles(uint32_t current, uint32_t step);
#endif

#endif
