#include <astra/runtime.h>
#include <astra/syscall.h>

int
astra_startup_validate(const AstraStartupInfo *startup)
{
    if (startup == NULL || startup->magic != ASTRA_STARTUP_MAGIC ||
        startup->abi_version != ASTRA_STARTUP_ABI_VERSION ||
        startup->header_size != ASTRA_STARTUP_INFO_SIZE ||
        startup->total_size < ASTRA_STARTUP_INFO_SIZE ||
        startup->syscall_abi_version != ASTRA_SYSCALL_ABI_VERSION ||
        startup->capability_count > ASTRA_STARTUP_CAPABILITY_MAX ||
        startup->launch_source > ASTRA_LAUNCH_SOURCE_DESKTOP) {
        return 0;
    }
    if ((startup->argc != 0u && startup->argv_address == 0u) ||
        (startup->environment_count != 0u &&
         startup->environment_address == 0u) ||
        (startup->capability_count != 0u &&
         startup->capabilities_address == 0u)) {
        return 0;
    }

    if ((startup->reserved[0] | startup->reserved[1]) != 0u) {
        return 0;
    }
    /*
     * The diagnostic channel is bound here because this is the one call every
     * program makes before it does anything else, and because a handle that
     * has not been validated is not one to log through.
     */
    return 1;
}

AstraLaunchSource
astra_startup_launch_source(const AstraStartupInfo *startup)
{
    return astra_startup_validate(startup) ?
        (AstraLaunchSource)startup->launch_source :
        ASTRA_LAUNCH_SOURCE_SYSTEM;
}

const char *
astra_startup_argument(const AstraStartupInfo *startup, uint32_t index)
{
    const uint32_t *arguments;

    if (!astra_startup_validate(startup) || index >= startup->argc)
        return NULL;
    arguments = (const uint32_t *)(uintptr_t)startup->argv_address;
    return (const char *)(uintptr_t)arguments[index];
}
