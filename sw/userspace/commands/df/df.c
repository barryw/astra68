#include <astra/posix.h>
#include <astra/program.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>
#include <astra/volume.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("df", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

/* Report whole KiB without overflowing the filesystem's full-width counts. */
static int kib(uint64_t blocks, uint32_t unit, uint64_t *value)
{
    if (unit == 0u || blocks / 1024u > UINT64_MAX / unit) return 0;
    uint64_t whole = (blocks / 1024u) * unit;
    uint64_t tail = ((blocks % 1024u) * unit) / 1024u;
    if (whole > UINT64_MAX - tail) return 0;
    *value = whole + tail;
    return 1;
}

static uint32_t capacity(AstraProcessFilesystem *filesystem,
    const AstraVolumeRecord *record, uint64_t epoch, AstraHandle broker,
    AstraFilesystemInfo *info)
{
    /* Reserved roots retain explicit startup authority and cannot be rebound
     * by mount controls. Every public mount is queried by exact incarnation. */
    if ((record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u)
        return astra_filesystem_capacity_path(&filesystem->filesystem, record->name, info);
    if (broker == ASTRA_INVALID_HANDLE) return ASTRA_VFS_ERR_ACCESS;
    AstraVolumeReference reference = {epoch, record->id, record->generation};
    AstraHandle root = ASTRA_INVALID_HANDLE;
    AstraResult result = astra_volume_root(broker, &reference, ASTRA_RIGHT_READ, &root);
    if (result != ASTRA_OK) return astra_status_from_result(result);
    AstraVfsClient client = {0};
    uint32_t status = astra_vfs_client_connect_service(&client, root);
    if (status == ASTRA_VFS_OK) {
        status = astra_vfs_filesystem_info_path(&client, "/", info);
        uint32_t closed = astra_vfs_disconnect(&client);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    uint32_t closed = astra_close(root);
    if (status == ASTRA_VFS_OK && closed != ASTRA_SYSCALL_OK) status = ASTRA_VFS_ERR_IO;
    return status;
}

int main(int argc, char **argv)
{
    if (argc != 1 && (argc != 2 || strcmp(argv[1], "-k") != 0)) {
        fputs("usage: df [-k]\n", stderr);
        return 2;
    }
    const AstraStartupCapability *metadata = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *reader = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUME_READ);
    if (metadata == NULL) {
        fputs("df: volume metadata capability unavailable\n", stderr);
        return 1;
    }
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(metadata->handle, &snapshot);
    if (result != ASTRA_OK) {
        fprintf(stderr, "df: catalog query failed (%ld)\n", (long)result);
        return 1;
    }
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    uint32_t status = astra_process_filesystem_open(&filesystem, astra_posix_startup());
    int failed = status != ASTRA_VFS_OK;
    if (failed) fprintf(stderr, "df: namespace attach failed (%u)\n", status);
    else failed = puts("Filesystem 1K-blocks Used Available Mounted on") == EOF;
    for (uint32_t at = 0u; at < snapshot.count && status == ASTRA_VFS_OK; ++at) {
        const AstraVolumeRecord *record = &snapshot.records[at];
        if (record->kind != ASTRA_VOLUME_MOUNT || record->state != ASTRA_VOLUME_MOUNTED)
            continue;
        AstraFilesystemInfo info = {0};
        uint32_t queried = capacity(&filesystem, record, snapshot.epoch,
            reader != NULL ? reader->handle : ASTRA_INVALID_HANDLE, &info);
        uint64_t total, used, available;
        if (queried != ASTRA_VFS_OK || info.blocks_free > info.blocks ||
            info.blocks_available > info.blocks_free ||
            !kib(info.blocks, info.fragment_size, &total) ||
            !kib(info.blocks - info.blocks_free, info.fragment_size, &used) ||
            !kib(info.blocks_available, info.fragment_size, &available)) {
            fprintf(stderr, "df: %s: capacity unavailable (%u)\n", record->name,
                queried != ASTRA_VFS_OK ? queried : ASTRA_VFS_ERR_IO);
            failed = 1;
            continue;
        }
        if (printf("%s %llu %llu %llu %s\n", record->name,
                (unsigned long long)total, (unsigned long long)used,
                (unsigned long long)available, record->name) < 0) failed = 1;
    }
    astra_process_filesystem_close(&filesystem);
    astra_volume_snapshot_close(&snapshot);
    return failed || fflush(stdout) != 0 ? 1 : 0;
}
