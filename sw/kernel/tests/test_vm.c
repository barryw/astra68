#include "allocation.h"
#include "memory.h"
#include "ohci.h"
#include "pmmu.h"
#include "thread.h"
#include "vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t physical_memory[32u * 1024u * 1024u];
static KernelPmmuRootPointer loaded_srp;
static KernelPmmuRootPointer loaded_crp;
static uint32_t loaded_tc;
static uint32_t loaded_tt0;
static uint32_t loaded_tt1;
static uint32_t flush_count;
static uint32_t cache_invalidation_count;
static uint32_t function_code_sets;

void kernel_pmmu_load_tc(const uint32_t *value)
{
    loaded_tc = *value;
}

void kernel_pmmu_load_srp(const KernelPmmuRootPointer *root)
{
    loaded_srp.limit_descriptor = root->limit_descriptor;
    loaded_srp.table_address = root->table_address;
}

void kernel_pmmu_load_crp(const KernelPmmuRootPointer *root)
{
    loaded_crp.limit_descriptor = root->limit_descriptor;
    loaded_crp.table_address = root->table_address;
}

void kernel_pmmu_load_tt0(const uint32_t *value)
{
    loaded_tt0 = *value;
}

void kernel_pmmu_load_tt1(const uint32_t *value)
{
    loaded_tt1 = *value;
}

void kernel_pmmu_read_tc(uint32_t *value)
{
    *value = loaded_tc;
}

void kernel_pmmu_read_srp(KernelPmmuRootPointer *root)
{
    *root = loaded_srp;
}

void kernel_pmmu_read_crp(KernelPmmuRootPointer *root)
{
    *root = loaded_crp;
}

void kernel_pmmu_flush_all(void)
{
    ++flush_count;
}

void kernel_pmmu_set_user_function_codes(void)
{
    ++function_code_sets;
}

void kernel_cache_invalidate_all(void)
{
    ++cache_invalidation_count;
}

uint32_t kernel_cache_read_control(void)
{
    return 0x00003119u;
}

static uint32_t *physical_words(uint32_t physical)
{
    assert(physical >= 0x02000000u && physical <= 0x03fff000u);
    return (uint32_t *)(void *)&physical_memory[physical - 0x02000000u];
}

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

