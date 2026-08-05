#include <supervisor.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

static int
capability_matches(const AstraStartupCapability *capabilities, uint32_t count,
                   uint32_t name, uint32_t handle)
{
    for (uint32_t index = 0u; index < count; ++index) {
        if (capabilities[index].name != name) {
            continue;
        }
        return capabilities[index].handle == handle &&
               capabilities[index].rights != 0u;
    }
    return 0;
}

uint32_t
supervisor_validate(const AstraStartupInfo *startup,
                    const AstraStartupCapability *capabilities,
                    const SupervisorProbe *probe)
{
    uint32_t status = ASTRA_SUPERVISOR_STATUS_TAG;

    if (probe == NULL) {
        return status | ASTRA_SUPERVISOR_FAIL_STARTUP;
    }
    if (!astra_startup_validate(startup) || startup->process_handle == 0u ||
        startup->thread_handle == 0u) {
        /* Nothing below can be trusted once the startup block is not ours. */
        return status | ASTRA_SUPERVISOR_FAIL_STARTUP;
    }

    if (probe->query_status != ASTRA_SYSCALL_OK) {
        status |= ASTRA_SUPERVISOR_FAIL_QUERY_ABI;
    } else {
        if (probe->abi_version != ASTRA_SYSCALL_ABI_VERSION) {
            status |= ASTRA_SUPERVISOR_FAIL_ABI_VERSION;
        }
        /*
         * The startup block and the syscall must name the same objects. If
         * they disagree, one of the two paths published a stale handle and
         * every later capability transfer would inherit the error.
         */
        if (probe->process_handle != startup->process_handle ||
            probe->thread_handle != startup->thread_handle) {
            status |= ASTRA_SUPERVISOR_FAIL_SELF_HANDLES;
        }
    }

    if (capabilities == NULL || startup->capabilities_address == 0u ||
        startup->capability_count < 2u ||
        !capability_matches(capabilities, startup->capability_count,
                            ASTRA_CAPABILITY_PROCESS,
                            startup->process_handle) ||
        !capability_matches(capabilities, startup->capability_count,
                            ASTRA_CAPABILITY_THREAD,
                            startup->thread_handle)) {
        status |= ASTRA_SUPERVISOR_FAIL_CAPABILITIES;
    }

    if (probe->info_status != ASTRA_SYSCALL_OK) {
        status |= ASTRA_SUPERVISOR_FAIL_PROCESS_INFO;
    } else if (probe->info.size != ASTRA_PROCESS_INFO_SIZE ||
               probe->info.id == 0u || probe->info.generation == 0u ||
               probe->info.owner == 0u || probe->info.resident_frames == 0u ||
               probe->info.thread_count == 0u ||
               probe->info.live_threads == 0u ||
               probe->info.live_threads > probe->info.thread_count ||
               probe->info.exit_reason != 0u || probe->info.exit_status != 0u) {
        /* A live process reporting an exit is the kernel describing a ghost. */
        status |= ASTRA_SUPERVISOR_FAIL_INFO_CONTENT;
    }

    return status;
}
