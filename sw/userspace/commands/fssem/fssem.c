/*
 * fssem -- the filesystem semantic suite (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * Phase 1 exit evidence).
 *
 *   fssem CASE...
 *
 * Each case runs in a fresh directory /work/fssem-<pid>-<case> and prints
 * "FSSEM PASS <case>" or "FSSEM FAIL <case>: <detail>". The last line is
 * "FSSEM DONE passed P failed F"; the exit status is 0 only when F is 0.
 * Cases use the POSIX layer unless their name says otherwise; the raw-*
 * cases go beneath it: raw-lane drives a host channel on its own scope, and
 * the grant cases (storage protocol 30, FS15) drive grant ports with raw
 * messages and lanes with the scopes those grants hand out -- what a hostile
 * program can send, with no Kit in the way.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <astra/host.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

typedef struct LaneCall {
    uint32_t scope;
    uint32_t session;
    uint16_t service;
    uint16_t operation;
    uint16_t flags;
    const char *path;
    uint32_t error;
    uint32_t status;
    AstraHostNode node;
} LaneCall;

static void *lane_call_thread(void *argument)
{
    LaneCall *call = argument;
    AstraHostChannelClient channel;
    AstraHostCommand *command;
    size_t length = strlen(call->path);

    memset(&channel, 0, sizeof(channel));
    call->error = astra_host_client_open(call->scope,
                                         ASTRA_HOST_CAP_FS_SCOPE,
                                         ASTRA_VFS_PATH_MAX + ASTRA_VFS_IO_MAX,
                                         &channel);
    if (call->error != ASTRA_SYSCALL_OK)
        return NULL;
    command = astra_host_client_prepare(&channel, call->service,
                                        call->operation);
    command->flags = call->flags;
    command->session = call->service == ASTRA_HOST_SERVICE_FILESYSTEM ?
                           call->session : 0u;
    command->value_lo = call->operation == ASTRA_HOST_FS_OPEN ? 0600u :
                                                                0755u;
    if (call->service == ASTRA_HOST_SERVICE_ENTROPY) {
        command->value_lo = 0u;
        command->data_capacity = 32u;
    } else {
        command->data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                               ASTRA_HOST_COMMAND_SIZE + (uint32_t)length;
        command->data_capacity = ASTRA_VFS_IO_MAX;
        if (length != 0u) {
            memcpy((void *)(uintptr_t)channel.data, call->path, length);
            command->path_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                                   ASTRA_HOST_COMMAND_SIZE;
            command->path_length = (uint16_t)length;
        }
    }
    call->error = astra_host_client_submit(&channel);
    if (call->error == ASTRA_SYSCALL_OK) {
        call->status = command->status;
        call->node = command->node;
    }
    (void)astra_host_client_close(&channel);
    return NULL;
}

/* One raw command on a lane of its own -- a thread has one host channel --
 * through `scope`: what a hostile program can send with a scope it holds.
 * Answers the provider's status, or ASTRA_STATUS_IO when the transport
 * itself failed. */
static uint32_t lane_call(uint32_t scope, uint32_t session,
                          uint16_t service, uint16_t operation,
                          uint16_t flags, const char *path, AstraHostNode *node)
{
    LaneCall call = {scope, session, service, operation, flags, path,
                     0u, 0u, {0}};
    pthread_t thread;

    if (pthread_create(&thread, NULL, lane_call_thread, &call) != 0 ||
        pthread_join(thread, NULL) != 0)
        return ASTRA_STATUS_IO;
    if (node != NULL)
        *node = call.node;
    return call.error == ASTRA_SYSCALL_OK ? call.status : ASTRA_STATUS_IO;
}

/* The session the kernel says `scope` binds. */
static uint32_t scope_session(uint32_t scope)
{
    AstraHostLeaseInfo lease = {0};

    return astra_host_lease_query(scope, &lease) == ASTRA_SYSCALL_OK ?
        lease.scope : 0u;
}

/*
 * A client's raw commands on its own lane (FS15, FS44): its own scope's
 * session answers; a forged session and every other service are refused
 * by the provider. The gate's `lane` check confirms nothing landed in
 * SOUND.
 */
