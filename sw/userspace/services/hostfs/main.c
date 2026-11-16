#include <astra/host.h>
#include <astra/filesystem_change.h>
#include <astra/config_document.h>
#include <astra/metrics.h>
#include <astra/metrics_vfs.h>
#include <astra/messaging.h>
#include <astra/pager_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service_core.h>
#include <astra/volume_port.h>
#include <astra/string.h>

#include <stddef.h>
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
static uint32_t secret_device_handle;
/* Last completely rebound provider view; zero requires revalidation. */
static uint32_t bound_provider_epoch;
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
enum {
    CACHE_HITS, CACHE_CREATES, CACHE_PAGE_REQUESTS, CACHE_CLOSES,
    CACHE_INVALIDATIONS, CACHE_IDENTITY_RETIREMENTS, CACHE_LINK_RETIREMENTS,
    CACHE_GRANT_RETIREMENTS, CACHE_REQUESTED_RETIREMENTS, CACHE_STAT_COUNT
};

/* Provider first: raw backend operations share this context with HostFS policy. */
typedef struct HostfsBackend {
    AstraVfsHostBackend provider;
    struct HostfsVolume *owner;
    struct HostfsGrant *grant;
    uint32_t references;
    uint32_t epoch;
} HostfsBackend;

typedef struct HostfsVolume {
    struct HostfsVolume *next;
    AstraVolumeReference reference;
    uint16_t root_flags;
    uint8_t dynamic;
    uint8_t metric_registered;
    uint8_t service_ready;
    uint8_t config_store;
    uint32_t pending_session;
    char metric_group[ASTRA_METRIC_NAME_MAX];
    HostfsBackend backend;
    AstraVfsBackendOps ops;
    AstraVfsService service;
    AstraVfsChangeSnapshot published_changes;
    uint64_t published_change_sequence;
    uint64_t observed_cache_sequence;
    uint64_t observed_cache_nodes;
    uint32_t cache_stats[CACHE_STAT_COUNT];
    AstraVfsSessionSlot sessions[ASTRA_VFS_SESSION_MAX];
    AstraVfsPortService port;
    AstraArea storage;
} HostfsVolume;

static HostfsVolume *volumes;
static uint32_t filesystem_transaction = 1u;
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
 * FS15). Its rooted provider session stays private; native service sessions
 * bind callers to the grant's rights. The port record comes first, so the
 * port layer's grant pointer is this record.
 */
typedef struct HostfsGrant {
    AstraVfsPortGrant port;        /* receive, id, rights */
    HostfsVolume *owner;          /* canonical owner of this mounted volume */
    uint32_t session;              /* its provider session */
    HostfsBackend *backend;       /* immutable rooted context for this incarnation */
    /* A derived grant belongs to the session that derived it (review I1):
     * it is retired when that session ends -- the deriver said BYE, died,
     * or its own grant retired -- so no holder keeps HostFS full after it
     * has gone. 0 for base volume grants. */
    uint32_t parent;               /* the grant it was derived from */
    uint32_t deriver;              /* kernel-stamped deriving process */
    uint32_t deriver_session;      /* its session on `parent` */
    uint32_t requester;            /* kernel-stamped creation/recovery provenance */
    uint32_t retiring;             /* charged until backend/session/port retirement */
    uint32_t backing_references;  /* backend contexts retaining this grant */
    AstraVolumeReference volume;
    char root[ASTRA_HOST_FS_PATH_MAX]; /* recorded volume-relative recovery path */
    AstraVfsNodeMeta root_meta;
} HostfsGrant;

typedef struct HostfsFileArea {
    struct HostfsFileArea *next;
    HostfsVolume *owner;
    HostfsGrant *grant;
    AstraVfsFilePageSource source;
    AstraVfsFileIO *io;
    AstraVfsFileCache cache;
    uint32_t writeback_cursor;
    uint32_t provider_epoch;
    uint32_t area;
    uint64_t usage_mapping_sequence;
    uint32_t pins;
    uint8_t stale;
    uint8_t check_links;
    uint8_t retired_grant;
    uint8_t reclaim_requested;
} HostfsFileArea;

static HostfsFileArea *file_areas;
static AstraVfsFilePager file_pager;
static uint32_t file_pager_send;
static uint32_t file_sync_receive;
static uint32_t file_pager_thread;
static uint32_t file_pager_cookie = 1u;
static volatile uint32_t file_pager_stop;
static uint32_t file_pager_failed;
static void *file_pager_page;
static _Alignas(16) uint8_t cache_page[ASTRA_MEMORY_PAGE_SIZE];
static _Alignas(16) uint8_t cache_sync_page[ASTRA_MEMORY_PAGE_SIZE];

/*
 * The grant table grows on demand: no count is compiled in or configured
 * (CLAUDE.md, "no ceilings compiled into an image"). The limits that remain
 * are real ones and answer for themselves: the kernel's handle pool,
 * the wait-set ABI (ASTRA_WAIT_MULTIPLE_MAX) and
 * memory. Each record is allocated on its own so the port layer's grant
 * pointer never moves while a pump is using it.
 */
static HostfsGrant **grants;
static uint32_t grant_count;       /* live records, packed at the front */
static uint32_t grant_capacity;    /* slots in grants, wait_* */
static uint32_t next_grant_id = 1u;
static uint32_t *wait_handles;     /* grant_capacity + HOSTFS_FIXED_WAITS */
static uint32_t *wait_owners;      /* what each handle is */
static uint32_t wait_rotation;     /* first grant served this pass (M6) */
static uint32_t usage_holders_changed;

/* Existing service slots own holder identity. This notification only invalidates
 * the canonical QEMU usage sequence before another page can be published. */
static void hostfs_holders_changed(void *context)
{
    (void)context;
    usage_holders_changed = 1u;
}

/* wait_owners values that are not a grant's index. */
#define WAIT_METRICS  (UINT32_MAX - 1u)
#define WAIT_VOLUMES  (UINT32_MAX - 2u)
#define WAIT_VOLUME_READ (UINT32_MAX - 3u)
#define WAIT_VOLUME_WRITE (UINT32_MAX - 4u)
#define WAIT_VOLUME_DELEGATE (UINT32_MAX - 10u)
#define WAIT_VOLUME_ADMIN (UINT32_MAX - 5u)
#define WAIT_VOLUME_USAGE (UINT32_MAX - 6u)
#define WAIT_DISK_ADMIN (UINT32_MAX - 9u)
#define WAIT_RAM_READ (UINT32_MAX - 7u)
#define WAIT_RAM_WRITE (UINT32_MAX - 8u)
#define WAIT_SHUTDOWN UINT32_MAX
#define WAIT_FILE_SYNC (UINT32_MAX - 11u)
#define HOSTFS_FIXED_WAITS 12u

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
static void file_areas_maintain(int discard_idle);
static uint32_t file_areas_writeback(uint32_t budget);
static int file_areas_observe_nodes(HostfsVolume *volume, uint64_t nodes);
static void file_pager_stop_and_close(void);
static uint32_t close_owned_session(uint32_t *session);
static uint32_t grant_backend_detach(HostfsGrant *grant, int lost);
static uint32_t hostfs_check_provider(void);

static int hostfs_reclaim_once(uint32_t attempt, int limited)
{
    if (attempt != 0u || !limited) return 0;
    /* Dirty owners must make bounded progress before clean handles can retire. */
    (void)file_areas_writeback(16u);
    file_areas_maintain(1);
    return 1;
}

static uint32_t hostfs_session_open(uint32_t parent,
    const AstraVolumeReference *volume, const char *root, uint16_t flags,
    uint32_t requester, uint32_t *session)
{
    uint32_t status;
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        status = astra_vfs_host_session_open(&transport, transport.generation,
            parent, volume, root, flags, requester, session);
        if (!hostfs_reclaim_once(attempt, status == ASTRA_VFS_ERR_LIMIT)) break;
    }
    return status;
}

static void hostfs_cache_reclaim(void *context)
{
    (void)context;
    (void)hostfs_reclaim_once(0u, 1);
}

static uint32_t file_pager_check(void)
{
    uint32_t status = __atomic_load_n(&file_pager_failed, __ATOMIC_ACQUIRE);
    if (status != ASTRA_SYSCALL_RESOURCE_LIMIT) return status;
    /* Only the control thread may retire cache owners while the pager waits. */
    hostfs_cache_reclaim(NULL);
    __atomic_store_n(&file_pager_failed, 0u, __ATOMIC_RELEASE);
    (void)astra_log("HostFS pager admission reclaim");
    return astra_futex_wake(&file_pager_failed, 1u, NULL);
}

static uint32_t finish_hostfs(void)
{
    uint32_t status;
    uint32_t sync_status;

    if (file_sync_receive != 0u) {
        while (astra_vfs_file_pager_sync_pump(&file_pager, file_sync_receive,
                8u, &sync_status) != 0u) {}
        if (sync_status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_IO;
    }

    while (astra_vfs_port_service_worker_pump(&metrics_port,
                                               &metrics_worker, 1u) != 0u) {}
    for (uint32_t at = 0u; at < grant_count; ++at)
        while (!grants[at]->retiring && astra_vfs_port_service_grant_pump(
                   grant_host(grants[at]), &worker, &grants[at]->port,
                   1u) != 0u) {}
    if (metrics_port.stalled != 0u) {
        (void)astra_log_failure("HostFS shutdown metrics drain", metrics_port.stalled);
        return ASTRA_STATUS_IO;
    }
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        if (volume->port.stalled != 0u) {
            (void)astra_log_failure("HostFS shutdown volume drain", volume->port.stalled);
            return ASTRA_STATUS_IO;
        }
    /* Draining requests can admit another file area. */
    status = file_areas_writeback(0u);
    if (status != ASTRA_VFS_OK) return status;
    file_areas_maintain(1);
    if (file_areas != NULL) return ASTRA_VFS_ERR_BUSY;
    bound_provider_epoch = 0u;
    for (uint32_t at = 0u; at < grant_count; ++at) {
        HostfsGrant *grant = grants[at];
        status = grant_backend_detach(grant, 1);
        if (status == ASTRA_VFS_OK)
            status = close_owned_session(&grant->session);
        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("HostFS shutdown grant", status);
            return status;
        }
    }
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        status = close_owned_session(&volume->backend.provider.session);
        if (status == ASTRA_VFS_OK)
            status = close_owned_session(&volume->pending_session);
        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("HostFS shutdown volume", status);
            return status;
        }
    }
    /* Public endpoints survive until READY and process exit, so failed
     * retirement can reuse normal provider recovery without breaking clients. */
    status = astra_vfs_host_claim(&transport, transport.generation, 1, NULL);
    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("HostFS shutdown claim", status);
    return status;
}

