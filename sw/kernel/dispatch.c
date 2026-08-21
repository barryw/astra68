#include "dispatch.h"

#include "exception.h"
#include "fault.h"
#include "interrupt.h"
#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "trace.h"
#include "user_copy.h"
#include "worker.h"

#include <astra/syscall.h>
#include <vesta.h>

#include <stddef.h>

static uint32_t user_fault_irqoff_max_cycles;
static uint32_t last_supervisor_irq_pc;
static uint16_t last_supervisor_irq_sr;
#if ASTRA_KERNEL_SCHED_TRACE
#define KERNEL_DISPATCH_WAIT_SET_TRACE_MAX 8u
#define KERNEL_DISPATCH_THREAD_CREATE_TRACE_MAX 8u
static uint32_t syscall_max_number;
static uint32_t syscall_max_argument;
static uint32_t syscall_max_body_cycles;
static uint32_t wait_set_trace_cycles[KERNEL_DISPATCH_WAIT_SET_TRACE_MAX];
static uint32_t wait_set_trace_count;
static uint32_t thread_create_trace_cycles[
    KERNEL_DISPATCH_THREAD_CREATE_TRACE_MAX];
static uint32_t thread_create_trace_ticks[
    KERNEL_DISPATCH_THREAD_CREATE_TRACE_MAX];
static uint32_t thread_create_trace_count;
#endif

#if ASTRA_KERNEL_SCHED_TRACE
static void scheduler_trace(uint32_t value)
{
    kernel_platform_debug_marker(value);
}
#else
static void scheduler_trace(uint32_t value)
{
    (void)value;
}
#endif

static void schedule_process_worker(void)
{
    if (!kernel_process_maintenance_pending())
        kernel_panic("process worker missing teardown");
    if (kernel_worker_schedule(KERNEL_WORKER_PROCESS_REAP) !=
        KERNEL_WORKER_OK)
        kernel_panic("process worker scheduling failed");
}

uint32_t kernel_dispatch_user_fault_irqoff_max_cycles(void)
{
    return user_fault_irqoff_max_cycles;
}

uint32_t kernel_dispatch_syscall_max_number(void)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return syscall_max_number;
#else
    return UINT32_MAX;
#endif
}

uint32_t kernel_dispatch_syscall_max_argument(void)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return syscall_max_argument;
#else
    return 0u;
#endif
}

uint32_t kernel_dispatch_syscall_max_body_cycles(void)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return syscall_max_body_cycles;
#else
    return 0u;
#endif
}

uint32_t kernel_dispatch_wait_set_trace_count(void)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return wait_set_trace_count;
#else
    return 0u;
#endif
}

uint32_t kernel_dispatch_wait_set_trace_cycles(uint32_t index)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return index < wait_set_trace_count ? wait_set_trace_cycles[index] : 0u;
#else
    (void)index;
    return 0u;
#endif
}

uint32_t kernel_dispatch_thread_create_trace_count(void)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return thread_create_trace_count;
#else
    return 0u;
#endif
}

uint32_t kernel_dispatch_thread_create_trace_cycles(uint32_t index)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return index < thread_create_trace_count ?
        thread_create_trace_cycles[index] : 0u;
#else
    (void)index;
    return 0u;
#endif
}

uint32_t kernel_dispatch_thread_create_trace_ticks(uint32_t index)
{
#if ASTRA_KERNEL_SCHED_TRACE
    return index < thread_create_trace_count ?
        thread_create_trace_ticks[index] : 0u;
#else
    (void)index;
    return 0u;
#endif
}

uint32_t kernel_dispatch_last_supervisor_irq_pc(void)
{
    return last_supervisor_irq_pc;
}

uint16_t kernel_dispatch_last_supervisor_irq_sr(void)
{
    return last_supervisor_irq_sr;
}

static void record_user_fault_irqoff(uint32_t started)
{
    uint32_t elapsed = kernel_platform_cpu_cycles_low() - started;

    if (elapsed > user_fault_irqoff_max_cycles)
        user_fault_irqoff_max_cycles = elapsed;
}

static bool fault_is_pmmu(const KernelFaultReport *fault)
{
    return fault->kind == KERNEL_FAULT_PMMU_TRANSLATION ||
           fault->kind == KERNEL_FAULT_PMMU_PROTECTION;
}

static bool fault_is_physical(const KernelFaultReport *fault)
{
    return fault->kind == KERNEL_FAULT_PHYSICAL_UNMAPPED ||
           fault->kind == KERNEL_FAULT_PHYSICAL_TIMEOUT ||
           fault->kind == KERNEL_FAULT_PHYSICAL_DEVICE ||
           fault->kind == KERNEL_FAULT_PHYSICAL_EXTERNAL;
}

