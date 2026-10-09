#include <astra/host.h>
#include <astra/metrics.h>
#include <astra/metrics_vfs.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>

#include <string.h>

ASTRA_PROGRAM("hostfs", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    HOSTFS_FAIL_DEVICE = ASTRA_STATUS_PROGRAM_FIRST,
    HOSTFS_FAIL_LOCK,
    HOSTFS_FAIL_PORT,
    HOSTFS_FAIL_READY
};

static uint32_t device_handle;
static uint32_t work_session;
static uint32_t sound_session;
static _Alignas(4) uint32_t transport_lock;
static _Alignas(4) uint32_t state_lock;
static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;
static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService port;
static AstraVfsPortWorker worker;
static AstraHostMetricsSnapshot host_metrics;
static AstraMetricVfs metrics_backend;
static AstraVfsService metrics_service;
static AstraVfsSessionSlot metrics_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService metrics_port;
static AstraVfsPortWorker metrics_worker;
/* SOUND: is the host directory the Linux audio host reads SoundFonts from,
 * so an update here is what it plays. Its clients reach it through their
 * own lanes, bound to their grant's scope beneath SOUND's root session. */
static AstraVfsHostBackend sound_backend;
static AstraVfsService sound_service;
static AstraVfsSessionSlot sound_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService sound_port;
static volatile uint32_t shutdown_requested;
static _Alignas(16) uint8_t signal_stack[4096u];

/*
 * A grant HostFS answers on its own port (docs/FILESYSTEM_PRODUCTION_PRD.md
 * FS15): its provider session, the scope that binds clients' channels to
 * that session with the grant's rights, and the service whose sessions it
 * binds. The port record comes first, so the port layer's grant pointer is
 * this record.
 */
typedef struct HostfsGrant {
    AstraVfsPortGrant port;        /* receive, id, rights, scope */
    AstraVfsService *service;      /* WORK's or SOUND's */
    uint32_t session;              /* its provider session */
    /* A derived grant belongs to the session that derived it (review I1):
     * it is retired when that session ends -- the deriver said BYE, died,
     * or its own grant retired -- so no holder keeps HostFS full after it
     * has gone. 0 for the two base grants. */
    uint32_t parent;               /* the grant it was derived from */
    uint32_t deriver;              /* kernel-stamped deriving process */
    uint32_t deriver_session;      /* its session on `parent` */
} HostfsGrant;

/*
 * The grant table grows on demand: no count is compiled in or configured
 * (CLAUDE.md, "no ceilings compiled into an image"). The limits that remain
 * are real ones and answer for themselves: the kernel's scope pool
 * (astra_host_scope_create), the wait-set ABI (ASTRA_WAIT_MULTIPLE_MAX) and
 * memory. Each record is allocated on its own so the port layer's grant
 * pointer never moves while a pump is using it.
 */
static HostfsGrant **grants;
static uint32_t grant_count;       /* live records, packed at the front */
static uint32_t grant_capacity;    /* slots in grants, wait_* */
static uint32_t next_grant_id = 1u;
static uint32_t *wait_handles;     /* grant_capacity + 2 */
static uint32_t *wait_owners;      /* what each handle is */
static uint32_t wait_rotation;     /* first grant served this pass (M6) */

/* wait_owners values that are not a grant's index. */
#define WAIT_METRICS  (UINT32_MAX - 1u)
#define WAIT_SHUTDOWN UINT32_MAX

static void hostfs_signal(int number)
{
    if (number == (int)ASTRA_SIGNAL_TERMINATE)
        shutdown_requested = 1u;
}

static uint32_t volume_noop_unmount(void *context)
{
    (void)context;
    return ASTRA_VFS_OK;
}

static uint32_t hostfs_unmount(void *context)
{
    astra_vfs_host_transport_destroy(context);
    return ASTRA_VFS_OK;
}

static AstraVfsPortService *grant_host(const HostfsGrant *grant);

