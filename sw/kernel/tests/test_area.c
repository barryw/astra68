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
void kernel_pmmu_flush_page(uint32_t virtual_address) { (void)virtual_address; }
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

static void test_same_address_aliases_survive_creator_death(void)
{
    KernelAddressSpace spaces[KERNEL_VM_ADDRESS_SPACE_MAX + 1u] = {{0}};
    KernelAreaPoolStats stats;
    KernelAreaSnapshot snapshot;
    uint32_t rejected_base;
    uint32_t rejected_size;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    KernelArea *area;
    uint32_t bases[KERNEL_VM_ADDRESS_SPACE_MAX + 1u];
    uint32_t sizes[KERNEL_VM_ADDRESS_SPACE_MAX + 1u];
    uint32_t physical[KERNEL_VM_ADDRESS_SPACE_MAX];
    uint8_t bytes[2u * KERNEL_PAGE_SIZE];

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    for (uint32_t index = 0u;
         index < KERNEL_VM_ADDRESS_SPACE_MAX + 1u; ++index)
        assert(kernel_vm_create_address_space(100u + index, &spaces[index]) ==
               KERNEL_VM_OK);
    assert(kernel_area_create(7u, sizeof(bytes), 0u, &area) == KERNEL_AREA_OK);
    assert(kernel_area_handle_retain(area, NULL));
    memset(bytes, 0, sizeof(bytes));
    assert(kernel_area_read(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    for (uint32_t index = 0u; index < sizeof(bytes); ++index)
        assert(bytes[index] == 0u);

    for (uint32_t index = 0u; index < KERNEL_VM_ADDRESS_SPACE_MAX; ++index) {
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
                           &bases[KERNEL_VM_ADDRESS_SPACE_MAX],
                           &sizes[KERNEL_VM_ADDRESS_SPACE_MAX]) ==
           KERNEL_AREA_OK);
    assert(bases[KERNEL_VM_ADDRESS_SPACE_MAX] == bases[0] &&
           sizes[KERNEL_VM_ADDRESS_SPACE_MAX] == sizes[0]);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &bases[KERNEL_VM_ADDRESS_SPACE_MAX],
                           &sizes[KERNEL_VM_ADDRESS_SPACE_MAX]) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_map(area, 100u + KERNEL_VM_ADDRESS_SPACE_MAX,
                           &spaces[KERNEL_VM_ADDRESS_SPACE_MAX],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[KERNEL_VM_ADDRESS_SPACE_MAX],
                           &sizes[KERNEL_VM_ADDRESS_SPACE_MAX]) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_unmap(101u, &spaces[1], bases[1]) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 100u + KERNEL_VM_ADDRESS_SPACE_MAX,
                           &spaces[KERNEL_VM_ADDRESS_SPACE_MAX],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[KERNEL_VM_ADDRESS_SPACE_MAX],
                           &sizes[KERNEL_VM_ADDRESS_SPACE_MAX]) ==
           KERNEL_AREA_OK);

    bytes[0] = 0x5au;
    bytes[sizeof(bytes) - 1u] = 0xc3u;
    assert(kernel_area_write(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(physical_memory[physical[0] - RAM_BASE] == 0x5au);

    assert(kernel_area_process_died(7u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.creator == 7u);
    assert(snapshot.terminal_result == 0u);
    assert(snapshot.mapping_references == KERNEL_VM_ADDRESS_SPACE_MAX);
    assert(snapshot.frames_released == 0u);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &rejected_base, &rejected_size) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 1u && stats.closing_areas == 0u);
    assert(stats.active_mappings == KERNEL_VM_ADDRESS_SPACE_MAX);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_live(area));
    for (uint32_t index = 0u; index < KERNEL_VM_ADDRESS_SPACE_MAX; ++index)
        if (index != 1u)
            assert(kernel_area_unmap(100u + index, &spaces[index],
                                     bases[index]) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(100u + KERNEL_VM_ADDRESS_SPACE_MAX,
                             &spaces[KERNEL_VM_ADDRESS_SPACE_MAX],
                             bases[KERNEL_VM_ADDRESS_SPACE_MAX]) ==
           KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.closing_areas == 0u);

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    for (uint32_t index = 0u;
         index < KERNEL_VM_ADDRESS_SPACE_MAX + 1u; ++index) {
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
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_PAGES, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(2u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);

    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
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
        assert(kernel_area_create(9u, KERNEL_PAGE_SIZE, 0u, &areas[index]) ==
               KERNEL_AREA_OK);
    assert(kernel_area_create(9u, KERNEL_PAGE_SIZE, 0u, &extra) ==
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

static void test_service_can_map_every_area_slot(void)
{
    KernelAddressSpace service = {0};
    KernelArea *areas[KERNEL_AREA_MAX];
    uint32_t bases[KERNEL_AREA_MAX];
    uint32_t sizes[KERNEL_AREA_MAX];

    initialize_test();
    assert(kernel_vm_create_address_space(200u, &service) == KERNEL_VM_OK);
    for (uint32_t index = 0u; index < KERNEL_AREA_MAX; ++index) {
        uint32_t owner = 201u + index / KERNEL_AREA_OWNER_MAX;

        assert(kernel_area_create(owner, KERNEL_PAGE_SIZE, 0u, &areas[index]) ==
               KERNEL_AREA_OK);
        assert(kernel_area_map(areas[index], 200u, &service,
                               KERNEL_VM_READ | KERNEL_VM_WRITE,
                               &bases[index], &sizes[index]) ==
               KERNEL_AREA_OK);
        assert(bases[index] == KERNEL_VM_AREA_BASE +
               index * KERNEL_VM_AREA_SLOT_SIZE);
        assert(sizes[index] == KERNEL_PAGE_SIZE);
    }
    for (uint32_t index = 0u; index < KERNEL_AREA_MAX; ++index) {
        uint32_t owner = 201u + index / KERNEL_AREA_OWNER_MAX;

        assert(kernel_area_unmap(200u, &service, bases[index]) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(areas[index], NULL);
        if ((index + 1u) % KERNEL_AREA_OWNER_MAX == 0u)
            assert(kernel_memory_release_owner(owner, NULL) ==
                   KERNEL_MEMORY_OK);
    }
    assert(kernel_vm_destroy_address_space(&service) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(200u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_area_pool_valid());
}

static void test_screen_sized_area_reaches_its_last_pixel(void)
{
    KernelAddressSpace space = {0};
    KernelAreaPoolStats stats;
    KernelAreaSnapshot snapshot;
    KernelArea *area;
    uint8_t pixel[2] = {0x13u, 0x5du};
    uint8_t readback[2] = {0u, 0u};
    uint32_t virtual_base = 0u;
    uint32_t byte_size = 0u;
    const uint32_t screen_bytes = 1280u * 720u * 2u;

    initialize_test();
    assert(screen_bytes <= ASTRA_AREA_SIZE_MAX);
    assert(kernel_area_create(91u, screen_bytes, 0u, &area) == KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.page_count ==
           (screen_bytes + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE);
    assert(kernel_vm_create_address_space(92u, &space) == KERNEL_VM_OK);
    assert(kernel_area_map(area, 92u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &virtual_base, &byte_size) == KERNEL_AREA_OK);
    assert(byte_size == snapshot.page_count * KERNEL_PAGE_SIZE);
    assert(kernel_area_write(area, screen_bytes - sizeof(pixel), pixel,
                             sizeof(pixel)) == KERNEL_AREA_OK);
    assert(kernel_area_read(area, screen_bytes - sizeof(readback), readback,
                            sizeof(readback)) == KERNEL_AREA_OK);
    assert(memcmp(pixel, readback, sizeof(pixel)) == 0);
    assert(kernel_area_unmap(92u, &space, virtual_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(92u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.committed_pages == 0u);
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
        assert(kernel_area_create(71u, 2u * KERNEL_PAGE_SIZE, 0u, &area) ==
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

        assert(kernel_area_create(71u, 2u * KERNEL_PAGE_SIZE, 0u, &area) ==
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
        assert(kernel_area_create(72u, 2u * KERNEL_PAGE_SIZE, 0u, &area) ==
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

/*
 * Reserving is not committing.
 *
 * The whole point of the reserved form is that creating one costs no frames,
 * so that is what gets asserted first -- against the allocator's own free
 * count, because a page_count field agreeing with itself would prove nothing.
 */
static void test_reserved_area_commits_only_what_is_touched(void)
{
    KernelAddressSpace space = {0};
    KernelAreaSnapshot snapshot;
    KernelMemoryStats before;
    KernelMemoryStats after;
    KernelArea *area = NULL;
    uint32_t base;
    uint32_t size;
    uint32_t physical;
    const uint32_t cluster = KERNEL_AREA_COMMIT_CLUSTER_PAGES;
    const uint32_t touched = 100u;
    const uint32_t first = touched - (touched % KERNEL_AREA_COMMIT_CLUSTER_PAGES);

    initialize_test();
    assert(kernel_vm_create_address_space(300u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_stats(&before));
    assert(kernel_area_create(300u, KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_memory_stats(&after));
    /* A 2 MiB reservation, and not one frame spent on it. */
    assert(after.free_frames == before.free_frames);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.page_count == KERNEL_AREA_PAGE_MAX);
    assert(snapshot.committed_pages == 0u);
    assert(snapshot.reserved_form == 1u);

    assert(kernel_area_map(area, 300u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);
    /* The full extent is named even though none of it is there yet. */
    assert(size == KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE);
    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base + touched * KERNEL_PAGE_SIZE,
                                             true, &physical));

    assert(kernel_memory_stats(&before));
    assert(kernel_area_fault(300u, &space, base + touched * KERNEL_PAGE_SIZE));
    assert(kernel_memory_stats(&after));
    /*
     * The cluster, plus the page table it is published through: mapping an
     * area with nothing committed publishes no descriptors, so the table for
     * the slot is bought by the first commit rather than by the map.
     */
    assert(before.free_frames - after.free_frames == cluster + 1u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);

    /* The cluster containing the address, and nothing either side of it. */
    assert(kernel_vm_test_translate_current(base + touched * KERNEL_PAGE_SIZE,
                                            true, &physical));
    assert(kernel_vm_test_translate_current(base + first * KERNEL_PAGE_SIZE,
                                            true, &physical));
    assert(!kernel_vm_test_translate_current(
        base + (first - 1u) * KERNEL_PAGE_SIZE, true, &physical));
    assert(!kernel_vm_test_translate_current(
        base + (first + cluster) * KERNEL_PAGE_SIZE, true, &physical));

    /* A page already committed is not a fault this answers. */
    assert(!kernel_area_fault(300u, &space,
                              base + touched * KERNEL_PAGE_SIZE));
    assert(!kernel_area_fault(300u, &space, base + first * KERNEL_PAGE_SIZE));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);

    /*
     * A second cluster is a second commit, not a re-commit of the first, and
     * it costs exactly its own pages because the table is already there.
     */
    assert(kernel_memory_stats(&before));
    assert(kernel_area_fault(300u, &space,
                             base + (first + cluster) * KERNEL_PAGE_SIZE));
    assert(kernel_memory_stats(&after));
    assert(before.free_frames - after.free_frames == cluster);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 2u * cluster);
    assert(kernel_area_pool_valid());

    /* Closing gives back exactly what was committed and nothing more. */
    assert(kernel_memory_stats(&before));
    assert(kernel_area_unmap(300u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames - before.free_frames >= 2u * cluster);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
}

/*
 * An area is one object, so a page committed for one holder exists for every
 * holder. The alternative -- each address space faulting its own copy in --
 * would make an area two different objects that happened to share a name.
 */
static void test_reserved_area_commit_reaches_every_mapping(void)
{
    KernelAddressSpace owner = {0};
    KernelAddressSpace peer = {0};
    KernelArea *area = NULL;
    uint32_t owner_base;
    uint32_t peer_base;
    uint32_t size;
    uint32_t owner_physical;
    uint32_t peer_physical;

    initialize_test();
    assert(kernel_vm_create_address_space(310u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(311u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(310u, 64u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 310u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &owner_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 311u, &peer, KERNEL_VM_READ, &peer_base,
                           &size) == KERNEL_AREA_OK);
    assert(owner_base == peer_base);

    /* The owner touches it; the peer never faults at all. */
    assert(kernel_area_fault(310u, &owner, owner_base));
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(owner_base, true,
                                            &owner_physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(peer_base, false, &peer_physical));
    assert(owner_physical == peer_physical);
    /* The peer mapped read-only and that survives the commit. */
    assert(!kernel_vm_test_translate_current(peer_base, true, &peer_physical));
    assert(kernel_area_pool_valid());

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(310u, &owner, owner_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(311u, &peer, peer_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
}

/*
 * Committing on fault must not become a way to spend somebody else's frames.
 * A process with no mapping of the area gets nothing, however good its address
 * looks, and an eagerly committed area never grows this way either.
 */
static void test_reserved_area_fault_requires_authority(void)
{
    KernelAddressSpace owner = {0};
    KernelAddressSpace stranger = {0};
    KernelAreaSnapshot snapshot;
    KernelArea *reserved = NULL;
    KernelArea *eager = NULL;
    uint32_t base;
    uint32_t eager_base;
    uint32_t size;

    initialize_test();
    assert(kernel_vm_create_address_space(320u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(321u, &stranger) == KERNEL_VM_OK);
    assert(kernel_area_create(320u, 64u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &reserved) == KERNEL_AREA_OK);
    assert(kernel_area_map(reserved, 320u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);

    /* Right address, wrong process. */
    assert(!kernel_area_fault(321u, &stranger, base));
    /* Right process, but an address space it does not hold the area in. */
    assert(!kernel_area_fault(320u, &stranger, base));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);

    /* An address outside every area slot is not this path's business. */
    assert(!kernel_area_fault(320u, &owner, KERNEL_VM_AREA_BASE - 4u));
    /* Nor is one past the end of the reservation. */
    assert(!kernel_area_fault(320u, &owner, base + 64u * KERNEL_PAGE_SIZE));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);

    /* An ordinary area is already whole and never takes this path. */
    assert(kernel_area_create(320u, 2u * KERNEL_PAGE_SIZE, 0u, &eager) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map(eager, 320u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &eager_base,
                           &size) == KERNEL_AREA_OK);
    assert(!kernel_area_fault(320u, &owner, eager_base));
    assert(kernel_area_pool_valid());

    assert(kernel_area_unmap(320u, &owner, base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(320u, &owner, eager_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(reserved, NULL);
    kernel_area_handle_release(eager, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&stranger) == KERNEL_VM_OK);
}

/*
 * The kernel reaching a reserved page before the owner's own access does. A
 * write has to commit it, because the bytes must survive; a read must not,
 * because an uncommitted page reads as the zeros it would hold anyway, and
 * spending a frame to say so is the one case that does not need the memory.
 */
static void test_reserved_area_kernel_access_commits_only_on_write(void)
{
    KernelAreaSnapshot snapshot;
    KernelArea *area = NULL;
    uint8_t pattern[8];
    uint8_t read_back[8];

    initialize_test();
    assert(kernel_area_create(330u, 64u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);

    memset(read_back, 0xa5, sizeof(read_back));
    assert(kernel_area_read(area, 0u, read_back, sizeof(read_back)) ==
           KERNEL_AREA_OK);
    for (uint32_t index = 0u; index < sizeof(read_back); ++index)
        assert(read_back[index] == 0u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);

    memset(pattern, 0x5a, sizeof(pattern));
    assert(kernel_area_write(area, 32u * KERNEL_PAGE_SIZE, pattern,
                             sizeof(pattern)) == KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == KERNEL_AREA_COMMIT_CLUSTER_PAGES);
    memset(read_back, 0u, sizeof(read_back));
    assert(kernel_area_read(area, 32u * KERNEL_PAGE_SIZE, read_back,
                            sizeof(read_back)) == KERNEL_AREA_OK);
    assert(memcmp(read_back, pattern, sizeof(pattern)) == 0);
    assert(kernel_area_pool_valid());

    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_valid());
}

/*
 * Handing frames back. Without this, "frees memory" is a phrase: a program
 * that allocates and releases in a loop ratchets upward until it dies, which
 * on a 128 MB machine is not a detail.
 */
static void test_reserved_area_decommit_returns_frames(void)
{
    KernelAddressSpace owner = {0};
    KernelAddressSpace peer = {0};
    KernelAreaSnapshot snapshot;
    KernelMemoryStats before;
    KernelMemoryStats after;
    KernelArea *area = NULL;
    KernelArea *eager = NULL;
    uint32_t base;
    uint32_t peer_base;
    uint32_t eager_base;
    uint32_t size;
    uint32_t released;
    uint32_t physical;
    const uint32_t cluster = KERNEL_AREA_COMMIT_CLUSTER_PAGES;

    initialize_test();
    assert(kernel_vm_create_address_space(340u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(341u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(340u, KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 340u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 341u, &peer, KERNEL_VM_READ, &peer_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(340u, &owner, base));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);

    /* The frames come back, and the reservation does not. */
    assert(kernel_memory_stats(&before));
    assert(kernel_area_decommit(340u, &owner, base,
                                cluster * KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_OK);
    assert(kernel_memory_stats(&after));
    assert(released == cluster);
    /*
     * The cluster, and the page table each holder was using it through: the
     * last descriptor leaving a table takes the table with it, and there are
     * two address spaces holding this area.
     */
    assert(after.free_frames - before.free_frames == cluster + 2u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);
    assert(snapshot.page_count == KERNEL_AREA_PAGE_MAX);
    assert(snapshot.byte_size == KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE);

    /* Gone from every holder, not merely from the one that asked. */
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(peer_base, false, &physical));
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);

    /* And the reservation still works: touching it again re-commits. */
    assert(kernel_area_fault(340u, &owner, base));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);
    /* Dropping nothing is not an error, it is a range with nothing in it. */
    assert(kernel_area_decommit(340u, &owner,
                                base + 200u * KERNEL_PAGE_SIZE,
                                KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK);
    assert(released == 0u);

    /*
     * A partly covered page keeps its contents: rounding outward here would
     * throw away bytes the owner never offered.
     */
    assert(kernel_area_decommit(340u, &owner, base + 1u,
                                KERNEL_PAGE_SIZE - 2u, &released) ==
           KERNEL_AREA_OK);
    assert(released == 0u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);

    /* Not the caller's area, and not an ordinary area either. */
    assert(kernel_area_decommit(341u, &peer, base,
                                KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK);
    assert(kernel_area_create(340u, 2u * KERNEL_PAGE_SIZE, 0u, &eager) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map(eager, 340u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &eager_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_decommit(340u, &owner, eager_base, KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_decommit(340u, &owner, KERNEL_VM_AREA_BASE - 4u,
                                KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_NOT_MAPPED);
    assert(kernel_area_pool_valid());

    assert(kernel_area_unmap(340u, &owner, base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(341u, &peer, peer_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(340u, &owner, eager_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    kernel_area_handle_release(eager, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
}

/*
 * The awkward shapes of a cluster: a hole punched into a committed run, and a
 * cluster the end of the area cuts short. Both are off-by-one country, and the
 * first one is how a decommitted page gets its memory back -- scanning from
 * the cluster's base rather than from the faulting page would find the base
 * occupied and refuse, which retires a process for touching its own heap.
 */
static void test_reserved_area_commits_holes_and_short_tails(void)
{
    KernelAddressSpace owner = {0};
    KernelAreaSnapshot snapshot;
    KernelArea *area = NULL;
    uint32_t base;
    uint32_t size;
    uint32_t released;
    uint32_t physical;
    /* Deliberately not a whole number of clusters: 20 = 16 + 4. */
    const uint32_t pages = KERNEL_AREA_COMMIT_CLUSTER_PAGES + 4u;
    const uint32_t cluster = KERNEL_AREA_COMMIT_CLUSTER_PAGES;

    initialize_test();
    assert(kernel_vm_create_address_space(350u, &owner) == KERNEL_VM_OK);
    assert(kernel_area_create(350u, pages * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 350u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);

    /* The last cluster is short, and takes only the pages that exist. */
    assert(kernel_area_fault(350u, &owner, base + 18u * KERNEL_PAGE_SIZE));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages - cluster);
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base + cluster * KERNEL_PAGE_SIZE,
                                            true, &physical));
    assert(kernel_vm_test_translate_current(
        base + (pages - 1u) * KERNEL_PAGE_SIZE, true, &physical));

    /* Now a full cluster, then a hole in the middle of it. */
    assert(kernel_area_fault(350u, &owner, base));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages);
    assert(kernel_area_decommit(350u, &owner, base + 5u * KERNEL_PAGE_SIZE,
                                KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK);
    assert(released == 1u);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages - 1u);
    assert(!kernel_vm_test_translate_current(base + 5u * KERNEL_PAGE_SIZE,
                                             true, &physical));

    /*
     * Touching the hole refills exactly it -- one page, because its
     * neighbours on both sides are already there.
     */
    assert(kernel_area_fault(350u, &owner, base + 5u * KERNEL_PAGE_SIZE));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages);
    assert(kernel_vm_test_translate_current(base + 5u * KERNEL_PAGE_SIZE,
                                            true, &physical));

    /* A wider hole, refilled from a fault in its middle rather than its end. */
    assert(kernel_area_decommit(350u, &owner, base + 8u * KERNEL_PAGE_SIZE,
                                3u * KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK);
    assert(released == 3u);
    assert(kernel_area_fault(350u, &owner, base + 9u * KERNEL_PAGE_SIZE));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages);
    for (uint32_t page = 8u; page < 11u; ++page)
        assert(kernel_vm_test_translate_current(
            base + page * KERNEL_PAGE_SIZE, true, &physical));
    assert(kernel_area_pool_valid());

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(350u, &owner, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
}

/*
 * The quota is charged at commit, not at creation, which is the whole point of
 * reserving. So a reservation larger than the owner's page budget is allowed
 * to exist, and it is the commit that eventually refuses -- leaving the area
 * intact and every page already committed still committed.
 */
static void test_reserved_area_commit_meets_the_owner_quota(void)
{
    KernelAddressSpace owner = {0};
    KernelAreaSnapshot snapshot;
    KernelArea *area = NULL;
    uint32_t base;
    uint32_t size;
    uint32_t committed = 0u;
    uint32_t page = 0u;

    initialize_test();
    assert(kernel_vm_create_address_space(360u, &owner) == KERNEL_VM_OK);
    assert(kernel_area_create(360u, KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 360u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);

    /* Commit until the owner's page budget says no. */
    while (page < KERNEL_AREA_PAGE_MAX) {
        if (!kernel_area_fault(360u, &owner,
                               base + page * KERNEL_PAGE_SIZE))
            break;
        committed += KERNEL_AREA_COMMIT_CLUSTER_PAGES;
        page += KERNEL_AREA_COMMIT_CLUSTER_PAGES;
    }
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == committed);
    assert(committed <= KERNEL_AREA_OWNER_PAGE_MAX);
    /* Refusing leaves the reservation and everything already there alone. */
    assert(snapshot.page_count == KERNEL_AREA_PAGE_MAX);
    assert(kernel_area_pool_valid());

    assert(kernel_area_unmap(360u, &owner, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
}

int main(void)
{
    test_allocation_injection_preserves_mapping_baseline();
    test_same_address_aliases_survive_creator_death();
    test_child_lifetime_and_quotas();
    test_service_can_map_every_area_slot();
    test_screen_sized_area_reaches_its_last_pixel();
    test_create_transaction_rolls_back_every_stage();
    test_map_transaction_rolls_back_every_stage();
    test_reserved_area_commits_only_what_is_touched();
    test_reserved_area_commit_reaches_every_mapping();
    test_reserved_area_fault_requires_authority();
    test_reserved_area_kernel_access_commits_only_on_write();
    test_reserved_area_decommit_returns_frames();
    test_reserved_area_commits_holes_and_short_tails();
    test_reserved_area_commit_meets_the_owner_quota();
    puts("area tests passed");
    return 0;
}
