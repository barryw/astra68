#define _GNU_SOURCE
#include "astra_storage_provider.h"
#include "astra_storage_discovery.h"
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <libudev.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

static void wait_work(AstraStorageProvider *provider)
{
    for (unsigned at = 0u; provider->workers != 0u && at < 10000u; ++at) {
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    assert(provider->workers == 0u);
}

static int mount_wait(AstraStorageProvider *provider, uint64_t source, uint64_t generation,
                       uint32_t flags, uint64_t bytes, uint64_t inodes, uint64_t *mount)
{
    int error = astra_storage_provider_mount(provider, source, generation, flags, bytes, inodes, mount);
    if (error == 0) {
        wait_work(provider);
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, *mount);
        return record->state == ASTRA_VOLUME_MOUNTED ? 0 : record->error;
    }
    return error;
}

static int unmount_wait(AstraStorageProvider *provider, uint64_t mount, uint64_t generation)
{
    int error = astra_storage_provider_unmount(provider, mount, generation);
    if (error == 0) {
        wait_work(provider);
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, mount);
        return record->state == ASTRA_VOLUME_UNMOUNTED ? 0 : record->error;
    }
    return error;
}

static void poll_probes(AstraStorageProvider *provider)
{
    do {
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    } while (provider->probes != 0u);
}

static void discovered_media(AstraStorageProvider *provider, const char *image)
{
    int file = open(image, O_RDONLY | O_CLOEXEC);
    struct stat info;
    assert(file >= 0 && fstat(file, &info) == 0);
    int loop = astra_storage_linux_loop(file, 0u, (uint64_t)info.st_size, 1);
    close(file);
    assert(loop >= 0 && fstat(loop, &info) == 0);
    uint64_t disk_sequence;
    assert(ioctl(loop, BLKGETDISKSEQ, &disk_sequence) == 0);
    char node[64];
    snprintf(node, sizeof(node), "/dev/loop%u", minor(info.st_rdev));
    AstraStorageDiscovery discovery = {.provider = provider, .byte_limit = 1u << 20,
        .names = ASTRA_STORAGE_NAMES_INIT, .fd = -1};
    AstraStorageObservation observation = {.syspath = "/sys/devices/test/optical", .disk_syspath = "/sys/devices/test/optical",
        .devnode = node, .devnum = info.st_rdev, .disk_devnum = info.st_rdev,
        .sequence = 1u, .disk_sequence = disk_sequence,
        .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_OPTICAL, .media = 1};
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    poll_probes(provider);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    wait_work(provider);
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count;
    uint64_t next, sequence, mounted = 0u;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].kind == ASTRA_VOLUME_MOUNT && rows[at].origin == ASTRA_VOLUME_ORIGIN_DEVICE) {
            assert(rows[at].state == ASTRA_VOLUME_MOUNTED && (rows[at].flags & ASTRA_VOLUME_READ_ONLY));
            mounted = rows[at].id;
        }
    assert(mounted != 0u);
    uint64_t old_source = astra_storage_find(&provider->catalog, mounted)->parent;
    int root;
    assert(astra_storage_provider_root(provider, mounted, 1u, &root) == 0);
    assert(openat(root, "forbidden", O_CREAT | O_WRONLY, 0600) == -1 && errno == EROFS);
    /* An actual Linux directory reference must keep lost media charged. */
    ++observation.sequence;
    observation.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    observation.media = 0;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    int stale;
    assert(astra_storage_provider_root(provider, mounted, 1u, &stale) == ESTALE);
    assert(astra_storage_provider_reap(provider) == EAGAIN);
    wait_work(provider);
    assert(astra_storage_find(&provider->catalog, mounted) != NULL);
    ++observation.sequence;
    observation.media = 1;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    poll_probes(provider);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    uint64_t replacement = 0u;
    for (size_t at = 0u; at < count; ++at) {
        if (rows[at].kind == ASTRA_VOLUME_SOURCE &&
            rows[at].origin == ASTRA_VOLUME_ORIGIN_DEVICE &&
            rows[at].id != old_source && rows[at].state != ASTRA_VOLUME_LOST)
            replacement = rows[at].id;
        if (rows[at].kind == ASTRA_VOLUME_MOUNT &&
            rows[at].origin == ASTRA_VOLUME_ORIGIN_DEVICE &&
            rows[at].id != mounted)
            assert(rows[at].state != ASTRA_VOLUME_MOUNTING &&
                rows[at].state != ASTRA_VOLUME_MOUNTED);
    }
    assert(replacement != 0u); /* old held Linux mount fences replacement */
    close(root);
    for (unsigned at = 0u; at < 10000u && astra_storage_find(&provider->catalog, mounted) != NULL; ++at) {
        int reaped = astra_storage_provider_reap(provider);
        assert(reaped == 0 || reaped == EAGAIN);
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    assert(astra_storage_find(&provider->catalog, mounted) == NULL);
    assert(astra_storage_discovery_poll(&discovery) == 0);
    wait_work(provider);
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    uint64_t replacement_mount = 0u;
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].kind == ASTRA_VOLUME_MOUNT && rows[at].parent == replacement &&
            rows[at].state == ASTRA_VOLUME_MOUNTED)
            replacement_mount = rows[at].id;
    assert(replacement_mount != 0u && replacement_mount != mounted);
    assert(astra_storage_provider_root(provider, replacement_mount, 1u, &root) == 0);
    assert(openat(root, "forbidden", O_CREAT | O_WRONLY, 0600) == -1 && errno == EROFS);
    close(root);
    assert(unmount_wait(provider, replacement_mount, 1u) == 0);
    assert(astra_storage_provider_eject(provider, replacement, 1u) == 0);
    wait_work(provider);
    const AstraStorageRecord *failed_eject = astra_storage_find(&provider->catalog, replacement);
    assert(failed_eject != NULL && failed_eject->state == ASTRA_VOLUME_UNMOUNTED &&
        failed_eject->error == EINVAL);
    ++observation.sequence;
    observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    assert(astra_storage_provider_reap(provider) == 0);
    astra_storage_discovery_close(&discovery);
    int released;
    for (unsigned attempt = 0u;; ++attempt) {
        released = astra_storage_linux_loop_release(&loop);
        if (released != EAGAIN) break;
        assert(attempt < 10000u);
        usleep(1000);
    }
    if (released != 0) fprintf(stderr, "discovered loop release error: %d\n", released);
    assert(released == 0);
}

