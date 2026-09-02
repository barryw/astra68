#include "allocation.h"
#include "memory.h"
#include "ohci.h"
#include "pmmu.h"
#include "thread.h"
#include "vm.h"

#include <astra/library.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * The guards sit just above the thread-stack arena and move with it, so the
 * test derives them the way vm.c does. They used to be written out, and
 * raising KERNEL_THREAD_MAX moved the arena over the literals.
 */
#define TEST_THREAD_ARENA_END \
    (KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE + \
     KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE)
#define TEST_KERNEL_STACK_GUARD TEST_THREAD_ARENA_END
#define TEST_WORKER_STACK_GUARD (TEST_THREAD_ARENA_END + KERNEL_PAGE_SIZE)

static uint8_t physical_memory[32u * 1024u * 1024u];
static KernelPmmuRootPointer loaded_srp;
static KernelPmmuRootPointer loaded_crp;
static uint32_t loaded_tc;
static uint32_t loaded_tt0;
static uint32_t loaded_tt1;
static uint32_t flush_count;
static uint32_t flush_page_count;
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

void kernel_pmmu_flush_page(uint32_t virtual_address)
{
    (void)virtual_address;
    ++flush_page_count;
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

/* The standard machine, so a test wanting a different one can start here. */
static void fill_info(AstraBootInfo *info)
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
}

static void initialize_test(void)
{
    AstraBootInfo info;

    fill_info(&info);
    astra_boot_info_finalize(&info);
    kernel_memory_test_bind_physical_memory(physical_memory, 0x02000000u,
                                            sizeof(physical_memory));
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
    flush_page_count = 0u;
    cache_invalidation_count = 0u;
    function_code_sets = 0u;
    assert(kernel_vm_init() == KERNEL_VM_OK);
}

static void test_private_reservation_fault_and_decommit(void)
{
    KernelAddressSpace space = {0};
    KernelMemoryStats before;
    KernelMemoryStats reserved;
    KernelMemoryStats committed;
    KernelMemoryStats decommitted;
    KernelAllocationStats allocation;
    uint32_t base;
    uint32_t span;
    uint32_t readonly_base;
    uint32_t readonly_span;
    uint32_t physical;
    uint32_t released;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(93u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_stats(&before));
    assert(kernel_vm_private_reserve(
               &space, 1u, KERNEL_VM_READ | KERNEL_VM_WRITE,
               &base, &span) == KERNEL_VM_OK);
    assert(base == KERNEL_VM_PRIVATE_BASE);
    assert(span == KERNEL_VM_PRIVATE_SLOT_SIZE);
    assert(kernel_vm_private_reserve(
               &space, KERNEL_VM_PRIVATE_SLOT_SIZE + 1u, KERNEL_VM_READ,
               &readonly_base, &readonly_span) == KERNEL_VM_OK);
    assert(readonly_base == base + span);
    assert(readonly_span == 2u * KERNEL_VM_PRIVATE_SLOT_SIZE);
    assert(kernel_memory_stats(&reserved));
    assert(reserved.free_frames == before.free_frames);

    assert(kernel_vm_private_fault(&space, base + 7u, true) == KERNEL_VM_OK);
    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base + 7u, true, &physical));
    assert(physical_memory[physical - 0x02000000u] == 0u);
    assert(kernel_vm_private_fault(&space, base + 7u, false) ==
           KERNEL_VM_ALREADY_MAPPED);
    assert(kernel_memory_stats(&committed));
    assert(committed.free_frames + 2u == reserved.free_frames);

    assert(kernel_vm_private_fault(&space, readonly_base, true) ==
           KERNEL_VM_NOT_OWNED);
    assert(kernel_vm_private_fault(&space, readonly_base, false) ==
           KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(readonly_base, false, &physical));
    assert(!kernel_vm_test_translate_current(readonly_base, true, &physical));

    assert(kernel_vm_private_decommit(&space, base + 1u,
                                      KERNEL_PAGE_SIZE - 1u,
                                      &released) == KERNEL_VM_OK);
    assert(released == 0u);
    assert(kernel_vm_private_decommit(&space, base, KERNEL_PAGE_SIZE,
                                      &released) == KERNEL_VM_OK);
    assert(released == 1u);
    assert(!kernel_vm_test_translate_current(base, false, &physical));
    assert(kernel_memory_stats(&decommitted));
    assert(decommitted.free_frames == committed.free_frames);

    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, 1u);
    assert(kernel_vm_private_fault(&space, base, true) ==
           KERNEL_VM_OUT_OF_MEMORY);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, &allocation));
    assert(allocation.injected_failures == 1u);
    assert(kernel_vm_private_fault(&space, base, true) == KERNEL_VM_OK);
    assert(kernel_vm_private_commit_range(
               &space, base + KERNEL_PAGE_SIZE - 8u, 32u, true) ==
           KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(base + KERNEL_PAGE_SIZE,
                                            true, &physical));
    assert(kernel_vm_private_commit_range(
               &space, readonly_base, 1u, true) == KERNEL_VM_NOT_OWNED);
    assert(kernel_vm_private_decommit(
               &space, KERNEL_VM_PRIVATE_END, KERNEL_PAGE_SIZE, &released) ==
           KERNEL_VM_INVALID_ARGUMENT);

    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(93u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&decommitted));
    assert(decommitted.free_frames == before.free_frames + 1u);
}

