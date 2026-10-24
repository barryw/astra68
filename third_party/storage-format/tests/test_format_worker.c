#define _GNU_SOURCE
#include "astra_storage_work.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 6 || (strcmp(argv[1], "fat") != 0 && strcmp(argv[1], "exfat") != 0))
        return 2;
    int device = open(argv[2], O_RDWR | O_EXCL | O_CLOEXEC);
    if (device < 0) { perror("exclusive open"); return 3; }
    int executable = open(argv[3], O_RDONLY | O_CLOEXEC);
    if (executable < 0) { perror("formatter open"); return 4; }
    uid_t uid = (uid_t)strtoul(argv[4], NULL, 10);
    gid_t gid = (gid_t)strtoul(argv[5], NULL, 10);
    AstraStorageRecord source = {.id = 1u, .generation = 1u,
        .kind = ASTRA_VOLUME_SOURCE, .origin = ASTRA_VOLUME_ORIGIN_DEVICE,
        .state = ASTRA_VOLUME_FORMATTING};
    AstraStorageWork work = {.result = -1};
    assert(astra_storage_work_format_start(&work, &source, device, executable,
        ASTRA_VOLUME_FAT32, "INVALID/LABEL", uid, gid) == EINVAL);
    assert(work.pid == 0);
    int error = astra_storage_work_format_start(&work, &source, device, executable,
        strcmp(argv[1], "fat") == 0 ? ASTRA_VOLUME_FAT32 : ASTRA_VOLUME_EXFAT,
        "ASTRA_TEST", uid, gid);
    close(executable);
    if (error != 0) { fprintf(stderr, "format start: %d\n", error); return 5; }
    int ready;
    do {
        ready = astra_storage_work_edit_poll(&work, &error);
        if (ready == 0) usleep(1000);
    } while (ready == 0);
    close(device);
    if (ready != 1 || error != 0) {
        fprintf(stderr, "format worker: ready=%d error=%d\n", ready, error);
        return 6;
    }
    return 0;
}
