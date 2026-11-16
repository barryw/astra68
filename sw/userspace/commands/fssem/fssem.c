/*
 * fssem -- the filesystem semantic suite (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * Phase 1 exit evidence).
 *
 *   fssem CASE...
 *
 * Each case runs in a fresh directory /work/fssem-<pid>-<case>-XXXXXX and prints
 * "FSSEM PASS <case>" or "FSSEM FAIL <case>: <detail>". The last line is
 * "FSSEM DONE passed P failed F"; the exit status is 0 only when F is 0.
 * Cases use the POSIX layer unless their name says otherwise; the raw-*
 * raw cases drive native grant ports without the Kit and verify that ordinary
 * filesystem authority cannot reach private provider channels.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <astra/host.h>
#include <astra/address_space.h>
#include <astra/application_service.h>
#include <astra/event_control.h>
#include <astra/limits.h>
#include <astra/posix.h>
#include <astra/pager_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service_manager_abi.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_service_core.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service.h>
#include <astra/volume.h>
#include <astra/messaging_abi.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "native_backend.h"

ASTRA_PROGRAM("fssem", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

typedef int (*FssemCase)(const char *directory);

static char failure[192];

static int fail(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(failure, sizeof(failure), format, arguments);
    va_end(arguments);
    return -1;
}

#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition))                                                  \
            return fail(__VA_ARGS__);                                      \
    } while (0)

static void join(char *out, size_t capacity, const char *directory,
                 const char *leaf)
{
    (void)snprintf(out, capacity, "%s/%s", directory, leaf);
}

static int write_text(const char *path, const char *text, int flags)
{
    size_t length = strlen(text);
    int fd = open(path, flags, 0644);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = write(fd, text, length);
    if (close(fd) != 0 || moved != (ssize_t)length)
        return -1;
    return 0;
}

static ssize_t read_text(const char *path, char *out, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = read(fd, out, capacity - 1u);
    (void)close(fd);
    if (moved >= 0)
        out[moved] = '\0';
    return moved;
}

/* Create, read back, rename, unlink: the floor every later case stands on. */
static int case_basic(const char *directory)
{
    char a[160];
    char b[160];
    char text[32];
    struct stat st;

    join(a, sizeof(a), directory, "a");
    join(b, sizeof(b), directory, "b");
    CHECK(write_text(a, "basic", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "create %s: %s", a, strerror(errno));
    CHECK(read_text(a, text, sizeof(text)) == 5 &&
          strcmp(text, "basic") == 0, "read back %s", a);
    CHECK(rename(a, b) == 0, "rename: %s", strerror(errno));
    CHECK(stat(a, &st) != 0 && errno == ENOENT, "old name survived rename");
    CHECK(stat(b, &st) == 0 && st.st_size == 5, "new name: %s",
          strerror(errno));
    CHECK(unlink(b) == 0, "unlink: %s", strerror(errno));
    return 0;
}

static int case_durable_save(const char *directory)
{
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile parent = ASTRA_FILE_INIT, file = ASTRA_FILE_INIT;
    char path[160], pending[160], text[32];
    uint32_t moved = 0u;
    int dirfd, fd;

    CHECK(astra_process_filesystem_open(&native, astra_posix_startup()) == ASTRA_VFS_OK &&
          astra_filesystem_open(&native.filesystem, directory,
              ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY, &parent) == ASTRA_VFS_OK,
          "native durable parent open");
    CHECK(astra_filesystem_open_at(&native.filesystem, &parent, "pending",
              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE,
              &file) == ASTRA_VFS_OK &&
          astra_filesystem_write(&file, UINT64_MAX, "native-old", 10u, &moved) == ASTRA_VFS_OK &&
          moved == 10u && astra_filesystem_sync(&file) == ASTRA_VFS_OK &&
          astra_filesystem_close(&file) == ASTRA_VFS_OK,
          "native durable file write/sync/close");
    CHECK(astra_filesystem_rename_at(&native.filesystem, &parent, "pending",
              &parent, "saved") == ASTRA_VFS_OK &&
          astra_filesystem_sync(&parent) == ASTRA_VFS_OK,
          "native atomic save/parent sync");
    join(path, sizeof(path), directory, "saved");
    CHECK(read_text(path, text, sizeof(text)) == 10 && strcmp(text, "native-old") == 0,
          "POSIX read of native durable save");
    dirfd = open(directory, O_RDONLY | O_DIRECTORY);
    CHECK(dirfd >= 0, "POSIX durable parent open: %s", strerror(errno));
    fd = openat(dirfd, "pending", O_WRONLY | O_CREAT | O_EXCL, 0644);
    CHECK(fd >= 0 && write(fd, "posix-new", 9u) == 9 && fsync(fd) == 0 && close(fd) == 0,
          "POSIX durable file write/sync/close: %s", strerror(errno));
    join(pending, sizeof(pending), directory, "pending");
    CHECK(rename(pending, path) == 0 && fsync(dirfd) == 0,
          "POSIX atomic replacement/parent sync: %s", strerror(errno));
    CHECK(astra_filesystem_open_at(&native.filesystem, &parent, "saved",
              ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK &&
          astra_filesystem_read(&file, text, sizeof(text), &moved) == ASTRA_VFS_OK &&
          moved == 9u && memcmp(text, "posix-new", 9u) == 0 &&
          astra_filesystem_close(&file) == ASTRA_VFS_OK,
          "native read of POSIX durable replacement");
    CHECK(unlinkat(dirfd, "saved", 0) == 0 && fsync(dirfd) == 0 && close(dirfd) == 0 &&
          astra_filesystem_close(&parent) == ASTRA_VFS_OK,
          "durable save cleanup: %s", strerror(errno));
    astra_process_filesystem_close(&native);
    return 0;
}

static int case_symlink_in_volume(const char *directory)
{
    char a[160], b[160], file[160], link[160], path[192], text[32];
    struct stat st;

    join(a, sizeof(a), directory, "a");
    join(b, sizeof(b), directory, "a/b");
    join(file, sizeof(file), directory, "a/file");
    join(link, sizeof(link), directory, "link");
    CHECK(mkdir(a, 0755) == 0 && mkdir(b, 0755) == 0, "mkdir ordered fixture");
    CHECK(write_text(file, "ordered", O_WRONLY | O_CREAT | O_EXCL) == 0, "create ordered fixture");
    CHECK(symlink("a/b", link) == 0, "relative symlink: %s", strerror(errno));
    join(path, sizeof(path), directory, "link/../file");
    CHECK(read_text(path, text, sizeof(text)) == 7 && strcmp(text, "ordered") == 0,
          "link/../file resolved before following link: %s", strerror(errno));
    join(path, sizeof(path), directory, "missing/../a/file");
    CHECK(stat(path, &st) == -1 && errno == ENOENT, "missing component was folded away");
    join(path, sizeof(path), directory, "a/file/../file");
    CHECK(stat(path, &st) == -1 && errno == ENOTDIR, "file component was folded away");
    CHECK(unlink(link) == 0 && symlink("\xC3\xA9", link) == 0,
          "UTF-8 symlink fixture: %s", strerror(errno));
    unsigned char truncated[2] = {0u, 0xA5u};
    CHECK(readlink(link, (char *)truncated, 1u) == 1 && truncated[0] == 0xC3u &&
          truncated[1] == 0xA5u, "readlink byte truncation split UTF-8 incorrectly: %s", strerror(errno));
    return 0;
}

static int case_symlink_cross_volume(const char *directory)
{
    char link_path[160], path[192], target[160], file[192], renamed[160], hard[160], text[32];
    struct stat st;
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile anchor = ASTRA_FILE_INIT, opened = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t moved = 0u;
    int dirfd, fd;

    join(link_path, sizeof(link_path), directory, "system");
    CHECK(symlink("/system", link_path) == 0, "absolute symlink: %s", strerror(errno));
    join(path, sizeof(path), link_path, "commands");
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode),
          "absolute namespace continuation: %s", strerror(errno));
    dirfd = open(directory, O_RDONLY | O_DIRECTORY);
    CHECK(dirfd >= 0 && fstatat(dirfd, "system/commands", &st, 0) == 0 &&
          S_ISDIR(st.st_mode), "dirfd cross-volume stat: %s", strerror(errno));
    fd = openat(dirfd, "system/commands", O_RDONLY | O_DIRECTORY);
    CHECK(fd >= 0 && close(fd) == 0, "dirfd cross-volume open: %s", strerror(errno));

    join(target, sizeof(target), directory, "target");
    join(file, sizeof(file), target, "file");
    join(link_path, sizeof(link_path), directory, "bridge");
    join(path, sizeof(path), link_path, "file");
    join(renamed, sizeof(renamed), directory, "renamed");
    join(hard, sizeof(hard), target, "hard");
    CHECK(mkdir(target, 0755) == 0 && write_text(file, "namespace", O_WRONLY | O_CREAT | O_EXCL) == 0 &&
          symlink(target, link_path) == 0, "absolute same-volume link fixture: %s", strerror(errno));
    CHECK(rename(path, renamed) == 0, "rename through absolute intermediate link: %s", strerror(errno));
    join(path, sizeof(path), link_path, "hard");
    CHECK(link(renamed, path) == 0 && read_text(hard, text, sizeof(text)) == 9 &&
          strcmp(text, "namespace") == 0, "hard link through absolute intermediate link: %s", strerror(errno));
    join(link_path, sizeof(link_path), directory, "absolute-file");
    CHECK(symlink(renamed, link_path) == 0, "absolute file link fixture");
    fd = openat(dirfd, "absolute-file", O_RDONLY);
    CHECK(fd >= 0 && read(fd, text, sizeof(text)) == 9 && close(fd) == 0 &&
          memcmp(text, "namespace", 9u) == 0, "dirfd absolute-link read: %s", strerror(errno));
    CHECK(fstatat(dirfd, "absolute-file", &st, 0) == 0 && S_ISREG(st.st_mode) &&
          fstatat(dirfd, "absolute-file", &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(st.st_mode),
          "dirfd absolute-link final policy: %s", strerror(errno));
    CHECK(astra_process_filesystem_open(&native, astra_posix_startup()) == ASTRA_VFS_OK &&
          astra_filesystem_open(&native.filesystem, directory,
              ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY, &anchor) == ASTRA_VFS_OK &&
          astra_filesystem_open_at(&native.filesystem, &anchor, "absolute-file",
              ASTRA_VFS_OPEN_READ, &opened) == ASTRA_VFS_OK &&
          astra_filesystem_read(&opened, text, sizeof(text), &moved) == ASTRA_VFS_OK &&
          moved == 9u && memcmp(text, "namespace", 9u) == 0,
          "native dirfd namespace read disagrees with POSIX");
    CHECK(astra_filesystem_close(&opened) == ASTRA_VFS_OK &&
          astra_filesystem_stat_at(&native.filesystem, &anchor, "system/commands",
              ASTRA_VFS_STAT_FOLLOW, &info) == ASTRA_VFS_OK &&
          info.kind == ASTRA_VFS_KIND_DIRECTORY,
          "native dirfd cross-volume stat disagrees with POSIX");
    CHECK(astra_filesystem_rename_at(&native.filesystem, &anchor, "bridge/hard",
              &anchor, "moved") == ASTRA_VFS_OK &&
          astra_filesystem_link_at(&native.filesystem, &anchor, "moved",
              &anchor, "bridge/hard") == ASTRA_VFS_OK,
          "native anchored rename/link lost namespace continuation");
    CHECK(astra_filesystem_close(&anchor) == ASTRA_VFS_OK && close(dirfd) == 0,
          "close namespace fixture");
    astra_process_filesystem_close(&native);
    return 0;
}

static int case_dotdot_at_root(const char *directory)
{
    struct stat st;
    DIR *root;
    struct dirent *entry;
    int saw_work = 0;
    (void)directory;
    CHECK(stat("/work/..", &st) == 0 && S_ISDIR(st.st_mode), "provider root parent: %s", strerror(errno));
    CHECK(stat("/work/../system/commands", &st) == 0 && S_ISDIR(st.st_mode),
          "parent into another grant: %s", strerror(errno));
    root = opendir("/work/..");
    CHECK(root != NULL, "namespace root enumeration after parent traversal: %s", strerror(errno));
    errno = 0;
    while ((entry = readdir(root)) != NULL)
        if (strcmp(entry->d_name, "work") == 0)
            saw_work = 1;
    CHECK(errno == 0, "namespace root enumeration: %s", strerror(errno));
    CHECK(closedir(root) == 0 && saw_work, "namespace root contains work");
    CHECK(mkdir("/work/..", 0755) == -1 && errno == EEXIST, "namespace root mkdir");
    CHECK(unlink("/work/..") == -1 && errno == EROFS, "namespace root unlink");
    CHECK(open("/work/..", O_WRONLY | O_CREAT | O_EXCL, 0644) == -1 &&
          errno == EEXIST, "namespace root exclusive create");
    CHECK(chmod("/work/..", 0700) == -1 && errno == EROFS, "namespace root chmod");
    CHECK(rename("/work/..", "/work/other") == -1 && errno == EROFS, "namespace root rename");
    CHECK(link("/work/..", "/work/other") == -1 && errno == EROFS, "namespace root hard link");
    CHECK(symlink("other", "/work/..") == -1 && errno == EROFS, "namespace root symlink");
    CHECK(mkdir("/work/../stuff", 0755) == -1 && errno == EROFS,
          "namespace root publication after parent traversal");
    return 0;
}

static int case_escape_relative(const char *directory)
{
    char link[160], path[192];
    struct stat st;

    join(link, sizeof(link), directory, "up");
    CHECK(symlink("../../system", link) == 0, "escaping relative link");
    join(path, sizeof(path), link, "commands");
    CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode),
          "relative namespace continuation: %s", strerror(errno));
    return 0;
}

static int case_link_edges(const char *directory)
{
    char file[160], link_path[160], target[160], other[160], path[192], text[32];
    struct stat st;
    int fd;

    join(file, sizeof(file), directory, "file");
    join(link_path, sizeof(link_path), directory, "link");
    join(target, sizeof(target), directory, "created");
    join(other, sizeof(other), directory, "other");
    CHECK(write_text(file, "intact", O_WRONLY | O_CREAT | O_EXCL) == 0, "edge fixture");
    CHECK(symlink("file", link_path) == 0, "edge symlink");
    CHECK(lstat(link_path, &st) == 0 && S_ISLNK(st.st_mode), "lstat followed final link");
    fd = open(link_path, O_WRONLY | O_TRUNC | O_NOFOLLOW);
    CHECK(fd == -1 && errno == ELOOP, "nofollow open: %s", strerror(errno));
    CHECK(read_text(file, text, sizeof(text)) == 6 && strcmp(text, "intact") == 0,
          "rejected open truncated target");
    CHECK(unlink(link_path) == 0 && symlink("created", link_path) == 0, "dangling link fixture");
    fd = open(link_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    CHECK(fd == -1 && errno == EEXIST, "exclusive create followed dangling link");
    CHECK(stat(target, &st) == -1 && errno == ENOENT, "exclusive create changed target");
    CHECK(write_text(link_path, "created", O_WRONLY | O_CREAT) == 0, "create through dangling link");
    CHECK(read_text(target, text, sizeof(text)) == 7, "dangling target was not created");
    CHECK(unlink(link_path) == 0 && symlink("other", link_path) == 0 && symlink("link", other) == 0,
          "loop fixture");
    CHECK(stat(link_path, &st) == -1 && errno == ELOOP, "symlink loop did not terminate");
    join(path, sizeof(path), file, "");
    CHECK(stat(path, &st) == -1 && errno == ENOTDIR, "file trailing slash accepted");
    join(path, sizeof(path), directory, "newdir/");
    CHECK(mkdir(path, 0755) == 0 && rmdir(path) == 0, "directory trailing slash: %s", strerror(errno));
    CHECK(mkdir(path, 0755) == 0 && unlink(link_path) == 0 &&
          symlink("newdir", link_path) == 0, "directory link fixture");
    join(path, sizeof(path), link_path, "");
    CHECK(lstat(path, &st) == 0 && S_ISDIR(st.st_mode), "trailing lstat did not follow directory link");
    CHECK(mkdir(path, 0755) == -1 && errno == EEXIST, "mkdir followed existing directory link");
    CHECK(rmdir(path) == -1 && errno == ENOTDIR, "rmdir followed final directory link");
    CHECK(rename(path, other) == -1 && errno == ENOTDIR, "rename followed final directory link");
    CHECK(lstat(link_path, &st) == 0 && S_ISLNK(st.st_mode), "mutation destroyed directory link");
    join(path, sizeof(path), directory, "absent/");
    CHECK(link(file, path) == -1, "hard link ignored destination trailing slash");
    CHECK(rename(file, path) == -1 && errno == ENOTDIR, "rename ignored file destination trailing slash");
    CHECK(stat(file, &st) == 0, "rejected rename changed source");
    return 0;
}

/* Ordinary filesystem authority cannot be used as a provider capability. */
static int case_raw_lane(const char *directory)
{
    (void)directory;
    const AstraAssign *work = astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    AstraVfsClient *client = astra_process_vfs_client_for(work);
    AstraVfsDirEntry info = {0};
    AstraHostLeaseInfo lease = {0};
    AstraHostChannelClient channel = {0};
    CHECK(client != NULL && astra_vfs_stat_meta(client, "/", &info) == ASTRA_VFS_OK,
        "native WORK unavailable");
    CHECK(astra_host_lease_query(work->handle, &lease) != ASTRA_SYSCALL_OK,
        "native grant exposed provider lease authority");
    uint32_t status = astra_host_client_open(work->handle, ASTRA_HOST_CAP_FS_SCOPE,
        256u, &channel);
    if (status == ASTRA_SYSCALL_OK) (void)astra_host_client_close(&channel);
    CHECK(status != ASTRA_SYSCALL_OK, "native grant opened provider channel");
    return 0;
}

/* The `leaf` of a /work case directory, seen through /cwd: the shell's CWD
 * grant is derived from WORK at WORK's root. */
static void cwd_path(char *out, size_t capacity, const char *directory,
                     const char *leaf)
{
    (void)snprintf(out, capacity, "/cwd%s/%s", directory + strlen("/work"),
                   leaf);
}

static int case_alias_identity(const char *directory)
{
    char file[160], alias[200], hard[160], other[160], moved[160];
    struct stat a, b;
    int fd;

    join(file, sizeof(file), directory, "file");
    cwd_path(alias, sizeof(alias), directory, "file");
    join(hard, sizeof(hard), directory, "hard");
    join(other, sizeof(other), directory, "other");
    join(moved, sizeof(moved), directory, "moved");
    CHECK(write_text(file, "identity", O_WRONLY | O_CREAT | O_EXCL) == 0 &&
          write_text(other, "other", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "identity fixture");
    CHECK(stat(file, &a) == 0 && stat(alias, &b) == 0 &&
          a.st_dev != 0 && a.st_ino != 0 && a.st_dev == b.st_dev &&
          a.st_ino == b.st_ino, "aliases disagree about identity");
    CHECK(stat(other, &b) == 0 && a.st_ino != b.st_ino, "distinct nodes share identity");
    CHECK(link(file, hard) == 0 && stat(hard, &b) == 0 &&
          a.st_dev == b.st_dev && a.st_ino == b.st_ino && b.st_nlink == 2,
          "hard-link identity");
    CHECK(rename(hard, moved) == 0 && stat(moved, &b) == 0 &&
          a.st_ino == b.st_ino, "rename changed identity");
    CHECK(chmod(file, 0000) == 0 && stat(file, &b) == 0 &&
          (b.st_mode & 07777) == 0, "real mode zero was replaced by a default");
    CHECK(chmod(file, 0600) == 0, "restore mode");
    fd = open(file, O_RDWR);
    CHECK(fd >= 0 && unlink(file) == 0 && fstat(fd, &b) == 0 &&
          a.st_ino == b.st_ino && b.st_nlink == 1, "held unlinked file identity");
    CHECK(fchmod(fd, 0400) == 0 && fstat(fd, &b) == 0 &&
          (b.st_mode & 07777) == 0400,
          "held unlinked file mode");
    CHECK(ftruncate(fd, 1 << 20) == 0 && fstat(fd, &b) == 0 &&
          b.st_blksize > 0 && b.st_blocks < b.st_size / 512 &&
          b.st_mtim.tv_nsec >= 0 && b.st_mtim.tv_nsec < 1000000000,
          "provider allocation or timestamp metadata");
    CHECK(close(fd) == 0, "close identity fixture");
    CHECK(unlink(other) == 0 && unlink(moved) == 0 &&
          rmdir(directory) == 0, "identity fixture cleanup");
    return 0;
}

static int case_continuation_limit(const char *directory)
{
    char link[160];
    char *target = malloc(ASTRA_VFS_PATH_MAX);
    struct stat st;
    size_t length = strlen("/system");

    CHECK(target != NULL, "continuation allocation");
    strcpy(target, "/system");
    while (length + 2u < ASTRA_VFS_PATH_MAX) {
        target[length++] = '/';
        target[length++] = '.';
    }
    target[length] = '\0';
    CHECK(length == ASTRA_VFS_PATH_MAX - 1u, "exact-limit fixture");
    join(link, sizeof(link), directory, "long");
    int valid = symlink(target, link) == 0 && stat(link, &st) == 0 && S_ISDIR(st.st_mode);
    int saved_error = errno;
    free(target);
    CHECK(valid, "valid maximum-length continuation: %s", strerror(saved_error));
    return 0;
}

static int path_boundaries(const char *directory, char *path, char *other, char *saved)
{
    CHECK(ASTRA_VFS_PATH_MAX == 4096u && ASTRA_VFS_NAME_MAX == 256u,
          "production path/name contract not installed");
    size_t base = strlen(directory);
    memcpy(path, directory, base);
    path[base] = '/';
    memset(path + base + 1u, 'n', 255u);
    path[base + 256u] = '\0';
    CHECK(write_text(path, "full", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "255-byte component: %s", strerror(errno));
    DIR *listing = opendir(directory);
    CHECK(listing != NULL, "long-name directory");
    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL)
        if (strcmp(entry->d_name, path + base + 1u) == 0) found = 1;
    int closed = closedir(listing);
    CHECK(found && closed == 0, "255-byte name not enumerated intact");
    path[base + 256u] = 'n';
    path[base + 257u] = '\0';
    struct stat info;
    errno = 0;
    CHECK(stat(path, &info) == -1 && errno == ENAMETOOLONG,
          "256-byte component errno %d", errno);
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    uint32_t opened = astra_process_filesystem_open(&native, astra_posix_startup());
    AstraFileInfo native_info = ASTRA_FILE_INFO_INIT;
    uint32_t native_status = opened == ASTRA_VFS_OK ?
        astra_filesystem_stat(&native.filesystem, path, &native_info) : opened;
    astra_process_filesystem_close(&native);
    CHECK(native_status == ASTRA_VFS_ERR_NAME_TOO_LONG,
          "native long component status %u", native_status);
    path[base + 256u] = '\0';
    CHECK(unlink(path) == 0, "long-name cleanup");

    strcpy(path, directory);
    size_t length = base;
    while (ASTRA_VFS_PATH_MAX - 1u - length > 256u) {
        path[length++] = '/';
        memset(path + length, 'd', 255u);
        length += 255u;
        path[length] = '\0';
        CHECK(mkdir(path, 0755) == 0, "deep directory length %lu: %s",
              (unsigned long)length, strerror(errno));
    }
    size_t parent_length = length;
    size_t remaining = ASTRA_VFS_PATH_MAX - 1u - length;
    CHECK(remaining > 1u && remaining <= 256u, "deep fixture remainder");
    path[length++] = '/';
    memset(path + length, 'f', remaining - 1u);
    length += remaining - 1u;
    path[length] = '\0';
    CHECK(length == 4095u && write_text(path, "full", O_WRONLY | O_CREAT | O_EXCL) == 0 &&
          stat(path, &info) == 0 && info.st_size == 4, "4095-byte path: %s", strerror(errno));
    opened = astra_process_filesystem_open(&native, astra_posix_startup());
    native_status = opened == ASTRA_VFS_OK ?
        astra_filesystem_stat(&native.filesystem, path, &native_info) : opened;
    astra_process_filesystem_close(&native);
    CHECK(native_status == ASTRA_VFS_OK, "native maximum path status %u", native_status);
    path[length] = 'f'; path[length + 1u] = '\0';
    errno = 0;
    CHECK(stat(path, &info) == -1 && errno == ENAMETOOLONG, "4096-byte path errno %d", errno);
    opened = astra_process_filesystem_open(&native, astra_posix_startup());
    native_status = opened == ASTRA_VFS_OK ?
        astra_filesystem_stat(&native.filesystem, path, &native_info) : opened;
    astra_process_filesystem_close(&native);
    CHECK(native_status == ASTRA_VFS_ERR_NAME_TOO_LONG, "native overlong path status %u", native_status);
    path[length] = '\0';
    strcpy(other, path); other[length - 1u] = 'r';
    CHECK(rename(path, other) == 0 && stat(other, &info) == 0 && info.st_size == 4,
          "two maximum-length rename operands: %s", strerror(errno));
    CHECK(unlink(other) == 0, "deep file cleanup");
    path[parent_length] = '\0';
    CHECK(getcwd(saved, ASTRA_VFS_PATH_MAX) != NULL && chdir(path) == 0 &&
          getcwd(other, ASTRA_VFS_PATH_MAX) != NULL && strcmp(path, other) == 0,
          "deep CWD identity: %s", strerror(errno));
    pid_t child = fork();
    CHECK(child >= 0, "deep CWD fork");
    if (child == 0) {
        char *const arguments[] = {"fssem", "--cwd-helper", path, NULL};
        char *const environment[] = {NULL};
        /* This tests CWD transfer, within the combined argv/environment budget. */
        execve("/commands/fssem", arguments, environment);
        fprintf(stderr, "FSSEM deep CWD exec errno=%d\n", errno);
        _exit(127);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "deep CWD exec handoff: %d", status);
    CHECK(unlink("exec") == 0 && chdir(saved) == 0, "deep CWD cleanup");
    return 0;
}

static int case_path_boundaries(const char *directory)
{
    size_t bytes = (size_t)ASTRA_VFS_PATH_MAX + 1u;
    char *storage = malloc(bytes * 3u);
    CHECK(storage != NULL, "path boundary allocation");
    int result = path_boundaries(directory, storage, storage + bytes, storage + bytes * 2u);
    free(storage);
    return result;
}

/* fork and exec keep native volumes working through independent child reply
 * lanes and transferred native descriptions. The terminal's
 * CWD is a grant of its own (Task 5), so a command holds two host-volume
 * clients -- WORK and CWD -- and both are live across the fork (Task 3b
 * review M6). */
static int case_lane_fork_exec(const char *directory)
{
    const AstraAssign *work_assign =
        astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    const AstraAssign *cwd_assign =
        astra_assign_lookup(astra_process_vfs_assigns(), "CWD");
    AstraVfsClient *work;
    AstraVfsClient *cwd;
    char path[160];
    char through_cwd[160];
    struct stat st;
    pid_t child;
    int status;

    join(path, sizeof(path), directory, "lane-file");
    cwd_path(through_cwd, sizeof(through_cwd), directory, "lane-file");
    CHECK(write_text(path, "lane", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed");
    CHECK(stat(through_cwd, &st) == 0 && st.st_size == 4,
          "%s through CWD: %s", through_cwd, strerror(errno));
    work = astra_process_vfs_client_for(work_assign);
    cwd = astra_process_vfs_client_for(cwd_assign);
    CHECK(work != NULL && cwd != NULL && work != cwd &&
          astra_process_vfs_client_handle(work) != 0u &&
          astra_process_vfs_client_handle(cwd) != 0u &&
          astra_process_vfs_client_handle(work) != astra_process_vfs_client_handle(cwd),
          "CWD is not a host client of its own");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0)
        _exit(stat(path, &st) == 0 && st.st_size == 4 &&
              stat(through_cwd, &st) == 0 && st.st_size == 4 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0,
          "fork child could not stat through both clients");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        char *const argv[] = {"fssem", "--lane-helper", path, NULL};

        execv("/commands/fssem", argv);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "exec'd image could not stat: %d",
          WEXITSTATUS(status));
    return 0;
}
static int case_cache_pressure(const char *directory)
{
    char path[192];

    for (unsigned index = 0u; index < 512u; ++index) {
        (void)snprintf(path, sizeof(path), "%s/cache-%u", directory, index);
        unsigned char expected = (unsigned char)(index % 251u), actual = 0u;
        int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
        CHECK(fd >= 0, "cache pressure open %u: %s", index, strerror(errno));
        ssize_t written = write(fd, &expected, 1u);
        ssize_t read = pread(fd, &actual, 1u, 0u);
        int error = errno;
        int closed = close(fd);
        CHECK(written == 1 && read == 1 && actual == expected && closed == 0,
            "cache pressure %u write=%ld read=%ld byte=%u expected=%u close=%d errno=%d",
            index, (long)written, (long)read, actual, expected, closed, error);
    }
    return 0;
}

static int case_sequential_race(const char *directory)
{
    char path[160];
    unsigned char sequence[32], counts[32] = {0}, child_counts[32];
    unsigned char byte;
    int gate[2], result[2], status = 0, failed = 0;
    uint32_t reads = 0u;
    ssize_t moved;

    join(path, sizeof(path), directory, "sequential-race");
    for (uint32_t index = 0u; index < sizeof(sequence); ++index)
        sequence[index] = (unsigned char)index;
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, sequence, sizeof(sequence)) == sizeof(sequence) &&
          lseek(fd, 0, SEEK_SET) == 0 && pipe(gate) == 0 && pipe(result) == 0,
          "sequential race setup: %s", strerror(errno));
    pid_t child = fork();
    CHECK(child >= 0, "sequential race fork: %s", strerror(errno));
    if (child == 0) {
        (void)close(gate[1]); (void)close(result[0]);
        if (read(gate[0], &byte, 1u) != 1) _exit(1);
        (void)close(gate[0]);
    } else {
        (void)close(gate[0]); (void)close(result[1]);
        CHECK(write(gate[1], "g", 1u) == 1, "sequential race release");
        (void)close(gate[1]);
    }
    while ((moved = read(fd, &byte, 1u)) == 1) {
        if (byte >= sizeof(counts) || ++reads > sizeof(sequence)) {
            failed = 1;
            break;
        }
        ++counts[byte];
    }
    if (moved != 0) failed = 1;
    if (failed)
        printf("FSSEM sequential race %s reads=%lu moved=%ld errno=%d\n",
            child == 0 ? "child" : "parent", (unsigned long)reads, (long)moved, errno);
    if (child == 0) {
        if (write(result[1], counts, sizeof(counts)) != sizeof(counts)) failed = 1;
        (void)close(result[1]); (void)close(fd);
        _exit(failed);
    }
    ssize_t received = read(result[0], child_counts, sizeof(child_counts));
    pid_t reaped = waitpid(child, &status, 0);
    off_t position = lseek(fd, 0, SEEK_CUR);
    CHECK(received == sizeof(child_counts) && reaped == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0 && !failed &&
          position == sizeof(sequence), "race got=%ld child=%ld status=%d bad=%d pos=%ld errno=%d",
          (long)received, (long)reaped, status, failed, (long)position, errno);
    (void)close(result[0]); (void)close(fd);
    for (uint32_t index = 0u; index < sizeof(counts); ++index)
        CHECK(counts[index] + child_counts[index] == 1u,
              "sequential race byte %u delivered %u times", index,
              counts[index] + child_counts[index]);
    return 0;
}

