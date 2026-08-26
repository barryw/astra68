#include "interrupt.h"

#include <astra/compiler.h>
#include <astra/integer.h>

#include "irq.h"
#include "monitor.h"
#include "performance.h"
#include "platform.h"
#include "trace.h"
#include "vesta.h"
#include "worker.h"

#include <stddef.h>

typedef struct KernelDeferredInterrupt {
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t elapsed_cycles;
    uint8_t source;
    uint8_t vector;
    uint8_t reserved[2];
} KernelDeferredInterrupt;

static KernelDeferredInterrupt deferred_interrupts[
    KERNEL_INTERRUPT_DEFERRED_DEPTH];
static volatile uint32_t deferred_write_sequence;
static volatile uint32_t deferred_read_sequence;
static uint32_t deferred_maximum_pending;
static uint32_t deferred_queued;
static uint32_t deferred_dispatched;
static uint32_t deferred_dropped;

_Static_assert(sizeof(KernelDeferredInterrupt) == 16u,
               "deferred interrupt record size changed");
_Static_assert((KERNEL_INTERRUPT_DEFERRED_DEPTH &
                (KERNEL_INTERRUPT_DEFERRED_DEPTH - 1u)) == 0u,
               "deferred interrupt depth must be a power of two");

static void clear_deferred_interrupts(void)
{
    deferred_write_sequence = 0u;
    deferred_read_sequence = 0u;
    deferred_maximum_pending = 0u;
    deferred_queued = 0u;
    deferred_dispatched = 0u;
    deferred_dropped = 0u;
}

static KernelInterruptDispatchResult classify_device_status(
    KernelIrqStatus status);

static bool device_capture(uint8_t source, uint32_t *status, void *context)
{
    (void)context;
    return kernel_platform_device_irq_capture(source, status);
}

static bool device_complete(uint8_t source, const KernelIrqRecord *record,
                            void *context)
{
    (void)context;
    return kernel_platform_device_irq_complete(source, record->status);
}

static bool device_quiesce(uint8_t source, void *context)
{
    (void)context;
    return kernel_platform_device_irq_quiesce(source);
}

static KernelWorkerServiceResult service_device_reset(uint32_t batch_limit,
                                                       void *context)
{
    KernelIrqStatus status;
    uint32_t completed;

    (void)context;
    status = kernel_irq_service_revocations(batch_limit, &completed);
    (void)completed;
    if (status != KERNEL_IRQ_OK && status != KERNEL_IRQ_DEVICE_ERROR)
        return KERNEL_WORKER_SERVICE_FATAL;
    return status == KERNEL_IRQ_DEVICE_ERROR ||
           kernel_irq_revocation_pending() ?
        KERNEL_WORKER_SERVICE_RETRY : KERNEL_WORKER_SERVICE_COMPLETE;
}

static bool write_dispatch_trace(uint8_t source, uint8_t vector,
                                 KernelIrqStatus status,
                                 KernelInterruptDispatchResult result,
                                 uint32_t woken_threads,
                                 uint64_t timestamp,
                                 const KernelIrqTrace *trace)
{
    if (trace != NULL && trace->valid != 0u) {
        /*
         * The staging site declared a level and could not gate on it: the
         * record is built inside the interrupt and written after it. So the
         * drop happens here. It is a comparison rather than a compiled-out
         * branch because the level is only known at run time -- but the
         * expensive half, the ring write, is what it skips.
         */
        if (!KERNEL_TRACE_KEEPS(trace->level))
            return true;
        return kernel_trace_write_at(
            (KernelTraceEvent)trace->event, trace->flags, timestamp,
            trace->argument[0], trace->argument[1], trace->argument[2],
            trace->argument[3]);
    }
    /*
     * One record per interrupt taken, which is the chattiest thing the kernel
     * writes -- a disk doing sequential work produces two of these per sector.
     * A release build compiles it away entirely; nothing above needs to know
     * that an interrupt happened and was handled normally.
     */
    if (!KERNEL_TRACE_KEEPS(KERNEL_TRACE_LEVEL_DEBUG))
        return true;
    return kernel_trace_write_at(
        KERNEL_TRACE_EVENT_IRQ_EXIT,
        (uint16_t)((source & KERNEL_IRQ_TRACE_SOURCE_MASK) |
                   ((uint16_t)result << 8)),
        timestamp, source, vector, (uint32_t)status, woken_threads);
}

