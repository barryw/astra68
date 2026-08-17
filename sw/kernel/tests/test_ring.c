#include "allocation.h"
#include "area.h"
#include "memory.h"
#include "performance.h"
#include "pmmu.h"
#include "ring.h"
#include "ohci.h"
#include "thread.h"
#include "vm.h"

#include <astra/boot.h>
#include <astra/syscall.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RAM_BASE 0x02000000u

static uint8_t physical_memory[32u * 1024u * 1024u];
static KernelHandle next_thread_handle;

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
    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_area_pool_init();
    kernel_area_test_bind_physical_memory(physical_memory, RAM_BASE,
                                          sizeof(physical_memory));
    kernel_ring_pool_init();
    next_thread_handle = 0x101u;
}

static KernelThread *allocate_running_thread(uint16_t slot)
{
    KernelThread *thread;

    assert(kernel_thread_allocate(
               0u, 0x10000001u, slot, 0x00100000u,
               0x70001000u + (uint32_t)slot * KERNEL_THREAD_STACK_STRIDE,
               0u, (uint8_t)(KERNEL_THREAD_PRIORITY_NORMAL + slot),
               &thread) ==
           KERNEL_THREAD_OK);
    assert(kernel_thread_attach_handle(thread, next_thread_handle) ==
           KERNEL_THREAD_OK);
    next_thread_handle += 0x100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    return thread;
}

static void block_on(KernelThread *thread, KernelThreadWaitSpec *spec,
                     KernelRing *ring, KernelRingEndpoint endpoint)
{
    assert(kernel_thread_block_until(
               thread, spec->queue, spec->sequence, 1u,
               KERNEL_THREAD_DEADLINE_NEVER, ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_THREAD_OK);
    assert(kernel_ring_commit_wait(ring, endpoint) == KERNEL_RING_OK);
}

static void test_header_batching_waits_and_wrap(void)
{
    AstraBulkRingHeader header;
    KernelArea *area;
    KernelRing *ring;
    KernelRingPoolStats stats;
    KernelRingSnapshot snapshot;
    KernelThreadWaitSpec spec;
    KernelThread *consumer_thread;
    KernelThread *producer_thread;
    uint32_t producer;
    uint32_t consumer;
    uint32_t woken;

    initialize_test();
    assert(kernel_area_create(1u, KERNEL_PAGE_SIZE, &area) == KERNEL_AREA_OK);
    assert(kernel_ring_create(1u, area, 0u, 16u, 4u, &ring) ==
           KERNEL_RING_OK);
    assert(kernel_area_read(area, 0u, &header, sizeof(header)) ==
           KERNEL_AREA_OK);
    assert(header.magic == ASTRA_BULK_RING_MAGIC);
    assert(header.version == ASTRA_BULK_RING_ABI_VERSION);
    assert(header.header_size == sizeof(header));
    assert(header.element_size == 16u && header.capacity == 4u);
    assert(header.data_offset == sizeof(header));
    assert(header.total_size == sizeof(header) + 64u);
    assert(header.generation != 0u);
    assert(header.producer_position == 0u && header.consumer_position == 0u);

    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_PRODUCER,
                                    &spec) == KERNEL_RING_OK);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &spec) == KERNEL_RING_WOULD_BLOCK);
    consumer_thread = allocate_running_thread(0u);
    block_on(consumer_thread, &spec, ring, KERNEL_RING_ENDPOINT_CONSUMER);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 4u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer == 4u && consumer == 0u && woken == 1u);
    assert(consumer_thread->context.data[0] == ASTRA_SYSCALL_OK);

    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_PRODUCER,
                                    &spec) == KERNEL_RING_WOULD_BLOCK);
    producer_thread = allocate_running_thread(1u);
    block_on(producer_thread, &spec, ring, KERNEL_RING_ENDPOINT_PRODUCER);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_CONSUMER, 3u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer == 4u && consumer == 3u && woken == 1u);
    assert(producer_thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 7u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer == 7u && consumer == 3u);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_CONSUMER, 7u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);

    assert(kernel_ring_test_set_positions(ring, UINT32_MAX - 1u,
                                          UINT32_MAX - 1u));
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer == 1u && consumer == UINT32_MAX - 1u);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_CONSUMER, 0u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer - consumer == 1u);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 2u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);

    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &spec) == KERNEL_RING_OK);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_CONSUMER, 2u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &spec) == KERNEL_RING_PEER_DEAD);
    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    assert(kernel_ring_snapshot(0u, &snapshot));
    assert(snapshot.state == 0u);
    kernel_area_handle_release(area, NULL);
    assert(kernel_ring_pool_stats(&stats));
    assert(stats.active_rings == 0u);
    assert(stats.created_rings == 1u);
    assert(stats.producer_notifications == 4u);
    assert(stats.consumer_notifications == 4u);
    assert(stats.wait_wakeups == 2u);
    assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
}

