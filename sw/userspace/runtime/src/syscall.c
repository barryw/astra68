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
astra_irq_arm(uint32_t handle)
{
    return invoke(ASTRA_SYSCALL_IRQ_ARM, handle).status;
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
