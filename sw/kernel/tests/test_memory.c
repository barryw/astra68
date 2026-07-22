#include "memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    info->flags = ASTRA_BOOT_REQUIRED_FLAGS;
    info->machine_id = 0x41363801u;
    info->hardware_build_id = 0x12345678u;
    info->cpu_model = 0x00068030u;
    info->cpu_implementation = 0x54474d32u;
    info->cpu_features = 0x0000000du;
    info->cpu_hz = 12500000u;
    info->ram_base = 0x02000000u;
    info->ram_size = 0x02000000u;
    info->rom_base = 0xffe00000u;
    info->rom_size = ASTRA_ROM_BACKING_SIZE;
    info->kernel_base = ASTRA_KERNEL_LOAD_ADDRESS;
    info->kernel_image_size = 0x00010000u;
    info->kernel_memory_size = ASTRA_KERNEL_RESERVED_SIZE;
    info->kernel_entry = ASTRA_KERNEL_LOAD_ADDRESS;
    info->early_log_base = ASTRA_EARLY_LOG_ADDRESS;
    info->early_log_size = ASTRA_EARLY_LOG_SIZE;
    info->memory_range_entry_size = sizeof(AstraBootMemoryRange);

    add_range(info, ASTRA_BOOT_SCRATCH_ADDRESS, ASTRA_BOOT_SCRATCH_SIZE,
              ASTRA_MEMORY_RANGE_FIRMWARE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(info, ASTRA_EARLY_LOG_ADDRESS, ASTRA_EARLY_LOG_SIZE,
              ASTRA_MEMORY_RANGE_EARLY_LOG,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(info, 0x02004000u, 0x0000c000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(info, 0x02090000u, 0x01d70000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_ROM_BACKING_ADDRESS, ASTRA_ROM_BACKING_SIZE,
              ASTRA_MEMORY_RANGE_ROM_BACKING,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_EXECUTE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(info, 0x03e40000u, 0x001c0000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    astra_boot_info_finalize(info);
}

static void test_initial_map(void)
{
    AstraBootInfo info;
    KernelMemoryStats stats;
    KernelFrameInfo frame;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.ram_base == 0x02000000u);
    assert(stats.total_frames == 8192u);
    assert(stats.free_frames == 7996u);
    assert(stats.high_water_frames == 196u);

    assert(kernel_memory_frame_info(ASTRA_EARLY_LOG_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_EARLY_LOG);
    assert(kernel_memory_frame_info(ASTRA_KERNEL_LOAD_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_KERNEL);
    assert(kernel_memory_frame_info(ASTRA_ROM_BACKING_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_ROM_BACKING);
    assert(kernel_memory_frame_info(0x02004000u, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
}

static void test_rejects_unclassified_and_unaligned_ram(void)
{
    AstraBootInfo info;

    make_valid_info(&info);
    info.memory_ranges[2].size -= KERNEL_PAGE_SIZE;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_INVALID_MAP);

    make_valid_info(&info);
    ++info.memory_ranges[2].base;
    --info.memory_ranges[2].size;
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_INVALID_MAP);
}

static void test_allocation_references_and_pins(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    KernelMemoryStats before;
    KernelMemoryStats stats;
    uint32_t base;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&before));
    assert(kernel_memory_alloc(3u, 4u, KERNEL_FRAME_PROCESS, 42u, &base) ==
           KERNEL_MEMORY_OK);
    assert((base & (4u * KERNEL_PAGE_SIZE - 1u)) == 0u);
    assert(kernel_memory_stats(&stats));
    assert(stats.free_frames == before.free_frames - 3u);
    assert(stats.high_water_frames == before.high_water_frames + 3u);
    assert(kernel_memory_range_owned(base + 17u,
                                     2u * KERNEL_PAGE_SIZE,
                                     42u, KERNEL_FRAME_PROCESS, false));
    assert(kernel_memory_frame_info(base, &frame));
    assert(frame.owner == 42u && frame.references == 1u && frame.pins == 0u);

    assert(kernel_memory_release(base, 3u, 7u) ==
           KERNEL_MEMORY_NOT_OWNED);
    assert(kernel_memory_retain(base, 3u, 42u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release(base, 3u, 42u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_pin(base, 3u, 42u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_range_owned(base, 3u * KERNEL_PAGE_SIZE,
                                     42u, KERNEL_FRAME_PROCESS, true));
    assert(kernel_memory_release(base, 3u, 42u) == KERNEL_MEMORY_BUSY);
    assert(kernel_memory_unpin(base, 3u, 42u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release(base, 3u, 42u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.free_frames == before.free_frames);
    assert(kernel_memory_frame_info(base, &frame));
    assert(frame.state == KERNEL_FRAME_FREE && frame.owner == 0u);
}

static void test_owner_teardown_is_atomic_while_dma_is_pinned(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    uint32_t first;
    uint32_t second;
    uint32_t released = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(2u, 1u, KERNEL_FRAME_PROCESS, 9u, &first) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_DMA, 9u, &second) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_pin(second, 1u, 9u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(9u, &released) == KERNEL_MEMORY_BUSY);
    assert(released == 0u);
    assert(kernel_memory_frame_info(first, &frame));
    assert(frame.owner == 9u);

    assert(kernel_memory_unpin(second, 1u, 9u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(9u, &released) == KERNEL_MEMORY_OK);
    assert(released == 3u);
    assert(kernel_memory_frame_info(first, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
    assert(kernel_memory_frame_info(second, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
}

static void test_reinit_discards_stale_dynamic_metadata(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    uint32_t base;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 55u, &base) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_pin(base, 1u, 55u) == KERNEL_MEMORY_OK);

    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_frame_info(base, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
    assert(frame.owner == KERNEL_OWNER_NONE);
    assert(frame.references == 0u && frame.pins == 0u);
}

static void test_exhaustion_and_checked_ranges(void)
{
    AstraBootInfo info;
    KernelMemoryStats stats;
    uint32_t first;
    uint32_t second;
    uint32_t third;
    uint32_t extra = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(12u, 1u, KERNEL_FRAME_PROCESS, 1u, &first) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(7536u, 1u, KERNEL_FRAME_PROCESS, 1u, &second) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(448u, 1u, KERNEL_FRAME_PROCESS, 1u, &third) ==
           KERNEL_MEMORY_OK);
    assert(first == 0x02004000u);
    assert(second == 0x02090000u);
    assert(third == 0x03e40000u);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 1u, &extra) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&stats));
    assert(stats.free_frames == 0u);
    assert(stats.allocation_failures == 1u);
    assert(kernel_memory_alloc(1u, 3u, KERNEL_FRAME_PROCESS, 1u, &extra) ==
           KERNEL_MEMORY_INVALID_ARGUMENT);
    assert(!kernel_memory_range_owned(0x03fffff0u, 32u, 1u,
                                      KERNEL_FRAME_PROCESS, false));
}

int main(void)
{
    test_initial_map();
    test_rejects_unclassified_and_unaligned_ram();
    test_allocation_references_and_pins();
    test_owner_teardown_is_atomic_while_dma_is_pinned();
    test_reinit_discards_stale_dynamic_metadata();
    test_exhaustion_and_checked_ranges();
    puts("KERNEL MEMORY PASS");
    return 0;
}