static uint32_t hostfs_shutdown(const AstraShutdownRequest *request, uint32_t reply)
{
    uint32_t status = finish_hostfs();
    uint32_t sent = astra_shutdown_respond(reply, request->header.transaction_id,
        status == ASTRA_VFS_OK ? ASTRA_SHUTDOWN_READY : ASTRA_SHUTDOWN_CANCEL,
        status);

    if (status == ASTRA_VFS_OK && sent == ASTRA_SYSCALL_OK)
        return ASTRA_VFS_OK;
    if (bound_provider_epoch == 0u) {
        uint32_t recovered = hostfs_check_provider();
        if (recovered != ASTRA_VFS_OK)
            (void)astra_log_failure("HostFS shutdown recovery", recovered);
    }
    if (status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_PEER;
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
    "host.fs.share", "host.fs.adopt", "host.fs.file_flags", "host.fs.path",
    "host.fs.try_lock", "host.fs.change_snapshot", "host.fs.change_next",
    "host.fs.set_file_times"
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
    static const char *const names[CACHE_STAT_COUNT] = {
        "cache_hits", "cache_creates", "cache_page_requests", "cache_closes",
        "cache_invalidations", "cache_identity_retirements", "cache_link_retirements",
        "cache_grant_retirements", "cache_requested_retirements"
    };
    uint32_t count = 17u + CACHE_STAT_COUNT;
    if (out == NULL || capacity < count) return count;
    (void)sample_vfs_stats(astra_vfs_service_stats(&volume->service), out, capacity, 17u);
    for (uint32_t index = 0u; index < CACHE_STAT_COUNT; ++index) {
        out[17u + index].name = names[index];
        out[17u + index].value = volume->cache_stats[index];
    }
    return count;
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

    if (capacity + HOSTFS_FIXED_WAITS > ASTRA_WAIT_MULTIPLE_MAX)
        capacity = ASTRA_WAIT_MULTIPLE_MAX - HOSTFS_FIXED_WAITS;
    if (capacity <= grant_capacity)
        return ASTRA_VFS_ERR_LIMIT;
    table = astra_runtime_reallocate(grants, capacity * sizeof(*grants));
    if (table == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    grants = table;
    handles = astra_runtime_reallocate(wait_handles,
                                       (capacity + HOSTFS_FIXED_WAITS) * sizeof(*handles));
    if (handles == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    wait_handles = handles;
    owners = astra_runtime_reallocate(wait_owners,
                                      (capacity + HOSTFS_FIXED_WAITS) * sizeof(*owners));
    if (owners == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    wait_owners = owners;
    grant_capacity = capacity;
    return ASTRA_VFS_OK;
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

/* These reference transitions run under state_lock. */
static uint32_t grant_backing_retain(HostfsGrant *grant)
{
    if (grant->retiring) return ASTRA_VFS_ERR_LOST;
    if (grant->backing_references == UINT32_MAX) return ASTRA_VFS_ERR_LIMIT;
    ++grant->backing_references;
    return ASTRA_VFS_OK;
}

static void hostfs_backend_put(HostfsBackend *backend)
{
    if (--backend->references != 0u) return;
    --backend->grant->backing_references;
    astra_runtime_deallocate(backend);
}

static uint32_t hostfs_backend_retain(void *context, const AstraVfsBackend *backend)
{
    HostfsBackend *root = backend->context;
    (void)context;
    if (root->grant->retiring || __atomic_load_n(&root->provider.lost, __ATOMIC_ACQUIRE))
        return ASTRA_VFS_ERR_LOST;
    if (root->references == UINT32_MAX) return ASTRA_VFS_ERR_LIMIT;
    ++root->references;
    return ASTRA_VFS_OK;
}

static void hostfs_backend_release(void *context, const AstraVfsBackend *backend)
{
    (void)context;
    hostfs_backend_put(backend->context);
}

static uint64_t hostfs_backend_incarnation(void *context, const AstraVfsBackend *backend)
{
    const HostfsBackend *root = backend->context;
    (void)context;
    return root->epoch;
}

static uint32_t hostfs_backend_acquire(void *context, uint32_t owner, uint32_t id,
    AstraVfsBackend *backend)
{
    HostfsVolume *volume = context;
    (void)owner;
    for (uint32_t at = 0u; at < grant_count; ++at) {
        HostfsGrant *grant = grants[at];
        if (grant->port.id != id || grant->owner != volume) continue;
        if (grant->retiring || grant->backend == NULL) return ASTRA_VFS_ERR_LOST;
        *backend = (AstraVfsBackend){&volume->ops, grant->backend};
        return hostfs_backend_retain(context, backend);
    }
    return ASTRA_VFS_ERR_ACCESS;
}

static const AstraVfsBackendBindingOps hostfs_backend_bindings = {
    hostfs_backend_acquire, hostfs_backend_retain, hostfs_backend_release,
    hostfs_backend_incarnation
};

static uint32_t grant_backend_create(HostfsGrant *grant, uint32_t epoch)
{
    HostfsBackend *backend = astra_runtime_callocate(1u, sizeof(*backend));
    uint32_t status;

    if (backend == NULL) return ASTRA_VFS_ERR_LIMIT;
    if (!astra_vfs_host_init(&backend->provider, &transport, transport.generation, grant->session)) {
        astra_runtime_deallocate(backend);
        return ASTRA_VFS_ERR_LOST;
    }
    backend->owner = grant->owner;
    backend->grant = grant;
    backend->references = 1u;
    backend->epoch = epoch;
    if (!astra_vfs_state_lock_acquire(&state_lock)) {
        astra_runtime_deallocate(backend);
        return ASTRA_VFS_ERR_IO;
    }
    status = grant->backend != NULL ? ASTRA_VFS_ERR_BUSY : grant_backing_retain(grant);
    if (status == ASTRA_VFS_OK) grant->backend = backend;
    astra_vfs_state_lock_release(&state_lock);
    if (status != ASTRA_VFS_OK) astra_runtime_deallocate(backend);
    return status;
}

static uint32_t grant_backend_detach(HostfsGrant *grant, int lost)
{
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    HostfsBackend *backend = grant->backend;
    grant->backend = NULL;
    if (backend != NULL) {
        if (lost) __atomic_store_n(&backend->provider.lost, 1u, __ATOMIC_RELEASE);
        hostfs_backend_put(backend);
    }
    astra_vfs_state_lock_release(&state_lock);
    return ASTRA_VFS_OK;
}

/* Grant port carries native authority; the rooted provider session stays private. */
static uint32_t grant_add(HostfsVolume *owner, uint32_t session,
                          uint32_t rights, const AstraVolumeReference *volume, const char *root, uint32_t requester, uint32_t *send,
                          HostfsGrant **added)
{
    HostfsGrant *grant;
    uint32_t status;

    *send = 0u;
    if (added != NULL) *added = NULL;
    if (next_grant_id == UINT32_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    if (root == NULL || root[0] != '/' || strlen(root) >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (grant_count == grant_capacity &&
        (status = grow_grants()) != ASTRA_VFS_OK) {
        (void)astra_log_failure("hostfs grant table", status);
        return status;
    }
    grant = astra_runtime_callocate(1u, sizeof(*grant));
    if (grant == NULL) {
        (void)astra_log_failure("hostfs grant allocation", ASTRA_VFS_ERR_LIMIT);
        return ASTRA_VFS_ERR_LIMIT;
    }
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
    /* Whole-volume authority is pinned by provider epoch, mount and generation.
     * A live root stat can wait forever on a disconnected network mount. */
    status = strcmp(root, "/") == 0 ? ASTRA_VFS_OK :
             grant_root_meta(session, &grant->root_meta);
    if (status != ASTRA_VFS_OK) {
        (void)grant_retire(at);
        return status;
    }
    status = grant_backend_create(grant, bound_provider_epoch);
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("hostfs grant backend", status);
        (void)grant_retire(at);
        return status;
    }
    AstraHostLeaseInfo lease = {0};
    status = astra_host_lease_query(device_handle, &lease);
    if (status != ASTRA_SYSCALL_OK) {
        (void)grant_retire(at);
        return astra_vfs_host_status_from_syscall(status);
    }
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        status = astra_rt_port_create(
            queue_messages,
            queue_messages *
                (uint32_t)sizeof(AstraVfsRequestMessage),
            &grant->port.receive, send);
        if (!hostfs_reclaim_once(attempt,
            status == ASTRA_SYSCALL_RESOURCE_LIMIT || status == ASTRA_SYSCALL_OUT_OF_MEMORY)) break;
    }
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("hostfs grant port", status);
        (void)grant_retire(at);
        if (*send != 0u) (void)astra_close(*send);
        *send = 0u;
        return astra_vfs_host_status_from_syscall(status);
    }
    grant->port.features = ASTRA_VFS_FEATURE_RESOLVES | ASTRA_VFS_FEATURE_NODE_META |
                           ASTRA_VFS_FEATURE_DESCRIPTIONS | ASTRA_VFS_FEATURE_DIRECTORY_PATH;
    if ((lease.capabilities & ASTRA_HOST_CAP_FS_POSITION_ADVANCE) != 0u)
        grant->port.features |= ASTRA_VFS_FEATURE_POSITION_ADVANCE;
    if (!owner->config_store && file_pager_thread != 0u)
        grant->port.features |= ASTRA_VFS_FEATURE_FILE_AREAS |
                                ASTRA_VFS_FEATURE_CACHE_RECLAIM;
    if (owner->config_store)
        grant->port.features |= ASTRA_VFS_FEATURE_CONFIG_CRYPT;
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
    status = hostfs_session_open(from->session, NULL, provider_root,
        (request->rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u, request->deriver, &session);
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("hostfs derive session", status);
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

static uint32_t hostfs_config_crypt(void *context,
    const AstraVfsPortGrant *port, uint32_t operation, const char *key,
    const uint8_t *input, uint32_t input_length, uint8_t *output,
    uint32_t output_capacity, uint32_t *output_length)
{
    const HostfsGrant *grant = (const HostfsGrant *)(const void *)port;
    AstraHostChannelClient host = {0};
    AstraHostCommand *command;
    uint32_t root_bytes = (uint32_t)strlen(grant->root) + 1u;
    uint32_t key_bytes = (uint32_t)strlen(key) + 1u;
    uint32_t context_bytes = root_bytes + key_bytes;
    uint32_t status;

    (void)context;
    *output_length = 0u;
    if (!grant->owner->config_store || secret_device_handle == 0u ||
        (operation != ASTRA_VFS_CONFIG_SEAL &&
         operation != ASTRA_VFS_CONFIG_OPEN) ||
        context_bytes > 512u || input_length > 1068u ||
        context_bytes + input_length > 2048u ||
        output_capacity > 1068u)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_host_client_open(secret_device_handle,
        ASTRA_HOST_CAP_CONFIG_SECRETS, 2048u, &host);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_IO;
    command = astra_host_client_prepare(&host, ASTRA_HOST_SERVICE_CONFIG_SECRETS,
        operation == ASTRA_VFS_CONFIG_SEAL ? ASTRA_HOST_CONFIG_SECRET_SEAL :
                                          ASTRA_HOST_CONFIG_SECRET_OPEN);
    if (command == NULL) {
        status = ASTRA_VFS_ERR_IO;
        goto done;
    }
    command->offset_lo = context_bytes;
    command->data_length = context_bytes + input_length;
    command->data_capacity = output_capacity;
    memcpy((void *)(uintptr_t)host.data, grant->root, root_bytes);
    memcpy((void *)(uintptr_t)(host.data + root_bytes), key, key_bytes);
    memcpy((void *)(uintptr_t)(host.data + context_bytes), input, input_length);
    status = astra_host_client_submit(&host);
    if (status != ASTRA_SYSCALL_OK)
        status = ASTRA_VFS_ERR_IO;
    else if (command->status == ASTRA_STATUS_ACCESS)
        status = ASTRA_VFS_ERR_ACCESS;
    else if (command->status != ASTRA_STATUS_OK ||
             command->result_length == 0u ||
             command->result_length > output_capacity)
        status = ASTRA_VFS_ERR_IO;
    else {
        *output_length = command->result_length;
        memcpy(output, (const void *)(uintptr_t)host.data, *output_length);
        status = ASTRA_VFS_OK;
    }
done:
    astra_secret_wipe((void *)(uintptr_t)host.data, 2048u);
    (void)astra_host_client_close(&host);
    return status;
}

static const AstraVfsFilePageSource *file_page_acquire(void *context,
    uint32_t cookie)
{
    const AstraVfsFilePageSource *source = NULL;
    (void)context;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return NULL;
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        if (entry->source.cookie == cookie &&
            entry->source.retained != ASTRA_VFS_FILE_INVALID) {
            ++entry->pins;
            ++entry->owner->cache_stats[CACHE_PAGE_REQUESTS];
            source = &entry->source;
            break;
        }
    astra_vfs_state_lock_release(&state_lock);
    return source;
}

static void file_page_release(void *context,
    const AstraVfsFilePageSource *source)
{
    (void)context;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return;
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        if (&entry->source == source) {
            --entry->pins;
            break;
        }
    astra_vfs_state_lock_release(&state_lock);
}

static void file_pager_run(uint32_t argument)
{
    (void)argument;
    while (!file_pager_stop) {
        uint32_t status = astra_wait_one(file_pager.receive,
            astra_clock_monotonic() + UINT64_C(1000000000), NULL);
        if (file_pager_stop) break;
        if (status == ASTRA_SYSCALL_CANCELLED ||
            status == ASTRA_SYSCALL_TIMED_OUT) continue;
        if (status != ASTRA_SYSCALL_OK) {
            file_pager.stalled = status;
            break;
        }
        (void)astra_vfs_file_pager_pump(&file_pager, 8u);
        if (file_pager.stalled == ASTRA_SYSCALL_RESOURCE_LIMIT) {
            __atomic_store_n(&file_pager_failed, file_pager.stalled, __ATOMIC_RELEASE);
            while (!file_pager_stop && __atomic_load_n(&file_pager_failed,
                    __ATOMIC_ACQUIRE) == ASTRA_SYSCALL_RESOURCE_LIMIT) {
                status = astra_futex_wait(&file_pager_failed,
                    ASTRA_SYSCALL_RESOURCE_LIMIT, ASTRA_DEADLINE_FOREVER);
                if (status != ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_WOULD_BLOCK &&
                    status != ASTRA_SYSCALL_CANCELLED) break;
            }
            if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_WOULD_BLOCK ||
                status == ASTRA_SYSCALL_CANCELLED)
                file_pager.stalled = 0u;
            else file_pager.stalled = status;
        }
        if (file_pager.stalled != 0u) break;
    }
    if (file_pager.stalled != 0u)
        __atomic_store_n(&file_pager_failed, file_pager.stalled,
                         __ATOMIC_RELEASE);
}

static uint32_t file_pager_start(void)
{
    uint32_t scratch = 0u, mapped = 0u, sync_send = 0u;
    uint32_t status = astra_rt_port_create(queue_messages,
        queue_messages * (uint32_t)sizeof(AstraPagerPageRequest),
        &file_pager.receive, &file_pager_send);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_LIMIT;
    status = astra_rt_port_create(queue_messages,
        queue_messages * (uint32_t)sizeof(AstraPagerSyncRequest),
        &file_sync_receive, &sync_send);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_area_create(ASTRA_MEMORY_PAGE_SIZE,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER, &scratch);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_area_map(scratch,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &file_pager_page, &mapped);
    if (status != ASTRA_SYSCALL_OK || mapped < ASTRA_MEMORY_PAGE_SIZE ||
        !astra_vfs_file_pager_init(&file_pager, file_pager.receive,
            scratch, file_pager_page, mapped,
            file_page_acquire, file_page_release, NULL)) {
        if (file_pager_page != NULL) (void)astra_rt_area_unmap(file_pager_page);
        if (scratch != 0u) (void)astra_close(scratch);
        (void)astra_close(file_pager_send);
        (void)astra_close(file_pager.receive);
        if (sync_send != 0u) (void)astra_close(sync_send);
        if (file_sync_receive != 0u) (void)astra_close(file_sync_receive);
        file_sync_receive = 0u;
        file_pager_send = file_pager.receive = 0u;
        file_pager_page = NULL;
        return ASTRA_VFS_ERR_LIMIT;
    }
    file_pager.sync_send = sync_send;
    AstraThreadStart start = {file_pager_run, 0u};
    status = astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_ADMINISTER, &file_pager_thread, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_rt_area_unmap(file_pager_page);
        (void)astra_close(file_pager.data_area);
        (void)astra_close(file_pager_send);
        (void)astra_close(file_pager.receive);
        (void)astra_close(file_pager.sync_send);
        (void)astra_close(file_sync_receive);
        file_sync_receive = 0u;
        file_pager = (AstraVfsFilePager){0};
        file_pager_page = NULL;
        file_pager_send = 0u;
        return ASTRA_VFS_ERR_LIMIT;
    }
    return ASTRA_VFS_OK;
}

static void file_pager_stop_and_close(void)
{
    file_pager_stop = 1u;
    if (file_pager_thread != 0u) {
        (void)astra_rt_cancel_wait(file_pager_thread);
        (void)astra_wait_one_restart(file_pager_thread, ASTRA_DEADLINE_FOREVER, NULL);
        (void)astra_close(file_pager_thread);
        file_pager_thread = 0u;
    }
    if (file_pager_page != NULL) (void)astra_rt_area_unmap(file_pager_page);
    if (file_pager.data_area != 0u) (void)astra_close(file_pager.data_area);
    if (file_pager_send != 0u) (void)astra_close(file_pager_send);
    if (file_pager.receive != 0u) (void)astra_close(file_pager.receive);
    if (file_pager.sync_send != 0u) (void)astra_close(file_pager.sync_send);
    if (file_sync_receive != 0u) (void)astra_close(file_sync_receive);
    file_sync_receive = 0u;
    file_pager = (AstraVfsFilePager){0};
    file_pager_page = NULL;
    file_pager_send = 0u;
}

/* The caller pins the source while reclaiming other idle area handles. */
static uint32_t file_area_duplicate(uint32_t source, uint32_t rights, uint32_t *area)
{
    for (uint32_t attempt = 0u; ; ++attempt) {
        uint32_t status = astra_rt_handle_duplicate(source, rights, area);
        if (status == ASTRA_SYSCALL_OK) return ASTRA_VFS_OK;
        int limited = status == ASTRA_SYSCALL_RESOURCE_LIMIT ||
                      status == ASTRA_SYSCALL_OUT_OF_MEMORY;
        if (!hostfs_reclaim_once(attempt, limited))
            return limited ? ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_IO;
    }
}

static uint32_t file_area_resize(const AstraVfsFilePageSource *source,
    uint32_t area, uint64_t size)
{
    uint64_t bytes = size > source->base ? size - source->base : 0u;
    uint32_t status = astra_rt_area_resize_backing(area,
        bytes < source->span ? (uint32_t)bytes : source->span);
    return status == ASTRA_SYSCALL_OK ? ASTRA_VFS_OK :
        astra_vfs_host_status_from_syscall(status);
}

/* The native caller holds node I/O across validation and extent publication. */
static uint32_t hostfs_stat(void *context, const AstraVfsOpenFile *file,
    AstraVfsNodeInfo *info)
{
    (void)context;
    if (!astra_vfs_open_file_active(file)) return ASTRA_VFS_ERR_ACCESS;
    HostfsBackend *backend = file->backend.context;
    uint64_t nodes = 0u, entries = 0u;
    uint32_t status = astra_vfs_host_change_snapshot(&backend->owner->backend.provider,
        &nodes, &entries);
    if (status != ASTRA_VFS_OK) return status;
    if (!file_areas_observe_nodes(backend->owner, nodes)) return ASTRA_VFS_ERR_BUSY;
    status = astra_vfs_host_ops()->stat_node(file->backend.context, file->node, info);
    if (status != ASTRA_VFS_OK) return status;
    if (info->kind != ASTRA_VFS_KIND_FILE || info->size > INT64_MAX ||
        (astra_vfs_node_identity_equal(&file->io->identity, &file->io->identity) &&
         !astra_vfs_node_identity_equal(&file->io->identity, &info->meta)))
        return ASTRA_VFS_ERR_LOST;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    AstraVfsFileCache *cache = file->io->cache;
    int reset = cache != NULL && !astra_vfs_node_revision_equal(&cache->backing, info);
    int refresh = reset;
    if (cache == NULL)
        for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
            if (entry->io == file->io && entry->source.cookie != 0u &&
                (entry->stale || !astra_vfs_node_revision_equal(&entry->cache.backing, info))) {
                refresh = 1;
                break;
            }
    for (HostfsFileArea *entry = file_areas;
            refresh && status == ASTRA_VFS_OK && entry != NULL; entry = entry->next) {
        if (entry->io != file->io || entry->source.cookie == 0u) continue;
        uint32_t released = 0u;
        uint32_t reclaimed = astra_rt_area_reclaim(entry->area, 0u,
            entry->source.span, &released);
        if (reclaimed != ASTRA_SYSCALL_OK) {
            status = reclaimed == ASTRA_SYSCALL_WOULD_BLOCK ?
                ASTRA_VFS_ERR_BUSY : ASTRA_VFS_ERR_IO;
            break;
        }
    }
    /* Refuse dirty conflicts across every chunk before revoking private copies. */
    for (HostfsFileArea *entry = file_areas;
            refresh && status == ASTRA_VFS_OK && entry != NULL; entry = entry->next)
        if (entry->io == file->io && entry->source.cookie != 0u)
            status = file_area_resize(&entry->source, entry->area, info->size);
    if (status == ASTRA_VFS_OK) {
        /* Every owner is clean/reclaimed before adopting an external revision. */
        if (reset) *cache = (AstraVfsFileCache){.backing = *info,
            .size = info->size, .writer = cache->writer,
            .external_writes = cache->external_writes};
        for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
            if (entry->io == file->io && entry->source.cookie != 0u) {
                entry->cache.backing = *info;
                entry->stale = 0u;
            }
    } else astra_vfs_file_cache_record_status(cache, status);
    astra_vfs_state_lock_release(&state_lock);
    return status;
}

static void file_area_release(HostfsFileArea *entry)
{
    if (!astra_vfs_state_lock_acquire(&state_lock)) return;
    --entry->pins;
    astra_vfs_state_lock_release(&state_lock);
}

/* Rotate the existing owner list; no second writeback queue owns these nodes. */
static void file_area_rotate(HostfsFileArea *entry)
{
    HostfsFileArea **slot = &file_areas;
    while (*slot != entry) slot = &(*slot)->next;
    *slot = entry->next;
    while (*slot != NULL) slot = &(*slot)->next;
    entry->next = NULL;
    *slot = entry;
}

static uint32_t file_area_get(const AstraVfsOpenFile *file,
    const AstraVfsNodeInfo *info, uint64_t base, HostfsFileArea **result)
{
    HostfsBackend *backend = file->backend.context;
    HostfsGrant *grant = backend->grant;
    AstraVfsFilePageSource prepared = {0};
    HostfsFileArea *entry = NULL, *cached;
    uint32_t status, cookie;
    uint64_t owner_base = base - base % ASTRA_AREA_SIZE_MAX;

    *result = NULL;
    if (file_pager_thread == 0u ||
        __atomic_load_n(&file_pager_failed, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_VFS_ERR_BUSY;
select_owner:
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    for (cached = file_areas; cached != NULL; cached = cached->next)
        if (cached->io == file->io && cached->owner == grant->owner &&
            cached->provider_epoch == bound_provider_epoch &&
            cached->area != 0u && cached->source.cookie != 0u &&
            cached->source.retained != ASTRA_VFS_FILE_INVALID &&
            cached->source.base == owner_base) {
            ++grant->owner->cache_stats[CACHE_HITS];
            ++cached->pins;
            *result = cached;
            if (entry != NULL) entry->pins = 0u;
            astra_vfs_state_lock_release(&state_lock);
            return ASTRA_VFS_OK;
        }
    if (entry != NULL) {
        entry->source = prepared;
        entry->cache.backing = *info;
        entry->provider_epoch = bound_provider_epoch;
        ++grant->owner->cache_stats[CACHE_CREATES];
        *result = entry;
        astra_vfs_state_lock_release(&state_lock);
        return ASTRA_VFS_OK;
    }
    astra_vfs_state_lock_release(&state_lock);
    for (uint32_t attempt = 0u; ; ++attempt) {
        entry = astra_runtime_callocate(1u, sizeof(*entry));
        if (entry != NULL) break;
        if (!hostfs_reclaim_once(attempt, 1)) return ASTRA_VFS_ERR_LIMIT;
    }
    entry->owner = grant->owner;
    entry->grant = grant;
    entry->io = file->io;
    entry->source.service = &grant->owner->service;
    status = astra_vfs_service_retain_active(entry->source.service,
        file, &entry->source.retained);
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(entry);
        return status;
    }
    /* An unpublished source owns its native reference but cannot serve faults. */
    entry->pins = 1u;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    HostfsFileArea **tail = &file_areas;
    while (*tail != NULL) tail = &(*tail)->next;
    *tail = entry;
    if (file_pager_cookie == UINT32_MAX) {
        astra_vfs_state_lock_release(&state_lock);
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    cookie = file_pager_cookie++;
    astra_vfs_state_lock_release(&state_lock);
    for (uint32_t attempt = 0u; ; ++attempt) {
        status = astra_rt_area_create_flagged(ASTRA_AREA_SIZE_MAX,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER |
            ASTRA_RIGHT_ADMINISTER, ASTRA_AREA_CREATE_RESERVED, &entry->area);
        if (status == ASTRA_SYSCALL_OK) break;
        if (!hostfs_reclaim_once(attempt,
                status == ASTRA_SYSCALL_RESOURCE_LIMIT || status == ASTRA_SYSCALL_OUT_OF_MEMORY)) {
            status = ASTRA_VFS_ERR_LIMIT;
            goto done;
        }
    }
    if (astra_rt_area_bind_pager(entry->area, file_pager_send, cookie) != ASTRA_SYSCALL_OK) {
        status = ASTRA_VFS_ERR_IO;
        goto done;
    }
    if (!astra_vfs_file_page_source_init(&prepared,
        &grant->owner->service, entry->source.retained,
        file->flags & ASTRA_VFS_OPEN_READ ? ASTRA_VFS_OPEN_READ : ASTRA_VFS_OPEN_WRITE,
        &info->meta,
        owner_base, ASTRA_AREA_SIZE_MAX, cookie)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    status = file_area_resize(&prepared, entry->area,
        file->io->cache != NULL ? file->io->cache->size : info->size);
    if (status != ASTRA_VFS_OK) goto done;
    goto select_owner;

done:
    file_area_release(entry);
    return status;
}

static uint32_t hostfs_area(void *context, const AstraVfsOpenFile *file,
    AstraVfsFile *owned, const AstraVfsNodeInfo *information,
    uint64_t base, uint32_t span, uint32_t *area)
{
    HostfsGrant *admission = context;
    HostfsFileArea *entry;
    (void)span;
    *area = 0u;
    if (admission->retiring) return ASTRA_VFS_ERR_BUSY;
    const AstraVfsNodeInfo *backing = file->io->cache != NULL ?
        &file->io->cache->backing : information;
    uint32_t status = file_area_get(file, backing, base, &entry);
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("HostFS map owner", status);
        return status;
    }
    uint32_t rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;
    if ((file->flags & ASTRA_VFS_OPEN_WRITE) != 0u) {
        AstraVfsFileCache snapshot;
        status = astra_vfs_service_cache_prepare(entry->source.service,
            file, owned, ASTRA_VFS_CACHE_EXTERNAL_WRITES, &entry->cache, &snapshot);
        if (status != ASTRA_VFS_OK) {
            file_area_release(entry);
            return status;
        }
        rights |= ASTRA_RIGHT_WRITE;
    }
    status = file_area_duplicate(entry->area, rights, area);
    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("HostFS map duplicate", status);
    file_area_release(entry);
    return status;
}

static uint32_t hostfs_file_area(void *context,
    const AstraVfsPortGrant *port, uint32_t requester, uint32_t session,
    AstraVfsFile file, uint64_t base, uint32_t span, uint32_t *area,
    uint64_t *file_size)
{
    HostfsGrant *grant = (HostfsGrant *)(void *)port;
    (void)context;
    *area = 0u;
    *file_size = 0u;
    if (!astra_vfs_service_session_bound(&grant->owner->service,
            session, requester, port->id))
        return ASTRA_VFS_ERR_ACCESS;
    uint32_t status = astra_vfs_service_file_area(&grant->owner->service,
        session, file, base, span, hostfs_area, grant, area, file_size);
    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("HostFS map source", status);
    file_areas_maintain(0);
    return status;
}

static int file_areas_invalidate(HostfsVolume *volume, uint32_t kind,
    uint64_t inode)
{
    if (!astra_vfs_state_lock_acquire(&state_lock)) return 0;
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        if (entry->owner == volume && entry->source.cookie != 0u &&
            (kind != ASTRA_HOST_FS_CHANGE_NODE ||
             !astra_vfs_node_identity_equal(&entry->source.identity,
                                            &entry->source.identity) ||
             entry->source.identity.inode == inode)) {
            if (kind == ASTRA_HOST_FS_CHANGE_GAP ||
                kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED)
                entry->check_links = 1u;
            if (kind != ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED) {
                ++volume->cache_stats[CACHE_INVALIDATIONS];
                entry->stale = 1u;
            }
        }
    astra_vfs_state_lock_release(&state_lock);
    return 1;
}

static int file_areas_observe_nodes(HostfsVolume *volume, uint64_t nodes)
{
    if (nodes < volume->observed_cache_nodes) {
        if (!file_areas_invalidate(volume, ASTRA_HOST_FS_CHANGE_GAP, 0u))
            return 0;
        volume->observed_cache_sequence = 0u;
        volume->observed_cache_nodes = nodes;
    }
    for (uint32_t limit = 0u; limit < 8u; ++limit) {
        AstraHostFsChangeRecord record = {0};
        uint64_t cursor = volume->observed_cache_sequence;
        uint32_t kind;
        uint32_t status = astra_vfs_host_change_next(&volume->backend.provider, &cursor,
            &record, &kind);
        if (status != ASTRA_VFS_OK) {
            kind = ASTRA_HOST_FS_CHANGE_GAP;
            cursor = volume->observed_cache_sequence;
        }
        if (kind == ASTRA_HOST_FS_CHANGE_NODE &&
            record.nodes > volume->observed_cache_nodes &&
            !file_areas_invalidate(volume, kind, record.inode))
            return 0;
        if (kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED &&
            !file_areas_invalidate(volume, kind, 0u))
            return 0;
        if (kind == ASTRA_HOST_FS_CHANGE_GAP) {
            uint64_t current = nodes, entries;
            int known = status == ASTRA_VFS_OK &&
                astra_vfs_host_change_snapshot(&volume->backend.provider, &current,
                    &entries) == ASTRA_VFS_OK;
            /* A covered GAP still requires link retirement, not another
               eviction of pages filled after its data generation. */
            uint32_t invalidate = known && current == volume->observed_cache_nodes ?
                ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED : ASTRA_HOST_FS_CHANGE_GAP;
            if (!file_areas_invalidate(volume, invalidate, 0u)) return 0;
            if (known) volume->observed_cache_nodes = current;
            else if (nodes > volume->observed_cache_nodes)
                volume->observed_cache_nodes = nodes;
        } else if (kind == ASTRA_HOST_FS_CHANGE_NONE &&
                   nodes > volume->observed_cache_nodes) {
            if (!file_areas_invalidate(volume, ASTRA_HOST_FS_CHANGE_GAP, 0u))
                return 0;
            if (nodes > volume->observed_cache_nodes)
                volume->observed_cache_nodes = nodes;
        } else if (kind != ASTRA_HOST_FS_CHANGE_NONE &&
                   record.nodes > volume->observed_cache_nodes) {
            volume->observed_cache_nodes = record.nodes;
        }
        volume->observed_cache_sequence = cursor;
        if (kind == ASTRA_HOST_FS_CHANGE_GAP ||
            kind == ASTRA_HOST_FS_CHANGE_NONE)
            return 1;
    }
    /* The full invalidation covers queued records through this snapshot. */
    if (nodes > volume->observed_cache_nodes) {
        if (!file_areas_invalidate(volume, ASTRA_HOST_FS_CHANGE_GAP, 0u))
            return 0;
        volume->observed_cache_nodes = nodes;
    }
    return 1;
}

static void file_areas_maintain(int discard_idle)
{
    HostfsFileArea **slot = &file_areas;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return;
    while (*slot != NULL) {
        HostfsFileArea *entry = *slot;
        /* Reclaim may run inside a cache callback; never reenter its node I/O. */
        if (entry->io != NULL && entry->io->active != NULL) {
            slot = &entry->next;
            continue;
        }
        uint32_t handles = 0u, mappings = 0u, children = 0u;
        uint32_t status = ASTRA_SYSCALL_OK;
        int idle = 0;
        int unidentified = !astra_vfs_node_identity_equal(
            &entry->source.identity, &entry->source.identity);
        int link_retirement = (entry->cache.backing.meta.valid & ASTRA_VFS_META_NLINK) == 0u ||
            entry->cache.backing.meta.nlink == 0u;
        int discard = discard_idle || entry->grant == NULL || entry->grant->retiring ||
            entry->provider_epoch != bound_provider_epoch ||
            entry->source.retained == ASTRA_VFS_FILE_INVALID || entry->source.cookie == 0u ||
            unidentified || link_retirement;
        if (entry->source.cookie == 0u)
            idle = entry->pins == 0u;
        else if (discard || entry->check_links) {
            status = astra_rt_area_references(entry->area,
                &handles, &mappings, &children);
            idle = status == ASTRA_SYSCALL_OK && handles == 1u &&
                mappings == 0u && children == 0u && entry->pins == 0u;
        }
        if (idle && !discard && entry->check_links) {
            AstraVfsNodeInfo info = {0};
            ++entry->pins;
            astra_vfs_state_lock_release(&state_lock);
            uint32_t checked = astra_vfs_service_stat_retained(
                entry->source.service, entry->source.retained, &info);
            if (!astra_vfs_state_lock_acquire(&state_lock)) return;
            --entry->pins;
            link_retirement = checked != ASTRA_VFS_OK ||
                (info.meta.valid & ASTRA_VFS_META_NLINK) == 0u || info.meta.nlink == 0u;
            discard = link_retirement;
            entry->check_links = discard ? 1u : 0u;
            status = astra_rt_area_references(entry->area, &handles, &mappings, &children);
            idle = status == ASTRA_SYSCALL_OK && handles == 1u &&
                mappings == 0u && children == 0u && entry->pins == 0u;
        }
        if (idle && discard && entry->io != NULL && entry->io->cache == &entry->cache) {
            /* Detach shared metadata only after every chunk has no client or dirty pin. */
            for (HostfsFileArea *sibling = file_areas; sibling != NULL; sibling = sibling->next) {
                if (sibling->io != entry->io || sibling == entry) continue;
                status = astra_rt_area_references(sibling->area, &handles, &mappings, &children);
                if (status != ASTRA_SYSCALL_OK || handles != 1u || mappings != 0u ||
                    children != 0u || sibling->pins != 0u) { idle = 0; break; }
            }
            if (idle) {
                AstraVfsFileCache snapshot;
                uint32_t sequence;
                ++entry->pins;
                astra_vfs_state_lock_release(&state_lock);
                status = astra_vfs_service_cache_snapshot(entry->source.service,
                    entry->source.retained, &snapshot, &sequence);
                if (status == ASTRA_VFS_OK)
                    status = astra_vfs_service_cache_retire(entry->source.service,
                        entry->source.retained, sequence);
                if (!astra_vfs_state_lock_acquire(&state_lock)) return;
                --entry->pins;
                status = astra_rt_area_references(entry->area, &handles, &mappings, &children);
                idle = status == ASTRA_SYSCALL_OK && handles == 1u && mappings == 0u &&
                    children == 0u && entry->pins == 0u && entry->io->cache != &entry->cache;
            }
        }
        if (idle && discard) {
            entry->retired_grant |= entry->grant != NULL && entry->grant->retiring;
            entry->reclaim_requested |= discard_idle != 0;
            *slot = entry->next;
            hostfs_holders_changed(NULL);
            astra_vfs_state_lock_release(&state_lock);
            if (entry->source.retained != ASTRA_VFS_FILE_INVALID) {
                status = astra_vfs_service_release_retained(
                    entry->source.service, &entry->source.retained);
                if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_BAD_HANDLE &&
                    status != ASTRA_VFS_ERR_LOST)
                    (void)astra_log_failure("HostFS retained close", status);
            }
            if (entry->source.retained == ASTRA_VFS_FILE_INVALID) {
                entry->grant = NULL;
                if (entry->area != 0u) {
                    status = astra_close(entry->area);
                    if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_INVALID_HANDLE)
                        entry->area = 0u;
                }
            }
            if (entry->source.retained == ASTRA_VFS_FILE_INVALID && entry->area == 0u) {
                if (entry->source.cookie != 0u) {
                    ++entry->owner->cache_stats[CACHE_CLOSES];
                    entry->owner->cache_stats[CACHE_IDENTITY_RETIREMENTS] += unidentified;
                    entry->owner->cache_stats[CACHE_LINK_RETIREMENTS] += link_retirement;
                    entry->owner->cache_stats[CACHE_GRANT_RETIREMENTS] += entry->retired_grant;
                    entry->owner->cache_stats[CACHE_REQUESTED_RETIREMENTS] += entry->reclaim_requested;
                }
                astra_runtime_deallocate(entry);
            } else {
                if (!astra_vfs_state_lock_acquire(&state_lock)) return;
                entry->next = file_areas;
                file_areas = entry;
                astra_vfs_state_lock_release(&state_lock);
                return;
            }
            if (!astra_vfs_state_lock_acquire(&state_lock)) return;
            slot = &file_areas;
            continue;
        }
        if (entry->source.cookie != 0u && entry->stale && status == ASTRA_SYSCALL_OK) {
            AstraVfsNodeInfo info;
            ++entry->pins;
            astra_vfs_state_lock_release(&state_lock);
            (void)astra_vfs_service_stat_retained(entry->source.service,
                entry->source.retained, &info);
            if (!astra_vfs_state_lock_acquire(&state_lock)) return;
            --entry->pins;
        }
        slot = &entry->next;
    }
    astra_vfs_state_lock_release(&state_lock);
}

static uint32_t hostfs_io(const AstraVfsOpenFile *file, AstraVfsFile *owned,
    uint64_t offset, uint32_t flags, uint64_t max_end,
    void *buffer, uint32_t length, uint32_t *moved, uint64_t *position)
{
    const int writing = owned != NULL;
    const uint32_t allowed = ASTRA_VFS_IO_SEQUENTIAL |
        (writing ? ASTRA_VFS_WRITE_APPEND : 0u);
    AstraVfsNodeInfo info;
    AstraVfsReply reply = {0};
    uint64_t cursor = offset, size, modified_ns = 0u;
    uint32_t status, wanted = length;
    *moved = 0u; *position = offset;
    if (!astra_vfs_open_file_active(file) || (flags & ~allowed) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u) {
        AstraVfsRequest request = {.flags = 1u};
        status = astra_vfs_backend_description_operation(&file->backend,
            file->node, ASTRA_VFS_OP_SEEK, &request, &reply, NULL);
        if (status != ASTRA_VFS_OK) return status;
        offset = cursor = reply.position;
    }
    *position = offset;
    if (length == 0u) return ASTRA_VFS_OK;
    status = hostfs_stat(NULL, file, &info);
    if (status != ASTRA_VFS_OK) return status;
    size = file->io->cache != NULL ? file->io->cache->size : info.size;
    if (writing) {
        AstraVfsRequest request = {0};
        status = astra_vfs_backend_description_operation(&file->backend,
            file->node, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply, NULL);
        if (status != ASTRA_VFS_OK) return status;
        if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u ||
            ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
             (reply.open_flags & ASTRA_VFS_OPEN_APPEND) != 0u)) offset = size;
        if (max_end > INT64_MAX) max_end = INT64_MAX;
        if (offset >= max_end) return ASTRA_VFS_ERR_FILE_TOO_LARGE;
        if (wanted > max_end - offset) wanted = (uint32_t)(max_end - offset);
        if (astra_clock_realtime(&modified_ns) != ASTRA_SYSCALL_OK)
            return ASTRA_VFS_ERR_IO;
    } else {
        if (offset >= size) return ASTRA_VFS_OK;
        if (wanted > size - offset) wanted = (uint32_t)(size - offset);
    }
    while (*moved < wanted) {
        HostfsFileArea *entry = NULL;
        uint64_t at = offset + *moved;
        uint64_t page_base = at - at % ASTRA_MEMORY_PAGE_SIZE;
        uint32_t within = (uint32_t)(at % ASTRA_MEMORY_PAGE_SIZE);
        uint32_t chunk = ASTRA_MEMORY_PAGE_SIZE - within;
        if (chunk > wanted - *moved) chunk = wanted - *moved;
        status = file_area_get(file, &info, at, &entry);
        if (status != ASTRA_VFS_OK) break;
        uint32_t relative = (uint32_t)(at - entry->source.base);
        if (writing && file->io->cache == NULL) {
            AstraVfsFileCache snapshot;
            status = astra_vfs_service_cache_prepare(entry->source.service,
                file, owned, 0u, &entry->cache, &snapshot);
            if (status != ASTRA_VFS_OK) { file_area_release(entry); break; }
        }
        for (;;) {
            uint32_t result = writing ?
                astra_rt_area_write_pager(entry->area, relative,
                    (const uint8_t *)buffer + *moved, chunk) :
                astra_rt_area_read_resident(entry->area, relative,
                    (uint8_t *)buffer + *moved, chunk);
            if (result != ASTRA_SYSCALL_WOULD_BLOCK) {
                status = result == ASTRA_SYSCALL_OK ? ASTRA_VFS_OK :
                    astra_vfs_host_status_from_syscall(result);
                break;
            }
            memset(cache_page, 0, sizeof(cache_page));
            if (!writing || within != 0u || chunk != ASTRA_MEMORY_PAGE_SIZE) {
                uint32_t valid = 0u;
                status = astra_vfs_backend_read_page(&file->backend, file->node,
                    writing ? ASTRA_VFS_OPEN_WRITE : ASTRA_VFS_OPEN_READ,
                    &entry->source.identity,
                    file->io->cache != NULL ? &file->io->cache->backing : &info,
                    page_base, cache_page, &valid);
                if (status != ASTRA_VFS_OK) break;
            }
            if (writing) {
                memcpy(cache_page + within, (const uint8_t *)buffer + *moved, chunk);
                result = astra_rt_area_publish_dirty_page(entry->area,
                    relative - within, cache_page);
            } else {
                uint32_t valid = size - page_base < ASTRA_MEMORY_PAGE_SIZE ?
                    (uint32_t)(size - page_base) : ASTRA_MEMORY_PAGE_SIZE;
                result = astra_rt_area_publish_clean_page(entry->area,
                    relative - within, cache_page, valid);
            }
            if (result == ASTRA_SYSCALL_WOULD_BLOCK) continue;
            if (result != ASTRA_SYSCALL_OK) {
                status = astra_vfs_host_status_from_syscall(result);
                break;
            }
            if (writing) { status = ASTRA_VFS_OK; break; }
            /* Clean frames can be reclaimed before the resident copy; retry. */
        }
        if (status == ASTRA_VFS_OK && writing) {
            if (at + chunk > size) {
                size = at + chunk;
                if (!astra_vfs_state_lock_acquire(&state_lock)) {
                    file_area_release(entry);
                    return ASTRA_VFS_ERR_IO;
                }
                for (HostfsFileArea *sibling = file_areas; sibling != NULL; sibling = sibling->next) {
                    if (sibling->io != file->io || sibling->source.cookie == 0u) continue;
                    status = file_area_resize(&sibling->source, sibling->area, size);
                    if (status != ASTRA_VFS_OK) break;
                }
                astra_vfs_state_lock_release(&state_lock);
            }
            if (status == ASTRA_VFS_OK)
                status = astra_vfs_service_cache_publish(entry->source.service,
                    file, size, modified_ns);
        }
        file_area_release(entry);
        if (status != ASTRA_VFS_OK) break;
        *moved += chunk;
    }
    *position = offset + *moved;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u && *moved != 0u) {
        /* Node I/O also serializes shared-description cursor commits. */
        AstraVfsRequest request = {.offset = *position};
        uint32_t advanced = astra_vfs_backend_description_operation(&file->backend,
            file->node, ASTRA_VFS_OP_SEEK, &request, &reply, NULL);
        if (advanced != ASTRA_VFS_OK) status = advanced;
    } else if (*moved == 0u) *position = cursor;
    return status;
}

static uint32_t hostfs_write(void *context, const AstraVfsOpenFile *file,
    AstraVfsFile *owned, uint64_t offset, uint32_t flags, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved, uint64_t *position)
{
    (void)context;
    return hostfs_io(file, owned, offset, flags, max_end, (void *)buffer,
        length, moved, position);
}

static uint32_t hostfs_read(void *context, const AstraVfsOpenFile *file,
    uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    (void)context;
    return hostfs_io(file, NULL, offset, flags, 0u, buffer, length, moved, position);
}

static uint32_t hostfs_sync(void *context, const AstraVfsOpenFile *file,
    uint32_t *remaining)
{
    (void)context;
    AstraVfsNodeInfo info;
    int full = remaining == NULL;
    uint32_t complete = UINT32_MAX;
    if (full) remaining = &complete;
    if (*remaining == 0u) return ASTRA_VFS_ERR_BUSY;
    uint32_t status = hostfs_stat(NULL, file, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next) {
        if (entry->io != file->io || entry->source.cookie == 0u) continue;
        if (full) entry->writeback_cursor = 0u;
        ++entry->pins;
        astra_vfs_state_lock_release(&state_lock);
        status = astra_vfs_file_pager_writeback(&entry->source, file,
            file->io->cache->writer, entry->area, cache_sync_page,
            remaining, &entry->writeback_cursor);
        if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
        --entry->pins;
        /* Own backing writes are already reflected in the shared revision. */
        for (HostfsFileArea *sibling = file_areas; sibling != NULL; sibling = sibling->next)
            if (sibling->io == file->io)
                sibling->cache.backing = file->io->cache->backing;
        if (status == ASTRA_VFS_OK && *remaining == 0u)
            for (HostfsFileArea *next = entry->next; next != NULL; next = next->next)
                if (next->io == file->io && next->source.cookie != 0u) {
                    status = ASTRA_VFS_ERR_BUSY; break;
                }
        if (status != ASTRA_VFS_OK || *remaining == 0u) {
            if (!full) {
                file_area_rotate(entry);
            }
            break;
        }
    }
    astra_vfs_state_lock_release(&state_lock);
    return status;
}

static const AstraVfsCacheOps hostfs_cache_ops = {
    .read = hostfs_read, .write = hostfs_write, .sync = hostfs_sync,
    .stat = hostfs_stat
};

static uint32_t file_areas_writeback(uint32_t budget)
{
    HostfsFileArea *entry;
    uint32_t status = ASTRA_VFS_OK;
    uint32_t remaining = budget;
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
    entry = file_areas;
    while (entry != NULL) {
        if (entry->io == NULL || entry->io->cache != &entry->cache ||
            entry->cache.writer == ASTRA_VFS_FILE_INVALID) { entry = entry->next; continue; }
        if (entry->io->active != NULL) {
            if (budget == 0u) { status = ASTRA_VFS_ERR_BUSY; break; }
            entry = entry->next;
            continue;
        }
        AstraVfsFile writer = entry->cache.writer;
        uint32_t before = remaining;
        ++entry->pins;
        astra_vfs_state_lock_release(&state_lock);
        status = astra_vfs_service_sync_retained(entry->source.service, writer, budget == 0u ? NULL : &remaining);
        if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_BUSY)
            (void)astra_log_failure("HostFS writeback", status);
        if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
        --entry->pins;
        if (budget != 0u) {
            file_area_rotate(entry);
            /* Failed admission still consumes a turn so no-progress work is bounded. */
            if (remaining == before) --remaining;
            if (remaining == 0u || (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_BUSY)) break;
            entry = file_areas;
            continue;
        }
        if (status != ASTRA_VFS_OK) break;
        entry = entry->next;
    }
    astra_vfs_state_lock_release(&state_lock);
    return status;
}

