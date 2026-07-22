#ifndef ASTRA_KERNEL_CONTEXT_H
#define ASTRA_KERNEL_CONTEXT_H

#define KERNEL_CONTEXT_D0_OFFSET 0
#define KERNEL_CONTEXT_USP_OFFSET 60
#define KERNEL_CONTEXT_PC_OFFSET 64
#define KERNEL_CONTEXT_SR_OFFSET 68
#define KERNEL_CONTEXT_VECTOR_OFFSET 70
#define KERNEL_CONTEXT_FORMAT_OFFSET 72
#define KERNEL_CONTEXT_VALID_OFFSET 73
#define KERNEL_CONTEXT_SIZE 76
#define KERNEL_CONTEXT_ALIGNMENT 4

#define KERNEL_CONTEXT_REGISTER_COUNT 15u
#define KERNEL_USER_SR_MASK 0x001fu

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum KernelContextStatus {
    KERNEL_CONTEXT_OK = 0,
    KERNEL_CONTEXT_INVALID_ARGUMENT,
    KERNEL_CONTEXT_INVALID_FRAME,
    KERNEL_CONTEXT_NOT_USER
} KernelContextStatus;

typedef struct __attribute__((aligned(KERNEL_CONTEXT_ALIGNMENT)))
    KernelCpuContext {
    uint32_t data[8];
    uint32_t address[7];
    uint32_t usp;
    uint32_t program_counter;
    uint16_t status_register;
    uint16_t vector;
    uint8_t frame_format;
    uint8_t valid;
    uint16_t reserved;
} KernelCpuContext;

_Static_assert(offsetof(KernelCpuContext, data) == KERNEL_CONTEXT_D0_OFFSET,
               "context D0 offset changed");
_Static_assert(offsetof(KernelCpuContext, usp) == KERNEL_CONTEXT_USP_OFFSET,
               "context USP offset changed");
_Static_assert(offsetof(KernelCpuContext, program_counter) ==
                   KERNEL_CONTEXT_PC_OFFSET,
               "context PC offset changed");
_Static_assert(offsetof(KernelCpuContext, status_register) ==
                   KERNEL_CONTEXT_SR_OFFSET,
               "context SR offset changed");
_Static_assert(offsetof(KernelCpuContext, vector) ==
                   KERNEL_CONTEXT_VECTOR_OFFSET,
               "context vector offset changed");
_Static_assert(offsetof(KernelCpuContext, frame_format) ==
                   KERNEL_CONTEXT_FORMAT_OFFSET,
               "context format offset changed");
_Static_assert(offsetof(KernelCpuContext, valid) ==
                   KERNEL_CONTEXT_VALID_OFFSET,
               "context valid offset changed");
_Static_assert(sizeof(KernelCpuContext) == KERNEL_CONTEXT_SIZE,
               "context size changed");
_Static_assert(_Alignof(KernelCpuContext) == KERNEL_CONTEXT_ALIGNMENT,
               "context alignment changed");

void kernel_context_initialize(KernelCpuContext *context,
                               uint32_t program_counter, uint32_t user_stack);
KernelContextStatus kernel_context_capture(KernelCpuContext *context,
                                           const uint32_t *registers,
                                           uint32_t user_stack,
                                           const void *raw_frame);
bool kernel_context_valid(const KernelCpuContext *context);

#endif

#endif
