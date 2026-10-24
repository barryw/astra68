#include "../volumes/volume_cli.h"
#include <astra/dos_library.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("disk", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static void usage(void)
{
    fputs("usage: disk list\n"
          "       disk info ID\n"
          "       disk init EPOCH:ID:GEN mbr|gpt --confirm ERASE\n"
          "       disk create EPOCH:ID:GEN fat32|exfat SIZE_MIB --confirm ERASE\n"
          "       disk delete EPOCH:ID:GEN --confirm ERASE\n"
          "       disk format EPOCH:ID:GEN fat32|exfat LABEL --confirm ERASE\n",
          stderr);
}

static const char *state(uint32_t value)
{
    static const char *const names[] = {"?", "discovered", "probing",
        "unmounted", "mounting", "mounted", "quiescing", "lost", "failed",
        "formatting", "partitioning"};
    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}

static const char *reason(AstraResult result)
{
    switch (result) {
    case ASTRA_ERROR_BUSY: return "disk or partition in use; unmount and inspect lsof";
    case ASTRA_ERROR_PERMISSION: return "system or write-protected disk";
    case ASTRA_ERROR_INVALID_HANDLE: return "selection changed or disappeared";
    case ASTRA_ERROR_INVALID_ARGUMENT: return "invalid operation, label, or partition size";
    case ASTRA_ERROR_NO_RESOURCES: return "no free aligned extent or provider capacity";
    case ASTRA_ERROR_TIMEOUT: return "operation still pending; inspect disk list";
    default: return "device or provider error; inspect disk list";
    }
}

static const AstraVolumeRecord *by_id(const AstraDosDiskSnapshot *snapshot,
    uint64_t id)
{
    for (uint32_t at = 0u; at < snapshot->volumes.count; ++at)
        if (snapshot->volumes.records[at].id == id)
            return &snapshot->volumes.records[at];
    return NULL;
}

static const AstraVolumeRecord *mount_of(const AstraDosDiskSnapshot *snapshot,
    const AstraVolumeRecord *source)
{
    for (uint32_t at = 0u; at < snapshot->volumes.count; ++at) {
        const AstraVolumeRecord *row = &snapshot->volumes.records[at];
        if (row->kind == ASTRA_VOLUME_MOUNT && row->parent == source->id &&
            row->parent_generation == source->generation &&
            (row->state == ASTRA_VOLUME_MOUNTING ||
             row->state == ASTRA_VOLUME_MOUNTED ||
             row->state == ASTRA_VOLUME_QUIESCING)) return row;
    }
    return NULL;
}

static int print_row(const AstraDosDiskSnapshot *snapshot,
    const AstraVolumeRecord *row)
{
    const AstraVolumeRecord *mount = row->kind == ASTRA_VOLUME_SOURCE ?
        mount_of(snapshot, row) : NULL;
    const char *format = astra_volume_source_issue(row) == ASTRA_VOLUME_SOURCE_NDOS ?
        "NDOS" : row->format == ASTRA_VOLUME_UNSUPPORTED ? row->detected_type :
        astra_volume_format_name(row->format);
    if (printf("%llu:%llu:%llu %-9s %-4u %-5s %-10s %-11llu %-11llu %-11s %-5s %-8s ",
        (unsigned long long)snapshot->volumes.epoch,
        (unsigned long long)row->id,
        (unsigned long long)row->generation,
        row->kind == ASTRA_VOLUME_DEVICE ? "disk" : "partition",
        row->partition_number, astra_volume_partition_map_name(row->partition_map),
        format, (unsigned long long)row->offset_bytes,
        (unsigned long long)row->capacity_bytes,
        state(mount == NULL ? row->state : mount->state),
        mount == NULL ? "-" : (mount->flags & ASTRA_VOLUME_READ_ONLY) != 0u ?
            "ro" : "rw", mount == NULL ? "-" : mount->name) < 0)
        return 0;
    uint32_t length = 0u;
    while (length < sizeof(row->label) && row->label[length] != '\0') ++length;
    return volume_cli_print_bytes(row->label, length) &&
        putchar('\n') != EOF;
}