static int case_raw_lane(const char *directory)
{
    const AstraAssign *work =
        astra_assign_lookup(astra_process_vfs_assigns(), "WORK");
    AstraVfsClient *client = astra_process_vfs_client_for(work);
    struct stat st;
    uint32_t scope;
    uint32_t own;
    uint32_t status;

    (void)directory;
    CHECK(stat("/work", &st) == 0 && client != NULL, "no WORK client");
    scope = client->port_direct_scope;
    own = scope_session(scope);
    CHECK(scope != 0u && own != 0u, "WORK is not on a lane");
    status = lane_call(scope, own, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_STAT, 0u, "/", NULL);
    CHECK(status == ASTRA_STATUS_OK, "own scope STAT: %u", (unsigned)status);
    /* SOUND's session is opened right after WORK's: the likeliest
     * forgery, and not bound to this lane. */
    status = lane_call(scope, own + 1u, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_MKDIR, 0u, "/raw-lane-forged", NULL);
    CHECK(status == ASTRA_STATUS_ACCESS, "forged session: %u",
          (unsigned)status);
    /* A lane cannot claim the filesystem or open a session of its own. */
    status = lane_call(scope, 0u, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_CLAIM, 0u, "", NULL);
    CHECK(status == ASTRA_STATUS_ACCESS, "claim on a lane: %u",
          (unsigned)status);
    status = lane_call(scope, own, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_SESSION_OPEN, 0u, "/", NULL);
    CHECK(status == ASTRA_STATUS_ACCESS, "session open on a lane: %u",
          (unsigned)status);
    status = lane_call(scope, own, ASTRA_HOST_SERVICE_ENTROPY,
                       ASTRA_HOST_ENTROPY_FILL, 0u, "", NULL);
    CHECK(status == ASTRA_STATUS_ACCESS, "entropy on a lane: %u",
          (unsigned)status);
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
    CHECK(ftruncate(fd, 1 << 20) == 0 && fstat(fd, &b) == 0 &&
          b.st_blksize > 0 && b.st_blocks < b.st_size / 512 &&
          b.st_mtim.tv_nsec >= 0 && b.st_mtim.tv_nsec < 1000000000,
          "provider allocation or timestamp metadata");
    CHECK(close(fd) == 0, "close identity fixture");
    return 0;
}

static int case_continuation_limit(const char *directory)
{
    char link[160], target[ASTRA_VFS_PATH_MAX];
    struct stat st;
    size_t length = strlen("/system");

    strcpy(target, "/system");
    while (length + 2u < sizeof(target)) {
        target[length++] = '/';
        target[length++] = '.';
    }
    target[length] = '\0';
    CHECK(length == ASTRA_VFS_PATH_MAX - 1u, "exact-limit fixture");
    join(link, sizeof(link), directory, "long");
    CHECK(symlink(target, link) == 0 && stat(link, &st) == 0 &&
          S_ISDIR(st.st_mode), "valid maximum-length continuation: %s", strerror(errno));
    return 0;
}

/* fork and exec keep host volumes working: the child opens its own lane on
 * the inherited scopes, and an exec'd image resumes them. The terminal's
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
          work->port_direct_scope != 0u && cwd->port_direct_scope != 0u &&
          work->port_direct_scope != cwd->port_direct_scope,
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
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0 && close(fd) == 0, "parent exit");
    CHECK(write(go[1], "g", 1u) == 1 && read(done[0], bytes, 1u) == 1,
          "description lost on parent exit");
    (void)close(go[1]); (void)close(done[0]);
    CHECK(read_text(path, bytes, sizeof(bytes)) == 6 &&
          strcmp(bytes, "QbXdef") == 0, "parent-exit data");
    return 0;
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
    uint32_t session;
    uint32_t scope;            /* what its HELLO attached */
} RawClient;

#define RAW_LANE 0x5EA1u

static uint32_t raw_open_until(RawClient *raw, uint32_t service,
                               uint64_t deadline);

