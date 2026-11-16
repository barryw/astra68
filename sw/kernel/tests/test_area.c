#include "allocation.h"
#include "generation.h"
#include "area.h"
#include "port.h"
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
#define TEST_ADDRESS_SPACE_COUNT 40u

static uint8_t physical_memory[32u * 1024u * 1024u];
static uint32_t pager_owner;

/* Pager fixtures use sentinel pointers; process tests cover real port owners. */
uint32_t kernel_port_owner(const KernelPort *port)
{
    assert(port == (const KernelPort *)(uintptr_t)1u);
    return pager_owner;
}

void kernel_pmmu_load_tc(uint32_t value) { (void)value; }
void kernel_pmmu_load_srp(uint32_t root) { (void)root; }
void kernel_pmmu_load_urp(uint32_t root) { (void)root; }
void kernel_pmmu_disable_transparent_translation(void) { }
void kernel_pmmu_read_tc(uint32_t *value) { *value = 0u; }
void kernel_pmmu_read_srp(uint32_t *root) { *root = 0u; }
void kernel_pmmu_read_urp(uint32_t *root) { *root = 0u; }
void kernel_pmmu_flush_all(void) { }
void kernel_pmmu_flush_non_global(void) { }
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

    pager_owner = 0u;
    memset(&info, 0, sizeof(info));
    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.abi_major = ASTRA_BOOT_ABI_MAJOR;
    info.abi_minor = ASTRA_BOOT_ABI_MINOR;
    info.total_size = sizeof(info);
    info.flags = ASTRA_BOOT_REQUIRED_FLAGS;
    info.machine_id = 0x41363801u;
    info.hardware_build_id = 0x12345678u;
    info.cpu_model = 0x00068040u;
    info.cpu_implementation = 0x54474d32u;
    info.cpu_features = 0x0000000du;
    info.cpu_hz = 12500000u;
    info.ram_base = RAM_BASE;
    info.ram_size = sizeof(physical_memory);
    info.rom_base = ASTRA_ROM_ADDRESS;
    info.rom_size = ASTRA_ROM_SIZE;
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
    add_range(&info, ASTRA_BOOT_LOW_USABLE_ADDRESS,
              ASTRA_BOOT_LOW_USABLE_SIZE,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_USER_IMAGE_ADDRESS,
              OHCI_DMA_POOL_BASE - ASTRA_USER_IMAGE_ADDRESS,
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
    KernelAddressSpace spaces[TEST_ADDRESS_SPACE_COUNT] = {{0}};
    KernelAllocationStats mapping_metadata;
    KernelAllocationStats area_metadata;
    KernelAreaPoolStats stats;
    KernelAreaSnapshot snapshot;
    uint32_t rejected_base;
    uint32_t rejected_size;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    KernelArea *area;
    uint32_t bases[TEST_ADDRESS_SPACE_COUNT];
    uint32_t sizes[TEST_ADDRESS_SPACE_COUNT];
    uint32_t physical[TEST_ADDRESS_SPACE_COUNT - 1u];
    uint8_t bytes[2u * KERNEL_PAGE_SIZE];

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    for (uint32_t index = 0u;
         index < TEST_ADDRESS_SPACE_COUNT; ++index)
        assert(kernel_vm_create_address_space(100u + index, &spaces[index]) ==
               KERNEL_VM_OK);
    assert(kernel_area_create(7u, sizeof(bytes), 0u, &area) == KERNEL_AREA_OK);
    assert(kernel_area_handle_retain(area, NULL));
    memset(bytes, 0, sizeof(bytes));
    assert(kernel_area_read(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    for (uint32_t index = 0u; index < sizeof(bytes); ++index)
        assert(bytes[index] == 0u);

    for (uint32_t index = 0u; index < TEST_ADDRESS_SPACE_COUNT - 1u;
         ++index) {
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
                           &bases[TEST_ADDRESS_SPACE_COUNT - 1u],
                           &sizes[TEST_ADDRESS_SPACE_COUNT - 1u]) ==
           KERNEL_AREA_OK);
    assert(bases[TEST_ADDRESS_SPACE_COUNT - 1u] == bases[0] &&
           sizes[TEST_ADDRESS_SPACE_COUNT - 1u] == sizes[0]);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &bases[TEST_ADDRESS_SPACE_COUNT - 1u],
                           &sizes[TEST_ADDRESS_SPACE_COUNT - 1u]) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_map(area, 100u + TEST_ADDRESS_SPACE_COUNT - 1u,
                           &spaces[TEST_ADDRESS_SPACE_COUNT - 1u],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[TEST_ADDRESS_SPACE_COUNT - 1u],
                           &sizes[TEST_ADDRESS_SPACE_COUNT - 1u]) ==
           KERNEL_AREA_OK);
    assert(kernel_area_unmap(101u, &spaces[1], bases[1]) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 101u, &spaces[1],
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &bases[1], &sizes[1]) ==
           KERNEL_AREA_OK);

    bytes[0] = 0x5au;
    bytes[sizeof(bytes) - 1u] = 0xc3u;
    assert(kernel_area_write(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(physical_memory[physical[0] - RAM_BASE] == 0x5au);

    assert(kernel_area_process_died(7u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.creator == 7u);
    assert(snapshot.terminal_result == 0u);
    assert(snapshot.mapping_references == TEST_ADDRESS_SPACE_COUNT);
    assert(snapshot.frames_released == 0u);
    assert(kernel_area_map(area, 100u, &spaces[0], KERNEL_VM_READ,
                           &rejected_base, &rejected_size) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 1u && stats.closing_areas == 0u);
    assert(stats.active_mappings == TEST_ADDRESS_SPACE_COUNT);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_live(area));
    for (uint32_t index = 0u; index < TEST_ADDRESS_SPACE_COUNT - 1u; ++index)
        assert(kernel_area_unmap(100u + index, &spaces[index], bases[index]) ==
               KERNEL_AREA_OK);
    assert(kernel_area_unmap(100u + TEST_ADDRESS_SPACE_COUNT - 1u,
                             &spaces[TEST_ADDRESS_SPACE_COUNT - 1u],
                             bases[TEST_ADDRESS_SPACE_COUNT - 1u]) ==
           KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.closing_areas == 0u);

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    for (uint32_t index = 0u;
         index < TEST_ADDRESS_SPACE_COUNT; ++index) {
        assert(kernel_vm_destroy_address_space(&spaces[index]) == KERNEL_VM_OK);
        assert(kernel_memory_release_owner(100u + index, NULL) ==
               KERNEL_MEMORY_OK);
    }
    assert(kernel_memory_stats(&after));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA, &mapping_metadata));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, &area_metadata));
    assert(after.free_frames + mapping_metadata.current_units +
               area_metadata.current_units ==
           baseline.free_frames);
    assert(kernel_area_pool_valid());
}

static void test_allocation_injection_preserves_mapping_baseline(void)
{
    KernelAddressSpace space = {0};
    KernelAllocationStats area_allocation;
    KernelAllocationStats area_metadata_allocation;
    KernelAllocationStats mapping_allocation;
    KernelAllocationStats mapping_metadata_allocation;
    KernelAllocationStats page_allocation;
    KernelAllocationStats page_metadata_allocation;
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
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_NO_SLOT);
    assert(area == NULL);
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    assert(kernel_area_pool_valid());
    area = (KernelArea *)(uintptr_t)1u;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_PAGES, 1u);
    assert(kernel_area_create(31u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    assert(area == NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_memory_stats(&after));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA,
        &area_metadata_allocation));
    assert(after.free_frames + area_metadata_allocation.current_units ==
           baseline.free_frames);

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

    virtual_base = UINT32_MAX;
    byte_size = UINT32_MAX;
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA, 1u);
    assert(kernel_area_map(area, 32u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &virtual_base, &byte_size) ==
           KERNEL_AREA_OUT_OF_MEMORY);
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
    assert(after.free_frames + area_metadata_allocation.current_units ==
           baseline.free_frames);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT, &area_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_PAGES, &page_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
        &page_metadata_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING, &mapping_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA,
        &mapping_metadata_allocation));
    assert(area_allocation.current_units == 0u);
    assert(page_allocation.current_units == 0u);
    assert(mapping_allocation.current_units == 0u);
    assert(mapping_metadata_allocation.current_units == 0u);
    assert(area_allocation.injected_failures == 2u);
    assert(area_metadata_allocation.injected_failures == 1u);
    assert(page_allocation.injected_failures == 1u);
    assert(page_metadata_allocation.current_units == 0u);
    assert(page_metadata_allocation.injected_failures == 1u);
    assert(mapping_allocation.injected_failures == 2u);
    assert(mapping_metadata_allocation.injected_failures == 1u);
    assert(kernel_area_pool_valid());
    assert(kernel_allocation_valid());
}

static void test_mapping_table_grows_past_process_derived_limit(void)
{
    enum {
        LEGACY_MAPPING_CAPACITY = 32u * 32u,
        PROCESS_COUNT = 33u,
        AREA_COUNT = KERNEL_VM_AREA_SLOT_COUNT
    };
    static KernelAddressSpace spaces[PROCESS_COUNT];
    KernelArea *areas[AREA_COUNT];
    KernelAreaPoolStats stats;
    uint32_t base;
    uint32_t size;

    initialize_test();
    memset(spaces, 0, sizeof(spaces));
    for (uint32_t process = 0u; process < PROCESS_COUNT; ++process)
        assert(kernel_vm_create_address_space(500u + process,
                                              &spaces[process]) ==
               KERNEL_VM_OK);
    for (uint32_t area = 0u; area < AREA_COUNT; ++area)
        assert(kernel_area_create(99u, KERNEL_PAGE_SIZE,
                                  KERNEL_AREA_CREATE_RESERVED,
                                  &areas[area]) == KERNEL_AREA_OK);
    for (uint32_t process = 0u; process < PROCESS_COUNT; ++process) {
        for (uint32_t area = 0u; area < AREA_COUNT; ++area)
            assert(kernel_area_map(areas[area], 500u + process,
                                   &spaces[process], KERNEL_VM_READ,
                                   &base, &size) == KERNEL_AREA_OK);
    }
    assert(PROCESS_COUNT * AREA_COUNT > LEGACY_MAPPING_CAPACITY);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_mappings == PROCESS_COUNT * AREA_COUNT);

    for (uint32_t process = 0u; process < PROCESS_COUNT; ++process) {
        uint32_t unmapped = 0u;

        assert(kernel_area_unmap_process(500u + process, &unmapped) ==
               KERNEL_AREA_OK);
        assert(unmapped == AREA_COUNT);
        assert(kernel_vm_destroy_address_space(&spaces[process]) ==
               KERNEL_VM_OK);
        assert(kernel_memory_release_owner(500u + process, NULL) ==
               KERNEL_MEMORY_OK);
    }
    for (uint32_t area = 0u; area < AREA_COUNT; ++area)
        kernel_area_handle_release(areas[area], NULL);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.active_mappings == 0u);
    assert(kernel_area_pool_valid());
}

