#include "dispatch.h"

#include "exception.h"
#include "panic.h"
#include "platform.h"
#include "process.h"
#include "user_copy.h"
#include "worker.h"
#if ASTRA_KERNEL_SCHED_TRACE
#include "vesta.h"
#endif

#include <astra/syscall.h>

#include <stddef.h>

static uint32_t user_fault_irqoff_max_cycles;
static uint32_t last_supervisor_irq_pc;
static uint16_t last_supervisor_irq_sr;

#if ASTRA_KERNEL_SCHED_TRACE
static void scheduler_trace(uint32_t value)
{
    VESTA->SCRATCH = value;
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

static KernelDispatchTarget dispatch_user_fault(const uint32_t *registers,
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
    schedule_process_worker();
    scheduler_trace(0x4b464f55u); /* KFOU */
    record_user_fault_irqoff(started);
    return KERNEL_DISPATCH_WORKER;
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

    if (kernel_user_copy_handle_fault(raw_frame))
        return KERNEL_DISPATCH_RESUME;
    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK ||
        frame.from_user == 0u)
        kernel_exception_panic(raw_frame);
    return dispatch_user_fault(registers, raw_frame, user_stack);
}

KernelDispatchTarget kernel_syscall_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;
    bool process_exited;

    if (!kernel_process_active())
        kernel_exception_panic(raw_frame);
    scheduler_trace(0x4b53494eu); /* KSIN */
    process_exited = registers != NULL &&
                     registers[0] == ASTRA_SYSCALL_EXIT;
    status = kernel_process_on_syscall(registers, user_stack, raw_frame,
                                       &next);
    if (!((status == KERNEL_PROCESS_OK && next != NULL) ||
          (status == KERNEL_PROCESS_NO_RUNNABLE && next == NULL &&
           !kernel_process_active())))
        kernel_panic("syscall left no runnable process");
    if (process_exited) {
        schedule_process_worker();
        return KERNEL_DISPATCH_WORKER;
    }
    if (kernel_worker_try_select())
        return KERNEL_DISPATCH_WORKER;
    scheduler_trace(0x4b534f55u); /* KSOU */
    return kernel_dispatch_user_target(next);
}

KernelDispatchTarget kernel_timer_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack)
{
    KernelExceptionFrame frame;
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;

    if (!kernel_interrupt_dispatch())
        return KERNEL_DISPATCH_RESUME;
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
