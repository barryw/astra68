#include <loader.h>
#include <supervisor.h>
#include <volume.h>

#include <astra/event_emit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/vfs_service.h>

#include <string.h>

/*
 * The first user image firmware hands to the kernel and the protected-service
 * loader.
 *
 * It proves the bring-up path end to end on every boot: the startup block the
 * loader published, the ABI the syscall layer reports, the process view the
 * kernel renders of the caller, and the canonical provider
 * session it was granted at launch. Each stage is reported through the progress counter,
 * so the boot log shows how far the service came up rather than only whether
 * it started. Services publish one port back to this loader and no registrar
 * exists.
 *
 * This process is resident after a successful startup. A startup failure
 * returns an exit status to the kernel's degraded-boot path.
 */

/*
 * The first image on the machine declares what it is, like every other one.
 * The rule that every program carries its own provenance has no exceptions,
 * and that is the only way a rule about every program survives the first
 * program that finds it inconvenient. Layout spec 11.2.
 */
ASTRA_PROGRAM("supervisor", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    SupervisorProbe probe;
    const AstraStartupCapability *capabilities = NULL;
    uint32_t status;

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

    status = supervisor_volume_open(startup);
    if (status != ASTRA_VFS_OK)
        return (int)(ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_VOLUME);
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_FOUND);
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_MOUNTED);
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED);

    status = supervisor_loader_start(startup);
    if (status != ASTRA_STATUS_OK) {
        (void)supervisor_volume_source_close(NULL);
        (void)supervisor_volume_unmount();
        return (int)status;
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_TERMINAL);

    return (int)supervisor_loader_watch(startup);
}