static int list(const AstraDosDiskSnapshot *snapshot)
{
    if (puts("REFERENCE KIND      PART MAP   FORMAT     START_BYTES SIZE_BYTES  STATE       ACCESS MOUNT    LABEL") == EOF)
        return 0;
    for (uint32_t at = 0u; at < snapshot->volumes.count; ++at) {
        const AstraVolumeRecord *disk = &snapshot->volumes.records[at];
        if (disk->kind != ASTRA_VOLUME_DEVICE) continue;
        if (!print_row(snapshot, disk)) return 0;
        for (uint32_t source = 0u; source < snapshot->volumes.count; ++source) {
            const AstraVolumeRecord *partition = &snapshot->volumes.records[source];
            if (partition->kind != ASTRA_VOLUME_SOURCE ||
                partition->origin != ASTRA_VOLUME_ORIGIN_DEVICE) continue;
            const AstraVolumeReference ref = {snapshot->volumes.epoch,
                partition->id, partition->generation};
            if (astra_volume_snapshot_ancestor(&snapshot->volumes, &ref,
                ASTRA_VOLUME_DEVICE) == disk && !print_row(snapshot, partition))
                return 0;
        }
    }
    return fflush(stdout) == 0;
}

static int info(const AstraDosDiskSnapshot *snapshot, const AstraVolumeRecord *row)
{
    if (!print_row(snapshot, row)) return 0;
    uint32_t mounts = 0u, holders = 0u;
    for (uint32_t at = 0u; at < snapshot->volumes.count; ++at) {
        const AstraVolumeRecord *mount = &snapshot->volumes.records[at];
        if (mount->kind != ASTRA_VOLUME_MOUNT ||
            mount->state != ASTRA_VOLUME_MOUNTED) continue;
        const AstraVolumeReference ref = {snapshot->volumes.epoch, mount->id,
            mount->generation};
        if (astra_volume_snapshot_ancestor(&snapshot->volumes, &ref, row->kind) != row)
            continue;
        ++mounts;
        for (uint32_t held = 0u; held < snapshot->usage.count; ++held) {
            const AstraVolumeUsageRecord *use = &snapshot->usage.records[held];
            if (use->volume.epoch != ref.epoch || use->volume.id != ref.id ||
                use->volume.generation != ref.generation) continue;
            ++holders;
            if (printf("  PID %lu holds ", (unsigned long)use->process) < 0 ||
                !volume_cli_print_bytes(use->path, use->path_length) ||
                putchar('\n') == EOF) return 0;
        }
    }
    return printf("Mounted: %lu  Retained resources: %lu\n",
        (unsigned long)mounts, (unsigned long)holders) >= 0 &&
        fflush(stdout) == 0;
}

static int parse_request(int argc, char **argv, AstraDosDiskRequest *request)
{
    uint64_t mib = 0u;
    if (argc < 4) return 0;
    size_t length = strlen(argv[2]);
    char reference[64];
    if (length >= sizeof(reference)) return 0;
    memcpy(reference, argv[2], length + 1u);
    char *id = strchr(reference, ':');
    if (id == NULL) return 0;
    *id++ = '\0';
    char *generation = strchr(id, ':');
    if (generation == NULL) return 0;
    *generation++ = '\0';
    if (!astra_volume_reference_parse(reference, id, generation,
        &request->target)) return 0;
    if (strcmp(argv[1], "init") == 0 && argc == 6 &&
        strcmp(argv[4], "--confirm") == 0 && strcmp(argv[5], "ERASE") == 0) {
        request->operation = strcmp(argv[3], "mbr") == 0 ?
            ASTRA_DOS_DISK_INITIALIZE_MBR : strcmp(argv[3], "gpt") == 0 ?
            ASTRA_DOS_DISK_INITIALIZE_GPT : 0u;
    } else if (strcmp(argv[1], "create") == 0 && argc == 7 &&
        strcmp(argv[5], "--confirm") == 0 && strcmp(argv[6], "ERASE") == 0 &&
        volume_cli_number(argv[4], &mib) && mib <= UINT64_MAX / 1048576u) {
        request->operation = strcmp(argv[3], "fat32") == 0 ?
            ASTRA_DOS_DISK_CREATE_FAT32 : strcmp(argv[3], "exfat") == 0 ?
            ASTRA_DOS_DISK_CREATE_EXFAT : 0u;
        request->size_bytes = mib * 1048576u;
    } else if (strcmp(argv[1], "delete") == 0 && argc == 5 &&
        strcmp(argv[3], "--confirm") == 0 && strcmp(argv[4], "ERASE") == 0) {
        request->operation = ASTRA_DOS_DISK_DELETE_PARTITION;
    } else if (strcmp(argv[1], "format") == 0 && argc == 7 &&
        strcmp(argv[5], "--confirm") == 0 && strcmp(argv[6], "ERASE") == 0) {
        request->operation = strcmp(argv[3], "fat32") == 0 ?
            ASTRA_DOS_DISK_FORMAT_FAT32 : strcmp(argv[3], "exfat") == 0 ?
            ASTRA_DOS_DISK_FORMAT_EXFAT : 0u;
        request->label = argv[4];
    }
    return request->operation != 0u;
}