static void initialize_test(void)
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
    add_range(&info, ASTRA_USER_IMAGE_ADDRESS, ASTRA_USER_IMAGE_MAX_SIZE,
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
    add_range(&info, 0x03e40000u, 0x000c0000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, OHCI_DMA_POOL_BASE, OHCI_DMA_POOL_SIZE,
              ASTRA_MEMORY_RANGE_DEVICE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    astra_boot_info_finalize(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    memset(physical_memory, 0xa5, sizeof(physical_memory));
    kernel_vm_test_bind_physical_memory(physical_memory, 0x02000000u,
                                        sizeof(physical_memory));
    memset(&loaded_srp, 0, sizeof(loaded_srp));
    memset(&loaded_crp, 0, sizeof(loaded_crp));
    loaded_tc = 0xffffffffu;
    loaded_tt0 = 0xffffffffu;
    loaded_tt1 = 0xffffffffu;
    flush_count = 0u;
    cache_invalidation_count = 0u;
    function_code_sets = 0u;
    assert(kernel_vm_init() == KERNEL_VM_OK);
}

static void test_kernel_root_and_enable_sequence(void)
{
    KernelVmControlState control;
    KernelVmStats stats;
    uint32_t *root;
    uint32_t *low;
    uint32_t *top;
    uint32_t *high;
    uint32_t low_physical;
    uint32_t top_physical;
    uint32_t high_physical;
    uint32_t translated;

    initialize_test();
    assert(kernel_vm_stats(&stats));
    root = physical_words(stats.kernel_root_physical);
    assert(root[0] == 0u);
    assert((root[8] & 3u) == 2u);
    low_physical = root[8] & 0xfffffff0u;
    low = physical_words(low_physical);
    assert(low[0] == 0x02000001u);
    assert(low[(0x02080000u - 0x02000000u) >> 12] == 0u);
    assert(low[(0x02083000u - 0x02000000u) >> 12] == 0u);
    for (uint32_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        uint32_t guard = KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE +
                         slot * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE;
        uint32_t index = (guard - 0x02000000u) >> 12;

        assert(low[index] == 0u);
        assert(low[index + 1u] ==
               ((guard + KERNEL_THREAD_SUPERVISOR_GUARD_SIZE) | 1u));
        assert(low[index + 2u] ==
               ((guard + KERNEL_THREAD_SUPERVISOR_GUARD_SIZE +
                  KERNEL_PAGE_SIZE) | 1u));
    }
    assert(low[0x3ffu] == 0x023ff001u);
    for (uint32_t index = 9u; index < 15u; ++index)
        assert(root[index] == ((index << 22) | 1u));
    assert((root[15] & 3u) == 2u);
    top_physical = root[15] & 0xfffffff0u;
    top = physical_words(top_physical);
    assert(top[0] == 0x03c00001u);
    assert(top[(ASTRA_ROM_BACKING_ADDRESS - 0x03c00000u) >> 12] ==
           (ASTRA_ROM_BACKING_ADDRESS | 1u));
    assert(top[(OHCI_DMA_POOL_BASE - 0x03c00000u) >> 12] ==
           (OHCI_DMA_POOL_BASE | 0x41u));
    assert(top[0x3ffu] == 0x03fff041u);
    assert(stats.kernel_stack_guard == 0x02080000u);
    assert(stats.kernel_worker_stack_guard == 0x02083000u);
    assert(stats.kernel_thread_stack_arena ==
           KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE);
    assert(stats.kernel_thread_stack_arena_end ==
           KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE +
               KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE);
    assert(stats.kernel_thread_stack_guards == KERNEL_THREAD_MAX);
    assert(stats.supervisor_table_pages == 4u);
    assert((root[1023] & 3u) == 2u);
    high_physical = root[1023] & 0xfffffff0u;
    high = physical_words(high_physical);
    assert(high[(0xfff00000u >> 12) & 0x3ffu] == 0xfff00041u);
    assert(high[(0xfff20000u >> 12) & 0x3ffu] == 0xfff20041u);
    assert(high[(0xfff40000u >> 12) & 0x3ffu] == 0xfff40041u);
    assert(high[(0xfff30000u >> 12) & 0x3ffu] == 0u);
    assert(kernel_vm_probe_current(0x02010004u, true, &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == 0x02010004u);
    assert(kernel_vm_probe_current(0x02080000u, true, &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_current(0x02401234u, true, &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == 0x02401234u);
    assert(kernel_vm_probe_current(OHCI_DMA_POOL_BASE + 0x84u, true,
                                   &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == OHCI_DMA_POOL_BASE + 0x84u);
    assert(kernel_vm_probe_current(0xfff40020u, true, &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == 0xfff40020u);
    assert(kernel_vm_probe_current(0xfff30000u, true, &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_current(0x10000000u, false, &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_current(0x10000000u, false, NULL) ==
           KERNEL_VM_MAPPING_UNKNOWN);

    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_enabled());
    assert(loaded_srp.limit_descriptor == KERNEL_PMMU_ROOT_LIMIT_SHORT);
    assert(loaded_srp.table_address == stats.kernel_root_physical);
    assert(loaded_crp.limit_descriptor == KERNEL_PMMU_ROOT_LIMIT_SHORT);
    assert(loaded_crp.table_address == stats.empty_root_physical);
    assert(loaded_tt0 == 0u && loaded_tt1 == 0u);
    assert(loaded_tc == KERNEL_PMMU_TC_4K_10_10_SRE);
    assert(function_code_sets == 1u && flush_count == 1u);
    assert(cache_invalidation_count == 1u);
    assert(kernel_vm_control_state(&control));
    assert(control.srp_limit_descriptor == loaded_srp.limit_descriptor);
    assert(control.srp_table_address == loaded_srp.table_address);
    assert(control.crp_limit_descriptor == loaded_crp.limit_descriptor);
    assert(control.crp_table_address == loaded_crp.table_address);
    assert(control.translation_control == loaded_tc);
    assert(control.cache_control == 0x00003119u);
    assert(control.translation_enabled == 1u);
    assert(control.reserved[0] == 0u && control.reserved[1] == 0u &&
           control.reserved[2] == 0u);
}

static void test_page_table_injection_preserves_baseline(void)
{
    KernelAddressSpace space = {0};
    KernelAllocationStats allocation_before;
    KernelAllocationStats allocation_after;
    KernelMemoryStats memory_before;
    KernelMemoryStats memory_after;
    KernelVmStats vm_before;
    KernelVmStats vm_after;

    initialize_test();
    assert(kernel_memory_stats(&memory_before));
    assert(kernel_vm_stats(&vm_before));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, &allocation_before));
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, 1u);
    assert(kernel_vm_create_address_space(91u, &space) ==
           KERNEL_VM_OUT_OF_MEMORY);
    assert(space.initialized == 0u && space.root_physical == 0u);
    assert(kernel_memory_stats(&memory_after));
    assert(kernel_vm_stats(&vm_after));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, &allocation_after));
    assert(memory_after.free_frames == memory_before.free_frames);
    assert(memory_after.owner_slots_used == memory_before.owner_slots_used);
    assert(vm_after.address_spaces == vm_before.address_spaces);
    assert(vm_after.user_table_pages == vm_before.user_table_pages);
    assert(allocation_after.current_units == allocation_before.current_units);
    assert(allocation_after.current_bytes == allocation_before.current_bytes);
    assert(allocation_after.injected_failures ==
           allocation_before.injected_failures + 1u);
    assert(kernel_allocation_valid());

    kernel_allocation_test_fail_global(1u);
    assert(kernel_vm_create_address_space(91u, &space) ==
           KERNEL_VM_OUT_OF_MEMORY);
    assert(space.initialized == 0u && space.root_physical == 0u);
    assert(kernel_memory_stats(&memory_after));
    assert(memory_after.free_frames == memory_before.free_frames);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, &allocation_after));
    assert(allocation_after.current_units == allocation_before.current_units);
    assert(allocation_after.injected_failures ==
           allocation_before.injected_failures + 2u);

    assert(kernel_vm_create_address_space(91u, &space) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_stats(&memory_after));
    assert(memory_after.free_frames == memory_before.free_frames);
}

static void test_map_switch_unmap_and_stale_guards(void)
{
    KernelAddressSpace space = {0};
    KernelFrameInfo frame;
    KernelVmStats stats;
    uint32_t physical;
    uint32_t translated;
    uint32_t *root;
    uint32_t *table;
    uint32_t table_physical;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(42u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 42u,
                               &physical) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&space, 0x10000000u, physical,
                              KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    root = physical_words(space.root_physical);
    assert((root[0x40u] & 3u) == 2u);
    table_physical = root[0x40u] & 0xfffffff0u;
    table = physical_words(table_physical);
    assert(table[0] == (physical | 1u));
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.references == 2u);
    assert(kernel_vm_map_page(&space, 0x10000000u, physical,
                              KERNEL_VM_READ) == KERNEL_VM_ALREADY_MAPPED);
    assert(kernel_vm_map_page(&space, 0x10001000u, physical,
                              KERNEL_VM_READ) == KERNEL_VM_CACHE_ALIAS);
    assert(kernel_vm_map_page(&space, 0u, physical, KERNEL_VM_READ) ==
           KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_map_page(&space, 0x10001000u, physical,
                              KERNEL_VM_READ | KERNEL_VM_WRITE |
                                  KERNEL_VM_EXEC) ==
           KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_map_page(&space, 0x10001000u,
                              ASTRA_KERNEL_LOAD_ADDRESS,
                              KERNEL_VM_READ) == KERNEL_VM_NOT_OWNED);

    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(loaded_crp.table_address == space.root_physical);
    assert(cache_invalidation_count == 3u);
    assert(kernel_vm_probe_current(0x10000000u, false, &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == physical);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_BUSY);
    assert(kernel_vm_unmap_page(&space, 0x10000000u) == KERNEL_VM_OK);
    assert(cache_invalidation_count == 4u);
    assert(root[0x40u] == 0u);
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.references == 1u);
    assert(kernel_vm_unmap_page(&space, 0x10000000u) ==
           KERNEL_VM_NOT_MAPPED);
    assert(kernel_vm_map_page(&space, 0x10001000u, physical,
                              KERNEL_VM_READ) == KERNEL_VM_OK);
    assert(kernel_vm_probe_current(0x10001004u, false, &translated) ==
           KERNEL_VM_MAPPING_READ_ONLY);
    assert(translated == physical + 4u);
    assert(kernel_vm_unmap_page(&space, 0x10001000u) == KERNEL_VM_OK);
    assert(kernel_vm_probe_current(0x10001000u, false, &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(cache_invalidation_count == 6u);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(cache_invalidation_count == 7u);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(42u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_vm_stats(&stats));
    assert(stats.address_spaces == 0u && stats.user_mappings == 0u);
    assert(stats.user_table_pages == 0u);
}

static void test_destroy_releases_read_only_mapping(void)
{
    KernelAddressSpace space = {0};
    KernelFrameInfo frame;
    uint32_t physical;
    uint32_t *root;
    uint32_t *table;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(77u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 77u,
                               &physical) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&space, 0x20000000u, physical,
                              KERNEL_VM_READ | KERNEL_VM_EXEC) ==
           KERNEL_VM_OK);
    root = physical_words(space.root_physical);
    table = physical_words(root[0x80u] & 0xfffffff0u);
    assert(table[0] == (physical | 5u));
    assert(cache_invalidation_count == 2u);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(cache_invalidation_count == 3u);
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.references == 1u);
    assert(kernel_memory_release_owner(77u, NULL) == KERNEL_MEMORY_OK);
}

