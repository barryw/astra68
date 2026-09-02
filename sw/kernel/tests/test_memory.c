#include "memory.h"
#include "ohci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * The largest machine these tests build. There is no kernel-side frame ceiling
 * any more -- the tables are carved from RAM at init -- so a test that wants a
 * scratch array per frame states the size of its own synthetic machine.
 */
#define TEST_MAX_FRAMES (ASTRA_RAM_SIZE_ARTY_GUEST / KERNEL_PAGE_SIZE)

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
    info->rom_base = ASTRA_ROM_ADDRESS;
    info->rom_size = ASTRA_ROM_SIZE;
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
    /*
     * The user image hole, expressed through the constants rather than
     * literals: it moved when ABI 0.4 raised the ceiling, and a hardcoded
     * range silently left a gap that only surfaced as a failed init.
     */
    add_range(info, ASTRA_USER_IMAGE_ADDRESS, ASTRA_USER_IMAGE_MAX_SIZE,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(info, ASTRA_KERNEL_USABLE_ADDRESS, (OHCI_DMA_POOL_BASE - ASTRA_KERNEL_USABLE_ADDRESS),
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(info, OHCI_DMA_POOL_BASE, OHCI_DMA_POOL_SIZE,
              ASTRA_MEMORY_RANGE_DEVICE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
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
    assert(stats.free_frames ==
           (ASTRA_USER_IMAGE_MAX_SIZE +
            (OHCI_DMA_POOL_BASE - ASTRA_KERNEL_USABLE_ADDRESS)) /
                   KERNEL_PAGE_SIZE -
               KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(stats.high_water_frames == stats.total_frames - stats.free_frames);
    assert(stats.owner_slots_used == 0u);
    assert(stats.owner_release_operations == 0u);
    assert(stats.owner_release_frame_visits == 0u);
    assert(stats.emergency_total_frames ==
           KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(stats.emergency_available_frames ==
           KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(stats.emergency_acquisitions == 0u);
    assert(stats.emergency_failures == 0u);
    assert(stats.protected_reserve_frames ==
           stats.total_frames / ASTRA_PROCESS_COUNT_MAX);
    assert(stats.protected_reserve_denials == 0u);
    assert(stats.protected_owners == 0u);

    assert(kernel_memory_frame_info(ASTRA_EARLY_LOG_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_EARLY_LOG);
    assert(kernel_memory_frame_info(ASTRA_KERNEL_LOAD_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_KERNEL);
    assert(kernel_memory_frame_info(ASTRA_KERNEL_TRACE_ADDRESS, &frame));
    assert(frame.state == KERNEL_FRAME_KERNEL);
    assert(kernel_memory_frame_info(0x03e00000u, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
    assert(kernel_memory_frame_info(0x02004000u, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
    assert(kernel_memory_frame_info(0x03eff000u, &frame));
    assert(frame.state == KERNEL_FRAME_EMERGENCY_RESERVED);
    assert(kernel_memory_frame_info(0x03fff000u, &frame));
    assert(frame.state == KERNEL_FRAME_DEVICE);
}

static void test_arty_guest_map(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    KernelMemoryStats stats;

    make_valid_info(&info);
    info.ram_size = ASTRA_RAM_SIZE_ARTY_GUEST;
    add_range(&info, 0x04000000u, 0x06000000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    astra_boot_info_finalize(&info);

    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.total_frames == TEST_MAX_FRAMES);
    assert(kernel_memory_frame_info(0x09fff000u, &frame));
    assert(frame.state == KERNEL_FRAME_EMERGENCY_RESERVED);
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
    assert(kernel_memory_alloc_zeroed(3u, 4u, KERNEL_FRAME_PROCESS, 42u,
                                      &base) == KERNEL_MEMORY_OK);
    assert((base & (4u * KERNEL_PAGE_SIZE - 1u)) == 0u);
    assert(kernel_memory_stats(&stats));
    assert(stats.free_frames == before.free_frames - 3u);
    assert(stats.high_water_frames == before.high_water_frames + 3u);
    assert(stats.owner_slots_used == 1u);
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
    assert(stats.owner_slots_used == 0u);
    assert(kernel_memory_frame_info(base, &frame));
    assert(frame.state == KERNEL_FRAME_FREE && frame.owner == 0u);
}

static void test_owner_teardown_is_atomic_while_dma_is_pinned(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    KernelMemoryStats after_busy;
    KernelMemoryStats after_release;
    uint32_t first;
    uint32_t second;
    uint32_t released = 99u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(2u, 1u, KERNEL_FRAME_PROCESS, 9u, &first) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_DMA, 9u, &second) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_pin(second, 1u, 9u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(9u, &released) == KERNEL_MEMORY_BUSY);
    assert(released == 99u);
    assert(kernel_memory_stats(&after_busy));
    assert(after_busy.owner_slots_used == 1u);
    assert(after_busy.owner_release_operations == 1u);
    assert(after_busy.owner_release_frame_visits == 1u);
    assert(kernel_memory_frame_info(first, &frame));
    assert(frame.owner == 9u);

    assert(kernel_memory_unpin(second, 1u, 9u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(9u, &released) == KERNEL_MEMORY_OK);
    assert(released == 3u);
    assert(kernel_memory_stats(&after_release));
    assert(after_release.owner_slots_used == 0u);
    assert(after_release.owner_release_operations == 2u);
    assert(after_release.owner_release_frame_visits == 7u);
    assert(kernel_memory_frame_info(first, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
    assert(kernel_memory_frame_info(second, &frame));
    assert(frame.state == KERNEL_FRAME_FREE);
}

static void test_owner_frame_count_tracks_unique_frames(void)
{
    AstraBootInfo info;
    uint32_t base;
    uint32_t frame_count = UINT32_MAX;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(!kernel_memory_owner_frames(KERNEL_OWNER_NONE, &frame_count));
    assert(!kernel_memory_owner_frames(41u, NULL));
    assert(kernel_memory_owner_frames(41u, &frame_count));
    assert(frame_count == 0u);

    assert(kernel_memory_alloc(3u, 1u, KERNEL_FRAME_SHARED, 41u, &base) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_owner_frames(41u, &frame_count));
    assert(frame_count == 3u);
    assert(kernel_memory_retain(base, 3u, 41u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release(base, 3u, 41u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_owner_frames(41u, &frame_count));
    assert(frame_count == 3u);
    assert(kernel_memory_release(base, 3u, 41u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_owner_frames(41u, &frame_count));
    assert(frame_count == 0u);
}

static void test_owner_release_work_scales_with_owned_frames(void)
{
    AstraBootInfo info;
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t large_base;
    uint32_t small_base;
    uint32_t released = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1000u, 1u, KERNEL_FRAME_PROCESS, 7u,
                               &large_base) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(3u, 1u, KERNEL_FRAME_PROCESS, 9u,
                               &small_base) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&before));

    assert(kernel_memory_release_owner(9u, &released) == KERNEL_MEMORY_OK);
    assert(released == 3u);
    assert(kernel_memory_stats(&after));
    assert(after.owner_release_frame_visits -
               before.owner_release_frame_visits == 6u);
    assert(after.free_frames == before.free_frames + 3u);
    assert(kernel_memory_range_owned(large_base, 1000u * KERNEL_PAGE_SIZE,
                                     7u, KERNEL_FRAME_PROCESS, false));
    assert(!kernel_memory_range_owned(small_base, 3u * KERNEL_PAGE_SIZE,
                                      9u, KERNEL_FRAME_PROCESS, false));

    assert(kernel_memory_release_owner(7u, &released) == KERNEL_MEMORY_OK);
    assert(released == 1000u);
}

static void test_owner_ledger_capacity_is_bounded_and_reusable(void)
{
    AstraBootInfo info;
    KernelMemoryStats stats;
    uint32_t bases[KERNEL_MAX_FRAME_OWNERS];
    uint32_t replacement;
    uint32_t released;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    for (uint32_t index = 0u; index < KERNEL_MAX_FRAME_OWNERS; ++index) {
        assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS,
                                   100u + index, &bases[index]) ==
               KERNEL_MEMORY_OK);
    }
    assert(kernel_memory_stats(&stats));
    assert(stats.owner_slots_used == KERNEL_MAX_FRAME_OWNERS);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 1000u,
                               &replacement) == KERNEL_MEMORY_OUT_OF_MEMORY);

    assert(kernel_memory_release_owner(100u, &released) == KERNEL_MEMORY_OK);
    assert(released == 1u);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 1000u,
                               &replacement) == KERNEL_MEMORY_OK);
    for (uint32_t index = 1u; index < KERNEL_MAX_FRAME_OWNERS; ++index) {
        assert(kernel_memory_release(bases[index], 1u, 100u + index) ==
               KERNEL_MEMORY_OK);
    }
    assert(kernel_memory_release(replacement, 1u, 1000u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.owner_slots_used == 0u);
}

static void test_reinit_discards_stale_dynamic_metadata(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    KernelMemoryStats stats;
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
    assert(kernel_memory_stats(&stats));
    assert(stats.owner_slots_used == 0u);
}

static void test_scattered_page_allocation_is_atomic(void)
{
    static uint32_t impossible[TEST_MAX_FRAMES];
    AstraBootInfo info;
    KernelMemoryStats before_failure;
    KernelMemoryStats after_failure;
    uint32_t contiguous;
    uint32_t pages[3];

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(6u, 1u, KERNEL_FRAME_PROCESS, 41u,
                               &contiguous) == KERNEL_MEMORY_OK);
    for (uint32_t page = 0u; page < 6u; page += 2u)
        assert(kernel_memory_release(contiguous + page * KERNEL_PAGE_SIZE,
                                     1u, 41u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc_pages_zeroed(
               3u, KERNEL_FRAME_SHARED, 42u, pages) == KERNEL_MEMORY_OK);
    assert(pages[0] == contiguous);
    assert(pages[1] == contiguous + 2u * KERNEL_PAGE_SIZE);
    assert(pages[2] == contiguous + 4u * KERNEL_PAGE_SIZE);
    assert(kernel_memory_release_owner(41u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(42u, NULL) == KERNEL_MEMORY_OK);

    for (uint32_t index = 0u; index < TEST_MAX_FRAMES; ++index)
        impossible[index] = UINT32_MAX;
    assert(kernel_memory_stats(&before_failure));
    assert(kernel_memory_alloc_pages_zeroed(
               before_failure.total_frames, KERNEL_FRAME_SHARED, 43u,
               impossible) == KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&after_failure));
    assert(after_failure.free_frames == before_failure.free_frames);
    assert(after_failure.owner_slots_used == before_failure.owner_slots_used);
    for (uint32_t index = 0u; index < before_failure.free_frames; ++index)
        assert(impossible[index] == 0u);
}

static void test_exhaustion_and_checked_ranges(void)
{
    AstraBootInfo info;
    KernelMemoryStats stats;
    uint32_t first;
    uint32_t second;
    uint32_t extra = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_protect_owner(1u));
    assert(kernel_memory_stats(&stats));
    /*
     * Drains the two usable regions exactly, so the sizes are derived from
     * the layout rather than written out: ABI 0.4 moved 52 frames from the tail
     * into the user image hole and literal counts stopped matching.
     */
    assert(kernel_memory_alloc(ASTRA_USER_IMAGE_MAX_SIZE / 0x1000u, 1u,
                               KERNEL_FRAME_PROCESS, 1u, &first) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(
               (OHCI_DMA_POOL_BASE - ASTRA_KERNEL_USABLE_ADDRESS) / 0x1000u -
                   KERNEL_EMERGENCY_RESERVE_FRAMES - stats.dma_zone_frames,
               1u, KERNEL_FRAME_PROCESS, 1u, &second) ==
           KERNEL_MEMORY_OK);
    assert(first == ASTRA_USER_IMAGE_ADDRESS);
    assert(second == ASTRA_KERNEL_USABLE_ADDRESS);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 1u, &extra) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&stats));
    /*
     * The zone's frames are still free -- they are unspent, not blocked --
     * they are simply not free to *this* kind of allocation, which is the
     * whole of what reserving them means.
     */
    assert(stats.free_frames == stats.dma_zone_frames);
    assert(stats.allocation_failures == 1u);
    assert(kernel_memory_alloc(1u, 3u, KERNEL_FRAME_PROCESS, 1u, &extra) ==
           KERNEL_MEMORY_INVALID_ARGUMENT);
    assert(!kernel_memory_range_owned(0x03fffff0u, 32u, 1u,
                                      KERNEL_FRAME_PROCESS, false));
    assert(kernel_memory_release(first, 12u, 1u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_protect_owner(2u));
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 2u, &extra) ==
           KERNEL_MEMORY_OK);
    assert(extra == first);
}

static void test_emergency_reserve_isolated_and_replenished(void)
{
    AstraBootInfo info;
    KernelAllocationStats cleanup_stats;
    KernelAllocationStats reserve_stats;
    KernelMemoryStats baseline;
    KernelMemoryStats exhausted;
    KernelMemoryStats borrowed;
    KernelMemoryStats restored;
    KernelAllocationSite site;
    uint32_t emergency[KERNEL_EMERGENCY_RESERVE_FRAMES];
    uint32_t first;
    uint32_t second;
    uint32_t extra;
    uint32_t released = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_protect_owner(1u));
    assert(kernel_memory_stats(&baseline));
    assert(kernel_memory_alloc(ASTRA_USER_IMAGE_MAX_SIZE / 0x1000u, 1u,
                               KERNEL_FRAME_PROCESS, 1u, &first) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(
               (OHCI_DMA_POOL_BASE - ASTRA_KERNEL_USABLE_ADDRESS) / 0x1000u -
                   KERNEL_EMERGENCY_RESERVE_FRAMES - baseline.dma_zone_frames,
               1u, KERNEL_FRAME_PROCESS, 1u, &second) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&exhausted));
    assert(exhausted.free_frames == baseline.dma_zone_frames);
    assert(exhausted.emergency_available_frames ==
           KERNEL_EMERGENCY_RESERVE_FRAMES);

    for (uint32_t index = 0u;
         index < KERNEL_EMERGENCY_RESERVE_FRAMES; ++index) {
        assert(kernel_memory_emergency_acquire(
                   KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE,
                   KERNEL_FRAME_PROCESS, 2u, &emergency[index]) ==
               KERNEL_MEMORY_OK);
        assert(kernel_memory_frame_allocation_site(emergency[index], &site));
        assert(site == KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE);
        assert(kernel_memory_retain(emergency[index], 1u, 2u) ==
               KERNEL_MEMORY_BUSY);
        assert(kernel_memory_pin(emergency[index], 1u, 2u) ==
               KERNEL_MEMORY_BUSY);
    }
    assert(kernel_memory_emergency_acquire(
               KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE,
               KERNEL_FRAME_PROCESS, 2u, &extra) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&borrowed));
    /* The DMA zone is untouched by any of this; it is not general memory. */
    assert(borrowed.free_frames == baseline.dma_zone_frames);
    assert(borrowed.emergency_available_frames == 0u);
    assert(borrowed.emergency_acquisitions ==
           KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(borrowed.emergency_failures == 1u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_EMERGENCY_RESERVE, &reserve_stats));
    assert(reserve_stats.current_units == 0u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE, &cleanup_stats));
    assert(cleanup_stats.current_units == KERNEL_EMERGENCY_RESERVE_FRAMES);

    assert(kernel_memory_release_owner(2u, &released) == KERNEL_MEMORY_OK);
    assert(released == KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(kernel_memory_stats(&restored));
    assert(restored.free_frames == baseline.dma_zone_frames);
    assert(restored.emergency_available_frames ==
           KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_EMERGENCY_RESERVE, &reserve_stats));
    assert(reserve_stats.current_units == KERNEL_EMERGENCY_RESERVE_FRAMES);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE, &cleanup_stats));
    assert(cleanup_stats.current_units == 0u);

    assert(kernel_memory_release_owner(1u, &released) == KERNEL_MEMORY_OK);
    /* Owner 1 never held the zone, so it cannot give it back. */
    assert(released == baseline.free_frames - baseline.dma_zone_frames);
    assert(kernel_memory_stats(&restored));
    assert(restored.free_frames == baseline.free_frames);
    assert(restored.emergency_available_frames ==
           baseline.emergency_available_frames);
    assert(kernel_allocation_valid());
    (void)first;
    (void)second;
}

