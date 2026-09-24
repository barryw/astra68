#include <loader.h>
#include <launch_report.h>
#include <proc_tree.h>
#include <service_definition_store.h>

#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/area.h>
#include <astra/config_document.h>
#include <astra/config_library.h>
#include <astra/application_service.h>
#include <astra/boot.h>
#include <astra/bundle.h>
#include <astra/display.h>
#include <astra/event_control.h>
#include <astra/posix_process.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/service_manager.h>
#include <astra/status.h>
#include <astra/vfs_path.h>
#include <astra/vfs_host_direct.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_reader.h>
#include <astra/vfs_service_core.h>

#include <string.h>

#define MANIFEST_PATH "/vol/startup/system"
#define STORAGE_IMAGE_PATH "/vol/services/storage"
#define SERVICE_DEFINITION_DIRECTORY "/config/services"
#define SERVICE_RETRY_NS UINT64_C(5000000000)

/* Sized by the process table, not by the number of services in one image. */
static SupervisorManifest startup_manifest;
static SupervisorProcessTable process_table;
#define process_count (process_table.count)
enum SupervisorProcessAction {
    SUPERVISOR_PROCESS_ACTION_NONE = 0u,
    SUPERVISOR_PROCESS_ACTION_STOP,
    SUPERVISOR_PROCESS_ACTION_RESTART,
    SUPERVISOR_PROCESS_ACTION_PAUSE
};
static uint32_t service_handles[ASTRA_HANDLE_COUNT_MAX];
static char service_names[ASTRA_HANDLE_COUNT_MAX]
                         [ASTRA_CAPABILITY_NAME_MAX];
static AstraVfsClient service_clients[ASTRA_HANDLE_COUNT_MAX];
static uint32_t service_is_vfs[ASTRA_HANDLE_COUNT_MAX];
static char service_owner_names[ASTRA_HANDLE_COUNT_MAX][ASTRA_VFS_NAME_MAX];
static uint32_t supervisor_process_handle;
static uint32_t service_count;
static uint32_t event_target_receive;
static uint32_t event_target_send;
static uint32_t event_control_handle;
static uint32_t launch_receive;
/*
 * PROC: is served by this process because this process holds the handles. A
 * port of its own, the same shape the events service uses for EVENTS:, so a
 * child reads process state with the protocol it already reads files with.
 */
static AstraVfsService proc_service;
static AstraVfsSessionSlot proc_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService proc_port;
static uint32_t proc_receive;
static uint32_t proc_send;
static uint32_t launch_send;
static uint32_t manager_receive;
static uint32_t manager_send;
static AstraServiceDefinition definition_scratch[2];
static SupervisorProcessTable paused_services;
#define paused_service_count (paused_services.count)
static SupervisorProcessTable failed_services;
#define failed_service_count (failed_services.count)
static uint64_t service_retry_at;

static int append(char *out, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (at < capacity && out[at] != '\0') ++at;
    while (*text != '\0') {
        if (at + 1u >= capacity) return 0;
        out[at++] = *text++;
    }
    out[at] = '\0';
    return 1;
}

static int ends_with(const char *text, const char *suffix)
{
    uint32_t length = 0u;
    uint32_t ending = 0u;

    while (text[length] != '\0') ++length;
    while (suffix[ending] != '\0') ++ending;
    if (length < ending) return 0;
    return strcmp(text + length - ending, suffix) == 0;
}

static int declared_capability(const AstraBundleManifest *bundle,
                               const SupervisorManifestGrant *wanted)
{
    char name[ASTRA_BUNDLE_CAPABILITY_NAME_MAX];

    name[0] = '\0';
    if (!append(name, sizeof(name), wanted->name)) return 0;
    if (wanted->is_namespace != 0u) {
        if (!append(name, sizeof(name),
                    wanted->rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE) ?
                        ":rw" : ":r")) return 0;
    }
    for (uint32_t at = 0u; at < bundle->capability_count; ++at)
        if (strcmp(name, bundle->capabilities[at]) == 0) return 1;
    return 0;
}

static uint32_t resolve_entry_image(const SupervisorManifestEntry *entry,
                                    char *path, uint32_t path_capacity,
                                    char *bundle_root,
                                    uint32_t root_capacity,
                                    AstraBundleManifest *out_bundle)
{
    AstraBundleManifest bundle = ASTRA_BUNDLE_MANIFEST_INIT;
    char *bundle_text = NULL;
    const AstraAssign *apps;
    const char *tail;
    uint32_t length = 0u;
    uint32_t line = 0u;
    uint32_t status;

    path[0] = '\0';
    bundle_root[0] = '\0';
    if (!ends_with(entry->path, ".app"))
        return append(path, path_capacity, entry->path) ? ASTRA_STATUS_OK :
                                                         ASTRA_STATUS_LIMIT;
    if (!append(path, path_capacity, entry->path) ||
        !append(path, path_capacity, "/manifest")) return ASTRA_STATUS_LIMIT;
    status = supervisor_vfs_read_alloc(path, (void **)&bundle_text, &length);
    if (status != ASTRA_VFS_OK) return ASTRA_STATUS_NOT_FOUND;
    status = astra_bundle_manifest_parse(
        bundle_text, length, &bundle, &line);
    astra_runtime_deallocate(bundle_text);
    if (status != ASTRA_BUNDLE_OK)
        return SUPERVISOR_LOADER_FAIL_MANIFEST;
    if (bundle.kind != ASTRA_BUNDLE_APPLICATION) {
        astra_bundle_manifest_destroy(&bundle);
        return SUPERVISOR_LOADER_FAIL_MANIFEST;
    }
    for (uint32_t at = 0u; at < entry->grant_count; ++at)
        if (!declared_capability(&bundle, &entry->grants[at])) {
            astra_bundle_manifest_destroy(&bundle);
            return ASTRA_STATUS_ACCESS;
        }
    path[0] = '\0';
    if (!append(path, path_capacity, entry->path) ||
        !append(path, path_capacity, "/") ||
        !append(path, path_capacity, bundle.executable))
    {
        astra_bundle_manifest_destroy(&bundle);
        return ASTRA_STATUS_LIMIT;
    }
    if (strncmp(entry->path, "/apps/", 6u) != 0)
    {
        astra_bundle_manifest_destroy(&bundle);
        return ASTRA_STATUS_INVALID;
    }
    tail = entry->path + 6u;
    apps = astra_assign_lookup(supervisor_assigns(), "APPS");
    if (apps == NULL || !append(bundle_root, root_capacity, apps->root) ||
        (bundle_root[0] != '\0' && !append(bundle_root, root_capacity, "/")) ||
        !append(bundle_root, root_capacity, tail))
    {
        astra_bundle_manifest_destroy(&bundle);
        return ASTRA_STATUS_LIMIT;
    }
    if (out_bundle != NULL) {
        *out_bundle = bundle;
    } else {
        astra_bundle_manifest_destroy(&bundle);
    }
    return ASTRA_STATUS_OK;
}

static const char *private_store_root(const SupervisorManifestEntry *entry)
{
    const char *root = entry->path;

    if (*root == '/')
        ++root;
    while (*root != '\0' && *root != '/')
        ++root;
    return *root == '/' ? root + 1 : "";
}

static uint32_t private_config_root(const SupervisorManifestEntry *entry,
                                    const char *parent, char *out,
                                    uint32_t capacity)
{
    return astra_config_capability_root(
        parent,
        entry->resident != 0u ? ASTRA_CONFIG_OWNER_SERVICE :
                               ASTRA_CONFIG_OWNER_APPLICATION,
        private_store_root(entry), out, capacity);
}

static uint32_t named_service(const char *name)
{
    if (strcmp(name, "SYSTEM") == 0)
        return supervisor_vfs_port();
    for (uint32_t index = 0u; index < service_count; ++index)
        if (astra_capability_name_equal(service_names[index], name))
            return service_handles[index];
    return 0u;
}

static int definition_path(const char *name, const char *leaf, char *out,
                           uint32_t capacity)
{
    out[0] = '\0';
    return append(out, capacity, "/config/services/") &&
           append(out, capacity, name) && append(out, capacity, "/") &&
           append(out, capacity, leaf);
}

static uint32_t dynamic_definition_read(const char *name,
                                        AstraServiceDefinition *definition)
{
    char *definition_text = NULL;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t length = 0u;
    uint32_t status;

    if (!astra_service_name_valid(name) ||
        !definition_path(name, "service.conf", path, sizeof(path)))
        return ASTRA_STATUS_INVALID;
    status = supervisor_vfs_read_alloc(path, (void **)&definition_text,
                                       &length);
    if (status != ASTRA_VFS_OK)
        return status;
    status = supervisor_service_definition_parse(
        definition_text, length, definition, NULL);
    astra_runtime_deallocate(definition_text);
    return status;
}

