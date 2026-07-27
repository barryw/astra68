#include "allocation.h"
#include "block.h"
#include "memory.h"
#include "platform.h"
#include "vesta.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FAKE_COMPLETIONS 16u

static KernelPlatformBlockState fake_state;
static KernelPlatformBlockCompletion fake_completions[FAKE_COMPLETIONS];
static uint32_t fake_completion_read;
static uint32_t fake_completion_write;
static uint32_t fake_submit_error;
static bool fake_state_available;
static uint32_t submitted_id;
static uint8_t submitted_operation;
static uint64_t submitted_lba;
static uint16_t submitted_sectors;
static uint32_t submitted_buffer;

bool kernel_platform_block_state(KernelPlatformBlockState *state)
{
    if (!fake_state_available || state == NULL)
        return false;
    state->capabilities = fake_state.capabilities;
    state->state_flags = fake_state.state_flags;
    state->media_generation = fake_state.media_generation;
    state->host_generation = fake_state.host_generation;
    state->media_sectors = fake_state.media_sectors;
    state->max_sectors = fake_state.max_sectors;
    state->reserved = 0u;
    return true;
}

uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer)
{
    assert(flags == 0u);
    submitted_id = id;
    submitted_operation = operation;
    submitted_lba = lba;
    submitted_sectors = sectors;
    submitted_buffer = physical_buffer;
    return fake_submit_error;
}

bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion)
{
    KernelPlatformBlockCompletion *source;

    if (completion == NULL || fake_completion_read == fake_completion_write)
        return false;
    source = &fake_completions[fake_completion_read % FAKE_COMPLETIONS];
    completion->id = source->id;
    completion->status = source->status;
    completion->sectors = source->sectors;
    completion->detail = source->detail;
    completion->media_generation = source->media_generation;
    completion->host_generation = source->host_generation;
    ++fake_completion_read;
    return true;
}

static void queue_completion(uint32_t id, uint16_t status, uint16_t sectors,
                             uint32_t detail, uint32_t media_generation,
                             uint32_t host_generation)
{
    KernelPlatformBlockCompletion *completion;

    assert(fake_completion_write - fake_completion_read < FAKE_COMPLETIONS);
    completion = &fake_completions[fake_completion_write % FAKE_COMPLETIONS];
    completion->id = id;
    completion->status = status;
    completion->sectors = sectors;
    completion->detail = detail;
    completion->media_generation = media_generation;
    completion->host_generation = host_generation;
    ++fake_completion_write;
}

static uint32_t service_completions(void)
{
    uint32_t serviced = 99u;

    assert(kernel_block_service(&serviced) == KERNEL_BLOCK_OK);
    return serviced;
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

    memset(&fake_state, 0, sizeof(fake_state));
    fake_state.capabilities = BLOCK_CAP_READ | BLOCK_CAP_WRITE |
                              BLOCK_CAP_FLUSH;
    fake_state.state_flags = BLOCK_STATE_LINK_UP |
                             BLOCK_STATE_MEDIA_PRESENT |
                             BLOCK_STATE_WRITE_ENABLE;
    fake_state.media_generation = 2u;
    fake_state.host_generation = 1u;
    fake_state.media_sectors = 10000u;
    fake_state.max_sectors = 16u;
    fake_completion_read = 0u;
    fake_completion_write = 0u;
    fake_submit_error = 0u;
    fake_state_available = true;
    submitted_id = 0u;
    submitted_operation = 0u;
    submitted_lba = 0u;
    submitted_sectors = 0u;
    submitted_buffer = 0u;
    kernel_block_init();
}