static bool fault_is_fatal_memory_fabric(const KernelFaultReport *fault)
{
    return fault_is_physical(fault) &&
           fault->bus_record_matched != 0u &&
           fault->bus_target == BUS_FAULT_TARGET_SDRAM;
}

static uint16_t fault_trace_flags(const KernelFaultReport *fault,
                                  bool stale)
{
    uint16_t flags = (uint16_t)(fault->kind &
                                KERNEL_FAULT_TRACE_KIND_MASK);

    flags |= (uint16_t)((fault->mapping <<
                         KERNEL_FAULT_TRACE_MAPPING_SHIFT) &
                        KERNEL_FAULT_TRACE_MAPPING_MASK);
    if (fault->write != 0u)
        flags |= KERNEL_FAULT_TRACE_WRITE;
    if (fault->bus_record_matched != 0u)
        flags |= KERNEL_FAULT_TRACE_RECORD_MATCHED;
    if (stale)
        flags |= KERNEL_FAULT_TRACE_RECORD_STALE;
    if (fault->bus_lost != 0u)
        flags |= KERNEL_FAULT_TRACE_RECORD_LOST;
    return flags;
}

static void trace_access_fault(const KernelExceptionFrame *frame,
                               const KernelFaultReport *fault)
{
    uint64_t bus_timestamp = ((uint64_t)fault->bus_cycles_high << 32) |
                             fault->bus_cycles_low;

    if (fault->bus_record_present != 0u &&
        fault->bus_record_matched == 0u) {
        (void)kernel_trace_write_at(
            KERNEL_TRACE_EVENT_PHYSICAL_FAULT,
            fault_trace_flags(fault, true), bus_timestamp,
            fault->logical_address, fault->bus_address,
            fault->bus_status, fault->bus_target);
    }
    if (fault_is_physical(fault)) {
        (void)kernel_trace_write_at(
            KERNEL_TRACE_EVENT_PHYSICAL_FAULT,
            fault_trace_flags(fault, false), bus_timestamp,
            fault->logical_address, fault->bus_address,
            fault->bus_status, fault->bus_target);
    } else {
        KERNEL_TRACE(
            KERNEL_TRACE_LEVEL_ERROR, KERNEL_TRACE_EVENT_PMMU_FAULT,
            fault_trace_flags(fault, false), fault->logical_address,
            fault->expected_physical, frame->special_status,
            frame->program_counter);
    }
}

static __attribute__((noinline))
KernelDispatchTarget dispatch_user_fault_fast(const uint32_t *registers,
                                              const void *raw_frame,
                                              uint32_t user_stack)
{
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;
    uint32_t started = kernel_platform_cpu_cycles_low();

    if (!kernel_process_active())
        kernel_exception_panic(raw_frame);
    scheduler_trace(0x4b46494eu); /* KFIN */
    status = kernel_process_on_fault(registers, user_stack, raw_frame, &next);
    if (!((status == KERNEL_PROCESS_OK && next != NULL) ||
          (status == KERNEL_PROCESS_NO_RUNNABLE && next == NULL &&
           !kernel_process_active())))
        kernel_panic("user-fault process teardown failed");
    scheduler_trace(0x4b464f55u); /* KFOU */
    record_user_fault_irqoff(started);
    /*
     * Not every user fault is a death now. One that grew a stack leaves the
     * thread runnable and nothing to tear down, so the worker is scheduled on
     * the same condition the syscall path uses rather than unconditionally --
     * a worker with no teardown waiting for it panics on arrival.
     */
    if (kernel_process_maintenance_pending()) {
        schedule_process_worker();
        return KERNEL_DISPATCH_WORKER;
    }
    if (next == NULL)
        kernel_panic("user fault left no context to resume");
    return kernel_dispatch_user_target(next);
}

static __attribute__((noinline))
KernelDispatchTarget dispatch_user_fault_profiled(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelPerformanceToken performance;
    KernelDispatchTarget target;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_USER_FAULT);
    target = dispatch_user_fault_fast(registers, raw_frame, user_stack);
    kernel_performance_end(performance);
    return target;
}

static KernelDispatchTarget dispatch_user_fault(const uint32_t *registers,
                                                const void *raw_frame,
                                                uint32_t user_stack)
{
    if (kernel_performance_sampling_enabled == 0u)
        return dispatch_user_fault_fast(registers, raw_frame, user_stack);
    return dispatch_user_fault_profiled(registers, raw_frame, user_stack);
}

KernelDispatchTarget kernel_exception_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelExceptionFrame frame;

    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK ||
        frame.from_user == 0u)
        kernel_exception_panic(raw_frame);
    return dispatch_user_fault(registers, raw_frame, user_stack);
}