static void test_protected_reserve_survives_ordinary_exhaustion(void)
{
    AstraBootInfo info;
    KernelMemoryStats baseline;
    KernelMemoryStats exhausted;
    uint32_t pages[TEST_MAX_FRAMES];
    uint32_t protected_page = 0u;
    uint32_t extra = 0u;
    uint32_t ordinary;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&baseline));
    ordinary = baseline.free_frames - baseline.dma_zone_frames -
               baseline.protected_reserve_frames;
    assert(kernel_memory_alloc_pages_zeroed(
               ordinary, KERNEL_FRAME_PROCESS, 10u, pages) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 10u, &extra) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&exhausted));
    assert(exhausted.free_frames == baseline.dma_zone_frames +
                                      baseline.protected_reserve_frames);
    assert(exhausted.protected_reserve_denials == 1u);

    assert(kernel_memory_protect_owner(11u));
    assert(kernel_memory_owner_protected(11u));
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 11u,
                               &protected_page) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release(protected_page, 1u, 11u) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_unprotect_owner(11u));
    assert(!kernel_memory_owner_protected(11u));
    assert(kernel_memory_release_owner(10u, NULL) == KERNEL_MEMORY_OK);
}

static void test_tagged_failure_injection_and_boot_retirement(void)
{
    AstraBootInfo info;
    KernelAllocationStats boot_stats;
    KernelAllocationSite site;
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t physical = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&before));
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE, 1u);
    assert(kernel_memory_alloc_zeroed_tagged(
               KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE, 1u, 1u,
               KERNEL_FRAME_PROCESS, 7u, &physical) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
    assert(after.owner_slots_used == before.owner_slots_used);

    physical = 0xdeadbeefu;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u);
    assert(kernel_memory_alloc_tagged(
               KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 1u,
               KERNEL_FRAME_PROCESS, 8u, &physical) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(physical == 0u);
    physical = 0xdeadbeefu;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_memory_alloc_tagged(
               KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 1u,
               KERNEL_FRAME_PROCESS, 8u, &physical) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(physical == 0u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, &boot_stats));
    assert(boot_stats.current_units == 0u);
    assert(boot_stats.injected_failures == 2u);

    assert(kernel_memory_alloc_tagged(
               KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 1u,
               KERNEL_FRAME_PROCESS, 8u, &physical) == KERNEL_MEMORY_OK);
    assert(kernel_memory_frame_allocation_site(physical, &site));
    assert(site == KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE);
    assert(!kernel_allocation_retire_boot());
    assert(kernel_memory_release(physical, 1u, 8u) == KERNEL_MEMORY_OK);
    assert(kernel_allocation_retire_boot());
    assert(kernel_memory_alloc_tagged(
               KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 1u,
               KERNEL_FRAME_PROCESS, 8u, &physical) ==
           KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
    assert(kernel_allocation_valid());
}

