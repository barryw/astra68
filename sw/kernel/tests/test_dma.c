#include "allocation.h"
#include "dma.h"
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

static void initialize_memory(void)
{
    AstraBootInfo info;

    memset(&info, 0, sizeof(info));
    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.abi_major = ASTRA_BOOT_ABI_MAJOR;
    info.abi_minor = ASTRA_BOOT_ABI_MINOR;
    info.total_size = sizeof(info);
    info.flags = ASTRA_BOOT_REQUIRED_FLAGS;
    info.machine_id = 0x41363801u;
    info.hardware_build_id = 0x12345678u;
    info.cpu_model = 0x00068030u;
    info.cpu_implementation = 0x54474d32u;
    info.cpu_features = 0x0000000du;
    info.cpu_hz = 12500000u;
    info.ram_base = 0x02000000u;
    info.ram_size = 0x02000000u;
    info.rom_base = 0xffe00000u;
    info.rom_size = ASTRA_ROM_BACKING_SIZE;
    info.kernel_base = ASTRA_KERNEL_LOAD_ADDRESS;
    info.kernel_image_size = 0x00010000u;
    info.kernel_memory_size = ASTRA_KERNEL_RESERVED_SIZE;
    info.kernel_entry = ASTRA_KERNEL_LOAD_ADDRESS;
    info.early_log_base = ASTRA_EARLY_LOG_ADDRESS;
    info.early_log_size = ASTRA_EARLY_LOG_SIZE;
    info.memory_range_entry_size = sizeof(AstraBootMemoryRange);

    add_range(&info, ASTRA_BOOT_SCRATCH_ADDRESS, ASTRA_BOOT_SCRATCH_SIZE,
              ASTRA_MEMORY_RANGE_FIRMWARE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(&info, ASTRA_EARLY_LOG_ADDRESS, ASTRA_EARLY_LOG_SIZE,
              ASTRA_MEMORY_RANGE_EARLY_LOG,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(&info, 0x02004000u, 0x0000c000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_USABLE_ADDRESS, ASTRA_KERNEL_USABLE_SIZE,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_ROM_BACKING_ADDRESS, ASTRA_ROM_BACKING_SIZE,
              ASTRA_MEMORY_RANGE_ROM_BACKING,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_EXECUTE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x03e40000u, 0x001c0000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    astra_boot_info_finalize(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    kernel_dma_init();
}

static void test_create_transfer_complete_and_stale_handle(void)
{
    KernelDmaHandle first;
    KernelDmaHandle second;
    KernelDmaBufferInfo info;
    KernelDmaToken token;
    KernelDmaToken stale;
    KernelFrameInfo frame;

    initialize_memory();
    assert(kernel_dma_create(7u, 5000u, 2u, &first) == KERNEL_DMA_OK);
    assert(kernel_dma_buffer_info(first, 7u, &info) == KERNEL_DMA_OK);
    assert(info.byte_size == 5000u && info.frame_count == 2u);
    assert((info.physical_base & (2u * KERNEL_PAGE_SIZE - 1u)) == 0u);
    assert(info.state == KERNEL_DMA_CPU_OWNED);

    assert(kernel_dma_begin(first, 7u, 4000u, 1000u,
                            KERNEL_DMA_FROM_DEVICE, 11u, &token) ==
           KERNEL_DMA_OK);
    assert(token.physical_address == info.physical_base + 4000u);
    assert(kernel_memory_frame_info(info.physical_base, &frame));
    assert(frame.pins == 1u);
    assert(kernel_memory_frame_info(info.physical_base + KERNEL_PAGE_SIZE,
                                    &frame));
    assert(frame.pins == 1u);
    assert(kernel_dma_close(first, 7u) == KERNEL_DMA_BUSY);

    stale = token;
    ++stale.device_generation;
    assert(kernel_dma_complete(&stale) == KERNEL_DMA_STALE);
    assert(kernel_dma_complete(&token) == KERNEL_DMA_OK);
    assert(kernel_memory_frame_info(info.physical_base, &frame));
    assert(frame.pins == 0u);
    assert(kernel_dma_close(first, 7u) == KERNEL_DMA_OK);
    assert(kernel_dma_buffer_info(first, 7u, &info) ==
           KERNEL_DMA_INVALID_HANDLE);

    assert(kernel_dma_create(7u, 4096u, 1u, &second) == KERNEL_DMA_OK);
    assert(second != first);
    assert(kernel_dma_begin(first, 7u, 0u, 512u,
                            KERNEL_DMA_TO_DEVICE, 12u, &token) ==
           KERNEL_DMA_INVALID_HANDLE);
    assert(kernel_dma_close(second, 7u) == KERNEL_DMA_OK);
}

static void test_checked_ranges_and_ownership(void)
{
    KernelDmaHandle handle;
    KernelDmaToken token;

    initialize_memory();
    assert(kernel_dma_create(3u, 4097u, 1u, &handle) == KERNEL_DMA_OK);
    assert(kernel_dma_begin(handle, 4u, 0u, 1u,
                            KERNEL_DMA_TO_DEVICE, 1u, &token) ==
           KERNEL_DMA_NOT_OWNED);
    assert(kernel_dma_begin(handle, 3u, 4097u, 1u,
                            KERNEL_DMA_TO_DEVICE, 1u, &token) ==
           KERNEL_DMA_INVALID_ARGUMENT);
    assert(kernel_dma_begin(handle, 3u, UINT32_MAX, 2u,
                            KERNEL_DMA_TO_DEVICE, 1u, &token) ==
           KERNEL_DMA_INVALID_ARGUMENT);
    assert(kernel_dma_begin(handle, 3u, 0u, 0u,
                            KERNEL_DMA_TO_DEVICE, 1u, &token) ==
           KERNEL_DMA_INVALID_ARGUMENT);
    assert(kernel_dma_begin(handle, 3u, 0u, 1u,
                            KERNEL_DMA_TO_DEVICE, 0u, &token) ==
           KERNEL_DMA_INVALID_ARGUMENT);
    assert(kernel_dma_close(handle, 3u) == KERNEL_DMA_OK);
}

static void test_owner_revoke_defers_in_flight_memory(void)
{
    KernelDmaHandle idle;
    KernelDmaHandle active;
    KernelDmaToken token;
    KernelDmaBufferInfo info;
    KernelDmaStats stats;
    uint32_t released = 0u;
    uint32_t deferred = 0u;
    uint32_t released_frames = 99u;

    initialize_memory();
    assert(kernel_dma_create(9u, 4096u, 1u, &idle) == KERNEL_DMA_OK);
    assert(kernel_dma_create(9u, 8192u, 1u, &active) == KERNEL_DMA_OK);
    assert(kernel_dma_begin(active, 9u, 0u, 8192u,
                            KERNEL_DMA_BIDIRECTIONAL, 77u, &token) ==
           KERNEL_DMA_OK);

    assert(kernel_dma_revoke_owner(9u, &released, &deferred) ==
           KERNEL_DMA_OK);
    assert(released == 1u && deferred == 1u);
    assert(kernel_dma_buffer_info(idle, 9u, &info) ==
           KERNEL_DMA_INVALID_HANDLE);
    assert(kernel_dma_buffer_info(active, 9u, &info) == KERNEL_DMA_OK);
    assert(info.state == KERNEL_DMA_REVOKING);
    assert(kernel_memory_release_owner(9u, &released_frames) ==
           KERNEL_MEMORY_BUSY);
    assert(released_frames == 99u);

    assert(kernel_dma_complete(&token) == KERNEL_DMA_OK);
    assert(kernel_dma_buffer_info(active, 9u, &info) ==
           KERNEL_DMA_INVALID_HANDLE);
    assert(kernel_memory_release_owner(9u, &released_frames) ==
           KERNEL_MEMORY_OK);
    assert(released_frames == 0u);
    assert(kernel_dma_complete(&token) == KERNEL_DMA_STALE);
    assert(kernel_dma_stats(&stats));
    assert(stats.live_buffers == 0u);
    assert(stats.in_flight_buffers == 0u);
    assert(stats.deferred_reclaims == 0u);
    assert(stats.stale_completions == 1u);
}

static void test_bounded_table(void)
{
    KernelDmaHandle handles[KERNEL_DMA_MAX_BUFFERS];
    KernelDmaHandle extra = KERNEL_DMA_HANDLE_INVALID;
    KernelDmaStats stats;

    initialize_memory();
    for (uint32_t index = 0u; index < KERNEL_DMA_MAX_BUFFERS; ++index)
        assert(kernel_dma_create(1u, 1u, 1u, &handles[index]) ==
               KERNEL_DMA_OK);
    assert(kernel_dma_create(1u, 1u, 1u, &extra) ==
           KERNEL_DMA_NO_RESOURCES);
    assert(kernel_dma_stats(&stats));
    assert(stats.live_buffers == KERNEL_DMA_MAX_BUFFERS);
    assert(stats.create_failures == 1u);
    for (uint32_t index = 0u; index < KERNEL_DMA_MAX_BUFFERS; ++index)
        assert(kernel_dma_close(handles[index], 1u) == KERNEL_DMA_OK);
}

static void test_allocation_injection_is_atomic(void)
{
    KernelAllocationStats object_stats;
    KernelAllocationStats page_stats;
    KernelDmaHandle handle = 0xdeadbeefu;
    KernelDmaStats dma_stats;
    KernelMemoryStats baseline;
    KernelMemoryStats after;

    initialize_memory();
    assert(kernel_memory_stats(&baseline));

    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_DMA_OBJECT, 1u);
    assert(kernel_dma_create(23u, KERNEL_PAGE_SIZE, 1u, &handle) ==
           KERNEL_DMA_NO_RESOURCES);
    assert(handle == KERNEL_DMA_HANDLE_INVALID);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);

    handle = 0xdeadbeefu;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_dma_create(23u, KERNEL_PAGE_SIZE, 1u, &handle) ==
           KERNEL_DMA_NO_RESOURCES);
    assert(handle == KERNEL_DMA_HANDLE_INVALID);

    handle = 0xdeadbeefu;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_DMA_PAGES, 1u);
    assert(kernel_dma_create(23u, KERNEL_PAGE_SIZE, 1u, &handle) ==
           KERNEL_DMA_OUT_OF_MEMORY);
    assert(handle == KERNEL_DMA_HANDLE_INVALID);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(after.owner_slots_used == baseline.owner_slots_used);

    handle = 0xdeadbeefu;
    kernel_allocation_test_fail_global(2u);
    assert(kernel_dma_create(23u, KERNEL_PAGE_SIZE, 1u, &handle) ==
           KERNEL_DMA_OUT_OF_MEMORY);
    assert(handle == KERNEL_DMA_HANDLE_INVALID);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(after.owner_slots_used == baseline.owner_slots_used);
    assert(kernel_dma_stats(&dma_stats));
    assert(dma_stats.live_buffers == 0u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_DMA_OBJECT, &object_stats));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_DMA_PAGES, &page_stats));
    assert(object_stats.current_units == 0u);
    assert(page_stats.current_units == 0u);
    assert(object_stats.injected_failures == 2u);
    assert(page_stats.injected_failures == 2u);
    assert(kernel_dma_valid());
    assert(kernel_allocation_valid());
}

int main(void)
{
    test_create_transfer_complete_and_stale_handle();
    test_checked_ranges_and_ownership();
    test_owner_revoke_defers_in_flight_memory();
    test_bounded_table();
    test_allocation_injection_is_atomic();
    puts("KERNEL DMA PASS");
    return 0;
}