static void hostfs_backend_ops_init(AstraVfsBackendOps *ops)
{
    *ops = *astra_vfs_host_ops();
    ops->cache_reclaim = hostfs_cache_reclaim;
}

/* Withdraw native admission and sessions first. Retained descriptions keep the
 * private backend alive until their final cache/map reference is released. */
static int grant_retire(uint32_t at)
{
    HostfsGrant *grant = grants[at];
    uint32_t status;
    if (!grant->retiring) {
        grant->retiring = 1u;
        hostfs_holders_changed(NULL);
        astra_vfs_port_service_release_grant(grant_host(grant), grant->port.id);
    }
    if (grant_backend_detach(grant, 0) != ASTRA_VFS_OK) return 0;
    /* Stop further control admission even when provider cleanup must retry. */
    if (grant->port.receive != 0u) {
        status = astra_close(grant->port.receive);
        if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_INVALID_HANDLE)
            grant->port.receive = 0u;
    }
    if (grant->backing_references == 0u && grant->session != 0u) {
        status = close_owned_session(&grant->session);
        if (status != ASTRA_VFS_OK) (void)astra_log_failure("hostfs grant session close", status);
    }
    if (grant->session != 0u || grant->port.receive != 0u || grant->backing_references != 0u)
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
    uint32_t *const sessions[] = {&volume->backend.provider.session, &volume->pending_session};
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