static uint32_t raw_open(RawClient *raw, uint32_t service)
{
    return raw_open_until(raw, service, ASTRA_DEADLINE_FOREVER);
}

/* HELLO, answered by `deadline` or not at all: a service that declines a
 * HELLO answers nothing, and ASTRA_SYSCALL_TIMED_OUT says so. */
static uint32_t raw_open_until(RawClient *raw, uint32_t service,
                               uint64_t deadline)
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
    status = astra_rt_port_create(4u, 4u * (uint32_t)sizeof(reply),
                                  &raw->reply_receive, &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    memset(&message, 0, sizeof(message));
    astra_message_header_set(&message.header, (uint32_t)sizeof(message),
                             ASTRA_VFS_PROTOCOL, ASTRA_VFS_VERSION,
                             ASTRA_VFS_OP_HELLO, RAW_LANE);
    message.request.size = ASTRA_VFS_REQUEST_SIZE;
    message.request.version = ASTRA_VFS_VERSION;
    handles[0] = reply_send;
    status = astra_port_send(service, &message, sizeof(message), handles, 1u);
    (void)astra_close(reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_wait_one(raw->reply_receive, deadline, NULL);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_port_receive(raw->reply_receive, &reply, sizeof(reply),
                                    handles, 1u, &size, &count);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (reply.reply.status != ASTRA_VFS_OK)
        return reply.reply.status;
    raw->session = reply.reply.session;
    raw->scope = count != 0u ? handles[0] : 0u;
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
    (void)snprintf((char *)message.request.body.path, ASTRA_VFS_PATH_MAX,
                   "%s", path);
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

/* BYE, then drop the reply port and the scope: a raw client that leaves
 * tidily (review N4). */
static void raw_close(RawClient *raw)
{
    AstraVfsReplyMessage reply;

    if (raw->session != ASTRA_VFS_SESSION_INVALID)
        (void)raw_call(raw, raw->service, raw->session, ASTRA_VFS_OP_BYE, 0u,
                       "", &reply, NULL);
    if (raw->scope != 0u)
        (void)astra_close(raw->scope);
    if (raw->reply_receive != 0u)
        (void)astra_close(raw->reply_receive);
    memset(raw, 0, sizeof(*raw));
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
    uint32_t session;
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
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK && ro.scope != 0u,
          "HELLO on the derived grant attached no scope");
    CHECK(raw_call(&ro, ro.service, ro.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "widened a read-only grant: status %u", (unsigned)reply.reply.status);
    session = scope_session(ro.scope);
    wire_path(wire, sizeof(wire), directory, "ro-made");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_MKDIR, 0u, wire, NULL) == ASTRA_STATUS_READ_ONLY,
          "read-only MKDIR on the lane was not refused");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_OPEN,
                    ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, wire, NULL) ==
              ASTRA_STATUS_READ_ONLY,
          "read-only OPEN for writing on the lane was not refused");
    join(posix, sizeof(posix), directory, "ro-made");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT, "read-only MKDIR made it");
    join(posix, sizeof(posix), directory, "readable");
    CHECK(write_text(posix, "ok", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed readable");
    wire_path(wire, sizeof(wire), directory, "readable");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, wire, NULL) == ASTRA_STATUS_OK,
          "read-only STAT on the lane");
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
    /* The writable grant's provider session, on the read-only scope's lane. */
    wire_path(wire, sizeof(wire), directory, "borrowed");
    CHECK(lane_call(ro.scope, scope_session(rw.scope),
                    ASTRA_HOST_SERVICE_FILESYSTEM, ASTRA_HOST_FS_MKDIR, 0u,
                    wire, NULL) == ASTRA_STATUS_ACCESS,
          "a lane reached a session its scope does not bind");
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
    uint32_t session;
    char wire[160];
    char posix[160];
    AstraHostNode parent_meta, sub_meta;

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
    CHECK(lane_call(rw.scope, scope_session(rw.scope), ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, wire, &parent_meta) == ASTRA_STATUS_OK,
          "identity through WORK");
    session = scope_session(sub.scope);
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/inside", &sub_meta) == ASTRA_STATUS_OK,
          "sub sees inside");
    CHECK((parent_meta.valid & (ASTRA_HOST_NODE_INODE | ASTRA_HOST_NODE_DEVICE)) ==
              (ASTRA_HOST_NODE_INODE | ASTRA_HOST_NODE_DEVICE) &&
          parent_meta.inode_lo == sub_meta.inode_lo &&
          parent_meta.inode_hi == sub_meta.inode_hi &&
          parent_meta.device_lo == sub_meta.device_lo &&
          parent_meta.device_hi == sub_meta.device_hi,
          "derived root changed one node's identity");
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/../outside", NULL) == ASTRA_STATUS_ESCAPED,
          "sub reached its parent through ..");
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