static uint32_t finish_hostfs(void)
{
    const AstraVfsMountOps metrics_mount = {NULL, volume_noop_unmount};
    const AstraVfsMountOps host_mount = {NULL, hostfs_unmount};
    uint32_t status;

    while (astra_vfs_port_service_worker_pump(&metrics_port,
                                               &metrics_worker, 1u) != 0u) {}
    for (uint32_t at = 0u; at < grant_count; ++at)
        while (astra_vfs_port_service_grant_pump(
                   grant_host(grants[at]), &worker, &grants[at]->port,
                   1u) != 0u) {}
    if (metrics_port.stalled != 0u || sound_port.stalled != 0u ||
        port.stalled != 0u)
        return ASTRA_STATUS_IO;
    /* Every channel bound to a grant stops here, before its sessions go. */
    for (uint32_t at = 0u; at < grant_count; ++at) {
        (void)astra_host_scope_revoke(grants[at]->port.scope);
        (void)astra_close(grants[at]->port.scope);
        grants[at]->port.scope = 0u;
    }
    status = astra_vfs_service_shutdown(&metrics_service, &metrics_mount,
                                        NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    /* SOUND shares WORK's transport; WORK's unmount destroys it. */
    status = astra_vfs_service_shutdown(&sound_service, &metrics_mount,
                                        NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    /* Linux owns the host volume and syncs it during host poweroff. Closing
     * every guest session is the required guest-side durability boundary. */
    return astra_vfs_service_shutdown(&service, &host_mount, &transport);
}

typedef struct HostMetricGroup {
    const char *const *names;
    const uint32_t *metrics;
    uint32_t count;
} HostMetricGroup;

typedef struct HostFsMetricGroup {
    uint32_t operation;
} HostFsMetricGroup;

static const char *const block_names[] = {
    "read_requests", "read_sectors", "write_requests", "write_sectors",
    "flush_requests", "durability_transitions"
};
static const uint32_t block_metrics[] = {
    ASTRA_HOST_METRIC_BLOCK_READ_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_READ_SECTORS,
    ASTRA_HOST_METRIC_BLOCK_WRITE_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_WRITE_SECTORS,
    ASTRA_HOST_METRIC_BLOCK_FLUSH_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_DURABILITY_TRANSITIONS
};
static const char *const channel_names[] = {
    "timestamp_ns", "submissions", "commands", "execution_ns", "inflight",
    "max_inflight"
};
static const uint32_t channel_metrics[] = {
    ASTRA_HOST_METRIC_TIMESTAMP_NS, ASTRA_HOST_METRIC_HOST_SUBMISSIONS,
    ASTRA_HOST_METRIC_HOST_COMMANDS, ASTRA_HOST_METRIC_HOST_EXECUTION_NS,
    ASTRA_HOST_METRIC_HOST_INFLIGHT, ASTRA_HOST_METRIC_HOST_MAX_INFLIGHT
};
static const char *const fs_group_names[] = {
    "host.fs.invalid", "host.fs.open", "host.fs.close", "host.fs.read",
    "host.fs.write", "host.fs.sync", "host.fs.truncate", "host.fs.stat",
    "host.fs.readdir", "host.fs.mkdir", "host.fs.unlink", "host.fs.rename",
    "host.fs.chmod", "host.fs.readlink", "host.fs.symlink", "host.fs.link",
    "host.fs.filesystem_info", "host.fs.session_open",
    "host.fs.session_close", "host.fs.claim"
};
_Static_assert(sizeof(fs_group_names) / sizeof(fs_group_names[0]) ==
                   ASTRA_HOST_FS_MAX + 1u,
               "host filesystem metric names must cover every operation");
static HostFsMetricGroup fs_groups[ASTRA_HOST_FS_MAX + 1u];
static const HostMetricGroup block_group = {
    block_names, block_metrics, sizeof(block_metrics) / sizeof(block_metrics[0])
};
static const HostMetricGroup channel_group = {
    channel_names, channel_metrics,
    sizeof(channel_metrics) / sizeof(channel_metrics[0])
};

static uint32_t sample_host_group(void *context, AstraMetricSample *out,
                                  uint32_t capacity)
{
    const HostMetricGroup *group = context;

    if (out == NULL || capacity < group->count)
        return group->count;
    for (uint32_t index = 0u; index < group->count; ++index) {
        out[index].name = group->names[index];
        out[index].value = astra_host_metric_value(
            &host_metrics, group->metrics[index]);
    }
    return group->count;
}

static uint32_t sample_host_fs(void *context, AstraMetricSample *out,
                               uint32_t capacity)
{
    const HostFsMetricGroup *group = context;

    if (out == NULL || capacity < 2u)
        return 2u;
    out[0].name = "calls";
    out[0].value = astra_host_metric_value(
        &host_metrics, ASTRA_HOST_METRIC_FS_COUNT_BASE + group->operation);
    out[1].name = "execution_ns";
    out[1].value = astra_host_metric_value(
        &host_metrics,
        ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE + group->operation);
    return 2u;
}

static uint32_t sample_vfs(void *context, AstraMetricSample *out,
                           uint32_t capacity)
{
    static const char *const names[] = {
        "requests", "replies_failed", "protocol_rejects", "sessions_opened",
        "sessions_closed", "files_opened", "files_closed", "stale_handles",
        "cross_session_denied", "cross_owner_denied",
        "owner_session_quota_denied", "owner_quota_denied",
        "peak_open_files", "peak_sessions", "last_failed_operation",
        "last_failed_status", "last_failed_owner"
    };
    const AstraVfsServiceStats *stats = astra_vfs_service_stats(context);
    uint32_t values[sizeof(names) / sizeof(names[0])];
    uint32_t count = sizeof(names) / sizeof(names[0]);

    _Static_assert(sizeof(AstraVfsServiceStats) ==
                       sizeof(names) / sizeof(names[0]) * sizeof(uint32_t),
                   "VFS metric names no longer match the stats record");
    if (stats == NULL)
        return 0u;
    if (out == NULL || capacity < count)
        return count;
    values[0] = stats->requests;
    values[1] = stats->replies_failed;
    values[2] = stats->protocol_rejects;
    values[3] = stats->sessions_opened;
    values[4] = stats->sessions_closed;
    values[5] = stats->files_opened;
    values[6] = stats->files_closed;
    values[7] = stats->stale_handles;
    values[8] = stats->cross_session_denied;
    values[9] = stats->cross_owner_denied;
    values[10] = stats->owner_session_quota_denied;
    values[11] = stats->owner_quota_denied;
    values[12] = stats->peak_open_files;
    values[13] = stats->peak_sessions;
    values[14] = stats->last_failed_operation;
    values[15] = stats->last_failed_status;
    values[16] = stats->last_failed_owner;
    for (uint32_t index = 0u; index < count; ++index) {
        out[index].name = names[index];
        out[index].value = values[index];
    }
    return count;
}

static uint32_t refresh_host_metrics(void *context)
{
    uint32_t status = astra_vfs_host_metrics(context, &host_metrics);

    return status == ASTRA_VFS_OK ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

static int register_metrics(void)
{
    astra_metric_reset_registry();
    if (astra_metric_register("host.block", sample_host_group,
                              (void *)&block_group) != ASTRA_METRIC_OK ||
        astra_metric_register("host.channel", sample_host_group,
                              (void *)&channel_group) != ASTRA_METRIC_OK ||
        astra_metric_register("hostfs.vfs", sample_vfs, &service) !=
            ASTRA_METRIC_OK)
        return 0;
    for (uint32_t operation = 0u;
         operation <= ASTRA_HOST_FS_MAX; ++operation) {
        fs_groups[operation].operation = operation;
        if (astra_metric_register(fs_group_names[operation], sample_host_fs,
                                  &fs_groups[operation]) != ASTRA_METRIC_OK)
            return 0;
    }
    return astra_metric_vfs_init(&metrics_backend, refresh_host_metrics,
                                 &transport);
}

static uint32_t grow_grants(void)
{
    uint32_t capacity = grant_capacity == 0u ? 4u : grant_capacity * 2u;
    HostfsGrant **table;
    uint32_t *handles;
    uint32_t *owners;

    /* The wait set holds every grant, the shutdown and metrics ports. */
    if (capacity + 2u > ASTRA_WAIT_MULTIPLE_MAX)
        capacity = ASTRA_WAIT_MULTIPLE_MAX - 2u;
    if (capacity <= grant_capacity)
        return ASTRA_VFS_ERR_LIMIT;
    table = astra_runtime_reallocate(grants, capacity * sizeof(*grants));
    if (table == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    grants = table;
    handles = astra_runtime_reallocate(wait_handles,
                                       (capacity + 2u) * sizeof(*handles));
    if (handles == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    wait_handles = handles;
    owners = astra_runtime_reallocate(wait_owners,
                                      (capacity + 2u) * sizeof(*owners));
    if (owners == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    wait_owners = owners;
    grant_capacity = capacity;
    return ASTRA_VFS_OK;
}

static uint32_t scope_status(uint32_t status)
{
    return status == ASTRA_SYSCALL_RESOURCE_LIMIT ||
           status == ASTRA_SYSCALL_OUT_OF_MEMORY ? ASTRA_VFS_ERR_LIMIT :
           status == ASTRA_SYSCALL_PEER_DEAD ? ASTRA_VFS_ERR_PEER :
                                               ASTRA_VFS_ERR_IO;
}

/* Adds a grant over provider `session` with `rights`; answers its send
 * handle. Its scope carries exactly those rights: read-only without WRITE,
 * delegable only with DELEGATE. */
static uint32_t grant_add(AstraVfsService *owner, uint32_t session,
                          uint32_t rights, uint32_t *send,
                          HostfsGrant **added)
{
    HostfsGrant *grant;
    uint32_t receive = 0u;
    uint32_t scope = 0u;
    uint32_t status;
    uint32_t flags =
        ((rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
             ASTRA_HOST_SCOPE_READ_ONLY : 0u) |
        ((rights & ASTRA_VFS_GRANT_DELEGATE) != 0u ?
             ASTRA_HOST_SCOPE_DELEGATE : 0u);

    *send = 0u;
    if (grant_count == grant_capacity &&
        (status = grow_grants()) != ASTRA_VFS_OK)
        return status;
    grant = astra_runtime_callocate(1u, sizeof(*grant));
    if (grant == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_host_scope_create(device_handle, session, flags, &scope);
    if (status != ASTRA_SYSCALL_OK) {
        astra_runtime_deallocate(grant);
        return scope_status(status);
    }
    status = astra_rt_port_create(
        ASTRA_PORT_MESSAGES_MAX,
        ASTRA_PORT_MESSAGES_MAX *
            (uint32_t)sizeof(AstraVfsRenameRequestMessage),
        &receive, send);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_host_scope_revoke(scope);
        (void)astra_close(scope);
        astra_runtime_deallocate(grant);
        *send = 0u;
        return scope_status(status);
    }
    grant->service = owner;
    grant->session = session;
    grant->port.receive = receive;
    grant->port.id = next_grant_id++;
    grant->port.rights = rights;
    grant->port.scope = scope;
    grants[grant_count++] = grant;
    if (added != NULL)
        *added = grant;
    return ASTRA_VFS_OK;
}

/* GRANT_DERIVE (storage protocol 29): a provider session beneath the
 * parent's, read-only when the grant lacks WRITE, and a grant over it,
 * owned by the session that derived it. The port layer has already checked
 * that the rights only narrow. */
static uint32_t hostfs_derive(void *context,
                              const AstraVfsPortDeriveRequest *request,
                              uint32_t *send)
{
    const HostfsGrant *from = (const HostfsGrant *)(const void *)
        request->parent;
    char provider_root[ASTRA_VFS_PATH_MAX + 1u];
    HostfsGrant *grant = NULL;
    uint32_t session = 0u;
    uint32_t status;

    (void)context;
    *send = 0u;
    /* A root is a relative path beneath the parent's root; the provider
     * resolves it beneath that session and refuses anything outside. */
    if (request->root[0] == '/')
        return ASTRA_VFS_ERR_INVALID;
    provider_root[0] = '/';
    (void)strcpy(provider_root + 1, request->root);
    status = astra_vfs_host_session_open(
        &transport, transport.generation, from->session, 0u, provider_root,
        (request->rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u, &session);
    if (status != ASTRA_VFS_OK)
        return status;
    status = grant_add(from->service, session, request->rights, send,
                       &grant);
    if (status != ASTRA_VFS_OK) {
        (void)astra_vfs_host_session_close(&transport, transport.generation,
                                           session);
        return status;
    }
    grant->parent = from->port.id;
    grant->deriver = request->deriver;
    grant->deriver_session = request->session;
    return ASTRA_VFS_OK;
}

static AstraVfsPortService *grant_host(const HostfsGrant *grant)
{
    /* A port host's reply lanes are keyed by session, and sessions are
     * unique per service, so SOUND's grants use SOUND's host. */
    return grant->service == &service ? &port : &sound_port;
}

/* The grant's last holder closed its handle, or its deriver's session
 * ended. Revoke its scope -- every channel bound to it stops at once --
 * then end its sessions with their reply lanes, and its provider session,
 * whose handles go with it (Task 4). The way Haiku's disconnect_fd
 * (src/system/kernel/fs/fd.cpp) cuts every descriptor of a vnode at once
 * rather than waiting for each holder. */
static void grant_retire(uint32_t at)
{
    HostfsGrant *grant = grants[at];
    uint32_t status = astra_host_scope_revoke(grant->port.scope);

    if (status != ASTRA_SYSCALL_OK)
        (void)astra_log_failure("hostfs grant revoke", status);
    (void)astra_close(grant->port.scope);
    astra_vfs_port_service_release_grant(grant_host(grant), grant->port.id);
    (void)astra_vfs_host_session_close(&transport, transport.generation,
                                       grant->session);
    (void)astra_close(grant->port.receive);
    astra_runtime_deallocate(grant);
    grants[at] = grants[--grant_count];
    grants[grant_count] = NULL;
}

static int grant_live(uint32_t id)
{
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (grants[at]->port.id == id)
            return 1;
    return 0;
}

/* Retires every derived grant whose deriving session has ended, until none
 * is left: a grant derived from a retired one goes too. Dead clients'
 * sessions are reaped first, so "the deriver exited" counts. */
static void retire_orphans(void)
{
    int retired = 1;

    astra_vfs_port_service_reap(&port);
    astra_vfs_port_service_reap(&sound_port);
    while (retired) {
        retired = 0;
        for (uint32_t at = 0u; at < grant_count; ++at) {
            const HostfsGrant *grant = grants[at];

            if (grant->parent == 0u ||
                (grant_live(grant->parent) &&
                 astra_vfs_service_session_bound(
                     grant->service, grant->deriver_session, grant->deriver,
                     grant->parent)))
                continue;
            grant_retire(at);
            retired = 1;
            break;
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    void *service_storage = NULL;
    void *metrics_storage = NULL;
    void *sound_storage = NULL;
    uint32_t service_capacity = 0u;
    uint32_t metrics_capacity = 0u;
    uint32_t sound_capacity = 0u;
    uint32_t work_send = 0u;
    uint32_t metrics_receive = 0u;
    uint32_t metrics_send = 0u;
    uint32_t sound_send = 0u;
    uint32_t shutdown_receive = 0u;
    uint32_t shutdown_send = 0u;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_FILESYSTEM);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    device_handle = device->handle;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_host_transport_init(
            &transport, device_handle, astra_vfs_state_lock_acquire,
            astra_vfs_state_lock_release, &transport_lock))
        status = HOSTFS_FAIL_DEVICE;
    /* HostFS is the one claimed filesystem owner: no other HOST_DEVICE
     * holder drives files, and only a session it opened can be bound to a
     * scope it created. One root session per volume. */
    if (status == ASTRA_STATUS_OK &&
        (astra_vfs_host_claim(&transport, transport.generation, 0) !=
             ASTRA_VFS_OK ||
         astra_vfs_host_session_open(&transport, transport.generation, 0u,
                                     ASTRA_HOST_FS_VOLUME_WORK, "/", 0u,
                                     &work_session) != ASTRA_VFS_OK ||
         astra_vfs_host_session_open(&transport, transport.generation, 0u,
                                     ASTRA_HOST_FS_VOLUME_SOUND, "/", 0u,
                                     &sound_session) != ASTRA_VFS_OK))
        status = HOSTFS_FAIL_DEVICE;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &service_storage,
                                       &service_capacity) ||
         !astra_vfs_host_init(&backend, &transport, transport.generation,
                              work_session) ||
         !astra_vfs_service_init(
             &service, astra_vfs_host_ops(), &backend, service_sessions,
             ASTRA_VFS_SESSION_MAX, service_storage, service_capacity) ||
         !astra_vfs_service_set_state_lock(
             &service, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_service_set_state_wait(
             &service, astra_vfs_state_futex_wait,
             astra_vfs_state_futex_wake)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &sound_storage, &sound_capacity) ||
         !astra_vfs_host_init(&sound_backend, &transport,
                              transport.generation, sound_session) ||
         !astra_vfs_service_init(
             &sound_service, astra_vfs_host_ops(), &sound_backend,
             sound_sessions, ASTRA_VFS_SESSION_MAX, sound_storage,
             sound_capacity) ||
         !astra_vfs_service_set_state_lock(
             &sound_service, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_service_set_state_wait(
             &sound_service, astra_vfs_state_futex_wait,
             astra_vfs_state_futex_wake)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (!register_metrics() ||
         !astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &metrics_storage,
                                       &metrics_capacity) ||
         !astra_vfs_service_init(
             &metrics_service, astra_metric_vfs_ops(), &metrics_backend,
             metrics_sessions, ASTRA_VFS_SESSION_MAX, metrics_storage,
             metrics_capacity)))
        status = ASTRA_STATUS_LIMIT;
    /* The base grants, one per volume. They are delegable: the supervisor
     * narrows per child (supervisor/src/loader.c narrowed_grant). */
    if (status == ASTRA_STATUS_OK) {
        const uint32_t base = ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE |
                              ASTRA_VFS_GRANT_DELEGATE;

        if (grant_add(&service, work_session, base, &work_send, NULL) !=
                ASTRA_VFS_OK ||
            grant_add(&sound_service, sound_session, base, &sound_send,
                      NULL) != ASTRA_VFS_OK)
            status = ASTRA_STATUS_LIMIT;
    }
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_service_init(&port, grants[0]->port.receive,
                                      &service) ||
         !astra_vfs_port_service_set_state_lock(
             &port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_port_service_set_derive(&port, hostfs_derive, NULL)))
        status = HOSTFS_FAIL_PORT;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(
             ASTRA_PORT_MESSAGES_MAX,
             ASTRA_PORT_MESSAGES_MAX *
                 (uint32_t)sizeof(AstraVfsRenameRequestMessage),
             &metrics_receive, &metrics_send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&metrics_port, metrics_receive,
                                      &metrics_service)))
        status = HOSTFS_FAIL_PORT;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_service_init(&sound_port, grants[1]->port.receive,
                                      &sound_service) ||
         !astra_vfs_port_service_set_state_lock(
             &sound_port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_port_service_set_derive(&sound_port, hostfs_derive,
                                            NULL)))
        status = HOSTFS_FAIL_PORT;
    if (status == ASTRA_STATUS_OK &&
        astra_rt_signal_configure(
            hostfs_signal, signal_stack + sizeof(signal_stack),
            0u, NULL, NULL) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(1u, sizeof(AstraShutdownRequest),
                             &shutdown_receive, &shutdown_send) !=
            ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    {
        uint32_t published[] = {work_send, metrics_send, sound_send};
        uint32_t ready = astra_service_ready_managed(bootstrap->handle, status,
                                             published, 3u, shutdown_send);
        if (ready != ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
            status = HOSTFS_FAIL_READY;
    }
    if (shutdown_send != 0u)
        (void)astra_close(shutdown_send);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    /* The pre-27 single pump, with the wait set built from the grant
     * table each time round: HostFS serves only the control plane. */
    for (;;) {
        uint32_t count = 0u;
        uint32_t selected = ASTRA_WAIT_INDEX_NONE;

        wait_handles[count] = shutdown_receive;
        wait_owners[count++] = WAIT_SHUTDOWN;
        wait_handles[count] = metrics_port.receive;
        wait_owners[count++] = WAIT_METRICS;
        /* Grants in rotation: the first grant served moves on each pass,
         * so one busy port cannot starve the others' control plane (M6). */
        if (grant_count != 0u)
            wait_rotation %= grant_count;
        for (uint32_t step = 0u; step < grant_count; ++step) {
            uint32_t at = (wait_rotation + step) % grant_count;

            wait_handles[count] = grants[at]->port.receive;
            wait_owners[count++] = at;
        }
        ++wait_rotation;
        status = astra_wait_multiple(wait_handles, count,
                                     ASTRA_DEADLINE_FOREVER, &selected,
                                     NULL);
        if (status == ASTRA_SYSCALL_OK && selected == 0u) {
            AstraShutdownRequest request = {0};
            uint32_t reply = 0u;
            uint32_t result = astra_shutdown_receive(
                shutdown_receive, &request, &reply);

            if (result != ASTRA_SYSCALL_OK)
                return (int)ASTRA_STATUS_PROTOCOL;
            result = finish_hostfs();
            (void)astra_shutdown_respond(
                reply, request.header.transaction_id,
                result == ASTRA_VFS_OK ? ASTRA_SHUTDOWN_READY :
                                         ASTRA_SHUTDOWN_CANCEL,
                result);
            return (int)result;
        }
        if (shutdown_requested != 0u)
            return (int)finish_hostfs();
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (selected >= count || wait_owners[selected] == WAIT_SHUTDOWN)
            return (int)status;
        if (wait_owners[selected] == WAIT_METRICS) {
            if (status != ASTRA_SYSCALL_OK)
                return (int)status;
            (void)astra_vfs_port_service_worker_pump(&metrics_port,
                                                     &metrics_worker, 1u);
            continue;
        }
        /* A grant's receive port with no sender left and nothing queued:
         * its last holder has gone (sw/kernel/port.c). */
        if (status == ASTRA_SYSCALL_PEER_DEAD) {
            grant_retire(wait_owners[selected]);
            retire_orphans();
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
        (void)astra_vfs_port_service_grant_pump(
            grant_host(grants[wait_owners[selected]]), &worker,
            &grants[wait_owners[selected]]->port, 1u);
        retire_orphans();
    }
}