static KernelWorkerServiceResult service_deferred_interrupts(
    uint32_t batch_limit, void *context)
{
    uint32_t completed = 0u;

    (void)context;
    while (completed < batch_limit) {
        KernelDeferredInterrupt event;
        const KernelDeferredInterrupt *source_event;
        KernelInterruptDispatchResult result;
        KernelIrqTrace trace;
        KernelIrqStatus status;
        uint32_t read_sequence = deferred_read_sequence;
        uint32_t woken_threads = 0u;
        uint64_t timestamp;

        if (read_sequence == deferred_write_sequence)
            break;
        astra_compiler_barrier();
        source_event = &deferred_interrupts[
            read_sequence & (KERNEL_INTERRUPT_DEFERRED_DEPTH - 1u)];
        event = *source_event;
        timestamp = ((uint64_t)event.timestamp_high << 32) |
                    event.timestamp_low;
        status = kernel_irq_dispatch_claimed_traced(
            event.source, event.vector, timestamp, &woken_threads, &trace);
        result = classify_device_status(status);
        kernel_performance_record_call(
            woken_threads == 0u ? KERNEL_PERFORMANCE_HARD_IRQ :
                                  KERNEL_PERFORMANCE_HARD_IRQ_WAKE,
            event.elapsed_cycles);
        if (!write_dispatch_trace(event.source, event.vector, status, result,
                                  woken_threads, timestamp, &trace))
            return KERNEL_WORKER_SERVICE_FATAL;
        deferred_read_sequence = read_sequence + 1u;
        astra_u32_increment_saturating(&deferred_dispatched);
        ++completed;
        if (result == KERNEL_INTERRUPT_FATAL)
            return KERNEL_WORKER_SERVICE_FATAL;
    }
    if (deferred_read_sequence != deferred_write_sequence &&
        kernel_worker_signal(KERNEL_WORKER_IRQ_DISPATCH) != KERNEL_WORKER_OK)
        return KERNEL_WORKER_SERVICE_FATAL;
    return KERNEL_WORKER_SERVICE_COMPLETE;
}

bool kernel_interrupt_device_binding(uint8_t source,
                                     KernelIrqBinding *binding)
{
    if (binding == NULL ||
        (source != IRQ_SRC_STORAGE && source != IRQ_SRC_INPUT &&
         source != IRQ_SRC_USB && source != IRQ_SRC_VEGA &&
         source != IRQ_SRC_ASTRAEA))
        return false;
    binding->capture = device_capture;
    binding->complete = device_complete;
    binding->quiesce = device_quiesce;
    binding->context = NULL;
    binding->source = source;
    binding->trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding->ipl = 3u;
    binding->vector = KERNEL_IRQ_COMMON_VECTOR;
    return true;
}

bool kernel_interrupt_init(uint32_t cpu_hz)
{
    /* Timer, and the monitor's UART and SPI when the build has a monitor. */
    KernelIrqInternalBinding internal[3];
    KernelIrqControllerOps controller;
    uint32_t count = 0u;

    kernel_platform_interrupt_init(cpu_hz);
    clear_deferred_interrupts();
    controller.configure = kernel_platform_irq_configure;
    controller.mask = kernel_platform_irq_mask;
    controller.enable = kernel_platform_irq_enable;
    controller.acknowledge = kernel_platform_irq_acknowledge;
    controller.context = NULL;
    if (!kernel_irq_pool_init(&controller))
        return false;
    if (kernel_worker_register(KERNEL_WORKER_DEVICE_RESET,
                               service_device_reset, NULL) !=
        KERNEL_WORKER_OK)
        return false;
    if (kernel_worker_register(KERNEL_WORKER_IRQ_DISPATCH,
                               service_deferred_interrupts, NULL) !=
        KERNEL_WORKER_OK)
        return false;

    internal[count].service = kernel_platform_timer_irq_service;
    internal[count].context = NULL;
    internal[count].source = IRQ_SRC_TIMER0;
    internal[count].trigger = KERNEL_IRQ_TRIGGER_EDGE;
    internal[count].ipl = 4u;
    internal[count].vector = KERNEL_IRQ_COMMON_VECTOR;
    ++count;
    /*
     * The monitor is compiled out of a build with no debug surface -- the
     * qualification harness is one -- and then there is nothing behind either
     * of these sources to bind.
     */
    if (kernel_monitor_ready()) {
        if (!kernel_monitor_uart_binding(&internal[count]))
            return false;
        ++count;
        if (kernel_platform_monitor_spi_present()) {
            if (!kernel_monitor_spi_binding(&internal[count]))
                return false;
            ++count;
        }
    }
    for (uint32_t index = 0u; index < count; ++index)
        if (kernel_irq_bind_internal(&internal[index]) != KERNEL_IRQ_OK)
            return false;

    /*
     * Armed as one group so the unwind is one loop: a source that refuses
     * leaves the machine with nothing armed rather than a partial set.
     */
    kernel_platform_timer_arm(kernel_platform_quantum_cycles());
    for (uint32_t index = 0u; index < count; ++index) {
        if (kernel_irq_arm_internal(internal[index].source) == KERNEL_IRQ_OK)
            continue;
        while (index-- > 0u)
            (void)kernel_irq_mask_internal(internal[index].source);
        kernel_platform_timer_disarm();
        return false;
    }
    return kernel_irq_pool_valid();
}

