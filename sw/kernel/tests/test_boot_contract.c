#include <astra/boot.h>

#include "ohci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define USER_IMAGE_RESERVATION 0x2000u
#define USER_IMAGE_SIZE 0x1234u

static void add_range(AstraBootInfo *info, uint32_t base, uint32_t size,
                      uint32_t type, uint32_t flags)
{
    AstraBootMemoryRange *range =
        &info->memory_ranges[info->memory_range_count++];
    range->base = base;
    range->size = size;
    range->type = type;
    range->flags = flags;
}

static void make_valid_info(AstraBootInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->magic = ASTRA_BOOT_INFO_MAGIC;
    info->abi_major = ASTRA_BOOT_ABI_MAJOR;
    info->abi_minor = ASTRA_BOOT_ABI_MINOR;
    info->total_size = sizeof(*info);
    info->flags = ASTRA_BOOT_REQUIRED_FLAGS |
                  ASTRA_BOOT_FLAG_ICACHE_ENABLED |
                  ASTRA_BOOT_FLAG_DCACHE_ENABLED;
    info->machine_id = 0x41363801u;
    info->hardware_build_id = 0x12345678u;
    info->firmware_image_id = 0x89abcdefu;
    info->cpu_model = 0x00068030u;
    info->cpu_implementation = 0x54474d32u;
    info->cpu_features = 0x0000000du;
    info->cpu_hz = 12500000u;
    info->ram_base = 0x02000000u;
    info->ram_size = 0x02000000u;
    info->rom_base = ASTRA_ROM_ADDRESS;
    info->rom_size = ASTRA_ROM_SIZE;
    info->kernel_base = ASTRA_KERNEL_LOAD_ADDRESS;
    info->kernel_image_size = 0x00010000u;
    info->kernel_memory_size = ASTRA_KERNEL_RESERVED_SIZE;
    info->kernel_entry = ASTRA_KERNEL_LOAD_ADDRESS;
    info->early_log_base = ASTRA_EARLY_LOG_ADDRESS;
    info->early_log_size = ASTRA_EARLY_LOG_SIZE;
    info->user_image_base = ASTRA_USER_IMAGE_ADDRESS;
    info->user_image_size = USER_IMAGE_SIZE;
    info->memory_range_entry_size = sizeof(AstraBootMemoryRange);

    add_range(info, ASTRA_BOOT_SCRATCH_ADDRESS, ASTRA_BOOT_SCRATCH_SIZE,
              ASTRA_MEMORY_RANGE_FIRMWARE, ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(info, ASTRA_EARLY_LOG_ADDRESS, ASTRA_EARLY_LOG_SIZE,
              ASTRA_MEMORY_RANGE_EARLY_LOG, ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(info, ASTRA_USER_IMAGE_ADDRESS, USER_IMAGE_RESERVATION,
              ASTRA_MEMORY_RANGE_FIRMWARE, ASTRA_MEMORY_READ);
    add_range(info, ASTRA_USER_IMAGE_ADDRESS + USER_IMAGE_RESERVATION,
              ASTRA_USER_IMAGE_MAX_SIZE - USER_IMAGE_RESERVATION,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE | ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
              ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_KERNEL_USABLE_ADDRESS,
              OHCI_DMA_POOL_BASE - ASTRA_KERNEL_USABLE_ADDRESS,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE | ASTRA_MEMORY_CACHEABLE);
    add_range(info, OHCI_DMA_POOL_BASE, OHCI_DMA_POOL_SIZE,
              ASTRA_MEMORY_RANGE_DEVICE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    astra_boot_info_finalize(info);
}

static void test_boot_info(void)
{
    AstraBootInfo info;

    make_valid_info(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);
    assert(astra_boot_checksum(&info) == 0u);

    info.cpu_hz ^= 1u;
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_CHECKSUM);

    make_valid_info(&info);
    info.flags &= ~ASTRA_BOOT_FLAG_DMA_IDLE;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_FLAGS);

    make_valid_info(&info);
    info.kernel_entry = info.kernel_base + info.kernel_image_size;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_KERNEL);

    make_valid_info(&info);
    info.memory_ranges[3].base = info.memory_ranges[2].base;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_MEMORY_MAP);

    make_valid_info(&info);
    info.memory_ranges[info.memory_range_count - 1u].flags |=
        ASTRA_MEMORY_CACHEABLE;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_MEMORY_MAP);
}

static void test_user_image(void)
{
    AstraBootInfo info;

    /* Firmware may legitimately supply no initial user image. */
    make_valid_info(&info);
    info.user_image_base = 0u;
    info.user_image_size = 0u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);

    make_valid_info(&info);
    info.user_image_size = ASTRA_USER_IMAGE_MAX_SIZE + 1u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);

    make_valid_info(&info);
    info.user_image_base += 4u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);

    /* A size that runs past the reservation escapes firmware-owned memory. */
    make_valid_info(&info);
    info.user_image_size = USER_IMAGE_RESERVATION + 1u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);

    /* Memory the allocator will hand out cannot hold the image. */
    make_valid_info(&info);
    info.user_image_base = ASTRA_USER_IMAGE_ADDRESS + USER_IMAGE_RESERVATION;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);

    make_valid_info(&info);
    info.user_image_base = 0u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);

    make_valid_info(&info);
    info.user_image_size = 0u;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_BAD_USER_IMAGE);
}

static void test_early_log(void)
{
    union {
        uint32_t align;
        uint8_t bytes[64];
    } storage;
    AstraEarlyLog *log = (AstraEarlyLog *)storage.bytes;
    const uint8_t *data;

    memset(&storage, 0, sizeof(storage));
    astra_early_log_init(log, sizeof(storage));
    assert(astra_early_log_validate(log, sizeof(storage)));
    for (unsigned index = 0; index < 40u; ++index)
        astra_early_log_putc(log, (char)('A' + index));

    data = storage.bytes + sizeof(*log);
    assert(log->write_offset == 8u);
    assert(log->wrap_count == 1u);
    for (unsigned index = 0; index < 8u; ++index)
        assert(data[index] == (uint8_t)('A' + 32u + index));

    log->magic = 0u;
    assert(!astra_early_log_validate(log, sizeof(storage)));
}

int main(void)
{
    test_boot_info();
    test_user_image();
    test_early_log();
    puts("BOOT CONTRACT PASS");
    return 0;
}
