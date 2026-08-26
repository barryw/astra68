#include "exception.h"

#include <astra/endian.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t expected_size(uint8_t format)
{
    switch (format) {
    case 0x0u:
    case 0x1u:
        return 8u;
    case 0x2u:
        return 12u;
    case 0x9u:
        return 20u;
    case 0xau:
        return 32u;
    default:
        return 92u;
    }
}

static void test_all_motorola_formats(void)
{
    static const uint8_t formats[] = {0x0u, 0x1u, 0x2u, 0x9u, 0xau, 0xbu};

    for (uint32_t index = 0u; index < sizeof(formats); ++index) {
        uint8_t raw[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
        KernelExceptionFrame frame;
        uint16_t size = expected_size(formats[index]);

        memset(raw, 0xa5, sizeof(raw));
        astra_store_be16(raw, index == 0u ? 0x2000u : 0x001fu);
        astra_store_be32(raw + 2u, 0x12345678u + index);
        astra_store_be16(raw + 6u,
                         (uint16_t)((uint16_t)formats[index] << 12) |
                         0x0080u);
        if (formats[index] == 0x2u)
            astra_store_be32(raw + 8u, 0x87654321u);
        if (formats[index] == 0xau || formats[index] == 0xbu) {
            astra_store_be16(raw + 10u, 0x0151u);
            astra_store_be32(raw + 16u, 0xdffffff8u);
            astra_store_be32(raw + 24u, 0xc0dec0deu);
        }
        if (formats[index] == 0xbu)
            astra_store_be32(raw + 36u, 0x000001a6u);

        assert(kernel_exception_decode(raw, size, &frame) ==
               KERNEL_EXCEPTION_OK);
        assert(frame.format == formats[index]);
        assert(frame.frame_size == size);
        assert(frame.vector_offset == 0x0080u);
        assert(frame.program_counter == 0x12345678u + index);
        assert(frame.from_user == (index == 0u ? 0u : 1u));
        assert(frame.access_fault ==
               (formats[index] == 0xau || formats[index] == 0xbu));
        if (formats[index] == 0x2u)
            assert(frame.instruction_address == 0x87654321u);
        if (frame.access_fault != 0u) {
            assert(frame.special_status == 0x0151u);
            assert(frame.fault_address == 0xdffffff8u);
            assert(frame.data_output == 0xc0dec0deu);
        }
        if (formats[index] == 0xbu)
            assert(frame.stage_b_address == 0x000001a6u);

        for (uint32_t available = 0u; available < size; ++available) {
            assert(kernel_exception_decode(raw, available, &frame) ==
                   KERNEL_EXCEPTION_TRUNCATED);
        }
    }
}

static void test_rejects_malformed_frames(void)
{
    uint8_t raw[KERNEL_EXCEPTION_FRAME_MAX_SIZE] = {0};
    KernelExceptionFrame frame;
    uint16_t size = 123u;

    assert(kernel_exception_decode(NULL, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_INVALID_ARGUMENT);
    assert(kernel_exception_decode(raw, sizeof(raw), NULL) ==
           KERNEL_EXCEPTION_INVALID_ARGUMENT);
    assert(!kernel_exception_format_size(0x3u, &size));
    assert(size == 0u);
    assert(!kernel_exception_format_size(0u, NULL));

    astra_store_be16(raw + 6u, 0x3008u);
    assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_UNSUPPORTED_FORMAT);
    astra_store_be16(raw + 6u, 0x0009u);
    assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_INVALID_VECTOR);
}

static void test_access_fault_fixup_changes_only_pc(void)
{
    uint8_t raw[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint8_t original[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    KernelExceptionFrame frame;

    memset(raw, 0x5a, sizeof(raw));
    astra_store_be16(raw, 0x2000u);
    astra_store_be32(raw + 2u, 0x02012340u);
    astra_store_be16(raw + 6u, 0xb008u);
    astra_store_be16(raw + 10u, 0x0141u);
    astra_store_be32(raw + 16u, 0x10001fffu);
    memcpy(original, raw, sizeof(raw));

    assert(kernel_exception_set_program_counter(
               raw, sizeof(raw), 0x0201abc0u) == KERNEL_EXCEPTION_OK);
    assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_OK);
    assert(frame.program_counter == 0x0201abc0u);
    assert(memcmp(raw, original, 2u) == 0);
    assert(memcmp(raw + 6u, original + 6u, sizeof(raw) - 6u) == 0);

    astra_store_be16(raw + 6u, 0x0008u);
    assert(kernel_exception_set_program_counter(
               raw, sizeof(raw), 0x0201abc0u) ==
           KERNEL_EXCEPTION_UNSUPPORTED_FORMAT);
}

int main(void)
{
    test_all_motorola_formats();
    test_rejects_malformed_frames();
    test_access_fault_fixup_changes_only_pc();
    puts("KERNEL EXCEPTION FRAME PASS");
    return 0;
}
