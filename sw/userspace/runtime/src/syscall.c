#include <astra/runtime.h>
#include <astra/syscall.h>

static AstraSyscallResult
invoke(uint32_t number, uint32_t argument0)
{
    AstraSyscallResult result;

    astra_syscall5(number, argument0, 0u, 0u, 0u, 0u, &result);
    return result;
}

uint32_t
astra_progress(uint32_t value)
{
    return invoke(ASTRA_SYSCALL_PROGRESS, value).status;
}

uint32_t
astra_rt_handle_duplicate(uint32_t handle, uint32_t rights, uint32_t *duplicate)
{
    AstraSyscallResult result;

    if (duplicate == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_HANDLE_DUPLICATE, handle, rights, 0u, 0u, 0u,
                   &result);
    if (result.status == ASTRA_SYSCALL_OK)
        *duplicate = result.value0;
    return result.status;
}

uint32_t
astra_rt_area_create_flagged(uint32_t byte_size, uint32_t rights,
                             uint32_t flags, uint32_t *handle)
{
    AstraSyscallResult result;

    if (handle == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_AREA_CREATE, byte_size, rights, flags, 0u,
                   0u, &result);
    if (result.status == ASTRA_SYSCALL_OK)
        *handle = result.value0;
    return result.status;
}

uint32_t
astra_rt_area_create(uint32_t byte_size, uint32_t rights, uint32_t *handle)
{
    return astra_rt_area_create_flagged(byte_size, rights, 0u, handle);
}

uint32_t
astra_rt_area_decommit(void *address, uint32_t byte_size,
                    uint32_t *released_pages)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_AREA_DECOMMIT, (uint32_t)(uintptr_t)address,
                   byte_size, 0u, 0u, 0u, &result);
    if (result.status == ASTRA_SYSCALL_OK && released_pages != NULL)
        *released_pages = result.value0;
    return result.status;
}

uint32_t
astra_rt_area_map(uint32_t handle, uint32_t permissions, void **address,
               uint32_t *byte_size)
{
    AstraSyscallResult result;

    if (address == NULL || byte_size == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_AREA_MAP, handle, permissions, 0u, 0u, 0u,
                   &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *address = (void *)(uintptr_t)result.value0;
        *byte_size = result.value1;
    }
    return result.status;
}

