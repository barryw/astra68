#include "context.h"

#include "bytes.h"
#include "exception.h"

#include <stddef.h>

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
    KernelExceptionFrame frame;

    if (context == NULL || registers == NULL || raw_frame == NULL)
        return KERNEL_CONTEXT_INVALID_ARGUMENT;
    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK)
        return KERNEL_CONTEXT_INVALID_FRAME;
    if (frame.from_user == 0u)
        return KERNEL_CONTEXT_NOT_USER;

    for (uint32_t index = 0u; index < 8u; ++index)
        context->data[index] = registers[index];
    for (uint32_t index = 0u; index < 7u; ++index)
        context->address[index] = registers[index + 8u];
    context->usp = user_stack;
    context->program_counter = frame.program_counter;
    context->status_register = frame.status_register & KERNEL_USER_SR_MASK;
    context->vector = frame.vector_offset >> 2;
    context->frame_format = frame.format;
    context->valid = 1u;
    context->reserved = 0u;
    return KERNEL_CONTEXT_OK;
}

bool kernel_context_valid(const KernelCpuContext *context)
{
    return context != NULL && context->valid != 0u &&
           context->program_counter != 0u && context->usp != 0u &&
           (context->status_register & ~KERNEL_USER_SR_MASK) == 0u;
}
