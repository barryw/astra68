#ifndef ASTRA_KERNEL_EXCEPTION_H
#define ASTRA_KERNEL_EXCEPTION_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_EXCEPTION_FRAME_MAX_SIZE 92u

typedef enum KernelExceptionStatus {
    KERNEL_EXCEPTION_OK = 0,
    KERNEL_EXCEPTION_INVALID_ARGUMENT,
    KERNEL_EXCEPTION_TRUNCATED,
    KERNEL_EXCEPTION_UNSUPPORTED_FORMAT,
    KERNEL_EXCEPTION_INVALID_VECTOR
} KernelExceptionStatus;

typedef struct KernelExceptionFrame {
    uint32_t program_counter;
    uint32_t instruction_address;
    uint32_t fault_address;
    uint32_t data_output;
    uint32_t stage_b_address;
    uint16_t status_register;
    uint16_t format_vector;
    uint16_t vector_offset;
    uint16_t special_status;
    uint16_t frame_size;
    uint8_t format;
    uint8_t from_user;
    uint8_t access_fault;
    uint8_t reserved;
} KernelExceptionFrame;

KernelExceptionStatus kernel_exception_decode(const void *raw_frame,
                                              uint32_t available,
                                              KernelExceptionFrame *frame);
KernelExceptionStatus kernel_exception_set_program_counter(
    void *raw_frame, uint32_t available, uint32_t program_counter);
bool kernel_exception_format_size(uint8_t format, uint16_t *size);

#endif