static int case_shared_description(const char *directory)
{
    char path[160];
    char sibling[160];
    char bytes[16];
    int go[2], done[2], fd, alias, status;
    DIR *stream;
    pid_t child;

    join(path, sizeof(path), directory, "data");
    join(sibling, sizeof(sibling), directory, "other");
    CHECK(write_text(path, "abcdef", O_WRONLY | O_CREAT | O_EXCL) == 0 &&
          write_text(sibling, "", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    fd = open(path, O_RDWR | O_APPEND | O_NONBLOCK | O_CLOEXEC);
    CHECK(fd >= 0 && (alias = dup(fd)) >= 0, "open/dup: %s", strerror(errno));
    CHECK((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0 &&
          fcntl(fd, F_GETFD) == FD_CLOEXEC && fcntl(alias, F_GETFD) == 0,
          "initial status/descriptor flags");
    stream = opendir(directory);
    CHECK(stream != NULL && readdir(stream) != NULL, "directory stream");
    CHECK(fcntl(dirfd(stream), F_GETFD) == FD_CLOEXEC,
          "opendir descriptor survives exec");
    CHECK(pipe(go) == 0 && pipe(done) == 0, "pipes");
    CHECK(read(fd, bytes, 1u) == 1 && bytes[0] == 'a', "initial position");
    child = fork();
    CHECK(child >= 0, "fork with descriptions: %s", strerror(errno));
    if (child == 0) {
        (void)close(go[1]); (void)close(done[0]);
        if (read(alias, bytes, 1u) != 1 || bytes[0] != 'b' ||
            pread(fd, bytes, 1u, 0) != 1 || bytes[0] != 'a' ||
            pwrite(fd, "Z", 1u, 0) != 1 || lseek(alias, 0, SEEK_CUR) != 2 ||
            fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || readdir(stream) == NULL ||
            write(done[1], "d", 1u) != 1 || read(go[0], bytes, 1u) != 1 ||
            write(alias, "X", 1u) != 1 || lseek(fd, 0, SEEK_CUR) != 3)
            _exit(1);
        (void)close(fd); (void)close(alias); (void)closedir(stream);
        _exit(0);
    }
    (void)close(go[0]); (void)close(done[1]);
    CHECK(read(done[0], bytes, 1u) == 1 && lseek(alias, 0, SEEK_CUR) == 2 &&
          (fcntl(fd, F_GETFL) & (O_APPEND | O_NONBLOCK)) == O_NONBLOCK,
          "shared position/flags");
    CHECK(close(fd) == 0 && close(alias) == 0 && closedir(stream) == 0,
          "parent close");
    CHECK(write(go[1], "g", 1u) == 1, "release child");
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "child description: %d", status);
    (void)close(go[1]); (void)close(done[0]);
    CHECK(read_text(path, bytes, sizeof(bytes)) == 6 &&
          strcmp(bytes, "ZbXdef") == 0, "positioned/retained writes: %s", bytes);
    /* A grandchild keeps the description after its actual parent exits. */
    fd = open(path, O_RDWR);
    CHECK(fd >= 0 && pipe(go) == 0 && pipe(done) == 0, "parent-exit setup");
    child = fork();
    CHECK(child >= 0, "parent-exit fork");
    if (child == 0) {
        pid_t grandchild = fork();

        if (grandchild < 0) _exit(1);
        if (grandchild > 0) _exit(0);
        (void)close(go[1]); (void)close(done[0]);
        if (read(go[0], bytes, 1u) != 1 || write(fd, "Q", 1u) != 1 ||
            close(fd) != 0 || write(done[1], "d", 1u) != 1)
            _exit(1);
        _exit(0);
    }
    (void)close(go[0]); (void)close(done[1]);
    pid_t waited = waitpid(child, &status, 0);
    CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "parent exit child=%ld waited=%ld status=0x%x errno=%d",
          (long)child, (long)waited, status, errno);
    CHECK(close(fd) == 0, "parent-exit description close errno=%d", errno);
    CHECK(write(go[1], "g", 1u) == 1 && read(done[0], bytes, 1u) == 1,
          "description lost on parent exit");
    (void)close(go[1]); (void)close(done[0]);
    CHECK(read_text(path, bytes, sizeof(bytes)) == 6 &&
          strcmp(bytes, "QbXdef") == 0, "parent-exit data");
    return case_sequential_race(directory);
}

static int case_exec_inherit(const char *directory)
{
    char path[160];
    char fd_text[24];
    char bytes[16];
    pid_t child;
    int fd, status;

    join(path, sizeof(path), directory, "exec");
    CHECK(write_text(path, "abcdef", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    fd = open(path, O_RDWR);
    CHECK(fd >= 0 && read(fd, bytes, 1u) == 1, "open/read");
    {
        char *oversize = malloc(ASTRA_LAUNCH_STRING_BYTES + 1u);
        char *const no_environment[] = {NULL};
        pid_t before = getpid();

        CHECK(oversize != NULL, "exec budget allocation");
        memset(oversize, 'x', ASTRA_LAUNCH_STRING_BYTES);
        oversize[ASTRA_LAUNCH_STRING_BYTES] = '\0';
        char *const arguments[] = {"fssem", oversize, NULL};
        errno = 0;
        int replaced = execve("/commands/fssem", arguments, no_environment);
        int saved_error = errno;
        free(oversize);
        CHECK(replaced == -1 && saved_error == E2BIG && getpid() == before &&
              lseek(fd, 0, SEEK_CUR) == 1,
              "exec budget changed live image/description: return %d errno %d",
              replaced, saved_error);
    }
    (void)snprintf(fd_text, sizeof(fd_text), "%d", fd);
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        char *const arguments[] = {"fssem", "--descriptor-helper", fd_text, NULL};
        execv("/commands/fssem", arguments);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0 && lseek(fd, 0, SEEK_CUR) == 2,
          "exec inheritance: %d", status);
    child = fork();
    CHECK(child >= 0, "parent exec fork");
    if (child == 0) {
        pid_t grandchild = fork();
        char pid_text[24];

        if (grandchild < 0)
            _exit(1);
        if (grandchild == 0)
            _exit(42);
        (void)snprintf(pid_text, sizeof(pid_text), "%ld", (long)grandchild);
        char *const arguments[] = {"fssem", "--wait-helper", pid_text, NULL};
        execv("/commands/fssem", arguments);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "child accounting across parent exec: %d", status);
    CHECK(close(fd) == 0, "close");
    return 0;
}

/* CWD follows its open directory through rename, fork, exec and removal. */
static int case_cwd_identity(const char *directory)
{
    char before[160], after[160], final[160], path[192], cwd[192], text[32];
    struct stat st;
    int dirfd, go[2], status;
    pid_t child;
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;

    join(before, sizeof(before), directory, "before");
    join(after, sizeof(after), directory, "after");
    join(final, sizeof(final), directory, "final");
    CHECK(mkdir(before, 0755) == 0 && chdir(before) == 0, "chdir fixture: %s", strerror(errno));
    CHECK(rename(before, after) == 0 && mkdir(before, 0755) == 0, "rename CWD fixture");
    CHECK(getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, after) == 0,
          "renamed CWD spelling: %s", strerror(errno));
    errno = 0;
    CHECK(getcwd(cwd, 2u) == NULL && errno == ERANGE, "small getcwd buffer: %s", strerror(errno));
    CHECK(write_text("born", "identity", O_WRONLY | O_CREAT | O_EXCL) == 0, "relative create: %s", strerror(errno));
    join(path, sizeof(path), after, "born");
    CHECK(stat(path, &st) == 0 && st.st_size == 8, "relative create missed retained CWD");
    join(path, sizeof(path), before, "born");
    CHECK(stat(path, &st) == -1 && errno == ENOENT, "replacement old CWD received file");
    CHECK(mkdir("sub", 0700) == 0 && rename("born", "renamed") == 0 &&
          link("renamed", "hard") == 0 && symlink("renamed", "soft") == 0,
          "relative path mutations: %s", strerror(errno));
    CHECK(readlink("soft", text, sizeof(text)) == 7 && memcmp(text, "renamed", 7u) == 0,
          "relative readlink: %s", strerror(errno));
    dirfd = open(".", O_RDONLY | O_DIRECTORY);
    CHECK(dirfd >= 0 && fstatat(dirfd, "soft", &st, 0) == 0 && S_ISREG(st.st_mode) &&
          fstatat(dirfd, "soft", &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(st.st_mode),
          "fstatat follow policy: %s", strerror(errno));
    CHECK(close(dirfd) == 0 && chmod("renamed", 0640) == 0 &&
          stat("renamed", &st) == 0 && (st.st_mode & 0777) == 0640,
          "relative metadata: %s", strerror(errno));
    CHECK(astra_process_filesystem_open(&native, astra_posix_startup()) == ASTRA_VFS_OK &&
          astra_filesystem_stat(&native.filesystem, "renamed", &info) == ASTRA_VFS_OK &&
          info.byte_size == 8u, "native facade has different CWD");
    AstraDirectory listing = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[2];
    uint32_t count = 0u;
    CHECK(astra_filesystem_directory_open(&native.filesystem, ".", &listing) == ASTRA_VFS_OK &&
          astra_filesystem_directory_read(&listing, entries, 2u, &count) == ASTRA_VFS_OK &&
          count == 2u, "native relative directory enumeration");
    astra_filesystem_directory_close(&listing);
    astra_process_filesystem_close(&native);
    CHECK(pipe(go) == 0, "CWD fork pipe");
    child = fork();
    CHECK(child >= 0, "CWD fork: %s", strerror(errno));
    if (child == 0) {
        (void)close(go[1]);
        if (read(go[0], text, 1u) != 1 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, final) != 0 || write_text("child", "child", O_WRONLY | O_CREAT | O_EXCL) != 0)
            _exit(1);
        (void)close(go[0]);
        _exit(0);
    }
    (void)close(go[0]);
    CHECK(rename(after, final) == 0 && mkdir(after, 0755) == 0 && write(go[1], "g", 1u) == 1,
          "rename while child holds CWD");
    (void)close(go[1]);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child CWD identity: %d", status);
    join(path, sizeof(path), final, "child");
    CHECK(stat(path, &st) == 0 && symlink("/commands/fssem", "runner") == 0,
          "CWD exec fixture");
    child = fork();
    CHECK(child >= 0, "CWD exec fork: %s", strerror(errno));
    if (child == 0) {
        char *const arguments[] = {"fssem", "--cwd-helper", final, NULL};
        execv("runner", arguments);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "CWD identity across relative exec: %d", status);
    join(path, sizeof(path), final, "exec");
    CHECK(stat(path, &st) == 0 && chdir("..") == 0 &&
          getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, directory) == 0,
          "renamed CWD parent: %s", strerror(errno));
    join(path, sizeof(path), final, "gone");
    CHECK(mkdir(path, 0755) == 0 && chdir(path) == 0 && rmdir(path) == 0 && mkdir(path, 0755) == 0,
          "removed CWD fixture: %s", strerror(errno));
    errno = 0;
    CHECK(getcwd(cwd, sizeof(cwd)) == NULL && errno == ENOENT,
          "removed CWD reopened replacement: %s", strerror(errno));
    CHECK(write_text("ghost", "bad", O_WRONLY | O_CREAT | O_EXCL) == -1 && errno == ENOENT,
          "relative create in removed CWD: %s", strerror(errno));
    CHECK(chdir("/") == 0 && getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, "/") == 0,
          "absolute chdir recovery: %s", strerror(errno));
    return 0;
}

static volatile sig_atomic_t file_size_signals;

/* Count delivery as well as EFBIG; no existing semantic case handles signals. */
static void file_size_signal(int number)
{
    (void)number;
    ++file_size_signals;
}

static int case_file_size_limit(const char *directory)
{
    char path[160], bytes[513];
    struct stat metadata;
    struct rlimit limit = {16, 16};
    pid_t children[2];
    int fd, status;

    join(path, sizeof(path), directory, "bounded");
    CHECK(write_text(path, "abc", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    fd = open(path, O_RDWR | O_APPEND);
    CHECK(fd >= 0 && lseek(fd, 0, SEEK_SET) == 0, "append setup");
    children[0] = fork();
    CHECK(children[0] >= 0, "limit fork");
    if (children[0] == 0) {
        memset(bytes, 'x', sizeof(bytes));
        if (signal(SIGXFSZ, file_size_signal) == SIG_ERR)
            _exit(3);
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
            _exit(4);
        if (write(fd, bytes, sizeof(bytes)) != 13)
            _exit(5);
        if (lseek(fd, 0, SEEK_CUR) != 16)
            _exit(6);
        if (write(fd, NULL, 0) != 0 || file_size_signals != 0)
            _exit(7);
        if (write(fd, bytes, 1) != -1 || errno != EFBIG)
            _exit(8);
        if (file_size_signals != 1)
            _exit(9);
        if (pwrite(fd, "YZ", 2, 15) != 1)
            _exit(10);
        if (lseek(fd, 0, SEEK_CUR) != 16)
            _exit(11);
        if (pwrite(fd, NULL, 0, 16) != 0 || file_size_signals != 1)
            _exit(12);
        if (pwrite(fd, bytes, 1, 16) != -1 || errno != EFBIG)
            _exit(13);
        if (file_size_signals != 2)
            _exit(14);
        {
            char control_path[64];
            int control_fd, priority = getpriority(PRIO_PROCESS, 0);

            snprintf(control_path, sizeof(control_path), "/proc/%ld/ctl", (long)getpid());
            control_fd = open(control_path, O_WRONLY);
            limit.rlim_cur = 4;
            if (control_fd < 0 || setrlimit(RLIMIT_FSIZE, &limit) != 0 ||
                write(control_fd, "priority 27", 11) != -1 || errno != EFBIG ||
                file_size_signals != 3 || getpriority(PRIO_PROCESS, 0) != priority ||
                lseek(control_fd, 0, SEEK_CUR) != 0 || close(control_fd) != 0)
                _exit(2);
        }
        if (ftruncate(fd, 17) != -1 || errno != EFBIG || file_size_signals != 4 ||
            fstat(fd, &metadata) != 0 || metadata.st_size != 16)
            _exit(15);
        _exit(0);
    }
    {
        pid_t waited = waitpid(children[0], &status, 0);
        int closed = close(fd);

        CHECK(waited == children[0] && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "append/positioned limit or zero write: stage %d status %d",
              WIFEXITED(status) ? WEXITSTATUS(status) : -1, status);
        CHECK(closed == 0, "close bounded file");
    }
    CHECK(stat(path, &metadata) == 0 && metadata.st_size == 16 &&
          read_text(path, bytes, sizeof(bytes)) == 16 && bytes[15] == 'Y',
          "bounded write changed size/data");

    join(path, sizeof(path), directory, "race");
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0644);
    CHECK(fd >= 0, "bounded race setup");
    for (unsigned index = 0; index < 2; ++index) {
        children[index] = fork();
        CHECK(children[index] >= 0, "bounded race fork");
        if (children[index] == 0) {
            ssize_t moved;

            limit.rlim_cur = limit.rlim_max = 256;
            memset(bytes, index == 0 ? 'L' : 'R', sizeof(bytes));
            fd = open(path, O_WRONLY | O_APPEND);
            if (fd < 0)
                _exit(1);
            if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR ||
                setrlimit(RLIMIT_FSIZE, &limit) != 0)
                _exit(1);
            moved = write(fd, bytes, sizeof(bytes));
            _exit(moved == 256 || (moved == -1 && errno == EFBIG) ? 0 : 1);
        }
    }
    for (unsigned index = 0; index < 2; ++index)
        CHECK(waitpid(children[index], &status, 0) == children[index] &&
              WIFEXITED(status) && WEXITSTATUS(status) == 0, "bounded race child");
    CHECK(close(fd) == 0 && stat(path, &metadata) == 0 && metadata.st_size == 256,
          "concurrent append exceeded limit");
    return 0;
}

static int case_append_race(const char *directory)
{
    char path[160];
    char record[16];
    char contents[4097];
    pid_t child;
    int fd, status;
    unsigned counts[2] = {0u, 0u};

    join(path, sizeof(path), directory, "append");
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0644);
    CHECK(fd >= 0, "open append");
    child = fork();
    CHECK(child >= 0, "append fork");
    memset(record, child == 0 ? 'C' : 'P', sizeof(record));
    for (unsigned index = 0u; index < 128u; ++index) {
        if (write(fd, record, sizeof(record)) != sizeof(record)) {
            if (child == 0) _exit(1);
            return fail("append write");
        }
    }
    if (child == 0) { (void)close(fd); _exit(0); }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0 && lseek(fd, 0, SEEK_CUR) == 4096,
          "append child/position");
    CHECK(close(fd) == 0 && read_text(path, contents, sizeof(contents)) == 4096,
          "append length");
    for (unsigned offset = 0u; offset < 4096u; offset += sizeof(record)) {
        char kind = contents[offset];

        CHECK(kind == 'P' || kind == 'C', "append record kind");
        for (unsigned index = 1u; index < sizeof(record); ++index)
            CHECK(contents[offset + index] == kind, "torn append record");
        ++counts[kind == 'C'];
    }
    CHECK(counts[0] == 128u && counts[1] == 128u, "lost append records");
    return 0;
}

/* A storage-protocol client with no Kit: what a hostile program can send. */
typedef struct RawClient {
    uint32_t service;
    uint32_t reply_receive;
    uint32_t owns_reply;
    uint32_t session;
    uint32_t area;
    void *address;
    uint32_t capacity;
    const char *stage;
} RawClient;

#define RAW_LANE 0x5EA1u

static uint32_t raw_open_until(RawClient *raw, uint32_t service,
                               uint64_t deadline, uint32_t reply_receive,
                               uint32_t reply_send, int control_only);

static uint32_t raw_open(RawClient *raw, uint32_t service)
{
    return raw_open_until(raw, service, ASTRA_DEADLINE_FOREVER, 0u, 0u, 0);
}

/* HELLO, answered by `deadline` or not at all: a service that declines a
 * HELLO answers nothing, and ASTRA_SYSCALL_TIMED_OUT says so. */
static uint32_t raw_open_until(RawClient *raw, uint32_t service,
                               uint64_t deadline, uint32_t reply_receive,
                               uint32_t shared_reply_send, int control_only)
{
    uint32_t reply_send = 0u;
    AstraVfsRequestMessage message;
    AstraVfsReplyMessage reply;
    uint32_t handles[1];
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    memset(raw, 0, sizeof(*raw));
    raw->service = service;
    if (reply_receive != 0u) {
        raw->reply_receive = reply_receive;
        raw->stage = "reply-duplicate";
        status = astra_rt_handle_duplicate(shared_reply_send,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
            ASTRA_RIGHT_TRANSFER, &reply_send);
    } else {
        raw->owns_reply = 1u;
        raw->stage = "reply-port";
        status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(reply),
                                      &raw->reply_receive, &reply_send);
    }
    if (status != ASTRA_SYSCALL_OK)
        return status;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             ASTRA_VFS_OP_HELLO, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    handles[0] = reply_send;
    raw->stage = "hello-send";
    status = astra_port_send(service, &message, sizeof(message), handles, 1u);
    (void)astra_close(reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    raw->stage = "hello-wait";
    status = astra_wait_one(raw->reply_receive, deadline, NULL);
    if (status == ASTRA_SYSCALL_OK) {
        raw->stage = "hello-receive";
        status = astra_port_receive(raw->reply_receive, &reply, sizeof(reply),
                                    handles, 1u, &size, &count);
    }
    if (status != ASTRA_SYSCALL_OK)
        return status;
    raw->stage = "hello-status";
    if (reply.reply.status != ASTRA_VFS_OK) {
        if (count != 0u)
            (void)astra_close(handles[0]);
        return reply.reply.status;
    }
    raw->session = reply.reply.session;
    if (count != 0u) {
        (void)astra_close(handles[0]);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    if (control_only) {
        raw->stage = "ready";
        return ASTRA_SYSCALL_OK;
    }
    raw->stage = "area-create";
    status = astra_rt_area_create(4096u,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER, &raw->area);
    if (status == ASTRA_SYSCALL_OK) {
        raw->stage = "area-map";
        status = astra_rt_area_map(raw->area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &raw->address, &raw->capacity);
    }
    if (status != ASTRA_SYSCALL_OK) return status;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
        ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, ASTRA_VFS_OP_BIND_AREA, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = raw->session;
    message.request.length = raw->capacity;
    uint32_t area_send = 0u;
    raw->stage = "area-duplicate";
    status = astra_rt_handle_duplicate(raw->area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area_send);
    if (status == ASTRA_SYSCALL_OK) {
        handles[0] = area_send;
        raw->stage = "bind-send";
        status = astra_port_send(service, &message, sizeof(message), handles, 1u);
    }
    if (area_send != 0u) (void)astra_close(area_send);
    if (status == ASTRA_SYSCALL_OK) {
        raw->stage = "bind-wait";
        status = astra_wait_one(raw->reply_receive, deadline, NULL);
    }
    if (status == ASTRA_SYSCALL_OK) {
        raw->stage = "bind-receive";
        status = astra_port_receive(raw->reply_receive, &reply, sizeof(reply),
            handles, 1u, &size, &count);
    }
    if (status != ASTRA_SYSCALL_OK) return status;
    if (count != 0u) (void)astra_close(handles[0]);
    raw->stage = "bind-status";
    if (reply.reply.status != ASTRA_VFS_OK) return reply.reply.status;
    raw->stage = "ready";
    return ASTRA_SYSCALL_OK;
}

/* One request for `session` on `service`, answered within two seconds or
 * refused: ASTRA_SYSCALL_TIMED_OUT is the service declining to answer. */
static uint32_t raw_call(RawClient *raw, uint32_t service, uint32_t session,
                         uint32_t operation, uint32_t flags, const char *path,
                         AstraVfsReplyMessage *reply, uint32_t *received)
{
    AstraVfsRequestMessage message;
    uint32_t handles[1] = {0u};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    if (received != NULL)
        *received = 0u;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             operation, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    message.request.session = session;
    message.request.flags = flags;
    message.request.offset = ASTRA_VFS_MODE_DEFAULT;
    if (operation == ASTRA_VFS_OP_GRANT_DERIVE || path[0] != '\0') {
        size_t bytes = strlen(path) + 1u;
        if (bytes > ASTRA_VFS_PATH_MAX || bytes > raw->capacity) return ASTRA_VFS_ERR_NAME_TOO_LONG;
        memcpy(raw->address, path, bytes);
        message.request.body.paths.first_length = (uint32_t)bytes;
    }
    status = astra_port_send(service, &message, sizeof(message), NULL, 0u);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_wait_one(raw->reply_receive,
                            astra_clock_monotonic() + UINT64_C(2000000000),
                            NULL);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_port_receive(raw->reply_receive, reply, sizeof(*reply),
                                handles, 1u, &size, &count);
    if (received != NULL)
        *received = count != 0u ? handles[0] : 0u;
    else if (count != 0u)
        (void)astra_close(handles[0]);
    return status;
}

static void raw_discard(RawClient *raw)
{
    if (raw->owns_reply && raw->reply_receive != 0u)
        (void)astra_close(raw->reply_receive);
    if (raw->address != NULL) (void)astra_rt_area_unmap(raw->address);
    if (raw->area != 0u) (void)astra_close(raw->area);
    memset(raw, 0, sizeof(*raw));
}

/* A retired connection needs only raw_discard(); a live one first sends BYE. */
static void raw_close(RawClient *raw)
{
    AstraVfsReplyMessage reply;

    if (raw->session != ASTRA_VFS_SESSION_INVALID)
        (void)raw_call(raw, raw->service, raw->session, ASTRA_VFS_OP_BYE, 0u,
                       "", &reply, NULL);
    raw_discard(raw);
}

static int raw_open_lost(const RawClient *raw, uint32_t status)
{
    return raw->session == 0u &&
        ((status == ASTRA_STATUS_LOST && strcmp(raw->stage, "hello-status") == 0) ||
         (status == ASTRA_SYSCALL_PEER_DEAD &&
          (strcmp(raw->stage, "hello-send") == 0 ||
           strcmp(raw->stage, "hello-wait") == 0 ||
           strcmp(raw->stage, "hello-receive") == 0)));
}

/* The grant-relative path of `leaf` in a /work case directory. */
static void wire_path(char *out, size_t capacity, const char *directory,
                      const char *leaf)
{
    (void)snprintf(out, capacity, "%s%s%s", directory + strlen("/work"),
                   leaf[0] != '\0' ? "/" : "", leaf);
}

static uint32_t work_grant(void)
{
    const AstraStartupCapability *work =
        astra_startup_capability(astra_posix_startup(), "WORK");

    return work != NULL ? work->handle : 0u;
}

/* A read-only grant, derived and used raw on its own lane: the provider
 * refuses every mutation, and the grant cannot be widened again. */
static int case_raw_readonly(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    char wire[160];
    char posix[160];
    struct stat st;

    CHECK(work_grant() != 0u, "no WORK grant");
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK &&
          derived != 0u, "derive read-only: status %u",
          (unsigned)reply.reply.status);
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK,
          "HELLO on derived grant failed");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "widened a read-only grant: status %u", (unsigned)reply.reply.status);
    wire_path(wire, sizeof(wire), directory, "ro-made");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_MKDIR, 0u,
        wire, &reply, NULL) == ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_ERR_READ_ONLY,
        "read-only native MKDIR status %u", reply.reply.status);
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_OPEN,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, wire, &reply, NULL) == ASTRA_SYSCALL_OK &&
        reply.reply.status == ASTRA_VFS_ERR_READ_ONLY, "read-only native OPEN status %u", reply.reply.status);
    join(posix, sizeof(posix), directory, "ro-made");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT, "read-only MKDIR made it");
    join(posix, sizeof(posix), directory, "readable");
    CHECK(write_text(posix, "ok", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed readable");
    wire_path(wire, sizeof(wire), directory, "readable");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_STAT, 0u, wire,
        &reply, NULL) == ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK,
        "read-only native STAT failed");
    raw_close(&ro);
    raw_close(&rw);
    (void)astra_close(derived);
    return 0;
}

/* A session is refused on any port but its own grant's, and a lane cannot
 * name a session its scope does not bind. */
static int case_raw_cross_grant(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t derived_again = 0u;
    char wire[160];
    char posix[160];
    struct stat st;

    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && derived != 0u, "derive read-only");
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK, "HELLO on derived");
    /* The writable session's id, on the read-only grant's port. A reply
     * would go to the lane that session bound -- rw's -- so nothing may
     * arrive there either, and the session must still be rw's. */
    CHECK(raw_call(&ro, ro.service, rw.session, ASTRA_VFS_OP_BYE, 0u, "",
                   &reply, NULL) == ASTRA_SYSCALL_TIMED_OUT &&
              astra_wait_one(rw.reply_receive, 0u, NULL) !=
                  ASTRA_SYSCALL_OK,
          "a foreign session was answered");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived_again) ==
              ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK,
          "the session lost its own grant: status %u",
          (unsigned)reply.reply.status);
    (void)astra_close(derived_again);
    wire_path(wire, sizeof(wire), directory, "borrowed");
    CHECK(raw_call(&ro, ro.service, rw.session, ASTRA_VFS_OP_MKDIR, 0u, wire,
        &reply, NULL) == ASTRA_SYSCALL_TIMED_OUT &&
        astra_wait_one(rw.reply_receive, 0u, NULL) != ASTRA_SYSCALL_OK,
        "foreign session performed native data operation");
    join(posix, sizeof(posix), directory, "borrowed");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT,
          "a refused request made the directory");
    raw_close(&ro);
    raw_close(&rw);
    (void)astra_close(derived);
    return 0;
}

/* A grant derived beneath a sub-directory sees that directory as its root
 * and cannot name its parent's tree or another volume. */
static int case_raw_subroot(const char *directory)
{
    RawClient rw;
    RawClient sub;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    char wire[160];
    char posix[160];
    AstraVfsNodeMeta parent_meta;

    join(posix, sizeof(posix), directory, "sub");
    CHECK(mkdir(posix, 0755) == 0, "mkdir sub");
    join(posix, sizeof(posix), directory, "sub/inside");
    CHECK(write_text(posix, "in", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    join(posix, sizeof(posix), directory, "outside");
    CHECK(write_text(posix, "out", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed");
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    /* A derived root is relative to the parent's: drop the leading '/'. */
    wire_path(wire, sizeof(wire), directory, "sub");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, wire + 1, &reply, &derived) ==
              ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK &&
          derived != 0u, "derive sub: status %u",
          (unsigned)reply.reply.status);
    CHECK(raw_open(&sub, derived) == ASTRA_SYSCALL_OK, "HELLO on sub");
    wire_path(wire, sizeof(wire), directory, "sub/inside");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_STAT, 0u, wire,
        &reply, NULL) == ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK,
        "identity through WORK");
    parent_meta = reply.reply.meta;
    CHECK(raw_call(&sub, sub.service, sub.session, ASTRA_VFS_OP_STAT, 0u, "/inside",
        &reply, NULL) == ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK,
        "sub sees inside");
    CHECK(astra_vfs_node_identity_equal(&parent_meta, &reply.reply.meta),
        "derived root changed native node identity");
    CHECK(raw_call(&sub, sub.service, sub.session, ASTRA_VFS_OP_STAT, 0u, "/../outside",
        &reply, NULL) == ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_ERR_ESCAPED,
        "sub reached parent through ..");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "../sound", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant outside the volume");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "/etc", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant at an absolute root");
    raw_close(&sub);
    raw_close(&rw);
    (void)astra_close(derived);
    return 0;
}

/* Delegation never widens: DELEGATE cannot be derived from a grant without it.
 * WORK as fssem holds it may or may not delegate (that is its launcher's
 * choice); the case checks both kinds explicitly. */
static int case_raw_delegate(const char *directory)
{
    RawClient rw;
    RawClient plain;
    RawClient onward;
    AstraVfsReplyMessage reply;
    uint32_t derived = 0u;
    uint32_t again = 0u;
    int work_delegates;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   &derived) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && derived != 0u,
          "derive without DELEGATE");
    CHECK(raw_open(&plain, derived) == ASTRA_SYSCALL_OK,
          "HELLO on the non-delegable grant");
    CHECK(raw_call(&plain, plain.service, plain.session,
                   ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "DELEGATE derived from a grant without it");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, &again) == ASTRA_SYSCALL_OK &&
          (reply.reply.status == ASTRA_VFS_OK || reply.reply.status == ASTRA_VFS_ERR_ACCESS),
          "derive DELEGATE from WORK: status %u", (unsigned)reply.reply.status);
    work_delegates = reply.reply.status == ASTRA_VFS_OK;
    if (work_delegates) {
        CHECK(raw_open(&onward, again) == ASTRA_SYSCALL_OK,
              "delegable native grant failed");
        raw_close(&onward);
        (void)astra_close(again);
    }
    raw_close(&plain);
    raw_close(&rw);
    (void)astra_close(derived);
    return 0;
}

/* Sends `handle` to this process through a fresh port: what handing it to
 * another holder needs from the kernel. Answers the send's status and, when
 * it moved, the handle that arrived. */
static uint32_t carry(uint32_t handle, uint32_t *arrived)
{
    AstraMessageHeader message;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t handles[1] = {handle};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    *arrived = 0u;
    status = astra_rt_port_create(1u, sizeof(message), &receive, &send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, 1u, 1u);
    status = astra_port_send(send, &message, sizeof(message), handles, 1u);
    if (status == ASTRA_SYSCALL_OK &&
        astra_port_receive(receive, &message, sizeof(message), handles, 1u,
                           &size, &count) == ASTRA_SYSCALL_OK &&
        count == 1u)
        *arrived = handles[0];
    (void)astra_close(send);
    (void)astra_close(receive);
    return status;
}

/* A port derived from a non-delegable grant arrives without TRANSFER
 * (review C1; the kernel's send dispositions, syscall ABI 0x00010042): the
 * kernel refuses to move it on, and a fork child keeps it as usual. A port
 * derived from a delegable grant still travels and works. */
static int case_raw_onward(const char *directory)
{
    RawClient rw;
    RawClient plain;
    RawClient mine;
    RawClient moved;
    AstraVfsReplyMessage reply;
    uint32_t free_grant = 0u;
    uint32_t bound_grant = 0u;
    uint32_t arrived = 0u;
    uint32_t copy = 0u;
    uint32_t status;
    pid_t child;
    int exited;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   &free_grant) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && free_grant != 0u,
          "derive from WORK");
    CHECK(raw_open(&plain, free_grant) == ASTRA_SYSCALL_OK,
          "HELLO on the non-delegable grant");
    CHECK(raw_call(&plain, plain.service, plain.session,
                   ASTRA_VFS_OP_GRANT_DERIVE, ASTRA_VFS_GRANT_READ, "",
                   &reply, &bound_grant) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && bound_grant != 0u,
          "derive from the non-delegable grant: status %u",
          (unsigned)reply.reply.status);
    /* The kernel itself refuses to move it on. */
    status = carry(bound_grant, &arrived);
    CHECK(status == ASTRA_SYSCALL_ACCESS_DENIED && arrived == 0u,
          "a port derived from a non-delegable grant was moved on: %u",
          (unsigned)status);
    CHECK(astra_rt_handle_duplicate(bound_grant, ASTRA_RIGHT_SIGNAL |
                                        ASTRA_RIGHT_TRANSFER, &copy) ==
              ASTRA_SYSCALL_ACCESS_DENIED,
          "the derived port kept TRANSFER");
    /* Its holder uses it, and a fork child keeps it as usual. */
    CHECK(raw_open(&mine, bound_grant) == ASTRA_SYSCALL_OK,
          "the holder could not HELLO its derived grant");
    raw_close(&mine);
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        RawClient other;

        _exit(raw_open(&other, bound_grant) == ASTRA_SYSCALL_OK ? 0 : 1);
    }
    CHECK(waitpid(child, &exited, 0) == child && WIFEXITED(exited) &&
          WEXITSTATUS(exited) == 0,
          "a fork child could not HELLO the inherited grant");
    raw_close(&plain);
    (void)astra_close(bound_grant);
    /* A delegable parent's derived port moves and works where it lands. */
    status = carry(free_grant, &arrived);
    CHECK(status == ASTRA_SYSCALL_OK && arrived != 0u,
          "a port derived from a delegable grant did not move: %u",
          (unsigned)status);
    CHECK(raw_open(&moved, arrived) == ASTRA_SYSCALL_OK,
          "the moved port did not answer");
    raw_close(&moved);
    raw_close(&rw);
    (void)astra_close(arrived);
    return 0;
}

/* A derived grant belongs to the session that derived it (review I1): when
 * that session ends, every grant it derived is retired, held or not, and
 * HostFS's table does not keep them. */
static int case_raw_orphans(const char *directory)
{
    enum { DERIVED = 8 };
    RawClient rw;
    RawClient held;
    AstraVfsReplyMessage reply;
    uint32_t derived[DERIVED];
    uint32_t answer = ASTRA_SYSCALL_OK;
    int index;

    (void)directory;
    memset(derived, 0, sizeof(derived));
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    for (index = 0; index < DERIVED; ++index)
        CHECK(raw_call(&rw, rw.service, rw.session,
                       ASTRA_VFS_OP_GRANT_DERIVE, ASTRA_VFS_GRANT_READ, "",
                       &reply, &derived[index]) == ASTRA_SYSCALL_OK &&
                  reply.reply.status == ASTRA_VFS_OK && derived[index] != 0u,
              "derive %d: status %u", index, (unsigned)reply.reply.status);
    CHECK(raw_open(&held, derived[0]) == ASTRA_SYSCALL_OK,
          "HELLO on a derived grant before its deriver left");
    raw_close(&rw);
    /* The deriver's BYE closes every derived reply lane and admission port. */
    for (int tries = 0; tries < 40 && answer == ASTRA_SYSCALL_OK; ++tries) {
        struct timespec pause = {0, 50000000L};
        answer = astra_wait_one(held.reply_receive, 0u, NULL);
        if (answer == ASTRA_SYSCALL_OK) (void)nanosleep(&pause, NULL);
    }
    CHECK(answer == ASTRA_SYSCALL_PEER_DEAD,
        "derived reply lane outlived deriver: %u", (unsigned)answer);
    for (index = 0; index < DERIVED; ++index) {
        RawClient late;
        uint32_t status = raw_open_until(
            &late, derived[index],
            astra_clock_monotonic() + UINT64_C(2000000000), 0u, 0u, 0);

        CHECK(raw_open_lost(&late, status),
              "derived grant %d status %u stage %s session %u", index,
              (unsigned)status, late.stage, (unsigned)late.session);
        raw_discard(&late);
        (void)astra_close(derived[index]);
    }
    uint32_t held_area = held.area, handles = 0u, mappings = 0u, children = 0u;
    void *held_address = held.address;
    raw_discard(&held);
    CHECK(held_area != 0u && held_address != NULL &&
        astra_rt_area_references(held_area, &handles, &mappings, &children) ==
            ASTRA_SYSCALL_INVALID_HANDLE &&
        astra_rt_area_unmap(held_address) == ASTRA_SYSCALL_INVALID_ARGUMENT,
        "orphan fixture kept transfer resources");
    return 0;
}

