/* `metrics` -- the system and service counters published at METRICS:. */

#include <astra/metrics.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_union.h>

#include <string.h>

ASTRA_PROGRAM("metrics", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define METRIC_READ_RECORDS 8u

static uint32_t stdout_handle;
static uint32_t error_handle;

static int write_all(const char *text, uint32_t length)
{
    return astra_stream_write_all(stdout_handle, text, length) ==
           ASTRA_SYSCALL_OK;
}

static int emit_record(const AstraMetricRecord *record)
{
    char line[ASTRA_METRIC_NAME_MAX * 2u + 24u];
    char digits[20];
    uint64_t value = astra_metric_record_value(record);
    uint32_t used = 0u;
    uint32_t count = 0u;

    if (record->size != sizeof(*record) ||
        record->version != ASTRA_METRIC_RECORD_VERSION ||
        record->reserved != 0u ||
        memchr(record->group, '\0', sizeof(record->group)) == NULL ||
        memchr(record->name, '\0', sizeof(record->name)) == NULL)
        return 0;
    while (record->group[used] != '\0') {
        line[used] = record->group[used];
        ++used;
    }
    line[used++] = '.';
    for (uint32_t at = 0u; record->name[at] != '\0'; ++at)
        line[used++] = record->name[at];
    line[used++] = ' ';
    do {
        uint64_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (count != 0u)
        line[used++] = digits[--count];
    line[used++] = '\n';
    return write_all(line, used);
}

static int show_metrics(void)
{
    static AstraMetricRecord records[METRIC_READ_RECORDS];
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t offset = 0u;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_vfs_assign_open(
        astra_process_vfs_assigns(), "METRICS:snapshot", ASTRA_RIGHT_READ,
        ASTRA_VFS_OPEN_READ, astra_process_vfs_assign_client, NULL, wire,
        sizeof(wire), &file, &size, &kind, &client, NULL);

    if (status != ASTRA_VFS_OK)
        return (int)status;
    while (offset < size) {
        uint32_t moved = 0u;

        status = astra_vfs_port_read_bulk(client, file, offset, records,
                                          sizeof(records), &moved);
        if (status != ASTRA_VFS_OK || moved == 0u ||
            moved % sizeof(records[0]) != 0u) {
            status = status != ASTRA_VFS_OK ? status : ASTRA_VFS_ERR_PROTOCOL;
            break;
        }
        for (uint32_t index = 0u;
             index < moved / sizeof(records[0]); ++index)
            if (!emit_record(&records[index])) {
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
        if (status != ASTRA_VFS_OK)
            break;
        offset += moved;
    }
    (void)astra_vfs_close(client, file);
    return status == ASTRA_VFS_OK && offset == size ? 0 : (int)status;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capability;
    uint32_t status;
    int result;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    capability = astra_startup_capability(startup, "STDOUT");
    if (capability != NULL)
        stdout_handle = capability->handle;
    capability = astra_startup_capability(startup, "STDERR");
    if (capability != NULL)
        error_handle = capability->handle;
    if (stdout_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (error_handle == 0u)
        error_handle = stdout_handle;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        (void)astra_print(error_handle, "metrics: filesystem unavailable\n");
        return (int)status;
    }
    result = show_metrics();
    astra_process_vfs_close();
    if (result != 0)
        (void)astra_print(error_handle,
                          "metrics: METRICS: unavailable or invalid\n");
    return result;
}
