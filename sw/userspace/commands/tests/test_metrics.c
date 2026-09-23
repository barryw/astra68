#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/metrics.h>
#include <astra/posix.h>
#include <astra/vfs_process.h>

static AstraStartupInfo startup;
static uint8_t snapshot[2u * sizeof(AstraMetricRecord)];
static uint32_t snapshot_length;
static uint32_t position;
static uint32_t read_limit;
static uint32_t open_status;
static uint32_t read_status;
static uint32_t close_status;
static uint32_t closes;

const AstraStartupInfo *astra_posix_startup(void)
{
    return &startup;
}

int astra_startup_validate(const AstraStartupInfo *candidate)
{
    return candidate == &startup;
}

uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *candidate)
{
    assert(filesystem != NULL && candidate == &startup);
    return ASTRA_VFS_OK;
}

void astra_process_filesystem_close(AstraProcessFilesystem *filesystem)
{
    assert(filesystem != NULL);
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    assert(filesystem != NULL && file != NULL);
    assert(strcmp(path, "/metrics/snapshot") == 0);
    assert(flags == ASTRA_VFS_OPEN_READ);
    position = 0u;
    return open_status;
}

uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                               uint32_t length, uint32_t *moved)
{
    assert(file != NULL && buffer != NULL && moved != NULL);
    if (read_status != ASTRA_VFS_OK)
        return read_status;
    *moved = snapshot_length - position;
    if (*moved > length)
        *moved = length;
    if (*moved > read_limit)
        *moved = read_limit;
    memcpy(buffer, snapshot + position, *moved);
    position += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    assert(file != NULL);
    ++closes;
    return close_status;
}

#define main astra_metrics_main
#include "../metrics/metrics.c"
#undef main

int main(void)
{
    AstraMetricRecord record = {
        .size = sizeof(AstraMetricRecord),
        .version = ASTRA_METRIC_RECORD_VERSION,
    };

    memcpy(record.group, "test", sizeof("test"));
    memcpy(record.name, "count", sizeof("count"));
    memcpy(snapshot, &record, sizeof(record));
    snapshot_length = sizeof(record);
    read_limit = 17u;
    assert(show_metrics() == ASTRA_VFS_OK);
    assert(position == snapshot_length && closes == 1u);

    memcpy(snapshot + sizeof(record), &record, sizeof(record));
    snapshot_length = sizeof(snapshot);
    assert(show_metrics() == ASTRA_VFS_OK);
    assert(position == snapshot_length && closes == 2u);

    snapshot_length = sizeof(record) - 1u;
    assert(show_metrics() == ASTRA_VFS_ERR_PROTOCOL);
    assert(closes == 3u);

    snapshot_length = sizeof(record);
    record.size = 0u;
    memcpy(snapshot, &record, sizeof(record));
    assert(show_metrics() == ASTRA_VFS_ERR_PROTOCOL);
    assert(closes == 4u);
    record.size = sizeof(record);
    memcpy(snapshot, &record, sizeof(record));

    read_status = ASTRA_VFS_ERR_IO;
    assert(show_metrics() == ASTRA_VFS_ERR_IO);
    assert(closes == 5u);
    read_status = ASTRA_VFS_OK;

    open_status = ASTRA_VFS_ERR_NOT_FOUND;
    assert(show_metrics() == ASTRA_VFS_ERR_NOT_FOUND);
    assert(closes == 5u);
    open_status = ASTRA_VFS_OK;

    close_status = ASTRA_VFS_ERR_IO;
    assert(show_metrics() == ASTRA_VFS_ERR_IO);
    return 0;
}
