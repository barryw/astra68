#include "allocation.h"
#include "area.h"
#include "ohci.h"
#include "memory.h"
#include "pmmu.h"
#include "vm.h"

#include <astra/boot.h>
#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RAM_BASE 0x02000000u

static uint8_t physical_memory[32u * 1024u * 1024u];

void kernel_pmmu_load_tc(const uint32_t *value) { (void)value; }
void kernel_pmmu_load_srp(const KernelPmmuRootPointer *root) { (void)root; }
void kernel_pmmu_load_crp(const KernelPmmuRootPointer *root) { (void)root; }
void kernel_pmmu_load_tt0(const uint32_t *value) { (void)value; }
void kernel_pmmu_load_tt1(const uint32_t *value) { (void)value; }
void kernel_pmmu_read_tc(uint32_t *value) { *value = 0u; }
void kernel_pmmu_read_srp(KernelPmmuRootPointer *root) { *root = (KernelPmmuRootPointer){0}; }
void kernel_pmmu_read_crp(KernelPmmuRootPointer *root) { *root = (KernelPmmuRootPointer){0}; }
void kernel_pmmu_flush_all(void) { }
void kernel_pmmu_set_user_function_codes(void) { }
void kernel_cache_invalidate_all(void) { }
uint32_t kernel_cache_read_control(void) { return 0u; }

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
    info.ram_base = RAM_BASE;
    info.ram_size = sizeof(physical_memory);
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
    kernel_memory_test_bind_physical_memory(physical_memory, RAM_BASE,
                                            sizeof(physical_memory));
    kernel_vm_test_bind_physical_memory(physical_memory, RAM_BASE,
                                        sizeof(physical_memory));
    assert(kernel_vm_init() == KERNEL_VM_OK);
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    kernel_area_pool_init();
    kernel_area_test_bind_physical_memory(physical_memory, RAM_BASE,
                                          sizeof(physical_memory));
}

static void test_same_address_aliases_and_atomic_revoke(void)
{
    KernelAddressSpace spaces[5] = {{0}};
    KernelAreaPoolStats stats;
    KernelAreaSnapshot snapshot;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    KernelArea *area;
    uint32_t bases[5];
    uint32_t sizes[5];
    uint32_t physical[4];
    uint8_t bytes[2u * KERNEL_PAGE_SIZE];

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    for (uint32_t index = 0u; index < 5u; ++index)
        assert(kernel_vm_create_address_space(100u + index, &spaces[index]) ==
               KERNEL_VM_OK);
    assert(kernel_area_create(7u, sizeof(bytes), &area) == KERNEL_AREA_OK);
    memset(bytes, 0, sizeof(bytes));
    assert(kernel_area_read(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    for (uint32_t index = 0u; index < sizeof(bytes); ++index)
        assert(bytes[index] == 0u);

    for (uint32_t index = 0u; index < 4u; ++index) {
        assert(kernel_area_map(area, 100u + index, &spaces[index],
                               KERNEL_VM_READ | KERNEL_VM_WRITE,
                               &bases[index], &sizes[index]) == KERNEL_AREA_OK);
        assert(bases[index] == KERNEL_VM_AREA_BASE);
        assert(sizes[index] == sizeof(bytes));
        assert(kernel_vm_switch(&spaces[index]) == KERNEL_VM_OK);
        assert(kernel_vm_test_translate_current(bases[index], true,
                                                &physical[index]));
        if (index != 0u)
            assert(physical[index] == physical[0]);
    }
    assert(kernel_area_map(area, 100u, &spaces[0],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[4], &sizes[4]) == KERNEL_AREA_OK);
    assert(bases[4] == bases[0] && sizes[4] == sizes[0]);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &bases[4], &sizes[4]) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_map(area, 104u, &spaces[4],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[4], &sizes[4]) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_unmap(101u, &spaces[1], bases[1]) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 104u, &spaces[4],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[4], &sizes[4]) == KERNEL_AREA_OK);

    bytes[0] = 0x5au;
    bytes[sizeof(bytes) - 1u] = 0xc3u;
    assert(kernel_area_write(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(physical_memory[physical[0] - RAM_BASE] == 0x5au);

    assert(kernel_area_process_died(7u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.terminal_result == ASTRA_SYSCALL_PEER_DEAD);
    assert(snapshot.mapping_references == 0u);
    assert(snapshot.frames_released == 1u);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &bases[0], &sizes[0]) == KERNEL_AREA_PEER_DEAD);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 1u && stats.closing_areas == 1u);
    assert(stats.active_mappings == 0u && stats.committed_pages == 0u);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.closing_areas == 0u);

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    for (uint32_t index = 0u; index < 5u; ++index) {
        assert(kernel_vm_destroy_address_space(&spaces[index]) == KERNEL_VM_OK);
        assert(kernel_memory_release_owner(100u + index, NULL) ==
               KERNEL_MEMORY_OK);
    }
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(kernel_area_pool_valid());
}

