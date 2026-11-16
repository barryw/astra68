#include <stdlib.h>

void *astra_runtime_reallocate(void *pointer, size_t size) { return realloc(pointer, size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }

void *astra_runtime_allocate(size_t size) { return malloc(size); }
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
#include <astra/vfs_port_transport.h>
#include <astra/vfs_service_core.h>
#include <astra/vfs_path_payload.h>
#include <astra/endian.h>

uint32_t astra_vfs_port_read_cached_position(AstraVfsClient *client,
    AstraVfsFile file, uint64_t offset, uint32_t flags, void *buffer,
    uint32_t length, uint32_t *moved, uint64_t *position)
{
    (void)client; (void)file; (void)flags; (void)buffer; (void)length;
    *moved = 0u;
    *position = offset;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

#define FAKE_NODE_MAX 8u
#define FAKE_BYTES_MAX (2u * ASTRA_MEMORY_PAGE_SIZE + 1u)

typedef struct FakeNode {
    char path[ASTRA_VFS_PATH_MAX];
    uint8_t bytes[FAKE_BYTES_MAX];
    uint32_t size;
    uint16_t kind;
    uint16_t mode;
    int used;
    int open_count;
    int64_t atime, mtime;
    uint32_t atime_nsec, mtime_nsec, time_fields, ctime_nsec;
} FakeNode;

typedef struct FakeFs {
    FakeNode nodes[FAKE_NODE_MAX];
    uint32_t closes;
    uint32_t read_mode;
    uint32_t read_limit;
    uint32_t read_resize_after;
    uint8_t read_note_change;
    uint8_t read_note_provider_change;
    uint8_t read_bump_timestamps;
    uint8_t read_bump_ctime;
    uint8_t read_drop_time_valid;
    uint8_t exact_identity;
    uint32_t mtime_nsec;
    uint32_t ctime_nsec;
    uint64_t provider_nodes;
    uint32_t provider_status;
    uint32_t close_status;
    uint32_t write_status;
    uint32_t write_overreport;
    uint32_t sync_status;
    uint32_t set_times_status, set_times_calls;
    uint32_t write_limit;
    atomic_uint write_calls;
    uint32_t write_fail_call;
    uint32_t write_zero;
    uint32_t write_bad_position;
    atomic_uint sync_calls;
    uint32_t sync_shrink;
    uint32_t sync_drop_identity;
    atomic_uint truncate_calls;
    uint32_t write_bump_timestamps;
    atomic_uint stat_calls;
    uint32_t stat_fail_call;
    uint64_t position;
    uint32_t sequential_reads;
    uint64_t read_report_position;
    uint64_t description_argument;
    uint32_t description_mask;
    uint32_t description_result;
    uint32_t description_status;
    uint32_t description_calls;
    void *expected_context;
    uint32_t check_context;
    uint32_t backend_calls;
    const AstraVfsOpenFile *expected_pin;
    void (*io_hook)(void);
} FakeFs;

static FakeFs fake;
static AstraVfsService service;

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

static void check_backend_context(void *context)
{
    if (fake.expected_pin != NULL) {
        assert(astra_vfs_open_file_active(fake.expected_pin));
        assert(pthread_mutex_trylock(service.state_lock_context) == 0);
        assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    }
    if (fake.check_context) {
        assert(context == fake.expected_context);
        ++fake.backend_calls;
    }
}

static void fake_node_identity(FakeNode *found, AstraVfsNodeInfo *info)
{
    if (fake.exact_identity)
        info->meta = (AstraVfsNodeMeta){
            .valid = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                     ASTRA_VFS_META_GENERATION | ASTRA_VFS_META_MTIME |
                     ASTRA_VFS_META_CTIME,
            .volume = 3u,
            .inode = (uint64_t)(found - fake.nodes) + 1u,
            .generation = 7u,
            .mtime_nsec = fake.mtime_nsec,
            .ctime = 1234567,
            .ctime_nsec = fake.ctime_nsec + found->ctime_nsec
        };
    if (found->time_fields & ASTRA_VFS_META_MTIME) {
        info->mtime = found->mtime;
        info->meta.mtime_nsec = found->mtime_nsec;
    }
    if (found->time_fields & ASTRA_VFS_META_ATIME) {
        info->meta.atime = found->atime;
        info->meta.atime_nsec = found->atime_nsec;
    }
    info->meta.valid |= found->time_fields;
}

static uint32_t
fake_open(void *context, const char *path, uint32_t flags,
          uint16_t create_mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    FakeNode *found = fake_find(path);
    int created = 0;

    check_backend_context(context);
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
    info->mtime = 1234567;
    info->uid = 17u;
    info->gid = 23u;
    info->kind = found->kind;
    info->mode = found->mode;
    info->nlink = 3u;
    fake_node_identity(found, info);
    return ASTRA_VFS_OK;
}

static uint32_t
fake_close(void *context, uintptr_t node)
{
    FakeNode *found = (FakeNode *)node;

    check_backend_context(context);
    ++fake.closes;
    if (found != NULL && found->open_count > 0) {
        --found->open_count;
    }
    return fake.close_status;
}

static uint32_t
fake_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
          uint32_t length, uint32_t *moved)
{
    FakeNode *found = (FakeNode *)node;
    uint32_t available;

    check_backend_context(context);
    if (fake.io_hook != NULL) fake.io_hook();
    if (found == fake_find("/slow"))
        hold_slow_backend();
    *moved = 0u;
    if (offset > found->size) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (fake.read_mode != 0u) {
        *moved = fake.read_mode == 1u ? 0u : length + 1u;
        return ASTRA_VFS_OK;
    }
    available = found->size - (uint32_t)offset;
    if (length > available) {
        length = available;
    }
    if (fake.read_limit != 0u && length > fake.read_limit)
        length = fake.read_limit;
    memcpy(buffer, &found->bytes[offset], length);
    *moved = length;
    if (fake.read_resize_after != 0u)
        found->size = fake.read_resize_after;
    if (fake.read_note_change)
        ++service.changes.nodes;
    if (fake.read_note_provider_change)
        ++fake.provider_nodes;
    if (fake.read_bump_timestamps)
        ++fake.mtime_nsec;
    if (fake.read_bump_ctime)
        ++fake.ctime_nsec;
    if (fake.read_drop_time_valid)
        fake.read_drop_time_valid = 2u;
    return ASTRA_VFS_OK;
}

static uint32_t fake_change_snapshot(void *context, uint64_t *nodes,
                                     uint64_t *entries)
{
    check_backend_context(context);

    if (fake.provider_status != ASTRA_VFS_OK)
        return fake.provider_status;
    *nodes = fake.provider_nodes;
    *entries = 0u;
    return ASTRA_VFS_OK;
}

static uint32_t fake_read_sequential(void *context, uintptr_t node, void *buffer,
    uint32_t length, uint32_t *moved, uint64_t *position)
{
    ++fake.sequential_reads;
    uint32_t status = fake_read(context, node, fake.position, buffer, length, moved);
    if (status == ASTRA_VFS_OK) fake.position += *moved;
    *position = fake.position;
    if (fake.read_report_position != 0u) *position = fake.read_report_position;
    return status;
}

static uint32_t fake_seek(void *context, uintptr_t node, int64_t delta,
    uint32_t origin, uint64_t *position)
{
    check_backend_context(context);
    assert(node == (uintptr_t)&fake.nodes[0]);
    assert(astra_vfs_open_file_active(&service.files[0]));
    ++fake.description_calls;
    fake.description_argument = (uint64_t)delta;
    fake.description_mask = origin;
    *position = fake.position;
    return fake.description_status;
}

static uint32_t fake_file_flags(void *context, uintptr_t node, uint32_t mask,
    uint32_t flags, uint32_t *result)
{
    check_backend_context(context);
    assert(node == (uintptr_t)&fake.nodes[0]);
    ++fake.description_calls;
    fake.description_argument = flags;
    fake.description_mask = mask;
    *result = fake.description_result;
    return fake.description_status;
}

static uint32_t fake_position_advance(void *context, uintptr_t node,
    uint64_t expected, uint32_t length, uint64_t *position, uint32_t *advanced)
{
    check_backend_context(context);
    assert(node == (uintptr_t)&fake.nodes[0]);
    ++fake.description_calls;
    fake.description_argument = expected;
    fake.description_mask = length;
    *position = fake.position;
    *advanced = fake.description_result;
    return fake.description_status;
}

static uint32_t
fake_write(void *context, uintptr_t node, uint64_t offset, uint32_t flags, uint64_t max_end,
           const void *buffer, uint32_t length, uint32_t *moved,
           uint64_t *position)
{
    FakeNode *found = (FakeNode *)node;

    check_backend_context(context);
    if (fake.io_hook != NULL) fake.io_hook();
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        offset = fake.position;
    if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u)
        offset = found->size;
    *moved = 0u;
    *position = offset;
    ++fake.write_calls;
    if (fake.write_fail_call != 0u && fake.write_calls == fake.write_fail_call)
        return ASTRA_VFS_ERR_NO_SPACE;
    if (fake.write_status != ASTRA_VFS_OK)
        return fake.write_status;
    if (fake.write_zero) return ASTRA_VFS_OK;
    if (fake.write_limit != 0u && length > fake.write_limit)
        length = fake.write_limit;
    if (length != 0u && offset >= max_end)
        return ASTRA_VFS_ERR_FILE_TOO_LARGE;
    if (offset < max_end && length > max_end - offset)
        length = (uint32_t)(max_end - offset);
    if (offset + length > FAKE_BYTES_MAX) {
        return ASTRA_VFS_ERR_NO_SPACE;
    }
    memcpy(&found->bytes[offset], buffer, length);
    found->time_fields &= ~ASTRA_VFS_META_MTIME;
    if (fake.write_bump_timestamps) ++fake.mtime_nsec;
    if (offset + length > found->size) {
        found->size = (uint32_t)(offset + length);
    }
    *moved = length;
    *position = offset + length;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        fake.position = *position;
    if (fake.write_bad_position) ++*position;
    if (fake.write_overreport)
        ++*moved;
    return ASTRA_VFS_OK;
}

static uint32_t fake_sync(void *context, uintptr_t node)
{
    check_backend_context(context);
    ++fake.sync_calls;
    if (fake.sync_shrink && node != 0u) --((FakeNode *)node)->size;
    if (fake.sync_drop_identity) fake.exact_identity = 0u;
    return node == 0u ? ASTRA_VFS_ERR_BAD_HANDLE : fake.sync_status;
}

static uint32_t fake_set_file_times(void *context, uintptr_t node,
    const AstraVfsNodeInfo *times, uint32_t fields)
{
    FakeNode *found = (FakeNode *)node;
    check_backend_context(context);
    ++fake.set_times_calls;
    assert(found != NULL && astra_vfs_file_times_valid(fields,
        times->meta.atime_nsec, times->meta.mtime_nsec));
    if (fake.set_times_status != ASTRA_VFS_OK) return fake.set_times_status;
    if (fields & ASTRA_VFS_META_ATIME) {
        found->atime = times->meta.atime; found->atime_nsec = times->meta.atime_nsec;
    }
    if (fields & ASTRA_VFS_META_MTIME) {
        found->mtime = times->mtime; found->mtime_nsec = times->meta.mtime_nsec;
    }
    found->time_fields |= fields;
    ++found->ctime_nsec;
    return ASTRA_VFS_OK;
}

