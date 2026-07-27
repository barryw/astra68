#include "mmio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_BASE 0xfff00000u

static _Alignas(4) uint32_t registers[16];

static void test_width_alignment_and_range_validation(void)
{
    assert(kernel_mmio_test_bind(TEST_BASE, registers, sizeof(registers)));
    assert(kernel_mmio_address_valid(TEST_BASE, 1u));
    assert(kernel_mmio_address_valid(TEST_BASE + 2u, 2u));
    assert(kernel_mmio_address_valid(TEST_BASE + 4u, 4u));
    assert(!kernel_mmio_address_valid(TEST_BASE, 0u));
    assert(!kernel_mmio_address_valid(TEST_BASE, 3u));
    assert(!kernel_mmio_address_valid(TEST_BASE + 1u, 2u));
    assert(!kernel_mmio_address_valid(TEST_BASE + 2u, 4u));
    assert(!kernel_mmio_address_valid(TEST_BASE + sizeof(registers), 1u));
    assert(!kernel_mmio_address_valid(0x02000000u, 4u));
    assert(!kernel_mmio_test_bind(TEST_BASE, NULL, sizeof(registers)));
    assert(!kernel_mmio_test_bind(TEST_BASE, registers, 0u));
}

static void test_scalar_accesses_and_fence(void)
{
    for (uint32_t index = 0u; index < 16u; ++index)
        registers[index] = 0u;

    kernel_mmio_write32(TEST_BASE, 0x12345678u);
    assert(registers[0] == 0x12345678u);
    assert(kernel_mmio_read32(TEST_BASE) == 0x12345678u);

    kernel_mmio_write16(TEST_BASE + 4u, 0x55aau);
    assert(*(uint16_t *)(void *)&registers[1] == 0x55aau);
    assert(kernel_mmio_read16(TEST_BASE + 4u) == 0x55aau);

    kernel_mmio_write8(TEST_BASE + 8u, 0xa5u);
    assert(*(uint8_t *)(void *)&registers[2] == 0xa5u);
    assert(kernel_mmio_read8(TEST_BASE + 8u) == 0xa5u);

    registers[3] = 0x89abcdefu;
    assert(kernel_mmio_fence32(TEST_BASE + 12u) == 0x89abcdefu);
    assert(kernel_mmio_test_sync_count() == 1u);
    kernel_mmio_cpu_sync();
    assert(kernel_mmio_test_sync_count() == 2u);
}

int main(void)
{
    test_width_alignment_and_range_validation();
    test_scalar_accesses_and_fence();
    puts("MMIO tests passed");
    return 0;
}
