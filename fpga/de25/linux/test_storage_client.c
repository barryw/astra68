#define _GNU_SOURCE
#include "astra_storage_transport.h"
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <linux/capability.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static void clean_stop(int number)
{
    (void)number;
    _exit(0);
}

int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);
    uid_t uid = (uid_t)strtoul(argv[1], NULL, 10);
    gid_t gid = (gid_t)strtoul(argv[2], NULL, 10);
    uid_t real, effective, saved;
    gid_t greal, geffective, gsaved;
    assert(uid != 0u && gid != 0u);
    assert(getresuid(&real, &effective, &saved) == 0 && real == uid && effective == uid && saved == uid);
    assert(getresgid(&greal, &geffective, &gsaved) == 0 && greal == gid && geffective == gid && gsaved == gid);
    assert(getgroups(0, NULL) == 0 && prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == 1);
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[2];
    assert(syscall(SYS_capget, &header, caps) == 0);
    for (unsigned at = 0; at < 2u; ++at)
        assert(caps[at].effective == 0u && caps[at].permitted == 0u && caps[at].inheritable == 0u);
    for (int cap = 0; ; ++cap) {
        int present = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
        if (present < 0) {
            assert(errno == EINVAL);
            break;
        }
        assert(present == 0);
    }
    int death;
    assert(prctl(PR_GET_PDEATHSIG, &death, 0, 0, 0) == 0 && death == SIGKILL);
    /* Launcher closes every source, loop, mount and worker FD before exec. */
    for (int fd = 5; fd < 1024; ++fd)
        assert(fcntl(fd, F_GETFD) < 0 && errno == EBADF);
    assert(strcmp(getenv("ASTRA_STORAGE_CONTROL_FD"), "3") == 0);
    assert(strcmp(getenv("ASTRA_STORAGE_PATH_FD"), "4") == 0);
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT, .serial = 1u};
    AstraStorageControlReply reply;
    int root = -1;
    assert(astra_storage_control_call(3, &request, -1, &reply, &root) == 0 && reply.error == 0);
    assert(reply.next == 0u && root == -1);
    unsigned found = 0u;
    uint64_t system = 0u, generation = 0u;
    const char *names[] = {"/dh0", "/work", "/sound", "/ram0", "/tmp"};
    for (unsigned at = 0; at < reply.count; ++at) {
        const AstraStorageRecord *record = &reply.records[at];
        if (record->kind != ASTRA_VOLUME_MOUNT && record->kind != ASTRA_VOLUME_BINDING)
            continue;
        assert(record->state == ASTRA_VOLUME_MOUNTED && record->linux_mount_id != 0u);
        for (unsigned name = 0; name < 5u; ++name)
            if (strcmp(record->name, names[name]) == 0) {
                assert((found & (1u << name)) == 0u);
                found |= 1u << name;
            }
        if (strcmp(record->name, "/dh0") == 0) {
            assert(record->flags & ASTRA_VOLUME_SYSTEM);
            assert(record->format == ASTRA_VOLUME_EXT4);
            system = record->id;
            generation = record->generation;
        }
    }
    assert(found == 31u && system != 0u);
    request = (AstraStorageControlRequest){.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_ROOT, .serial = 2u,
        .epoch = reply.epoch, .target = system, .generation = generation};
    assert(astra_storage_control_call(3, &request, -1, &reply, &root) == 0 && reply.error == 0 && root >= 0);
    /* No uid/mode workaround: direct path access still refuses root-owned 0600. */
    assert(openat(root, "home/private", O_RDWR | O_CLOEXEC) < 0 && errno == EACCES);
    AstraStoragePathRequest path = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_PATH_OPEN, .serial = 1u, .epoch = reply.epoch,
        .mount = system, .generation = generation, .fd_mask = 1u,
        .flags = O_RDWR | O_CLOEXEC, .resolve = ASTRA_RESOLVE_FOLLOW_FINAL,
        .links = ASTRA_RESOLVE_LINK_MAX};
    strcpy(path.path, "/home/private");
    int fds[3] = {root, -1, -1}, file;
    AstraStoragePathReply answer;
    assert(astra_storage_path_call(4, &path, fds, &answer, &file) == 0 && answer.error == 0 && file >= 0);
    char bytes[32] = {0};
    struct stat info;
    assert(pread(file, bytes, sizeof(bytes), 0) == 12 && strcmp(bytes, "private-data") == 0);
    assert(fstat(file, &info) == 0 && info.st_uid == 0u && info.st_gid == 0u && (info.st_mode & 07777) == 0600);
    assert(pwrite(file, "Astra", 5u, 12) == 5 && fsync(file) == 0);
    close(file);
    close(root);
    request = (AstraStorageControlRequest){.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_PARTITION, .serial = 3u,
        .epoch = reply.epoch, .target = system, .generation = generation,
        .edit = {.operation = ASTRA_VOLUME_PARTITION_INITIALIZE,
            .map = ASTRA_VOLUME_PARTITION_MAP_MBR}};
    assert(astra_storage_control_call(3, &request, -1, &reply, &root) == 0 &&
        reply.error == ENOTSUP && root == -1);
    puts("storage launcher client: exact volumes, root-owned data, zero privilege, FD confinement PASS");
    fflush(stdout);
    if (argc == 4) {
        /* Parent test breaks the backing lease or stops the launcher. */
        if (strcmp(argv[3], "hold-zero") == 0)
            assert(signal(SIGTERM, clean_stop) != SIG_ERR);
        for (;;) pause();
    }
    return 0;
}
