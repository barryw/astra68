/*
 * The storage service core, driven directly with no transport.
 *
 * The backend here is a fixed in-memory tree, not lwext4, and that is the
 * point twice over: it proves the core has no filesystem knowledge in it, and
 * it lets the misuse cases -- stale handles, one session reaching for
 * another's file, malformed records -- be provoked exactly, which is awkward
 * against a real volume and is where the security-relevant behaviour lives.
 */

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <astra/vfs_local_transport.h>
#include <astra/vfs_service_core.h>

#define FAKE_NODE_MAX 8u
#define FAKE_BYTES_MAX 512u

typedef struct FakeNode {
    char path[ASTRA_VFS_PATH_MAX];
    uint8_t bytes[FAKE_BYTES_MAX];
    uint32_t size;
    uint16_t kind;
    uint16_t mode;
    int used;
    int open_count;
} FakeNode;

typedef struct FakeFs {
    FakeNode nodes[FAKE_NODE_MAX];
    uint32_t closes;
} FakeFs;

static FakeFs fake;

typedef struct ConcurrentGate {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int enabled;
    int slow_entered;
    int release_slow;
    int fast_finished;
    int close_started;
    int close_finished;
} ConcurrentGate;

static ConcurrentGate concurrent_gate;
static pthread_cond_t file_condition;
static uint32_t state_wakes;

static void
hold_slow_backend(void)
{
    if (!concurrent_gate.enabled)
        return;
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.slow_entered = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    while (!concurrent_gate.release_slow)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
}

static FakeNode *
fake_find(const char *path)
{
    uint32_t index;

    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        if (fake.nodes[index].used &&
            strcmp(fake.nodes[index].path, path) == 0) {
            return &fake.nodes[index];
        }
    }
    return NULL;
}

static FakeNode *
fake_create(const char *path, uint16_t kind)
{
    uint32_t index;

    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        if (!fake.nodes[index].used) {
            fake.nodes[index].used = 1;
            fake.nodes[index].kind = kind;
            fake.nodes[index].mode = kind == ASTRA_VFS_KIND_DIRECTORY ?
                                         0755u : 0644u;
            fake.nodes[index].size = 0u;
            fake.nodes[index].open_count = 0;
            snprintf(fake.nodes[index].path, sizeof(fake.nodes[index].path),
                     "%s", path);
            return &fake.nodes[index];
        }
    }
    return NULL;
}

