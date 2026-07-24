#include "memory.h"
#include "pmmu.h"
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
    add_range(&info, 0x02004000u, 0x0000c000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x02090000u, 0x01d70000u,
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
    KernelVmStats stats;
    uint32_t *root;
    uint32_t *low;
    uint32_t *high;
    uint32_t low_physical;
    uint32_t high_physical;

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
    assert(low[0x3ffu] == 0x023ff001u);
    for (uint32_t index = 9u; index <= 15u; ++index)
        assert(root[index] == ((index << 22) | 1u));
    assert(stats.kernel_stack_guard == 0x02080000u);
    assert(stats.kernel_worker_stack_guard == 0x02083000u);
    assert(stats.supervisor_table_pages == 3u);
    assert((root[1023] & 3u) == 2u);
    high_physical = root[1023] & 0xfffffff0u;
    high = physical_words(high_physical);
    assert(high[(0xfff00000u >> 12) & 0x3ffu] == 0xfff00041u);
    assert(high[(0xfff20000u >> 12) & 0x3ffu] == 0xfff20041u);
    assert(high[(0xfff40000u >> 12) & 0x3ffu] == 0xfff40041u);
    assert(high[(0xfff30000u >> 12) & 0x3ffu] == 0u);

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
}

static void test_map_switch_unmap_and_stale_guards(void)
{
    KernelAddressSpace space = {0};
    KernelFrameInfo frame;
    KernelVmStats stats;
    uint32_t physical;
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
    assert(kernel_vm_unmap_page(&space, 0x10001000u) == KERNEL_VM_OK);
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

int main(void)
{
    test_kernel_root_and_enable_sequence();
    test_map_switch_unmap_and_stale_guards();
    test_destroy_releases_read_only_mapping();
    puts("KERNEL VM PASS");
    return 0;
}