static void test_read_completion_and_generation_mismatch(void)
{
    KernelDmaHandle dma;
    KernelDmaBufferInfo dma_info;
    KernelBlockHandle request;
    KernelBlockHandle second_request;
    KernelBlockRequestInfo request_info;
    KernelBlockResult result;

    initialize_test();
    assert(kernel_dma_create(7u, 8192u, 1u, &dma) == KERNEL_DMA_OK);
    assert(kernel_dma_buffer_info(dma, 7u, &dma_info) == KERNEL_DMA_OK);
    assert(kernel_block_submit(7u, BLOCK_OP_READ, 5u, 2u, dma, 0u,
                               &request) == KERNEL_BLOCK_OK);
    assert(submitted_id == request && submitted_operation == BLOCK_OP_READ);
    assert(submitted_lba == 5u && submitted_sectors == 2u);
    assert(submitted_buffer == dma_info.physical_base);
    assert(kernel_block_request_info(request, 7u, &request_info) ==
           KERNEL_BLOCK_OK);
    assert(request_info.state == KERNEL_BLOCK_REQUEST_ACTIVE);
    assert(kernel_block_collect(request, 7u, &result) ==
           KERNEL_BLOCK_PENDING);

    queue_completion(request, 0u, 2u, 0x1234u, 2u, 1u);
    assert(service_completions() == 1u);
    assert(kernel_dma_buffer_info(dma, 7u, &dma_info) == KERNEL_DMA_OK);
    assert(dma_info.state == KERNEL_DMA_CPU_OWNED);
    assert(kernel_block_collect(request, 7u, &result) == KERNEL_BLOCK_OK);
    assert(result.status == 0u && result.sectors == 2u &&
           result.detail == 0x1234u);
    assert(kernel_block_collect(request, 7u, &result) ==
           KERNEL_BLOCK_INVALID_HANDLE);

    assert(kernel_block_submit(7u, BLOCK_OP_READ, 7u, 1u, dma, 0u,
                               &second_request) == KERNEL_BLOCK_OK);
    fake_state.media_generation = 3u;
    queue_completion(second_request, 0u, 1u, 0u, 3u, 1u);
    assert(service_completions() == 1u);
    assert(kernel_block_collect(second_request, 7u, &result) ==
           KERNEL_BLOCK_OK);
    assert(result.status == KERNEL_BLOCK_COMPLETION_MEDIA_CHANGED);
    assert(kernel_dma_close(dma, 7u) == KERNEL_DMA_OK);
}

static void test_validation_and_submit_rollback(void)
{
    KernelDmaHandle dma;
    KernelDmaBufferInfo info;
    KernelBlockHandle request = KERNEL_BLOCK_HANDLE_INVALID;
    KernelBlockStats stats;

    initialize_test();
    assert(kernel_dma_create(3u, 8192u, 1u, &dma) == KERNEL_DMA_OK);
    assert(kernel_block_submit(3u, BLOCK_OP_READ, 9999u, 2u, dma, 0u,
                               &request) == KERNEL_BLOCK_OUT_OF_RANGE);
    assert(kernel_block_submit(3u, BLOCK_OP_READ, 0u, 17u, dma, 0u,
                               &request) == KERNEL_BLOCK_INVALID_ARGUMENT);
    assert(kernel_block_submit(4u, BLOCK_OP_READ, 0u, 1u, dma, 0u,
                               &request) == KERNEL_BLOCK_NOT_OWNED);

    fake_submit_error = BLOCK_ERROR_QUEUE_FULL;
    assert(kernel_block_submit(3u, BLOCK_OP_READ, 0u, 1u, dma, 0u,
                               &request) == KERNEL_BLOCK_QUEUE_FULL);
    assert(request == KERNEL_BLOCK_HANDLE_INVALID);
    assert(kernel_dma_buffer_info(dma, 3u, &info) == KERNEL_DMA_OK);
    assert(info.state == KERNEL_DMA_CPU_OWNED);
    assert(kernel_block_stats(&stats));
    assert(stats.rejected == 1u);
    assert(kernel_dma_close(dma, 3u) == KERNEL_DMA_OK);
}