KernelDispatchTarget kernel_access_entry_dispatch(const uint32_t *registers,
                                                  void *raw_frame,
                                                  uint32_t user_stack)
{
    KernelExceptionFrame frame;
    KernelFaultReport fault;
    bool copy_recovered;

    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK)
        kernel_exception_panic(raw_frame);
    copy_recovered = kernel_user_copy_handle_fault(raw_frame);
    if (!kernel_fault_capture(&frame, &fault))
        kernel_exception_panic(raw_frame);

    trace_access_fault(&frame, &fault);
    if (fault_is_fatal_memory_fabric(&fault))
        kernel_exception_panic_classified(raw_frame, &fault);
    if (fault_is_pmmu(&fault) && fault.bus_record_present != 0u &&
        fault.bus_record_matched == 0u)
        kernel_platform_bus_fault_acknowledge();
    if (copy_recovered) {
        if (fault_is_physical(&fault))
            kernel_platform_bus_fault_acknowledge();
        else if (!fault_is_pmmu(&fault))
            kernel_exception_panic_classified(raw_frame, &fault);
        return KERNEL_DISPATCH_RESUME;
    }
    if (fault_is_pmmu(&fault)) {
        if (frame.from_user == 0u)
            kernel_exception_panic_classified(raw_frame, &fault);
        return dispatch_user_fault(registers, raw_frame, user_stack);
    }
    if (!fault_is_physical(&fault) || frame.from_user == 0u)
        kernel_exception_panic_classified(raw_frame, &fault);

    kernel_platform_bus_fault_acknowledge();
    return dispatch_user_fault(registers, raw_frame, user_stack);
}

static __attribute__((noinline))
KernelDispatchTarget syscall_entry_dispatch_fast(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;

    if (!kernel_process_active())
        kernel_exception_panic(raw_frame);
    scheduler_trace(0x4b53494eu); /* KSIN */
    if (registers != NULL)
        scheduler_trace(registers[0]);
    status = kernel_process_on_syscall(registers, user_stack, raw_frame,
                                       &next);
    if (!((status == KERNEL_PROCESS_OK && next != NULL) ||
          (status == KERNEL_PROCESS_NO_RUNNABLE && next == NULL &&
           !kernel_process_active())))
        kernel_panic("syscall left no runnable process");
    if (kernel_process_maintenance_pending()) {
        schedule_process_worker();
        return KERNEL_DISPATCH_WORKER;
    }
    if (kernel_worker_try_select())
        return KERNEL_DISPATCH_WORKER;
    if (next == NULL) {
        if (kernel_worker_select_idle())
            return KERNEL_DISPATCH_WORKER;
        kernel_panic("blocked syscall could not select idle worker");
    }
    scheduler_trace(0x4b534f55u); /* KSOU */
    return kernel_dispatch_user_target(next);
}

static __attribute__((noinline))
KernelDispatchTarget syscall_entry_dispatch_profiled(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelPerformanceToken performance;
    KernelPerformanceMetric metric = KERNEL_PERFORMANCE_SYSCALL_DISPATCH;
    KernelDispatchTarget target;
#if ASTRA_KERNEL_SCHED_TRACE
    uint32_t started;
    uint32_t started_ticks = 0u;
    uint32_t elapsed;
#endif

    if (registers != NULL) {
        switch (registers[0]) {
        case ASTRA_SYSCALL_THREAD_CREATE:
            metric = KERNEL_PERFORMANCE_THREAD_CREATE;
            break;
        case ASTRA_SYSCALL_THREAD_EXIT:
            metric = KERNEL_PERFORMANCE_THREAD_EXIT;
            break;
        case ASTRA_SYSCALL_AREA_CREATE:
            metric = KERNEL_PERFORMANCE_AREA_CREATE;
            break;
        case ASTRA_SYSCALL_AREA_MAP:
            metric = KERNEL_PERFORMANCE_AREA_MAP;
            break;
        case ASTRA_SYSCALL_AREA_UNMAP:
            metric = KERNEL_PERFORMANCE_AREA_UNMAP;
            break;
        case ASTRA_SYSCALL_RING_NOTIFY:
            metric = KERNEL_PERFORMANCE_RING_NOTIFY;
            break;
        case ASTRA_SYSCALL_LIBRARY_ATTACH:
            metric = KERNEL_PERFORMANCE_LIBRARY_ATTACH;
            break;
        default:
            break;
        }
    }
    performance = kernel_performance_begin_sampled(metric);
#if ASTRA_KERNEL_SCHED_TRACE
    started = kernel_platform_cpu_cycles_low();
    if (metric == KERNEL_PERFORMANCE_THREAD_CREATE)
        started_ticks = kernel_platform_ticks();
#endif
    target = syscall_entry_dispatch_fast(registers, raw_frame, user_stack);
#if ASTRA_KERNEL_SCHED_TRACE
    elapsed = kernel_platform_cpu_cycles_low() - started;
    if (registers != NULL &&
        registers[0] == ASTRA_SYSCALL_WAIT_MULTIPLE &&
        wait_set_trace_count < KERNEL_DISPATCH_WAIT_SET_TRACE_MAX)
        wait_set_trace_cycles[wait_set_trace_count++] = elapsed;
    if (metric == KERNEL_PERFORMANCE_THREAD_CREATE &&
        thread_create_trace_count <
            KERNEL_DISPATCH_THREAD_CREATE_TRACE_MAX) {
        thread_create_trace_cycles[thread_create_trace_count] = elapsed;
        thread_create_trace_ticks[thread_create_trace_count] =
            kernel_platform_ticks() - started_ticks;
        ++thread_create_trace_count;
    }
    if (metric == KERNEL_PERFORMANCE_SYSCALL_DISPATCH &&
        elapsed > syscall_max_body_cycles) {
        syscall_max_body_cycles = elapsed;
        syscall_max_number = registers != NULL ? registers[0] : UINT32_MAX;
        syscall_max_argument = registers != NULL ? registers[1] : 0u;
    }
#endif
    kernel_performance_end(performance);
    return target;
}

