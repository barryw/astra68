#include "performance.h"

#include "bytes.h"
#include "generation.h"
#include "platform.h"

#include <stddef.h>

static KernelPerformanceStats performance_stats;
static uint32_t performance_window_pending;
static volatile uint32_t performance_interrupt_epoch;
static uint32_t performance_interrupt_depth;
static uint16_t performance_generation;
static uint8_t performance_window_active;
uint8_t kernel_performance_sampling_enabled;

#define KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT 64u

typedef struct PerformanceInterruptRecord {
    uint32_t sequence;
    uint32_t cycles;
} PerformanceInterruptRecord;

static volatile PerformanceInterruptRecord performance_interrupt_records[
    KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT];

_Static_assert((KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT &
                (KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT - 1u)) == 0u,
               "interrupt performance record count must be a power of two");

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
    [KERNEL_PERFORMANCE_WAIT_SET_BLOCK] =
        KERNEL_PERFORMANCE_BUDGET_WAIT_SET_BLOCK,
    [KERNEL_PERFORMANCE_WAIT_SET_WAKE] =
        KERNEL_PERFORMANCE_BUDGET_WAIT_SET_WAKE,
    [KERNEL_PERFORMANCE_DEADLINE_EXPIRE] =
        KERNEL_PERFORMANCE_BUDGET_DEADLINE_EXPIRE,
    [KERNEL_PERFORMANCE_THREAD_CREATE] =
        KERNEL_PERFORMANCE_BUDGET_THREAD_CREATE,
    [KERNEL_PERFORMANCE_THREAD_EXIT] =
        KERNEL_PERFORMANCE_BUDGET_THREAD_EXIT,
    [KERNEL_PERFORMANCE_THREAD_REAP] =
        KERNEL_PERFORMANCE_BUDGET_THREAD_REAP,
    [KERNEL_PERFORMANCE_PORT_SEND] =
        KERNEL_PERFORMANCE_BUDGET_PORT_SEND,
    [KERNEL_PERFORMANCE_PORT_RECEIVE] =
        KERNEL_PERFORMANCE_BUDGET_PORT_RECEIVE,
    [KERNEL_PERFORMANCE_AREA_CREATE] =
        KERNEL_PERFORMANCE_BUDGET_AREA_CREATE,
    [KERNEL_PERFORMANCE_AREA_MAP] =
        KERNEL_PERFORMANCE_BUDGET_AREA_MAP,
    [KERNEL_PERFORMANCE_AREA_UNMAP] =
        KERNEL_PERFORMANCE_BUDGET_AREA_UNMAP,
    [KERNEL_PERFORMANCE_RING_NOTIFY] =
        KERNEL_PERFORMANCE_BUDGET_RING_NOTIFY,
    [KERNEL_PERFORMANCE_HARD_IRQ] =
        KERNEL_PERFORMANCE_BUDGET_HARD_IRQ,
    [KERNEL_PERFORMANCE_HARD_IRQ_WAKE] =
        KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE,
    [KERNEL_PERFORMANCE_IRQ_READ] =
        KERNEL_PERFORMANCE_BUDGET_IRQ_READ,
    [KERNEL_PERFORMANCE_IRQ_ACK] =
        KERNEL_PERFORMANCE_BUDGET_IRQ_ACK,
    [KERNEL_PERFORMANCE_DEVICE_BATCH] =
        KERNEL_PERFORMANCE_BUDGET_DEVICE_BATCH,
    [KERNEL_PERFORMANCE_MONITOR_COMMAND] =
        KERNEL_PERFORMANCE_BUDGET_MONITOR_COMMAND,
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

uint32_t kernel_performance_cycles_low(void)
{
    return performance_cycles_low();
}

void kernel_performance_init(void)
{
    kernel_bytes_clear(&performance_stats, sizeof(performance_stats));
    performance_window_pending = 0u;
    performance_interrupt_epoch = 0u;
    performance_interrupt_depth = 0u;
    for (uint32_t record = 0u;
         record < KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT; ++record) {
        performance_interrupt_records[record].sequence = 0u;
        performance_interrupt_records[record].cycles = 0u;
    }
    performance_generation = 1u;
    performance_window_active = 0u;
    for (uint32_t metric = 0u;
         metric < KERNEL_PERFORMANCE_METRIC_COUNT; ++metric)
        performance_stats.metric[metric].budget_cycles =
            performance_budgets[metric];
    kernel_performance_sampling_enabled = 1u;
}

KernelPerformanceInterruptToken kernel_performance_interrupt_enter(void)
{
    KernelPerformanceInterruptToken token = {0u, 0u};

    token.sequence = ++performance_interrupt_epoch;
    ++performance_interrupt_depth;
    if (performance_interrupt_depth == 1u)
        token.started = performance_cycles_low();
    return token;
}

