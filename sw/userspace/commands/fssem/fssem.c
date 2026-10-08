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
 * cases go beneath it (raw-lane drives a host channel on its own scope).
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_process.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

typedef struct LaneCall {
    uint32_t scope;
    uint32_t session;
    uint16_t service;
    uint16_t operation;
    uint16_t flags;
    const char *path;
    uint32_t error;
    uint32_t status;
} LaneCall;

static void *lane_call_thread(void *argument)
{
    LaneCall *call = argument;
    AstraHostChannelClient channel;
    AstraHostCommand *command;
    size_t length = strlen(call->path);

    memset(&channel, 0, sizeof(channel));
    call->error = astra_host_client_open(call->scope,
                                         ASTRA_HOST_CAP_FS_SCOPE, 256u,
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
        command->data_offset = 0u;
        if (length != 0u) {
            memcpy((void *)(uintptr_t)channel.data, call->path, length);
            command->path_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                                   ASTRA_HOST_COMMAND_SIZE;
            command->path_length = (uint16_t)length;
        }
    }
    call->error = astra_host_client_submit(&channel);
    if (call->error == ASTRA_SYSCALL_OK)
        call->status = command->status;
    (void)astra_host_client_close(&channel);
    return NULL;
}

/* One raw command on a lane of its own -- a thread has one host channel --
 * through `scope`: what a hostile program can send with a scope it holds.
 * Answers the provider's status, or ASTRA_STATUS_IO when the transport
 * itself failed. */
static uint32_t lane_call(uint32_t scope, uint32_t session,
                          uint16_t service, uint16_t operation,
                          uint16_t flags, const char *path)
{
    LaneCall call = {scope, session, service, operation, flags, path,
                     0u, 0u};
    pthread_t thread;

    if (pthread_create(&thread, NULL, lane_call_thread, &call) != 0 ||
        pthread_join(thread, NULL) != 0)
        return ASTRA_STATUS_IO;
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
                       ASTRA_HOST_FS_STAT, 0u, "/");
    CHECK(status == ASTRA_STATUS_OK, "own scope STAT: %u", (unsigned)status);
    /* SOUND's session is opened right after WORK's: the likeliest
     * forgery, and not bound to this lane. */
    status = lane_call(scope, own + 1u, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_MKDIR, 0u, "/raw-lane-forged");
    CHECK(status == ASTRA_STATUS_ACCESS, "forged session: %u",
          (unsigned)status);
    /* A lane cannot claim the filesystem or open a session of its own. */
    status = lane_call(scope, 0u, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_CLAIM, 0u, "");
    CHECK(status == ASTRA_STATUS_ACCESS, "claim on a lane: %u",
          (unsigned)status);
    status = lane_call(scope, own, ASTRA_HOST_SERVICE_FILESYSTEM,
                       ASTRA_HOST_FS_SESSION_OPEN, 0u, "/");
    CHECK(status == ASTRA_STATUS_ACCESS, "session open on a lane: %u",
          (unsigned)status);
    status = lane_call(scope, own, ASTRA_HOST_SERVICE_ENTROPY,
                       ASTRA_HOST_ENTROPY_FILL, 0u, "");
    CHECK(status == ASTRA_STATUS_ACCESS, "entropy on a lane: %u",
          (unsigned)status);
    return 0;
}

/* fork and exec keep host volumes working: the child opens its own lane on
 * the inherited scope, and an exec'd image resumes it. No file is open at
 * the fork: until Task 8, fork refuses while one is. */
static int case_lane_fork_exec(const char *directory)
{
    char path[160];
    struct stat st;
    pid_t child;
    int status;

    join(path, sizeof(path), directory, "lane-file");
    CHECK(write_text(path, "lane", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed");
    child = fork();
    CHECK(child >= 0, "fork: %s", strerror(errno));
    if (child == 0)
        _exit(stat(path, &st) == 0 && st.st_size == 4 ? 0 : 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0, "fork child could not stat on its lane");
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
static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
    {"raw-lane", case_raw_lane},
    {"lane-fork-exec", case_lane_fork_exec},
};

int main(int argc, char **argv)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;

    if (argc == 3 && strcmp(argv[1], "--lane-helper") == 0) {
        struct stat st;

        return stat(argv[2], &st) == 0 && st.st_size == 4 ? 0 : 2;
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
