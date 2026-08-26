#define KERNEL_MMIO_IMPLEMENTATION 1
#include "mmio.h"

#include <astra/compiler.h>

#include <stddef.h>

#if defined(KERNEL_MMIO_HOST_TEST)
static volatile uint8_t *host_storage;
static uint32_t host_physical_base;
static uint32_t host_byte_size;
static uint32_t host_sync_count;
#endif

bool kernel_mmio_address_valid(uint32_t address, uint32_t width)
{
    if (!kernel_mmio_access_shape_valid(address, width))
        return false;
#if defined(KERNEL_MMIO_HOST_TEST)
    if (host_storage == NULL || address < host_physical_base ||
        width > host_byte_size ||
        address - host_physical_base > host_byte_size - width)
        return false;
#endif
    return true;
}

static __attribute__((noreturn)) void invalid_access(void)
{
    __builtin_trap();
    for (;;) {
    }
}

static volatile void *mmio_pointer(uint32_t address, uint32_t width)
{
    if (!kernel_mmio_address_valid(address, width))
        invalid_access();
#if defined(KERNEL_MMIO_HOST_TEST)
    return host_storage + (address - host_physical_base);
#else
    return (volatile void *)(uintptr_t)address;
#endif
}

uint8_t kernel_mmio_read8(uint32_t address)
{
    volatile uint8_t *pointer = mmio_pointer(address, sizeof(*pointer));
    uint8_t value;

    astra_compiler_barrier();
    value = *pointer;
    astra_compiler_barrier();
    return value;
}

uint16_t kernel_mmio_read16(uint32_t address)
{
    volatile uint16_t *pointer = mmio_pointer(address, sizeof(*pointer));
    uint16_t value;

    astra_compiler_barrier();
    value = *pointer;
    astra_compiler_barrier();
    return value;
}

uint32_t kernel_mmio_read32(uint32_t address)
{
    volatile uint32_t *pointer = mmio_pointer(address, sizeof(*pointer));
    uint32_t value;

    astra_compiler_barrier();
    value = *pointer;
    astra_compiler_barrier();
    return value;
}

void kernel_mmio_write8(uint32_t address, uint8_t value)
{
    volatile uint8_t *pointer = mmio_pointer(address, sizeof(*pointer));

    astra_compiler_barrier();
    *pointer = value;
    astra_compiler_barrier();
}

void kernel_mmio_write16(uint32_t address, uint16_t value)
{
    volatile uint16_t *pointer = mmio_pointer(address, sizeof(*pointer));

    astra_compiler_barrier();
    *pointer = value;
    astra_compiler_barrier();
}

void kernel_mmio_write32(uint32_t address, uint32_t value)
{
    volatile uint32_t *pointer = mmio_pointer(address, sizeof(*pointer));

    astra_compiler_barrier();
    *pointer = value;
    astra_compiler_barrier();
}

void kernel_mmio_cpu_sync(void)
{
    astra_compiler_barrier();
#if defined(KERNEL_MMIO_HOST_TEST)
    if (host_sync_count != UINT32_MAX)
        ++host_sync_count;
#else
    __asm__ volatile ("nop" ::: "memory");
#endif
    astra_compiler_barrier();
}

uint32_t kernel_mmio_fence32(uint32_t address)
{
    kernel_mmio_cpu_sync();
    return kernel_mmio_read32(address);
}

#if defined(KERNEL_MMIO_HOST_TEST)
bool kernel_mmio_test_bind(uint32_t physical_base, volatile void *storage,
                           uint32_t byte_size)
{
    if (storage == NULL || byte_size == 0u ||
        physical_base < KERNEL_MMIO_PHYSICAL_BASE ||
        physical_base > UINT32_MAX - (byte_size - 1u))
        return false;
    host_storage = storage;
    host_physical_base = physical_base;
    host_byte_size = byte_size;
    host_sync_count = 0u;
    return true;
}

uint32_t kernel_mmio_test_sync_count(void)
{
    return host_sync_count;
}
#endif