static int volume_referenced(const HostfsVolume *volume)
{
    for (uint32_t at = 0u; at < grant_count; ++at)
        if (grants[at]->owner == volume) return 1;
    for (const HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        if (entry->owner == volume) return 1;
    return 0;
}

static void volume_reap_idle(void)
{
    HostfsVolume **at = &volumes;
    while (*at != NULL) {
        HostfsVolume *volume = *at;
        int held = !volume->dynamic || volume_referenced(volume);
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
    /* Subdirectory authority needs node generation: reopening its spelling
     * must not select a replacement. Whole-volume grants use mount identity. */
    return astra_vfs_node_identity_equal(meta, &grant->root_meta);
}

/* Recovery keeps native grant ports; old captured backend incarnations stay lost. */
static uint32_t hostfs_check_provider(void)
{
    uint32_t epoch = 0u;
    uint32_t status = astra_vfs_host_claim(&transport, transport.generation, 0, &epoch);

    if (status != ASTRA_VFS_OK || epoch == bound_provider_epoch)
        return status;
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next)
        if (!file_areas_invalidate(volume, ASTRA_HOST_FS_CHANGE_GAP, 0u))
            return ASTRA_VFS_ERR_IO;
    file_areas_maintain(1);
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        status = close_owned_session(&volume->backend.provider.session);
        if (status != ASTRA_VFS_OK) {
            volume->backend.provider.lost = 1u;
            return status;
        }
        uint32_t session = 0u;
        status = hostfs_session_open(
            0u, &volume->reference, "/", volume->root_flags, 0u, &session);
        volume->backend.provider.session = session;
        volume->backend.provider.lost = status != ASTRA_VFS_OK;
    }
    for (uint32_t at = 0u; at < grant_count;) {
        HostfsGrant *grant = grants[at];
        if (grant->retiring) { ++at; continue; }
        if (grant_backend_detach(grant, 1) != ASTRA_VFS_OK) return ASTRA_VFS_ERR_IO;
        uint32_t session = 0u;
        uint16_t flags = (grant->port.rights & ASTRA_VFS_GRANT_WRITE) == 0u ?
            ASTRA_HOST_FS_SESSION_READ_ONLY : 0u;

        if (grant->session != 0u) {
            status = close_owned_session(&grant->session);
            if (status != ASTRA_VFS_OK) return status;
        }
        status = hostfs_session_open(
            0u, &grant->volume, grant->root, flags, grant->requester, &session);
        grant->session = session;
        if (status == ASTRA_VFS_OK) status = grant_backend_create(grant, epoch);
        if (status == ASTRA_VFS_OK) {
            AstraVfsNodeMeta meta = {0};
            status = strcmp(grant->root, "/") == 0 ? ASTRA_VFS_OK :
                     grant_root_meta(session, &meta);
            if (status == ASTRA_VFS_OK && strcmp(grant->root, "/") != 0 &&
                !grant_root_matches(grant, &meta))
                status = ASTRA_VFS_ERR_LOST;
            if (status != ASTRA_VFS_OK) {
                grant->port.lost = 1u;
                if (!grant_retire(at)) ++at;
                continue;
            }
        }
        grant->port.lost = status != ASTRA_VFS_OK;
        if (grant->port.lost)
            (void)astra_log_failure(grant->root, status);
        ++at;
    }
    /* Rebind path authority through HELLO; open files keep their lost backend.
     * Recovery runs on the sole control worker, outside retained pager I/O. */
    for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        for (uint32_t index = 0u; index < volume->service.session_capacity; ++index) {
            AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION};
            AstraVfsReply reply;
            uint32_t owner, grant_id, features = 0u;
            if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_VFS_ERR_IO;
            const AstraVfsSessionSlot *slot = &volume->service.sessions[index];
            request.session = slot->closing ? 0u : slot->id;
            owner = slot->owner; grant_id = slot->grant;
            astra_vfs_state_lock_release(&state_lock);
            if (request.session == 0u) continue;
            for (uint32_t at = 0u; at < grant_count; ++at)
                if (grants[at]->port.id == grant_id) features = grants[at]->port.features;
            astra_vfs_service_dispatch_grant(&volume->service, owner, grant_id, features,
                ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
            if (reply.status != ASTRA_VFS_OK) return reply.status;
        }
    }
    bound_provider_epoch = epoch;
    (void)astra_log("hostfs restored provider bindings");
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