static uint32_t dynamic_definition_write(
    const AstraServiceDefinition *definition)
{
    AstraVfsClient *client = supervisor_vfs_client();
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    char *definition_text = NULL;
    char directory[ASTRA_VFS_PATH_MAX];
    char path[ASTRA_VFS_PATH_MAX];
    char temporary[ASTRA_VFS_PATH_MAX];
    uint64_t ignored_size = 0u;
    uint16_t ignored_kind = 0u;
    uint32_t required = 0u;
    uint32_t used = 0u;
    uint32_t status;

    if (client == NULL ||
        astra_service_definition_validate(definition) != ASTRA_OK ||
        !definition_path(definition->name, "service.conf", path,
                         sizeof(path)) ||
        !definition_path(definition->name, ".service.tmp", temporary,
                         sizeof(temporary)))
        return ASTRA_STATUS_INVALID;
    directory[0] = '\0';
    if (!append(directory, sizeof(directory), SERVICE_DEFINITION_DIRECTORY) ||
        !append(directory, sizeof(directory), "/") ||
        !append(directory, sizeof(directory), definition->name))
        return ASTRA_STATUS_LIMIT;
    status = supervisor_service_definition_serialize(
        definition, NULL, 0u, &required);
    if (status != ASTRA_STATUS_BUFFER_TOO_SMALL || required == 0u)
        return status;
    definition_text = astra_runtime_allocate(required);
    if (definition_text == NULL)
        return ASTRA_STATUS_LIMIT;
    status = supervisor_service_definition_serialize(
        definition, definition_text, required, &required);
    if (status != ASTRA_STATUS_OK)
        goto done;
    status = astra_vfs_mkdir_mode(client, SERVICE_DEFINITION_DIRECTORY, 0700u);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        goto done;
    status = astra_vfs_mkdir_mode(client, directory, 0700u);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        goto done;
    status = astra_vfs_unlink(client, temporary);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND)
        goto done;
    status = astra_vfs_open_mode(
        client, temporary,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_TRUNCATE,
        0600u, &file, &ignored_size, &ignored_kind);
    while (status == ASTRA_VFS_OK && used + 1u < required) {
        uint32_t moved = 0u;

        status = astra_vfs_write(
            client, file, used, definition_text + used,
            required - used - 1u, &moved);
        if (status == ASTRA_VFS_OK && moved == 0u)
            status = ASTRA_STATUS_IO;
        used += moved;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_sync(client, file);
    if (file != ASTRA_VFS_FILE_INVALID) {
        uint32_t close_status = astra_vfs_close(client, file);

        file = ASTRA_VFS_FILE_INVALID;
        if (status == ASTRA_VFS_OK)
            status = close_status;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_rename(client, temporary, path);
    if (status != ASTRA_VFS_OK)
        (void)astra_vfs_unlink(client, temporary);
done:
    astra_runtime_deallocate(definition_text);
    return status;
}

static uint32_t startup_definition(const char *name,
                                   AstraServiceDefinition *definition)
{
    for (uint32_t index = 0u; index < startup_manifest.count; ++index) {
        const SupervisorManifestEntry *entry = &startup_manifest.entries[index];

        if (entry->resident == 0u ||
            supervisor_service_definition_from_manifest(entry, definition) !=
                ASTRA_STATUS_OK)
            continue;
        if (strcmp(definition->name, name) == 0) {
            return ASTRA_STATUS_OK;
        }
    }
    return ASTRA_STATUS_NOT_FOUND;
}

static uint32_t configured_definition(const char *name,
                                      AstraServiceDefinition *definition,
                                      int *dynamic)
{
    uint32_t status = startup_definition(name, definition);

    if (status == ASTRA_STATUS_OK) {
        if (dynamic != NULL)
            *dynamic = 0;
        return status;
    }
    status = dynamic_definition_read(name, definition);
    if (status == ASTRA_STATUS_OK && dynamic != NULL)
        *dynamic = 1;
    return status;
}

static uint32_t service_process_slot(const char *name)
{
    for (uint32_t index = 0u; index < process_count; ++index)
        if (process_table.records[index].service_name[0] != '\0' &&
            strcmp(process_table.records[index].service_name, name) == 0)
            return index;
    return UINT32_MAX;
}

static uint32_t paused_service_slot(const char *name)
{
    for (uint32_t index = 0u; index < paused_service_count; ++index)
        if (strcmp(paused_services.records[index].service_name, name) == 0)
            return index;
    return UINT32_MAX;
}

static uint32_t failed_service_slot(const char *name)
{
    for (uint32_t index = 0u; index < failed_service_count; ++index)
        if (strcmp(failed_services.records[index].service_name, name) == 0)
            return index;
    return UINT32_MAX;
}

static void clear_service_failure(const char *name)
{
    uint32_t slot = failed_service_slot(name);

    if (slot != UINT32_MAX) {
        --failed_service_count;
        failed_services.records[slot] =
            failed_services.records[failed_service_count];
        if (failed_service_count == 0u)
            service_retry_at = 0u;
    }
}

static void mark_service_failed(const char *name)
{
    if (failed_service_slot(name) != UINT32_MAX)
        return;
    if (!supervisor_process_table_reserve(&failed_services,
                                          failed_service_count + 1u,
                                          astra_runtime_reallocate)) {
        (void)astra_log_failure("service failure state", ASTRA_STATUS_NO_SPACE);
        return;
    }
    (void)strcpy(failed_services.records[failed_service_count++].service_name,
                 name);
    if (failed_service_count == 1u)
        service_retry_at = astra_clock_monotonic() + SERVICE_RETRY_NS;
}

static void service_info(const AstraServiceDefinition *definition,
                         AstraServiceInfo *info)
{
    uint32_t slot = service_process_slot(definition->name);

    (void)memset(info, 0, sizeof(*info));
    (void)strcpy(info->name, definition->name);
    (void)strcpy(info->executable, definition->executable);
    info->flags = definition->flags;
    info->start_policy = definition->start_policy;
    info->restart_policy = definition->restart_policy;
    if (slot != UINT32_MAX) {
        info->state = process_table.records[slot].action !=
                          SUPERVISOR_PROCESS_ACTION_NONE ?
            ASTRA_SERVICE_STATE_STOPPING :
            process_table.records[slot].paused != 0u ?
                ASTRA_SERVICE_STATE_PAUSED : ASTRA_SERVICE_STATE_READY;
        info->process_id = process_table.records[slot].id;
    } else if (paused_service_slot(definition->name) !=
               UINT32_MAX) {
        info->state = ASTRA_SERVICE_STATE_PAUSED;
    } else if (failed_service_slot(definition->name) != UINT32_MAX) {
        info->state = ASTRA_SERVICE_STATE_FAILED;
    } else {
        info->state = ASTRA_SERVICE_STATE_STOPPED;
    }
    for (uint32_t index = 0u; index < definition->publication_count; ++index) {
        const char *name = definition->publications[index].name;

        for (uint32_t service = 0u; service < service_count; ++service)
            if (strcmp(service_names[service], name) == 0)
                info->client_count += service_clients[service].session != 0u;
    }
}

static int entry_serves(const SupervisorManifestEntry *entry,
                        const char *name)
{
    for (uint32_t index = 0u; index < entry->serves_count; ++index)
        if (astra_capability_name_equal(entry->serves[index].name, name))
            return 1;
    return 0;
}

static int entry_grants(const SupervisorManifestEntry *entry,
                        const char *name)
{
    for (uint32_t index = 0u; index < entry->grant_count; ++index)
        if (astra_capability_name_equal(entry->grants[index].name, name))
            return 1;
    return 0;
}

static uint32_t add_grant(AstraLaunchGrant *out, uint32_t *count,
                          const char *name, uint32_t handle, uint32_t rights,
                          uint32_t flags, const char *root)
{
    if (*count == ASTRA_LAUNCH_GRANT_MAX)
        return ASTRA_STATUS_LIMIT;
    (void)memset(&out[*count], 0, sizeof(out[*count]));
    astra_capability_name_set(out[*count].name, name);
    out[*count].handle = handle;
    out[*count].rights = rights;
    out[*count].flags = flags;
    if (root != NULL)
        astra_capability_root_set(out[*count].root, root);
    ++*count;
    return ASTRA_STATUS_OK;
}

static uint32_t build_grants(const AstraStartupInfo *startup,
                             const SupervisorManifestEntry *entry,
                             const char *bundle_root, uint32_t ready_send,
                             AstraLaunchGrant *out,
                             uint32_t *count)
{
    uint32_t delegated = entry->delegates != 0u ? ASTRA_RIGHT_TRANSFER : 0u;
    uint32_t status;

    *count = 0u;
    status = add_grant(out, count, ASTRA_CAPABILITY_SERVICE_READY, ready_send,
                       ASTRA_RIGHT_SIGNAL, 0u, NULL);
    if (status != ASTRA_STATUS_OK)
        return status;
    for (uint32_t index = 0u; index < entry->grant_count; ++index) {
        const SupervisorManifestGrant *wanted = &entry->grants[index];

        if (!wanted->is_namespace) {
            if (strcmp(wanted->name,
                       ASTRA_CAPABILITY_APPLICATION_LAUNCH) == 0) {
                if (launch_send == 0u)
                    continue;
                status = add_grant(out, count, wanted->name, launch_send,
                                   ASTRA_RIGHT_SIGNAL | delegated,
                                   0u, NULL);
                if (status != ASTRA_STATUS_OK)
                    return status;
                continue;
            }
            if (strcmp(wanted->name, ASTRA_CAPABILITY_EVENT_CONTROL) == 0) {
                if (event_control_handle == 0u)
                    continue;
                status = add_grant(out, count, wanted->name,
                                   event_control_handle,
                                   ASTRA_RIGHT_SIGNAL | delegated,
                                   0u, NULL);
                if (status != ASTRA_STATUS_OK)
                    return status;
                continue;
            }
            const AstraStartupCapability *held = astra_startup_capability(
                startup, wanted->name);

            if (held == NULL) {
                uint32_t published = named_service(wanted->name);

                if (published != 0u) {
                    status = add_grant(out, count, wanted->name, published,
                                       ASTRA_RIGHT_SIGNAL | delegated,
                                       0u, NULL);
                    if (status != ASTRA_STATUS_OK)
                        return status;
                }
                continue;
            }
            status = add_grant(out, count, wanted->name, held->handle,
                               held->rights, 0u, NULL);
            if (status != ASTRA_STATUS_OK)
                return status;
            continue;
        }
        if (strcmp(wanted->name, "STORE") == 0) {
            AstraVfsDirEntry store_info = {0};
            char store_path[ASTRA_VFS_PATH_MAX] = "/";

            if (supervisor_vfs_port() == 0u)
                continue;
            /* A namespace entry is not the backing directory. Provision
             * private storage before granting it to the child. */
            if (private_store_root(entry)[0] == '\0' ||
                !append(store_path, sizeof(store_path),
                        private_store_root(entry)))
                return ASTRA_STATUS_INVALID;
            status = astra_vfs_mkdir_mode(supervisor_vfs_client(), store_path,
                                          0700u);
            if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
                return status;
            status = astra_vfs_stat_meta(supervisor_vfs_client(), store_path,
                                          &store_info);
            if (status != ASTRA_VFS_OK)
                return status;
            if (store_info.kind != ASTRA_VFS_KIND_DIRECTORY)
                return ASTRA_STATUS_NOT_DIR;
            status = add_grant(
                out, count, "STORE", supervisor_vfs_port(),
                ASTRA_RIGHT_SIGNAL | delegated,
                ASTRA_CAPABILITY_FLAG_NAMESPACE |
                    ((wanted->rights & ASTRA_RIGHT_READ) != 0u ?
                         ASTRA_CAPABILITY_FLAG_READ : 0u) |
                    ((wanted->rights & ASTRA_RIGHT_WRITE) != 0u ?
                         ASTRA_CAPABILITY_FLAG_WRITE : 0u),
                private_store_root(entry));
            if (status != ASTRA_STATUS_OK)
                return status;
            continue;
        }
        if (strcmp(wanted->name, ASTRA_CONFIG_CAPABILITY) == 0 ||
            strcmp(wanted->name, ASTRA_CONFIG_COMMANDS_CAPABILITY) == 0) {
            const AstraAssign *held = astra_assign_lookup(
                supervisor_assigns(), ASTRA_CONFIG_CAPABILITY);
            char root[ASTRA_CAPABILITY_ROOT_MAX];

            if (held == NULL)
                continue;
            if ((held->rights & wanted->rights) != wanted->rights)
                return ASTRA_STATUS_ACCESS;
            if ((strcmp(wanted->name, ASTRA_CONFIG_CAPABILITY) == 0 ?
                    private_config_root(entry, held->root, root,
                                        sizeof(root)) :
                    astra_config_scope_root(
                        held->root, ASTRA_CONFIG_OWNER_COMMAND, root,
                        sizeof(root))) != ASTRA_CONFIG_OK)
                return ASTRA_STATUS_LIMIT;
            status = add_grant(
                out, count, wanted->name, held->handle,
                ASTRA_RIGHT_SIGNAL | delegated,
                ASTRA_CAPABILITY_FLAG_NAMESPACE |
                    ((wanted->rights & ASTRA_RIGHT_READ) != 0u ?
                         ASTRA_CAPABILITY_FLAG_READ : 0u) |
                    ((wanted->rights & ASTRA_RIGHT_WRITE) != 0u ?
                         ASTRA_CAPABILITY_FLAG_WRITE : 0u),
                root);
            if (status != ASTRA_STATUS_OK)
                return status;
            continue;
        }
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *held = astra_assign_member(
                supervisor_assigns(), wanted->name, member);

            if (held == NULL)
                break;
            if ((held->rights & wanted->rights) != wanted->rights)
                return ASTRA_STATUS_ACCESS;
            status = add_grant(
                out, count, wanted->name, held->handle,
                ASTRA_RIGHT_SIGNAL | delegated,
                ASTRA_CAPABILITY_FLAG_NAMESPACE |
                    ((wanted->rights & ASTRA_RIGHT_READ) != 0u ?
                         ASTRA_CAPABILITY_FLAG_READ : 0u) |
                    ((wanted->rights & ASTRA_RIGHT_WRITE) != 0u ?
                         ASTRA_CAPABILITY_FLAG_WRITE : 0u),
                held->root);
            if (status != ASTRA_STATUS_OK)
                return status;
        }
    }
    if (!entry_grants(entry, "LIBS")) {
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *held = astra_assign_member(
                supervisor_assigns(), "LIBS", member);

            if (held == NULL)
                break;
            if ((held->rights & ASTRA_RIGHT_READ) == 0u)
                return ASTRA_STATUS_ACCESS;
            status = add_grant(
                out, count, "LIBS",
                held->handle, ASTRA_RIGHT_SIGNAL,
                ASTRA_CAPABILITY_FLAG_NAMESPACE |
                    ASTRA_CAPABILITY_FLAG_READ,
                held->root);
            if (status != ASTRA_STATUS_OK)
                return status;
        }
    }
    if (entry_serves(entry, "EVENTS")) {
        if (event_target_send == 0u)
            return ASTRA_STATUS_BAD_HANDLE;
        status = add_grant(out, count, ASTRA_CAPABILITY_EVENT_TARGET,
                           event_target_send, ASTRA_RIGHT_SIGNAL, 0u, NULL);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
    if (bundle_root != NULL && bundle_root[0] != '\0') {
        const AstraAssign *apps = astra_assign_lookup(supervisor_assigns(),
                                                       "APPS");

        if (apps == NULL) {
            return ASTRA_STATUS_NOT_FOUND;
        }
        status = add_grant(out, count, "APP", apps->handle,
                           ASTRA_RIGHT_SIGNAL,
                           ASTRA_CAPABILITY_FLAG_NAMESPACE |
                               ASTRA_CAPABILITY_FLAG_READ,
                           bundle_root);
        if (status != ASTRA_STATUS_OK) return status;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t receive_ready(uint32_t receive, uint32_t child,
                              uint32_t expected_handles,
                              uint32_t published[ASTRA_MESSAGE_HANDLES_MAX])
{
    AstraServiceReady message;
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0u};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status;

    for (uint32_t index = 0u; index < ASTRA_MESSAGE_HANDLES_MAX; ++index)
        published[index] = 0u;
    for (;;) {
        uint32_t exit_status = 0u;

        status = astra_port_receive(receive, &message, sizeof(message),
                                    handles, ASTRA_MESSAGE_HANDLES_MAX,
                                    &size, &handle_count);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK)
            return ASTRA_STATUS_PEER_DEAD;
        if (astra_process_wait(child, 0u, &exit_status) !=
            ASTRA_SYSCALL_TIMED_OUT)
            return supervisor_loader_child_status(exit_status);
        /*
         * Required service recovery is bounded by its media and stored state,
         * not by a guessed wall-clock number.  The ready port still wakes if
         * the child exits, so waiting forever does not hide a crashed service;
         * receive_ready() reports that child's actual status below.
         */
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            uint32_t child_status = 0u;

            /*
             * The child dying is the usual reason this wait ends, and its exit
             * status is the only thing that says why it died. Ask before
             * answering: PEER_DEAD on its own says a service is gone and
             * nothing about which check it failed, and a boot that stops with
             * that alone sends the next person to the port rather than to the
             * service. The loop's other exit already prefers the child's
             * status; this one threw it away.
             */
            if (astra_process_wait(child, 0u, &child_status) !=
                    ASTRA_SYSCALL_TIMED_OUT && child_status != 0u)
                return supervisor_loader_child_status(child_status);
            return ASTRA_STATUS_PEER_DEAD;
        }
    }
    if (size != sizeof(message) ||
        message.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        message.header.protocol != ASTRA_SERVICE_PROTOCOL ||
        message.header.protocol_version != ASTRA_SERVICE_VERSION ||
        message.header.operation != ASTRA_SERVICE_READY) {
        for (uint32_t index = 0u; index < handle_count; ++index)
            (void)astra_close(handles[index]);
        return ASTRA_STATUS_PROTOCOL;
    }
    if (message.status != ASTRA_STATUS_OK) {
        for (uint32_t index = 0u; index < handle_count; ++index)
            (void)astra_close(handles[index]);
        return message.status;
    }
    if (handle_count != expected_handles) {
        for (uint32_t index = 0u; index < handle_count; ++index)
            (void)astra_close(handles[index]);
        return ASTRA_STATUS_PROTOCOL;
    }
    for (uint32_t index = 0u; index < handle_count; ++index)
        published[index] = handles[index];
    return ASTRA_STATUS_OK;
}

