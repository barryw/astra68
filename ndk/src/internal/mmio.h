#ifndef ASTRA_INTERNAL_MMIO_H
#define ASTRA_INTERNAL_MMIO_H

#include <stdint.h>

#ifdef ASTRA_NDK_TEST

uint32_t astra_ndk_test_mmio_read32(uint32_t address);
void astra_ndk_test_mmio_write32(uint32_t address, uint32_t value);

static inline uint32_t astra_mmio_read32(uint32_t address)
{
    return astra_ndk_test_mmio_read32(address);
}

static inline void astra_mmio_write32(uint32_t address, uint32_t value)
{
    astra_ndk_test_mmio_write32(address, value);
}

#else

static inline uint32_t astra_mmio_read32(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}

static inline void astra_mmio_write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

#endif

#endif