/* A deriver that dies without BYE takes its grants with it, and a grant
 * derived from one of those goes too (re-review M4). A forked child
 * derives G1 from WORK and G2 from G1, hands both ports to its parent, and
 * exits. The parent's next HELLO wakes HostFS, which reaps the dead
 * session first; neither port answers again. */
static int case_raw_orphans_death(const char *directory)
{
    AstraMessageHeader message;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t got[2] = {0u, 0u};
    uint32_t size = 0u;
    uint32_t count = 0u;
    pid_t child;
    int exited;

    (void)directory;
    CHECK(astra_rt_port_create(1u, sizeof(message), &receive, &send) ==
              ASTRA_SYSCALL_OK, "port");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0) {
        RawClient rw;
        RawClient first;
        AstraVfsReplyMessage reply;
        uint32_t handles[2] = {0u, 0u};

        if (raw_open(&rw, work_grant()) != ASTRA_SYSCALL_OK ||
            raw_call(&rw, rw.service, rw.session,
                     ASTRA_VFS_OP_GRANT_DERIVE,
                     ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                     &reply, &handles[0]) != ASTRA_SYSCALL_OK ||
            handles[0] == 0u ||
            raw_open(&first, handles[0]) != ASTRA_SYSCALL_OK ||
            raw_call(&first, first.service, first.session,
                     ASTRA_VFS_OP_GRANT_DERIVE,
                     ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                     &reply, &handles[1]) != ASTRA_SYSCALL_OK ||
            handles[1] == 0u)
            _exit(1);
        memset(&message, 0, sizeof(message));
        astra_message_header_set(&message, (uint32_t)sizeof(message),
                                 ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION, 1u,
                                 1u);
        /* No BYE: the process simply ends. */
        _exit(astra_port_send(send, &message, sizeof(message), handles,
                              2u) == ASTRA_SYSCALL_OK ? 0 : 2);
    }
    CHECK(waitpid(child, &exited, 0) == child && WIFEXITED(exited) &&
          WEXITSTATUS(exited) == 0, "the deriving child failed: %d",
          WEXITSTATUS(exited));
    CHECK(astra_port_receive(receive, &message, sizeof(message), got, 2u,
                             &size, &count) == ASTRA_SYSCALL_OK &&
          count == 2u, "the child's grants did not arrive");
    for (int index = 0; index < 2; ++index) {
        RawClient late;
        uint32_t status = raw_open_until(
            &late, got[index],
            astra_clock_monotonic() + UINT64_C(2000000000), 0u, 0u, 0);

        CHECK(raw_open_lost(&late, status),
              "%s status %u stage %s session %u",
              index == 0 ? "dead deriver's grant" : "dead deriver's grandchild grant",
              (unsigned)status, late.stage, (unsigned)late.session);
        raw_discard(&late);
        (void)astra_close(got[index]);
    }
    (void)astra_close(send);
    (void)astra_close(receive);
    return 0;
}

/* One channel held open across a grant's retirement: what "revocation
 * stops every bound channel at once" means for a client mid-flight. */
typedef struct HeldLane {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int stage;                 /* 1 first STAT done, 2 go on, 3 finished */
    uint32_t first;
    uint32_t last;
} HeldLane;

static void held_stage(HeldLane *held, int stage)
{
    (void)pthread_mutex_lock(&held->mutex);
    held->stage = stage;
    (void)pthread_cond_broadcast(&held->changed);
    (void)pthread_mutex_unlock(&held->mutex);
}

static void held_wait(HeldLane *held, int stage)
{
    (void)pthread_mutex_lock(&held->mutex);
    while (held->stage < stage)
        (void)pthread_cond_wait(&held->changed, &held->mutex);
    (void)pthread_mutex_unlock(&held->mutex);
}

/* Closing native authority retires sessions but preserves an already granted
 * read-only file area until its final holder releases it. */
static int case_raw_retire(const char *directory)
{
    RawClient rw, ro;
    AstraVfsReplyMessage reply;
    AstraVfsClient *client = calloc(1u, sizeof(*client));
    AstraVfsFile file;
    uint32_t derived = 0u, area = 0u;
    uint64_t size = 0u;
    char path[192];
    join(path, sizeof(path), directory, "retained");
    CHECK(write_text(path, "retained", O_WRONLY | O_CREAT | O_EXCL) == 0, "seed retained file");
    CHECK(client != NULL && raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
        ASTRA_VFS_GRANT_READ, directory + 6u, &reply, &derived) == ASTRA_SYSCALL_OK &&
        reply.reply.status == ASTRA_VFS_OK && derived != 0u, "derive retained root");
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK &&
        astra_vfs_client_connect_service(client, derived) == ASTRA_VFS_OK &&
        astra_vfs_open(client, "/retained", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) == ASTRA_VFS_OK,
        "open retained description");
    CHECK(astra_vfs_file_area(client, file, 0u, ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK &&
        size == 8u && astra_vfs_close(client, file) == ASTRA_VFS_OK, "retain read-only area");
    CHECK(astra_close(derived) == ASTRA_SYSCALL_OK, "close last grant handle");
    uint32_t status = ASTRA_SYSCALL_OK;
    for (unsigned i = 0u; i < 40u && status == ASTRA_SYSCALL_OK; ++i) {
        CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_STAT, 0u, "/", &reply, NULL) ==
            ASTRA_SYSCALL_OK && reply.reply.status == ASTRA_VFS_OK, "wake native retirement");
        status = astra_wait_one(ro.reply_receive, 0u, NULL);
        if (status == ASTRA_SYSCALL_OK) {
            struct timespec pause = {0, 50000000L}; (void)nanosleep(&pause, NULL);
        }
    }
    CHECK(status == ASTRA_SYSCALL_PEER_DEAD, "native reply lane outlived retired grant");
    char bytes[8];
    CHECK(astra_rt_area_prefetch(area, 0u) == ASTRA_SYSCALL_OK &&
        astra_rt_area_read_resident(area, 0u, bytes, sizeof(bytes)) == ASTRA_SYSCALL_OK &&
        memcmp(bytes, "retained", sizeof(bytes)) == 0, "grant retirement broke retained file area");
    CHECK(astra_close(area) == ASTRA_SYSCALL_OK, "close retained area");
    status = astra_vfs_disconnect(client);
    CHECK(status == ASTRA_VFS_ERR_BAD_HANDLE || status == ASTRA_VFS_ERR_LOST, "retired client cleanup %u", status);
    free(client); raw_discard(&ro); raw_close(&rw);
    CHECK(unlink(path) == 0, "remove retained fixture");
    return 0;
}

static void *many_lane_thread(void *argument)
{
    HeldLane *held = argument;
    struct stat metadata;

    held->first = stat("/work", &metadata) == 0 ? ASTRA_STATUS_OK : ASTRA_STATUS_IO;
    held_stage(held, 1);
    held_wait(held, 2);
    held->last = stat("/work", &metadata) == 0 ? ASTRA_STATUS_OK : ASTRA_STATUS_IO;
    return NULL;
}

/* Main already owns one lane. Keep every remaining thread slot live at once. */
static int case_many_lanes(const char *directory)
{
    HeldLane lanes[ASTRA_PROCESS_THREAD_SLOT_COUNT - 1u];
    pthread_t threads[ASTRA_PROCESS_THREAD_SLOT_COUNT - 1u];
    unsigned started = 0u;
    int result = 0;

    (void)directory;
    memset(lanes, 0, sizeof(lanes));
    for (unsigned index = 0u; index < ASTRA_PROCESS_THREAD_SLOT_COUNT - 1u; ++index) {
        HeldLane *lane = &lanes[index];

        if (pthread_mutex_init(&lane->mutex, NULL) != 0 ||
            pthread_cond_init(&lane->changed, NULL) != 0 ||
            pthread_create(&threads[index], NULL, many_lane_thread, lane) != 0) {
            result = fail("create lane thread %u", index);
            break;
        }
        ++started;
    }
    for (unsigned index = 0u; index < started; ++index) {
        held_wait(&lanes[index], 1);
        if (lanes[index].first != ASTRA_STATUS_OK)
            result = fail("first use lane %u", index + 1u);
    }
    for (unsigned index = 0u; index < started; ++index)
        held_stage(&lanes[index], 2);
    for (unsigned index = 0u; index < started; ++index) {
        if (pthread_join(threads[index], NULL) != 0 ||
            lanes[index].last != ASTRA_STATUS_OK)
            result = fail("repeat use lane %u", index + 1u);
        (void)pthread_cond_destroy(&lanes[index].changed);
        (void)pthread_mutex_destroy(&lanes[index].mutex);
    }
    return result;
}

static int case_owner_sessions(const char *directory)
{
    RawClient *held = calloc(65u, sizeof(*held));
    uint32_t *pressure = calloc(ASTRA_HANDLE_COUNT_MAX, sizeof(*pressure));
    uint32_t pressure_count = 0u;
    uint32_t native_handles;
    uint32_t reply_receive = 0u, reply_send = 0u;
    uint32_t pressure_receive = 0u, pressure_send = 0u;
    uint32_t count = 0u;
    uint32_t kit_sessions = 1u;
    uint32_t status = ASTRA_SYSCALL_OK;
    int result = 0;
    pid_t parent = getpid();
    pid_t child;
    int child_status;

    (void)directory;
    if (held == NULL || pressure == NULL || parent <= 0) {
        result = fail("owner-sessions fixture allocation or self identity");
        goto done;
    }
    const AstraStartupCapability *cwd =
        astra_startup_capability(astra_posix_startup(), "CWD");
    if (cwd != NULL && cwd->handle != work_grant()) {
        AstraVfsClient *client = astra_process_vfs_client_handle_lookup(cwd->handle);
        uint64_t size = 0u;
        uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
        if (client == NULL || astra_vfs_stat(client, "/", &size, &kind) != ASTRA_VFS_OK ||
            kind != ASTRA_VFS_KIND_DIRECTORY) {
            result = fail("owner-sessions CWD client");
            goto done;
        }
        ++kit_sessions;
    }
    status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(AstraVfsReplyMessage),
                                  &reply_receive, &reply_send);
    if (status != ASTRA_SYSCALL_OK) {
        result = fail("native pressure reply endpoint: %u", status);
        goto done;
    }

    /* Native handle admission is a separate boundary from the VFS owner
     * quota. Duplicate an owned, transferable PORT_SEND, rather than a
     * filesystem authority grant. Self PROCESS_INFO must remain usable with
     * every handle occupied. */
    while (pressure_count < ASTRA_HANDLE_COUNT_MAX) {
        status = astra_rt_handle_duplicate(reply_send,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
            ASTRA_RIGHT_TRANSFER, &pressure[pressure_count]);
        if (status != ASTRA_SYSCALL_OK)
            break;
        ++pressure_count;
    }
    if (status != ASTRA_SYSCALL_RESOURCE_LIMIT || pressure_count == 0u)
        result = fail("native handle pressure accepted %u status %u", pressure_count, status);
    if (result == 0 && getpid() != parent)
        result = fail("self identity changed under native handle pressure (expected %ld)", (long)parent);
    status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(AstraVfsReplyMessage),
                                  &pressure_receive, &pressure_send);
    if (result == 0 && status != ASTRA_SYSCALL_RESOURCE_LIMIT)
        result = fail("native handle pressure admitted reply port: %u", status);
    if (pressure_receive != 0u) (void)astra_close(pressure_receive);
    if (pressure_send != 0u) (void)astra_close(pressure_send);
    native_handles = pressure_count;
    while (pressure_count != 0u)
        (void)astra_close(pressure[--pressure_count]);
    if (result != 0)
        goto done;

    /* Only HELLO/BYE are needed here. One serial reply endpoint and no
     * bound data areas let the test reach 64 VFS sessions within the 255
     * native handle slots, instead of first exhausting unrelated handles. */
    status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(AstraVfsReplyMessage),
                                  &pressure_receive, &pressure_send);
    if (status != ASTRA_SYSCALL_OK) {
        result = fail("native handles did not return after pressure: %u", status);
        goto done;
    }
    (void)astra_close(pressure_receive);
    (void)astra_close(pressure_send);
    printf("FSSEM owner-sessions native-pressure pid=%ld duplicated=%u identity-preserved handles-returned\n",
           (long)parent, native_handles);
    while (count < 65u) {
        status = raw_open_until(&held[count], work_grant(), ASTRA_DEADLINE_FOREVER,
                                reply_receive, reply_send, 1);
        if (status != ASTRA_SYSCALL_OK)
            break;
        ++count;
    }
    if (status != ASTRA_VFS_ERR_LIMIT || count + kit_sessions != 64u) {
        result = fail("owner=%ld sessions accepted %u stage=%s status=%u (%u Kit sessions held)",
                      (long)parent, count, count < 65u ? held[count].stage : "over-quota", status,
                      kit_sessions);
    }
    if (count < 65u)
        raw_close(&held[count]);
    if (result != 0)
        goto done;
    if (getpid() != parent) {
        result = fail("self identity changed under VFS owner quota (expected %ld)", (long)parent);
        goto done;
    }
    child = fork();
    if (child == 0) {
        RawClient other;
        uint32_t opened = raw_open(&other, work_grant());

        if (opened != ASTRA_SYSCALL_OK) {
            printf("FSSEM owner-sessions child pid=%ld raw-stage=%s status=%u session=%u\n",
                   (long)getpid(), other.stage, opened, other.session);
            fflush(stdout);
        }
        raw_close(&other);
        _exit(opened == ASTRA_SYSCALL_OK ? 0 : 1);
    }
    if (child < 0) {
        result = fail("owner-sessions parent pid=%ld fork errno=%d with %u raw sessions held",
                      (long)parent, errno, count);
    } else {
        pid_t waited = waitpid(child, &child_status, 0);

        if (waited != child)
            result = fail("owner-sessions wait child=%ld result=%ld errno=%d",
                          (long)child, (long)waited, errno);
        else if (!WIFEXITED(child_status))
            result = fail("owner-sessions child=%ld wait-status=0x%x signal=%d",
                          (long)child, child_status,
                          WIFSIGNALED(child_status) ? WTERMSIG(child_status) : 0);
        else if (WEXITSTATUS(child_status) == 127)
            result = fail("owner-sessions child=%ld failed I/O reinitialization (127)",
                          (long)child);
        else if (WEXITSTATUS(child_status) != 0)
            result = fail("one owner's sessions blocked child=%ld exit=%d",
                          (long)child, WEXITSTATUS(child_status));
    }
    if (count != 0u) {
        raw_close(&held[--count]);
        status = raw_open_until(&held[count], work_grant(), ASTRA_DEADLINE_FOREVER,
                                reply_receive, reply_send, 1);
        if (status != ASTRA_SYSCALL_OK && result == 0)
            result = fail("closed session did not release quota: %u", status);
        if (status == ASTRA_SYSCALL_OK)
            ++count;
        if (status == ASTRA_SYSCALL_OK) {
            status = raw_open_until(&held[count], work_grant(), ASTRA_DEADLINE_FOREVER,
                                    reply_receive, reply_send, 1);
            if (status != ASTRA_VFS_ERR_LIMIT && result == 0)
                result = fail("released session widened owner quota: %u", status);
            raw_close(&held[count]);
        } else {
            raw_close(&held[count]);
        }
    }
done:
    while (count != 0u)
        raw_close(&held[--count]);
    if (reply_send != 0u) (void)astra_close(reply_send);
    if (reply_receive != 0u) (void)astra_close(reply_receive);
    free(pressure);
    free(held);
    return result;
}

static int wait_mapped_byte(const volatile char *mapping, size_t offset,
                            char expected)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(4000000000);
    while (mapping[offset] != expected && astra_clock_monotonic() < deadline)
        (void)astra_rt_thread_sleep(UINT64_C(10000000),
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    return mapping[offset] == expected;
}

enum FileMapRead { FILE_MAP_POSITIONED, FILE_MAP_BORROWED, FILE_MAP_PATH,
                   FILE_MAP_SEQUENTIAL };

/* Keep the original owner pinned through asynchronous invalidation. Every
 * read must return fresh bytes; its canonical page must settle by the deadline. */
static int file_map_cached_page(uint32_t area,
    uint32_t offset, int fd, char expected, enum FileMapRead kind,
    AstraProcessFilesystem *native, const char *path)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(4000000000);
    for (;;) {
        char observed = 0;
        if (kind == FILE_MAP_POSITIONED) {
            if (pread(fd, &observed, 1u, offset) != 1 || observed != expected) return 0;
        } else if (kind == FILE_MAP_BORROWED) {
            AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
            const uint8_t *bytes = NULL;
            uint32_t moved = 0u;
            uint32_t status = astra_process_vfs_source_open(path, &source);
            if (status == ASTRA_VFS_OK)
                status = astra_vfs_read_source_read_at(&source, offset, 1u,
                    &bytes, &moved);
            int valid = status == ASTRA_VFS_OK && moved == 1u &&
                bytes != NULL && bytes[0] == (uint8_t)expected;
            if (source.file != ASTRA_VFS_FILE_INVALID &&
                astra_vfs_read_source_close(&source) != ASTRA_VFS_OK) valid = 0;
            if (!valid) return 0;
        } else if (kind == FILE_MAP_PATH) {
            char whole[2u * ASTRA_MEMORY_PAGE_SIZE + 1u];
            uint32_t moved = 0u;
            if (astra_process_read_file(native, path, whole, sizeof(whole),
                    &moved) != ASTRA_VFS_OK || moved != sizeof(whole) ||
                whole[0] != 'b' || whole[1] != 'c' || whole[offset] != expected)
                return 0;
        } else {
            if (lseek(fd, offset, SEEK_SET) != (off_t)offset ||
                read(fd, &observed, 1u) != 1 || observed != expected ||
                lseek(fd, 0, SEEK_CUR) != (off_t)offset + 1) return 0;
        }
        uint32_t status = astra_rt_area_read_resident(area, offset, &observed, 1u);
        if (status == ASTRA_SYSCALL_OK) return observed == expected;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK ||
            astra_clock_monotonic() >= deadline) {
            fprintf(stderr, "FSSEM canonical resident status %u area %08lx offset %lu\n",
                (unsigned)status, (unsigned long)area, (unsigned long)offset);
            return 0;
        }
    }
}

static int scoped_core_lock(void *context)
{ return pthread_mutex_lock(context) == 0; }
static void scoped_core_unlock(void *context)
{ (void)pthread_mutex_unlock(context); }

static uint32_t prefix_read(void *client, uintptr_t node, uint64_t offset,
    void *bytes, uint32_t length, uint32_t *moved)
{
    uint32_t status = probe_read(client, node, offset, bytes, length < 3u ? length : 3u, moved);
    return status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_IO : status;
}

static uint32_t prefix_read_sequential(void *client, uintptr_t node,
    void *bytes, uint32_t length, uint32_t *moved, uint64_t *position)
{
    uint32_t status = probe_read_sequential(client, node, bytes,
        length < 3u ? length : 3u, moved, position);
    return status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_IO : status;
}

static uint32_t prefix_write(void *client, uintptr_t node, uint64_t offset,
    uint32_t flags, uint64_t limit, const void *bytes, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    uint32_t status = probe_write(client, node, offset, flags, limit, bytes,
        length < 3u ? length : 3u, moved, position);
    return status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_IO : status;
}

typedef struct PrefixServiceProbe {
    HeldLane ready;
    AstraVfsPortService *host;
    AstraVfsClient *backing;
    uint32_t grant, stop;
} PrefixServiceProbe;

static void *prefix_service_run(void *argument)
{
    PrefixServiceProbe *probe = argument;
    /* Backing descriptions and their reply lane stay on this worker. */
    probe->ready.first = astra_vfs_client_connect_service(probe->backing, probe->grant);
    held_stage(&probe->ready, 1);
    while (probe->ready.first == ASTRA_VFS_OK &&
           !__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE)) {
        uint32_t status = astra_wait_one(probe->host->receive,
            astra_clock_monotonic() + UINT64_C(1000000000), NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT || status == ASTRA_SYSCALL_CANCELLED) continue;
        if (status != ASTRA_SYSCALL_OK) {
            (void)fail("byte-prefix service wait %u", status);
            _exit(2);
        }
        (void)astra_vfs_port_service_pump(probe->host, 8u);
        if (probe->host->stalled != 0u) {
            (void)fail("byte-prefix service pump %u", probe->host->stalled);
            _exit(2);
        }
    }
    astra_vfs_port_service_reap(probe->host);
    if (probe->ready.first == ASTRA_VFS_OK)
        probe->ready.last = astra_vfs_disconnect(probe->backing);
    return NULL;
}

static int case_byte_prefix(const char *directory)
{
    AstraVfsClient *clients = calloc(2u, sizeof(*clients));
    AstraVfsPortService *host = calloc(1u, sizeof(*host));
    AstraVfsService core = {0};
    AstraVfsSessionSlot sessions[1] = {0};
    AstraVfsOpenFile files[1] = {0};
    AstraVfsBackendOps backend = probe_backend_ops;
    AstraProcessFilesystem io = ASTRA_PROCESS_FILESYSTEM_INIT;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    PrefixServiceProbe probe = {.ready = {.mutex = PTHREAD_MUTEX_INITIALIZER,
        .changed = PTHREAD_COND_INITIALIZER}};
    pthread_t thread;
    uint32_t receive = 0u, send = 0u, started = 0u;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    char path[192];
    const char *stage = "rooted service setup";
    int result = 2;
    join(path, sizeof(path), directory, "byte-prefix");
    AstraVfsClient *parent = astra_process_vfs_client_handle_lookup(work_grant());
    if (clients == NULL || host == NULL || parent == NULL ||
        astra_process_filesystem_open(&io, astra_posix_startup()) != ASTRA_VFS_OK ||
        io.filesystem._private_read == NULL || io.filesystem._private_write == NULL ||
        astra_vfs_grant_derive(parent, directory + 6u,
            ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, &probe.grant) != ASTRA_VFS_OK)
        goto done;
    backend.read = prefix_read; backend.read_sequential = prefix_read_sequential;
    backend.write = prefix_write;
    probe.host = host; probe.backing = &clients[0];
    if (!astra_vfs_service_init(&core, &backend, &clients[0], sessions, 1u, files, 1u) ||
        !astra_vfs_service_set_state_lock(&core, scoped_core_lock, scoped_core_unlock, &mutex) ||
        astra_rt_port_create(4u, 4u * sizeof(AstraVfsRequestMessage), &receive, &send) != ASTRA_SYSCALL_OK ||
        !astra_vfs_port_service_init(host, receive, &core) ||
        !astra_vfs_port_service_set_state_lock(host, scoped_core_lock, scoped_core_unlock, &mutex) ||
        pthread_create(&thread, NULL, prefix_service_run, &probe) != 0) goto done;
    started = 1u;
    held_wait(&probe.ready, 1);
    if (probe.ready.first != ASTRA_VFS_OK ||
        astra_vfs_client_connect_service(&clients[1], send) != ASTRA_VFS_OK) goto done;
    for (uint32_t write = 0u; write < 2u; ++write)
        for (uint32_t bulk = 0u; bulk < 2u; ++bulk) {
            stage = bulk ? "shared-area error prefix" : "inline error prefix";
            uint64_t position;
            if (write_text(path, "abcdef", O_CREAT | O_TRUNC | O_WRONLY) != 0 ||
                astra_vfs_open(&clients[1], "/byte-prefix", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
                    &file, NULL, NULL) != ASTRA_VFS_OK ||
                astra_vfs_seek(&clients[1], file, 2, 0u, &position) != ASTRA_VFS_OK)
                goto done;
            for (uint32_t call = 0u; call < 2u; ++call) {
                uint8_t bytes[5];
                uint32_t moved = 0u, status;
                memset(bytes, 0xa5, sizeof(bytes));
                if (write) memcpy(bytes, call == 0u ? "XYZ12" : "12345", sizeof(bytes));
                /* Borrow installed Kit adapters; keep one client implementation. */
                status = write ?
                    (bulk ? io.filesystem._private_write : astra_vfs_write_position)(
                        &clients[1], file, 0u, ASTRA_VFS_IO_SEQUENTIAL, UINT64_MAX,
                        bytes, sizeof(bytes), &moved, &position) :
                    (bulk ? io.filesystem._private_read : astra_vfs_read_position)(
                        &clients[1], file, 0u, ASTRA_VFS_IO_SEQUENTIAL,
                        bytes, sizeof(bytes), &moved, &position);
                uint32_t expected = write || call == 0u ? 3u : 1u;
                uint64_t cursor = write ? 5u + call * 3u : 5u + call;
                if (status != ASTRA_VFS_ERR_IO || moved != expected ||
                    position != cursor) {
                    printf("FSSEM PREFIX write=%u bulk=%u call=%u status=%u moved=%u expected=%u position=%llu cursor=%llu\n",
                        write, bulk, call, status, moved, expected,
                        (unsigned long long)position, (unsigned long long)cursor);
                    goto done;
                }
                if (astra_vfs_seek(&clients[1], file, 0, 1u, &position) != ASTRA_VFS_OK ||
                    position != cursor) goto done;
                if (!write) {
                    if (memcmp(bytes, call == 0u ? "cde" : "f", moved) != 0) goto done;
                    for (uint32_t at = moved; at < sizeof(bytes); ++at)
                        if (bytes[at] != 0xa5u) goto done;
                } else {
                    char actual[8];
                    int fd = open(path, O_RDONLY);
                    if (fd < 0) goto done;
                    ssize_t count = read(fd, actual, sizeof(actual));
                    int closed = close(fd);
                    uint32_t size = call == 0u ? 6u : 8u;
                    if (count != (ssize_t)size || closed != 0 ||
                        memcmp(actual, call == 0u ? "abXYZf" : "abXYZ123", size) != 0) goto done;
                }
            }
            if (astra_vfs_close(&clients[1], file) != ASTRA_VFS_OK) goto done;
            file = ASTRA_VFS_FILE_INVALID;
        }
    result = 0;
done:
    if (clients != NULL && clients[1].transport != NULL) {
        if (file != ASTRA_VFS_FILE_INVALID) (void)astra_vfs_close(&clients[1], file);
        if (astra_vfs_disconnect(&clients[1]) != ASTRA_VFS_OK) result = 2;
    }
    if (started) {
        __atomic_store_n(&probe.stop, 1u, __ATOMIC_RELEASE);
        if (pthread_join(thread, NULL) != 0) {
            (void)fail("byte-prefix worker join");
            _exit(2);
        }
        if (host->stalled != 0u ||
            probe.ready.last != ASTRA_VFS_OK || core.open_files != 0u || core.open_sessions != 0u)
            result = 2;
    }
    if (send != 0u) (void)astra_close(send);
    if (receive != 0u) (void)astra_close(receive);
    if (probe.grant != 0u) (void)astra_close(probe.grant);
    astra_process_filesystem_close(&io);
    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&probe.ready.changed);
    (void)pthread_mutex_destroy(&probe.ready.mutex);
    free(host); free(clients); (void)unlink(path);
    if (result != 0) return fail("byte-prefix %s", stage);
    printf("FSSEM BYTE PREFIX inline shared-area read write cursor backing PASS\n");
    return 0;
}

typedef struct PageWritebackProbe {
    AstraVfsFilePageSource source;
    AstraVfsFilePager pager;
    AstraVfsFileCache cache_storage;
    uint32_t stop;
    uint32_t cache_area, cache_writes, cache_reads;
    AstraVfsFile cache_writer;
    uint32_t sync_calls, sync_fail_once, sync_cursor;
    uint8_t *sync_page;
} PageWritebackProbe;

static uint32_t writeback_cache_write(void *context, const AstraVfsOpenFile *file,
    AstraVfsFile *owned, uint64_t offset, uint32_t flags, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved, uint64_t *position)
{
    PageWritebackProbe *probe = context;
    if (!astra_vfs_open_file_active(file) || flags != 0u ||
        offset > ASTRA_MEMORY_PAGE_SIZE || length > ASTRA_MEMORY_PAGE_SIZE - offset ||
        offset > max_end || length > max_end - offset)
        return ASTRA_VFS_ERR_INVALID;
    AstraVfsFileCache cache;
    uint64_t modified_ns = 0u;
    if (astra_clock_realtime(&modified_ns) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_IO;
    uint32_t status = astra_vfs_service_cache_prepare(probe->source.service,
        file, owned, 0u, probe->cache_storage.writer == ASTRA_VFS_FILE_INVALID ?
            &probe->cache_storage : NULL, &cache);
    if (status != ASTRA_VFS_OK) return status;
    probe->cache_writer = cache.writer;
    status = astra_rt_area_write_pager(probe->cache_area,
        (uint32_t)offset, buffer, length);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_IO;
    uint64_t size = offset + length;
    if (size < cache.size) size = cache.size;
    status = astra_vfs_service_cache_publish(probe->source.service, file, size, modified_ns);
    if (status != ASTRA_VFS_OK) return status;
    ++probe->cache_writes;
    *moved = length; *position = offset + length;
    return ASTRA_VFS_OK;
}

static uint32_t writeback_cache_sync(void *context, const AstraVfsOpenFile *file,
    uint32_t *remaining)
{
    PageWritebackProbe *probe = context;
    if (probe->sync_page == NULL) return ASTRA_VFS_ERR_INVALID;
    ++probe->sync_calls;
    if (probe->sync_fail_once) {
        uint32_t token = 0u;
        probe->sync_fail_once = 0u;
        if (astra_rt_area_writeback_begin(probe->cache_area, 0u, probe->sync_page,
                &token) != ASTRA_SYSCALL_OK) return ASTRA_VFS_ERR_IO;
        (void)astra_rt_area_writeback_complete(probe->cache_area, 0u, token, false);
        return ASTRA_VFS_ERR_IO;
    }
    uint32_t complete = UINT32_MAX;
    if (remaining == NULL) { remaining = &complete; probe->sync_cursor = 0u; }
    return astra_vfs_file_pager_writeback(&probe->source, file,
        probe->cache_writer, probe->cache_area, probe->sync_page,
        remaining, &probe->sync_cursor);
}

static uint32_t writeback_cache_read(void *context, const AstraVfsOpenFile *file,
    uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    PageWritebackProbe *probe = context;
    if (file->io->cache == NULL)
        return astra_vfs_backend_read_position(&file->backend, file->node,
            offset, flags, buffer, length, moved, position);
    ++probe->cache_reads;
    AstraVfsRequest request = {.flags = 1u};
    AstraVfsReply reply = {0};
    uint32_t status = ASTRA_VFS_OK;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u) {
        status = astra_vfs_backend_description_operation(&file->backend,
            file->node, ASTRA_VFS_OP_SEEK, &request, &reply, NULL);
        if (status != ASTRA_VFS_OK) return status;
        offset = reply.position;
    }
    uint64_t size = file->io->cache->size;
    uint32_t wanted = offset >= size ? 0u :
        size - offset < length ? (uint32_t)(size - offset) : length;
    while (*moved < wanted) {
        uint32_t at = (uint32_t)(offset + *moved);
        uint32_t chunk = ASTRA_MEMORY_PAGE_SIZE - at % ASTRA_MEMORY_PAGE_SIZE;
        if (chunk > wanted - *moved) chunk = wanted - *moved;
        if (astra_rt_area_read_resident(probe->cache_area, at,
                (uint8_t *)buffer + *moved, chunk) != ASTRA_SYSCALL_OK) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        *moved += chunk;
    }
    *position = offset + *moved;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u && *moved != 0u) {
        request = (AstraVfsRequest){.offset = offset, .length = *moved};
        uint32_t advanced = astra_vfs_backend_description_operation(&file->backend,
            file->node, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply, NULL);
        if (advanced != ASTRA_VFS_OK || reply.count != 1u) {
            *moved = 0u;
            return advanced == ASTRA_VFS_OK ? ASTRA_VFS_ERR_BUSY : advanced;
        }
    }
    return status;
}

static const AstraVfsCacheOps writeback_cache_ops = {
    .write = writeback_cache_write, .sync = writeback_cache_sync,
    .read = writeback_cache_read
};

static const AstraVfsFilePageSource *writeback_source_acquire(void *context,
    uint32_t cookie)
{
    PageWritebackProbe *probe = context;
    if (cookie != probe->source.cookie) return NULL;
    return &probe->source;
}

static void writeback_source_release(void *context,
    const AstraVfsFilePageSource *source)
{
    (void)context; (void)source;
}

static void writeback_pager_run(uint32_t argument)
{
    PageWritebackProbe *probe = (void *)(uintptr_t)argument;
    while (!__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE)) {
        uint32_t status = astra_wait_one(probe->pager.receive,
            astra_clock_monotonic() + UINT64_C(1000000000), NULL);
        if (__atomic_load_n(&probe->stop, __ATOMIC_ACQUIRE)) break;
        if (status == ASTRA_SYSCALL_TIMED_OUT || status == ASTRA_SYSCALL_CANCELLED)
            continue;
        if (status != ASTRA_SYSCALL_OK) { probe->pager.stalled = status; break; }
        (void)astra_vfs_file_pager_pump(&probe->pager, 1u);
        if (probe->pager.stalled != 0u) break;
    }
}