/* Delegation never widens: a scope from a grant without DELEGATE arrives
 * without TRANSFER, and DELEGATE cannot be derived from a grant without it.
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
    uint32_t copy = 0u;
    int work_delegates;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    work_delegates = astra_rt_handle_duplicate(rw.scope, ASTRA_RIGHT_READ,
                                               &copy) == ASTRA_SYSCALL_OK;
    if (copy != 0u)
        (void)astra_close(copy);
    copy = 0u;
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_WRITE, "", &reply,
                   &derived) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_OK && derived != 0u,
          "derive without DELEGATE");
    CHECK(raw_open(&plain, derived) == ASTRA_SYSCALL_OK && plain.scope != 0u,
          "HELLO on the non-delegable grant");
    CHECK(astra_rt_handle_duplicate(plain.scope, ASTRA_RIGHT_READ, &copy) ==
              ASTRA_SYSCALL_ACCESS_DENIED,
          "a non-delegable scope arrived with TRANSFER");
    CHECK(raw_call(&plain, plain.service, plain.session,
                   ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, NULL) == ASTRA_SYSCALL_OK &&
          reply.reply.status == ASTRA_VFS_ERR_ACCESS,
          "DELEGATE derived from a grant without it");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ | ASTRA_VFS_GRANT_DELEGATE, "",
                   &reply, &again) == ASTRA_SYSCALL_OK &&
          (reply.reply.status == ASTRA_VFS_OK) == work_delegates,
          "derive DELEGATE from WORK (delegable %d): status %u",
          work_delegates, (unsigned)reply.reply.status);
    if (work_delegates) {
        CHECK(raw_open(&onward, again) == ASTRA_SYSCALL_OK &&
              astra_rt_handle_duplicate(onward.scope, ASTRA_RIGHT_READ,
                                        &copy) == ASTRA_SYSCALL_OK,
              "a delegable scope lost TRANSFER");
        (void)astra_close(copy);
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
    CHECK(raw_open(&mine, bound_grant) == ASTRA_SYSCALL_OK && mine.scope != 0u,
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
    AstraHostLeaseInfo lease;
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
    CHECK(raw_open(&held, derived[0]) == ASTRA_SYSCALL_OK && held.scope != 0u,
          "HELLO on a derived grant before its deriver left");
    raw_close(&rw);
    /* The deriver's BYE retires all of them: the held scope is revoked and
     * no derived port answers a HELLO again. */
    for (int tries = 0; tries < 40 && answer == ASTRA_SYSCALL_OK; ++tries) {
        struct timespec pause = {0, 50000000L};

        answer = astra_host_lease_query(held.scope, &lease);
        if (answer == ASTRA_SYSCALL_OK)
            (void)nanosleep(&pause, NULL);
    }
    CHECK(answer == ASTRA_SYSCALL_PEER_DEAD,
          "a derived grant's scope outlived its deriver: %u",
          (unsigned)answer);
    for (index = 0; index < DERIVED; ++index) {
        RawClient late;
        uint32_t status = raw_open_until(
            &late, derived[index],
            astra_clock_monotonic() + UINT64_C(2000000000));

        CHECK(status != ASTRA_SYSCALL_OK,
              "derived grant %d outlived its deriver", index);
        if (late.reply_receive != 0u)
            (void)astra_close(late.reply_receive);
        (void)astra_close(derived[index]);
    }
    (void)astra_close(held.scope);
    (void)astra_close(held.reply_receive);
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
            astra_clock_monotonic() + UINT64_C(2000000000));

        CHECK(status != ASTRA_SYSCALL_OK,
              "%s outlived its dead deriver",
              index == 0 ? "a grant" : "a grandchild grant");
        if (late.reply_receive != 0u)
            (void)astra_close(late.reply_receive);
        (void)astra_close(got[index]);
    }
    (void)astra_close(send);
    (void)astra_close(receive);
    return 0;
}