static void exclusive_format_target(AstraStorageProvider *provider, const char *image,
    int format_gate)
{
    int file = open(image, O_RDWR | O_CLOEXEC);
    struct stat info;
    assert(file >= 0 && fstat(file, &info) == 0);
    uint64_t capacity = (uint64_t)info.st_size;
    int loop = astra_storage_linux_loop(file, 0u, capacity, 0);
    close(file);
    assert(loop >= 0 && fstat(loop, &info) == 0);
    uint64_t disk_sequence = 0u;
    assert(ioctl(loop, BLKGETDISKSEQ, &disk_sequence) == 0);
    char node[64];
    snprintf(node, sizeof(node), "/dev/loop%u", minor(info.st_rdev));
    AstraStorageDiscovery discovery = {.provider = provider, .byte_limit = 1u << 20,
        .names = ASTRA_STORAGE_NAMES_INIT, .fd = -1};
    AstraStorageObservation observation = {.syspath = "/sys/devices/test/format-loop",
        .disk_syspath = "/sys/devices/test/format-loop", .devnode = node,
        .devnum = info.st_rdev, .disk_devnum = info.st_rdev,
        .sequence = 1u, .disk_sequence = disk_sequence,
        .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_USB,
        .media = 1, .capacity_bytes = capacity};
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count = 0u;
    uint64_t next = 0u, sequence = 0u, source = 0u;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].kind == ASTRA_VOLUME_SOURCE && rows[at].origin == ASTRA_VOLUME_ORIGIN_DEVICE)
            source = rows[at].id;
    assert(source != 0u);
    const AstraStorageRecord *source_record = astra_storage_find(&provider->catalog, source);
    const AstraStorageRecord *medium = astra_storage_find(&provider->catalog,
        source_record->parent);
    assert(medium != NULL && medium->kind == ASTRA_VOLUME_MEDIUM);
    uint64_t device = medium->parent;
    int exclusive = -1;
    assert(astra_storage_discovery_device_fd(&discovery, device, 2u, &exclusive) == ESTALE &&
        exclusive == -1);
    assert(astra_storage_discovery_device_fd(&discovery, device, 1u, &exclusive) == EBUSY &&
        exclusive == -1);
    poll_probes(provider);
    assert(astra_storage_discovery_device_fd(&discovery, device, 1u, &exclusive) == 0);
    assert(exclusive >= 0 && fstat(exclusive, &info) == 0 && info.st_rdev == observation.devnum);
    assert((fcntl(exclusive, F_GETFL) & O_ACCMODE) == O_RDWR);
    int competing = open(node, O_RDWR | O_EXCL | O_CLOEXEC);
    assert(competing < 0 && errno == EBUSY);
    close(exclusive);
    exclusive = -1;
    assert(astra_storage_discovery_format_fd(&discovery, source, 1u, &exclusive) == 0);
    assert(exclusive >= 0 && fstat(exclusive, &info) == 0 && info.st_rdev == observation.devnum);
    assert((fcntl(exclusive, F_GETFL) & O_ACCMODE) == O_RDWR);
    competing = open(node, O_RDWR | O_EXCL | O_CLOEXEC);
    assert(competing < 0 && errno == EBUSY);
    close(exclusive);
    if (format_gate) {
        provider->discovery = &discovery;
        poll_probes(provider);
        assert(astra_storage_find(&provider->catalog, source)->state == ASTRA_VOLUME_UNMOUNTED);
        assert(astra_storage_discovery_poll(&discovery) == 0);
        wait_work(provider);
        assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
            ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
        uint64_t initial_mount = 0u;
        for (size_t row = 0u; row < count; ++row)
            if (rows[row].kind == ASTRA_VOLUME_MOUNT && rows[row].parent == source &&
                rows[row].state == ASTRA_VOLUME_MOUNTED) initial_mount = rows[row].id;
        assert(initial_mount != 0u);
        assert(astra_storage_discovery_device_fd(&discovery, device, 1u, &exclusive) == EBUSY &&
            exclusive == -1);
        assert(unmount_wait(provider, initial_mount, 1u) == 0);
        const uint32_t formats[] = {ASTRA_VOLUME_EXFAT, ASTRA_VOLUME_FAT32};
        for (unsigned at = 0u; at < 2u; ++at) {
            const AstraStorageRecord *before = astra_storage_find(&provider->catalog, source);
            uint64_t generation = before->generation;
            assert(astra_storage_provider_format(provider, source, generation,
                formats[at], "ASTRA_TEST") == 0);
            int outcome = 0;
            assert(astra_storage_provider_format_status(provider, source, generation,
                &outcome) == 0 && outcome == EAGAIN);
            ++observation.sequence;
            observation.action = ASTRA_STORAGE_OBSERVE_CHANGE;
            assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
            assert(astra_storage_find(&provider->catalog, source)->state == ASTRA_VOLUME_FORMATTING);
            wait_work(provider);
            poll_probes(provider);
            const AstraStorageRecord *formatted = astra_storage_find(&provider->catalog, source);
            assert(formatted != NULL && formatted->generation == generation + 1u &&
                formatted->state == ASTRA_VOLUME_UNMOUNTED &&
                formatted->format == formats[at] &&
                strcmp(formatted->label, "ASTRA_TEST") == 0);
            assert(astra_storage_provider_format_status(provider, source, generation,
                &outcome) == 0 && outcome == 0);
            if (at == 1u) {
                uint64_t retired = source;
                ++observation.sequence;
                assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
                assert(astra_storage_find(&provider->catalog, retired)->state == ASTRA_VOLUME_LOST);
                poll_probes(provider);
                assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
                    ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
                for (size_t row = 0u; row < count; ++row)
                    if (rows[row].kind == ASTRA_VOLUME_SOURCE &&
                        rows[row].origin == ASTRA_VOLUME_ORIGIN_DEVICE &&
                        rows[row].state == ASTRA_VOLUME_UNMOUNTED) source = rows[row].id;
                assert(source != retired);
                const AstraStorageRecord *target = astra_storage_find(&provider->catalog, source);
                uint64_t sibling = 0u;
                assert(astra_storage_source(&provider->catalog, target->parent,
                    target->parent_generation, 0u, 1u, 1u << 20, 8u << 20, &sibling) == 0);
                assert(astra_storage_probe_result(&provider->catalog, sibling, 1u,
                    ASTRA_VOLUME_FAT32, "", "OBSTACLE", "vfat", 0, 0) == 0);
                assert(astra_storage_format_begin(&provider->catalog, sibling, 1u) == 0);
                assert(astra_storage_discovery_poll(&discovery) == 0);
                assert(astra_storage_format_abort(&provider->catalog, sibling, 1u) == 0);
                assert(astra_storage_loss(&provider->catalog, sibling, 1u) == 0);
                assert(astra_storage_provider_reap(provider) == 0);
                assert(astra_storage_discovery_poll(&discovery) == 0);
                wait_work(provider);
                assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
                    ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
                uint64_t mounted = 0u;
                for (size_t row = 0u; row < count; ++row)
                    if (rows[row].kind == ASTRA_VOLUME_MOUNT && rows[row].parent == source &&
                        rows[row].state == ASTRA_VOLUME_MOUNTED) mounted = rows[row].id;
                assert(mounted != 0u);
                assert(unmount_wait(provider, mounted, 1u) == 0);
            }
        }
        provider->discovery = NULL;
    }
    observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    ++observation.sequence;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    for (unsigned at = 0u; astra_storage_find(&provider->catalog, source) != NULL &&
         at < 10000u; ++at) {
        int error = astra_storage_provider_reap(provider);
        assert(error == 0 || error == EAGAIN);
        usleep(1000);
    }
    assert(astra_storage_find(&provider->catalog, source) == NULL);
    astra_storage_discovery_close(&discovery);
    int released;
    for (unsigned attempt = 0u;; ++attempt) {
        released = astra_storage_linux_loop_release(&loop);
        if (released != EAGAIN) break;
        assert(attempt < 10000u);
        usleep(1000);
    }
    assert(released == 0);
}