static uint32_t fake_truncate(void *context, uintptr_t node, uint64_t size)
{
    FakeNode *found = (FakeNode *)node;

    check_backend_context(context);
    ++fake.truncate_calls;
    if (fake.io_hook != NULL) fake.io_hook();
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

    check_backend_context(context);
    if (strcmp(path, "/slow") == 0)
        hold_slow_backend();
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    info->size = found->size;
    info->mtime = 1234567;
    info->uid = 17u;
    info->gid = 23u;
    info->kind = found->kind;
    info->mode = found->mode;
    info->nlink = 3u;
    info->meta = (AstraVfsNodeMeta){ .valid = ASTRA_VFS_META_MODE |
        ASTRA_VFS_META_INODE | ASTRA_VFS_META_VOLUME, .volume = 3u,
        .inode = (uint64_t)(found - fake.nodes) + 1u };
    fake_node_identity(found, info);
    info->meta.valid |= ASTRA_VFS_META_MODE;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_stat_node(void *context, uintptr_t node, AstraVfsNodeInfo *info)
{
    FakeNode *found = (FakeNode *)node;

    check_backend_context(context);
    ++fake.stat_calls;
    if (fake.stat_fail_call != 0u && fake.stat_calls == fake.stat_fail_call)
        return ASTRA_VFS_ERR_IO;
    if (found == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    info->size = found->size;
    info->mtime = 1234567;
    info->uid = 17u;
    info->gid = 23u;
    info->kind = found->kind;
    info->mode = found->mode;
    info->nlink = 3u;
    fake_node_identity(found, info);
    if (fake.read_drop_time_valid == 2u)
        info->meta.valid &= ~ASTRA_VFS_META_MTIME;
    return ASTRA_VFS_OK;
}

/*
 * The cookie is the slot after the one returned, so a scan resumes where it
 * stopped without counting from the first node. `fake_readdir_scans` is how a
 * test sees that a listing costs one visit per entry rather than one walk.
 */
static uint32_t fake_readdir_visits;

static uint32_t terminal_readdir_calls;

static uint32_t
terminal_readdir(void *context, uintptr_t directory, const char *path,
                 uint64_t cookie, char *name, uint32_t capacity,
                 AstraVfsNodeInfo *info, uint64_t *next)
{
    const char *entry;

    check_backend_context(context);
    (void)directory;
    (void)path;
    ++terminal_readdir_calls;
    if (cookie == 0u) {
        entry = "first";
        *next = 7u;
    } else if (cookie == 7u) {
        entry = "last";
        *next = 0u;
    } else {
        return ASTRA_VFS_ERR_INVALID;
    }
    assert(strlen(entry) + 1u <= capacity);
    strcpy(name, entry);
    info->kind = ASTRA_VFS_KIND_FILE;
    info->meta.valid = ASTRA_VFS_META_INODE;
    info->meta.inode = cookie == 0u ? 42u : 43u;
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps terminal_readdir_ops = {
    .readdir = terminal_readdir,
    NULL,
};

static void
test_terminal_readdir_cookie(void)
{
    const AstraVfsBackend backend = {&terminal_readdir_ops, NULL};
    uint8_t buffer[128];
    uint64_t next = 0u;
    uint32_t used = 0u;

    /* Negative: a nonterminal cookie must continue the scan. */
    terminal_readdir_calls = 0u;
    assert(astra_vfs_backend_readdir_into(
               &backend, 0u, "/", 0u, 1u, buffer, sizeof(buffer), &used,
               &next) == ASTRA_VFS_OK);
    assert(terminal_readdir_calls == 1u);
    assert(used == ASTRA_VFS_DIRENT_HEADER + 5u);
    assert(astra_load_be32(buffer + 32u) == ASTRA_VFS_META_INODE);
    assert(astra_load_be64(buffer + 48u) == 42u);
    assert(next == 7u);

    /* Positive: the terminal entry is returned once, never replayed. */
    terminal_readdir_calls = 0u;
    assert(astra_vfs_backend_readdir_into(
               &backend, 0u, "/", 7u, 8u, buffer, sizeof(buffer), &used,
               &next) == ASTRA_VFS_OK);
    assert(terminal_readdir_calls == 1u);
    assert(used == ASTRA_VFS_DIRENT_HEADER + 4u);
    assert(astra_load_be32(buffer + 32u) == ASTRA_VFS_META_INODE);
    assert(astra_load_be64(buffer + 48u) == 43u);
    assert(next == 0u);
}

static uint32_t
fake_readdir(void *context, uintptr_t directory, const char *path,
             uint64_t cookie, char *name, uint32_t capacity,
             AstraVfsNodeInfo *info, uint64_t *next)
{
    uint32_t slot;

    check_backend_context(context);
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

    check_backend_context(context);
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
fake_unlink(void *context, const char *path, uint32_t flags)
{
    FakeNode *found = fake_find(path);

    check_backend_context(context);
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (flags & ASTRA_VFS_UNLINK_DIRECTORY) {
        if (found->kind != ASTRA_VFS_KIND_DIRECTORY)
            return ASTRA_VFS_ERR_NOT_DIR;
    } else if (found->kind == ASTRA_VFS_KIND_DIRECTORY) {
        return ASTRA_VFS_ERR_IS_DIR;
    }
    found->used = 0;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_rename(void *context, const char *from, const char *to)
{
    FakeNode *source = fake_find(from);
    FakeNode *target = fake_find(to);

    check_backend_context(context);
    if (source == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (target != NULL && target != source)
        target->used = 0;
    (void)snprintf(source->path, sizeof(source->path), "%s", to);
    return ASTRA_VFS_OK;
}

static uint32_t fake_rename_noreplace(void *context, const char *from,
    const char *to)
{
    if (fake_find(to) != NULL) return ASTRA_VFS_ERR_EXISTS;
    return fake_rename(context, from, to);
}

static uint32_t
fake_chmod(void *context, const char *path, uint16_t mode)
{
    FakeNode *found = fake_find(path);

    check_backend_context(context);
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

    check_backend_context(context);
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

    check_backend_context(context);
    if (fake_find(path) != NULL)
        return ASTRA_VFS_ERR_EXISTS;
    created = fake_create(path, ASTRA_VFS_KIND_SYMLINK);
    if (created == NULL)
        return ASTRA_VFS_ERR_NO_SPACE;
    created->size = (uint32_t)strlen(target);
    memcpy(created->bytes, target, created->size);
    return ASTRA_VFS_OK;
}

static char linked_from[ASTRA_VFS_PATH_MAX];
static char linked_to[ASTRA_VFS_PATH_MAX];

static uint32_t
fake_link(void *context, const char *from, const char *to)
{
    check_backend_context(context);
    (void)snprintf(linked_from, sizeof(linked_from), "%s", from);
    (void)snprintf(linked_to, sizeof(linked_to), "%s", to);
    return ASTRA_VFS_OK;
}

static int
fake_relative_path(uintptr_t directory, const char *path, char *full,
                   uint32_t capacity)
{
    const FakeNode *node = (const FakeNode *)directory;
    int written;

    if (node == NULL || !node->used ||
        node->kind != ASTRA_VFS_KIND_DIRECTORY || path == NULL ||
        path[0] == '\0' || path[0] == '/')
        return 0;
    written = snprintf(full, capacity, "%s/%s", node->path, path);
    return written > 0 && (uint32_t)written < capacity;
}

static uint32_t
fake_open_at(void *context, uintptr_t directory, const char *path,
             uint32_t flags, uint16_t create_mode, uintptr_t *node,
             AstraVfsNodeInfo *info)
{
    char full[ASTRA_VFS_PATH_MAX];

    if (!fake_relative_path(directory, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_open(context, full, flags, create_mode, node, info);
}

static uint32_t
fake_stat_at(void *context, uintptr_t directory, const char *path, uint32_t flags,
             AstraVfsNodeInfo *info)
{
    (void)flags;
    char full[ASTRA_VFS_PATH_MAX];

    if (directory == 0u) return fake_stat(context, path, info);
    if (!fake_relative_path(directory, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_stat(context, full, info);
}

static uint32_t
fake_unlink_at(void *context, uintptr_t directory, const char *path,
               uint32_t flags)
{
    char full[ASTRA_VFS_PATH_MAX];

    if ((flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u ||
        !fake_relative_path(directory, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_unlink(context, full, flags);
}

static uint32_t
fake_chmod_node(void *context, uintptr_t node, uint16_t mode)
{
    FakeNode *found = (FakeNode *)node;

    check_backend_context(context);
    if (found == NULL || !found->used)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    found->mode = mode;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_chmod_at(void *context, uintptr_t directory, const char *path,
              uint16_t mode, uint32_t flags)
{
    char full[ASTRA_VFS_PATH_MAX];

    if (flags != 0u ||
        !fake_relative_path(directory, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_chmod(context, full, mode);
}

static uint32_t
fake_filesystem_info(void *context, uintptr_t node, const char *path,
                     AstraVfsFilesystemInfo *info)
{
    check_backend_context(context);
    if (info == NULL || ((node == 0u) == (path == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    if (node == 0u && fake_find(path) == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->block_size = 4096u;
    info->fragment_size = 4096u;
    info->blocks = 4096u;
    info->blocks_free = 2048u;
    info->blocks_available = 2000u;
    info->files = 512u;
    info->files_free = 480u;
    info->name_max = ASTRA_VFS_NAME_MAX - 1u;
    return ASTRA_VFS_OK;
}

static uint32_t fake_mkdir_at(void *context, uintptr_t directory, const char *path, uint16_t mode)
{
    char full[ASTRA_VFS_PATH_MAX];
    return fake_relative_path(directory, path, full, sizeof(full)) ?
        fake_mkdir(context, full, mode) : ASTRA_VFS_ERR_INVALID;
}

static uint32_t fake_rename_at(void *context, uintptr_t left, const char *from,
                               uintptr_t right, const char *to)
{
    char a[ASTRA_VFS_PATH_MAX], b[ASTRA_VFS_PATH_MAX];
    if (!fake_relative_path(left, from, a, sizeof(a)) ||
        !fake_relative_path(right, to, b, sizeof(b)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_rename(context, a, b);
}

static uint32_t fake_rename_noreplace_at(void *context, uintptr_t left,
    const char *from, uintptr_t right, const char *to)
{
    char a[ASTRA_VFS_PATH_MAX], b[ASTRA_VFS_PATH_MAX];
    if (!fake_relative_path(left, from, a, sizeof(a)) ||
        !fake_relative_path(right, to, b, sizeof(b)))
        return ASTRA_VFS_ERR_INVALID;
    return fake_rename_noreplace(context, a, b);
}

static uint32_t fake_link_at(void *context, uintptr_t left, const char *from,
    uintptr_t right, const char *to)
{
    char a[ASTRA_VFS_PATH_MAX], b[ASTRA_VFS_PATH_MAX];
    if (!fake_relative_path(left, from, a, sizeof(a)) ||
        !fake_relative_path(right, to, b, sizeof(b))) return ASTRA_VFS_ERR_INVALID;
    return fake_link(context, a, b);
}

static uint32_t fake_directory_path(void *context, uintptr_t directory,
                                     void *buffer, uint32_t capacity, uint32_t *length)
{
    const FakeNode *node = (const FakeNode *)directory;
    check_backend_context(context);
    *length = (uint32_t)strlen(node->path);
    if (*length > capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, node->path, *length);
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps fake_ops = {
    .open = fake_open, .close = fake_close, .read = fake_read,
    .write = fake_write, .sync = fake_sync, .set_file_times = fake_set_file_times,
    .truncate = fake_truncate,
    .stat = fake_stat, .readdir = fake_readdir, .mkdir = fake_mkdir,
    .unlink = fake_unlink, .rename = fake_rename,
    .rename_noreplace = fake_rename_noreplace, .chmod = fake_chmod,
    .readlink = fake_readlink, .symlink = fake_symlink, .link = fake_link,
    .open_at = fake_open_at, .unlink_at = fake_unlink_at,
    .chmod_node = fake_chmod_node, .chmod_at = fake_chmod_at,
    .filesystem_info = fake_filesystem_info, .stat_at = fake_stat_at,
    .stat_node = fake_stat_node, .mkdir_at = fake_mkdir_at,
    .rename_at = fake_rename_at,
    .link_at = fake_link_at,
    .rename_noreplace_at = fake_rename_noreplace_at,
    .directory_path = fake_directory_path,
    .change_snapshot = fake_change_snapshot,
};

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

static _Thread_local uint8_t fixture_paths[2u * ASTRA_VFS_PATH_MAX];
static _Thread_local uint8_t fixture_output[16u * (ASTRA_VFS_DIRENT_HEADER + ASTRA_VFS_NAME_MAX)];
static AstraVfsTransfer fixture_transfer(const AstraVfsRequest *request, uint32_t operation)
{
    return (AstraVfsTransfer){.input = fixture_paths,
        .input_size = astra_vfs_path_operand_count(operation, request) == 0u ? 0u :
            request->body.paths.first_length + request->body.paths.second_length,
        .output = fixture_output, .output_capacity = sizeof(fixture_output)};
}
static void fixture_dispatch(AstraVfsService *value, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsTransfer transfer = fixture_transfer(request, operation);
    astra_vfs_service_dispatch(value, operation, request, &transfer, reply);
}
static void fixture_dispatch_from(AstraVfsService *value, uint32_t owner,
    uint32_t operation, const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsTransfer transfer = fixture_transfer(request, operation);
    astra_vfs_service_dispatch_from(value, owner, operation, request, &transfer, reply);
}
static void fixture_dispatch_grant(AstraVfsService *value, uint32_t owner,
    uint32_t grant, uint32_t features, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsTransfer transfer = fixture_transfer(request, operation);
    astra_vfs_service_dispatch_grant(value, owner, grant, features, operation,
        request, &transfer, reply);
}

static void
begin_request(AstraVfsRequest *request, uint32_t session, const char *path)
{
    memset(request, 0, sizeof(*request));
    request->size = (uint16_t)ASTRA_VFS_REQUEST_SIZE;
    request->write_limit = UINT64_MAX;
    request->version = ASTRA_VFS_VERSION;
    request->session = session;
    if (path != NULL) {
        uint32_t used;
        assert(astra_vfs_path_pack(path, NULL, fixture_paths, sizeof(fixture_paths),
            &request->body.paths, &used) == ASTRA_VFS_OK);
    }
}

static uint32_t
open_session(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;

    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
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

typedef struct BoundBackend {
    uint32_t references;
    uint32_t retain_status;
    uint64_t incarnation;
} BoundBackend;

typedef struct BackendBindings {
    BoundBackend *selected;
    const AstraVfsBackendOps *ops;
    uint32_t status;
    uint32_t invalid_ops;
    uint32_t acquired;
    uint32_t owner;
    uint32_t grant;
} BackendBindings;

static uint32_t binding_acquire(void *context, uint32_t owner, uint32_t grant,
    AstraVfsBackend *backend)
{
    static const AstraVfsBackendOps invalid_ops = {0};
    BackendBindings *bindings = context;
    bindings->owner = owner;
    bindings->grant = grant;
    if (bindings->status != ASTRA_VFS_OK) return bindings->status;
    ++bindings->acquired;
    ++bindings->selected->references;
    *backend = (AstraVfsBackend){bindings->invalid_ops == 2u ? NULL :
        bindings->invalid_ops ? &invalid_ops : bindings->ops != NULL ? bindings->ops : &fake_ops,
        bindings->selected};
    return ASTRA_VFS_OK;
}

static uint32_t binding_retain(void *context, const AstraVfsBackend *backend)
{
    BoundBackend *binding = backend->context;
    (void)context;
    assert(binding->references != 0u);
    if (binding->retain_status != ASTRA_VFS_OK) return binding->retain_status;
    ++binding->references;
    return ASTRA_VFS_OK;
}

static void binding_release(void *context, const AstraVfsBackend *backend)
{
    BoundBackend *binding = backend->context;
    (void)context;
    assert(binding->references != 0u);
    --binding->references;
}

static uint64_t binding_incarnation(void *context, const AstraVfsBackend *backend)
{
    const BoundBackend *binding = backend->context;
    (void)context;
    return binding->incarnation;
}

static const AstraVfsBackendBindingOps backend_bindings = {
    binding_acquire, binding_retain, binding_release, binding_incarnation
};

static void test_file_io_identity_domains(void)
{
    const struct { uint64_t second_epoch; int exact; int other_node; int shared; } cases[] = {
        {7u, 1, 0, 1}, {8u, 1, 0, 0}, {0u, 1, 0, 0},
        {7u, 0, 0, 0}, {7u, 1, 1, 0}
    };
    for (uint32_t at = 0u; at < sizeof(cases) / sizeof(cases[0]); ++at) {
        AstraVfsRequest request;
        AstraVfsReply reply;
        BoundBackend first = {.incarnation = 7u}, second = {.incarnation = cases[at].second_epoch};
        BackendBindings bindings = {.selected = &first};
        reset();
        fake.exact_identity = cases[at].exact;
        assert(fake_create("/first", ASTRA_VFS_KIND_FILE) != NULL);
        assert(fake_create("/second", ASTRA_VFS_KIND_FILE) != NULL);
        AstraVfsBackendBindingOps missing = backend_bindings;
        missing.incarnation = NULL;
        assert(!astra_vfs_service_set_backend_bindings(&service, &missing, &bindings));
        assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
        uint32_t left = open_session();
        begin_request(&request, left, "/first"); request.flags = ASTRA_VFS_OPEN_READ;
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        AstraVfsOpenFile *a = &service.files[(reply.file & 0xffffu) - 1u];
        bindings.selected = &second;
        uint32_t right = open_session();
        begin_request(&request, right, cases[at].other_node ? "/second" : "/first");
        request.flags = ASTRA_VFS_OPEN_WRITE;
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        AstraVfsOpenFile *b = &service.files[(reply.file & 0xffffu) - 1u];
        assert((a->io == b->io) == cases[at].shared);
        astra_vfs_service_release_session(&service, left);
        astra_vfs_service_release_session(&service, right);
        assert(service.open_files == 0u && first.references == 0u && second.references == 0u);
        for (uint32_t cell = 0u; cell < service.file_high_water; ++cell)
            assert(service.files[cell].io_storage.references == 0u);
    }
}

static void test_scoped_backend_lifetime_and_rebind(void)
{
    AstraVfsClient client;
    AstraVfsFile file, retained;
    AstraVfsRequest request;
    AstraVfsReply reply;
    BoundBackend first = {0}, second = {0};
    BackendBindings bindings = {.selected = &first};
    uint8_t bytes[8], page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t moved, used;
    uint64_t size, next;

    reset();
    assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    assert(!astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    service.backend.context = &bindings; /* An unscoped fallback must fail the context check. */
    fake.check_context = 1u;
    fake.expected_context = &first;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(first.references == 1u && bindings.acquired == 1u);
    assert(astra_vfs_open(&client, "/bound", ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &file, NULL, NULL) == ASTRA_VFS_OK);
    assert(first.references == 2u);
    assert(astra_vfs_write(&client, file, 0u, UINT64_MAX, "abc", 3u, &moved) == ASTRA_VFS_OK);
    assert(astra_vfs_service_read_path(&service, client.session, "/bound",
        bytes, sizeof(bytes), &moved, &size) == ASTRA_VFS_OK && moved == 3u && size == 3u);
    assert(astra_vfs_service_readdir_into(&service, client.session, "/", 0u,
        1u, fixture_output, sizeof(fixture_output), &used, &next) == ASTRA_VFS_OK && used != 0u);
    bindings.selected = &second;
    begin_request(&request, client.session, NULL);
    service.sessions[0].inflight = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BUSY && first.references == 2u &&
        second.references == 0u && bindings.acquired == 1u);
    service.sessions[0].inflight = 0u;
    bindings.status = ASTRA_VFS_ERR_LOST;
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LOST && first.references == 2u && second.references == 0u);
    bindings.status = ASTRA_VFS_OK;
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && first.references == 1u && second.references == 1u);
    /* Old files keep their exact backend after control-plane recovery. */
    assert(astra_vfs_read(&client, file, 0u, bytes, 3u, &moved) == ASTRA_VFS_OK && moved == 3u);
    assert(astra_vfs_service_retain_file(&service, client.session, file, ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK && first.references == 1u);
    fake.expected_context = &second;
    second.retain_status = ASTRA_VFS_ERR_LIMIT;
    uint32_t calls = fake.backend_calls;
    assert(astra_vfs_open(&client, "/bound", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) ==
        ASTRA_VFS_ERR_LIMIT && fake.backend_calls == calls && second.references == 1u);
    second.retain_status = ASTRA_VFS_OK;
    assert(astra_vfs_open(&client, "/missing", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) ==
        ASTRA_VFS_ERR_NOT_FOUND && second.references == 1u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && second.references == 0u);
    /* Session-slot reuse must not redirect the retained file's backend. */
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(second.references == 1u);
    fake.expected_context = &first;
    assert(astra_vfs_service_read_retained(&service, retained, 0u, bytes, 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && memcmp(bytes, "abc", 3u) == 0);
    AstraVfsNodeInfo info;
    assert(astra_vfs_service_stat_retained(&service, retained, &info) == ASTRA_VFS_OK);
    assert(astra_vfs_service_write_retained(&service, retained, 0u, UINT64_MAX,
        "Z", 1u, &moved) == ASTRA_VFS_OK && moved == 1u);
    assert(astra_vfs_service_sync_retained(&service, retained, NULL) == ASTRA_VFS_OK);
    memcpy(page, "Rbc", 3u);
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL, &info,
        0u, info.size, page, &moved, &info) == ASTRA_VFS_OK && moved == 3u && info.size == 3u);
    assert(astra_vfs_service_read_retained(&service, retained, 0u, bytes, 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && memcmp(bytes, "Rbc", 3u) == 0);
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK && first.references == 0u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && second.references == 0u);
    bindings.invalid_ops = 1u;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) ==
        ASTRA_VFS_ERR_PROTOCOL && second.references == 0u && service.open_sessions == 0u);
    bindings.invalid_ops = 2u;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) ==
        ASTRA_VFS_ERR_PROTOCOL && second.references == 0u && service.open_sessions == 0u);
    bindings.invalid_ops = 0u;
    begin_request(&request, 0u, NULL);
    fixture_dispatch_grant(&service, 42u, 71u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && bindings.owner == 42u && bindings.grant == 71u);
    astra_vfs_service_release_session(&service, reply.session);
    assert(second.references == 0u);
    bindings.selected = &first;
    fake.expected_context = &first;
    assert(fake_create("/dir", ASTRA_VFS_KIND_DIRECTORY) != NULL);
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    AstraVfsFile old_directory, new_directory;
    assert(astra_vfs_open(&client, "/dir", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &old_directory, NULL, NULL) == ASTRA_VFS_OK);
    bindings.selected = &second;
    begin_request(&request, client.session, NULL);
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    fake.expected_context = &second;
    assert(astra_vfs_open(&client, "/dir", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &new_directory, NULL, NULL) == ASTRA_VFS_OK);
    calls = fake.backend_calls;
    assert(astra_vfs_rename_at(&client, old_directory, "a", new_directory, "b") ==
        ASTRA_VFS_ERR_CROSS_DEVICE && fake.backend_calls == calls);
    assert(astra_vfs_close(&client, new_directory) == ASTRA_VFS_OK);
    fake.expected_context = &first;
    assert(astra_vfs_close(&client, old_directory) == ASTRA_VFS_OK && first.references == 0u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && second.references == 0u);
    fake.check_context = 0u;
}

typedef struct SessionProbe {
    atomic_uint notifications;
    atomic_uint members;
} SessionProbe;

static void
holders_changed(void *context)
{
    SessionProbe *probe = context;
    uint32_t members = 0u;

    /* The observer runs under the core state lock; inspect, do not reenter. */
    for (uint32_t index = 0u; index < service.session_capacity; ++index) {
        const AstraVfsSessionSlot *slot = &service.sessions[index];
        if (slot->id != ASTRA_VFS_SESSION_INVALID)
            ++members;
    }
    assert(members == service.open_sessions);
    atomic_store(&probe->members, members);
    (void)atomic_fetch_add(&probe->notifications, 1u);
}

static void
test_session_observer(void)
{
    SessionProbe probe = {0};
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t sessions[3];

    reset();
    assert(!astra_vfs_service_set_holders_changed(NULL, holders_changed, &probe));
    assert(!astra_vfs_service_set_holders_changed(&service, NULL, &probe));
    assert(astra_vfs_service_set_holders_changed(&service, holders_changed, &probe));
    assert(!astra_vfs_service_set_holders_changed(&service, holders_changed, &probe));
    for (uint32_t index = 0u; index < 3u; ++index) {
        begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
        fixture_dispatch_grant(&service, 41u + index, 71u + index, 0u,
            ASTRA_VFS_OP_HELLO, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        sessions[index] = reply.session;
        assert(astra_vfs_service_session_bound(&service, sessions[index],
            41u + index, 71u + index));
        assert(atomic_load(&probe.notifications) == index + 1u);
        assert(atomic_load(&probe.members) == index + 1u);
    }
    begin_request(&request, sessions[0], NULL);
    fixture_dispatch_grant(&service, 41u, 71u, 0u,
        ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.session == sessions[0]);
    fixture_dispatch_grant(&service, 99u, 71u, 0u,
        ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(atomic_load(&probe.notifications) == 3u);
    fixture_dispatch_grant(&service, 41u, 71u, 0u,
        ASTRA_VFS_OP_BYE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(atomic_load(&probe.notifications) == 4u);
    assert(atomic_load(&probe.members) == 2u);
    astra_vfs_service_release_session(&service, sessions[1]);
    assert(atomic_load(&probe.notifications) == 5u);
    assert(atomic_load(&probe.members) == 1u);
    astra_vfs_service_release_grant(&service, 73u);
    assert(atomic_load(&probe.notifications) == 6u);
    assert(atomic_load(&probe.members) == 0u);
    astra_vfs_service_release_session(&service, sessions[1]);
    astra_vfs_service_release_grant(&service, 73u);
    assert(atomic_load(&probe.notifications) == 6u);

    reset();
    (void)open_session();
    assert(!astra_vfs_service_set_holders_changed(&service, holders_changed, &probe));
}


typedef struct MountProbe {
    uint32_t calls;
    uint32_t fail_flush;
    uint32_t fail_unmount;
} MountProbe;

static uint32_t mount_flush(void *context)
{
    MountProbe *probe = context;

    assert(probe->calls == 0u);
    ++probe->calls;
    return probe->fail_flush ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
}

static uint32_t mount_unmount(void *context)
{
    MountProbe *probe = context;

    ++probe->calls;
    return probe->fail_unmount ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
}

static void test_mount_shutdown(void)
{
    const AstraVfsMountOps writable = {mount_flush, mount_unmount};
    const AstraVfsMountOps readonly = {NULL, mount_unmount};
    const AstraVfsMountOps invalid = {mount_flush, NULL};
    AstraVfsRequest request;
    AstraVfsReply reply;
    MountProbe probe = {0};
    uint32_t session;

    reset();
    assert(fake_create("/file", ASTRA_VFS_KIND_FILE) != NULL);
    session = open_session();
    begin_request(&request, session, "/file");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN,
                               &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(fake_find("/file")->open_count == 1);
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_OK);
    assert(probe.calls == 2u && service.open_sessions == 0u);
    assert(fake_find("/file")->open_count == 0);

    reset();
    probe = (MountProbe){0};
    assert(astra_vfs_service_shutdown(&service, &readonly, &probe) ==
           ASTRA_VFS_OK);
    assert(probe.calls == 1u); /* CDFS flush is a no-op. */

    reset();
    probe = (MountProbe){.fail_flush = 1u};
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_ERR_IO);
    assert(probe.calls == 1u); /* Never unmount after a failed flush. */
    reset();
    probe = (MountProbe){.fail_unmount = 1u};
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_ERR_IO);
    assert(probe.calls == 2u);

    reset();
    probe = (MountProbe){0};
    session = open_session();
    for (uint32_t index = 0u; index < service.session_capacity; ++index)
        if (service.sessions[index].id == session)
            service.sessions[index].inflight = 1u;
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_ERR_BUSY);
    assert(probe.calls == 0u);
    assert(astra_vfs_service_shutdown(NULL, &writable, &probe) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_service_shutdown(&service, &invalid, &probe) ==
           ASTRA_VFS_ERR_INVALID);
}

static void
test_last_failed_request_diagnostics(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    reset();
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch_from(
        &service, 41u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    session = reply.session;
    begin_request(&request, session, "/missing");
    fixture_dispatch_from(
        &service, 41u, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_NOT_FOUND);
    assert(service.stats.last_failed_operation == ASTRA_VFS_OP_STAT);
    assert(service.stats.last_failed_status == ASTRA_VFS_ERR_NOT_FOUND);
    assert(service.stats.last_failed_owner == 41u);

    assert(fake_create("/present", ASTRA_VFS_KIND_FILE) != NULL);
    begin_request(&request, session, "/present");
    fixture_dispatch_from(
        &service, 41u, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(service.stats.last_failed_operation == ASTRA_VFS_OP_STAT);
    assert(service.stats.last_failed_status == ASTRA_VFS_ERR_NOT_FOUND);
    assert(service.stats.last_failed_owner == 41u);
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

static int
test_slot_reuse_wait(void *context, volatile uint32_t *sequence,
                     uint32_t expected)
{
    AstraVfsFileIO *io = (AstraVfsFileIO *)((uint8_t *)sequence -
        offsetof(AstraVfsFileIO, sequence));
    AstraVfsOpenFile *file = io->active;

    (void)context;
    assert(*sequence == expected && file != NULL);
    io->active = NULL;
    ++file->generation;
    ++io->sequence;
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    state_wakes = 0u;
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(state_wakes == 0u);
    assert(pthread_cond_destroy(&file_condition) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

static void
test_read_refuses_reused_waiting_slot(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t handle;
    uint32_t slot;

    reset();
    assert(fake_create("/old", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_cond_init(&file_condition, NULL) == 0);
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    assert(astra_vfs_service_set_state_wait(
        &service, test_slot_reuse_wait, test_state_wake));
    session = open_session();
    begin_request(&request, session, "/old");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    handle = reply.file;
    slot = (handle & 0xffffu) - 1u;
    service.files[slot].io->active = &service.files[slot];
    begin_request(&request, session, NULL);
    request.file = handle;
    request.length = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && reply.count == 0u);
    assert(pthread_cond_destroy(&file_condition) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

typedef struct ConcurrentRequest {
    uint32_t session;
    const char *path;
    AstraVfsReply reply;
    uint32_t operation, flags;
} ConcurrentRequest;

static void *
run_path_request(void *context)
{
    ConcurrentRequest *call = context;
    AstraVfsRequest request;

    begin_request(&request, call->session, call->path);
    request.flags = call->flags; request.offset = 0644u;
    fixture_dispatch(&service, call->operation == 0u ? ASTRA_VFS_OP_STAT : call->operation, &request,
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
    ConcurrentRequest slow = {0};
    ConcurrentRequest fast = {0};
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

    assert(pthread_create(&slow_thread, NULL, run_path_request, &slow) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_create(&fast_thread, NULL, run_path_request, &fast) == 0);

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
    ConcurrentRequest slow = {0};
    ConcurrentRequest fast = {0};
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

    assert(pthread_create(&slow_thread, NULL, run_path_request, &slow) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed,
                                 &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_create(&fast_thread, NULL, run_path_request, &fast) == 0);

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
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request,
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
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request,
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
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
    SessionProbe probe = {0};

    reset();
    assert(astra_vfs_service_set_holders_changed(&service, holders_changed, &probe));
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
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
    AstraVfsFile refused = UINT32_MAX;
    assert(astra_vfs_service_retain_file(&service, call.session, call.file, ASTRA_VFS_OPEN_READ,
        &refused) == ASTRA_VFS_ERR_BUSY && refused == ASTRA_VFS_FILE_INVALID);
    assert(atomic_load(&probe.notifications) == 2u);
    assert(atomic_load(&probe.members) == 1u);

    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.reply.status == ASTRA_VFS_OK);
    assert(fake.closes == 1u);
    assert(slow->open_count == 0);
    assert(!astra_vfs_service_session_bound(&service, call.session, 0u, 0u));
    assert(atomic_load(&probe.notifications) == 4u);
    assert(atomic_load(&probe.members) == 0u);
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

static void
test_retained_description_survives_client_close(void)
{
    const AstraVfsMountOps writable = {mount_flush, mount_unmount};
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsFile handle, retained;
    AstraVfsNodeInfo info;
    FakeNode *node;
    MountProbe probe = {0};
    uint8_t bytes[8];
    uint32_t moved, session;

    reset();
    node = fake_create("/mapped", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "cache", 5u);
    node->size = 5u;
    session = open_session();
    begin_request(&request, session, "/mapped");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    handle = reply.file;
    assert(astra_vfs_service_retain_file(&service, session, handle, ASTRA_VFS_OPEN_READ,
                                         &retained) == ASTRA_VFS_OK);
    assert(retained == handle);
    AstraVfsFile first_retained = retained;
    assert(astra_vfs_service_retain_file(&service, session, handle, ASTRA_VFS_OPEN_READ,
                                         &retained) == ASTRA_VFS_OK);
    assert(retained == handle);
    fake.read_mode = 2u;
    begin_request(&request, session, NULL);
    request.file = handle;
    request.length = sizeof(bytes);
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && reply.count == 0u);
    fake.read_mode = 0u;
    begin_request(&request, session, NULL);
    request.file = handle;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(fake.closes == 0u && service.open_files == 1u);
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    astra_vfs_service_release_session(&service, session);
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_ERR_BUSY);
    assert(probe.calls == 0u);
    assert(astra_vfs_service_read_retained(&service, handle, 0u, bytes,
                                           sizeof(bytes), &moved) == ASTRA_VFS_OK);
    assert(moved == 5u && memcmp(bytes, "cache", 5u) == 0);
    fake.read_mode = 2u;
    assert(astra_vfs_service_read_retained(&service, handle, 0u, bytes,
                                           sizeof(bytes), &moved) ==
           ASTRA_VFS_ERR_IO);
    assert(moved == 0u);
    fake.read_mode = 0u;
    assert(astra_vfs_service_stat_retained(&service, handle, &info) ==
           ASTRA_VFS_OK);
    assert(info.size == 5u && info.kind == ASTRA_VFS_KIND_FILE);
    node->size = 3u;
    assert(astra_vfs_service_stat_retained(&service, handle, &info) ==
           ASTRA_VFS_OK);
    assert(info.size == 3u);
    assert(astra_vfs_service_write_retained(&service, handle, 1u, 2u,
                                            "XY", 2u, &moved) == ASTRA_VFS_OK);
    assert(moved == 1u && node->size == 3u);
    assert(node->bytes[0] == 'c' && node->bytes[1] == 'X');
    assert(service.changes.nodes == 1u);
    fake.write_overreport = 1u;
    assert(astra_vfs_service_write_retained(&service, handle, 2u, UINT64_MAX,
                                            "Y", 1u, &moved) == ASTRA_VFS_ERR_IO);
    assert(moved == 0u && node->bytes[2] == 'Y' &&
           service.changes.nodes == 2u);
    fake.write_overreport = 0u;
    fake.write_status = ASTRA_VFS_ERR_NO_SPACE;
    assert(astra_vfs_service_write_retained(&service, handle, 2u, UINT64_MAX,
                                            "Z", 1u, &moved) ==
           ASTRA_VFS_ERR_NO_SPACE);
    assert(moved == 0u && node->bytes[2] == 'Y' &&
           service.changes.nodes == 2u);
    fake.write_status = ASTRA_VFS_OK;
    assert(astra_vfs_service_sync_retained(&service, handle, NULL) == ASTRA_VFS_OK);
    fake.sync_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_sync_retained(&service, handle, NULL) == ASTRA_VFS_ERR_IO);
    fake.sync_status = ASTRA_VFS_OK;
    assert(astra_vfs_service_release_retained(&service, &first_retained) == ASTRA_VFS_OK);
    assert(first_retained == ASTRA_VFS_FILE_INVALID && retained == handle);
    assert(fake.closes == 0u && service.open_files == 1u);
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_read_retained(&service, handle, 0u, bytes,
                                           sizeof(bytes), &moved) == ASTRA_VFS_OK);
    AstraVfsOpenFile *open = &service.files[(handle & 0xffffu) - 1u];
    open->io->active = open;
    service.state_wait = NULL;
    assert(astra_vfs_service_release_retained(&service, &retained) ==
           ASTRA_VFS_ERR_IO);
    assert(open->retained == 1u && open->state == ASTRA_VFS_FILE_DETACHED && retained == handle);
    open->io->active = NULL;
    service.state_wait = test_state_wait;
    fake.close_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_ERR_IO);
    fake.close_status = ASTRA_VFS_OK;
    assert(retained == ASTRA_VFS_FILE_INVALID);
    assert(fake.closes == 1u && service.open_files == 0u);
    assert(astra_vfs_service_shutdown(&service, &writable, &probe) ==
           ASTRA_VFS_OK);
    assert(probe.calls == 2u);
    assert(astra_vfs_service_read_retained(&service, handle, 0u, bytes,
                                           sizeof(bytes), &moved) ==
           ASTRA_VFS_ERR_BAD_HANDLE);
    AstraVfsFile stale = handle;
    assert(astra_vfs_service_release_retained(&service, &stale) ==
           ASTRA_VFS_ERR_BAD_HANDLE);
    assert(stale == ASTRA_VFS_FILE_INVALID);
    assert(astra_vfs_service_stat_retained(&service, handle, &info) ==
           ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_vfs_service_write_retained(&service, handle, 0u, UINT64_MAX,
                                            "x", 1u, &moved) ==
           ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_vfs_service_sync_retained(&service, handle, NULL) ==
           ASTRA_VFS_ERR_BAD_HANDLE);

    reset();
    assert(fake_create("/readonly", ASTRA_VFS_KIND_FILE) != NULL);
    session = open_session();
    begin_request(&request, session, "/readonly");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    handle = reply.file;
    assert(astra_vfs_service_retain_file(&service, session, handle,
        ASTRA_VFS_OPEN_WRITE, &retained) == ASTRA_VFS_ERR_ACCESS);
    assert(retained == ASTRA_VFS_FILE_INVALID);
    assert(astra_vfs_service_retain_file(&service, session, handle, ASTRA_VFS_OPEN_READ,
                                         &retained) == ASTRA_VFS_OK);
    begin_request(&request, session, NULL);
    request.file = handle;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_write_retained(&service, retained, 0u, UINT64_MAX,
                                            "x", 1u, &moved) ==
           ASTRA_VFS_ERR_ACCESS);
    assert(moved == 0u && service.changes.nodes == 0u);
    assert(astra_vfs_service_release_retained(&service, &retained) ==
           ASTRA_VFS_OK);
}

static void test_retained_write_only_access(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsFile retained = ASTRA_VFS_FILE_INVALID;
    AstraVfsNodeInfo info;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t moved = 0u;

    reset();
    AstraVfsBackendOps cache_ops = fake_ops;
    service.backend.ops = &cache_ops;
    fake.check_context = 1u;
    fake.expected_context = service.backend.context;
    FakeNode *node = fake_create("/write-only", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    uint32_t session = open_session();
    begin_request(&request, session, "/write-only");
    request.flags = ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile handle = reply.file;
    uint32_t calls = fake.backend_calls;
    const uint32_t invalid[] = {0u, ASTRA_VFS_OPEN_APPEND,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND};
    for (uint32_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        retained = handle;
        assert(astra_vfs_service_retain_file(&service, session, handle,
            invalid[index], &retained) == ASTRA_VFS_ERR_INVALID);
        assert(retained == ASTRA_VFS_FILE_INVALID);
    }
    assert(astra_vfs_service_retain_file(&service, session, handle,
        ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_retain_file(&service, session, handle,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &retained) == ASTRA_VFS_ERR_ACCESS);
    assert(retained == ASTRA_VFS_FILE_INVALID && service.files[0].retained == 0u &&
        fake.backend_calls == calls);
    assert(astra_vfs_service_retain_file(&service, session, handle,
        ASTRA_VFS_OPEN_WRITE, &retained) == ASTRA_VFS_OK);
    begin_request(&request, session, NULL); request.file = handle;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, session);
    assert(service.open_sessions == 0u && service.open_files == 1u && node->open_count == 1);
    memset(page, 0xa5, sizeof(page));
    calls = fake.backend_calls;
    assert(astra_vfs_service_read_retained(&service, retained,
        0u, page, sizeof(page), &moved) == ASTRA_VFS_ERR_ACCESS);
    assert(moved == 0u && page[0] == 0xa5u && fake.backend_calls == calls);
    assert(astra_vfs_service_read_page_retained(&service, retained, ASTRA_VFS_OPEN_WRITE,
        NULL, 0u, page, &moved) == ASTRA_VFS_ERR_UNSUPPORTED);
    assert(moved == 0u && page[0] == 0xa5u && service.files[0].retained == 1u);
    cache_ops.read_for_write = fake_read;
    assert(astra_vfs_service_read_page_retained(&service, retained, ASTRA_VFS_OPEN_WRITE,
        NULL, 0u, page, &moved) == ASTRA_VFS_OK);
    assert(moved == 3u && memcmp(page, "old", 3u) == 0 && service.files[0].retained == 1u);
    calls = fake.backend_calls;
    assert(astra_vfs_service_read_page_retained(&service, retained,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, NULL, 0u, page, &moved) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_service_read_page_retained(&service, retained, 0u,
        NULL, 0u, page, &moved) == ASTRA_VFS_ERR_INVALID);
    assert(fake.backend_calls == calls && moved == 0u);
    memset(page, 0xa5, sizeof(page));
    moved = 99u;
    assert(astra_vfs_service_read_page_retained(&service, retained, ASTRA_VFS_OPEN_READ,
        NULL, 0u, page, &moved) == ASTRA_VFS_ERR_ACCESS);
    assert(moved == 0u && page[0] == 0xa5u && fake.backend_calls == calls);
    assert(astra_vfs_service_stat_retained(&service, retained, &info) == ASTRA_VFS_OK);
    memcpy(page, "new", 3u);
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &info, 0u, info.size, page, &moved, &info) == ASTRA_VFS_OK);
    assert(moved == 3u && node->size == 3u && memcmp(node->bytes, "new", 3u) == 0);
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    assert(retained == ASTRA_VFS_FILE_INVALID && service.open_files == 0u &&
        node->open_count == 0 && fake.closes == 1u);
}

static AstraVfsFile page_io_reference;
static uint32_t page_io_session;

static void close_and_fill_page_during_write(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t valid = 0u;

    fake.io_hook = NULL;
    assert(astra_vfs_open_file_active(&service.files[0]));
    begin_request(&request, page_io_session, NULL);
    request.file = page_io_reference;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(service.files[0].state == ASTRA_VFS_FILE_DETACHED &&
        astra_vfs_open_file_active(&service.files[0]) && fake.closes == 0u);
    memset(page, 0xa5, sizeof(page));
    assert(astra_vfs_service_read_page_retained(&service, page_io_reference, ASTRA_VFS_OPEN_READ,
        NULL, 0u, page, &valid) == ASTRA_VFS_ERR_BUSY);
    assert(valid == 0u);
    assert(astra_vfs_open_file_active(&service.files[0]) && service.files[0].retained == 1u);
}

static void close_and_release_during_page_read(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;

    fake.io_hook = NULL;
    assert(!astra_vfs_open_file_active(&service.files[0]) && service.files[0].retained == 2u);
    begin_request(&request, page_io_session, NULL);
    request.file = page_io_reference;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, page_io_session);
    assert(astra_vfs_service_release_retained(&service, &page_io_reference) == ASTRA_VFS_OK);
    assert(page_io_reference == ASTRA_VFS_FILE_INVALID && fake.closes == 0u &&
        service.files[0].retained == 1u);
}

static void sync_during_page_read(void)
{
    fake.io_hook = NULL;
    assert(astra_vfs_service_sync_retained(&service, page_io_reference, NULL) == ASTRA_VFS_OK);
}

static void test_retained_page_read_during_description_io(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t moved = 0u;

    reset();
    FakeNode *node = fake_create("/page-io", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    page_io_session = open_session();
    begin_request(&request, page_io_session, "/page-io");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile handle = reply.file;
    assert(astra_vfs_service_retain_file(&service, page_io_session, handle,
        ASTRA_VFS_OPEN_READ, &page_io_reference) == ASTRA_VFS_OK);
    fake.io_hook = close_and_fill_page_during_write;
    assert(astra_vfs_service_write_from(&service, page_io_session, handle,
        0u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK && moved == 3u);
    assert(fake.io_hook == NULL && memcmp(node->bytes, "new", 3u) == 0 &&
        !astra_vfs_open_file_active(&service.files[0]) && fake.closes == 0u);
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    assert(astra_vfs_service_read_page_retained(&service, page_io_reference, ASTRA_VFS_OPEN_READ,
        NULL, 0u, page, &moved) == ASTRA_VFS_OK && moved == 3u &&
        memcmp(page, "new", 3u) == 0);
    assert(astra_vfs_service_release_retained(&service, &page_io_reference) == ASTRA_VFS_OK);
    assert(fake.closes == 1u && service.open_files == 0u);
    astra_vfs_service_release_session(&service, page_io_session);
}

static void fill_independent_page_during_mutation(void)
{
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t valid = 99u;

    fake.io_hook = NULL;
    assert(astra_vfs_service_read_page_retained(&service, page_io_reference,
        ASTRA_VFS_OPEN_READ, NULL, 0u, page, &valid) == ASTRA_VFS_ERR_BUSY);
    assert(valid == 0u);
}

static void fill_independent_page_then_fail_stat(void)
{
    fill_independent_page_during_mutation();
    fake.stat_fail_call = fake.stat_calls + 1u;
}

typedef struct CacheWriteProbe {
    AstraVfsFile owned;
    AstraVfsFileCache cache_storage;
    AstraVfsFile read_only;
    uint8_t *flush_page;
    uint32_t flush_calls, flush_status, flush_budget;
    uint32_t read_calls, read_limit, read_status, read_overreport, read_bad_position;
    uint32_t stat_calls, stat_status;
    const AstraVfsOpenFile *file;
    uint32_t calls, accepted, status, flags, close_session, prepare_flags;
    uint64_t offset, max_end;
    int adopt, metadata;
    struct CacheWriteProbe *next;
} CacheWriteProbe;

static CacheWriteProbe *cache_probe(void *context, const AstraVfsOpenFile *file)
{
    CacheWriteProbe *probe = context;
    while (probe != NULL && probe->file != NULL && probe->file->io != file->io)
        probe = probe->next;
    assert(probe != NULL);
    return probe;
}

static uint32_t cache_write_probe(void *context, const AstraVfsOpenFile *file,
    AstraVfsFile *owned, uint64_t offset, uint32_t flags, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved, uint64_t *position)
{
    CacheWriteProbe *probe = cache_probe(context, file);
    assert(astra_vfs_open_file_active(file) && file->retained != 0u);
    assert((file->flags & ASTRA_VFS_OPEN_WRITE) != 0u && *owned != ASTRA_VFS_FILE_INVALID);
    assert(pthread_mutex_trylock(service.state_lock_context) == 0);
    assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    assert(length == 3u && memcmp(buffer, "new", 3u) == 0);
    ++probe->calls;
    probe->file = file;
    probe->offset = offset; probe->flags = flags; probe->max_end = max_end;
    if (probe->close_session != 0u) {
        AstraVfsRequest request;
        AstraVfsReply reply;
        begin_request(&request, probe->close_session, NULL); request.file = *owned;
        fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK && file->state == ASTRA_VFS_FILE_DETACHED);
    }
    if (probe->metadata) {
        AstraVfsFileCache snapshot = {.size = UINT64_MAX};
        if (probe->prepare_flags != 0u) {
            AstraVfsFile original = *owned;
            assert(astra_vfs_service_cache_prepare(&service, file, owned,
                probe->prepare_flags | 2u, NULL, &snapshot) == ASTRA_VFS_ERR_INVALID &&
                *owned == original && snapshot.size == UINT64_MAX);
        }
        AstraVfsFileCache *storage = probe->cache_storage.writer == ASTRA_VFS_FILE_INVALID ?
            &probe->cache_storage : NULL;
        if (storage != NULL) {
            AstraVfsFile original = *owned;
            uint32_t missing = astra_vfs_service_cache_prepare(&service, file, owned,
                probe->prepare_flags, NULL, &snapshot);
            if (missing == ASTRA_VFS_ERR_UNSUPPORTED) return missing;
            assert(missing == ASTRA_VFS_ERR_LIMIT && *owned == original &&
                snapshot.size == UINT64_MAX);
            storage->writer = original;
            assert(astra_vfs_service_cache_prepare(&service, file, owned, probe->prepare_flags, storage, &snapshot) ==
                ASTRA_VFS_ERR_BUSY && *owned == original && snapshot.size == UINT64_MAX);
            storage->writer = ASTRA_VFS_FILE_INVALID;
        }
        uint32_t status = astra_vfs_service_cache_prepare(&service, file, owned,
            probe->prepare_flags, storage, &snapshot);
        if (status != ASTRA_VFS_OK) return status;
        assert(!probe->prepare_flags || snapshot.external_writes);
        assert(file->io->cache == &probe->cache_storage);
        probe->owned = snapshot.writer;
        uint32_t remaining = 1u;
        assert(astra_vfs_service_sync_retained(&service, probe->owned, &remaining) == ASTRA_VFS_ERR_BUSY &&
            remaining == 1u);
        uint64_t size = offset + probe->accepted;
        if (size < snapshot.size) size = snapshot.size;
        status = astra_vfs_service_cache_publish(&service, file, size, UINT64_C(1234567890123));
        if (status != ASTRA_VFS_OK) return status;
    }
    if (probe->adopt) {
        assert(probe->owned == ASTRA_VFS_FILE_INVALID);
        probe->owned = *owned; *owned = ASTRA_VFS_FILE_INVALID;
    }
    *moved = probe->accepted;
    *position = offset + *moved;
    return probe->status;
}

static const AstraVfsCacheOps cache_write_ops = {.write = cache_write_probe};

static uint32_t cache_stat_probe(void *context, const AstraVfsOpenFile *file,
    AstraVfsNodeInfo *info)
{
    CacheWriteProbe *probe = cache_probe(context, file);
    assert(astra_vfs_open_file_active(file));
    assert(pthread_mutex_trylock(service.state_lock_context) == 0);
    assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    ++probe->stat_calls;
    if (probe->stat_status != ASTRA_VFS_OK) return probe->stat_status;
    return file->backend.ops->stat_node(file->backend.context, file->node, info);
}

static void test_cache_stat_keeps_native_owner(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CacheWriteProbe probe = {0};
    AstraVfsCacheOps ops = cache_write_ops;
    AstraVfsClient client;
    AstraVfsFile handle, retained;
    AstraVfsDirEntry info;
    AstraVfsNodeInfo backing;
    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/cache-stat", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    node->size = 3u;
    ops.stat = cache_stat_probe;
    assert(astra_vfs_service_set_cache_ops(&service, &ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/cache-stat", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
        &handle, NULL, NULL) == ASTRA_VFS_OK);
    uint32_t calls = probe.stat_calls;
    assert(astra_vfs_stat_file_meta(&client, handle, &info) == ASTRA_VFS_OK &&
        info.size == 3u && probe.stat_calls == calls + 1u);
    calls = probe.stat_calls;
    assert(astra_vfs_truncate(&client, handle, 2u) == ASTRA_VFS_OK &&
        node->size == 2u && probe.stat_calls == calls + 1u);
    assert(astra_vfs_service_retain_file(&service, client.session, handle,
        ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, handle) == ASTRA_VFS_OK);
    calls = probe.stat_calls;
    assert(astra_vfs_service_stat_retained(&service, retained, &backing) == ASTRA_VFS_OK &&
        backing.size == 2u && probe.stat_calls == calls + 1u);
    probe.stat_status = ASTRA_VFS_ERR_BUSY;
    assert(astra_vfs_service_stat_retained(&service, retained, &backing) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_open(&client, "/cache-stat", ASTRA_VFS_OPEN_READ,
        &handle, NULL, NULL) == ASTRA_VFS_ERR_BUSY && service.open_files == 1u &&
        node->open_count == 1);
    probe.stat_status = ASTRA_VFS_OK;
    ops.stat = NULL;
    calls = probe.stat_calls;
    assert(astra_vfs_service_stat_retained(&service, retained, &backing) == ASTRA_VFS_OK &&
        backing.size == 2u && probe.stat_calls == calls);
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(service.open_files == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static uint32_t cache_sync_probe(void *context, const AstraVfsOpenFile *file,
    uint32_t *remaining)
{
    CacheWriteProbe *probe = cache_probe(context, file);
    AstraVfsFileCache cache;
    AstraVfsNodeInfo current;
    uint32_t sequence, moved = 99u;
    assert(astra_vfs_open_file_active(file));
    assert(pthread_mutex_trylock(service.state_lock_context) == 0);
    assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    ++probe->flush_calls;
    probe->flush_budget = remaining == NULL ? 0u : *remaining;
    if (probe->flush_status != ASTRA_VFS_OK || probe->flush_page == NULL)
        return probe->flush_status;
    assert(astra_vfs_service_cache_snapshot(&service, probe->owned, &cache, &sequence) ==
        ASTRA_VFS_OK);
    AstraVfsOpenFile inactive = {0};
    assert(astra_vfs_service_write_page_retained(&service, probe->owned, &inactive,
        &cache.backing, 0u, cache.size, probe->flush_page, &moved, &current) ==
        ASTRA_VFS_ERR_ACCESS && moved == 0u && current.kind == 0u);
    if (probe->read_only != ASTRA_VFS_FILE_INVALID) {
        assert(astra_vfs_service_write_page_retained(&service, probe->read_only, file,
            &cache.backing, 0u, cache.size, probe->flush_page, &moved, &current) ==
            ASTRA_VFS_ERR_ACCESS && moved == 0u && current.kind == 0u);
    }
    uint32_t status = astra_vfs_service_write_page_retained(&service, probe->owned, file,
        &cache.backing, 0u, cache.size, probe->flush_page, &moved, &current);
    assert(astra_vfs_open_file_active(file));
    return status;
}

static const AstraVfsCacheOps cache_flush_ops = {
    .write = cache_write_probe, .sync = cache_sync_probe
};

static uint32_t cache_read_probe(void *context, const AstraVfsOpenFile *file,
    uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    CacheWriteProbe *probe = cache_probe(context, file);
    assert(astra_vfs_open_file_active(file));
    assert(file->io->cache == &probe->cache_storage);
    assert((file->flags & ASTRA_VFS_OPEN_READ) != 0u);
    assert(pthread_mutex_trylock(service.state_lock_context) == 0);
    assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    if (probe->read_calls == 0u) {
        AstraVfsFile retained = ASTRA_VFS_FILE_INVALID;
        uint16_t references = file->retained;
        assert(astra_vfs_service_retain_active(&service, file, &retained) == ASTRA_VFS_OK);
        assert(file->retained == references + 1u);
        assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK &&
            retained == ASTRA_VFS_FILE_INVALID && file->retained == references);
    }
    ++probe->read_calls;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u) offset = fake.position;
    uint64_t size = probe->cache_storage.size;
    uint32_t count = offset >= size ? 0u :
        size - offset < length ? (uint32_t)(size - offset) : length;
    if (probe->read_limit != 0u && count > probe->read_limit) count = probe->read_limit;
    if (count != 0u) memcpy(buffer, probe->flush_page + offset, count);
    *moved = probe->read_overreport ? length + 1u : count;
    *position = probe->read_bad_position ? UINT64_MAX : offset + count;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u) fake.position += count;
    if (probe->close_session != 0u) {
        assert(file->session == probe->close_session && file->state == ASTRA_VFS_FILE_OPEN);
        astra_vfs_service_release_session(&service, probe->close_session);
        probe->close_session = 0u;
    }
    return probe->read_status;
}

static void test_cache_reads_share_native_owner(uint32_t flags)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'o', 'l', 'd', 0u, 'n', 'e', 'w'};
    CacheWriteProbe probe = {.accepted = 3u, .metadata = 1, .flush_page = page,
        .prepare_flags = flags};
    AstraVfsCacheOps ops = cache_flush_ops;
    AstraVfsClient client;
    AstraVfsFile writer, reader, retained;
    uint32_t moved, sequence;
    uint64_t position, size;
    uint8_t bytes[8];
    AstraVfsFileCache cache;

    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/read-cache", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    ops.read = cache_read_probe;
    assert(astra_vfs_service_set_cache_ops(&service, &ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/read-cache", ASTRA_VFS_OPEN_WRITE, &writer, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, writer, 4u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK);
    assert(node->size == 3u && memcmp(node->bytes, "old", 3u) == 0);
    assert(astra_vfs_open(&client, "/read-cache", ASTRA_VFS_OPEN_READ, &reader, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_read(&client, reader, 0u, bytes, 7u, &moved) == ASTRA_VFS_OK &&
        moved == 7u && memcmp(bytes, page, 7u) == 0 && probe.read_calls == 1u);
    ops.read = NULL;
    memset(bytes, 0xa5, sizeof(bytes));
    assert(astra_vfs_read(&client, reader, 0u, bytes, 7u, &moved) == ASTRA_VFS_ERR_BUSY &&
        moved == 0u && bytes[0] == 0xa5u && probe.read_calls == 1u);
    ops.read = cache_read_probe;
    assert(astra_vfs_read(&client, writer, 0u, bytes, 7u, &moved) == ASTRA_VFS_ERR_ACCESS &&
        probe.read_calls == 1u);
    assert(astra_vfs_service_retain_file(&service, client.session, reader,
        ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, reader) == ASTRA_VFS_OK);
    assert(astra_vfs_service_read_retained(&service, retained, 4u, bytes, 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && memcmp(bytes, "new", 3u) == 0);
    probe.read_limit = 2u;
    assert(astra_vfs_service_read_path(&service, client.session, "/read-cache",
        bytes, 7u, &moved, &size) == ASTRA_VFS_OK && moved == 7u && size == 7u &&
        memcmp(bytes, page, 7u) == 0);
    assert(service.open_files == 2u && node->open_count == 2);
    assert(astra_vfs_service_read_path(&service, client.session, "/read-cache",
        bytes, 6u, &moved, &size) == ASTRA_VFS_ERR_LIMIT && moved == 0u && size == 7u);
    fake.close_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_read_path(&service, client.session, "/read-cache",
        bytes, 7u, &moved, &size) == ASTRA_VFS_ERR_IO && moved == 7u && size == 7u);
    assert(service.open_files == 2u && node->open_count == 2);
    fake.close_status = ASTRA_VFS_OK;
    fake.stat_fail_call = fake.stat_calls + 1u;
    assert(astra_vfs_service_read_path(&service, client.session, "/read-cache",
        bytes, 7u, &moved, &size) == ASTRA_VFS_ERR_IO && moved == 0u);
    assert(service.open_files == 2u && node->open_count == 2);
    fake.stat_fail_call = 0u;
    probe.read_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_read_retained(&service, retained, 4u, bytes, 3u, &moved) ==
        ASTRA_VFS_ERR_IO && moved == 2u && memcmp(bytes, "ne", 2u) == 0);
    probe.read_status = ASTRA_VFS_OK;
    assert(astra_vfs_open(&client, "/read-cache", ASTRA_VFS_OPEN_READ, &reader, NULL, NULL) == ASTRA_VFS_OK);
    fake.position = 4u;
    assert(astra_vfs_read_position(&client, reader, 0u, ASTRA_VFS_IO_SEQUENTIAL,
        bytes, 3u, &moved, &position) == ASTRA_VFS_OK && moved == 2u && position == 6u &&
        memcmp(bytes, "ne", 2u) == 0);
    probe.read_overreport = 1u;
    assert(astra_vfs_read(&client, reader, 0u, bytes, 3u, &moved) == ASTRA_VFS_ERR_IO && moved == 0u);
    probe.read_overreport = 0u; probe.read_bad_position = 1u;
    assert(astra_vfs_read_position(&client, reader, 0u, ASTRA_VFS_IO_SEQUENTIAL,
        bytes, 3u, &moved, &position) == ASTRA_VFS_ERR_IO && moved == 0u);
    probe.read_bad_position = 0u; probe.read_limit = 0u;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
    probe.owned = ASTRA_VFS_FILE_INVALID;
    AstraVfsDirEntry persisted;
    assert(astra_vfs_stat_file_meta(&client, reader, &persisted) == ASTRA_VFS_OK);
    if ((flags & ASTRA_VFS_CACHE_EXTERNAL_WRITES) != 0u)
        assert(persisted.mtime == 1234567 && fake.set_times_calls == 0u);
    else
        assert(persisted.mtime == 1234 && persisted.meta.mtime_nsec == 567890123u);
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, writer) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, reader) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(service.open_files == 0u && node->open_count == 0);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_descriptor_times_validate_and_preserve_unselected_fields(void)
{
    AstraVfsClient client;
    AstraVfsFile writer, reader;
    AstraVfsDirEntry times = {.mtime = -1, .meta = {.atime = 91,
        .atime_nsec = 123u, .mtime_nsec = 999999999u}}, actual;
    const uint32_t fields = ASTRA_VFS_META_ATIME | ASTRA_VFS_META_MTIME;
    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/times", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/times", ASTRA_VFS_OPEN_WRITE, &writer, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/times", ASTRA_VFS_OPEN_READ, &reader, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_set_file_times(&client, reader, &times, fields) == ASTRA_VFS_ERR_ACCESS);
    assert(fake.set_times_calls == 0u);
    assert(astra_vfs_set_file_times(&client, writer, &times, fields) == ASTRA_VFS_OK);
    assert(astra_vfs_stat_file_meta(&client, writer, &actual) == ASTRA_VFS_OK &&
        actual.mtime == -1 && actual.meta.mtime_nsec == 999999999u &&
        actual.meta.atime == 91 && actual.meta.atime_nsec == 123u);
    times.mtime = 0; times.meta.mtime_nsec = 0u; times.meta.atime = -222;
    assert(astra_vfs_set_file_times(&client, writer, &times, ASTRA_VFS_META_MTIME) == ASTRA_VFS_OK);
    assert(astra_vfs_stat_file_meta(&client, writer, &actual) == ASTRA_VFS_OK &&
        actual.mtime == 0 && actual.meta.mtime_nsec == 0u && actual.meta.atime == 91);
    times.meta.mtime_nsec = 1000000000u;
    assert(astra_vfs_set_file_times(&client, writer, &times, ASTRA_VFS_META_MTIME) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_set_file_times(&client, writer, &times, 0u) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_set_file_times(&client, writer, &times, ASTRA_VFS_META_MODE) == ASTRA_VFS_ERR_INVALID);
    times.meta.mtime_nsec = 0u;
    fake.set_times_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_set_file_times(&client, writer, &times, fields) == ASTRA_VFS_ERR_IO);
    assert(fake.set_times_calls == 3u);
    fake.set_times_status = ASTRA_VFS_OK;
    AstraVfsRequest request; AstraVfsReply reply;
    begin_request(&request, client.session, NULL);
    request.file = writer; request.flags = ASTRA_VFS_META_MTIME;
    request.length = ASTRA_VFS_FILE_TIMES_SIZE - 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_SET_FILE_TIMES, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.set_times_calls == 3u);
    request.length = ASTRA_VFS_FILE_TIMES_SIZE;
    astra_store_be32(request.body.payload + 20u, 1000000000u);
    fixture_dispatch(&service, ASTRA_VFS_OP_SET_FILE_TIMES, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.set_times_calls == 3u);
    assert(astra_vfs_unlink(&client, "/times", 0u) == ASTRA_VFS_OK);
    assert(astra_vfs_set_file_times(&client, writer, &times, fields) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, reader) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, writer) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
}

static void test_whole_cache_read_keeps_session_lifetime(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'o', 'l', 'd', 0u, 'n', 'e', 'w'};
    CacheWriteProbe probe = {.accepted = 3u, .metadata = 1, .flush_page = page, .read_limit = 2u};
    AstraVfsCacheOps ops = cache_flush_ops;
    AstraVfsClient client;
    AstraVfsFile writer;
    AstraVfsFileCache cache;
    uint8_t bytes[8];
    uint32_t moved, sequence;
    uint64_t size;

    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/whole-cache", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    ops.read = cache_read_probe;
    assert(astra_vfs_service_set_cache_ops(&service, &ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/whole-cache", ASTRA_VFS_OPEN_WRITE, &writer, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, writer, 4u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, writer) == ASTRA_VFS_OK);
    probe.close_session = client.session;
    assert(astra_vfs_service_read_path(&service, client.session, "/whole-cache",
        bytes, sizeof(bytes), &moved, &size) == ASTRA_VFS_OK && moved == 7u && size == 7u &&
        memcmp(bytes, page, 7u) == 0 && probe.read_calls == 4u);
    assert(service.open_sessions == 0u && service.open_files == 1u && node->open_count == 1);
    assert(astra_vfs_service_sync_retained(&service, probe.owned, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
    assert(service.open_files == 0u && node->open_count == 0);
    moved = 99u; size = 99u;
    assert(astra_vfs_service_read_path(NULL, 0u, "/whole-cache", bytes,
        sizeof(bytes), &moved, &size) == ASTRA_VFS_ERR_INVALID && moved == 0u && size == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_cache_write_handoff_keeps_rooted_authority(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CacheWriteProbe probe = {.accepted = 3u, .adopt = 1};
    BoundBackend first = {.incarnation = 7u}, second = {.incarnation = 8u};
    BackendBindings bindings = {.selected = &first};
    AstraVfsClient client;
    AstraVfsFile handle;
    uint32_t moved;
    uint64_t position;

    reset();
    AstraVfsCacheOps incomplete = {.sync = cache_sync_probe};
    assert(!astra_vfs_service_set_cache_ops(NULL, &cache_write_ops, &probe));
    assert(!astra_vfs_service_set_cache_ops(&service, NULL, &probe));
    assert(!astra_vfs_service_set_cache_ops(&service, &incomplete, &probe));
    assert(astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
    assert(!astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    FakeNode *node = fake_create("/cached", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    fake.check_context = 1u; fake.expected_context = &first;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/cached", ASTRA_VFS_OPEN_WRITE, &handle, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_service_write_position_from(&service, client.session, handle,
        17u, ASTRA_VFS_IO_SEQUENTIAL | ASTRA_VFS_WRITE_APPEND, 99u, "new", 3u,
        &moved, &position) == ASTRA_VFS_OK);
    assert(probe.calls == 1u && probe.owned == handle && moved == 3u && position == 20u);
    assert(probe.offset == 17u && probe.max_end == 99u &&
        probe.flags == (ASTRA_VFS_IO_SEQUENTIAL | ASTRA_VFS_WRITE_APPEND));
    assert(probe.file->backend.context == &first && probe.file->retained == 1u);
    assert(fake.write_calls == 0u && memcmp(node->bytes, "old", 3u) == 0);
    assert(service.changes.nodes == 1u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(first.references == 1u && service.open_files == 1u && probe.file->state == ASTRA_VFS_FILE_DETACHED);
    bindings.selected = &second;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    uint8_t unread[3];
    assert(astra_vfs_service_read_retained(&service, probe.owned, 0u,
        unread, sizeof(unread), &moved) == ASTRA_VFS_ERR_ACCESS && moved == 0u);
    /* A later retained write lends another reference without replacing the owner. */
    probe.adopt = 0;
    assert(astra_vfs_service_write_retained(&service, probe.owned, 0u, 99u,
        "new", 3u, &moved) == ASTRA_VFS_OK && moved == 3u);
    assert(probe.calls == 2u && probe.file->retained == 1u &&
        probe.file->backend.context == &first && fake.write_calls == 0u);
    AstraVfsNodeInfo expected, current;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'n', 'e', 'w'};
    assert(astra_vfs_service_stat_retained(&service, probe.owned, &expected) == ASTRA_VFS_OK);
    assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL, &expected,
        0u, 3u, page, &moved, &current) == ASTRA_VFS_OK && moved == 3u);
    assert(probe.calls == 2u && memcmp(node->bytes, "new", 3u) == 0);
    assert(astra_vfs_service_release_retained(&service, &probe.owned) == ASTRA_VFS_OK);
    assert(first.references == 0u && service.open_files == 0u && fake.closes == 1u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && second.references == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_cache_write_borrow_and_failed_prefix(void)
{
    for (uint32_t variant = 0u; variant < 5u; ++variant) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        CacheWriteProbe probe = {.accepted = variant == 2u ? 1u : variant == 3u ? 4u : 3u,
            .status = variant == 2u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK, .adopt = variant == 2u};
        AstraVfsClient client;
        AstraVfsFile handle;
        uint32_t moved;
        reset();
        assert(astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
        assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
        assert(fake_create("/cached", ASTRA_VFS_KIND_FILE) != NULL);
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/cached", ASTRA_VFS_OPEN_READ, &handle, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_service_write_from(&service, client.session, handle, 0u,
            UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_ERR_ACCESS && probe.calls == 0u);
        assert(astra_vfs_close(&client, handle) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/cached", ASTRA_VFS_OPEN_WRITE, &handle, NULL, NULL) == ASTRA_VFS_OK);
        AstraVfsOpenFile *file = &service.files[(handle & 0xffffu) - 1u];
        file->retained = UINT16_MAX;
        assert(astra_vfs_service_write_from(&service, client.session, handle, 0u,
            UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_ERR_LIMIT && probe.calls == 0u);
        file->retained = 0u;
        if (variant == 1u || variant == 2u || variant == 4u) probe.close_session = client.session;
        if (variant == 4u) fake.close_status = ASTRA_VFS_ERR_IO;
        assert(astra_vfs_write(&client, handle, 0u,
            UINT64_MAX, "new", 3u, &moved) == (variant >= 2u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK));
        assert(probe.calls == 1u && fake.write_calls == 0u &&
            moved == (variant == 3u ? 0u : probe.accepted));
        /* An overreport can follow a real mutation; preserve invalidation. */
        assert(service.changes.nodes == 1u);
        assert(service.open_files == (variant == 1u || variant == 4u ? 0u : 1u));
        if (variant != 1u && variant != 4u)
            assert(file->retained == (variant == 2u ? 1u : 0u));
        if (variant == 2u) {
            assert(probe.owned == handle && file->state == ASTRA_VFS_FILE_DETACHED);
            assert(astra_vfs_service_release_retained(&service, &probe.owned) == ASTRA_VFS_OK);
        }
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
        assert(pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_cache_logical_metadata(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CacheWriteProbe probe = {.accepted = 3u, .metadata = 1};
    AstraVfsClient client;
    AstraVfsFile handle;
    AstraVfsDirEntry info;
    AstraVfsFileCache snapshot;
    uint32_t sequence;
    uint32_t moved;

    reset();
    AstraVfsBackendOps descriptions = fake_ops;
    descriptions.seek = fake_seek;
    service.backend.ops = &descriptions;
    fake.exact_identity = 1u;
    FakeNode *node = fake_create("/cached-meta", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    assert(astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/cached-meta", ASTRA_VFS_OPEN_READ |
        ASTRA_VFS_OPEN_WRITE, &handle, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, handle, 17u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && node->size == 3u);
    assert(astra_vfs_stat_file_meta(&client, handle, &info) == ASTRA_VFS_OK &&
        info.size == 20u);
    assert(info.mtime == 1234 && info.meta.mtime_nsec == 567890123u &&
        (info.meta.valid & (ASTRA_VFS_META_MTIME | ASTRA_VFS_META_CTIME)) ==
        (ASTRA_VFS_META_MTIME | ASTRA_VFS_META_CTIME) &&
        info.meta.ctime == 1234 && info.meta.ctime_nsec == 567890123u);
    assert(astra_vfs_stat_meta(&client, "/cached-meta", &info) == ASTRA_VFS_OK &&
        info.size == 20u);
    node->mode = 0u;
    fake.ctime_nsec = 1u;
    assert(astra_vfs_stat_meta(&client, "/cached-meta", &info) == ASTRA_VFS_OK &&
        info.size == 20u && info.mode == 0u && (info.meta.valid & ASTRA_VFS_META_MODE) != 0u &&
        info.meta.ctime == 1234567 && info.meta.ctime_nsec == 1u);
    fake.ctime_nsec = 0u;
    uint64_t size, position;
    AstraVfsFile peer;
    assert(astra_vfs_open(&client, "/cached-meta", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
        &peer, &size, NULL) == ASTRA_VFS_OK && size == 20u);
    AstraVfsFile writer = probe.owned;
    assert(astra_vfs_write(&client, peer, 17u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && probe.owned == writer && node->size == 3u);
    assert(astra_vfs_stat_file_meta(&client, peer, &info) == ASTRA_VFS_OK && info.size == 20u);
    assert(service.cached_nodes == 1u);
    fake.position = 19u;
    assert(astra_vfs_seek(&client, handle, -1, 2u, &position) == ASTRA_VFS_OK &&
        position == 19u && fake.description_mask == 0u && fake.description_argument == 19u);
    assert(astra_vfs_seek(&client, handle, -21, 2u, &position) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_seek(&client, handle, INT64_MAX, 2u, &position) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_truncate(&client, handle, 0u) == ASTRA_VFS_ERR_BUSY && node->size == 3u);
    assert(astra_vfs_sync(&client, handle) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &snapshot, &sequence) ==
        ASTRA_VFS_OK && snapshot.size == 20u && snapshot.backing.size == 3u);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_ERR_BUSY);
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'o', 'l', 'd'};
    memcpy(page + 17u, "new", 3u);
    AstraVfsNodeInfo current;
    assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL, &snapshot.backing,
        0u, snapshot.size, page, &moved, &current) == ASTRA_VFS_OK && moved == 20u);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &snapshot, &sequence) ==
        ASTRA_VFS_OK && snapshot.backing.size == 20u && node->size == 20u);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
    assert(service.cached_nodes == 0u && probe.file->io->cache == NULL &&
        probe.cache_storage.writer == ASTRA_VFS_FILE_INVALID);
    assert(astra_vfs_sync(&client, handle) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, peer) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, handle) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_cache_sync_and_truncate(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'o', 'l', 'd'};
    CacheWriteProbe probe = {.accepted = 3u, .metadata = 1, .flush_page = page};
    BoundBackend first = {.incarnation = 7u}, second = {.incarnation = 8u};
    BackendBindings bindings = {.selected = &first};
    AstraVfsClient client;
    AstraVfsFile writer, reader;
    AstraVfsFileCache cache;
    AstraVfsDirEntry info;
    uint32_t moved, sequence;

    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/sync-cache", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    assert(astra_vfs_service_set_cache_ops(&service, &cache_flush_ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    fake.check_context = 1u; fake.expected_context = &first;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/sync-cache", ASTRA_VFS_OPEN_WRITE, &writer, NULL, NULL) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_service_set_state_wait(&service, test_slot_reuse_wait, test_state_wake));
    assert(astra_vfs_write(&client, writer, 17u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && node->size == 3u);
    memcpy(page + 17u, "new", 3u);
    assert(astra_vfs_open(&client, "/sync-cache", ASTRA_VFS_OPEN_READ, &reader, NULL, NULL) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_service_retain_file(&service, client.session, reader,
        ASTRA_VFS_OPEN_READ, &probe.read_only) == ASTRA_VFS_OK);
    fake.set_times_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_ERR_IO && fake.sync_calls == 0u);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.written && cache.error == ASTRA_VFS_ERR_IO &&
        cache.backing.size == 20u);
    fake.set_times_status = ASTRA_VFS_OK;
    probe.flush_calls = 0u;
    fake.sync_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_ERR_IO && node->size == 20u);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_ERR_IO && cache.backing.size == 20u);
    ++fake.ctime_nsec;
    AstraVfsNodeInfo conflict = {0};
    assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL,
        &cache.backing, 0u, cache.size, page, &moved, &conflict) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_ERR_IO);
    --fake.ctime_nsec;
    probe.flush_status = ASTRA_VFS_ERR_BUSY;
    uint32_t remaining = 7u;
    assert(astra_vfs_service_sync_retained(&service, probe.owned, &remaining) == ASTRA_VFS_ERR_BUSY &&
        probe.flush_budget == 7u);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_ERR_IO);
    probe.flush_status = ASTRA_VFS_OK;
    fake.sync_status = ASTRA_VFS_OK;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_OK && probe.flush_calls == 3u &&
        probe.flush_budget == 0u &&
        node->size == 20u && memcmp(node->bytes, page, 20u) == 0);
    probe.flush_status = ASTRA_VFS_ERR_BUSY;
    remaining = 7u;
    assert(astra_vfs_service_sync_retained(&service, probe.owned, &remaining) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_OK);
    AstraVfsFile deferred_reader;
    assert(astra_vfs_open(&client, "/sync-cache", ASTRA_VFS_OPEN_READ, &deferred_reader, NULL, NULL) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, deferred_reader) == ASTRA_VFS_OK);
    ++fake.ctime_nsec;
    assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL,
        &cache.backing, 0u, cache.size, page, &moved, &conflict) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_sync_retained(&service, probe.owned, &remaining) == ASTRA_VFS_ERR_BUSY);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_ERR_BUSY);
    --fake.ctime_nsec;
    probe.flush_status = ASTRA_VFS_OK;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_OK);
    probe.flush_page = NULL;
    fake.sync_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_ERR_IO);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.error == ASTRA_VFS_ERR_IO);
    fake.sync_status = ASTRA_VFS_OK;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_OK);
    probe.flush_page = page;

    assert(astra_vfs_write(&client, writer, 20u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && node->size == 20u);
    memcpy(page + 20u, "new", 3u);
    probe.flush_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_ERR_IO && node->size == 20u);
    AstraVfsFile transient;
    assert(astra_vfs_open(&client, "/sync-cache", ASTRA_VFS_OPEN_READ, &transient, NULL, NULL) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, transient) == ASTRA_VFS_ERR_IO);
    assert(astra_vfs_close(&client, writer) == ASTRA_VFS_ERR_IO);
    AstraVfsFile held = probe.owned;
    assert(astra_vfs_service_release_retained(&service, &held) == ASTRA_VFS_ERR_BUSY &&
        held == probe.owned);
    probe.flush_status = ASTRA_VFS_OK;
    bindings.selected = &second;
    assert(astra_vfs_sync(&client, reader) == ASTRA_VFS_OK && node->size == 23u);
    bindings.selected = &first;

    assert(astra_vfs_truncate(&client, reader, 2u) == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_open(&client, "/sync-cache", ASTRA_VFS_OPEN_WRITE, &writer, NULL, NULL) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, writer, 25u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_OK && moved == 3u && node->size == 23u);
    memcpy(page + 25u, "new", 3u);
    probe.flush_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_truncate(&client, writer, 2u) == ASTRA_VFS_ERR_IO && node->size == 23u);
    probe.flush_status = ASTRA_VFS_OK;
    assert(astra_vfs_truncate(&client, writer, 2u) == ASTRA_VFS_OK && node->size == 2u);
    assert(astra_vfs_stat_file_meta(&client, reader, &info) == ASTRA_VFS_OK && info.size == 2u);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.size == 2u && cache.backing.size == 2u && cache.written == 0u);
    assert(astra_vfs_write(&client, writer, 0u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK &&
        node->size == 2u);
    memcpy(page, "new", 3u);
    AstraVfsDirEntry times = {.mtime = 0, .meta = {.mtime_nsec = 17u}};
    fake.set_times_status = ASTRA_VFS_ERR_IO;
    uint32_t timestamp_calls = fake.set_times_calls;
    assert(astra_vfs_set_file_times(&client, writer, &times, ASTRA_VFS_META_MTIME) == ASTRA_VFS_ERR_IO &&
        fake.set_times_calls == timestamp_calls + 1u);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && cache.written && cache.error == ASTRA_VFS_ERR_IO);
    fake.set_times_status = ASTRA_VFS_OK;
    assert(astra_vfs_set_file_times(&client, writer, &times, ASTRA_VFS_META_MTIME) == ASTRA_VFS_OK &&
        fake.set_times_calls == timestamp_calls + 3u);
    assert(astra_vfs_stat_file_meta(&client, reader, &info) == ASTRA_VFS_OK && info.size == 3u &&
        info.mtime == 0 && info.meta.mtime_nsec == 17u && memcmp(node->bytes, "new", 3u) == 0);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) ==
        ASTRA_VFS_OK && !cache.written && cache.error == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_release_retained(&service, &probe.read_only) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, writer) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, reader) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_metadata_flushes_cached_node(void)
{
    for (uint32_t operation = 0u; operation < 9u; ++operation) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'n', 'e', 'w'};
        CacheWriteProbe probe = {.accepted = 3u, .metadata = 1, .flush_page = page};
        AstraVfsClient client;
        AstraVfsFile file, directory;
        AstraVfsFileCache cache;
        uint32_t moved, sequence;

        reset(); fake.exact_identity = 1u;
        assert(fake_create("/dir", ASTRA_VFS_KIND_DIRECTORY) != NULL);
        FakeNode *node = fake_create("/dir/cache", ASTRA_VFS_KIND_FILE);
        assert(node != NULL);
        node->mode = 0600u;
        assert(astra_vfs_service_set_cache_ops(&service, &cache_flush_ops, &probe));
        assert(astra_vfs_service_set_state_lock(&service,
            test_lock_acquire, test_lock_release, &mutex));
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/dir", ASTRA_VFS_OPEN_READ,
            &directory, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/dir/cache", ASTRA_VFS_OPEN_WRITE,
            &file, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_write(&client, file, 0u, UINT64_MAX,
            "new", 3u, &moved) == ASTRA_VFS_OK && moved == 3u && node->size == 0u);
        for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
            uint32_t status;
            probe.flush_status = attempt == 0u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
            switch (operation) {
            case 0u: status = astra_vfs_chmod_file(&client, file, 0644u); break;
            case 1u: status = astra_vfs_chmod(&client, "/dir/cache", 0644u); break;
            case 2u: status = astra_vfs_chmod_at(&client, directory, "cache", 0644u, 0u); break;
            case 3u: status = astra_vfs_unlink(&client, "/dir/cache", 0u); break;
            case 4u: status = astra_vfs_unlink_at(&client, directory, "cache", 0u); break;
            case 5u: status = astra_vfs_rename(&client, "/dir/cache", "/dir/new"); break;
            case 6u: status = astra_vfs_rename_at(&client, directory, "cache", directory, "new"); break;
            case 7u: status = astra_vfs_link(&client, "/dir/cache", "/dir/new"); break;
            default: status = astra_vfs_link_at(&client, directory, "cache", directory, "new"); break;
            }
            if (status != probe.flush_status || probe.flush_calls != attempt + 1u)
                fprintf(stderr, "metadata operation %u attempt %u status %u expected %u flushes %u\n",
                    operation, attempt, status, probe.flush_status, probe.flush_calls);
            assert(status == probe.flush_status && probe.flush_calls == attempt + 1u);
            if (attempt == 0u)
                assert(node->used && node->size == 0u && node->mode == 0600u &&
                    strcmp(node->path, "/dir/cache") == 0);
        }
        assert(node->size == 3u && memcmp(node->bytes, "new", 3u) == 0);
        assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
        assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, directory) == ASTRA_VFS_OK);
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
        assert(pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_rename_flushes_both_cached_nodes(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    uint8_t source_page[ASTRA_MEMORY_PAGE_SIZE] = {'n', 'e', 'w'};
    uint8_t target_page[ASTRA_MEMORY_PAGE_SIZE] = {0u, 'n', 'e', 'w'};
    CacheWriteProbe target_probe = {.accepted = 3u, .metadata = 1, .flush_page = target_page};
    CacheWriteProbe source_probe = {.accepted = 3u, .metadata = 1,
        .flush_page = source_page, .next = &target_probe};
    AstraVfsClient client;
    AstraVfsFile source, target;
    uint32_t moved, sequence;
    AstraVfsFileCache cache;
    reset(); fake.exact_identity = 1u;
    FakeNode *first = fake_create("/source", ASTRA_VFS_KIND_FILE);
    FakeNode *second = fake_create("/target", ASTRA_VFS_KIND_FILE);
    assert(first != NULL && second != NULL);
    assert(astra_vfs_service_set_cache_ops(&service, &cache_flush_ops, &source_probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/source", ASTRA_VFS_OPEN_WRITE, &source, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/target", ASTRA_VFS_OPEN_WRITE, &target, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, source, 0u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, target, 1u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK);
    target_probe.flush_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_rename(&client, "/source", "/target") == ASTRA_VFS_ERR_IO);
    assert(fake_find("/source") == first && fake_find("/target") == second && second->size == 0u);
    target_probe.flush_status = ASTRA_VFS_OK;
    assert(astra_vfs_rename(&client, "/source", "/target") == ASTRA_VFS_OK);
    assert(fake_find("/source") == NULL && fake_find("/target") == first &&
        first->size == 3u && memcmp(first->bytes, source_page, 3u) == 0 &&
        second->size == 4u && memcmp(second->bytes, target_page, 4u) == 0);
    assert(source_probe.flush_calls != 0u && target_probe.flush_calls == 2u);
    assert(astra_vfs_service_cache_snapshot(&service, source_probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, source_probe.owned, sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_snapshot(&service, target_probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, target_probe.owned, sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, source) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&client, target) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_cached_namespace_transaction_orders_open_and_mutation(void)
{
    const uint32_t operations[] = {ASTRA_VFS_OP_OPEN, ASTRA_VFS_OP_CHMOD, ASTRA_VFS_OP_UNLINK};
    for (uint32_t variant = 0u; variant < 3u; ++variant) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_t slow_thread, fast_thread;
        uint8_t page[ASTRA_MEMORY_PAGE_SIZE] = {'n', 'e', 'w'};
        CacheWriteProbe probe = {.accepted = 3u, .metadata = 1, .flush_page = page};
        AstraVfsClient client;
        AstraVfsFile file;
        AstraVfsFileCache cache;
        uint32_t moved, sequence;
        struct timespec deadline;
        reset(); fake.exact_identity = 1u;
        assert(fake_create("/slow", ASTRA_VFS_KIND_FILE) != NULL);
        assert(fake_create("/fast", ASTRA_VFS_KIND_FILE) != NULL);
        assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
        assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
        assert(pthread_cond_init(&file_condition, NULL) == 0);
        memset(&concurrent_gate.enabled, 0,
            sizeof(concurrent_gate) - offsetof(ConcurrentGate, enabled));
        assert(astra_vfs_service_set_cache_ops(&service, &cache_flush_ops, &probe));
        assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
        assert(astra_vfs_service_set_state_wait(&service, test_state_wait, test_state_wake));
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/slow", ASTRA_VFS_OPEN_WRITE, &file, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_write(&client, file, 0u, UINT64_MAX, "new", 3u, &moved) == ASTRA_VFS_OK);
        ConcurrentRequest slow = {.session = client.session, .path = "/slow", .operation = ASTRA_VFS_OP_CHMOD};
        ConcurrentRequest fast = {.session = open_session(), .path = "/fast", .operation = operations[variant],
            .flags = variant == 0u ? ASTRA_VFS_OPEN_WRITE : 0u};
        concurrent_gate.enabled = 1;
        assert(pthread_create(&slow_thread, NULL, run_path_request, &slow) == 0);
        assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
        while (!concurrent_gate.slow_entered)
            assert(pthread_cond_wait(&concurrent_gate.changed, &concurrent_gate.mutex) == 0);
        assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
        assert(pthread_create(&fast_thread, NULL, run_path_request, &fast) == 0);
        assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC); ++deadline.tv_sec;
        assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
        while (!concurrent_gate.fast_finished)
            if (pthread_cond_timedwait(&concurrent_gate.changed, &concurrent_gate.mutex, &deadline) != 0) break;
        int overtook = concurrent_gate.fast_finished;
        concurrent_gate.release_slow = 1;
        assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
        assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
        assert(pthread_join(fast_thread, NULL) == 0 && pthread_join(slow_thread, NULL) == 0);
        assert(!overtook && slow.reply.status == ASTRA_VFS_OK && fast.reply.status == ASTRA_VFS_OK);
        concurrent_gate.enabled = 0;
        assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
        assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
        astra_vfs_service_release_session(&service, fast.session);
        assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
        assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
        assert(pthread_cond_destroy(&file_condition) == 0);
        assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0 && pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_cache_sparse_metadata_and_failed_writeback(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CacheWriteProbe probe = {.accepted = 1u, .status = ASTRA_VFS_ERR_IO, .metadata = 1};
    AstraVfsClient client;
    AstraVfsFile handle;
    AstraVfsFileCache cache;
    AstraVfsDirEntry info;
    uint32_t moved, sequence;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];

    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/sparse-meta", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    assert(astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/sparse-meta", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
        &handle, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, handle, 2u * sizeof(page), UINT64_MAX,
        "new", 3u, &moved) == ASTRA_VFS_ERR_IO && moved == 1u && node->size == 3u);
    assert(astra_vfs_stat_file_meta(&client, handle, &info) == ASTRA_VFS_OK &&
        info.size == 2u * sizeof(page) + 1u);
    memset(page, 0xa5, sizeof(page));
    assert(astra_vfs_service_read_page_retained(&service, probe.owned, ASTRA_VFS_OPEN_READ,
        &info.meta, sizeof(page), page, &moved) == ASTRA_VFS_OK && moved == sizeof(page));
    for (uint32_t at = 0u; at < sizeof(page); ++at) assert(page[at] == 0u);
    assert(astra_vfs_service_read_page_retained(&service, probe.owned, ASTRA_VFS_OPEN_READ,
        &info.meta, 3u * sizeof(page), page, &moved) == ASTRA_VFS_ERR_INVALID && moved == 0u);
    node->size = 4u;
    assert(astra_vfs_service_read_page_retained(&service, probe.owned, ASTRA_VFS_OPEN_READ,
        &info.meta, sizeof(page), page, &moved) == ASTRA_VFS_ERR_BUSY && moved == 0u);
    assert(astra_vfs_write(&client, handle, 0u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_ERR_BUSY && moved == 0u);
    node->size = 3u;
    assert(astra_vfs_close(&client, handle) == ASTRA_VFS_OK);
    AstraVfsFile borrowed = probe.owned;
    assert(astra_vfs_service_release_retained(&service, &borrowed) == ASTRA_VFS_ERR_BUSY &&
        borrowed == probe.owned && service.open_files == 1u);
    for (uint32_t chunk = 0u; chunk < 3u; ++chunk) {
        memset(page, 0, sizeof(page));
        if (chunk == 0u) memcpy(page, "old", 3u);
        if (chunk == 2u) page[0] = 'n';
        assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
        fake.sync_status = chunk == 2u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
        AstraVfsNodeInfo current;
        assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL,
            &cache.backing, (uint64_t)chunk * sizeof(page), cache.size,
            page, &moved, &current) == fake.sync_status);
        assert(moved == (chunk == 2u ? 1u : sizeof(page)));
    }
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK &&
        cache.error == ASTRA_VFS_ERR_IO && cache.backing.size == cache.size);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_ERR_IO);
    fake.sync_status = ASTRA_VFS_OK;
    memset(page, 0, sizeof(page)); page[0] = 'n';
    assert(astra_vfs_service_write_page_retained(&service, probe.owned, NULL,
        &cache.backing, 2u * sizeof(page), cache.size, page, &moved, &cache.backing) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_snapshot(&service, probe.owned, &cache, &sequence) == ASTRA_VFS_OK);
    assert(astra_vfs_service_cache_retire(&service, probe.owned, sequence) == ASTRA_VFS_OK);
    assert(service.open_files == 0u && probe.cache_storage.writer == ASTRA_VFS_FILE_INVALID &&
        node->size == 2u * sizeof(page) + 1u);
    assert(memcmp(node->bytes, "old", 3u) == 0 && node->bytes[2u * sizeof(page)] == 'n');
    for (uint32_t at = 3u; at < 2u * sizeof(page); ++at) assert(node->bytes[at] == 0u);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_cache_requires_exact_identity(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    CacheWriteProbe probe = {.accepted = 3u, .metadata = 1};
    AstraVfsClient client;
    AstraVfsFile handle;
    uint32_t moved = 99u;
    reset();
    FakeNode *node = fake_create("/unknown-meta", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    assert(astra_vfs_service_set_cache_ops(&service, &cache_write_ops, &probe));
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/unknown-meta", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
        &handle, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, handle, 17u, UINT64_MAX, "new", 3u, &moved) ==
        ASTRA_VFS_ERR_UNSUPPORTED && moved == 0u && node->size == 0u);
    assert(probe.owned == ASTRA_VFS_FILE_INVALID && service.files[0].retained == 0u);
    assert(astra_vfs_service_cache_publish(&service, &service.files[0], 20u, 0u) == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_close(&client, handle) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_independent_opens_share_page_fence(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsFile reader, writer, retained_reader, retained_writer;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t valid = 0u;

    reset();
    fake.exact_identity = 1u;
    FakeNode *node = fake_create("/node-io", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "old", 3u); node->size = 3u;
    uint32_t session = open_session();
    begin_request(&request, session, "/node-io");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    reader = reply.file;
    request.flags = ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    writer = reply.file;
    assert(astra_vfs_service_retain_file(&service, session, reader,
        ASTRA_VFS_OPEN_READ, &retained_reader) == ASTRA_VFS_OK);
    assert(astra_vfs_service_retain_file(&service, session, writer,
        ASTRA_VFS_OPEN_WRITE, &retained_writer) == ASTRA_VFS_OK);
    page_io_reference = retained_reader;
    fake.io_hook = fill_independent_page_during_mutation;
    assert(astra_vfs_service_write_from(&service, session, writer, 0u,
        UINT64_MAX, "new", 3u, &valid) == ASTRA_VFS_OK && valid == 3u);
    assert(fake.io_hook == NULL && memcmp(node->bytes, "new", 3u) == 0);
    assert(astra_vfs_service_read_page_retained(&service, retained_reader,
        ASTRA_VFS_OPEN_READ, NULL, 0u, page, &valid) == ASTRA_VFS_OK);
    assert(valid == 3u && memcmp(page, "new", 3u) == 0);
    page_io_reference = retained_writer;
    fake.io_hook = sync_during_page_read;
    assert(astra_vfs_service_read_page_retained(&service, retained_reader,
        ASTRA_VFS_OPEN_READ, NULL, 0u, page, &valid) == ASTRA_VFS_ERR_BUSY);
    assert(valid == 0u && fake.io_hook == NULL);
    assert(astra_vfs_service_release_retained(&service, &retained_reader) == ASTRA_VFS_OK);
    assert(astra_vfs_service_release_retained(&service, &retained_writer) == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, session);
    assert(service.open_files == 0u && node->open_count == 0);
}

static void test_open_truncate_joins_node_before_mutation(void)
{
    for (uint32_t variant = 0u; variant < 3u; ++variant) {
        AstraVfsRequest request;
        AstraVfsReply reply;
        reset();
        fake.exact_identity = 1u;
        assert(fake_create("/parent", ASTRA_VFS_KIND_DIRECTORY) != NULL);
        FakeNode *node = fake_create("/parent/cut", ASTRA_VFS_KIND_FILE);
        assert(node != NULL);
        memcpy(node->bytes, "old", 3u); node->size = 3u;
        uint32_t session = open_session();
        begin_request(&request, session, "/parent/cut"); request.flags = ASTRA_VFS_OPEN_READ;
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        assert(astra_vfs_service_retain_file(&service, session, reply.file,
            ASTRA_VFS_OPEN_READ, &page_io_reference) == ASTRA_VFS_OK);
        AstraVfsFile directory = 0u;
        if (variant == 1u) {
            begin_request(&request, session, "/parent");
            request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY;
            fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
            assert(reply.status == ASTRA_VFS_OK);
            directory = reply.file;
        }
        begin_request(&request, session, variant == 1u ? "cut" : "/parent/cut");
        request.file = directory; request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_TRUNCATE;
        uint32_t operation = variant == 1u ? ASTRA_VFS_OP_OPEN_AT : ASTRA_VFS_OP_OPEN;
        uint32_t opened = service.open_files;
        fixture_dispatch(&service, operation, &request, &reply);
        assert(reply.status == ASTRA_VFS_ERR_ACCESS && node->size == 3u &&
            fake.truncate_calls == 0u && service.open_files == opened);
        request.flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_TRUNCATE;
        fake.io_hook = variant == 2u ? fill_independent_page_then_fail_stat :
                                      fill_independent_page_during_mutation;
        fixture_dispatch(&service, operation, &request, &reply);
        assert(reply.status == (variant == 2u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK));
        assert(node->size == 0u && fake.truncate_calls == 1u && fake.io_hook == NULL &&
            service.changes.nodes == 1u);
        assert(service.open_files == opened + (variant == 2u ? 0u : 1u));
        if (variant != 2u) assert(reply.node_size == 0u);
        assert(astra_vfs_service_release_retained(&service, &page_io_reference) == ASTRA_VFS_OK);
        astra_vfs_service_release_session(&service, session);
        assert(service.open_files == 0u && node->open_count == 0);
    }
}

static void test_retained_page_read_owns_lifetime(void)
{
    for (uint32_t failed_close = 0u; failed_close < 2u; ++failed_close) {
        AstraVfsRequest request;
        AstraVfsReply reply;
        uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
        uint32_t valid = 0u;

        reset();
        FakeNode *node = fake_create("/page-io", ASTRA_VFS_KIND_FILE);
        assert(node != NULL);
        memcpy(node->bytes, "old", 3u); node->size = 3u;
        page_io_session = open_session();
        begin_request(&request, page_io_session, "/page-io");
        request.flags = ASTRA_VFS_OPEN_READ;
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        assert(astra_vfs_service_retain_file(&service, page_io_session, reply.file,
            ASTRA_VFS_OPEN_READ, &page_io_reference) == ASTRA_VFS_OK);
        fake.io_hook = close_and_release_during_page_read;
        fake.close_status = failed_close ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
        memset(page, 0xa5, sizeof(page));
        assert(astra_vfs_service_read_page_retained(&service, page_io_reference, ASTRA_VFS_OPEN_READ,
            NULL, 0u, page, &valid) == fake.close_status);
        assert(valid == (failed_close ? 0u : 3u) && fake.io_hook == NULL &&
            memcmp(page, "old", 3u) == 0 && page[3] == 0xa5u &&
            fake.closes == 1u && node->open_count == 0 && service.open_files == 0u);
    }
}

static void
test_retained_page_read_checks_complete_file(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsNodeInfo info;
    AstraVfsNodeMeta stale;
    AstraVfsFile retained;
    FakeNode *node;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t session, valid;

    reset();
    node = fake_create("/page", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    node->size = 300u;
    for (uint32_t index = 0u; index < node->size; ++index)
        node->bytes[index] = (uint8_t)index;
    session = open_session();
    begin_request(&request, session, "/page");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_retain_file(&service, session, reply.file, ASTRA_VFS_OPEN_READ,
                                         &retained) == ASTRA_VFS_OK);
    begin_request(&request, session, NULL);
    request.file = reply.file;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    fake.exact_identity = 1u;
    assert(astra_vfs_service_stat_retained(&service, retained, &info) ==
           ASTRA_VFS_OK);
    fake.read_limit = 37u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_OK);
    assert(valid == 300u && memcmp(page, node->bytes, valid) == 0);
    page_io_reference = retained;
    fake.io_hook = sync_during_page_read;
    assert(astra_vfs_service_read_page_retained(&service, retained, ASTRA_VFS_OPEN_READ, &info.meta,
        0u, page, &valid) == ASTRA_VFS_ERR_BUSY && valid == 0u);
    assert(!astra_vfs_open_file_active(&service.files[0]) && service.files[0].retained == 1u);
    page_io_reference = ASTRA_VFS_FILE_INVALID;
    assert(astra_vfs_backend_read_page(&service.backend, (uintptr_t)node, ASTRA_VFS_OPEN_READ,
               &info.meta, NULL, 0u, page, &valid) == ASTRA_VFS_OK);
    assert(valid == 300u && memcmp(page, node->bytes, valid) == 0);
    assert(astra_vfs_backend_read_page(&service.backend, 0u, ASTRA_VFS_OPEN_READ,
               &info.meta, NULL, 0u, page, &valid) ==
           ASTRA_VFS_ERR_INVALID && valid == 0u);

    stale = info.meta;
    ++stale.generation;
    assert(astra_vfs_backend_read_page(&service.backend, (uintptr_t)node, ASTRA_VFS_OPEN_READ,
               &stale, NULL, 0u, page, &valid) ==
           ASTRA_VFS_ERR_LOST && valid == 0u);
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &stale, 0u, page, &valid) ==
           ASTRA_VFS_ERR_LOST && valid == 0u);
    stale.valid = ASTRA_VFS_META_INODE;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &stale, 0u, page, &valid) ==
           ASTRA_VFS_ERR_INVALID && valid == 0u);
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, NULL, 1u, page, &valid) ==
           ASTRA_VFS_ERR_INVALID && valid == 0u);
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, NULL, ASTRA_MEMORY_PAGE_SIZE,
               page, &valid) == ASTRA_VFS_ERR_INVALID && valid == 0u);

    fake.read_limit = 0u;
    fake.read_mode = 1u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_IO && valid == 0u);
    fake.read_mode = 2u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_IO && valid == 0u);
    fake.read_mode = 0u;
    fake.read_resize_after = 250u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_resize_after = 0u;
    node->size = 300u;
    fake.read_note_change = 1u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_note_change = 0u;
    fake.read_note_provider_change = 1u;
    assert(astra_vfs_backend_read_page(&service.backend, (uintptr_t)node, ASTRA_VFS_OPEN_READ,
               &info.meta, NULL, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_note_provider_change = 0u;
    fake.read_bump_timestamps = 1u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_bump_timestamps = 0u;
    fake.read_bump_ctime = 1u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_bump_ctime = 0u;
    fake.read_drop_time_valid = 1u;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BUSY && valid == 0u);
    fake.read_drop_time_valid = 0u;
    fake.provider_status = ASTRA_VFS_ERR_LOST;
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, &info.meta, 0u, page, &valid) ==
           ASTRA_VFS_ERR_LOST && valid == 0u);
    fake.provider_status = ASTRA_VFS_OK;
    assert(astra_vfs_service_release_retained(&service, &retained) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_service_read_page_retained(
               &service, retained, ASTRA_VFS_OPEN_READ, NULL, 0u, page, &valid) ==
           ASTRA_VFS_ERR_BAD_HANDLE && valid == 0u);
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
            fixture_dispatch(&service, ASTRA_VFS_OP_WRITE,
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
            fixture_dispatch(&service, ASTRA_VFS_OP_READ,
                                       &request, &reply);
            assert(reply.status == ASTRA_VFS_OK && reply.count == expected);
            assert(memcmp(reply.payload, &worker->bytes[offset], expected) == 0);
            break;
        }
        case 2u: {
            uint32_t size = (random >> 8) % (MODEL_BYTES + 1u);

            request.offset = size;
            fixture_dispatch(&service, ASTRA_VFS_OP_TRUNCATE,
                                       &request, &reply);
            assert(reply.status == ASTRA_VFS_OK);
            if (size > worker->size)
                memset(&worker->bytes[worker->size], 0, size - worker->size);
            worker->size = size;
            break;
        }
        default:
            if ((random & 4u) != 0u) {
                fixture_dispatch(&service, ASTRA_VFS_OP_SYNC,
                                           &request, &reply);
                assert(reply.status == ASTRA_VFS_OK);
            } else {
                begin_request(&request, worker->session, worker->path);
                fixture_dispatch(&service, ASTRA_VFS_OP_STAT,
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
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN,
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
        fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE,
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
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    session = open_session();
    assert(session != ASTRA_VFS_SESSION_INVALID);

    /* Every in-tree producer and consumer speaks the same current format. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION + 5u);
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
    assert(reply.version == ASTRA_VFS_VERSION);

    /* Below the floor is refused outright rather than downgraded. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION_MIN - 1u);
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
}

/*
 * A session is bound to the grant it said HELLO on: the same session id on
 * another grant is refused (docs/FILESYSTEM_PRODUCTION_PRD.md FS15).
 */
static void
test_sessions_are_bound_to_their_grant(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    reset();
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch_grant(&service, 7u, 1u,
                                     ASTRA_VFS_FEATURE_GRANTS,
                                     ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.features == ASTRA_VFS_FEATURE_GRANTS);
    session = reply.session;
    assert(astra_vfs_service_session_bound(&service, session, 7u, 1u));
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 2u));
    assert(!astra_vfs_service_session_bound(&service, session, 8u, 1u));
    assert(astra_vfs_service_session_grant(&service, session) == 1u);
    assert(astra_vfs_service_session_grant(&service, session + 1u) == 0u);

    /* Every other operation answers no features. */
    begin_request(&request, session, "/owned.txt");
    fixture_dispatch_grant(&service, 7u, 1u,
                                     ASTRA_VFS_FEATURE_GRANTS,
                                     ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.features == 0u);

    begin_request(&request, session, NULL);
    fixture_dispatch_grant(&service, 7u, 2u, 0u, ASTRA_VFS_OP_BYE,
                                     &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_session_bound(&service, session, 7u, 1u));

    /* The core answers GRANT_DERIVE UNSUPPORTED: only a port carries the
     * new grant's handle. */
    begin_request(&request, session, "");
    request.flags = ASTRA_VFS_GRANT_READ;
    fixture_dispatch_grant(&service, 7u, 1u, 0u,
                                     ASTRA_VFS_OP_GRANT_DERIVE, &request,
                                     &reply);
    assert(reply.status == ASTRA_VFS_ERR_UNSUPPORTED);

    astra_vfs_service_release_grant(&service, 1u);
    assert(!astra_vfs_service_session_bound(&service, session, 7u, 1u));
    assert(astra_vfs_service_session_grant(&service, session) == 0u);
}

