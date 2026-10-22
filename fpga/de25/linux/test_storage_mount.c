#define _GNU_SOURCE
#include "astra_storage_mount.h"
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static void preserved_owner_loan(AstraStorageDomain *domain)
{
    AstraResolution resolved;
    struct stat before;
    struct stat after;
    char bytes[8] = {0};
    int file = openat(domain->directory, "loan-file",
        O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    int loan;
    int status;
    pid_t consumer;

    assert(file >= 0 && write(file, "before\n", 7u) == 7);
    assert(fstat(file, &before) == 0 && before.st_uid == 0u &&
           before.st_gid == 0u && (before.st_mode & 0777u) == 0600u);
    close(file);
    /* Same ordered resolver used by HostFS; root lends an authorized fd,
     * preserving ownership and permissions instead of rewriting user data. */
    loan = astra_resolve_open_path(domain->directory, domain->directory,
        "/loan-file", O_RDWR | O_CLOEXEC, 0u, ASTRA_RESOLVE_FOLLOW_FINAL,
        ASTRA_RESOLVE_LINK_MAX, &resolved);
    assert(loan >= 0);
    consumer = fork();
    assert(consumer >= 0);
    if (consumer == 0) {
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        assert(setgroups(0u, NULL) == 0);
        assert(setresgid(domain->gid, domain->gid, domain->gid) == 0);
        assert(setresuid(domain->uid, domain->uid, domain->uid) == 0);
        errno = 0;
        assert(openat(domain->directory, "loan-file", O_RDWR) < 0 && errno == EACCES);
        assert(pread(loan, bytes, 7u, 0) == 7 && strcmp(bytes, "before\n") == 0);
        assert(pwrite(loan, "after!\n", 7u, 0) == 7);
        _exit(0);
    }
    assert(waitpid(consumer, &status, 0) == consumer);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(fstat(loan, &after) == 0 && before.st_uid == after.st_uid &&
           before.st_gid == after.st_gid && before.st_mode == after.st_mode);
    assert(pread(loan, bytes, 7u, 0) == 7 && strcmp(bytes, "after!\n") == 0);
    close(loan);
    assert(unlinkat(domain->directory, "loan-file", 0) == 0);
}

static void domain_retirement(uid_t uid, gid_t gid)
{
    AstraStorageDomain domain = {.directory = -1};
    struct stat before, after;
    assert(stat("/proc/self/ns/mnt", &before) == 0);
    assert(astra_storage_domain_open(&domain, uid, gid) == 0);
    assert(stat("/proc/self/ns/mnt", &after) == 0 && before.st_ino == after.st_ino);
    char name[sizeof(domain.path)];
    strcpy(name, domain.path);
    assert(mkdirat(domain.directory, "0000000000000001", 0700) == 0);
    int point = openat(domain.directory, "0000000000000001", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    assert(point >= 0);
    int file = openat(point, "retained", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    assert(file >= 0); close(file);
    assert(astra_storage_domain_retire(&domain) == ENOTEMPTY && domain.directory >= 0);
    assert(unlinkat(point, "retained", 0) == 0); close(point);
    assert(symlinkat("/", domain.directory, "0000000000000002") == 0);
    assert(astra_storage_domain_retire(&domain) == EACCES && domain.directory >= 0);
    assert(unlinkat(domain.directory, "0000000000000002", 0) == 0);
    assert(mkdirat(domain.directory, "unexpected", 0700) == 0);
    assert(astra_storage_domain_retire(&domain) == EPROTO && domain.directory >= 0);
    assert(unlinkat(domain.directory, "unexpected", AT_REMOVEDIR) == 0);
    assert(astra_storage_domain_retire(&domain) == 0 && domain.directory == -1 && access(name, F_OK) != 0 && errno == ENOENT);
    /* A normal provider removed the root; its outside owner still holds the
     * exact unlinked inode. Closing that owner must not touch another path. */
    assert(astra_storage_domain_open(&domain, uid, gid) == 0);
    AstraStorageDomain owner = domain;
    owner.directory = dup(domain.directory);
    assert(owner.directory >= 0 && astra_storage_domain_close(&domain) == 0);
    strcpy(name, owner.path);
    assert(mkdir(owner.path, 0700) == 0);
    assert(astra_storage_domain_retire(&owner) == 0 && owner.directory == -1);
    assert(access(name, F_OK) == 0 && rmdir(name) == 0);
}

int main(int argc, char **argv)
{
    AstraStorageDomain domain = {.directory = -1};
    AstraStorageMounted mounted;
    AstraStorageRecord record = {
        .id = 1u, .generation = 1u,
        .kind = ASTRA_VOLUME_MOUNT, .class_id = ASTRA_VOLUME_OPTICAL,
        .state = ASTRA_VOLUME_MOUNTING, .origin = ASTRA_VOLUME_ORIGIN_IMAGE,
        .flags = ASTRA_VOLUME_READ_ONLY, .backing_object = 812u
    };
    struct stat info;
    struct statvfs limits;
    int source;
    int held;
    int root_loan;
    int loop_loan;
    int result;
    pid_t consumer;
    char bytes[64] = {0};
    char loop_path[64];
    uint64_t identity;
    int blank;

    assert(argc == 5 && geteuid() == 0);
    domain_retirement((uid_t)strtoul(argv[3], NULL, 10), (gid_t)strtoul(argv[4], NULL, 10));
    record.format = (uint32_t)strtoul(argv[2], NULL, 10);
    blank = record.format == ASTRA_VOLUME_FAT12 || record.format == ASTRA_VOLUME_FAT16 ||
            record.format == ASTRA_VOLUME_NTFS;
    if (blank)
        record.class_id = ASTRA_VOLUME_USB;
    assert(astra_storage_domain_open(&domain,
        (uid_t)strtoul(argv[3], NULL, 10),
        (gid_t)strtoul(argv[4], NULL, 10)) == 0);
    struct stat owned;
    assert(fstat(domain.directory, &owned) == 0);
    assert(astra_storage_domain_enter(&domain) == 0);
    assert(fstat(domain.directory, &info) == 0 &&
        info.st_dev == owned.st_dev && info.st_ino == owned.st_ino);
    struct statx relative, absolute;
    assert(statx(domain.directory, "", AT_EMPTY_PATH, STATX_MNT_ID, &relative) == 0 &&
        statx(AT_FDCWD, domain.path, AT_SYMLINK_NOFOLLOW, STATX_MNT_ID, &absolute) == 0 &&
        (relative.stx_mask & STATX_MNT_ID) != 0u &&
        (absolute.stx_mask & STATX_MNT_ID) != 0u &&
        relative.stx_mnt_id == absolute.stx_mnt_id);
    preserved_owner_loan(&domain);
    source = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    assert(source >= 0 && fstat(source, &info) == 0 && info.st_size > 0);
    result = astra_storage_linux_mount(&domain, &record, source,
        0u, (uint64_t)info.st_size, &mounted);
    if (result != 0)
        fprintf(stderr, "storage mount error: %d (%s)\n", result, strerror(result));
    assert(result == 0);
    assert(mounted.attached && mounted.root >= 0 && mounted.loop >= 0);
    assert(mounted.linux_mount_id != 0u);
    assert(statvfs(mounted.target, &limits) == 0);
    assert((limits.f_flag & (ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC)) ==
           (ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC));
    identity = mounted.linux_mount_id;
    consumer = fork();
    assert(consumer >= 0);
    if (consumer == 0) {
        int file;
        close(domain.directory);
        close(mounted.loop);
        close(mounted.backing);
        close(source);
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        assert(setgroups(0u, NULL) == 0);
        assert(setresgid(domain.gid, domain.gid, domain.gid) == 0);
        assert(setresuid(domain.uid, domain.uid, domain.uid) == 0);
        file = openat(mounted.root, blank ? "." : "Folder/café.txt",
                      O_RDONLY | O_CLOEXEC);
        assert(file >= 0);
        if (!blank) {
            assert(read(file, bytes, sizeof(bytes)) == 15);
            assert(strcmp(bytes, "Astra ISO test\n") == 0);
        }
        close(file);
        _exit(0);
    }
    assert(waitpid(consumer, &result, 0) == consumer);
    assert(WIFEXITED(result) && WEXITSTATUS(result) == 0);
    held = openat(mounted.root, blank ? "." : "Folder/café.txt", O_RDONLY | O_CLOEXEC);
    assert(held >= 0);
    if (!blank) {
        assert(read(held, bytes, sizeof(bytes)) == 15);
        assert(strcmp(bytes, "Astra ISO test\n") == 0);
    }
    errno = 0;
    assert(openat(mounted.root, "new-file", O_WRONLY | O_CREAT, 0600) < 0);
    assert(errno == EROFS);
    root_loan = dup(mounted.root);
    assert(root_loan >= 0);
    assert(astra_storage_linux_unmount(&mounted, 0) == EBUSY);
    assert(mounted.attached && mounted.root >= 0 && mounted.linux_mount_id == identity);
    close(root_loan);
    close(held);
    (void)snprintf(loop_path, sizeof(loop_path), "/proc/self/fd/%d", mounted.loop);
    loop_loan = open(loop_path, O_RDONLY | O_CLOEXEC);
    assert(loop_loan >= 0);
    assert(astra_storage_linux_unmount(&mounted, 0) == EAGAIN);
    assert(!mounted.attached && mounted.linux_mount_id == 0u &&
           mounted.loop >= 0 && mounted.backing >= 0);
    close(loop_loan);
    for (unsigned attempt = 0u;; ++attempt) {
        result = astra_storage_linux_unmount(&mounted, 0);
        if (result != EAGAIN)
            break;
        assert(attempt < 10000u);
        (void)poll(NULL, 0u, 1);
    }
    assert(result == 0);
    assert(!mounted.attached && mounted.root == -1 && mounted.loop == -1 &&
           mounted.backing == -1 && mounted.linux_mount_id == 0u);
    close(source);
    assert(astra_storage_domain_close(&domain) == 0);
    puts("storage mount: unprivileged contents, read-only, busy/refusal restore, unmount/loop cleanup PASS");
    return 0;
}