static void test_ring_injection_releases_child_authority(void)
{
    KernelAllocationStats allocation_stats;
    KernelArea *area;
    KernelAreaSnapshot snapshot;
    KernelMemoryStats baseline;
    KernelMemoryStats after;
    KernelRing *ring = (KernelRing *)(uintptr_t)1u;
    KernelRingPoolStats pool_stats;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_area_create(41u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_RING_OBJECT, 1u);
    assert(kernel_ring_create(41u, area, 0u, 16u, 4u, &ring) ==
           KERNEL_RING_NO_SLOT);
    assert(ring == NULL);
    ring = (KernelRing *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_ring_create(41u, area, 0u, 16u, 4u, &ring) ==
           KERNEL_RING_NO_SLOT);
    assert(ring == NULL);
    assert(kernel_area_snapshot(0u, &snapshot));
    assert(snapshot.child_references == 0u);
    assert(kernel_ring_pool_stats(&pool_stats));
    assert(pool_stats.active_rings == 0u);
    kernel_area_handle_release(area, NULL);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == baseline.free_frames);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_RING_OBJECT, &allocation_stats));
    assert(allocation_stats.current_units == 0u);
    assert(allocation_stats.injected_failures == 2u);
    assert(kernel_ring_pool_valid());
    assert(kernel_area_pool_valid());
    assert(kernel_allocation_valid());
}