KernelDispatchTarget kernel_syscall_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    if (kernel_performance_sampling_enabled == 0u)
        return syscall_entry_dispatch_fast(registers, raw_frame, user_stack);
    return syscall_entry_dispatch_profiled(registers, raw_frame, user_stack);
}

static __attribute__((noinline))
KernelDispatchTarget interrupt_entry_dispatch_fast(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelExceptionFrame frame;
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;
    KernelInterruptDispatchResult interrupt;
    uint32_t woken_threads;

    interrupt = kernel_interrupt_dispatch(&woken_threads);
    if (interrupt == KERNEL_INTERRUPT_FATAL)
        kernel_panic("interrupt dispatch failed");
    if (interrupt != KERNEL_INTERRUPT_TIMER) {
        if (woken_threads == 0u && !kernel_worker_work_pending())
            return KERNEL_DISPATCH_RESUME;
        if (kernel_exception_decode(raw_frame,
                                    KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                    &frame) != KERNEL_EXCEPTION_OK)
            kernel_exception_panic(raw_frame);
        if (frame.from_user == 0u || !kernel_process_active())
            return KERNEL_DISPATCH_RESUME;
        status = kernel_process_on_interrupt_wakeup(
            registers, user_stack, raw_frame, &next);
        if (status != KERNEL_PROCESS_OK || next == NULL)
            kernel_panic("device interrupt scheduling failed");
        if (kernel_worker_try_select())
            return KERNEL_DISPATCH_WORKER;
        return kernel_dispatch_user_target(next);
    }
    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK)
        kernel_exception_panic(raw_frame);
    if (kernel_worker_on_timer() != KERNEL_WORKER_OK)
        kernel_panic("deferred worker timer failed");

    /*
     * An IRQ can be accepted after an exception vectors but before its entry
     * stub masks interrupts.  Preserve that supervisor frame and let RTE
     * resume the interrupted handler; only user frames are schedulable.
     */
    if (frame.from_user == 0u) {
        last_supervisor_irq_pc = frame.program_counter;
        last_supervisor_irq_sr = frame.status_register;
        if (kernel_process_on_supervisor_timer() != KERNEL_PROCESS_OK)
            kernel_panic("supervisor timer scheduling failed");
        return KERNEL_DISPATCH_RESUME;
    }
    if (!kernel_process_active())
        return KERNEL_DISPATCH_RESUME;
    scheduler_trace(0x4b54494eu); /* KTIN */
    status = kernel_process_on_timer(registers, user_stack, raw_frame, &next);
    if (status != KERNEL_PROCESS_OK || next == NULL)
        kernel_panic("timer scheduler dispatch failed");
    if (kernel_worker_try_select())
        return KERNEL_DISPATCH_WORKER;
    scheduler_trace(0x4b544f55u); /* KTOU */
    return kernel_dispatch_user_target(next);
}

static __attribute__((noinline))
KernelDispatchTarget interrupt_entry_dispatch_profiled(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelPerformanceToken performance;
    KernelDispatchTarget target;

    performance = kernel_performance_begin_sampled(
        KERNEL_PERFORMANCE_TIMER_DISPATCH);
    target = interrupt_entry_dispatch_fast(registers, raw_frame, user_stack);
    kernel_performance_end(performance);
    return target;
}

KernelDispatchTarget kernel_interrupt_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelPerformanceInterruptToken interrupt;
    KernelDispatchTarget target;

    if (kernel_performance_sampling_enabled == 0u)
        return interrupt_entry_dispatch_fast(registers, raw_frame,
                                             user_stack);
    interrupt = kernel_performance_interrupt_enter();
    target = interrupt_entry_dispatch_profiled(registers, raw_frame,
                                                user_stack);
    kernel_performance_interrupt_leave(interrupt);
    return target;
}