static void test_allocation_injection_preserves_mapping_baseline(void)
{
    KernelAddressSpace space = {0};
    KernelAllocationStats area_allocation;
    KernelAllocationStats mapping_allocation;
    KernelAllocationStats page_allocation;
    KernelArea *area = (KernelArea *)(uintptr_t)1u;
    KernelAreaPoolStats pool_stats;
    KernelAreaSnapshot snapshot;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    uint32_t virtual_base = UINT32_MAX;
    uint32_t byte_size = UINT32_MAX;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_PAGES, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(2u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);

    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_vm_create_address_space(32u, &space) == KERNEL_VM_OK);
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING, 1u);
    assert(kernel_area_map(area, 32u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &virtual_base, &byte_size) ==
           KERNEL_AREA_NO_SLOT);
    assert(virtual_base == 0u && byte_size == 0u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.mapping_references == 0u);
    assert(kernel_area_pool_stats(&pool_stats));
    assert(pool_stats.active_mappings == 0u);

    virtual_base = UINT32_MAX;
    byte_size = UINT32_MAX;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_area_map(area, 32u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &virtual_base, &byte_size) ==
           KERNEL_AREA_NO_SLOT);
    assert(virtual_base == 0u && byte_size == 0u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.mapping_references == 0u);

    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(32u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT, &area_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_PAGES, &page_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING, &mapping_allocation));
    assert(area_allocation.current_units == 0u);
    assert(page_allocation.current_units == 0u);
    assert(mapping_allocation.current_units == 0u);
    assert(area_allocation.injected_failures == 2u);
    assert(page_allocation.injected_failures == 2u);
    assert(mapping_allocation.injected_failures == 2u);
    assert(kernel_area_pool_valid());
    assert(kernel_allocation_valid());
}

static void test_child_lifetime_and_quotas(void)
{
    KernelAreaPoolStats stats;
    KernelArea *areas[KERNEL_AREA_OWNER_MAX];
    KernelArea *extra = NULL;

    initialize_test();
    for (uint32_t index = 0u; index < KERNEL_AREA_OWNER_MAX; ++index)
        assert(kernel_area_create(9u, KERNEL_PAGE_SIZE, &areas[index]) ==
               KERNEL_AREA_OK);
    assert(kernel_area_create(9u, KERNEL_PAGE_SIZE, &extra) ==
           KERNEL_AREA_QUOTA_EXCEEDED);
    assert(extra == NULL);
    assert(kernel_area_child_retain(areas[0]) == KERNEL_AREA_OK);
    kernel_area_handle_release(areas[0], NULL);
    assert(kernel_area_live(areas[0]));
    assert(kernel_area_child_release(areas[0]) == KERNEL_AREA_OK);
    for (uint32_t index = 1u; index < KERNEL_AREA_OWNER_MAX; ++index)
        kernel_area_handle_release(areas[index], NULL);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.committed_pages == 0u);
    assert(stats.quota_failures == 1u);
    assert(kernel_area_pool_valid());
}

static void test_create_transaction_rolls_back_every_stage(void)
{
    static const KernelAreaTestFault faults[] = {
        KERNEL_AREA_TEST_FAULT_CREATE_AFTER_RESERVE,
        KERNEL_AREA_TEST_FAULT_CREATE_AFTER_FRAME_ALLOCATE
    };

    for (uint32_t index = 0u;
         index < sizeof(faults) / sizeof(faults[0]); ++index) {
        KernelAreaPoolStats baseline_pool;
        KernelAreaPoolStats after_pool;
        KernelMemoryStats baseline_memory;
        KernelMemoryStats after_memory;
        KernelArea *area = (KernelArea *)(uintptr_t)1u;

        initialize_test();
        assert(kernel_area_pool_stats(&baseline_pool));
        assert(kernel_memory_stats(&baseline_memory));
        kernel_area_test_fail_next(faults[index]);
        assert(kernel_area_create(71u, 2u * KERNEL_PAGE_SIZE, &area) ==
               KERNEL_AREA_OUT_OF_MEMORY);
        assert(area == NULL);
        assert(kernel_area_pool_stats(&after_pool));
        assert(after_pool.created_areas == baseline_pool.created_areas);
        assert(after_pool.active_areas == baseline_pool.active_areas);
        assert(after_pool.committed_pages == baseline_pool.committed_pages);
        assert(after_pool.active_mappings == baseline_pool.active_mappings);
        assert(after_pool.allocation_failures ==
               baseline_pool.allocation_failures + 1u);
        assert(kernel_memory_stats(&after_memory));
        assert(after_memory.free_frames == baseline_memory.free_frames);
        assert(after_memory.owner_slots_used ==
               baseline_memory.owner_slots_used);
        assert(kernel_area_pool_valid());

        assert(kernel_area_create(71u, 2u * KERNEL_PAGE_SIZE, &area) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_memory_stats(&after_memory));
        assert(after_memory.free_frames == baseline_memory.free_frames);
        assert(kernel_area_pool_valid());
    }
}