static void publish_filesystem_changes(uint32_t messaging)
{
    for (HostfsVolume *volume = volumes; volume != NULL;
         volume = volume->next) {
        if (!volume->service_ready || volume->config_store) continue;
        uint64_t nodes, entries;
        if (astra_vfs_host_change_snapshot(&volume->backend.provider,
                &nodes, &entries) != ASTRA_VFS_OK) {
            file_areas_invalidate(volume, ASTRA_HOST_FS_CHANGE_GAP, 0u);
            continue;
        }
        if (!file_areas_observe_nodes(volume, nodes)) continue;
        for (uint32_t limit = 0u; limit < 8u; ++limit) {
            AstraHostFsChangeRecord record = {0};
            AstraVfsChangeSnapshot snapshot;
            uint64_t cursor = volume->published_change_sequence;
            uint32_t kind;
            uint32_t queued = 0u;
            if (astra_vfs_host_change_next(&volume->backend.provider,
                    &cursor, &record, &kind) != ASTRA_VFS_OK)
                break;
            if (kind == ASTRA_HOST_FS_CHANGE_NODE ||
                kind == ASTRA_HOST_FS_CHANGE_ENTRY_CREATED ||
                kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED) {
                snapshot.nodes = record.nodes;
                snapshot.entries = record.entries;
            } else if (astra_vfs_host_change_snapshot(&volume->backend.provider,
                    &snapshot.nodes, &snapshot.entries) != ASTRA_VFS_OK) {
                break;
            }
            if (kind == ASTRA_HOST_FS_CHANGE_NONE &&
                snapshot.nodes == volume->published_changes.nodes &&
                snapshot.entries == volume->published_changes.entries)
                break;
            if (kind == ASTRA_HOST_FS_CHANGE_GAP &&
                snapshot.nodes == volume->published_changes.nodes &&
                snapshot.entries == volume->published_changes.entries) {
                volume->published_change_sequence = cursor;
                break;
            }
            AstraFilesystemChange change = {
                .version = ASTRA_FILESYSTEM_CHANGE_VERSION,
                .size = sizeof(change),
                .volume = volume->reference,
                .nodes = snapshot.nodes,
                .entries = snapshot.entries,
                .kind = kind == ASTRA_HOST_FS_CHANGE_NODE ?
                    ASTRA_FILESYSTEM_CHANGE_NODE :
                    kind == ASTRA_HOST_FS_CHANGE_ENTRY_CREATED ?
                    ASTRA_FILESYSTEM_CHANGE_ENTRY_CREATED :
                    kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED ?
                    ASTRA_FILESYSTEM_CHANGE_ENTRY_REMOVED :
                    ASTRA_FILESYSTEM_CHANGE_INVALIDATE,
                .inode = kind == ASTRA_HOST_FS_CHANGE_NODE ||
                    kind == ASTRA_HOST_FS_CHANGE_ENTRY_CREATED ? record.inode : 0u,
                .parent_inode = kind == ASTRA_HOST_FS_CHANGE_ENTRY_CREATED ||
                    kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED ?
                    record.parent_inode : 0u
            };
            if (kind == ASTRA_HOST_FS_CHANGE_ENTRY_CREATED ||
                kind == ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED)
                memcpy(change.name, record.name, sizeof(change.name));
            if (snapshot.nodes > volume->observed_cache_nodes &&
                !file_areas_observe_nodes(volume, snapshot.nodes))
                break;
            /* BUSY leaves both sequence and generation pending for retry. */
            AstraResult result = astra_messaging_publish_reliable(messaging,
                ASTRA_FILESYSTEM_CHANGE_TOPIC, &change, sizeof(change),
                filesystem_transaction,
                (AstraMonotonicDeadline)(astra_clock_monotonic() +
                    UINT64_C(20000000)), &queued);
            if (result != ASTRA_OK) break;
            volume->published_changes = snapshot;
            volume->published_change_sequence = cursor;
            ++filesystem_transaction;
            if (filesystem_transaction == 0u) filesystem_transaction = 1u;
            if (kind != ASTRA_HOST_FS_CHANGE_NODE &&
                kind != ASTRA_HOST_FS_CHANGE_ENTRY_CREATED &&
                kind != ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED) break;
        }
    }
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
    volume->backend.owner = volume;
    volume->config_store = strcmp(name, "/config-store") == 0;
    volume->dynamic = dynamic != 0;
    volume->root_flags = (rights & ASTRA_VFS_GRANT_WRITE) != 0u ? 0u : ASTRA_HOST_FS_SESSION_READ_ONLY;
    if (!volume_metric_group(volume, name)) {
        astra_runtime_deallocate(volume);
        return ASTRA_STATUS_INVALID;
    }
    status = hostfs_session_open(0u,
        reference, "/", volume->root_flags, 0u,
        &session);
    volume->backend.provider.session = session;
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_host_init(&volume->backend.provider, &transport, transport.generation, session) ||
         !astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile), &volume->storage, &capacity)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_VFS_OK) {
        hostfs_backend_ops_init(&volume->ops);
        volume->service_ready = astra_vfs_service_init(&volume->service, &volume->ops, &volume->backend,
            volume->sessions, ASTRA_VFS_SESSION_MAX, volume->storage.address, capacity);
        if (!volume->service_ready) status = ASTRA_STATUS_LIMIT;
    }
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_service_set_state_lock(&volume->service, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_service_set_state_wait(&volume->service, astra_vfs_state_futex_wait,
             astra_vfs_state_futex_wake) ||
         !astra_vfs_service_set_owner_sessions(&volume->service, owner_sessions) ||
         !astra_vfs_service_set_owner_files(&volume->service, owner_files) ||
         !astra_vfs_service_set_backend_bindings(&volume->service, &hostfs_backend_bindings, volume) ||
         !astra_vfs_service_set_cache_ops(&volume->service, &hostfs_cache_ops, NULL) ||
         !astra_vfs_service_set_holders_changed(&volume->service, hostfs_holders_changed, NULL)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_VFS_OK)
        status = hostfs_session_open(0u,
            reference, "/", volume->root_flags, requester, &granted_session);
    volume->pending_session = granted_session;
    if (status == ASTRA_VFS_OK)
        status = grant_add(volume, granted_session, rights, reference, "/", requester, send, &grant);
    if (status == ASTRA_VFS_OK) volume->pending_session = 0u;
    if (status == ASTRA_VFS_OK &&
        (!astra_vfs_port_service_init(&volume->port, grant->port.receive, &volume->service) ||
         !astra_vfs_port_service_set_state_lock(&volume->port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock) ||
         !astra_vfs_port_service_set_derive(&volume->port, hostfs_derive, NULL) ||
         (!volume->config_store && !astra_vfs_port_service_set_file_area(
             &volume->port, hostfs_file_area, NULL)) ||
         (volume->config_store && !astra_vfs_port_service_set_config_crypt(
             &volume->port, hostfs_config_crypt, NULL))))
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
        if (!volume_referenced(volume) && volume_release(volume) == ASTRA_VFS_OK)
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
    const AstraVolumeReference *source, int delegates, uint32_t *send)
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
        (delegates ? ASTRA_VFS_GRANT_DELEGATE : 0u);
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
        status = hostfs_session_open(0u, reference, "/",
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

static uint32_t hostfs_image_from_share(void *context, const AstraVfsOpenFile *file)
{
    const HostfsBackend *backend = file->backend.context;
    if (file->kind != ASTRA_VFS_KIND_FILE || (file->flags & ASTRA_VFS_OPEN_READ) == 0u ||
        (file->flags & ASTRA_VFS_OPEN_WRITE) != 0u) return ASTRA_VFS_ERR_ACCESS;
    if (backend == NULL || __atomic_load_n(&backend->provider.lost, __ATOMIC_ACQUIRE))
        return ASTRA_VFS_ERR_LOST;
    uint32_t process = 0u;
    AstraProcessInfo info = {.size = sizeof(info)};
    if (astra_query_abi(NULL, &process, NULL) != ASTRA_SYSCALL_OK ||
        astra_process_info(process, &info) != ASTRA_SYSCALL_OK || info.id == 0u)
        return ASTRA_VFS_ERR_IO;
    AstraVolumeControl control = {.operation = ASTRA_VOLUME_IMAGE, .image_share = file->token};
    return astra_vfs_host_volume_control(&transport, transport.generation,
        info.id, &control, context);
}

static uint32_t hostfs_volume_control(void *context, uint32_t requester,
    const AstraVolumeControl *control, AstraVolumeReference *reference)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    file_areas_maintain(1);
    retire_orphans();
    if (control->operation == ASTRA_VOLUME_IMAGE) {
        for (HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
            status = astra_vfs_service_borrow_share(&volume->service, requester,
                control->image_share, hostfs_image_from_share, reference);
            if (status != ASTRA_VFS_ERR_BAD_HANDLE) return status;
        }
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    return astra_vfs_host_volume_control(&transport, transport.generation,
        requester, control, reference);
}

static uint32_t hostfs_volume_network_source(void *context, uint32_t requester,
    const AstraVolumeNetworkSource *source, uint32_t managed_id,
    AstraVolumeReference *reference)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    file_areas_maintain(1);
    retire_orphans();
    return astra_vfs_host_volume_network_source(&transport, transport.generation,
        requester, source, managed_id, reference);
}