static int generation_exhaustion_gate(const char *directory)
{
    AstraVfsService core = {0};
    AstraVfsSessionSlot sessions[2] = {0};
    AstraVfsOpenFile files[2] = {0};
    AstraVfsClient *clients = calloc(3u, sizeof(*clients));
    AstraVfsClient *parent = astra_process_vfs_client_handle_lookup(work_grant());
    AstraVfsFile old, last, companion, replacement, overflow;
    AstraVfsFile retained = ASTRA_VFS_FILE_INVALID;
    uint32_t grant = 0u, ready = 0u, moved = 0u;
    AstraVfsDirEntry info = {0};
    char path[192], byte = 0;
    int fd = -1, result = 2;
    const char *stage = "create";

    join(path, sizeof(path), directory, "generation-exhaustion");
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0 || write(fd, "X", 1u) != 1 || fsync(fd) != 0) goto done;
    close(fd); fd = -1;
    stage = "rooted native client";
    if (clients == NULL || parent == NULL ||
        astra_vfs_grant_derive(parent, directory + 6u, ASTRA_VFS_GRANT_READ,
            &grant) != ASTRA_VFS_OK ||
        astra_vfs_client_connect_service(&clients[0], grant) != ASTRA_VFS_OK ||
        !astra_vfs_service_init(&core, &probe_backend_ops, &clients[0],
            sessions, 2u, files, 2u)) goto done;
    ready = 1u;
    stage = "file exhaustion";
    if (astra_vfs_connect(&clients[1], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[1], "/generation-exhaustion", ASTRA_VFS_OPEN_READ,
            &old, NULL, NULL) != ASTRA_VFS_OK ||
        astra_vfs_close(&clients[1], old) != ASTRA_VFS_OK) goto done;
    files[0].generation = UINT16_MAX;
    if (astra_vfs_open(&clients[1], "/generation-exhaustion", ASTRA_VFS_OPEN_READ,
            &last, NULL, NULL) != ASTRA_VFS_OK || (last >> 16) != UINT16_MAX ||
        astra_vfs_service_retain_file(&core, clients[1].session, last,
            ASTRA_VFS_OPEN_READ, &retained) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[1], "/generation-exhaustion", ASTRA_VFS_OPEN_READ,
            &companion, NULL, NULL) != ASTRA_VFS_OK || files[0].io != files[1].io ||
        astra_vfs_close(&clients[1], last) != ASTRA_VFS_OK ||
        astra_vfs_service_release_retained(&core, &retained) != ASTRA_VFS_OK ||
        astra_vfs_read(&clients[1], companion, 0u, &byte, 1u, &moved) != ASTRA_VFS_OK ||
        moved != 1u || byte != 'X' ||
        astra_vfs_close(&clients[1], companion) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[1], "/generation-exhaustion", ASTRA_VFS_OPEN_READ,
            &replacement, NULL, NULL) != ASTRA_VFS_OK ||
        astra_vfs_read(&clients[1], old, 0u, &byte, 1u, &moved) != ASTRA_VFS_ERR_BAD_HANDLE ||
        files[0].generation != 0u || files[0].session != 0u ||
        astra_vfs_read(&clients[1], replacement, 0u, &byte, 1u, &moved) != ASTRA_VFS_OK ||
        moved != 1u || byte != 'X' ||
        astra_vfs_open(&clients[1], "/generation-exhaustion", ASTRA_VFS_OPEN_READ,
            &overflow, NULL, NULL) != ASTRA_VFS_ERR_LIMIT) goto done;
    stage = "session exhaustion";
    core.next_session = UINT32_MAX;
    if (astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        clients[2].session != UINT32_MAX ||
        astra_vfs_disconnect(&clients[2]) != ASTRA_VFS_OK ||
        astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_ERR_LIMIT ||
        core.next_session != 0u || !astra_vfs_service_session_capacity_reached(&core) ||
        astra_vfs_stat_file_meta(&clients[1], replacement, &info) != ASTRA_VFS_OK ||
        info.size != 1u ||
        astra_vfs_close(&clients[1], replacement) != ASTRA_VFS_OK ||
        astra_vfs_disconnect(&clients[1]) != ASTRA_VFS_OK ||
        astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_ERR_LIMIT ||
        core.open_files != 0u || core.open_sessions != 0u ||
        files[0].io_storage.references != 0u) goto done;
    printf("FSSEM GENERATION EXHAUSTION stale-file retained-io live-session limit PASS\n");
    result = 0;
done:
    if (ready != 0u) {
        for (uint32_t at = 0u; at < core.session_capacity; ++at)
            if (sessions[at].id != 0u)
                astra_vfs_service_release_session(&core, sessions[at].id);
        if (retained != ASTRA_VFS_FILE_INVALID &&
            astra_vfs_service_release_retained(&core, &retained) != ASTRA_VFS_OK) result = 2;
        if (core.open_files != 0u || core.open_sessions != 0u) result = 2;
    }
    if (clients != NULL && clients[0].session != ASTRA_VFS_SESSION_INVALID &&
        astra_vfs_disconnect(&clients[0]) != ASTRA_VFS_OK) result = 2;
    free(clients);
    if (grant != 0u) (void)astra_close(grant);
    if (fd >= 0) close(fd);
    if (unlink(path) != 0) result = 2;
    if (result != 0) printf("FSSEM GENERATION EXHAUSTION stage=%s FAIL\n", stage);
    return result;
}

static int page_writeback_gate(const char *directory)
{
    AstraVfsService core = {0};
    AstraVfsSessionSlot sessions[1] = {0};
    AstraVfsOpenFile files[4] = {0};
    AstraVfsClient *clients = calloc(2u, sizeof(*clients));
    AstraVfsClient *parent = astra_process_vfs_client_handle_lookup(work_grant());
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t grant = 0u, ready = 0u;
    PageWritebackProbe probe = {0};
    AstraVfsFile timestamp_file = ASTRA_VFS_FILE_INVALID;
    AstraVfsNodeInfo expected = {0}, current = {0};
    AstraVfsBackend backend = {0};
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    char path[192];
    uint32_t area = 0u, scratch = 0u, receive = 0u, send = 0u, thread = 0u, read_owner = 0u;
    uint32_t mapped = 0u, token = 0u, moved = 0u, status, snapshot_offset = 0u;
    uint32_t dirty_offset = 0u, dirty_state = 0u;
    uint64_t position = 0u;
    uint64_t writeback_started, writeback_cycles = 0u;
    void *scratch_page = NULL, *extent_private = NULL, *extent_shared = NULL;
    int fd = -1, result = 2;
    const char *stage = "create";

    join(path, sizeof(path), directory, "page-writeback");
    memset(page, 'A', sizeof(page));
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0 || write(fd, page, sizeof(page)) != (ssize_t)sizeof(page) ||
        write(fd, page, 204u) != 204 || fsync(fd) != 0) goto done;
    close(fd); fd = -1;
    stage = "attach rooted backend";
    AstraVfsDirEntry root = {0};
    if (clients == NULL || parent == NULL ||
        astra_vfs_grant_derive(parent, directory + 6u,
            ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, &grant) != ASTRA_VFS_OK ||
        astra_vfs_client_connect_service(&clients[0], grant) != ASTRA_VFS_OK ||
        astra_vfs_stat_meta(&clients[0], "/", &root) != ASTRA_VFS_OK) goto done;
    backend = (AstraVfsBackend){&probe_backend_ops, &clients[0]};
    if (!astra_vfs_service_init(&core, backend.ops, backend.context,
            sessions, 1u, files, 4u) ||
        !astra_vfs_service_set_state_lock(&core, scoped_core_lock, scoped_core_unlock, &mutex) ||
        !astra_vfs_service_set_cache_ops(&core, &writeback_cache_ops, &probe))
        goto done;
    ready = 1u;
    if (astra_vfs_connect(&clients[1], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[1], "/page-writeback", ASTRA_VFS_OPEN_READ |
            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND, &file, NULL, NULL) != ASTRA_VFS_OK)
        goto done;
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION,
        .session = clients[1].session, .file = file, .offset = 123u};
    AstraVfsReply reply;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.position != 123u ||
        astra_vfs_service_retain_file(&core, clients[1].session, file, ASTRA_VFS_OPEN_READ,
            &probe.source.retained) != ASTRA_VFS_OK ||
        astra_vfs_service_stat_retained(&core, probe.source.retained, &expected) != ASTRA_VFS_OK ||
        expected.size != sizeof(page) + 204u ||
        !astra_vfs_file_page_source_init(&probe.source, &core,
            probe.source.retained, ASTRA_VFS_OPEN_READ, &expected.meta, 0u, 3u * sizeof(page), 1u)) goto done;
    if (astra_vfs_close(&clients[1], file) != ASTRA_VFS_OK) goto done;
    file = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_disconnect(&clients[1]) != ASTRA_VFS_OK ||
        core.open_files != 1u || core.open_sessions != 0u) goto done;
    stage = "pager setup";
    if (astra_rt_port_create(4u, 4u * sizeof(AstraPagerPageRequest),
            &receive, &send) != ASTRA_SYSCALL_OK ||
        astra_rt_area_create(sizeof(page), ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP, &scratch) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(scratch, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &scratch_page, &mapped) != ASTRA_SYSCALL_OK ||
        !astra_vfs_file_pager_init(&probe.pager, receive, scratch, scratch_page,
            mapped, writeback_source_acquire, writeback_source_release, &probe) ||
        astra_rt_area_create_flagged(3u * sizeof(page), ASTRA_RIGHT_READ |
            ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_ADMINISTER | ASTRA_RIGHT_TRANSFER,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK ||
        astra_rt_area_bind_pager(area, send, 1u) != ASTRA_SYSCALL_OK) goto done;
    AstraThreadStart start = {writeback_pager_run, (uint32_t)(uintptr_t)&probe};
    if (astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
            ASTRA_RIGHT_WAIT | ASTRA_RIGHT_ADMINISTER, &thread, NULL) !=
        ASTRA_SYSCALL_OK) goto done;
    stage = "publish clean preparation";
    if (astra_rt_handle_duplicate(area, ASTRA_RIGHT_READ | ASTRA_RIGHT_ADMINISTER,
            &read_owner) != ASTRA_SYSCALL_OK ||
        astra_rt_area_publish_clean_page(read_owner, sizeof(page), page, 204u) != ASTRA_SYSCALL_OK ||
        astra_rt_area_publish_clean_page(read_owner, sizeof(page), page, sizeof(page)) !=
            ASTRA_SYSCALL_WOULD_BLOCK ||
        astra_rt_area_publish_dirty_page(read_owner, 0u, page) != ASTRA_SYSCALL_ACCESS_DENIED ||
        astra_rt_area_dirty_scan(read_owner, 0u, &dirty_offset, &dirty_state) != ASTRA_SYSCALL_OK ||
        dirty_offset != probe.source.span || dirty_state != 0u ||
        astra_rt_area_read_resident(read_owner, sizeof(page), page, sizeof(page)) != ASTRA_SYSCALL_OK)
        goto done;
    for (uint32_t at = 0u; at < sizeof(page); ++at)
        if (page[at] != (at < 204u ? 'A' : 0u)) goto done;
    if (astra_rt_area_reclaim(read_owner, sizeof(page), sizeof(page), &moved) != ASTRA_SYSCALL_OK ||
        moved != 1u ||
        astra_rt_area_read_resident(read_owner, sizeof(page), page, 1u) != ASTRA_SYSCALL_WOULD_BLOCK)
        goto done;
    (void)astra_close(read_owner); read_owner = 0u;
    memset(page, 'A', sizeof(page));
    printf("FSSEM CLEAN PAGE readonly-owner zero-tail no-dirty reclaim PASS\n");
    stage = "publish cold write page";
    if (astra_rt_area_publish_dirty_page(area, 0u, page) != ASTRA_SYSCALL_OK ||
        astra_rt_area_publish_dirty_page(area, 0u, page) != ASTRA_SYSCALL_WOULD_BLOCK ||
        astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) != ASTRA_SYSCALL_OK)
        goto done;
    writeback_started = astra_clock_monotonic();
    status = astra_vfs_service_write_page_retained(&core, probe.source.retained, NULL,
        &expected, 0u, expected.size, page, &moved, &current);
    writeback_cycles += astra_clock_monotonic() - writeback_started;
    if (status != ASTRA_VFS_OK || moved != sizeof(page) || current.size != expected.size ||
        astra_rt_area_writeback_complete(area, snapshot_offset, token, true) != ASTRA_SYSCALL_OK)
        goto done;
    token = 0u;
    expected = current;
    stage = "page in";
    if (astra_rt_area_prefetch(area, sizeof(page)) != ASTRA_SYSCALL_OK ||
        astra_rt_area_read_resident(area, sizeof(page), page, sizeof(page)) !=
            ASTRA_SYSCALL_OK || page[0] != 'A' || page[203] != 'A' ||
        page[204] != 0u || page[sizeof(page) - 1u] != 0u) goto done;
    stage = "dirty snapshot";
    uint64_t page_read_started = astra_clock_monotonic();
    for (uint32_t sample = 0u; sample < 16u; ++sample)
        if (astra_vfs_service_read_page_retained(&core, probe.source.retained, ASTRA_VFS_OPEN_READ,
                &expected.meta, sizeof(page), page, &moved) != ASTRA_VFS_OK ||
            moved != 204u || page[0] != 'A' || page[203] != 'A' || page[204] != 0u)
            goto done;
    printf("FSSEM PAGE READ cycles-per-op %llu samples 16\n",
        (unsigned long long)((astra_clock_monotonic() - page_read_started) / (80u * 16u)));
    memset(page, 'R', 204u);
    snapshot_offset = sizeof(page);
    if (astra_rt_area_write_pager(area, sizeof(page), page, 204u) != ASTRA_SYSCALL_OK ||
        astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) !=
            ASTRA_SYSCALL_OK) goto done;
    stage = "backing write";
    writeback_started = astra_clock_monotonic();
    status = astra_vfs_service_write_page_retained(&core, probe.source.retained, NULL,
        &expected, sizeof(page), expected.size, page, &moved, &current);
    writeback_cycles += astra_clock_monotonic() - writeback_started;
    if (status != ASTRA_VFS_OK || moved != 204u || current.size != expected.size ||
        backend.ops->seek(backend.context, files[0].node, 0, 1u,
            &position) != ASTRA_VFS_OK || position != 123u)
        goto done;
    if (astra_rt_area_writeback_complete(area, snapshot_offset, token, true) !=
        ASTRA_SYSCALL_OK) goto done;
    token = 0u;
    expected = current;
    stage = "backing read";
    memset(page, 0, sizeof(page));
    if (astra_vfs_service_read_retained(&core, probe.source.retained, sizeof(page),
            page, sizeof(page), &moved) != ASTRA_VFS_OK || moved != 204u)
        goto done;
    for (uint32_t index = 0u; index < moved; ++index)
        if (page[index] != 'R') goto done;
    stage = "clean reclaim";
    if (astra_rt_area_reclaim(area, sizeof(page), sizeof(page), &moved) !=
            ASTRA_SYSCALL_OK || moved != 1u) goto done;
    stage = "backing refault";
    if (astra_rt_area_prefetch(area, sizeof(page)) != ASTRA_SYSCALL_OK ||
        astra_rt_area_read_resident(area, sizeof(page), page, sizeof(page)) !=
            ASTRA_SYSCALL_OK || page[0] != 'R' || page[203] != 'R' ||
        page[204] != 0u || page[sizeof(page) - 1u] != 0u) goto done;
    printf("FSSEM PAGE WRITEBACK cycles-per-op %llu samples 2\n",
        (unsigned long long)(writeback_cycles / (80u * 2u)));
    stage = "partial EOF dirty write";
    status = astra_rt_area_write_pager(area, sizeof(page) + 300u, "grow", 4u);
    if (status != ASTRA_SYSCALL_OK) {
        printf("FSSEM EOF dirty status %u\n", status);
        goto done;
    }
    stage = "partial EOF snapshot";
    if (astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) != ASTRA_SYSCALL_OK)
        goto done;
    stage = "partial EOF backing write";
    status = astra_vfs_service_write_page_retained(&core, probe.source.retained, NULL,
        &expected, snapshot_offset, sizeof(page) + 304u, page, &moved, &current);
    if (status != ASTRA_VFS_OK || moved != 304u || current.size != sizeof(page) + 304u) {
        AstraVfsNodeInfo observed = {0};
        (void)astra_vfs_service_stat_retained(&core, probe.source.retained, &observed);
        printf("FSSEM EOF status %u moved %u backing %llu current %llu\n", status, moved,
            (unsigned long long)expected.size, (unsigned long long)current.size);
        printf("FSSEM EOF revision mtime %llu/%u observed %llu/%u ctime %llu/%u observed %llu/%u\n",
            (unsigned long long)expected.mtime, expected.meta.mtime_nsec,
            (unsigned long long)observed.mtime, observed.meta.mtime_nsec,
            (unsigned long long)expected.meta.ctime, expected.meta.ctime_nsec,
            (unsigned long long)observed.meta.ctime, observed.meta.ctime_nsec);
        goto done;
    }
    stage = "partial EOF completion";
    if (astra_rt_area_writeback_complete(area, snapshot_offset, token, true) != ASTRA_SYSCALL_OK)
        goto done;
    token = 0u; expected = current;
    stage = "partial EOF readback";
    status = astra_vfs_service_read_page_retained(&core, probe.source.retained,
        ASTRA_VFS_OPEN_READ, &expected.meta, sizeof(page), page, &moved);
    if (status != ASTRA_VFS_OK || moved != 304u || memcmp(page + 300u, "grow", 4u) != 0) {
        printf("FSSEM EOF read status %u moved %u tail %u %u %u %u\n", status, moved,
            page[300], page[301], page[302], page[303]);
        goto done;
    }
    for (uint32_t at = 0u; at < 300u; ++at)
        if (page[at] != (at < 204u ? 'R' : 0u)) {
            printf("FSSEM EOF byte %u value %u\n", at, page[at]);
            goto done;
        }
    stage = "sparse EOF growth";
    snapshot_offset = 2u * sizeof(page);
    memset(page, 0, sizeof(page)); memcpy(page, "new", 3u);
    if (astra_rt_area_publish_dirty_page(area, snapshot_offset, page) != ASTRA_SYSCALL_OK ||
        astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) != ASTRA_SYSCALL_OK ||
        astra_vfs_service_write_page_retained(&core, probe.source.retained, NULL,
            &expected, snapshot_offset, snapshot_offset + 3u, page, &moved, &current) != ASTRA_VFS_OK ||
        moved != 3u || current.size != snapshot_offset + 3u ||
        astra_rt_area_writeback_complete(area, snapshot_offset, token, true) != ASTRA_SYSCALL_OK)
        goto done;
    token = 0u; expected = current;
    if (astra_vfs_service_read_page_retained(&core, probe.source.retained,
            ASTRA_VFS_OPEN_READ, &expected.meta, sizeof(page), page, &moved) != ASTRA_VFS_OK ||
        moved != sizeof(page))
        goto done;
    for (uint32_t at = 304u; at < moved; ++at)
        if (page[at] != 0u) goto done;
    if (astra_vfs_service_read_retained(&core, probe.source.retained, snapshot_offset,
            page, sizeof(page), &moved) != ASTRA_VFS_OK || moved != 3u || memcmp(page, "new", 3u) != 0)
        goto done;
    stage = "empty backing growth";
    if (astra_rt_area_reclaim(area, 0u, 3u * sizeof(page), &moved) != ASTRA_SYSCALL_OK ||
        backend.ops->truncate(backend.context, files[0].node, 0u) != ASTRA_VFS_OK ||
        astra_vfs_service_stat_retained(&core, probe.source.retained, &expected) != ASTRA_VFS_OK ||
        expected.size != 0u) goto done;
    snapshot_offset = 0u;
    memset(page, 0, sizeof(page)); memset(page, 'E', 23u);
    if (astra_rt_area_publish_dirty_page(area, snapshot_offset, page) != ASTRA_SYSCALL_OK ||
        astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) != ASTRA_SYSCALL_OK ||
        astra_vfs_service_write_page_retained(&core, probe.source.retained, NULL,
            &expected, snapshot_offset, 23u, page, &moved, &expected) != ASTRA_VFS_OK ||
        moved != 23u || expected.size != 23u ||
        astra_rt_area_writeback_complete(area, snapshot_offset, token, true) != ASTRA_SYSCALL_OK)
        goto done;
    token = 0u;
    if (astra_vfs_service_read_retained(&core, probe.source.retained, 0u,
            page, sizeof(page), &moved) != ASTRA_VFS_OK || moved != 23u ||
        backend.ops->seek(backend.context, files[0].node, 0, 1u, &position) != ASTRA_VFS_OK ||
        position != 123u) goto done;
    for (uint32_t at = 0u; at < moved; ++at)
        if (page[at] != 'E') goto done;
    printf("FSSEM WRITEBACK EOF partial sparse empty preserved-cursor PASS\n");
    stage = "native cache writer handoff";
    probe.cache_area = area;
    if (astra_vfs_service_write_retained(&core, probe.source.retained,
            30u, 35u, "cache", 5u, &moved) != ASTRA_VFS_OK || moved != 5u ||
        probe.cache_writes != 1u || probe.cache_writer == ASTRA_VFS_FILE_INVALID ||
        astra_vfs_service_stat_retained(&core, probe.cache_writer, &current) != ASTRA_VFS_OK ||
        current.size != 35u ||
        backend.ops->stat_node(backend.context, files[0].node, &current) != ASTRA_VFS_OK ||
        current.size != 23u)
        goto done;
    stage = "native shared cache metadata";
    uint64_t logical_size = 0u;
    AstraVfsDirEntry logical = {0};
    uint8_t escape_path[1];
    AstraVfsEscape escape = {.path = escape_path, .capacity = sizeof(escape_path)};
    if (astra_vfs_connect(&clients[1], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_stat_meta(&clients[1], "/page-writeback", &logical) != ASTRA_VFS_OK ||
        logical.size != 35u ||
        astra_vfs_lookup(&clients[1], 0u, "/page-writeback", ASTRA_VFS_LOOKUP_FOLLOW,
            ASTRA_VFS_LINK_MAX, &logical, &escape) != ASTRA_VFS_OK || logical.size != 35u ||
        astra_vfs_open(&clients[1], "/page-writeback", ASTRA_VFS_OPEN_READ |
            ASTRA_VFS_OPEN_WRITE, &file, &logical_size, NULL) != ASTRA_VFS_OK ||
        logical_size != 35u ||
        astra_vfs_seek(&clients[1], file, -1, 2u, &position) != ASTRA_VFS_OK || position != 34u ||
        astra_vfs_write(&clients[1], file, 36u, 40u, "++", 2u, &moved) != ASTRA_VFS_OK || moved != 2u ||
        astra_vfs_stat_file_meta(&clients[1], file, &logical) != ASTRA_VFS_OK || logical.size != 38u ||
        astra_vfs_seek(&clients[1], file, 0, 2u, &position) != ASTRA_VFS_OK || position != 38u ||
        backend.ops->stat_node(backend.context, files[0].node, &current) != ASTRA_VFS_OK ||
        current.size != 23u || probe.cache_writes != 2u ||
        astra_vfs_close(&clients[1], file) != ASTRA_VFS_OK)
        goto done;
    file = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_disconnect(&clients[1]) != ASTRA_VFS_OK ||
        astra_vfs_service_release_retained(&core, &probe.source.retained) != ASTRA_VFS_OK ||
        core.open_files != 1u || files[0].retained != 1u)
        goto done;
    stage = "native cache resident bytes";
    if (astra_rt_area_read_resident(area, 0u, page, sizeof(page)) != ASTRA_SYSCALL_OK ||
        memcmp(page + 30u, "cache", 5u) != 0 || page[35] != 0u ||
        memcmp(page + 36u, "++", 2u) != 0) goto done;
    for (uint32_t at = 0u; at < 30u; ++at)
        if (page[at] != (at < 23u ? 'E' : 0u)) goto done;
    stage = "native cache writer flush";
    probe.sync_page = page;
    probe.sync_fail_once = 1u;
    uint32_t writeback_remaining = 1u;
    if (astra_vfs_connect(&clients[1], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[1], "/page-writeback", ASTRA_VFS_OPEN_READ,
            &file, &logical_size, NULL) != ASTRA_VFS_OK || logical_size != 38u ||
        astra_vfs_read(&clients[1], file, 0u, page, sizeof(page), &moved) != ASTRA_VFS_OK ||
        moved != 38u || memcmp(page + 30u, "cache", 5u) != 0 ||
        memcmp(page + 36u, "++", 2u) != 0 ||
        astra_vfs_service_read_path(&core, clients[1].session, "/page-writeback", page,
            sizeof(page), &moved, &logical_size) != ASTRA_VFS_OK || moved != 38u || logical_size != 38u ||
        memcmp(page + 30u, "cache", 5u) != 0 || memcmp(page + 36u, "++", 2u) != 0 ||
        astra_vfs_seek(&clients[1], file, 30, 0u, &position) != ASTRA_VFS_OK || position != 30u ||
        astra_vfs_read_position(&clients[1], file, 0u, ASTRA_VFS_IO_SEQUENTIAL,
            page, 5u, &moved, &position) != ASTRA_VFS_OK || moved != 5u || position != 35u ||
        memcmp(page, "cache", 5u) != 0 || probe.cache_reads != 3u ||
        backend.ops->stat_node(backend.context, files[0].node, &current) != ASTRA_VFS_OK ||
        current.size != 23u ||
        astra_vfs_sync(&clients[1], file) != ASTRA_VFS_ERR_IO || probe.sync_calls != 1u ||
        backend.ops->stat_node(backend.context, files[0].node, &current) != ASTRA_VFS_OK ||
        current.size != 23u ||
        astra_vfs_stat_file_meta(&clients[1], file, &logical) != ASTRA_VFS_OK || logical.size != 38u ||
        astra_rt_area_dirty_scan(area, 0u, &dirty_offset, &dirty_state) != ASTRA_SYSCALL_OK ||
        dirty_offset != 0u || dirty_state != ASTRA_AREA_DIRTY_MODIFIED ||
        astra_vfs_service_sync_retained(&core, probe.cache_writer, &writeback_remaining) != ASTRA_VFS_ERR_BUSY ||
        writeback_remaining != 0u ||
        probe.sync_cursor != ASTRA_MEMORY_PAGE_SIZE ||
        astra_vfs_sync(&clients[1], file) != ASTRA_VFS_OK || probe.sync_calls != 3u ||
        backend.ops->stat_node(backend.context, files[0].node, &current) != ASTRA_VFS_OK ||
        current.size != 38u || probe.cache_writes != 2u ||
        astra_vfs_close(&clients[1], file) != ASTRA_VFS_OK)
        goto done;
    file = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_disconnect(&clients[1]) != ASTRA_VFS_OK || core.open_files != 1u)
        goto done;
    if (astra_vfs_service_read_retained(&core, probe.cache_writer, 0u,
            page, sizeof(page), &moved) != ASTRA_VFS_OK || moved != 38u ||
        memcmp(page + 30u, "cache", 5u) != 0 ||
        memcmp(page + 36u, "++", 2u) != 0 ||
        backend.ops->seek(backend.context, files[0].node, 0, 1u, &position) != ASTRA_VFS_OK ||
        position != 123u) goto done;
    AstraVfsFileCache cache;
    uint32_t cache_sequence;
    if (astra_vfs_service_cache_snapshot(&core, probe.cache_writer, &cache,
            &cache_sequence) != ASTRA_VFS_OK || cache.size != 38u || cache.backing.size != 38u ||
        astra_vfs_service_cache_retire(&core, probe.cache_writer, cache_sequence) != ASTRA_VFS_OK ||
        core.open_files != 0u) goto done;
    probe.cache_writer = ASTRA_VFS_FILE_INVALID;
    stage = "native timestamps survive cache retirement";
    AstraVfsDirEntry persisted = {0};
    if (astra_vfs_stat_meta(&clients[0], "/page-writeback", &persisted) != ASTRA_VFS_OK ||
        persisted.mtime != (int64_t)(cache.modified_ns / UINT64_C(1000000000)) ||
        persisted.meta.mtime_nsec != cache.modified_ns % UINT64_C(1000000000)) goto done;
    stage = "native descriptor timestamp authority";
    AstraVfsDirEntry times = {.mtime = 1234567890, .meta = {
        .atime = 1234567800, .atime_nsec = 123456789u, .mtime_nsec = 987654321u}};
    if (astra_vfs_open(&clients[0], "/page-writeback", ASTRA_VFS_OPEN_READ,
            &timestamp_file, NULL, NULL) != ASTRA_VFS_OK ||
        astra_vfs_set_file_times(&clients[0], timestamp_file, &times, ASTRA_VFS_META_MTIME) !=
            ASTRA_VFS_ERR_ACCESS || astra_vfs_close(&clients[0], timestamp_file) != ASTRA_VFS_OK)
        goto done;
    timestamp_file = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_open(&clients[0], "/page-writeback", ASTRA_VFS_OPEN_WRITE,
            &timestamp_file, NULL, NULL) != ASTRA_VFS_OK ||
        astra_vfs_set_file_times(&clients[0], timestamp_file, &times,
            ASTRA_VFS_META_ATIME | ASTRA_VFS_META_MTIME) != ASTRA_VFS_OK ||
        astra_vfs_sync(&clients[0], timestamp_file) != ASTRA_VFS_OK ||
        astra_vfs_stat_file_meta(&clients[0], timestamp_file, &persisted) != ASTRA_VFS_OK ||
        persisted.mtime != times.mtime || persisted.meta.mtime_nsec != times.meta.mtime_nsec ||
        persisted.meta.atime != times.meta.atime || persisted.meta.atime_nsec != times.meta.atime_nsec)
        goto done;
    times.mtime = -1; times.meta.mtime_nsec = 999999999u;
    if (astra_vfs_set_file_times(&clients[0], timestamp_file, &times,
            ASTRA_VFS_META_MTIME) != ASTRA_VFS_OK ||
        astra_vfs_close(&clients[0], timestamp_file) != ASTRA_VFS_OK) goto done;
    timestamp_file = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_stat_meta(&clients[0], "/page-writeback", &persisted) != ASTRA_VFS_OK ||
        persisted.mtime != -1 || persisted.meta.mtime_nsec != 999999999u ||
        persisted.meta.atime != times.meta.atime || persisted.meta.atime_nsec != times.meta.atime_nsec)
        goto done;
    printf("FSSEM FILE TIMES retained-authority cache-retirement sync subset PASS\n");
    stage = "native backing resize";
    if (astra_rt_area_resize_backing(area, 0u) != ASTRA_SYSCALL_OK)
        goto done;
    snapshot_offset = 2u * sizeof(page);
    memset(page, 'Z', sizeof(page));
    if (astra_rt_area_publish_dirty_page(area, snapshot_offset, page) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE |
            ASTRA_AREA_MAP_PRIVATE, &extent_private, &mapped) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(area, ASTRA_AREA_MAP_READ, &extent_shared, &mapped) != ASTRA_SYSCALL_OK ||
        astra_rt_area_resize_backing(area, 3u * sizeof(page)) != ASTRA_SYSCALL_OK ||
        ((volatile uint8_t *)extent_private)[snapshot_offset] != 'Z')
        goto done;
    ((volatile uint8_t *)extent_private)[snapshot_offset] = 'P';
    if (((volatile uint8_t *)extent_shared)[snapshot_offset] != 'Z' ||
        astra_rt_area_writeback_begin(area, snapshot_offset, page, &token) != ASTRA_SYSCALL_OK ||
        astra_rt_area_resize_backing(area, sizeof(page) + 9u) != ASTRA_SYSCALL_WOULD_BLOCK ||
        ((volatile uint8_t *)extent_private)[snapshot_offset] != 'P' ||
        astra_rt_area_writeback_complete(area, snapshot_offset, token, true) != ASTRA_SYSCALL_OK)
        goto done;
    token = 0u;
    if (astra_rt_area_reclaim(area, snapshot_offset, sizeof(page), &moved) != ASTRA_SYSCALL_OK ||
        moved != 1u || astra_rt_area_resize_backing(area, sizeof(page) + 9u) != ASTRA_SYSCALL_OK ||
        astra_rt_area_prefetch(area, snapshot_offset) != ASTRA_SYSCALL_IO_ERROR)
        goto done;
    memset(page, 'N', sizeof(page));
    if (astra_rt_area_publish_clean_page(area, snapshot_offset, page, sizeof(page)) != ASTRA_SYSCALL_OK ||
        astra_rt_area_read_resident(area, snapshot_offset, page, 1u) != ASTRA_SYSCALL_WOULD_BLOCK ||
        astra_rt_area_write_pager(area, snapshot_offset, "new", 3u) != ASTRA_SYSCALL_OK ||
        astra_rt_area_resize_backing(area, snapshot_offset + 3u) != ASTRA_SYSCALL_OK ||
        ((volatile uint8_t *)extent_private)[snapshot_offset] != 'n' ||
        ((volatile uint8_t *)extent_shared)[snapshot_offset + 2u] != 'w' ||
        ((volatile uint8_t *)extent_private)[snapshot_offset + 3u] != 0u ||
        astra_rt_area_resize_backing(area, 0u) != ASTRA_SYSCALL_OK)
        goto done;
    printf("FSSEM BACKING RESIZE staged-growth private-revoke tail-zero inflight-refusal PASS\n");
    printf("FSSEM CACHE SYNC native-pages readonly-caller retained-writer PASS\n");
    printf("FSSEM CACHE WRITEBACK bounded shared-worker retained-io PASS\n");
    printf("FSSEM CACHE READ native-pages inline whole-file sequential logical-eof PASS\n");
    printf("FSSEM DIRTY SCAN pending inflight failed retry clean PASS\n");
    printf("FSSEM CACHE METADATA shared-owner logical-eof seek raw-backing PASS\n");
    printf("FSSEM CACHE WRITE native-pages retained-authority backing-bypass PASS\n");
    result = 0;
done:
    if (token != 0u)
        (void)astra_rt_area_writeback_complete(area, snapshot_offset, token, false);
    if (extent_private != NULL) (void)astra_rt_area_unmap(extent_private);
    if (extent_shared != NULL) (void)astra_rt_area_unmap(extent_shared);
    if (read_owner != 0u) (void)astra_close(read_owner);
    __atomic_store_n(&probe.stop, 1u, __ATOMIC_RELEASE);
    if (thread != 0u) {
        (void)astra_rt_cancel_wait(thread);
        (void)astra_wait_one_restart(thread, ASTRA_DEADLINE_FOREVER, NULL);
        (void)astra_close(thread);
    }
    if (scratch_page != NULL) (void)astra_rt_area_unmap(scratch_page);
    if (area != 0u) (void)astra_close(area);
    if (scratch != 0u) (void)astra_close(scratch);
    if (send != 0u) (void)astra_close(send);
    if (receive != 0u) (void)astra_close(receive);
    if (clients != NULL) {
        if (timestamp_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_close(&clients[0], timestamp_file);
        if (file != ASTRA_VFS_FILE_INVALID) (void)astra_vfs_close(&clients[1], file);
        if (clients[1].session != ASTRA_VFS_SESSION_INVALID) (void)astra_vfs_disconnect(&clients[1]);
        if (probe.source.retained != ASTRA_VFS_FILE_INVALID &&
            astra_vfs_service_release_retained(&core, &probe.source.retained) != ASTRA_VFS_OK)
            result = 2;
        if (probe.cache_writer != ASTRA_VFS_FILE_INVALID &&
            astra_vfs_service_release_retained(&core, &probe.cache_writer) != ASTRA_VFS_OK)
            result = 2;
        if (ready && (core.open_files != 0u || core.open_sessions != 0u)) result = 2;
        if (clients[0].transport != NULL) (void)astra_vfs_disconnect(&clients[0]);
        free(clients);
    }
    if (grant != 0u) (void)astra_close(grant);
    (void)pthread_mutex_destroy(&mutex);
    if (fd >= 0) close(fd);
    (void)unlink(path);
    return result == 0 ? 0 : fail("page-writeback %s", stage);
}