static void test_owner_revoke_and_late_completion(void)
{
    KernelDmaHandle idle;
    KernelDmaHandle active;
    KernelDmaBufferInfo info;
    KernelBlockHandle request;
    KernelBlockRequestInfo request_info;
    KernelBlockStats stats;
    uint32_t released = 0u;
    uint32_t deferred = 0u;
    uint32_t released_frames = 99u;

    initialize_test();
    assert(kernel_dma_create(9u, 4096u, 1u, &idle) == KERNEL_DMA_OK);
    assert(kernel_dma_create(9u, 4096u, 1u, &active) == KERNEL_DMA_OK);
    assert(kernel_block_submit(9u, BLOCK_OP_WRITE, 0u, 1u, active, 0u,
                               &request) == KERNEL_BLOCK_OK);
    assert(kernel_block_revoke_owner(9u, &released, &deferred) ==
           KERNEL_BLOCK_OK);
    assert(released == 1u && deferred == 1u);
    assert(kernel_dma_buffer_info(idle, 9u, &info) ==
           KERNEL_DMA_INVALID_HANDLE);
    assert(kernel_block_request_info(request, 9u, &request_info) ==
           KERNEL_BLOCK_OK);
    assert(request_info.state == KERNEL_BLOCK_REQUEST_REVOKING);

    queue_completion(request, 0u, 1u, 0u, 2u, 1u);
    assert(service_completions() == 1u);
    assert(kernel_block_request_info(request, 9u, &request_info) ==
           KERNEL_BLOCK_INVALID_HANDLE);
    assert(kernel_dma_buffer_info(active, 9u, &info) ==
           KERNEL_DMA_INVALID_HANDLE);
    assert(kernel_memory_release_owner(9u, &released_frames) ==
           KERNEL_MEMORY_OK);
    assert(released_frames == 0u);

    queue_completion(request, 0u, 1u, 0u, 2u, 1u);
    assert(service_completions() == 1u);
    assert(kernel_block_stats(&stats));
    assert(stats.revoked_requests == 1u);
    assert(stats.unknown_completions == 1u);
}

static void test_flush_has_no_dma_buffer(void)
{
    KernelBlockHandle request;
    KernelBlockResult result;

    initialize_test();
    assert(kernel_block_submit(5u, BLOCK_OP_FLUSH, 0u, 0u,
                               KERNEL_DMA_HANDLE_INVALID, 0u, &request) ==
           KERNEL_BLOCK_OK);
    assert(submitted_buffer == 0u && submitted_sectors == 0u);
    queue_completion(request, 0u, 0u, 0u, 2u, 1u);
    assert(service_completions() == 1u);
    assert(kernel_block_collect(request, 5u, &result) == KERNEL_BLOCK_OK);
    assert(result.status == 0u && result.sectors == 0u);
}

static void test_request_injection_does_not_take_dma_ownership(void)
{
    KernelAllocationStats allocation_stats;
    KernelBlockHandle request = 0xdeadbeefu;
    KernelBlockStats block_before;
    KernelBlockStats block_after;
    KernelDmaBufferInfo info;
    KernelDmaHandle dma;

    initialize_test();
    assert(kernel_dma_create(15u, KERNEL_PAGE_SIZE, 1u, &dma) ==
           KERNEL_DMA_OK);
    assert(kernel_block_stats(&block_before));
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_BLOCK_REQUEST, 1u);
    assert(kernel_block_submit(15u, BLOCK_OP_READ, 0u, 1u, dma, 0u,
                               &request) == KERNEL_BLOCK_QUEUE_FULL);
    assert(request == KERNEL_BLOCK_HANDLE_INVALID);
    assert(kernel_dma_buffer_info(dma, 15u, &info) == KERNEL_DMA_OK);
    assert(info.state == KERNEL_DMA_CPU_OWNED);
    assert(kernel_block_stats(&block_after));
    assert(block_after.submitted == block_before.submitted);
    assert(block_after.completed == block_before.completed);
    request = 0xdeadbeefu;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_block_submit(15u, BLOCK_OP_READ, 0u, 1u, dma, 0u,
                               &request) == KERNEL_BLOCK_QUEUE_FULL);
    assert(request == KERNEL_BLOCK_HANDLE_INVALID);
    assert(kernel_dma_buffer_info(dma, 15u, &info) == KERNEL_DMA_OK);
    assert(info.state == KERNEL_DMA_CPU_OWNED);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_BLOCK_REQUEST, &allocation_stats));
    assert(allocation_stats.current_units == 0u);
    assert(allocation_stats.injected_failures == 2u);
    assert(kernel_block_valid());
    assert(kernel_dma_valid());
    assert(kernel_allocation_valid());
    assert(kernel_dma_close(dma, 15u) == KERNEL_DMA_OK);
}

int main(void)
{
    test_read_completion_and_generation_mismatch();
    test_validation_and_submit_rollback();
    test_owner_revoke_and_late_completion();
    test_flush_has_no_dma_buffer();
    test_request_injection_does_not_take_dma_ownership();
    puts("KERNEL BLOCK PASS");
    return 0;
}
