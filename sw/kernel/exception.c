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

static __attribute__((always_inline)) inline uint16_t
exception_format_size(uint8_t format)
{
    switch (format) {
    case 0x0u:
    case 0x1u:
        return 8u;
    case 0x2u:
    case 0x3u:
        return 12u;
    case 0x7u:
        return 60u;
    default:
        return 0u;
    }
}

bool kernel_exception_format_size(uint8_t format, uint16_t *size)
{
    if (size == NULL)
        return false;
    *size = exception_format_size(format);
    return *size != 0u;
}

KernelExceptionStatus kernel_exception_decode(const void *raw_frame,
                                              uint32_t available,
                                              KernelExceptionFrame *frame)
{
    const uint8_t *bytes = raw_frame;
    KernelExceptionBaseFrame base;
    KernelExceptionStatus status;

    if (bytes == NULL || frame == NULL)
        return KERNEL_EXCEPTION_INVALID_ARGUMENT;
    clear_frame(frame);
    status = kernel_exception_decode_base(raw_frame, available, &base);
    if (status != KERNEL_EXCEPTION_OK)
        return status;

    frame->program_counter = base.program_counter;
    frame->status_register = base.status_register;
    frame->format_vector = base.format_vector;
    frame->vector_offset = base.vector_offset;
    frame->frame_size = base.frame_size;
    frame->format = base.format;
    frame->from_user = base.from_user;

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

KernelExceptionStatus kernel_exception_decode_base(
    const void *raw_frame, uint32_t available, KernelExceptionBaseFrame *frame)
{
    const uint8_t *bytes = raw_frame;
    uint16_t format_vector;
    uint16_t frame_size;
    uint8_t format;

    if (bytes == NULL || frame == NULL)
        return KERNEL_EXCEPTION_INVALID_ARGUMENT;
    if (available < 8u)
        return KERNEL_EXCEPTION_TRUNCATED;

    format_vector = astra_load_be16(bytes + 6u);
    format = (uint8_t)(format_vector >> 12);
    if ((format_vector & 3u) != 0u)
        return KERNEL_EXCEPTION_INVALID_VECTOR;
    frame_size = exception_format_size(format);
    if (frame_size == 0u)
        return KERNEL_EXCEPTION_UNSUPPORTED_FORMAT;
    if (available < frame_size)
        return KERNEL_EXCEPTION_TRUNCATED;

    frame->program_counter = astra_load_be32(bytes + 2u);
    frame->status_register = astra_load_be16(bytes);
    frame->format_vector = format_vector;
    frame->vector_offset = format_vector & 0x0fffu;
    frame->frame_size = frame_size;
    frame->format = format;
    frame->from_user =
        (frame->status_register & M68K_SR_SUPERVISOR) == 0u ? 1u : 0u;
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