static void assert_failed_map_baseline(
    const KernelAddressSpace *space, const KernelAddressSpace *baseline_space,
    const KernelMemoryStats *baseline_memory,
    const KernelVmStats *baseline_vm,
    const KernelAreaPoolStats *baseline_pool)
{
    KernelAreaPoolStats area_stats;
    KernelMemoryStats memory;
    KernelVmStats vm;

    assert(memcmp(space, baseline_space, sizeof(*space)) == 0);
    assert(kernel_memory_stats(&memory));
    assert(memory.free_frames == baseline_memory->free_frames);
    assert(memory.owner_slots_used == baseline_memory->owner_slots_used);
    assert(kernel_vm_stats(&vm));
    assert(vm.address_spaces == baseline_vm->address_spaces);
    assert(vm.user_mappings == baseline_vm->user_mappings);
    assert(vm.user_table_pages == baseline_vm->user_table_pages);
    assert(kernel_area_pool_stats(&area_stats));
    assert(area_stats.active_areas == baseline_pool->active_areas);
    assert(area_stats.committed_pages == baseline_pool->committed_pages);
    assert(area_stats.active_mappings == baseline_pool->active_mappings);
    assert(area_stats.map_operations == baseline_pool->map_operations);
    assert(area_stats.unmap_operations == baseline_pool->unmap_operations);
    assert(area_stats.map_rollbacks == baseline_pool->map_rollbacks + 1u);
}

static void test_map_transaction_rolls_back_every_stage(void)
{
    static const KernelVmSharedMapFault vm_faults[] = {
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_MAPPING_METADATA,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_ROOT_PUBLISH
    };
    const uint32_t fault_count =
        (uint32_t)(sizeof(vm_faults) / sizeof(vm_faults[0])) + 1u;

    for (uint32_t index = 0u; index < fault_count; ++index) {
        KernelAddressSpace space = {0};
        KernelAddressSpace baseline_space;
        KernelAreaPoolStats baseline_pool;
        KernelAreaSnapshot snapshot;
        KernelMemoryStats initial_memory;
        KernelMemoryStats baseline_memory;
        KernelMemoryStats final_memory;
        KernelVmStats baseline_vm;
        KernelArea *area;
        uint32_t virtual_base = UINT32_MAX;
        uint32_t byte_size = UINT32_MAX;

        initialize_test();
        assert(kernel_memory_stats(&initial_memory));
        assert(kernel_vm_create_address_space(81u, &space) == KERNEL_VM_OK);
        assert(kernel_area_create(72u, 2u * KERNEL_PAGE_SIZE, &area) ==
               KERNEL_AREA_OK);
        baseline_space = space;
        assert(kernel_memory_stats(&baseline_memory));
        assert(kernel_vm_stats(&baseline_vm));
        assert(kernel_area_pool_stats(&baseline_pool));

        if (index < fault_count - 1u)
            kernel_vm_test_fail_next_shared_map(vm_faults[index]);
        else
            kernel_area_test_fail_next(
                KERNEL_AREA_TEST_FAULT_MAP_AFTER_VM_PUBLISH);
        assert(kernel_area_map(
                   area, 81u, &space,
                   KERNEL_VM_READ | KERNEL_VM_WRITE,
                   &virtual_base, &byte_size) == KERNEL_AREA_OUT_OF_MEMORY);
        assert(virtual_base == 0u && byte_size == 0u);
        assert_failed_map_baseline(
            &space, &baseline_space, &baseline_memory, &baseline_vm,
            &baseline_pool);
        assert(kernel_area_snapshot(0u, &snapshot));
        assert(snapshot.mapping_references == 0u);
        assert(kernel_area_pool_valid());

        assert(kernel_area_map(
                   area, 81u, &space,
                   KERNEL_VM_READ | KERNEL_VM_WRITE,
                   &virtual_base, &byte_size) == KERNEL_AREA_OK);
        assert(virtual_base == KERNEL_VM_AREA_BASE);
        assert(byte_size == 2u * KERNEL_PAGE_SIZE);
        assert(kernel_area_unmap(81u, &space, virtual_base) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
        assert(kernel_memory_release_owner(81u, NULL) == KERNEL_MEMORY_OK);
        assert(kernel_memory_stats(&final_memory));
        assert(final_memory.free_frames == initial_memory.free_frames);
        assert(kernel_area_pool_valid());
    }
}

int main(void)
{
    test_allocation_injection_preserves_mapping_baseline();
    test_same_address_aliases_and_atomic_revoke();
    test_child_lifetime_and_quotas();
    test_create_transaction_rolls_back_every_stage();
    test_map_transaction_rolls_back_every_stage();
    puts("area tests passed");
    return 0;
}