static void test_address_space_copy_crosses_pages_and_checks_rights(void)
{
    KernelAddressSpace space = {0};
    uint8_t source[32];
    uint8_t destination[32];
    uint32_t first;
    uint32_t second;
    const uint32_t address = 0x00100ff0u;

    initialize_test();
    assert(kernel_vm_create_address_space(94u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_alloc_zeroed(1u, 1u, KERNEL_FRAME_PROCESS, 94u,
                                      &first) == KERNEL_MEMORY_OK);
    assert(kernel_memory_alloc_zeroed(1u, 1u, KERNEL_FRAME_PROCESS, 94u,
                                      &second) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&space, address & ~(KERNEL_PAGE_SIZE - 1u),
                              first, KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    assert(kernel_vm_map_page(&space,
                              (address & ~(KERNEL_PAGE_SIZE - 1u)) +
                                  KERNEL_PAGE_SIZE,
                              second, KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    assert(kernel_memory_release(first, 1u, 94u) == KERNEL_MEMORY_OK);
    assert(kernel_memory_release(second, 1u, 94u) == KERNEL_MEMORY_OK);
    for (uint32_t index = 0u; index < sizeof(source); ++index)
        source[index] = (uint8_t)(index + 1u);
    memset(destination, 0, sizeof(destination));
    assert(kernel_vm_write(&space, address, source, sizeof(source)) ==
           KERNEL_VM_OK);
    assert(kernel_vm_read(&space, address, destination,
                          sizeof(destination)) == KERNEL_VM_OK);
    assert(memcmp(source, destination, sizeof(source)) == 0);
    assert(kernel_vm_read(&space, address - KERNEL_PAGE_SIZE, destination,
                          1u) == KERNEL_VM_NOT_MAPPED);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);

    initialize_test();
    assert(kernel_vm_create_address_space(95u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_alloc_zeroed(1u, 1u, KERNEL_FRAME_PROCESS, 95u,
                                      &first) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&space, 0x00100000u, first, KERNEL_VM_READ) ==
           KERNEL_VM_OK);
    assert(kernel_memory_release(first, 1u, 95u) == KERNEL_MEMORY_OK);
    assert(kernel_vm_write(&space, 0x00100000u, source, 1u) ==
           KERNEL_VM_NOT_OWNED);
    assert(kernel_vm_read(&space, 0x00100000u, destination, 1u) ==
           KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
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
    assert(low[(TEST_KERNEL_STACK_GUARD - 0x02000000u) >> 12] == 0u);
    assert(low[(TEST_WORKER_STACK_GUARD - 0x02000000u) >> 12] == 0u);
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
    assert(top[(0x03e00000u - 0x03c00000u) >> 12] == 0x03e00001u);
    assert(top[(OHCI_DMA_POOL_BASE - 0x03c00000u) >> 12] ==
           (OHCI_DMA_POOL_BASE | 0x41u));
    assert(top[0x3ffu] == 0x03fff041u);
    assert(stats.kernel_stack_guard == TEST_KERNEL_STACK_GUARD);
    assert(stats.kernel_worker_stack_guard == TEST_WORKER_STACK_GUARD);
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
    assert(kernel_vm_probe_current(TEST_KERNEL_STACK_GUARD, true, &translated) ==
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
           KERNEL_VM_MAPPING_UNMAPPED);

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
    uint32_t flushes_before_switch;

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

    flushes_before_switch = flush_count;
    assert(kernel_vm_switch(&space) == KERNEL_VM_OK);
    assert(loaded_crp.table_address == space.root_physical);
    assert(flush_count == flushes_before_switch);
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
    flushes_before_switch = flush_count;
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(flush_count == flushes_before_switch);
    assert(cache_invalidation_count == 7u);
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(42u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_vm_stats(&stats));
    assert(stats.address_spaces == 0u && stats.user_mappings == 0u);
    assert(stats.user_table_pages == 0u);
}

static void test_host_channel_mapping_is_private_and_uncached(void)
{
    KernelAddressSpace parent = {0};
    KernelAddressSpace child = {0};
    uint32_t *root;
    uint32_t *table;
    uint32_t translated;
    uint32_t physical = KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE +
                        3u * KERNEL_PAGE_SIZE;
    uint32_t second_virtual = KERNEL_VM_HOST_CHANNEL_BASE + KERNEL_PAGE_SIZE;
    uint32_t second_physical = KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE +
                               7u * KERNEL_PAGE_SIZE;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(42u, &parent) == KERNEL_VM_OK);
    assert(kernel_vm_map_host_channel_page(
               &parent, KERNEL_VM_HOST_CHANNEL_BASE,
               KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE -
                            KERNEL_PAGE_SIZE) == KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_map_host_channel_page(&parent,
               KERNEL_VM_HOST_CHANNEL_BASE - KERNEL_PAGE_SIZE,
               physical) == KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_map_host_channel_page(
               &parent, KERNEL_VM_HOST_CHANNEL_BASE, physical) == KERNEL_VM_OK);
    assert(kernel_vm_map_host_channel_page(
               &parent, second_virtual, second_physical) == KERNEL_VM_OK);
    root = physical_words(parent.root_physical);
    table = physical_words(root[KERNEL_VM_HOST_CHANNEL_BASE >> 22] &
                           0xfffffff0u);
    assert(table[(KERNEL_VM_HOST_CHANNEL_BASE >> 12) & 0x3ffu] ==
           (physical | 0x41u));
    assert(kernel_vm_clone_address_space(&parent, 43u, &child) ==
           KERNEL_VM_OK);
    assert(kernel_vm_switch(&child) == KERNEL_VM_OK);
    assert(kernel_vm_probe_current(KERNEL_VM_HOST_CHANNEL_BASE, false,
                                   &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_probe_current(second_virtual, false, &translated) ==
           KERNEL_VM_MAPPING_UNMAPPED);
    assert(kernel_vm_switch(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_probe_current(KERNEL_VM_HOST_CHANNEL_BASE, false,
                                   &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == physical);
    assert(kernel_vm_probe_current(second_virtual, false, &translated) ==
           KERNEL_VM_MAPPING_READ_WRITE);
    assert(translated == second_physical);
    assert(kernel_vm_unmap_page(&parent, KERNEL_VM_HOST_CHANNEL_BASE) ==
           KERNEL_VM_OK);
    assert(kernel_vm_unmap_page(&parent, second_virtual) == KERNEL_VM_OK);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
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

static void test_cow_alias_can_change_owner_and_become_private(void)
{
    KernelAddressSpace parent = {0};
    KernelAddressSpace child = {0};
    KernelFrameInfo frame;
    uint32_t physical;
    uint32_t translated;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(101u, &parent) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(102u, &child) == KERNEL_VM_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 101u,
                               &physical) == KERNEL_MEMORY_OK);
    assert(kernel_vm_map_page(&parent, 0x18000000u, physical,
                              KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    assert(kernel_memory_release(physical, 1u, 101u) == KERNEL_MEMORY_OK);
    assert(kernel_vm_promote_page_to_cow(&parent, 0x18000000u) ==
           KERNEL_VM_OK);
    assert(kernel_vm_map_cow_page(&child, 0x18000000u, physical, 101u,
                                  true) ==
           KERNEL_VM_OK);
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.state == KERNEL_FRAME_COW_WRITE && frame.references == 2u);
    assert(kernel_vm_switch(&child) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(0x18000000u, false,
                                            &translated));
    assert(!kernel_vm_test_translate_current(0x18000000u, true,
                                             &translated));
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_memory_frame_info(physical, &frame));
    assert(frame.owner == 102u && frame.references == 1u);
    assert(kernel_vm_cow_make_private(&child, 0x18000000u) == KERNEL_VM_OK);
    assert(kernel_vm_switch(&child) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(0x18000000u, true,
                                            &translated));
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
}

static void test_clone_is_lazy_and_write_fault_copies_one_page(void)
{
    KernelAddressSpace parent = {0};
    KernelAddressSpace child = {0};
    KernelFrameInfo frame;
    uint32_t source_physical;
    uint32_t child_physical;
    uint32_t private_base;
    uint32_t private_span;
    uint32_t translated;
    uint32_t *words;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(201u, &parent) == KERNEL_VM_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, 201u,
                               &source_physical) == KERNEL_MEMORY_OK);
    words = physical_words(source_physical);
    words[0] = 0x12345678u;
    words[1023] = 0xabcdef01u;
    assert(kernel_vm_map_page(&parent, 0x18000000u, source_physical,
                              KERNEL_VM_READ | KERNEL_VM_WRITE) ==
           KERNEL_VM_OK);
    assert(kernel_memory_release(source_physical, 1u, 201u) ==
           KERNEL_MEMORY_OK);
    assert(kernel_vm_private_reserve(&parent, KERNEL_VM_PRIVATE_SLOT_SIZE,
                                     KERNEL_VM_READ | KERNEL_VM_WRITE,
                                     &private_base, &private_span) ==
           KERNEL_VM_OK);
    assert(kernel_vm_clone_address_space(&parent, 202u, &child) ==
           KERNEL_VM_OK);
    assert(child.private_reserved[0] == parent.private_reserved[0]);
    assert(child.private_writable[0] == parent.private_writable[0]);
    assert(kernel_memory_frame_info(source_physical, &frame));
    assert(frame.state == KERNEL_FRAME_COW_WRITE && frame.references == 2u);

    assert(kernel_vm_cow_fault(&child, 0x18000003u) == KERNEL_VM_OK);
    assert(kernel_vm_switch(&child) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(0x18000000u, true,
                                            &child_physical));
    assert(child_physical != source_physical);
    words = physical_words(child_physical);
    assert(words[0] == 0x12345678u && words[1023] == 0xabcdef01u);
    words[0] = 0xfeedfaceu;
    assert(kernel_vm_switch(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_test_translate_current(0x18000000u, false,
                                            &translated));
    assert(translated == source_physical);
    assert(physical_words(source_physical)[0] == 0x12345678u);
    assert(kernel_vm_switch_to_empty() == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&parent) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&child) == KERNEL_VM_OK);
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

    /*
     * A shared run no longer has to start at a slot boundary -- an area
     * commits a cluster at a time, at whatever page offset the fault landed
     * on. What it still may not do is straddle two page tables, because both
     * of these functions resolve the root descriptor once, and it must be
     * page aligned. Those are the invariants that replaced slot alignment.
     */
    assert(kernel_vm_map_shared_range(
               &first, KERNEL_VM_AREA_BASE + KERNEL_PAGE_SIZE,
               physical_pages, 1u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_WRITE) == KERNEL_VM_OK);
    assert(kernel_vm_unmap_shared_range(
               &first, KERNEL_VM_AREA_BASE + KERNEL_PAGE_SIZE,
               physical_pages, 1u, frame_owner) == KERNEL_VM_OK);
    assert(kernel_vm_map_shared_range(
               &first, KERNEL_VM_AREA_BASE + 1u, physical_pages, 1u,
               frame_owner, KERNEL_VM_READ) == KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_unmap_shared_range(
               &first, KERNEL_VM_AREA_BASE + 1u, physical_pages, 1u,
               frame_owner) == KERNEL_VM_INVALID_ARGUMENT);
    /*
     * One page below a 4 MiB boundary, two pages long: the second page is
     * under the next root descriptor, which the single-table walk would not
     * have reached.
     */
    assert(kernel_vm_map_shared_range(
               &first, KERNEL_VM_AREA_BASE + 0x00400000u - KERNEL_PAGE_SIZE,
               physical_pages, 2u, frame_owner, KERNEL_VM_READ) ==
           KERNEL_VM_INVALID_ARGUMENT);

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

static void test_library_code_range_is_shared_and_executable(void)
{
    const uint32_t frame_owner = 0x30000001u;
    const uint32_t address = ASTRA_LIBRARY_BASE + 0x1000u;
    KernelAddressSpace first = {0};
    KernelAddressSpace second = {0};
    KernelFrameInfo frame;
    uint32_t physical_pages[2];
    uint32_t flushes_before;
    uint32_t invalidations_before;

    initialize_test();
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(61u, &first) == KERNEL_VM_OK);
    assert(kernel_vm_create_address_space(62u, &second) == KERNEL_VM_OK);
    assert(kernel_memory_alloc_pages_zeroed(
               2u, KERNEL_FRAME_SHARED, frame_owner, physical_pages) ==
           KERNEL_MEMORY_OK);
    flushes_before = flush_count;
    invalidations_before = cache_invalidation_count;
    assert(kernel_vm_map_shared_range(
               &first, address, physical_pages, 2u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_EXEC) == KERNEL_VM_OK);
    assert(flush_count == flushes_before + 1u);
    assert(cache_invalidation_count == invalidations_before + 1u);
    assert(kernel_vm_map_shared_range(
               &second, address, physical_pages, 2u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_EXEC) == KERNEL_VM_OK);
    assert(kernel_memory_frame_info(physical_pages[0], &frame));
    assert(frame.references == 3u); /* cache plus two address spaces */
    assert(kernel_memory_frame_info(physical_pages[1], &frame));
    assert(frame.references == 3u);
    assert(kernel_vm_map_shared_range(
               &second, address + ASTRA_LIBRARY_SLOT_SIZE, physical_pages,
               2u, frame_owner, KERNEL_VM_READ | KERNEL_VM_EXEC) ==
           KERNEL_VM_CACHE_ALIAS);
    assert(kernel_vm_map_shared_range(
               &second, address + 0x2000u, physical_pages, 2u, frame_owner,
               KERNEL_VM_READ | KERNEL_VM_WRITE | KERNEL_VM_EXEC) ==
           KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_map_shared_range(
               &second, KERNEL_VM_AREA_BASE + 0x2000u, physical_pages, 2u,
               frame_owner, KERNEL_VM_READ | KERNEL_VM_EXEC) ==
           KERNEL_VM_INVALID_ARGUMENT);
    assert(kernel_vm_unmap_shared_range(
               &first, address, physical_pages, 2u, frame_owner) ==
           KERNEL_VM_OK);
    assert(kernel_vm_unmap_shared_range(
               &second, address, physical_pages, 2u, frame_owner) ==
           KERNEL_VM_OK);
    assert(kernel_memory_frame_info(physical_pages[0], &frame));
    assert(frame.references == 1u);
    assert(kernel_memory_frame_info(physical_pages[1], &frame));
    assert(frame.references == 1u);
    assert(kernel_memory_release(physical_pages[0], 1u, frame_owner) ==
           KERNEL_MEMORY_OK);
    assert(kernel_memory_release(physical_pages[1], 1u, frame_owner) ==
           KERNEL_MEMORY_OK);
    assert(kernel_vm_destroy_address_space(&first) == KERNEL_VM_OK);
    assert(kernel_vm_destroy_address_space(&second) == KERNEL_VM_OK);
}

/*
 * A device aperture above the initial root-table span must not be mapped cacheable.
 *
 * The supervisor map covers RAM beyond the low region in 4 MiB
 * early-termination pages. That is fine for RAM and wrong for a DMA pool: the
 * USB controller's aperture used to be pinned to 0x03F00000, inside the one
 * span that gets a page table and per-page cache bits, and the moment the pool
 * is allowed to sit anywhere the board puts it, a cacheable 4 MiB page could
 * cover it and the kernel would read its own stale copy of what the controller
 * wrote.
 *
 * The machine here is 64 MiB with a device range at 0x05F00000 -- root index
 * 23 -- which the old layout could not express at all.
 */
static void test_device_aperture_above_the_low_region_is_uncached(void)
{
    AstraBootInfo info;
    KernelVmStats stats;
    uint32_t *root;

    fill_info(&info);
    info.ram_size = 0x04000000u;
    /*
     * The tail of the machine, ending in a device aperture. Two ranges rather
     * than three because the boot info holds ten and the standard machine has
     * already spent eight.
     */
    add_range(&info, 0x04000000u, 0x01f00000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x05f00000u, 0x00100000u,
              ASTRA_MEMORY_RANGE_DEVICE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    astra_boot_info_finalize(&info);
    assert(astra_boot_info_validate(&info) == ASTRA_BOOT_VALID);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    kernel_vm_test_bind_physical_memory(physical_memory, 0x02000000u,
                                        sizeof(physical_memory));
    assert(kernel_vm_init() == KERNEL_VM_OK);
    assert(kernel_vm_stats(&stats));
    root = physical_words(stats.kernel_root_physical);

    /* The span holding the aperture is a 4 MiB page, and uncached. */
    assert(root[23] == ((23u << 22) | 0x41u));
    /* Its neighbour is ordinary cacheable RAM. */
    assert(root[22] == ((22u << 22) | 1u));
    /* And the machine really is larger than the low region. */
    assert(root[16] == ((16u << 22) | 1u));
}

int main(void)
{
    test_kernel_root_and_enable_sequence();
    test_page_table_injection_preserves_baseline();
    test_map_switch_unmap_and_stale_guards();
    test_host_channel_mapping_is_private_and_uncached();
    test_destroy_releases_read_only_mapping();
    test_cow_alias_can_change_owner_and_become_private();
    test_clone_is_lazy_and_write_fault_copies_one_page();
    test_shared_map_transaction_rolls_back_every_stage();
    test_shared_map_existing_leaf_rollback_and_alias_guards();
    test_library_code_range_is_shared_and_executable();
    test_device_aperture_above_the_low_region_is_uncached();
    test_private_reservation_fault_and_decommit();
    test_address_space_copy_crosses_pages_and_checks_rights();
    puts("KERNEL VM PASS");
    return 0;
}
