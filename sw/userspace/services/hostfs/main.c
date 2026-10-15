#include <astra/host.h>
#include <astra/config_document.h>
#include <astra/metrics.h>
#include <astra/metrics_vfs.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service_core.h>
#include <astra/volume_port.h>
#include <astra/string.h>

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
static uint32_t provider_epoch;
static uint32_t owner_files;
static uint32_t owner_sessions;
static uint32_t queue_messages;

static uint32_t configured_limits(AstraVfsHostBackend *root)
{
    const char *path = "/config/services/hostfs/settings.conf";
    AstraConfigDocumentError error = {0};
    AstraVfsBackend source = {astra_vfs_host_ops(), root};
    AstraVfsNodeInfo info = {0};
    const char *const names[] = {"owner_files", "owner_sessions", "queue_messages"};
    uint32_t values[3] = {0};
    uint32_t moved = 0u;
    uint64_t size = 0u;
    char *text;
    uint32_t status = source.ops->stat(root, path, &info);

    if (status != ASTRA_VFS_OK)
        return status;
    if (info.kind != ASTRA_VFS_KIND_FILE || info.size == 0u || info.size >= UINT32_MAX)
        return ASTRA_STATUS_INVALID;
    text = astra_runtime_allocate((size_t)info.size + 1u);
    if (text == NULL)
        return ASTRA_STATUS_LIMIT;
    status = astra_vfs_backend_read_path(&source, path, text, (uint32_t)info.size, &moved, &size);
    if (status == ASTRA_VFS_OK && (size != info.size || moved != size))
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_VFS_OK)
        status = astra_config_document_validate(text, moved, 1u, &error) == ASTRA_CONFIG_OK ?
            ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < 3u && status == ASTRA_STATUS_OK; ++index) {
        uint64_t value = 0u;
        uint32_t count = 0u;
        if (astra_config_document_count(text, moved, names[index], &count) != ASTRA_CONFIG_OK ||
            count != 1u ||
            astra_config_document_get_u64(text, moved, names[index], 0u, &value) != ASTRA_CONFIG_OK ||
            value == 0u || value > (index == 2u ? ASTRA_PORT_MESSAGES_MAX : UINT32_MAX))
            status = ASTRA_STATUS_INVALID;
        else
            values[index] = (uint32_t)value;
    }
    astra_runtime_deallocate(text);
    if (status == ASTRA_STATUS_OK) {
        owner_files = values[0];
        owner_sessions = values[1];
        queue_messages = values[2];
    }
    return status;
}
static _Alignas(4) uint32_t transport_lock;
static _Alignas(4) uint32_t state_lock;
static AstraVfsHostTransport transport;
typedef struct HostfsVolume {
    struct HostfsVolume *next;
    AstraVolumeReference reference;
    uint16_t root_flags;
    uint8_t dynamic;
    uint8_t metric_registered;
    uint8_t service_ready;
    uint32_t pending_session;
    char metric_group[ASTRA_METRIC_NAME_MAX];
    AstraVfsHostBackend backend;
    AstraVfsService service;
    AstraVfsSessionSlot sessions[ASTRA_VFS_SESSION_MAX];
    AstraVfsPortService port;
    AstraArea storage;
} HostfsVolume;

static HostfsVolume *volumes;
static AstraVfsPortWorker worker;
static AstraHostMetricsSnapshot host_metrics;
static AstraMetricVfs metrics_backend;
static AstraVfsService metrics_service;
static AstraVfsSessionSlot metrics_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService metrics_port;
static AstraVfsPortWorker metrics_worker;
static AstraVolumePort volume_port;
static uint32_t metrics_registered;
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
    HostfsVolume *owner;          /* canonical owner of this mounted volume */
    uint32_t session;              /* its provider session */
    /* A derived grant belongs to the session that derived it (review I1):
     * it is retired when that session ends -- the deriver said BYE, died,
     * or its own grant retired -- so no holder keeps HostFS full after it
     * has gone. 0 for base volume grants. */
    uint32_t parent;               /* the grant it was derived from */
    uint32_t deriver;              /* kernel-stamped deriving process */
    uint32_t deriver_session;      /* its session on `parent` */
    uint32_t requester;            /* kernel-stamped creation/recovery provenance */
    uint32_t retiring;             /* charged until actual scope/session/port retirement */
    uint32_t scope_revoked;
    AstraVolumeReference volume;
    char root[ASTRA_HOST_FS_PATH_MAX]; /* recorded volume-relative recovery path */
    AstraVfsNodeMeta root_meta;
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
static uint32_t *wait_handles;     /* grant_capacity + 9 */
static uint32_t *wait_owners;      /* what each handle is */
static uint32_t wait_rotation;     /* first grant served this pass (M6) */
static uint32_t usage_holders_changed;

/* Existing service slots own holder identity. This notification only invalidates
 * the canonical QEMU usage sequence before another page can be published. */
static void hostfs_sessions_changed(void *context)
{
    (void)context;
    usage_holders_changed = 1u;
}

/* wait_owners values that are not a grant's index. */
#define WAIT_METRICS  (UINT32_MAX - 1u)
#define WAIT_VOLUMES  (UINT32_MAX - 2u)
#define WAIT_VOLUME_READ (UINT32_MAX - 3u)
#define WAIT_VOLUME_WRITE (UINT32_MAX - 4u)
#define WAIT_VOLUME_ADMIN (UINT32_MAX - 5u)
#define WAIT_VOLUME_USAGE (UINT32_MAX - 6u)
#define WAIT_RAM_READ (UINT32_MAX - 7u)
#define WAIT_RAM_WRITE (UINT32_MAX - 8u)
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

static AstraVfsPortService *grant_host(const HostfsGrant *grant);
static int grant_retire(uint32_t at);

