#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/volume.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("volumes", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static const char *state(uint32_t value)
{
    static const char *const names[] = {"?", "discovered", "probing", "unmounted",
        "mounting", "mounted", "quiescing", "lost", "failed"};
    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}
static const char *kind(uint32_t value)
{
    static const char *const names[] = {"?", "device", "medium", "source", "mount", "binding"};
    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}
static const char *format(uint32_t value)
{
    static const char *const names[] = {"unknown", "ext4", "FAT32", "exFAT", "ISO9660", "UDF", "tmpfs", "FAT12", "FAT16"};
    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}
static int label(const char *text)
{
    for (; *text != '\0'; ++text) {
        unsigned char byte = (unsigned char)*text;
        if (putchar(byte < 32u || byte == 127u ? '?' : byte) == EOF) return 0;
    }
    return putchar('\n') != EOF;
}

int main(int argc, char **argv)
{
    int all = 0, references = 0;
    for (int at = 1; at < argc; ++at) {
        if (!all && strcmp(argv[at], "--all") == 0) all = 1;
        else if (!references && strcmp(argv[at], "--references") == 0) references = 1;
        else {
            fputs("usage: volumes [--all] [--references]\n", stderr);
            return 2;
        }
    }
    const AstraStartupCapability *capability = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUMES);
    if (capability == NULL) {
        fputs("volumes: volume metadata capability unavailable\n", stderr);
        return 1;
    }
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(capability->handle, &snapshot);
    if (result != ASTRA_OK) {
        fprintf(stderr, "volumes: catalog query failed (%ld)%s\n", (long)result,
            result == ASTRA_ERROR_BUSY ? "; catalog changed, retry" : "");
        return 1;
    }
    int failed = printf(references ? all ? "EPOCH ID GENERATION KIND PARENT PARENT_GENERATION FLAGS NAME\n" : "EPOCH ID GENERATION NAME\n" :
        "ID    KIND     STATE       NAME       FORMAT   ACCESS  LABEL\n") < 0;
    for (uint32_t at = 0u; at < snapshot.count && !failed; ++at) {
        const AstraVolumeRecord *record = &snapshot.records[at];
        if (!all && (record->kind != ASTRA_VOLUME_MOUNT ||
            (record->state != ASTRA_VOLUME_MOUNTED && record->state != ASTRA_VOLUME_QUIESCING)))
            continue;
        if (references) {
            failed = printf("%llu %llu %llu ", (unsigned long long)snapshot.epoch,
                (unsigned long long)record->id, (unsigned long long)record->generation) < 0;
            if (all)
                failed |= printf("%s %llu %llu %u ", kind(record->kind),
                    (unsigned long long)record->parent, (unsigned long long)record->parent_generation,
                    record->flags) < 0;
            failed |= printf("%s\n", record->name[0] != '\0' ? record->name : "-") < 0;
            continue;
        }
        failed = printf("%-5llu %-8s %-11s %-10s %-8s %-7s ",
            (unsigned long long)record->id, kind(record->kind), state(record->state),
            record->name[0] != '\0' ? record->name : "-", format(record->format),
            (record->flags & ASTRA_VOLUME_READ_ONLY) != 0u ? "ro" : "rw") < 0 ||
            !label(record->label);
    }
    astra_volume_snapshot_close(&snapshot);
    return failed || fflush(stdout) != 0 ? 1 : 0;
}