static void test_emergency_site_injection_is_atomic(void)
{
    static const KernelAllocationSite sites[] = {
        KERNEL_ALLOCATION_SITE_EMERGENCY_FAULT_PAGE,
        KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE,
        KERNEL_ALLOCATION_SITE_EMERGENCY_LOG_PAGE
    };
    AstraBootInfo info;
    KernelAllocationStats allocation_stats;
    KernelMemoryStats baseline;
    KernelMemoryStats after;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&baseline));
    for (uint32_t index = 0u;
         index < sizeof(sites) / sizeof(sites[0]); ++index) {
        uint32_t physical = 0xdeadbeefu;

        kernel_allocation_test_fail_site(sites[index], 1u);
        assert(kernel_memory_emergency_acquire(
                   sites[index], KERNEL_FRAME_PROCESS, 71u + index,
                   &physical) == KERNEL_MEMORY_OUT_OF_MEMORY);
        assert(physical == 0u);
        assert(kernel_memory_stats(&after));
        assert(after.free_frames == baseline.free_frames);
        assert(after.emergency_available_frames ==
               baseline.emergency_available_frames);
        assert(after.owner_slots_used == baseline.owner_slots_used);
        assert(kernel_allocation_site_stats(sites[index],
                                            &allocation_stats));
        assert(allocation_stats.current_units == 0u);
        assert(allocation_stats.injected_failures == 1u);

        physical = 0xdeadbeefu;
        kernel_allocation_test_fail_global(1u);
        assert(kernel_memory_emergency_acquire(
                   sites[index], KERNEL_FRAME_PROCESS, 71u + index,
                   &physical) == KERNEL_MEMORY_OUT_OF_MEMORY);
        assert(physical == 0u);
        assert(kernel_memory_stats(&after));
        assert(after.emergency_available_frames ==
               baseline.emergency_available_frames);
        assert(kernel_allocation_site_stats(sites[index],
                                            &allocation_stats));
        assert(allocation_stats.current_units == 0u);
        assert(allocation_stats.injected_failures == 2u);
    }
    assert(kernel_allocation_valid());
}