static void
test_transport_owner_isolation_and_resource_exhaustion(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    const uint32_t owner = 0x10000001u;
    uint32_t owner_sessions[ASTRA_VFS_SESSION_MAX];
    uint32_t session;

    reset();
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    session = reply.session;
    owner_sessions[0] = session;
    assert(astra_vfs_service_session_bound(&service, session, owner, 0u));
    assert(!astra_vfs_service_session_bound(&service, session, owner + 1u, 0u));

    for (uint32_t index = 1u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
        fixture_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                        &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
        owner_sessions[index] = reply.session;
    }
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch_from(&service, owner, ASTRA_VFS_OP_HELLO,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(service.stats.owner_session_quota_denied == 0u);
    begin_request(&request, session, "/owned.txt");
    fixture_dispatch_from(&service, owner + 1u, ASTRA_VFS_OP_STAT,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);

    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    for (uint32_t index = 0u;
         index < (uint32_t)(sizeof(service_files) /
                            sizeof(service_files[0]));
         ++index) {
        fixture_dispatch_from(&service, owner, ASTRA_VFS_OP_OPEN,
                                        &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    fixture_dispatch_from(&service, owner, ASTRA_VFS_OP_OPEN,
                                    &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    assert(astra_vfs_service_stats(&service)->owner_quota_denied == 0u);
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;
    assert(file != ASTRA_VFS_FILE_INVALID);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = (uint32_t)(sizeof(payload) - 1u);
    memcpy(request.body.payload, payload, sizeof(payload) - 1u);
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 64u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);
    assert(memcmp(reply.payload, payload, sizeof(payload) - 1u) == 0);

    begin_request(&request, session, "/hello.txt");
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.node_size == sizeof(payload) - 1u);
    assert(reply.kind == ASTRA_VFS_KIND_FILE);

    begin_request(&request, session, NULL);
    request.file = file;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    begin_request(&request, session, "/hello.txt");
    request.flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                    ASTRA_VFS_OPEN_EXCLUSIVE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_EXISTS);

    begin_request(&request, session, "/exclusive.txt");
    request.flags = ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                    ASTRA_VFS_OPEN_EXCLUSIVE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    begin_request(&request, session, NULL);
    request.file = reply.file;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
}

static void
test_mutation_generations(void)
{
    AstraVfsChangeSnapshot changes;
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t moved = 0u;
    AstraVfsFile file;

    reset();
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 0u && changes.entries == 0u);
    session = open_session();

    begin_request(&request, session, "/changed.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                    ASTRA_VFS_OPEN_CREATE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 0u && changes.entries == 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 1u;
    request.body.payload[0] = 'x';
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.count == 1u);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 1u && changes.entries == 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 1u && changes.entries == 1u);

    assert(astra_vfs_service_write_from(&service, session, file, 1u,
        UINT64_MAX, "y", 1u, &moved) == ASTRA_VFS_OK && moved == 1u);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 2u && changes.entries == 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.offset = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_TRUNCATE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 3u && changes.entries == 1u);

    begin_request(&request, session, "/newdir");
    fixture_dispatch(&service, ASTRA_VFS_OP_MKDIR, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    fixture_dispatch(&service, ASTRA_VFS_OP_MKDIR, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_EXISTS);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 3u && changes.entries == 2u);

    fixture_dispatch(&service, ASTRA_VFS_OP_UNLINK, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IS_DIR);
    request.flags = ASTRA_VFS_UNLINK_DIRECTORY;
    fixture_dispatch(&service, ASTRA_VFS_OP_UNLINK, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_change_snapshot(&service, &changes));
    assert(changes.nodes == 3u && changes.entries == 3u);
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    /* Another session may not touch it, even holding the exact handle. */
    begin_request(&request, second, NULL);
    request.file = file;
    request.length = 8u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_stats(&service)->cross_session_denied == 1u);

    /* Closed, then reused: the old handle must not reach the new file. */
    begin_request(&request, first, NULL);
    request.file = file;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    begin_request(&request, first, "/other.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    reused = reply.file;
    assert(reused != file); /* the generation moved */

    begin_request(&request, first, NULL);
    request.file = file;
    request.length = 8u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_vfs_service_stats(&service)->stale_handles == 1u);

    /* A write to a read-only handle is refused by the core, not the backend. */
    begin_request(&request, first, NULL);
    request.file = reused;
    request.length = 4u;
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
}

