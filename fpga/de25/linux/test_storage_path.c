#define _GNU_SOURCE
#include "astra_storage_path.h"
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <grp.h>

static AstraStoragePathRequest request(unsigned operation, const char *path)
{
    AstraStoragePathRequest value = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = operation, .serial = 1, .epoch = 1, .mount = 1, .generation = 1,
        .fd_mask = 1, .links = ASTRA_RESOLVE_LINK_MAX};
    assert(strlen(path) < sizeof(value.path));
    strcpy(value.path, path);
    return value;
}

static void execute(int root, int readonly, const AstraStoragePathRequest *value,
                      const int *fds, AstraStoragePathReply *reply, int *result)
{
    astra_storage_path_execute(root, readonly, value, fds, reply, result);
}

static void semantics(int root)
{
    AstraStoragePathRequest value = request(ASTRA_STORAGE_PATH_MKDIR, "/sub");
    AstraStoragePathReply reply;
    int fds[3] = {root, -1, -1};
    int result;
    value.resolve = ASTRA_RESOLVE_ENTRY;
    value.mode = 0700;
    execute(root, 1, &value, fds, &reply, &result);
    assert(reply.error == EROFS && result == -1);
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0 && result == -1);
    value = request(ASTRA_STORAGE_PATH_OPEN, "/sub/file");
    value.flags = O_CREAT | O_EXCL | O_RDWR;
    value.mode = 0600;
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0 && result >= 0);
    assert(write(result, "old", 3) == 3);
    close(result);
    value = request(ASTRA_STORAGE_PATH_RENAME, "/sub/file");
    value.resolve = ASTRA_RESOLVE_ENTRY;
    strcpy(value.path2, "/sub/renamed");
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0);
    value = request(ASTRA_STORAGE_PATH_SYMLINK, "../sub/renamed");
    value.resolve = ASTRA_RESOLVE_ENTRY;
    strcpy(value.path2, "/sub/link");
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0);
    value = request(ASTRA_STORAGE_PATH_READLINK, "/sub/link");
    value.capacity = 3;
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == ERANGE);
    value.capacity = sizeof(reply.data);
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0 && reply.length == 14);
    assert(memcmp(reply.data, "../sub/renamed", 14) == 0);
    value = request(ASTRA_STORAGE_PATH_OPEN, "/sub/../sub/link");
    value.resolve = ASTRA_RESOLVE_FOLLOW_FINAL;
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == 0 && result >= 0);
    close(result);
    value = request(ASTRA_STORAGE_PATH_OPEN, "/../outside");
    value.resolve = ASTRA_RESOLVE_FOLLOW_FINAL;
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error == EXDEV && reply.escape == ASTRA_RESOLVE_ESCAPE_PARENT);
    int outside = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    assert(outside >= 0);
    fds[0] = outside;
    value = request(ASTRA_STORAGE_PATH_OPEN, "/etc/passwd");
    execute(root, 0, &value, fds, &reply, &result);
    assert(reply.error != 0 && result == -1);
    close(outside);
    assert(unlinkat(root, "sub/link", 0) == 0);
    assert(unlinkat(root, "sub/renamed", 0) == 0);
    assert(unlinkat(root, "sub", AT_REMOVEDIR) == 0);
}

