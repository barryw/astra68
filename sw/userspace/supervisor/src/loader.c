#include <loader.h>
#include <proc_tree.h>

#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/application_service.h>
#include <astra/boot.h>
#include <astra/bundle.h>
#include <astra/display.h>
#include <astra/event_control.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_path.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

#define MANIFEST_PATH "/vol/startup/system"
#define STORAGE_IMAGE_PATH "/vol/services/storage"
#define LOADER_MANIFEST_MAX 2048u
#define READY_DEADLINE_NS 10000000000ull

#define LOADER_FAIL_MANIFEST 32u
#define LOADER_FAIL_ORDER 33u
#define LOADER_FAIL_PUBLISH 35u

static char manifest_text[LOADER_MANIFEST_MAX];
static char bundle_text[ASTRA_BUNDLE_MANIFEST_MAX + 1u];
static uint8_t image[ASTRA_USER_IMAGE_MAX_SIZE];
static uint32_t process_handles[SUPERVISOR_PROCESS_MAX];
/*
 * What each process was launched from, so PROC: can name it. A pid
 * with no name is a number, and `ps` that prints numbers is a worse `ps` than
 * the one nobody wrote.
 */
static char process_paths[SUPERVISOR_PROCESS_MAX]
                         [SUPERVISOR_PROCESS_NAME_MAX];
static uint32_t process_resident[SUPERVISOR_PROCESS_MAX];
static uint32_t service_handles[SUPERVISOR_MANIFEST_ENTRY_MAX];
static char service_names[SUPERVISOR_MANIFEST_ENTRY_MAX]
                         [ASTRA_CAPABILITY_NAME_MAX];
static AstraVfsClient service_clients[SUPERVISOR_MANIFEST_ENTRY_MAX];
static uint32_t process_count;
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
static AstraVfsPortService proc_port;
static uint32_t proc_receive;
static uint32_t proc_send;
static uint32_t launch_send;

static void log_failure(const char *operation, uint32_t status)
{
    char message[64];
    uint32_t length = astra_assert_message(
        message, sizeof(message), operation, status, "failed");

    (void)astra_log_write(message, length);
}

static int equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

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
    return equal(text + length - ending, suffix);
}

static int declared_capability(const AstraBundleManifest *bundle,
                               const SupervisorManifestGrant *wanted)
{
    char name[ASTRA_BUNDLE_NAME_MAX];

    name[0] = '\0';
    if (!append(name, sizeof(name), wanted->name)) return 0;
    if (wanted->is_namespace != 0u) {
        if (!append(name, sizeof(name),
                    wanted->rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE) ?
                        ":rw" : ":r")) return 0;
    }
    for (uint32_t at = 0u; at < bundle->capability_count; ++at)
        if (equal(name, bundle->capabilities[at])) return 1;
    return 0;
}