static uint32_t
fake_open(void *context, const char *path, uint32_t flags,
          uint16_t create_mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    FakeNode *found = fake_find(path);
    int created = 0;

    (void)context;
    if (found != NULL &&
        (flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
            (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        return ASTRA_VFS_ERR_EXISTS;
    }
    if (found == NULL) {
        if ((flags & ASTRA_VFS_OPEN_CREATE) == 0u) {
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        found = fake_create(path, ASTRA_VFS_KIND_FILE);
        if (found == NULL) {
            return ASTRA_VFS_ERR_NO_SPACE;
        }
        created = 1;
    }
    if (created && create_mode != ASTRA_VFS_MODE_DEFAULT)
        found->mode = create_mode;
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) {
        found->size = 0u;
    }
    ++found->open_count;
    *node = (uintptr_t)found;
    info->size = found->size;
    info->kind = found->kind;
    info->mode = found->mode;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_close(void *context, uintptr_t node)
{
    FakeNode *found = (FakeNode *)node;

    (void)context;
    ++fake.closes;
    if (found != NULL && found->open_count > 0) {
        --found->open_count;
    }
    return ASTRA_VFS_OK;
}

static uint32_t
fake_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
          uint32_t length, uint32_t *moved)
{
    FakeNode *found = (FakeNode *)node;
    uint32_t available;

    (void)context;
    if (found == fake_find("/slow"))
        hold_slow_backend();
    *moved = 0u;
    if (offset > found->size) {
        return ASTRA_VFS_ERR_INVALID;
    }
    available = found->size - (uint32_t)offset;
    if (length > available) {
        length = available;
    }
    memcpy(buffer, &found->bytes[offset], length);
    *moved = length;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_write(void *context, uintptr_t node, uint64_t offset, uint32_t flags,
           const void *buffer, uint32_t length, uint32_t *moved,
           uint64_t *position)
{
    FakeNode *found = (FakeNode *)node;

    (void)context;
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u)
        offset = found->size;
    *moved = 0u;
    *position = offset;
    if (offset + length > FAKE_BYTES_MAX) {
        return ASTRA_VFS_ERR_NO_SPACE;
    }
    memcpy(&found->bytes[offset], buffer, length);
    if (offset + length > found->size) {
        found->size = (uint32_t)(offset + length);
    }
    *moved = length;
    *position = offset + length;
    return ASTRA_VFS_OK;
}

static uint32_t fake_sync(void *context, uintptr_t node)
{
    (void)context;
    return node == 0u ? ASTRA_VFS_ERR_BAD_HANDLE : ASTRA_VFS_OK;
}

static uint32_t fake_truncate(void *context, uintptr_t node, uint64_t size)
{
    FakeNode *found = (FakeNode *)node;

    (void)context;
    if (found == NULL || size > FAKE_BYTES_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (size > found->size)
        memset(&found->bytes[found->size], 0, (size_t)size - found->size);
    found->size = (uint32_t)size;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (strcmp(path, "/slow") == 0)
        hold_slow_backend();
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    info->size = found->size;
    info->kind = found->kind;
    info->mode = found->mode;
    return ASTRA_VFS_OK;
}

/*
 * The cookie is the slot after the one returned, so a scan resumes where it
 * stopped without counting from the first node. `fake_readdir_scans` is how a
 * test sees that a listing costs one visit per entry rather than one walk.
 */
static uint32_t fake_readdir_visits;

static uint32_t
fake_readdir(void *context, uintptr_t directory, const char *path,
             uint64_t cookie, char *name, uint32_t capacity,
             AstraVfsNodeInfo *info, uint64_t *next)
{
    uint32_t slot;

    (void)context;
    (void)directory;
    (void)path;
    if (cookie > FAKE_NODE_MAX) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (slot = (uint32_t)cookie; slot < FAKE_NODE_MAX; ++slot) {
        ++fake_readdir_visits;
        if (!fake.nodes[slot].used) {
            continue;
        }
        snprintf(name, capacity, "%s", fake.nodes[slot].path);
        info->size = fake.nodes[slot].size;
        info->kind = fake.nodes[slot].kind;
        /*
         * Values a decoder could not have guessed, and each one a different
         * width, so a field packed at the wrong offset or in the wrong byte
         * order shows up as a wrong number rather than as a plausible zero.
         */
        info->mode = (uint16_t)(0100644u + slot);
        info->nlink = (uint16_t)(slot + 3u);
        info->uid = 0x11223344u + slot;
        info->gid = 0x55667788u + slot;
        info->mtime = (int64_t)0x0000000123456789 + slot;
        *next = slot + 1u;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

static uint32_t
fake_mkdir(void *context, const char *path, uint16_t create_mode)
{
    FakeNode *created;

    (void)context;
    if (fake_find(path) != NULL) {
        return ASTRA_VFS_ERR_EXISTS;
    }
    created = fake_create(path, ASTRA_VFS_KIND_DIRECTORY);
    if (created == NULL)
        return ASTRA_VFS_ERR_NO_SPACE;
    if (create_mode != ASTRA_VFS_MODE_DEFAULT)
        created->mode = create_mode;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_unlink(void *context, const char *path)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    found->used = 0;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_rename(void *context, const char *from, const char *to)
{
    FakeNode *source = fake_find(from);
    FakeNode *target = fake_find(to);

    (void)context;
    if (source == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (target != NULL && target != source)
        target->used = 0;
    (void)snprintf(source->path, sizeof(source->path), "%s", to);
    return ASTRA_VFS_OK;
}

static uint32_t
fake_chmod(void *context, const char *path, uint16_t mode)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    found->mode = mode;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_readlink(void *context, const char *path, void *buffer,
              uint32_t capacity, uint32_t *length)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (capacity < found->size)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, found->bytes, found->size);
    *length = found->size;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_symlink(void *context, const char *target, const char *path)
{
    FakeNode *created;

    (void)context;
    if (fake_find(path) != NULL)
        return ASTRA_VFS_ERR_EXISTS;
    created = fake_create(path, ASTRA_VFS_KIND_SYMLINK);
    if (created == NULL)
        return ASTRA_VFS_ERR_NO_SPACE;
    created->size = (uint32_t)strlen(target);
    memcpy(created->bytes, target, created->size);
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps fake_ops = {
    fake_open, fake_close, fake_read, fake_write, fake_sync, fake_truncate,
    fake_stat, fake_readdir, fake_mkdir, fake_unlink, fake_rename,
    fake_chmod, fake_readlink, fake_symlink
};

static AstraVfsService service;
static AstraVfsSessionSlot service_sessions[ASTRA_VFS_SESSION_MAX];
static AstraVfsCallState counted_call;
static uint32_t call_acquires;

static AstraVfsCallState *
count_call_acquire(AstraVfsClient *client)
{
    assert(client != NULL);
    ++call_acquires;
    return &counted_call;
}
static AstraVfsOpenFile service_files[128];

static void
begin_request(AstraVfsRequest *request, uint32_t session, const char *path)
{
    memset(request, 0, sizeof(*request));
    request->size = (uint16_t)ASTRA_VFS_REQUEST_SIZE;
    request->version = ASTRA_VFS_VERSION;
    request->session = session;
    if (path != NULL) {
        snprintf((char *)request->body.path, ASTRA_VFS_PATH_MAX, "%s", path);
    }
}

static uint32_t
open_session(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;

    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.version == ASTRA_VFS_VERSION);
    return reply.session;
}

static void
reset(void)
{
    memset(&fake, 0, sizeof(fake));
    assert(astra_vfs_service_init(
        &service, &fake_ops, NULL, service_sessions,
        ASTRA_VFS_SESSION_MAX, service_files,
        (uint32_t)(sizeof(service_files) / sizeof(service_files[0]))));
}

static int
test_lock_acquire(void *context)
{
    return pthread_mutex_lock(context) == 0;
}

static void
test_lock_release(void *context)
{
    assert(pthread_mutex_unlock(context) == 0);
}

static int
test_state_wait(void *context, volatile uint32_t *sequence,
                uint32_t expected)
{
    pthread_mutex_t *mutex = context;

    while (*sequence == expected)
        assert(pthread_cond_wait(&file_condition, mutex) == 0);
    return 1;
}

static void
test_state_wake(void *context, volatile uint32_t *sequence)
{
    (void)context;
    (void)sequence;
    ++state_wakes;
    assert(pthread_cond_broadcast(&file_condition) == 0);
}

static void
test_uncontended_file_operation_does_not_wake(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t file;

    reset();
    assert(fake_create("/file", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_cond_init(&file_condition, NULL) == 0);
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    assert(astra_vfs_service_set_state_wait(
        &service, test_state_wait, test_state_wake));
    session = open_session();
    begin_request(&request, session, "/file");
    request.flags = ASTRA_VFS_OPEN_READ;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    state_wakes = 0u;
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 1u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(state_wakes == 0u);
    assert(pthread_cond_destroy(&file_condition) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

typedef struct ConcurrentRequest {
    uint32_t session;
    const char *path;
    AstraVfsReply reply;
} ConcurrentRequest;

static void *
run_stat(void *context)
{
    ConcurrentRequest *call = context;
    AstraVfsRequest request;

    begin_request(&request, call->session, call->path);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request,
                               &call->reply);
    if (strcmp(call->path, "/fast") == 0) {
        assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
        concurrent_gate.fast_finished = 1;
        assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
        assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    }
    return NULL;
}

static void
test_unrelated_session_overtakes_held_backend(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t slow_thread;
    pthread_t fast_thread;
    ConcurrentRequest slow;
    ConcurrentRequest fast;
    struct timespec deadline;
    int fast_overtook;

    reset();
    assert(fake_create("/slow", ASTRA_VFS_KIND_FILE) != NULL);
    assert(fake_create("/fast", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
    assert(pthread_cond_init(&file_condition, NULL) == 0);
    concurrent_gate.enabled = 1;
    concurrent_gate.slow_entered = 0;
    concurrent_gate.release_slow = 0;
    concurrent_gate.fast_finished = 0;
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    assert(astra_vfs_service_set_state_wait(
        &service, test_state_wait, test_state_wake));
    slow.session = open_session();
    slow.path = "/slow";
    fast.session = open_session();
    fast.path = "/fast";

    assert(pthread_create(&slow_thread, NULL, run_stat, &slow) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_create(&fast_thread, NULL, run_stat, &fast) == 0);

    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    ++deadline.tv_sec;
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.fast_finished) {
        if (pthread_cond_timedwait(&concurrent_gate.changed,
                                   &concurrent_gate.mutex, &deadline) != 0)
            break;
    }
    fast_overtook = concurrent_gate.fast_finished;
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);

    assert(pthread_join(fast_thread, NULL) == 0);
    assert(pthread_join(slow_thread, NULL) == 0);
    assert(fast_overtook);
    assert(fast.reply.status == ASTRA_VFS_OK);
    assert(slow.reply.status == ASTRA_VFS_OK);
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_cond_destroy(&file_condition) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

static void
test_same_session_independent_request_overtakes_held_backend(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t slow_thread;
    pthread_t fast_thread;
    ConcurrentRequest slow;
    ConcurrentRequest fast;
    struct timespec deadline;
    int fast_overtook;

    reset();
    assert(fake_create("/slow", ASTRA_VFS_KIND_FILE) != NULL);
    assert(fake_create("/fast", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
    concurrent_gate.enabled = 1;
    concurrent_gate.slow_entered = 0;
    concurrent_gate.release_slow = 0;
    concurrent_gate.fast_finished = 0;
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    slow.session = open_session();
    slow.path = "/slow";
    fast.session = slow.session;
    fast.path = "/fast";

    assert(pthread_create(&slow_thread, NULL, run_stat, &slow) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_create(&fast_thread, NULL, run_stat, &fast) == 0);

    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    ++deadline.tv_sec;
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.fast_finished) {
        if (pthread_cond_timedwait(&concurrent_gate.changed,
                                   &concurrent_gate.mutex, &deadline) != 0)
            break;
    }
    fast_overtook = concurrent_gate.fast_finished;
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);

    assert(pthread_join(fast_thread, NULL) == 0);
    assert(pthread_join(slow_thread, NULL) == 0);
    assert(fast_overtook);
    assert(fast.reply.status == ASTRA_VFS_OK);
    assert(slow.reply.status == ASTRA_VFS_OK);
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

typedef struct ConcurrentRead {
    uint32_t session;
    AstraVfsFile file;
    AstraVfsReply reply;
} ConcurrentRead;

typedef struct ConcurrentClose {
    uint32_t session;
    AstraVfsFile file;
    AstraVfsReply reply;
} ConcurrentClose;

static void *
run_read(void *context)
{
    ConcurrentRead *call = context;
    AstraVfsRequest request;

    begin_request(&request, call->session, NULL);
    request.file = call->file;
    request.length = 1u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request,
                               &call->reply);
    return NULL;
}

static void *
run_close(void *context)
{
    ConcurrentClose *call = context;
    AstraVfsRequest request;

    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.close_started = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    begin_request(&request, call->session, NULL);
    request.file = call->file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request,
                               &call->reply);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.close_finished = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    return NULL;
}

static void
test_close_waits_for_active_file_operation(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t read_thread;
    pthread_t close_thread;
    ConcurrentRead read_call;
    ConcurrentClose close_call;
    AstraVfsRequest request;
    AstraVfsReply reply;
    struct timespec deadline;

    reset();
    assert(fake_create("/slow", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
    assert(pthread_cond_init(&file_condition, NULL) == 0);
    memset(&concurrent_gate.enabled, 0,
           sizeof(concurrent_gate) - offsetof(ConcurrentGate, enabled));
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    assert(astra_vfs_service_set_state_wait(
        &service, test_state_wait, test_state_wake));
    read_call.session = open_session();
    begin_request(&request, read_call.session, "/slow");
    request.flags = ASTRA_VFS_OPEN_READ;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    read_call.file = reply.file;
    close_call.session = read_call.session;
    close_call.file = read_call.file;
    concurrent_gate.enabled = 1;

    assert(pthread_create(&read_thread, NULL, run_read, &read_call) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_create(&close_thread, NULL, run_close, &close_call) == 0);
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_nsec += 100000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        ++deadline.tv_sec;
    }
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.close_started)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    while (!concurrent_gate.close_finished &&
           pthread_cond_timedwait(&concurrent_gate.changed,
                                  &concurrent_gate.mutex, &deadline) == 0) {}
    assert(!concurrent_gate.close_finished);
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_join(read_thread, NULL) == 0);
    assert(pthread_join(close_thread, NULL) == 0);
    assert(read_call.reply.status == ASTRA_VFS_OK);
    assert(close_call.reply.status == ASTRA_VFS_OK);
    assert(fake.closes == 1u);
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_cond_destroy(&file_condition) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

static void
test_session_release_waits_for_pinned_file(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t thread;
    ConcurrentRead call;
    AstraVfsRequest request;
    AstraVfsReply reply;
    FakeNode *slow;

    reset();
    slow = fake_create("/slow", ASTRA_VFS_KIND_FILE);
    assert(slow != NULL);
    assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
    concurrent_gate.enabled = 0;
    concurrent_gate.slow_entered = 0;
    concurrent_gate.release_slow = 0;
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    call.session = open_session();
    begin_request(&request, call.session, "/slow");
    request.flags = ASTRA_VFS_OPEN_READ;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    call.file = reply.file;
    concurrent_gate.enabled = 1;

    assert(pthread_create(&thread, NULL, run_read, &call) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    astra_vfs_service_release_session(&service, call.session);
    assert(fake.closes == 0u);

    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.reply.status == ASTRA_VFS_OK);
    assert(fake.closes == 1u);
    assert(slow->open_count == 0);
    assert(!astra_vfs_service_session_owned(&service, call.session, 0u));
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

#define MODEL_WORKERS 4u
#define MODEL_OPERATIONS_PER_WORKER 250000u
#define MODEL_BYTES 64u

typedef struct ModelWorker {
    char path[16];
    uint8_t bytes[MODEL_BYTES];
    uint32_t size;
    uint32_t session;
    AstraVfsFile file;
    uint32_t random;
} ModelWorker;

static atomic_uint_fast64_t model_transitions[ASTRA_VFS_TEST_TRANSITION_COUNT];

static uint32_t
model_random(ModelWorker *worker)
{
    uint32_t value = worker->random;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    worker->random = value;
    return value;
}

static void
force_model_switch(void *context, uint32_t transition)
{
    (void)context;
    assert(transition < ASTRA_VFS_TEST_TRANSITION_COUNT);
    (void)atomic_fetch_add_explicit(&model_transitions[transition], 1u,
                                    memory_order_relaxed);
    assert(sched_yield() == 0);
}

static void *
run_model(void *context)
{
    ModelWorker *worker = context;
    AstraVfsRequest request;
    AstraVfsReply reply;

    for (uint32_t operation = 0u;
         operation < MODEL_OPERATIONS_PER_WORKER; ++operation) {
        uint32_t random = model_random(worker);

        begin_request(&request, worker->session, NULL);
        request.file = worker->file;
        switch (random & 3u) {
        case 0u: {
            uint32_t offset = worker->size == 0u ? 0u :
                (random >> 8) % (worker->size + 1u);
            uint32_t length = 1u + ((random >> 16) & 15u);

            if (offset == MODEL_BYTES)
                --offset;
            if (length > MODEL_BYTES - offset)
                length = MODEL_BYTES - offset;
            request.offset = offset;
            request.length = length;
            for (uint32_t index = 0u; index < length; ++index)
                request.body.payload[index] =
                    (uint8_t)(random + operation + index);
            astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE,
                                       &request, &reply);
            assert(reply.status == ASTRA_VFS_OK && reply.count == length);
            memcpy(&worker->bytes[offset], request.body.payload, length);
            if (offset + length > worker->size)
                worker->size = offset + length;
            break;
        }
        case 1u: {
            uint32_t offset = worker->size == 0u ? 0u :
                (random >> 8) % (worker->size + 1u);
            uint32_t length = 1u + ((random >> 16) & 31u);
            uint32_t expected = worker->size - offset;

            if (expected > length)
                expected = length;
            request.offset = offset;
            request.length = length;
            astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ,
                                       &request, &reply);
            assert(reply.status == ASTRA_VFS_OK && reply.count == expected);
            assert(memcmp(reply.payload, &worker->bytes[offset], expected) == 0);
            break;
        }
        case 2u: {
            uint32_t size = (random >> 8) % (MODEL_BYTES + 1u);

            request.offset = size;
            astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_TRUNCATE,
                                       &request, &reply);
            assert(reply.status == ASTRA_VFS_OK);
            if (size > worker->size)
                memset(&worker->bytes[worker->size], 0, size - worker->size);
            worker->size = size;
            break;
        }
        default:
            if ((random & 4u) != 0u) {
                astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_SYNC,
                                           &request, &reply);
                assert(reply.status == ASTRA_VFS_OK);
            } else {
                begin_request(&request, worker->session, worker->path);
                astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT,
                                           &request, &reply);
                assert(reply.status == ASTRA_VFS_OK &&
                       reply.node_size == worker->size);
            }
            break;
        }
    }
    return NULL;
}

static void
test_forced_switch_model(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t threads[MODEL_WORKERS];
    ModelWorker workers[MODEL_WORKERS];
    AstraVfsRequest request;
    AstraVfsReply reply;

    reset();
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    for (uint32_t index = 0u; index < ASTRA_VFS_TEST_TRANSITION_COUNT; ++index)
        atomic_store_explicit(&model_transitions[index], 0u,
                              memory_order_relaxed);
    astra_vfs_service_set_test_transition(&service, force_model_switch, NULL);

    for (uint32_t index = 0u; index < MODEL_WORKERS; ++index) {
        ModelWorker *worker = &workers[index];

        memset(worker, 0, sizeof(*worker));
        (void)snprintf(worker->path, sizeof(worker->path), "/model-%u", index);
        assert(fake_create(worker->path, ASTRA_VFS_KIND_FILE) != NULL);
        worker->session = open_session();
        worker->random = UINT32_C(0x68a50000) + index + 1u;
        begin_request(&request, worker->session, worker->path);
        request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
        astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN,
                                   &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        worker->file = reply.file;
    }
    for (uint32_t index = 0u; index < MODEL_WORKERS; ++index)
        assert(pthread_create(&threads[index], NULL, run_model,
                              &workers[index]) == 0);
    for (uint32_t index = 0u; index < MODEL_WORKERS; ++index)
        assert(pthread_join(threads[index], NULL) == 0);

    for (uint32_t index = 0u; index < MODEL_WORKERS; ++index) {
        begin_request(&request, workers[index].session, NULL);
        request.file = workers[index].file;
        astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE,
                                   &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        astra_vfs_service_release_session(&service, workers[index].session);
    }
    for (uint32_t index = 0u; index < ASTRA_VFS_TEST_TRANSITION_COUNT; ++index)
        assert(atomic_load_explicit(&model_transitions[index],
                                    memory_order_relaxed) != 0u);
    assert(service.open_files == 0u && service.open_sessions == 0u);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
    puts("forced-switch model: 1000000 operations PASS");
}

/* A session must be agreed before anything else is answered. */
static void
test_session_handshake(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    reset();
    /* No session yet: a path operation is refused rather than served. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, "/a.txt");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    session = open_session();
    assert(session != ASTRA_VFS_SESSION_INVALID);

    /*
     * A client that speaks a newer protocol than the service gets the
     * service's version, not its own. This is the check that keeps an
     * upgraded client from assuming fields the service will never send.
     */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION + 5u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.version == ASTRA_VFS_VERSION);

    /* Below the floor is refused outright rather than downgraded. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION_MIN - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
}

static void
test_transport_owner_isolation_and_fair_file_share(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    const uint32_t owner = 0x10000001u;
    uint32_t owner_sessions[ASTRA_LAUNCH_GRANT_MAX];
    uint32_t session;
    uint32_t peer_session;

    reset();
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    session = reply.session;
    owner_sessions[0] = session;
    assert(astra_vfs_service_session_owned(&service, session, owner));
    assert(!astra_vfs_service_session_owned(&service, session, owner + 1u));

    for (uint32_t index = 1u; index < ASTRA_LAUNCH_GRANT_MAX; ++index) {
        begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
        astra_vfs_service_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                        &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        owner_sessions[index] = reply.session;
    }
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(service.stats.owner_session_quota_denied == 1u);
    astra_vfs_service_dispatch_from(&service, owner + 1u,
                                    ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    peer_session = reply.session;

    begin_request(&request, session, "/owned.txt");
    astra_vfs_service_dispatch_from(&service, owner + 1u, ASTRA_VFS_OP_STAT,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);

    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof(service_files) /
                            sizeof(service_files[0])) /
                     ASTRA_PROCESS_COUNT_MAX;
         ++index) {
        astra_vfs_service_dispatch_from(&service, owner, ASTRA_VFS_OP_OPEN,
                                        &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    astra_vfs_service_dispatch_from(&service, owner, ASTRA_VFS_OP_OPEN,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(astra_vfs_service_stats(&service)->owner_quota_denied == 1u);
    astra_vfs_service_release_session(&service, peer_session);
    for (uint32_t index = 0u; index < ASTRA_LAUNCH_GRANT_MAX; ++index)
        astra_vfs_service_release_session(&service, owner_sessions[index]);
}

static void
test_file_round_trip(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    AstraVfsFile file;
    static const char payload[] = "astra";

    reset();
    session = open_session();

    begin_request(&request, session, "/hello.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                    ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;
    assert(file != ASTRA_VFS_FILE_INVALID);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = (uint32_t)(sizeof(payload) - 1u);
    memcpy(request.body.payload, payload, sizeof(payload) - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 64u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);
    assert(memcmp(reply.payload, payload, sizeof(payload) - 1u) == 0);

    begin_request(&request, session, "/hello.txt");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.node_size == sizeof(payload) - 1u);
    assert(reply.kind == ASTRA_VFS_KIND_FILE);

    begin_request(&request, session, NULL);
    request.file = file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    begin_request(&request, session, "/hello.txt");
    request.flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                    ASTRA_VFS_OPEN_EXCLUSIVE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_EXISTS);

    begin_request(&request, session, "/exclusive.txt");
    request.flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                    ASTRA_VFS_OPEN_EXCLUSIVE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    begin_request(&request, session, NULL);
    request.file = reply.file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
}

/*
 * The properties the core exists for. A backend cannot enforce any of these
 * because it never sees a session or a handle.
 */
static void
test_handle_discipline(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t first;
    uint32_t second;
    AstraVfsFile file;
    AstraVfsFile reused;

    reset();
    first = open_session();
    second = open_session();
    assert(first != second);

    begin_request(&request, first, "/owned.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    /* Another session may not touch it, even holding the exact handle. */
    begin_request(&request, second, NULL);
    request.file = file;
    request.length = 8u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_stats(&service)->cross_session_denied == 1u);

    /* Closed, then reused: the old handle must not reach the new file. */
    begin_request(&request, first, NULL);
    request.file = file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    begin_request(&request, first, "/other.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    reused = reply.file;
    assert(reused != file); /* the generation moved */

    begin_request(&request, first, NULL);
    request.file = file;
    request.length = 8u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_vfs_service_stats(&service)->stale_handles == 1u);

    /* A write to a read-only handle is refused by the core, not the backend. */
    begin_request(&request, first, NULL);
    request.file = reused;
    request.length = 4u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
}

/* A dead client must not strand the files it had open. */
static void
test_session_release_frees_files(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;

    reset();
    session = open_session();
    for (index = 0u; index < 4u; ++index) {
        char path[32];

        snprintf(path, sizeof(path), "/f%u", index);
        begin_request(&request, session, path);
        request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
        astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request,
                                   &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    assert(astra_vfs_service_stats(&service)->files_opened == 4u);

    astra_vfs_service_release_session(&service, session);
    assert(astra_vfs_service_stats(&service)->files_closed == 4u);
    assert(fake.closes == 4u);
    /* Every backend node was closed exactly as many times as it was opened. */
    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        assert(fake.nodes[index].open_count == 0);
    }
}

/*
 * Malformed records get a complete reply, not silence. A client cannot tell a
 * dropped message from a dead service, so the service never drops one.
 */
static void
test_malformed_records(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;

    reset();
    session = open_session();

    begin_request(&request, session, "/a");
    request.size = (uint16_t)(ASTRA_VFS_REQUEST_SIZE - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
    assert(reply.size == ASTRA_VFS_REPLY_SIZE);

    /*
     * An activity is the caller's account of what it was doing, so any value
     * is as valid as any other. This field used to be reserved-must-be-zero
     * and a non-zero one was a protocol error; refusing work over a log field
     * would be the service deciding what a caller may call its own story.
     */
    begin_request(&request, session, "/a");
    request.activity = 0x1a2bu;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status != ASTRA_VFS_ERR_PROTOCOL);

    /* An unterminated path is a malformed record, not a long path. */
    begin_request(&request, session, NULL);
    for (index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        request.body.path[index] = (uint8_t)'x';
    }
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_MAX + 1u, &request,
                               &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    astra_vfs_service_dispatch(&service, 0u, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
}

/*
 * A full-width binary write must not be rejected for lacking a NUL. The
 * request body is a union, and this is the case that catches anyone who
 * reintroduces path validation on the payload arm.
 */
static void
test_full_width_binary_write(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;
    AstraVfsFile file;

    reset();
    session = open_session();
    begin_request(&request, session, "/bin");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                    ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX;
    for (index = 0u; index < ASTRA_VFS_IO_MAX; ++index) {
        request.body.payload[index] = (uint8_t)(index + 1u); /* never zero */
    }
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == ASTRA_VFS_IO_MAX);

    /* One byte past what a message carries is refused, not truncated. */
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX + 1u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID);
}

static void
test_limits(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t index;

    reset();
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        assert(open_session() != ASTRA_VFS_SESSION_INVALID);
    }
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
}

static void
test_file_storage_budget(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    reset();
    assert(fake_create("/many", ASTRA_VFS_KIND_FILE) != NULL);
    session = open_session();
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof(service_files) / sizeof(service_files[0]));
         ++index) {
        begin_request(&request, session, "/many");
        request.flags = ASTRA_VFS_OPEN_READ;
        astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request,
                                   &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    begin_request(&request, session, "/many");
    request.flags = ASTRA_VFS_OPEN_READ;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(service.stats.peak_open_files == 128u);
    astra_vfs_service_release_session(&service, session);
    assert(service.open_files == 0u);
}

/*
 * The whole chain, the way a caller actually meets it: Kit -> transport ->
 * core -> backend. Everything above only ever touched the core directly, which
 * would not have caught a Kit that marshals a field into the wrong place.
 */
static void
test_client_through_transport(void)
{
    AstraVfsClient client;
    AstraVfsFile file;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    uint32_t moved = 0u;
    uint32_t status;
    char name[ASTRA_VFS_NAME_MAX];
    uint8_t buffer[32];
    static const char text[] = "pluggable";

    reset();
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) ==
           ASTRA_VFS_OK);
    assert(client.session != ASTRA_VFS_SESSION_INVALID);
    assert(client.version == ASTRA_VFS_VERSION);
    memset(&counted_call, 0, sizeof(counted_call));
    client.call_acquire = count_call_acquire;

    call_acquires = 0u;
    assert(astra_vfs_mkdir_mode(&client, "/dir", 0710u) == ASTRA_VFS_OK);
    assert(call_acquires == 1u);
    assert(fake_find("/dir")->mode == 0710u);
    call_acquires = 0u;
    assert(astra_vfs_open_mode(&client, "/dir/note.txt",
                               ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                                   ASTRA_VFS_OPEN_CREATE,
                               0600u, &file, &size, &kind) == ASTRA_VFS_OK);
    assert(call_acquires == 1u);
    assert(fake_find("/dir/note.txt")->mode == 0600u);
    assert(astra_vfs_write(&client, file, 0u, text, sizeof(text) - 1u,
                           &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(text) - 1u);
    assert(astra_vfs_sync(&client, file) == ASTRA_VFS_OK);
    client.version = UINT16_C(12);
    assert(astra_vfs_sync(&client, file) == ASTRA_VFS_ERR_UNSUPPORTED);
    client.version = ASTRA_VFS_VERSION;
    assert(astra_vfs_truncate(&client, file, 4u) == ASTRA_VFS_OK);

    memset(buffer, 0, sizeof(buffer));
    assert(astra_vfs_read(&client, file, 0u, buffer, sizeof(buffer), &moved) ==
           ASTRA_VFS_OK);
    assert(moved == 4u);
    assert(memcmp(buffer, text, 4u) == 0);
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);
    assert(astra_vfs_chmod(&client, "/dir/note.txt", 0640u) ==
           ASTRA_VFS_OK);
    assert(fake_find("/dir/note.txt")->mode == 0640u);
    assert(astra_vfs_chmod(&client, "/dir/note.txt", 010000u) ==
           ASTRA_VFS_ERR_INVALID);

    {
        FakeNode *link;
        uint32_t length = 0u;
        static const char target[] = "/dir/note.txt";

        assert(astra_vfs_symlink(&client, target, "/note-link") ==
               ASTRA_VFS_OK);
        link = fake_find("/note-link");
        assert(link != NULL && link->kind == ASTRA_VFS_KIND_SYMLINK);
        memset(buffer, 0, sizeof(buffer));
        assert(astra_vfs_readlink(&client, "/note-link", buffer,
                                  sizeof(buffer), &length) == ASTRA_VFS_OK);
        assert(length == sizeof(target) - 1u);
        assert(memcmp(buffer, target, length) == 0);
        client.version = UINT16_C(13);
        assert(astra_vfs_readlink(&client, "/note-link", buffer,
                                  sizeof(buffer), &length) ==
               ASTRA_VFS_ERR_UNSUPPORTED);
        assert(astra_vfs_mkdir_mode(&client, "/old", 0700u) ==
               ASTRA_VFS_ERR_UNSUPPORTED);
        assert(astra_vfs_symlink(&client, target, "/old-link") ==
               ASTRA_VFS_ERR_UNSUPPORTED);
        client.version = ASTRA_VFS_VERSION;
        link->used = 0;
    }

    assert(astra_vfs_open(&client, "/dir/note.txt",
                          ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                          &file, &size, &kind) == ASTRA_VFS_OK);
    {
        uint64_t position = 0u;
        static const char tail[] = "++";

        assert(astra_vfs_write_position(&client, file, 0u, tail,
                                        sizeof(tail) - 1u, &moved,
                                        &position) == ASTRA_VFS_OK);
        assert(moved == sizeof(tail) - 1u && position == 6u);
    }
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);

    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_OK);
    assert(size == 6u);
    assert(kind == ASTRA_VFS_KIND_FILE);
    client.version = UINT16_C(10);
    assert(astra_vfs_rename(&client, "/dir/note.txt", "/dir/renamed.txt") ==
           ASTRA_VFS_ERR_UNSUPPORTED);
    {
        uint32_t requests = service.stats.requests;

        client.version = UINT16_C(15);
        call_acquires = 0u;
        assert(astra_vfs_rename(&client, "/dir/note.txt",
                                "/dir/legacy.txt") == ASTRA_VFS_OK);
        assert(call_acquires == 1u);
        assert(service.stats.requests == requests + 2u);
        requests = service.stats.requests;
        client.version = ASTRA_VFS_VERSION;
        call_acquires = 0u;
        assert(astra_vfs_rename(&client, "/dir/legacy.txt",
                                "/dir/renamed.txt") == ASTRA_VFS_OK);
        assert(call_acquires == 1u);
        assert(service.stats.requests == requests + 1u);
    }
    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_vfs_stat(&client, "/dir/renamed.txt", &size, &kind) ==
           ASTRA_VFS_OK);
    assert(fake_create("/a", ASTRA_VFS_KIND_FILE) != NULL);
    assert(fake_create("/b", ASTRA_VFS_KIND_FILE) != NULL);
    assert(fake_create("/c", ASTRA_VFS_KIND_FILE) != NULL);

    {
        /*
         * A scan visits every entry exactly once and costs one visit per
         * entry. Index-addressed, this walked from the first node for every
         * entry, which is a listing that gets slower the longer it is -- and
         * on a 30 MHz machine that is the difference between a directory you
         * can list and one you cannot.
         */
        char seen[FAKE_NODE_MAX][ASTRA_VFS_NAME_MAX];
        uint64_t cursor = 0u;
        uint64_t previous;
        uint32_t entries = 0u;
        uint32_t at;

        fake_readdir_visits = 0u;
        for (;;) {
            previous = cursor;
            status = astra_vfs_readdir(&client, "/", cursor, name,
                                       sizeof(name), &kind, &cursor);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            assert(status == ASTRA_VFS_OK);
            assert(name[0] != '\0');
            /* A cursor that did not move is a listing that never ends. */
            assert(cursor > previous);
            assert(entries < FAKE_NODE_MAX);
            for (at = 0u; at < entries; ++at) {
                assert(strcmp(seen[at], name) != 0);
            }
            snprintf(seen[entries], sizeof(seen[entries]), "%s", name);
            ++entries;
        }
        assert(entries >= 2u);
        assert(fake_readdir_visits <= FAKE_NODE_MAX + entries);

        /* Past the last entry is a clean end, not an error to guess at. */
        assert(astra_vfs_readdir(&client, "/", cursor, name, sizeof(name),
                                 &kind, &cursor) == ASTRA_VFS_ERR_NOT_FOUND);
        /* A cursor nothing issued is refused rather than answered. */
        assert(astra_vfs_readdir(&client, "/", FAKE_NODE_MAX + 1u, name,
                                 sizeof(name), &kind,
                                 &cursor) == ASTRA_VFS_ERR_INVALID);

        {
            AstraVfsDirEntry entries[FAKE_NODE_MAX];
            const AstraVfsServiceStats *stats =
                astra_vfs_service_stats(&service);
            uint32_t requests = stats->requests;
            uint32_t count = 0u;

            cursor = 0u;
            call_acquires = 0u;
            assert(astra_vfs_readdir_batch(
                       &client, "/", cursor, entries, FAKE_NODE_MAX, &count,
                       &cursor) == ASTRA_VFS_OK);
            assert(call_acquires == 1u);
            /* Five short names fit; worst-case reservation used to stop at 2. */
            assert(count == 5u);
            assert(cursor == 0u);
            assert(stats->requests == requests + 1u);
            assert(entries[0].name[0] != '\0');
            assert(entries[1].name[0] != '\0');
            /*
             * The metadata a listing needs, through the wire record and back.
             * A directory entry that arrives without it makes `ls -l` a stat
             * per name, which is a round trip per name.
             */
            for (uint32_t index = 0u; index < count; ++index) {
                assert(entries[index].mode >= 0100644u);
                assert(entries[index].nlink >= 3u);
                assert((entries[index].uid & 0xffffff00u) == 0x11223300u);
                assert((entries[index].gid & 0xffffff00u) == 0x55667700u);
                assert(entries[index].mtime >= (int64_t)0x0000000123456789);
            }
            assert(entries[0].uid != entries[1].uid);
        }

        /* A negotiated v3 peer keeps the one-entry API without a new op. */
        client.version = UINT16_C(3);
        cursor = 0u;
        {
            AstraVfsDirEntry entry;
            uint32_t count = 0u;

            assert(astra_vfs_readdir_batch(&client, "/", cursor, &entry, 1u,
                                            &count, &cursor) == ASTRA_VFS_OK);
            assert(count == 1u);
            assert(cursor != 0u);
        }
        client.version = ASTRA_VFS_VERSION;
    }

    assert(astra_vfs_unlink(&client, "/dir/renamed.txt") == ASTRA_VFS_OK);
    assert(astra_vfs_stat(&client, "/dir/renamed.txt", &size, &kind) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /* A path that cannot fit the record is refused, never truncated: a
     * truncated path names a different file. */
    {
        char huge[ASTRA_VFS_PATH_MAX + 16];

        memset(huge, 'a', sizeof(huge) - 1u);
        huge[sizeof(huge) - 1u] = '\0';
        assert(astra_vfs_stat(&client, huge, &size, &kind) ==
               ASTRA_VFS_ERR_INVALID);
    }

    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(client.session == ASTRA_VFS_SESSION_INVALID);
}

int
main(void)
{
    test_session_handshake();
    test_transport_owner_isolation_and_fair_file_share();
    test_file_round_trip();
    test_handle_discipline();
    test_session_release_frees_files();
    test_malformed_records();
    test_full_width_binary_write();
    test_limits();
    test_file_storage_budget();
    test_client_through_transport();
    test_uncontended_file_operation_does_not_wake();
    test_unrelated_session_overtakes_held_backend();
    test_same_session_independent_request_overtakes_held_backend();
    test_close_waits_for_active_file_operation();
    test_session_release_waits_for_pinned_file();
    test_forced_switch_model();
    puts("astra vfs service core: PASS");
    return 0;
}
