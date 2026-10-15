/* Exercise the real private loader policy/retry code. Unused launch/IPC
 * sections are discarded by the host linker; no alternate recovery helper
 * or production-only test entry point is needed. */
#include "../src/loader.c"

#include <assert.h>
#include <stdlib.h>

static AstraVfsClient managed_client;
static int managed, claim_live, allocation_fail;
static uint32_t open_result, read_result, close_result;
static uint32_t provider_opens, provider_reads, provider_closes, managed_reads;
static uint32_t logged_status;
static uint64_t now;
static const char *policy_text;
static const AstraStartupInfo *expected_startup;

void *astra_runtime_allocate(size_t size)
{
    return allocation_fail ? NULL : malloc(size);
}

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer) { free(pointer); }
uint64_t astra_clock_monotonic(void) { return now; }
uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    assert(strcmp(operation, "service failure state") == 0);
    logged_status = status;
    return ASTRA_STATUS_OK;
}

AstraVfsClient *supervisor_vfs_client(void)
{
    return managed ? &managed_client : NULL;
}

static uint32_t read_policy(const char *path, void **buffer, uint32_t *length)
{
    assert(strcmp(path, "/config/services/hostfs/policy.conf") == 0);
    *buffer = NULL;
    *length = 0u;
    if (read_result != ASTRA_VFS_OK) return read_result;
    *length = (uint32_t)strlen(policy_text);
    *buffer = astra_runtime_allocate((size_t)*length + 1u);
    if (*buffer == NULL) return ASTRA_VFS_ERR_LIMIT;
    memcpy(*buffer, policy_text, (size_t)*length + 1u);
    return ASTRA_VFS_OK;
}

uint32_t supervisor_vfs_read_alloc(const char *path, void **buffer, uint32_t *length)
{
    assert(managed && !claim_live);
    ++managed_reads;
    return read_policy(path, buffer, length);
}

uint32_t supervisor_volume_open(const AstraStartupInfo *startup)
{
    assert(!managed && !claim_live && startup == expected_startup);
    ++provider_opens;
    claim_live = open_result == ASTRA_VFS_OK;
    return open_result;
}

uint32_t supervisor_volume_read_alloc(const char *path, void **buffer, uint32_t *length)
{
    assert(claim_live && !managed);
    ++provider_reads;
    return read_policy(path, buffer, length);
}

uint32_t supervisor_volume_unmount(void)
{
    assert(claim_live);
    ++provider_closes;
    claim_live = 0;
    return close_result;
}

static void reset(void)
{
    managed = claim_live = allocation_fail = 0;
    provider_opens = provider_reads = provider_closes = managed_reads = 0u;
    open_result = read_result = close_result = ASTRA_VFS_OK;
    policy_text = "astra-config 1\nschema 1\nstart manual\nrestart never\n";
}

