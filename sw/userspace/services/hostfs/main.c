#include <astra/config_library.h>
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
#include <astra/vfs_process.h>
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
static _Alignas(4) uint32_t transport_lock;
static _Alignas(4) uint32_t state_lock;
static AstraVfsHostTransport transport;
static AstraVfsHostBackend backend;
static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService port;
static AstraHostMetricsSnapshot host_metrics;
static AstraMetricVfs metrics_backend;
static AstraVfsService metrics_service;
static AstraVfsSessionSlot metrics_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService metrics_port;
/* SOUND: is the host directory the Linux audio host reads SoundFonts from,
 * so an update here is what it plays. It has no direct path: a client's
 * commands come through this service, which names the volume. */
static AstraVfsHostBackend sound_backend;
static AstraVfsService sound_service;
static AstraVfsSessionSlot sound_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsPortService sound_port;
static volatile uint32_t shutdown_requested;
static _Alignas(16) uint8_t signal_stack[4096u];
/* Every worker owns its request scratch; the service, the port tables and
 * the host transport (one lane per thread) are shared. */
static AstraVfsPortWorker *workers;
static uint32_t *worker_handles;
static AstraThreadStart *worker_starts;
static uint32_t worker_count = 1u;
static uint32_t shutdown_event;
static uint32_t shutdown_receive;
static uint32_t shutdown_reply;
static uint32_t shutdown_transaction;
static volatile uint32_t worker_failed;

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

/*
 * services/hostfs/settings.conf: `workers N`. The initial thread is worker
 * 0, so N is at most one less than the process thread slots. Each worker
 * also owns one host-transport lane, and a lane is one DMA buffer: the
 * per-process DMA region holds ASTRA_DMA_SLOT_COUNT of them, so that is the
 * real ceiling. A worker past it would boot and then fail every
 * request it took with "VFS host DMA create" -- refuse it here instead.
 */
static uint32_t configured_workers(const AstraStartupInfo *startup,
                                   uint32_t *count)
{
    AstraConfig config = ASTRA_CONFIG_INIT;
    AstraConfigError error = {0};
    uint64_t value = 0u;
    uint32_t found = 0u;
    uint32_t status;

    if (astra_process_vfs_init(startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ, &config,
                          &error) != ASTRA_CONFIG_OK)
        return ASTRA_STATUS_INVALID;
    status = astra_config_count(&config, "workers", &found);
    if (status == ASTRA_CONFIG_OK && found == 1u)
        status = astra_config_get_u64(&config, "workers", 0u, &value);
    astra_config_close(&config);
    if (status != ASTRA_CONFIG_OK || found != 1u || value == 0u ||
        value >= ASTRA_PROCESS_THREAD_SLOT_COUNT ||
        value > ASTRA_DMA_SLOT_COUNT)
        return ASTRA_STATUS_INVALID;
    *count = (uint32_t)value;
    return ASTRA_STATUS_OK;
}

/*
 * Worker 0 alone serves METRICS and the shutdown port: the metrics backend
 * refreshes one static snapshot (`host_metrics`) and has no state lock, so
 * it stays single-threaded as it was. WORK and SOUND are served by all.
 */
static void hostfs_worker(uint32_t index)
{
    for (;;) {
        uint32_t handles[5] = {shutdown_event, port.receive,
                               sound_port.receive, metrics_port.receive,
                               shutdown_receive};
        uint32_t ready = ASTRA_WAIT_INDEX_NONE;
        uint32_t status = astra_wait_multiple(
            handles, index == 0u ? 5u : 3u, ASTRA_DEADLINE_FOREVER, &ready,
            NULL);

        if (status == ASTRA_SYSCALL_OK && ready == 0u)
            break;
        if (status == ASTRA_SYSCALL_OK && ready == 4u) {
            AstraShutdownRequest request = {0};

            if (astra_shutdown_receive(shutdown_receive, &request,
                                       &shutdown_reply) !=
                ASTRA_SYSCALL_OK) {
                __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
                break;
            }
            shutdown_transaction = request.header.transaction_id;
            shutdown_requested = 1u;
            break;
        }
        if (index == 0u && shutdown_requested != 0u)
            break;
        if (status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("hostfs receive wait", status);
            __atomic_store_n(&worker_failed, 1u, __ATOMIC_RELEASE);
            break;
        }
        if (ready == 1u)
            (void)astra_vfs_port_service_worker_pump(&port, &workers[index],
                                                     1u);
        else if (ready == 2u)
            (void)astra_vfs_port_service_worker_pump(&sound_port,
                                                     &workers[index], 1u);
        else if (ready == 3u)
            (void)astra_vfs_port_service_worker_pump(&metrics_port,
                                                     &workers[index], 1u);
    }
    if (index != 0u)
        astra_thread_exit(ASTRA_STATUS_OK);
}