static void test_handle_generation_exhaustion(void)
{
    for (uint32_t retain = 0u; retain < 2u; ++retain) {
        AstraVfsClient client;
        AstraVfsFile old, last, companion, replacement, overflow;
        char byte = 0;
        uint32_t moved = 0u;

        reset();
        fake.exact_identity = 1u;
        service.file_capacity = 2u;
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/generation", ASTRA_VFS_OPEN_READ |
            ASTRA_VFS_OPEN_CREATE, &old, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, old) == ASTRA_VFS_OK);
        service.files[0].generation = UINT16_MAX;
        assert(astra_vfs_open(&client, "/generation", ASTRA_VFS_OPEN_READ,
            &last, NULL, NULL) == ASTRA_VFS_OK);
        assert((last >> 16) == UINT16_MAX);
        if (retain != 0u)
            assert(astra_vfs_service_retain_file(&service, client.session,
                last, ASTRA_VFS_OPEN_READ, &last) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/generation", ASTRA_VFS_OPEN_READ,
            &companion, NULL, NULL) == ASTRA_VFS_OK);
        assert(service.files[0].io == service.files[1].io);
        assert(astra_vfs_close(&client, last) == ASTRA_VFS_OK);
        if (retain != 0u)
            assert(astra_vfs_service_release_retained(&service, &last) == ASTRA_VFS_OK);
        assert(astra_vfs_read(&client, companion, 0u, &byte, 1u, &moved) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, companion) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/generation", ASTRA_VFS_OPEN_READ,
            &replacement, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_read(&client, old, 0u, &byte, 1u, &moved) == ASTRA_VFS_ERR_BAD_HANDLE);
        assert(service.files[0].generation == 0u && service.files[0].session == 0u);
        assert(astra_vfs_read(&client, replacement, 0u, &byte, 1u, &moved) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/generation", ASTRA_VFS_OPEN_READ,
            &overflow, NULL, NULL) == ASTRA_VFS_ERR_LIMIT);
        assert(astra_vfs_close(&client, replacement) == ASTRA_VFS_OK);
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
        assert(service.open_files == 0u && service.open_sessions == 0u);
        assert(service.files[0].io_storage.references == 0u);
        assert(fake_find("/generation")->open_count == 0);
    }
}