static uint32_t finish_hostfs(void)
{
    const AstraVfsMountOps metrics_mount = {NULL, volume_noop_unmount};
    uint32_t status;

    astra_volume_port_destroy(&volume_port);
    while (astra_vfs_port_service_worker_pump(&metrics_port,
                                               &metrics_worker, 1u) != 0u) {}
    for (uint32_t at = 0u; at < grant_count; ++at)
        while (!grants[at]->retiring && astra_vfs_port_service_grant_pump(
                   grant_host(grants[at]), &worker, &grants[at]->port,
                   1u) != 0u) {}
    if (metrics_port.stalled != 0u)
        return ASTRA_STATUS_IO;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        if (volume->port.stalled != 0u)
            return ASTRA_STATUS_IO;
    /* Normal retirement is also the shutdown owner; failed cleanup remains
       charged until retry or canonical process/provider teardown. */
    while (grant_count != 0u)
        if (!grant_retire(0u)) return ASTRA_VFS_ERR_BUSY;
    status = astra_vfs_service_shutdown(&metrics_service, &metrics_mount,
                                        NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        status = astra_vfs_service_shutdown(&volume->service, &metrics_mount, NULL);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        if (volume->backend.session == 0u) continue;
        status = astra_vfs_host_session_close(&transport, transport.generation, volume->backend.session);
        if (status != ASTRA_VFS_OK) return status;
    }
    status = astra_vfs_host_claim(&transport, transport.generation, 1, NULL);
    astra_vfs_host_transport_destroy(&transport);
    return status;
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
    "host.fs.session_close", "host.fs.claim", "host.fs.seek",
    "host.fs.share", "host.fs.adopt", "host.fs.file_flags", "host.fs.path"
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

static uint32_t sample_vfs_stats(const AstraVfsServiceStats *stats, AstraMetricSample *out,
                                  uint32_t capacity, uint32_t count)
{
    static const char *const names[] = {
        "requests", "replies_failed", "protocol_rejects", "sessions_opened",
        "sessions_closed", "files_opened", "files_closed", "stale_handles",
        "cross_session_denied", "cross_owner_denied",
        "owner_session_quota_denied", "owner_quota_denied",
        "peak_open_files", "peak_sessions", "last_failed_operation",
        "last_failed_status", "last_failed_owner"
    };
    uint32_t values[sizeof(names) / sizeof(names[0])];

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

static uint32_t sample_vfs(void *context, AstraMetricSample *out, uint32_t capacity)
{
    HostfsVolume *volume = context;
    return sample_vfs_stats(astra_vfs_service_stats(&volume->service), out, capacity, 17u);
}

static uint32_t sample_vfs_totals(void *context, AstraMetricSample *out, uint32_t capacity)
{
    AstraVfsServiceStats combined = {0};
    (void)context;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        const AstraVfsServiceStats *part = astra_vfs_service_stats(&volume->service);
        combined.requests += part->requests;
        combined.replies_failed += part->replies_failed;
        combined.protocol_rejects += part->protocol_rejects;
        combined.sessions_opened += part->sessions_opened;
        combined.sessions_closed += part->sessions_closed;
        combined.files_opened += part->files_opened;
        combined.files_closed += part->files_closed;
        combined.stale_handles += part->stale_handles;
        combined.cross_session_denied += part->cross_session_denied;
        combined.cross_owner_denied += part->cross_owner_denied;
        combined.owner_session_quota_denied += part->owner_session_quota_denied;
        combined.owner_quota_denied += part->owner_quota_denied;
    }
    /* Peaks and last failures belong to one service. Adding independently
     * timed peaks or picking a linked-list failure is not an aggregate fact. */
    return sample_vfs_stats(&combined, out, capacity, 12u);
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
        astra_metric_register("hostfs.vfs", sample_vfs_totals, NULL) !=
            ASTRA_METRIC_OK)
        return 0;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        if (astra_metric_register(volume->metric_group, sample_vfs, volume) != ASTRA_METRIC_OK)
            return 0;
        else volume->metric_registered = 1u;
    for (uint32_t operation = 0u;
         operation <= ASTRA_HOST_FS_MAX; ++operation) {
        fs_groups[operation].operation = operation;
        if (astra_metric_register(fs_group_names[operation], sample_host_fs,
                                  &fs_groups[operation]) != ASTRA_METRIC_OK)
            return 0;
    }
    metrics_registered = astra_metric_vfs_init(&metrics_backend, refresh_host_metrics, &transport);
    return metrics_registered != 0u;
}

