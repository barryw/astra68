#define _GNU_SOURCE
#include "astra_storage_work.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 5 || (strcmp(argv[1], "init") != 0 &&
        strcmp(argv[1], "create") != 0 && strcmp(argv[1], "delete") != 0))
        return 2;
    uint32_t map = strcmp(argv[2], "mbr") == 0 ? ASTRA_VOLUME_PARTITION_MAP_MBR :
                   strcmp(argv[2], "gpt") == 0 ? ASTRA_VOLUME_PARTITION_MAP_GPT : 0u;
    if (map == 0u) return 2;
    int device = open(argv[3], O_RDWR | O_EXCL | O_CLOEXEC);
    if (device < 0) { perror("exclusive open"); return 3; }
    int executable = open(argv[4], O_RDONLY | O_CLOEXEC);
    if (executable < 0) { perror("partition tool open"); return 4; }
    uint64_t bytes = 0;
    if (ioctl(device, BLKGETSIZE64, &bytes) != 0) return 5;
    AstraStorageRecord record = {.id = 1u, .generation = 1u,
        .kind = ASTRA_VOLUME_DEVICE, .class_id = ASTRA_VOLUME_USB,
        .origin = ASTRA_VOLUME_ORIGIN_DEVICE,
        .state = ASTRA_VOLUME_PARTITIONING,
        .partition_map = strcmp(argv[1], "init") == 0 ?
            ASTRA_VOLUME_PARTITION_MAP_NONE : map,
        .capacity_bytes = bytes};
    AstraVolumePartitionEdit edit = {.map = map};
    if (strcmp(argv[1], "init") == 0)
        edit.operation = ASTRA_VOLUME_PARTITION_INITIALIZE;
    else if (strcmp(argv[1], "create") == 0) {
        edit.operation = ASTRA_VOLUME_PARTITION_CREATE;
        edit.format = ASTRA_VOLUME_FAT32;
        edit.offset_bytes = 2048u << 9;
        edit.length_bytes = 65536u << 9;
    } else {
        edit.operation = ASTRA_VOLUME_PARTITION_DELETE;
        edit.number = 1u;
    }
    AstraStorageWork work = {.result = -1};
    AstraVolumePartitionEdit invalid = edit;
    invalid.map = ASTRA_VOLUME_PARTITION_MAP_OTHER;
    assert(astra_storage_work_partition_start(&work, &record, device, executable,
        &invalid) == EINVAL);
    AstraStorageRecord wrong_size = record;
    --wrong_size.capacity_bytes;
    assert(astra_storage_work_partition_start(&work, &wrong_size, device,
        executable, &edit) == EINVAL);
    if (edit.operation == ASTRA_VOLUME_PARTITION_CREATE) {
        invalid = edit;
        invalid.length_bytes = UINT64_MAX;
        assert(astra_storage_work_partition_start(&work, &record, device,
            executable, &invalid) == EINVAL);
    }
    assert(work.pid == 0);
    int error = astra_storage_work_partition_start(&work, &record, device,
        executable, &edit);
    close(executable);
    if (error != 0) { fprintf(stderr, "edit start: %d\n", error); return 6; }
    int ready;
    do {
        ready = astra_storage_work_edit_poll(&work, &error);
        if (ready == 0) usleep(1000);
    } while (ready == 0);
    close(device);
    if (ready != 1 || error != 0) {
        fprintf(stderr, "edit worker: ready=%d error=%d\n", ready, error);
        return 7;
    }
    return 0;
}