static void partition_wait(AstraStorageProvider *provider,
    uint64_t device, uint64_t generation)
{
    int last_result = EAGAIN;
    for (unsigned at = 0u; at < 10000u; ++at) {
        assert(astra_storage_provider_poll(provider) >= 0);
        int cleanup = astra_storage_provider_reap(provider);
        assert(cleanup == 0 || cleanup == EAGAIN);
        int result = EAGAIN;
        assert(astra_storage_provider_partition_status(provider, device,
            generation, &result) == 0);
        last_result = result;
        const AstraStorageRecord *record = astra_storage_find(&provider->catalog, device);
        if (record != NULL && record->state == ASTRA_VOLUME_DISCOVERED &&
            record->generation == generation + 1u) {
            assert(result == 0 && provider->workers == 0u);
            return;
        }
        assert(result == EAGAIN);
        usleep(1000);
    }
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, device);
    fprintf(stderr, "partition_wait: expected=%llu actual=%llu state=%u result=%d workers=%u\n",
        (unsigned long long)(generation + 1u),
        (unsigned long long)(record == NULL ? 0u : record->generation),
        record == NULL ? 0u : record->state, last_result, provider->workers);
    assert(0 && "partition worker did not complete kernel reread");
}

static void exclusive_partition_target(AstraStorageProvider *provider)
{
    char image[] = "/var/tmp/astra-partition-provider.XXXXXX";
    int file = mkstemp(image);
    assert(file >= 0 && ftruncate(file, 128u << 20) == 0);
    int loop = astra_storage_linux_loop(file, 0u, 128u << 20, 0);
    close(file);
    assert(loop >= 0);
    struct loop_info64 loop_info;
    assert(ioctl(loop, LOOP_GET_STATUS64, &loop_info) == 0);
    loop_info.lo_flags |= LO_FLAGS_PARTSCAN;
    assert(ioctl(loop, LOOP_SET_STATUS64, &loop_info) == 0);
    struct stat info;
    assert(fstat(loop, &info) == 0);
    uint64_t disk_sequence = 0u;
    assert(ioctl(loop, BLKGETDISKSEQ, &disk_sequence) == 0);
    char node[64];
    snprintf(node, sizeof(node), "/dev/loop%u", minor(info.st_rdev));
    AstraStorageDiscovery discovery = {.provider = provider, .byte_limit = 1u << 20,
        .names = ASTRA_STORAGE_NAMES_INIT, .fd = -1};
    AstraStorageObservation observation = {.syspath = "/sys/devices/test/partition-loop",
        .disk_syspath = "/sys/devices/test/partition-loop", .devnode = node,
        .devnum = info.st_rdev, .disk_devnum = info.st_rdev,
        .sequence = 1u, .disk_sequence = disk_sequence,
        .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_USB,
        .media = 1, .capacity_bytes = 128u << 20};
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count = 0u;
    uint64_t next = 0u, sequence = 0u, device = 0u;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, rows,
        ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = 0u; at < count; ++at)
        if (rows[at].kind == ASTRA_VOLUME_DEVICE &&
            rows[at].capacity_bytes == (128u << 20)) device = rows[at].id;
    assert(device != 0u);
    provider->discovery = &discovery;
    AstraVolumePartitionEdit edit = {.operation = ASTRA_VOLUME_PARTITION_INITIALIZE,
        .map = ASTRA_VOLUME_PARTITION_MAP_MBR};
    assert(astra_storage_provider_partition(provider, device, 1u, &edit) == EBUSY);
    poll_probes(provider);
    assert(astra_storage_provider_partition(provider, device, 1u, &edit) == 0);
    partition_wait(provider, device, 1u);
    ++observation.sequence;
    observation.action = ASTRA_STORAGE_OBSERVE_CHANGE;
    observation.partition_table = 1;
    observation.partition_map = ASTRA_VOLUME_PARTITION_MAP_MBR;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    edit = (AstraVolumePartitionEdit){.operation = ASTRA_VOLUME_PARTITION_CREATE,
        .map = ASTRA_VOLUME_PARTITION_MAP_MBR, .format = ASTRA_VOLUME_FAT32,
        .offset_bytes = 2048u << 9, .length_bytes = 65536u << 9};
    assert(astra_storage_provider_partition(provider, device, 2u, &edit) == 0);
    partition_wait(provider, device, 2u);
    unsigned char sector[512];
    assert(pread(loop, sector, sizeof(sector), 0) == sizeof(sector));
    assert(sector[510] == 0x55u && sector[511] == 0xaau && sector[450] == 0x0cu);
    char partition_node[80];
    snprintf(partition_node, sizeof(partition_node), "%sp1", node);
    struct stat partition_info;
    for (unsigned at = 0u; stat(partition_node, &partition_info) != 0 &&
         at < 10000u; ++at)
        usleep(1000);
    assert(stat(partition_node, &partition_info) == 0 &&
        S_ISBLK(partition_info.st_mode));
    ++observation.sequence;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    AstraStorageObservation partition = {.syspath = "/sys/devices/test/partition-loop/part1",
        .disk_syspath = observation.syspath, .devnode = partition_node,
        .devnum = partition_info.st_rdev, .disk_devnum = info.st_rdev,
        .sequence = 1u, .disk_sequence = disk_sequence,
        .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_USB,
        .media = 1, .partition = 1, .partition_number = 1u,
        .partition_map = ASTRA_VOLUME_PARTITION_MAP_MBR,
        .offset_bytes = 2048u << 9, .capacity_bytes = 65536u << 9};
    assert(astra_storage_discovery_observe(&discovery, &partition) == 0);
    poll_probes(provider);
    uint64_t retired_source = 0u;
    uint64_t after = 0u;
    sequence = 0u;
    do {
        assert(astra_storage_snapshot(&provider->catalog, sequence, after, rows,
            ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &sequence) == 0);
        for (size_t at = 0u; at < count; ++at)
            if (rows[at].kind == ASTRA_VOLUME_SOURCE &&
                rows[at].capacity_bytes == partition.capacity_bytes &&
                rows[at].offset_bytes == partition.offset_bytes)
                retired_source = rows[at].id;
        after = next;
    } while (next != 0u);
    assert(retired_source != 0u);
    edit = (AstraVolumePartitionEdit){.operation = ASTRA_VOLUME_PARTITION_DELETE,
        .map = ASTRA_VOLUME_PARTITION_MAP_MBR, .number = 1u};
    assert(astra_storage_provider_partition(provider, device, 3u, &edit) == 0);
    partition_wait(provider, device, 3u);
    assert(astra_storage_find(&provider->catalog, retired_source) == NULL);
    assert(pread(loop, sector, sizeof(sector), 0) == sizeof(sector));
    assert(sector[450] == 0u);
    ++observation.sequence;
    observation.action = ASTRA_STORAGE_OBSERVE_REMOVE;
    assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
    for (unsigned at = 0u; at < 10000u &&
         astra_storage_find(&provider->catalog, device) != NULL; ++at) {
        int cleanup = astra_storage_provider_reap(provider);
        assert(cleanup == 0 || cleanup == EAGAIN);
        usleep(1000);
    }
    assert(astra_storage_find(&provider->catalog, device) == NULL);
    provider->discovery = NULL;
    astra_storage_discovery_close(&discovery);
    int released;
    for (unsigned attempt = 0u;; ++attempt) {
        released = astra_storage_linux_loop_release(&loop);
        if (released != EAGAIN) break;
        assert(attempt < 10000u);
        usleep(1000);
    }
    assert(released == 0);
    assert(unlink(image) == 0);
}