static void seeding(int root)
{
    assert(mkdirat(root, "shipped", 0700) == 0);
    int source = openat(root, "shipped", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(source >= 0);
    int file = openat(source, "font", O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    assert(file >= 0 && write(file, "font bytes", 10u) == 10);
    close(file);
    assert(astra_storage_path_seed(root, source, "/fonts") == 0);
    file = openat(root, "fonts/font", O_RDWR | O_CLOEXEC);
    char bytes[16] = {0};
    struct stat before, after;
    assert(file >= 0 && read(file, bytes, sizeof(bytes)) == 10 && memcmp(bytes, "font bytes", 10) == 0);
    assert(fchmod(file, 0600) == 0 && pwrite(file, "user bytes", 10u, 0u) == 10);
    assert(fstat(file, &before) == 0);
    close(file);
    assert(symlinkat("/outside", root, "fonts/default") == 0);
    file = openat(source, "default", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    assert(file >= 0); close(file);
    assert(astra_storage_path_seed(root, source, "/fonts") == 0);
    file = openat(root, "fonts/font", O_RDONLY | O_CLOEXEC);
    assert(file >= 0 && read(file, bytes, sizeof(bytes)) == 10 && memcmp(bytes, "user bytes", 10) == 0);
    assert(fstat(file, &after) == 0 && before.st_uid == after.st_uid && before.st_gid == after.st_gid &&
        before.st_mode == after.st_mode && before.st_ino == after.st_ino);
    close(file);
    assert(symlinkat("/outside", root, "escape-fonts") == 0);
    assert(astra_storage_path_seed(root, source, "/escape-fonts") != 0);
    assert(astra_storage_path_seed(root, source, "/../outside") == EXDEV);
    assert(unlinkat(root, "escape-fonts", 0) == 0);
    assert(unlinkat(root, "fonts/default", 0) == 0);
    assert(unlinkat(root, "fonts/font", 0) == 0);
    assert(unlinkat(root, "fonts", AT_REMOVEDIR) == 0);
    assert(unlinkat(source, "font", 0) == 0);
    assert(unlinkat(source, "default", 0) == 0);
    assert(symlinkat("/etc/passwd", source, "invalid") == 0);
    assert(astra_storage_path_seed(root, source, "/fonts") != 0);
    assert(unlinkat(source, "invalid", 0) == 0);
    assert(unlinkat(root, "fonts", AT_REMOVEDIR) == 0);
    close(source);
    assert(unlinkat(root, "shipped", AT_REMOVEDIR) == 0);
}

static void lending(int root)
{
    int sockets[2];
    int enabled = 1;
    int file = openat(root, "private", O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    uid_t uid = geteuid() == 0 ? 999 : geteuid();
    gid_t gid = geteuid() == 0 ? 999 : getegid();
    struct stat before, after;
    int status;
    pid_t child;

    assert(file >= 0 && write(file, "old", 3) == 3 && fstat(file, &before) == 0);
    close(file);
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) == 0);
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        AstraStoragePathRequest value = request(ASTRA_STORAGE_PATH_OPEN, "/private");
        AstraStoragePathReply reply;
        int fds[3] = {root, -1, -1};
        int result;
        char bytes[3];
        close(sockets[0]);
        if (geteuid() == 0) {
            assert(setgroups(0, NULL) == 0);
            assert(setresgid(gid, gid, gid) == 0 && setresuid(uid, uid, uid) == 0);
            assert(openat(root, "private", O_RDWR) < 0 && errno == EACCES);
        }
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        value.flags = O_RDWR;
        assert(astra_storage_path_call(sockets[1], &value, fds, &reply, &result) == 0);
        assert(reply.error == EACCES && result == -1);
        ++value.serial;
        assert(astra_storage_path_call(sockets[1], &value, fds, &reply, &result) == 0);
        assert(reply.error == 0 && result >= 0);
        assert(pread(result, bytes, sizeof(bytes), 0) == 3 && memcmp(bytes, "old", 3) == 0);
        assert(pwrite(result, "new", 3, 0) == 3);
        close(result);
        /* Missing rooted authority is refused. */
        ++value.serial;
        value.fd_mask = 0;
        assert(astra_storage_path_call(sockets[1], &value, fds, &reply, &result) == 0);
        assert(reply.error == EPROTO && result == -1);
        close(sockets[1]);
        _exit(0);
    }
    close(sockets[1]);
    for (unsigned at = 0; at < 3; ++at) {
        AstraStoragePathRequest value;
        AstraStoragePathReply reply;
        int fds[3];
        int result = -1;
        int error = astra_storage_path_receive(sockets[0], at == 0 ? getpid() : child,
                                               uid, gid, &value, fds);
        if (at == 1) {
            assert(error == 0);
            execute(root, 0, &value, fds, &reply, &result);
        } else {
            assert(error == (at == 0 ? EACCES : EPROTO));
            reply = (AstraStoragePathReply){.version = ASTRA_STORAGE_PROVIDER_VERSION,
                .serial = value.serial, .epoch = value.epoch, .error = error};
        }
        assert(astra_storage_path_reply(sockets[0], &reply, result) == 0);
        if (result >= 0)
            close(result);
        for (unsigned index = 0; index < 3; ++index)
            if (fds[index] >= 0)
                close(fds[index]);
    }
    close(sockets[0]);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(fstatat(root, "private", &after, 0) == 0);
    assert(before.st_uid == after.st_uid && before.st_gid == after.st_gid &&
           before.st_mode == after.st_mode);
    file = openat(root, "private", O_RDONLY | O_CLOEXEC);
    char bytes[3];
    assert(file >= 0 && read(file, bytes, sizeof(bytes)) == 3 && memcmp(bytes, "new", 3) == 0);
    close(file);
    assert(unlinkat(root, "private", 0) == 0);
}

int main(void)
{
    char path[] = "/var/tmp/astra-storage-path-test.XXXXXX";
    assert(mkdtemp(path) != NULL);
    int root = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    assert(root >= 0);
    semantics(root);
    seeding(root);
    lending(root);
    close(root);
    assert(rmdir(path) == 0);
    puts("storage path: confinement, read-only, authenticated FD lending, preserved metadata PASS");
    return 0;
}
