/*
 * sampler -- the machine's accounting, where the host can read it.
 *
 * Once a second this copies PROC:snapshot (every live process's
 * AstraProcSnapshot) and PROC:scheduler (the kernel's machine-wide
 * counters) into WORK:.astra/sample, replacing the previous copy by rename
 * so a reader never sees half of one. On the DE25, WORK: is
 * /var/lib/astra/hostfs/work, and `astra-top` reads it from Linux: CPU per
 * process, idle, switches and preemptions, beside the host's own threads.
 *
 * Every counter is cumulative, so a late or skipped sample loses nothing; a
 * reader subtracts two. The sampler's own cost is its own row.
 *
 * File: AstraSampleHeader, one AstraSchedulerStats, then `count`
 * AstraProcSnapshot records -- all host-readable fixed layouts, big-endian.
 */

#include <astra/proc.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/vfs_process.h>

#include <stdint.h>

ASTRA_PROGRAM("sampler", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define SAMPLER_PERIOD_NS UINT64_C(1000000000)
#define SAMPLER_MAGIC UINT32_C(0x41534d31) /* ASM1 */

typedef struct AstraSampleHeader {
    uint32_t magic;
    uint32_t header_bytes;
    uint32_t scheduler_bytes;
    uint32_t record_bytes;
    uint32_t count;
    uint32_t sequence;
} AstraSampleHeader;

static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static uint8_t *records;
static uint32_t records_capacity;

/* Reads `path` into `buffer`, at most `capacity` bytes. */
static uint32_t read_into(AstraFile *file, uint8_t *buffer,
                          uint32_t capacity, uint32_t *length)
{
    uint32_t status = ASTRA_VFS_OK;

    *length = 0u;
    while (status == ASTRA_VFS_OK && *length < capacity) {
        uint32_t moved = 0u;

        status = astra_filesystem_read(file, buffer + *length,
                                       capacity - *length, &moved);
        if (moved == 0u)
            break;
        *length += moved;
    }
    return status;
}

static uint32_t read_fixed(const char *path, void *buffer, uint32_t size)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t length = 0u;
    uint32_t status = astra_filesystem_open(&filesystem.filesystem, path,
                                            ASTRA_VFS_OPEN_READ, &file);

    if (status != ASTRA_VFS_OK)
        return status;
    status = read_into(&file, buffer, size, &length);
    (void)astra_filesystem_close(&file);
    return status == ASTRA_VFS_OK && length != size ?
        ASTRA_VFS_ERR_PROTOCOL : status;
}

/* The snapshot's size is the live process count, known at open. */
static uint32_t read_snapshot(uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = {.size = sizeof(info)};
    uint32_t status = astra_filesystem_open(&filesystem.filesystem,
                                            "/proc/snapshot",
                                            ASTRA_VFS_OPEN_READ, &file);

    *length = 0u;
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_filesystem_file_info(&file, &info);
    if (status == ASTRA_VFS_OK && info.byte_size > records_capacity) {
        uint8_t *grown = info.byte_size > UINT32_MAX ? NULL :
            astra_runtime_reallocate(records, (size_t)info.byte_size);

        if (grown == NULL) {
            status = ASTRA_VFS_ERR_LIMIT;
        } else {
            records = grown;
            records_capacity = (uint32_t)info.byte_size;
        }
    }
    if (status == ASTRA_VFS_OK)
        status = read_into(&file, records, (uint32_t)info.byte_size, length);
    (void)astra_filesystem_close(&file);
    return status;
}

static uint32_t write_all(AstraFile *file, const void *data, uint32_t length)
{
    const uint8_t *bytes = data;

    while (length != 0u) {
        uint32_t moved = 0u;
        uint32_t status = astra_filesystem_write(file, bytes, length, &moved);

        if (status != ASTRA_VFS_OK)
            return status;
        if (moved == 0u)
            return ASTRA_VFS_ERR_IO;
        bytes += moved;
        length -= moved;
    }
    return ASTRA_VFS_OK;
}

static uint32_t sample(uint32_t sequence)
{
    AstraSchedulerStats scheduler;
    AstraSampleHeader header;
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t length = 0u;
    uint32_t status = read_fixed("/proc/scheduler", &scheduler,
                                 sizeof(scheduler));

    if (status == ASTRA_VFS_OK)
        status = read_snapshot(&length);
    if (status != ASTRA_VFS_OK)
        return status;
    header = (AstraSampleHeader){
        .magic = SAMPLER_MAGIC,
        .header_bytes = sizeof(header),
        .scheduler_bytes = sizeof(scheduler),
        .record_bytes = sizeof(AstraProcSnapshot),
        .count = length / (uint32_t)sizeof(AstraProcSnapshot),
        .sequence = sequence,
    };
    status = astra_filesystem_open(&filesystem.filesystem,
                                   "/work/.astra/sample.new",
                                   ASTRA_VFS_OPEN_WRITE |
                                       ASTRA_VFS_OPEN_CREATE |
                                       ASTRA_VFS_OPEN_TRUNCATE,
                                   &file);
    if (status != ASTRA_VFS_OK)
        return status;
    status = write_all(&file, &header, sizeof(header));
    if (status == ASTRA_VFS_OK)
        status = write_all(&file, &scheduler, sizeof(scheduler));
    if (status == ASTRA_VFS_OK)
        status = write_all(&file, records,
                           header.count * (uint32_t)sizeof(AstraProcSnapshot));
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename(&filesystem.filesystem,
                                         "/work/.astra/sample.new",
                                         "/work/.astra/sample");
    return status;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    uint32_t last_failure = ASTRA_VFS_OK;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_process_filesystem_open(&filesystem, startup);
    (void)astra_service_ready(bootstrap->handle,
                              status == ASTRA_VFS_OK ? ASTRA_STATUS_OK :
                                                       ASTRA_STATUS_IO,
                              NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_VFS_OK)
        return ASTRA_STATUS_IO;
    status = astra_filesystem_mkdir(&filesystem.filesystem, "/work/.astra");
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        (void)astra_log_failure("sampler WORK:.astra", status);
    for (uint32_t sequence = 1u;; ++sequence) {
        status = sample(sequence);
        /* Said once per change, not once a second. */
        if (status != last_failure && status != ASTRA_VFS_OK)
            (void)astra_log_failure("sampler", status);
        last_failure = status;
        (void)astra_rt_thread_sleep(SAMPLER_PERIOD_NS,
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
}
