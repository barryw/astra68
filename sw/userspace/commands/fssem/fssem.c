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
 * the grant cases (storage protocol 29, FS15) drive grant ports with raw
 * messages and lanes with the scopes those grants hand out -- what a hostile
 * program can send, with no Kit in the way.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/host.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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

/* The `leaf` of a /work case directory, seen through /cwd: the shell's CWD
 * grant is derived from WORK at WORK's root. */
static void cwd_path(char *out, size_t capacity, const char *directory,
                     const char *leaf)
{
    (void)snprintf(out, capacity, "/cwd%s/%s", directory + strlen("/work"),
                   leaf);
}

/* fork and exec keep host volumes working: the child opens its own lane on
 * the inherited scopes, and an exec'd image resumes them. The terminal's
 * CWD is a grant of its own (Task 5), so a command holds two host-volume
 * clients -- WORK and CWD -- and both are live across the fork (Task 3b
 * review M6). No file is open at the fork: until Task 8, fork refuses while
 * one is. */
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
/* A storage-protocol client with no Kit: what a hostile program can send. */
typedef struct RawClient {
    uint32_t service;
    uint32_t reply_receive;
    uint32_t session;
    uint32_t scope;            /* what its HELLO attached */
} RawClient;

#define RAW_LANE 0x5EA1u

static uint32_t raw_open(RawClient *raw, uint32_t service)
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
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_wait_one(raw->reply_receive, ASTRA_DEADLINE_FOREVER, NULL);
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
                    ASTRA_HOST_FS_MKDIR, 0u, wire) == ASTRA_STATUS_READ_ONLY,
          "read-only MKDIR on the lane was not refused");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_OPEN,
                    ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE, wire) ==
              ASTRA_STATUS_READ_ONLY,
          "read-only OPEN for writing on the lane was not refused");
    join(posix, sizeof(posix), directory, "ro-made");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT, "read-only MKDIR made it");
    join(posix, sizeof(posix), directory, "readable");
    CHECK(write_text(posix, "ok", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "seed readable");
    wire_path(wire, sizeof(wire), directory, "readable");
    CHECK(lane_call(ro.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, wire) == ASTRA_STATUS_OK,
          "read-only STAT on the lane");
    (void)astra_close(ro.scope);
    (void)astra_close(rw.scope);
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
                    wire) == ASTRA_STATUS_ACCESS,
          "a lane reached a session its scope does not bind");
    join(posix, sizeof(posix), directory, "borrowed");
    CHECK(stat(posix, &st) != 0 && errno == ENOENT,
          "a refused request made the directory");
    (void)astra_close(ro.scope);
    (void)astra_close(rw.scope);
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
    session = scope_session(sub.scope);
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/inside") == ASTRA_STATUS_OK,
          "sub sees inside");
    CHECK(lane_call(sub.scope, session, ASTRA_HOST_SERVICE_FILESYSTEM,
                    ASTRA_HOST_FS_STAT, 0u, "/../outside") != ASTRA_STATUS_OK,
          "sub reached its parent through ..");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "../sound", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant outside the volume");
    CHECK(raw_call(&rw, rw.service, rw.session, ASTRA_VFS_OP_GRANT_DERIVE,
                   ASTRA_VFS_GRANT_READ, "/etc", &reply, NULL) ==
              ASTRA_SYSCALL_OK && reply.reply.status != ASTRA_VFS_OK,
          "derived a grant at an absolute root");
    (void)astra_close(sub.scope);
    (void)astra_close(rw.scope);
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
        (void)astra_close(onward.scope);
        (void)astra_close(again);
    }
    (void)astra_close(plain.scope);
    (void)astra_close(rw.scope);
    (void)astra_close(derived);
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
    CHECK(held.last == ASTRA_STATUS_ACCESS,
          "a retired grant's scope answered %u", (unsigned)held.last);
    CHECK(astra_host_lease_query(ro.scope, &lease) ==
              ASTRA_SYSCALL_PEER_DEAD,
          "a retired grant's scope still answers queries");
    (void)astra_close(ro.scope);
    (void)astra_close(rw.scope);
    return 0;
}

static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
    {"raw-lane", case_raw_lane},
    {"lane-fork-exec", case_lane_fork_exec},
    {"raw-readonly", case_raw_readonly},
    {"raw-cross-grant", case_raw_cross_grant},
    {"raw-subroot", case_raw_subroot},
    {"raw-delegate", case_raw_delegate},
    {"raw-retire", case_raw_retire},
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
