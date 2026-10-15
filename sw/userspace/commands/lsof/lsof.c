#include <astra/posix.h>
#include <astra/program.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>
#include <astra/volume.h>
#include <stdio.h>
#include <string.h>
#include "../volumes/volume_cli.h"

ASTRA_PROGRAM("lsof", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static int print_bytes(const char *text, uint32_t length)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t at = 0u; at < length; ++at) {
        unsigned char byte = (unsigned char)text[at];
        if (byte < 32u || byte == 127u) {
            if (fputs("\\x", stdout) == EOF || putchar(hex[byte >> 4u]) == EOF ||
                putchar(hex[byte & 15u]) == EOF) return 0;
        } else if (byte == '\\') {
            if (fputs("\\\\", stdout) == EOF) return 0;
        } else if (putchar(byte) == EOF) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc > 2) { fputs("usage: lsof [mounted-volume]\n", stderr); return 2; }
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *usage = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_USAGE);
    const AstraStartupCapability *metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    if (usage == NULL || (argc == 2 && metadata == NULL)) {
        fputs("lsof: volume usage capability unavailable\n", stderr); return 1;
    }
    AstraVolumeReference reference = {0};
    if (argc == 2) {
        VolumeCli cli = {.metadata = metadata->handle, .watch = ASTRA_VOLUME_WATCH_INIT};
        AstraVolumeRecord record;
        if (!volume_cli_select(&cli, argv[1], 0u, &record, &reference, NULL)) return 1;
    }
    AstraProcSnapshot *processes = NULL;
    uint32_t process_bytes = 0u;
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    if (astra_process_filesystem_open(&filesystem, startup) == ASTRA_STATUS_OK) {
        if (astra_process_read_file_alloc(&filesystem, "/proc/snapshot", (void **)&processes,
                &process_bytes) != ASTRA_VFS_OK || process_bytes % sizeof(*processes) != 0u) {
            astra_runtime_deallocate(processes); processes = NULL; process_bytes = 0u;
        }
        astra_process_filesystem_close(&filesystem);
    }
    AstraVolumeUsageSnapshot snapshot = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    AstraResult result = ASTRA_ERROR_BUSY;
    for (uint32_t retry = 0u; retry < 8u && result == ASTRA_ERROR_BUSY; ++retry) {
        result = astra_volume_usage_snapshot_open(usage->handle, argc == 2 ? &reference : NULL, &snapshot);
        if (result == ASTRA_ERROR_BUSY)
            (void)astra_rt_thread_sleep(UINT64_C(50000000), ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
    if (result != ASTRA_OK) {
        fprintf(stderr, "lsof: usage snapshot failed (%ld)%s\n", (long)result,
            result == ASTRA_ERROR_BUSY ? "; resources changed, retry" : "");
        astra_runtime_deallocate(processes); return 1;
    }
    static const char *const kinds[] = {"?", "file", "directory", "root", "operation", "share", "image"};
    int failed = fputs("PID COMMAND KIND HANDLE ACCESS INODE VOLUME PATH\n", stdout) == EOF;
    for (uint32_t at = 0u; at < snapshot.count && !failed; ++at) {
        const AstraVolumeUsageRecord *row = &snapshot.records[at];
        const char *name = row->process == 0u ? "provider" : "-";
        for (uint32_t process = 0u; process < process_bytes / sizeof(*processes); ++process)
            if (processes[process].process.size == sizeof(AstraProcessInfo) &&
                processes[process].process.id == row->process &&
                memchr(processes[process].name, '\0', sizeof(processes[process].name)) != NULL)
                name = processes[process].name;
        failed = printf("%lu ", (unsigned long)row->process) < 0 || !print_bytes(name, (uint32_t)strlen(name)) ||
            printf(" %s %llu %s%s %llu %llu:%llu:%llu ", kinds[row->kind],
                (unsigned long long)row->handle, (row->rights & ASTRA_RIGHT_READ) != 0u ? "r" : row->rights == 0u ? "-" : "",
                (row->rights & ASTRA_RIGHT_WRITE) != 0u ? "w" : "",
                (unsigned long long)row->inode, (unsigned long long)row->volume.epoch,
                (unsigned long long)row->volume.id, (unsigned long long)row->volume.generation) < 0 ||
            !print_bytes(row->path, row->path_length);
        if (row->flags & ASTRA_VOLUME_USAGE_UNNAMED) failed |= fputs("[unnamed retained object]", stdout) == EOF;
        if (row->flags & ASTRA_VOLUME_USAGE_LOST) failed |= fputs(" [lost]", stdout) == EOF;
        if (row->flags & ASTRA_VOLUME_USAGE_ACTIVE) failed |= fputs(" [operation active]", stdout) == EOF;
        if (row->flags & ASTRA_VOLUME_USAGE_CLEANUP_PENDING) failed |= fputs(" [cleanup pending]", stdout) == EOF;
        if (row->flags & ASTRA_VOLUME_USAGE_INVALID_NAME) failed |= fputs(" [name not representable]", stdout) == EOF;
        failed |= putchar('\n') == EOF;
    }
    astra_runtime_deallocate(processes);
    astra_volume_usage_snapshot_close(&snapshot);
    return failed || fflush(stdout) != 0 ? 1 : 0;
}