static uint32_t publish(const SupervisorManifestPublication *publication,
                        uint32_t handle, const char *owner)
{
    AstraVfsClient *client;

    if (strcmp(publication->name, "SYSTEM") == 0)
        return supervisor_vfs_start(handle) ? ASTRA_STATUS_OK :
                                              SUPERVISOR_LOADER_FAIL_PUBLISH;
    if (service_count == ASTRA_HANDLE_COUNT_MAX)
        return ASTRA_STATUS_LIMIT;
    service_handles[service_count] = handle;
    astra_capability_name_set(service_names[service_count], publication->name);
    service_owner_names[service_count][0] = '\0';
    if (owner != NULL)
        (void)append(service_owner_names[service_count],
                     sizeof(service_owner_names[service_count]), owner);
    if (publication->rights == 0u) {
        service_is_vfs[service_count] = 0u;
        ++service_count;
        return ASTRA_STATUS_OK;
    }
    client = &service_clients[service_count];
    if (astra_vfs_host_port_connect(client,
                                    service_handles[service_count]) !=
            ASTRA_VFS_OK ||
        supervisor_vfs_register(client, handle) == 0u ||
        astra_assign_bind(supervisor_assigns(), publication->name, handle,
                          publication->rights, "") != ASTRA_VFS_OK)
        return SUPERVISOR_LOADER_FAIL_PUBLISH;
    service_is_vfs[service_count] = 1u;
    ++service_count;
    return ASTRA_STATUS_OK;
}


/*
 * What a launch cost, one line per program.
 *
 * Kept in the shipped build rather than behind the debug split, because the
 * question it answers -- "which stage of starting this program got slower" --
 * is the one asked after a change has already shipped, and a launch that has
 * to be reproduced under a different build to be measured is a launch nobody
 * measures. The four clock reads cost about 230 us against a launch budget in
 * the tens of milliseconds.
 *
 *   read   the image off the volume, through the VFS
 *   spawn  ASTRA_SYSCALL_PROCESS_CREATE: address space, image, first thread
 *   ready  the child's own start-up, until it reports itself ready
 */
static uint64_t launch_microseconds(uint64_t nanoseconds)
{
    return nanoseconds / UINT64_C(1000);
}

static void launch_log(const char *prefix, const char *path,
                       const SupervisorLaunchReportField *fields,
                       uint32_t field_count)
{
    uint32_t length = supervisor_launch_report_format(
        NULL, 0u, prefix, path, fields, field_count);
    char *line;

    if (length == 0u)
        return;
    line = astra_runtime_allocate((size_t)length + 1u);
    if (line == NULL)
        return;
    if (supervisor_launch_report_format(
            line, length + 1u, prefix, path, fields, field_count) == length)
        (void)astra_log_write(line, length);
    astra_runtime_deallocate(line);
}