static void processed_inventory(AstraStorageProvider *provider)
{
    AstraStorageDiscovery discovery = ASTRA_STORAGE_DISCOVERY_INIT;
    assert(astra_storage_discovery_open(&discovery, provider, 1u << 20, NULL) == 0);
    assert(discovery.fd >= 0 && discovery.udev != NULL && discovery.monitor != NULL);
    struct stat info;
    assert(stat("/", &info) == 0);
    struct udev_device *root = udev_device_new_from_devnum(discovery.udev, 'b', info.st_dev);
    if (root != NULL) {
        struct udev_device *disk = udev_device_get_parent_with_subsystem_devtype(root, "block", "disk");
        if (disk == NULL)
            disk = root;
        const char *node = udev_device_get_devnode(disk);
        if (node != NULL) {
            uint64_t sequence = provider->catalog.sequence;
            AstraStorageObservation observation = {.syspath = udev_device_get_syspath(disk),
                .disk_syspath = udev_device_get_syspath(disk), .devnode = node,
                .devnum = udev_device_get_devnum(disk), .disk_devnum = udev_device_get_devnum(disk),
                .action = ASTRA_STORAGE_OBSERVE_PRESENT, .class_id = ASTRA_VOLUME_FIXED, .media = 1};
            assert(astra_storage_discovery_observe(&discovery, &observation) == 0);
            assert(provider->catalog.sequence == sequence); /* root and siblings cannot gain guest authority */
        }
        udev_device_unref(root);
    }
    /* This test subscribes and enumerates actual processed inventory but never
     * automounts a host disk. Real mutation uses the disposable loop below. */
    AstraStorageRecord rows[ASTRA_STORAGE_PROVIDER_PAGE];
    size_t count;
    uint64_t after = 0u, next, sequence = 0u, current;
    do {
        assert(astra_storage_snapshot(&provider->catalog, sequence, after, rows,
            ASTRA_STORAGE_PROVIDER_PAGE, &count, &next, &current) == 0);
        for (size_t at = 0u; at < count; ++at)
            if (rows[at].kind == ASTRA_VOLUME_DEVICE)
                assert(astra_storage_provider_loss(provider, rows[at].id, rows[at].generation) == 0);
        after = next;
        sequence = provider->catalog.sequence;
    } while (next != 0u);
    astra_storage_discovery_close(&discovery);
    for (unsigned at = 0u; at < 10000u && provider->catalog.bytes != 0u; ++at) {
        int reaped = astra_storage_provider_reap(provider);
        assert(reaped == 0 || reaped == EAGAIN);
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    assert(provider->catalog.bytes == 0u && provider->resource_bytes == 0u);
}

static void ram(AstraStorageProvider *provider)
{
    uint64_t source, mounted, other;
    assert(astra_storage_ram_source(&provider->catalog, 0u, &source) == 0);
    assert(mount_wait(provider, source, 1u, 0u,
                                          1024u * 1024u, 16u, &mounted) == 0);
    assert(strcmp(astra_storage_find(&provider->catalog, mounted)->name, "/ram0") == 0);
    assert(astra_storage_ram_source(&provider->catalog, 0u, &other) == 0);
    assert(mount_wait(provider, other, 1u, 0u,
                                          2u * 1024u * 1024u, 16u, &other) == ENOSPC);
    int root;
    assert(astra_storage_provider_root(provider, mounted, 1u, &root) == 0);
    int file = openat(root, "pin", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    assert(file >= 0);
    char bytes[4096] = {0};
    unsigned written = 0u;
    while (write(file, bytes, sizeof(bytes)) == sizeof(bytes))
        written += sizeof(bytes);
    assert(errno == ENOSPC && written == 1024u * 1024u);
    assert(unmount_wait(provider, mounted, 1u) == EBUSY);
    assert(astra_storage_find(&provider->catalog, mounted)->state == ASTRA_VOLUME_MOUNTED);
    close(file);
    close(root);
    assert(unmount_wait(provider, mounted, 1u) == 0);
    assert(provider->ram_bytes == 0u && provider->ram_inodes == 0u);
    int stale;
    assert(astra_storage_provider_root(provider, mounted, 1u, &stale) == ESTALE);
    uint64_t replacement;
    assert(mount_wait(provider, source, 1u, 0u, 1024u * 1024u, 16u, &replacement) == 0);
    assert(replacement != mounted && provider->ram_bytes == 1024u * 1024u);
    assert(unmount_wait(provider, replacement, 1u) == 0);
    assert(astra_storage_provider_release(provider, replacement) == 0);
    assert(astra_storage_provider_release(provider, mounted) == 0);
    assert(provider->ram_bytes == 0u && provider->ram_inodes == 0u);
    assert(astra_storage_provider_loss(provider, source, 1u) == 0);
    assert(astra_storage_provider_release(provider, source) == 0);
}

static void blocked_path(AstraStorageProvider *provider, int original)
{
    uint64_t binding;
    int sockets[2], controls[2], enabled = 1, root;
    assert(mkfifoat(original, "slow", 0600) == 0);
    assert(astra_storage_provider_export(provider, original, "/slow-test", 0u, &binding) == 0);
    assert(astra_storage_provider_root(provider, binding, 1u, &root) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, controls) == 0);
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    assert(setsockopt(controls[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    AstraStoragePathRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_PATH_OPEN, .serial = 44u, .epoch = provider->epoch,
        .mount = binding, .generation = 1u, .fd_mask = 1u, .flags = O_RDONLY,
        .resolve = ASTRA_RESOLVE_FOLLOW_FINAL, .links = ASTRA_RESOLVE_LINK_MAX};
    strcpy(request.path, "/slow");
    assert(astra_storage_send_packet(sockets[1], &request, sizeof(request), &root, 1u) == 0);
    /* A synchronous broker never returns from this FIFO open. */
    assert(astra_storage_provider_path(provider, sockets[0], getpid(), 0u, 0u) == 0);
    assert(provider->workers == 1u);
    AstraStorageControlRequest snapshot = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT, .serial = 45u, .epoch = provider->epoch};
    assert(astra_storage_send_packet(controls[1], &snapshot, sizeof(snapshot), NULL, 0u) == 0);
    assert(astra_storage_provider_control(provider, controls[0], getpid(), 0u, 0u) == 0);
    AstraStorageControlReply catalog;
    int fds[ASTRA_STORAGE_PROVIDER_FDS];
    unsigned count;
    assert(astra_storage_receive_packet(controls[1], &catalog, sizeof(catalog), fds, &count, NULL) == 0);
    assert(catalog.error == 0 && catalog.serial == 45u && count == 0u);
    assert(astra_storage_provider_loss(provider, binding, 1u) == 0);
    wait_work(provider);
    AstraStoragePathReply reply;
    assert(astra_storage_receive_packet(sockets[1], &reply, sizeof(reply), fds, &count, NULL) == 0);
    assert(reply.serial == 44u && reply.error == ESTALE && count == 0u);
    close(root);
    close(sockets[0]); close(sockets[1]);
    close(controls[0]); close(controls[1]);
    assert(unmount_wait(provider, binding, 2u) == 0);
    assert(astra_storage_provider_release(provider, binding) == 0);
    assert(unlinkat(original, "slow", 0) == 0);
}

static void queued_probes(AstraStorageProvider *provider, const char *path)
{
    uint64_t sources[3];
    struct stat info;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    assert(fd >= 0 && fstat(fd, &info) == 0);
    for (unsigned at = 0u; at < 3u; ++at) {
        uint64_t device, medium;
        assert(astra_storage_device(&provider->catalog, ASTRA_VOLUME_OPTICAL, 0u, 0u, &device) == 0);
        assert(astra_storage_medium(&provider->catalog, device, 1u, &medium) == 0);
        assert(astra_storage_source(&provider->catalog, medium, 1u,
            0u, 0u, 0u, 0u, &sources[at]) == 0);
        assert(astra_storage_provider_source(provider, sources[at], 1u,
                                                fd, 0u, (uint64_t)info.st_size) == 0);
    }
    close(fd);
    assert(provider->probes == 0u);
    assert(astra_storage_provider_poll(provider) >= 0 && provider->probes == 1u);
    assert(astra_storage_find(&provider->catalog, sources[1])->state == ASTRA_VOLUME_PROBING);
    assert(astra_storage_provider_loss(provider, sources[1], 1u) == 0);
    poll_probes(provider);
    assert(astra_storage_find(&provider->catalog, sources[0])->format == ASTRA_VOLUME_ISO9660);
    assert(astra_storage_find(&provider->catalog, sources[2])->format == ASTRA_VOLUME_ISO9660);
    assert(astra_storage_find(&provider->catalog, sources[1])->state == ASTRA_VOLUME_LOST);
    for (unsigned at = 0u; at < 3u; ++at) {
        if (at != 1u)
            assert(astra_storage_provider_loss(provider, sources[at], 1u) == 0);
        assert(astra_storage_provider_release(provider, sources[at]) == 0);
    }
}

static void usb_format(AstraStorageProvider *provider, const char *path,
                       uint32_t format, const char *label)
{
    int writable = format == ASTRA_VOLUME_FAT32 || format == ASTRA_VOLUME_EXFAT;
    int file = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    struct stat info;
    assert(file >= 0 && fstat(file, &info) == 0);
    int block = astra_storage_linux_loop(file, 0u, (uint64_t)info.st_size,
        !writable);
    assert(block >= 0);
    close(file);
    uint64_t device, medium, source, mount;
    assert(astra_storage_device(&provider->catalog, ASTRA_VOLUME_USB,
        ASTRA_VOLUME_PARTITION_MAP_NONE, (uint64_t)info.st_size, &device) == 0);
    assert(astra_storage_medium(&provider->catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(&provider->catalog, medium, 1u,
        ASTRA_VOLUME_PARTITION_MAP_NONE, 0u, 0u,
        (uint64_t)info.st_size, &source) == 0);
    assert(astra_storage_provider_source(provider, source, 1u,
        block, 0u, 0u) == 0);
    poll_probes(provider);
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, source);
    assert(record != NULL && record->state == ASTRA_VOLUME_UNMOUNTED &&
        record->format == format && strcmp(record->label, label) == 0 &&
        ((record->flags & ASTRA_VOLUME_READ_ONLY) != 0u) == !writable);
    if (!writable) {
        assert(astra_storage_provider_mount(provider, source, 1u, 0u,
            0u, 0u, &mount) == EROFS);
        assert(mount == 0u);
    }
    int error = mount_wait(provider, source, 1u,
        writable ? 0u : ASTRA_VOLUME_READ_ONLY,
        0u, 0u, &mount);
    if (error != 0)
        fprintf(stderr, "USB fixture %s mount failed: %d (%s)\n",
            path, error, strerror(error));
    assert(error == 0);
    int root;
    assert(astra_storage_provider_root(provider, mount, 1u, &root) == 0);
    if (writable) {
        int created = openat(root, "astra-rw-test", O_CREAT | O_EXCL | O_RDWR, 0600);
        assert(created >= 0 && write(created, "fat32 durable\n", 14u) == 14);
        assert(fsync(created) == 0);
        close(created);
    } else
        assert(openat(root, "forbidden", O_CREAT | O_WRONLY, 0600) == -1 && errno == EROFS);
    if (format == ASTRA_VOLUME_ISO9660 || format == ASTRA_VOLUME_UDF)
        assert(faccessat(root, "Folder/café.txt", R_OK, 0) == 0);
    close(root);
    assert(unmount_wait(provider, mount, 1u) == 0);
    if (writable) {
        assert(astra_storage_provider_release(provider, mount) == 0);
        assert(mount_wait(provider, source, 1u, 0u, 0u, 0u, &mount) == 0);
        assert(astra_storage_provider_root(provider, mount, 1u, &root) == 0);
        int opened = openat(root, "astra-rw-test", O_RDONLY);
        char bytes[15] = {0};
        assert(opened >= 0 && read(opened, bytes, 14u) == 14 &&
            strcmp(bytes, "fat32 durable\n") == 0);
        close(opened);
        assert(unlinkat(root, "astra-rw-test", 0) == 0);
        close(root);
        assert(unmount_wait(provider, mount, 1u) == 0);
    }
    assert(astra_storage_provider_loss(provider, device, 1u) == 0);
    for (unsigned at = 0u; at < 10000u &&
        astra_storage_find(&provider->catalog, device) != NULL; ++at) {
        int reaped = astra_storage_provider_reap(provider);
        assert(reaped == 0 || reaped == EAGAIN);
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    assert(astra_storage_find(&provider->catalog, device) == NULL);
    for (unsigned at = 0u; at < 10000u; ++at) {
        int released = astra_storage_linux_loop_release(&block);
        if (released == 0) break;
        assert(released == EAGAIN && at + 1u < 10000u);
        usleep(1000);
    }
}

static void loss_during_mount(AstraStorageProvider *provider)
{
    uint64_t source, mounted;
    assert(astra_storage_ram_source(&provider->catalog, 0u, &source) == 0);
    assert(astra_storage_provider_mount(provider, source, 1u, 0u,
        1024u * 1024u, 16u, &mounted) == 0);
    assert(provider->workers == 1u);
    assert(astra_storage_find(&provider->catalog, mounted)->state == ASTRA_VOLUME_MOUNTING);
    AstraStorageRecord page[16];
    size_t count;
    uint64_t next, sequence;
    assert(astra_storage_snapshot(&provider->catalog, 0u, 0u, page, 16u,
                                    &count, &next, &sequence) == 0);
    assert(astra_storage_provider_release(provider, mounted) == EBUSY);
    assert(astra_storage_provider_loss(provider, source, 1u) == 0);
    wait_work(provider);
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, mounted);
    assert(record->state == ASTRA_VOLUME_FAILED && record->error == ENODEV &&
           record->generation == 2u);
    int root;
    assert(astra_storage_provider_root(provider, mounted, 1u, &root) == ESTALE);
    if (record->linux_mount_id != 0u)
        assert(unmount_wait(provider, mounted, 2u) == 0);
    assert(astra_storage_provider_release(provider, mounted) == 0);
    assert(astra_storage_provider_release(provider, source) == 0);
    assert(provider->workers == 0u && provider->ram_bytes == 0u && provider->ram_inodes == 0u);
}

static void lost_worker_reply(AstraStorageProvider *provider)
{
    uint64_t source, mount;
    AstraStorageMounted mounted;
    AstraStorageWork work = {.result = -1};
    assert(astra_storage_ram_source(&provider->catalog, 0u, &source) == 0);
    assert(astra_storage_mount_begin(&provider->catalog, source, 1u, 0u,
                                      1024u * 1024u, 16u, UINT32_MAX, &mount) == 0);
    const AstraStorageRecord *record = astra_storage_find(&provider->catalog, mount);
    assert(astra_storage_linux_prepare(&provider->domain, record, -1, 0u, 0u, &mounted) == 0);
    assert(astra_storage_work_start(&work, &provider->domain, record, -1,
                                      &mounted, ASTRA_STORAGE_WORK_ATTACH) == 0);
    for (unsigned at = 0u; !mounted.attached && at < 5000u; ++at) {
        assert(astra_storage_linux_reconcile(&provider->domain, &mounted) == 0);
        usleep(1000);
    }
    assert(mounted.attached && mounted.linux_mount_id != 0u);
    /* Discard the acknowledgement after Linux committed the actual mount. */
    close(work.result);
    work.result = -1;
    AstraStorageWorkReply reply;
    int root = -1, ready = 0;
    for (unsigned at = 0u; ready == 0 && at < 5000u; ++at) {
        ready = astra_storage_work_poll(&work, &reply, &root);
        assert(ready >= 0);
        usleep(1000);
    }
    assert(ready == 1 && reply.error != 0 && root == -1);
    assert(astra_storage_linux_reconcile(&provider->domain, &mounted) == 0 && mounted.attached);
    assert(astra_storage_mount_result(&provider->catalog, mount, 1u,
        mounted.linux_mount_id, reply.error) == reply.error);
    assert(astra_storage_find(&provider->catalog, mount)->state == ASTRA_VOLUME_FAILED);
    assert(astra_storage_forget(&provider->catalog, mount) == EBUSY);
    assert(astra_storage_linux_unmount(&mounted, 1) == 0);
    assert(astra_storage_unmount_result(&provider->catalog, mount, 1u, 0, 0, 0) == 0);
    assert(astra_storage_forget(&provider->catalog, mount) == 0);
    assert(astra_storage_loss(&provider->catalog, source, 1u) == 0);
    assert(astra_storage_forget(&provider->catalog, source) == 0);
}

static void image(AstraStorageProvider *provider, int export_root, uint64_t binding,
                    const char *path)
{
    uint64_t source, mounted, duplicate = 0u;
    int file = openat(export_root, path + 1u, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int writer = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    assert(file >= 0 && writer >= 0);
    struct stat original, loan;
    assert(fstat(file, &original) == 0);
    uint64_t before_sequence = provider->catalog.sequence;
    size_t before_resources = provider->resource_bytes;
    assert(astra_storage_provider_image(provider, binding, 1u, original.st_ino ^ UINT64_C(1),
        file, &source) == EINVAL && source == 0u);
    assert(provider->catalog.sequence == before_sequence && provider->resource_bytes == before_resources);
    assert(astra_storage_provider_image(provider, binding, 1u, original.st_ino, file, &source) == EAGAIN);
    close(writer);
    assert(astra_storage_provider_image(provider, binding, 1u, original.st_ino, file, &source) == 0);
    assert(astra_storage_find(&provider->catalog, source)->backing_object == (uint64_t)original.st_ino);
    int backing;
    assert(astra_storage_provider_backing(provider, source, 2u, &backing) == ESTALE && backing == -1);
    assert(astra_storage_provider_backing(provider, binding, 1u, &backing) == EPERM && backing == -1);
    assert(astra_storage_provider_backing(provider, source, 1u, &backing) == 0 && backing >= 0);
    assert(fstat(file, &original) == 0 && fstat(backing, &loan) == 0 &&
        original.st_dev == loan.st_dev && original.st_ino == loan.st_ino &&
        (fcntl(backing, F_GETFL) & O_ACCMODE) == O_RDONLY && (fcntl(backing, F_GETFD) & FD_CLOEXEC));
    close(backing);
    assert(astra_storage_provider_image(provider, binding, 1u, original.st_ino, file, &duplicate) == EBUSY);
    assert(astra_storage_provider_release(provider, source) == EBUSY);
    poll_probes(provider);
    assert(astra_storage_find(&provider->catalog, source)->format == ASTRA_VOLUME_ISO9660);
    assert(mount_wait(provider, source, 1u, ASTRA_VOLUME_READ_ONLY,
                                          0u, 0u, &mounted) == 0);
    assert(strcmp(astra_storage_find(&provider->catalog, mounted)->name, "/cd0") == 0);
    assert(astra_storage_provider_release_image(provider, source, 1u) == EBUSY);
    int root;
    assert(astra_storage_provider_root(provider, mounted, 1u, &root) == 0);
    int contents = openat(root, "Folder/caf\xc3\xa9.txt", O_RDONLY | O_CLOEXEC);
    char bytes[32];
    assert(contents >= 0 && read(contents, bytes, sizeof(bytes)) == 15);
    assert(memcmp(bytes, "Astra ISO test\n", 15) == 0);
    close(contents);
    close(root);
    pid_t writer_process = fork();
    assert(writer_process >= 0);
    if (writer_process == 0) {
        int conflicting = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        _exit(conflicting < 0 && errno == EWOULDBLOCK ? 0 : 1);
    }
    int status;
    assert(waitpid(writer_process, &status, 0) == writer_process &&
           WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(astra_storage_provider_poll(provider) >= 0);
    assert(astra_storage_find(&provider->catalog, source)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_find(&provider->catalog, mounted)->state == ASTRA_VOLUME_LOST);
    assert(astra_storage_provider_root(provider, mounted, 1u, &root) == ESTALE);
    int unmounted = unmount_wait(provider, mounted, 2u);
    for (unsigned retry = 0u; unmounted == EAGAIN && retry < 5000u; ++retry) {
        assert(astra_storage_provider_release(provider, mounted) == EBUSY);
        usleep(1000);
        unmounted = unmount_wait(provider, mounted, 2u);
    }
    assert(unmounted == 0);
    assert(astra_storage_provider_release(provider, mounted) == 0);
    assert(astra_storage_provider_release(provider, source) == 0);
    close(file);
    writer = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    assert(writer >= 0); /* Lease disappears even if the original file was kept. */
    close(writer);
    file = openat(export_root, path + 1u, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    assert(file >= 0 && fstat(file, &original) == 0 &&
        astra_storage_provider_image(provider, binding, 1u, original.st_ino, file, &source) == 0);
    assert(astra_storage_provider_release_image(provider, source, 2u) == ESTALE);
    assert(astra_storage_provider_release_image(provider, source, 1u) == 0);
    close(file);
    for (unsigned retry = 0u; retry < 5000u; ++retry) {
        int pending = astra_storage_provider_reap(provider);
        assert(pending == 0 || pending == EAGAIN);
        if (astra_storage_find(&provider->catalog, source) == NULL) break;
        assert(retry != 4999u && astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    writer = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    assert(writer >= 0);
    close(writer);
    assert(astra_storage_provider_loss(provider, binding, 1u) == 0);
    close(export_root);
    assert(unmount_wait(provider, binding, 2u) == 0);
    assert(astra_storage_provider_release(provider, binding) == 0);
}

static void peer(AstraStorageProvider *provider, uid_t uid, gid_t gid, uint64_t binding,
                   int outside_root)
{
    int controls[2], paths[2], enabled = 1, status;
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, controls) == 0);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, paths) == 0);
    assert(setsockopt(controls[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    assert(setsockopt(paths[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT, .serial = 1};
        AstraStorageControlReply reply;
        int root;
        close(controls[0]);
        close(paths[0]);
        assert(setgroups(0, NULL) == 0);
        assert(setresgid(gid, gid, gid) == 0 && setresuid(uid, uid, uid) == 0);
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &root) == 0);
        assert(reply.error == 0 && reply.count == ASTRA_STORAGE_PROVIDER_PAGE && reply.next != 0u);
        uint64_t epoch = reply.epoch;
        request.epoch = epoch;
        request.sequence = reply.sequence;
        request.after = reply.next;
        ++request.serial;
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &root) == 0);
        assert(reply.error == 0 && reply.count != 0u && reply.next == 0u);
        request = (AstraStorageControlRequest){.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_CONTROL_ROOT, .serial = 3,
            .epoch = epoch, .target = binding, .generation = 9};
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &root) == 0);
        assert(reply.error == ESTALE && root == -1);
        request.generation = 1;
        request.epoch = epoch == UINT64_MAX ? epoch - 1u : epoch + 1u;
        ++request.serial;
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &root) == 0);
        assert(reply.error == ESTALE && root == -1);
        request.epoch = epoch;
        ++request.serial;
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &root) == 0);
        assert(reply.error == 0 && root >= 0);
        int unused_root;
        request = (AstraStorageControlRequest){.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_CONTROL_FORMAT, .serial = 6,
            .epoch = epoch, .target = binding, .generation = 1,
            .format = ASTRA_VOLUME_FAT32};
        strcpy(request.label, "TEST");
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &unused_root) == 0);
        assert(reply.error == ENOTSUP);
        request.label[5] = 'X';
        ++request.serial;
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &unused_root) == 0);
        assert(reply.error == EINVAL);
        request = (AstraStorageControlRequest){.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_CONTROL_FORMAT_STATUS, .serial = 8,
            .epoch = epoch, .target = binding, .generation = 1};
        assert(astra_storage_control_call(controls[1], &request, -1, &reply, &unused_root) == 0);
        assert(reply.error == ESTALE);
        AstraStoragePathRequest path = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_PATH_OPEN, .serial = 1,
            .epoch = epoch, .mount = binding, .generation = 1, .fd_mask = 1,
            .flags = O_RDWR, .resolve = ASTRA_RESOLVE_FOLLOW_FINAL,
            .links = ASTRA_RESOLVE_LINK_MAX};
        AstraStoragePathReply answer;
        int fds[3] = {root, -1, -1}, file;
        strcpy(path.path, "/private");
        assert(astra_storage_path_call(paths[1], &path, fds, &answer, &file) == 0);
        assert(answer.error == 0 && file >= 0);
        assert(pwrite(file, "Astra", 5, 0) == 5);
        close(file);
        path.epoch = epoch == UINT64_MAX ? epoch - 1u : epoch + 1u;
        ++path.serial;
        assert(astra_storage_path_call(paths[1], &path, fds, &answer, &file) == 0);
        assert(answer.error == ESTALE && file == -1);
        path.epoch = epoch;
        /* Same inode through the original host mount is not broker authority. */
        fds[0] = outside_root;
        ++path.serial;
        assert(astra_storage_path_call(paths[1], &path, fds, &answer, &file) == 0);
        assert(answer.error == EXDEV && file == -1);
        fds[0] = root;
        fds[1] = outside_root;
        path.fd_mask = 3;
        path.operation = ASTRA_STORAGE_PATH_ROOTED;
        path.flags = 0;
        path.path[0] = '\0';
        ++path.serial;
        assert(astra_storage_path_call(paths[1], &path, fds, &answer, &file) == 0);
        assert(answer.error == EXDEV && file == -1);
        close(root);
        close(paths[1]);
        close(controls[1]);
        _exit(0);
    }
    close(controls[1]);
    close(paths[1]);
    int served = astra_storage_provider_serve(provider, controls[0], paths[0], child);
    assert(served == 0 || served == EPIPE);
    close(controls[0]);
    close(paths[0]);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(int argc, char **argv)
{
    AstraStorageProvider provider = ASTRA_STORAGE_PROVIDER_INIT;
    uint64_t bindings[18], image_binding;
    char path[] = "/var/tmp/astra-storage-provider-test.XXXXXX";
    int format_gate = argc >= 10 && strcmp(argv[argc - 1], "--format-gate") == 0;
    assert((argc == 9 || argc == 10 || (argc == 11 && format_gate)) && geteuid() == 0);
    uid_t uid = (uid_t)strtoul(argv[2], NULL, 10);
    gid_t gid = (gid_t)strtoul(argv[3], NULL, 10);
    AstraStorageDomain domain = {.directory = -1};
    assert(astra_storage_domain_open(&domain, uid, gid) == 0);
    int domain_fd = domain.directory;
    struct stat domain_identity, entered_identity;
    assert(fstat(domain_fd, &domain_identity) == 0);
    assert(astra_storage_provider_open(&provider, &domain, 1u << 20, 1u << 20,
                                        0u, 5000u, 2u, 2u << 20, 64u) == EINVAL);
    assert(domain.directory == domain_fd && domain.path[0] != '\0');
    assert(astra_storage_provider_open(&provider, &domain, 1u << 20, 1u << 20,
                                        1u, 5000u, 2u, 2u << 20, 64u) == 0);
    assert(domain.directory == -1 && domain.path[0] == '\0');
    assert(fstat(provider.domain.directory, &entered_identity) == 0 &&
        entered_identity.st_dev == domain_identity.st_dev &&
        entered_identity.st_ino == domain_identity.st_ino);
    uint64_t epoch = provider.epoch;
    assert(astra_storage_provider_open(&provider, &domain, 1u << 20, 1u << 20,
                                        1u, 5000u, 2u, 2u << 20, 64u) == EBUSY);
    assert(provider.epoch == epoch);
    processed_inventory(&provider);
    assert(mkdtemp(path) != NULL);
    int root = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    int file = openat(root, "private", O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    assert(root >= 0 && file >= 0);
    close(file);
    for (unsigned at = 0; at < 18u; ++at) {
        char name[32];
        (void)snprintf(name, sizeof(name), "/export%u", at);
        assert(astra_storage_provider_export(&provider, root, name, 0u, &bindings[at]) == 0);
    }
    assert(astra_storage_provider_close(&provider) == EBUSY);
    blocked_path(&provider, root);
    peer(&provider, uid, gid, bindings[0], root);
    struct stat info;
    assert(fstatat(root, "private", &info, 0) == 0 && info.st_uid == 0u &&
           info.st_gid == 0u && (info.st_mode & 07777) == 0600 && info.st_size == 5);
    for (unsigned at = 0; at < 18u; ++at) {
        assert(astra_storage_provider_loss(&provider, bindings[at], 1u) == 0);
        assert(unmount_wait(&provider, bindings[at], 2u) == 0);
        assert(astra_storage_provider_release(&provider, bindings[at]) == 0);
    }
    assert(unlinkat(root, "private", 0) == 0);
    close(root);
    assert(rmdir(path) == 0);
    ram(&provider);
    root = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    assert(root >= 0 && astra_storage_provider_export(&provider, root, "/fixture",
                                                       0u, &image_binding) == 0);
    close(root);
    assert(astra_storage_provider_root(&provider, image_binding, 1u, &root) == 0);
    image(&provider, root, image_binding, argv[1]);
    queued_probes(&provider, argv[1]);
    usb_format(&provider, argv[1], ASTRA_VOLUME_ISO9660, "ASTRA_DISC");
    usb_format(&provider, argv[4], ASTRA_VOLUME_FAT12, "ASTRA_FAT12");
    usb_format(&provider, argv[5], ASTRA_VOLUME_FAT16, "ASTRA_FAT16");
    usb_format(&provider, argv[6], ASTRA_VOLUME_FAT32, "ASTRA_FAT32");
    usb_format(&provider, argv[7], ASTRA_VOLUME_NTFS, "ASTRA_NTFS");
    usb_format(&provider, argv[8], ASTRA_VOLUME_UDF, "ASTRA_DISC");
    if (argc == 11 || (argc == 10 && !format_gate))
        usb_format(&provider, argv[9], ASTRA_VOLUME_EXFAT, "ASTRA_EXFAT");
    loss_during_mount(&provider);
    lost_worker_reply(&provider);
    discovered_media(&provider, argv[1]);
    exclusive_format_target(&provider, argv[6], format_gate);
    exclusive_partition_target(&provider);
    /* Retire source-only observations after failed RAM admission. */
    AstraStorageRecord page[16];
    uint64_t sequence, next;
    size_t count;
    assert(astra_storage_snapshot(&provider.catalog, 0u, 0u, page, 16u,
                                    &count, &next, &sequence) == 0 && next == 0u);
    for (size_t at = count; at != 0u; --at) {
        if (page[at - 1u].state != ASTRA_VOLUME_LOST)
            assert(astra_storage_provider_loss(&provider, page[at - 1u].id,
                                                 page[at - 1u].generation) == 0);
        assert(astra_storage_provider_release(&provider, page[at - 1u].id) == 0);
    }
    assert(provider.resource_bytes == 0u && provider.catalog.bytes == 0u && provider.probes == 0u);
    assert(astra_storage_provider_close(&provider) == 0);
    puts(argc == 10 ?
        "storage provider: USB FAT12/16 ISO/UDF NTFS read-only, FAT32/exFAT durable read/write, catalog/root/path and cleanup PASS" :
        "storage provider: USB FAT12/16 ISO/UDF NTFS read-only, FAT32 durable read/write, catalog/root/path and cleanup PASS");
    return 0;
}
