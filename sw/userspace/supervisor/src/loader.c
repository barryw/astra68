#include <loader.h>

#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/event_control.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_path.h>
#include <astra/vfs_port_transport.h>

#define MANIFEST_PATH "/vol/startup/system"
#define STORAGE_IMAGE_PATH "/vol/services/storage"
#define LOADER_MANIFEST_MAX 2048u
#define LOADER_IMAGE_MAX (80u * 1024u)
#define READY_DEADLINE_NS 10000000000ull

#define LOADER_FAIL_MANIFEST 32u
#define LOADER_FAIL_ORDER 33u
#define LOADER_FAIL_PUBLISH 35u

static char manifest_text[LOADER_MANIFEST_MAX];
static uint8_t image[LOADER_IMAGE_MAX];
static uint32_t process_handles[SUPERVISOR_MANIFEST_ENTRY_MAX];
static uint32_t service_handles[SUPERVISOR_MANIFEST_ENTRY_MAX];
static char service_names[SUPERVISOR_MANIFEST_ENTRY_MAX]
                         [ASTRA_CAPABILITY_NAME_MAX];
static AstraVfsClient service_clients[SUPERVISOR_MANIFEST_ENTRY_MAX];
static uint32_t process_count;
static uint32_t service_count;
static uint32_t event_target_receive;
static uint32_t event_target_send;
static uint32_t event_control_handle;

static int equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static const char *private_store_root(const SupervisorManifestEntry *entry)
{
    const char *root = entry->path;

    while (*root != '\0' && *root != ':')
        ++root;
    return *root == ':' ? root + 1 : "";
}

static const AstraStartupCapability *
startup_capability(const AstraStartupInfo *startup,
                   const AstraStartupCapability *capabilities,
                   const char *name)
{
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, name))
            return &capabilities[index];
    }
    return NULL;
}

static uint32_t named_service(const char *name)
{
    for (uint32_t index = 0u; index < service_count; ++index)
        if (astra_capability_name_equal(service_names[index], name))
            return service_handles[index];
    return 0u;
}