static void test_retained_log_is_allocation_free_under_pressure(void)
{
    union {
        uint32_t align;
        uint8_t bytes[64];
    } storage;
    AstraBootInfo info;
    AstraEarlyLog *log = (AstraEarlyLog *)(void *)storage.bytes;
    KernelAllocationStats generic_stats;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    uint32_t physical = 0xdeadbeefu;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    memset(&storage, 0, sizeof(storage));
    astra_early_log_init(log, sizeof(storage));
    assert(kernel_memory_stats(&baseline));

    kernel_allocation_test_fail_global(1u);
    astra_early_log_puts(log, "K9 pressure log\n");
    assert(astra_early_log_validate(log, sizeof(storage)));
    assert(log->write_offset == 16u);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 81u,
                               &physical) == KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(physical == 0u);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(after.emergency_available_frames ==
           baseline.emergency_available_frames);

    physical = 0xdeadbeefu;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_MEMORY_GENERIC, 1u);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 81u,
                               &physical) == KERNEL_MEMORY_OUT_OF_MEMORY);
    assert(physical == 0u);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_MEMORY_GENERIC, &generic_stats));
    assert(generic_stats.current_units == 0u);
    assert(generic_stats.injected_failures == 2u);
    assert(kernel_allocation_valid());
}

/*
 * Whether physical fragmentation is a real risk on this machine or only a
 * theoretical one, which is the question section 5 item 5 of
 * The memory policy has to answer before choosing between a
 * buddy allocator, a reserved zone, and doing nothing.
 *
 * The whole kernel asks for exactly one contiguous multi-frame run: DMA. The
 * display's framebuffer is ASTRA_RENDER_BUILDER_BYTES, 256 KiB, which is 64
 * frames at alignment one, and a block transfer is two. Everything else --
 * stacks, code pages, page tables, library pages, and every page of an area --
 * asks for one frame at a time and does not care where it lands.
 *
 * So this combs the frame map and then asks for the display's 64. If that can
 * fail, a display service restarting at hour six can fail, and the answer is
 * worth building. If it cannot, the whole item is theatre.
 */