static uint32_t finish_hostfs(void)
{
    const AstraVfsMountOps metrics_mount = {NULL, volume_noop_unmount};
    const AstraVfsMountOps host_mount = {NULL, hostfs_unmount};
    uint32_t status;

    if (astra_rt_signal(shutdown_event, 1u, NULL) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    for (uint32_t index = 1u; index < worker_count; ++index) {
        if (astra_wait_one(worker_handles[index - 1u], ASTRA_DEADLINE_FOREVER,
                           NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_IO;
        (void)astra_close(worker_handles[index - 1u]);
    }
    if (__atomic_load_n(&worker_failed, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_STATUS_IO;
    while (astra_vfs_port_service_worker_pump(&metrics_port, &workers[0],
                                               1u) != 0u) {}
    while (astra_vfs_port_service_worker_pump(&sound_port, &workers[0],
                                               1u) != 0u) {}
    while (astra_vfs_port_service_worker_pump(&port, &workers[0], 1u) !=
           0u) {}
    if (metrics_port.stalled != 0u || sound_port.stalled != 0u ||
        port.stalled != 0u)
        return ASTRA_STATUS_IO;
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
    "host.fs.open_at", "host.fs.unlink_at", "host.fs.chmod_file",
    "host.fs.chmod_at", "host.fs.filesystem_info", "host.fs.stat_at",
    "host.fs.stat_file"
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
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t metrics_receive = 0u;
    uint32_t metrics_send = 0u;
    uint32_t sound_receive = 0u;
    uint32_t sound_send = 0u;
    uint32_t shutdown_send = 0u;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    device_handle = device->handle;
    if (configured_workers(startup, &worker_count) != ASTRA_STATUS_OK)
        status = ASTRA_STATUS_INVALID;
    if (status == ASTRA_STATUS_OK &&
        astra_rt_event_create(ASTRA_EVENT_MANUAL_RESET,
                              ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                              &shutdown_event) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    /* ponytail: the thread arrays take worker_count entries, one more than
     * they use, so a single-worker service never asks for zero bytes. */
    if (status == ASTRA_STATUS_OK) {
        workers = astra_runtime_allocate(worker_count * sizeof(*workers));
        worker_handles = astra_runtime_allocate(
            worker_count * sizeof(*worker_handles));
        worker_starts = astra_runtime_allocate(
            worker_count * sizeof(*worker_starts));
        if (workers == NULL || worker_handles == NULL ||
            worker_starts == NULL) {
            status = ASTRA_STATUS_LIMIT;
        } else {
            memset(workers, 0, worker_count * sizeof(*workers));
            memset(worker_handles, 0, worker_count * sizeof(*worker_handles));
            memset(worker_starts, 0, worker_count * sizeof(*worker_starts));
        }
    }
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_host_transport_init(
            &transport, device_handle, astra_vfs_state_lock_acquire,
            astra_vfs_state_lock_release, &transport_lock))
        status = HOSTFS_FAIL_DEVICE;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &service_storage,
                                       &service_capacity) ||
         !astra_vfs_host_init(&backend, &transport, transport.generation,
                              ASTRA_HOST_FS_VOLUME_WORK) ||
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
                              transport.generation,
                              ASTRA_HOST_FS_VOLUME_SOUND) ||
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
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_port_create(
             ASTRA_PORT_MESSAGES_MAX,
             ASTRA_PORT_MESSAGES_MAX *
                 (uint32_t)sizeof(AstraVfsRenameRequestMessage),
             &receive, &send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&port, receive, &service) ||
         !astra_vfs_port_service_set_state_lock(
             &port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock)))
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
        (astra_rt_port_create(
             ASTRA_PORT_MESSAGES_MAX,
             ASTRA_PORT_MESSAGES_MAX *
                 (uint32_t)sizeof(AstraVfsRenameRequestMessage),
             &sound_receive, &sound_send) != ASTRA_SYSCALL_OK ||
         !astra_vfs_port_service_init(&sound_port, sound_receive,
                                      &sound_service) ||
         !astra_vfs_port_service_set_state_lock(
             &sound_port, astra_vfs_state_lock_acquire,
             astra_vfs_state_lock_release, &state_lock)))
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
    if (status == ASTRA_STATUS_OK && worker_count > 1u) {
        AstraProcessInfo info = {0};
        uint32_t process_handle = 0u;
        uint32_t query = astra_query_abi(NULL, &process_handle, NULL);

        if (query == ASTRA_SYSCALL_OK)
            query = astra_process_info(process_handle, &info);
        if (query != ASTRA_SYSCALL_OK) {
            (void)astra_log_failure("hostfs thread process info", query);
            status = ASTRA_STATUS_IO;
        }
        for (uint32_t index = 1u;
             status == ASTRA_STATUS_OK && index < worker_count; ++index) {
            uint32_t created;

            worker_starts[index - 1u].entry = hostfs_worker;
            worker_starts[index - 1u].argument = index;
            created = astra_rt_thread_create(
                &worker_starts[index - 1u], info.default_priority,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT,
                &worker_handles[index - 1u], NULL);
            if (created != ASTRA_SYSCALL_OK) {
                (void)astra_log_failure("hostfs worker thread", created);
                status = ASTRA_STATUS_LIMIT;
            }
        }
    }
    {
        uint32_t published[] = {send, metrics_send, sound_send};
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
    hostfs_worker(0u);
    status = finish_hostfs();
    if (shutdown_reply != 0u)
        (void)astra_shutdown_respond(
            shutdown_reply, shutdown_transaction,
            status == ASTRA_STATUS_OK ? ASTRA_SHUTDOWN_READY :
                                        ASTRA_SHUTDOWN_CANCEL,
            status);
    return (int)status;
}