static uint32_t load_vfs_image(const char *path, uint32_t *length)
{
    AstraVfsFile file;
    uint64_t size = 0u;
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;
    uint16_t kind = 0u;

    *length = 0u;
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        char wire[ASTRA_VFS_PATH_MAX];

        status = astra_assign_resolve(supervisor_assigns(), path,
                                      ASTRA_RIGHT_READ, member, wire,
                                      sizeof(wire), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return status;
        if (status != ASTRA_VFS_OK)
            continue;
        client = supervisor_vfs_client_for(assign);
        if (client == NULL)
            continue;
        status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ, &file,
                                &size, &kind);
        if (status != ASTRA_VFS_OK)
            continue;
        if (kind == ASTRA_VFS_KIND_DIRECTORY || size > sizeof(image)) {
            (void)astra_vfs_close(client, file);
            return ASTRA_VFS_ERR_LIMIT;
        }
        while (*length < (uint32_t)size) {
            uint32_t moved = 0u;

            status = astra_vfs_read(client, file, *length, image + *length,
                                    (uint32_t)size - *length, &moved);
            if (status != ASTRA_VFS_OK || moved == 0u)
                break;
            *length += moved;
        }
        (void)astra_vfs_close(client, file);
        return status == ASTRA_VFS_OK && *length == (uint32_t)size ?
            ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
    }
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
                             const AstraStartupCapability *capabilities,
                             const SupervisorManifestEntry *entry,
                             uint32_t ready_send, AstraLaunchGrant *out,
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
            if (equal(wanted->name, ASTRA_CAPABILITY_EVENT_CONTROL)) {
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
            const AstraStartupCapability *held = startup_capability(
                startup, capabilities, wanted->name);

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
        if (equal(wanted->name, "STORE")) {
            if (supervisor_vfs_port() == 0u)
                continue;
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
    if (equal(entry->serves, "EVENTS")) {
        if (event_target_send == 0u)
            return ASTRA_STATUS_BAD_HANDLE;
        status = add_grant(out, count, ASTRA_CAPABILITY_EVENT_TARGET,
                           event_target_send, ASTRA_RIGHT_SIGNAL, 0u, NULL);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t receive_ready(uint32_t receive, uint32_t child,
                              uint32_t expected_handles,
                              uint32_t published[2])
{
    AstraServiceReady message;
    uint32_t handles[2] = {0u, 0u};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint64_t deadline = astra_clock_monotonic() + READY_DEADLINE_NS;
    uint32_t status;

    published[0] = 0u;
    published[1] = 0u;
    for (;;) {
        uint32_t exit_status = 0u;

        status = astra_port_receive(receive, &message, sizeof(message),
                                    handles, 2u, &size, &handle_count);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK)
            return ASTRA_STATUS_PEER_DEAD;
        if (astra_process_wait(child, 0u, &exit_status) !=
            ASTRA_SYSCALL_TIMED_OUT)
            return exit_status != 0u ? exit_status : ASTRA_STATUS_PEER_DEAD;
        status = astra_wait_one(receive, deadline, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_TIMED_OUT ? ASTRA_STATUS_BUSY :
                                                       ASTRA_STATUS_PEER_DEAD;
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

static uint32_t publish(const SupervisorManifestEntry *entry,
                        uint32_t handle)
{
    AstraVfsClient *client;

    if (entry->serves[0] == '\0')
        return ASTRA_STATUS_OK;
    if (equal(entry->serves, "SYS"))
        return supervisor_vfs_start(handle) ? ASTRA_STATUS_OK :
                                              LOADER_FAIL_PUBLISH;
    if (service_count == SUPERVISOR_MANIFEST_ENTRY_MAX)
        return ASTRA_STATUS_LIMIT;
    service_handles[service_count] = handle;
    astra_capability_name_set(service_names[service_count], entry->serves);
    if (entry->serves_rights == 0u) {
        ++service_count;
        return ASTRA_STATUS_OK;
    }
    client = &service_clients[service_count];
    if (astra_vfs_port_connect(client, service_handles[service_count]) !=
            ASTRA_VFS_OK ||
        supervisor_vfs_register(client, handle) == 0u ||
        astra_assign_bind(supervisor_assigns(), entry->serves, handle,
                          entry->serves_rights, "") != ASTRA_VFS_OK)
        return LOADER_FAIL_PUBLISH;
    ++service_count;
    return ASTRA_STATUS_OK;
}

static uint32_t launch_entry(const AstraStartupInfo *startup,
                             const AstraStartupCapability *capabilities,
                             const SupervisorManifestEntry *entry,
                             uint32_t image_length)
{
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    uint32_t grant_count = 0u;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t child = 0u;
    uint32_t child_id = 0u;
    uint32_t published[2] = {0u, 0u};
    uint32_t expected_handles = entry->serves[0] == '\0' ? 0u :
        (equal(entry->serves, "EVENTS") ? 2u : 1u);
    uint32_t status;

    if (astra_rt_port_create(1u, sizeof(AstraServiceReady), &receive, &send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    status = build_grants(startup, capabilities, entry, send, grants,
                          &grant_count);
    if (status == ASTRA_STATUS_OK)
        status = astra_launch(image, image_length, grants, grant_count, NULL,
                              &child, &child_id) == ASTRA_SYSCALL_OK ?
            ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    (void)astra_close(send);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_close(receive);
        return status;
    }
    status = receive_ready(receive, child, expected_handles, published);
    (void)astra_close(receive);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_close(child);
        return status;
    }
    status = publish(entry, published[0]);
    if (status != ASTRA_STATUS_OK) {
        for (uint32_t index = 0u; index < expected_handles; ++index)
            if (published[index] != 0u)
                (void)astra_close(published[index]);
        (void)astra_close(child);
        return status;
    }
    if (expected_handles == 2u)
        event_control_handle = published[1];
    if (entry->resident != 0u)
        process_handles[process_count++] = child;
    else
        (void)astra_close(child);
    (void)child_id;
    return ASTRA_STATUS_OK;
}

uint32_t supervisor_loader_start(
    const AstraStartupInfo *startup,
    const AstraStartupCapability *capabilities)
{
    SupervisorManifest manifest;
    uint32_t manifest_length = 0u;
    uint32_t image_length = 0u;
    uint32_t status;

    status = supervisor_volume_read(MANIFEST_PATH, manifest_text,
                                    sizeof(manifest_text) - 1u,
                                    &manifest_length);
    if (status != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    manifest_text[manifest_length] = '\0';
    if (!supervisor_manifest_parse(manifest_text, manifest_length, &manifest))
        return LOADER_FAIL_MANIFEST;
    if (!equal(manifest.entries[0].path, "SERVICES:storage"))
        return LOADER_FAIL_ORDER;
    if (astra_rt_port_create(1u, ASTRA_EVENT_CONTROL_REQUEST_SIZE,
                          &event_target_receive, &event_target_send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    status = supervisor_volume_read(STORAGE_IMAGE_PATH, image, sizeof(image),
                                    &image_length);
    if (status != ASTRA_VFS_OK)
        return status == ASTRA_VFS_ERR_LIMIT ? ASTRA_STATUS_LIMIT :
                                              ASTRA_STATUS_NOT_FOUND;
    if (supervisor_volume_unmount() != ASTRA_VFS_OK)
        return ASTRA_STATUS_IO;
    supervisor_bootstrap_block_release();

    status = launch_entry(startup, capabilities, &manifest.entries[0],
                          image_length);
    if (status != ASTRA_STATUS_OK)
        return status;
    supervisor_bootstrap_block_close();
    for (uint32_t index = 1u; index < manifest.count; ++index) {
        const SupervisorManifestEntry *entry = &manifest.entries[index];

        status = load_vfs_image(entry->path, &image_length);
        if (status == ASTRA_VFS_OK)
            status = launch_entry(startup, capabilities, entry, image_length);
        else
            status = status == ASTRA_VFS_ERR_LIMIT ? ASTRA_STATUS_LIMIT :
                                                     ASTRA_STATUS_NOT_FOUND;
        if (status != ASTRA_STATUS_OK && entry->required)
            return status;
    }
    (void)astra_close(event_target_send);
    event_target_send = 0u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        static const char *const names[] = {
            ASTRA_CAPABILITY_DISPLAY_DEVICE, ASTRA_CAPABILITY_INPUT_DEVICE,
            ASTRA_CAPABILITY_INPUT_IRQ, ASTRA_CAPABILITY_DISPLAY_IRQ
        };
        const AstraStartupCapability *held = startup_capability(
            startup, capabilities, names[index]);

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

uint32_t supervisor_loader_event_control(void)
{
    return event_control_handle;
}

void supervisor_loader_pump_event_control(void)
{
    (void)astra_event_control_pump(event_target_receive, 1u);
}

uint32_t supervisor_loader_watch(void)
{
    uint32_t waits[SUPERVISOR_MANIFEST_ENTRY_MAX + 1u];

    waits[0] = event_target_receive;
    for (uint32_t index = 0u; index < process_count; ++index)
        waits[index + 1u] = process_handles[index];
    for (;;) {
        uint32_t index = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(
            waits, process_count + 1u, ASTRA_DEADLINE_FOREVER, &index, NULL);

        if (index == 0u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            supervisor_loader_pump_event_control();
            continue;
        }
        if (index > 0u && index <= process_count) {
            uint32_t exit_status = 0u;
            uint32_t wait_status = astra_process_wait(
                process_handles[index - 1u], 0u, &exit_status);

            if (wait_status != ASTRA_SYSCALL_TIMED_OUT)
                return exit_status != 0u ? exit_status :
                                           ASTRA_STATUS_PEER_DEAD;
        }
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
    }
}