void kernel_performance_interrupt_leave(
    KernelPerformanceInterruptToken token)
{
    volatile PerformanceInterruptRecord *record;
    uint32_t elapsed = 0u;

    if (performance_interrupt_depth == 0u)
        return;
    if (performance_interrupt_depth == 1u)
        elapsed = performance_cycles_low() - token.started;
    --performance_interrupt_depth;
    record = &performance_interrupt_records[
        token.sequence & (KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT - 1u)];
    record->cycles = elapsed;
    record->sequence = token.sequence;
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
        (metric_mask & ~KERNEL_PERFORMANCE_VALID_MASK) != 0u ||
        kernel_performance_sampling_enabled != 0u)
        return false;
    performance_generation = (uint16_t)kernel_generation_next_masked(
        performance_generation, UINT16_MAX);
    performance_window_pending = metric_mask;
    performance_window_active = 1u;
    kernel_performance_sampling_enabled = 1u;
    return true;
}

static void complete_window_metric(KernelPerformanceMetric metric)
{
    if (performance_window_active == 0u)
        return;
    performance_window_pending &= ~KERNEL_PERFORMANCE_METRIC_MASK(metric);
    if (performance_window_pending != 0u)
        return;
    performance_generation = (uint16_t)kernel_generation_next_masked(
        performance_generation, UINT16_MAX);
    performance_window_active = 0u;
    kernel_performance_sampling_enabled = 0u;
}

KernelPerformanceToken kernel_performance_begin_sampled(
    KernelPerformanceMetric metric)
{
    KernelPerformanceToken token = {
        0u, 0u, 0u, (uint8_t)KERNEL_PERFORMANCE_METRIC_COUNT
    };
    KernelPerformanceMetricStats *stats;
    uint32_t interrupt_epoch;
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
    do {
        interrupt_epoch = performance_interrupt_epoch;
        token.started = performance_cycles_low();
    } while (performance_interrupt_epoch != interrupt_epoch);
    token.interrupt_epoch = (uint16_t)interrupt_epoch;
    token.generation = (uint8_t)performance_generation;
    token.metric = (uint8_t)metric;
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

void kernel_performance_record_call(KernelPerformanceMetric metric,
                                    uint32_t cycles)
{
    KernelPerformanceMetricStats *stats;

    if (!valid_metric(metric))
        return;
    stats = &performance_stats.metric[metric];
    if (stats->calls != UINT32_MAX)
        ++stats->calls;
    kernel_performance_record(metric, cycles);
    complete_window_metric(metric);
}

static bool interrupt_cycles_between(uint16_t started_epoch,
                                     uint32_t finished_epoch,
                                     uint32_t *cycles)
{
    uint32_t count = (uint16_t)((uint16_t)finished_epoch - started_epoch);
    uint32_t first_sequence;
    uint32_t total = 0u;

    if (cycles == NULL ||
        count > KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT)
        return false;
    first_sequence = finished_epoch - count + 1u;
    for (uint32_t offset = 0u; offset < count; ++offset) {
        uint32_t sequence = first_sequence + offset;
        volatile const PerformanceInterruptRecord *record =
            &performance_interrupt_records[
                sequence &
                (KERNEL_PERFORMANCE_INTERRUPT_RECORD_COUNT - 1u)];
        uint32_t committed = record->sequence;
        uint32_t elapsed;

        if (committed != sequence)
            return false;
        elapsed = record->cycles;
        if (record->sequence != committed || UINT32_MAX - total < elapsed)
            return false;
        total += elapsed;
    }
    *cycles = total;
    return true;
}

void kernel_performance_end_sampled(KernelPerformanceToken token)
{
    uint32_t elapsed;
    uint32_t excluded;
    uint32_t finished;
    uint32_t interrupt_epoch;

    if (token.metric >= KERNEL_PERFORMANCE_METRIC_COUNT ||
        token.generation != (uint8_t)performance_generation)
        return;
    do {
        interrupt_epoch = performance_interrupt_epoch;
        finished = performance_cycles_low();
    } while (performance_interrupt_epoch != interrupt_epoch);
    elapsed = finished - token.started;
    if (interrupt_cycles_between(token.interrupt_epoch, interrupt_epoch,
                                 &excluded) && excluded <= elapsed)
        elapsed -= excluded;
    kernel_performance_record((KernelPerformanceMetric)token.metric,
                              elapsed);
    complete_window_metric((KernelPerformanceMetric)token.metric);
}

KernelPerformanceSpan kernel_performance_span_begin(void)
{
    KernelPerformanceSpan span = {0u, 0u, 0u, 0u};

    if (kernel_performance_sampling_enabled == 0u)
        return span;
    span.started = performance_cycles_low();
    span.generation = performance_generation;
    span.active = 1u;
    return span;
}

void kernel_performance_span_end(KernelPerformanceSpan span,
                                 KernelPerformanceMetric metric)
{
    KernelPerformanceMetricStats *stats;
    uint32_t finished;
    uint32_t metric_mask;

    if (span.active == 0u || span.generation != performance_generation ||
        !valid_metric(metric))
        return;
    metric_mask = KERNEL_PERFORMANCE_METRIC_MASK(metric);
    if (performance_window_active != 0u &&
        (performance_window_pending & metric_mask) == 0u)
        return;
    stats = &performance_stats.metric[metric];
    if (stats->calls != UINT32_MAX)
        ++stats->calls;
    finished = performance_cycles_low();
    kernel_performance_record(metric, finished - span.started);
    complete_window_metric(metric);
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
    uint32_t valid_mask = KERNEL_PERFORMANCE_VALID_MASK;

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
