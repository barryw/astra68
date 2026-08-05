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

/* A boot check must terminate whether or not the device answers. */
#define BLOCK_POLL_LIMIT 100000u

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
                  const AstraStartupCapability *capabilities)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    AstraDeviceInfo info;

    /*
     * Validation upstream guarantees both, but this is the boundary where a
     * missing capability table stops being a launch error and starts being a
     * null dereference in the block path.
     */
    if (startup == NULL || capabilities == NULL) {
        return 0u;
    }

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
    return device->handle;
}

/*
 * One real transfer through the admission path, at every boot: geometry, a
 * transfer buffer the service owns, a read of the first sector, and a
 * collected completion. If any of it is wrong the service exits and the
 * kernel turns that into a panic naming the check, which is the whole point of
 * proving it here rather than trusting it.
 */
static uint32_t
verify_block_round_trip(uint32_t device)
{
    AstraBlockGeometry geometry;
    AstraDmaBufferInfo buffer;
    AstraBlockRequest request;
    AstraBlockCompletion completion;
    uint32_t block_request = 0u;
    uint32_t status;
    uint32_t attempt;

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

    /*
     * Bounded polling with yields. The completion endpoint this service holds
     * is the right thing to wait on, and the block service loop will; a boot
     * check that must terminate either way does not need it yet.
     */
    (void)memset(&completion, 0, sizeof(completion));
    status = ASTRA_SYSCALL_WOULD_BLOCK;
    for (attempt = 0u; attempt < BLOCK_POLL_LIMIT; ++attempt) {
        status = astra_block_collect(device, block_request, &completion);
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            break;
        }
        (void)astra_yield();
    }
    (void)astra_close(buffer.handle);
    if (status != ASTRA_SYSCALL_OK ||
        completion.size != ASTRA_BLOCK_COMPLETION_SIZE ||
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

    block_device = claim_block_lease(startup, capabilities);
    if (block_device == 0u) {
        /*
         * Not a failure: without media the kernel grants nothing and there is
         * no block service to run. The service still stays resident.
         */
        park();
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_LEASED);

    status = verify_block_round_trip(block_device);
    if (status != 0u) {
        return (int)(ASTRA_SUPERVISOR_STATUS_TAG | status);
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_VERIFIED);

    park();
    return (int)ASTRA_SUPERVISOR_STATUS_OK;
}
