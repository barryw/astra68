#include <astra/config_library.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <string.h>

ASTRA_PROGRAM("config-secret-certify", 0, 1, 0,
              "Astra68 contributors", "Astra native");

int astra_main(const AstraStartupInfo *startup)
{
    static const char value[] = "host held credential probe";
    char output[sizeof(value)] = {0};
    uint32_t length = 0u;
    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    const AstraStartupCapability *bootstrap = astra_startup_capability(
        startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL ||
        astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                            NULL, 0u) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_BAD_HANDLE;
    (void)astra_close(bootstrap->handle);
    AstraConfig config = ASTRA_CONFIG_INIT;
    const char *stage = "config-secret open-system";
    uint32_t status = astra_config_open_system(startup, 1u,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, &config, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "config-secret first-read";
    status = astra_config_get_secret(&config, "network.probe", output,
        sizeof(output), &length);
    if (status == ASTRA_CONFIG_NOT_FOUND) {
        stage = "config-secret write";
        status = astra_config_set_secret(&config, "network.probe",
            value, sizeof(value));
        if (status != ASTRA_CONFIG_OK) goto failed;
        (void)astra_log("config-secret first-write");
    } else if (status == ASTRA_CONFIG_OK) {
        (void)astra_log("config-secret restart-read");
    } else goto failed;
    stage = "config-secret reload";
    status = astra_config_reload(&config, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "config-secret second-read";
    status = astra_config_get_secret(&config, "network.probe", output,
        sizeof(output), &length);
    if (status != ASTRA_CONFIG_OK || length != sizeof(value) ||
        memcmp(output, value, sizeof(value)) != 0) goto failed;
    (void)astra_log("config-secret roundtrip PASS");
    astra_config_close(&config);
    const AstraStartupCapability *ordinary = astra_startup_capability(
        startup, ASTRA_CAPABILITY_HOST_DEVICE);
    AstraHostChannelClient unprivileged = {0};
    if (ordinary == NULL ||
        astra_host_client_open(ordinary->handle,
            ASTRA_HOST_CAP_CONFIG_SECRETS, 256u, &unprivileged) !=
            ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_ACCESS;
    AstraHostCommand *attempt = astra_host_client_prepare(&unprivileged,
        ASTRA_HOST_SERVICE_CONFIG_SECRETS, ASTRA_HOST_CONFIG_SECRET_OPEN);
    if (attempt == NULL ||
        astra_host_client_submit(&unprivileged) != ASTRA_SYSCALL_OK ||
        attempt->status != ASTRA_STATUS_ACCESS) {
        (void)astra_host_client_close(&unprivileged);
        return ASTRA_STATUS_ACCESS;
    }
    if (astra_host_client_close(&unprivileged) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    (void)astra_log("config-secret unprivileged denied PASS");
    for (;;) {
        status = astra_rt_thread_sleep(1000000000ull,
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
        if (status != ASTRA_SYSCALL_TIMED_OUT &&
            status != ASTRA_SYSCALL_CANCELLED)
            return ASTRA_STATUS_IO;
    }
failed:
    (void)astra_log_failure(stage, status);
    astra_config_close(&config);
    return ASTRA_STATUS_IO;
}