uint32_t
astra_rt_area_unmap(void *address)
{
    AstraSyscallResult result;

    if (address == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_AREA_UNMAP, (uint32_t)(uintptr_t)address,
                   0u, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_rt_library_map(const void *image, uint32_t length, uint32_t *base,
                     uint32_t *span)
{
    AstraSyscallResult result;

    if (image == NULL || length == 0u || base == NULL || span == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *base = 0u;
    *span = 0u;
    astra_syscall5(ASTRA_SYSCALL_LIBRARY_MAP, (uint32_t)(uintptr_t)image,
                   length, 0u, 0u, 0u, &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *base = result.value0;
        *span = result.value1;
    }
    return result.status;
}

uint32_t
astra_device_query(uint32_t handle, AstraDeviceInfo *info)
{
    AstraSyscallResult result;

    if (info == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_DEVICE_QUERY, handle,
                   (uint32_t)(uintptr_t)info, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info)
{
    AstraSyscallResult result;

    if (info == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_DMA_CREATE, byte_size,
                   (uint32_t)(uintptr_t)info, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_block_lease_query(uint32_t device, AstraBlockLeaseInfo *geometry)
{
    AstraSyscallResult result;

    if (geometry == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_BLOCK_QUERY, device,
                   (uint32_t)(uintptr_t)geometry, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_block_lease_submit(uint32_t device, const AstraBlockRequest *request,
                   uint32_t *block_request)
{
    AstraSyscallResult result;

    if (request == NULL || block_request == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_BLOCK_SUBMIT, device,
                   (uint32_t)(uintptr_t)request, 0u, 0u, 0u, &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *block_request = result.value0;
    }
    return result.status;
}

uint32_t
astra_block_lease_collect(uint32_t device, uint32_t block_request,
                    AstraBlockCompletion *completion)
{
    AstraSyscallResult result;

    if (completion == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_BLOCK_COLLECT, device,
                   (uint32_t)(uintptr_t)completion, block_request, 0u, 0u,
                   &result);
    return result.status;
}

uint64_t
astra_clock_monotonic(void)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_CLOCK_MONOTONIC, 0u, 0u, 0u, 0u, 0u,
                   &result);
    if (result.status != ASTRA_SYSCALL_OK) {
        return 0u;
    }
    return ((uint64_t)result.value0 << 32) | result.value1;
}

/*
 * The date, and whether the machine has one. Zero is a real instant, so the
 * answer to "what time is it" cannot be a number alone: a caller that ignores
 * the status and stamps a file has written midnight in 1970 and called it a
 * fact.
 */
uint32_t
astra_clock_realtime(uint64_t *nanoseconds)
{
    return astra_clock_realtime_zone(nanoseconds, NULL);
}

/*
 * The instant and the zone in one call, because they belong to one moment: a
 * program that asked for them separately could straddle a summer-time change
 * and render an hour that never happened.
 */
uint32_t
astra_clock_realtime_zone(uint64_t *nanoseconds, AstraTimeZone *zone)
{
    AstraSyscallResult result;

    if (nanoseconds == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_CLOCK_REALTIME, 0u, 0u, 0u, 0u, 0u,
                   &result);
    if (result.status != ASTRA_SYSCALL_OK) {
        return result.status;
    }
    *nanoseconds = ((uint64_t)result.value0 << 32) | result.value1;
    if (zone != NULL) {
        astra_civil_zone_unpack((int32_t)result.value2, result.value3, zone);
    }
    return ASTRA_SYSCALL_OK;
}

/*
 * Deadlines are absolute monotonic nanoseconds. ASTRA_DEADLINE_NONE waits
 * forever, which a service may do but a boot check may not.
 */
uint32_t
astra_wait_one(uint32_t handle, uint64_t deadline_ns, uint32_t *detail)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_WAIT_ONE, handle,
                   (uint32_t)(deadline_ns >> 32), (uint32_t)deadline_ns, 0u,
                   0u, &result);
    if (detail != NULL) {
        *detail = result.value0;
    }
    return result.status;
}

uint32_t
astra_wait_multiple(const uint32_t *handles, uint32_t count,
                    uint64_t deadline_ns, uint32_t *index, uint32_t *detail)
{
    AstraSyscallResult result;

    if (handles == NULL || count == 0u || count > ASTRA_WAIT_MULTIPLE_MAX)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_WAIT_MULTIPLE,
                   (uint32_t)(uintptr_t)handles, count,
                   (uint32_t)(deadline_ns >> 32), (uint32_t)deadline_ns, 0u,
                   &result);
    if (index != NULL)
        *index = result.value0;
    if (detail != NULL)
        *detail = result.value1;
    return result.status;
}

uint32_t
astra_irq_arm(uint32_t handle)
{
    return invoke(ASTRA_SYSCALL_IRQ_ARM, handle).status;
}

uint32_t
astra_irq_mask(uint32_t handle)
{
    return invoke(ASTRA_SYSCALL_IRQ_MASK, handle).status;
}

uint32_t
astra_irq_read(uint32_t handle, AstraIrqRecord *record, uint32_t *events)
{
    AstraSyscallResult result;

    if (record == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_IRQ_READ, handle,
                   (uint32_t)(uintptr_t)record, 0u, 0u, 0u, &result);
    if (events != NULL) {
        *events = result.value0;
    }
    return result.status;
}

uint32_t
astra_irq_ack(uint32_t handle, uint32_t sequence)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_IRQ_ACK, handle, sequence, 0u, 0u, 0u,
                   &result);
    return result.status;
}

uint32_t
astra_device_reset(uint32_t handle)
{
    return invoke(ASTRA_SYSCALL_DEVICE_RESET, handle).status;
}

uint32_t
astra_process_info(uint32_t handle, AstraProcessInfo *info)
{
    AstraSyscallResult result;

    if (info == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_PROCESS_INFO, handle,
                   (uint32_t)(uintptr_t)info, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_yield(void)
{
    return invoke(ASTRA_SYSCALL_YIELD, 0u).status;
}

uint32_t
astra_close(uint32_t handle)
{
    return invoke(ASTRA_SYSCALL_CLOSE, handle).status;
}

uint32_t
astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                uint32_t *thread_handle)
{
    AstraSyscallResult result = invoke(ASTRA_SYSCALL_QUERY_ABI, 0u);

    if (result.status == ASTRA_SYSCALL_OK) {
        if (abi_version != NULL) {
            *abi_version = result.value0;
        }
        if (process_handle != NULL) {
            *process_handle = result.value1;
        }
        if (thread_handle != NULL) {
            *thread_handle = result.value2;
        }
    }
    return result.status;
}

void
astra_process_exit(uint32_t status)
{
    (void)invoke(ASTRA_SYSCALL_PROCESS_EXIT, status);
    for (;;) {
    }
}

void
astra_thread_exit(uint32_t status)
{
    (void)invoke(ASTRA_SYSCALL_THREAD_EXIT, status);
    for (;;) {
    }
}

uint32_t
astra_console_info(uint32_t device, uint32_t *columns, uint32_t *rows)
{
    AstraSyscallResult result;

    if (columns == NULL || rows == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_CONSOLE_INFO, device, 0u, 0u, 0u, 0u,
                   &result);
    if (result.status == ASTRA_SYSCALL_OK) {
        *columns = result.value0;
        *rows = result.value1;
    }
    return result.status;
}

uint32_t
astra_console_write(uint32_t device, uint32_t cell, const uint8_t *cells,
                    uint32_t count)
{
    AstraSyscallResult result;

    if (cells == NULL || count == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_CONSOLE_WRITE, device, cell,
                   (uint32_t)(uintptr_t)cells, count, 0u, &result);
    return result.status;
}

uint32_t
astra_console_cursor(uint32_t device, uint32_t row, uint32_t column,
                     uint32_t visible)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_CONSOLE_CURSOR, device, row, column,
                   visible, 0u, &result);
    return result.status;
}

uint32_t
astra_display_submit(uint32_t device,
                     const AstraDisplayFrameRequest *request)
{
    AstraSyscallResult result;

    if (request == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_DISPLAY_SUBMIT, device,
                   (uint32_t)(uintptr_t)request, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_display_collect(uint32_t device,
                      AstraDisplayFrameCompletion *completion)
{
    AstraSyscallResult result;

    if (completion == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_syscall5(ASTRA_SYSCALL_DISPLAY_COLLECT, device,
                   (uint32_t)(uintptr_t)completion, 0u, 0u, 0u, &result);
    return result.status;
}

uint32_t
astra_input_read(uint32_t device, AstraInputEvent *events, uint32_t capacity,
                 uint32_t *count, uint32_t *flags)
{
    AstraSyscallResult result;

    if (events == NULL || count == NULL || flags == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *count = 0u;
    *flags = 0u;
    astra_syscall5(ASTRA_SYSCALL_INPUT_READ_TRY, device,
                   (uint32_t)(uintptr_t)events, capacity, 0u, 0u, &result);
    *count = result.value0;
    *flags = result.value1;
    return result.status;
}