static int case_description_io(const char *directory)
{
    AstraVfsService core = {0};
    AstraVfsSessionSlot sessions[1] = {0};
    AstraVfsOpenFile files[1] = {0};
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE,
        .version = ASTRA_VFS_VERSION, .write_limit = UINT64_MAX};
    AstraVfsReply reply;
    AstraVfsTransfer transfer = {0};
    AstraVfsClient *client = NULL;
    AstraVfsFile file = 0u, retained = 0u;
    uint32_t session = 0u, moved = 0u, used = 0u;
    uint64_t position = 0u;
    char path[192], wire[192], bytes[16];
    uint8_t paths[sizeof(wire)];
    int ready = 0, result = 2;
    const char *stage = "create";

    join(path, sizeof(path), directory, "description-io");
    wire_path(wire, sizeof(wire), directory, "description-io");
    if (write_text(path, "abcdef", O_WRONLY | O_CREAT | O_EXCL) != 0) goto done;
    const AstraAssign *assign = astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    if (assign == NULL) goto done;
    client = astra_process_vfs_client_for(assign);
    if (client == NULL) goto done;
    stage = "service admission";
    if (!astra_vfs_service_init(&core, &probe_backend_ops,
        client, sessions, 1u, files, 1u)) goto done;
    ready = 1;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK) goto done;
    session = request.session = reply.session;
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    if (astra_vfs_path_pack(wire, NULL, paths, sizeof(paths), &request.body.paths, &used) !=
        ASTRA_VFS_OK) goto done;
    transfer.input = paths;
    transfer.input_size = used;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_OPEN, &request, &transfer, &reply);
    if (reply.status != ASTRA_VFS_OK) goto done;
    file = reply.file;
    stage = "description controls";
    request = (AstraVfsRequest){.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION,
        .session = session, .file = file, .offset = (uint64_t)(int64_t)-2, .flags = 2u};
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.position != 4u) goto done;
    request.offset = 4u;
    request.flags = 0u;
    request.length = 1u;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_POSITION_ADVANCE, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.count != 1u || reply.position != 5u) goto done;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_POSITION_ADVANCE, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.count != 0u || reply.position != 5u) goto done;
    request.length = 0u;
    request.flags = ASTRA_VFS_FILE_STATUS_MASK;
    request.offset = ASTRA_VFS_OPEN_APPEND;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_FILE_FLAGS, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK ||
        (reply.open_flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_FILE_STATUS_MASK)) !=
        (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND)) goto done;
    request.offset = 0u;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_FILE_FLAGS, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || (reply.open_flags & ASTRA_VFS_FILE_STATUS_MASK) != 0u)
        goto done;
    request.flags = 0u;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.position != 0u) goto done;
    stage = "inline sequential read";
    request = (AstraVfsRequest){.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION,
        .session = session, .file = file, .offset = 99u, .length = 2u,
        .flags = ASTRA_VFS_IO_SEQUENTIAL, .write_limit = UINT64_MAX};
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_READ, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.count != 2u || reply.position != 2u ||
        memcmp(reply.payload, "ab", 2u) != 0) {
        printf("FSSEM description-io read status=%u count=%u position=%llu bytes=%u,%u\n",
            reply.status, reply.count, (unsigned long long)reply.position,
            reply.payload[0], reply.payload[1]);
        goto done;
    }
    stage = "inline sequential write";
    request.length = 1u;
    request.body.payload[0] = 'X';
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_WRITE, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.count != 1u || reply.position != 3u) goto done;
    stage = "positioned read";
    if (astra_vfs_service_read_position_into(&core, session, file, 0u, 0u,
        bytes, 2u, &moved, &position) != ASTRA_VFS_OK || moved != 2u ||
        position != 2u || memcmp(bytes, "ab", 2u) != 0) goto done;
    stage = "bulk sequential read";
    if (astra_vfs_service_read_position_into(&core, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, 1u, &moved, &position) != ASTRA_VFS_OK ||
        moved != 1u || position != 4u || bytes[0] != 'd') goto done;
    stage = "bounded sequential append";
    if (astra_vfs_service_write_position_from(&core, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL | ASTRA_VFS_WRITE_APPEND, 8u, "LMN", 3u,
        &moved, &position) != ASTRA_VFS_OK || moved != 2u || position != 8u) goto done;
    stage = "sequential EOF";
    if (astra_vfs_service_read_position_into(&core, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) != ASTRA_VFS_OK ||
        moved != 0u || position != 8u) goto done;
    stage = "retained positioned read";
    if (astra_vfs_service_retain_file(&core, session, file, ASTRA_VFS_OPEN_READ, &retained) != ASTRA_VFS_OK)
        goto done;
    astra_vfs_service_release_session(&core, session);
    session = 0u;
    if (astra_vfs_service_read_retained(&core, retained, 0u, bytes, sizeof(bytes), &moved) !=
        ASTRA_VFS_OK || moved != 8u || memcmp(bytes, "abXdefLM", 8u) != 0) goto done;
    result = 0;
done:
    if (ready && session != 0u) astra_vfs_service_release_session(&core, session);
    if (retained != 0u) (void)astra_vfs_service_release_retained(&core, &retained);
    if (result == 0 && (read_text(path, bytes, sizeof(bytes)) != 8 ||
        memcmp(bytes, "abXdefLM", 8u) != 0)) { result = 2; stage = "backing bytes"; }
    (void)unlink(path);
    return result == 0 ? 0 : fail("description-io %s", stage);
}

typedef struct ScopedIoBindings {
    AstraVfsBackend roots[2];
    uint32_t references[2];
    uint32_t selected;
    uint32_t failed;
} ScopedIoBindings;

static uint32_t scoped_io_index(const ScopedIoBindings *bindings, const AstraVfsBackend *backend)
{
    for (uint32_t i = 0u; i < 2u; ++i)
        if (bindings->roots[i].ops == backend->ops && bindings->roots[i].context == backend->context)
            return i;
    return 2u;
}

static uint32_t scoped_io_retain(void *context, const AstraVfsBackend *backend)
{
    ScopedIoBindings *bindings = context;
    uint32_t i = scoped_io_index(bindings, backend);
    if (i == 2u) return ASTRA_VFS_ERR_BAD_HANDLE;
    ++bindings->references[i];
    return ASTRA_VFS_OK;
}

static uint32_t scoped_io_acquire(void *context, uint32_t owner, uint32_t grant,
    AstraVfsBackend *backend)
{
    ScopedIoBindings *bindings = context;
    (void)owner; (void)grant;
    if (bindings->selected >= 2u) return ASTRA_VFS_ERR_INVALID;
    *backend = bindings->roots[bindings->selected];
    return scoped_io_retain(context, backend);
}

static void scoped_io_release(void *context, const AstraVfsBackend *backend)
{
    ScopedIoBindings *bindings = context;
    uint32_t i = scoped_io_index(bindings, backend);
    if (i == 2u || bindings->references[i] == 0u) bindings->failed = 1u;
    else --bindings->references[i];
}

static struct {
    uint32_t (*adopt)(void *, uint64_t, uint32_t, uintptr_t *);
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    AstraVfsService *core;
    uint32_t source, target, owner;
    uint64_t token;
    int enabled, entered, probed, done, unexpected, passed, fail_adopt;
} scoped_transfer_probe;

/* Hold before provider submission: its borrowed channel stays on this thread. */
static uint32_t scoped_transfer_adopt(void *context, uint64_t token,
    uint32_t flags, uintptr_t *node)
{
    if (scoped_transfer_probe.enabled && token == scoped_transfer_probe.token) {
        if (pthread_mutex_lock(&scoped_transfer_probe.mutex) != 0) return ASTRA_VFS_ERR_IO;
        if (scoped_transfer_probe.entered || flags != 0u) {
            scoped_transfer_probe.unexpected = 1;
            (void)pthread_mutex_unlock(&scoped_transfer_probe.mutex);
            return ASTRA_VFS_ERR_IO;
        }
        scoped_transfer_probe.entered = 1;
        (void)pthread_cond_broadcast(&scoped_transfer_probe.changed);
        while (!scoped_transfer_probe.probed)
            (void)pthread_cond_wait(&scoped_transfer_probe.changed, &scoped_transfer_probe.mutex);
        int fail_adopt = scoped_transfer_probe.fail_adopt;
        (void)pthread_mutex_unlock(&scoped_transfer_probe.mutex);
        if (fail_adopt) return ASTRA_VFS_ERR_IO;
    }
    return scoped_transfer_probe.adopt(context, token, flags, node);
}

static void *scoped_transfer_compete(void *unused)
{
    AstraVfsReply reply;
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION,
        .session = scoped_transfer_probe.target, .offset = scoped_transfer_probe.token};
    (void)unused;
    (void)pthread_mutex_lock(&scoped_transfer_probe.mutex);
    while (!scoped_transfer_probe.entered && !scoped_transfer_probe.done)
        (void)pthread_cond_wait(&scoped_transfer_probe.changed, &scoped_transfer_probe.mutex);
    int entered = scoped_transfer_probe.entered;
    (void)pthread_mutex_unlock(&scoped_transfer_probe.mutex);
    int passed = 0;
    if (entered) {
        astra_vfs_service_dispatch_from(scoped_transfer_probe.core, scoped_transfer_probe.owner,
            ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
        passed = reply.status == ASTRA_VFS_ERR_BUSY;
        request.session = scoped_transfer_probe.source;
        request.flags = ASTRA_VFS_ADOPT_DISCARD;
        astra_vfs_service_dispatch(scoped_transfer_probe.core, ASTRA_VFS_OP_ADOPT,
            &request, NULL, &reply);
        passed = passed && reply.status == ASTRA_VFS_ERR_BUSY;
    }
    (void)pthread_mutex_lock(&scoped_transfer_probe.mutex);
    scoped_transfer_probe.passed = passed;
    scoped_transfer_probe.probed = 1;
    (void)pthread_cond_broadcast(&scoped_transfer_probe.changed);
    (void)pthread_mutex_unlock(&scoped_transfer_probe.mutex);
    return NULL;
}

static int scoped_transfer_claim(AstraVfsService *core, uint32_t source,
    uint32_t owner, const AstraVfsRequest *request, int fail_adopt, AstraVfsReply *reply)
{
    pthread_t thread;
    scoped_transfer_probe.core = core;
    scoped_transfer_probe.source = source;
    scoped_transfer_probe.target = request->session;
    scoped_transfer_probe.owner = owner;
    scoped_transfer_probe.token = request->offset;
    scoped_transfer_probe.entered = scoped_transfer_probe.probed = scoped_transfer_probe.done = 0;
    scoped_transfer_probe.unexpected = scoped_transfer_probe.passed = 0;
    scoped_transfer_probe.fail_adopt = fail_adopt;
    CHECK(pthread_mutex_init(&scoped_transfer_probe.mutex, NULL) == 0, "transfer probe mutex");
    if (pthread_cond_init(&scoped_transfer_probe.changed, NULL) != 0) {
        (void)pthread_mutex_destroy(&scoped_transfer_probe.mutex);
        return fail("transfer probe condition");
    }
    scoped_transfer_probe.enabled = 1;
    if (pthread_create(&thread, NULL, scoped_transfer_compete, NULL) != 0) {
        scoped_transfer_probe.enabled = 0;
        (void)pthread_cond_destroy(&scoped_transfer_probe.changed);
        (void)pthread_mutex_destroy(&scoped_transfer_probe.mutex);
        return fail("transfer probe thread");
    }
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_ADOPT, request, NULL, reply);
    (void)pthread_mutex_lock(&scoped_transfer_probe.mutex);
    scoped_transfer_probe.done = 1;
    (void)pthread_cond_broadcast(&scoped_transfer_probe.changed);
    (void)pthread_mutex_unlock(&scoped_transfer_probe.mutex);
    int joined = pthread_join(thread, NULL);
    if (joined != 0) {
        (void)fail("native transfer probe join: %d", joined);
        _exit(2);
    }
    scoped_transfer_probe.enabled = 0;
    (void)pthread_cond_destroy(&scoped_transfer_probe.changed);
    (void)pthread_mutex_destroy(&scoped_transfer_probe.mutex);
    CHECK(scoped_transfer_probe.passed && !scoped_transfer_probe.unexpected &&
        reply->status == (fail_adopt ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK),
        "native concurrent transfer/withdrawal status %u", (unsigned)reply->status);
    return 0;
}

static struct {
    AstraVfsService *core;
    AstraVfsFile alias;
    uint32_t passed;
} scoped_alias_io;

static uint32_t scoped_alias_write(void *context, uintptr_t node, uint64_t offset,
    uint32_t flags, uint64_t limit, const void *bytes, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    if (astra_vfs_service_sync_retained(scoped_alias_io.core, scoped_alias_io.alias, NULL) != ASTRA_VFS_ERR_BUSY) return ASTRA_VFS_ERR_IO;
    ++scoped_alias_io.passed;
    return probe_write(context, node, offset, flags, limit, bytes, length, moved, position);
}

static int scoped_io_shares(AstraVfsService *core, AstraVfsFile original,
    uint32_t source, ScopedIoBindings *bindings, AstraVfsBackendOps *backend)
{
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION};
    AstraVfsReply reply;
    uint32_t owner = (uint32_t)getpid(), target, foreign;
    uint64_t token;
    AstraVfsFile adopted, extra;
    uint8_t path[16];
    uint32_t used;
    AstraVfsTransfer transfer = {.input = path};

    CHECK(astra_vfs_service_set_owner_files(core, 2u), "native file quota setup");
    request.session = source; request.file = original;
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_SHARE, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.token != 0u, "native SHARE");
    token = reply.token;
    request.session = request.file = 0u;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "native receiver HELLO");
    target = reply.session;
    astra_vfs_service_dispatch_grant(core, owner + 1u, 91u, 0u,
        ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "foreign grant HELLO");
    foreign = request.session = reply.session; request.offset = token;
    astra_vfs_service_dispatch_grant(core, owner + 1u, 91u, 0u,
        ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_ACCESS, "share crossed native grant");
    astra_vfs_service_release_session(core, foreign);
    bindings->selected = 1u;
    request.session = target; request.offset = 0u;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "rebind receiver root");
    request.offset = token;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_LOST, "share crossed provider incarnation/root");
    bindings->selected = 0u; request.offset = 0u;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "restore receiver root");
    request.session = source; request.file = original; request.length = 1u;
    request.flags = ASTRA_VFS_IO_SEQUENTIAL;
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_READ, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.count == 1u && reply.position == 1u &&
        reply.payload[0] == 'l', "shared source cursor");
    request.session = target; request.file = request.flags = request.length = 0u;
    request.offset = token;
    CHECK(scoped_transfer_claim(core, source, owner, &request, 1, &reply) == 0,
        "failed claim retains native token");
    CHECK(core->open_files == 2u, "failed claim kept its native reference");
    CHECK(scoped_transfer_claim(core, source, owner, &request, 0, &reply) == 0,
        "retry native token after failed claim");
    CHECK(reply.status == ASTRA_VFS_OK && reply.file != 0u, "native ADOPT");
    adopted = reply.file;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_BAD_HANDLE, "share token replay");
    request.offset = 0u; request.file = adopted; request.length = 1u;
    request.flags = ASTRA_VFS_IO_SEQUENTIAL;
    astra_vfs_service_dispatch_from(core, owner + 1u, ASTRA_VFS_OP_READ, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_ACCESS, "adopted file crossed native owner");
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_READ, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.count == 1u && reply.position == 2u &&
        reply.payload[0] == 'e', "adopted shared cursor");
    CHECK(astra_vfs_service_retain_file(core, target, adopted,
        ASTRA_VFS_OPEN_WRITE, &scoped_alias_io.alias) == ASTRA_VFS_OK,
        "retain shared I/O alias");
    scoped_alias_io.core = core;
    scoped_alias_io.passed = 0u;
    backend->write = scoped_alias_write;
    uint32_t moved = 0u;
    uint32_t written = astra_vfs_service_write_from(core, source, original,
        2u, UINT64_MAX, "f", 1u, &moved);
    backend->write = probe_write;
    uint32_t released = astra_vfs_service_release_retained(core, &scoped_alias_io.alias);
    CHECK(written == ASTRA_VFS_OK && moved == 1u && released == ASTRA_VFS_OK &&
        scoped_alias_io.passed == 1u, "shared native description I/O status %u", written);
    request.write_limit = UINT64_MAX; request.body.payload[0] = 'f';
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_WRITE, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.count == 1u && reply.position == 3u,
        "adopted access flags/write cursor");
    request.session = source; request.file = original;
    memset(&request.body, 0, sizeof(request.body));
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_READ, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.position == 4u && reply.payload[0] == 't',
        "source observes adopted cursor");
    request.flags = request.length = 0u; request.write_limit = 0u;
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK && reply.position == 0u, "reset shared cursor");
    request.session = target; request.file = 0u; request.flags = ASTRA_VFS_OPEN_READ;
    CHECK(astra_vfs_path_pack("/same", NULL, path, sizeof(path), &request.body.paths, &used) ==
        ASTRA_VFS_OK, "quota path");
    transfer.input_size = used;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_OPEN, &request, &transfer, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "receiver second file");
    extra = reply.file;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_OPEN, &request, &transfer, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_LIMIT, "native receiver quota");
    request.flags = 0u; request.file = extra;
    memset(&request.body, 0, sizeof(request.body));
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_CLOSE, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "close quota file");
    request.file = adopted;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_CLOSE, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "close adopted file");
    request.session = source; request.file = original;
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_SHARE, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "native share for withdrawal");
    request.offset = reply.token;
    request.session = target; request.file = 0u; request.flags = ASTRA_VFS_ADOPT_DISCARD;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_ACCESS, "foreign withdrawal");
    request.session = source;
    astra_vfs_service_dispatch(core, ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_OK, "owner withdrawal");
    request.session = target; request.flags = 0u;
    astra_vfs_service_dispatch_from(core, owner, ASTRA_VFS_OP_ADOPT, &request, NULL, &reply);
    CHECK(reply.status == ASTRA_VFS_ERR_BAD_HANDLE, "withdrawn token replay");
    astra_vfs_service_release_session(core, target);
    AstraVfsClient local = {0};
    AstraVfsFile directory = 0u, directory_alias = 0u;
    uint64_t directory_token = 0u;
    CHECK(astra_vfs_connect(&local, astra_vfs_local_transport, core) == ASTRA_VFS_OK &&
        astra_vfs_open(&local, "/", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
            &directory, NULL, NULL) == ASTRA_VFS_OK &&
        astra_vfs_share(&local, directory, &directory_token) == ASTRA_VFS_OK &&
        astra_vfs_adopt(&local, directory_token, &directory_alias) == ASTRA_VFS_OK,
        "shared native directory anchors");
    CHECK(astra_vfs_rename_at(&local, directory, "same", directory_alias, "renamed") == ASTRA_VFS_OK &&
        astra_vfs_rename_at(&local, directory_alias, "renamed", directory, "same") == ASTRA_VFS_OK,
        "shared native directory owner recursion");
    CHECK(astra_vfs_close(&local, directory) == ASTRA_VFS_OK &&
        astra_vfs_close(&local, directory_alias) == ASTRA_VFS_OK &&
        astra_vfs_disconnect(&local) == ASTRA_VFS_OK, "shared native directory cleanup");
    CHECK(core->open_sessions == 1u && core->open_files == 1u &&
        bindings->references[0] == 2u && bindings->references[1] == 0u,
        "native transfer backing lifetime");
    return 0;
}

static struct {
    AstraVfsService *core;
    AstraVfsClient *client;
    AstraVfsFile retained;
    uint8_t *page;
    uint32_t passed;
} scoped_page_fill;

static uint32_t scoped_page_fill_write(void *context, uintptr_t node, uint64_t offset,
    uint32_t flags, uint64_t limit, const void *bytes, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    uint32_t valid = 0u;
    memset(scoped_page_fill.page, 0xa5, ASTRA_MEMORY_PAGE_SIZE);
    uint32_t closed = astra_vfs_close(scoped_page_fill.client, scoped_page_fill.retained);
    uint32_t read = closed == ASTRA_VFS_OK ?
        astra_vfs_service_read_page_retained(scoped_page_fill.core, scoped_page_fill.retained, ASTRA_VFS_OPEN_READ,
            NULL, 0u, scoped_page_fill.page, &valid) : ASTRA_VFS_ERR_IO;
    if (closed != ASTRA_VFS_OK || read != ASTRA_VFS_ERR_BUSY ||
        valid != 0u) {
        printf("FSSEM PAGE FILL close=%u read=%u valid=%u first=%u tail=%u\n",
            closed, read, valid, scoped_page_fill.page[0], scoped_page_fill.page[4]);
        return ASTRA_VFS_ERR_IO;
    }
    ++scoped_page_fill.passed;
    return probe_write(context, node, offset, flags, limit, bytes, length, moved, position);
}

static uint64_t scoped_io_incarnation(void *context, const AstraVfsBackend *backend)
{
    (void)context; (void)backend;
    /* Root rebinding in this probe stays on one native test volume. */
    return 1u;
}

static int case_scoped_io(const char *directory)
{
    static const AstraVfsBackendBindingOps ops = {scoped_io_acquire, scoped_io_retain,
        scoped_io_release, scoped_io_incarnation};
    AstraVfsClient *clients = calloc(3u, sizeof(*clients));
    uint8_t *page = malloc(ASTRA_MEMORY_PAGE_SIZE);
    AstraVfsService core = {0};
    AstraVfsSessionSlot sessions[3] = {0};
    AstraVfsOpenFile files[4] = {0};
    AstraVfsBackendOps backends[2];
    pthread_mutex_t core_mutex = PTHREAD_MUTEX_INITIALIZER;
    ScopedIoBindings bindings = {0};
    AstraVfsFile file = 0u, retained = 0u;
    AstraVfsClient *parent = NULL;
    uint32_t grants[2] = {0}, ready = 0u, moved = 0u, used = 0u, flags = 0u;
    uint64_t size = 0u, next = 0u, position = 0u;
    char roots[2][192], paths[2][192], bytes[128];
    const char *stage = "create roots";
    int result = 2;

    join(roots[0], sizeof(roots[0]), directory, "left");
    join(roots[1], sizeof(roots[1]), directory, "right");
    join(paths[0], sizeof(paths[0]), roots[0], "same");
    join(paths[1], sizeof(paths[1]), roots[1], "same");
    if (clients == NULL || page == NULL || mkdir(roots[0], 0755) != 0 || mkdir(roots[1], 0755) != 0 ||
        write_text(paths[0], "left", O_WRONLY | O_CREAT | O_EXCL) != 0 ||
        write_text(paths[1], "right", O_WRONLY | O_CREAT | O_EXCL) != 0) goto done;
    parent = astra_process_vfs_client_handle_lookup(work_grant());
    if (parent == NULL) goto done;
    stage = "derive rooted backends";
    for (uint32_t i = 0u; i < 2u; ++i) {
        AstraVfsDirEntry info = {0};
        if (astra_vfs_grant_derive(parent, roots[i] + 6u,
            ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, &grants[i]) != ASTRA_VFS_OK ||
            astra_vfs_client_connect_service(&clients[i], grants[i]) != ASTRA_VFS_OK ||
            astra_vfs_stat_meta(&clients[i], "/", &info) != ASTRA_VFS_OK) goto done;
        backends[i] = probe_backend_ops;
        if (i == 0u) scoped_transfer_probe.adopt = backends[i].adopt;
        if (backends[i].adopt == NULL || backends[i].adopt != scoped_transfer_probe.adopt) goto done;
        backends[i].adopt = scoped_transfer_adopt;
        bindings.roots[i] = (AstraVfsBackend){&backends[i],
            &clients[i]};
    }
    stage = "bind service";
    if (!astra_vfs_service_init(&core, &probe_backend_ops,
        parent, sessions, 3u, files, 4u) ||
        !astra_vfs_service_set_state_lock(&core, scoped_core_lock, scoped_core_unlock, &core_mutex) ||
        !astra_vfs_service_set_backend_bindings(&core, &ops, &bindings)) goto done;
    ready = 1u;
    if (astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[2], "/same", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
            &file, NULL, NULL) != ASTRA_VFS_OK) goto done;
    stage = "rooted path and file I/O";
    if (bindings.references[0] != 2u ||
        astra_vfs_read(&clients[2], file, 0u, bytes, sizeof(bytes), &moved) != ASTRA_VFS_OK ||
        moved != 4u || memcmp(bytes, "left", 4u) != 0 ||
        astra_vfs_service_read_path(&core, clients[2].session, "/same", bytes,
            sizeof(bytes), &moved, &size) != ASTRA_VFS_OK || moved != 4u || size != 4u ||
        astra_vfs_service_readdir_into(&core, clients[2].session, "/", 0u, 1u,
            (uint8_t *)bytes, sizeof(bytes), &used, &next) != ASTRA_VFS_OK ||
        used != ASTRA_VFS_DIRENT_HEADER + 4u ||
        memcmp(bytes + ASTRA_VFS_DIRENT_HEADER, "same", 4u) != 0) goto done;
    for (uint32_t independent = 1u; ; independent = 0u) {
        stage = independent ? "page fill through independent open during write" :
                              "page fill during descriptor close";
        AstraVfsFile reader = file;
        if (independent && astra_vfs_open(&clients[2], "/same", ASTRA_VFS_OPEN_READ,
            &reader, NULL, NULL) != ASTRA_VFS_OK) goto done;
        if (independent &&
            (astra_vfs_seek(&clients[2], file, 123, 0u, &position) != ASTRA_VFS_OK ||
             astra_vfs_seek(&clients[2], reader, 1, 0u, &position) != ASTRA_VFS_OK ||
             astra_vfs_seek(&clients[2], file, 0, 1u, &position) != ASTRA_VFS_OK || position != 123u ||
             astra_vfs_file_flags(&clients[2], file, ASTRA_VFS_OPEN_APPEND,
                 ASTRA_VFS_OPEN_APPEND, &flags) != ASTRA_VFS_OK ||
             astra_vfs_file_flags(&clients[2], reader, 0u, 0u, &flags) != ASTRA_VFS_OK ||
             (flags & ASTRA_VFS_OPEN_APPEND) != 0u ||
             astra_vfs_file_flags(&clients[2], file, ASTRA_VFS_OPEN_APPEND, 0u, &flags) != ASTRA_VFS_OK))
            goto done;
        if (astra_vfs_service_retain_file(&core, clients[2].session, reader,
                ASTRA_VFS_OPEN_READ, &retained) != ASTRA_VFS_OK) goto done;
        scoped_page_fill.core = &core;
        scoped_page_fill.client = &clients[2];
        scoped_page_fill.retained = retained;
        scoped_page_fill.page = page;
        scoped_page_fill.passed = 0u;
        backends[0].write = scoped_page_fill_write;
        uint32_t page_fill_status = astra_vfs_service_write_from(&core,
            clients[2].session, file, 0u, UINT64_MAX, "l", 1u, &moved);
        backends[0].write = probe_write;
        if (page_fill_status != ASTRA_VFS_OK || moved != 1u || scoped_page_fill.passed != 1u)
            printf("FSSEM PAGE FILL write=%u moved=%u passed=%u refs=%u\n",
                page_fill_status, moved, scoped_page_fill.passed, bindings.references[0]);
        if (page_fill_status != ASTRA_VFS_OK || moved != 1u || scoped_page_fill.passed != 1u ||
            astra_vfs_service_read_page_retained(&core, retained, ASTRA_VFS_OPEN_READ, NULL, 0u,
                page, &moved) != ASTRA_VFS_OK || moved != 4u || memcmp(page, "left", 4u) != 0 ||
            astra_vfs_service_release_retained(&core, &retained) != ASTRA_VFS_OK)
            goto done;
        if (independent == 0u) break;
        if (astra_vfs_seek(&clients[2], file, 0, 1u, &position) != ASTRA_VFS_OK || position != 123u ||
            astra_vfs_seek(&clients[2], file, 0, 0u, &position) != ASTRA_VFS_OK) goto done;
    }
    file = 0u;
    if (astra_vfs_open(&clients[2], "/same", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
            &file, NULL, NULL) != ASTRA_VFS_OK || bindings.references[0] != 2u)
        goto done;
    stage = "scoped description transfer";
    if (scoped_io_shares(&core, file, clients[2].session, &bindings, &backends[0]) != 0) goto done;
    stage = "rebind without redirecting old file";
    bindings.selected = 1u;
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = ASTRA_VFS_VERSION,
        .session = clients[2].session};
    AstraVfsReply reply;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK ||
        astra_vfs_service_read_path(&core, clients[2].session, "/same", bytes,
            sizeof(bytes), &moved, &size) != ASTRA_VFS_OK || moved != 5u ||
        memcmp(bytes, "right", 5u) != 0 ||
        astra_vfs_read(&clients[2], file, 0u, bytes, sizeof(bytes), &moved) != ASTRA_VFS_OK ||
        moved != 4u || memcmp(bytes, "left", 4u) != 0) goto done;
    stage = "retained description flags and cursor";
    request.file = file;
    request.flags = ASTRA_VFS_OPEN_APPEND;
    request.offset = ASTRA_VFS_OPEN_APPEND;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_FILE_FLAGS, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || (reply.open_flags & ASTRA_VFS_OPEN_APPEND) == 0u) goto done;
    request.flags = 0u;
    request.offset = 123u;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.position != 123u) goto done;
    stage = "retain before client close";
    if (astra_vfs_service_retain_file(&core, clients[2].session, file, ASTRA_VFS_OPEN_READ, &retained) != ASTRA_VFS_OK ||
        astra_vfs_close(&clients[2], file) != ASTRA_VFS_OK ||
        astra_vfs_disconnect(&clients[2]) != ASTRA_VFS_OK) goto done;
    stage = "retained backend after session reuse";
    if (astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_service_write_retained(&core, retained, 0u, UINT64_MAX, "X", 1u, &moved) !=
            ASTRA_VFS_OK || moved != 1u ||
        astra_vfs_service_sync_retained(&core, retained, NULL) != ASTRA_VFS_OK ||
        astra_vfs_service_read_retained(&core, retained, 0u, bytes, sizeof(bytes), &moved) !=
            ASTRA_VFS_OK || moved != 4u || memcmp(bytes, "Xeft", 4u) != 0) goto done;
    stage = "retained snapshot writeback";
    AstraVfsNodeInfo expected;
    memset(page, 0x52, ASTRA_MEMORY_PAGE_SIZE);
    memcpy(page, "Yeft", 4u);
    if (astra_vfs_service_stat_retained(&core, retained, &expected) != ASTRA_VFS_OK ||
        astra_vfs_service_write_page_retained(&core, retained, NULL, &expected,
            0u, expected.size, page, &moved, &expected) != ASTRA_VFS_OK || moved != 4u || expected.size != 4u ||
        files[0].backend.ops->seek(files[0].backend.context, files[0].node,
            0, 1u, &position) != ASTRA_VFS_OK || position != 123u ||
        files[0].backend.ops->file_flags(files[0].backend.context, files[0].node,
            0u, 0u, &flags) != ASTRA_VFS_OK || (flags & ASTRA_VFS_OPEN_APPEND) == 0u ||
        astra_vfs_service_read_retained(&core, retained, 0u, bytes, sizeof(bytes), &moved) !=
            ASTRA_VFS_OK || moved != 4u || memcmp(bytes, "Yeft", 4u) != 0) goto done;
    stage = "retain write-only description";
    if (astra_vfs_service_release_retained(&core, &retained) != ASTRA_VFS_OK) goto done;
    bindings.selected = 0u;
    request = (AstraVfsRequest){.size = ASTRA_VFS_REQUEST_SIZE,
        .version = ASTRA_VFS_VERSION, .session = clients[2].session};
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_HELLO, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK ||
        astra_vfs_open(&clients[2], "/same", ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
            &file, NULL, NULL) != ASTRA_VFS_OK) goto done;
    AstraVfsFile denied = ASTRA_VFS_FILE_INVALID;
    if (astra_vfs_service_retain_file(&core, clients[2].session, file,
            ASTRA_VFS_OPEN_READ, &denied) != ASTRA_VFS_ERR_ACCESS || denied != 0u ||
        astra_vfs_service_retain_file(&core, clients[2].session, file,
            ASTRA_VFS_OPEN_WRITE, &retained) != ASTRA_VFS_OK) goto done;
    request.file = file; request.offset = 123u;
    astra_vfs_service_dispatch(&core, ASTRA_VFS_OP_SEEK, &request, NULL, &reply);
    if (reply.status != ASTRA_VFS_OK || reply.position != 123u ||
        astra_vfs_close(&clients[2], file) != ASTRA_VFS_OK ||
        astra_vfs_disconnect(&clients[2]) != ASTRA_VFS_OK) goto done;
    bindings.selected = 1u;
    stage = "write-only backing after session reuse";
    memset(page, 0xa5, ASTRA_MEMORY_PAGE_SIZE);
    if (astra_vfs_connect(&clients[2], astra_vfs_local_transport, &core) != ASTRA_VFS_OK ||
        astra_vfs_service_read_retained(&core, retained, 0u, bytes, sizeof(bytes), &moved) !=
            ASTRA_VFS_ERR_ACCESS || moved != 0u ||
        astra_vfs_service_read_page_retained(&core, retained, ASTRA_VFS_OPEN_READ, NULL, 0u,
            page, &moved) != ASTRA_VFS_ERR_ACCESS || moved != 0u || page[0] != 0xa5u ||
        astra_vfs_service_write_retained(&core, retained, 0u, UINT64_MAX, "Z", 1u, &moved) !=
            ASTRA_VFS_OK || moved != 1u ||
        astra_vfs_service_sync_retained(&core, retained, NULL) != ASTRA_VFS_OK ||
        astra_vfs_service_stat_retained(&core, retained, &expected) != ASTRA_VFS_OK)
        goto done;
    memcpy(page, "Weft", 4u);
    if (astra_vfs_service_write_page_retained(&core, retained, NULL, &expected,
            0u, expected.size, page, &moved, &expected) != ASTRA_VFS_OK || moved != 4u || expected.size != 4u ||
        files[0].backend.ops->seek(files[0].backend.context, files[0].node,
            0, 1u, &position) != ASTRA_VFS_OK || position != 123u ||
        files[0].backend.ops->file_flags(files[0].backend.context, files[0].node,
            0u, 0u, &flags) != ASTRA_VFS_OK || (flags & ASTRA_VFS_OPEN_APPEND) == 0u)
        goto done;
    result = 0;
