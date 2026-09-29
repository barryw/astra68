/*
 * A service that dies. It reports itself ready, then exits with a fault
 * status, so a gate can drive the supervisor's real death path -- restart
 * and backoff for a user service, the halt for a critical one -- on the same
 * ROM that ships, without anyone holding authority to crash a real service.
 * It is installed only by the test image profiles that name it.
 */
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

ASTRA_PROGRAM("fault-probe", 0, 1, 0, "Astra68 contributors",
              "Astra native");

/* Distinctive, so a gate can tell this death from any other. */
#define FAULT_PROBE_STATUS 77

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL, 0u) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    (void)astra_close(bootstrap->handle);
    (void)astra_log("fault probe dying");
    return FAULT_PROBE_STATUS;
}