int main(void)
{
    AstraStartupInfo startup = {0};
    AstraServiceDefinition definition = ASTRA_SERVICE_DEFINITION_INIT;
    expected_startup = &startup;
    assert(astra_service_definition_init(&definition, "hostfs", HOSTFS_IMAGE_PATH,
                                         ASTRA_SERVICE_RUNS_ASTRA) == ASTRA_OK);
    definition.start_policy = ASTRA_SERVICE_START_BOOT;
    definition.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;

    reset();
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_OK);
    assert(definition.start_policy == ASTRA_SERVICE_START_MANUAL &&
           definition.restart_policy == ASTRA_SERVICE_RESTART_NEVER);
    assert(provider_opens == 1u && provider_reads == 1u && provider_closes == 1u &&
           managed_reads == 0u && !claim_live);
    /* A later publisher recovery reacquires and releases the same facility. */
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_OK);
    assert(provider_opens == 2u && provider_closes == 2u && !claim_live);

    reset(); managed = 1;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_OK);
    assert(managed_reads == 1u && provider_opens == 0u);
    reset(); read_result = ASTRA_STATUS_NOT_FOUND;
    definition.start_policy = ASTRA_SERVICE_START_BOOT;
    definition.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_OK);
    assert(definition.start_policy == ASTRA_SERVICE_START_BOOT &&
           definition.restart_policy == ASTRA_SERVICE_RESTART_ON_FAULT && provider_closes == 1u);
    definition.start_policy = ASTRA_SERVICE_START_MANUAL;
    definition.restart_policy = ASTRA_SERVICE_RESTART_NEVER;

    for (uint32_t status = ASTRA_STATUS_BUSY; status <= ASTRA_STATUS_PEER_DEAD; ++status) {
        reset(); open_result = status;
        assert(policy_apply(&startup, &definition) == status);
        assert(provider_reads == 0u && provider_closes == 0u && !claim_live);
    }
    reset(); open_result = ASTRA_STATUS_NOT_FOUND;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_NOT_FOUND);
    assert(provider_reads == 0u); /* Missing provider/mount is not missing policy. */
    reset(); read_result = ASTRA_STATUS_NOT_FOUND; close_result = ASTRA_STATUS_IO;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_IO);
    assert(provider_closes == 1u && !claim_live);
    for (uint32_t status = ASTRA_STATUS_LIMIT; status <= ASTRA_STATUS_IO; ++status) {
        reset(); read_result = status;
        assert(policy_apply(&startup, &definition) == status);
        assert(provider_closes == 1u && !claim_live);
    }
    reset(); policy_text = "astra-config 1\nschema 1\nstart corrupt\n";
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_INVALID);
    assert(definition.start_policy == ASTRA_SERVICE_START_MANUAL && !claim_live);
    reset(); allocation_fail = 1;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_LIMIT);
    assert(provider_opens == 0u);
    reset(); definition.executable[10] = 'm';
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_PEER_DEAD);
    assert(provider_opens == 0u); /* An unrelated service cannot bypass SYSTEM. */
    definition.executable[10] = 'h';
    definition.flags |= ASTRA_SERVICE_PROTECTED;
    assert(policy_apply(&startup, &definition) == ASTRA_STATUS_OK && provider_opens == 0u);
    {
        /* A found static service must not fall through to dynamic lookup
         * merely because its provider is absent during policy resolution. */
        static SupervisorManifestEntry entry;
        strcpy(entry.path, HOSTFS_IMAGE_PATH);
        entry.resident = 1u;
        entry.start_policy = ASTRA_SERVICE_START_BOOT;
        entry.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
        startup_manifest.entries = &entry;
        startup_manifest.count = 1u;
        reset(); open_result = ASTRA_STATUS_NOT_FOUND;
        assert(configured_definition(&startup, "hostfs", &definition, NULL) ==
               ASTRA_STATUS_NOT_FOUND);
        assert(provider_opens == 1u && provider_reads == 0u && managed_reads == 0u);
        reset(); read_result = ASTRA_STATUS_NOT_FOUND;
        int dynamic = 99;
        assert(configured_definition(&startup, "hostfs", &definition, &dynamic) == ASTRA_STATUS_OK);
        assert(dynamic == 0 && definition.start_policy == ASTRA_SERVICE_START_BOOT &&
               provider_closes == 1u && !claim_live);
    }
    astra_service_definition_destroy(&definition);

    now = UINT64_C(1000000000);
    mark_service_failed("hostfs", 1u, ASTRA_STATUS_PEER_DEAD);
    assert(failed_service_count == 1u &&
           next_service_retry() == now + SERVICE_BACKOFF_FIRST_NS);
    mark_service_failed("hostfs", 2u, ASTRA_STATUS_IO);
    assert(failed_service_count == 1u && failed_services.records[0].failures == 2u &&
           next_service_retry() == now + 2u * SERVICE_BACKOFF_FIRST_NS);
    mark_service_failed("hostfs", 3u, ASTRA_STATUS_LIMIT);
    assert(next_service_retry() == now + 4u * SERVICE_BACKOFF_FIRST_NS);
    mark_service_failed("hostfs", 4u, ASTRA_STATUS_INVALID);
    assert(failed_service_count == 1u && next_service_retry() == ASTRA_DEADLINE_FOREVER);
    mark_service_failed("hostfs", 100u, ASTRA_STATUS_BUSY);
    assert(next_service_retry() == now + SERVICE_BACKOFF_MAX_NS && logged_status == 0u);
    clear_service_failure("hostfs");
    assert(failed_service_count == 0u && next_service_retry() == ASTRA_DEADLINE_FOREVER);
    free(failed_services.records);
    return 0;
}
