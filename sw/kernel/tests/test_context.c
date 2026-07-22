#include "context.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void put_be16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1u] = (uint8_t)value;
}

static void put_be32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1u] = (uint8_t)(value >> 16);
    bytes[offset + 2u] = (uint8_t)(value >> 8);
    bytes[offset + 3u] = (uint8_t)value;
}

static void make_frame(uint8_t *frame, uint16_t status, uint32_t pc,
                       uint8_t format, uint16_t vector)
{
    for (uint32_t index = 0u; index < 92u; ++index)
        frame[index] = 0u;
    put_be16(frame, 0u, status);
    put_be32(frame, 2u, pc);
    put_be16(frame, 6u,
             (uint16_t)((uint16_t)format << 12 | (vector << 2)));
}

static void test_initialize(void)
{
    KernelCpuContext context;

    kernel_context_initialize(&context, 0x00100000u, 0x70001000u);
    assert(kernel_context_valid(&context));
    assert(context.program_counter == 0x00100000u);
    assert(context.usp == 0x70001000u);
    assert(context.status_register == 0u);
    for (uint32_t index = 0u; index < 8u; ++index)
        assert(context.data[index] == 0u);
    for (uint32_t index = 0u; index < 7u; ++index)
        assert(context.address[index] == 0u);
}

static void test_capture_and_sanitize(void)
{
    KernelCpuContext context;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[92];

    for (uint32_t index = 0u; index < KERNEL_CONTEXT_REGISTER_COUNT; ++index)
        registers[index] = 0x11110000u + index;
    make_frame(frame, 0xc71fu, 0x00102340u, 0u, 80u);
    assert(kernel_context_capture(&context, registers, 0x70000ff0u,
                                  frame) == KERNEL_CONTEXT_OK);
    assert(kernel_context_valid(&context));
    assert(context.data[0] == registers[0]);
    assert(context.data[7] == registers[7]);
    assert(context.address[0] == registers[8]);
    assert(context.address[6] == registers[14]);
    assert(context.usp == 0x70000ff0u);
    assert(context.program_counter == 0x00102340u);
    assert(context.status_register == 0x001fu);
    assert(context.vector == 80u);
    assert(context.frame_format == 0u);
}

static void test_rejections(void)
{
    KernelCpuContext context;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[92];

    make_frame(frame, 0x2000u, 0x00100000u, 0u, 47u);
    assert(kernel_context_capture(&context, registers, 0x70001000u,
                                  frame) == KERNEL_CONTEXT_NOT_USER);
    make_frame(frame, 0u, 0x00100000u, 3u, 47u);
    assert(kernel_context_capture(&context, registers, 0x70001000u,
                                  frame) == KERNEL_CONTEXT_INVALID_FRAME);
    assert(kernel_context_capture(NULL, registers, 0x70001000u,
                                  frame) == KERNEL_CONTEXT_INVALID_ARGUMENT);
    kernel_context_initialize(&context, 0u, 0x70001000u);
    assert(!kernel_context_valid(&context));
    context.program_counter = 0x00100000u;
    context.status_register = 0x2000u;
    assert(!kernel_context_valid(&context));
}

int main(void)
{
    test_initialize();
    test_capture_and_sanitize();
    test_rejections();
    puts("context tests passed");
    return 0;
}
