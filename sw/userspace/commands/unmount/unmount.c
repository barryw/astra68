#include "../volumes/volume_cli.h"
#include <astra/program.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("unmount", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    int eject = argc == 3 && strcmp(argv[1], "--eject") == 0;
    int source_only = argc == 3 && strcmp(argv[1], "--image-source") == 0;
    if (argc != 2 && !eject && !source_only) {
        fputs("usage: unmount [--eject] /VOLUME_OR_MOUNT_ID\n"
              "       unmount --image-source SOURCE_ID\n", stderr);
        return 2;
    }
    VolumeCli cli;
    if (!volume_cli_open(&cli, "unmount")) return 1;
    AstraVolumeRecord record = {0};
    AstraVolumeReference reference, parent, retired;
    int success = 0;
    if (!volume_cli_select(&cli, argv[eject || source_only ? 2 : 1],
        source_only ? ASTRA_VOLUME_SOURCE : ASTRA_VOLUME_MOUNT, &record, &reference, &parent)) goto done;
    uint32_t origin = record.origin;
    if (!source_only && (record.flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u) {
        fprintf(stderr, "unmount: %s belongs to the running system and cannot be removed\n", record.name);
        goto done;
    }
    if (eject && (record.class_id != ASTRA_VOLUME_OPTICAL || origin != ASTRA_VOLUME_ORIGIN_DEVICE)) {
        fputs("unmount: eject requires a physical optical volume\n", stderr);
        goto done;
    }
    AstraVolumeControl control = {.reference = reference,
        .operation = source_only ? ASTRA_VOLUME_RELEASE_IMAGE_SOURCE : ASTRA_VOLUME_UNMOUNT};
    if (!volume_cli_apply(&cli, &control, &retired) ||
        !volume_cli_wait(&cli, &reference, source_only ? 0u : ASTRA_VOLUME_UNMOUNTED, 1, &record)) goto done;
    if (eject || (!source_only && origin == ASTRA_VOLUME_ORIGIN_IMAGE)) {
        control.reference = parent;
        control.operation = eject ? ASTRA_VOLUME_EJECT : ASTRA_VOLUME_RELEASE_IMAGE_SOURCE;
        if (!volume_cli_apply(&cli, &control, &retired) ||
            !volume_cli_wait(&cli, &parent, 0u, 1, &record)) goto done;
    }
    success = printf("%s %llu:%llu:%llu\n", eject ? "ejected" : source_only ? "released image source" : "unmounted",
        (unsigned long long)reference.epoch, (unsigned long long)reference.id,
        (unsigned long long)reference.generation) >= 0;
done:
    volume_cli_close(&cli);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