bool kernel_interrupt_schedule_device_reset(void)
{
    return kernel_irq_revocation_pending() &&
           kernel_worker_signal(KERNEL_WORKER_DEVICE_RESET) ==
               KERNEL_WORKER_OK;
}

static __attribute__((noinline))
KernelInterruptDispatchResult dispatch_timer_interrupt(
    uint8_t vector, uint32_t *woken_threads)
{
    KernelPlatformCycleCount timestamp;
    KernelIrqTrace trace;
    KernelIrqStatus status;

    status = kernel_irq_dispatch_claimed_traced(
        IRQ_SRC_TIMER0, vector, 0u, woken_threads, &trace);
    if (status == KERNEL_IRQ_OK)
        return KERNEL_INTERRUPT_TIMER;

    kernel_platform_cpu_cycles(&timestamp);
    (void)write_dispatch_trace(
        IRQ_SRC_TIMER0, vector, status, KERNEL_INTERRUPT_FATAL,
        *woken_threads,
        ((uint64_t)timestamp.high << 32) | timestamp.low, &trace);
    return KERNEL_INTERRUPT_FATAL;
}

static KernelInterruptDispatchResult classify_device_status(
    KernelIrqStatus status)
{
    if (status == KERNEL_IRQ_OK)
        return KERNEL_INTERRUPT_DEVICE;
    if (status == KERNEL_IRQ_UNCLAIMED || status == KERNEL_IRQ_CLOSED ||
        status == KERNEL_IRQ_INVALID_STATE || status == KERNEL_IRQ_OVERFLOW ||
        status == KERNEL_IRQ_STORM || status == KERNEL_IRQ_DEVICE_ERROR)
        return KERNEL_INTERRUPT_QUARANTINED;
    return KERNEL_INTERRUPT_FATAL;
}

static __attribute__((noinline))
KernelInterruptDispatchResult dispatch_device_interrupt(
    uint8_t source, uint8_t vector)
{
    KernelDeferredInterrupt *event;
    KernelPlatformCycleCount timestamp;
    uint32_t pending;
    uint32_t write_sequence;

    kernel_platform_cpu_cycles(&timestamp);
    write_sequence = deferred_write_sequence;
    pending = write_sequence - deferred_read_sequence;
    if (pending >= KERNEL_INTERRUPT_DEFERRED_DEPTH) {
        astra_u32_increment_saturating(&deferred_dropped);
        return KERNEL_INTERRUPT_FATAL;
    }
    event = &deferred_interrupts[
        write_sequence & (KERNEL_INTERRUPT_DEFERRED_DEPTH - 1u)];
    event->timestamp_high = timestamp.high;
    event->timestamp_low = timestamp.low;
    event->source = source;
    event->vector = vector;
    if (kernel_worker_signal(KERNEL_WORKER_IRQ_DISPATCH) != KERNEL_WORKER_OK) {
        astra_u32_increment_saturating(&deferred_dropped);
        return KERNEL_INTERRUPT_FATAL;
    }
    astra_compiler_barrier();
    deferred_write_sequence = write_sequence + 1u;
    astra_u32_increment_saturating(&deferred_queued);
    ++pending;
    if (pending > deferred_maximum_pending)
        deferred_maximum_pending = pending;
    event->elapsed_cycles = kernel_platform_cpu_cycles_low() - timestamp.low;
    astra_compiler_barrier();
    return KERNEL_INTERRUPT_DEVICE;
}

KernelInterruptDispatchResult kernel_interrupt_dispatch(
    uint32_t *woken_threads)
{
    uint8_t source;
    uint8_t vector;

    if (woken_threads == NULL)
        return KERNEL_INTERRUPT_FATAL;
    *woken_threads = 0u;
    if (!kernel_platform_irq_current(&source, &vector))
        return KERNEL_INTERRUPT_NONE;
    if (source == IRQ_SRC_TIMER0)
        return dispatch_timer_interrupt(vector, woken_threads);
    return dispatch_device_interrupt(source, vector);
}

bool kernel_interrupt_stats(KernelInterruptStats *stats)
{
    uint16_t saved_status;

    if (stats == NULL)
        return false;
    saved_status = kernel_interrupt_save_disable();
    stats->pending = deferred_write_sequence - deferred_read_sequence;
    stats->maximum_pending = deferred_maximum_pending;
    stats->queued = deferred_queued;
    stats->dispatched = deferred_dispatched;
    stats->dropped = deferred_dropped;
    kernel_interrupt_restore(saved_status);
    return stats->pending <= KERNEL_INTERRUPT_DEFERRED_DEPTH &&
           stats->dispatched <= stats->queued;
}