static uint32_t resolve_entry_image(const SupervisorManifestEntry *entry,
                                    char *path, uint32_t path_capacity,
                                    char *bundle_root,
                                    uint32_t root_capacity,
                                    AstraBundleManifest *out_bundle)
{
    AstraBundleManifest bundle;
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
    status = supervisor_vfs_read(path, bundle_text,
                                 ASTRA_BUNDLE_MANIFEST_MAX, &length);
    if (status != ASTRA_VFS_OK) return ASTRA_STATUS_NOT_FOUND;
    bundle_text[length] = '\0';
    if (astra_bundle_manifest_parse(bundle_text, length, &bundle, &line) !=
            ASTRA_BUNDLE_OK || bundle.kind != ASTRA_BUNDLE_APPLICATION)
        return LOADER_FAIL_MANIFEST;
    for (uint32_t at = 0u; at < entry->grant_count; ++at)
        if (!declared_capability(&bundle, &entry->grants[at]))
            return ASTRA_STATUS_ACCESS;
    path[0] = '\0';
    if (!append(path, path_capacity, entry->path) ||
        !append(path, path_capacity, "/") ||
        !append(path, path_capacity, bundle.executable))
        return ASTRA_STATUS_LIMIT;
    if (entry->path[0] != 'A' || entry->path[1] != 'P' ||
        entry->path[2] != 'P' || entry->path[3] != 'S' ||
        entry->path[4] != ':')
        return ASTRA_STATUS_INVALID;
    tail = entry->path + 5u;
    apps = astra_assign_lookup(supervisor_assigns(), "APPS");
    if (apps == NULL || !append(bundle_root, root_capacity, apps->root) ||
        (bundle_root[0] != '\0' && !append(bundle_root, root_capacity, "/")) ||
        !append(bundle_root, root_capacity, tail))
        return ASTRA_STATUS_LIMIT;
    if (out_bundle != NULL)
        *out_bundle = bundle;
    return ASTRA_STATUS_OK;
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
            if (equal(wanted->name, ASTRA_CAPABILITY_APPLICATION_LAUNCH)) {
                if (launch_send == 0u)
                    continue;
                status = add_grant(out, count, wanted->name, launch_send,
                                   ASTRA_RIGHT_SIGNAL | delegated,
                                   0u, NULL);
                if (status != ASTRA_STATUS_OK)
                    return status;
                continue;
            }
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
                return child_status;
            return status == ASTRA_SYSCALL_TIMED_OUT ? ASTRA_STATUS_BUSY :
                                                       ASTRA_STATUS_PEER_DEAD;
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
static uint32_t launch_text(char *out, uint32_t at, uint32_t capacity,
                            const char *text)
{
    while (*text != '\0' && at + 1u < capacity) out[at++] = *text++;
    out[at] = '\0';
    return at;
}

static uint32_t launch_number(char *out, uint32_t at, uint32_t capacity,
                              uint32_t value)
{
    char digits[12];
    uint32_t count = 0u;

    do { digits[count++] = (char)('0' + value % 10u); value /= 10u; }
    while (value != 0u);
    while (count != 0u && at + 1u < capacity) out[at++] = digits[--count];
    out[at] = '\0';
    return at;
}

static uint32_t launch_micros(uint64_t from, uint64_t to)
{
    return to > from ? (uint32_t)((to - from) / 1000u) : 0u;
}

static void launch_report(const char *path, uint32_t bytes, uint32_t read_us,
                          uint32_t spawn_us, uint32_t ready_us)
{
    char line[120];
    uint32_t at = 0u;

    at = launch_text(line, at, sizeof(line), "launch ");
    at = launch_text(line, at, sizeof(line), path);
    at = launch_text(line, at, sizeof(line), " bytes=");
    at = launch_number(line, at, sizeof(line), bytes);
    at = launch_text(line, at, sizeof(line), " read=");
    at = launch_number(line, at, sizeof(line), read_us);
    at = launch_text(line, at, sizeof(line), " spawn=");
    at = launch_number(line, at, sizeof(line), spawn_us);
    at = launch_text(line, at, sizeof(line), " ready=");
    at = launch_number(line, at, sizeof(line), ready_us);
    (void)launch_text(line, at, sizeof(line), "us");
    (void)astra_log(line);
}

/* Set by whichever call site read the image; consumed by the next launch. */
static uint32_t launch_read_us;

static uint32_t launch_entry(const AstraStartupInfo *startup,
                             const AstraStartupCapability *capabilities,
                             const SupervisorManifestEntry *entry,
                             const char *bundle_root,
                             const AstraLaunchArguments *arguments,
                             const uint8_t *image_bytes,
                             uint32_t image_length,
                             uint32_t *process_id)
{
    AstraLaunchArguments essential_arguments = {0};
    const AstraLaunchArguments *launch_arguments = arguments;
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

    if (entry->resident != 0u) {
        if (arguments != NULL)
            essential_arguments = *arguments;
        essential_arguments.flags |= ASTRA_LAUNCH_FLAG_ESSENTIAL;
        launch_arguments = &essential_arguments;
    }

    if (process_count == SUPERVISOR_PROCESS_MAX)
        return ASTRA_STATUS_LIMIT;
    if (astra_rt_port_create(1u, sizeof(AstraServiceReady), &receive, &send) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    status = build_grants(startup, capabilities, entry, bundle_root, send, grants,
                          &grant_count);
    uint64_t spawn_start = astra_clock_monotonic();
    uint32_t spawn_us;
    uint64_t ready_start;

    if (status == ASTRA_STATUS_OK) {
        uint32_t launch_status = astra_launch(
            image_bytes, image_length, grants, grant_count, launch_arguments,
            &child, &child_id);

        if (launch_status == ASTRA_SYSCALL_OK)
            status = ASTRA_STATUS_OK;
        else {
            log_failure("astra_launch", launch_status);
            status = ASTRA_STATUS_INVALID;
        }
    }
    spawn_us = launch_micros(spawn_start, astra_clock_monotonic());
    (void)astra_close(send);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_close(receive);
        return status;
    }
    ready_start = astra_clock_monotonic();
    status = receive_ready(receive, child, expected_handles, published);
    launch_report(entry->path, image_length, launch_read_us, spawn_us,
                  launch_micros(ready_start, astra_clock_monotonic()));
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
    {
        uint32_t at = 0u;

        while (at < SUPERVISOR_PROCESS_NAME_MAX - 1u &&
               entry->path[at] != '\0') {
            process_paths[process_count][at] = entry->path[at];
            ++at;
        }
        process_paths[process_count][at] = '\0';
        process_resident[process_count] = entry->resident;
        process_handles[process_count++] = child;
    }
    if (process_id != NULL)
        *process_id = child_id;
    return ASTRA_STATUS_OK;
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
        !astra_vfs_service_init(&proc_service, supervisor_proc_ops(), NULL,
                                file_storage, file_capacity))
        return;
    if (astra_rt_port_create(SUPERVISOR_PROC_PORT_MESSAGES,
                             (uint32_t)sizeof(AstraVfsRequestMessage),
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
                         uint32_t status, uint32_t process_id)
{
    AstraApplicationLaunchReply reply = {0};

    reply.header.total_size = sizeof(reply);
    reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply.header.protocol = ASTRA_APPLICATION_PROTOCOL;
    reply.header.protocol_version = ASTRA_APPLICATION_VERSION;
    reply.header.operation = ASTRA_APPLICATION_LAUNCHED;
    reply.header.transaction_id = transaction;
    reply.status = status;
    reply.process_id = status == ASTRA_STATUS_OK ? process_id : 0u;
    (void)astra_port_send(reply_send, &reply, sizeof(reply), NULL, 0u);
    (void)astra_close(reply_send);
}

static void pump_launch(const AstraStartupInfo *startup,
                        const AstraStartupCapability *capabilities)
{
    AstraApplicationLaunchRequest request = {0};
    AstraBundleManifest bundle;
    SupervisorManifestEntry entry = {0};
    uint32_t reply_send = 0u;
    uint32_t size = 0u;
    uint32_t handles = 0u;
    const uint8_t *image_bytes = image;
    uint32_t image_length = 0u;
    uint32_t process_id = 0u;
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
        request.arguments.count > ASTRA_LAUNCH_ARGUMENT_MAX ||
        request.arguments.length == 0u ||
        request.arguments.length > ASTRA_LAUNCH_ARGUMENT_BYTES ||
        request.arguments.flags != 0u ||
        request.arguments.environment_count != 0u ||
        request.arguments.environment_length != 0u ||
        request.arguments.environment_address != 0u ||
        (request.arguments.source != ASTRA_LAUNCH_SOURCE_SHELL &&
         request.arguments.source != ASTRA_LAUNCH_SOURCE_DESKTOP)) {
        if (reply_send != 0u)
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_PROTOCOL, 0u);
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
                             ASTRA_STATUS_PROTOCOL, 0u);
                return;
            }
            ++consumed;
        }
        if (consumed != request.arguments.length) {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_PROTOCOL, 0u);
            return;
        }
    }
    {
        uint32_t path_length = 0u;

        while (request.arguments.bytes[path_length] != '\0')
            ++path_length;
        if (path_length == 0u || path_length >= sizeof(bundle_path)) {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_INVALID, 0u);
            return;
        }
        for (uint32_t at = 0u; at <= path_length; ++at)
            bundle_path[at] = request.arguments.bytes[at];
    }
    for (uint32_t at = 0u; bundle_path[at] != '\0'; ++at)
        if (bundle_path[at] == '/' || bundle_path[at] == '\\') {
            launch_reply(reply_send, request.header.transaction_id,
                         ASTRA_STATUS_INVALID, 0u);
            return;
        }
    if (!ends_with(bundle_path, ".app") ||
        bundle_path[0] != 'A' || bundle_path[1] != 'P' ||
        bundle_path[2] != 'P' || bundle_path[3] != 'S' ||
        bundle_path[4] != ':') {
        launch_reply(reply_send, request.header.transaction_id,
                     ASTRA_STATUS_INVALID, 0u);
        return;
    }
    if (!append(entry.path, sizeof(entry.path), bundle_path)) {
        launch_reply(reply_send, request.header.transaction_id,
                     ASTRA_STATUS_LIMIT, 0u);
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
    if (status == ASTRA_STATUS_OK) {
        {
            uint64_t read_start = astra_clock_monotonic();

            status = supervisor_vfs_read_borrow(entry_path, &image_bytes,
                                                &image_length);
            if (status != ASTRA_VFS_OK) {
                image_bytes = image;
                status = supervisor_vfs_read(entry_path, image, sizeof(image),
                                             &image_length);
            }
            launch_read_us = launch_micros(read_start,
                                           astra_clock_monotonic());
        }
        if (status == ASTRA_VFS_OK) {
            status = launch_entry(startup, capabilities, &entry, bundle_root,
                                  &request.arguments, image_bytes,
                                  image_length, &process_id);
        } else {
            status = status == ASTRA_VFS_ERR_LIMIT ? ASTRA_STATUS_LIMIT :
                                                     ASTRA_STATUS_NOT_FOUND;
        }
    }
    if (status != ASTRA_STATUS_OK)
        log_failure("application launch", status);
    launch_reply(reply_send, request.header.transaction_id, status,
                 process_id);
}