static void test_contiguous_demand_survives_a_combed_frame_map(void)
{
    static uint32_t every_frame[TEST_MAX_FRAMES];
    AstraBootInfo info;
    KernelMemoryStats before;
    KernelMemoryStats after_comb;
    KernelMemoryStats stats;
    uint32_t taken;
    uint32_t framebuffer = 0u;
    uint32_t transfer = 0u;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_protect_owner(70u));
    assert(kernel_memory_stats(&before));
    assert(kernel_memory_stats(&stats));
    /* Everything the general pool can reach -- the zone is not part of it. */
    taken = before.free_frames - before.dma_zone_frames;
    /*
     * Every free frame, one at a time, the way every non-DMA allocation on
     * this machine takes them.
     */
    assert(kernel_memory_alloc_pages_zeroed(
               taken, KERNEL_FRAME_PROCESS, 70u, every_frame) ==
           KERNEL_MEMORY_OK);
    /* Then half of them back, alternating: the worst case, and a reachable
     * one -- it is what a long run of processes starting and exiting tends
     * toward. */
    for (uint32_t index = 0u; index < taken; index += 2u)
        assert(kernel_memory_release(every_frame[index], 1u, 70u) ==
               KERNEL_MEMORY_OK);

    /*
     * Half the machine is free and, outside the zone, the largest run in it is
     * one frame -- so this is the hour-six state the zone exists for.
     *
     * Both still succeed, and that is the whole result: the frames come from
     * the reserved zone, which the comb above could never reach because the
     * scattered path is not allowed into it. Without the zone this same test
     * refused a two-frame transfer.
     */
    assert(kernel_memory_stats(&after_comb));
    assert(after_comb.dma_zone_frames == KERNEL_DMA_ZONE_FRAMES);
    assert(kernel_memory_alloc(2u, 1u, KERNEL_FRAME_DMA, 71u, &transfer) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(64u, 1u, KERNEL_FRAME_DMA, 71u,
                               &framebuffer) == KERNEL_MEMORY_OK);
    /* Both landed inside the zone rather than in the combed general pool. */
    {
        uint32_t zone_base = stats.ram_base +
            after_comb.dma_zone_first * KERNEL_PAGE_SIZE;
        uint32_t zone_end = zone_base +
            after_comb.dma_zone_frames * KERNEL_PAGE_SIZE;

        assert(transfer >= zone_base && transfer < zone_end);
        assert(framebuffer >= zone_base && framebuffer < zone_end);
    }
    assert(kernel_memory_release_owner(70u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(71u, NULL) == KERNEL_MEMORY_OK);
}

