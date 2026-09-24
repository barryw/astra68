#include <astra/compiler.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("remote-desktop", 0, 1, 0, "Astra68 contributors",
              "Astra native");

#define HEALTH_INTERVAL_NS UINT64_C(100000000)

static uint32_t submit(AstraHostChannelClient *channel, uint16_t operation,
                       uint32_t *host_detail)
{
    AstraHostCommand *command = astra_host_client_prepare(
        channel, ASTRA_HOST_SERVICE_REMOTE_DESKTOP, operation);
    uint32_t status = command == NULL ? ASTRA_SYSCALL_INVALID_ARGUMENT :
                                       astra_host_client_submit(channel);

    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    if (host_detail != NULL)
        *host_detail = command->result_value;
    return command->status;
}

static void sleep_until_health_check(void)
{
    (void)astra_rt_thread_sleep(HEALTH_INTERVAL_NS,
                                ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    AstraHostChannelClient channel = {0};
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    if (bootstrap == NULL || device == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_host_client_open(device->handle,
                                    ASTRA_HOST_CAP_REMOTE_DESKTOP, 0u,
                                    &channel);
    if (status == ASTRA_SYSCALL_OK) {
        do {
            status = submit(&channel, ASTRA_HOST_REMOTE_DESKTOP_ACQUIRE,
                            NULL);
            if (status == ASTRA_STATUS_BUSY)
                sleep_until_health_check();
        } while (status == ASTRA_STATUS_BUSY);
    } else {
        status = status == ASTRA_SYSCALL_UNSUPPORTED ?
                 ASTRA_STATUS_UNSUPPORTED : ASTRA_STATUS_IO;
    }
    if (status != ASTRA_STATUS_OK) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        return (int)status;
    }
    (void)astra_log("remote desktop ready");
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        sleep_until_health_check();
        status = submit(&channel, ASTRA_HOST_REMOTE_DESKTOP_STATUS, NULL);
        if (status == ASTRA_STATUS_OK)
            continue;
        (void)astra_log_failure("remote desktop peer", status);
        return (int)status;
    }
}