static uint32_t grow_grants(void)
{
    uint32_t capacity = grant_capacity == 0u ? 4u : grant_capacity * 2u;
    HostfsGrant **table;
    uint32_t *handles;
    uint32_t *owners;

    /* Grants and the nine canonical control endpoints. */
    if (capacity + 9u > ASTRA_WAIT_MULTIPLE_MAX)
        capacity = ASTRA_WAIT_MULTIPLE_MAX - 9u;
    if (capacity <= grant_capacity)
        return ASTRA_VFS_ERR_LIMIT;
    table = astra_runtime_reallocate(grants, capacity * sizeof(*grants));
    if (table == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    grants = table;
    handles = astra_runtime_reallocate(wait_handles,
                                       (capacity + 9u) * sizeof(*handles));
    if (handles == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    wait_handles = handles;
    owners = astra_runtime_reallocate(wait_owners,
                                      (capacity + 9u) * sizeof(*owners));
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

static uint32_t grant_scope_flags(uint32_t rights)
{
    return ((rights & ASTRA_VFS_GRANT_WRITE) == 0u ? ASTRA_HOST_SCOPE_READ_ONLY : 0u) |
           ((rights & ASTRA_VFS_GRANT_DELEGATE) != 0u ? ASTRA_HOST_SCOPE_DELEGATE : 0u);
}

static uint32_t close_owned_session(uint32_t *session)
{
    if (*session == 0u) return ASTRA_VFS_OK;
    uint32_t status = astra_vfs_host_session_close(&transport, transport.generation, *session);
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_LOST || status == ASTRA_VFS_ERR_BAD_HANDLE) {
        *session = 0u;
        return ASTRA_VFS_OK;
    }
    return status;
}

static uint32_t grant_root_meta(uint32_t session, AstraVfsNodeMeta *meta)
{
    AstraVfsHostBackend root;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (!astra_vfs_host_init(&root, &transport, transport.generation, session))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_host_ops()->stat(&root, "/", &info);
    if (status == ASTRA_VFS_OK)
        *meta = info.meta;
    return status;
}

/* Adds a grant over provider `session` with `rights`; answers its send
 * handle. Its scope carries exactly those rights: read-only without WRITE,
 * delegable only with DELEGATE. */
static uint32_t grant_add(HostfsVolume *owner, uint32_t session,
                          uint32_t rights, const AstraVolumeReference *volume, const char *root, uint32_t requester, uint32_t *send,
                          HostfsGrant **added)
{
    HostfsGrant *grant;
    uint32_t status;
    uint32_t flags = grant_scope_flags(rights);

    *send = 0u;
    if (added != NULL) *added = NULL;
    if (next_grant_id == UINT32_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    if (root == NULL || root[0] != '/' || strlen(root) >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (grant_count == grant_capacity &&
        (status = grow_grants()) != ASTRA_VFS_OK)
        return status;
    grant = astra_runtime_callocate(1u, sizeof(*grant));
    if (grant == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    /* Adopt before creating any further resource. A partial construction uses
     * the same charged grant and retirement path as a published grant. */
    grant->owner = owner;
    grant->requester = requester;
    grant->session = session;
    grant->volume = *volume;
    strcpy(grant->root, root);
    grant->port.id = next_grant_id++;
    grant->port.rights = rights;
    uint32_t at = grant_count;
    grants[grant_count++] = grant;
    owner->pending_session = 0u;
    status = grant_root_meta(session, &grant->root_meta);
    if (status != ASTRA_VFS_OK) {
        (void)grant_retire(at);
        return status;
    }
    status = astra_host_scope_create(device_handle, session, flags, &grant->port.scope);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_host_scope_limit(grant->port.scope, owner_files);
    if (status != ASTRA_SYSCALL_OK) {
        (void)grant_retire(at);
        return scope_status(status);
    }
    status = astra_rt_port_create(
        queue_messages,
        queue_messages *
            (uint32_t)sizeof(AstraVfsRequestMessage),
        &grant->port.receive, send);
    if (status != ASTRA_SYSCALL_OK) {
        (void)grant_retire(at);
        if (*send != 0u) (void)astra_close(*send);
        *send = 0u;
        return scope_status(status);
    }
    grant->port.features = ASTRA_VFS_FEATURE_RESOLVES | ASTRA_VFS_FEATURE_NODE_META |
                           ASTRA_VFS_FEATURE_DESCRIPTIONS | ASTRA_VFS_FEATURE_DIRECTORY_PATH;
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
    char *paths;
    char *provider_root;
    char *recovery_root;
    HostfsGrant *grant = NULL;
    uint32_t session = 0u;
    uint32_t status;

    (void)context;
    *send = 0u;
    if (from->owner->pending_session != 0u) return ASTRA_VFS_ERR_BUSY;
    /* A root is a relative path beneath the parent's root; the provider
     * resolves it beneath that session and refuses anything outside. */
    if (request->root[0] == '/')
        return ASTRA_VFS_ERR_INVALID;
    uint32_t prefix = (uint32_t)strlen(from->root);
    uint32_t separator = from->root[prefix - 1u] == '/' ? 0u : 1u;
    uint32_t length = (uint32_t)strlen(request->root);
    if (prefix + separator + length >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_NAME_TOO_LONG;
    paths = astra_runtime_allocate(2u * ASTRA_HOST_FS_PATH_MAX);
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    provider_root = paths; recovery_root = paths + ASTRA_HOST_FS_PATH_MAX;
    memcpy(recovery_root, from->root, prefix);
    if (separator != 0u)
        recovery_root[prefix++] = '/';
    memcpy(recovery_root + prefix, request->root, length + 1u);
    provider_root[0] = '/';
    (void)strcpy(provider_root + 1, request->root);
    status = astra_vfs_host_session_open(
        &transport, transport.generation, from->session, NULL, provider_root,
        (request->rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u, request->deriver, &session);
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(paths);
        return status;
    }
    from->owner->pending_session = session;
    status = grant_add(from->owner, session, request->rights, &from->volume, recovery_root, request->deriver, send,
                       &grant);
    astra_runtime_deallocate(paths);
    if (status != ASTRA_VFS_OK) {
        (void)close_owned_session(&from->owner->pending_session);
        return status;
    }
    grant->parent = from->port.id;
    grant->deriver = request->deriver;
    grant->deriver_session = request->session;
    return ASTRA_VFS_OK;
}

static AstraVfsPortService *grant_host(const HostfsGrant *grant)
{
    return &grant->owner->port;
}

/* The grant's last holder closed its handle, or its deriver's session
 * ended. Revoke its scope -- every channel bound to it stops at once --
 * then end its sessions with their reply lanes, and its provider session,
 * whose handles go with it (Task 4). The way Haiku's disconnect_fd
 * (src/system/kernel/fs/fd.cpp) cuts every descriptor of a vnode at once
 * rather than waiting for each holder. */
static int grant_retire(uint32_t at)
{
    HostfsGrant *grant = grants[at];
    uint32_t status;
    if (!grant->retiring) {
        grant->retiring = 1u;
        hostfs_sessions_changed(NULL);
        astra_vfs_port_service_release_grant(grant_host(grant), grant->port.id);
    }
    /* Stop further control admission even when provider cleanup must retry. */
    if (grant->port.receive != 0u) {
        status = astra_close(grant->port.receive);
        if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_INVALID_HANDLE)
            grant->port.receive = 0u;
    }
    if (grant->port.scope != 0u && !grant->scope_revoked) {
        status = astra_host_scope_revoke(grant->port.scope);
        if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_INVALID_HANDLE)
            grant->scope_revoked = 1u;
        else (void)astra_log_failure("hostfs grant revoke", status);
    }
    if (grant->port.scope != 0u && grant->scope_revoked) {
        status = astra_close(grant->port.scope);
        if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_INVALID_HANDLE)
            grant->port.scope = 0u;
    }
    if ((grant->port.scope == 0u || grant->scope_revoked) && grant->session != 0u) {
        status = close_owned_session(&grant->session);
        if (status != ASTRA_VFS_OK) (void)astra_log_failure("hostfs grant session close", status);
    }
    if (grant->port.scope != 0u || grant->session != 0u || grant->port.receive != 0u)
        return 0;
    astra_runtime_deallocate(grant);
    grants[at] = grants[--grant_count];
    grants[grant_count] = NULL;
    return 1;
}

static int grant_live(uint32_t id)
{
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (!grants[at]->retiring && grants[at]->port.id == id)
            return 1;
    return 0;
}

static uint32_t volume_release(HostfsVolume *volume)
{
    const AstraVfsMountOps mount = {NULL, volume_noop_unmount};
    uint32_t status = ASTRA_VFS_OK;
    if (volume->service_ready)
        status = astra_vfs_service_shutdown(&volume->service, &mount, NULL);
    uint32_t *const sessions[] = {&volume->backend.session, &volume->pending_session};
    for (uint32_t at = 0u; at < 2u && status == ASTRA_VFS_OK; ++at) {
        if (*sessions[at] == 0u) continue;
        status = close_owned_session(sessions[at]);
    }
    if (status == ASTRA_VFS_OK && (volume->storage.handle != ASTRA_INVALID_HANDLE || volume->storage.address != NULL))
        status = astra_area_close(&volume->storage) == ASTRA_OK ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK && volume->metric_registered) {
        if (!astra_metric_unregister(volume->metric_group, sample_vfs, volume)) return ASTRA_VFS_ERR_IO;
        volume->metric_registered = 0u;
    }
    return status;
}

static int volume_has_grants(const HostfsVolume *volume)
{
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (grants[at]->owner == volume) return 1;
    return 0;
}

static void volume_reap_idle(void)
{
    HostfsVolume **at = &volumes;
    while (*at != NULL) {
        HostfsVolume *volume = *at;
        int held = !volume->dynamic || volume_has_grants(volume);
        if (held) { at = &volume->next; continue; }
        uint32_t status = volume_release(volume);
        if (status != ASTRA_VFS_OK) { at = &volume->next; continue; }
        *at = volume->next;
        astra_runtime_deallocate(volume);
    }
}

/* Retires every derived grant whose deriving session has ended, until none
 * is left: a grant derived from a retired one goes too. Dead clients'
 * sessions are reaped first, so "the deriver exited" counts.
 * shortcut: per control message this scans both hosts' 255 reply slots,
 * waits once per bound lane and walks the grants O(n^2); HostFS carries no
 * data, so that is cheap. A dead-session notification would replace the
 * scan if control traffic ever matters. */
static void retire_orphans(void)
{
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        if (volume->pending_session != 0u) (void)close_owned_session(&volume->pending_session);
    int retired = 1;

    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        astra_vfs_port_service_reap(&volume->port);
    while (retired) {
        retired = 0;
        for (uint32_t at = 0u; at < grant_count; ++at) {
            const HostfsGrant *grant = grants[at];

            if (!grant->retiring && (grant->parent == 0u ||
                (grant_live(grant->parent) &&
                 astra_vfs_service_session_bound(
                     &grant->owner->service, grant->deriver_session, grant->deriver,
                     grant->parent))))
                continue;
            int was_retiring = grant->retiring != 0u;
            if (grant_retire(at)) { retired = 1; break; }
            if (!was_retiring) retired = 1;
        }
    }
    volume_reap_idle();
}

static int grant_root_matches(const HostfsGrant *grant, const AstraVfsNodeMeta *meta)
{
    uint32_t identity = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE;
    /* The exact native mount incarnation owns an immutable whole-mount root.
     * FAT/exFAT may not report birth time. Subdirectory authority instead needs
     * its node generation: reopening its spelling must not select a replacement. */
    int whole = strcmp(grant->root, "/") == 0;
    if (!whole) identity |= ASTRA_VFS_META_GENERATION;
    return (meta->valid & grant->root_meta.valid & identity) == identity &&
        meta->volume == grant->root_meta.volume && meta->inode == grant->root_meta.inode &&
        (whole || meta->generation == grant->root_meta.generation);
}

/* Control-plane recovery keeps grant ports; old files and scopes stay lost. */
static uint32_t hostfs_check_provider(void)
{
    uint32_t epoch = 0u;
    uint32_t status = astra_vfs_host_claim(&transport, transport.generation, 0, &epoch);

    if (status != ASTRA_VFS_OK || epoch == provider_epoch)
        return status;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        status = close_owned_session(&volume->backend.session);
        if (status != ASTRA_VFS_OK) {
            volume->backend.lost = 1u;
            return status;
        }
        uint32_t session = 0u;
        status = astra_vfs_host_session_open(&transport, transport.generation,
            0u, &volume->reference, "/", volume->root_flags, 0u, &session);
        volume->backend.session = session;
        volume->backend.lost = status != ASTRA_VFS_OK;
    }
    for (uint32_t at = 0u; at < grant_count;) {
        HostfsGrant *grant = grants[at];
        if (grant->retiring) { ++at; continue; }
        uint32_t session = 0u, scope = 0u;
        uint16_t flags = (grant->port.rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u;

        /* A new scope must never overwrite an old scope whose cleanup failed.
         * Existing retirement keeps that exact resource owned and retryable. */
        uint32_t revoked = grant->port.scope == 0u ? ASTRA_SYSCALL_OK : astra_host_scope_revoke(grant->port.scope);
        if (revoked == ASTRA_SYSCALL_OK || revoked == ASTRA_SYSCALL_INVALID_HANDLE) grant->scope_revoked = 1u;
        uint32_t closed = grant->scope_revoked && grant->port.scope != 0u ? astra_close(grant->port.scope) : ASTRA_SYSCALL_OK;
        if (!grant->scope_revoked || (closed != ASTRA_SYSCALL_OK && closed != ASTRA_SYSCALL_INVALID_HANDLE)) {
            grant->port.lost = 1u;
            if (!grant_retire(at)) ++at;
            continue;
        }
        grant->port.scope = 0u;
        grant->scope_revoked = 0u;
        if (grant->session != 0u) {
            if (close_owned_session(&grant->session) != ASTRA_VFS_OK) {
                grant->port.lost = 1u;
                if (!grant_retire(at)) ++at;
                continue;
            }
        }
        status = astra_vfs_host_session_open(&transport, transport.generation,
            0u, &grant->volume, grant->root, flags, grant->requester, &session);
        grant->session = session;
        if (status == ASTRA_VFS_OK) {
            AstraVfsNodeMeta meta = {0};
            status = grant_root_meta(session, &meta);
            if (status == ASTRA_VFS_OK && !grant_root_matches(grant, &meta))
                status = ASTRA_VFS_ERR_LOST;
            if (status != ASTRA_VFS_OK) {
                grant->port.lost = 1u;
                if (!grant_retire(at)) ++at;
                continue;
            }
        }
        if (status == ASTRA_VFS_OK) {
            uint32_t created = astra_host_scope_create(device_handle, session,
                grant_scope_flags(grant->port.rights), &scope);
            if (created == ASTRA_SYSCALL_OK)
                created = astra_host_scope_limit(scope, owner_files);
            grant->port.scope = scope;
            if (created != ASTRA_SYSCALL_OK) {
                grant->port.lost = 1u;
                if (!grant_retire(at)) ++at;
                continue;
            }
        }
        grant->session = session;
        grant->port.scope = scope;
        grant->port.lost = status != ASTRA_VFS_OK;
        if (grant->port.lost)
            (void)astra_log_failure(grant->root, status);
        ++at;
    }
    provider_epoch = epoch;
    (void)astra_log("hostfs recovered from provider loss");
    return ASTRA_VFS_OK;
}

static AstraVolumeCatalogPage initial_volumes;

static int volume_metric_group(HostfsVolume *volume, const char *name)
{
    AstraString group;
    astra_string_init(&group, volume->metric_group, sizeof(volume->metric_group));
    if (!astra_string_append(&group, "hostfs.vfs.")) return 0;
    if (volume->dynamic) {
        /* Namespace spellings can name replacement media while old grants are
         * still retiring. Metrics must retain exact incarnation identity too. */
        return astra_string_append_hex(&group, volume->reference.epoch, 16u) &&
            astra_string_append_char(&group, '.') &&
            astra_string_append_hex(&group, volume->reference.id, 16u) &&
            astra_string_append_char(&group, '.') &&
            astra_string_append_hex(&group, volume->reference.generation, 16u);
    }
    return name != NULL && name[0] == '/' && name[1] != '\0' &&
        astra_string_append(&group, name + 1u);
}

static uint32_t read_volume_catalog(void *context, const AstraVolumeCatalogQuery *query,
                                    AstraVolumeCatalogPage *page)
{
    AstraVfsHostTransport *host = context;
    return astra_vfs_host_catalog(host, host->generation, query, page);
}

/* All mounted volumes use one owner shape. Ports, sessions and grants stay
 * tied to their exact native reference; no fixed WORK/SOUND dispatch exists. */
static uint32_t volume_add(const AstraVolumeReference *reference, const char *name,
                            uint32_t rights, int dynamic, uint32_t requester, uint32_t *send)
{
    HostfsVolume *volume = astra_runtime_callocate(1u, sizeof(*volume));
    uint32_t capacity = 0u;
    uint32_t session = 0u;
    uint32_t granted_session = 0u;
    uint32_t status;
    HostfsGrant *grant = NULL;

    if (volume == NULL)
        return ASTRA_STATUS_LIMIT;
    volume->reference = *reference;
    volume->dynamic = dynamic != 0;
    volume->root_flags = (rights & ASTRA_VFS_GRANT_WRITE) != 0u ? 0u : ASTRA_HOST_FS_SESSION_READ_ONLY;
    if (!volume_metric_group(volume, name)) {
        astra_runtime_deallocate(volume);
        return ASTRA_STATUS_INVALID;
    }
    status = astra_vfs_host_session_open(&transport, transport.generation, 0u,
        reference, "/", volume->root_flags, 0u,
        &session);
    volume->backend.session = session;
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_host_init(&volume->backend, &transport, transport.generation, session) ||
         !astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &volume->storage, &capacity)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_VFS_OK) {
        volume->service_ready = astra_vfs_service_init(&volume->service, astra_vfs_host_ops(), &volume->backend,
            volume->sessions, ASTRA_VFS_SESSION_MAX, volume->storage.address, capacity);
        if (!volume->service_ready) status = ASTRA_STATUS_LIMIT;
    }
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_service_set_state_lock(&volume->service, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_service_set_state_wait(&volume->service, astra_vfs_state_futex_wait,
             astra_vfs_state_futex_wake) ||
         !astra_vfs_service_set_owner_sessions(&volume->service, owner_sessions) ||
         !astra_vfs_service_set_session_changed(&volume->service, hostfs_sessions_changed, NULL)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_session_open(&transport, transport.generation, 0u,
            reference, "/", volume->root_flags, requester, &granted_session);
    volume->pending_session = granted_session;
    if (status == ASTRA_VFS_OK)
        status = grant_add(volume, granted_session, rights, reference, "/", requester, send, &grant);
    if (status == ASTRA_VFS_OK) volume->pending_session = 0u;
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_port_service_init(&volume->port, grant->port.receive, &volume->service) ||
         !astra_vfs_port_service_set_state_lock(&volume->port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_port_service_set_derive(&volume->port, hostfs_derive, NULL)))
        status = HOSTFS_FAIL_PORT;
    if (status == ASTRA_VFS_OK && metrics_registered != 0u) {
        if (astra_metric_register(volume->metric_group, sample_vfs, volume) != ASTRA_METRIC_OK)
            status = ASTRA_STATUS_LIMIT;
        else volume->metric_registered = 1u;
    }
    if (status != ASTRA_VFS_OK) {
        if (grant != NULL) {
            grant_retire(grant_count - 1u);
            (void)astra_close(*send);
            *send = 0u;
        }
        if (!volume_has_grants(volume) && volume_release(volume) == ASTRA_VFS_OK)
            astra_runtime_deallocate(volume);
        else {
            /* Keep failed cleanup owned and charged until actual retirement. */
            volume->dynamic = 1u;
            volume->next = volumes;
            volumes = volume;
        }
        return status;
    }
    volume->next = volumes;
    volumes = volume;
    return ASTRA_VFS_OK;
}