static void test_session_id_exhaustion(void)
{
    AstraVfsClient first, last, overflow;
    AstraVfsDirEntry info;
    AstraVfsFile file;

    reset();
    assert(astra_vfs_connect(&first, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&first, "/session", ASTRA_VFS_OPEN_READ |
        ASTRA_VFS_OPEN_CREATE, &file, NULL, NULL) == ASTRA_VFS_OK);
    service.next_session = UINT32_MAX;
    assert(astra_vfs_connect(&last, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(last.session == UINT32_MAX);
    assert(astra_vfs_disconnect(&last) == ASTRA_VFS_OK);
    assert(astra_vfs_connect(&overflow, astra_vfs_local_transport, &service) == ASTRA_VFS_ERR_LIMIT);
    assert(service.next_session == 0u && service.open_sessions == 1u);
    assert(astra_vfs_service_session_capacity_reached(&service));
    AstraVfsRequest request;
    AstraVfsReply reply;
    begin_request(&request, first.session, NULL);
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.session == first.session);
    assert(astra_vfs_stat_file_meta(&first, file, &info) == ASTRA_VFS_OK);
    assert(astra_vfs_close(&first, file) == ASTRA_VFS_OK);
    assert(astra_vfs_disconnect(&first) == ASTRA_VFS_OK);
    assert(astra_vfs_connect(&overflow, astra_vfs_local_transport, &service) == ASTRA_VFS_ERR_LIMIT);
    assert(service.open_sessions == 0u && service.open_files == 0u);
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
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request,
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
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    /* Path records are UTF-8 text, never arbitrary non-NUL bytes. */
    begin_request(&request, session, "/a");
    fixture_paths[1] = UINT8_C(0xc0);
    fixture_paths[2] = UINT8_C(0x80);
    fixture_paths[3] = 0u;
    request.body.paths.first_length = 4u;
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
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
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status != ASTRA_VFS_ERR_PROTOCOL);

    /* An unterminated path is a malformed record, not a long path. */
    begin_request(&request, session, NULL);
    for (index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        fixture_paths[index] = (uint8_t)'x';
    }
    fixture_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    fixture_dispatch(&service, ASTRA_VFS_OP_MAX + 1u, &request,
                               &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    fixture_dispatch(&service, 0u, &request, &reply);
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
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX;
    for (index = 0u; index < ASTRA_VFS_IO_MAX; ++index) {
        request.body.payload[index] = (uint8_t)(index + 1u); /* never zero */
    }
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == ASTRA_VFS_IO_MAX);

    /* One byte past what a message carries is refused, not truncated. */
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX + 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID);

    fake.write_overreport = 1u;
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 1u;
    request.body.payload[0] = 0x5au;
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && reply.count == 0u);
    assert(fake_find("/bin")->bytes[0] == 0x5au);
    assert(service.changes.nodes == 2u);
    fake.write_overreport = 0u;
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
    fixture_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
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
        fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request,
                                   &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    begin_request(&request, session, "/many");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
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
static uint8_t lookup_payload[ASTRA_VFS_ESCAPE_HEADER + ASTRA_VFS_PATH_MAX];
static void lookup_encode(const AstraVfsEscape *escape)
{
    astra_store_be16(lookup_payload, escape->kind);
    astra_store_be16(lookup_payload + 2u, escape->length);
    astra_store_be32(lookup_payload + 4u, escape->consumed);
    astra_store_be32(lookup_payload + 8u, escape->links);
    if (escape->path != NULL)
        memcpy(lookup_payload + ASTRA_VFS_ESCAPE_HEADER, escape->path,
            escape->length > ASTRA_VFS_ESCAPE_PATH_MAX ? ASTRA_VFS_ESCAPE_PATH_MAX : escape->length);
}
static uint32_t lookup_reply(void *context, uint32_t operation,
                             const AstraVfsRequest *request,
                             const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    assert(operation == ASTRA_VFS_OP_LOOKUP && request->length == 2u);
    *reply = *(const AstraVfsReply *)context;
    memcpy(transfer->output, lookup_payload, transfer->output_capacity);
    return ASTRA_VFS_OK;
}

