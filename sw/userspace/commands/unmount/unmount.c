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
    AstraVolumeReference reference, retired;
    int success = 0;
    if (!volume_cli_select(&cli, argv[eject || source_only ? 2 : 1],
        source_only ? ASTRA_VOLUME_SOURCE : ASTRA_VOLUME_MOUNT, &record, &reference, NULL)) goto done;
    if (!source_only && (record.flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u) {
        fprintf(stderr, "unmount: %s belongs to the running system and cannot be removed\n", record.name);
        goto done;
    }
    if (source_only) {
        AstraVolumeControl control = {.reference = reference,
            .operation = ASTRA_VOLUME_RELEASE_IMAGE_SOURCE};
        if (!volume_cli_apply(&cli, &control, &retired) ||
            !volume_cli_wait(&cli, &reference, 0u, 1, &record)) goto done;
    } else {
        AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
        AstraResult result = astra_volume_snapshot_open(cli.metadata, &catalog);
        if (result != ASTRA_OK) {
            fprintf(stderr, "unmount: catalog unavailable (%ld)\n", (long)result);
            goto done;
        }
        AstraVolumeAction action = {0};
        result = astra_volume_action_begin(cli.administrator, cli.inspector,
            &catalog, &reference, eject, &action);
        astra_volume_snapshot_close(&catalog);
        if (result != ASTRA_OK) {
            if (result == ASTRA_ERROR_BUSY)
                volume_cli_report_busy(&cli, action.failure_mount.id != 0u ?
                    &action.failure_mount : &reference, 1);
            else if (eject && result == ASTRA_ERROR_INVALID_ARGUMENT)
                fputs("unmount: eject requires a physical optical volume\n", stderr);
            else fprintf(stderr, "unmount: lifecycle admission failed (%ld)\n", (long)result);
            goto done;
        }
        if (!volume_cli_wait_action(&cli, &action)) goto done;
    }
    success = printf("%s %llu:%llu:%llu\n", eject ? "ejected" : source_only ? "released image source" : "unmounted",
        (unsigned long long)reference.epoch, (unsigned long long)reference.id,
        (unsigned long long)reference.generation) >= 0;
done:
    volume_cli_close(&cli);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