static uint32_t hostfs_volume_root(void *context, uint32_t requester,
    const AstraVolumeReference *reference, uint32_t rights,
    const AstraVolumeReference *source, uint32_t *send)
{
    (void)context;
    *send = 0u;
    uint32_t provider_status = hostfs_check_provider();
    if (provider_status != ASTRA_VFS_OK) return provider_status;
    retire_orphans();
    uint32_t held = 0u;
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (grants[at]->requester == requester) ++held;
    if (held >= owner_sessions) return ASTRA_STATUS_LIMIT;
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), reference->epoch, 0u, 0u};
    AstraVolumeRecord record = {0};
    for (;;) {
        uint32_t status = read_volume_catalog(&transport, &query, &initial_volumes);
        if (status != ASTRA_STATUS_OK) return status;
        for (uint32_t at = 0u; at < initial_volumes.count; ++at)
            if (initial_volumes.records[at].id == reference->id) record = initial_volumes.records[at];
        if (record.id != 0u || initial_volumes.next == 0u) break;
        query.epoch = initial_volumes.epoch; query.sequence = initial_volumes.sequence;
        query.after = initial_volumes.next;
    }
    if (record.id == 0u || record.generation != reference->generation || record.state == ASTRA_VOLUME_LOST)
        return ASTRA_STATUS_LOST;
    if (record.kind != ASTRA_VOLUME_MOUNT) return ASTRA_STATUS_INVALID;
    if (source != NULL && (source->epoch != reference->epoch ||
        source->id != record.parent || source->generation != record.parent_generation))
        return ASTRA_STATUS_ACCESS;
    if ((record.flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u ||
        record.origin == ASTRA_VOLUME_ORIGIN_EXPORT || record.origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE)
        return ASTRA_STATUS_ACCESS;
    if (record.state != ASTRA_VOLUME_MOUNTED) return ASTRA_STATUS_BUSY;
    if ((rights & ASTRA_RIGHT_WRITE) != 0u && (record.flags & ASTRA_VOLUME_READ_ONLY) != 0u)
        return ASTRA_STATUS_READ_ONLY;
    uint32_t granted = ASTRA_VFS_GRANT_READ |
        ((rights & ASTRA_RIGHT_WRITE) != 0u ? ASTRA_VFS_GRANT_WRITE : 0u) |
        (source != NULL ? ASTRA_VFS_GRANT_DELEGATE : 0u);
    HostfsVolume *owner = volumes;
    while (owner != NULL && (owner->reference.epoch != reference->epoch ||
        owner->reference.id != reference->id || owner->reference.generation != reference->generation))
        owner = owner->next;
    uint32_t status;
    HostfsGrant *grant = NULL;
    if (owner == NULL) {
        status = volume_add(reference, record.name, granted, 1, requester, send);
        if (status == ASTRA_STATUS_OK) {
            grant = grants[grant_count - 1u];
        }
    } else {
        if (owner->pending_session != 0u) return ASTRA_STATUS_BUSY;
        uint32_t session = 0u;
        status = astra_vfs_host_session_open(&transport, transport.generation, 0u, reference, "/",
            (granted & ASTRA_VFS_GRANT_WRITE) != 0u ? 0u : ASTRA_HOST_FS_SESSION_READ_ONLY, requester, &session);
        if (status == ASTRA_STATUS_OK) {
            owner->pending_session = session;
            status = grant_add(owner, session, granted, reference, "/", requester, send, &grant);
            if (status != ASTRA_STATUS_OK)
                (void)close_owned_session(&owner->pending_session);
        }
    }
    if (status == ASTRA_STATUS_OK) grant->requester = requester;
    return status;
}