static uint32_t hostfs_volume_mount_auth(void *context, uint32_t requester,
    const AstraVolumeMountAuth *auth, AstraVolumeReference *reference)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    retire_orphans();
    return astra_vfs_host_volume_mount_auth(&transport, transport.generation,
        requester, auth, reference);
}

static uint32_t hostfs_volume_format(void *context, uint32_t requester,
    const AstraVolumeFormatRequest *request, uint32_t *outcome)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    file_areas_maintain(1);
    retire_orphans();
    return astra_vfs_host_volume_format(&transport, transport.generation,
        requester, request, outcome);
}

static uint32_t hostfs_volume_partition(void *context, uint32_t requester,
    const AstraVolumePartitionRequest *request, uint32_t *outcome)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    file_areas_maintain(1);
    retire_orphans();
    return astra_vfs_host_volume_partition(&transport, transport.generation,
        requester, request, outcome);
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

/* Native tables own client identity; provider rows supply the retained name. */
static const AstraVfsOpenFile *usage_file_owner(AstraVolumeUsageRecord *row, uint32_t *owner)
{
    if (row->kind != ASTRA_VOLUME_USAGE_FILE && row->kind != ASTRA_VOLUME_USAGE_DIRECTORY &&
        row->kind != ASTRA_VOLUME_USAGE_SHARE) return 0;
    for (const HostfsVolume *volume = volumes; volume != NULL; volume = volume->next) {
        const AstraVfsService *service = &volume->service;
        for (uint32_t at = 0u; at < service->file_high_water; ++at) {
            const AstraVfsOpenFile *file = &service->files[at];
            int share = file->state == ASTRA_VFS_FILE_SHARED ||
                        file->state == ASTRA_VFS_FILE_TRANSFERRING;
            if (row->kind == ASTRA_VOLUME_USAGE_SHARE ?
                    !share || file->token != row->handle :
                    (file->state != ASTRA_VFS_FILE_OPEN && file->state != ASTRA_VFS_FILE_CLOSING &&
                     file->state != ASTRA_VFS_FILE_DETACHED) || file->node != row->handle)
                continue;
            *owner = 0u;
            for (uint32_t index = 0u; index < service->session_capacity; ++index) {
                const AstraVfsSessionSlot *slot = &service->sessions[index];
                if (slot->id != 0u && slot->id == file->session) {
                    *owner = slot->owner;
                    if (slot->closing) row->flags |= ASTRA_VOLUME_USAGE_CLEANUP_PENDING;
                    break;
                }
            }
            if (astra_vfs_open_file_active(file)) row->flags |= ASTRA_VOLUME_USAGE_ACTIVE;
            return file;
        }
    }
    return 0;
}

