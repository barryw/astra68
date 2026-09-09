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

ASTRA_PROGRAM("hostfs", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    HOSTFS_FAIL_DEVICE = ASTRA_STATUS_PROGRAM_FIRST,
    HOSTFS_FAIL_LOCK,
    HOSTFS_FAIL_PORT,
    HOSTFS_FAIL_READY
};

static uint32_t device_handle;
static uint32_t transport_lock;
static uint32_t state_lock;
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
    "host.fs.chmod", "host.fs.readlink", "host.fs.symlink", "host.fs.link"
};
static HostFsMetricGroup fs_groups[ASTRA_HOST_FS_LINK + 1u];
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

    if (capacity < group->count)
        return 0u;
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

    if (capacity < 2u)
        return 0u;
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
        "peak_open_files", "peak_sessions"
    };
    const AstraVfsServiceStats *stats = astra_vfs_service_stats(context);
    uint32_t values[sizeof(names) / sizeof(names[0])];
    uint32_t count = sizeof(names) / sizeof(names[0]);

    _Static_assert(sizeof(AstraVfsServiceStats) ==
                       sizeof(names) / sizeof(names[0]) * sizeof(uint32_t),
                   "VFS metric names no longer match the stats record");
    if (stats == NULL || capacity < count)
        return 0u;
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
         operation <= ASTRA_HOST_FS_LINK; ++operation) {
        fs_groups[operation].operation = operation;
        if (astra_metric_register(fs_group_names[operation], sample_host_fs,
                                  &fs_groups[operation]) != ASTRA_METRIC_OK)
            return 0;
    }
    return astra_metric_vfs_init(&metrics_backend, refresh_host_metrics,
                                 &transport);
}

static int acquire(void *context)
{
    return astra_wait_one(*(uint32_t *)context, ASTRA_DEADLINE_FOREVER,
                          NULL) == ASTRA_SYSCALL_OK;
}

static void release(void *context)
{
    if (astra_rt_signal(*(uint32_t *)context, 1u, NULL) != ASTRA_SYSCALL_OK)
        astra_process_exit(HOSTFS_FAIL_LOCK);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    void *service_storage = NULL;
    void *metrics_storage = NULL;
    uint32_t service_capacity = 0u;
    uint32_t metrics_capacity = 0u;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t metrics_receive = 0u;
    uint32_t metrics_send = 0u;
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
    if (status == ASTRA_STATUS_OK &&
        (astra_rt_semaphore_create(
             1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
             &transport_lock) != ASTRA_SYSCALL_OK ||
         astra_rt_semaphore_create(
             1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
             &state_lock) != ASTRA_SYSCALL_OK))
        status = HOSTFS_FAIL_LOCK;
    if (status == ASTRA_STATUS_OK &&
        !astra_vfs_host_transport_init(&transport, device_handle, acquire,
                                       release, &transport_lock))
        status = HOSTFS_FAIL_DEVICE;
    if (status == ASTRA_STATUS_OK &&
        (!astra_vfs_port_quota_storage(sizeof(AstraVfsOpenFile),
                                       &service_storage,
                                       &service_capacity) ||
         !astra_vfs_host_init(&backend, &transport, transport.generation) ||
         !astra_vfs_service_init(
             &service, astra_vfs_host_ops(), &backend, service_sessions,
             ASTRA_VFS_SESSION_MAX, service_storage, service_capacity) ||
         !astra_vfs_service_set_state_lock(&service, acquire, release,
                                           &state_lock) ||
         !astra_vfs_service_set_state_wait(
             &service, astra_vfs_state_futex_wait,
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
         !astra_vfs_port_service_set_state_lock(&port, acquire, release,
                                                &state_lock) ||
         !astra_vfs_port_service_set_accelerator(&port, device_handle)))
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
    {
        uint32_t published[] = {send, metrics_send};
        uint32_t ready = astra_service_ready(bootstrap->handle, status,
                                             published, 2u);
        if (ready != ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
            status = HOSTFS_FAIL_READY;
    }
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        uint32_t waits[] = {port.receive, metrics_port.receive};
        uint32_t selected = ASTRA_WAIT_INDEX_NONE;

        status = astra_wait_multiple(waits, 2u, ASTRA_DEADLINE_FOREVER,
                                     &selected, NULL);
        if (status != ASTRA_SYSCALL_OK || selected >= 2u)
            return (int)status;
        if (selected == 0u)
            (void)astra_vfs_port_service_worker_pump(&port, &worker, 1u);
        else
            (void)astra_vfs_port_service_worker_pump(
                &metrics_port, &metrics_worker, 1u);
    }
}
