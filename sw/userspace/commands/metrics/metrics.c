/* `metrics` -- the system and service counters published at METRICS:. */

#include <astra/metrics.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("metrics", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

_Static_assert(sizeof(AstraMetricRecord) <= ASTRA_VFS_IO_MAX,
               "one metric record must fit one public VFS read");

static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;

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
    return fwrite(line, 1u, used, stdout) == used;
}

static int show_metrics(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraMetricRecord record;
    uint32_t used = 0u;
    uint32_t status = astra_filesystem_open(
        &filesystem.filesystem, "/metrics/snapshot", ASTRA_VFS_OPEN_READ,
        &file);

    if (status != ASTRA_VFS_OK)
        return (int)status;
    while (status == ASTRA_VFS_OK) {
        uint32_t moved = 0u;

        status = astra_filesystem_read(
            &file, (uint8_t *)&record + used, sizeof(record) - used, &moved);
        if (status != ASTRA_VFS_OK || moved == 0u)
            break;
        used += moved;
        if (used == sizeof(record)) {
            if (!emit_record(&record))
                status = ASTRA_VFS_ERR_PROTOCOL;
            used = 0u;
        }
    }
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK && used != 0u)
        status = ASTRA_VFS_ERR_PROTOCOL;
    return (int)status;
}

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    uint32_t status;
    int result;

    (void)argc;
    (void)argv;
    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    status = astra_process_filesystem_open(&filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fputs("metrics: filesystem unavailable\n", stderr);
        return (int)status;
    }
    result = show_metrics();
    astra_process_filesystem_close(&filesystem);
    if (result != 0)
        (void)fputs("metrics: METRICS: unavailable or invalid\n", stderr);
    return fflush(stdout) == 0 ? result : (int)ASTRA_STATUS_IO;
}
