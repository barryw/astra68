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
    case 0x3u:
        return 12u;
    case 0x7u:
        return 60u;
    default:
        return 0u;
    }
}

static void test_all_motorola_formats(void)
{
    static const uint8_t formats[] = {
        0x0u, 0x1u, 0x2u, 0x3u, 0x7u
    };

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
        if (formats[index] == 0x7u) {
            astra_store_be16(raw + 12u, 0x0525u);
            astra_store_be32(raw + 20u, 0xdffffff8u);
        }

        assert(kernel_exception_decode(raw, size, &frame) ==
               KERNEL_EXCEPTION_OK);
        assert(frame.format == formats[index]);
        assert(frame.frame_size == size);
        assert(frame.vector_offset == 0x0080u);
        assert(frame.program_counter == 0x12345678u + index);
        assert(frame.from_user == (index == 0u ? 0u : 1u));
        assert(frame.access_fault == (formats[index] == 0x7u));
        if (formats[index] == 0x2u)
            assert(frame.instruction_address == 0x87654321u);
        if (frame.access_fault != 0u) {
            assert(frame.special_status == 0x0525u);
            assert(frame.fault_address == 0xdffffff8u);
        }

        for (uint32_t available = 0u; available < size; ++available) {
            assert(kernel_exception_decode(raw, available, &frame) ==
                   KERNEL_EXCEPTION_TRUNCATED);
        }
    }
}

static void test_mc68040_access_semantics(void)
{
    uint8_t raw[KERNEL_EXCEPTION_FRAME_MAX_SIZE] = {0};
    KernelExceptionFrame frame;

    astra_store_be16(raw, 0x2000u);
    astra_store_be16(raw + 6u, 0x7008u);
    astra_store_be16(raw + 12u, 0x0405u);
    assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
           KERNEL_EXCEPTION_OK);
    assert(frame.access_write == 1u && frame.access_data == 1u);
    assert(frame.access_supervisor == 1u);
    assert(frame.access_size == 2u && frame.access_transfer_mode == 5u);
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
    assert(kernel_exception_format_size(0x3u, &size));
    assert(size == 12u);
    assert(!kernel_exception_format_size(0u, NULL));

    for (uint8_t format = 0x9u; format <= 0xbu; ++format) {
        astra_store_be16(raw + 6u,
                         (uint16_t)((uint16_t)format << 12 | 0x0008u));
        assert(kernel_exception_decode(raw, sizeof(raw), &frame) ==
               KERNEL_EXCEPTION_UNSUPPORTED_FORMAT);
    }
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
    astra_store_be16(raw + 6u, 0x7008u);
    astra_store_be16(raw + 12u, 0x0501u);
    astra_store_be32(raw + 20u, 0x10001fffu);
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
    test_mc68040_access_semantics();
    puts("KERNEL EXCEPTION FRAME PASS");
    return 0;
}
