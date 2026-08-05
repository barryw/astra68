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

    park();
    return (int)ASTRA_SUPERVISOR_STATUS_OK;
}
