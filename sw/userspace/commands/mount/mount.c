#include "../volumes/volume_cli.h"
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/vfs_process.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("mount", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    int image = argc == 3 && strcmp(argv[1], "--image") == 0;
    int ram = argc == 5 && strcmp(argv[1], "--ram") == 0;
    int readonly = argc == 3 && strcmp(argv[1], "--read-only") == 0;
    uint64_t bytes = 0u, inodes = 0u;
    if ((!image && !ram && !readonly && argc != 2) ||
        (ram && (!volume_cli_number(argv[3], &bytes) || !volume_cli_number(argv[4], &inodes)))) {
        fputs("usage: mount [--read-only] SOURCE_ID\n"
              "       mount --image NATIVE_FILE\n"
              "       mount --ram SOURCE_ID BYTES INODES\n", stderr);
        return 2;
    }
    VolumeCli cli;
    if (!volume_cli_open(&cli, "mount")) return 1;
    AstraVolumeReference source = {0};
    AstraVolumeRecord record = {0};
    int success = 0;
    if (image) {
        AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
        AstraFile file = ASTRA_FILE_INIT;
        uint32_t status = astra_process_filesystem_open(&native, astra_posix_startup());
        if (status == ASTRA_VFS_OK)
            status = astra_filesystem_open(&native.filesystem, argv[2], ASTRA_VFS_OPEN_READ, &file);
        if (status != ASTRA_VFS_OK) {
            fprintf(stderr, "mount: cannot open native image '%s': %s\n", argv[2], astra_vfs_status_text(status));
        } else {
            AstraResult result = astra_volume_image(cli.administrator, &file, &source);
            uint32_t closed = astra_filesystem_close(&file);
            if (result != ASTRA_OK || closed != ASTRA_VFS_OK)
                fprintf(stderr, "mount: image admission failed (%ld, close %u)\n", (long)result, closed);
            else if (volume_cli_wait(&cli, &source, ASTRA_VOLUME_UNMOUNTED, 0, &record))
                success = volume_cli_mount(&cli, &source, ASTRA_VOLUME_READ_ONLY, 0u, 0u);
        }
        astra_process_filesystem_close(&native);
        if (!success && source.id != 0u) {
            AstraVolumeControl release = {.reference = source, .operation = ASTRA_VOLUME_RELEASE_IMAGE_SOURCE};
            AstraVolumeReference retired;
            AstraResult result = astra_volume_control(cli.administrator, &release, &retired);
            if (result != ASTRA_OK)
                fprintf(stderr, "mount: image source %llu cleanup admission failed (%ld); inspect volumes --all\n",
                    (unsigned long long)source.id, (long)result);
            else if (!volume_cli_wait(&cli, &source, 0u, 1, &record))
                fprintf(stderr, "mount: image source %llu cleanup incomplete; inspect volumes --all\n",
                    (unsigned long long)source.id);
        }
    } else if (volume_cli_select(&cli, argv[ram || readonly ? 2 : 1], ASTRA_VOLUME_SOURCE,
        &record, &source, NULL)) {
        uint32_t flags = readonly || record.class_id == ASTRA_VOLUME_OPTICAL ? ASTRA_VOLUME_READ_ONLY : 0u;
        success = volume_cli_mount(&cli, &source, flags, bytes, inodes);
    }
    volume_cli_close(&cli);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
