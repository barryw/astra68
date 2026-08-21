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
#define KERNEL_PERFORMANCE_BUDGET_WAIT_SET_BLOCK  50000u
#define KERNEL_PERFORMANCE_BUDGET_WAIT_SET_WAKE   50000u
#define KERNEL_PERFORMANCE_BUDGET_DEADLINE_EXPIRE 20000u
#define KERNEL_PERFORMANCE_BUDGET_THREAD_CREATE   150000u
#define KERNEL_PERFORMANCE_BUDGET_THREAD_EXIT      50000u
#define KERNEL_PERFORMANCE_BUDGET_THREAD_REAP     125000u
#define KERNEL_PERFORMANCE_BUDGET_PORT_SEND         25000u
#define KERNEL_PERFORMANCE_BUDGET_PORT_RECEIVE      30000u
#define KERNEL_PERFORMANCE_BUDGET_AREA_CREATE      250000u
#define KERNEL_PERFORMANCE_BUDGET_AREA_MAP         125000u
#define KERNEL_PERFORMANCE_BUDGET_AREA_UNMAP       100000u
#define KERNEL_PERFORMANCE_BUDGET_RING_NOTIFY       30000u
#define KERNEL_PERFORMANCE_BUDGET_HARD_IRQ            1250u
#define KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE       5000u
#define KERNEL_PERFORMANCE_BUDGET_IRQ_READ           15000u
#define KERNEL_PERFORMANCE_BUDGET_IRQ_ACK            20000u
#define KERNEL_PERFORMANCE_BUDGET_DEVICE_BATCH       50000u
#define KERNEL_PERFORMANCE_BUDGET_MONITOR_COMMAND   125000u
#define KERNEL_PERFORMANCE_BUDGET_LIBRARY_ATTACH   2500000u

typedef enum KernelPerformanceMetric {
    KERNEL_PERFORMANCE_SYSCALL_DISPATCH = 0,
    KERNEL_PERFORMANCE_TIMER_DISPATCH,
    KERNEL_PERFORMANCE_USER_FAULT,
    KERNEL_PERFORMANCE_SCHEDULER_PICK,
    KERNEL_PERFORMANCE_SAME_CRP_SWITCH,
    KERNEL_PERFORMANCE_CROSS_CRP_SWITCH,
    KERNEL_PERFORMANCE_WAIT_BLOCK,
    KERNEL_PERFORMANCE_WAKE,
    KERNEL_PERFORMANCE_WAIT_SET_BLOCK,
    KERNEL_PERFORMANCE_WAIT_SET_WAKE,
    KERNEL_PERFORMANCE_DEADLINE_EXPIRE,
    KERNEL_PERFORMANCE_THREAD_CREATE,
    KERNEL_PERFORMANCE_THREAD_EXIT,
    KERNEL_PERFORMANCE_THREAD_REAP,
    KERNEL_PERFORMANCE_PORT_SEND,
    KERNEL_PERFORMANCE_PORT_RECEIVE,
    KERNEL_PERFORMANCE_AREA_CREATE,
    KERNEL_PERFORMANCE_AREA_MAP,
    KERNEL_PERFORMANCE_AREA_UNMAP,
    KERNEL_PERFORMANCE_RING_NOTIFY,
    KERNEL_PERFORMANCE_HARD_IRQ,
    KERNEL_PERFORMANCE_HARD_IRQ_WAKE,
    KERNEL_PERFORMANCE_IRQ_READ,
    KERNEL_PERFORMANCE_IRQ_ACK,
    KERNEL_PERFORMANCE_DEVICE_BATCH,
    KERNEL_PERFORMANCE_MONITOR_COMMAND,
    KERNEL_PERFORMANCE_LIBRARY_ATTACH,
    KERNEL_PERFORMANCE_METRIC_COUNT
} KernelPerformanceMetric;

_Static_assert(KERNEL_PERFORMANCE_METRIC_COUNT < 32,
               "performance metric mask exceeds one word");

#define KERNEL_PERFORMANCE_METRIC_MASK(metric) (1u << (uint32_t)(metric))
#define KERNEL_PERFORMANCE_VALID_MASK \
    ((1u << KERNEL_PERFORMANCE_METRIC_COUNT) - 1u)
#define KERNEL_PERFORMANCE_REQUIRED_MASK \
    ((1u << KERNEL_PERFORMANCE_HARD_IRQ) - 1u)
#define KERNEL_PERFORMANCE_K10_MASK \
    (KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_HARD_IRQ) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_HARD_IRQ_WAKE) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_IRQ_READ) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_IRQ_ACK) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_DEVICE_BATCH) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_MONITOR_COMMAND) | \
     KERNEL_PERFORMANCE_METRIC_MASK(KERNEL_PERFORMANCE_LIBRARY_ATTACH))
#define KERNEL_PERFORMANCE_RELEASE_MASK \
    (KERNEL_PERFORMANCE_REQUIRED_MASK | KERNEL_PERFORMANCE_K10_MASK)
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
    uint16_t interrupt_epoch;
    uint8_t generation;
    uint8_t metric;
} KernelPerformanceToken;

_Static_assert(sizeof(KernelPerformanceToken) == 8u,
               "performance token must remain register-copyable");

typedef struct KernelPerformanceInterruptToken {
    uint32_t started;
    uint32_t sequence;
} KernelPerformanceInterruptToken;

_Static_assert(sizeof(KernelPerformanceInterruptToken) == 8u,
               "interrupt performance token must remain register-copyable");

typedef struct KernelPerformanceSpan {
    uint32_t started;
    uint16_t generation;
    uint8_t active;
    uint8_t reserved;
} KernelPerformanceSpan;

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
uint32_t kernel_performance_cycles_low(void);
KernelPerformanceInterruptToken kernel_performance_interrupt_enter(void);
void kernel_performance_interrupt_leave(
    KernelPerformanceInterruptToken token);
bool kernel_performance_start_window(uint32_t metric_mask);
KernelPerformanceToken kernel_performance_begin_sampled(
    KernelPerformanceMetric metric);
void kernel_performance_end_sampled(KernelPerformanceToken token);
KernelPerformanceSpan kernel_performance_span_begin(void);
void kernel_performance_span_end(KernelPerformanceSpan span,
                                 KernelPerformanceMetric metric);
void kernel_performance_record(KernelPerformanceMetric metric,
                               uint32_t cycles);
void kernel_performance_record_call(KernelPerformanceMetric metric,
                                    uint32_t cycles);
bool kernel_performance_stats(KernelPerformanceStats *stats);
bool kernel_performance_pass(const KernelPerformanceStats *stats,
                             uint32_t required_mask,
                             KernelPerformanceMetric *failed_metric);

static inline __attribute__((always_inline))
KernelPerformanceToken kernel_performance_begin(
    KernelPerformanceMetric metric)
{
    KernelPerformanceToken token = {
        0u, 0u, 0u, (uint8_t)KERNEL_PERFORMANCE_METRIC_COUNT
    };

    if (kernel_performance_sampling_enabled != 0u)
        token = kernel_performance_begin_sampled(metric);
    return token;
}

static inline __attribute__((always_inline))
void kernel_performance_end(KernelPerformanceToken token)
{
    if (token.metric < KERNEL_PERFORMANCE_METRIC_COUNT)
        kernel_performance_end_sampled(token);
}

#if defined(KERNEL_PERFORMANCE_HOST_TEST)
void kernel_performance_test_set_cycles(uint32_t current, uint32_t step);
#endif

#endif
