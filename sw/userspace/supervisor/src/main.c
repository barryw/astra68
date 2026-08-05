#include <supervisor.h>

#include <astra/block.h>
#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

/*
 * The first user image firmware hands to the kernel, and the block service
 * until there is a launch path for a separate one.
 *
 * It proves the bring-up path end to end on every boot: the startup block the
 * loader published, the ABI the syscall layer reports, the process view the
 * kernel renders of the caller, and the block lease and completion endpoint it
 * was granted at launch. Each stage is reported through the progress counter,
 * so the boot log shows how far the service came up rather than only whether
 * it started. The registrar and service lifecycle described in
 * docs/USERSPACE_ARCHITECTURE.md grow from here.
 *
 * This service is resident. It never returns: an exit is a boot failure the
 * kernel turns into a panic, because nothing else can start what it would
 * have started.
 */

/*
 * A boot check must terminate whether or not the device answers, so the wait
 * carries a deadline rather than trusting the device to complete. Two seconds
 * is far beyond a 16-sector transfer on this transport and far below a hang
 * anyone would sit through.
 */
#define BLOCK_REQUEST_TIMEOUT_NS 2000000000u

static const AstraStartupCapability *
find_capability(const AstraStartupCapability *capabilities, uint32_t count,
                uint32_t name)
{
    for (uint32_t index = 0u; index < count; ++index) {
        if (capabilities[index].name == name) {
            return &capabilities[index];
        }
    }
    return NULL;
}

/* Returns 0 when the granted block objects are absent or unusable. */
static uint32_t
claim_block_lease(const AstraStartupInfo *startup,
                  const AstraStartupCapability *capabilities,
                  uint32_t *irq_handle)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    AstraDeviceInfo info;

    /*
     * Validation upstream guarantees both, but this is the boundary where a
     * missing capability table stops being a launch error and starts being a
     * null dereference in the block path.
     */
    if (startup == NULL || capabilities == NULL || irq_handle == NULL) {
        return 0u;
    }
    *irq_handle = 0u;

    device = find_capability(capabilities, startup->capability_count,
                             ASTRA_CAPABILITY_BLOCK_DEVICE);
    irq = find_capability(capabilities, startup->capability_count,
                          ASTRA_CAPABILITY_BLOCK_IRQ);
    if (device == NULL || irq == NULL || device->handle == 0u ||
        irq->handle == 0u) {
        /* A machine without media boots without a block service. */
        return 0u;
    }

    (void)memset(&info, 0, sizeof(info));
    if (astra_device_query(device->handle, &info) != ASTRA_SYSCALL_OK ||
        info.size != ASTRA_DEVICE_INFO_SIZE ||
        info.device_id != ASTRA_DEVICE_ID_BLOCK0 ||
        info.class_id != ASTRA_DEVICE_CLASS_BLOCK ||
        info.generation == 0u) {
        return 0u;
    }
    *irq_handle = irq->handle;
    return device->handle;
}

/*
 * Consumes a delivered interrupt so the endpoint can be armed again. A record
 * must be read and acknowledged before the next arm; leaving one queued would
 * refuse every later arm and strand the service.
 */
static uint32_t
drain_endpoint(uint32_t irq)
{
    AstraIrqRecord record;
    uint32_t events = 0u;
    uint32_t status;

    (void)memset(&record, 0, sizeof(record));
    status = astra_irq_read(irq, &record, &events);
    if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
        return ASTRA_SYSCALL_OK; /* nothing was delivered */
    }
    if (status != ASTRA_SYSCALL_OK) {
        return status;
    }
    return astra_irq_ack(irq, record.sequence);
}

/*
 * Waits on the completion endpoint the service was granted, rather than
 * spinning on the collect call.
 *
 * The endpoint is armed before the request is submitted, never after. A
 * granted endpoint starts masked, so the interrupt source is disabled until
 * the first arm: arming after submitting leaves a window in which the
 * completion fires into a disabled source and the service then waits for an
 * interrupt that already happened.
 *
 * Collecting first on each pass is the fast path for a device that answered
 * while we were still asking, and a spurious wake simply finds the request
 * pending and waits again. On timeout the device is reset, which ends every
 * in-flight request with a status the service can still collect; that is what
 * turns a device that never answers into a bounded failure instead of a hang.
 */
static uint32_t
await_completion(uint32_t device, uint32_t irq, uint32_t block_request,
                 AstraBlockCompletion *completion)
{
    uint64_t deadline = astra_clock_monotonic() + BLOCK_REQUEST_TIMEOUT_NS;
    uint32_t status;

    for (;;) {
        status = astra_block_collect(device, block_request, completion);
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            if (status != ASTRA_SYSCALL_OK) {
                return ASTRA_SUPERVISOR_FAIL_BLOCK_IO;
            }
            /* Leave the endpoint clean for whoever submits next. */
            return drain_endpoint(irq) == ASTRA_SYSCALL_OK ?
                0u : ASTRA_SUPERVISOR_FAIL_BLOCK_DRAIN;
        }

        status = astra_wait_one(irq, deadline, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            break;
        }
        if (status != ASTRA_SYSCALL_OK) {
            return ASTRA_SUPERVISOR_FAIL_BLOCK_WAIT;
        }
        if (drain_endpoint(irq) != ASTRA_SYSCALL_OK) {
            return ASTRA_SUPERVISOR_FAIL_BLOCK_DRAIN;
        }
        if (astra_irq_arm(irq) != ASTRA_SYSCALL_OK) {
            return ASTRA_SUPERVISOR_FAIL_BLOCK_ARM;
        }
    }

    /* The device did not answer. Reset it and collect what that produced. */
    if (astra_device_reset(device) != ASTRA_SYSCALL_OK ||
        astra_block_collect(device, block_request, completion) !=
            ASTRA_SYSCALL_OK ||
        completion->status != ASTRA_BLOCK_COMPLETION_RESET) {
        return ASTRA_SUPERVISOR_FAIL_BLOCK_WAIT;
    }
    return ASTRA_SUPERVISOR_FAIL_BLOCK_TIMEOUT;
}

