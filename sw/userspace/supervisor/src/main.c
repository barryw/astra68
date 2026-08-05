#include <supervisor.h>

#include <astra/bytes.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

/*
 * The first user image firmware hands to the kernel. Today it proves the whole
 * bring-up path end to end: the startup block the loader published, the ABI
 * the syscall layer reports, and the process view the kernel renders of the
 * caller. The registrar and service lifecycle described in
 * docs/USERSPACE_ARCHITECTURE.md grow from here.
 */
int
astra_main(const AstraStartupInfo *startup)
{
    SupervisorProbe probe;
    const AstraStartupCapability *capabilities = NULL;

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

    return (int)supervisor_validate(startup, capabilities, &probe);
}