done:
    if (ready && clients != NULL && clients[2].session != 0u)
        (void)astra_vfs_disconnect(&clients[2]);
    if (ready)
        for (uint32_t i = 0u; i < 3u; ++i)
            if (sessions[i].id != 0u) astra_vfs_service_release_session(&core, sessions[i].id);
    if (retained != 0u) (void)astra_vfs_service_release_retained(&core, &retained);
    for (uint32_t i = 0u; i < 2u; ++i) {
        if (clients != NULL && clients[i].transport != NULL) (void)astra_vfs_disconnect(&clients[i]);
        if (grants[i] != 0u) (void)astra_close(grants[i]);
    }
    if (result == 0 && (bindings.failed || bindings.references[0] != 0u || bindings.references[1] != 0u ||
        read_text(paths[0], bytes, sizeof(bytes)) != 4 || memcmp(bytes, "Weft", 4u) != 0 ||
        read_text(paths[1], bytes, sizeof(bytes)) != 5 || memcmp(bytes, "right", 5u) != 0)) {
        result = 2; stage = "exact backing and lifetime";
    }
    (void)unlink(paths[0]); (void)unlink(paths[1]);
    (void)rmdir(roots[0]); (void)rmdir(roots[1]);
    free(clients);
    free(page);
    (void)pthread_mutex_destroy(&core_mutex);
    return result == 0 ? 0 : fail("scoped-io %s", stage);
}

typedef struct FileAreaRace {
    HeldLane lane;
    const char *path;
    uint32_t area;
} FileAreaRace;