/* One channel held open across a grant's retirement: what "revocation
 * stops every bound channel at once" means for a client mid-flight. */
typedef struct HeldLane {
    uint32_t scope;
    uint32_t session;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int stage;                 /* 1 first STAT done, 2 go on, 3 finished */
    uint32_t first;
    uint32_t last;
    uint32_t error;
} HeldLane;

static uint32_t held_stat(AstraHostChannelClient *channel, uint32_t session)
{
    AstraHostCommand *command = astra_host_client_prepare(
        channel, ASTRA_HOST_SERVICE_FILESYSTEM, ASTRA_HOST_FS_STAT);
    uint32_t error;

    command->session = session;
    command->data_offset = 0u;
    memcpy((void *)(uintptr_t)channel->data, "/", 1u);
    command->path_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                           ASTRA_HOST_COMMAND_SIZE;
    command->path_length = 1u;
    error = astra_host_client_submit(channel);
    return error == ASTRA_SYSCALL_OK ? command->status : ASTRA_STATUS_IO;
}

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

static void *held_lane_thread(void *argument)
{
    HeldLane *held = argument;
    AstraHostChannelClient channel;

    memset(&channel, 0, sizeof(channel));
    held->error = astra_host_client_open(held->scope, ASTRA_HOST_CAP_FS_SCOPE,
                                         256u, &channel);
    held->first = held->error == ASTRA_SYSCALL_OK ?
        held_stat(&channel, held->session) : ASTRA_STATUS_IO;
    held_stage(held, 1);
    held_wait(held, 2);
    held->last = held->first;
    for (int tries = 0; held->error == ASTRA_SYSCALL_OK && tries < 40 &&
                        held->last == ASTRA_STATUS_OK; ++tries) {
        struct timespec pause = {0, 50000000L};

        (void)nanosleep(&pause, NULL);
        held->last = held_stat(&channel, held->session);
    }
    if (held->error == ASTRA_SYSCALL_OK)
        (void)astra_host_client_close(&channel);
    held_stage(held, 3);
    return NULL;
}

/* Retiring a grant -- its last port handle closed -- revokes its scope: a
 * channel bound to it then answers ACCESS, not the session's BAD_HANDLE
 * (Task 10 makes it LOST), and the scope opens no new channel. */