static void assert_shared_map_baseline(
    const KernelAddressSpace *space, const KernelAddressSpace *baseline_space,
    const uint32_t *physical_pages, uint32_t page_count,
    const KernelMemoryStats *baseline_memory,
    const KernelVmStats *baseline_vm, uint32_t root_descriptor)
{
    KernelMemoryStats memory;
    KernelVmStats vm;
    uint32_t *root = physical_words(space->root_physical);

    assert(memcmp(space, baseline_space, sizeof(*space)) == 0);
    assert(root[0x100u] == root_descriptor);
    assert(kernel_memory_stats(&memory));
    assert(memory.free_frames == baseline_memory->free_frames);
    assert(memory.owner_slots_used == baseline_memory->owner_slots_used);
    assert(kernel_vm_stats(&vm));
    assert(vm.address_spaces == baseline_vm->address_spaces);
    assert(vm.user_mappings == baseline_vm->user_mappings);
    assert(vm.user_table_pages == baseline_vm->user_table_pages);
    for (uint32_t page = 0u; page < page_count; ++page) {
        KernelFrameInfo frame;

        assert(kernel_memory_frame_info(physical_pages[page], &frame));
        assert(frame.state == KERNEL_FRAME_SHARED);
        assert(frame.references == 1u);
    }
}

