#ifndef ASTRA_KERNEL_DISPATCH_H
#define ASTRA_KERNEL_DISPATCH_H

#include "context.h"

#include <stdint.h>

KernelCpuContext *kernel_exception_entry_dispatch(const uint32_t *registers,
                                                  const void *raw_frame,
                                                  uint32_t user_stack);
KernelCpuContext *kernel_access_entry_dispatch(const uint32_t *registers,
                                               void *raw_frame,
                                               uint32_t user_stack);
KernelCpuContext *kernel_syscall_entry_dispatch(const uint32_t *registers,
                                                const void *raw_frame,
                                                uint32_t user_stack);
KernelCpuContext *kernel_timer_entry_dispatch(const uint32_t *registers,
                                              const void *raw_frame,
                                              uint32_t user_stack);
void kernel_idle_maintenance(void);

#endif