static uint32_t hostfs_volume_control(void *context, uint32_t requester,
    const AstraVolumeControl *control, AstraVolumeReference *reference)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    retire_orphans();
    return astra_vfs_host_volume_control(&transport, transport.generation,
        requester, control, reference);
}

/* Shared directory roots may have several authenticated application holders.
 * Derivation's requester is provenance; HELLO's existing native sessions name
 * actual holders. No separate holder table or last-writer ownership exists. */
static HostfsGrant *usage_root_grant(uint64_t session)
{
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (grants[at]->session == session) return grants[at];
    return NULL;
}

static int usage_root_owner(HostfsGrant *grant, uint32_t after, uint32_t *owner)
{
    uint32_t selected = 0u;
    if (grant == NULL) return 0;
    AstraVfsService *service = &grant->owner->service;
    for (uint32_t at = 0u; at < service->session_capacity; ++at) {
        const AstraVfsSessionSlot *slot = &service->sessions[at];
        if (slot->id != 0u && slot->grant == grant->port.id && slot->owner > after &&
            (selected == 0u || slot->owner < selected)) selected = slot->owner;
    }
    *owner = selected;
    return selected != 0u;
}

static int usage_emit(AstraVolumeUsagePage *page, const AstraVolumeUsageQuery *query,
    const AstraVolumeUsageRecord *row, uint32_t owner)
{
    if (astra_volume_usage_key_compare(row->kind, row->handle, owner,
            query->after_kind, query->after, query->after_process) <= 0) return 0;
    if (page->count == ASTRA_VOLUME_USAGE_PAGE_RECORDS) {
        const AstraVolumeUsageRecord *last = &page->records[page->count - 1u];
        page->next = last->handle; page->next_kind = last->kind; page->next_process = last->process;
        return 1;
    }
    page->records[page->count] = *row;
    page->records[page->count++].process = owner;
    return 0;
}

