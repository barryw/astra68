/*
 * A service that dies. It reports itself ready, then exits with a fault
 * status, so a gate can drive the supervisor's real death path -- restart
 * and backoff for a user service, the halt for a critical one -- on the same
 * ROM that ships, without anyone holding authority to crash a real service.
 * With --fail-ready it refuses READY instead, so a gate can drive a boot
 * service that fails to start.
 * --lose-shutdown-port publishes managed shutdown, then closes its receiver,
 * so a gate can check failure reporting and recovery without crashing a peer.
 * --stay-ready remains alive until stopped, without managing shutdown.
 * --cancel-shutdown declines each managed shutdown request and remains usable.
 * It is installed only by the test image profiles that name it.
 */
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/shutdown.h>
#include <astra/status.h>

#include <string.h>

ASTRA_PROGRAM("fault-probe", 0, 1, 0, "Astra68 contributors",
              "Astra native");

/* Distinctive, so a gate can tell this death from any other. */
#define FAULT_PROBE_STATUS 77

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    const char *mode;
    uint32_t receive = 0u;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    mode = astra_startup_argument(startup, 1u);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_startup_argument(startup, 1u) != NULL &&
        strcmp(astra_startup_argument(startup, 1u), "--fail-ready") == 0) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_IO, NULL,
                                  0u);
        (void)astra_close(bootstrap->handle);
        return ASTRA_STATUS_IO;
    }
    if (mode != NULL && (strcmp(mode, "--lose-shutdown-port") == 0 ||
                         strcmp(mode, "--cancel-shutdown") == 0)) {
        uint32_t send = 0u;
        if (astra_rt_port_create(1u, sizeof(AstraShutdownRequest),
                &receive, &send) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_LIMIT;
        uint32_t status = astra_service_ready_managed(bootstrap->handle,
            ASTRA_STATUS_OK, NULL, 0u, send);
        (void)astra_close(send);
        if (strcmp(mode, "--lose-shutdown-port") == 0)
            (void)astra_close(receive);
        (void)astra_close(bootstrap->handle);
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
    } else {
        if (astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL, 0u) !=
            ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
        (void)astra_close(bootstrap->handle);
    }
    if (mode != NULL && strcmp(mode, "--cancel-shutdown") == 0) {
        for (;;) {
            AstraShutdownRequest request;
            uint32_t reply = 0u;
            uint32_t status = astra_shutdown_receive(receive, &request, &reply);
            if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
                if (astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL) != ASTRA_SYSCALL_OK)
                    return ASTRA_STATUS_PEER_DEAD;
                continue;
            }
            if (status != ASTRA_SYSCALL_OK ||
                astra_shutdown_respond(reply, request.header.transaction_id,
                    ASTRA_SHUTDOWN_CANCEL, 0u) != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PROTOCOL;
        }
    }
    if (mode != NULL && (strcmp(mode, "--lose-shutdown-port") == 0 ||
                         strcmp(mode, "--stay-ready") == 0))
        for (;;)
            (void)astra_rt_thread_sleep(UINT64_C(1000000000),
                ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    (void)astra_log("fault probe dying");
    return FAULT_PROBE_STATUS;
}
