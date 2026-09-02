#ifndef ASTRA_KERNEL_MMIO_H
#define ASTRA_KERNEL_MMIO_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_MMIO_PHYSICAL_BASE 0xffd00000u
#define KERNEL_MMIO_PHYSICAL_LIMIT UINT32_MAX

static inline __attribute__((always_inline)) bool
kernel_mmio_access_shape_valid(uint32_t address, uint32_t width)
{
    return (width == 1u || width == 2u || width == 4u) &&
           address >= KERNEL_MMIO_PHYSICAL_BASE &&
           (address & (width - 1u)) == 0u &&
           address <= UINT32_MAX - (width - 1u);
}

bool kernel_mmio_address_valid(uint32_t address, uint32_t width);

#if defined(KERNEL_MMIO_HOST_TEST) || defined(KERNEL_MMIO_IMPLEMENTATION)
uint8_t kernel_mmio_read8(uint32_t address);
uint16_t kernel_mmio_read16(uint32_t address);
uint32_t kernel_mmio_read32(uint32_t address);
void kernel_mmio_write8(uint32_t address, uint8_t value);
void kernel_mmio_write16(uint32_t address, uint16_t value);
void kernel_mmio_write32(uint32_t address, uint32_t value);
#else
static inline __attribute__((always_inline)) void
kernel_mmio_inline_validate(uint32_t address, uint32_t width)
{
    if (!kernel_mmio_access_shape_valid(address, width))
        __builtin_trap();
}

static inline __attribute__((always_inline)) void
kernel_mmio_inline_barrier(void)
{
    __asm__ volatile ("" ::: "memory");
}

static inline __attribute__((always_inline)) uint8_t
kernel_mmio_read8(uint32_t address)
{
    uint8_t value;

    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    value = *(volatile uint8_t *)(uintptr_t)address;
    kernel_mmio_inline_barrier();
    return value;
}

static inline __attribute__((always_inline)) uint16_t
kernel_mmio_read16(uint32_t address)
{
    uint16_t value;

    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    value = *(volatile uint16_t *)(uintptr_t)address;
    kernel_mmio_inline_barrier();
    return value;
}

static inline __attribute__((always_inline)) uint32_t
kernel_mmio_read32(uint32_t address)
{
    uint32_t value;

    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    value = *(volatile uint32_t *)(uintptr_t)address;
    kernel_mmio_inline_barrier();
    return value;
}

static inline __attribute__((always_inline)) void
kernel_mmio_write8(uint32_t address, uint8_t value)
{
    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    *(volatile uint8_t *)(uintptr_t)address = value;
    kernel_mmio_inline_barrier();
}

static inline __attribute__((always_inline)) void
kernel_mmio_write16(uint32_t address, uint16_t value)
{
    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    *(volatile uint16_t *)(uintptr_t)address = value;
    kernel_mmio_inline_barrier();
}

static inline __attribute__((always_inline)) void
kernel_mmio_write32(uint32_t address, uint32_t value)
{
    kernel_mmio_inline_validate(address, sizeof(value));
    kernel_mmio_inline_barrier();
    *(volatile uint32_t *)(uintptr_t)address = value;
    kernel_mmio_inline_barrier();
}
#endif

void kernel_mmio_cpu_sync(void);
uint32_t kernel_mmio_fence32(uint32_t address);

#if defined(KERNEL_MMIO_HOST_TEST)
bool kernel_mmio_test_bind(uint32_t physical_base, volatile void *storage,
                           uint32_t byte_size);
uint32_t kernel_mmio_test_sync_count(void);
#endif

#endif