int main(int argc, char **argv)
{
    int listing = argc == 2 && strcmp(argv[1], "list") == 0;
    int showing = argc == 3 && strcmp(argv[1], "info") == 0;
    AstraDosDiskRequest request = {0};
    if (!listing && !showing && !parse_request(argc, argv, &request)) {
        usage();
        return 2;
    }
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *metadata = astra_startup_capability(startup,
        ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *usage_cap = astra_startup_capability(startup,
        ASTRA_CAPABILITY_VOLUME_USAGE);
    const AstraStartupCapability *administrator = astra_startup_capability(startup,
        ASTRA_CAPABILITY_DISK_ADMIN);
    if (metadata == NULL || usage_cap == NULL ||
        (!listing && !showing && administrator == NULL)) {
        fputs("disk: volume inventory or disk administration capability unavailable\n", stderr);
        return 1;
    }
    AstraDosDiskSnapshot snapshot = ASTRA_DOS_DISK_SNAPSHOT_INIT;
    AstraResult result = ASTRA_ERROR_BUSY;
    for (uint32_t retry = 0u; retry < 8u && result == ASTRA_ERROR_BUSY; ++retry) {
        result = astra_dos_disk_snapshot_open(metadata->handle, usage_cap->handle,
            &snapshot);
        if (result == ASTRA_ERROR_BUSY)
            (void)astra_rt_thread_sleep(UINT64_C(50000000),
                ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
    if (result != ASTRA_OK) {
        fprintf(stderr, "disk: cannot read coherent inventory (%ld)\n", (long)result);
        return 1;
    }
    int success = 0;
    if (listing) success = list(&snapshot);
    else {
        uint64_t id = request.target.id;
        if (showing && !volume_cli_number(argv[2], &id)) {
            usage();
            astra_dos_disk_snapshot_close(&snapshot);
            return 2;
        }
        const AstraVolumeRecord *row = showing ? by_id(&snapshot, id) :
            astra_volume_snapshot_find(&snapshot.volumes, &request.target);
        if (row == NULL || (row->kind != ASTRA_VOLUME_DEVICE &&
            (row->kind != ASTRA_VOLUME_SOURCE ||
             row->origin != ASTRA_VOLUME_ORIGIN_DEVICE))) {
            fprintf(stderr, "disk: physical disk or partition ID %llu not found\n",
                (unsigned long long)id);
        } else if (showing) success = info(&snapshot, row);
        else {
            AstraDosDiskJob job = {0};
            result = astra_dos_disk_submit(administrator->handle, &snapshot,
                &request, &job);
            if (result == ASTRA_OK) {
                uint64_t deadline = astra_clock_monotonic() + UINT64_C(320000000000);
                for (;;) {
                    AstraResult outcome = ASTRA_ERROR_BUSY;
                    result = astra_dos_disk_status(administrator->handle,
                        &job, &outcome);
                    if (result != ASTRA_OK) break;
                    if (outcome != ASTRA_ERROR_BUSY) { result = outcome; break; }
                    if (astra_clock_monotonic() >= deadline) {
                        result = ASTRA_ERROR_TIMEOUT;
                        break;
                    }
                    (void)astra_rt_thread_sleep(UINT64_C(100000000),
                        ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
                }
            }
            if (result == ASTRA_OK) success = puts(
                request.operation == ASTRA_DOS_DISK_CREATE_FAT32 ||
                request.operation == ASTRA_DOS_DISK_CREATE_EXFAT ?
                "disk: partition created; run disk list, then format its new source" :
                "disk: operation complete") != EOF;
            else fprintf(stderr, "disk: %s (%ld)\n", reason(result), (long)result);
        }
    }
    astra_dos_disk_snapshot_close(&snapshot);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