uint32_t supervisor_loader_start(
    const AstraStartupInfo *startup,
    const AstraStartupCapability *capabilities)
{
    SupervisorManifest manifest;
    const uint8_t *boot_bytes = image;
    uint32_t manifest_length = 0u;
    uint32_t image_length = 0u;
    uint32_t status;

    supervisor_process_handle = startup->process_handle;
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
    if (astra_rt_port_create(4u, ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE * 4u,
                             &launch_receive, &launch_send) !=
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

    status = launch_entry(startup, capabilities, &manifest.entries[0], NULL,
                          NULL, image, image_length, NULL);
    if (status != ASTRA_STATUS_OK)
        return status;
    supervisor_bootstrap_block_close();
    for (uint32_t index = 1u; index < manifest.count; ++index) {
        const SupervisorManifestEntry *entry = &manifest.entries[index];
        char entry_path[ASTRA_VFS_PATH_MAX];
        char bundle_root[ASTRA_VFS_PATH_MAX];

        status = resolve_entry_image(entry, entry_path, sizeof(entry_path),
                                     bundle_root, sizeof(bundle_root), NULL);
        if (status == ASTRA_STATUS_OK) {
            {
                uint64_t read_start = astra_clock_monotonic();

                status = supervisor_vfs_read_borrow(entry_path, &boot_bytes,
                                                    &image_length);
                if (status != ASTRA_VFS_OK) {
                    boot_bytes = image;
                    status = supervisor_vfs_read(entry_path, image,
                                                 sizeof(image), &image_length);
                }
                launch_read_us = launch_micros(read_start,
                                               astra_clock_monotonic());
            }
            if (status == ASTRA_VFS_OK)
                status = launch_entry(startup, capabilities, entry,
                                      bundle_root, NULL, boot_bytes,
                                      image_length, NULL);
            else
                status = status == ASTRA_VFS_ERR_LIMIT ? ASTRA_STATUS_LIMIT :
                                                         ASTRA_STATUS_NOT_FOUND;
        }
        if (status != ASTRA_STATUS_OK && entry->required)
            return status;
    }
    proc_tree_start();
    (void)astra_close(event_target_send);
    event_target_send = 0u;
    (void)astra_close(launch_send);
    launch_send = 0u;
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

/*
 * The process table, for the PROC: tree to render. An accessor rather
 * than a shared array because the table is the loader's: it decides what is
 * tracked and it compacts the list when something exits, and a second file
 * indexing into it directly would be a second place that has to know both.
 */
uint32_t supervisor_loader_process_count(void)
{
    return process_count + (supervisor_process_handle != 0u ? 1u : 0u);
}

uint32_t supervisor_loader_process_at(uint32_t index, const char **path)
{
    if (supervisor_process_handle != 0u) {
        if (index == 0u) {
            if (path != NULL)
                *path = "ROM:supervisor";
            return supervisor_process_handle;
        }
        --index;
    }
    if (index >= process_count)
        return 0u;
    if (path != NULL)
        *path = process_paths[index];
    return process_handles[index];
}

uint32_t supervisor_loader_event_control(void)
{
    return event_control_handle;
}

void supervisor_loader_pump_event_control(void)
{
    (void)astra_event_control_pump(event_target_receive, 1u);
}

uint32_t supervisor_loader_watch(
    const AstraStartupInfo *startup,
    const AstraStartupCapability *capabilities)
{
    uint32_t waits[SUPERVISOR_PROCESS_MAX + 3u];

    for (;;) {
        uint32_t index = ASTRA_WAIT_INDEX_NONE;
        uint32_t status;

        waits[0] = event_target_receive;
        waits[1] = launch_receive;
        waits[2] = proc_receive;
        for (uint32_t at = 0u; at < process_count; ++at)
            waits[at + 3u] = process_handles[at];
        status = astra_wait_multiple(waits, process_count + 3u,
                                     ASTRA_DEADLINE_FOREVER, &index, NULL);

        if (index == 0u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            supervisor_loader_pump_event_control();
            continue;
        }
        if (index == 1u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            pump_launch(startup, capabilities);
            continue;
        }
        if (index == 2u) {
            if (status != ASTRA_SYSCALL_OK)
                return ASTRA_STATUS_PEER_DEAD;
            supervisor_loader_pump_proc();
            continue;
        }
        if (index > 2u && index <= process_count + 2u) {
            uint32_t exit_status = 0u;
            uint32_t slot = index - 3u;
            uint32_t wait_status = astra_process_wait(process_handles[slot],
                                                      0u, &exit_status);

            if (wait_status != ASTRA_SYSCALL_TIMED_OUT) {
                if (process_resident[slot] != 0u)
                    log_failure("resident process exited",
                                exit_status != 0u ? exit_status :
                                                    ASTRA_STATUS_PEER_DEAD);
                (void)astra_close(process_handles[slot]);
                --process_count;
                process_handles[slot] = process_handles[process_count];
                process_resident[slot] = process_resident[process_count];
                for (uint32_t at = 0u; at < SUPERVISOR_PROCESS_NAME_MAX; ++at)
                    process_paths[slot][at] = process_paths[process_count][at];
                continue;
            }
        }
        if (status != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
    }
}