static void *file_area_race_thread(void *argument)
{
    FileAreaRace *race = argument;
    AstraVfsClient *client = calloc(1u, sizeof(*client));
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint32_t status = client == NULL ? ASTRA_VFS_ERR_LIMIT :
        astra_vfs_client_connect_service(client, work_grant());
    int connected = status == ASTRA_VFS_OK;
    if (connected)
        status = astra_vfs_open(client, race->path, ASTRA_VFS_OPEN_READ,
            &file, NULL, NULL);
    race->lane.first = status;
    held_stage(&race->lane, 1);
    held_wait(&race->lane, 2);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_file_area(client, file, 0u, ASTRA_MEMORY_PAGE_SIZE,
            &race->area, &size);
    if (status == ASTRA_VFS_OK && size != 1u) status = ASTRA_VFS_ERR_IO;
    if (file != ASTRA_VFS_FILE_INVALID) {
        uint32_t closed = astra_vfs_close(client, file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (connected) {
        uint32_t closed = astra_vfs_disconnect(client);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    free(client);
    race->lane.last = status;
    return NULL;
}

static int case_map_publication(const char *directory)
{
    for (unsigned round = 0u; round < 8u; ++round) {
        char path[192];
        (void)snprintf(path, sizeof(path), "%s/publish-%u", directory, round);
        CHECK(write_text(path, "x", O_WRONLY | O_CREAT | O_EXCL) == 0,
            "seed map publication %u", round);
        AstraVfsClient *client = calloc(1u, sizeof(*client));
        CHECK(client != NULL, "map publication client");
        AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
        uint32_t area = 0u;
        uint64_t size = 0u;
        FileAreaRace race = {.path = path + 5u};
        pthread_t thread;
        int mutex_ready = 0, cond_ready = 0, shared = 0;
        uint32_t status = astra_vfs_client_connect_service(client, work_grant());
        int connected = status == ASTRA_VFS_OK;
        if (connected)
            status = astra_vfs_open(client, race.path, ASTRA_VFS_OPEN_READ,
                &file, NULL, NULL);
        if (status == ASTRA_VFS_OK) {
            mutex_ready = pthread_mutex_init(&race.lane.mutex, NULL) == 0;
            cond_ready = mutex_ready && pthread_cond_init(&race.lane.changed, NULL) == 0;
            if (!cond_ready || pthread_create(&thread, NULL, file_area_race_thread, &race) != 0)
                status = ASTRA_VFS_ERR_IO;
            else {
                held_wait(&race.lane, 1);
                held_stage(&race.lane, 2);
                status = astra_vfs_file_area(client, file, 0u, ASTRA_MEMORY_PAGE_SIZE,
                    &area, &size);
                if (pthread_join(thread, NULL) != 0) status = ASTRA_VFS_ERR_IO;
                if (status == ASTRA_VFS_OK && race.lane.first == ASTRA_VFS_OK &&
                    race.lane.last == ASTRA_VFS_OK && size == 1u) {
                    char byte = 0;
                    /* Fill only the first handle: the second must already share its page. */
                    shared = astra_rt_area_prefetch(area, 0u) == ASTRA_SYSCALL_OK &&
                        astra_rt_area_read_resident(race.area, 0u, &byte, 1u) == ASTRA_SYSCALL_OK &&
                        byte == 'x';
                }
            }
        }
        if (cond_ready) (void)pthread_cond_destroy(&race.lane.changed);
        if (mutex_ready) (void)pthread_mutex_destroy(&race.lane.mutex);
        if (race.area != 0u && astra_close(race.area) != ASTRA_SYSCALL_OK) shared = 0;
        if (area != 0u && astra_close(area) != ASTRA_SYSCALL_OK) shared = 0;
        if (file != ASTRA_VFS_FILE_INVALID && astra_vfs_close(client, file) != ASTRA_VFS_OK) shared = 0;
        if (connected && astra_vfs_disconnect(client) != ASTRA_VFS_OK) shared = 0;
        free(client);
        if (unlink(path) != 0) shared = 0;
        CHECK(shared, "map publication %u: main=%u worker-open=%u worker-map=%u",
            round, status, race.lane.first, race.lane.last);
    }
    return 0;
}

static int file_map_gate(const char *directory)
{
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile file = ASTRA_FILE_INIT;
    char path[192];
    uint32_t area = 0u, fresh_area = 0u, mapped_size = 0u;
    uint64_t file_size = 0u;
    void *mapping = NULL, *fresh_mapping = NULL, *posix_mapping = MAP_FAILED;
    void *posix_alias = MAP_FAILED, *cross_mapping = MAP_FAILED;
    void *private_mapping = NULL, *private_alias = NULL;
    size_t posix_mapping_length = ASTRA_MEMORY_PAGE_SIZE * 2u;
    int fd = -1, result = 2;
    const char *stage = "create";

    if (snprintf(path, sizeof(path), "%s/fssem-map-%ld", directory,
                 (long)getpid()) >= (int)sizeof(path))
        return fail("file-map path too long");
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) goto done;
    char bytes[ASTRA_MEMORY_PAGE_SIZE * 2u];
    memset(bytes, 'a', sizeof(bytes));
    stage = "write";
    if (write(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) ||
        fsync(fd) != 0) goto done;
    close(fd); fd = -1;
    stage = "POSIX open";
    fd = open(path, O_RDONLY);
    if (fd < 0) goto done;
    stage = "POSIX reject writable map";
    errno = 0;
    void *rejected = mmap(NULL, sizeof(bytes), PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    if (rejected != MAP_FAILED) {
        (void)munmap(rejected, sizeof(bytes));
        goto done;
    }
    if (errno != EACCES) goto done;
    stage = "POSIX readonly map";
    posix_mapping = mmap(NULL, sizeof(bytes), PROT_READ, MAP_SHARED, fd, 0);
    if (posix_mapping == MAP_FAILED) goto done;
    stage = "POSIX independent mappings";
    posix_alias = mmap(NULL, sizeof(bytes), PROT_READ, MAP_SHARED, fd, 0);
    if (posix_alias == MAP_FAILED || posix_alias == posix_mapping ||
        ((volatile const char *)posix_alias)[0] != 'a') goto done;
    if (munmap(posix_mapping, sizeof(bytes)) != 0 ||
        ((volatile const char *)posix_alias)[0] != 'a') goto done;
    posix_mapping = posix_alias;
    posix_alias = MAP_FAILED;
    close(fd); fd = -1;
    if (((volatile const char *)posix_mapping)[0] != 'a' ||
        ((volatile const char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'a')
        goto done;
    stage = "POSIX partial unmap";
    if (munmap(posix_mapping, ASTRA_MEMORY_PAGE_SIZE) != 0 ||
        ((volatile const char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'a') goto done;
    stage = "POSIX unmap";
    if (munmap(posix_mapping, sizeof(bytes)) != 0) goto done;
    posix_mapping = MAP_FAILED;
    stage = "POSIX private map";
    fd = open(path, O_RDONLY);
    if (fd < 0) goto done;
    posix_mapping = mmap(NULL, ASTRA_MEMORY_PAGE_SIZE, PROT_READ,
                         MAP_PRIVATE, fd, 0);
    if (posix_mapping == MAP_FAILED) goto done;
    posix_mapping_length = ASTRA_MEMORY_PAGE_SIZE;
    close(fd); fd = -1;
    if (((volatile const char *)posix_mapping)[0] != 'a' ||
        munmap(posix_mapping, ASTRA_MEMORY_PAGE_SIZE) != 0) goto done;
    posix_mapping = MAP_FAILED;
    stage = "POSIX writable private map from read-only file";
    fd = open(path, O_RDONLY);
    if (fd < 0) goto done;
    posix_mapping = mmap(NULL, sizeof(bytes), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE, fd, 0);
    if (posix_mapping == MAP_FAILED) goto done;
    posix_mapping_length = sizeof(bytes);
    ((volatile char *)posix_mapping)[0] = 'v';
    ((volatile char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] = 'w';
    close(fd); fd = -1;
    stage = "POSIX private fork after descriptor close";
    pid_t posix_child = fork();
    if (posix_child < 0) goto done;
    if (posix_child == 0) {
        int valid = ((volatile char *)posix_mapping)[0] == 'v' &&
            ((volatile char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] == 'w';
        ((volatile char *)posix_mapping)[0] = 'c';
        ((volatile char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] = 'd';
        _exit(valid && ((volatile char *)posix_mapping)[0] == 'c' &&
            ((volatile char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] == 'd' ? 0 : 1);
    }
    int posix_status = 0;
    if (waitpid(posix_child, &posix_status, 0) != posix_child ||
        !WIFEXITED(posix_status) || WEXITSTATUS(posix_status) != 0 ||
        ((volatile char *)posix_mapping)[0] != 'v' ||
        ((volatile char *)posix_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'w') goto done;
    stage = "POSIX private writes preserve file";
    fd = open(path, O_RDONLY);
    char original[2];
    if (fd < 0 || pread(fd, original, 1u, 0) != 1 || original[0] != 'a' ||
        pread(fd, original + 1, 1u, ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        original[1] != 'a' || munmap(posix_mapping, sizeof(bytes)) != 0) goto done;
    posix_mapping = MAP_FAILED;
    stage = "POSIX write-only private protection";
    posix_mapping = mmap(NULL, ASTRA_MEMORY_PAGE_SIZE, PROT_WRITE,
                         MAP_PRIVATE, fd, 0);
    if (posix_mapping == MAP_FAILED) goto done;
    posix_mapping_length = ASTRA_MEMORY_PAGE_SIZE;
    ((volatile char *)posix_mapping)[0] = 'j';
    if (pread(fd, original, 1u, 0) != 1 || original[0] != 'a' ||
        munmap(posix_mapping, ASTRA_MEMORY_PAGE_SIZE) != 0) goto done;
    posix_mapping = MAP_FAILED;
    close(fd); fd = -1;
    stage = "POSIX private map requires file read access";
    fd = open(path, O_WRONLY);
    if (fd < 0) goto done;
    errno = 0;
    rejected = mmap(NULL, ASTRA_MEMORY_PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE, fd, 0);
    if (rejected != MAP_FAILED) {
        (void)munmap(rejected, ASTRA_MEMORY_PAGE_SIZE);
        goto done;
    }
    if (errno != EACCES) goto done;
    close(fd); fd = -1;
    printf("FSSEM POSIX PRIVATE readonly-file writable-copy descriptor-close fork isolation PASS\n");
    stage = "attach";
    if (astra_process_filesystem_open(&native, astra_posix_startup()) !=
        ASTRA_VFS_OK) goto done;
    stage = "write-only area refusal";
    if (astra_filesystem_open(&native.filesystem, path,
        ASTRA_VFS_OPEN_WRITE, &file) != ASTRA_VFS_OK) goto done;
    uint32_t area_status = astra_filesystem_file_area(&file, 0u,
        sizeof(bytes), &area, &file_size);
    if (area_status != ASTRA_VFS_ERR_ACCESS || area != 0u) {
        fprintf(stderr, "FSSEM write-only area status %u handle %u\n",
                (unsigned)area_status, (unsigned)area);
        goto done;
    }
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK) goto done;
    stage = "open";
    if (astra_filesystem_open(&native.filesystem, path,
        ASTRA_VFS_OPEN_READ, &file) != ASTRA_VFS_OK) goto done;
    stage = "area";
    if (astra_filesystem_file_area(&file, 0u, sizeof(bytes),
        &area, &file_size) != ASTRA_VFS_OK || file_size != sizeof(bytes)) goto done;
    stage = "ordinary read retains canonical page";
    fd = open(path, O_RDWR);
    if (fd < 0 || pwrite(fd, "a", 1u, 0u) != 1 || fsync(fd) != 0 ||
        !file_map_cached_page(area, 0u, fd, 'a',
            FILE_MAP_POSITIONED, &native, path)) goto done;
    close(fd); fd = -1;
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK) goto done;
    stage = "map";
    if (astra_rt_area_map_range_at(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE | ASTRA_AREA_MAP_PRIVATE,
            0u, ASTRA_SHARED_AREA_TABLE_SPAN,
            ASTRA_SHARED_AREA_ADDRESS_END - 2u * ASTRA_SHARED_AREA_TABLE_SPAN,
            &private_mapping, &mapped_size) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map_range_at(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE | ASTRA_AREA_MAP_PRIVATE,
            0u, ASTRA_SHARED_AREA_TABLE_SPAN,
            ASTRA_SHARED_AREA_ADDRESS_END - ASTRA_SHARED_AREA_TABLE_SPAN,
            &private_alias, &mapped_size) != ASTRA_SYSCALL_OK)
        goto done;
    stage = "private native write after descriptor close";
    if (((volatile char *)private_mapping)[0] != 'a' ||
        ((volatile char *)private_alias)[0] != 'a') goto done;
    ((volatile char *)private_mapping)[0] = 'p';
    if (((volatile char *)private_alias)[0] != 'a') goto done;
    stage = "private fork isolation";
    pid_t private_child = fork();
    if (private_child < 0) {
        printf("FSSEM PRIVATE fork errno=%d\n", errno);
        goto done;
    }
    if (private_child == 0) {
        int valid = ((volatile char *)private_mapping)[0] == 'p';
        ((volatile char *)private_mapping)[0] = 'c';
        ((volatile char *)private_alias)[0] = 'd';
        _exit(valid && ((volatile char *)private_mapping)[0] == 'c' &&
            ((volatile char *)private_alias)[0] == 'd' ? 0 : 1);
    }
    int private_status = 0;
    pid_t private_wait = waitpid(private_child, &private_status, 0);
    if (private_wait != private_child ||
        !WIFEXITED(private_status) || WEXITSTATUS(private_status) != 0 ||
        ((volatile char *)private_mapping)[0] != 'p' ||
        ((volatile char *)private_alias)[0] != 'a') {
        printf("FSSEM PRIVATE child=%ld wait=%ld status=%x errno=%d bytes=%x/%x\n",
            (long)private_child, (long)private_wait, private_status, errno,
            (unsigned char)((volatile char *)private_mapping)[0],
            (unsigned char)((volatile char *)private_alias)[0]);
        goto done;
    }
    stage = "shared view beside private views";
    if (astra_rt_area_map_range(area, ASTRA_AREA_MAP_READ, 0u,
        ASTRA_SHARED_AREA_TABLE_SPAN, &mapping, &mapped_size) !=
        ASTRA_SYSCALL_OK || mapped_size != ASTRA_SHARED_AREA_TABLE_SPAN)
        goto done;
    stage = "fault";
    if (((volatile const char *)mapping)[0] != 'a' ||
        ((volatile const char *)mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'a')
        goto done;
    stage = "resident ordinary read";
    fd = open(path, O_RDONLY);
    char observed = 0;
    if (fd < 0 || pread(fd, &observed, 1u, ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        observed != 'a') goto done;
    close(fd); fd = -1;
    stage = "forked write invalidates parent mapping";
    fd = open(path, O_RDWR);
    if (fd < 0) goto done;
    stage = "owned EOF across fork";
    ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE] = 'Q';
    ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE + 9u] = 'T';
    pid_t writer = fork();
    if (writer < 0) goto done;
    if (writer == 0) {
        int written = pwrite(fd, "F", 1u, 0) == 1 && fsync(fd) == 0;
        ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE] = 'q';
        ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE + 9u] = 't';
        written = written && ftruncate(fd, ASTRA_MEMORY_PAGE_SIZE + 9u) == 0 &&
            fsync(fd) == 0 &&
            ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE] == 'q' &&
            ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE + 9u] == 0 &&
            ftruncate(fd, 1u) == 0 && fsync(fd) == 0 &&
            pwrite(fd, "g", 1u, ASTRA_MEMORY_PAGE_SIZE) == 1 && fsync(fd) == 0 &&
            ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE] == 'g' &&
            ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE + 1u] == 0 &&
            ((volatile char *)private_mapping)[0] == 'p' &&
            ftruncate(fd, sizeof(bytes)) == 0 && fsync(fd) == 0;
        (void)close(fd);
        _exit(written ? 0 : 1);
    }
    int writer_status = 0;
    if (waitpid(writer, &writer_status, 0) != writer ||
        !WIFEXITED(writer_status) || WEXITSTATUS(writer_status) != 0 ||
        ((volatile const char *)mapping)[0] != 'F' ||
        ((volatile char *)private_mapping)[0] != 'p' ||
        ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'g' ||
        ((volatile char *)private_mapping)[ASTRA_MEMORY_PAGE_SIZE + 1u] != 0) {
        printf("FSSEM MAPPED EOF writer=%ld status=%x errno=%d\n",
            (long)writer, writer_status, errno);
        goto done;
    }
    puts("FSSEM MAPPED EOF owned-truncate private-fork tail-zero growth PASS");
    close(fd); fd = -1;
    stage = "mutation";
    fd = open(path, O_WRONLY);
    if (fd < 0 || pwrite(fd, "b", 1u, 0) != 1 || fsync(fd) != 0)
        goto done;
    close(fd); fd = -1;
    stage = "write immediate invalidation";
    if (((volatile const char *)mapping)[0] != 'b') goto done;
    stage = "private copy survives source invalidation";
    if (((volatile char *)private_mapping)[0] != 'p' ||
        ((volatile char *)private_alias)[0] != 'b' ||
        astra_rt_area_unmap(private_mapping) != ASTRA_SYSCALL_OK)
        goto done;
    private_mapping = NULL;
    if (astra_rt_area_unmap(private_alias) != ASTRA_SYSCALL_OK) goto done;
    private_alias = NULL;
    puts("FSSEM PRIVATE MAP native read-grant descriptor-close source-isolation fork reclaim PASS");
    stage = "fresh open";
    if (astra_filesystem_open(&native.filesystem, path,
        ASTRA_VFS_OPEN_READ, &file) != ASTRA_VFS_OK) goto done;
    stage = "fresh area";
    if (astra_filesystem_file_area(&file, 0u, sizeof(bytes),
        &fresh_area, &file_size) != ASTRA_VFS_OK ||
        file_size != sizeof(bytes)) goto done;
    if (astra_filesystem_close(&file) != ASTRA_VFS_OK) goto done;
    stage = "fresh map";
    if (astra_rt_area_map_range(fresh_area, ASTRA_AREA_MAP_READ, 0u,
        ASTRA_SHARED_AREA_TABLE_SPAN, &fresh_mapping, &mapped_size) !=
        ASTRA_SYSCALL_OK || mapped_size != ASTRA_SHARED_AREA_TABLE_SPAN)
        goto done;
    stage = "fresh contents";
    if (((volatile const char *)fresh_mapping)[0] != 'b') goto done;
    stage = "invalidate";
    if (!wait_mapped_byte(mapping, 0u, 'b')) goto done;
    stage = "truncate";
    fd = open(path, O_WRONLY);
    if (fd < 0 || ftruncate(fd, 1) != 0 || fsync(fd) != 0)
        goto done;
    close(fd); fd = -1;
    stage = "truncate immediate invalidation";
    if (((volatile const char *)mapping)[1] != '\0') goto done;
    stage = "zero-tail";
    if (!wait_mapped_byte(mapping, 1u, '\0')) goto done;
    stage = "extend";
    fd = open(path, O_WRONLY);
    if (fd < 0 || pwrite(fd, "c", 1u, 1) != 1 || fsync(fd) != 0)
        goto done;
    close(fd); fd = -1;
    stage = "refault";
    if (!wait_mapped_byte(mapping, 1u, 'c')) goto done;
    stage = "canonical page write";
    fd = open(path, O_RDWR);
    if (fd < 0 || pwrite(fd, "P", 1u, 2u * ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        fsync(fd) != 0) goto done;
    stage = "ordinary read fills canonical page";
    if (!file_map_cached_page(area, 2u * ASTRA_MEMORY_PAGE_SIZE,
        fd, 'P', FILE_MAP_POSITIONED, &native, path)) goto done;
    stage = "borrowed read fills canonical page";
    if (pwrite(fd, "B", 1u, 2u * ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        fsync(fd) != 0 || !file_map_cached_page(area,
            2u * ASTRA_MEMORY_PAGE_SIZE, fd, 'B', FILE_MAP_BORROWED, &native, path))
        goto done;
    stage = "path read fills canonical page";
    if (pwrite(fd, "H", 1u, 2u * ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        fsync(fd) != 0 || !file_map_cached_page(area,
            2u * ASTRA_MEMORY_PAGE_SIZE, fd, 'H', FILE_MAP_PATH, &native, path)) goto done;
    stage = "sequential read fills canonical page";
    if (pwrite(fd, "S", 1u, 2u * ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        fsync(fd) != 0 || !file_map_cached_page(area,
            2u * ASTRA_MEMORY_PAGE_SIZE, fd, 'S', FILE_MAP_SEQUENTIAL, &native, path)) goto done;
    close(fd); fd = -1;
    stage = "cross-chunk file map";
    fd = open(path, O_RDWR);
    if (fd < 0 || pwrite(fd, "R", 1u, ASTRA_AREA_SIZE_MAX) != 1 ||
        ((volatile const char *)mapping)[3u * ASTRA_MEMORY_PAGE_SIZE] != 0 ||
        pwrite(fd, "L", 1u, ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        fsync(fd) != 0) goto done;
    cross_mapping = mmap(NULL, 2u * ASTRA_MEMORY_PAGE_SIZE,
        PROT_READ, MAP_SHARED, fd,
        ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE);
    if (cross_mapping == MAP_FAILED ||
        ((volatile const char *)cross_mapping)[0] != 'L' ||
        ((volatile const char *)cross_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'R')
        goto done;
    char boundary[2];
    if (pread(fd, boundary, sizeof(boundary), ASTRA_AREA_SIZE_MAX - 1u) != 2 ||
        boundary[0] != '\0' || boundary[1] != 'R' ||
        munmap(cross_mapping, 2u * ASTRA_MEMORY_PAGE_SIZE) != 0) goto done;
    cross_mapping = MAP_FAILED;
    stage = "cross-chunk private copies";
    cross_mapping = mmap(NULL, 2u * ASTRA_MEMORY_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
        ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE);
    if (cross_mapping == MAP_FAILED ||
        ((volatile char *)cross_mapping)[0] != 'L' ||
        ((volatile char *)cross_mapping)[ASTRA_MEMORY_PAGE_SIZE] != 'R') goto done;
    ((volatile char *)cross_mapping)[0] = 'l';
    ((volatile char *)cross_mapping)[ASTRA_MEMORY_PAGE_SIZE] = 'r';
    if (pread(fd, boundary, 1u, ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE) != 1 ||
        boundary[0] != 'L' || pread(fd, boundary + 1, 1u, ASTRA_AREA_SIZE_MAX) != 1 ||
        boundary[1] != 'R' || munmap(cross_mapping, 2u * ASTRA_MEMORY_PAGE_SIZE) != 0)
        goto done;
    cross_mapping = MAP_FAILED;
    printf("FSSEM POSIX PRIVATE cross-chunk canonical-isolation PASS\n");
    close(fd); fd = -1;
    result = 0;
done:
    if (fd >= 0) close(fd);
    if (private_mapping != NULL) (void)astra_rt_area_unmap(private_mapping);
    if (private_alias != NULL) (void)astra_rt_area_unmap(private_alias);
    if (cross_mapping != MAP_FAILED)
        (void)munmap(cross_mapping, 2u * ASTRA_MEMORY_PAGE_SIZE);
    if (posix_alias != MAP_FAILED) (void)munmap(posix_alias, sizeof(bytes));
    if (posix_mapping != MAP_FAILED)
        (void)munmap(posix_mapping, posix_mapping_length);
    if (fresh_mapping != NULL) (void)astra_rt_area_unmap(fresh_mapping);
    if (fresh_area != 0u) (void)astra_close(fresh_area);
    if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
    if (area != 0u) (void)astra_close(area);
    (void)astra_filesystem_close(&file);
    astra_process_filesystem_close(&native);
    if (unlink(path) != 0 && errno != ENOENT && result == 0) {
        stage = "cleanup file";
        result = 2;
    }
    if (rmdir(directory) != 0 && result == 0) {
        stage = "cleanup directory";
        result = 2;
    }
    return result == 0 ? 0 : fail("file-map %s", stage);
}

static int wait_control_file(const char *path)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(30000000000);
    struct stat info;
    for (;;) {
        if (stat(path, &info) == 0) return S_ISREG(info.st_mode);
        if (errno != ENOENT || astra_clock_monotonic() >= deadline) return 0;
        (void)astra_rt_thread_sleep(UINT64_C(10000000),
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
}

static int external_map_probe(const char *directory)
{
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile file = ASTRA_FILE_INIT;
    char path[192];
    uint32_t area = 0u, mapped_size = 0u;
    uint64_t file_size = 0u;
    void *view = NULL;
    volatile char *bytes = NULL;
    pid_t child = -1;
    int result = 1, status = 0;
    const char *stage = "attach";

    if (strlen(directory) > sizeof(path) - 16u) return 1;
    join(path, sizeof(path), directory, "data");
    if (astra_process_filesystem_open(&native, astra_posix_startup()) !=
            ASTRA_VFS_OK ||
        astra_filesystem_open(&native.filesystem, path, ASTRA_VFS_OPEN_READ,
            &file) != ASTRA_VFS_OK ||
        astra_filesystem_file_area(&file, 0u, 2u * ASTRA_MEMORY_PAGE_SIZE,
            &area, &file_size) != ASTRA_VFS_OK ||
        file_size != 2u * ASTRA_MEMORY_PAGE_SIZE ||
        astra_filesystem_close(&file) != ASTRA_VFS_OK ||
        astra_rt_area_map_range(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE | ASTRA_AREA_MAP_PRIVATE,
            0u, ASTRA_SHARED_AREA_TABLE_SPAN, &view, &mapped_size) !=
            ASTRA_SYSCALL_OK)
        goto done;
    bytes = view;
    stage = "private copies";
    if (bytes[0] != 'a' || bytes[ASTRA_MEMORY_PAGE_SIZE] != 'a') goto done;
    bytes[0] = 'p';
    bytes[ASTRA_MEMORY_PAGE_SIZE] = 'Q';
    bytes[ASTRA_MEMORY_PAGE_SIZE + 9u] = 'T';
    join(path, sizeof(path), directory, "ready");
    stage = "external partial tail";
    if (write_text(path, "", O_WRONLY | O_CREAT | O_EXCL) != 0 ||
        !wait_mapped_byte(bytes, ASTRA_MEMORY_PAGE_SIZE + 9u, 0) ||
        bytes[ASTRA_MEMORY_PAGE_SIZE] != 'Q' || bytes[0] != 'p') goto done;
    stage = "external full-page EOF";
    join(path, sizeof(path), directory, "partial");
    if (write_text(path, "", O_WRONLY | O_CREAT | O_EXCL) != 0) goto done;
    join(path, sizeof(path), directory, "partial.done");
    if (!wait_control_file(path)) goto done;
    child = fork();
    if (child < 0) goto done;
    if (child == 0) {
        uint64_t deadline = astra_clock_monotonic() + UINT64_C(4000000000);
        while (bytes[ASTRA_MEMORY_PAGE_SIZE] == 'Q' &&
               astra_clock_monotonic() < deadline)
            (void)astra_rt_thread_sleep(UINT64_C(10000000),
                ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
        _exit(1);
    }
    if (waitpid(child, &status, 0) != child) goto done;
    child = -1;
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGBUS) goto done;
    join(path, sizeof(path), directory, "eof");
    stage = "external growth";
    if (write_text(path, "", O_WRONLY | O_CREAT | O_EXCL) != 0) goto done;
    join(path, sizeof(path), directory, "eof.done");
    if (!wait_control_file(path)) goto done;
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(4000000000);
    uint32_t prefetched;
    do {
        prefetched = astra_rt_area_prefetch(area, ASTRA_MEMORY_PAGE_SIZE);
        if (prefetched == ASTRA_SYSCALL_OK) break;
        (void)astra_rt_thread_sleep(UINT64_C(10000000),
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    } while (astra_clock_monotonic() < deadline);
    if (prefetched != ASTRA_SYSCALL_OK || bytes[ASTRA_MEMORY_PAGE_SIZE] != 'g' ||
        bytes[ASTRA_MEMORY_PAGE_SIZE + 1u] != 0 || bytes[0] != 'p') goto done;
    result = 0;
done:
    if (child > 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, &status, 0);
    }
    if (view != NULL) (void)astra_rt_area_unmap(view);
    if (area != 0u) (void)astra_close(area);
    (void)astra_filesystem_close(&file);
    astra_process_filesystem_close(&native);
    if (result == 0)
        printf("FSSEM EXTERNAL MAP idle-owner partial-tail full-eof growth PASS\n");
    else
        printf("FSSEM EXTERNAL MAP FAIL %s status=%x errno=%d\n", stage, status, errno);
    return result;
}

static int area_range_gate(const char *directory)
{
    const uint32_t span = ASTRA_SHARED_AREA_TABLE_SPAN;
    uint32_t target = 0u;
    uint32_t area = 0u, size = 0u, released = 0u;
    void *tail = NULL, *head = NULL, *whole = NULL;
    void *at_head = NULL, *at_tail = NULL, *collision = NULL;
    const char *stage = "create";
    int result = 2;

    if (astra_rt_area_create_flagged(span + ASTRA_MEMORY_PAGE_SIZE,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK)
        goto done;
    stage = "reject unaligned offset";
    if (astra_rt_area_map_range(area, ASTRA_AREA_MAP_READ, 1u,
            ASTRA_MEMORY_PAGE_SIZE, &tail, &size) !=
            ASTRA_SYSCALL_INVALID_ARGUMENT || tail != NULL)
        goto done;
    stage = "map terminal page";
    if (astra_rt_area_map_range(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, span,
            ASTRA_MEMORY_PAGE_SIZE, &tail, &size) != ASTRA_SYSCALL_OK ||
        size != ASTRA_MEMORY_PAGE_SIZE)
        goto done;
    stage = "map first span";
    if (astra_rt_area_map_range(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, 0u, span,
            &head, &size) != ASTRA_SYSCALL_OK || size != span)
        goto done;
    stage = "map overlapping view";
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &whole, &size) != ASTRA_SYSCALL_OK ||
        size != span + ASTRA_MEMORY_PAGE_SIZE)
        goto done;
    stage = "release exact-view slot";
    target = (uint32_t)(uintptr_t)whole;
    if (astra_rt_area_unmap(whole) != ASTRA_SYSCALL_OK) goto done;
    whole = NULL;
    stage = "map adjacent exact views";
    if (astra_rt_area_map_range_at(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, 0u, span,
            target, &at_head, &size) != ASTRA_SYSCALL_OK ||
        (uintptr_t)at_head != target || size != span ||
        astra_rt_area_map_range_at(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, span,
            ASTRA_MEMORY_PAGE_SIZE, target + span, &at_tail,
            &size) != ASTRA_SYSCALL_OK ||
        (uintptr_t)at_tail != target + span ||
        size != ASTRA_MEMORY_PAGE_SIZE)
        goto done;
    stage = "remap whole view";
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            &whole, &size) != ASTRA_SYSCALL_OK ||
        size != span + ASTRA_MEMORY_PAGE_SIZE)
        goto done;
    stage = "reject occupied target";
    if (astra_rt_area_map_range_at(area,
            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE, 0u,
            span, target, &collision, &size) !=
            ASTRA_SYSCALL_INVALID_ARGUMENT || collision != NULL)
        goto done;
    if (astra_rt_area_map_range_at(area, ASTRA_AREA_MAP_READ, 0u,
            span, target + span, &collision, &size) !=
            ASTRA_SYSCALL_INVALID_ARGUMENT || collision != NULL)
        goto done;
    stage = "shared frames";
    ((volatile uint8_t *)tail)[0] = 0x5au;
    ((volatile uint8_t *)head)[0] = 0x33u;
    if (((volatile uint8_t *)whole)[span] != 0x5au ||
        ((volatile uint8_t *)whole)[0] != 0x33u ||
        ((volatile uint8_t *)at_tail)[0] != 0x5au ||
        ((volatile uint8_t *)at_head)[0] != 0x33u)
        goto done;
    stage = "decommit and refault";
    if (astra_rt_area_decommit(tail, ASTRA_MEMORY_PAGE_SIZE, &released) !=
            ASTRA_SYSCALL_OK || released != 1u ||
        ((volatile uint8_t *)tail)[0] != 0u ||
        ((volatile uint8_t *)whole)[span] != 0u ||
        ((volatile uint8_t *)at_tail)[0] != 0u)
        goto done;
    result = 0;
done:
    if (at_tail != NULL) (void)astra_rt_area_unmap(at_tail);
    if (at_head != NULL) (void)astra_rt_area_unmap(at_head);
    if (whole != NULL) (void)astra_rt_area_unmap(whole);
    if (head != NULL) (void)astra_rt_area_unmap(head);
    if (tail != NULL) (void)astra_rt_area_unmap(tail);
    if (area != 0u) (void)astra_close(area);
    if (rmdir(directory) != 0 && result == 0) {
        stage = "cleanup directory";
        result = 2;
    }
    return result == 0 ? 0 : fail("area-range %s", stage);
}

static int flood(const char *directory, uint32_t limit, int wait)
{
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile *held;
    AstraFile extra = ASTRA_FILE_INIT;
    AstraFileInfo metadata = ASTRA_FILE_INFO_INIT;
    char path[192], absent[192], go[192], text[32];
    uint32_t count = 0u, status;
    int result = 0, child_status;
    pid_t child;

    if (limit == 0u || limit > 65536u)
        return fail("invalid flood limit");
    /* Fill every native slot; the virtual root retains no CWD description. */
    if (chdir("/") != 0)
        return fail("flood virtual CWD");
    held = calloc(limit + 1u, sizeof(*held));
    if (held == NULL)
        return fail("flood allocation");
    join(path, sizeof(path), directory, "held");
    join(absent, sizeof(absent), directory, "must-not-exist");
    join(go, sizeof(go), directory, "go");
    if (write_text(path, "unchanged", O_WRONLY | O_CREAT | O_EXCL) != 0 ||
        astra_process_filesystem_open(&native, astra_posix_startup()) != ASTRA_VFS_OK) {
        result = fail("flood fixture");
        goto done;
    }
    while (count <= limit) {
        status = astra_filesystem_open(&native.filesystem, path,
            ASTRA_VFS_OPEN_READ, &held[count]);
        if (status != ASTRA_VFS_OK)
            break;
        ++count;
    }
    if (count != limit || status != ASTRA_VFS_ERR_LIMIT) {
        result = fail("file quota accepted %u status %u expected %u", count, status, limit);
        goto done;
    }
    if (astra_filesystem_open_mode(&native.filesystem, absent,
            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, 0644u, &extra) != ASTRA_VFS_ERR_LIMIT ||
        astra_filesystem_open(&native.filesystem, path,
            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_TRUNCATE, &extra) != ASTRA_VFS_ERR_LIMIT ||
        astra_filesystem_stat(&native.filesystem, path, &metadata) != ASTRA_VFS_OK ||
        metadata.byte_size != 9u ||
        astra_filesystem_stat(&native.filesystem, absent, &metadata) != ASTRA_VFS_ERR_NOT_FOUND) {
        result = fail("quota refusal mutated filesystem");
        goto done;
    }
    child = fork();
    if (child == 0) {
        char contents[16];
        _exit(read_text(path, contents, sizeof(contents)) == 9 ? 0 : 1);
    }
    if (child < 0 || waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
        result = fail("file quota blocked another owner");
        goto done;
    }
    if (wait) {
        uint64_t deadline = astra_clock_monotonic() + UINT64_C(60000000000);
        struct stat observed;

        printf("FSSEM FLOOD HOLD count=%u\n", count);
        fflush(stdout);
        while (stat(go, &observed) != 0 && astra_clock_monotonic() < deadline) {
            struct timespec pause = {0, 50000000L};
            (void)nanosleep(&pause, NULL);
        }
        if (stat(go, &observed) != 0 ||
            astra_filesystem_file_info(&held[0], &metadata) != ASTRA_VFS_ERR_LOST)
            result = fail("full-quota reset did not preserve lost identity");
    }
done:
    while (count != 0u) {
        uint32_t closed = astra_filesystem_close(&held[--count]);
        if (closed != ASTRA_VFS_OK && result == 0)
            result = fail("flood close index=%u status=%u", count, closed);
    }
    if (native.filesystem._private_assigns != NULL &&
        (read_text(path, text, sizeof(text)) != 9 || strcmp(text, "unchanged") != 0))
        result = fail("flood recovery read");
    astra_process_filesystem_close(&native);
    free(held);
    return result;
}

static int case_owner_files(const char *directory)
{
    return flood(directory, 1024u, 0);
}

static int hold_sessions(uint32_t count)
{
    RawClient *held;
    uint32_t opened = 0u;
    const char *marker = "/work/.astra/fssem-sessions.ready";
    struct stat metadata;
    time_t deadline;
    int fd, result = 1;

    if (count == 0u || count > 65536u)
        return 2;
    held = calloc(count, sizeof(*held));
    if (held == NULL)
        return 1;
    (void)mkdir("/work/.astra", 0755);
    for (; opened < count; ++opened)
        if (raw_open(&held[opened], work_grant()) != ASTRA_SYSCALL_OK) {
            raw_close(&held[opened]);
            goto done;
        }
    fd = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 || close(fd) != 0)
        goto done;
    printf("FSSEM SESSIONS holding %u\n", count);
    fflush(stdout);
    deadline = time(NULL) + 120;
    while (stat(marker, &metadata) == 0 && time(NULL) < deadline) {
        struct timespec pause = {0, 100000000L};
        (void)nanosleep(&pause, NULL);
    }
    result = stat(marker, &metadata) == -1 && errno == ENOENT ? 0 : 1;
    (void)unlink(marker);
done:
    while (opened != 0u)
        raw_close(&held[--opened]);
    free(held);
    printf("FSSEM SESSIONS %s\n", result == 0 ? "done" : "failed");
    return result;
}

static int case_volume_catalog(const char *directory)
{
    (void)directory;
    const AstraStartupCapability *capability = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *messaging = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_MESSAGING);
    CHECK(capability != NULL, "missing native VOLUMES capability");
    CHECK(messaging != NULL, "missing native MESSAGING capability");
    const char *const brokers[] = {ASTRA_CAPABILITY_MESSAGING,
        ASTRA_CAPABILITY_APPLICATION_LAUNCH, ASTRA_CAPABILITY_SERVICE_MANAGER,
        ASTRA_CAPABILITY_EVENT_CONTROL};
    for (uint32_t at = 0u; at < sizeof(brokers) / sizeof(brokers[0]); ++at) {
        const AstraStartupCapability *broker = astra_startup_capability(
            astra_posix_startup(), brokers[at]);
        CHECK(broker != NULL, "missing native %s capability", brokers[at]);
        uint32_t waited = astra_wait_one(broker->handle, 0u, NULL);
        CHECK(waited == ASTRA_SYSCALL_OK || waited == ASTRA_SYSCALL_TIMED_OUT,
            "broker %s cannot wait for send credit: status=%u", brokers[at], waited);
    }
    AstraVolumeCatalogPage *page = malloc(sizeof(*page));
    CHECK(page != NULL, "catalog page allocation");
    AstraVolumeWatch watch = ASTRA_VOLUME_WATCH_INIT;
    AstraResult result = astra_volume_watch_open(messaging->handle, &watch);
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    uint32_t mounts = 0u, bindings = 0u, system = 0u, present = 0u;
    uint64_t last = 0u;
    while (result == ASTRA_OK) {
        result = astra_volume_catalog(capability->handle, &query, page);
        if (result != ASTRA_OK) break;
        for (uint32_t at = 0u; at < page->count; ++at) {
            const AstraVolumeRecord *record = &page->records[at];
            if (record->id <= last) { result = ASTRA_ERROR_IO; break; }
            last = record->id;
            if (record->kind == ASTRA_VOLUME_MOUNT && record->state == ASTRA_VOLUME_MOUNTED) {
                ++mounts;
                if (strcmp(record->name, "/dh0") == 0 &&
                    (record->flags & ASTRA_VOLUME_SYSTEM) != 0u) ++system;
            }
            if (record->kind == ASTRA_VOLUME_BINDING && record->state == ASTRA_VOLUME_MOUNTED)
                ++bindings;
            if ((record->kind == ASTRA_VOLUME_MOUNT || record->kind == ASTRA_VOLUME_BINDING) &&
                record->state == ASTRA_VOLUME_MOUNTED) {
                const char *const expected[] = {"/dh0", "/work", "/sound", "/ram0", "/tmp"};
                for (uint32_t name = 0u; name < sizeof(expected) / sizeof(expected[0]); ++name)
                    if (strcmp(record->name, expected[name]) == 0) present |= 1u << name;
            }
        }
        query.epoch = page->epoch; query.sequence = page->sequence; query.after = page->next;
        if (page->next == 0u) break;
    }
    if (result == ASTRA_OK && (mounts < 3u || bindings < 2u || system != 1u || present != 31u))
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) {
        query.after = 0u;
        query.sequence = 0u;
        query.epoch ^= UINT64_C(0x100000000);
        if (astra_volume_catalog(capability->handle, &query, page) != ASTRA_ERROR_INVALID_HANDLE ||
            page->count != 0u || page->epoch != 0u) result = ASTRA_ERROR_IO;
    }
    AstraResult closed = astra_volume_watch_close(messaging->handle, &watch);
    if (result == ASTRA_OK) result = closed;
    if (result == ASTRA_OK && (watch.id != 0u || watch.receive != ASTRA_INVALID_HANDLE))
        result = ASTRA_ERROR_IO;
    /* Repeated construction must return each native port/subscription resource. */
    for (uint32_t at = 0u; at < 32u && result == ASTRA_OK; ++at) {
        result = astra_volume_watch_open(messaging->handle, &watch);
        closed = astra_volume_watch_close(messaging->handle, &watch);
        if (result == ASTRA_OK) result = closed;
    }
    free(page);
    CHECK(result == ASTRA_OK, "native catalog/subscription result=%ld mounts=%u bindings=%u system=%u present=%u",
        (long)result, mounts, bindings, system, present);
    return 0;
}

static int case_volume_root(const char *directory)
{
    (void)directory;
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *reader = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_READ);
    const AstraStartupCapability *writer = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_WRITE);
    CHECK(metadata != NULL && reader != NULL && writer != NULL, "missing root broker capabilities");
    AstraVolumeCatalogPage *page = malloc(sizeof(*page));
    CHECK(page != NULL, "root catalog allocation");
    AstraVolumeReference ram = {0}, system = {0};
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    AstraResult result = ASTRA_OK;
    do {
        result = astra_volume_catalog(metadata->handle, &query, page);
        if (result != ASTRA_OK) break;
        for (uint32_t at = 0u; at < page->count; ++at) {
            const AstraVolumeRecord *row = &page->records[at];
            if (row->kind != ASTRA_VOLUME_MOUNT || row->state != ASTRA_VOLUME_MOUNTED) continue;
            if (strcmp(row->name, "/ram0") == 0) ram = (AstraVolumeReference){page->epoch, row->id, row->generation};
            if (strcmp(row->name, "/dh0") == 0) system = (AstraVolumeReference){page->epoch, row->id, row->generation};
        }
        query.epoch = page->epoch; query.sequence = page->sequence; query.after = page->next;
    } while (query.after != 0u);
    free(page);
    CHECK(result == ASTRA_OK && ram.id != 0u && system.id != 0u, "root catalog identities");
    AstraHandle root = ASTRA_INVALID_HANDLE;
    CHECK(astra_volume_root(metadata->handle, &ram, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_PERMISSION,
        "metadata minted data authority");
    CHECK(astra_volume_root(reader->handle, &ram, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, &root) == ASTRA_ERROR_PERMISSION,
        "reader minted write authority");
    CHECK(astra_volume_root(reader->handle, &system, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_PERMISSION,
        "public broker bypassed system grant policy");
    AstraVolumeReference stale = ram; ++stale.generation;
    CHECK(astra_volume_root(reader->handle, &stale, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_INVALID_HANDLE,
        "stale root reference accepted");
    uint64_t root_inode = 0u;
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        uint32_t rights = ASTRA_RIGHT_READ | (attempt != 0u ? ASTRA_RIGHT_WRITE : 0u);
        AstraResult granted = astra_volume_root(attempt == 0u ? reader->handle : writer->handle,
            &ram, rights, &root);
        CHECK(granted == ASTRA_OK, "root grant failed attempt=%u result=%d handle=%u",
            attempt, granted, root);
        AstraVfsClient *client = calloc(1u, sizeof(*client));
        CHECK(client != NULL, "root client allocation");
        uint32_t status = astra_vfs_client_connect_service(client, root);
        AstraVfsDirEntry info = {0};
        if (status == ASTRA_VFS_OK) status = astra_vfs_stat_meta(client, "/", &info);
        if (status == ASTRA_VFS_OK && info.kind != ASTRA_VFS_KIND_DIRECTORY) status = ASTRA_VFS_ERR_INVALID;
        if (status == ASTRA_VFS_OK &&
            (!(info.meta.valid & ASTRA_VFS_META_INODE) || info.meta.inode == 0u ||
             (root_inode != 0u && info.meta.inode != root_inode)))
            status = ASTRA_VFS_ERR_PROTOCOL;
        if (status == ASTRA_VFS_OK) root_inode = info.meta.inode;
        char path[64]; snprintf(path, sizeof(path), "/volume-root-%ld", (long)getpid());
        if (status == ASTRA_VFS_OK && attempt == 0u) {
            uint32_t refused = astra_vfs_mkdir(client, path);
            if (refused != ASTRA_VFS_ERR_READ_ONLY && refused != ASTRA_VFS_ERR_ACCESS) status = ASTRA_VFS_ERR_ACCESS;
        } else if (status == ASTRA_VFS_OK) {
            status = astra_vfs_mkdir(client, path);
            if (status == ASTRA_VFS_OK) {
                status = astra_vfs_stat_meta(client, path, &info);
                if (status == ASTRA_VFS_OK &&
                    (!(info.meta.valid & ASTRA_VFS_META_INODE) ||
                     info.meta.inode == root_inode))
                    status = ASTRA_VFS_ERR_PROTOCOL;
                uint32_t removed = astra_vfs_unlink(client, path,
                    ASTRA_VFS_UNLINK_DIRECTORY);
                if (status == ASTRA_VFS_OK) status = removed;
            }
        }
        uint32_t disconnected = astra_vfs_disconnect(client);
        free(client);
        uint32_t closed = astra_close(root); root = ASTRA_INVALID_HANDLE;
        CHECK(status == ASTRA_VFS_OK && disconnected == ASTRA_VFS_OK && closed == ASTRA_SYSCALL_OK,
            "root data/control cleanup status=%u disconnect=%u close=%u", status, disconnected, closed);
    }
    return 0;
}

static int mapping_fault_signal(volatile unsigned char *address, int write, int expected)
{
    pid_t child = fork();
    int status = 0;
    if (child < 0) return 0;
    if (child == 0) {
        if (write) *address = 'X';
        else { unsigned char observed = *address; (void)observed; }
        _exit(0);
    }
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
    }
    if (waited == child && WIFSIGNALED(status) && WTERMSIG(status) == expected) return 1;
    printf("FSSEM MAPPING FAULT expected=%d status=%x errno=%d\n", expected, status, errno);
    return 0;
}

static int case_mapped_faults(const char *directory)
{
    const size_t page = ASTRA_MEMORY_PAGE_SIZE;
    char path[512];
    int fd = -1, read_fd = -1, result = 2;
    void *shared = MAP_FAILED, *private_view = MAP_FAILED;
    void *readonly = MAP_FAILED, *cross = MAP_FAILED;
    const char *stage = "create empty file";
    join(path, sizeof(path), directory, "data");
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) goto done;
    stage = "initial empty shared view";
    shared = mmap(NULL, 3u * page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) goto done;
    read_fd = open(path, O_RDONLY);
    if (read_fd < 0) goto done;
    stage = "initial empty private and read-only views";
    private_view = mmap(NULL, 3u * page, PROT_READ | PROT_WRITE, MAP_PRIVATE, read_fd, 0);
    readonly = mmap(NULL, page, PROT_READ, MAP_SHARED, read_fd, 0);
    if (private_view == MAP_FAILED || readonly == MAP_FAILED) goto done;
    stage = "initial past-EOF cross-owner view";
    cross = mmap(NULL, 2u * page, PROT_READ, MAP_SHARED, read_fd, ASTRA_AREA_SIZE_MAX - page);
    if (cross == MAP_FAILED) goto done;
    close(read_fd); read_fd = -1;
    close(fd); fd = -1;
    stage = "closed-descriptor empty backing faults";
    if (!mapping_fault_signal(shared, 0, SIGBUS) ||
        !mapping_fault_signal(shared, 1, SIGBUS) ||
        !mapping_fault_signal(private_view, 1, SIGBUS) ||
        !mapping_fault_signal(cross, 0, SIGBUS)) goto done;
    stage = "write protection precedes backing failure";
    if (!mapping_fault_signal(readonly, 1, SIGSEGV)) goto done;
    stage = "empty file grows through existing views";
    fd = open(path, O_RDWR);
    if (fd < 0 || pwrite(fd, "abc", 3u, 0) != 3 || fsync(fd) != 0) goto done;
    volatile unsigned char *bytes = shared, *private_bytes = private_view;
    if (bytes[0] != 'a' || bytes[3] != 0 || private_bytes[0] != 'a') goto done;
    bytes[3] = 't'; private_bytes[3] = 'u'; private_bytes[0] = 'p';
    stage = "partial-page tail does not grow backing";
    struct stat info;
    if (msync(shared, 3u * page, MS_SYNC) != 0 || fstat(fd, &info) != 0 || info.st_size != 3 ||
        !mapping_fault_signal(bytes + page, 0, SIGBUS)) goto done;
    stage = "growth clears shared tail and preserves private copies";
    if (ftruncate(fd, page + 9u) != 0 || fsync(fd) != 0) goto done;
    if (bytes[3] != 0 || private_bytes[3] != 'u' || private_bytes[0] != 'p') {
        printf("FSSEM GROWTH shared-tail=%u private-tail=%u private-prefix=%u\n",
            bytes[3], private_bytes[3], private_bytes[0]);
        goto done;
    }
    unsigned char grown = 0xffu;
    if (pread(fd, &grown, 1u, 3u) != 1 || grown != 0) goto done;
    if (bytes[page] != 0) goto done;
    private_bytes[page] = 'q';
    stage = "growth reaches initially empty second owner";
    if (pwrite(fd, "z", 1u, ASTRA_AREA_SIZE_MAX) != 1 || fsync(fd) != 0 ||
        ((volatile unsigned char *)cross)[page] != 'z' ||
        ((volatile unsigned char *)cross)[page - 1u] != 0) goto done;
    stage = "shrink revokes shared and copied private pages";
    if (ftruncate(fd, 1u) != 0 || fsync(fd) != 0 || bytes[1] != 0 ||
        private_bytes[0] != 'p' || private_bytes[1] != 0 ||
        !mapping_fault_signal(bytes + page, 0, SIGBUS) ||
        !mapping_fault_signal(private_bytes + page, 0, SIGBUS) ||
        !mapping_fault_signal((volatile unsigned char *)cross + page, 0, SIGBUS)) goto done;
    close(fd); fd = -1;
    stage = "unlinked backing retains native authority";
    if (unlink(path) != 0 || bytes[0] != 'a' || private_bytes[0] != 'p') goto done;
    stage = "unmapped hole is an address fault";
    if (munmap((unsigned char *)shared + page, page) != 0 ||
        !mapping_fault_signal(bytes + page, 0, SIGSEGV)) goto done;
    result = 0;
done:
    if (fd >= 0) close(fd);
    if (read_fd >= 0) close(read_fd);
    if (shared != MAP_FAILED) (void)munmap(shared, 3u * page);
    if (private_view != MAP_FAILED) (void)munmap(private_view, 3u * page);
    if (readonly != MAP_FAILED) (void)munmap(readonly, page);
    if (cross != MAP_FAILED) (void)munmap(cross, 2u * page);
    if (unlink(path) != 0 && errno != ENOENT && result == 0) result = 2;
    if (result == 0)
        puts("FSSEM MAPPED FAULTS EOF COW SIGBUS SIGSEGV PASS");
    else
        printf("FSSEM MAPPED FAULTS FAIL %s errno=%d\n", stage, errno);
    return result;
}

static int case_anonymous_map(const char *directory)
{
    const size_t length = ASTRA_MEMORY_PAGE_SIZE + 1u;
    void *mapping = MAP_FAILED;
    const char *stage = "private map";
    int result = 1;

    (void)directory;
    for (unsigned shared = 0u; shared < 2u; ++shared) {
        stage = shared ? "shared map" : "private map";
        mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | (shared ? MAP_SHARED : MAP_PRIVATE), INT_MAX, 0);
        if (mapping == MAP_FAILED) goto done;
        volatile unsigned char *bytes = mapping;
        stage = "zero-filled pages";
        for (size_t i = 0u; i < 2u * ASTRA_MEMORY_PAGE_SIZE; ++i)
            if (bytes[i] != 0u) goto done;
        bytes[0] = 'p';
        bytes[length - 1u] = 'z';
        stage = "fork";
        pid_t child = fork();
        if (child < 0) goto done;
        if (child == 0) {
            int valid = bytes[0] == 'p' && bytes[length - 1u] == 'z';
            bytes[0] = 'c';
            bytes[length - 1u] = 'y';
            valid = valid && bytes[0] == 'c' && bytes[length - 1u] == 'y' &&
                munmap(mapping, length) == 0;
            _exit(valid ? 0 : 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0 || bytes[0] != (shared ? 'c' : 'p') ||
            bytes[length - 1u] != (shared ? 'y' : 'z')) goto done;
        stage = "unmap";
        if (munmap(mapping, length) != 0) goto done;
        mapping = MAP_FAILED;
    }
    stage = "repeated zero-fill and retirement";
    for (unsigned i = 0u; i < 32u; ++i) {
        mapping = mmap(NULL, length, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mapping == MAP_FAILED) goto done;
        volatile unsigned char *bytes = mapping;
        if (bytes[0] != 0u || bytes[length - 1u] != 0u) goto done;
        bytes[0] = (unsigned char)(i + 1u);
        if (munmap(mapping, length) != 0) goto done;
        mapping = MAP_FAILED;
    }
    printf("FSSEM ANONYMOUS zero-fill private-fork shared-fork ignored-fd retirement PASS\n");
    result = 0;
done:
    if (mapping != MAP_FAILED) (void)munmap(mapping, length);
    return result == 0 ? 0 : fail("anonymous-map %s errno=%d", stage, errno);
}

static int case_partial_map(const char *directory)
{
    const size_t page = ASTRA_MEMORY_PAGE_SIZE, length = 6u * ASTRA_MEMORY_PAGE_SIZE;
    const off_t offset = (off_t)ASTRA_AREA_SIZE_MAX - 2 * (off_t)ASTRA_MEMORY_PAGE_SIZE;
    void *mapping = MAP_FAILED;
    size_t mapped_length = length;
    char path[256];
    unsigned char data[ASTRA_MEMORY_PAGE_SIZE];
    const char *stage = "anonymous map";
    int fd = -1, result = 1, error;

    if (snprintf(path, sizeof(path), "%s/partial-map", directory) >= (int)sizeof(path))
        return fail("partial-map path");
    for (unsigned shared = 0u; shared < 2u; ++shared) {
        stage = "anonymous map";
        mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | (shared ? MAP_SHARED : MAP_PRIVATE), -1, 0);
        if (mapping == MAP_FAILED) goto done;
        volatile unsigned char *bytes = mapping;
        for (size_t i = 0u; i < 6u; ++i) bytes[i * page] = (unsigned char)('1' + i);
        stage = "invalid unmap admission";
        errno = 0;
        if (munmap((void *)(bytes + 1u), page) != -1 || errno != EINVAL ||
            munmap(mapping, 0u) != -1 || errno != EINVAL) goto done;
        stage = "anonymous middle cut";
        if (munmap((void *)(bytes + page), 2u * page) != 0 ||
            bytes[0] != '1' || bytes[3u * page] != '4' || bytes[5u * page] != '6') goto done;
        stage = "anonymous partial-view fork";
        pid_t child = fork();
        if (child < 0) goto done;
        if (child == 0) {
            int valid = bytes[0] == '1' && bytes[3u * page] == '4';
            bytes[0] = 'x';
            bytes[3u * page] = 'y';
            valid = valid && munmap(mapping, length) == 0;
            _exit(valid ? 0 : 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
            bytes[0] != (shared ? 'x' : '1') || bytes[3u * page] != (shared ? 'y' : '4')) goto done;
        stage = "anonymous head tail and holes";
        if (munmap(mapping, page) != 0 || munmap((void *)(bytes + 5u * page), page) != 0 ||
            bytes[4u * page] != '5' || munmap((void *)(bytes + page), 4u * page) != 0 ||
            munmap(mapping, length) != 0) goto done;
        mapping = MAP_FAILED;
    }
    for (unsigned shared = 0u; shared < 2u; ++shared) {
        stage = "file setup";
        fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
        if (fd < 0 || ftruncate(fd, offset + 4 * (off_t)page) != 0) goto done;
        for (size_t i = 0u; i < 4u; ++i) {
            memset(data, (int)('a' + i), sizeof(data));
            if (pwrite(fd, data, sizeof(data), offset + (off_t)(i * page)) != (ssize_t)sizeof(data)) goto done;
        }
        stage = "two-window file map";
        mapped_length = 4u * page;
        mapping = mmap(NULL, 4u * page, PROT_READ | (shared ? 0 : PROT_WRITE),
            shared ? MAP_SHARED : MAP_PRIVATE, fd, offset);
        if (mapping == MAP_FAILED || close(fd) != 0) goto done;
        fd = -1;
        volatile unsigned char *bytes = mapping;
        if (bytes[0] != 'a' || bytes[3u * page] != 'd') goto done;
        stage = "cross-window cut and fork";
        if (munmap((void *)(bytes + page), 2u * page) != 0) goto done;
        pid_t child = fork();
        if (child < 0) goto done;
        if (child == 0) {
            int valid = bytes[0] == 'a' && bytes[3u * page] == 'd';
            if (!shared) {
                bytes[0] = 'x';
                bytes[3u * page] = 'y';
                valid = valid && bytes[0] == 'x' && bytes[3u * page] == 'y';
            }
            valid = valid && munmap(mapping, 4u * page) == 0;
            _exit(valid ? 0 : 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
            bytes[0] != 'a' || bytes[3u * page] != 'd') goto done;
        stage = "file close unlink and retirement";
        if (unlink(path) != 0 || bytes[0] != 'a' || bytes[3u * page] != 'd' ||
            munmap(mapping, 4u * page) != 0 || munmap(mapping, 4u * page) != 0) goto done;
        mapping = MAP_FAILED;
    }
    puts("FSSEM PARTIAL MAP anonymous private/shared fork head middle tail holes file-window close unlink PASS");
    result = 0;
done:
    error = errno;
    if (mapping != MAP_FAILED) (void)munmap(mapping, mapped_length);
    if (fd >= 0) close(fd);
    unlink(path);
    return result == 0 ? 0 : fail("partial-map %s errno=%d", stage, error);
}

static int mapped_usage_owners(uint32_t service, const AstraVolumeReference *volume, const char *path,
    pid_t parent, pid_t child, uint32_t expected)
{
    AstraVolumeUsageSnapshot snapshot = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    AstraResult status = ASTRA_ERROR_BUSY;
    for (uint32_t retry = 0u; retry < 8u && status == ASTRA_ERROR_BUSY; ++retry) {
        status = astra_volume_usage_snapshot_open(service, volume, &snapshot);
        if (status == ASTRA_ERROR_BUSY)
            (void)astra_rt_thread_sleep(UINT64_C(50000000), ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
    if (status != ASTRA_OK) return fail("mapped usage snapshot status=%ld", (long)status);
    uint32_t found = 0u, unexpected = 0u;
    for (uint32_t at = 0u; at < snapshot.count; ++at) {
        const AstraVolumeUsageRecord *row = &snapshot.records[at];
        if (row->kind != ASTRA_VOLUME_USAGE_FILE || strcmp(row->path, path) != 0) continue;
        if (row->process == (uint32_t)parent) found |= 1u;
        else if (child > 0 && row->process == (uint32_t)child) found |= 2u;
        else if (row->process != 0u || expected != 0u) ++unexpected;
    }
    astra_volume_usage_snapshot_close(&snapshot);
    return found == expected && unexpected == 0u ? 0 :
        fail("mapped usage owners=%u expected=%u unexpected=%u", found, expected, unexpected);
}

static int case_mapped_usage(const char *directory)
{
    const size_t length = 2u * ASTRA_MEMORY_PAGE_SIZE;
    const off_t offset = (off_t)ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE;
    const AstraStartupCapability *usage = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUME_USAGE);
    const AstraStartupCapability *metadata = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_VOLUMES);
    AstraVolumeReference volume = {0};
    AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
    void *mapping = MAP_FAILED;
    char path[192];
    uint32_t ready = 0u, release = 0u;
    pid_t parent = getpid(), child = -1;
    int fd = -1, result = 2, child_status = 0, error;
    const char *stage = "create";
    join(path, sizeof(path), directory, "mapped-usage");
    if (usage == NULL || metadata == NULL) return fail("mapped usage capability missing");
    fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, offset + (off_t)length) != 0 ||
        pwrite(fd, "L", 1u, offset) != 1 ||
        pwrite(fd, "R", 1u, offset + ASTRA_MEMORY_PAGE_SIZE) != 1 || fsync(fd) != 0)
        goto done;
    struct stat info;
    stage = "volume identity";
    if (fstat(fd, &info) != 0 || astra_volume_snapshot_open(metadata->handle, &catalog) != ASTRA_OK)
        goto done;
    for (uint32_t at = 0u; at < catalog.count; ++at) {
        const AstraVolumeRecord *row = &catalog.records[at];
        if (row->id == (uint64_t)info.st_dev)
            volume = (AstraVolumeReference){catalog.epoch, row->id, row->generation};
    }
    astra_volume_snapshot_close(&catalog);
    if (volume.id == 0u) goto done;
    stage = "map";
    mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, offset);
    if (mapping == MAP_FAILED || close(fd) != 0) goto done;
    fd = -1;
    volatile char *bytes = mapping;
    if (bytes[0] != 'L' || bytes[ASTRA_MEMORY_PAGE_SIZE] != 'R') goto done;
    bytes[0] = 'p';
    stage = "fork readiness";
    uint32_t rights = ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER;
    if (astra_rt_event_create(0u, rights, &ready) != ASTRA_SYSCALL_OK ||
        astra_rt_event_create(0u, rights, &release) != ASTRA_SYSCALL_OK) goto done;
    child = fork();
    if (child < 0) goto done;
    if (child == 0) {
        int valid = bytes[0] == 'p' && bytes[ASTRA_MEMORY_PAGE_SIZE] == 'R';
        bytes[0] = 'c';
        if (astra_rt_signal(ready, 1u, NULL) != ASTRA_SYSCALL_OK ||
            astra_wait_one(release, astra_clock_monotonic() + UINT64_C(30000000000), NULL) != ASTRA_SYSCALL_OK)
            _exit(1);
        valid = valid && bytes[0] == 'c' && munmap(mapping, length) == 0;
        _exit(valid ? 0 : 2);
    }
    if (astra_wait_one(ready, astra_clock_monotonic() + UINT64_C(30000000000), NULL) != ASTRA_SYSCALL_OK ||
        bytes[0] != 'p') goto done;
    if (mapped_usage_owners(usage->handle, &volume, path, parent, child, 3u) != 0) { result = -1; goto done; }
    stage = "parent unmap";
    if (munmap(mapping, length) != 0) goto done;
    mapping = MAP_FAILED;
    if (mapped_usage_owners(usage->handle, &volume, path, parent, child, 2u) != 0) { result = -1; goto done; }
    stage = "child unmap";
    if (astra_rt_signal(release, 1u, NULL) != ASTRA_SYSCALL_OK ||
        waitpid(child, &child_status, 0) != child) goto done;
    child = -1;
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) goto done;
    if (mapped_usage_owners(usage->handle, &volume, path, parent, 0, 0u) != 0) { result = -1; goto done; }
    puts("FSSEM MAPPED USAGE closed-fd fork COW cross-owner unmap PASS");
    result = 0;
done:
    error = errno;
    astra_volume_snapshot_close(&catalog);
    if (child > 0) { (void)kill(child, SIGKILL); (void)waitpid(child, &child_status, 0); }
    if (mapping != MAP_FAILED) (void)munmap(mapping, length);
    if (fd >= 0) (void)close(fd);
    if (ready != 0u) (void)astra_close(ready);
    if (release != 0u) (void)astra_close(release);
    (void)unlink(path);
    return result == 0 || result == -1 ? result : fail("mapped usage %s child=%d errno=%d", stage, child_status, error);
}

/* A real closed-descriptor mapping keeps GUI/lsof checks independent of fds. */
static int hold_mapping(const char *path)
{
    struct stat info;
    char marker[96], text[64];
    int fd = open(path, O_RDONLY), result = 1;
    void *mapping = MAP_FAILED;
    if (fd < 0 || fstat(fd, &info) != 0 || info.st_size <= 0 ||
        (uint64_t)info.st_size > SIZE_MAX) goto done;
    mapping = mmap(NULL, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED || close(fd) != 0) goto done;
    fd = -1;
    (void)*(volatile const unsigned char *)mapping;
    snprintf(marker, sizeof(marker), "/work/fssem-map-%ld", (long)getpid());
    int flag = open(marker, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (flag < 0) goto done;
    if (close(flag) != 0) { (void)unlink(marker); goto done; }
    snprintf(text, sizeof(text), "FSSEM MAPPING holding %ld", (long)getpid());
    (void)astra_log_debug(text, (uint32_t)strlen(text));
    printf("%s\n", text); fflush(stdout);
    time_t end = time(NULL) + 120;
    struct stat held;
    while (stat(marker, &held) == 0 && time(NULL) < end) {
        struct timespec pause = {0, 100000000L};
        (void)nanosleep(&pause, NULL);
    }
    result = stat(marker, &held) == -1 && errno == ENOENT ? 0 : 1;
    (void)unlink(marker);
done:
    if (fd >= 0) (void)close(fd);
    if (mapping != MAP_FAILED && munmap(mapping, (size_t)info.st_size) != 0) result = 1;
    printf("FSSEM MAPPING %s\n", result == 0 ? "done" : "failed");
    return result;
}

static int case_unmap_errors(const char *directory)
{
    const uint32_t length = 3u * ASTRA_MEMORY_PAGE_SIZE;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t receive = 0u, send = 0u, area = 0u, reply_handle = 0u;
    uint32_t moved = 0u, count = 0u, token = 0u;
    pid_t child = -1;
    int result = 2, child_status = 0, error;
    const char *stage = "pager setup";
    (void)directory;

    for (unsigned posix = 0u; posix < 2u; ++posix) {
        memset(page, 'a', sizeof(page));
        if (astra_rt_port_create(1u, sizeof(AstraPagerSyncRequest), &receive, &send) != ASTRA_SYSCALL_OK ||
            astra_rt_area_create_flagged(length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                ASTRA_RIGHT_MAP | ASTRA_RIGHT_ADMINISTER,
                ASTRA_AREA_CREATE_RESERVED | ASTRA_AREA_CREATE_MAPPED_LIFETIME, &area) != ASTRA_SYSCALL_OK ||
            astra_rt_area_bind_pager(area, send, 79u) != ASTRA_SYSCALL_OK ||
            astra_rt_area_publish_clean_page(area, 0u, page, sizeof(page)) != ASTRA_SYSCALL_OK)
            goto done;
        stage = "client fork";
        child = fork();
        if (child < 0) goto done;
        if (child == 0) {
            void *view = NULL;
            uint32_t span = 0u;
            /* A cloned client inherits send authority, never the service endpoint. */
            if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                    &view, &span) != ASTRA_SYSCALL_OK || span != length ||
                astra_close(area) != ASTRA_SYSCALL_OK ||
                astra_close(receive) != ASTRA_SYSCALL_INVALID_HANDLE ||
                astra_close(send) != ASTRA_SYSCALL_OK)
                _exit(1);
            volatile uint8_t *bytes = view;
            bytes[0] = 'q';
            for (unsigned attempt = 0u; attempt < 3u; ++attempt) {
                if (posix) {
                    errno = 0;
                    int status = munmap(view, length);
                    if ((attempt < 2u && (status != -1 || errno != EIO)) ||
                        (attempt == 2u && status != 0)) _exit(2);
                } else {
                    uint32_t status = astra_rt_area_unmap(view);
                    if (status != (attempt < 2u ? ASTRA_SYSCALL_IO_ERROR : ASTRA_SYSCALL_OK))
                        _exit(3);
                }
                if (attempt < 2u && bytes[0] != 'q') _exit(4);
            }
            _exit(0);
        }
        stage = "saved errors and retry";
        for (unsigned attempt = 0u; attempt < 3u; ++attempt) {
            AstraPagerSyncRequest request = {0};
            if (astra_wait_one(receive, astra_clock_monotonic() + UINT64_C(10000000000), NULL) != ASTRA_SYSCALL_OK ||
                astra_port_receive(receive, &request, sizeof(request), &reply_handle, 1u,
                    &moved, &count) != ASTRA_SYSCALL_OK ||
                moved != sizeof(request) || count != 1u ||
                request.header.protocol != ASTRA_PAGER_PROTOCOL ||
                request.header.operation != ASTRA_PAGER_OPERATION_SYNC ||
                request.cookie != 79u || request.offset != 0u || request.byte_size != length ||
                request.flags != ASTRA_AREA_SYNC_ASYNC) goto done;
            uint64_t sequence = 0u;
            uint32_t holder = 0u;
            if (astra_rt_area_next_process(area, 0u, &sequence, &holder) != ASTRA_SYSCALL_OK ||
                holder != (uint32_t)child || sequence == 0u) goto done;
            AstraPagerSyncReply reply = {.cookie = request.cookie, .offset = request.offset,
                .byte_size = request.byte_size, .status = attempt == 0u ? ASTRA_STATUS_NO_SPACE :
                    attempt == 1u ? ASTRA_STATUS_IO : ASTRA_STATUS_OK};
            astra_message_header_set(&reply.header, sizeof(reply), ASTRA_PAGER_PROTOCOL,
                ASTRA_PAGER_PROTOCOL_VERSION, ASTRA_PAGER_OPERATION_SYNC_REPLY,
                request.header.transaction_id);
            if (astra_port_send(reply_handle, &reply, sizeof(reply), NULL, 0u) != ASTRA_SYSCALL_OK)
                goto done;
            (void)astra_close(reply_handle);
            reply_handle = 0u;
        }
        stage = "client completion and dirty retention";
        if (waitpid(child, &child_status, 0) != child) goto done;
        child = -1;
        uint64_t sequence = 0u;
        uint32_t holder = UINT32_MAX;
        if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 ||
            astra_rt_area_next_process(area, 0u, &sequence, &holder) != ASTRA_SYSCALL_OK || holder != 0u ||
            astra_rt_area_writeback_begin(area, 0u, page, &token) != ASTRA_SYSCALL_OK ||
            page[0] != 'q' ||
            astra_rt_area_writeback_complete(area, 0u, token, true) != ASTRA_SYSCALL_OK)
            goto done;
        token = 0u;
        if (astra_close(area) != ASTRA_SYSCALL_OK) goto done;
        area = 0u;
        if (astra_close(send) != ASTRA_SYSCALL_OK) goto done;
        send = 0u;
        if (astra_close(receive) != ASTRA_SYSCALL_OK) goto done;
        receive = 0u;
    }
    puts("FSSEM UNMAP saved-error retention retry native/POSIX PASS");
    result = 0;