static uint32_t hostfs_read_usage(const AstraVolumeUsageQuery *query,
    AstraVolumeUsagePage *page)
{
    if (!astra_volume_usage_query_valid(query)) return ASTRA_STATUS_INVALID;
    memset(page, 0, sizeof(*page));
    /* The same core state lock keeps canonical holder slots stable through
     * the streamed page, including continuation inside one shared root. */
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_STATUS_BUSY;
    if (usage_holders_changed != 0u) {
        uint32_t status = astra_vfs_host_usage_holders_changed(&transport, transport.generation);
        if (status != ASTRA_STATUS_OK) { astra_vfs_state_lock_release(&state_lock); return status; }
        usage_holders_changed = 0u;
    }
    AstraVolumeUsagePage *raw = astra_runtime_allocate(sizeof(*raw));
    if (raw == NULL) { astra_vfs_state_lock_release(&state_lock); return ASTRA_STATUS_LIMIT; }
    AstraVolumeUsageQuery cursor = *query;
    /* A public page may stop between the holders of one retained root. Reread
     * that exact raw root under the same sequence before expanding its tail. */
    if (cursor.after_kind == ASTRA_VOLUME_USAGE_ROOT) {
        cursor.after_process = UINT32_MAX;
        if (--cursor.after == 0u) { --cursor.after_kind; cursor.after = UINT64_MAX; }
    }
    uint32_t status;
    do {
        status = astra_vfs_host_usage(&transport, transport.generation, &cursor, raw);
        if (status != ASTRA_STATUS_OK) break;
        if (!astra_volume_usage_page_valid(&cursor, raw)) { status = ASTRA_STATUS_INVALID; break; }
        page->version = raw->version; page->size = sizeof(*page); page->volume = raw->volume;
        page->sequence = raw->sequence; page->catalog_sequence = raw->catalog_sequence;
        for (uint32_t at = 0u; at < raw->count; ++at) {
            AstraVolumeUsageRecord *row = &raw->records[at];
            int complete = 0;
            if (row->kind == ASTRA_VOLUME_USAGE_ROOT) {
                HostfsGrant *grant = usage_root_grant(row->handle);
                if (grant != NULL && (grant->retiring || grant->port.lost)) {
                    row->flags |= ASTRA_VOLUME_USAGE_LOST;
                    if (grant->retiring) row->flags |= ASTRA_VOLUME_USAGE_CLEANUP_PENDING;
                }
                uint32_t owner = 0u, previous = 0u;
                if (usage_root_owner(grant, 0u, &owner)) {
                    do {
                        complete = usage_emit(page, query, row, owner);
                        previous = owner;
                    } while (!complete && usage_root_owner(grant, previous, &owner));
                } else complete = usage_emit(page, query, row, 0u);
            } else complete = usage_emit(page, query, row, row->process);
            if (complete) goto done;
        }
        cursor.volume = raw->volume; cursor.sequence = raw->sequence;
        cursor.catalog_sequence = raw->catalog_sequence;
        cursor.after = raw->next; cursor.after_kind = raw->next_kind; cursor.after_process = raw->next_process;
    } while (raw->next != 0u);
done:
    astra_runtime_deallocate(raw);
    astra_vfs_state_lock_release(&state_lock);
    return status;
}

