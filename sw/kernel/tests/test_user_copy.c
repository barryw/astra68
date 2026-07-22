#include "exception.h"
#include "user_copy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int low_level_status;
static uint32_t low_level_address;
static uint32_t low_level_size;

int kernel_user_copy_from_asm(void *destination, uint32_t source,
                              uint32_t size)
{
    assert(destination != NULL);
    low_level_address = source;
    low_level_size = size;
    return low_level_status;
}

int kernel_user_copy_to_asm(uint32_t destination, const void *source,
                            uint32_t size)
{
    assert(source != NULL);
    low_level_address = destination;
    low_level_size = size;
    return low_level_status;
}

static void write_be16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1u] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1u] = (uint8_t)(value >> 16);
    bytes[offset + 2u] = (uint8_t)(value >> 8);
    bytes[offset + 3u] = (uint8_t)value;
}

static void make_fault(uint8_t *raw, uint32_t pc, uint32_t address,
                       uint16_t ssw)
{
    memset(raw, 0, KERNEL_EXCEPTION_FRAME_MAX_SIZE);
    write_be16(raw, 0u, 0x2000u);
    write_be32(raw, 2u, pc);
    write_be16(raw, 6u, 0xb008u);
    write_be16(raw, 10u, ssw);
    write_be32(raw, 16u, address);
}

static void test_checked_wrappers(void)
{
    uint8_t buffer[16];

    low_level_status = KERNEL_USER_COPY_OK;
    assert(kernel_copy_from_user(buffer, 0x00010000u, sizeof(buffer)) ==
           KERNEL_USER_COPY_OK);
    assert(low_level_address == 0x00010000u &&
           low_level_size == sizeof(buffer));
    low_level_status = KERNEL_USER_COPY_BAD_ADDRESS;
    assert(kernel_copy_to_user(0x7ffffff0u, buffer, sizeof(buffer)) ==
           KERNEL_USER_COPY_BAD_ADDRESS);
    assert(kernel_copy_from_user(NULL, 0x00010000u, 1u) ==
           KERNEL_USER_COPY_INVALID_ARGUMENT);
    assert(kernel_copy_to_user(0x00010000u, NULL, 1u) ==
           KERNEL_USER_COPY_INVALID_ARGUMENT);
    assert(kernel_copy_from_user(buffer, 0x0000ffffu, 1u) ==
           KERNEL_USER_COPY_BAD_ADDRESS);
    assert(kernel_copy_from_user(buffer, 0x7fffffffu, 2u) ==
           KERNEL_USER_COPY_BAD_ADDRESS);
    assert(kernel_copy_to_user(0x7ffffff0u, buffer, sizeof(buffer) + 1u) ==
           KERNEL_USER_COPY_BAD_ADDRESS);
    assert(kernel_copy_from_user(buffer, 0x00010000u,
                                 KERNEL_USER_COPY_MAX_BYTES + 1u) ==
           KERNEL_USER_COPY_TOO_LARGE);
    assert(kernel_copy_from_user(NULL, 0u, 0u) == KERNEL_USER_COPY_OK);
}

static void test_precise_fault_recovery(void)
{
    uint8_t raw[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    KernelExceptionFrame frame;
    KernelUserCopyFaultScope scope = {
        .start = 0x10001000u,
        .end = 0x10002000u,
        .direction = KERNEL_USER_COPY_FROM_USER,
        .active = 1u
    };
    const KernelUserCopyFaultSite sites[] = {
        {0x02011000u, 0x02011004u, 0x02011080u,
         KERNEL_USER_COPY_FROM_USER},
        {0x02012000u, 0x02012004u, 0x02012080u,
         KERNEL_USER_COPY_TO_USER}
    };

    make_fault(raw, 0x02011000u, 0x10001fffu, 0x0141u);
    assert(kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
    assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_OK);
    assert(frame.program_counter == 0x02011080u);

    make_fault(raw, 0x02011000u, 0x10002000u, 0x0141u);
    assert(!kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
    make_fault(raw, 0x02011000u, 0x10001fffu, 0x0101u);
    assert(!kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
    make_fault(raw, 0x02011010u, 0x10001fffu, 0x0141u);
    assert(!kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
    make_fault(raw, 0x02011000u, 0x10001fffu, 0x0145u);
    assert(!kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));

    scope.direction = KERNEL_USER_COPY_TO_USER;
    make_fault(raw, 0x02012004u, 0x10001000u, 0x0101u);
    assert(kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
    scope.active = 0u;
    assert(!kernel_user_copy_recover_frame(
        raw, sizeof(raw), &scope, sites, 2u));
}

int main(void)
{
    test_checked_wrappers();
    test_precise_fault_recovery();
    puts("KERNEL USER COPY PASS");
    return 0;
}