static void launch_profile_report(const char *path,
                                  const AstraProcessLoadProfile *profile)
{
    const SupervisorLaunchReportField source[] = {
        {" time=", launch_microseconds(profile->source_read_ns)},
        {"us bytes=", profile->source_bytes},
        {" calls=", profile->source_reads},
    };
    const SupervisorLaunchReportField kernel[] = {
        {" begin=", launch_microseconds(profile->kernel_begin_ns)},
        {" interp=", launch_microseconds(profile->kernel_interpreter_ns)},
        {" write=", launch_microseconds(profile->kernel_write_ns)},
        {" create=", launch_microseconds(profile->kernel_create_ns)},
        {" commit=", launch_microseconds(profile->kernel_commit_ns)},
    };
    const SupervisorLaunchReportField total[] = {
        {" time=", launch_microseconds(profile->total_ns)},
        {"us release=", launch_microseconds(profile->source_release_ns)},
        {"us bytes=", profile->kernel_write_bytes},
        {" writes=", profile->kernel_writes},
    };

    launch_log("load-source ", path, source,
               (uint32_t)(sizeof(source) / sizeof(source[0])));
    launch_log("load-kernel ", path, kernel,
               (uint32_t)(sizeof(kernel) / sizeof(kernel[0])));
    launch_log("load-total ", path, total,
               (uint32_t)(sizeof(total) / sizeof(total[0])));
}