/*
 * A board larger than the constant this used to be compiled against.
 *
 * KERNEL_MAX_FRAMES was ASTRA_RAM_SIZE_ARTY_GUEST / 4096 -- 32768 -- and
 * `kernel_memory_init` refused any map larger than it outright, so a board
 * with more RAM than the image was built for did not boot. Behind that sat a
 * second, quieter ceiling: the owner frame links were `uint16_t`, so even
 * raising the constant would have stopped at 65535 frames, 256 MB.
 *
 * This builds a gigabyte and expects a working machine, not merely one that
 * initialised. The DMA zone is the useful witness -- it is placed at the top
 * of memory, so on this machine it sits at a frame index the old build could
 * not represent, and allocating from it and giving it back walks the owner
 * links at that index.
 */
static void test_frame_tables_scale_past_the_old_ceiling(void)
{
    AstraBootInfo info;
    KernelMemoryStats stats;
    const uint32_t gigabyte = 0x40000000u;
    uint32_t transfer = 0u;
    uint32_t frame;

    make_valid_info(&info);
    info.ram_size = gigabyte;
    add_range(&info, 0x04000000u,
              (info.ram_base + gigabyte) - 0x04000000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);

    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.total_frames == gigabyte / KERNEL_PAGE_SIZE);
    /* Four times what the old links could address, eight times the old cap. */
    assert(stats.total_frames > (uint32_t)UINT16_MAX);
    /* The bookkeeping is proportional and stated rather than guessed at. */
    assert(kernel_memory_metadata_bytes(stats.total_frames) >
           stats.total_frames * 16u);

    /* The zone is at the top, which is a frame index the old build lacked. */
    assert(stats.dma_zone_frames == KERNEL_DMA_ZONE_FRAMES);
    assert(stats.dma_zone_first > (uint32_t)UINT16_MAX);
    assert(kernel_memory_alloc(2u, 1u, KERNEL_FRAME_DMA, 11u, &transfer) ==
           KERNEL_MEMORY_OK);
    frame = (transfer - stats.ram_base) / KERNEL_PAGE_SIZE;
    assert(frame > (uint32_t)UINT16_MAX);
    /* Owning and releasing at that index is what exercises the wide links. */
    assert(kernel_memory_release_owner(11u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(kernel_allocation_valid());

    /* And back to a small board in the same run: nothing is sticky. */
    make_valid_info(&info);
    astra_boot_info_finalize(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&stats));
    assert(stats.total_frames == 0x02000000u / KERNEL_PAGE_SIZE);
}