/*
 * One real transfer through the admission path, at every boot: geometry, a
 * transfer buffer the service owns, a read of the first sector, and a
 * collected completion. If any of it is wrong the service exits and the
 * kernel turns that into a panic naming the check, which is the whole point of
 * proving it here rather than trusting it.
 */
static uint32_t
verify_block_round_trip(uint32_t device, uint32_t irq)
{
    AstraBlockGeometry geometry;
    AstraDmaBufferInfo buffer;
    AstraBlockRequest request;
    AstraBlockCompletion completion;
    uint32_t block_request = 0u;
    uint32_t status;

    (void)memset(&geometry, 0, sizeof(geometry));
    if (astra_block_query(device, &geometry) != ASTRA_SYSCALL_OK ||
        geometry.size != ASTRA_BLOCK_GEOMETRY_SIZE ||
        geometry.sector_bytes != ASTRA_BLOCK_SECTOR_BYTES ||
        geometry.max_transfer_sectors == 0u ||
        geometry.sector_count == 0u ||
        (geometry.capabilities & ASTRA_BLOCK_CAP_READ) == 0u ||
        (geometry.state_flags & ASTRA_BLOCK_STATE_LINK_UP) == 0u ||
        (geometry.state_flags & ASTRA_BLOCK_STATE_MEDIA_PRESENT) == 0u) {
        return ASTRA_SUPERVISOR_FAIL_BLOCK_GEOMETRY;
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_ONLINE);

    (void)memset(&buffer, 0, sizeof(buffer));
    if (astra_dma_create(geometry.sector_bytes, &buffer) !=
            ASTRA_SYSCALL_OK ||
        buffer.size != ASTRA_DMA_BUFFER_INFO_SIZE ||
        buffer.handle == 0u || buffer.virtual_base == 0u ||
        buffer.byte_size < geometry.sector_bytes) {
        return ASTRA_SUPERVISOR_FAIL_BLOCK_MEMORY;
    }

    /* Armed before submission: the completion must not be able to precede it. */
    if (astra_irq_arm(irq) != ASTRA_SYSCALL_OK) {
        (void)astra_close(buffer.handle);
        return ASTRA_SUPERVISOR_FAIL_BLOCK_ARM;
    }

    (void)memset(&request, 0, sizeof(request));
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.operation = ASTRA_BLOCK_OP_READ;
    request.buffer = buffer.handle;
    request.sectors = 1u;
    request.lba = 0u;
    if (astra_block_submit(device, &request, &block_request) !=
            ASTRA_SYSCALL_OK || block_request == 0u) {
        (void)astra_close(buffer.handle);
        return ASTRA_SUPERVISOR_FAIL_BLOCK_IO;
    }

    (void)memset(&completion, 0, sizeof(completion));
    status = await_completion(device, irq, block_request, &completion);
    (void)astra_close(buffer.handle);
    if (status != 0u) {
        return status;
    }
    if (completion.size != ASTRA_BLOCK_COMPLETION_SIZE ||
        completion.request != block_request ||
        completion.status != ASTRA_BLOCK_COMPLETION_OK ||
        completion.sectors != 1u) {
        return ASTRA_SUPERVISOR_FAIL_BLOCK_IO;
    }
    return 0u;
}

static void
park(void)
{
    for (;;) {
        (void)astra_yield();
    }
}

int
astra_main(const AstraStartupInfo *startup)
{
    SupervisorProbe probe;
    const AstraStartupCapability *capabilities = NULL;
    uint32_t status;
    uint32_t block_device;
    uint32_t block_irq = 0u;

    probe.query_status = astra_query_abi(&probe.abi_version,
                                         &probe.process_handle,
                                         &probe.thread_handle);
    if (probe.query_status != ASTRA_SYSCALL_OK) {
        probe.abi_version = 0u;
        probe.process_handle = 0u;
        probe.thread_handle = 0u;
    }

    (void)memset(&probe.info, 0, sizeof(probe.info));
    probe.info_status = startup != NULL ?
        astra_process_info(startup->process_handle, &probe.info) :
        ASTRA_SYSCALL_INVALID_ARGUMENT;

    if (startup != NULL && startup->capabilities_address != 0u) {
        capabilities = (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;
    }

    status = supervisor_validate(startup, capabilities, &probe);
    if (status != ASTRA_SUPERVISOR_STATUS_OK) {
        return (int)status;
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_SELF_VERIFIED);

    block_device = claim_block_lease(startup, capabilities, &block_irq);
    if (block_device == 0u) {
        /*
         * Not a failure: without media the kernel grants nothing and there is
         * no block service to run. The service still stays resident.
         */
        park();
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_LEASED);

    status = verify_block_round_trip(block_device, block_irq);
    if (status != 0u) {
        return (int)(ASTRA_SUPERVISOR_STATUS_TAG | status);
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_VERIFIED);

    park();
    return (int)ASTRA_SUPERVISOR_STATUS_OK;
}