typedef struct ByteReplyProbe {
    AstraVfsReply reply;
    uint32_t transport_status;
    int untouched;
} ByteReplyProbe;

static uint32_t byte_reply(void *context, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    ByteReplyProbe *probe = context;
    (void)request; (void)transfer;
    assert(operation == ASTRA_VFS_OP_READ || operation == ASTRA_VFS_OP_WRITE);
    if (!probe->untouched) *reply = probe->reply;
    return probe->transport_status;
}

static void test_byte_reply_preserves_only_validated_prefixes(void)
{
    for (uint32_t write = 0u; write < 2u; ++write)
        for (uint32_t fault = 0u; fault < 8u; ++fault) {
            ByteReplyProbe probe = {.reply = {.size = ASTRA_VFS_REPLY_SIZE,
                .version = ASTRA_VFS_VERSION, .count = 1u, .position = 18u}};
            AstraVfsClient client = {.transport = byte_reply, .context = &probe, .session = 1u};
            uint8_t bytes[3] = {0xa5, 0xa5, 0xa5};
            uint32_t moved = 0u;
            uint64_t position = 0u;
            probe.reply.payload[0] = 'N';
            /* Prime the reusable reply before testing a failed/no-output call. */
            uint32_t status = write ?
                astra_vfs_write_position(&client, 1u, 17u, ASTRA_VFS_IO_SEQUENTIAL,
                    UINT64_MAX, bytes, sizeof(bytes), &moved, &position) :
                astra_vfs_read_position(&client, 1u, 17u, ASTRA_VFS_IO_SEQUENTIAL,
                    bytes, sizeof(bytes), &moved, &position);
            assert(status == ASTRA_VFS_OK && moved == 1u && position == 18u);
            memset(bytes, 0xa5, sizeof(bytes));
            probe.reply.status = ASTRA_VFS_ERR_IO;
            uint32_t expected = ASTRA_VFS_ERR_PROTOCOL;
            switch (fault) {
            case 0u: expected = ASTRA_VFS_ERR_IO; break;
            case 1u: probe.reply.count = 0u; expected = ASTRA_VFS_ERR_IO; break;
            case 2u: probe.transport_status = ASTRA_VFS_ERR_IO; expected = ASTRA_VFS_ERR_IO; break;
            case 3u: probe.transport_status = ASTRA_VFS_ERR_LOST; probe.untouched = 1;
                     expected = ASTRA_VFS_ERR_LOST; break;
            case 4u: --probe.reply.size; break;
            case 5u: probe.reply.count = sizeof(bytes) + 1u; break;
            case 6u: probe.reply.reserved = 1u; break;
            default: probe.reply.position = 0u; break;
            }
            status = write ?
                astra_vfs_write_position(&client, 1u, 17u, ASTRA_VFS_IO_SEQUENTIAL,
                    UINT64_MAX, bytes, sizeof(bytes), &moved, &position) :
                astra_vfs_read_position(&client, 1u, 17u, ASTRA_VFS_IO_SEQUENTIAL,
                    bytes, sizeof(bytes), &moved, &position);
            assert(status == expected && moved == (fault == 0u ? 1u : 0u));
            assert(position == (fault == 0u ? 18u : 17u));
            assert(bytes[0] == (!write && fault == 0u ? 'N' : 0xa5));
            assert(bytes[1] == 0xa5 && bytes[2] == 0xa5);
        }
}

static void test_lookup_reply_validation(void)
{
    AstraVfsReply scripted = {0}, valid;
    AstraVfsClient client = { .transport = lookup_reply, .context = &scripted,
                              .session = 1u };
    AstraVfsDirEntry meta;
    uint8_t escape_bytes[ASTRA_VFS_PATH_MAX], payload_bytes[ASTRA_VFS_PATH_MAX];
    AstraVfsEscape escape = {.path = escape_bytes, .capacity = ASTRA_VFS_ESCAPE_PATH_MAX};
    AstraVfsEscape payload = { .kind = ASTRA_VFS_ESCAPE_SYMLINK, .length = 6u,
                               .consumed = 7u, .links = 1u, .path = payload_bytes,
                               .capacity = ASTRA_VFS_ESCAPE_PATH_MAX };

    memcpy(payload.path, "/dh0/x", 6u);
    scripted.size = ASTRA_VFS_REPLY_SIZE;
    scripted.version = ASTRA_VFS_VERSION;
    scripted.status = ASTRA_VFS_ERR_ESCAPED;
    scripted.count = ASTRA_VFS_ESCAPE_HEADER + payload.length;
    lookup_encode(&payload);
    valid = scripted;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_ESCAPED);
    assert(escape.links == 1u && memcmp(escape.path, "/dh0/x", 6u) == 0);
    payload.length = ASTRA_VFS_PATH_MAX - 1u;
    memset(payload.path, 'x', payload.length);
    payload.path[0] = '/';
    for (uint32_t at = 256u; at < payload.length; at += 256u)
        payload.path[at] = '/';
    scripted.count = ASTRA_VFS_ESCAPE_HEADER + payload.length;
    lookup_encode(&payload);
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_ESCAPED);
    assert(escape.length == ASTRA_VFS_PATH_MAX - 1u);
    payload.length = 6u;
    memcpy(payload.path, "/dh0/x", 6u);
    for (uint32_t fault = 0u; fault < 16u; ++fault) {
        uint8_t bad_bytes[ASTRA_VFS_PATH_MAX];
        AstraVfsEscape bad = payload;
        memcpy(bad_bytes, payload.path, payload.length);
        bad.path = bad_bytes;

        scripted = valid;
        switch (fault) {
        case 0u: scripted.count = ASTRA_VFS_ESCAPE_HEADER - 1u; break;
        case 1u: scripted.count = sizeof(lookup_payload) + 1u; break;
        case 2u: bad.length = ASTRA_VFS_PATH_MAX; break;
        case 3u: --scripted.count; break;
        case 4u: bad.kind = 3u; break;
        case 5u: bad.links = 3u; break;
        case 6u: bad.consumed = 0u; break;
        case 7u: bad.consumed = 8u; break;
        case 8u: bad.path[0] = 'x'; break;
        case 9u: bad.path[2] = '\0'; break;
        case 10u: scripted.meta.reserved = 1u; break;
        case 11u: scripted.meta.valid = UINT32_C(1) << 31; break;
        case 12u:
            scripted.meta.valid = ASTRA_VFS_META_MTIME;
            scripted.meta.mtime_nsec = 1000000000u;
            break;
        case 13u:
            scripted.meta.valid = ASTRA_VFS_META_ATIME;
            scripted.meta.atime_nsec = 1000000000u;
            break;
        case 14u:
            scripted.meta.valid = ASTRA_VFS_META_CTIME;
            scripted.meta.ctime_nsec = 1000000000u;
            break;
        case 15u: scripted.version = ASTRA_VFS_VERSION - 1u; break;
        }
        lookup_encode(&bad);
        assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                                2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    }
    scripted = valid;
    scripted.status = ASTRA_VFS_OK;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    payload = (AstraVfsEscape){ .links = 1u };
    scripted.count = ASTRA_VFS_ESCAPE_HEADER;
    scripted.kind = ASTRA_VFS_KIND_DIRECTORY;
    scripted.meta = (AstraVfsNodeMeta){ .valid = ASTRA_VFS_META_MODE |
        ASTRA_VFS_META_INODE | ASTRA_VFS_META_VOLUME, .volume = 3u, .inode = 99u };
    lookup_encode(&payload);
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_OK);
    assert(meta.kind == ASTRA_VFS_KIND_DIRECTORY && escape.links == 1u);
    assert(meta.meta.inode == 99u && meta.meta.volume == 3u &&
           (meta.meta.valid & ASTRA_VFS_META_MODE) != 0u && meta.mode == 0u);
    scripted.meta.reserved = 1u;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    scripted.meta.reserved = 0u;
    scripted.version = ASTRA_VFS_VERSION - 1u;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    scripted.version = ASTRA_VFS_VERSION;
    scripted.meta = (AstraVfsNodeMeta){0};
    scripted.kind = ASTRA_VFS_KIND_UNKNOWN;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_MISSING,
                            2u, &meta, &escape) == ASTRA_VFS_OK);
    scripted.kind = ASTRA_VFS_KIND_SYMLINK;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", ASTRA_VFS_LOOKUP_FOLLOW,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
    scripted.kind = ASTRA_VFS_KIND_SYMLINK + 1u;
    assert(astra_vfs_lookup(&client, ASTRA_VFS_FILE_INVALID, "/escape", 0u,
                            2u, &meta, &escape) == ASTRA_VFS_ERR_PROTOCOL);
}

static void
test_client_through_transport(int scoped)
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
    BoundBackend binding = {0};
    BackendBindings bindings = {.selected = &binding};

    reset();
    if (scoped) {
        assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
        service.backend.context = &bindings;
        fake.check_context = 1u;
        fake.expected_context = &binding;
    }
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
    {
        AstraVfsFile directory;
        AstraVfsFile relative;
        AstraVfsDirEntry metadata;
        AstraVfsFilesystemInfo info;

        assert(astra_vfs_open(&client, "/dir",
                              ASTRA_VFS_OPEN_READ |
                                  ASTRA_VFS_OPEN_DIRECTORY,
                              &directory, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_open_at_mode(
                   &client, directory, "relative.txt",
                   ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                       ASTRA_VFS_OPEN_CREATE,
                   0600u, &relative, NULL, NULL) == ASTRA_VFS_OK);
        assert(fake_find("/dir/relative.txt") != NULL);
        assert(astra_vfs_chmod_file(&client, relative, 0640u) ==
               ASTRA_VFS_OK);
        assert(fake_find("/dir/relative.txt")->mode == 0640u);
        assert(astra_vfs_chmod_at(&client, directory, "relative.txt", 0620u,
                                  0u) == ASTRA_VFS_OK);
        assert(fake_find("/dir/relative.txt")->mode == 0620u);
        assert(astra_vfs_stat_file_meta(&client, relative, &metadata) ==
               ASTRA_VFS_OK);
        assert(metadata.mode == 0620u && metadata.mtime == 1234567 &&
               metadata.uid == 17u && metadata.nlink == 3u);
        assert(astra_vfs_stat_at_meta(&client, directory, "relative.txt", 0u,
                                      &metadata) == ASTRA_VFS_OK);
        assert(metadata.mode == 0620u && metadata.mtime == 1234567 &&
               metadata.uid == 17u && metadata.gid == 23u &&
               metadata.nlink == 3u);
        assert(metadata.meta.volume == 3u && metadata.meta.inode != 0u &&
               (metadata.meta.valid & ASTRA_VFS_META_MODE) != 0u);
        assert(astra_vfs_stat_meta(&client, "/dir/relative.txt", &metadata) ==
               ASTRA_VFS_OK);
        assert(metadata.mode == 0620u && metadata.mtime == 1234567 &&
               metadata.uid == 17u && metadata.gid == 23u &&
               metadata.nlink == 3u);
        assert(astra_vfs_filesystem_info_file(&client, relative, &info) ==
               ASTRA_VFS_OK);
        assert(info.block_size == 4096u && info.blocks == 4096u);
        assert(astra_vfs_filesystem_info_path(&client,
                                              "/dir/relative.txt", &info) ==
               ASTRA_VFS_OK);
        assert(info.files_free == 480u && info.name_max == ASTRA_VFS_NAME_MAX - 1u);
        assert(astra_vfs_close(&client, relative) == ASTRA_VFS_OK);
        assert(astra_vfs_stat_file_meta(&client, relative, &metadata) ==
               ASTRA_VFS_ERR_BAD_HANDLE);
        assert(astra_vfs_unlink_at(&client, directory, "relative.txt", 0u) ==
               ASTRA_VFS_OK);
        assert(fake_find("/dir/relative.txt") == NULL);
        uint32_t path_length;
        char held_path[32];
        assert(astra_vfs_mkdir_at(&client, directory, "nested", 0711u) == ASTRA_VFS_OK);
        assert(fake_find("/dir/nested")->mode == 0711u);
        assert(astra_vfs_rename_at(&client, directory, "nested", directory, "moved") == ASTRA_VFS_OK);
        assert(fake_find("/dir/moved") != NULL && fake_find("/dir/nested") == NULL);
        assert(astra_vfs_directory_path(&client, directory, held_path, sizeof(held_path), &path_length) == ASTRA_VFS_OK);
        assert(path_length == 4u && memcmp(held_path, "/dir", 4u) == 0);
        assert(astra_vfs_directory_path(&client, directory, held_path, 2u, &path_length) == ASTRA_VFS_ERR_BUFFER_TOO_SMALL);
        assert(astra_vfs_mkdir_at(&client, directory, "/outside", 0700u) == ASTRA_VFS_ERR_INVALID);
        assert(astra_vfs_rename_at(&client, directory, "moved", directory, "/outside") == ASTRA_VFS_ERR_INVALID);
        assert(astra_vfs_unlink_at(&client, directory, "moved", ASTRA_VFS_UNLINK_DIRECTORY) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, directory) == ASTRA_VFS_OK);
        assert(astra_vfs_directory_path(&client, directory, held_path, sizeof(held_path), &path_length) == ASTRA_VFS_ERR_BAD_HANDLE);
    }
    call_acquires = 0u;
    assert(astra_vfs_open_mode(&client, "/dir/note.txt",
                               ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                                   ASTRA_VFS_OPEN_CREATE,
                               0600u, &file, &size, &kind) == ASTRA_VFS_OK);
    assert(call_acquires == 1u);
    assert(fake_find("/dir/note.txt")->mode == 0600u);
    assert(astra_vfs_write(&client, file, 0u, UINT64_MAX, text, sizeof(text) - 1u,
                           &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(text) - 1u);
    assert(astra_vfs_sync(&client, file) == ASTRA_VFS_OK);
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
        link->used = 0;
    }

    assert(astra_vfs_open(&client, "/dir/note.txt",
                          ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                          &file, &size, &kind) == ASTRA_VFS_OK);
    {
        uint64_t position = 0u;
        static const char tail[] = "++";

        assert(astra_vfs_write_position(&client, file, 0u,
                                        ASTRA_VFS_WRITE_APPEND, 6u, tail,
                                        sizeof(tail) - 1u, &moved,
                                        &position) == ASTRA_VFS_OK);
        assert(moved == sizeof(tail) - 1u && position == 6u);
        assert(astra_vfs_write_position(&client, file, 0u,
                  ASTRA_VFS_WRITE_APPEND, 6u, tail, 1u, &moved,
                  &position) == ASTRA_VFS_ERR_FILE_TOO_LARGE && moved == 0u);
        assert(astra_vfs_write_position(&client, file, 0u, 0u, UINT64_MAX, tail,
                                        sizeof(tail) - 1u, &moved,
                                        &position) == ASTRA_VFS_OK);
        assert(moved == sizeof(tail) - 1u && position == 2u);
    }
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);

    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_OK);
    assert(size == 6u);
    assert(kind == ASTRA_VFS_KIND_FILE);
    assert(fake_create("/dir/occupied.txt", ASTRA_VFS_KIND_FILE) != NULL);
    assert(astra_vfs_rename_noreplace(&client, "/dir/note.txt",
        "/dir/occupied.txt") == ASTRA_VFS_ERR_EXISTS);
    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
        ASTRA_VFS_OK);
    assert(astra_vfs_stat(&client, "/dir/occupied.txt", &size, &kind) ==
        ASTRA_VFS_OK);
    fake_find("/dir/occupied.txt")->used = 0;
    {
        uint32_t requests = service.stats.requests;

        call_acquires = 0u;
        assert(astra_vfs_rename(&client, "/dir/note.txt",
                                "/dir/renamed.txt") == ASTRA_VFS_OK);
        assert(call_acquires == 1u);
        assert(service.stats.requests == requests + 1u);
    }
    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_vfs_stat(&client, "/dir/renamed.txt", &size, &kind) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_link(&client, "/dir/renamed.txt", "/dir/alias.txt") ==
           ASTRA_VFS_OK);
    assert(strcmp(linked_from, "/dir/renamed.txt") == 0);
    assert(strcmp(linked_to, "/dir/alias.txt") == 0);
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
    }

    assert(astra_vfs_unlink(&client, "/dir/renamed.txt",
                            ASTRA_VFS_UNLINK_DIRECTORY) ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(astra_vfs_unlink(&client, "/dir/renamed.txt", 0u) == ASTRA_VFS_OK);
    assert(astra_vfs_mkdir(&client, "/empty") == ASTRA_VFS_OK);
    assert(astra_vfs_unlink(&client, "/empty", 0u) == ASTRA_VFS_ERR_IS_DIR);
    assert(astra_vfs_unlink(&client, "/empty", UINT32_MAX) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_unlink(&client, "/empty", ASTRA_VFS_UNLINK_DIRECTORY) ==
           ASTRA_VFS_OK);
    assert(astra_vfs_stat(&client, "/dir/renamed.txt", &size, &kind) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /* A path that cannot fit the record is refused, never truncated: a
     * truncated path names a different file. */
    {
        char huge[ASTRA_VFS_PATH_MAX + 16];

        memset(huge, 'a', sizeof(huge) - 1u);
        huge[sizeof(huge) - 1u] = '\0';
        assert(astra_vfs_stat(&client, huge, &size, &kind) ==
               ASTRA_VFS_ERR_NAME_TOO_LONG);
    }

    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(client.session == ASTRA_VFS_SESSION_INVALID);
    assert(binding.references == 0u);
    if (scoped) assert(fake.backend_calls != 0u);
    fake.check_context = 0u;
}

static void test_owner_sessions_are_limited(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t first;

    reset();
    assert(astra_vfs_service_set_owner_sessions(&service, 2u));
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    fixture_dispatch_grant(&service, 7u, 1u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    first = reply.session;
    fixture_dispatch_grant(&service, 7u, 2u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    fixture_dispatch_grant(&service, 7u, 1u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
    fixture_dispatch_grant(&service, 8u, 1u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && service.stats.owner_session_quota_denied == 1u);
    request.session = first;
    fixture_dispatch_grant(&service, 7u, 1u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.session == first);
    astra_vfs_service_release_session(&service, first);
    request.session = ASTRA_VFS_SESSION_INVALID;
    fixture_dispatch_grant(&service, 7u, 1u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
}

static void test_read_path_failures(void)
{
    uint8_t bytes[8];
    uint32_t moved;
    uint64_t size;
    AstraVfsBackend backend = {&fake_ops, &fake};

    reset();
    FakeNode *node = fake_create("/readpath", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "data", 4u);
    node->size = 4u;
    assert(astra_vfs_backend_read_path(&backend, "/readpath", bytes, sizeof(bytes),
        &moved, &size) == ASTRA_VFS_OK);
    assert(moved == 4u && size == 4u && memcmp(bytes, "data", 4u) == 0);
    for (uint32_t mode = 1u; mode <= 2u; ++mode) {
        fake.read_mode = mode;
        assert(astra_vfs_backend_read_path(&backend, "/readpath", bytes, sizeof(bytes),
            &moved, &size) == ASTRA_VFS_ERR_IO);
        assert(moved == 0u && node->open_count == 0);
    }
    fake.read_mode = 0u;
    fake.close_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_backend_read_path(&backend, "/readpath", bytes, sizeof(bytes),
        &moved, &size) == ASTRA_VFS_ERR_IO);
    assert(node->open_count == 0 && fake.closes == 4u);
}

static void test_page_writeback_preserves_eof_and_reports_partial_failure(void)
{
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    AstraVfsNodeInfo expected = {0}, current;
    uint32_t moved;
    FakeNode *node;

    reset();
    node = fake_create("/write-page", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    node->size = 300u;
    memset(node->bytes, 0xa5, sizeof(node->bytes));
    memset(page, 0x52, sizeof(page));
    fake.exact_identity = 1u;
    assert(fake_stat_node(&fake, (uintptr_t)node, &expected) == ASTRA_VFS_OK);
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 1u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_INVALID);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == 0u);
    AstraVfsNodeInfo stale = expected;
    ++stale.meta.generation;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &stale, 0u, stale.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_LOST);
    stale = expected;
    ++stale.meta.mtime_nsec;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &stale, 0u, stale.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_BUSY);
    assert(fake.write_calls == 0u);
    fake.write_limit = 37u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == 300u && node->size == 300u && current.size == 300u &&
           fake.write_calls == 9u && fake.sync_calls == 1u);
    assert(memcmp(node->bytes, page, 300u) == 0 && node->bytes[300] == 0xa5u);
    fake.write_calls = fake.sync_calls = 0u;
    fake.write_fail_call = 3u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_NO_SPACE);
    assert(moved == 74u && fake.sync_calls == 0u && current.size == 300u);
    fake.write_fail_call = 0u;
    fake.write_zero = 1u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_IO && moved == 0u);
    fake.write_zero = 0u;
    fake.write_overreport = 1u;
    fake.write_limit = 0u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_IO && moved == 0u);
    fake.write_overreport = 0u;
    fake.write_bad_position = 1u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_IO && moved == 0u);
    fake.write_bad_position = 0u;
    fake.sync_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_IO);
    assert(moved == 300u && current.size == 300u);
    fake.sync_status = ASTRA_VFS_OK;
    fake.sync_shrink = 1u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_BUSY);
    fake.sync_shrink = 0u;
    node->size = 300u;
    fake.sync_drop_identity = 1u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_LOST);
    assert(current.kind == 0u);
    fake.sync_drop_identity = 0u;
    fake.exact_identity = 1u;
    fake.stat_fail_call = fake.stat_calls + 2u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_IO);
    assert(moved == 300u && current.kind == 0u);
    fake.stat_fail_call = 0u;
    fake.write_bump_timestamps = 1u;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, expected.size, NULL, page, &moved, &expected) == ASTRA_VFS_OK);
    assert(expected.kind == ASTRA_VFS_KIND_FILE && expected.size == 300u &&
           expected.meta.mtime_nsec == fake.mtime_nsec && fake.mtime_nsec != 0u);

    reset();
    node = fake_create("/write-full-page", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    node->size = 2u * ASTRA_MEMORY_PAGE_SIZE;
    memset(node->bytes, 0xa5, sizeof(node->bytes));
    fake.exact_identity = 1u;
    fake.write_limit = 1023u;
    assert(fake_stat_node(&fake, (uintptr_t)node, &expected) == ASTRA_VFS_OK);
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, ASTRA_MEMORY_PAGE_SIZE, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == sizeof(page) && current.size == expected.size &&
           fake.write_calls == 5u && fake.sync_calls == 1u);
    assert(memcmp(node->bytes + ASTRA_MEMORY_PAGE_SIZE, page, sizeof(page)) == 0 &&
           node->bytes[ASTRA_MEMORY_PAGE_SIZE - 1u] == 0xa5u &&
           node->bytes[2u * ASTRA_MEMORY_PAGE_SIZE] == 0xa5u);
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, expected.size, expected.size, NULL, page, &moved, &current) == ASTRA_VFS_ERR_INVALID);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == 5u);
}

static void test_page_writeback_grows_backing_to_logical_eof(void)
{
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    AstraVfsNodeInfo expected = {0}, current;
    uint32_t moved;

    reset();
    FakeNode *node = fake_create("/grow-page", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    fake.exact_identity = 1u;
    memset(page, 0x47, sizeof(page));
    assert(fake_stat_node(&fake, (uintptr_t)node, &expected) == ASTRA_VFS_OK);
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, 203u, NULL, page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == 203u && current.size == 203u && node->size == 203u);
    assert(memcmp(node->bytes, page, 203u) == 0 && node->bytes[203] == 0u);
    expected = current;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, sizeof(page), sizeof(page) + 19u, NULL,
        page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == 19u && current.size == sizeof(page) + 19u);
    for (uint32_t at = 203u; at < sizeof(page); ++at)
        assert(node->bytes[at] == 0u);
    assert(memcmp(node->bytes + sizeof(page), page, 19u) == 0 &&
        node->bytes[node->size] == 0u);
    expected = current;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, 0u, 100u, NULL, page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == 100u && current.size == expected.size && node->size == expected.size);
    uint32_t calls = fake.write_calls;
    assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
        &expected, sizeof(page), sizeof(page), NULL, page, &moved, &current) == ASTRA_VFS_ERR_INVALID);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == calls);

    for (uint32_t failure = 0u; failure < 2u; ++failure) {
        reset();
        node = fake_create("/grow-retry", ASTRA_VFS_KIND_FILE);
        assert(node != NULL);
        fake.exact_identity = 1u;
        fake.position = 123u;
        fake.write_bump_timestamps = 1u;
        fake.write_limit = 37u;
        fake.write_fail_call = failure == 0u ? 3u : 0u;
        fake.sync_status = failure == 1u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
        assert(fake_stat_node(&fake, (uintptr_t)node, &expected) == ASTRA_VFS_OK);
        assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
            &expected, 0u, 203u, NULL, page, &moved, &current) ==
            (failure == 0u ? ASTRA_VFS_ERR_NO_SPACE : ASTRA_VFS_ERR_IO));
        assert(moved == (failure == 0u ? 74u : 203u) && current.size == moved &&
            node->size == moved && fake.position == 123u);
        expected = current;
        fake.write_fail_call = 0u;
        fake.sync_status = ASTRA_VFS_OK;
        assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
            &expected, 0u, 203u, NULL, page, &moved, &expected) == ASTRA_VFS_OK);
        assert(moved == 203u && expected.size == 203u && node->size == 203u &&
            fake.position == 123u && memcmp(node->bytes, page, 203u) == 0);
        calls = fake.write_calls;
        node->size = 1u;
        assert(astra_vfs_backend_write_page(&service.backend, (uintptr_t)node,
            &expected, 0u, 203u, NULL, page, &moved, &current) == ASTRA_VFS_ERR_BUSY);
        assert(moved == 0u && current.kind == 0u && fake.write_calls == calls);
    }
}

