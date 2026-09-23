#include "context.h"

#include "bytes.h"
#include "exception.h"

#include <astra/endian.h>

#include <stddef.h>

#define M68K_SR_SUPERVISOR 0x2000u
#define M68K_VECTOR_COUNT  256u

static inline __attribute__((always_inline)) void capture_registers(
    KernelCpuContext *context, const uint32_t *registers)
{
#if defined(__m68k__)
    __asm__ volatile(
        "movem.l (%1),%%d0-%%d7\n\t"
        "movem.l %%d0-%%d7,(%0)\n\t"
        "movem.l 32(%1),%%d0-%%d6\n\t"
        "movem.l %%d0-%%d6,32(%0)"
        :
        : "a"(context), "a"(registers)
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "memory");
#else
    for (uint32_t index = 0u; index < 8u; ++index)
        context->data[index] = registers[index];
    for (uint32_t index = 0u; index < 7u; ++index)
        context->address[index] = registers[index + 8u];
#endif
}

static inline __attribute__((always_inline)) void finish_capture(
    KernelCpuContext *context, const uint32_t *registers,
    uint32_t user_stack, uint32_t program_counter, uint16_t status_register,
    uint16_t vector, uint8_t format)
{
    capture_registers(context, registers);
    context->usp = user_stack;
    context->program_counter = program_counter;
    context->status_register = status_register & KERNEL_USER_SR_MASK;
    context->vector = vector;
    context->frame_format = format;
    context->valid = 1u;
    context->reserved = 0u;
}

void kernel_context_initialize(KernelCpuContext *context,
                               uint32_t program_counter, uint32_t user_stack)
{
    if (context == NULL)
        return;
    kernel_bytes_clear(context, sizeof(*context));
    context->usp = user_stack;
    context->program_counter = program_counter;
    context->status_register = 0u;
    context->valid = 1u;
}

KernelContextStatus kernel_context_capture(KernelCpuContext *context,
                                           const uint32_t *registers,
                                           uint32_t user_stack,
                                           const void *raw_frame)
{
    KernelExceptionBaseFrame frame;

    if (context == NULL || registers == NULL || raw_frame == NULL)
        return KERNEL_CONTEXT_INVALID_ARGUMENT;
    if (kernel_exception_decode_base(raw_frame,
                                     KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                     &frame) != KERNEL_EXCEPTION_OK)
        return KERNEL_CONTEXT_INVALID_FRAME;
    if (frame.from_user == 0u)
        return KERNEL_CONTEXT_NOT_USER;

    finish_capture(context, registers, user_stack, frame.program_counter,
                   frame.status_register, frame.vector_offset >> 2,
                   frame.format);
    return KERNEL_CONTEXT_OK;
}

KernelContextStatus kernel_context_capture_format0(
    KernelCpuContext *context, const uint32_t *registers,
    uint32_t user_stack, const void *raw_frame, uint16_t expected_vector)
{
    const uint8_t *bytes = raw_frame;
    uint16_t format_vector;
    uint16_t status_register;

    if (context == NULL || registers == NULL || bytes == NULL ||
        expected_vector >= M68K_VECTOR_COUNT)
        return KERNEL_CONTEXT_INVALID_ARGUMENT;
    format_vector = astra_load_be16(bytes + 6u);
    if (format_vector != (uint16_t)(expected_vector << 2))
        return KERNEL_CONTEXT_INVALID_FRAME;
    status_register = astra_load_be16(bytes);
    if ((status_register & M68K_SR_SUPERVISOR) != 0u)
        return KERNEL_CONTEXT_NOT_USER;
    finish_capture(context, registers, user_stack,
                   astra_load_be32(bytes + 2u), status_register,
                   expected_vector, 0u);
    return KERNEL_CONTEXT_OK;
}

bool kernel_context_valid(const KernelCpuContext *context)
{
    return context != NULL && context->valid != 0u &&
           context->program_counter != 0u && context->usp != 0u &&
           (context->status_register & ~KERNEL_USER_SR_MASK) == 0u;
}
