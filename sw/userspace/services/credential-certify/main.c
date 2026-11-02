#include <astra/config_library.h>
#include <astra/credential_service.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <string.h>

ASTRA_PROGRAM("credential-certify", 0, 1, 0,
              "Astra68 contributors", "Astra native");

int astra_main(const AstraStartupInfo *startup)
{
    uint8_t original[ASTRA_CREDENTIAL_VALUE_MAX];
    uint8_t copied[ASTRA_CREDENTIAL_VALUE_MAX];
    uint32_t length = 0u;
    const char *stage = "credential grants";
    uint32_t status;
    const AstraStartupCapability *bootstrap;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL ||
        astra_startup_capability(startup,
            ASTRA_CAPABILITY_HOST_CONFIG_SECRETS) != NULL ||
        astra_startup_capability(startup,
            ASTRA_CONFIG_SYSTEM_CAPABILITY) != NULL)
        return ASTRA_STATUS_ACCESS;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
        NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_PEER_DEAD;
    for (uint32_t at = 0u; at < sizeof(original); ++at)
        original[at] = (uint8_t)(at ^ 0x5au);
    stage = "credential store";
    status = astra_credential_set(startup, "certify.generic",
        original, sizeof(original));
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "credential size query";
    status = astra_credential_get(startup, "certify.generic",
        NULL, 0u, &length);
    if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL ||
        length != sizeof(original)) goto failed;
    stage = "credential short read";
    status = astra_credential_get(startup, "certify.generic",
        copied, sizeof(copied) - 1u, &length);
    if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL ||
        length != sizeof(original)) goto failed;
    stage = "credential read";
    status = astra_credential_get(startup, "certify.generic",
        copied, sizeof(copied), &length);
    if (status != ASTRA_CONFIG_OK || length != sizeof(original) ||
        memcmp(original, copied, sizeof(original)) != 0) goto failed;
    stage = "credential remove";
    status = astra_credential_remove(startup, "certify.generic");
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "credential missing";
    status = astra_credential_get(startup, "certify.generic",
        copied, sizeof(copied), &length);
    if (status != ASTRA_CONFIG_NOT_FOUND) goto failed;
    memset(original, 0, sizeof(original));
    memset(copied, 0, sizeof(copied));
    (void)astra_log("credential broker roundtrip PASS");
    for (;;) {
        status = astra_rt_thread_sleep(1000000000ull,
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
        if (status != ASTRA_SYSCALL_TIMED_OUT &&
            status != ASTRA_SYSCALL_CANCELLED)
            return ASTRA_STATUS_IO;
    }
failed:
    (void)astra_log_failure(stage, status);
    memset(original, 0, sizeof(original));
    memset(copied, 0, sizeof(copied));
    return ASTRA_STATUS_IO;
}