static void test_retained_page_writeback_owns_transaction(void)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsFile retained, handle;
    AstraVfsNodeInfo expected = {0}, current;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t moved;

    reset();
    assert(astra_vfs_service_set_state_lock(
        &service, test_lock_acquire, test_lock_release, &state_mutex));
    FakeNode *node = fake_create("/retained-page", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    node->size = ASTRA_MEMORY_PAGE_SIZE + 200u;
    memset(node->bytes, 0xa5, sizeof(node->bytes));
    memset(page, 0x52, sizeof(page));
    fake.exact_identity = 1u;
    uint32_t session = open_session();
    begin_request(&request, session, "/retained-page");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    handle = reply.file;
    assert(astra_vfs_service_write_page_retained(&service, handle, NULL,
        &expected, 0u, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == 0u);
    assert(astra_vfs_service_retain_file(&service, session, handle, ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    assert(astra_vfs_service_stat_retained(&service, retained, &expected) == ASTRA_VFS_OK);
    begin_request(&request, session, NULL); request.file = handle;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, session);
    assert(service.open_sessions == 0u && node->open_count == 1);
    fake.expected_pin = &service.files[0];
    fake.position = 123u;
    fake.write_limit = 37u;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, 1u, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_INVALID);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == 0u &&
        !astra_vfs_open_file_active(&service.files[0]));
    assert(astra_vfs_service_write_page_retained(NULL, retained, NULL,
        &expected, 0u, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_INVALID);
    assert(moved == 0u && current.kind == 0u);
    uint64_t changes = service.changes.nodes;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, ASTRA_MEMORY_PAGE_SIZE, expected.size, page, &moved, &current) == ASTRA_VFS_OK);
    assert(moved == 200u && current.size == expected.size &&
        fake.write_calls == 6u && fake.sync_calls == 1u && fake.position == 123u);
    assert(node->size == expected.size && node->bytes[ASTRA_MEMORY_PAGE_SIZE - 1u] == 0xa5u &&
        memcmp(node->bytes + ASTRA_MEMORY_PAGE_SIZE, page, 200u) == 0 &&
        node->bytes[node->size] == 0xa5u && service.changes.nodes == ++changes);
    fake.write_calls = fake.sync_calls = 0u;
    fake.write_fail_call = 3u;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, ASTRA_MEMORY_PAGE_SIZE, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_NO_SPACE);
    assert(moved == 74u && fake.sync_calls == 0u && current.size == expected.size &&
        service.changes.nodes == ++changes && !astra_vfs_open_file_active(&service.files[0]));
    fake.write_fail_call = 0u;
    fake.sync_status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, ASTRA_MEMORY_PAGE_SIZE, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_IO);
    assert(moved == 200u && current.size == expected.size && service.changes.nodes == ++changes);
    fake.sync_status = ASTRA_VFS_OK;
    fake.write_bump_timestamps = 1u;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, ASTRA_MEMORY_PAGE_SIZE, expected.size, page, &moved, &expected) == ASTRA_VFS_OK);
    assert(expected.size == node->size && expected.meta.mtime_nsec == fake.mtime_nsec &&
        service.changes.nodes == ++changes);
    fake.expected_pin = NULL;
    AstraVfsFile stale_retained = retained;
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    assert(service.open_files == 0u && node->open_count == 0);
    assert(astra_vfs_service_write_page_retained(&service, stale_retained, NULL,
        &expected, 0u, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_BAD_HANDLE);
    begin_request(&request, open_session(), "/retained-page");
    request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(astra_vfs_service_retain_file(&service, request.session, reply.file, ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    uint32_t calls = fake.write_calls;
    assert(astra_vfs_service_write_page_retained(&service, retained, NULL,
        &expected, 0u, expected.size, page, &moved, &current) == ASTRA_VFS_ERR_ACCESS);
    assert(moved == 0u && current.kind == 0u && fake.write_calls == calls &&
        !astra_vfs_open_file_active(&service.files[0]));
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    astra_vfs_service_release_session(&service, request.session);
    assert(service.open_files == 0u && service.open_sessions == 0u && node->open_count == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

static void test_service_description_controls_use_backend_state(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsBackendOps descriptions = fake_ops;
    reset();
    descriptions.seek = fake_seek;
    descriptions.file_flags = fake_file_flags;
    descriptions.position_advance = fake_position_advance;
    service.backend.ops = &descriptions;
    assert(fake_create("/controls", ASTRA_VFS_KIND_FILE) != NULL);
    uint32_t session = open_session();
    begin_request(&request, session, "/controls");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile file = reply.file;
    begin_request(&request, session, NULL);
    request.file = file;
    request.write_limit = 0u;
    request.offset = (uint64_t)(int64_t)-3;
    request.flags = 2u;
    fake.position = 6u;
    fixture_dispatch(&service, ASTRA_VFS_OP_SEEK, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.position == 6u &&
           fake.description_argument == (uint64_t)(int64_t)-3 && fake.description_mask == 2u);
    fake.position = UINT64_MAX;
    fixture_dispatch(&service, ASTRA_VFS_OP_SEEK, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL && reply.position == 0u);
    fake.description_status = ASTRA_VFS_ERR_IO;
    fixture_dispatch(&service, ASTRA_VFS_OP_SEEK, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && reply.position == 0u);
    fake.description_status = ASTRA_VFS_OK;
    request.flags = 3u;
    uint32_t calls = fake.description_calls;
    fixture_dispatch(&service, ASTRA_VFS_OP_SEEK, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.description_calls == calls);
    request.flags = ASTRA_VFS_FILE_STATUS_MASK;
    request.offset = ASTRA_VFS_OPEN_APPEND;
    fake.description_result = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND;
    fixture_dispatch(&service, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.open_flags == fake.description_result &&
           fake.description_argument == ASTRA_VFS_OPEN_APPEND &&
           fake.description_mask == ASTRA_VFS_FILE_STATUS_MASK);
    const uint32_t bad_flags[] = {0u, UINT32_MAX, ASTRA_VFS_OPEN_READ};
    for (uint32_t index = 0u; index < 3u; ++index) {
        fake.description_result = bad_flags[index];
        fixture_dispatch(&service, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply);
        assert(reply.status == ASTRA_VFS_ERR_PROTOCOL && reply.open_flags == 0u);
    }
    request.flags = ASTRA_VFS_OPEN_WRITE;
    request.offset = 0u;
    calls = fake.description_calls;
    fixture_dispatch(&service, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.description_calls == calls);
    request.flags = ASTRA_VFS_OPEN_APPEND;
    request.offset = UINT64_C(1) << 32;
    fixture_dispatch(&service, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.description_calls == calls);
    request.flags = 0u;
    request.offset = 9u;
    request.length = 3u;
    fake.position = 12u;
    fake.description_result = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.position == 12u && reply.count == 1u &&
           fake.description_argument == 9u && fake.description_mask == 3u);
    fake.position = 15u;
    fake.description_result = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.position == 15u && reply.count == 0u);
    const struct { uint64_t position; uint32_t advanced; } bad_advances[] = {
        {UINT64_MAX, 1u}, {12u, 2u}, {11u, 1u}, {9u, 0u}
    };
    for (uint32_t index = 0u; index < 4u; ++index) {
        fake.position = bad_advances[index].position;
        fake.description_result = bad_advances[index].advanced;
        fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
        assert(reply.status == ASTRA_VFS_ERR_PROTOCOL && reply.position == 0u && reply.count == 0u);
    }
    request.offset = INT64_MAX;
    calls = fake.description_calls;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.description_calls == calls);
    request.offset = 9u;
    request.write_limit = 1u;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID && fake.description_calls == calls);
    request.write_limit = 0u;
    request.file ^= UINT32_C(1) << 16;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE && fake.description_calls == calls);
    request.file = file;
    request.session = open_session();
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS && fake.description_calls == calls);
    astra_vfs_service_release_session(&service, request.session);
    request.session = session;
    descriptions.seek = NULL;
    descriptions.file_flags = NULL;
    descriptions.position_advance = NULL;
    fixture_dispatch(&service, ASTRA_VFS_OP_POSITION_ADVANCE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_UNSUPPORTED && fake.description_calls == calls);
    request.length = 0u;
    request.flags = 0u;
    request.offset = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_SEEK, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_UNSUPPORTED && fake.description_calls == calls);
    fixture_dispatch(&service, ASTRA_VFS_OP_FILE_FLAGS, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_UNSUPPORTED && fake.description_calls == calls);
    astra_vfs_service_release_session(&service, session);
}

static void test_service_io_uses_backend_position(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsBackendOps descriptions = fake_ops;
    reset();
    descriptions.read_sequential = fake_read_sequential;
    service.backend.ops = &descriptions;
    FakeNode *node = fake_create("/description-io", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "abcdef", 6u);
    node->size = 6u;
    uint32_t session = open_session();
    begin_request(&request, session, "/description-io");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile file = reply.file;
    fake.position = 1u;
    begin_request(&request, session, NULL);
    request.file = file;
    request.flags = ASTRA_VFS_IO_SEQUENTIAL;
    request.offset = 99u;
    request.length = 2u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.count == 2u && reply.position == 3u &&
           memcmp(reply.payload, "bc", 2u) == 0 && fake.position == 3u && fake.sequential_reads == 1u);
    request.length = 1u;
    request.body.payload[0] = 'X';
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.count == 1u && reply.position == 4u &&
           node->bytes[3] == 'X' && fake.position == 4u);
    memset(&request.body, 0, sizeof(request.body));
    request.flags = 0u;
    request.offset = 0u;
    request.length = 2u;
    fixture_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.position == 2u &&
           memcmp(reply.payload, "ab", 2u) == 0 && fake.position == 4u);
    request.flags = ASTRA_VFS_IO_SEQUENTIAL | ASTRA_VFS_WRITE_APPEND;
    request.length = 3u;
    request.write_limit = 8u;
    memcpy(request.body.payload, "LMN", 3u);
    fixture_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.count == 2u && reply.position == 8u &&
           node->size == 8u && memcmp(node->bytes + 6u, "LM", 2u) == 0 && fake.position == 8u);
    uint8_t bytes[4];
    uint32_t moved;
    uint64_t position;
    fake.position = 0u;
    assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 4u && position == 4u && fake.position == 4u &&
           memcmp(bytes, "abcX", 4u) == 0);
    assert(astra_vfs_service_write_position_from(&service, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, UINT64_MAX, "YZ", 2u, &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 2u && position == 6u && fake.position == 6u &&
           memcmp(node->bytes, "abcXYZLM", 8u) == 0);
    assert(astra_vfs_service_read_position_into(&service, session, file, 0u,
        0u, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 4u && position == 4u && fake.position == 6u);
    const uint64_t bad_positions[] = {1u, UINT64_MAX};
    for (uint32_t index = 0u; index < 2u; ++index) {
        fake.position = 0u;
        fake.read_report_position = bad_positions[index];
        assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
            ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_ERR_IO);
        assert(moved == 0u && position == 99u);
    }
    fake.read_report_position = 0u;
    fake.position = 0u;
    fake.read_mode = 2u;
    assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_ERR_IO);
    assert(moved == 0u);
    fake.read_mode = 0u;
    fake.position = node->size;
    assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 0u && position == node->size && fake.position == node->size);
    uint32_t reads = fake.sequential_reads;
    assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
        UINT32_MAX, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_ERR_INVALID);
    assert(fake.sequential_reads == reads);
    descriptions.read_sequential = NULL;
    assert(astra_vfs_service_read_position_into(&service, session, file, 99u,
        ASTRA_VFS_IO_SEQUENTIAL, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_ERR_UNSUPPORTED);
    astra_vfs_service_release_session(&service, session);
}

static struct {
    FakeNode *node;
    void *context;
    uint64_t token;
    uint32_t shares, adopts, discards, status, share_status;
} description_share;

static uint32_t fake_description_share(void *context, uintptr_t node,
    uint64_t *token)
{
    assert(description_share.node == NULL);
    ++description_share.shares;
    if (description_share.share_status != ASTRA_VFS_OK) return description_share.share_status;
    description_share.node = (FakeNode *)node;
    description_share.context = context;
    ++description_share.node->open_count;
    *token = ++description_share.token;
    return ASTRA_VFS_OK;
}

static uint32_t fake_description_adopt(void *context, uint64_t token,
    uint32_t flags, uintptr_t *node)
{
    assert(description_share.node != NULL && token == description_share.token &&
        context == description_share.context);
    assert(description_share.adopts + description_share.discards < 8u);
    if (flags == ASTRA_VFS_ADOPT_DISCARD) ++description_share.discards;
    else { assert(flags == 0u && node != NULL); ++description_share.adopts; }
    if (flags == 0u) hold_slow_backend();
    if (description_share.status != ASTRA_VFS_OK) return description_share.status;
    if (flags == ASTRA_VFS_ADOPT_DISCARD) --description_share.node->open_count;
    else *node = (uintptr_t)description_share.node;
    description_share.node = NULL;
    return ASTRA_VFS_OK;
}

static uint32_t description_session(uint32_t owner, uint32_t grant)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    begin_request(&request, 0u, NULL);
    fixture_dispatch_grant(&service, owner, grant, ASTRA_VFS_FEATURE_DESCRIPTIONS,
        ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    return reply.session;
}

static AstraVfsFile alias_io_reference;

static void sync_through_alias_during_write(void)
{
    fake.io_hook = NULL;
    assert(astra_vfs_service_sync_retained(&service, alias_io_reference, NULL) ==
        ASTRA_VFS_ERR_BUSY);
}

static void sync_through_alias_during_page_read(void)
{
    fake.io_hook = NULL;
    assert(astra_vfs_service_sync_retained(&service, alias_io_reference, NULL) == ASTRA_VFS_OK);
}

static uint32_t io_reuse_session;
static AstraVfsFile io_reuse_file;

static void reuse_storage_owner_during_alias_write(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsOpenFile *writer = &service.files[(alias_io_reference & 0xffffu) - 1u];
    fake.io_hook = NULL;
    assert(astra_vfs_open_file_active(writer));
    begin_request(&request, io_reuse_session, NULL); request.file = io_reuse_file;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && astra_vfs_open_file_active(writer));
    begin_request(&request, io_reuse_session, "/independent"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && astra_vfs_open_file_active(writer));
    io_reuse_file = reply.file;
}

static void test_shared_description_serializes_native_io(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsFile retained;
    uint32_t moved = 0u;
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    FakeNode *node = fake_create("/shared-io", ASTRA_VFS_KIND_FILE);
    assert(node != NULL && fake_create("/independent", ASTRA_VFS_KIND_FILE) != NULL);
    uint32_t source = open_session(), target = open_session();
    begin_request(&request, source, "/shared-io");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile original = reply.file;
    assert(astra_vfs_service_retain_file(&service, source, original,
        ASTRA_VFS_OPEN_WRITE, &retained) == ASTRA_VFS_OK);
    begin_request(&request, source, NULL); request.file = original; request.write_limit = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    begin_request(&request, target, NULL); request.offset = reply.token; request.write_limit = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile alias = reply.file;
    assert(astra_vfs_service_retain_file(&service, target, alias,
        ASTRA_VFS_OPEN_WRITE, &alias_io_reference) == ASTRA_VFS_OK);
    fake.io_hook = sync_through_alias_during_write;
    assert(astra_vfs_service_write_retained(&service, retained, 0u,
        UINT64_MAX, "a", 1u, &moved) == ASTRA_VFS_OK && moved == 1u && fake.io_hook == NULL);
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    fake.io_hook = sync_through_alias_during_page_read;
    assert(astra_vfs_service_read_page_retained(&service, retained, ASTRA_VFS_OPEN_READ, NULL, 0u,
        page, &moved) == ASTRA_VFS_ERR_BUSY && moved == 0u && fake.io_hook == NULL);
    begin_request(&request, source, NULL); request.file = original;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK &&
        astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK);
    begin_request(&request, source, "/independent"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && (reply.file & 0xffffu) == (original & 0xffffu));
    AstraVfsFile independent = reply.file;
    assert(service.files[(alias & 0xffffu) - 1u].io !=
        service.files[(independent & 0xffffu) - 1u].io);
    assert(astra_vfs_service_sync_retained(&service, alias_io_reference, NULL) == ASTRA_VFS_OK);
    io_reuse_session = source; io_reuse_file = independent;
    fake.io_hook = reuse_storage_owner_during_alias_write;
    assert(astra_vfs_service_write_retained(&service, alias_io_reference, 0u,
        UINT64_MAX, "b", 1u, &moved) == ASTRA_VFS_OK && moved == 1u && fake.io_hook == NULL);
    independent = io_reuse_file;
    begin_request(&request, target, NULL); request.file = alias;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK &&
        astra_vfs_service_release_retained(&service, &alias_io_reference) == ASTRA_VFS_OK);
    begin_request(&request, source, NULL); request.file = independent;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 0 && service.open_files == 0u);
    begin_request(&request, source, "/independent"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && (reply.file & 0xffffu) == (original & 0xffffu));
    astra_vfs_service_release_session(&service, source);
    astra_vfs_service_release_session(&service, target);
    assert(service.open_files == 0u);
    for (uint32_t at = 0u; at < service.file_high_water; ++at)
        assert(service.files[at].io == NULL && service.files[at].io_storage.references == 0u &&
            service.files[at].io_storage.active == NULL && service.files[at].io_storage.waiters == 0u);
}

static void test_shared_directory_anchors_use_one_io_owner(void)
{
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsClient client;
    AstraVfsFile directory, alias;
    uint64_t token = 0u;
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    assert(fake_create("/dir", ASTRA_VFS_KIND_DIRECTORY) != NULL &&
        fake_create("/dir/a", ASTRA_VFS_KIND_FILE) != NULL);
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/dir", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &directory, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_share(&client, directory, &token) == ASTRA_VFS_OK &&
        astra_vfs_adopt(&client, token, &alias) == ASTRA_VFS_OK);
    assert(astra_vfs_rename_at(&client, directory, "a", alias, "b") == ASTRA_VFS_OK);
    assert(fake_find("/dir/a") == NULL && fake_find("/dir/b") != NULL);
    assert(astra_vfs_rename_at(&client, alias, "b", directory, "c") == ASTRA_VFS_OK);
    assert(fake_find("/dir/b") == NULL && fake_find("/dir/c") != NULL);
    assert(astra_vfs_close(&client, directory) == ASTRA_VFS_OK &&
        astra_vfs_close(&client, alias) == ASTRA_VFS_OK &&
        astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
}

static AstraVfsFileIO *ordered_io[2];
static uint32_t ordered_waits;

static int finish_ordered_io(void *context, volatile uint32_t *sequence, uint32_t expected)
{
    (void)context;
    assert(ordered_waits < 2u && sequence == &ordered_io[ordered_waits]->sequence &&
        *sequence == expected && ordered_io[ordered_waits]->active != NULL);
    ordered_io[ordered_waits++]->active = NULL;
    ++*sequence;
    return 1;
}

static int finish_growing_writer(void *context, volatile uint32_t *sequence, uint32_t expected)
{
    uint32_t moved;
    uint64_t position;
    AstraVfsOpenFile *writer = ordered_io[0]->active;
    assert(fake_write(writer->backend.context, writer->node, 0u, 0u,
        UINT64_MAX, "changed", 7u, &moved, &position) == ASTRA_VFS_OK && moved == 7u);
    return finish_ordered_io(context, sequence, expected);
}

typedef struct AreaProbe {
    AstraVfsClient *client;
    AstraVfsFile file, source;
    uint64_t size, base;
    uint32_t calls, status;
    int close_session, empty;
} AreaProbe;

static uint32_t create_test_area(void *context, const AstraVfsOpenFile *file,
    AstraVfsFile *owned, const AstraVfsNodeInfo *info,
    uint64_t base, uint32_t span, uint32_t *area)
{
    AreaProbe *probe = context;
    assert(astra_vfs_open_file_active(file) && file->retained != 0u &&
        info->kind == ASTRA_VFS_KIND_FILE && info->size == probe->size &&
        base == probe->base && span == ASTRA_MEMORY_PAGE_SIZE);
    assert(pthread_mutex_trylock(service.state_lock_context) == 0);
    assert(pthread_mutex_unlock(service.state_lock_context) == 0);
    ++probe->calls;
    if (probe->status != ASTRA_VFS_OK) return probe->status;
    if (probe->close_session) {
        assert(astra_vfs_close(probe->client, probe->file) == ASTRA_VFS_OK);
        astra_vfs_service_release_session(&service, probe->client->session);
        assert(file->state == ASTRA_VFS_FILE_DETACHED);
    }
    assert(astra_vfs_service_retain_active(&service, file, &probe->source) == ASTRA_VFS_OK);
    assert(probe->source == *owned && file->retained == 2u);
    *area = probe->empty ? 0u : 88u;
    return ASTRA_VFS_OK;
}