/* A double collect brackets the existing streamed usage page. */
static uint32_t usage_check_mappings(const AstraVolumeUsageQuery *query, int refresh)
{
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next) {
        if (entry->area == 0u || entry->source.cookie == 0u) continue;
        if (query->volume.id != 0u &&
            !astra_volume_reference_equal(&query->volume, &entry->owner->reference)) continue;
        uint64_t sequence = refresh ? 0u : entry->usage_mapping_sequence;
        uint32_t process = 0u;
        uint32_t status = astra_rt_area_next_process(entry->area, UINT32_MAX,
            &sequence, &process);
        if (status != ASTRA_SYSCALL_OK ||
            (!refresh && sequence != entry->usage_mapping_sequence)) {
            hostfs_holders_changed(NULL);
            return status == ASTRA_SYSCALL_OK ? ASTRA_STATUS_BUSY :
                astra_vfs_host_status_from_syscall(status);
        }
        if (refresh && sequence != entry->usage_mapping_sequence) {
            entry->usage_mapping_sequence = sequence;
            hostfs_holders_changed(NULL);
        }
    }
    return ASTRA_STATUS_OK;
}

static uint32_t usage_next_file_owner(const AstraVfsOpenFile *file,
    uint32_t direct, uint32_t after, uint32_t *owner)
{
    *owner = direct > after ? direct : 0u;
    if (file == NULL || file->io == NULL) return ASTRA_STATUS_OK;
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next) {
        if (entry->io != file->io || entry->area == 0u || entry->source.cookie == 0u)
            continue;
        uint64_t sequence = entry->usage_mapping_sequence;
        uint32_t process = 0u;
        uint32_t status = astra_rt_area_next_process(entry->area, after, &sequence, &process);
        if (status != ASTRA_SYSCALL_OK || sequence != entry->usage_mapping_sequence) {
            hostfs_holders_changed(NULL);
            return status == ASTRA_SYSCALL_OK ? ASTRA_STATUS_BUSY :
                astra_vfs_host_status_from_syscall(status);
        }
        if (process > after && (*owner == 0u || process < *owner)) *owner = process;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t usage_read_page(const AstraVolumeUsageQuery *query,
    AstraVolumeUsagePage *page)
{
    if (!astra_volume_usage_query_valid(query)) return ASTRA_STATUS_INVALID;
    memset(page, 0, sizeof(*page));
    uint32_t status;
    AstraVolumeUsagePage *raw = astra_runtime_allocate(sizeof(*raw));
    if (raw == NULL) return ASTRA_STATUS_LIMIT;
    AstraVolumeUsageQuery cursor = *query;
    /* A public page may stop between the holders of one retained root. Reread
     * that exact raw root under the same sequence before expanding its tail. */
    if (cursor.after_kind == ASTRA_VOLUME_USAGE_ROOT || cursor.after_kind == ASTRA_VOLUME_USAGE_FILE ||
        cursor.after_kind == ASTRA_VOLUME_USAGE_DIRECTORY || cursor.after_kind == ASTRA_VOLUME_USAGE_SHARE) {
        cursor.after_process = UINT32_MAX;
        if (--cursor.after == 0u) { --cursor.after_kind; cursor.after = UINT64_MAX; }
    }
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
            } else {
                uint32_t owner = row->process;
                const AstraVfsOpenFile *file = usage_file_owner(row, &owner);
                if (row->kind == ASTRA_VOLUME_USAGE_FILE && file != NULL) {
                    uint32_t direct = owner;
                    status = usage_next_file_owner(file, direct, 0u, &owner);
                    if (status != ASTRA_STATUS_OK) goto done;
                    if (owner == 0u) complete = usage_emit(page, query, row, 0u);
                    while (owner != 0u) {
                        complete = usage_emit(page, query, row, owner);
                        if (complete) break;
                        status = usage_next_file_owner(file, direct, owner, &owner);
                        if (status != ASTRA_STATUS_OK) goto done;
                    }
                } else complete = usage_emit(page, query, row, owner);
            }
            if (complete) goto done;
        }
        cursor.volume = raw->volume; cursor.sequence = raw->sequence;
        cursor.catalog_sequence = raw->catalog_sequence;
        cursor.after = raw->next; cursor.after_kind = raw->next_kind; cursor.after_process = raw->next_process;
    } while (raw->next != 0u);
done:
    astra_runtime_deallocate(raw);
    return status;
}

static uint32_t usage_begin(const AstraVolumeUsageQuery *query)
{
    uint32_t status = usage_check_mappings(query, 1);
    if (status == ASTRA_STATUS_OK && usage_holders_changed != 0u) {
        status = astra_vfs_host_usage_holders_changed(&transport, transport.generation);
        if (status == ASTRA_STATUS_OK) usage_holders_changed = 0u;
    }
    return status;
}

static uint32_t hostfs_read_usage(const AstraVolumeUsageQuery *query,
    AstraVolumeUsagePage *page)
{
    if (!astra_volume_usage_query_valid(query)) return ASTRA_STATUS_INVALID;
    memset(page, 0, sizeof(*page));
    file_areas_maintain(1);
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_STATUS_BUSY;
    uint32_t status = usage_begin(query);
    if (status == ASTRA_STATUS_OK) status = usage_read_page(query, page);
    if (status == ASTRA_STATUS_OK) status = usage_check_mappings(query, 0);
    astra_vfs_state_lock_release(&state_lock);
    return status;
}