static void launch_report(const char *path, uint32_t bytes, uint32_t open_us,
                          uint32_t probe_us, uint32_t interpreter_us,
                          uint32_t load_us, uint32_t ready_us,
                          uint32_t status,
                          const AstraProcessLoadProfile *profile)
{
    const SupervisorLaunchReportField fields[] = {
        {" bytes=", bytes},
        {" open=", open_us},
        {" probe=", probe_us},
        {" interp=", interpreter_us},
        {" load=", load_us},
        {" ready=", ready_us},
        {"us status=", status},
    };

    launch_log("launch ", path, fields,
               (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    launch_profile_report(path, profile);
}

static uint32_t launch_open_status(uint32_t status)
{
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        return ASTRA_STATUS_NOT_FOUND;
    if (status == ASTRA_VFS_ERR_LIMIT)
        return ASTRA_STATUS_LIMIT;
    if (status == ASTRA_VFS_ERR_IO)
        return ASTRA_STATUS_IO;
    return ASTRA_STATUS_INVALID;
}

static uint32_t open_interpreter_source(
    void *context, const char *identity, AstraReadSource *source)
{
    AstraVfsReadSource *vfs_source = context;
    AstraLibraryReference reference;
    uint32_t status = astra_vfs_library_source_open(
        vfs_source, supervisor_assigns(), "LIBS", identity,
        supervisor_vfs_assign_client, NULL, &reference);

    if (status != ASTRA_VFS_OK)
        return status;
    *source = (AstraReadSource){
        .length = vfs_source->length,
        .read_at = astra_vfs_read_source_read_at,
        .release = astra_vfs_read_source_close,
        .context = vfs_source,
    };
    return ASTRA_SYSCALL_OK;
}

static uint32_t release_bootstrap_source(void *context)
{
    uint32_t status = supervisor_volume_source_close(context);

    if (status != ASTRA_VFS_OK)
        return status;
    status = supervisor_volume_unmount();
    if (status != ASTRA_VFS_OK)
        return status;
    supervisor_bootstrap_block_release();
    return ASTRA_VFS_OK;
}

static uint32_t launch_entry(const AstraStartupInfo *startup,
                             const SupervisorManifestEntry *entry,
                             const AstraServiceDefinition *service,
                             const char *bundle_root,
                             const AstraLaunchArguments *arguments,
                             uint32_t image_length, uint32_t open_us,
                             AstraReadAt read_at,
                             AstraSourceRelease release,
                             void *source,
                             uint32_t *process_id,
                             uint32_t *process_wait_handle)
{
    AstraLaunchArguments default_arguments = {0};
    AstraLaunchArguments essential_arguments = {0};
    const AstraLaunchArguments *launch_arguments = arguments;
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    uint32_t grant_count = 0u;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t child = 0u;
    uint32_t child_id = 0u;
    uint32_t child_wait = 0u;
    uint32_t published[ASTRA_MESSAGE_HANDLES_MAX] = {0u};
    uint32_t expected_handles = entry->serves_count;
    AstraReadSource program = {
        .length = image_length,
        .read_at = read_at,
        .release = release,
        .context = source,
    };
    AstraVfsReadSource interpreter_source = ASTRA_VFS_READ_SOURCE_INIT;
    AstraExecutableLoadProfile profile = {
        .size = ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE,
    };
    uint32_t status;

    if (process_wait_handle != NULL)
        *process_wait_handle = 0u;

    if (launch_arguments == NULL) {
        uint32_t length = 0u;

        while (entry->path[length] != '\0')
            ++length;
        default_arguments.count = 1u;
        default_arguments.length = (uint16_t)(length + 1u);
        default_arguments.source = ASTRA_LAUNCH_SOURCE_SYSTEM;
        default_arguments.argument_address =
            (uint32_t)(uintptr_t)entry->path;
        launch_arguments = &default_arguments;
    }
    if (entry->required != 0u) {
        essential_arguments = *launch_arguments;
        essential_arguments.flags |= ASTRA_LAUNCH_FLAG_ESSENTIAL;
        launch_arguments = &essential_arguments;
    }

    if (!supervisor_process_table_reserve(
            &process_table, process_count + 1u,
            astra_runtime_reallocate)) {
        (void)release(source);
        return ASTRA_STATUS_LIMIT;
    }
    if (astra_rt_port_create(1u, sizeof(AstraServiceReady), &receive, &send) !=
        ASTRA_SYSCALL_OK) {
        (void)release(source);
        return ASTRA_STATUS_LIMIT;
    }
    status = build_grants(startup, entry, bundle_root, send, grants,
                          &grant_count);
    uint64_t load_start = astra_clock_monotonic();
    uint32_t load_us;
    uint64_t ready_start;

    if (status == ASTRA_STATUS_OK) {
        uint32_t launch_status = astra_launch_executable_stream(
            &program, open_interpreter_source, &interpreter_source,
            grants, grant_count, launch_arguments, &profile,
            &child, &child_id);

        if (launch_status == ASTRA_SYSCALL_OK) {
            status = ASTRA_STATUS_OK;
            if (entry_grants(entry, ASTRA_CAPABILITY_POSIX_PROCESS)) {
                uint32_t service = named_service(
                    ASTRA_CAPABILITY_POSIX_PROCESS);

                status = service != 0u ?
                    astra_posix_process_register(
                        service, child, child_id,
                        ASTRA_POSIX_PROCESS_NEW_SESSION) :
                    ASTRA_STATUS_BAD_HANDLE;
                if (status != ASTRA_STATUS_OK)
                    (void)astra_process_terminate(child, 9u);
            }
            if (status == ASTRA_STATUS_OK && process_wait_handle != NULL &&
                astra_rt_handle_duplicate(
                    child, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER,
                    &child_wait) != ASTRA_SYSCALL_OK) {
                (void)astra_process_terminate(child, 9u);
                status = ASTRA_STATUS_LIMIT;
            }
        } else {
            (void)astra_log_failure(
                "astra_launch_executable_stream",
                launch_status);
            status = launch_status == ASTRA_SYSCALL_IO_ERROR ?
                         ASTRA_STATUS_IO : ASTRA_STATUS_INVALID;
        }
    } else {
        (void)release(source);
    }
    load_us = astra_elapsed_microseconds(load_start,
                                         astra_clock_monotonic());
    (void)astra_close(send);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_close(receive);
        if (child_wait != 0u)
            (void)astra_close(child_wait);
        if (child != 0u)
            (void)astra_close(child);
        return status;
    }
    ready_start = astra_clock_monotonic();
    status = receive_ready(receive, child, expected_handles, published);
    launch_report(entry->path, image_length, open_us,
                  astra_elapsed_microseconds(
                      0u, profile.executable_probe_ns),
                  astra_elapsed_microseconds(
                      0u, profile.interpreter_open_ns),
                  load_us,
                  astra_elapsed_microseconds(ready_start,
                                             astra_clock_monotonic()),
                  status, &profile.transaction);
    (void)astra_close(receive);
    if (status != ASTRA_STATUS_OK) {
        if (child_wait != 0u)
            (void)astra_close(child_wait);
        (void)astra_close(child);
        return status;
    }
    for (uint32_t index = 0u;
         status == ASTRA_STATUS_OK && index < expected_handles; ++index) {
        status = publish(&entry->serves[index], published[index],
                         service != NULL ? service->name : NULL);
        if (status == ASTRA_STATUS_OK &&
            astra_capability_name_equal(entry->serves[index].name,
                                        ASTRA_CAPABILITY_EVENT_CONTROL))
            event_control_handle = published[index];
    }
    if (status != ASTRA_STATUS_OK) {
        for (uint32_t index = 0u; index < expected_handles; ++index)
            if (published[index] != 0u)
                (void)astra_close(published[index]);
        if (child_wait != 0u)
            (void)astra_close(child_wait);
        (void)astra_close(child);
        return status;
    }
    process_table.records[process_count].resident = entry->resident;
    process_table.records[process_count].id = child_id;
    process_table.records[process_count].paused = 0u;
    process_table.records[process_count].service_flags =
        service != NULL ? service->flags : 0u;
    process_table.records[process_count].restart_policy =
        service != NULL ? service->restart_policy :
                          ASTRA_SERVICE_RESTART_NEVER;
    process_table.records[process_count].action =
        SUPERVISOR_PROCESS_ACTION_NONE;
    process_table.records[process_count].service_name[0] = '\0';
    if (service != NULL)
        (void)append(process_table.records[process_count].service_name,
                     sizeof(process_table.records[process_count].service_name),
                     service->name);
    process_table.records[process_count++].handle = child;
    if (process_id != NULL)
        *process_id = child_id;
    if (process_wait_handle != NULL)
        *process_wait_handle = child_wait;
    return ASTRA_STATUS_OK;
}

static void unpublish_owner(const char *owner)
{
    uint32_t index = 0u;

    while (index < service_count) {
        if (strcmp(service_owner_names[index], owner) != 0) {
            ++index;
            continue;
        }
        if (service_is_vfs[index] != 0u) {
            (void)astra_assign_unbind(supervisor_assigns(),
                                      service_names[index]);
            (void)astra_vfs_disconnect(&service_clients[index]);
        }
        if (service_handles[index] == event_control_handle)
            event_control_handle = 0u;
        (void)astra_close(service_handles[index]);
        --service_count;
        service_handles[index] = service_handles[service_count];
        service_is_vfs[index] = service_is_vfs[service_count];
        service_clients[index] = service_clients[service_count];
        (void)memcpy(service_names[index], service_names[service_count],
                     sizeof(service_names[index]));
        (void)memcpy(service_owner_names[index],
                     service_owner_names[service_count],
                     sizeof(service_owner_names[index]));
    }
}

static void remove_process_slot(uint32_t slot)
{
    (void)astra_close(process_table.records[slot].handle);
    --process_count;
    process_table.records[slot] = process_table.records[process_count];
}

static uint32_t stop_process_slot(uint32_t slot, uint32_t action)
{
    uint32_t status;

    if (process_table.records[slot].action !=
        SUPERVISOR_PROCESS_ACTION_NONE) {
        if (action == SUPERVISOR_PROCESS_ACTION_STOP)
            process_table.records[slot].action = action;
        return ASTRA_STATUS_OK;
    }
    unpublish_owner(process_table.records[slot].service_name);
    process_table.records[slot].action = action;
    status = astra_process_terminate(process_table.records[slot].handle,
                                     ASTRA_SIGNAL_KILL);
    if (status != ASTRA_SYSCALL_OK) {
        process_table.records[slot].action = SUPERVISOR_PROCESS_ACTION_NONE;
        return status;
    }
    return ASTRA_STATUS_OK;
}

static void remove_paused_name(uint32_t slot)
{
    --paused_service_count;
    paused_services.records[slot] =
        paused_services.records[paused_service_count];
}

static uint32_t launch_definition(const AstraStartupInfo *startup,
                                  const AstraServiceDefinition *definition)
{
    AstraLaunchArguments arguments = {0};
    SupervisorManifestEntry entry;
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    char path[ASTRA_VFS_PATH_MAX];
    char bundle_root[ASTRA_VFS_PATH_MAX];
    uint32_t open_us = 0u;
    uint32_t status;

    if (service_process_slot(definition->name) != UINT32_MAX)
        return ASTRA_STATUS_BUSY;
    for (uint32_t index = 0u; index < definition->dependency_count; ++index)
        if (named_service(definition->dependencies[index]) == 0u)
            return ASTRA_STATUS_NOT_FOUND;
    status = supervisor_service_definition_to_manifest(definition, &entry);
    if (status != ASTRA_STATUS_OK)
        return status;
    status = resolve_entry_image(&entry, path, sizeof(path), bundle_root,
                                 sizeof(bundle_root), NULL);
    if (status != ASTRA_STATUS_OK)
        return status;
    {
        uint64_t start = astra_clock_monotonic();

        status = astra_vfs_read_source_open(
            &source, supervisor_assigns(), path,
            supervisor_vfs_assign_client, NULL);
        open_us = astra_elapsed_microseconds(start, astra_clock_monotonic());
    }
    if (status != ASTRA_VFS_OK)
        return launch_open_status(status);
    arguments.count = definition->argument_count;
    arguments.length = definition->argument_length;
    arguments.source = ASTRA_LAUNCH_SOURCE_SYSTEM;
    arguments.argument_address =
        (uint32_t)(uintptr_t)definition->arguments;
    status = launch_entry(startup, &entry, definition, bundle_root,
                          &arguments, source.length, open_us,
                          astra_vfs_read_source_read_at,
                          astra_vfs_read_source_close, &source, NULL, NULL);
    if (status == ASTRA_STATUS_OK) {
        uint32_t paused = paused_service_slot(definition->name);

        if (paused != UINT32_MAX)
            remove_paused_name(paused);
        clear_service_failure(definition->name);
    } else {
        mark_service_failed(definition->name);
    }
    return status;
}

static void retry_failed_services(const AstraStartupInfo *startup)
{
    uint32_t index = 0u;

    while (index < failed_service_count) {
        char name[ASTRA_VFS_NAME_MAX];
        AstraServiceDefinition *definition = &definition_scratch[0];
        uint32_t status;

        (void)strcpy(name, failed_services.records[index].service_name);
        status = configured_definition(name, definition, NULL);
        if (status == ASTRA_STATUS_OK &&
            (definition->flags & ASTRA_SERVICE_ENABLED) != 0u &&
            definition->restart_policy != ASTRA_SERVICE_RESTART_NEVER &&
            service_process_slot(name) == UINT32_MAX) {
            status = launch_definition(startup, definition);
            if (status == ASTRA_STATUS_OK)
                continue; /* launch removed this failure record */
        }
        ++index;
    }
}

static uint32_t dynamic_definition_remove(const char *name)
{
    AstraVfsClient *client = supervisor_vfs_client();
    char path[ASTRA_VFS_PATH_MAX];
    char directory[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (client == NULL ||
        !definition_path(name, "service.conf", path, sizeof(path)))
        return ASTRA_STATUS_INVALID;
    directory[0] = '\0';
    if (!append(directory, sizeof(directory),
                SERVICE_DEFINITION_DIRECTORY) ||
        !append(directory, sizeof(directory), "/") ||
        !append(directory, sizeof(directory), name))
        return ASTRA_STATUS_LIMIT;
    status = astra_vfs_unlink(client, path);
    if (status == ASTRA_VFS_OK)
        (void)astra_vfs_unlink(client, directory);
    return status;
}

static uint32_t service_control(const AstraStartupInfo *startup,
                                uint32_t operation, const char *name,
                                AstraServiceInfo *info)
{
    AstraServiceDefinition *definition = &definition_scratch[0];
    uint32_t slot;
    int dynamic = 0;
    uint32_t status = configured_definition(name, definition, &dynamic);

    if (status != ASTRA_STATUS_OK)
        return status;
    slot = service_process_slot(name);
    if (operation == ASTRA_SERVICE_MANAGER_START) {
        status = slot == UINT32_MAX ?
            launch_definition(startup, definition) :
            process_table.records[slot].action ==
                    SUPERVISOR_PROCESS_ACTION_NONE ?
                ASTRA_STATUS_OK : ASTRA_STATUS_BUSY;
    } else if (operation == ASTRA_SERVICE_MANAGER_STOP) {
        if ((definition->flags & ASTRA_SERVICE_PROTECTED) != 0u)
            return ASTRA_STATUS_ACCESS;
        if (slot != UINT32_MAX)
            status = stop_process_slot(slot, SUPERVISOR_PROCESS_ACTION_STOP);
        if (status == ASTRA_STATUS_OK) {
            uint32_t paused = paused_service_slot(name);

            if (paused != UINT32_MAX)
                remove_paused_name(paused);
            clear_service_failure(name);
        }
    } else if (operation == ASTRA_SERVICE_MANAGER_RESTART) {
        if (slot != UINT32_MAX) {
            status = stop_process_slot(
                slot, SUPERVISOR_PROCESS_ACTION_RESTART);
            if (status != ASTRA_STATUS_OK)
                return status;
        } else
            status = launch_definition(startup, definition);
    } else if (operation == ASTRA_SERVICE_MANAGER_PAUSE) {
        if ((definition->flags & ASTRA_SERVICE_PROTECTED) != 0u)
            return ASTRA_STATUS_ACCESS;
        if (slot == UINT32_MAX)
            status = ASTRA_STATUS_OK;
        else if ((definition->flags & ASTRA_SERVICE_RUNS_PAIRED) != 0u) {
            status = stop_process_slot(
                slot, SUPERVISOR_PROCESS_ACTION_PAUSE);
        } else {
            status = astra_process_suspend(process_table.records[slot].handle);
            if (status == ASTRA_SYSCALL_OK)
                process_table.records[slot].paused = 1u;
        }
    } else if (operation == ASTRA_SERVICE_MANAGER_RESUME) {
        uint32_t paused = paused_service_slot(name);

        if (slot != UINT32_MAX &&
            process_table.records[slot].action !=
                SUPERVISOR_PROCESS_ACTION_NONE) {
            status = ASTRA_STATUS_BUSY;
        } else if (slot != UINT32_MAX &&
                   process_table.records[slot].paused != 0u) {
            status = astra_process_resume(process_table.records[slot].handle);
            if (status == ASTRA_SYSCALL_OK)
                process_table.records[slot].paused = 0u;
        } else if (paused != UINT32_MAX) {
            status = launch_definition(startup, definition);
        } else {
            status = ASTRA_STATUS_OK;
        }
    } else if (operation == ASTRA_SERVICE_MANAGER_ENABLE ||
               operation == ASTRA_SERVICE_MANAGER_DISABLE) {
        if (!dynamic || (definition->flags & ASTRA_SERVICE_PROTECTED) != 0u)
            return ASTRA_STATUS_ACCESS;
        if (operation == ASTRA_SERVICE_MANAGER_ENABLE)
            definition->flags |= ASTRA_SERVICE_ENABLED;
        else
            definition->flags &= ~ASTRA_SERVICE_ENABLED;
        status = dynamic_definition_write(definition);
    } else if (operation == ASTRA_SERVICE_MANAGER_REMOVE) {
        if (!dynamic || (definition->flags & ASTRA_SERVICE_PROTECTED) != 0u)
            return ASTRA_STATUS_ACCESS;
        if (slot != UINT32_MAX ||
            paused_service_slot(name) != UINT32_MAX)
            return ASTRA_STATUS_BUSY;
        status = dynamic_definition_remove(name);
        if (status == ASTRA_STATUS_OK)
            clear_service_failure(name);
    } else {
        return ASTRA_STATUS_UNSUPPORTED;
    }
    if (status == ASTRA_SYSCALL_OK || status == ASTRA_STATUS_OK) {
        if (operation != ASTRA_SERVICE_MANAGER_REMOVE && info != NULL) {
            if ((operation == ASTRA_SERVICE_MANAGER_ENABLE ||
                 operation == ASTRA_SERVICE_MANAGER_DISABLE) &&
                dynamic_definition_read(name, definition) != ASTRA_STATUS_OK)
                return ASTRA_STATUS_IO;
            service_info(definition, info);
        }
        return ASTRA_STATUS_OK;
    }
    return status;
}

static uint32_t list_service(const AstraServiceListCursor *cursor,
                             AstraServiceDefinition *definition,
                             AstraServiceListCursor *next_cursor)
{
    if (cursor->source == ASTRA_SERVICE_LIST_SOURCE_STATIC) {
        uint32_t index;

        if (cursor->position > UINT32_MAX)
            return ASTRA_STATUS_INVALID;
        index = (uint32_t)cursor->position;

        while (index < startup_manifest.count) {
            const SupervisorManifestEntry *entry =
                &startup_manifest.entries[index++];

            if (entry->resident == 0u ||
                supervisor_service_definition_from_manifest(
                    entry, definition) != ASTRA_STATUS_OK)
                continue;
            next_cursor->position = index < startup_manifest.count ? index : 0u;
            next_cursor->source = index < startup_manifest.count ?
                ASTRA_SERVICE_LIST_SOURCE_STATIC :
                ASTRA_SERVICE_LIST_SOURCE_DYNAMIC;
            next_cursor->reserved = 0u;
            return ASTRA_STATUS_OK;
        }
    } else if (cursor->source != ASTRA_SERVICE_LIST_SOURCE_DYNAMIC) {
        return cursor->source == ASTRA_SERVICE_LIST_SOURCE_DONE ?
            ASTRA_STATUS_NOT_FOUND : ASTRA_STATUS_INVALID;
    }
    {
        AstraVfsClient *client = supervisor_vfs_client();
        uint64_t position = cursor->source == ASTRA_SERVICE_LIST_SOURCE_DYNAMIC ?
            cursor->position : 0u;

        if (client == NULL)
            return ASTRA_STATUS_NOT_FOUND;
        for (;;) {
            char name[ASTRA_VFS_NAME_MAX];
            uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
            uint64_t next = 0u;
            uint32_t status = astra_vfs_readdir(
                client, SERVICE_DEFINITION_DIRECTORY, position, name,
                sizeof(name), &kind, &next);

            if (status != ASTRA_VFS_OK)
                return status;
            position = next;
            if (kind == ASTRA_VFS_KIND_DIRECTORY &&
                startup_definition(name, &definition_scratch[1]) !=
                    ASTRA_STATUS_OK &&
                dynamic_definition_read(name, definition) == ASTRA_STATUS_OK) {
                next_cursor->position = next;
                next_cursor->source = next == 0u ?
                    ASTRA_SERVICE_LIST_SOURCE_DONE :
                    ASTRA_SERVICE_LIST_SOURCE_DYNAMIC;
                next_cursor->reserved = 0u;
                return ASTRA_STATUS_OK;
            }
            if (next == 0u)
                return ASTRA_STATUS_NOT_FOUND;
        }
    }
}

static uint32_t definition_area(const AstraServiceDefinition *definition,
                                uint32_t *transferred)
{
    uint32_t area = 0u;
    uint32_t bytes = 0u;
    void *mapping = NULL;
    uint32_t status;

    *transferred = 0u;
    status = astra_rt_area_create(
        sizeof(*definition), ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
        ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    status = astra_rt_area_map(area,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &mapping, &bytes);
    if (status == ASTRA_SYSCALL_OK && bytes >= sizeof(*definition))
        (void)memcpy(mapping, definition, sizeof(*definition));
    else if (status == ASTRA_SYSCALL_OK)
        status = ASTRA_SYSCALL_BUFFER_TOO_SMALL;
    if (mapping != NULL && astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK &&
        status == ASTRA_SYSCALL_OK)
        status = ASTRA_SYSCALL_IO_ERROR;
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_handle_duplicate(
            area, ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
            transferred);
    (void)astra_close(area);
    return status == ASTRA_SYSCALL_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_IO;
}

static void manager_reply(uint32_t reply_send, uint32_t transaction,
                          uint32_t status, const AstraServiceInfo *info,
                          const AstraServiceListCursor *next_cursor,
                          const AstraServiceDefinition *definition)
{
    AstraServiceManagerReply reply = {0};
    uint32_t area = 0u;
    uint32_t area_count = 0u;

    if (status == ASTRA_STATUS_OK && definition != NULL) {
        status = definition_area(definition, &area);
        area_count = status == ASTRA_STATUS_OK ? 1u : 0u;
    }
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_SERVICE_MANAGER_PROTOCOL,
                             ASTRA_SERVICE_MANAGER_VERSION,
                             ASTRA_SERVICE_MANAGER_REPLY, transaction);
    reply.status = status;
    if (status == ASTRA_STATUS_OK && next_cursor != NULL)
        reply.next_cursor = *next_cursor;
    if (status == ASTRA_STATUS_OK && info != NULL)
        reply.info = *info;
    (void)astra_port_send(reply_send, &reply, sizeof(reply),
                          area_count != 0u ? &area : NULL, area_count);
    if (area != 0u)
        (void)astra_close(area);
    (void)astra_close(reply_send);
}

static void pump_manager(const AstraStartupInfo *startup)
{
    AstraServiceManagerRequest request = {0};
    AstraServiceDefinition *definition = &definition_scratch[0];
    AstraServiceInfo info = {0};
    uint32_t handles[2] = {0u, 0u};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status = astra_port_receive(
        manager_receive, &request, sizeof(request), handles, 2u,
        &size, &handle_count);
    AstraServiceListCursor next = ASTRA_SERVICE_LIST_CURSOR_INIT;
    int send_definition = 0;

    if (status != ASTRA_SYSCALL_OK)
        return;
    if (size != sizeof(request) || handle_count < 1u ||
        handle_count > 2u || handles[0] == 0u ||
        request.header.total_size != sizeof(request) ||
        request.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request.header.protocol != ASTRA_SERVICE_MANAGER_PROTOCOL ||
        request.header.protocol_version != ASTRA_SERVICE_MANAGER_VERSION ||
        request.header.transaction_id == 0u ||
        request.header.operation < ASTRA_SERVICE_MANAGER_LIST ||
        request.header.operation > ASTRA_SERVICE_MANAGER_DISABLE ||
        (request.header.operation == ASTRA_SERVICE_MANAGER_LIST &&
         (request.cursor.source > ASTRA_SERVICE_LIST_SOURCE_DONE ||
          request.cursor.reserved != 0u || request.name[0] != '\0')) ||
        (request.header.operation != ASTRA_SERVICE_MANAGER_LIST &&
         (request.cursor.position != 0u || request.cursor.source != 0u ||
          request.cursor.reserved != 0u)) ||
        ((request.header.operation == ASTRA_SERVICE_MANAGER_ADD) !=
         (handle_count == 2u)) ||
         (request.header.operation != ASTRA_SERVICE_MANAGER_LIST &&
         !astra_service_name_valid(request.name))) {
        if (handle_count >= 1u && handles[0] != 0u)
            manager_reply(handles[0], request.header.transaction_id,
                          ASTRA_STATUS_PROTOCOL, NULL, NULL, NULL);
        if (handle_count == 2u)
            (void)astra_close(handles[1]);
        return;
    }
    if (request.header.operation == ASTRA_SERVICE_MANAGER_LIST) {
        status = list_service(&request.cursor, definition, &next);
        if (status == ASTRA_STATUS_OK)
            service_info(definition, &info);
    } else if (request.header.operation == ASTRA_SERVICE_MANAGER_INSPECT) {
        status = configured_definition(request.name, definition, NULL);
        if (status == ASTRA_STATUS_OK) {
            service_info(definition, &info);
            send_definition = 1;
        }
    } else if (request.header.operation == ASTRA_SERVICE_MANAGER_ADD) {
        void *mapping = NULL;
        uint32_t bytes = 0u;

        status = astra_rt_area_map(handles[1], ASTRA_AREA_MAP_READ,
                                   &mapping, &bytes);
        if (status == ASTRA_SYSCALL_OK && bytes >= sizeof(*definition)) {
            (void)memcpy(definition, mapping, sizeof(*definition));
            status = astra_service_definition_validate(definition) ==
                ASTRA_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
        } else {
            status = ASTRA_STATUS_INVALID;
        }
        if (mapping != NULL)
            (void)astra_rt_area_unmap(mapping);
        (void)astra_close(handles[1]);
        if (status == ASTRA_STATUS_OK &&
            (definition->flags & ASTRA_SERVICE_PROTECTED) != 0u)
            status = ASTRA_STATUS_ACCESS;
        if (status == ASTRA_STATUS_OK &&
            configured_definition(definition->name,
                                  &definition_scratch[1], NULL) ==
                ASTRA_STATUS_OK)
            status = ASTRA_STATUS_EXISTS;
        if (status == ASTRA_STATUS_OK)
            status = dynamic_definition_write(definition);
        if (status == ASTRA_STATUS_OK)
            service_info(definition, &info);
    } else {
        status = service_control(startup, request.header.operation,
                                 request.name, &info);
    }
    manager_reply(handles[0], request.header.transaction_id, status,
                  status == ASTRA_STATUS_OK ? &info : NULL, &next,
                  send_definition != 0 ? definition : NULL);
}

/*
 * Brings PROC: up. Failure is not fatal: a machine that cannot show its own
 * process list is worse than one that can, and much better than one that
 * refuses to boot over it. The send handle stays zero and children are granted
 * nothing, which is what a child then sees.
 */
static void proc_tree_start(void)
{
    void *file_storage = NULL;
    uint32_t file_capacity = 0u;

    if (proc_send != 0u)
        return;
    if (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &file_storage, &file_capacity) ||
        !astra_vfs_service_init(
            &proc_service, supervisor_proc_ops(), NULL, proc_sessions,
            ASTRA_VFS_SESSION_MAX, file_storage, file_capacity))
        return;
    if (astra_rt_port_create(SUPERVISOR_PROC_PORT_MESSAGES,
                             (uint32_t)sizeof(AstraVfsRenameRequestMessage),
                             &proc_receive, &proc_send) != ASTRA_SYSCALL_OK) {
        proc_send = 0u;
        return;
    }
    if (!astra_vfs_port_service_init(&proc_port, proc_receive,
                                     &proc_service)) {
        (void)astra_close(proc_receive);
        (void)astra_close(proc_send);
        proc_receive = 0u;
        proc_send = 0u;
        return;
    }
    /*
     * Bound into this process's own namespace, not special-cased at the point
     * of a launch. A child inherits PROC: the way it inherits COMMANDS:, and a
     * terminal that was granted it can pass it on to what it launches without
     * knowing it is served from here.
     */
    if (astra_assign_bind(supervisor_assigns(), "PROC", proc_send,
                          ASTRA_RIGHT_READ, "") != ASTRA_VFS_OK) {
        (void)astra_close(proc_receive);
        (void)astra_close(proc_send);
        proc_receive = 0u;
        proc_send = 0u;
    }
}

uint32_t supervisor_loader_proc_mount(void)
{
    return proc_send;
}

void supervisor_loader_pump_proc(void)
{
    if (proc_send != 0u)
        (void)astra_vfs_port_service_pump(&proc_port,
                                          SUPERVISOR_PROC_PORT_BUDGET);
}

static void launch_reply(uint32_t reply_send, uint32_t transaction,
                         uint32_t status, uint32_t process_id,
                         uint32_t process_wait_handle)
{
    AstraApplicationLaunchReply reply = {0};
    uint32_t send_status;

    if (status == ASTRA_STATUS_OK && process_wait_handle == 0u)
        status = ASTRA_STATUS_IO;
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_APPLICATION_PROTOCOL,
                             ASTRA_APPLICATION_VERSION,
                             ASTRA_APPLICATION_LAUNCHED, transaction);
    reply.status = status;
    reply.process_id = status == ASTRA_STATUS_OK ? process_id : 0u;
    send_status = astra_port_send(
        reply_send, &reply, sizeof(reply),
        status == ASTRA_STATUS_OK ? &process_wait_handle : NULL,
        status == ASTRA_STATUS_OK ? 1u : 0u);
    if (send_status != ASTRA_SYSCALL_OK && process_wait_handle != 0u)
        (void)astra_close(process_wait_handle);
    (void)astra_close(reply_send);
}

