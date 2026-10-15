#define _GNU_SOURCE
#include "astra_storage_provider.h"
#include "astra_storage_discovery.h"
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/fs.h>
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
    AstraStorageDiscovery discovery = {.provider = provider, .byte_limit = 1u << 20, .fd = -1};
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
    close(root);
    for (unsigned at = 0u; at < 10000u && astra_storage_find(&provider->catalog, mounted) != NULL; ++at) {
        int reaped = astra_storage_provider_reap(provider);
        assert(reaped == 0 || reaped == EAGAIN);
        assert(astra_storage_provider_poll(provider) >= 0);
        usleep(1000);
    }
    assert(astra_storage_find(&provider->catalog, mounted) == NULL);
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

static void processed_inventory(AstraStorageProvider *provider)
{
    AstraStorageDiscovery discovery = ASTRA_STORAGE_DISCOVERY_INIT;
    assert(astra_storage_discovery_open(&discovery, provider, 1u << 20) == 0);
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
        assert(astra_storage_device(&provider->catalog, ASTRA_VOLUME_OPTICAL, &device) == 0);
        assert(astra_storage_medium(&provider->catalog, device, 1u, &medium) == 0);
        assert(astra_storage_source(&provider->catalog, medium, 1u, &sources[at]) == 0);
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
                                      1024u * 1024u, 16u, &mount) == 0);
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
    assert(argc == 4 && geteuid() == 0);
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
    loss_during_mount(&provider);
    lost_worker_reply(&provider);
    discovered_media(&provider, argv[1]);
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
    puts("storage provider: authenticated catalog/root/path, preserved data, RAM limits, image pins/loss/cleanup PASS");
    return 0;
}
