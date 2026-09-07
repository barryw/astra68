#include "exception.h"

#include <astra/endian.h>

#include <stddef.h>

#define M68K_SR_SUPERVISOR 0x2000u

#define M68040_SSW_READ       0x0100u
#define M68040_SSW_SIZE_SHIFT 5u
#define M68040_SSW_SIZE_MASK  0x0003u
#define M68040_SSW_TM_MASK    0x0007u
#define M68040_TM_DATA        0x0001u
#define M68040_TM_SUPERVISOR  0x0004u

static void clear_frame(KernelExceptionFrame *frame)
{
    frame->program_counter = 0u;
    frame->instruction_address = 0u;
    frame->fault_address = 0u;
    frame->status_register = 0u;
    frame->format_vector = 0u;
    frame->vector_offset = 0u;
    frame->special_status = 0u;
    frame->frame_size = 0u;
    frame->format = 0u;
    frame->from_user = 0u;
    frame->access_fault = 0u;
    frame->access_write = 0u;
    frame->access_data = 0u;
    frame->access_supervisor = 0u;
    frame->access_size = 0u;
    frame->access_transfer_mode = 0u;
}

static uint8_t m68040_access_size(uint16_t ssw)
{
    static const uint8_t normalized[] = {2u, 0u, 1u, 3u};

    return normalized[(ssw >> M68040_SSW_SIZE_SHIFT) &
                      M68040_SSW_SIZE_MASK];
}

static void decode_access(KernelExceptionFrame *frame)
{
    uint16_t ssw = frame->special_status;
    uint8_t transfer_mode = (uint8_t)(ssw & M68040_SSW_TM_MASK);

    frame->access_write = (ssw & M68040_SSW_READ) == 0u;
    frame->access_data = (transfer_mode & 0x3u) == M68040_TM_DATA;
    frame->access_supervisor =
        (transfer_mode & M68040_TM_SUPERVISOR) != 0u;
    frame->access_size = m68040_access_size(ssw);
    frame->access_transfer_mode = transfer_mode;
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
    case 0x3u:
        *size = 12u;
        return true;
    case 0x7u:
        *size = 60u;
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
    if (frame->format == 0x7u) {
        frame->access_fault = 1u;
        frame->special_status = astra_load_be16(bytes + 12u);
        frame->fault_address = astra_load_be32(bytes + 20u);
        decode_access(frame);
    }
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
    if (frame.access_fault == 0u)
        return KERNEL_EXCEPTION_UNSUPPORTED_FORMAT;
    astra_store_be32((uint8_t *)raw_frame + 2u, program_counter);
    return KERNEL_EXCEPTION_OK;
}