static void pump_launch(const AstraStartupInfo *startup)
{
    AstraApplicationLaunchRequest request = {0};
    AstraLaunchArguments launch_arguments = {0};
    AstraBundleManifest bundle = ASTRA_BUNDLE_MANIFEST_INIT;
    SupervisorManifestEntry entry = {0};
    uint32_t reply_send = 0u;
    uint32_t size = 0u;
    uint32_t handles = 0u;
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    uint32_t process_id = 0u;
    uint32_t process_wait_handle = 0u;
    uint32_t open_us = 0u;
    uint32_t status;
    char entry_path[ASTRA_VFS_PATH_MAX];
    char bundle_root[ASTRA_VFS_PATH_MAX];
    char bundle_path[ASTRA_APPLICATION_PATH_MAX];

    status = astra_port_receive(launch_receive, &request, sizeof(request),
                                &reply_send, 1u, &size, &handles);
    if (status != ASTRA_SYSCALL_OK)
        return;
    if (size != sizeof(request) || handles != 1u || reply_send == 0u ||
        request.header.total_size != sizeof(request) ||
        request.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request.header.flags != 0u || request.header.reserved != 0u ||
        request.header.protocol != ASTRA_APPLICATION_PROTOCOL ||
        request.header.protocol_version != ASTRA_APPLICATION_VERSION ||
        request.header.operation != ASTRA_APPLICATION_LAUNCH ||
        request.header.transaction_id == 0u ||
        request.arguments.count == 0u ||
        request.arguments.count > ASTRA_APPLICATION_ARGUMENT_MAX ||
        request.arguments.length == 0u ||
        request.arguments.length > ASTRA_APPLICATION_ARGUMENT_BYTES ||
        request.arguments.flags != 0u ||
        request.arguments.environment_count != 0u ||
        request.arguments.environment_length != 0u ||
        request.arguments.environment_address != 0u ||
        (request.arguments.source != ASTRA_LAUNCH_SOURCE_SHELL &&
         request.arguments.source != ASTRA_LAUNCH_SOURCE_DESKTOP)) {
        if (reply_send != 0u)
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_PROTOCOL, 0u, 0u);
        return;
    }
    {
        uint32_t consumed = 0u;

        for (uint32_t index = 0u; index < request.arguments.count; ++index) {
            while (consumed < request.arguments.length &&
                   request.arguments.bytes[consumed] != '\0')
                ++consumed;
            if (consumed == request.arguments.length) {
                launch_reply(reply_send, request.header.transaction_id,
                             ASTRA_STATUS_PROTOCOL, 0u, 0u);
                return;
            }
            ++consumed;
        }
        if (consumed != request.arguments.length) {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_PROTOCOL, 0u, 0u);
            return;
        }
    }
    {
        uint32_t path_length = 0u;

        while (request.arguments.bytes[path_length] != '\0')
            ++path_length;
        if (path_length == 0u || path_length >= sizeof(bundle_path)) {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_INVALID, 0u, 0u);
            return;
        }
        for (uint32_t at = 0u; at <= path_length; ++at)
            bundle_path[at] = request.arguments.bytes[at];
    }
    if (!ends_with(bundle_path, ".app") ||
        strncmp(bundle_path, "/apps/", 6u) != 0 ||
        bundle_path[6] == '\0') {
        launch_reply(reply_send, request.header.transaction_id,
                     ASTRA_STATUS_INVALID, 0u, 0u);
        return;
    }
    for (uint32_t at = 6u; bundle_path[at] != '\0'; ++at)
        if (bundle_path[at] == '/' || bundle_path[at] == '\\') {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_INVALID, 0u, 0u);
            return;
        }
    if (!append(entry.path, sizeof(entry.path), bundle_path)) {
        launch_reply(reply_send, request.header.transaction_id,
                     ASTRA_STATUS_LIMIT, 0u, 0u);
        return;
    }
    entry.delegates = 1u;
    status = resolve_entry_image(&entry, entry_path, sizeof(entry_path),
                                 bundle_root, sizeof(bundle_root), &bundle);
    for (uint32_t at = 0u;
         status == ASTRA_STATUS_OK && at < bundle.capability_count; ++at) {
        if (entry.grant_count == SUPERVISOR_MANIFEST_GRANT_MAX ||
            !supervisor_manifest_grant(
                bundle.capabilities[at],
                &entry.grants[entry.grant_count])) {
            status = ASTRA_STATUS_LIMIT;
            break;
        }
        ++entry.grant_count;
    }
    astra_bundle_manifest_destroy(&bundle);
    if (status == ASTRA_STATUS_OK) {
        {
            uint64_t read_start = astra_clock_monotonic();

            status = astra_vfs_read_source_open(
                &source, supervisor_assigns(), entry_path,
                supervisor_vfs_assign_client, NULL);
            open_us = astra_elapsed_microseconds(
                read_start, astra_clock_monotonic());
        }
        if (status == ASTRA_VFS_OK) {
            launch_arguments.count = request.arguments.count;
            launch_arguments.length = request.arguments.length;
            launch_arguments.source = request.arguments.source;
            launch_arguments.argument_address =
                (uint32_t)(uintptr_t)request.arguments.bytes;
            status = launch_entry(startup, &entry, NULL, bundle_root,
                                  &launch_arguments, source.length, open_us,
                                  astra_vfs_read_source_read_at,
                                  astra_vfs_read_source_close, &source,
                                  &process_id, &process_wait_handle);
        } else {
            status = launch_open_status(status);
        }
    }
    if (status != ASTRA_STATUS_OK)
        (void)astra_log_failure("application launch", status);
    launch_reply(reply_send, request.header.transaction_id, status,
                 process_id, process_wait_handle);
}