static void test_file_views_recheck_eof_after_wait(void)
{
    for (unsigned view = 0u; view < 2u; ++view) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        AstraVfsClient client;
        AstraVfsFile writer;
        uint8_t bytes[8];
        uint32_t moved;
        uint64_t size;

        reset(); fake.exact_identity = 1u;
        FakeNode *node = fake_create("/whole-wait", ASTRA_VFS_KIND_FILE);
        assert(node != NULL);
        memcpy(node->bytes, "old", 3u); node->size = 3u;
        assert(pthread_cond_init(&file_condition, NULL) == 0);
        assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
        assert(astra_vfs_service_set_state_wait(&service, finish_growing_writer, test_state_wake));
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/whole-wait", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
            &writer, NULL, NULL) == ASTRA_VFS_OK);
        ordered_waits = 0u;
        ordered_io[0] = service.files[0].io;
        ordered_io[0]->active = &service.files[0];
        if (view == 0u) {
            assert(astra_vfs_service_read_path(&service, client.session, "/whole-wait",
                bytes, sizeof(bytes), &moved, &size) == ASTRA_VFS_OK && moved == 7u && size == 7u &&
                memcmp(bytes, "changed", 7u) == 0 && ordered_waits == 1u);
        } else {
            AreaProbe probe = {.size = 7u};
            assert(astra_vfs_service_file_area(&service, client.session, writer,
                0u, ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &moved, &size) ==
                ASTRA_VFS_OK && moved == 88u && size == 7u && ordered_waits == 1u);
            assert(astra_vfs_service_release_retained(&service, &probe.source) == ASTRA_VFS_OK);
        }
        assert(astra_vfs_close(&client, writer) == ASTRA_VFS_OK);
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
        assert(pthread_cond_destroy(&file_condition) == 0);
        assert(pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_file_area_allows_empty_and_beyond_eof(void)
{
    static const struct { uint64_t size, base; } cases[] = {
        {0u, 0u}, {3u, ASTRA_MEMORY_PAGE_SIZE}, {3u, ASTRA_AREA_SIZE_MAX}
    };
    for (uint32_t at = 0u; at < sizeof(cases) / sizeof(cases[0]); ++at) {
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        AstraVfsClient client;
        AstraVfsFile file;
        AreaProbe probe = {.size = cases[at].size, .base = cases[at].base};
        uint32_t area;
        uint64_t size;
        reset(); fake.exact_identity = 1u;
        FakeNode *node = fake_create("/empty-map", ASTRA_VFS_KIND_FILE);
        assert(node != NULL); node->size = probe.size;
        assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
        assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
        assert(astra_vfs_open(&client, "/empty-map", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) == ASTRA_VFS_OK);
        assert(astra_vfs_service_file_area(&service, client.session, file, probe.base,
            ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_OK);
        assert(area == 88u && size == probe.size && probe.calls == 1u);
        assert(astra_vfs_service_release_retained(&service, &probe.source) == ASTRA_VFS_OK);
        assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);
        assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK && service.open_files == 0u);
        assert(pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_file_area_retains_native_authority(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsClient client;
    AstraVfsFile file, write_only, source;
    uint32_t area;
    uint64_t size;
    AreaProbe probe = {.client = &client, .size = 3u};
    BoundBackend first = {.incarnation = 7u}, second = {.incarnation = 8u};
    BackendBindings bindings = {.selected = &first};

    reset(); fake.exact_identity = 1u;
    FakeNode *node = fake_create("/area", ASTRA_VFS_KIND_FILE);
    assert(node != NULL); memcpy(node->bytes, "old", 3u); node->size = 3u;
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    fake.check_context = 1u; fake.expected_context = &first;
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/area", ASTRA_VFS_OPEN_READ, &file, NULL, NULL) == ASTRA_VFS_OK);
    probe.file = file;
    assert(astra_vfs_service_retain_active(&service, &service.files[0], &source) == ASTRA_VFS_ERR_BAD_HANDLE &&
        source == ASTRA_VFS_FILE_INVALID);
    assert(astra_vfs_open(&client, "/area", ASTRA_VFS_OPEN_WRITE, &write_only, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_vfs_service_file_area(&service, client.session, write_only, 0u,
        ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_ERR_ACCESS &&
        area == 0u && size == 0u && probe.calls == 0u);
    assert(astra_vfs_close(&client, write_only) == ASTRA_VFS_OK);
    for (unsigned bad = 0u; bad < 3u; ++bad) {
        area = 99u; size = UINT64_MAX;
        assert(astra_vfs_service_file_area(&service, client.session, file,
            bad == 0u ? 1u : bad == 1u ? UINT64_MAX - (ASTRA_MEMORY_PAGE_SIZE - 1u) : 0u,
            bad == 2u ? 1u : ASTRA_MEMORY_PAGE_SIZE,
            create_test_area, &probe, &area, &size) == ASTRA_VFS_ERR_INVALID &&
            area == 0u && size == 0u && probe.calls == 0u);
    }
    fake.stat_fail_call = fake.stat_calls + 1u;
    assert(astra_vfs_service_file_area(&service, client.session, file, 0u,
        ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_ERR_IO &&
        service.files[0].retained == 0u && probe.calls == 0u);
    fake.stat_fail_call = 0u;
    probe.status = ASTRA_VFS_ERR_LIMIT;
    assert(astra_vfs_service_file_area(&service, client.session, file, 0u,
        ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_ERR_LIMIT &&
        service.files[0].retained == 0u && probe.calls == 1u);
    probe.status = ASTRA_VFS_OK; probe.empty = 1;
    assert(astra_vfs_service_file_area(&service, client.session, file, 0u,
        ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_ERR_IO &&
        area == 0u && size == 0u && service.files[0].retained == 1u);
    assert(astra_vfs_service_release_retained(&service, &probe.source) == ASTRA_VFS_OK);
    probe.empty = 0; probe.close_session = 1;
    assert(astra_vfs_service_file_area(&service, client.session, file, 0u,
        ASTRA_MEMORY_PAGE_SIZE, create_test_area, &probe, &area, &size) == ASTRA_VFS_OK &&
        area == 88u && size == 3u && service.open_sessions == 0u && service.open_files == 1u &&
        service.files[0].retained == 1u && first.references == 1u);
    bindings.selected = &second;
    uint8_t bytes[4]; uint32_t moved;
    assert(astra_vfs_service_read_retained(&service, probe.source, 0u,
        bytes, sizeof(bytes), &moved) == ASTRA_VFS_OK && moved == 3u && memcmp(bytes, "old", 3u) == 0);
    assert(astra_vfs_service_release_retained(&service, &probe.source) == ASTRA_VFS_OK &&
        probe.source == ASTRA_VFS_FILE_INVALID && service.open_files == 0u &&
        first.references == 0u && second.references == 0u);
    assert(astra_vfs_service_retain_active(&service, &service.files[0], &probe.source) == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(pthread_mutex_destroy(&mutex) == 0);
}

static void test_directory_io_lock_order(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    AstraVfsClient client;
    AstraVfsFile left, right;
    reset();
    assert(fake_create("/left", ASTRA_VFS_KIND_DIRECTORY) != NULL &&
        fake_create("/right", ASTRA_VFS_KIND_DIRECTORY) != NULL &&
        fake_create("/right/b", ASTRA_VFS_KIND_FILE) != NULL);
    assert(pthread_cond_init(&file_condition, NULL) == 0);
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &mutex));
    assert(astra_vfs_service_set_state_wait(&service, finish_ordered_io, test_state_wake));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/left", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &left, NULL, NULL) == ASTRA_VFS_OK &&
        astra_vfs_open(&client, "/right", ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        &right, NULL, NULL) == ASTRA_VFS_OK);
    AstraVfsOpenFile *a = &service.files[(left & 0xffffu) - 1u];
    AstraVfsOpenFile *b = &service.files[(right & 0xffffu) - 1u];
    ordered_io[0] = a->io; ordered_io[1] = b->io;
    assert((uintptr_t)a->io < (uintptr_t)b->io);
    for (unsigned reverse = 0u; reverse < 2u; ++reverse) {
        ordered_waits = 0u;
        a->io->active = a; b->io->active = b;
        assert(astra_vfs_rename_at(&client, reverse ? left : right, "b",
            reverse ? right : left, "b") == ASTRA_VFS_OK && ordered_waits == 2u);
    }
    assert(astra_vfs_close(&client, left) == ASTRA_VFS_OK &&
        astra_vfs_close(&client, right) == ASTRA_VFS_OK &&
        astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(pthread_cond_destroy(&file_condition) == 0 && pthread_mutex_destroy(&mutex) == 0);
}

static void test_file_holder_observer(void)
{
    SessionProbe probe = {0};
    AstraVfsRequest request;
    AstraVfsReply reply;
    AstraVfsFile retained;
    AstraVfsBackendOps descriptions = fake_ops;
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    assert(fake_create("/observed", ASTRA_VFS_KIND_FILE) != NULL);
    assert(astra_vfs_service_set_holders_changed(&service, holders_changed, &probe));
    uint32_t source = open_session(), target = open_session();
    assert(atomic_load(&probe.notifications) == 2u);
    begin_request(&request, source, "/observed"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && atomic_load(&probe.notifications) == 3u);
    AstraVfsFile file = reply.file;
    assert(astra_vfs_service_retain_file(&service, source, file, ASTRA_VFS_OPEN_READ, &retained) == ASTRA_VFS_OK);
    begin_request(&request, source, NULL); request.file = file; request.write_limit = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && atomic_load(&probe.notifications) == 4u);
    uint64_t token = reply.token;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && atomic_load(&probe.notifications) == 5u);
    begin_request(&request, target, NULL); request.offset = token; request.write_limit = 0u;
    fixture_dispatch(&service, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && atomic_load(&probe.notifications) == 6u);
    AstraVfsFile adopted = reply.file;
    assert(astra_vfs_service_release_retained(&service, &retained) == ASTRA_VFS_OK &&
        atomic_load(&probe.notifications) == 7u);
    begin_request(&request, target, NULL); request.file = adopted;
    fixture_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && atomic_load(&probe.notifications) == 8u);
    astra_vfs_service_release_session(&service, source);
    astra_vfs_service_release_session(&service, target);
    assert(atomic_load(&probe.notifications) == 10u && atomic_load(&probe.members) == 0u);
}

typedef struct SharedBorrowProbe {
    AstraVfsSessionSlot *source;
    uint32_t target, calls, status;
    uint64_t token;
    int close_source;
} SharedBorrowProbe;

static uint32_t borrow_shared(void *context, const AstraVfsOpenFile *file)
{
    SharedBorrowProbe *probe = context;
    AstraVfsRequest request;
    AstraVfsReply reply;
    assert(file->state == ASTRA_VFS_FILE_SHARED && astra_vfs_open_file_active(file) &&
        file->token == probe->token && probe->source->inflight == 1u);
    ++probe->calls;
    begin_request(&request, probe->target, NULL);
    request.offset = probe->token; request.write_limit = 0u;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BUSY);
    request.session = probe->source->id; request.flags = ASTRA_VFS_ADOPT_DISCARD;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BUSY);
    if (probe->close_source) {
        astra_vfs_service_release_session(&service, probe->source->id);
        assert(probe->source->closing && probe->source->inflight == 1u &&
            file->state == ASTRA_VFS_FILE_SHARED && astra_vfs_open_file_active(file));
    }
    return probe->status;
}

static void test_share_borrow_pins_native_owner(void)
{
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsRequest request;
    AstraVfsReply reply;
    SharedBorrowProbe probe = {0};
    reset(); memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share; descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    FakeNode *node = fake_create("/borrowed-share", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    uint32_t source = description_session(11u, 71u);
    probe.target = description_session(22u, 71u);
    begin_request(&request, source, "/borrowed-share"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile file = reply.file;
    begin_request(&request, source, NULL); request.file = file; request.write_limit = 0u;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    probe.token = reply.token; probe.source = &service.sessions[0];
    assert(probe.source->id == source);
    assert(astra_vfs_service_borrow_share(&service, 22u, probe.token, borrow_shared, &probe) ==
        ASTRA_VFS_ERR_ACCESS && probe.calls == 0u);
    assert(astra_vfs_service_borrow_share(&service, 11u, probe.token + 1u, borrow_shared, &probe) ==
        ASTRA_VFS_ERR_BAD_HANDLE && probe.calls == 0u);
    probe.status = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_service_borrow_share(&service, 11u, probe.token, borrow_shared, &probe) ==
        ASTRA_VFS_ERR_IO && probe.calls == 1u && service.open_files == 2u &&
        description_share.discards == 0u && description_share.adopts == 0u);
    probe.status = ASTRA_VFS_OK; probe.close_source = 1;
    assert(astra_vfs_service_borrow_share(&service, 11u, probe.token, borrow_shared, &probe) ==
        ASTRA_VFS_OK && probe.calls == 2u && service.open_files == 0u &&
        node->open_count == 0 && description_share.discards == 1u);
    assert(astra_vfs_service_borrow_share(&service, 11u, probe.token, borrow_shared, &probe) ==
        ASTRA_VFS_ERR_BAD_HANDLE && probe.calls == 2u);
    astra_vfs_service_release_session(&service, probe.target);
}

static void test_service_shares_keep_native_ownership(void)
{
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsRequest request;
    AstraVfsReply reply;
    BoundBackend first = {0}, second = {0};
    BackendBindings bindings = {.selected = &first, .ops = &descriptions};
    reset();
    memset(&description_share, 0, sizeof(description_share));
    description_share.token = UINT64_C(0x100000000);
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    descriptions.read_sequential = fake_read_sequential;
    service.backend.ops = &descriptions;
    service.file_capacity = 2u;
    assert(astra_vfs_service_set_backend_bindings(&service, &backend_bindings, &bindings));
    FakeNode *node = fake_create("/shared", ASTRA_VFS_KIND_FILE);
    assert(node != NULL);
    memcpy(node->bytes, "abcd", 4u); node->size = 4u;
    uint32_t source = description_session(11u, 71u);
    uint32_t target = description_session(22u, 71u);
    uint32_t other = description_session(33u, 72u);
    begin_request(&request, source, "/shared"); request.write_limit = 0u;
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 1);
    AstraVfsFile original = reply.file;
    begin_request(&request, source, NULL); request.write_limit = 0u;
    request.file = original;
    for (unsigned i = 0u; i < 4u; ++i) {
        AstraVfsRequest malformed = request;
        if (i == 0u) malformed.flags = 1u;
        if (i == 1u) malformed.offset = 1u;
        if (i == 2u) malformed.length = 1u;
        if (i == 3u) malformed.write_limit = 1u;
        fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &malformed, &reply);
        assert(reply.status == ASTRA_VFS_ERR_INVALID && description_share.shares == 0u &&
            service.open_files == 1u && first.references == 4u);
    }
    description_share.share_status = ASTRA_VFS_ERR_IO;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && node->open_count == 1 &&
        service.open_files == 1u && first.references == 4u);
    description_share.share_status = ASTRA_VFS_OK;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.token != 0u &&
        node->open_count == 2 && service.open_files == 2u && first.references == 5u);
    uint64_t token = reply.token;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT && description_share.shares == 2u);
    for (unsigned i = 0u; i < 5u; ++i) {
        AstraVfsRequest malformed;
        begin_request(&malformed, target, NULL); malformed.write_limit = 0u; malformed.offset = token;
        if (i == 0u) malformed.flags = 2u;
        if (i == 1u) malformed.file = original;
        if (i == 2u) malformed.length = 1u;
        if (i == 3u) malformed.write_limit = 1u;
        if (i == 4u) malformed.offset = 0u;
        fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &malformed, &reply);
        assert(reply.status == ASTRA_VFS_ERR_INVALID && description_share.adopts == 0u &&
            description_share.discards == 0u && service.open_files == 2u);
    }
    begin_request(&request, other, NULL); request.write_limit = 0u; request.offset = token;
    fixture_dispatch_grant(&service, 33u, 72u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS && description_share.adopts == 0u);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.offset = token;
    request.flags = ASTRA_VFS_ADOPT_DISCARD;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS && description_share.discards == 0u);
    begin_request(&request, source, NULL); request.write_limit = 0u; request.file = original;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 1 && service.open_files == 1u &&
        first.references == 4u);
    bindings.selected = &second;
    begin_request(&request, target, NULL);
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && first.references == 3u && second.references == 1u);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.offset = token;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LOST && description_share.adopts == 0u);
    bindings.selected = &first;
    begin_request(&request, target, NULL);
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && first.references == 4u && second.references == 0u);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.offset = token;
    description_share.status = ASTRA_VFS_ERR_IO;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_IO && node->open_count == 1 && service.open_files == 1u);
    description_share.status = ASTRA_VFS_OK;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.file != 0u && node->open_count == 1);
    AstraVfsFile adopted = reply.file;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE && description_share.adopts == 2u);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.file = adopted; request.length = 2u;
    request.flags = ASTRA_VFS_IO_SEQUENTIAL;
    fake.position = 1u;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && reply.position == 3u &&
        memcmp(reply.payload, "bc", 2u) == 0);
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS && fake.position == 3u);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.file = adopted;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 2);
    token = reply.token;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 1);
    description_share.status = ASTRA_VFS_ERR_IO;
    astra_vfs_service_release_session(&service, target);
    assert(node->open_count == 1 && service.open_files == 1u &&
        description_share.discards == 1u);
    assert(!astra_vfs_service_session_bound(&service, target, 22u, 71u));
    description_share.status = ASTRA_VFS_OK;
    astra_vfs_service_reap(&service);
    assert(node->open_count == 0 && service.open_files == 0u &&
        description_share.discards == 2u);
    begin_request(&request, source, NULL); request.write_limit = 0u; request.offset = token;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    astra_vfs_service_release_session(&service, source);
    astra_vfs_service_release_session(&service, other);
    assert(service.open_sessions == 0u && first.references == 0u && second.references == 0u);
}

typedef struct ShareCloseRace {
    uint32_t session;
    AstraVfsFile file;
    uint64_t token;
    unsigned armed;
} ShareCloseRace;

static void share_close_race(void *context, uint32_t point)
{
    ShareCloseRace *race = context;
    if (!race->armed ||
        point != (race->token == 0u ? ASTRA_VFS_TEST_AFTER_STATE_RELEASE : ASTRA_VFS_TEST_DURING_CLOSE))
        return;
    race->armed = 0u;
    AstraVfsRequest request;
    AstraVfsReply reply;
    begin_request(&request, race->session, NULL); request.write_limit = 0u;
    if (race->token != 0u) {
        request.offset = race->token;
        fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_ADOPT, &request, &reply);
        assert(reply.status == ASTRA_VFS_ERR_BUSY);
    } else {
        request.file = race->file;
        fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_CLOSE, &request, &reply);
        /* With no waiter installed, close must refuse the busy file, never free it. */
        assert(reply.status == ASTRA_VFS_ERR_IO);
    }
}

static void test_share_pins_source_through_backend_io(void)
{
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsRequest request;
    AstraVfsReply reply;
    ShareCloseRace race = {0};
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    FakeNode *node = fake_create("/share-race", ASTRA_VFS_KIND_FILE);
    begin_request(&request, 0u, NULL);
    fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    race.session = reply.session;
    begin_request(&request, race.session, "/share-race"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    race.file = reply.file; race.armed = 1u;
    astra_vfs_service_set_test_transition(&service, share_close_race, &race);
    begin_request(&request, race.session, NULL); request.write_limit = 0u; request.file = race.file;
    fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && race.armed == 0u && node->open_count == 2);
    race.token = reply.token; race.armed = 1u;
    request.file = 0u; request.offset = race.token; request.flags = ASTRA_VFS_ADOPT_DISCARD;
    fixture_dispatch_from(&service, 11u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && race.armed == 0u && node->open_count == 1);
    astra_vfs_service_set_test_transition(&service, NULL, NULL);
    astra_vfs_service_release_session(&service, race.session);
    assert(service.open_files == 0u && service.open_sessions == 0u && node->open_count == 0);
}

static void test_service_transfer_quota_is_owned_by_native_caller(void)
{
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsRequest request;
    AstraVfsReply reply;
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    service.file_capacity = 4u;
    assert(astra_vfs_service_set_owner_files(&service, 2u));
    FakeNode *node = fake_create("/quota", ASTRA_VFS_KIND_FILE);
    uint32_t source = description_session(11u, 71u);
    uint32_t same_owner = description_session(11u, 71u);
    uint32_t target = description_session(22u, 71u);
    uint32_t same_target = description_session(22u, 71u);
    begin_request(&request, source, "/quota"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    AstraVfsFile file = reply.file;
    begin_request(&request, source, NULL); request.write_limit = 0u; request.file = file;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    uint64_t token = reply.token;
    begin_request(&request, same_owner, "/quota"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT && node->open_count == 2);
    begin_request(&request, same_owner, NULL); request.write_limit = 0u; request.offset = token;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && node->open_count == 2);
    request.file = reply.file; request.offset = 0u;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT && description_share.shares == 1u);
    astra_vfs_service_release_session(&service, same_owner);
    begin_request(&request, source, NULL); request.write_limit = 0u; request.file = file;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    token = reply.token;
    const uint32_t sessions[] = {target, same_target};
    for (unsigned i = 0; i < 2u; ++i) {
        begin_request(&request, sessions[i], "/quota"); request.flags = ASTRA_VFS_OPEN_READ;
        fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    assert(service.open_files == 4u && node->open_count == 4);
    begin_request(&request, target, NULL); request.write_limit = 0u; request.offset = token;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT && description_share.adopts == 1u);
    assert(astra_vfs_service_set_owner_files(&service, 3u));
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK && service.open_files == 4u && node->open_count == 4);
    astra_vfs_service_release_session(&service, source);
    astra_vfs_service_release_session(&service, target);
    astra_vfs_service_release_session(&service, same_target);
    assert(service.open_files == 0u && service.open_sessions == 0u && node->open_count == 0);
}

typedef struct DescriptionAdoptCall {
    uint32_t session;
    uint64_t token;
    AstraVfsReply reply;
} DescriptionAdoptCall;

static void *run_description_adopt(void *context)
{
    DescriptionAdoptCall *call = context;
    AstraVfsRequest request;
    begin_request(&request, call->session, NULL); request.write_limit = 0u; request.offset = call->token;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &call->reply);
    return NULL;
}

static void test_adoption_reserves_quota_before_backend_io(int fail_adopt)
{
    pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t thread;
    AstraVfsBackendOps descriptions = fake_ops;
    AstraVfsRequest request;
    AstraVfsReply reply;
    DescriptionAdoptCall call = {0};
    reset();
    memset(&description_share, 0, sizeof(description_share));
    descriptions.share = fake_description_share;
    descriptions.adopt = fake_description_adopt;
    service.backend.ops = &descriptions;
    assert(astra_vfs_service_set_state_lock(&service, test_lock_acquire, test_lock_release, &state_mutex));
    FakeNode *node = fake_create("/adopting", ASTRA_VFS_KIND_FILE);
    uint32_t source = description_session(11u, 71u);
    call.session = description_session(22u, 71u);
    begin_request(&request, source, "/adopting"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    begin_request(&request, source, NULL); request.write_limit = 0u; request.file = reply.file;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_SHARE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    call.token = reply.token;
    description_share.status = fail_adopt ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
    assert(astra_vfs_service_set_owner_files(&service, 1u));
    assert(pthread_mutex_init(&concurrent_gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&concurrent_gate.changed, NULL) == 0);
    concurrent_gate.enabled = 1;
    concurrent_gate.slow_entered = concurrent_gate.release_slow = 0;
    assert(pthread_create(&thread, NULL, run_description_adopt, &call) == 0);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    while (!concurrent_gate.slow_entered)
        assert(pthread_cond_wait(&concurrent_gate.changed, &concurrent_gate.mutex) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    begin_request(&request, call.session, NULL); request.write_limit = 0u; request.offset = call.token;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BUSY && description_share.adopts == 1u);
    request.session = source; request.flags = ASTRA_VFS_ADOPT_DISCARD;
    fixture_dispatch_grant(&service, 11u, 71u, 0u, ASTRA_VFS_OP_ADOPT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BUSY && description_share.discards == 0u);
    begin_request(&request, call.session, "/adopting"); request.flags = ASTRA_VFS_OPEN_READ;
    fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT && node->open_count == 2);
    astra_vfs_service_release_session(&service, source);
    assert(service.open_sessions == 2u && service.open_files == 1u && node->open_count == 1);
    assert(pthread_mutex_lock(&concurrent_gate.mutex) == 0);
    concurrent_gate.release_slow = 1;
    assert(pthread_cond_broadcast(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_unlock(&concurrent_gate.mutex) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(call.reply.status == (fail_adopt ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK) &&
        service.open_sessions == (fail_adopt ? 2u : 1u) && service.open_files == 1u);
    concurrent_gate.enabled = 0;
    if (fail_adopt) {
        description_share.status = ASTRA_VFS_OK;
        begin_request(&request, call.session, "/adopting"); request.flags = ASTRA_VFS_OPEN_READ;
        fixture_dispatch_grant(&service, 22u, 71u, 0u, ASTRA_VFS_OP_OPEN, &request, &reply);
        assert(reply.status == ASTRA_VFS_OK && service.open_files == 2u && node->open_count == 2);
        astra_vfs_service_reap(&service);
        assert(service.open_sessions == 1u && service.open_files == 1u && node->open_count == 1);
    }
    astra_vfs_service_release_session(&service, call.session);
    assert(service.open_sessions == 0u && service.open_files == 0u && node->open_count == 0);
    assert(pthread_cond_destroy(&concurrent_gate.changed) == 0);
    assert(pthread_mutex_destroy(&concurrent_gate.mutex) == 0);
    assert(pthread_mutex_destroy(&state_mutex) == 0);
}

int
main(void)
{
    test_cache_reads_share_native_owner(0u);
    test_cache_reads_share_native_owner(ASTRA_VFS_CACHE_EXTERNAL_WRITES);
    test_cache_stat_keeps_native_owner();
    test_whole_cache_read_keeps_session_lifetime();
    test_file_views_recheck_eof_after_wait();
    test_file_area_retains_native_authority();
    test_file_area_allows_empty_and_beyond_eof();
    test_byte_reply_preserves_only_validated_prefixes();
    test_cache_write_handoff_keeps_rooted_authority();
    test_cache_write_borrow_and_failed_prefix();
    test_cache_logical_metadata();
    test_cache_sync_and_truncate();
    test_metadata_flushes_cached_node();
    test_rename_flushes_both_cached_nodes();
    test_cached_namespace_transaction_orders_open_and_mutation();
    test_cache_sparse_metadata_and_failed_writeback();
    test_cache_requires_exact_identity();
    test_file_io_identity_domains();
    test_open_truncate_joins_node_before_mutation();
    test_independent_opens_share_page_fence();
    test_shared_description_serializes_native_io();
    test_shared_directory_anchors_use_one_io_owner();
    test_directory_io_lock_order();
    test_retained_page_read_during_description_io();
    test_retained_page_read_owns_lifetime();
    test_retained_write_only_access();
    test_retained_page_writeback_owns_transaction();
    test_service_shares_keep_native_ownership();
    test_share_pins_source_through_backend_io();
    test_service_transfer_quota_is_owned_by_native_caller();
    test_adoption_reserves_quota_before_backend_io(0);
    test_adoption_reserves_quota_before_backend_io(1);
    test_scoped_backend_lifetime_and_rebind();
    test_service_description_controls_use_backend_state();
    test_service_io_uses_backend_position();
    test_session_observer();
    test_file_holder_observer();
    test_share_borrow_pins_native_owner();
    test_read_path_failures();
    test_mount_shutdown();
    test_last_failed_request_diagnostics();
    test_terminal_readdir_cookie();
    test_session_handshake();
    test_owner_sessions_are_limited();
    test_transport_owner_isolation_and_resource_exhaustion();
    test_sessions_are_bound_to_their_grant();
    test_file_round_trip();
    test_mutation_generations();
    test_handle_discipline();
    test_handle_generation_exhaustion();
    test_session_id_exhaustion();
    test_session_release_frees_files();
    test_malformed_records();
    test_full_width_binary_write();
    test_limits();
    test_file_storage_budget();
    test_client_through_transport(0);
    test_client_through_transport(1);
    test_lookup_reply_validation();
    test_uncontended_file_operation_does_not_wake();
    test_read_refuses_reused_waiting_slot();
    test_unrelated_session_overtakes_held_backend();
    test_same_session_independent_request_overtakes_held_backend();
    test_close_waits_for_active_file_operation();
    test_session_release_waits_for_pinned_file();
    test_retained_description_survives_client_close();
    test_retained_page_read_checks_complete_file();
    test_page_writeback_preserves_eof_and_reports_partial_failure();
    test_page_writeback_grows_backing_to_logical_eof();
    test_descriptor_times_validate_and_preserve_unselected_fields();
    test_forced_switch_model();
    puts("astra vfs service core: PASS");
    return 0;
}
