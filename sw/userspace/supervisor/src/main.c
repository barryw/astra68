#include <loader.h>
#include <supervisor.h>
#include <volume.h>

#include <astra/block.h>
#include <astra/display.h>
#include <astra/event_emit.h>
#include <astra/block_device.h>
#include <astra/lease_block.h>
#include <astra/bytes.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

/*
 * The first user image firmware hands to the kernel and the protected-service
 * loader.
 *
 * It proves the bring-up path end to end on every boot: the startup block the
 * loader published, the ABI the syscall layer reports, the process view the
 * kernel renders of the caller, and the block lease and completion endpoint it
 * was granted at launch. Each stage is reported through the progress counter,
 * so the boot log shows how far the service came up rather than only whether
 * it started. Services publish one port back to this loader and no registrar
 * exists.
 *
 * This process is resident. It never returns: an exit is a boot failure the
 * kernel turns into a panic, because nothing else can start the manifest.
 */

/*
 * The first image on the machine declares what it is, like every other one.
 * The rule that every program carries its own provenance has no exceptions,
 * and that is the only way a rule about every program survives the first
 * program that finds it inconvenient. Layout spec 11.2.
 */
ASTRA_PROGRAM("supervisor", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static uint64_t
supervisor_clock(void *context)
{
    (void)context;
    return astra_clock_monotonic();
}

static const AstraStartupCapability *
find_capability(const AstraStartupCapability *capabilities, uint32_t count,
                const char *name)
{
    for (uint32_t index = 0u; index < count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, name)) {
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
 * One real transfer at every boot, through the same facade a filesystem will
 * use rather than around it: attach the lease-backed backend, read the first
 * sector, and check what came back. Proving the path a filesystem takes is
 * worth more than proving a path only this check takes.
 */
/*
 * The lease and the device facade have process lifetime, not call lifetime.
 *
 * They were locals until boot began leaving the bootstrap volume mounted for
 * the loader. A mounted volume keeps a pointer to the device, so these two
 * outlived the frame they were declared in: the shell's stack reused
 * that memory, every filesystem write read a corrupted geometry out of it and
 * was refused as ASTRA_BLOCK_TRANSFER_TOO_LARGE, and reads kept working out of
 * lwext4's cache. That reads as a filesystem defect and is not one.
 */
static AstraLeaseBlock lease;
static AstraBlockDevice block;

void
supervisor_bootstrap_block_release(void)
{
    (void)astra_irq_mask(lease.irq);
    astra_lease_block_detach(&lease);
}

void
supervisor_bootstrap_block_close(void)
{
    (void)astra_close(lease.device);
    (void)astra_close(lease.irq);
    lease.device = 0u;
    lease.irq = 0u;
}

static uint32_t
verify_block_round_trip(uint32_t device, uint32_t irq)
{
    AstraBlockGeometry geometry;
    uint8_t sector[ASTRA_BLOCK_SECTOR_BYTES];
    uint32_t failure = 0u;

    if (astra_lease_block_attach(&lease, device, irq) != ASTRA_BLOCK_OK) {
        return ASTRA_SUPERVISOR_FAIL_BLOCK_MEMORY;
    }
    astra_block_device_init(&block, astra_lease_block_backend(), &lease,
                            supervisor_clock, NULL);

    (void)memset(&geometry, 0, sizeof(geometry));
    if (astra_block_query(&block, &geometry) != ASTRA_BLOCK_OK ||
        geometry.sector_size != ASTRA_BLOCK_SECTOR_BYTES ||
        geometry.sector_count == 0u ||
        geometry.max_transfer_sectors == 0u ||
        (geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u) {
        failure = ASTRA_SUPERVISOR_FAIL_BLOCK_GEOMETRY;
    } else {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_ONLINE);
        (void)memset(sector, 0, sizeof(sector));
        switch (astra_block_read(&block, 0u, 1u, sector, 0u)) {
        case ASTRA_BLOCK_OK:
            break;
        case ASTRA_BLOCK_TIMED_OUT:
            failure = ASTRA_SUPERVISOR_FAIL_BLOCK_TIMEOUT;
            break;
        default:
            failure = ASTRA_SUPERVISOR_FAIL_BLOCK_IO;
            break;
        }
    }

    /*
     * The volume is mounted while the lease is still held. Detaching and
     * reattaching would claim transfer memory twice, and that resource is
     * hard-capped precisely so it cannot be done casually.
     */
    if (failure == 0u) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_BLOCK_VERIFIED);
        /* The loader reads its manifest and first service before unmounting. */
        failure = supervisor_verify_volume(&block, 1);
    }

    /*
     * The lease is what the mounted volume transfers through, so it may only be
     * released once nothing holds it. The loader unmounts after reading the
     * manifest and storage image; failures before then are detected here.
     */
    if (!supervisor_volume_is_mounted()) {
        astra_lease_block_detach(&lease);
    }
    return failure;
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

    status = supervisor_loader_start(startup, capabilities);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_TERMINAL);

    return (int)supervisor_loader_watch();
}