static int case_raw_retire(const char *directory)
{
    RawClient rw;
    RawClient ro;
    AstraVfsReplyMessage reply;
    AstraHostLeaseInfo lease;
    HeldLane held;
    pthread_t thread;
    uint32_t derived = 0u;

    (void)directory;
    CHECK(raw_open(&rw, work_grant()) == ASTRA_SYSCALL_OK, "HELLO on WORK");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "", &reply, &derived) ==
              ASTRA_SYSCALL_OK && derived != 0u, "derive");
    CHECK(raw_open(&ro, derived) == ASTRA_SYSCALL_OK, "HELLO on derived");
    memset(&held, 0, sizeof(held));
    held.scope = ro.scope;
    held.session = scope_session(ro.scope);
    CHECK(pthread_mutex_init(&held.mutex, NULL) == 0 &&
          pthread_cond_init(&held.changed, NULL) == 0 &&
          pthread_create(&thread, NULL, held_lane_thread, &held) == 0,
          "held lane thread");
    held_wait(&held, 1);
    if (held.first != ASTRA_STATUS_OK) {
        held_stage(&held, 2);
        (void)pthread_join(thread, NULL);
        return fail("the derived scope failed before retirement: %u/%u",
                    (unsigned)held.error, (unsigned)held.first);
    }
    (void)astra_close(derived);
    held_stage(&held, 2);
    CHECK(pthread_join(thread, NULL) == 0, "join held lane");
    CHECK(held.last == ASTRA_STATUS_LOST,
          "a retired grant's scope answered %u", (unsigned)held.last);
    CHECK(astra_host_lease_query(ro.scope, &lease) ==
              ASTRA_SYSCALL_PEER_DEAD,
          "a retired grant's scope still answers queries");
    ro.session = ASTRA_VFS_SESSION_INVALID; /* its grant is gone */
    raw_close(&ro);
    raw_close(&rw);
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
    RawClient held[65];
    uint32_t count = 0u;
    uint32_t status = ASTRA_SYSCALL_OK;
    int result = 0;
    pid_t child;
    int child_status;

    (void)directory;
    memset(held, 0, sizeof(held));
    while (count < 65u) {
        status = raw_open(&held[count], work_grant());
        if (status != ASTRA_SYSCALL_OK)
            break;
        ++count;
    }
    if (status != ASTRA_VFS_ERR_LIMIT || count != 63u)
        result = fail("owner sessions accepted %u status %u (one Kit session held)", count, status);
    if (count < 65u)
        raw_close(&held[count]);
    child = fork();
    if (child == 0) {
        RawClient other;
        uint32_t opened = raw_open(&other, work_grant());

        raw_close(&other);
        _exit(opened == ASTRA_SYSCALL_OK ? 0 : 1);
    }
    if (child < 0 || waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        result = fail("one owner's sessions blocked another owner");
    if (count != 0u) {
        raw_close(&held[--count]);
        status = raw_open(&held[count], work_grant());
        if (status != ASTRA_SYSCALL_OK)
            result = fail("closed session did not release quota: %u", status);
        else
            ++count;
    }
    while (count != 0u)
        raw_close(&held[--count]);
    return result;
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
    while (count != 0u)
        if (astra_filesystem_close(&held[--count]) != ASTRA_VFS_OK)
            result = fail("flood close did not release quota");
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

static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
    {"symlink-in-volume", case_symlink_in_volume},
    {"symlink-cross-volume", case_symlink_cross_volume},
    {"dotdot-at-root", case_dotdot_at_root},
    {"escape-relative", case_escape_relative},
    {"link-edges", case_link_edges},
    {"alias-identity", case_alias_identity},
    {"continuation-limit", case_continuation_limit},
    {"raw-lane", case_raw_lane},
    {"lane-fork-exec", case_lane_fork_exec},
    {"shared-description", case_shared_description},
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
    AstraVfsClient *client = astra_process_vfs_client_handle_lookup(work_grant());
    DIR *directory;
    int fd;

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
            rename(root, moved) != 0 || mkdir(root, 0755) != 0)
            return loss_fail("replacement root", errno);
    }
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
            if (raw_open(&wrong, replaced) != ASTRA_STATUS_LOST)
                return loss_fail("grant rebound to replacement root", 0);
            if (wrong.reply_receive != 0u)
                astra_close(wrong.reply_receive);
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
        printf("FSSEM LOSS PASS after=%ld\n", writes);
        return 0;
    }
    printf("FSSEM LOSS none after=%ld\n", writes);
    return 2;
}

int main(int argc, char **argv)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;

    if (argc == 3 && strcmp(argv[1], "--hold-sessions") == 0)
        return hold_sessions((uint32_t)strtoul(argv[2], NULL, 10));

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
        char cwd[192];

        return getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, argv[2]) == 0 &&
            write_text("exec", "exec", O_WRONLY | O_CREAT | O_EXCL) == 0 ? 0 : 2;
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
        (void)snprintf(directory, sizeof(directory), "/work/fssem-%ld-%s",
                       (long)getpid(), cases[at].name);
        failure[0] = '\0';
        if (mkdir(directory, 0755) != 0) {
            printf("FSSEM FAIL %s: mkdir %s: %s\n", cases[at].name,
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