static uint32_t hostfs_volume_usage(void *context,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    retire_orphans();
    return hostfs_read_usage(query, page);
}

/* Configured authority belongs to the persistent source, even while unmounted.
 * Never reconstruct it from a reusable /ram0 namespace slot or RAM class. */
static uint32_t configured_ram_source(AstraVolumeReference *source)
{
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    memset(source, 0, sizeof(*source));
    for (;;) {
        uint32_t status = read_volume_catalog(&transport, &query, &initial_volumes);
        if (status != ASTRA_STATUS_OK) return status;
        for (uint32_t at = 0u; at < initial_volumes.count; ++at) {
            const AstraVolumeRecord *record = &initial_volumes.records[at];
            if ((record->flags & ASTRA_VOLUME_CONFIGURED_RAM) == 0u) continue;
            if (source->id != 0u || record->kind != ASTRA_VOLUME_SOURCE ||
                record->origin != ASTRA_VOLUME_ORIGIN_RAM || record->class_id != ASTRA_VOLUME_RAM ||
                record->state == ASTRA_VOLUME_LOST) return ASTRA_STATUS_INVALID;
            *source = (AstraVolumeReference){initial_volumes.epoch, record->id, record->generation};
        }
        if (initial_volumes.next == 0u) break;
        query.epoch = initial_volumes.epoch; query.sequence = initial_volumes.sequence;
        query.after = initial_volumes.next;
    }
    return source->id != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    AstraArea metrics_storage = ASTRA_AREA_INIT;
    uint32_t metrics_capacity = 0u;
    uint32_t metrics_receive = 0u;
    uint32_t metrics_send = 0u;
    uint32_t shutdown_receive = 0u;
    uint32_t shutdown_send = 0u;
    uint32_t volume_receive = 0u;
    uint32_t volume_send = 0u;
    uint32_t volume_read_receive = 0u, volume_read_send = 0u;
    uint32_t volume_write_receive = 0u, volume_write_send = 0u;
    uint32_t volume_admin_receive = 0u, volume_admin_send = 0u;
    uint32_t volume_usage_receive = 0u, volume_usage_send = 0u;
    uint32_t ram_read_receive = 0u, ram_read_send = 0u;
    uint32_t ram_write_receive = 0u, ram_write_send = 0u;
    AstraVolumeReference ram_source = {0};
    uint32_t status = ASTRA_STATUS_OK;
    const char *const names[] = {"/dh0", "/work", "/sound", "/tmp"};
    uint32_t published[12] = {0};
    AstraVolumeReference system;
    AstraVfsHostBackend system_backend;
    uint32_t config_session = 0u;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_FILESYSTEM);
    bootstrap = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    device_handle = device->handle;
    if (!astra_vfs_host_transport_init(&transport, device_handle,
        astra_vfs_state_lock_acquire, astra_vfs_state_lock_release, &transport_lock))
        return HOSTFS_FAIL_DEVICE;
    status = astra_vfs_host_claim(&transport, transport.generation, 0, &provider_epoch);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_lookup_mount(&transport, transport.generation,
            "/dh0", ASTRA_VOLUME_SYSTEM, &initial_volumes, &system);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_session_open(&transport, transport.generation,
            0u, &system, "/", ASTRA_HOST_FS_SESSION_READ_ONLY, 0u, &config_session);
    if (status == ASTRA_VFS_OK) {
        if (!astra_vfs_host_init(&system_backend, &transport, transport.generation, config_session))
            status = ASTRA_STATUS_INVALID;
        else
            status = configured_limits(&system_backend);
        uint32_t closed = astra_vfs_host_session_close(&transport, transport.generation, config_session);
        if (status == ASTRA_STATUS_OK)
            status = closed;
    }
    if (status == ASTRA_STATUS_OK) status = configured_ram_source(&ram_source);
    for (uint32_t index = 0u; index < 4u && status == ASTRA_STATUS_OK; ++index) {
        AstraVolumeReference reference;
        status = astra_vfs_host_lookup_mount(&transport, transport.generation,
            names[index], 0u, &initial_volumes, &reference);
        if (status == ASTRA_STATUS_OK)
            status = volume_add(&reference, names[index],
                ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE | ASTRA_VFS_GRANT_DELEGATE,
                0, 0u, &published[index]);
    }
    if (status == ASTRA_STATUS_OK &&
        (!register_metrics() ||
         !astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &metrics_storage, &metrics_capacity) ||
         !astra_vfs_service_init(&metrics_service, astra_metric_vfs_ops(), &metrics_backend,
             metrics_sessions, ASTRA_VFS_SESSION_MAX, metrics_storage.address, metrics_capacity) ||
         astra_rt_port_create(queue_messages,
             queue_messages * (uint32_t)sizeof(AstraVfsRequestMessage),
             &metrics_receive, &metrics_send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&metrics_port, metrics_receive, &metrics_service)))
        status = HOSTFS_FAIL_PORT;
    if (status == ASTRA_STATUS_OK &&
        astra_rt_signal_configure(hostfs_signal, signal_stack + sizeof(signal_stack),
            0u, NULL, NULL) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(1u, sizeof(AstraShutdownRequest), &shutdown_receive,
            &shutdown_send) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_receive, &volume_send) != ASTRA_SYSCALL_OK ||
         !astra_volume_port_init(&volume_port, volume_receive, owner_sessions,
            read_volume_catalog, &transport)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_read_receive, &volume_read_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_write_receive, &volume_write_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_admin_receive, &volume_admin_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_usage_receive, &volume_usage_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &ram_read_receive, &ram_read_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &ram_write_receive, &ram_write_send) != ASTRA_SYSCALL_OK))
        status = ASTRA_STATUS_LIMIT;
    volume_port.root = hostfs_volume_root;
    volume_port.control = hostfs_volume_control;
    volume_port.usage = hostfs_volume_usage;
    published[4] = metrics_send;
    published[5] = volume_send;
    published[6] = volume_read_send;
    published[7] = volume_write_send;
    published[8] = volume_admin_send;
    published[9] = volume_usage_send;
    published[10] = ram_read_send;
    published[11] = ram_write_send;
    uint32_t ready = astra_service_ready_managed(bootstrap->handle, status, published, 12u, shutdown_send);
    if (ready != ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
        status = HOSTFS_FAIL_READY;
    if (shutdown_send != 0u)
        (void)astra_close(shutdown_send);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    /* The pre-27 single pump, with the wait set built from the grant
     * table each time round: HostFS serves only the control plane. */
    uint64_t catalog_deadline = astra_clock_monotonic();
    for (;;) {
        uint32_t count = 0u;
        uint32_t selected = ASTRA_WAIT_INDEX_NONE;

        uint64_t now = astra_clock_monotonic();
        if (now >= catalog_deadline) {
            astra_volume_port_poll(&volume_port);
            retire_orphans();
            catalog_deadline = now + UINT64_C(1000000000);
        }

        wait_handles[count] = shutdown_receive;
        wait_owners[count++] = WAIT_SHUTDOWN;
        wait_handles[count] = metrics_port.receive;
        wait_owners[count++] = WAIT_METRICS;
        wait_handles[count] = volume_port.receive;
        wait_owners[count++] = WAIT_VOLUMES;
        wait_handles[count] = volume_read_receive;
        wait_owners[count++] = WAIT_VOLUME_READ;
        wait_handles[count] = volume_write_receive;
        wait_owners[count++] = WAIT_VOLUME_WRITE;
        wait_handles[count] = volume_admin_receive;
        wait_owners[count++] = WAIT_VOLUME_ADMIN;
        wait_handles[count] = volume_usage_receive;
        wait_owners[count++] = WAIT_VOLUME_USAGE;
        wait_handles[count] = ram_read_receive;
        wait_owners[count++] = WAIT_RAM_READ;
        wait_handles[count] = ram_write_receive;
        wait_owners[count++] = WAIT_RAM_WRITE;
        /* Grants in rotation: the first grant served moves on each pass,
         * so one busy port cannot starve the others' control plane (M6). */
        if (grant_count != 0u)
            wait_rotation %= grant_count;
        for (uint32_t step = 0u; step < grant_count; ++step) {
            uint32_t at = (wait_rotation + step) % grant_count;
            if (grants[at]->retiring || grants[at]->port.receive == 0u) continue;
            wait_handles[count] = grants[at]->port.receive;
            wait_owners[count++] = at;
        }
        ++wait_rotation;
        status = astra_wait_multiple(wait_handles, count,
                                     catalog_deadline, &selected,
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
        if (status == ASTRA_SYSCALL_TIMED_OUT)
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
        if (wait_owners[selected] == WAIT_VOLUMES) {
            if (status != ASTRA_SYSCALL_OK)
                return (int)status;
            (void)astra_volume_port_pump(&volume_port, 1u);
            continue;
        }
        if (wait_owners[selected] == WAIT_VOLUME_READ || wait_owners[selected] == WAIT_VOLUME_WRITE ||
            wait_owners[selected] == WAIT_RAM_READ || wait_owners[selected] == WAIT_RAM_WRITE) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            (void)astra_volume_port_root_pump(&volume_port, wait_handles[selected],
                ASTRA_RIGHT_READ | ((wait_owners[selected] == WAIT_VOLUME_WRITE ||
                    wait_owners[selected] == WAIT_RAM_WRITE) ? ASTRA_RIGHT_WRITE : 0u),
                (wait_owners[selected] == WAIT_RAM_READ || wait_owners[selected] == WAIT_RAM_WRITE) ?
                    &ram_source : NULL, 1u);
            continue;
        }
        if (wait_owners[selected] == WAIT_VOLUME_ADMIN) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            (void)astra_volume_port_control_pump(&volume_port, wait_handles[selected], 1u);
            continue;
        }
        if (wait_owners[selected] == WAIT_VOLUME_USAGE) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            (void)astra_volume_port_usage_pump(&volume_port, wait_handles[selected], 1u);
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
        status = hostfs_check_provider();
        if (status != ASTRA_VFS_OK)
            return (int)status;
        /* A dead deriver's grants go before this request is served, so a
         * derive never competes with grants nobody holds (re-review M4). */
        retire_orphans();
        if (wait_owners[selected] >= grant_count ||
            grants[wait_owners[selected]]->port.receive !=
                wait_handles[selected])
            continue;
        (void)astra_vfs_port_service_grant_pump(
            grant_host(grants[wait_owners[selected]]), &worker,
            &grants[wait_owners[selected]]->port, 1u);
        retire_orphans();
    }
}