static void test_overlap_corruption_and_creator_death_survival(void)
{
    KernelAreaPoolStats area_stats;
    KernelRingPoolStats ring_stats;
    KernelRingSnapshot snapshot;
    KernelArea *area;
    KernelRing *first;
    KernelRing *second;
    uint32_t producer;
    uint32_t consumer;
    uint32_t woken;

    initialize_test();
    assert(kernel_area_create(55u, 2u * KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_ring_create(77u, area, 0u, 32u, 8u, &first) ==
           KERNEL_RING_OK);
    assert(kernel_ring_create(77u, area, 64u, 32u, 8u, &second) ==
           KERNEL_RING_OVERLAP);
    assert(kernel_ring_create(77u, area, 512u, 32u, 8u, &second) ==
           KERNEL_RING_OK);
    kernel_ring_abandon_unpublished(second);

    assert(kernel_ring_notify(first, KERNEL_RING_ENDPOINT_PRODUCER, 9u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_IO_ERROR);
    assert(kernel_ring_snapshot(0u, &snapshot));
    assert(snapshot.state == 2u && snapshot.child_released == 0u);
    assert(snapshot.producer_terminal == ASTRA_SYSCALL_IO_ERROR);
    assert(kernel_area_process_died(55u, NULL, NULL) == KERNEL_AREA_OK);
    assert(kernel_area_pool_stats(&area_stats));
    assert(area_stats.active_areas == 1u && area_stats.closing_areas == 0u);
    assert(area_stats.committed_pages == 2u);
    kernel_ring_handle_release(
        first, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    kernel_ring_handle_release(
        first, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    kernel_area_handle_release(area, NULL);
    assert(kernel_ring_pool_stats(&ring_stats));
    assert(ring_stats.active_rings == 0u);
    assert(ring_stats.overlap_failures == 1u);
    assert(ring_stats.corruption_failures == 1u);
    assert(kernel_area_pool_stats(&area_stats));
    assert(area_stats.active_areas == 0u);
    assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
}

static void test_validation_no_advance_and_consumer_death(void)
{
    KernelArea *area;
    KernelRing *ring = NULL;
    KernelRingSnapshot snapshot;
    KernelThreadWaitSpec spec;
    KernelThread *consumer_thread;
    uint32_t producer;
    uint32_t consumer;
    uint32_t woken;

    initialize_test();
    assert(kernel_area_create(61u, 2u * KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_ring_create(61u, area, 1u, 16u, 4u, &ring) ==
           KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(61u, area, 0u, 3u, 4u, &ring) ==
           KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(
               61u, area, 0u, KERNEL_RING_ELEMENT_SIZE_MAX + 4u, 4u,
               &ring) == KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(61u, area, 0u, 16u, 3u, &ring) ==
           KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(
               61u, area, 0u, 16u, KERNEL_RING_CAPACITY_MAX + 1u,
               &ring) == KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(
               61u, area, UINT32_MAX &
                   ~(KERNEL_RING_OFFSET_ALIGNMENT - 1u),
               KERNEL_RING_ELEMENT_SIZE_MAX, KERNEL_RING_CAPACITY_MAX,
               &ring) == KERNEL_RING_INVALID_ARGUMENT);
    assert(kernel_ring_create(
               61u, area, KERNEL_PAGE_SIZE,
               KERNEL_RING_ELEMENT_SIZE_MAX, 2u, &ring) ==
           KERNEL_RING_INVALID_ARGUMENT);
    assert(ring == NULL);

    assert(kernel_ring_create(61u, area, 0u, 16u, 4u, &ring) ==
           KERNEL_RING_OK);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &spec) == KERNEL_RING_WOULD_BLOCK);
    consumer_thread = allocate_running_thread(0u);
    block_on(consumer_thread, &spec, ring, KERNEL_RING_ENDPOINT_CONSUMER);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 0u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(producer == 0u && consumer == 0u && woken == 0u);
    assert(kernel_ring_snapshot(0u, &snapshot));
    assert(snapshot.consumer_waiters == 1u);
    assert(consumer_thread->state == KERNEL_THREAD_BLOCKED);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(woken == 1u);
    assert(consumer_thread->context.data[0] == ASTRA_SYSCALL_OK);

    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_PRODUCER,
                                    &spec) == KERNEL_RING_PEER_DEAD);
    assert(kernel_ring_notify(ring, KERNEL_RING_ENDPOINT_PRODUCER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_PEER_DEAD);
    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    kernel_area_handle_release(area, NULL);
    assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
}

static void test_missed_wakeup_and_wait_multiple(void)
{
    KernelArea *area;
    KernelRing *first;
    KernelRing *second;
    KernelThreadWaitSpec stale;
    KernelThreadWaitSpec specs[2];
    KernelThread *thread;
    uint32_t producer;
    uint32_t consumer;
    uint32_t woken;

    initialize_test();
    assert(kernel_area_create(62u, 2u * KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_ring_create(62u, area, 0u, 16u, 4u, &first) ==
           KERNEL_RING_OK);
    assert(kernel_ring_create(62u, area, 512u, 16u, 4u, &second) ==
           KERNEL_RING_OK);

    assert(kernel_ring_prepare_wait(first, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &stale) == KERNEL_RING_WOULD_BLOCK);
    assert(kernel_ring_notify(first, KERNEL_RING_ENDPOINT_PRODUCER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(woken == 0u);
    thread = allocate_running_thread(0u);
    assert(kernel_thread_block_until(
               thread, stale.queue, stale.sequence, 1u,
               KERNEL_THREAD_DEADLINE_NEVER, ASTRA_SYSCALL_TIMED_OUT) ==
           KERNEL_THREAD_CONDITION_CHANGED);
    assert(thread->state == KERNEL_THREAD_RUNNING);
    assert(kernel_ring_commit_wait(first, KERNEL_RING_ENDPOINT_CONSUMER) ==
           KERNEL_RING_INVALID_STATE);
    assert(kernel_ring_notify(first, KERNEL_RING_ENDPOINT_CONSUMER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);

    assert(kernel_ring_prepare_wait(first, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &specs[0]) == KERNEL_RING_WOULD_BLOCK);
    assert(kernel_ring_prepare_wait(second, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &specs[1]) == KERNEL_RING_WOULD_BLOCK);
    assert(kernel_thread_block_wait_set(
               thread, specs, 2u, 1u, KERNEL_THREAD_DEADLINE_NEVER,
               ASTRA_SYSCALL_TIMED_OUT) == KERNEL_THREAD_OK);
    assert(kernel_ring_commit_wait(first, KERNEL_RING_ENDPOINT_CONSUMER) ==
           KERNEL_RING_OK);
    assert(kernel_ring_commit_wait(second, KERNEL_RING_ENDPOINT_CONSUMER) ==
           KERNEL_RING_OK);
    assert(kernel_ring_notify(second, KERNEL_RING_ENDPOINT_PRODUCER, 1u, 0u,
                              &producer, &consumer, &woken) ==
           KERNEL_RING_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    assert(thread->context.data[1] == 1u);
    assert(kernel_ring_snapshot(0u, &(KernelRingSnapshot){0}));
    assert(kernel_ring_commit_wait(first, KERNEL_RING_ENDPOINT_CONSUMER) ==
           KERNEL_RING_INVALID_STATE);

    kernel_ring_handle_release(
        first, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    kernel_ring_handle_release(
        first, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    kernel_ring_handle_release(
        second, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    kernel_ring_handle_release(
        second, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    kernel_area_handle_release(area, NULL);
    assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
}

static void test_owner_death_wakes_waiters(void)
{
    KernelArea *area;
    KernelRing *ring;
    KernelThreadWaitSpec spec;
    KernelThread *thread;
    KernelRingPoolStats stats;
    uint32_t closed;
    uint32_t woken;

    initialize_test();
    assert(kernel_area_create(63u, KERNEL_PAGE_SIZE, &area) ==
           KERNEL_AREA_OK);
    assert(kernel_ring_create(64u, area, 0u, 16u, 4u, &ring) ==
           KERNEL_RING_OK);
    assert(kernel_ring_prepare_wait(ring, KERNEL_RING_ENDPOINT_CONSUMER,
                                    &spec) == KERNEL_RING_WOULD_BLOCK);
    thread = allocate_running_thread(0u);
    block_on(thread, &spec, ring, KERNEL_RING_ENDPOINT_CONSUMER);
    assert(kernel_ring_process_died(64u, &closed, &woken) ==
           KERNEL_RING_OK);
    assert(closed == 1u && woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_ring_terminal_result(
               ring, KERNEL_RING_ENDPOINT_PRODUCER) ==
           ASTRA_SYSCALL_PEER_DEAD);
    assert(kernel_ring_terminal_result(
               ring, KERNEL_RING_ENDPOINT_CONSUMER) ==
           ASTRA_SYSCALL_PEER_DEAD);
    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
    kernel_ring_handle_release(
        ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
    kernel_area_handle_release(area, NULL);
    assert(kernel_ring_pool_stats(&stats));
    assert(stats.owner_deaths == 1u && stats.active_rings == 0u);
    assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
}

static void test_repeated_lifecycle_returns_exact_baseline(void)
{
    KernelAddressSpace space = {0};
    KernelAreaPoolStats area_stats;
    KernelRingPoolStats ring_stats;
    KernelMemoryStats initial_memory;
    KernelMemoryStats baseline_memory;
    KernelMemoryStats memory;

    initialize_test();
    assert(kernel_memory_stats(&initial_memory));
    assert(kernel_vm_create_address_space(65u, &space) == KERNEL_VM_OK);
    assert(kernel_memory_stats(&baseline_memory));
    for (uint32_t cycle = 0u; cycle < 1000u; ++cycle) {
        KernelArea *area;
        KernelRing *ring;
        uint32_t virtual_base;
        uint32_t byte_size;

        assert(kernel_area_create(66u, KERNEL_PAGE_SIZE, &area) ==
               KERNEL_AREA_OK);
        assert(kernel_area_map(
                   area, 65u, &space,
                   KERNEL_VM_READ | KERNEL_VM_WRITE,
                   &virtual_base, &byte_size) == KERNEL_AREA_OK);
        assert(virtual_base == KERNEL_VM_AREA_BASE);
        assert(byte_size == KERNEL_PAGE_SIZE);
        assert(kernel_ring_create(66u, area, 0u, 16u, 4u, &ring) ==
               KERNEL_RING_OK);
        kernel_ring_handle_release(
            ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER);
        kernel_ring_handle_release(
            ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
        assert(kernel_area_unmap(65u, &space, virtual_base) ==
               KERNEL_AREA_OK);
        kernel_area_handle_release(area, NULL);
        assert(kernel_memory_stats(&memory));
        assert(memory.free_frames == baseline_memory.free_frames);
        assert(kernel_area_pool_stats(&area_stats));
        assert(area_stats.active_areas == 0u);
        assert(area_stats.active_mappings == 0u);
        assert(area_stats.committed_pages == 0u);
        assert(kernel_ring_pool_stats(&ring_stats));
        assert(ring_stats.active_rings == 0u);
        assert(kernel_ring_pool_valid() && kernel_area_pool_valid());
    }
    assert(kernel_vm_destroy_address_space(&space) == KERNEL_VM_OK);
    assert(kernel_memory_release_owner(65u, NULL) == KERNEL_MEMORY_OK);
    assert(kernel_memory_stats(&memory));
    assert(memory.free_frames == initial_memory.free_frames);
    assert(kernel_area_pool_stats(&area_stats));
    assert(area_stats.created_areas == 1000u);
    assert(area_stats.map_operations == 1000u);
    assert(area_stats.unmap_operations == 1000u);
    assert(kernel_ring_pool_stats(&ring_stats));
    assert(ring_stats.created_rings == 1000u);
}

int main(void)
{
    _Static_assert(sizeof(AstraBulkRingHeader) == 64u,
                   "bulk-ring header size");
    _Static_assert(offsetof(AstraBulkRingHeader, producer_position) == 0x20u,
                   "bulk-ring producer offset");
    _Static_assert(offsetof(AstraBulkRingHeader, consumer_position) == 0x30u,
                   "bulk-ring consumer offset");
    test_ring_injection_releases_child_authority();
    test_header_batching_waits_and_wrap();
    test_overlap_corruption_and_creator_death_survival();
    test_validation_no_advance_and_consumer_death();
    test_missed_wakeup_and_wait_multiple();
    test_owner_death_wakes_waiters();
    test_repeated_lifecycle_returns_exact_baseline();
    puts("ring tests passed");
    return 0;
}