static void test_cow_frame_changes_owner_without_changing_charge(void)
{
    AstraBootInfo info;
    KernelFrameInfo frame;
    uint32_t physical;
    uint32_t old_frames;
    uint32_t new_frames;

    make_valid_info(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 81u,
                               &physical) == KERNEL_MEMORY_OK);
    assert(kernel_memory_owner_frames(81u, &old_frames) && old_frames == 1u);
    assert(kernel_memory_reclassify(physical, 81u, KERNEL_FRAME_PROCESS,
                                    KERNEL_FRAME_COW_WRITE) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_transfer_owner(physical, 81u, 82u) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_owner_frames(81u, &old_frames) && old_frames == 0u);
    assert(kernel_memory_owner_frames(82u, &new_frames) && new_frames == 1u);
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.owner == 82u && frame.state == KERNEL_FRAME_COW_WRITE);
    assert(kernel_memory_reclassify(physical, 82u, KERNEL_FRAME_COW_WRITE,
                                    KERNEL_FRAME_PROCESS) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(82u, NULL) == KERNEL_MEMORY_OK);
}

int main(void)
{
    test_initial_map();
    test_arty_guest_map();
    test_rejects_unclassified_and_unaligned_ram();
    test_allocation_references_and_pins();
    test_owner_teardown_is_atomic_while_dma_is_pinned();
    test_owner_frame_count_tracks_unique_frames();
    test_owner_release_work_scales_with_owned_frames();
    test_owner_ledger_capacity_is_bounded_and_reusable();
    test_reinit_discards_stale_dynamic_metadata();
    test_scattered_page_allocation_is_atomic();
    test_exhaustion_and_checked_ranges();
    test_emergency_reserve_isolated_and_replenished();
    test_protected_reserve_survives_ordinary_exhaustion();
    test_emergency_site_injection_is_atomic();
    test_retained_log_is_allocation_free_under_pressure();
    test_contiguous_demand_survives_a_combed_frame_map();
    test_frame_tables_scale_past_the_old_ceiling();
    test_cow_frame_changes_owner_without_changing_charge();
    test_tagged_failure_injection_and_boot_retirement();
    puts("KERNEL MEMORY PASS");
    return 0;
}