done:
    error = errno;
    if (reply_handle != 0u) (void)astra_close(reply_handle);
    if (child > 0) { (void)kill(child, SIGKILL); (void)waitpid(child, &child_status, 0); }
    if (area != 0u) {
        if (token == 0u) (void)astra_rt_area_writeback_begin(area, 0u, page, &token);
        if (token != 0u) (void)astra_rt_area_writeback_complete(area, 0u, token, true);
        (void)astra_close(area);
    }
    if (send != 0u) (void)astra_close(send);
    if (receive != 0u) (void)astra_close(receive);
    return result == 0 ? 0 : fail("unmap-errors %s child=%d errno=%d", stage, child_status, error);
}

static int case_shared_map(const char *directory)
{
    AstraProcessFilesystem native = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFile writer = ASTRA_FILE_INIT, reader = ASTRA_FILE_INIT;
    uint32_t areas[2] = {0u, 0u}, mapped_size = 0u, status = 0u, backing_status = 0u;
    void *views[2] = {NULL, NULL};
    uint64_t size = 0u;
    char path[192], bytes[ASTRA_MEMORY_PAGE_SIZE];
    const size_t length = 2u * ASTRA_MEMORY_PAGE_SIZE + 9u;
    const char *stage = "create";
    int fd = -1, result = 2, error;

    join(path, sizeof(path), directory, "data");
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) goto done;
    memset(bytes, 'a', sizeof(bytes));
    if (write(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) ||
        write(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) ||
        write(fd, bytes, 9u) != 9 || fsync(fd) != 0 || close(fd) != 0) goto done;
    fd = -1;
    stage = "native source";
    if (astra_process_filesystem_open(&native, astra_posix_startup()) != ASTRA_VFS_OK ||
        astra_filesystem_open(&native.filesystem, path, ASTRA_VFS_OPEN_READ,
            &reader) != ASTRA_VFS_OK ||
        astra_filesystem_file_area(&reader, 0u, 3u * ASTRA_MEMORY_PAGE_SIZE,
            &areas[0], &size) != ASTRA_VFS_OK || size != length) goto done;
    stage = "read-only WRITE refusal";
    status = astra_rt_area_map_range(areas[0], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
        0u, ASTRA_SHARED_AREA_TABLE_SPAN, &views[0], &mapped_size);
    if (status != ASTRA_SYSCALL_ACCESS_DENIED || views[0] != NULL) goto done;
    if (astra_close(areas[0]) != ASTRA_SYSCALL_OK) goto done;
    areas[0] = 0u;
    stage = "retained writable area";
    if (astra_filesystem_open(&native.filesystem, path,
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &writer) != ASTRA_VFS_OK ||
        astra_filesystem_file_area(&writer, 0u, 3u * ASTRA_MEMORY_PAGE_SIZE,
            &areas[0], &size) != ASTRA_VFS_OK) goto done;
    status = astra_rt_area_map_range(areas[0], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
        0u, ASTRA_SHARED_AREA_TABLE_SPAN, &views[0], &mapped_size);
    if (status != ASTRA_SYSCALL_OK || mapped_size != ASTRA_SHARED_AREA_TABLE_SPAN) goto done;
    stage = "read-only authority after writable exposure";
    if (astra_filesystem_file_area(&reader, 0u, 3u * ASTRA_MEMORY_PAGE_SIZE,
            &areas[1], &size) != ASTRA_VFS_OK) goto done;
    status = astra_rt_area_map_range(areas[1], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
        0u, ASTRA_SHARED_AREA_TABLE_SPAN, &views[1], &mapped_size);
    if (status != ASTRA_SYSCALL_ACCESS_DENIED || views[1] != NULL) goto done;
    stage = "independent read-only view";
    status = astra_rt_area_map_range_at(areas[1], ASTRA_AREA_MAP_READ,
        0u, ASTRA_SHARED_AREA_TABLE_SPAN,
        ASTRA_SHARED_AREA_ADDRESS_END - ASTRA_SHARED_AREA_TABLE_SPAN,
        &views[1], &mapped_size);
    if (status != ASTRA_SYSCALL_OK) goto done;
    stage = "close original writable description";
    status = astra_filesystem_close(&writer);
    if (status != ASTRA_VFS_OK) goto done;
    stage = "close original read-only description";
    status = astra_filesystem_close(&reader);
    if (status != ASTRA_VFS_OK) goto done;
    for (unsigned i = 0u; i < 2u; ++i) {
        if (astra_close(areas[i]) != ASTRA_SYSCALL_OK) goto done;
        areas[i] = 0u;
    }
    stage = "shared writes after description and handle close";
    volatile char *shared = views[0];
    volatile const char *alias = views[1];
    if (shared[0] != 'a' || shared[length] != 0) goto done;
    shared[0] = 'p';
    shared[2u * ASTRA_MEMORY_PAGE_SIZE + 1u] = 't';
    shared[length + 7u] = 'z';
    fd = open(path, O_RDWR);
    if (fd < 0 || pwrite(fd, "b", 1u, 4u) != 1) goto done;
    if (close(fd) != 0) goto done;
    fd = -1;
    stage = "shared fork";
    pid_t child = fork();
    if (child < 0) goto done;
    if (child == 0) {
        int valid = alias[0] == 'p' && shared[4u] == 'b';
        shared[ASTRA_MEMORY_PAGE_SIZE] = 'c';
        shared[length - 1u] = 'y';
        _exit(valid ? 0 : 1);
    }
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0 || alias[0] != 'p' || alias[4u] != 'b' ||
        alias[ASTRA_MEMORY_PAGE_SIZE] != 'c' || alias[length - 1u] != 'y') goto done;
    stage = "native range sync without descriptions or handles";
    status = astra_rt_area_sync_range(views[0], 3u * ASTRA_MEMORY_PAGE_SIZE, 0u, &backing_status);
    if (status != ASTRA_SYSCALL_OK || backing_status != 0u) goto done;
    stage = "native sync through read-only alias";
    status = astra_rt_area_sync_range(views[1], 3u * ASTRA_MEMORY_PAGE_SIZE, 0u, &backing_status);
    if (status != ASTRA_SYSCALL_OK || backing_status != 0u) goto done;
    for (unsigned i = 0u; i < 2u; ++i) {
        if (astra_rt_area_unmap(views[i]) != ASTRA_SYSCALL_OK) goto done;
        views[i] = NULL;
    }
    stage = "persisted size and bytes";
    struct stat info;
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &info) != 0 || info.st_size != (off_t)length ||
        pread(fd, bytes, 5u, 0u) != 5 || memcmp(bytes, "paaab", 5u) != 0 ||
        pread(fd, bytes, 1u, ASTRA_MEMORY_PAGE_SIZE) != 1 || bytes[0] != 'c' ||
        pread(fd, bytes, 9u, 2u * ASTRA_MEMORY_PAGE_SIZE) != 9 ||
        memcmp(bytes, "ataaaaaay", 9u) != 0) goto done;
    stage = "cross-window source";
    if (close(fd) != 0) goto done;
    fd = open(path, O_RDWR);
    if (fd < 0 || ftruncate(fd, (off_t)ASTRA_AREA_SIZE_MAX + 9) != 0 || close(fd) != 0)
        goto done;
    fd = -1;
    if (astra_filesystem_open(&native.filesystem, path,
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &writer) != ASTRA_VFS_OK) goto done;
    for (unsigned i = 0u; i < 2u; ++i) {
        uint32_t source = i == 0u ? ASTRA_AREA_SIZE_MAX - ASTRA_SHARED_AREA_TABLE_SPAN : 0u;
        uint64_t base = i == 0u ? source : ASTRA_AREA_SIZE_MAX;
        if (astra_filesystem_file_area(&writer, base, ASTRA_SHARED_AREA_TABLE_SPAN,
                &areas[i], &size) != ASTRA_VFS_OK ||
            astra_rt_area_map_range(areas[i], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                source, ASTRA_SHARED_AREA_TABLE_SPAN, &views[i], &mapped_size) != ASTRA_SYSCALL_OK)
            goto done;
    }
    if (astra_filesystem_close(&writer) != ASTRA_VFS_OK) goto done;
    for (unsigned i = 0u; i < 2u; ++i) {
        if (astra_close(areas[i]) != ASTRA_SYSCALL_OK) goto done;
        areas[i] = 0u;
    }
    ((volatile char *)views[0])[ASTRA_SHARED_AREA_TABLE_SPAN - 1u] = 'x';
    ((volatile char *)views[1])[0] = 'y';
    ((volatile char *)views[1])[8u] = 'z';
    stage = "writer retained by later window";
    if (astra_rt_area_unmap(views[0]) != ASTRA_SYSCALL_OK) goto done;
    views[0] = NULL;
    status = astra_rt_area_sync_range(views[1], ASTRA_MEMORY_PAGE_SIZE, 0u, &backing_status);
    if (status != ASTRA_SYSCALL_OK || backing_status != 0u ||
        astra_rt_area_unmap(views[1]) != ASTRA_SYSCALL_OK) goto done;
    views[1] = NULL;
    stage = "cross-window persistence and grown tail";
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &info) != 0 || info.st_size != (off_t)ASTRA_AREA_SIZE_MAX + 9 ||
        pread(fd, bytes, 8u, (off_t)length) != 8 ||
        memcmp(bytes, "\0\0\0\0\0\0\0\0", 8u) != 0 ||
        pread(fd, bytes, 10u, ASTRA_AREA_SIZE_MAX - 1u) != 10 ||
        memcmp(bytes, "xy\0\0\0\0\0\0\0z", 10u) != 0) goto done;
    if (close(fd) != 0) goto done;
    fd = -1;
    puts("FSSEM SHARED MAP authority close/fork coherence range-sync EOF cross-owner PASS");
    result = 0;
done:
    error = errno;
    for (unsigned i = 0u; i < 2u; ++i) {
        if (views[i] != NULL) (void)astra_rt_area_unmap(views[i]);
        if (areas[i] != 0u) (void)astra_close(areas[i]);
    }
    (void)astra_filesystem_close(&writer);
    (void)astra_filesystem_close(&reader);
    (void)astra_process_filesystem_close(&native);
    if (fd >= 0) close(fd);
    return result == 0 ? 0 : fail("shared-map %s native=%u backing=%u errno=%d", stage, status, backing_status, error);
}

static int case_shared_posix(const char *directory)
{
    char path[192], bytes[ASTRA_MEMORY_PAGE_SIZE];
    const size_t length = 2u * ASTRA_MEMORY_PAGE_SIZE + 9u;
    void *mapping = MAP_FAILED, *alias = MAP_FAILED, *private_copy = MAP_FAILED;
    size_t mapped_length = length;
    int fd = -1, readonly = -1, result = 2, error;
    const char *stage = "create";
    join(path, sizeof(path), directory, "data");
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    memset(bytes, 'a', sizeof(bytes));
    if (fd < 0 || write(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) ||
        write(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes) || write(fd, bytes, 9u) != 9 ||
        fsync(fd) != 0 || (readonly = open(path, O_RDONLY)) < 0) goto done;
    stage = "shared WRITE requires writable description";
    errno = 0;
    mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, readonly, 0);
    if (mapping != MAP_FAILED || errno != EACCES) goto done;
    stage = "shared mappings";
    mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    alias = mmap(NULL, length, PROT_READ, MAP_SHARED, readonly, 0);
    private_copy = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, readonly, 0);
    if (mapping == MAP_FAILED || alias == MAP_FAILED || private_copy == MAP_FAILED ||
        pwrite(fd, "b", 1u, 4u) != 1 || close(fd) != 0 || close(readonly) != 0) goto done;
    fd = readonly = -1;
    stage = "closed-description shared and private writes";
    volatile char *shared = mapping;
    volatile const char *reader = alias;
    shared[0] = 'p'; shared[2u * ASTRA_MEMORY_PAGE_SIZE + 1u] = 't';
    shared[length + 7u] = 'z';
    ((volatile char *)private_copy)[0] = 'q';
    if (msync(private_copy, length, MS_SYNC | MS_INVALIDATE) != 0 || reader[0] != 'p') goto done;
    pid_t child = fork();
    if (child < 0) goto done;
    if (child == 0) {
        int valid = reader[0] == 'p' && shared[4u] == 'b';
        shared[ASTRA_MEMORY_PAGE_SIZE] = 'c'; shared[length - 1u] = 'y';
        _exit(valid && msync(alias, length, MS_ASYNC) == 0 ? 0 : 1);
    }
    int child_status;
    if (waitpid(child, &child_status, 0) != child || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0 || reader[ASTRA_MEMORY_PAGE_SIZE] != 'c') goto done;
    stage = "msync validation";
    const int invalid_flags[] = {0, MS_SYNC | MS_ASYNC, ~0};
    for (unsigned i = 0u; i < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++i) {
        errno = 0;
        if (msync(mapping, length, invalid_flags[i]) != -1 || errno != EINVAL) goto done;
    }
    errno = 0;
    if (msync((char *)mapping + 1u, length, MS_SYNC) != -1 || errno != EINVAL) goto done;
    errno = 0;
    if (msync(NULL, 1u, MS_SYNC) != -1 || errno != ENOMEM ||
        msync(NULL, 0u, MS_SYNC) != 0) goto done;
    stage = "read-only alias sync and partial holes";
    if (msync(alias, length, MS_SYNC | MS_INVALIDATE) != 0 ||
        munmap((char *)alias + ASTRA_MEMORY_PAGE_SIZE, ASTRA_MEMORY_PAGE_SIZE) != 0) goto done;
    errno = 0;
    if (msync(alias, length, MS_SYNC) != -1 || errno != ENOMEM ||
        msync(alias, 1u, MS_SYNC) != 0 ||
        msync((char *)alias + 2u * ASTRA_MEMORY_PAGE_SIZE, 9u, MS_SYNC) != 0) goto done;
    if (munmap(alias, length) != 0 || munmap(mapping, length) != 0 || munmap(private_copy, length) != 0)
        goto done;
    alias = mapping = private_copy = MAP_FAILED;
    stage = "cross-owner range sync";
    fd = open(path, O_RDWR);
    if (fd < 0 || ftruncate(fd, (off_t)ASTRA_AREA_SIZE_MAX + 9) != 0) goto done;
    mapped_length = ASTRA_MEMORY_PAGE_SIZE + 9u;
    mapping = mmap(NULL, mapped_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
        (off_t)ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE);
    if (mapping == MAP_FAILED || close(fd) != 0) goto done;
    fd = -1;
    shared = mapping;
    shared[ASTRA_MEMORY_PAGE_SIZE - 1u] = 'x';
    shared[ASTRA_MEMORY_PAGE_SIZE] = 'y'; shared[ASTRA_MEMORY_PAGE_SIZE + 8u] = 'z';
    if (msync(mapping, mapped_length, MS_ASYNC) != 0 ||
        msync(mapping, mapped_length, MS_SYNC) != 0 ||
        munmap(mapping, ASTRA_MEMORY_PAGE_SIZE) != 0 ||
        msync((char *)mapping + ASTRA_MEMORY_PAGE_SIZE, 9u, MS_SYNC) != 0 ||
        munmap(mapping, mapped_length) != 0) goto done;
    mapping = MAP_FAILED;
    stage = "asynchronous retirement";
    join(path, sizeof(path), directory, "async-data");
    mapped_length = ASTRA_MEMORY_PAGE_SIZE + 9u;
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)mapped_length) != 0) goto done;
    mapping = mmap(NULL, mapped_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED || close(fd) != 0) goto done;
    fd = -1;
    memcpy(mapping, "ASYNC", 5u);
    ((volatile char *)mapping)[mapped_length - 1u] = '!';
    if (msync(mapping, mapped_length, MS_ASYNC | MS_INVALIDATE) != 0 ||
        munmap(mapping, mapped_length) != 0) goto done;
    mapping = MAP_FAILED;
    stage = "anonymous sync";
    mapping = mmap(NULL, mapped_length, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED || msync(mapping, mapped_length, MS_SYNC) != 0 ||
        msync(mapping, mapped_length, MS_ASYNC) != 0 || munmap(mapping, mapped_length) != 0) goto done;
    mapping = MAP_FAILED;
    puts("FSSEM POSIX SHARED msync sync/async close/fork holes COW cross-owner PASS");
    result = 0;
done:
    error = errno;
    if (mapping != MAP_FAILED) (void)munmap(mapping, mapped_length);
    if (alias != MAP_FAILED) (void)munmap(alias, length);
    if (private_copy != MAP_FAILED) (void)munmap(private_copy, length);
    if (fd >= 0) close(fd);
    if (readonly >= 0) close(readonly);
    return result == 0 ? 0 : fail("shared-posix %s errno=%d", stage, error);
}

static int file_map_gate(const char *directory);

static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
    {"durable-save", case_durable_save},
    {"volume-catalog", case_volume_catalog},
    {"volume-root", case_volume_root},
    {"symlink-in-volume", case_symlink_in_volume},
    {"symlink-cross-volume", case_symlink_cross_volume},
    {"dotdot-at-root", case_dotdot_at_root},
    {"escape-relative", case_escape_relative},
    {"link-edges", case_link_edges},
    {"alias-identity", case_alias_identity},
    {"continuation-limit", case_continuation_limit},
    {"path-boundaries", case_path_boundaries},
    {"raw-lane", case_raw_lane},
    {"lane-fork-exec", case_lane_fork_exec},
    {"shared-description", case_shared_description},
    {"area-range", area_range_gate},
    {"anonymous-map", case_anonymous_map},
    {"mapped-faults", case_mapped_faults},
    {"unmap-errors", case_unmap_errors},
    {"mapped-usage", case_mapped_usage},
    {"partial-map", case_partial_map},
    {"shared-map", case_shared_map},
    {"shared-posix", case_shared_posix},
    {"file-map", file_map_gate},
    {"page-writeback", page_writeback_gate},
    {"generation-exhaustion", generation_exhaustion_gate},
    {"cache-pressure", case_cache_pressure},
    {"description-io", case_description_io},
    {"scoped-io", case_scoped_io},
    {"byte-prefix", case_byte_prefix},
    {"map-publication", case_map_publication},
    {"exec-inherit", case_exec_inherit},
    {"append-race", case_append_race},
    {"file-size-limit", case_file_size_limit},
    {"cwd-identity", case_cwd_identity},
    {"raw-readonly", case_raw_readonly},
    {"raw-cross-grant", case_raw_cross_grant},
    {"raw-subroot", case_raw_subroot},
    {"raw-delegate", case_raw_delegate},
    {"raw-retire", case_raw_retire},
    {"raw-onward", case_raw_onward},
    {"raw-orphans", case_raw_orphans},
    {"raw-orphans-death", case_raw_orphans_death},
    {"many-lanes", case_many_lanes},
    {"owner-sessions", case_owner_sessions},
    {"owner-files", case_owner_files},
};

/* A live writer gives the host gate a deterministic provider-loss boundary. */
static int loss_fail(const char *what, int error)
{
    (void)astra_log_debug("FSSEM LOSS FAIL", sizeof("FSSEM LOSS FAIL") - 1u);
    printf("FSSEM LOSS FAIL %s errno=%d\n", what, error);
    return 1;
}

static int loss_probe(const char *path, int seconds, int reopen)
{
    char again[192], root[192], moved[192], cwd[192];
    struct stat st;
    time_t end;
    long writes = 0;
    uint32_t derived = 0u, replaced = 0u;
    uint64_t replacement_inode = 0u;
    AstraVfsClient *client = astra_process_vfs_client_handle_lookup(work_grant());
    DIR *directory;
    int fd;

    /* Terminal restarts with HostFS; this probe must finish independently. */
    if (!reopen && signal(SIGHUP, SIG_IGN) == SIG_ERR)
        return loss_fail("ignore terminal hangup", errno);
    if (chdir("/work") != 0)
        return loss_fail("cwd", errno);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    directory = opendir("/work");
    if (fd < 0 || directory == NULL)
        return loss_fail("open", errno);
    if (reopen) {
        if (client == NULL || astra_vfs_grant_derive(client, "", ASTRA_VFS_GRANT_READ, &derived) != ASTRA_VFS_OK)
            return loss_fail("derive", 0);
        snprintf(root, sizeof(root), "%s.root", path);
        snprintf(moved, sizeof(moved), "%s.moved", path);
        if (mkdir(root, 0755) != 0 || astra_vfs_grant_derive(client, root + 6u, ASTRA_VFS_GRANT_READ, &replaced) != ASTRA_VFS_OK ||
            rename(root, moved) != 0 || mkdir(root, 0755) != 0 || stat(root, &st) != 0)
            return loss_fail("replacement root", errno);
        replacement_inode = (uint64_t)st.st_ino;
    }
    (void)astra_log_debug("FSSEM LOSS armed", sizeof("FSSEM LOSS armed") - 1u);
    printf("FSSEM LOSS armed\n");
    fflush(stdout);
    end = time(NULL) + seconds;
    while (time(NULL) < end) {
        if (write(fd, "0123456789abcdef", 16) == 16) {
            ++writes;
            (void)astra_rt_thread_sleep(UINT64_C(1000000), ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
            continue;
        }
        if (errno != ESTALE)
            return loss_fail("write", errno);
        printf("FSSEM LOSS write lost\n");
        if (fstat(fd, &st) == 0 || errno != ESTALE)
            return loss_fail("descriptor did not stay lost", errno);
        errno = 0;
        if (readdir(directory) != NULL || errno != ESTALE)
            return loss_fail("directory did not stay lost", errno);
        if (getcwd(cwd, sizeof(cwd)) != NULL || errno != ESTALE)
            return loss_fail("cwd did not stay lost", errno);
        printf("FSSEM LOSS identities lost\n");
        snprintf(again, sizeof(again), "%s.after", path);
        int fresh = open(again, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (reopen ? fresh < 0 : !(fresh < 0 && errno == ESTALE))
            return loss_fail("reopen", fresh < 0 ? errno : 0);
        if (fresh >= 0)
            close(fresh);
        printf("FSSEM LOSS fresh checked\n");
        if (reopen) {
            RawClient held, wrong;
            pid_t child;
            int status;

            if (raw_open(&held, derived) != ASTRA_SYSCALL_OK)
                return loss_fail("derived grant lost with live deriver", 0);
            raw_close(&held);
            uint32_t rejected = raw_open(&wrong, replaced);
            /* A live lost grant replies LOST; canonical retirement instead
             * closes its native endpoint before HELLO can admit authority. */
            int refused = raw_open_lost(&wrong, rejected);
            printf("FSSEM LOSS retained grant status=%lu stage=%s session=%lu replacement_inode=%llu\n",
                (unsigned long)rejected, wrong.stage, (unsigned long)wrong.session, (unsigned long long)replacement_inode);
            if (rejected == ASTRA_SYSCALL_OK) {
                AstraVfsReplyMessage reply = {0};
                uint32_t inspected = raw_call(&wrong, wrong.service, wrong.session,
                    ASTRA_VFS_OP_STAT, 0u, "/", &reply, NULL);
                printf("FSSEM LOSS retained root stat transport=%lu status=%lu valid=%lu volume=%llu inode=%llu\n",
                    (unsigned long)inspected, (unsigned long)reply.reply.status,
                    (unsigned long)reply.reply.meta.valid, (unsigned long long)reply.reply.meta.volume,
                    (unsigned long long)reply.reply.meta.inode);
            }
            fflush(stdout);
            raw_close(&wrong);
            if (!refused) {
                (void)astra_log_debug("FSSEM LOSS FAIL", sizeof("FSSEM LOSS FAIL") - 1u);
                printf("FSSEM LOSS FAIL retained grant rejection boundary status=%lu\n", (unsigned long)rejected);
                return 1;
            }
            child = fork();
            if (child < 0)
                return loss_fail("fork with disconnected descriptions", errno);
            if (child == 0)
                _exit(fstat(fd, &st) == -1 && errno == ESTALE &&
                    getcwd(cwd, sizeof(cwd)) == NULL && errno == ESTALE &&
                    chdir("/work") == 0 && getcwd(cwd, sizeof(cwd)) != NULL ? 0 : 2);
            if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                return loss_fail("child lost state", status);
            if (chdir("/work") != 0 || getcwd(cwd, sizeof(cwd)) == NULL)
                return loss_fail("absolute cwd recovery", errno);
            astra_close(derived);
            astra_close(replaced);
        }
        closedir(directory);
        printf("FSSEM LOSS directory closed\n");
        close(fd);
        (void)astra_log_debug("FSSEM LOSS PASS", sizeof("FSSEM LOSS PASS") - 1u);
        printf("FSSEM LOSS PASS after=%ld\n", writes);
        return 0;
    }
    (void)astra_log_debug("FSSEM LOSS none", sizeof("FSSEM LOSS none") - 1u);
    printf("FSSEM LOSS none after=%ld\n", writes);
    return 2;
}

int main(int argc, char **argv)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;

    if (argc == 3 && strcmp(argv[1], "--sync-file") == 0) {
        int fd = open(argv[2], O_RDONLY);
        int status = fd < 0 ? -1 : fsync(fd);
        int error = errno;
        if (fd >= 0 && close(fd) != 0 && status == 0) {
            status = -1;
            error = errno;
        }
        if (status != 0)
            fprintf(stderr, "fssem: sync %s: %s\n", argv[2], strerror(error));
        return status != 0;
    }

    if (argc == 3 && strcmp(argv[1], "--external-map") == 0)
        return external_map_probe(argv[2]);

    if (argc == 3 && strcmp(argv[1], "--hold-sessions") == 0)
        return hold_sessions((uint32_t)strtoul(argv[2], NULL, 10));

    if (argc == 3 && strcmp(argv[1], "--hold-map") == 0)
        return hold_mapping(argv[2]);

    if (argc == 4 && strcmp(argv[1], "--flood") == 0) {
        int result = flood(argv[2], (uint32_t)strtoul(argv[3], NULL, 10), 1);
        printf("FSSEM FLOOD %s %s\n", result == 0 ? "PASS" : "FAIL", failure);
        return result == 0 ? 0 : 1;
    }

    if (argc == 5 && strcmp(argv[1], "--loss-probe") == 0)
        return loss_probe(argv[2], (int)strtol(argv[3], NULL, 10), strcmp(argv[4], "reopen") == 0);

    /* N stats of one path through the Kit: the gate's attributable lane
     * load (test-fs-semantics.py check_lane). */
    if (argc == 4 && strcmp(argv[1], "--stat-loop") == 0) {
        long count = strtol(argv[2], NULL, 10);
        struct stat st;

        for (long at = 0; at < count; ++at)
            if (stat(argv[3], &st) != 0)
                return 2;
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--lane-helper") == 0) {
        struct stat st;

        return stat(argv[2], &st) == 0 && st.st_size == 4 ? 0 : 2;
    }
    if (argc == 3 && strcmp(argv[1], "--descriptor-helper") == 0) {
        int fd = (int)strtol(argv[2], NULL, 10);
        char byte;

        return read(fd, &byte, 1u) == 1 && byte == 'b' &&
               pread(fd, &byte, 1u, 0) == 1 && byte == 'a' &&
               lseek(fd, 0, SEEK_CUR) == 2 && close(fd) == 0 ? 0 : 2;
    }
    if (argc == 3 && strcmp(argv[1], "--wait-helper") == 0) {
        pid_t child = (pid_t)strtol(argv[2], NULL, 10);
        int status;

        return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
               WEXITSTATUS(status) == 42 ? 0 : 2;
    }
    if (argc == 3 && strcmp(argv[1], "--cwd-helper") == 0) {
        size_t capacity = strlen(argv[2]) + 1u;
        char *cwd = malloc(capacity);
        int result = cwd != NULL && getcwd(cwd, capacity) != NULL && strcmp(cwd, argv[2]) == 0 &&
            write_text("exec", "exec", O_WRONLY | O_CREAT | O_EXCL) == 0 ? 0 : 2;
        free(cwd);
        return result;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: fssem CASE...\n");
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        char directory[128];
        size_t at = 0u;

        while (at < sizeof(cases) / sizeof(cases[0]) &&
               strcmp(cases[at].name, argv[index]) != 0)
            ++at;
        if (at == sizeof(cases) / sizeof(cases[0])) {
            printf("FSSEM FAIL %s: no such case\n", argv[index]);
            ++failed;
            continue;
        }
        (void)snprintf(directory, sizeof(directory), "/work/fssem-%ld-%s-XXXXXX",
                       (long)getpid(), cases[at].name);
        failure[0] = '\0';
        if (mkdtemp(directory) == NULL) {
            printf("FSSEM FAIL %s: mkdtemp %s: %s\n", cases[at].name,
                   directory, strerror(errno));
            ++failed;
            continue;
        }
        if (cases[at].run(directory) == 0) {
            printf("FSSEM PASS %s\n", cases[at].name);
            ++passed;
        } else {
            printf("FSSEM FAIL %s: %s\n", cases[at].name, failure);
            ++failed;
        }
        fflush(stdout);
    }
    printf("FSSEM DONE passed %u failed %u\n", (unsigned)passed,
           (unsigned)failed);
    return failed == 0u ? 0 : 1;
}
