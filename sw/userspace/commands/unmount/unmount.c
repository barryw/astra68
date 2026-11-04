#include "../volumes/volume_cli.h"
#include <astra/network_library.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("unmount", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    int eject = argc == 3 && strcmp(argv[1], "--eject") == 0;
    int source_only = argc == 3 && strcmp(argv[1], "--image-source") == 0;
    int network_source = argc == 3 && strcmp(argv[1], "--network-source") == 0;
    int saved = argc == 3 && strcmp(argv[1], "--saved") == 0;
    if (argc != 2 && !eject && !source_only && !network_source && !saved) {
        fputs("usage: unmount [--eject] /VOLUME_OR_MOUNT_ID\n"
              "       unmount --saved MAPPING_ID\n"
              "       unmount --image-source SOURCE_ID\n"
              "       unmount --network-source SOURCE_ID\n", stderr);
        return 2;
    }
    VolumeCli cli;
    if (!volume_cli_open(&cli, "unmount")) return 1;
    AstraVolumeRecord record = {0};
    AstraVolumeReference reference, retired;
    int success = 0;
    if (saved) {
        uint64_t id = 0u;
        const AstraStartupCapability *admin = astra_startup_capability(
            astra_posix_startup(), ASTRA_CAPABILITY_NETWORK_ADMIN);
        AstraNetworkSession session = ASTRA_NETWORK_SESSION_INIT;
        AstraNetworkMappingVolume volume;
        if (!volume_cli_number(argv[2], &id) || id == 0u ||
            id > UINT32_MAX || admin == NULL ||
            astra_network_session_open(admin->handle, &session) !=
                ASTRA_NETWORK_OK)
            fputs("unmount: saved mapping ID or network admin unavailable\n",
                  stderr);
        else {
            AstraNetworkStatus status = astra_network_mapping_unmount(
                &session, (uint32_t)id, &volume);
            if (status == ASTRA_NETWORK_BUSY && volume.mount.id != 0u)
                volume_cli_report_busy(&cli, &volume.mount, 1);
            if (status != ASTRA_NETWORK_OK &&
                status != ASTRA_NETWORK_IN_PROGRESS)
                fprintf(stderr, "unmount: saved mapping refused (%u)\n",
                        (unsigned)status);
            else {
                int settled = volume.mount.id == 0u ||
                    volume_cli_wait(&cli, &volume.mount,
                        ASTRA_VOLUME_UNMOUNTED, 1, &record);
                if (settled)
                    success = printf("unmounted saved mapping %llu\n",
                                     (unsigned long long)id) >= 0;
                else if (record.status == ASTRA_STATUS_BUSY)
                    volume_cli_report_busy(&cli, &volume.mount, 1);
            }
            (void)astra_network_session_close(&session);
        }
        goto done;
    }
    if (!volume_cli_select(&cli, argv[eject || source_only || network_source ? 2 : 1],
        source_only || network_source ? ASTRA_VOLUME_SOURCE : ASTRA_VOLUME_MOUNT,
        &record, &reference, NULL)) goto done;
    if (!source_only && !network_source &&
        (record.flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u) {
        fprintf(stderr, "unmount: %s belongs to the running system and cannot be removed\n", record.name);
        goto done;
    }
    if (source_only || network_source) {
        AstraVolumeControl control = {.reference = reference,
            .operation = source_only ? ASTRA_VOLUME_RELEASE_IMAGE_SOURCE :
                ASTRA_VOLUME_RELEASE_NETWORK_SOURCE};
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
    success = printf("%s %llu:%llu:%llu\n", eject ? "ejected" : source_only ?
        "released image source" : network_source ? "released network source" : "unmounted",
        (unsigned long long)reference.epoch, (unsigned long long)reference.id,
        (unsigned long long)reference.generation) >= 0;
done:
    volume_cli_close(&cli);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
