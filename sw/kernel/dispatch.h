#ifndef ASTRA_KERNEL_DISPATCH_H
#define ASTRA_KERNEL_DISPATCH_H

#define KERNEL_DISPATCH_RESUME 0
#define KERNEL_DISPATCH_WORKER 1

#ifndef __ASSEMBLER__

#include "context.h"

#include <stdint.h>

typedef uintptr_t KernelDispatchTarget;

static inline KernelDispatchTarget
kernel_dispatch_user_target(KernelCpuContext *context)
{
    return (KernelDispatchTarget)(uintptr_t)context;
}

KernelDispatchTarget kernel_exception_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack);
KernelDispatchTarget kernel_access_entry_dispatch(
    const uint32_t *registers, void *raw_frame, uint32_t user_stack);
KernelDispatchTarget kernel_syscall_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack);
KernelDispatchTarget kernel_interrupt_entry_dispatch(
    const uint32_t *registers, const void *raw_frame, uint32_t user_stack);
uint32_t kernel_dispatch_user_fault_irqoff_max_cycles(void);
uint32_t kernel_dispatch_syscall_max_number(void);
uint32_t kernel_dispatch_syscall_max_argument(void);
uint32_t kernel_dispatch_syscall_max_body_cycles(void);
uint32_t kernel_dispatch_wait_set_trace_count(void);
uint32_t kernel_dispatch_wait_set_trace_cycles(uint32_t index);
uint32_t kernel_dispatch_thread_create_trace_count(void);
uint32_t kernel_dispatch_thread_create_trace_cycles(uint32_t index);
uint32_t kernel_dispatch_thread_create_trace_ticks(uint32_t index);
uint32_t kernel_dispatch_last_supervisor_irq_pc(void);
uint16_t kernel_dispatch_last_supervisor_irq_sr(void);

#endif

#endif