static void test_child_lifetime(void)
{
    KernelAreaPoolStats stats;
    KernelArea *area = NULL;

    initialize_test();
    assert(kernel_area_create(9u, KERNEL_PAGE_SIZE, 0u, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_live(area));
    assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
    assert(kernel_area_pool_stats(&stats));
    assert(stats.active_areas == 0u && stats.committed_pages == 0u);
    assert(stats.quota_failures == 0u);
    assert(kernel_area_pool_valid());
}

static void test_area_objects_grow_past_legacy_limits(void)
{
    enum { area_count = 257u };
    KernelArea *areas[area_count];
    uint32_t frame_owners[area_count];

    initialize_test();
    for (uint32_t index = 0u; index < area_count; ++index) {
        KernelAreaSnapshot snapshot;

        assert(kernel_area_create(1000u, KERNEL_PAGE_SIZE,
                                  KERNEL_AREA_CREATE_RESERVED,
                                  &areas[index]) == KERNEL_AREA_OK);
        assert(kernel_area_snapshot(index, &snapshot));
        frame_owners[index] = snapshot.frame_owner;
        for (uint32_t prior = 0u; prior < index; ++prior)
            assert(frame_owners[prior] != frame_owners[index]);
    }
    for (uint32_t index = 0u; index < area_count; ++index)
        kernel_area_handle_release(areas[index], NULL);
    assert(kernel_area_pool_valid());
}

static void test_area_slots_belong_to_address_spaces(void)
{
    KernelAddressSpace first = {0};
    KernelAddressSpace second = {0};
    KernelArea *shared = NULL;
    KernelArea *local = NULL;
    uint32_t first_base;
    uint32_t second_base;
    uint32_t local_base;
    uint32_t size;
    uint32_t first_physical;
    uint32_t second_physical;

    initialize_test();
    assert(kernel_vm_create_address_space(1200u, &first) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(1201u, &second) == KERNEL_VM_OK);
    assert(kernel_area_create(1200u, KERNEL_PAGE_SIZE, 0u, &shared) ==
           KERNEL_AREA_OK);
    assert(kernel_area_create(1201u, KERNEL_PAGE_SIZE, 0u, &local) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map(shared, 1200u, &first,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &first_base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(local, 1201u, &second,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &local_base, &size) == KERNEL_AREA_OK);
    assert(first_base == KERNEL_VM_AREA_BASE);
    assert(local_base == KERNEL_VM_AREA_BASE);
    assert(kernel_area_map(shared, 1201u, &second, KERNEL_VM_READ,
                           &second_base, &size) == KERNEL_AREA_OK);
    assert(second_base == KERNEL_VM_AREA_BASE + KERNEL_VM_SHARED_TABLE_SPAN);
    assert(kernel_vm_switch(&first) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(first_base, true,
                                            &first_physical));
    assert(kernel_vm_switch(&second) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(second_base, false,
                                            &second_physical));
    assert(first_physical == second_physical);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(1200u, &first, first_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(1201u, &second, second_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(1201u, &second, local_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(shared, NULL);
    kernel_area_handle_release(local, NULL);
    assert(kernel_vm_destroy_address_space(&first) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&second) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(1200u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(1201u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_area_pool_valid());
}

static void test_process_clone_inherits_area_mappings(void)
{
    KernelAddressSpace parent = {0};
    KernelAddressSpace child = {0};
    KernelArea *area = NULL;
    uint32_t base;
    uint32_t size;
    uint32_t physical;
    uint32_t unmapped;
    uint32_t handles, mappings, children;

    initialize_test();
    assert(kernel_vm_create_address_space(1300u, &parent) == KERNEL_VM_OK);
    assert(kernel_area_create(1300u, 2u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 1300u, &parent,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(1300u, &parent, base, false));
    assert(kernel_vm_clone_address_space(&parent, 1301u, &child) ==
           KERNEL_VM_OK);
    assert(kernel_area_clone_process(1300u, &parent, 1301u, &child) ==
           KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(handles == 1u && mappings == 2u && children == 0u);
    assert(kernel_vm_switch(&child) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_area_unmap_process(1301u, &unmapped) == KERNEL_AREA_OK);
    assert(unmapped == 1u);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(handles == 1u && mappings == 1u && children == 0u);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(1300u, &parent, base) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(handles == 1u && mappings == 0u && children == 0u);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(1300u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release_owner(1301u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_area_pool_valid());
}

static void test_service_maps_more_than_thirty_two_small_areas(void)
{
    enum { mapping_count = ASTRA_SHARED_AREA_SLOT_COUNT + 8u };
    KernelAddressSpace service = {0};
    KernelArea *areas[mapping_count];
    uint32_t bases[mapping_count];
    uint32_t sizes[mapping_count];

    initialize_test();
    assert(kernel_vm_create_address_space(200u, &service) == KERNEL_VM_OK);
    for (uint32_t index = 0u; index < mapping_count; ++index) {
        assert(kernel_area_create(201u, KERNEL_PAGE_SIZE,
                                  KERNEL_AREA_CREATE_RESERVED,
                                  &areas[index]) == KERNEL_AREA_OK);
        assert(kernel_area_map(areas[index], 200u, &service,
                               KERNEL_VM_READ | KERNEL_VM_WRITE,
                               &bases[index], &sizes[index]) ==
               KERNEL_AREA_OK);
        /* One span each: mappings attach shared span tables. */
        assert(bases[index] == KERNEL_VM_AREA_BASE +
               index * KERNEL_VM_SHARED_TABLE_SPAN);
        assert(sizes[index] == KERNEL_PAGE_SIZE);
    }
    for (uint32_t index = 0u; index < mapping_count; ++index) {
        assert(kernel_area_unmap(200u, &service, bases[index]) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(areas[index], NULL);
    }
    assert(kernel_memory_release_owner(201u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_vm_destroy_address_space(&service) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(200u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_area_pool_valid());
}

static void test_area_mapping_stops_at_virtual_window_exhaustion(void)
{
    KernelAddressSpace service = {0};
    KernelArea *area;
    KernelArea *extra;
    uint32_t base;
    uint32_t extra_base;
    uint32_t size;

    initialize_test();
    assert(kernel_vm_create_address_space(220u, &service) == KERNEL_VM_OK);
    assert(kernel_area_create(221u, ASTRA_AREA_SIZE_MAX,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(ASTRA_AREA_SIZE_MAX > ASTRA_SHARED_AREA_SLOT_SIZE);
    assert(kernel_area_map(area, 220u, &service,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &base, &size) == KERNEL_AREA_OK);
    assert(base == KERNEL_VM_AREA_BASE && size == ASTRA_AREA_SIZE_MAX);
    assert(kernel_area_create(230u, KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &extra) == KERNEL_AREA_OK);
    assert(kernel_area_map(extra, 220u, &service,
                           KERNEL_VM_READ | KERNEL_VM_WRITE,
                           &extra_base, &size) ==
           KERNEL_AREA_QUOTA_EXCEEDED);
    assert(extra_base == 0u && size == 0u);
    kernel_area_handle_release(extra, NULL);
    assert(kernel_area_unmap(220u, &service, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&service) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(220u, NULL) == KERNEL_MEMORY_OK);
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
        KernelAllocationStats area_metadata;
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
        assert(kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, &area_metadata));
        assert(after_memory.free_frames + area_metadata.current_units ==
               baseline_memory.free_frames);
        assert(after_memory.owner_slots_used ==
               baseline_memory.owner_slots_used);
        assert(kernel_area_pool_valid());

        assert(kernel_area_create(71u, 2u * KERNEL_PAGE_SIZE, 0u, &area) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_memory_stats(&after_memory));
        assert(after_memory.free_frames + area_metadata.current_units ==
               baseline_memory.free_frames);
        assert(kernel_area_pool_valid());
    }
}

static void assert_failed_map_baseline(
    const KernelAddressSpace *space, const KernelAddressSpace *baseline_space,
    const KernelMemoryStats *baseline_memory,
    const KernelVmStats *baseline_vm,
    const KernelAreaPoolStats *baseline_pool)
{
    KernelAllocationStats mapping_metadata;
    KernelAreaPoolStats area_stats;
    KernelMemoryStats memory;
    KernelVmStats vm;

    assert(memcmp(space, baseline_space, sizeof(*space)) == 0);
    assert(kernel_memory_stats(&memory));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA, &mapping_metadata));
    assert(memory.free_frames + mapping_metadata.current_units ==
           baseline_memory->free_frames);
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
    /* An area maps by attaching its span tables: the stages are the
     * pointer table and the table reference (kernel_vm_attach_shared_table). */
    static const KernelVmSharedMapFault vm_faults[] = {
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE,
        KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN
    };
    const uint32_t fault_count =
        (uint32_t)(sizeof(vm_faults) / sizeof(vm_faults[0])) + 1u;

    for (uint32_t index = 0u; index < fault_count; ++index) {
        KernelAddressSpace space = {0};
        KernelAllocationStats mapping_metadata;
        KernelAllocationStats area_metadata;
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
        assert(kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA,
            &mapping_metadata));
        assert(kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, &area_metadata));
        assert(final_memory.free_frames + mapping_metadata.current_units +
                   area_metadata.current_units ==
               initial_memory.free_frames);
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
    KernelAllocationStats area_metadata;
    KernelAllocationStats page_metadata;
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
    /* A complete-window reservation, and not one frame spent on its data. */
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, &area_metadata));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, &page_metadata));
    assert(before.free_frames - after.free_frames ==
           area_metadata.current_units);
    assert(page_metadata.current_units == 0u);
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
    assert(kernel_area_fault(300u, &space, base + touched * KERNEL_PAGE_SIZE, false));
    assert(kernel_memory_stats(&after));
    /*
     * The cluster, its page-vector leaf, plus the 68040 pointer and page tables
     * it is published through: mapping an area with nothing committed
     * publishes no descriptors, so all three metadata frames arrive with the
     * first commit rather than with the reservation.
     */
    assert(before.free_frames - after.free_frames == cluster + 3u);
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
                              base + touched * KERNEL_PAGE_SIZE, false));
    assert(!kernel_area_fault(300u, &space, base + first * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == cluster);

    /*
     * A second cluster is a second commit, not a re-commit of the first, and
     * it costs exactly its own pages because the table is already there.
     */
    assert(kernel_memory_stats(&before));
    assert(kernel_area_fault(300u, &space,
                             base + (first + cluster) * KERNEL_PAGE_SIZE, false));
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
    assert(kernel_area_fault(310u, &owner, owner_base, false));
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

static void test_reserved_area_publishes_one_complete_page(void)
{
    KernelAddressSpace owner = {0};
    KernelAddressSpace peer = {0};
    KernelArea *area = NULL;
    KernelAreaSnapshot snapshot;
    uint32_t owner_base, peer_base, size, owner_physical, peer_physical;
    uint32_t published_physical;
    const uint8_t source[] = {0x51u, 0x52u, 0x53u, 0x54u, 0x55u};

    initialize_test();
    assert(kernel_vm_create_address_space(315u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(316u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(315u, 2u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 315u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &owner_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 316u, &peer, KERNEL_VM_READ, &peer_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(owner_base, false,
                                             &owner_physical));
    assert(kernel_area_publish_page(area, 1u, source, sizeof(source)) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_publish_page(area, 0u, NULL, 0u) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_publish_page(area, 0u, source, KERNEL_PAGE_SIZE + 1u) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_AREA_PAGES, 1u);
    assert(kernel_area_publish_page(area, 0u, source, sizeof(source)) ==
           KERNEL_AREA_OUT_OF_MEMORY);
    kernel_allocation_test_clear_failure();
    assert(!kernel_vm_test_translate_current(owner_base, false,
                                             &owner_physical));
    assert(kernel_area_snapshot(0u, &snapshot) &&
           snapshot.committed_pages == 0u);
    assert(kernel_area_publish_page(area, 0u, source, sizeof(source)) ==
           KERNEL_AREA_OK);
    assert(kernel_area_snapshot(0u, &snapshot) &&
           snapshot.committed_pages == 1u);
    assert(kernel_vm_test_translate_current(owner_base, true,
                                            &owner_physical));
    published_physical = owner_physical;
    assert(memcmp(&physical_memory[owner_physical - RAM_BASE], source,
                  sizeof(source)) == 0);
    assert(physical_memory[owner_physical - RAM_BASE + sizeof(source)] == 0u);
    assert(physical_memory[owner_physical - RAM_BASE + KERNEL_PAGE_SIZE - 1u]
           == 0u);
    assert(!kernel_vm_test_translate_current(owner_base + KERNEL_PAGE_SIZE,
                                             false, &owner_physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(peer_base, false, &peer_physical));
    assert(published_physical == peer_physical);
    assert(!kernel_vm_test_translate_current(peer_base, true, &peer_physical));
    assert(kernel_area_publish_page(area, 0u, source, sizeof(source)) ==
           KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_publish_page(area, KERNEL_PAGE_SIZE, source, 0u) ==
           KERNEL_AREA_OK);
    assert(kernel_vm_test_translate_current(peer_base + KERNEL_PAGE_SIZE,
                                            false, &peer_physical));
    assert(physical_memory[peer_physical - RAM_BASE] == 0u);
    assert(kernel_area_pool_valid());

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(315u, &owner, owner_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(316u, &peer, peer_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static uint32_t pager_release_count;

static void release_test_pager(void *object, void *context)
{
    assert(object == (void *)(uintptr_t)1u);
    assert(context == (void *)(uintptr_t)2u);
    ++pager_release_count;
}

static void test_pager_death_revokes_peers_and_releases_dirty_pin(void)
{
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        KernelAddressSpace creator = {0}, peer = {0};
        KernelArea *area;
        KernelAreaPoolStats stats;
        KernelAreaSnapshot state;
        uint8_t bytes[KERNEL_PAGE_SIZE] = {0};
        uint32_t base, peer_base, size, closed, revoked, physical, token = 0u;

        initialize_test();
        pager_owner = 432u;
        pager_release_count = 0u;
        assert(kernel_vm_create_address_space(431u, &creator) == KERNEL_VM_OK);
        assert(kernel_vm_create_address_space(433u, &peer) == KERNEL_VM_OK);
        assert(kernel_area_create(431u, 2u * KERNEL_PAGE_SIZE,
            KERNEL_AREA_CREATE_RESERVED |
                ((mode & 1u) ? KERNEL_AREA_CREATE_MAPPED_LIFETIME : 0u),
            &area) == KERNEL_AREA_OK);
        assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
            release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
        assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 9u) == KERNEL_AREA_OK);
        assert(kernel_area_publish_dirty_page(area, 0u, bytes) == KERNEL_AREA_OK);
        if ((mode & 2u) != 0u)
            assert(kernel_area_writeback_begin(area, 0u, bytes, &token) == KERNEL_AREA_OK);
        assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 431u, &creator, KERNEL_VM_READ,
            &base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 433u, &peer, KERNEL_VM_READ | KERNEL_VM_WRITE |
            ((mode & 1u) ? ASTRA_AREA_MAP_PRIVATE : 0u),
            &peer_base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_fault(433u, &peer, peer_base, true));
        assert(kernel_area_process_died(431u, &closed, &revoked) == KERNEL_AREA_OK);
        assert(closed == 0u && revoked == 1u && pager_release_count == 0u);
        assert(kernel_area_live(area) && kernel_area_pool_valid());
        assert(kernel_area_snapshot(0u, &state));
        assert(state.child_references == 2u && state.mapping_references == 1u);
        assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
        assert(kernel_vm_test_translate_current(peer_base, true, &physical));

        assert(kernel_area_process_died(pager_owner, &closed, &revoked) == KERNEL_AREA_OK);
        assert(closed == 1u && revoked == 1u && pager_release_count == 1u);
        assert(!kernel_vm_test_translate_current(peer_base, false, &physical));
        assert(!kernel_area_live(area) && kernel_area_pool_valid());
        assert(kernel_area_snapshot(0u, &state));
        assert(state.terminal_result == ASTRA_SYSCALL_PEER_DEAD && state.frames_released != 0u);
        assert(state.child_references == 1u && state.mapping_references == 0u);
        if (token != 0u)
            assert(kernel_area_writeback_complete(area, 0u, token, true) == KERNEL_AREA_PEER_DEAD);
        assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_area_pool_stats(&stats));
        assert(stats.active_areas == 0u && stats.active_mappings == 0u);
        assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
        assert(kernel_vm_destroy_address_space(&creator) == KERNEL_VM_OK);
        assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
        assert(kernel_area_pool_valid() && pager_release_count == 1u);
    }
}

static void test_publish_dirty_page_is_atomic_and_retained(void)
{
    KernelAddressSpace space = {0};
    KernelArea *area = NULL;
    uint8_t source[KERNEL_PAGE_SIZE], snapshot[KERNEL_PAGE_SIZE];
    uint32_t base, size, handles, mappings, children, token, released;

    memset(source, 0x51, sizeof(source));
    for (uint32_t failure = 1u; failure <= 4u; ++failure) {
        initialize_test();
        assert(kernel_area_create(317u, KERNEL_PAGE_SIZE,
                                  KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
        assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
            release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
        kernel_allocation_test_fail_site(failure == 4u ?
            KERNEL_ALLOCATION_SITE_AREA_PAGES : KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
            failure == 4u ? 1u : failure);
        assert(kernel_area_publish_dirty_page(area, 0u, source) == KERNEL_AREA_OUT_OF_MEMORY);
        kernel_allocation_test_clear_failure();
        assert(!kernel_area_page_resident(area, 0u));
        assert(kernel_area_references(area, &handles, &mappings, &children) && children == 0u);
        assert(kernel_area_pool_valid());
        kernel_area_handle_release(area, NULL);
        assert(kernel_area_pool_valid());
    }

    initialize_test();
    assert(kernel_vm_create_address_space(317u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(317u, 2u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_publish_dirty_page(area, 0u, source) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 317u, &space, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_publish_dirty_page(area, 1u, source) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_publish_dirty_page(area, size, source) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_publish_dirty_page(area, 0u, NULL) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_publish_dirty_page(area, 0u, source) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children) && children == 1u);
    assert(kernel_vm_read(&space, base, snapshot, sizeof(snapshot)) == KERNEL_VM_OK);
    assert(memcmp(source, snapshot, sizeof(source)) == 0);
    assert(!kernel_area_page_resident(area, KERNEL_PAGE_SIZE));
    memset(source, 0x72, sizeof(source));
    assert(kernel_area_publish_dirty_page(area, 0u, source) == KERNEL_AREA_BUSY);
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) == KERNEL_AREA_OK);
    for (uint32_t index = 0u; index < sizeof(snapshot); ++index)
        assert(snapshot[index] == 0x51u);
    assert(kernel_area_writeback_complete(area, 0u, token, false) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_BUSY);
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, 0u, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children) && children == 0u);
    assert(kernel_area_publish_dirty_page(area, 0u, source) == KERNEL_AREA_BUSY);
    assert(kernel_area_pool_valid());
    assert(kernel_area_unmap(317u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_pager_dirty_scan_preserves_pending_state(void)
{
    KernelAddressSpace space = {0};
    KernelArea *area = NULL;
    uint8_t page[KERNEL_PAGE_SIZE] = {0x71u};
    uint32_t base, size, offset = 99u, state = 99u, token;
    const uint32_t far = 8u * 1024u * 1024u;

    initialize_test();
    assert(kernel_vm_create_address_space(320u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(320u, KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, 0u, &offset, &state) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 320u, &space, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(NULL, 0u, &offset, &state) ==
        KERNEL_AREA_INVALID_ARGUMENT && offset == 0u && state == 0u);
    assert(kernel_area_dirty_scan(area, 1u, &offset, &state) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_dirty_scan(area, size + KERNEL_PAGE_SIZE, &offset, &state) ==
        KERNEL_AREA_INVALID_ARGUMENT);
    state = 99u;
    assert(kernel_area_dirty_scan(area, 0u, NULL, &state) ==
        KERNEL_AREA_INVALID_ARGUMENT && state == 0u);
    offset = 99u;
    assert(kernel_area_dirty_scan(area, 0u, &offset, NULL) ==
        KERNEL_AREA_INVALID_ARGUMENT && offset == 0u);
    assert(kernel_area_dirty_scan(area, 0u, &offset, &state) == KERNEL_AREA_OK &&
        offset == size && state == 0u);
    assert(kernel_area_publish_dirty_page(area, 0u, page) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_begin(area, 0u, page, &token) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, 0u, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_publish_dirty_page(area, far, page) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, 0u, &offset, &state) == KERNEL_AREA_OK &&
        state == 0u && offset > 0u && offset <= far);
    uint32_t previous = offset;
    assert(kernel_area_dirty_scan(area, offset, &offset, &state) == KERNEL_AREA_OK &&
        offset == far && offset >= previous && state == ASTRA_AREA_DIRTY_MODIFIED);
    assert(kernel_area_writeback_begin(area, far, page, &token) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, far, &offset, &state) == KERNEL_AREA_OK &&
        offset == far && state == ASTRA_AREA_DIRTY_WRITEBACK);
    assert(kernel_area_write_pager(area, far, "x", 1u) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, far, &offset, &state) == KERNEL_AREA_OK &&
        state == (ASTRA_AREA_DIRTY_MODIFIED | ASTRA_AREA_DIRTY_WRITEBACK));
    assert(kernel_area_writeback_complete(area, far, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, far, &offset, &state) == KERNEL_AREA_OK &&
        state == ASTRA_AREA_DIRTY_MODIFIED);
    assert(kernel_area_writeback_begin(area, far, page, &token) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, far, token, false) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, far, &offset, &state) == KERNEL_AREA_OK &&
        state == ASTRA_AREA_DIRTY_MODIFIED);
    assert(kernel_area_writeback_begin(area, far, page, &token) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, far, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_dirty_scan(area, 0u, &offset, &state) == KERNEL_AREA_OK &&
        offset == size && state == 0u);
    assert(kernel_area_dirty_scan(area, size, &offset, &state) == KERNEL_AREA_OK &&
        offset == size && state == 0u);
    assert(kernel_area_unmap(320u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_pager_writeback_preserves_failed_and_newer_data(void)
{
    KernelAddressSpace space = {0};
    KernelArea *area = NULL;
    uint8_t snapshot[KERNEL_PAGE_SIZE];
    uint32_t base, size, physical, released, token, newer, epoch;
    uint32_t handles, mappings, children;

    initialize_test();
    pager_release_count = 0u;
    assert(kernel_vm_create_address_space(319u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(319u, 3u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, 0u) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 319u, &space, KERNEL_VM_READ, &base, &size) ==
           KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(NULL, 0u) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_mark_dirty(area, 1u) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_mark_dirty(area, size) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_mark_dirty(area, 0u) == KERNEL_AREA_INVALID_STATE);
    memset(snapshot, 0x31, sizeof(snapshot));
    for (uint32_t offset = 0u; offset < size; offset += KERNEL_PAGE_SIZE)
        assert(kernel_area_publish_page(area, offset, snapshot,
                                         sizeof(snapshot)) == KERNEL_AREA_OK);
    token = 123u;
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) ==
           KERNEL_AREA_INVALID_STATE && token == 0u);
    assert(kernel_area_writeback_begin(area, 0u, NULL, &token) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_writeback_begin(area, 0u, snapshot, NULL) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(children == 1u);
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot,
                                       &token) == KERNEL_AREA_OK && token != 0u);
    for (uint32_t index = 0u; index < sizeof(snapshot); ++index)
        assert(snapshot[index] == 0x31u);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_page_physical(area, KERNEL_PAGE_SIZE, &physical));
    memset(kernel_memory_access(physical, KERNEL_PAGE_SIZE), 0x52,
           KERNEL_PAGE_SIZE);
    snapshot[0] = 0xabu;
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot,
                                       &newer) == KERNEL_AREA_BUSY && newer == 0u);
    assert(snapshot[0] == 0xabu);
    epoch = kernel_area_pager_epoch(area);
    assert(kernel_area_reclaim(area, 0u, size, &released) == KERNEL_AREA_BUSY &&
           released == 0u && kernel_area_page_resident(area, 0u) &&
           epoch == kernel_area_pager_epoch(area));
    /* Reclaiming an unrelated clean page must not stale an in-flight token. */
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK && released == 1u);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token + 1u,
                                          true) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token,
                                          true) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, KERNEL_PAGE_SIZE, KERNEL_PAGE_SIZE,
                               &released) == KERNEL_AREA_BUSY);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token,
                                          true) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot,
                                       &newer) == KERNEL_AREA_OK && newer != token);
    for (uint32_t index = 0u; index < sizeof(snapshot); ++index)
        assert(snapshot[index] == 0x52u);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, newer,
                                          false) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, KERNEL_PAGE_SIZE, KERNEL_PAGE_SIZE,
                               &released) == KERNEL_AREA_BUSY);
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot,
                                       &token) == KERNEL_AREA_OK && token != newer);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, newer,
                                          true) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token,
                                          true) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(children == 0u);
    assert(kernel_area_reclaim(area, KERNEL_PAGE_SIZE, KERNEL_PAGE_SIZE,
                               &released) == KERNEL_AREA_OK && released == 1u);
    assert(kernel_area_publish_page(area, KERNEL_PAGE_SIZE, snapshot,
                                     sizeof(snapshot)) == KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot,
                                       &newer) == KERNEL_AREA_OK && newer != token);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token,
                                          true) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, newer,
                                          true) == KERNEL_AREA_OK);
    assert(kernel_area_pool_valid());
    assert(kernel_area_unmap(319u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(pager_release_count == 1u);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_pager_writable_views_redirty_after_snapshot(bool publish_first)
{
    KernelAddressSpace owner = {0}, reader = {0};
    KernelArea *area = NULL;
    uint8_t page[KERNEL_PAGE_SIZE], snapshot[KERNEL_PAGE_SIZE];
    uint32_t base, read_base, size, physical, token, newer, released;
    const uint32_t offset = KERNEL_VM_SHARED_TABLE_SPAN;

    initialize_test();
    assert(kernel_vm_create_address_space(380u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(381u, &reader) == KERNEL_VM_OK);
    assert(kernel_area_create(380u, offset + KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    memset(page, 0x31, sizeof(page));
    if (publish_first)
        assert(kernel_area_publish_page(area, offset, page, sizeof(page)) == KERNEL_AREA_OK);
    assert(kernel_area_map_range(area, 380u, &owner,
        KERNEL_VM_READ | KERNEL_VM_WRITE, offset, KERNEL_PAGE_SIZE,
        &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_map_range(area, 381u, &reader, KERNEL_VM_READ, offset,
        KERNEL_PAGE_SIZE, &read_base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_pool_valid());
    KernelAreaPagerRequest request;
    assert(kernel_area_pager_request(381u, &reader, read_base, true, &request) ==
           KERNEL_AREA_NOT_MAPPED && request.area == NULL);
    if (!publish_first) {
        assert(kernel_area_pager_request(380u, &owner, base, true, &request) == KERNEL_AREA_OK &&
               request.offset == offset);
        assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
        assert(kernel_area_publish_page(area, offset, page, sizeof(page)) == KERNEL_AREA_OK);
    }
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(!kernel_area_fault(380u, &owner, base + 17u, false));
    assert(kernel_area_writeback_begin(area, offset, snapshot, &token) ==
           KERNEL_AREA_INVALID_STATE);
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, 1u);
    assert(!kernel_area_fault(380u, &owner, base + 17u, true));
    kernel_allocation_test_clear_failure();
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_area_pool_valid());
    assert(kernel_area_fault(380u, &owner, base + 17u, true));
    assert(kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_area_pool_valid());
    memset(kernel_memory_access(physical, sizeof(page)), 0x52, sizeof(page));
    assert(!kernel_area_fault(381u, &reader, read_base + 17u, true));
    assert(!kernel_area_fault(380u, &reader, base + 17u, true));
    assert(kernel_vm_switch(&reader) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(read_base, true, &physical));
    assert(kernel_vm_test_translate_current(read_base, false, &physical));
    assert(kernel_area_writeback_begin(area, offset, snapshot, &token) == KERNEL_AREA_OK);
    assert(snapshot[0] == 0x52u && snapshot[sizeof(snapshot) - 1u] == 0x52u);
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_area_fault(380u, &owner, base + 17u, true));
    assert(kernel_vm_test_translate_current(base, true, &physical));
    memset(kernel_memory_access(physical, sizeof(page)), 0x73, sizeof(page));
    assert(snapshot[0] == 0x52u && snapshot[sizeof(snapshot) - 1u] == 0x52u);
    assert(kernel_area_writeback_complete(area, offset, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_pool_valid());
    assert(kernel_area_reclaim(area, offset, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_BUSY);
    assert(kernel_area_writeback_begin(area, offset, snapshot, &newer) == KERNEL_AREA_OK &&
           newer != token && snapshot[0] == 0x73u);
    assert(kernel_area_writeback_complete(area, offset, newer, true) == KERNEL_AREA_OK);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(380u, &owner, base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(381u, &reader, read_base) == KERNEL_AREA_OK);
    assert(kernel_area_map_range(area, 380u, &owner,
        KERNEL_VM_READ | KERNEL_VM_WRITE, offset, KERNEL_PAGE_SIZE,
        &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_map_range(area, 381u, &reader, KERNEL_VM_READ, offset,
        KERNEL_PAGE_SIZE, &read_base, &size) == KERNEL_AREA_OK);
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_area_reclaim(area, offset, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_OK &&
           released == 1u);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(380u, &owner, base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(381u, &reader, read_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&reader) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_pager_dirty_lifetime_pressure_and_allocation_rollback(void)
{
    KernelArea *area = NULL;
    KernelAreaReclaimCursor cursor = {0};
    KernelAllocationStats before, after;
    uint8_t snapshot[KERNEL_PAGE_SIZE] = {0};
    uint32_t released, total, token, handles, mappings, children;
    const uint32_t far = (KERNEL_AREA_PAGE_MAX - 1u) * KERNEL_PAGE_SIZE;

    initialize_test();
    pager_release_count = 0u;
    assert(kernel_area_create(320u, far + KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_publish_page(area, 0u, snapshot, sizeof(snapshot)) ==
           KERNEL_AREA_OK);
    assert(kernel_area_publish_page(area, far, snapshot, sizeof(snapshot)) ==
           KERNEL_AREA_OK);
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
                                        &before));
    for (uint32_t reference = 0u; reference < UINT16_MAX; ++reference)
        assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, 0u) == KERNEL_AREA_BUSY);
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
                                        &after));
    assert(after.current_units == before.current_units);
    for (uint32_t reference = 0u; reference < UINT16_MAX; ++reference)
        assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
    for (uint32_t attempt = 1u; attempt <= 2u; ++attempt) {
        kernel_allocation_test_fail_site(
            KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, attempt);
        assert(kernel_area_mark_dirty(area, 0u) == KERNEL_AREA_OUT_OF_MEMORY);
        kernel_allocation_test_clear_failure();
        assert(kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, &after));
        assert(after.current_units == before.current_units);
        assert(kernel_area_references(area, &handles, &mappings, &children));
        assert(children == 0u);
        assert(kernel_area_pool_valid());
    }
    assert(kernel_area_mark_dirty(area, 0u) == KERNEL_AREA_OK);
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
                                        &before));
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, 1u);
    assert(kernel_area_mark_dirty(area, far) == KERNEL_AREA_OUT_OF_MEMORY);
    kernel_allocation_test_clear_failure();
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
                                        &after));
    assert(after.current_units == before.current_units);
    assert(kernel_area_mark_dirty(area, far) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, 0u, far + KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_BUSY && released == 0u && kernel_area_page_resident(area, 0u));
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(children == 1u);
    assert(kernel_area_writeback_begin(area, far, snapshot, &token) ==
           KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, far, token, true) ==
           KERNEL_AREA_OK);
    /* A skipped metadata hole must not skip the next leaf's first page. */
    const uint32_t middle = 2048u * KERNEL_PAGE_SIZE;
    assert(kernel_area_publish_page(area, middle, snapshot, sizeof(snapshot)) == KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, middle) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, middle / 2u, middle / 2u + KERNEL_PAGE_SIZE,
                               &released) == KERNEL_AREA_BUSY && released == 0u);
    assert(kernel_area_writeback_begin(area, middle, snapshot, &token) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, middle, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_reclaim(area, middle / 2u, middle / 2u + KERNEL_PAGE_SIZE,
                               &released) == KERNEL_AREA_OK && released == 1u);
    assert(kernel_area_page_resident(area, 0u) && kernel_area_page_resident(area, far));
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(children == 1u);
    assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
    total = 0u;
    do {
        assert(kernel_area_reclaim_pager_pages(&cursor, 64u, &released) ==
               KERNEL_AREA_OK);
        total += released;
    } while (cursor.remaining_slots != 0u);
    assert(total == 0u);
    assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_live(area) && pager_release_count == 0u);
    total = 0u;
    do {
        assert(kernel_area_reclaim_pager_pages(&cursor, 64u, &released) ==
               KERNEL_AREA_OK);
        total += released;
    } while (cursor.remaining_slots != 0u);
    assert(total == 1u && kernel_area_page_resident(area, 0u) &&
           !kernel_area_page_resident(area, far));
    assert(kernel_area_pool_valid());
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) ==
           KERNEL_AREA_OK);
    total = 0u;
    do {
        assert(kernel_area_reclaim_pager_pages(&cursor, 64u, &released) ==
               KERNEL_AREA_OK);
        total += released;
    } while (cursor.remaining_slots != 0u);
    assert(total == 0u && kernel_area_page_resident(area, 0u));
    assert(kernel_area_writeback_complete(area, 0u, token, false) ==
           KERNEL_AREA_OK && kernel_area_live(area));
    assert(kernel_area_pool_valid());
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) ==
           KERNEL_AREA_OK);
    assert(kernel_area_writeback_complete(area, 0u, token, true) ==
           KERNEL_AREA_OK);
    assert(!kernel_area_live(area) && pager_release_count == 1u);
    assert(kernel_area_pool_valid());
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
                                        &after));
    assert(after.current_units == 0u);
}