static void test_shared_map_transaction_rolls_back_every_stage(void)
{
    static const KernelVmSharedMapFault faults[] = {
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_MAPPING_METADATA,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_ROOT_PUBLISH
    };
    const uint32_t frame_owner = 0x40000011u;

    for (uint32_t index = 0u;
         index < sizeof(faults) / sizeof(faults[0]); ++index) {
        KernelAddressSpace space = {0};
        KernelAddressSpace baseline_space;
        KernelMemoryStats initial_memory;
        KernelMemoryStats baseline_memory;
        KernelMemoryStats final_memory;
        KernelVmStats baseline_vm;
        uint32_t physical_pages[2];
        uint32_t root_descriptor;

        initialize_test();
        assert(kernel_memory_stats(&initial_memory));
        assert(kernel_vm_create_address_space(42u, &space) == KERNEL_VM_OK);
        assert(kernel_memory_alloc_pages_zeroed(
                   2u, KERNEL_FRAME_SHARED, frame_owner, physical_pages) ==
               KERNEL_MEMORY_OK);
        baseline_space = space;
        assert(kernel_memory_stats(&baseline_memory));
        assert(kernel_vm_stats(&baseline_vm));
        root_descriptor = physical_words(space.root_physical)[0x100u];
        assert(root_descriptor == 0u);

        kernel_vm_test_fail_next_shared_map(faults[index]);
        assert(kernel_vm_map_shared_range(
                   &space, KERNEL_VM_AREA_BASE, physical_pages, 2u,
                   frame_owner, KERNEL_VM_READ | KERNEL_VM_WRITE) ==
               KERNEL_VM_OUT_OF_MEMORY);
        assert_shared_map_baseline(
            &space, &baseline_space, physical_pages, 2u, &baseline_memory,
            &baseline_vm, root_descriptor);

        assert(kernel_vm_map_shared_range(
                   &space, KERNEL_VM_AREA_BASE, physical_pages, 2u,
                   frame_owner, KERNEL_VM_READ | KERNEL_VM_WRITE) ==
               KERNEL_VM_OK);
        assert(kernel_vm_unmap_shared_range(
                   &space, KERNEL_VM_AREA_BASE, physical_pages, 2u,
                   frame_owner) == KERNEL_VM_OK);
        assert(kernel_memory_release(physical_pages[0], 1u, frame_owner) ==
               KERNEL_MEMORY_OK);
        assert(kernel_memory_release(physical_pages[1], 1u, frame_owner) ==
               KERNEL_MEMORY_OK);
        assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
        assert(kernel_memory_stats(&final_memory));
        assert(final_memory.free_frames == initial_memory.free_frames);
    }
}

