#include "exception.h"

#include <astra/endian.h>

#include <stddef.h>

#define M68K_SR_SUPERVISOR 0x2000u

static void clear_frame(KernelExceptionFrame *frame)
{
    frame->program_counter = 0u;
    frame->instruction_address = 0u;
    frame->fault_address = 0u;
    frame->data_output = 0u;
    frame->stage_b_address = 0u;
    frame->status_register = 0u;
    frame->format_vector = 0u;
    frame->vector_offset = 0u;
    frame->special_status = 0u;
    frame->frame_size = 0u;
    frame->format = 0u;
    frame->from_user = 0u;
    frame->access_fault = 0u;
    frame->reserved = 0u;
}

bool kernel_exception_format_size(uint8_t format, uint16_t *size)
{
    if (size == NULL)
        return false;
    switch (format) {
    case 0x0u:
    case 0x1u:
        *size = 8u;
        return true;
    case 0x2u:
        *size = 12u;
        return true;
    case 0x9u:
        *size = 20u;
        return true;
    case 0xau:
        *size = 32u;
        return true;
    case 0xbu:
        *size = 92u;
        return true;
    default:
        *size = 0u;
        return false;
    }
}

KernelExceptionStatus kernel_exception_decode(const void *raw_frame,
                                              uint32_t available,
                                              KernelExceptionFrame *frame)
{
    const uint8_t *bytes = raw_frame;
    uint16_t frame_size;

    if (bytes == NULL || frame == NULL)
        return KERNEL_EXCEPTION_INVALID_ARGUMENT;
    clear_frame(frame);
    if (available < 8u)
        return KERNEL_EXCEPTION_TRUNCATED;

    frame->status_register = astra_load_be16(bytes);
    frame->program_counter = astra_load_be32(bytes + 2u);
    frame->format_vector = astra_load_be16(bytes + 6u);
    frame->format = (uint8_t)(frame->format_vector >> 12);
    frame->vector_offset = frame->format_vector & 0x0fffu;
    frame->from_user =
        (frame->status_register & M68K_SR_SUPERVISOR) == 0u ? 1u : 0u;

    if ((frame->vector_offset & 3u) != 0u)
        return KERNEL_EXCEPTION_INVALID_VECTOR;
    if (!kernel_exception_format_size(frame->format, &frame_size))
        return KERNEL_EXCEPTION_UNSUPPORTED_FORMAT;
    frame->frame_size = frame_size;
    if (available < frame_size)
        return KERNEL_EXCEPTION_TRUNCATED;

    if (frame->format == 0x2u)
        frame->instruction_address = astra_load_be32(bytes + 8u);
    if (frame->format == 0xau || frame->format == 0xbu) {
        frame->access_fault = 1u;
        frame->special_status = astra_load_be16(bytes + 10u);
        frame->fault_address = astra_load_be32(bytes + 16u);
        frame->data_output = astra_load_be32(bytes + 24u);
    }
    if (frame->format == 0xbu)
        frame->stage_b_address = astra_load_be32(bytes + 36u);
    return KERNEL_EXCEPTION_OK;
}

KernelExceptionStatus kernel_exception_set_program_counter(
    void *raw_frame, uint32_t available, uint32_t program_counter)
{
    KernelExceptionFrame frame;
    KernelExceptionStatus status = kernel_exception_decode(
        raw_frame, available, &frame);

    if (status != KERNEL_EXCEPTION_OK)
        return status;
    if (frame.format != 0xau && frame.format != 0xbu)
        return KERNEL_EXCEPTION_UNSUPPORTED_FORMAT;
    astra_store_be32((uint8_t *)raw_frame + 2u, program_counter);
    return KERNEL_EXCEPTION_OK;
}