/* Count and fill under one existing ownership lock. The first pass admits
 * caller storage; the second retains that exact provider/resource generation. */
static uint32_t hostfs_read_usage_snapshot(const AstraVolumeReference *volume,
    AstraVolumeUsageRecord *records, uint32_t capacity, AstraVolumeUsageSnapshotInfo *info)
{
    memset(info, 0, sizeof(*info));
    AstraVolumeUsageQuery initial = {.version = ASTRA_VOLUME_USAGE_VERSION,
        .size = sizeof(initial), .volume = *volume};
    if (!astra_volume_usage_query_valid(&initial) || (capacity != 0u && records == NULL))
        return ASTRA_STATUS_INVALID;
    file_areas_maintain(1);
    if (!astra_vfs_state_lock_acquire(&state_lock)) return ASTRA_STATUS_BUSY;
    uint32_t status = usage_begin(&initial);
    AstraVolumeUsagePage *page = status == ASTRA_STATUS_OK ? astra_runtime_allocate(sizeof(*page)) : NULL;
    if (status == ASTRA_STATUS_OK && page == NULL) status = ASTRA_STATUS_LIMIT;
    for (uint32_t pass = 0u; pass < 2u && status == ASTRA_STATUS_OK; ++pass) {
        AstraVolumeUsageQuery cursor = initial;
        uint32_t count = 0u;
        if (pass != 0u) {
            cursor.volume = info->volume; cursor.sequence = info->sequence;
            cursor.catalog_sequence = info->catalog_sequence;
        }
        do {
            status = usage_read_page(&cursor, page);
            if (status != ASTRA_STATUS_OK) break;
            if (page->count > UINT32_MAX / sizeof(*records) - count) {
                status = ASTRA_STATUS_LIMIT; break;
            }
            if (pass != 0u) {
                if (count > capacity || page->count > capacity - count) {
                    status = ASTRA_STATUS_BUSY; break;
                }
                if (page->count != 0u)
                    memcpy(records + count, page->records, (size_t)page->count * sizeof(*records));
            }
            count += page->count;
            cursor.volume = page->volume; cursor.sequence = page->sequence;
            cursor.catalog_sequence = page->catalog_sequence;
            cursor.after = page->next; cursor.after_kind = page->next_kind; cursor.after_process = page->next_process;
        } while (cursor.after != 0u);
        if (status != ASTRA_STATUS_OK) break;
        if (pass == 0u) {
            info->volume = cursor.volume; info->sequence = cursor.sequence;
            info->catalog_sequence = cursor.catalog_sequence; info->count = count;
            if (count > capacity) status = ASTRA_STATUS_BUFFER_TOO_SMALL;
            if (count == 0u) break;
        } else if (count != info->count) status = ASTRA_STATUS_BUSY;
    }
    if (status == ASTRA_STATUS_OK || status == ASTRA_STATUS_BUFFER_TOO_SMALL) {
        uint32_t checked = usage_check_mappings(&initial, 0);
        if (checked != ASTRA_STATUS_OK) status = checked;
    }
    astra_runtime_deallocate(page);
    astra_vfs_state_lock_release(&state_lock);
    if (status != ASTRA_STATUS_OK && status != ASTRA_STATUS_BUFFER_TOO_SMALL)
        memset(info, 0, sizeof(*info));
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

static uint32_t hostfs_volume_usage_snapshot(void *context,
    const AstraVolumeReference *volume, AstraVolumeUsageRecord *records,
    uint32_t capacity, AstraVolumeUsageSnapshotInfo *info)
{
    (void)context;
    uint32_t status = hostfs_check_provider();
    if (status != ASTRA_STATUS_OK) return status;
    retire_orphans();
    return hostfs_read_usage_snapshot(volume, records, capacity, info);
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
    uint32_t volume_delegate_receive = 0u, volume_delegate_send = 0u;
    uint32_t volume_admin_receive = 0u, volume_admin_send = 0u;
    uint32_t volume_usage_receive = 0u, volume_usage_send = 0u;
    uint32_t volume_disk_receive = 0u, volume_disk_send = 0u;
    uint32_t ram_read_receive = 0u, ram_read_send = 0u;
    uint32_t ram_write_receive = 0u, ram_write_send = 0u;
    AstraVolumeReference ram_source = {0};
    uint32_t status = ASTRA_STATUS_OK;
    const char *const names[] = {"/dh0", "/work", "/sound", "/tmp",
                                 "/config-store"};
    uint32_t published[15] = {0};
    AstraVolumeReference system;
    AstraVolumeReference config_store;
    AstraVfsHostBackend system_backend;
    uint32_t config_session = 0u;
    AstraVolumeChange published_change = {0};

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_FILESYSTEM);
    const AstraStartupCapability *secrets = astra_startup_capability(
        startup, ASTRA_CAPABILITY_HOST_CONFIG_SECRETS);
    bootstrap = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    const AstraStartupCapability *messaging = astra_startup_capability(
        startup, ASTRA_CAPABILITY_MESSAGING);
    if (device == NULL || secrets == NULL || bootstrap == NULL ||
        messaging == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    device_handle = device->handle;
    secret_device_handle = secrets->handle;
    if (!astra_vfs_host_transport_init(&transport, device_handle,
        astra_vfs_state_lock_acquire, astra_vfs_state_lock_release, &transport_lock))
        return HOSTFS_FAIL_DEVICE;
    status = astra_vfs_host_claim(&transport, transport.generation, 0, &bound_provider_epoch);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_lookup_mount(&transport, transport.generation,
            "/dh0", ASTRA_VOLUME_SYSTEM, &initial_volumes, &system);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_host_lookup_mount(&transport, transport.generation,
            "/config-store", 0u, &initial_volumes, &config_store);
    if (status == ASTRA_VFS_OK)
        status = hostfs_session_open(
            0u, &config_store, "/", ASTRA_HOST_FS_SESSION_READ_ONLY, 0u, &config_session);
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
    if (status == ASTRA_STATUS_OK) status = file_pager_start();
    for (uint32_t index = 0u; index < 5u && status == ASTRA_STATUS_OK; ++index) {
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
         !astra_volume_port_init(&volume_port, volume_receive,
            read_volume_catalog, &transport)))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_read_receive, &volume_read_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_write_receive, &volume_write_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_delegate_receive, &volume_delegate_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_admin_receive, &volume_admin_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_usage_receive, &volume_usage_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &volume_disk_receive, &volume_disk_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &ram_read_receive, &ram_read_send) != ASTRA_SYSCALL_OK ||
         astra_rt_port_create(queue_messages, queue_messages * ASTRA_MESSAGE_SIZE_MAX,
            &ram_write_receive, &ram_write_send) != ASTRA_SYSCALL_OK))
        status = ASTRA_STATUS_LIMIT;
    volume_port.root = hostfs_volume_root;
    volume_port.control = hostfs_volume_control;
    volume_port.network_source = hostfs_volume_network_source;
    volume_port.mount_auth = hostfs_volume_mount_auth;
    volume_port.format = hostfs_volume_format;
    volume_port.partition = hostfs_volume_partition;
    volume_port.usage = hostfs_volume_usage;
    volume_port.usage_snapshot = hostfs_volume_usage_snapshot;
    volume_port.reclaim = hostfs_cache_reclaim;
    published[5] = metrics_send;
    published[6] = volume_send;
    published[7] = volume_read_send;
    published[8] = volume_write_send;
    published[9] = volume_admin_send;
    published[10] = volume_usage_send;
    published[11] = volume_disk_send;
    published[12] = ram_read_send;
    published[13] = ram_write_send;
    published[14] = volume_delegate_send;
    uint32_t ready = astra_service_ready_managed(bootstrap->handle, status, published, 15u, shutdown_send);
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

        status = file_pager_check();
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("HostFS file pager", status);
            return (int)ASTRA_STATUS_IO;
        }

        uint64_t now = astra_clock_monotonic();
        if (now >= catalog_deadline) {
            astra_volume_port_poll(&volume_port);
            if (memcmp(&volume_port.observed, &published_change,
                       sizeof(published_change)) != 0) {
                AstraResult posted = astra_messaging_post(messaging->handle,
                    "volumes", &volume_port.observed,
                    sizeof(volume_port.observed), 1u);
                if (posted == ASTRA_OK)
                    published_change = volume_port.observed;
            }
            retire_orphans();
            publish_filesystem_changes(messaging->handle);
            file_areas_writeback(16u);
            file_areas_maintain(0);
            uint64_t completed = astra_clock_monotonic();
            /* Missed periods cannot starve the queued control requests. */
            catalog_deadline = completed + UINT64_C(1000000000);
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
        wait_handles[count] = volume_delegate_receive;
        wait_owners[count++] = WAIT_VOLUME_DELEGATE;
        wait_handles[count] = volume_admin_receive;
        wait_owners[count++] = WAIT_VOLUME_ADMIN;
        wait_handles[count] = volume_usage_receive;
        wait_owners[count++] = WAIT_VOLUME_USAGE;
        wait_handles[count] = volume_disk_receive;
        wait_owners[count++] = WAIT_DISK_ADMIN;
        wait_handles[count] = ram_read_receive;
        wait_owners[count++] = WAIT_RAM_READ;
        wait_handles[count] = ram_write_receive;
        wait_owners[count++] = WAIT_RAM_WRITE;
        wait_handles[count] = file_sync_receive;
        wait_owners[count++] = WAIT_FILE_SYNC;
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
            status = hostfs_shutdown(&request, reply);
            if (status == ASTRA_VFS_OK) break;
            continue;
        }
        if (shutdown_requested != 0u) {
            status = finish_hostfs();
            break;
        }
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (status == ASTRA_SYSCALL_TIMED_OUT)
            continue;
        if (selected >= count || wait_owners[selected] == WAIT_SHUTDOWN)
            return (int)status;
        if (wait_owners[selected] == WAIT_FILE_SYNC) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            uint32_t sync_status;
            (void)astra_vfs_file_pager_sync_pump(&file_pager,
                file_sync_receive, 1u, &sync_status);
            if (sync_status != ASTRA_SYSCALL_OK) return (int)sync_status;
            continue;
        }
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
            wait_owners[selected] == WAIT_VOLUME_DELEGATE ||
            wait_owners[selected] == WAIT_RAM_READ || wait_owners[selected] == WAIT_RAM_WRITE) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            (void)astra_volume_port_root_pump(&volume_port, wait_handles[selected],
                ASTRA_RIGHT_READ | ((wait_owners[selected] == WAIT_VOLUME_WRITE ||
                    wait_owners[selected] == WAIT_RAM_WRITE) ? ASTRA_RIGHT_WRITE : 0u),
                (wait_owners[selected] == WAIT_RAM_READ || wait_owners[selected] == WAIT_RAM_WRITE) ?
                    &ram_source : NULL,
                wait_owners[selected] == WAIT_VOLUME_DELEGATE ||
                    wait_owners[selected] == WAIT_RAM_READ ||
                    wait_owners[selected] == WAIT_RAM_WRITE, 1u);
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
        if (wait_owners[selected] == WAIT_DISK_ADMIN) {
            if (status != ASTRA_SYSCALL_OK) return (int)status;
            (void)astra_volume_port_format_pump(&volume_port, wait_handles[selected], 1u);
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
            (void)astra_log_failure("HostFS provider recovery", status);
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
    file_pager_stop_and_close();
    astra_volume_port_destroy(&volume_port);
    astra_vfs_host_transport_destroy(&transport);
    return (int)status;
}