static void test_shared_map_existing_leaf_rollback_and_alias_guards(void)
{
    const uint32_t frame_owner = 0x40000012u;
    KernelAddressSpace first = {0};
    KernelAddressSpace second = {0};
    KernelAddressSpace baseline_space;
    KernelMemoryStats initial_memory;
    KernelMemoryStats baseline_memory;
    KernelMemoryStats final_memory;
    KernelVmStats baseline_vm;
    KernelFrameInfo frame;
    uint32_t physical_pages[2];
    uint32_t private_page;
    uint32_t root_descriptor;
    uint32_t *table;

    initialize_test();
    assert(kernel_memory_stats(&initial_memory));
    assert(kernel_vm_create_address_space(51u, &first) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(52u, &second) == KERNEL_VM_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 51u,
                               &private_page) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&first, 0x40080000u, private_page,
                              KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    assert(kernel_memory_alloc_pages_zeroed(
               2u, KERNEL_FRAME_SHARED, frame_owner, physical_pages) ==
           KERNEL_MEMORY_OK);
    root_descriptor = physical_words(first.root_physical)[0x100u];
    assert((root_descriptor & 3u) == 2u);
    table = physical_words(root_descriptor & 0xfffffff0u);
    assert(table[0] == 0u && table[1] == 0u);
    baseline_space = first;
    assert(kernel_memory_stats(&baseline_memory));
    assert(kernel_vm_stats(&baseline_vm));

    kernel_vm_test_fail_next_shared_map(
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH);
    assert(kernel_vm_map_shared_range(
               &first, KERNEL_VM_AREA_BASE, physical_pages, 2u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OUT_OF_MEMORY);
    assert_shared_map_baseline(
        &first, &baseline_space, physical_pages, 2u, &baseline_memory,
        &baseline_vm, root_descriptor);
    assert(table[0] == 0u && table[1] == 0u);

    assert(kernel_vm_map_shared_range(
               &first, KERNEL_VM_AREA_BASE, physical_pages, 2u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_WRITE) == KERNEL_VM_OK);
    assert(kernel_vm_map_shared_range(
               &second, KERNEL_VM_AREA_BASE + KERNEL_VM_AREA_SLOT_SIZE,
               physical_pages, 2u, frame_owner, KERNEL_VM_READ) ==
           KERNEL_VM_CACHE_ALIAS);
    assert(kernel_vm_map_shared_range(
               &second, KERNEL_VM_AREA_BASE, physical_pages, 2u,
               frame_owner, KERNEL_VM_READ) == KERNEL_VM_OK);
    assert(kernel_vm_map_shared_range(
               &second, KERNEL_VM_AREA_BASE, physical_pages, 2u,
               frame_owner, KERNEL_VM_READ) == KERNEL_VM_ALREADY_MAPPED);
    assert(kernel_vm_unmap_shared_range(
               &second, KERNEL_VM_AREA_BASE, physical_pages, 2u,
               frame_owner) == KERNEL_VM_OK);
    assert(kernel_vm_unmap_shared_range(
               &first, KERNEL_VM_AREA_BASE, physical_pages, 2u,
               frame_owner) == KERNEL_VM_OK);
    assert(kernel_memory_frame_info(physical_pages[0], &frame));
    assert(frame.references == 1u);
    assert(kernel_memory_release(physical_pages[0], 1u, frame_owner) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_release(physical_pages[1], 1u, frame_owner) ==
           KERNEL_MEMORY_OK);
    assert(kernel_vm_unmap_page(&first, 0x40080000u) == KERNEL_VM_OK);
    assert(kernel_memory_release(private_page, 1u, 51u) == KERNEL_MEMORY_OK);
    assert(kernel_vm_destroy_address_space(&first) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&second) == KERNEL_VM_OK);
    assert(kernel_memory_stats(&final_memory));
    assert(final_memory.free_frames == initial_memory.free_frames);
}

int main(void)
{
    test_kernel_root_and_enable_sequence();
    test_page_table_injection_preserves_baseline();
    test_map_switch_unmap_and_stale_guards();
    test_destroy_releases_read_only_mapping();
    test_shared_map_transaction_rolls_back_every_stage();
    test_shared_map_existing_leaf_rollback_and_alias_guards();
    puts("KERNEL VM PASS");
    return 0;
}
