#include <astra/config_library.h>
#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <string.h>

ASTRA_PROGRAM("config-secret-certify", 0, 1, 0,
              "Astra68 contributors", "Astra native");
static const AstraConfigField config_fields[] = {
    {"network.probe", ASTRA_CONFIG_FIELD_SENSITIVE},
    {"race.probe", 0u}, {"race.concurrent", 0u}
};
static const AstraConfigSchema config_schema = {1u, config_fields, 3u};
static const AstraConfigField public_fields[] = {{"probe.public", 0u}};
static const AstraConfigSchema public_schema = {1u, public_fields, 1u};

typedef struct ConcurrentWriter {
    AstraConfig *config;
    volatile uint32_t ready, start;
    uint32_t result;
} ConcurrentWriter;

static void append_worker(uint32_t argument)
{
    ConcurrentWriter *writer = (ConcurrentWriter *)(uintptr_t)argument;
    __atomic_store_n(&writer->ready, 1u, __ATOMIC_RELEASE);
    while (__atomic_load_n(&writer->start, __ATOMIC_ACQUIRE) == 0u)
        (void)astra_rt_thread_sleep(1000000ull,
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    writer->result = ASTRA_CONFIG_OK;
    for (uint32_t at = 0u; at < 20u && writer->result == ASTRA_CONFIG_OK; ++at)
        writer->result = astra_config_append_u64(
            writer->config, "race.concurrent", at + 20u);
}

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
    uint32_t status = astra_config_open_system(startup, &config_schema,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, &config, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "config-secret first-read";
    status = astra_config_get_string(&config, "network.probe", 0u, output,
        sizeof(output), &length);
    if (status == ASTRA_CONFIG_NOT_FOUND) {
        stage = "config-secret write";
        status = astra_config_set_string(&config, "network.probe", 0u, value);
        if (status != ASTRA_CONFIG_OK) goto failed;
        (void)astra_log("config-secret first-write");
    } else if (status == ASTRA_CONFIG_OK) {
        (void)astra_log("config-secret restart-read");
    } else goto failed;
    stage = "config-secret reload";
    status = astra_config_reload(&config, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "config-secret second-read";
    status = astra_config_get_string(&config, "network.probe", 0u, output,
        sizeof(output), &length);
    if (status != ASTRA_CONFIG_OK || length != sizeof(value) - 1u ||
        strcmp(output, value) != 0) goto failed;
    (void)astra_log("config-secret roundtrip PASS");
    AstraConfig other = ASTRA_CONFIG_INIT;
    stage = "config-stale open";
    status = astra_config_open_system(startup, &config_schema,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, &other, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    stage = "config-stale first-write";
    status = astra_config_set_string(&config, "race.probe", 0u, "first");
    if (status != ASTRA_CONFIG_OK) goto failed_other;
    stage = "config-stale conflict";
    status = astra_config_set_string_if_unchanged(
        &other, "race.probe", 0u, "second");
    if (status != ASTRA_CONFIG_CONFLICT) goto failed_other;
    stage = "config-stale retry";
    status = astra_config_set_string_if_unchanged(
        &other, "race.probe", 0u, "second");
    if (status != ASTRA_CONFIG_OK) goto failed_other;
    stage = "config-stale verify";
    status = astra_config_reload(&config, NULL);
    if (status != ASTRA_CONFIG_OK ||
        astra_config_get_string(&config, "race.probe", 0u, output,
            sizeof(output), &length) != ASTRA_CONFIG_OK ||
        strcmp(output, "second") != 0) goto failed_other;
    astra_config_close(&other);
    (void)astra_log("config-stale conflict/retry PASS");
    const AstraConfigChange batch[] = {
        {"race.probe", "batch", 0u, ASTRA_CONFIG_CHANGE_SET},
        {"network.probe", value, 0u, ASTRA_CONFIG_CHANGE_SET}
    };
    stage = "config-batch commit";
    status = astra_config_apply_if_unchanged(&config, batch, 2u);
    if (status != ASTRA_CONFIG_OK) goto failed;
    const AstraConfigChange rejected[] = {
        {"race.probe", "partial", 0u, ASTRA_CONFIG_CHANGE_SET},
        {"race.probe", NULL, 99u, ASTRA_CONFIG_CHANGE_REMOVE}
    };
    stage = "config-batch rollback";
    if (astra_config_apply(&config, rejected, 2u) == ASTRA_CONFIG_OK)
        goto failed;
    stage = "config-batch verify";
    if (astra_config_reload(&config, NULL) != ASTRA_CONFIG_OK ||
        astra_config_get_string(&config, "race.probe", 0u, output,
            sizeof(output), &length) != ASTRA_CONFIG_OK ||
        strcmp(output, "batch") != 0 ||
        astra_config_get_string(&config, "network.probe", 0u, output,
            sizeof(output), &length) != ASTRA_CONFIG_OK ||
        strcmp(output, value) != 0) goto failed;
    (void)astra_log("config-batch atomic PASS");
    stage = "config-concurrent open";
    status = astra_config_open_system(startup, &config_schema,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, &other, NULL);
    if (status != ASTRA_CONFIG_OK) goto failed;
    uint32_t before = 0u, after = 0u, thread = 0u;
    status = astra_config_count(&config, "race.concurrent", &before);
    if (status != ASTRA_CONFIG_OK) goto failed_other;
    ConcurrentWriter writer = {&other, 0u, 0u, ASTRA_CONFIG_OK};
    AstraThreadStart start = {append_worker, (uint32_t)(uintptr_t)&writer};
    stage = "config-concurrent thread";
    status = astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_READ, &thread, NULL);
    if (status != ASTRA_SYSCALL_OK) goto failed_other;
    while (__atomic_load_n(&writer.ready, __ATOMIC_ACQUIRE) == 0u)
        (void)astra_rt_thread_sleep(1000000ull,
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    __atomic_store_n(&writer.start, 1u, __ATOMIC_RELEASE);
    uint32_t main_status = ASTRA_CONFIG_OK;
    for (uint32_t at = 0u; at < 20u && main_status == ASTRA_CONFIG_OK; ++at)
        main_status = astra_config_append_u64(
            &config, "race.concurrent", at);
    status = astra_wait_one_restart(thread, ASTRA_DEADLINE_FOREVER, NULL);
    (void)astra_close(thread);
    if (status != ASTRA_SYSCALL_OK || main_status != ASTRA_CONFIG_OK ||
        writer.result != ASTRA_CONFIG_OK) goto failed_other;
    stage = "config-concurrent count";
    status = astra_config_reload(&config, NULL);
    if (status != ASTRA_CONFIG_OK ||
        astra_config_count(&config, "race.concurrent", &after) != ASTRA_CONFIG_OK ||
        after != before + 40u) goto failed_other;
    astra_config_close(&other);
    (void)astra_log("config-concurrent writers PASS");
    astra_config_close(&config);
    AstraConfig published = ASTRA_CONFIG_INIT;
    status = astra_config_open_publish(startup, &public_schema,
        &published, NULL);
    if (status != ASTRA_CONFIG_OK) return ASTRA_STATUS_IO;
    status = astra_config_set_string(&published, "probe.public", 0u, "visible");
    astra_config_close(&published);
    if (status != ASTRA_CONFIG_OK) return ASTRA_STATUS_IO;
    status = astra_config_open_public(startup, "config-secret-certify",
        &public_schema, &published, NULL);
    if (status != ASTRA_CONFIG_OK) return ASTRA_STATUS_IO;
    status = astra_config_get_string(&published, "probe.public", 0u,
        output, sizeof(output), &length);
    astra_config_close(&published);
    if (status != ASTRA_CONFIG_OK || strcmp(output, "visible") != 0)
        return ASTRA_STATUS_IO;
    (void)astra_log("config-public roundtrip PASS");
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
failed_other:
    astra_config_close(&other);
failed:
    (void)astra_log_failure(stage, status);
    astra_config_close(&config);
    return ASTRA_STATUS_IO;
}