uint32_t supervisor_loader_start(const AstraStartupInfo *startup)
{
    SupervisorManifest *manifest = &startup_manifest;
    char *manifest_text = NULL;
    uint32_t manifest_length = 0u;
    uint32_t image_length = 0u;
    uint32_t open_us = 0u;
    uint32_t status;

    supervisor_process_handle = startup->process_handle;
    status = supervisor_volume_read_alloc(MANIFEST_PATH,
                                          (void **)&manifest_text,
                                          &manifest_length);
    if (status != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    status = supervisor_manifest_parse(manifest_text, manifest_length,
                                       manifest) ? ASTRA_STATUS_OK :
                                       SUPERVISOR_LOADER_FAIL_MANIFEST;
    astra_runtime_deallocate(manifest_text);
    if (status != ASTRA_STATUS_OK)
        return SUPERVISOR_LOADER_FAIL_MANIFEST;
    if (strcmp(manifest->entries[0].path, "/services/storage") != 0)
        return SUPERVISOR_LOADER_FAIL_ORDER;
    if (astra_rt_port_create(1u, ASTRA_EVENT_CONTROL_REQUEST_SIZE,
                          &event_target_receive, &event_target_send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    if (astra_rt_port_create(4u, ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE * 4u,
                             &launch_receive, &launch_send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    {
        uint64_t open_start = astra_clock_monotonic();

        status = supervisor_volume_source_open(STORAGE_IMAGE_PATH,
                                               &image_length);
        open_us = astra_elapsed_microseconds(open_start,
                                             astra_clock_monotonic());
    }
    if (status != ASTRA_VFS_OK)
        return launch_open_status(status);

    {
        AstraServiceDefinition *service = &definition_scratch[0];

        status = supervisor_service_definition_from_manifest(
            &manifest->entries[0], service);
        if (status == ASTRA_STATUS_OK)
            status = launch_entry(startup, &manifest->entries[0], service,
                                  NULL, NULL, image_length, open_us,
                                  supervisor_volume_source_read_at,
                                  release_bootstrap_source, NULL, NULL, NULL);
    }
    if (status != ASTRA_STATUS_OK)
        return status;
    supervisor_bootstrap_block_close();
    if (astra_rt_port_create(8u,
            8u * (uint32_t)sizeof(AstraServiceManagerRequest),
            &manager_receive, &manager_send) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    {
        SupervisorManifestPublication manager = {0};

        astra_capability_name_set(manager.name,
                                  ASTRA_CAPABILITY_SERVICE_MANAGER);
        status = publish(&manager, manager_send, NULL);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
    {
        uint32_t create_status = astra_vfs_mkdir_mode(
            supervisor_vfs_client(), SERVICE_DEFINITION_DIRECTORY, 0700u);

        if (create_status != ASTRA_VFS_OK &&
            create_status != ASTRA_VFS_ERR_EXISTS)
            (void)astra_log_failure("service definition directory",
                                    create_status);
    }
    for (uint32_t index = 1u; index < manifest->count; ++index) {
        const SupervisorManifestEntry *entry = &manifest->entries[index];
        AstraServiceDefinition *service = &definition_scratch[0];
        const AstraServiceDefinition *service_pointer = NULL;
        char entry_path[ASTRA_VFS_PATH_MAX];
        char bundle_root[ASTRA_VFS_PATH_MAX];

        status = resolve_entry_image(entry, entry_path, sizeof(entry_path),
                                     bundle_root, sizeof(bundle_root), NULL);
        if (status == ASTRA_STATUS_OK) {
            AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;

            {
                uint64_t read_start = astra_clock_monotonic();

                status = astra_vfs_read_source_open(
                    &source, supervisor_assigns(), entry_path,
                    supervisor_vfs_assign_client, NULL);
                open_us = astra_elapsed_microseconds(
                    read_start, astra_clock_monotonic());
            }
            if (status == ASTRA_VFS_OK) {
                if (entry->resident != 0u &&
                    supervisor_service_definition_from_manifest(
                        entry, service) == ASTRA_STATUS_OK)
                    service_pointer = service;
                status = launch_entry(startup, entry, service_pointer,
                                      bundle_root, NULL, source.length,
                                      open_us,
                                      astra_vfs_read_source_read_at,
                                      astra_vfs_read_source_close, &source,
                                      NULL, NULL);
            } else
                status = launch_open_status(status);
        }
        if (status != ASTRA_STATUS_OK && service_pointer != NULL)
            mark_service_failed(service_pointer->name);
        if (status != ASTRA_STATUS_OK && entry->required)
            return status;
    }
    {
        AstraServiceListCursor cursor = {
            0u, ASTRA_SERVICE_LIST_SOURCE_DYNAMIC, 0u
        };

        for (;;) {
            AstraServiceListCursor next = ASTRA_SERVICE_LIST_CURSOR_INIT;
            AstraServiceDefinition *service = &definition_scratch[0];

            status = list_service(&cursor, service, &next);
            if (status == ASTRA_STATUS_NOT_FOUND)
                break;
            if (status != ASTRA_STATUS_OK) {
                (void)astra_log_failure("service definition scan", status);
                break;
            }
            if ((service->flags & ASTRA_SERVICE_ENABLED) != 0u &&
                service->start_policy == ASTRA_SERVICE_START_BOOT) {
                status = launch_definition(startup, service);
                if (status != ASTRA_STATUS_OK)
                    (void)astra_log_failure(service->name, status);
            }
            if (next.source == ASTRA_SERVICE_LIST_SOURCE_DONE)
                break;
            cursor = next;
        }
    }
    proc_tree_start();
    /*
     * These receivers are services implemented by this process, so their
     * senders are provider-owned state.  Retaining them keeps idle services
     * alive and lets later launches receive the same capabilities.  Tying an
     * endpoint's lifetime to whichever startup client happened to receive it
     * made that client's exit wake PID 1 with PEER_DEAD.
     */
    for (uint32_t index = 0u; index < 5u; ++index) {
        static const char *const names[] = {
            ASTRA_CAPABILITY_DISPLAY_DEVICE, ASTRA_CAPABILITY_INPUT_DEVICE,
            ASTRA_CAPABILITY_INPUT_IRQ, ASTRA_CAPABILITY_DISPLAY_IRQ,
            ASTRA_CAPABILITY_DISPLAY_VBLANK_IRQ
        };
        const AstraStartupCapability *held = astra_startup_capability(
            startup, names[index]);

        if (held != NULL && held->handle != 0u)
            (void)astra_close(held->handle);
    }
    /*
     * The supervisor owns the service-control lifetime.  A terminal or GUI is
     * only a client; closing the last sender here makes the events service die
     * whenever a boot profile has no such client, or when that client exits.
     */
    return ASTRA_STATUS_OK;
}

uint32_t supervisor_loader_process_handle(void)
{
    return supervisor_process_handle;
}

uint32_t supervisor_loader_event_control(void)
{
    return event_control_handle;
}

void supervisor_loader_pump_event_control(void)
{
    (void)astra_event_control_pump(event_target_receive, 1u);
}

uint32_t supervisor_loader_watch(const AstraStartupInfo *startup)
{
    uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];

    for (;;) {
        uint32_t index = ASTRA_WAIT_INDEX_NONE;
        uint32_t status;

        waits[0] = event_target_receive;
        waits[1] = launch_receive;
        waits[2] = proc_receive;
        waits[3] = manager_receive;
        if (process_count > ASTRA_WAIT_MULTIPLE_MAX - 4u)
            return ASTRA_STATUS_LIMIT;
        for (uint32_t at = 0u; at < process_count; ++at)
            waits[at + 4u] = process_table.records[at].handle;
        status = astra_wait_multiple(waits, process_count + 4u,
                                     failed_service_count != 0u ?
                                         service_retry_at :
                                         ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            retry_failed_services(startup);
            if (failed_service_count != 0u)
                service_retry_at = astra_clock_monotonic() +
                                   SERVICE_RETRY_NS;
            continue;
        }

        if (index == 0u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            supervisor_loader_pump_event_control();
            continue;
        }
        if (index == 1u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            pump_launch(startup);
            continue;
        }
        if (index == 2u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            supervisor_loader_pump_proc();
            continue;
        }
        if (index == 3u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            pump_manager(startup);
            continue;
        }
        if (index > 3u && index <= process_count + 3u) {
            uint32_t exit_status = 0u;
            uint32_t slot = index - 4u;
            uint32_t wait_status = astra_process_wait(
                process_table.records[slot].handle, 0u, &exit_status);

            if (wait_status != ASTRA_SYSCALL_TIMED_OUT) {
                char service_name[ASTRA_VFS_NAME_MAX];
                uint32_t restart =
                    process_table.records[slot].restart_policy;
                uint32_t action = process_table.records[slot].action;

                (void)memcpy(service_name,
                             process_table.records[slot].service_name,
                             sizeof(service_name));
                if (process_table.records[slot].resident != 0u &&
                    action == SUPERVISOR_PROCESS_ACTION_NONE)
                    (void)astra_log_failure(
                        "resident process exited",
                        exit_status != 0u ? exit_status :
                                            ASTRA_STATUS_PEER_DEAD);
                if (service_name[0] != '\0')
                    unpublish_owner(service_name);
                remove_process_slot(slot);
                if (service_name[0] != '\0') {
                    if (action == SUPERVISOR_PROCESS_ACTION_NONE &&
                        exit_status != ASTRA_STATUS_OK)
                        mark_service_failed(service_name);
                    else if (action == SUPERVISOR_PROCESS_ACTION_STOP ||
                             action == SUPERVISOR_PROCESS_ACTION_PAUSE)
                        clear_service_failure(service_name);
                }
                if (action == SUPERVISOR_PROCESS_ACTION_PAUSE &&
                    service_name[0] != '\0' &&
                    paused_service_slot(service_name) ==
                        UINT32_MAX) {
                    if (!supervisor_process_table_reserve(
                            &paused_services, paused_service_count + 1u,
                            astra_runtime_reallocate)) {
                        (void)astra_log_failure("service pause",
                                                ASTRA_STATUS_LIMIT);
                    } else {
                        (void)strcpy(
                            paused_services.records[
                                paused_service_count++].service_name,
                            service_name);
                    }
                } else if (action == SUPERVISOR_PROCESS_ACTION_RESTART &&
                           service_name[0] != '\0') {
                    AstraServiceDefinition *definition =
                        &definition_scratch[0];
                    uint32_t restart_status = configured_definition(
                        service_name, definition, NULL);

                    if (restart_status == ASTRA_STATUS_OK)
                        restart_status = launch_definition(startup,
                                                           definition);
                    if (restart_status != ASTRA_STATUS_OK)
                        (void)astra_log_failure("service restart",
                                                restart_status);
                } else if (action == SUPERVISOR_PROCESS_ACTION_NONE &&
                           service_name[0] != '\0' &&
                    (restart == ASTRA_SERVICE_RESTART_ALWAYS ||
                     (restart == ASTRA_SERVICE_RESTART_ON_FAULT &&
                      exit_status != ASTRA_STATUS_OK))) {
                    AstraServiceDefinition *definition =
                        &definition_scratch[0];
                    uint32_t restart_status = configured_definition(
                        service_name, definition, NULL);

                    if (restart_status == ASTRA_STATUS_OK)
                        restart_status = launch_definition(startup,
                                                           definition);
                    if (restart_status != ASTRA_STATUS_OK)
                        (void)astra_log_failure("service restart",
                                                restart_status);
                }
                continue;
            }
        }
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
    }
}
