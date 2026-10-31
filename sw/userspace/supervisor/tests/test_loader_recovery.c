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
static char created[2][ASTRA_VFS_PATH_MAX];
static uint32_t creation_count;
static uint32_t old_store_exists, new_store_exists, old_store_kind, new_store_kind;
static char renamed_from[ASTRA_VFS_PATH_MAX], renamed_to[ASTRA_VFS_PATH_MAX];
static int config_mode;
static uint32_t old_config_exists, new_config_exists;
static uint32_t config_rename_result;

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

uint32_t astra_vfs_mkdir_mode(AstraVfsClient *client, const char *path,
                              uint16_t mode)
{
    assert(client == &managed_client && mode == 0700u);
    assert(creation_count < 2u);
    strcpy(created[creation_count++], path);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_stat_meta(AstraVfsClient *client, const char *path,
                             AstraVfsDirEntry *info)
{
    assert(client == &managed_client);
    if (config_mode && strncmp(path, "/config/applications/", 21u) == 0) {
        uint32_t exists = strcmp(path, "/config/applications/Terminal.app") == 0 ?
            old_config_exists : new_config_exists;
        if (!exists) return ASTRA_VFS_ERR_NOT_FOUND;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    if (strncmp(path, "/home/Application Data/", 23u) == 0) {
        if (!new_store_exists) return ASTRA_VFS_ERR_NOT_FOUND;
        info->kind = (uint16_t)new_store_kind;
    } else {
        assert(path[0] == '/');
        if (!old_store_exists) return ASTRA_VFS_ERR_NOT_FOUND;
        info->kind = (uint16_t)old_store_kind;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_rename(AstraVfsClient *client, const char *from,
                           const char *to)
{
    if (config_mode) {
        assert(client == &managed_client && old_config_exists &&
               !new_config_exists);
        if (config_rename_result != ASTRA_VFS_OK)
            return config_rename_result;
        strcpy(renamed_from, from);
        strcpy(renamed_to, to);
        old_config_exists = 0u;
        new_config_exists = 1u;
        return ASTRA_VFS_OK;
    }
    assert(client == &managed_client && old_store_exists && !new_store_exists);
    strcpy(renamed_from, from);
    strcpy(renamed_to, to);
    old_store_exists = 0u;
    new_store_exists = 1u;
    return ASTRA_VFS_OK;
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
    config_mode = 0;
    config_rename_result = ASTRA_VFS_OK;
}

int main(void)
{
    assert(config_launch_status(ASTRA_CONFIG_IO) == ASTRA_STATUS_IO);
    assert(config_launch_status(ASTRA_CONFIG_ACCESS) == ASTRA_STATUS_ACCESS);
    assert(config_launch_status(ASTRA_CONFIG_NO_MEMORY) == ASTRA_STATUS_LIMIT);
    assert(config_launch_status(ASTRA_CONFIG_BUFFER_TOO_SMALL) ==
           ASTRA_STATUS_NAME_TOO_LONG);
    AstraStartupInfo startup = {0};
    AstraServiceDefinition definition = ASTRA_SERVICE_DEFINITION_INIT;
    expected_startup = &startup;
    {
        SupervisorManifestEntry entry = {0};
        char root[ASTRA_VFS_PATH_MAX], path[ASTRA_VFS_PATH_MAX];
        uint32_t created_store = 0u;

        managed = 1;
        strcpy(entry.path, "/apps/ChocolateDoom.app");
        old_store_kind = new_store_kind = ASTRA_VFS_KIND_DIRECTORY;
        assert(prepare_private_store(&entry, "org.chocolate-doom.doom",
                                     root, path, &created_store) == ASTRA_STATUS_OK);
        assert(strcmp(root, "home/Application Data/org.chocolate-doom.doom") == 0);
        assert(strcmp(path, "/home/Application Data/org.chocolate-doom.doom") == 0);
        assert(created_store == 1u && creation_count == 2u);
        assert(strcmp(created[0], "/home/Application Data") == 0);
        assert(strcmp(created[1], path) == 0);

        creation_count = 0u;
        old_store_exists = 1u;
        strcpy(entry.path, "/usb0/ChocolateDoom.app");
        assert(prepare_private_store(&entry, "org.chocolate-doom.doom",
                                     root, path, &created_store) == ASTRA_STATUS_OK);
        assert(created_store == 0u && creation_count == 1u);
        assert(strcmp(renamed_from, "/ChocolateDoom.app") == 0);
        assert(strcmp(renamed_to, path) == 0);

        creation_count = 0u;
        strcpy(entry.path, "/usb0/Renamed.app");
        assert(prepare_private_store(&entry, "org.chocolate-doom.doom",
                                     root, path, &created_store) == ASTRA_STATUS_OK);
        assert(strcmp(root, "home/Application Data/org.chocolate-doom.doom") == 0);
        assert(created_store == 0u && creation_count == 1u);

        creation_count = 0u;
        old_store_exists = 1u;
        assert(prepare_private_store(&entry, "org.chocolate-doom.doom",
                                     root, path, &created_store) == ASTRA_STATUS_BUSY);
        assert(creation_count == 1u && old_store_exists && new_store_exists);

        creation_count = 0u;
        old_store_exists = 0u;
        new_store_kind = ASTRA_VFS_KIND_FILE;
        assert(prepare_private_store(&entry, "org.chocolate-doom.doom",
                                     root, path, &created_store) == ASTRA_STATUS_NOT_DIR);

        creation_count = 0u;
        new_store_exists = 0u;
        strcpy(entry.path, "/services/events");
        assert(prepare_private_store(&entry, "", root, path,
                                     &created_store) == ASTRA_STATUS_OK);
        assert(strcmp(root, "events") == 0 && strcmp(path, "/events") == 0);
        assert(created_store == 1u && creation_count == 1u);
        creation_count = 0u;
        managed = 0;
    }
    {
        SupervisorManifestEntry entry = {0};
        char root[ASTRA_VFS_PATH_MAX];

        managed = 1;
        strcpy(entry.path, "/services/fonts");
        entry.resident = 1u;
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_SYSTEM,
                                   NULL,
                                   root, sizeof(root)) == ASTRA_CONFIG_OK);
        assert(strcmp(root, "config/system/fonts") == 0);
        assert(creation_count == 2u);
        assert(strcmp(created[0], "/config/system") == 0);
        assert(strcmp(created[1], "/config/system/fonts") == 0);
        creation_count = 0u;
        strcpy(entry.path, "/apps/Terminal.app");
        entry.resident = 0u;
        config_mode = 1;
        old_config_exists = new_config_exists = 0u;
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_APPLICATION,
                                   "org.astra.terminal",
                                   root, sizeof(root)) == ASTRA_CONFIG_OK);
        assert(strcmp(root, "config/applications/org.astra.terminal") == 0);
        assert(strcmp(created[0], "/config/applications") == 0);
        assert(strcmp(created[1], "/config/applications/org.astra.terminal") == 0);
        creation_count = 0u;
        old_config_exists = 1u;
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_APPLICATION,
                                   "org.astra.terminal",
                                   root, sizeof(root)) == ASTRA_CONFIG_OK);
        assert(strcmp(renamed_from, "/config/applications/Terminal.app") == 0);
        assert(strcmp(renamed_to,
                      "/config/applications/org.astra.terminal") == 0);
        assert(!old_config_exists && new_config_exists);
        creation_count = 0u;
        old_config_exists = 1u;
        new_config_exists = 0u;
        config_rename_result = ASTRA_VFS_ERR_IO;
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_APPLICATION,
                                   "org.astra.terminal",
                                   root, sizeof(root)) == ASTRA_CONFIG_IO);
        assert(old_config_exists && !new_config_exists);
        config_rename_result = ASTRA_VFS_OK;
        creation_count = 0u;
        old_config_exists = 1u;
        new_config_exists = 1u;
        renamed_from[0] = '\0';
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_APPLICATION,
                                   "org.astra.terminal",
                                   root, sizeof(root)) == ASTRA_CONFIG_OK);
        assert(renamed_from[0] == '\0' && old_config_exists &&
               new_config_exists);
        creation_count = 0u;
        old_config_exists = new_config_exists = 0u;
        strcpy(entry.path, "/usb0/Games/Foo.app");
        assert(private_config_root(&entry, "config",
                                   ASTRA_CONFIG_OWNER_APPLICATION,
                                   "org.foo",
                                   root, sizeof(root)) == ASTRA_CONFIG_OK);
        assert(strcmp(root, "config/applications/org.foo") == 0);
        creation_count = 0u;
        managed = 0;
    }
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
    {
        AstraServiceDefinition scratch = {0};

        assert(astra_service_definition_path_set(&scratch, "/services/first") == ASTRA_OK);
        assert(boot_plan_append(NULL, &scratch) == ASTRA_STATUS_OK);
        assert(scratch.executable == NULL);
        assert(astra_service_definition_path_set(&scratch, "/services/second") == ASTRA_OK);
        assert(boot_plan_append(NULL, &scratch) == ASTRA_STATUS_OK);
        assert(strcmp(boot_plan[0].definition.executable, "/services/first") == 0);
        assert(strcmp(boot_plan[1].definition.executable, "/services/second") == 0);
        boot_plan_release();
        assert(boot_plan == NULL && boot_plan_count == 0u);
    }
    free(failed_services.records);
    return 0;
}
