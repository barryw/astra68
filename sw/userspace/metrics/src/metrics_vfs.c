#include <astra/metrics.h>
#include <astra/metrics_vfs.h>

#include <stddef.h>
#include <string.h>

enum {
    METRIC_NODE_ROOT = 0,
    METRIC_NODE_SNAPSHOT = 1
};

static int path_is(const char *path, const char *wanted)
{
    if (path == NULL)
        return wanted[0] == '\0';
    while (*path == '/')
        ++path;
    return strcmp(path, wanted) == 0;
}

static int name_copy(char out[ASTRA_METRIC_NAME_MAX], const char *in)
{
    uint32_t at = 0u;

    if (in == NULL)
        return 0;
    while (at < ASTRA_METRIC_NAME_MAX && in[at] != '\0') {
        out[at] = in[at];
        ++at;
    }
    if (at == ASTRA_METRIC_NAME_MAX)
        return 0;
    out[at] = '\0';
    return 1;
}

static uint32_t valid_sample_count(void)
{
    AstraMetricSample samples[ASTRA_METRIC_SAMPLE_MAX];
    uint32_t total = 0u;

    for (uint32_t group_index = 0u;
         group_index < astra_metric_group_count(); ++group_index) {
        const AstraMetricGroup *group = astra_metric_group(group_index);
        uint32_t count = astra_metric_sample(
            group, samples, ASTRA_METRIC_SAMPLE_MAX);

        for (uint32_t index = 0u; index < count; ++index) {
            char ignored[ASTRA_METRIC_NAME_MAX];

            if (name_copy(ignored, group->name) &&
                name_copy(ignored, samples[index].name))
                ++total;
        }
    }
    return total;
}

static void record_set(AstraMetricRecord *record, const char *group,
                       const AstraMetricSample *sample)
{
    memset(record, 0, sizeof(*record));
    record->size = sizeof(*record);
    record->version = ASTRA_METRIC_RECORD_VERSION;
    (void)name_copy(record->group, group);
    (void)name_copy(record->name, sample->name);
    record->value_hi = (uint32_t)(sample->value >> 32);
    record->value_lo = (uint32_t)sample->value;
}

static uint32_t metrics_open(void *context, const char *path, uint32_t flags,
                             uint16_t create_mode, uintptr_t *node,
                             AstraVfsNodeInfo *info)
{
    AstraMetricVfs *metrics = context;

    (void)create_mode;
    if (metrics == NULL || node == NULL || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE)) != 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (path_is(path, "")) {
        *node = METRIC_NODE_ROOT;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0500u;
        info->nlink = 2u;
        return ASTRA_VFS_OK;
    }
    if (!path_is(path, "snapshot"))
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (metrics->refresh != NULL) {
        uint32_t status = metrics->refresh(metrics->refresh_context);

        if (status != ASTRA_VFS_OK)
            return status;
    }
    *node = METRIC_NODE_SNAPSHOT;
    info->size = (uint64_t)valid_sample_count() * sizeof(AstraMetricRecord);
    info->kind = ASTRA_VFS_KIND_FILE;
    info->mode = 0400u;
    info->nlink = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t metrics_close(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_OK;
}

static uint32_t metrics_read(void *context, uintptr_t node, uint64_t offset,
                             void *buffer, uint32_t length, uint32_t *moved)
{
    AstraMetricSample samples[ASTRA_METRIC_SAMPLE_MAX];
    uint8_t *out = buffer;
    uint64_t position = 0u;

    (void)context;
    if (moved == NULL || (length != 0u && buffer == NULL))
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (node == METRIC_NODE_ROOT)
        return ASTRA_VFS_ERR_IS_DIR;
    if (node != METRIC_NODE_SNAPSHOT)
        return ASTRA_VFS_ERR_NOT_FOUND;
    for (uint32_t group_index = 0u;
         group_index < astra_metric_group_count(); ++group_index) {
        const AstraMetricGroup *group = astra_metric_group(group_index);
        uint32_t count = astra_metric_sample(
            group, samples, ASTRA_METRIC_SAMPLE_MAX);

        for (uint32_t index = 0u; index < count; ++index) {
            AstraMetricRecord record;
            const uint8_t *bytes = (const uint8_t *)&record;
            char ignored[ASTRA_METRIC_NAME_MAX];

            if (!name_copy(ignored, group->name) ||
                !name_copy(ignored, samples[index].name))
                continue;
            record_set(&record, group->name, &samples[index]);
            for (uint32_t at = 0u; at < sizeof(record); ++at, ++position) {
                if (position < offset)
                    continue;
                if (*moved == length)
                    return ASTRA_VFS_OK;
                out[(*moved)++] = bytes[at];
            }
        }
    }
    return ASTRA_VFS_OK;
}

static uint32_t metrics_stat(void *context, const char *path,
                             AstraVfsNodeInfo *info)
{
    uintptr_t node = 0u;

    return metrics_open(context, path, ASTRA_VFS_OPEN_READ,
                        ASTRA_VFS_MODE_DEFAULT, &node, info);
}

static uint32_t metrics_readdir(void *context, uintptr_t directory,
                                const char *path, uint64_t cookie, char *name,
                                uint32_t capacity, AstraVfsNodeInfo *info,
                                uint64_t *next)
{
    (void)context;
    (void)directory;
    if (!path_is(path, ""))
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (cookie != 0u)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (name == NULL || info == NULL || next == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (capacity < sizeof("snapshot"))
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(name, "snapshot", sizeof("snapshot"));
    info->size = 0u;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->mode = 0400u;
    info->nlink = 1u;
    *next = 1u;
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps operations = {
    .open = metrics_open,
    .close = metrics_close,
    .read = metrics_read,
    .write = astra_vfs_backend_deny_write,
    .sync = astra_vfs_backend_deny_sync,
    .truncate = astra_vfs_backend_deny_truncate,
    .stat = metrics_stat,
    .readdir = metrics_readdir,
    .mkdir = astra_vfs_backend_deny_mkdir,
    .unlink = astra_vfs_backend_deny_unlink,
    .rename = astra_vfs_backend_deny_rename,
    .chmod = astra_vfs_backend_deny_chmod,
    .readlink = astra_vfs_backend_no_readlink,
    .symlink = astra_vfs_backend_deny_symlink,
    .link = astra_vfs_backend_deny_link,
};

int astra_metric_vfs_init(AstraMetricVfs *metrics,
                          AstraMetricRefresh refresh, void *context)
{
    if (metrics == NULL)
        return 0;
    metrics->refresh = refresh;
    metrics->refresh_context = context;
    return 1;
}

const AstraVfsBackendOps *astra_metric_vfs_ops(void)
{
    return &operations;
}
