#include <astra/runtime.h>
#include <astra/syscall.h>

int
astra_startup_validate(const AstraStartupInfo *startup)
{
    uint32_t reserved_or;

    if (startup == NULL || startup->magic != ASTRA_STARTUP_MAGIC ||
        startup->abi_version != ASTRA_STARTUP_ABI_VERSION ||
        startup->header_size != ASTRA_STARTUP_INFO_SIZE ||
        startup->total_size < ASTRA_STARTUP_INFO_SIZE ||
        startup->syscall_abi_version != ASTRA_SYSCALL_ABI_VERSION ||
        startup->capability_count > ASTRA_STARTUP_CAPABILITY_MAX) {
        return 0;
    }
    if ((startup->argc != 0u && startup->argv_address == 0u) ||
        (startup->environment_count != 0u &&
         startup->environment_address == 0u) ||
        (startup->capability_count != 0u &&
         startup->capabilities_address == 0u)) {
        return 0;
    }

    reserved_or = startup->reserved[0] | startup->reserved[1] |
                  startup->reserved[2];
    return reserved_or == 0u;
}