static void test_pager_area_never_zero_fills_missing_file_data(void)
{
    KernelAddressSpace client = {0};
    KernelAddressSpace peer = {0};
    KernelArea *area = NULL;
    KernelArea *other = NULL;
    KernelAreaPagerRequest request;
    KernelAreaReclaimCursor cursor = {0};
    uint32_t base, peer_base, other_base, size, released, physical;
    uint32_t total;
    uint8_t byte = 0xa5u;

    initialize_test();
    pager_release_count = 0u;
    assert(kernel_vm_create_address_space(317u, &client) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(318u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(317u, 2u * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map(area, 317u, &client, KERNEL_VM_READ, &base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_create(317u, KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &other) ==
           KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(other, (KernelPort *)(uintptr_t)1u,
                                  2u, release_test_pager,
                                  (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(other, 317u, &client, KERNEL_VM_READ,
                           &other_base, &size) == KERNEL_AREA_OK);
    assert(other_base != base);
    assert(kernel_area_pager_request(317u, &client, other_base, false,
                                     &request) == KERNEL_AREA_OK);
    assert(request.area == other && request.cookie == 2u &&
           request.offset == 0u);
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u,
                                  1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) ==
           KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_unmap(317u, &client, base) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u,
                                  0u, release_test_pager,
                                  (void *)(uintptr_t)2u) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u,
                                  1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u,
                                  1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) ==
           KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_map(area, 317u, &client, KERNEL_VM_READ, &base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 318u, &peer, KERNEL_VM_READ, &peer_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_pager_request(318u, &client, base, false, &request) ==
           KERNEL_AREA_NOT_MAPPED && request.area == NULL);
    assert(kernel_area_pager_request(317u, &client, base + 17u, false,
                                     &request) == KERNEL_AREA_OK);
    assert(request.area == area &&
           request.port == (KernelPort *)(uintptr_t)1u &&
           request.cookie == 1u && request.offset == 0u);
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(kernel_area_pager_request(317u, &client,
                                     base + KERNEL_PAGE_SIZE + 5u, false,
                                     &request) == KERNEL_AREA_OK);
    assert(request.offset == KERNEL_PAGE_SIZE);
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(!kernel_area_fault(317u, &client, base, false));
    assert(kernel_area_read(area, 0u, &byte, 1u) ==
           KERNEL_AREA_INVALID_STATE);
    assert(byte == 0xa5u);
    assert(kernel_area_write(area, 0u, &byte, 1u) ==
           KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_decommit(317u, &client, base, KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_publish_page(area, 0u, &byte, 1u) == KERNEL_AREA_OK);
    assert(kernel_vm_switch(&client) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base, false, &physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(peer_base, false, &physical));
    assert(kernel_area_pager_request(317u, &client, base, false, &request) ==
           KERNEL_AREA_NOT_MAPPED && request.area == NULL);
    byte = 0u;
    assert(kernel_area_read(area, 0u, &byte, 1u) == KERNEL_AREA_OK);
    assert(byte == 0xa5u);
    assert(kernel_area_reclaim(other, 0u, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK && released == 0u);
    assert(kernel_area_reclaim(area, 1u, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_INVALID_ARGUMENT && released == 0u);
    assert(kernel_area_reclaim(area, 0u, 0u, &released) ==
           KERNEL_AREA_INVALID_ARGUMENT && released == 0u);
    assert(kernel_area_pager_request(317u, &client,
                                     base + KERNEL_PAGE_SIZE, false, &request) ==
           KERNEL_AREA_OK);
    assert(request.epoch == kernel_area_pager_epoch(area));
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK && released == 1u);
    assert(request.epoch != kernel_area_pager_epoch(area));
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    {
        uint8_t across[2] = {0x7eu, 0x7eu};

        assert(kernel_area_read(area, KERNEL_PAGE_SIZE - 1u, across,
                                sizeof(across)) == KERNEL_AREA_INVALID_STATE);
        assert(across[0] == 0x7eu && across[1] == 0x7eu);
    }
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK && released == 0u);
    assert(kernel_vm_switch(&client) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, false, &physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(peer_base, false, &physical));
    assert(kernel_area_read(area, 0u, &byte, 1u) ==
           KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_pager_request(317u, &client, base, false, &request) ==
           KERNEL_AREA_OK && request.offset == 0u);
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(kernel_area_pager_request(318u, &peer, peer_base, false, &request) ==
           KERNEL_AREA_OK && request.offset == 0u);
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(kernel_area_publish_page(area, 0u, &byte, 1u) == KERNEL_AREA_OK);
    assert(kernel_area_pager_request(317u, &client,
                                    base + KERNEL_PAGE_SIZE, false, &request) ==
           KERNEL_AREA_OK);
    total = 0u;
    for (uint32_t at = 0u; at < 32u; ++at) {
        assert(kernel_area_reclaim_pager_pages(&cursor, 1u, &released) ==
               KERNEL_AREA_OK);
        total += released;
        if (cursor.remaining_slots == 0u) break;
        assert(at != 31u);
    }
    assert(total == 0u && kernel_area_page_resident(area, 0u));
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    cursor = (KernelAreaReclaimCursor){0};
    total = 0u;
    for (uint32_t at = 0u; at < 32u; ++at) {
        assert(kernel_area_reclaim_pager_pages(&cursor, 1u, &released) ==
               KERNEL_AREA_OK);
        total += released;
        if (cursor.remaining_slots == 0u) break;
        assert(at != 31u);
    }
    assert(total == 1u && !kernel_area_page_resident(area, 0u));
    assert(kernel_vm_switch(&client) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, false, &physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(peer_base, false, &physical));
    assert(kernel_area_pool_valid());
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap(317u, &client, base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(318u, &peer, peer_base) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(317u, &client, other_base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    kernel_area_handle_release(other, NULL);
    assert(pager_release_count == 2u);
    assert(kernel_vm_destroy_address_space(&client) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
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
    assert(!kernel_area_fault(321u, &stranger, base, false));
    /* Right process, but an address space it does not hold the area in. */
    assert(!kernel_area_fault(320u, &stranger, base, false));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);

    /* An address outside every area slot is not this path's business. */
    assert(!kernel_area_fault(320u, &owner, KERNEL_VM_AREA_BASE - 4u, false));
    /* Nor is one past the end of the reservation. */
    assert(!kernel_area_fault(320u, &owner, base + 64u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);

    /* An ordinary area is already whole and never takes this path. */
    assert(kernel_area_create(320u, 2u * KERNEL_PAGE_SIZE, 0u, &eager) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map(eager, 320u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &eager_base,
                           &size) == KERNEL_AREA_OK);
    assert(!kernel_area_fault(320u, &owner, eager_base, false));
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
    const uint32_t reserved_pages = 256u;

    initialize_test();
    assert(kernel_vm_create_address_space(340u, &owner) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(341u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(340u, reserved_pages * KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED,
                              &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 340u, &owner,
                           KERNEL_VM_READ | KERNEL_VM_WRITE, &base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 341u, &peer, KERNEL_VM_READ, &peer_base,
                           &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(340u, &owner, base, false));
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
     * The cluster. The span's shared table stays attached to both holders
     * until the area itself goes, so a recommit is one descriptor.
     */
    assert(after.free_frames - before.free_frames == cluster);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == 0u);
    assert(snapshot.page_count == reserved_pages);
    assert(snapshot.byte_size == reserved_pages * KERNEL_PAGE_SIZE);

    /* Gone from every holder, not merely from the one that asked. */
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(base, true, &physical));
    assert(kernel_vm_switch(&peer) == KERNEL_VM_OK);
    assert(!kernel_vm_test_translate_current(peer_base, false, &physical));
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);

    /* And the reservation still works: touching it again re-commits. */
    assert(kernel_area_fault(340u, &owner, base, false));
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
    assert(kernel_area_fault(350u, &owner, base + 18u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages - cluster);
    assert(kernel_vm_switch(&owner) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base + cluster * KERNEL_PAGE_SIZE,
                                            true, &physical));
    assert(kernel_vm_test_translate_current(
        base + (pages - 1u) * KERNEL_PAGE_SIZE, true, &physical));

    /* Now a full cluster, then a hole in the middle of it. */
    assert(kernel_area_fault(350u, &owner, base, false));
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
    assert(kernel_area_fault(350u, &owner, base + 5u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == pages);
    assert(kernel_vm_test_translate_current(base + 5u * KERNEL_PAGE_SIZE,
                                            true, &physical));

    /* A wider hole, refilled from a fault in its middle rather than its end. */
    assert(kernel_area_decommit(350u, &owner, base + 8u * KERNEL_PAGE_SIZE,
                                3u * KERNEL_PAGE_SIZE, &released) ==
           KERNEL_AREA_OK);
    assert(released == 3u);
    assert(kernel_area_fault(350u, &owner, base + 9u * KERNEL_PAGE_SIZE, false));
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
 * A reservation is address space, not promised RAM. Commit until physical
 * memory's protected reserve refuses the unprotected owner, and prove that
 * the failed commit leaves the reservation and prior pages intact.
 */
static void test_reserved_area_commit_stops_at_physical_memory(void)
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

    /* Commit until actual memory policy says no. */
    while (page < KERNEL_AREA_PAGE_MAX) {
        if (!kernel_area_fault(360u, &owner,
                               base + page * KERNEL_PAGE_SIZE, false))
            break;
        committed += KERNEL_AREA_COMMIT_CLUSTER_PAGES;
        page += KERNEL_AREA_COMMIT_CLUSTER_PAGES;
    }
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.committed_pages == committed);
    assert(committed < KERNEL_AREA_PAGE_MAX);
    /* Refusing leaves the reservation and everything already there alone. */
    assert(snapshot.page_count == KERNEL_AREA_PAGE_MAX);
    assert(kernel_area_pool_valid());

    assert(kernel_area_unmap(360u, &owner, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&owner) == KERNEL_VM_OK);
}

static void test_area_subrange_maps_distinct_source_spans(void)
{
    KernelAddressSpace space;
    KernelArea *area;
    uint32_t first, second, whole, size, physical, expected, released;
    uint8_t value = 0x6bu;

    initialize_test();
    assert(kernel_vm_create_address_space(370u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(370u,
                              KERNEL_VM_SHARED_TABLE_SPAN + KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_area_map_range(area, 370u, &space, KERNEL_VM_READ,
                                 1u, KERNEL_VM_SHARED_TABLE_SPAN,
                                 &first, &size) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_map_range(area, 370u, &space, KERNEL_VM_READ,
                                 0u, KERNEL_PAGE_SIZE, &first, &size) ==
           KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_map_range(area, 370u, &space, KERNEL_VM_READ,
                                 KERNEL_VM_SHARED_TABLE_SPAN,
                                 KERNEL_PAGE_SIZE, &first,
                                 &size) == KERNEL_AREA_OK);
    assert(size == KERNEL_PAGE_SIZE);
    assert(kernel_area_map_range(area, 370u, &space, KERNEL_VM_READ,
                                 0u, KERNEL_VM_SHARED_TABLE_SPAN,
                                 &second, &size) == KERNEL_AREA_OK);
    assert(second != first);
    assert(kernel_area_write(area, KERNEL_VM_SHARED_TABLE_SPAN,
                             &value, 1u) == KERNEL_AREA_OK);
    assert(kernel_area_page_physical(area, KERNEL_VM_SHARED_TABLE_SPAN,
                                     &expected));
    assert(kernel_area_map(area, 370u, &space, KERNEL_VM_READ,
                           &whole, &size) == KERNEL_AREA_OK);
    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(first, false, &physical));
    assert(physical == expected);
    assert(kernel_vm_test_translate_current(
        whole + KERNEL_VM_SHARED_TABLE_SPAN, false, &physical));
    assert(physical == expected);
    assert(!kernel_area_fault(370u, &space, first + KERNEL_PAGE_SIZE, false));
    assert(!kernel_vm_test_translate_current(second, false, &physical));
    assert(kernel_area_fault(370u, &space, second, false));
    assert(kernel_area_decommit(370u, &space, first, KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_OK);
    assert(released == 1u);
    assert(kernel_area_fault(370u, &space, first, false));
    assert(kernel_area_page_physical(area, KERNEL_VM_SHARED_TABLE_SPAN,
                                     &expected));
    assert(kernel_vm_test_translate_current(first, false, &physical));
    assert(physical == expected);
    assert(kernel_vm_test_translate_current(
        whole + KERNEL_VM_SHARED_TABLE_SPAN, false, &physical));
    assert(physical == expected);
    assert(kernel_area_unmap(370u, &space, first) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(370u, &space, second) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(370u, &space, whole) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
}

static void test_area_subranges_map_at_adjacent_slots(void)
{
    KernelAddressSpace space;
    KernelArea *first_area, *second_area;
    const uint32_t span = KERNEL_VM_SHARED_TABLE_SPAN;
    const uint32_t target = KERNEL_VM_AREA_BASE + 4u * span;
    uint32_t first, second, alias, size, expected, physical;
    uint8_t value = 0x42u;

    initialize_test();
    assert(kernel_vm_create_address_space(371u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(371u, span, KERNEL_AREA_CREATE_RESERVED,
                              &first_area) == KERNEL_AREA_OK);
    assert(kernel_area_create(371u, span, KERNEL_AREA_CREATE_RESERVED,
                              &second_area) == KERNEL_AREA_OK);
    assert(kernel_area_map_range_at(first_area, 371u, &space, KERNEL_VM_READ,
                                    0u, span, target + 1u, &first,
                                    &size) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_map_range_at(first_area, 371u, &space, KERNEL_VM_READ,
                                    0u, span, target, &first,
                                    &size) == KERNEL_AREA_OK);
    assert(first == target && size == span);
    assert(kernel_area_map_range_at(first_area, 371u, &space, KERNEL_VM_READ,
                                    0u, span, target, &alias,
                                    &size) == KERNEL_AREA_ALREADY_MAPPED);
    assert(kernel_area_map_range_at(second_area, 371u, &space,
                                    KERNEL_VM_READ, 0u, span, target + span,
                                    &second, &size) == KERNEL_AREA_OK);
    assert(second == first + span);
    assert(kernel_area_map_range_at(first_area, 371u, &space, KERNEL_VM_READ,
                                    0u, span, second, &alias,
                                    &size) == KERNEL_AREA_ALREADY_MAPPED);
    assert(kernel_area_map_range_at(first_area, 371u, &space, KERNEL_VM_READ,
                                    0u, span, target + 2u * span,
                                    &alias, &size) == KERNEL_AREA_OK);
    assert(alias == target + 2u * span);
    assert(kernel_area_write(first_area, 0u, &value, 1u) == KERNEL_AREA_OK);
    assert(kernel_area_page_physical(first_area, 0u, &expected));
    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(first, false, &physical));
    assert(physical == expected);
    assert(kernel_vm_test_translate_current(alias, false, &physical));
    assert(physical == expected);
    assert(kernel_area_unmap(371u, &space, alias) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(371u, &space, second) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(371u, &space, first) == KERNEL_AREA_OK);
    kernel_area_handle_release(second_area, NULL);
    kernel_area_handle_release(first_area, NULL);
    assert(kernel_area_pool_valid());
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
}

static void test_private_pager_views_keep_source_and_fork_isolation(void)
{
    KernelAddressSpace parent = {0}, child = {0}, survivor = {0};
    KernelArea *area;
    KernelAllocationStats allocation;
    KernelFrameInfo frame;
    uint32_t base, alias, shared, readonly, size, source, copy, translated, released;
    uint8_t bytes[KERNEL_PAGE_SIZE] = {0x37u};

    initialize_test();
    assert(kernel_vm_create_address_space(381u, &parent) == KERNEL_VM_OK);
    assert(kernel_area_create(383u, KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
                                  release_test_pager,
                                  (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 381u, &parent,
                           KERNEL_VM_READ | KERNEL_VM_WRITE | ASTRA_AREA_MAP_PRIVATE,
                           &base, &size) == KERNEL_AREA_OK);
    assert(!kernel_area_fault(381u, &parent, base, true));
    assert(kernel_area_publish_page(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_page_physical(area, 0u, &source));
    assert(kernel_area_decommit(381u, &parent, base, KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_ACCESS_DENIED);
    assert(kernel_area_fault(381u, &parent, base, false));
    assert(kernel_vm_probe_address_space(&parent, base, &translated) == KERNEL_VM_MAPPING_READ_ONLY);
    assert(translated == source);
    kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, 1u);
    assert(!kernel_area_fault(381u, &parent, base, true));
    kernel_allocation_test_clear_failure();
    assert(kernel_vm_probe_address_space(&parent, base, &translated) == KERNEL_VM_MAPPING_READ_ONLY);
    assert(translated == source && kernel_area_pool_valid());
    assert(kernel_area_fault(381u, &parent, base, true));
    assert(kernel_vm_probe_address_space(&parent, base, &copy) == KERNEL_VM_MAPPING_READ_WRITE);
    assert(copy != source && physical_memory[copy - RAM_BASE] == 0x37u);
    physical_memory[copy - RAM_BASE] = 0x91u;
    assert(physical_memory[source - RAM_BASE] == 0x37u);
    assert(kernel_area_map_range_at(area, 381u, &parent,
                                    KERNEL_VM_READ | KERNEL_VM_WRITE | ASTRA_AREA_MAP_PRIVATE,
                                    0u, KERNEL_PAGE_SIZE,
                                    base + KERNEL_VM_SHARED_TABLE_SPAN,
                                    &alias, &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(381u, &parent, alias, false));
    assert(kernel_area_map(area, 381u, &parent, KERNEL_VM_READ,
                           &shared, &size) == KERNEL_AREA_OK);
    assert(shared != base && shared != alias);
    assert(kernel_memory_frame_info(source, &frame));
    assert(kernel_vm_copy_shared_page(&parent, shared, source, frame.owner) ==
           KERNEL_VM_NOT_OWNED);
    assert(kernel_vm_probe_address_space(&parent, shared, &translated) == KERNEL_VM_MAPPING_READ_ONLY);
    assert(translated == source);
    assert(kernel_area_map_range_at(area, 381u, &parent,
                                    KERNEL_VM_READ | ASTRA_AREA_MAP_PRIVATE,
                                    0u, KERNEL_PAGE_SIZE,
                                    base + 3u * KERNEL_VM_SHARED_TABLE_SPAN,
                                    &readonly, &size) == KERNEL_AREA_OK);
    assert(!kernel_area_fault(381u, &parent, readonly, true));
    assert(kernel_area_fault(381u, &parent, readonly, false));
    assert(!kernel_area_fault(381u, &parent, readonly, true));
    assert(kernel_vm_clone_address_space(&parent, 382u, &child) == KERNEL_VM_OK);
    assert(kernel_area_clone_process(381u, &parent, 382u, &child) == KERNEL_AREA_OK);
    assert(kernel_vm_clone_address_space(&parent, 384u, &survivor) == KERNEL_VM_OK);
    assert(kernel_area_clone_process(381u, &parent, 384u, &survivor) == KERNEL_AREA_OK);
    assert(kernel_vm_cow_fault(&child, base) == KERNEL_VM_OK);
    assert(kernel_vm_probe_address_space(&child, base, &translated) == KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated != copy && physical_memory[translated - RAM_BASE] == 0x91u);
    physical_memory[translated - RAM_BASE] = 0x52u;
    assert(physical_memory[copy - RAM_BASE] == 0x91u);
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_OK);
    assert(released == 1u);
    assert(kernel_vm_probe_address_space(&parent, alias, &translated) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_address_space(&child, alias, &translated) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_address_space(&child, base, &translated) == KERNEL_VM_MAPPING_READ_WRITE);
    assert(physical_memory[translated - RAM_BASE] == 0x52u);
    assert(kernel_area_publish_page(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_fault(382u, &child, alias, false));
    assert(kernel_area_process_died(381u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_cow_fault(&survivor, base) == KERNEL_VM_OK);
    assert(kernel_vm_probe_address_space(&survivor, base, &translated) == KERNEL_VM_MAPPING_READ_WRITE);
    assert(physical_memory[translated - RAM_BASE] == 0x91u);
    assert(kernel_vm_cow_fault(&child, base) == KERNEL_VM_NOT_OWNED);
    assert(kernel_area_process_died(382u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_process_died(384u, NULL, NULL) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&survivor) == KERNEL_VM_OK);
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE,
                                        &allocation));
    assert(allocation.current_units == 0u);
    assert(kernel_area_pool_valid());
}

static void test_backing_resize_revokes_private_copies_and_zeroes_tail(void)
{
    KernelAddressSpace parent = {0}, child = {0};
    KernelArea *area;
    KernelAreaPagerRequest request;
    KernelAllocationStats allocation;
    uint32_t base, shared, size, physical, copy, epoch, token, released;
    uint32_t handles, mappings, children;
    uint8_t bytes[KERNEL_PAGE_SIZE], snapshot[KERNEL_PAGE_SIZE];
    memset(bytes, 0x37, sizeof(bytes));
    initialize_test();
    assert(kernel_vm_create_address_space(381u, &parent) == KERNEL_VM_OK);
    assert(kernel_area_create(383u, 3u * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_resize_backing(area, 0u) == KERNEL_AREA_INVALID_STATE);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 381u, &parent,
        KERNEL_VM_READ | KERNEL_VM_WRITE | ASTRA_AREA_MAP_PRIVATE, &base, &size) == KERNEL_AREA_OK);
    for (uint32_t offset = 0u; offset < size; offset += KERNEL_PAGE_SIZE)
        assert(kernel_area_publish_page(area, offset, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 381u, &parent, KERNEL_VM_READ, &shared, &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(381u, &parent, base + KERNEL_PAGE_SIZE, true));
    assert(kernel_vm_probe_address_space(&parent, base + KERNEL_PAGE_SIZE, &copy) == KERNEL_VM_MAPPING_READ_WRITE);
    physical_memory[copy - RAM_BASE] = 0x91u;
    assert(kernel_area_fault(381u, &parent, base + 2u * KERNEL_PAGE_SIZE, true));
    assert(kernel_vm_clone_address_space(&parent, 382u, &child) == KERNEL_VM_OK);
    assert(kernel_area_clone_process(381u, &parent, 382u, &child) == KERNEL_AREA_OK);
    assert(kernel_vm_cow_fault(&child, base + 2u * KERNEL_PAGE_SIZE) == KERNEL_VM_OK);
    assert(kernel_area_reclaim(area, 2u * KERNEL_PAGE_SIZE, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_OK && released == 1u);
    epoch = kernel_area_pager_epoch(area);
    assert(kernel_area_resize_backing(area, size + 1u) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_begin(area, KERNEL_PAGE_SIZE, snapshot, &token) == KERNEL_AREA_OK);
    assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 9u) == KERNEL_AREA_BUSY);
    assert(kernel_area_pager_epoch(area) == epoch && kernel_area_backing_bytes(area) == size);
    assert(kernel_vm_probe_address_space(&child, base + 2u * KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_READ_WRITE);
    assert(kernel_area_writeback_complete(area, KERNEL_PAGE_SIZE, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_mark_dirty(area, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 9u) == KERNEL_AREA_OK);
    assert(kernel_area_pager_epoch(area) != epoch);
    epoch = kernel_area_pager_epoch(area);
    assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 9u) == KERNEL_AREA_OK && kernel_area_pager_epoch(area) == epoch);
    assert(kernel_area_read(area, KERNEL_PAGE_SIZE + 9u, snapshot, 1u) == KERNEL_AREA_OK && snapshot[0] == 0u);
    assert(physical_memory[copy - RAM_BASE] == 0x91u && physical_memory[copy - RAM_BASE + 9u] == 0u);
    assert(kernel_vm_probe_address_space(&child, base + KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_READ_ONLY && physical == copy);
    assert(kernel_vm_probe_address_space(&parent, base + 2u * KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_address_space(&child, base + 2u * KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_area_page_request(area, 2u * KERNEL_PAGE_SIZE, &request) == KERNEL_AREA_NOT_MAPPED);
    memset(bytes, 0x55, sizeof(bytes));
    assert(kernel_area_publish_dirty_page(area, 2u * KERNEL_PAGE_SIZE, bytes) == KERNEL_AREA_OK);
    assert(!kernel_area_page_resident(area, 2u * KERNEL_PAGE_SIZE));
    assert(kernel_vm_probe_address_space(&parent, shared + 2u * KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(!kernel_area_fault(381u, &parent, base + 2u * KERNEL_PAGE_SIZE, true));
    assert(kernel_area_resize_backing(area, size) == KERNEL_AREA_OK);
    assert(kernel_vm_probe_address_space(&parent, shared + 2u * KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_READ_ONLY);
    assert(kernel_area_fault(381u, &parent, base + 2u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_resize_backing(area, 0u) == KERNEL_AREA_OK);
    assert(kernel_area_references(area, &handles, &mappings, &children) && handles == 1u && mappings == 4u && children == 0u);
    assert(kernel_area_backing_bytes(area) == 0u && !kernel_area_fault(381u, &parent, base, false));
    assert(kernel_area_process_died(381u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_process_died(382u, NULL, NULL) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    assert(kernel_allocation_site_stats(KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, &allocation) && allocation.current_units == 0u);
    assert(kernel_area_pool_valid());
}

static void test_staged_backing_pages_stay_hidden_until_growth(void)
{
    KernelAddressSpace space = {0};
    KernelArea *area;
    uint32_t base, size, physical;
    uint8_t bytes[KERNEL_PAGE_SIZE];
    memset(bytes, 0x55, sizeof(bytes));
    initialize_test();
    assert(kernel_vm_create_address_space(381u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(383u, 2u * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_resize_backing(area, 0u) == KERNEL_AREA_OK);
    assert(kernel_area_publish_page(area, KERNEL_PAGE_SIZE, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 381u, &space, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    assert(kernel_vm_probe_address_space(&space, base + KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 1u) == KERNEL_AREA_OK);
    assert(kernel_vm_probe_address_space(&space, base + KERNEL_PAGE_SIZE, &physical) == KERNEL_VM_MAPPING_READ_ONLY);
    assert(physical_memory[physical - RAM_BASE] == 0x55u && physical_memory[physical - RAM_BASE + 1u] == 0u);
    assert(kernel_area_unmap(381u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK && kernel_area_pool_valid());
}

static void test_private_anonymous_view_cannot_decommit_source(void)
{
    KernelAddressSpace space = {0};
    KernelArea *area;
    uint32_t base, size, released;
    uint8_t byte = 0x62u, observed = 0u;

    initialize_test();
    assert(kernel_vm_create_address_space(385u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(385u, KERNEL_PAGE_SIZE,
                              KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 385u, &space,
                           KERNEL_VM_READ | KERNEL_VM_WRITE | ASTRA_AREA_MAP_PRIVATE,
                           &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_fault(385u, &space, base, true));
    assert(kernel_area_decommit(385u, &space, base, KERNEL_PAGE_SIZE,
                                &released) == KERNEL_AREA_ACCESS_DENIED);
    assert(released == 0u);
    assert(kernel_area_write(area, 0u, &byte, 1u) == KERNEL_AREA_OK);
    assert(kernel_area_read(area, 0u, &observed, 1u) == KERNEL_AREA_OK && observed == byte);
    assert(kernel_area_unmap(385u, &space, base) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_mappings_can_own_backing_lifetime(void)
{
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        KernelAddressSpace parent = {0}, peer = {0};
        KernelArea *area;
        KernelAreaPoolStats stats;
        uint32_t base, alias, size, handles, mappings, children;
        uint32_t flags = KERNEL_AREA_CREATE_MAPPED_LIFETIME |
            ((mode & 1u) != 0u ? KERNEL_AREA_CREATE_RESERVED : 0u);
        uint32_t permissions = KERNEL_VM_READ | KERNEL_VM_WRITE |
            ((mode & 2u) != 0u ? ASTRA_AREA_MAP_PRIVATE : 0u);
        uint8_t value = 0x57u, observed = 0xffu;

        initialize_test();
        assert(kernel_vm_create_address_space(390u, &parent) == KERNEL_VM_OK);
        assert(kernel_vm_create_address_space(391u, &peer) == KERNEL_VM_OK);
        assert(kernel_area_create(390u, KERNEL_PAGE_SIZE, flags, &area) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 390u, &parent, permissions, &base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 391u, &peer, permissions, &alias, &size) == KERNEL_AREA_OK);
        if (mode != 0u) assert(kernel_area_fault(390u, &parent, base, true));
        assert(kernel_vm_write(&parent, base, &value, 1u) == KERNEL_VM_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_area_live(area));
        assert(kernel_area_references(area, &handles, &mappings, &children));
        assert(handles == 0u && mappings == 2u && children == 0u);
        if ((mode & 2u) != 0u)
            assert(kernel_area_fault(391u, &peer, alias, false));
        assert(kernel_vm_read(&peer, alias, &observed, 1u) == KERNEL_VM_OK);
        assert(observed == ((mode & 2u) != 0u ? 0u : value));
        assert(kernel_area_process_died(390u, NULL, NULL) == KERNEL_AREA_OK);
        assert(kernel_area_live(area));
        assert(kernel_area_unmap(391u, &peer, alias) == KERNEL_AREA_OK);
        assert(!kernel_area_live(area));
        assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
        assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
        assert(kernel_area_pool_stats(&stats));
        assert(stats.active_areas == 0u && stats.active_mappings == 0u);
        assert(kernel_area_pool_valid());
    }
}

static void test_mapping_lifetime_survives_fork_and_child_pin(void)
{
    for (uint32_t release_first = 0u; release_first < 2u; ++release_first) {
        KernelAddressSpace parent = {0}, child = {0};
        KernelArea *area;
        KernelAreaPoolStats stats;
        uint32_t base, size, physical;
        uint8_t value = 0x41u, observed = 0u;

        initialize_test();
        assert(kernel_vm_create_address_space(392u, &parent) == KERNEL_VM_OK);
        assert(kernel_area_create(392u, KERNEL_PAGE_SIZE,
            KERNEL_AREA_CREATE_RESERVED | KERNEL_AREA_CREATE_MAPPED_LIFETIME,
            &area) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 392u, &parent,
            KERNEL_VM_READ | KERNEL_VM_WRITE | ASTRA_AREA_MAP_PRIVATE,
            &base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_fault(392u, &parent, base, true));
        assert(kernel_vm_write(&parent, base, &value, 1u) == KERNEL_VM_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_area_live(area));
        assert(kernel_vm_clone_address_space(&parent, 393u, &child) == KERNEL_VM_OK);
        assert(kernel_area_clone_process(392u, &parent, 393u, &child) == KERNEL_AREA_OK);
        assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
        assert(kernel_area_unmap_process(392u, NULL) == KERNEL_AREA_OK);
        assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
        assert(kernel_vm_cow_fault(&child, base) == KERNEL_VM_OK);
        assert(kernel_vm_probe_address_space(&child, base, &physical) == KERNEL_VM_MAPPING_READ_WRITE);
        assert(kernel_vm_read(&child, base, &observed, 1u) == KERNEL_VM_OK && observed == value);
        if (release_first) assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
        assert(kernel_area_unmap_process(393u, NULL) == KERNEL_AREA_OK);
        assert(kernel_area_live(area) == (release_first == 0u));
        if (!release_first) assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
        assert(!kernel_area_live(area));
        assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
        assert(kernel_area_pool_stats(&stats));
        assert(stats.active_areas == 0u && stats.active_mappings == 0u);
        assert(kernel_area_pool_valid());
    }

    KernelAddressSpace space = {0};
    KernelArea *area;
    uint32_t base, size, physical;
    initialize_test();
    assert(kernel_vm_create_address_space(394u, &space) == KERNEL_VM_OK);
    assert(kernel_area_create(394u, KERNEL_PAGE_SIZE, 0u, &area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 394u, &space, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(!kernel_area_live(area));
    assert(kernel_vm_probe_address_space(&space, base, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

static void test_partial_pager_views_follow_writeback_reclaim_and_eof(void)
{
    KernelAddressSpace parent = {0}, peer = {0};
    KernelArea *area;
    uint8_t bytes[KERNEL_PAGE_SIZE], snapshot[KERNEL_PAGE_SIZE], observed;
    uint32_t base, alias, size, physical, token, newer, released;

    initialize_test();
    pager_release_count = 0u;
    assert(kernel_vm_create_address_space(450u, &parent) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(451u, &peer) == KERNEL_VM_OK);
    assert(kernel_area_create(450u, 3u * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 1u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    memset(bytes, 0x35, sizeof(bytes));
    for (uint32_t page = 0u; page < 3u; ++page)
        assert(kernel_area_publish_page(area, page * KERNEL_PAGE_SIZE,
            bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 450u, &parent, KERNEL_VM_READ | KERNEL_VM_WRITE,
        &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 451u, &peer, KERNEL_VM_READ | KERNEL_VM_WRITE,
        &alias, &size) == KERNEL_AREA_OK);
    assert(kernel_area_unmap_range(450u, &parent, base + KERNEL_PAGE_SIZE,
        KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_fault(450u, &parent, base, false));
    assert(kernel_vm_read(&parent, base, &observed, 1u) == KERNEL_VM_OK && observed == 0x35u);
    assert(kernel_area_fault(450u, &parent, base, true));
    assert(kernel_vm_switch(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base, true, &physical));
    physical_memory[physical - RAM_BASE] = 0x44u;
    assert(kernel_vm_read(&peer, alias, &observed, 1u) == KERNEL_VM_OK && observed == 0x44u);
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &token) == KERNEL_AREA_OK);
    assert(snapshot[0] == 0x44u);
    assert(kernel_vm_probe_address_space(&parent, base, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_area_fault(450u, &parent, base, false));
    assert(kernel_area_fault(450u, &parent, base, true));
    assert(kernel_vm_test_translate_current(base, true, &physical));
    physical_memory[physical - RAM_BASE] = 0x55u;
    assert(snapshot[0] == 0x44u);
    assert(kernel_area_writeback_complete(area, 0u, token, true) == KERNEL_AREA_OK);
    assert(kernel_area_writeback_begin(area, 0u, snapshot, &newer) == KERNEL_AREA_OK);
    assert(newer != token && snapshot[0] == 0x55u);
    assert(kernel_area_writeback_complete(area, 0u, newer, true) == KERNEL_AREA_OK);
    assert(kernel_area_fault(450u, &parent, base, false));
    assert(kernel_area_reclaim(area, 0u, KERNEL_PAGE_SIZE, &released) == KERNEL_AREA_OK && released == 1u);
    assert(kernel_vm_probe_address_space(&parent, base, &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(!kernel_area_fault(450u, &parent, base, false));
    memset(bytes, 0x66, sizeof(bytes));
    assert(kernel_area_publish_page(area, 0u, bytes, sizeof(bytes)) == KERNEL_AREA_OK);
    assert(kernel_area_fault(450u, &parent, base, false));
    assert(kernel_vm_read(&parent, base, &observed, 1u) == KERNEL_VM_OK && observed == 0x66u);
    assert(!kernel_area_fault(450u, &parent, base + KERNEL_PAGE_SIZE, false));
    assert(kernel_area_fault(450u, &parent, base + 2u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_resize_backing(area, KERNEL_PAGE_SIZE + 9u) == KERNEL_AREA_OK);
    assert(kernel_vm_probe_address_space(&parent, base + 2u * KERNEL_PAGE_SIZE,
        &physical) == KERNEL_VM_MAPPING_UNMAPPED);
    assert(!kernel_area_fault(450u, &parent, base + 2u * KERNEL_PAGE_SIZE, false));
    assert(kernel_area_pool_valid());
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_area_unmap_process(450u, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_unmap_process(451u, NULL) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(pager_release_count == 1u && kernel_area_pool_valid());
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
}

static void test_range_unmap_preserves_neighbors_and_fork(void)
{
    const uint32_t length = KERNEL_VM_SHARED_TABLE_SPAN + 3u * KERNEL_PAGE_SIZE;
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        KernelAddressSpace parent = {0}, peer = {0}, child = {0};
        KernelAreaPoolStats stats;
        KernelArea *area;
        uint32_t base, alias, size, physical, handles, mappings, children;
        uint8_t value = 0x31u, observed;
        uint32_t private_mode = (mode & 1u) ? ASTRA_AREA_MAP_PRIVATE : 0u;

        initialize_test();
        assert(kernel_vm_create_address_space(440u, &parent) == KERNEL_VM_OK);
        assert(kernel_vm_create_address_space(441u, &peer) == KERNEL_VM_OK);
        assert(kernel_area_create(440u, length, KERNEL_AREA_CREATE_MAPPED_LIFETIME |
            ((mode & 2u) ? KERNEL_AREA_CREATE_RESERVED : 0u), &area) == KERNEL_AREA_OK);
        assert(kernel_area_write(area, 0u, &value, 1u) == KERNEL_AREA_OK);
        assert(kernel_area_write(area, 2u * KERNEL_PAGE_SIZE, &value, 1u) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 440u, &parent,
            KERNEL_VM_READ | KERNEL_VM_WRITE | private_mode, &base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 441u, &peer,
            KERNEL_VM_READ | KERNEL_VM_WRITE, &alias, &size) == KERNEL_AREA_OK);
        if (private_mode != 0u) {
            assert(kernel_area_fault(440u, &parent, base, true));
            assert(kernel_area_fault(440u, &parent, base + KERNEL_PAGE_SIZE, true));
        }
        assert(kernel_vm_probe_address_space(&parent, base + KERNEL_PAGE_SIZE,
            &physical) == KERNEL_VM_MAPPING_READ_WRITE);
        kernel_allocation_test_fail_site(KERNEL_ALLOCATION_SITE_AREA_MAPPING, 1u);
        assert(kernel_area_unmap_range(440u, &parent, base + KERNEL_PAGE_SIZE,
            KERNEL_PAGE_SIZE) == KERNEL_AREA_NO_SLOT);
        kernel_allocation_test_clear_failure();
        assert(kernel_vm_probe_address_space(&parent, base + KERNEL_PAGE_SIZE,
            &physical) == KERNEL_VM_MAPPING_READ_WRITE);
        assert(kernel_area_pool_valid());
        assert(kernel_area_unmap_range(440u, &parent, base + KERNEL_PAGE_SIZE,
            KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
        assert(!kernel_area_fault(440u, &parent, base + KERNEL_PAGE_SIZE, false));
        assert(kernel_vm_probe_address_space(&parent, base + KERNEL_PAGE_SIZE,
            &physical) == KERNEL_VM_MAPPING_UNMAPPED);
        assert(kernel_vm_probe_address_space(&peer, alias + KERNEL_PAGE_SIZE,
            &physical) == KERNEL_VM_MAPPING_READ_WRITE);
        assert(kernel_area_references(area, &handles, &mappings, &children));
        assert(handles == 1u && mappings == 3u && children == 0u);
        if (kernel_vm_probe_address_space(&parent, base, &physical) == KERNEL_VM_MAPPING_UNMAPPED)
            assert(kernel_area_fault(440u, &parent, base, false));
        assert(kernel_vm_read(&parent, base, &observed, 1u) == KERNEL_VM_OK && observed == value);
        assert(kernel_area_pool_valid());
        kernel_area_handle_release(area, NULL);
        assert(kernel_vm_clone_address_space(&parent, 442u, &child) == KERNEL_VM_OK);
        assert(kernel_area_clone_process(440u, &parent, 442u, &child) == KERNEL_AREA_OK);
        assert(!kernel_area_fault(442u, &child, base + KERNEL_PAGE_SIZE, false));
        assert(kernel_vm_read(&child, base, &observed, 1u) == KERNEL_VM_OK && observed == value);
        assert(kernel_area_unmap_range(440u, &peer, base, KERNEL_PAGE_SIZE) == KERNEL_AREA_ACCESS_DENIED);
        assert(kernel_area_unmap_range(440u, &parent, base + 1u, KERNEL_PAGE_SIZE) == KERNEL_AREA_INVALID_ARGUMENT);
        assert(kernel_area_unmap_range(440u, &parent, base, 0u) == KERNEL_AREA_INVALID_ARGUMENT);
        assert(kernel_area_unmap_range(440u, &parent, base, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
        assert(!kernel_area_fault(440u, &parent, base, false));
        assert(kernel_vm_read(&child, base, &observed, 1u) == KERNEL_VM_OK && observed == value);
        assert(kernel_area_unmap_range(440u, &parent, base, length) == KERNEL_AREA_OK);
        assert(kernel_area_unmap_range(440u, &parent, base, length) == KERNEL_AREA_OK);
        assert(kernel_area_live(area) && kernel_area_pool_valid());
        assert(kernel_area_unmap_process(442u, NULL) == KERNEL_AREA_OK);
        assert(kernel_area_unmap_process(441u, NULL) == KERNEL_AREA_OK);
        assert(kernel_area_pool_stats(&stats));
        assert(stats.active_areas == 0u && stats.active_mappings == 0u);
        assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
        assert(kernel_vm_destroy_address_space(&peer) == KERNEL_VM_OK);
        assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    }
}

static void test_sync_resolves_live_views_without_handles(void)
{
    KernelAddressSpace parent = {0}, child = {0};
    KernelArea *area;
    KernelAreaPagerRequest request;
    uint32_t base, size, handles, mappings, children;
    initialize_test();
    assert(kernel_vm_create_address_space(450u, &parent) == KERNEL_VM_OK);
    assert(kernel_area_create(452u, 4u * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED | KERNEL_AREA_CREATE_MAPPED_LIFETIME, &area) == KERNEL_AREA_OK);
    assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 17u,
        release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 450u, &parent, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    assert(parent.area_mapping_sequence == 1u);
    uint32_t duplicate_base, duplicate_size, span = 0u;
    assert(kernel_area_map(area, 450u, &parent, KERNEL_VM_READ,
        &duplicate_base, &duplicate_size) == KERNEL_AREA_OK);
    assert(parent.area_mapping_sequence == 1u && duplicate_base == base && duplicate_size == size);
    assert(kernel_area_unmap_span(450u, &parent, base, &span) == KERNEL_AREA_OK && span == size);
    span = 0u;
    assert(kernel_area_unmap_span(450u, &parent, base + KERNEL_PAGE_SIZE, &span) == KERNEL_AREA_NOT_MAPPED);
    pager_owner = 450u;
    assert(kernel_area_sync_request(450u, &parent, base, size, true, &request) == KERNEL_AREA_OK);
    assert(request.area == NULL && request.byte_size == size);
    pager_owner = 452u;
    kernel_area_handle_release(area, NULL);
    assert(kernel_vm_clone_address_space(&parent, 451u, &child) == KERNEL_VM_OK);
    assert(kernel_area_clone_process(450u, &parent, 451u, &child) == KERNEL_AREA_OK);
    assert(kernel_area_backing_access(451u, &child, base, false));
    assert(!kernel_area_backing_access(450u, &child, base, false));
    assert(!kernel_area_backing_access(451u, &child, base, true));
    for (uint32_t reference = 0u; reference < UINT16_MAX; ++reference)
        assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
    assert(kernel_area_sync_request(450u, &parent, base, size, false, &request) == KERNEL_AREA_NO_SLOT);
    assert(request.area == NULL && kernel_area_child_retain(area) == KERNEL_AREA_NO_SLOT);
    assert(kernel_area_page_request(area, 0u, &request) == KERNEL_AREA_NO_SLOT);
    assert(kernel_area_references(area, &handles, &mappings, &children) && children == UINT16_MAX);
    for (uint32_t reference = 0u; reference < UINT16_MAX; ++reference)
        assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
    assert(kernel_area_sync_request(450u, &parent, base + 1u, KERNEL_PAGE_SIZE, false, &request) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_sync_request(450u, &parent, base, 0u, false, &request) == KERNEL_AREA_INVALID_ARGUMENT);
    assert(kernel_area_sync_request(450u, &child, base, size, false, &request) == KERNEL_AREA_NOT_MAPPED);
    assert(kernel_area_sync_request(451u, &child, base + KERNEL_PAGE_SIZE,
        size, false, &request) == KERNEL_AREA_OK);
    assert(request.area == area && request.port == (KernelPort *)(uintptr_t)1u &&
        request.cookie == 17u && request.offset == KERNEL_PAGE_SIZE &&
        request.byte_size == size - KERNEL_PAGE_SIZE && request.epoch != 0u);
    assert(kernel_area_references(area, &handles, &mappings, &children));
    assert(handles == 0u && mappings == 2u && children == 1u);
    assert(kernel_area_unmap_range(451u, &child, base, size) == KERNEL_AREA_OK);
    assert(!kernel_area_backing_access(451u, &child, base, false));
    assert(kernel_area_unmap_range(450u, &parent, base, size) == KERNEL_AREA_OK);
    assert(kernel_area_live(area));
    assert(kernel_area_child_release(request.area) == KERNEL_AREA_OK);
    assert(!kernel_area_live(area));
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);

    for (uint32_t private_view = 0u; private_view < 2u; ++private_view) {
        assert(kernel_vm_create_address_space(453u, &parent) == KERNEL_VM_OK);
        assert(kernel_area_create(454u, 3u * KERNEL_PAGE_SIZE,
            KERNEL_AREA_CREATE_RESERVED, &area) == KERNEL_AREA_OK);
        if (private_view != 0u)
            assert(kernel_area_bind_pager(area, (KernelPort *)(uintptr_t)1u, 18u,
                release_test_pager, (void *)(uintptr_t)2u) == KERNEL_AREA_OK);
        assert(kernel_area_map(area, 453u, &parent, KERNEL_VM_READ |
            (private_view ? ASTRA_AREA_MAP_PRIVATE : 0u), &base, &size) == KERNEL_AREA_OK);
        assert(kernel_area_sync_request(453u, &parent, base, size, false, &request) == KERNEL_AREA_OK);
        assert(request.area == NULL && request.port == NULL && request.byte_size == size);
        assert(kernel_area_unmap_range(453u, &parent, base + KERNEL_PAGE_SIZE, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
        assert(parent.area_mapping_sequence == 2u);
        assert(kernel_area_sync_request(453u, &parent, base, size, false, &request) == KERNEL_AREA_OK);
        assert(request.byte_size == KERNEL_PAGE_SIZE);
        assert(kernel_area_sync_request(453u, &parent, base + KERNEL_PAGE_SIZE,
            KERNEL_PAGE_SIZE, false, &request) == KERNEL_AREA_NOT_MAPPED);
        assert(kernel_area_sync_request(453u, &parent, base + KERNEL_PAGE_SIZE,
            2u * KERNEL_PAGE_SIZE, true, &request) == KERNEL_AREA_OK);
        assert(request.area == NULL && request.byte_size == KERNEL_PAGE_SIZE);
        parent.area_mapping_sequence = UINT64_MAX;
        span = size;
        assert(kernel_area_unmap_span(453u, &parent, base, &span) == KERNEL_AREA_QUOTA_EXCEEDED);
        assert(kernel_area_unmap_range(453u, &parent, base, size) == KERNEL_AREA_OK);
        assert(parent.area_mapping_sequence == UINT64_MAX);
        kernel_area_handle_release(area, NULL);
        assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    }
    assert(kernel_area_pool_valid());
}

static void test_area_holder_enumeration(void)
{
    KernelAddressSpace parent = {0}, child = {0}, other = {0};
    KernelArea *area, *unrelated;
    uint32_t base, size, alias, other_base, unused, process;
    uint64_t sequence = 0u, saturated = UINT64_MAX - 1u;
    initialize_test();
    assert(kernel_generation_advance64(&saturated) && saturated == UINT64_MAX);
    assert(!kernel_generation_advance64(&saturated) && saturated == UINT64_MAX);
    assert(!kernel_generation_advance64(NULL));
    assert(kernel_vm_create_address_space(700u, &parent) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(600u, &other) == KERNEL_VM_OK);
    assert(kernel_area_create(500u, 3u * KERNEL_PAGE_SIZE,
        KERNEL_AREA_CREATE_RESERVED | KERNEL_AREA_CREATE_MAPPED_LIFETIME, &area) == KERNEL_AREA_OK);
    assert(kernel_area_child_retain(area) == KERNEL_AREA_OK);
    assert(kernel_area_map(area, 700u, &parent, KERNEL_VM_READ, &base, &size) == KERNEL_AREA_OK);
    assert(kernel_area_map_range_at(area, 700u, &parent, KERNEL_VM_READ, 0u, size,
        base + KERNEL_VM_SHARED_TABLE_SPAN, &alias, &unused) == KERNEL_AREA_OK);
    assert(kernel_vm_clone_address_space(&parent, 800u, &child) == KERNEL_VM_OK);
    assert(kernel_area_clone_process(700u, &parent, 800u, &child) == KERNEL_AREA_OK);
    kernel_area_handle_release(area, NULL);
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_OK && process == 700u);
    assert(sequence == 4u);
    assert(kernel_area_next_process(area, process, &sequence, &process) == KERNEL_AREA_OK && process == 800u);
    assert(kernel_area_next_process(area, process, &sequence, &process) == KERNEL_AREA_OK && process == 0u);
    assert(kernel_area_create(500u, KERNEL_PAGE_SIZE, KERNEL_AREA_CREATE_RESERVED, &unrelated) == KERNEL_AREA_OK);
    assert(kernel_area_map(unrelated, 600u, &other, KERNEL_VM_READ, &other_base, &unused) == KERNEL_AREA_OK);
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_OK && process == 700u);
    assert(kernel_area_unmap_range(700u, &parent, base, KERNEL_PAGE_SIZE) == KERNEL_AREA_OK);
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_BUSY);
    sequence = 0u;
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_OK && process == 700u);
    assert(kernel_area_unmap_range(700u, &parent, base, size) == KERNEL_AREA_OK);
    assert(kernel_area_unmap(700u, &parent, alias) == KERNEL_AREA_OK);
    sequence = 0u;
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_OK && process == 800u);
    assert(kernel_area_unmap_process(800u, &unused) == KERNEL_AREA_OK);
    assert(kernel_area_next_process(area, UINT32_MAX, &sequence, &process) == KERNEL_AREA_BUSY);
    sequence = 0u;
    assert(kernel_area_next_process(area, 0u, &sequence, &process) == KERNEL_AREA_OK && process == 0u);
    assert(kernel_area_child_release(area) == KERNEL_AREA_OK);
    kernel_area_handle_release(unrelated, NULL);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&other) == KERNEL_VM_OK);
    assert(kernel_area_pool_valid());
}

int main(void)
{
    test_area_holder_enumeration();
    test_sync_resolves_live_views_without_handles();
    test_partial_pager_views_follow_writeback_reclaim_and_eof();
    test_range_unmap_preserves_neighbors_and_fork();
    test_mappings_can_own_backing_lifetime();
    test_mapping_lifetime_survives_fork_and_child_pin();
    test_private_pager_views_keep_source_and_fork_isolation();
    test_backing_resize_revokes_private_copies_and_zeroes_tail();
    test_staged_backing_pages_stay_hidden_until_growth();
    test_private_anonymous_view_cannot_decommit_source();
    test_allocation_injection_preserves_mapping_baseline();
    test_same_address_aliases_survive_creator_death();
    test_mapping_table_grows_past_process_derived_limit();
    test_child_lifetime();
    test_area_objects_grow_past_legacy_limits();
    test_area_slots_belong_to_address_spaces();
    test_process_clone_inherits_area_mappings();
    test_service_maps_more_than_thirty_two_small_areas();
    test_area_mapping_stops_at_virtual_window_exhaustion();
    test_screen_sized_area_reaches_its_last_pixel();
    test_create_transaction_rolls_back_every_stage();
    test_map_transaction_rolls_back_every_stage();
    test_reserved_area_commits_only_what_is_touched();
    test_reserved_area_commit_reaches_every_mapping();
    test_reserved_area_publishes_one_complete_page();
    test_publish_dirty_page_is_atomic_and_retained();
    test_pager_death_revokes_peers_and_releases_dirty_pin();
    test_pager_dirty_scan_preserves_pending_state();
    test_pager_area_never_zero_fills_missing_file_data();
    test_pager_writeback_preserves_failed_and_newer_data();
    test_pager_writable_views_redirty_after_snapshot(false);
    test_pager_writable_views_redirty_after_snapshot(true);
    test_pager_dirty_lifetime_pressure_and_allocation_rollback();
    test_reserved_area_fault_requires_authority();
    test_reserved_area_kernel_access_commits_only_on_write();
    test_reserved_area_decommit_returns_frames();
    test_reserved_area_commits_holes_and_short_tails();
    test_reserved_area_commit_stops_at_physical_memory();
    test_area_subrange_maps_distinct_source_spans();
    test_area_subranges_map_at_adjacent_slots();
    puts("area tests passed");
    return 0;
}
