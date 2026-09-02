/*
 * The storage service core.
 *
 * Everything here is validation, handle bookkeeping and dispatch. The bytes
 * belong to a backend and the transport belongs to a caller; see
 * astra/vfs_backend.h and astra/vfs_service_core.h for why the three are apart.
 */

#include <astra/vfs_service_core.h>

#include <astra/endian.h>

#include <stddef.h>
#include <string.h>

/*
 * A handle is (generation << 16) | (slot + 1).
 *
 * Slot zero would collide with ASTRA_VFS_FILE_INVALID, so slots are stored
 * one-based on the wire. The generation advances every time a slot is reused,
 * which is what turns "this handle is stale" from undetectable into a counted
 * refusal.
 */
#define HANDLE_SLOT_MASK 0xffffu
#define HANDLE_MAKE(slot, generation) \
    ((((uint32_t)(generation)) << 16) | ((uint32_t)(slot) + 1u))
#define HANDLE_SLOT(handle) (((handle) & HANDLE_SLOT_MASK) - 1u)
#define HANDLE_GENERATION(handle) ((uint16_t)((handle) >> 16))

#ifdef ASTRA_VFS_TEST_HOOKS
static void
test_transition(AstraVfsService *service, uint32_t transition)
{
    if (service->test_transition != NULL)
        service->test_transition(service->test_transition_context, transition);
}
#else
#define test_transition(service, transition) ((void)(service))
#endif

static int
state_acquire(AstraVfsService *service)
{
    test_transition(service, ASTRA_VFS_TEST_BEFORE_STATE_ACQUIRE);
    return service->state_acquire == NULL ||
           service->state_acquire(service->state_lock_context);
}

static void
state_release(AstraVfsService *service)
{
    if (service->state_release != NULL)
        service->state_release(service->state_lock_context);
    test_transition(service, ASTRA_VFS_TEST_AFTER_STATE_RELEASE);
}

static int
file_acquire(AstraVfsService *service, AstraVfsOpenFile *file)
{
    while (file->active != 0u) {
        uint32_t expected = file->sequence;
        int waited;

        if (service->state_wait == NULL || file->waiters == UINT16_MAX)
            return 0;
        ++file->waiters;
        waited = service->state_wait(service->state_lock_context,
                                     &file->sequence, expected);
        --file->waiters;
        if (!waited)
            return 0;
        if (file->state != ASTRA_VFS_FILE_OPEN)
            return 0;
    }
    file->active = 1u;
    return 1;
}

static void
file_release(AstraVfsService *service, AstraVfsOpenFile *file)
{
    file->active = 0u;
    ++file->sequence;
    if (file->waiters != 0u && service->state_wake != NULL)
        service->state_wake(service->state_lock_context, &file->sequence);
}

static int
file_wait_idle(AstraVfsService *service, AstraVfsOpenFile *file)
{
    while (file->active != 0u) {
        uint32_t expected = file->sequence;
        int waited;

        if (service->state_wait == NULL || file->waiters == UINT16_MAX)
            return 0;
        ++file->waiters;
        waited = service->state_wait(service->state_lock_context,
                                     &file->sequence, expected);
        --file->waiters;
        if (!waited)
            return 0;
    }
    return 1;
}

static void
clear_reply(AstraVfsReply *reply, uint16_t version)
{
    memset(reply, 0, sizeof(*reply));
    reply->size = (uint16_t)ASTRA_VFS_REPLY_SIZE;
    reply->version = version;
    reply->status = ASTRA_VFS_ERR_PROTOCOL;
}

/*
 * The path is bytes off the wire, so it is not trusted to be terminated.
 * Anything unterminated is a malformed record rather than a long path,
 * because accepting it would mean reading past the record.
 *
 * Applied only to path-addressed operations. A WRITE carries payload in the
 * same bytes and must not be required to contain a NUL: a binary write of
 * exactly ASTRA_VFS_IO_MAX bytes legitimately has none.
 */
static int
bounded_path_valid(const char *path)
{
    uint32_t index;

    for (index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        if (path[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int
path_valid(const AstraVfsRequest *request)
{
    return bounded_path_valid((const char *)request->body.path);
}

static int
operation_takes_path(uint32_t operation)
{
    return operation == ASTRA_VFS_OP_OPEN ||
           operation == ASTRA_VFS_OP_READ_PATH ||
           operation == ASTRA_VFS_OP_STAT ||
           operation == ASTRA_VFS_OP_READDIR ||
           operation == ASTRA_VFS_OP_READDIR_BATCH ||
           operation == ASTRA_VFS_OP_READDIR_AREA ||
           operation == ASTRA_VFS_OP_MKDIR ||
           operation == ASTRA_VFS_OP_UNLINK ||
           operation == ASTRA_VFS_OP_RENAME_FROM ||
           operation == ASTRA_VFS_OP_RENAME_TO ||
           operation == ASTRA_VFS_OP_CHMOD ||
           operation == ASTRA_VFS_OP_READLINK ||
           operation == ASTRA_VFS_OP_SYMLINK_TARGET ||
           operation == ASTRA_VFS_OP_SYMLINK_TO ||
           operation == ASTRA_VFS_OP_RENAME ||
           operation == ASTRA_VFS_OP_SYMLINK;
}

static int
request_shape_valid(uint32_t operation, const AstraVfsRequest *request)
{
    uint16_t expected = operation == ASTRA_VFS_OP_RENAME ||
                                operation == ASTRA_VFS_OP_SYMLINK ?
        (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE :
        (uint16_t)ASTRA_VFS_REQUEST_SIZE;

    if (request->size != expected ||
        (operation_takes_path(operation) && !path_valid(request)))
        return 0;
    return (operation != ASTRA_VFS_OP_RENAME &&
            operation != ASTRA_VFS_OP_SYMLINK) ||
           bounded_path_valid((const char *)
               ((const AstraVfsRenameRequest *)request)->to);
}

/*
 * A node's metadata, cleared and copied in one place each. Five call sites
 * cleared two fields by hand, and a sixth field added later would have been
 * zeroed in three of them.
 */
static void
node_info_clear(AstraVfsNodeInfo *info)
{
    info->size = 0u;
    info->mtime = 0;
    info->uid = 0u;
    info->gid = 0u;
    info->kind = ASTRA_VFS_KIND_UNKNOWN;
    info->mode = 0u;
    info->nlink = 0u;
    info->reserved = 0u;
}

static void
node_info_publish(AstraVfsReply *reply, const AstraVfsNodeInfo *info)
{
    reply->node_size = info->size;
    reply->mtime = info->mtime;
    reply->uid = info->uid;
    reply->gid = info->gid;
    reply->kind = info->kind;
    reply->mode = info->mode;
    reply->nlink = info->nlink;
}

uint32_t
astra_vfs_backend_readdir_into(const AstraVfsBackend *backend,
                               uintptr_t directory, const char *path,
                               uint64_t cursor, uint32_t entry_limit,
                               uint8_t *buffer, uint32_t capacity,
                               uint32_t *used_out, uint64_t *next_out)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t entries = 0u;
    uint32_t used = 0u;

    if (backend == NULL || backend->ops == NULL || path == NULL ||
        !bounded_path_valid(path) ||
        entry_limit == 0u || buffer == NULL || used_out == NULL ||
        next_out == NULL || capacity < ASTRA_VFS_DIRENT_HEADER + 1u)
        return ASTRA_VFS_ERR_INVALID;
    *used_out = 0u;
    *next_out = cursor;
    while (entries < entry_limit) {
        uint64_t next = 0u;
        uint32_t length = 0u;
        uint32_t status;

        node_info_clear(&info);
        name[0] = '\0';
        status = backend->ops->readdir(
            backend->context, directory, path, cursor, name,
            (uint32_t)sizeof(name), &info, &next);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            if (entries == 0u)
                return status;
            cursor = 0u;
            break;
        }
        if (status != ASTRA_VFS_OK)
            return status;
        while (length < ASTRA_VFS_NAME_MAX && name[length] != '\0')
            ++length;
        if (length == 0u || length == ASTRA_VFS_NAME_MAX || next == cursor)
            return ASTRA_VFS_ERR_PROTOCOL;
        if (ASTRA_VFS_DIRENT_HEADER + length > capacity - used) {
            if (entries == 0u)
                return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
            break;
        }
        astra_store_be16(&buffer[used + 0u], info.kind);
        astra_store_be16(&buffer[used + 2u], info.mode);
        astra_store_be16(&buffer[used + 4u], info.nlink);
        buffer[used + 6u] = (uint8_t)length;
        buffer[used + 7u] = 0u;
        astra_store_be32(&buffer[used + 8u], info.uid);
        astra_store_be32(&buffer[used + 12u], info.gid);
        astra_store_be64(&buffer[used + 16u], info.size);
        astra_store_be64(&buffer[used + 24u], (uint64_t)info.mtime);
        used += ASTRA_VFS_DIRENT_HEADER;
        for (uint32_t index = 0u; index < length; ++index)
            buffer[used++] = (uint8_t)name[index];
        cursor = next;
        ++entries;
    }
    *used_out = used;
    *next_out = cursor;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_service_readdir_into(AstraVfsService *service, const char *path,
                               uint64_t cursor, uint32_t entry_limit,
                               uint8_t *buffer, uint32_t capacity,
                               uint32_t *used_out, uint64_t *next_out)
{
    return astra_vfs_backend_readdir_into(
        &service->backend, 0u, path, cursor, entry_limit, buffer, capacity,
        used_out, next_out);
}

static void
handle_readdir_batch(AstraVfsService *service,
                     const AstraVfsRequest *request, AstraVfsReply *reply)
{
    reply->status = astra_vfs_backend_readdir_into(
        &service->backend, 0u, (const char *)request->body.path, request->offset,
        request->length, reply->payload, ASTRA_VFS_IO_MAX, &reply->count,
        &reply->cursor);
}

static AstraVfsSessionSlot *
find_session(AstraVfsService *service, uint32_t session)
{
    uint32_t index;

    if (session == ASTRA_VFS_SESSION_INVALID) {
        return NULL;
    }
    for (index = 0u; index < service->session_capacity; ++index) {
        if (service->sessions[index].id == session) {
            return &service->sessions[index];
        }
    }
    return NULL;
}

/*
 * Resolves a file handle for one session. The two failure modes are counted
 * apart on purpose: a stale generation is a client using a handle it already
 * closed, and a cross-session hit is a client reaching for someone else's.
 * The first is a bug, the second is an attack, and a single counter would
 * hide the difference.
 */
static AstraVfsOpenFile *
find_file(AstraVfsService *service, uint32_t session, AstraVfsFile handle,
          uint32_t *status)
{
    uint32_t slot;
    AstraVfsOpenFile *file;

    *status = ASTRA_VFS_ERR_BAD_HANDLE;
    if (handle == ASTRA_VFS_FILE_INVALID ||
        (handle & HANDLE_SLOT_MASK) == 0u) {
        return NULL;
    }
    slot = HANDLE_SLOT(handle);
    if (slot >= service->file_high_water) {
        return NULL;
    }
    file = &service->files[slot];
    if (file->state != ASTRA_VFS_FILE_OPEN || file->session == 0u) {
        return NULL;
    }
    if (file->generation != HANDLE_GENERATION(handle)) {
        ++service->stats.stale_handles;
        return NULL;
    }
    if (file->session != session) {
        ++service->stats.cross_session_denied;
        *status = ASTRA_VFS_ERR_ACCESS;
        return NULL;
    }
    *status = ASTRA_VFS_OK;
    return file;
}

static void
finish_file_close(AstraVfsService *service, AstraVfsOpenFile *file)
{
    AstraVfsSessionSlot *slot = find_session(service, file->session);

    file->session = 0u;
    file->node = 0u;
    file->flags = 0u;
    file->kind = ASTRA_VFS_KIND_UNKNOWN;
    file->state = ASTRA_VFS_FILE_FREE;
    file->active = 0u;
    ++file->sequence;
    if (service->state_wake != NULL)
        service->state_wake(service->state_lock_context, &file->sequence);
    ++file->generation;
    if (file->generation == 0u)
        file->generation = 1u;
    --service->open_files;
    if (slot != NULL && slot->open_files != 0u)
        --slot->open_files;
    ++service->stats.files_closed;
}

/* Called with the state lock held; returns with it held. */
static uint32_t
close_file(AstraVfsService *service, AstraVfsOpenFile *file)
{
    uintptr_t node = file->node;
    uint32_t status;

    if (file->state != ASTRA_VFS_FILE_OPEN)
        return ASTRA_VFS_ERR_BUSY;
    file->state = ASTRA_VFS_FILE_CLOSING;
    if (!file_wait_idle(service, file)) {
        file->state = ASTRA_VFS_FILE_OPEN;
        return ASTRA_VFS_ERR_IO;
    }
    state_release(service);
    test_transition(service, ASTRA_VFS_TEST_DURING_CLOSE);
    status = service->backend.ops->close(service->backend.context, node);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    finish_file_close(service, file);
    return status;
}

int
astra_vfs_service_init(AstraVfsService *service, const AstraVfsBackendOps *ops,
                       void *context, AstraVfsSessionSlot *sessions,
                       uint32_t session_capacity, AstraVfsOpenFile *files,
                       uint32_t file_capacity)
{
    uint32_t index;

    if (service == NULL || ops == NULL || ops->open == NULL ||
        ops->close == NULL || ops->read == NULL || ops->write == NULL ||
        ops->sync == NULL || ops->truncate == NULL ||
        ops->stat == NULL || ops->readdir == NULL || ops->mkdir == NULL ||
        ops->unlink == NULL || ops->rename == NULL || ops->chmod == NULL ||
        ops->readlink == NULL || ops->symlink == NULL || sessions == NULL ||
        session_capacity == 0u ||
        session_capacity > ASTRA_VFS_SESSION_MAX || files == NULL ||
        file_capacity == 0u || file_capacity > ASTRA_VFS_FILE_HANDLE_MAX) {
        return 0;
    }
    memset(service, 0, sizeof(*service));
    service->backend.ops = ops;
    service->backend.context = context;
    service->sessions = sessions;
    service->session_capacity = session_capacity;
    service->files = files;
    service->file_capacity = file_capacity;
    service->file_high_water = 0u;
    service->next_session = 1u;
    /*
     * Generations start at 1 so a zeroed handle can never be mistaken for a
     * valid one from slot 0 generation 0.
     */
    memset(sessions, 0, session_capacity * sizeof(*sessions));
    for (index = 0u; index < session_capacity; ++index) {
        service->sessions[index].generation = 1u;
    }
    return 1;
}

int
astra_vfs_service_set_state_lock(AstraVfsService *service,
                                 AstraVfsStateAcquire acquire,
                                 AstraVfsStateRelease release, void *context)
{
    if (service == NULL || acquire == NULL || release == NULL ||
        service->state_acquire != NULL || service->state_release != NULL)
        return 0;
    service->state_acquire = acquire;
    service->state_release = release;
    service->state_lock_context = context;
    return 1;
}

int
astra_vfs_service_set_state_wait(AstraVfsService *service,
                                 AstraVfsStateWait wait,
                                 AstraVfsStateWake wake)
{
    if (service == NULL || wait == NULL || wake == NULL ||
        service->state_acquire == NULL || service->state_release == NULL ||
        service->state_wait != NULL || service->state_wake != NULL)
        return 0;
    service->state_wait = wait;
    service->state_wake = wake;
    return 1;
}

#ifdef ASTRA_VFS_TEST_HOOKS
void
astra_vfs_service_set_test_transition(AstraVfsService *service,
                                      AstraVfsTestTransition transition,
                                      void *context)
{
    if (service == NULL)
        return;
    service->test_transition = transition;
    service->test_transition_context = context;
}
#endif

static void
handle_hello(AstraVfsService *service, uint32_t owner,
             const AstraVfsRequest *request,
             AstraVfsReply *reply)
{
    uint32_t index;
    uint32_t owner_sessions = 0u;
    uint32_t owner_limit = service->session_capacity /
                           ASTRA_PROCESS_COUNT_MAX;

    /*
     * The client sends the newest version it can speak. Agreeing on the lower
     * of the two is what lets an old client keep working against a new service
     * -- and refusing when the ranges do not overlap is what stops a silent
     * downgrade into a protocol neither side actually implements.
     */
    if (request->version < ASTRA_VFS_VERSION_MIN) {
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        return;
    }
    reply->version = request->version < ASTRA_VFS_VERSION ?
        request->version : ASTRA_VFS_VERSION;

    if (owner_limit == 0u)
        owner_limit = 1u;
    if (owner != 0u) {
        for (index = 0u; index < service->session_capacity; ++index)
            if (service->sessions[index].id != 0u &&
                service->sessions[index].owner == owner)
                ++owner_sessions;
        if (owner_sessions >= owner_limit) {
            ++service->stats.owner_session_quota_denied;
            reply->status = ASTRA_VFS_ERR_LIMIT;
            return;
        }
    }

    for (index = 0u; index < service->session_capacity; ++index) {
        if (service->sessions[index].id == 0u) {
            service->sessions[index].id = service->next_session++;
            service->sessions[index].owner = owner;
            service->sessions[index].open_files = 0u;
            service->sessions[index].version = reply->version;
            service->sessions[index].staged_operation = ASTRA_VFS_STAGE_NONE;
            service->sessions[index].inflight = 0u;
            service->sessions[index].closing = 0u;
            if (service->next_session == 0u) {
                service->next_session = 1u;
            }
            reply->session = service->sessions[index].id;
            reply->status = ASTRA_VFS_OK;
            ++service->open_sessions;
            ++service->stats.sessions_opened;
            if (service->open_sessions > service->stats.peak_sessions) {
                service->stats.peak_sessions = service->open_sessions;
            }
            return;
        }
    }
    reply->status = ASTRA_VFS_ERR_LIMIT;
}

/* Called with the state lock held; returns with it held. */
static void
reap_session(AstraVfsService *service, AstraVfsSessionSlot *slot)
{
    uint32_t session = slot->id;

    for (;;) {
        AstraVfsOpenFile *file = NULL;

        for (uint32_t index = 0u; index < service->file_high_water; ++index) {
            if (service->files[index].session == session &&
                service->files[index].state == ASTRA_VFS_FILE_OPEN) {
                file = &service->files[index];
                break;
            }
        }
        if (file == NULL)
            break;
        (void)close_file(service, file);
    }
    if (slot->open_files != 0u)
        return;
    slot->id = 0u;
    slot->owner = 0u;
    slot->open_files = 0u;
    slot->staged_operation = ASTRA_VFS_STAGE_NONE;
    slot->inflight = 0u;
    slot->closing = 0u;
    ++slot->generation;
    --service->open_sessions;
    ++service->stats.sessions_closed;
}

static void
finish_session_request(AstraVfsService *service, AstraVfsSessionSlot *slot)
{
    if (slot->inflight != 0u)
        --slot->inflight;
    if (slot->closing != 0u && slot->inflight == 0u)
        reap_session(service, slot);
}

static AstraVfsSessionSlot *
begin_session_request(AstraVfsService *service, uint32_t session,
                      uint32_t *status)
{
    AstraVfsSessionSlot *slot = find_session(service, session);

    if (slot == NULL) {
        *status = ASTRA_VFS_ERR_BAD_HANDLE;
        return NULL;
    }
    if (slot->closing != 0u || slot->inflight == UINT16_MAX ||
        (slot->version < UINT16_C(19) && slot->inflight != 0u)) {
        *status = ASTRA_VFS_ERR_BUSY;
        return NULL;
    }
    ++slot->inflight;
    test_transition(service, ASTRA_VFS_TEST_AFTER_SESSION_RESERVE);
    *status = ASTRA_VFS_OK;
    return slot;
}

uint32_t
astra_vfs_service_readdir_file_into(
    AstraVfsService *service, uint32_t session, AstraVfsFile directory,
    const char *path, uint64_t cursor, uint32_t entry_limit, uint8_t *buffer,
    uint32_t capacity, uint32_t *used, uint64_t *next)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || !state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    slot = begin_session_request(service, session, &status);
    if (slot == NULL) {
        state_release(service);
        return status;
    }
    file = find_file(service, session, directory, &status);
    if (file == NULL || file->kind != ASTRA_VFS_KIND_DIRECTORY) {
        if (file != NULL)
            status = ASTRA_VFS_ERR_NOT_DIR;
        finish_session_request(service, slot);
        state_release(service);
        return status;
    }
    if (!file_acquire(service, file)) {
        finish_session_request(service, slot);
        state_release(service);
        return ASTRA_VFS_ERR_IO;
    }
    state_release(service);
    status = astra_vfs_backend_readdir_into(
        &service->backend, file->node, path, cursor, entry_limit, buffer,
        capacity, used, next);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

void
astra_vfs_service_release_session(AstraVfsService *service, uint32_t session)
{
    AstraVfsSessionSlot *slot = NULL;

    if (service == NULL || !state_acquire(service))
        return;
    slot = find_session(service, session);
    if (slot == NULL) {
        state_release(service);
        return;
    }
    slot->closing = 1u;
    if (slot->inflight == 0u) {
        reap_session(service, slot);
    }
    state_release(service);
}

static void
handle_open(AstraVfsService *service, uint32_t session,
            const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsSessionSlot *session_slot = find_session(service, session);
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t index;
    uint32_t status;
    uint32_t owner_files = 0u;
    uint32_t owner_limit = service->file_capacity / ASTRA_PROCESS_COUNT_MAX;

    if (session_slot == NULL) {
        reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
        return;
    }
    if (owner_limit == 0u)
        owner_limit = 1u;
    if (session_slot->owner == 0u)
        owner_limit = service->file_capacity;
    for (uint32_t slot = 0u; slot < service->session_capacity; ++slot)
        if (service->sessions[slot].id != 0u &&
            service->sessions[slot].owner == session_slot->owner)
            owner_files += service->sessions[slot].open_files;
    if (owner_files >= owner_limit) {
        ++service->stats.owner_quota_denied;
        reply->status = ASTRA_VFS_ERR_LIMIT;
        return;
    }

    if ((request->flags & ~(ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                            ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
                            ASTRA_VFS_OPEN_DIRECTORY |
                            ASTRA_VFS_OPEN_EXCLUSIVE |
                            ASTRA_VFS_OPEN_APPEND)) != 0u ||
        ((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
         (request->flags & ASTRA_VFS_OPEN_CREATE) == 0u)) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    if (session_slot->version >= UINT16_C(14) &&
        (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u &&
        request->offset != ASTRA_VFS_MODE_DEFAULT &&
        request->offset > ASTRA_VFS_MODE_MASK) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    /* A slot is found before the backend is asked, so a full table costs no
     * open that would then have to be undone. */
    for (index = 0u; index < service->file_high_water; ++index) {
        if (service->files[index].state == ASTRA_VFS_FILE_FREE) {
            break;
        }
    }
    if (index == service->file_high_water) {
        if (service->file_high_water == service->file_capacity) {
            reply->status = ASTRA_VFS_ERR_LIMIT;
            return;
        }
        ++service->file_high_water;
        service->files[index].node = 0u;
        service->files[index].session = 0u;
        service->files[index].flags = 0u;
        service->files[index].sequence = 0u;
        service->files[index].generation = 1u;
        service->files[index].kind = ASTRA_VFS_KIND_UNKNOWN;
        service->files[index].state = ASTRA_VFS_FILE_FREE;
        service->files[index].active = 0u;
    }
    if (index >= service->file_capacity) {
        reply->status = ASTRA_VFS_ERR_LIMIT;
        return;
    }

    service->files[index].session = session;
    service->files[index].state = ASTRA_VFS_FILE_OPENING;
    test_transition(service, ASTRA_VFS_TEST_AFTER_SESSION_RESERVE);
    ++service->open_files;
    ++session_slot->open_files;

    node_info_clear(&info);
    state_release(service);
    status = service->backend.ops->open(service->backend.context,
                                        (const char *)request->body.path,
                                        request->flags,
                                        session_slot->version >= UINT16_C(14) &&
                                        (request->flags &
                                         ASTRA_VFS_OPEN_CREATE) != 0u ?
                                            (uint16_t)request->offset :
                                            ASTRA_VFS_MODE_DEFAULT,
                                        &node, &info);
    if (!state_acquire(service)) {
        reply->status = ASTRA_VFS_ERR_IO;
        return;
    }
    if (status != ASTRA_VFS_OK) {
        service->files[index].session = 0u;
        service->files[index].state = ASTRA_VFS_FILE_FREE;
        --service->open_files;
        --session_slot->open_files;
        reply->status = status;
        return;
    }
    service->files[index].node = node;
    service->files[index].session = session;
    service->files[index].flags = request->flags;
    service->files[index].kind = info.kind;
    service->files[index].state = ASTRA_VFS_FILE_OPEN;
    reply->file = HANDLE_MAKE(index, service->files[index].generation);
    node_info_publish(reply, &info);
    reply->status = ASTRA_VFS_OK;
    ++service->stats.files_opened;
    if (service->open_files > service->stats.peak_open_files) {
        service->stats.peak_open_files = service->open_files;
    }
}

static uint32_t
read_file(AstraVfsService *service, AstraVfsOpenFile *file, uint64_t offset,
          void *buffer, uint32_t length, uint32_t *moved)
{
    uint32_t status;

    *moved = 0u;
    if ((file->flags & ASTRA_VFS_OPEN_READ) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire(service, file))
        return ASTRA_VFS_ERR_IO;
    state_release(service);
    status = service->backend.ops->read(service->backend.context, file->node,
                                        offset, buffer, length, moved);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    if (status == ASTRA_VFS_OK && *moved > length)
        *moved = length;
    return status;
}

static void
handle_read(AstraVfsService *service, AstraVfsOpenFile *file,
            const AstraVfsRequest *request, AstraVfsReply *reply)
{
    uint32_t length = request->length > ASTRA_VFS_IO_MAX ?
        ASTRA_VFS_IO_MAX : request->length;

    reply->status = read_file(service, file, request->offset, reply->payload,
                              length, &reply->count);
}

uint32_t
astra_vfs_backend_read_path(const AstraVfsBackend *backend, const char *path,
                            void *buffer, uint32_t capacity, uint32_t *moved,
                            uint64_t *node_size)
{
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t status;

    if (backend == NULL || backend->ops == NULL || path == NULL ||
        buffer == NULL || moved == NULL ||
        node_size == NULL || capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    *node_size = 0u;
    node_info_clear(&info);
    status = backend->ops->open(backend->context, path, ASTRA_VFS_OPEN_READ,
                                ASTRA_VFS_MODE_DEFAULT, &node, &info);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *node_size = info.size;
    if (info.kind == ASTRA_VFS_KIND_DIRECTORY) {
        status = ASTRA_VFS_ERR_IS_DIR;
    } else if (info.size > capacity) {
        status = ASTRA_VFS_ERR_LIMIT;
    } else {
        uint32_t total = 0u;

        while (total < (uint32_t)info.size) {
            uint32_t chunk = 0u;

            status = backend->ops->read(
                backend->context, node, total,
                (uint8_t *)buffer + total, (uint32_t)info.size - total,
                &chunk);
            if (status != ASTRA_VFS_OK || chunk == 0u) {
                break;
            }
            total += chunk;
        }
        if (status == ASTRA_VFS_OK && total != (uint32_t)info.size) {
            status = ASTRA_VFS_ERR_IO;
        }
        *moved = total;
    }
    (void)backend->ops->close(backend->context, node);
    return status;
}

uint32_t
astra_vfs_service_read_path(AstraVfsService *service, const char *path,
                            void *buffer, uint32_t capacity, uint32_t *moved,
                            uint64_t *node_size)
{
    return astra_vfs_backend_read_path(&service->backend, path, buffer,
                                       capacity, moved, node_size);
}

static uint32_t
read_into_unlocked(AstraVfsService *service, uint32_t session,
                   AstraVfsFile handle, uint64_t offset, void *buffer,
                   uint32_t length, uint32_t *moved)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || buffer == NULL || moved == NULL || length == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    slot = find_session(service, session);
    if (slot == NULL) {
        ++service->stats.protocol_rejects;
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    file = find_file(service, slot->id, handle, &status);
    if (file == NULL) {
        return status;
    }
    return read_file(service, file, offset, buffer, length, moved);
}

uint32_t
astra_vfs_service_read_into(AstraVfsService *service, uint32_t session,
                            AstraVfsFile handle, uint64_t offset,
                            void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsSessionSlot *slot;
    uint32_t status;

    if (service == NULL || !state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    slot = begin_session_request(service, session, &status);
    if (slot == NULL) {
        state_release(service);
        return status;
    }
    status = read_into_unlocked(service, session, handle, offset, buffer,
                                length, moved);
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

static uint32_t
write_file(AstraVfsService *service, AstraVfsOpenFile *file, uint64_t offset,
           const void *buffer, uint32_t length, uint32_t *moved,
           uint64_t *position)
{
    uint32_t status;

    *moved = 0u;
    *position = offset;
    if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire(service, file))
        return ASTRA_VFS_ERR_IO;
    state_release(service);
    status = service->backend.ops->write(service->backend.context, file->node,
                                         offset, file->flags, buffer, length,
                                         moved, position);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    if (status == ASTRA_VFS_OK && *moved > length)
        *moved = length;
    return status;
}

static uint32_t
write_from_unlocked(AstraVfsService *service, uint32_t session,
                    AstraVfsFile handle, uint64_t offset, const void *buffer,
                    uint32_t length, uint32_t *moved, uint64_t *position)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || buffer == NULL || moved == NULL ||
        position == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    slot = find_session(service, session);
    if (slot == NULL) {
        ++service->stats.protocol_rejects;
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    file = find_file(service, slot->id, handle, &status);
    if (file == NULL)
        return status;
    return write_file(service, file, offset, buffer, length, moved, position);
}

uint32_t
astra_vfs_service_write_position_from(
    AstraVfsService *service, uint32_t session, AstraVfsFile handle,
    uint64_t offset, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position)
{
    AstraVfsSessionSlot *slot;
    uint32_t status;

    if (service == NULL || !state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    slot = begin_session_request(service, session, &status);
    if (slot == NULL) {
        state_release(service);
        return status;
    }
    status = write_from_unlocked(service, session, handle, offset, buffer,
                                 length, moved, position);
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

uint32_t
astra_vfs_service_write_from(AstraVfsService *service, uint32_t session,
                             AstraVfsFile handle, uint64_t offset,
                             const void *buffer, uint32_t length,
                             uint32_t *moved)
{
    uint64_t position = offset;

    return astra_vfs_service_write_position_from(
        service, session, handle, offset, buffer, length, moved, &position);
}

static void
handle_write(AstraVfsService *service, AstraVfsOpenFile *file,
             const AstraVfsRequest *request, AstraVfsReply *reply)
{
    uint32_t moved = 0u;
    uint64_t position = request->offset;

    /*
     * A write is refused rather than clamped, unlike a read: the payload
     * travels in the request, so a length past the record is a malformed
     * record and not an ambitious caller.
     */
    if (request->length > ASTRA_VFS_IO_MAX) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    reply->status = write_file(service, file, request->offset,
                               request->body.payload, request->length,
                               &moved, &position);
    if (reply->status == ASTRA_VFS_OK) {
        reply->count = moved;
        reply->node_size = position;
    }
}

static void
handle_readdir(AstraVfsService *service, const AstraVfsRequest *request,
               AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t index;

    node_info_clear(&info);
    name[0] = '\0';
    reply->status = service->backend.ops->readdir(
        service->backend.context, 0u, (const char *)request->body.path,
        request->offset, name, (uint32_t)sizeof(name), &info, &next);
    if (reply->status != ASTRA_VFS_OK) {
        return;
    }
    /*
     * Handed back unread. The cursor is the backend's own and the core has no
     * business knowing what it counts -- which is what keeps a scan stateless
     * here and costs nothing when a client walks away mid-listing.
     */
    reply->cursor = next;
    for (index = 0u; index < ASTRA_VFS_NAME_MAX && name[index] != '\0';
         ++index) {
        reply->payload[index] = (uint8_t)name[index];
    }
    reply->payload[index] = 0u;
    reply->count = index;
    node_info_publish(reply, &info);
}

static void
dispatch_from_unlocked(AstraVfsService *service, uint32_t owner,
                       uint32_t operation, const AstraVfsRequest *request,
                       AstraVfsReply *reply)
{
    AstraVfsSessionSlot *slot = NULL;
    AstraVfsOpenFile *file;
    uint32_t status;
    int reserved = 0;

    if (service == NULL || reply == NULL) {
        return;
    }
    clear_reply(reply, ASTRA_VFS_VERSION);
    if (request == NULL) {
        ++service->stats.protocol_rejects;
        return;
    }
    ++service->stats.requests;

    /*
     * Shape before meaning. A record whose own size is wrong came from a
     * different build of this protocol, and answering it as though the fields
     * meant what this build thinks they mean is how a version skew turns into
     * data loss instead of an error.
     *
     * The activity is not checked. It is the caller's account of what it was
     * doing, any value is as valid as any other, and a service that refused a
     * request over one would be refusing work because of a log field.
     */
    if (operation == 0u || operation > ASTRA_VFS_OP_MAX ||
        !request_shape_valid(operation, request)) {
        ++service->stats.protocol_rejects;
        goto done;
    }

    if (operation == ASTRA_VFS_OP_HELLO) {
        handle_hello(service, owner, request, reply);
        goto done;
    }

    slot = find_session(service, request->session);
    if (slot == NULL) {
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
        goto done;
    }
    if (slot->owner != owner) {
        ++service->stats.cross_owner_denied;
        reply->status = ASTRA_VFS_ERR_ACCESS;
        goto done;
    }
    if (slot->closing != 0u || slot->inflight == UINT16_MAX ||
        (slot->version < UINT16_C(19) && slot->inflight != 0u)) {
        reply->status = ASTRA_VFS_ERR_BUSY;
        goto done;
    }
    ++slot->inflight;
    reserved = 1;
    test_transition(service, ASTRA_VFS_TEST_AFTER_SESSION_RESERVE);
    reply->version = slot->version;
    reply->session = slot->id;
    if (operation != ASTRA_VFS_OP_RENAME_FROM &&
        operation != ASTRA_VFS_OP_RENAME_TO &&
        operation != ASTRA_VFS_OP_SYMLINK_TARGET &&
        operation != ASTRA_VFS_OP_SYMLINK_TO)
        slot->staged_operation = ASTRA_VFS_STAGE_NONE;

    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        slot->closing = 1u;
        reply->session = ASTRA_VFS_SESSION_INVALID;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_OPEN:
        if (((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
             slot->version < UINT16_C(12)) ||
            ((request->flags & ASTRA_VFS_OPEN_APPEND) != 0u &&
             slot->version < UINT16_C(13)))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else
            handle_open(service, slot->id, request, reply);
        break;
    case ASTRA_VFS_OP_CLOSE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        reply->status = close_file(service, file);
        break;
    case ASTRA_VFS_OP_READ:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        handle_read(service, file, request, reply);
        break;
    case ASTRA_VFS_OP_WRITE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        handle_write(service, file, request, reply);
        break;
    case ASTRA_VFS_OP_SYNC:
        if (slot->version < UINT16_C(13)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
        } else if (!file_acquire(service, file)) {
            reply->status = ASTRA_VFS_ERR_IO;
        } else {
            state_release(service);
            reply->status = service->backend.ops->sync(
                service->backend.context, file->node);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_TRUNCATE:
        if (slot->version < UINT16_C(13)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
        } else if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u) {
            reply->status = ASTRA_VFS_ERR_ACCESS;
        } else if (file->kind == ASTRA_VFS_KIND_DIRECTORY) {
            reply->status = ASTRA_VFS_ERR_IS_DIR;
        } else if (!file_acquire(service, file)) {
            reply->status = ASTRA_VFS_ERR_IO;
        } else {
            state_release(service);
            reply->status = service->backend.ops->truncate(
                service->backend.context, file->node, request->offset);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info;

        node_info_clear(&info);
        state_release(service);
        reply->status = service->backend.ops->stat(
            service->backend.context, (const char *)request->body.path, &info);
        (void)state_acquire(service);
        if (reply->status == ASTRA_VFS_OK) {
            reply->node_size = info.size;
            reply->kind = info.kind;
        }
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        state_release(service);
        handle_readdir(service, request, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
        if (slot->version < UINT16_C(4)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        state_release(service);
        handle_readdir_batch(service, request, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_READDIR_AREA:
        reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        break;
    case ASTRA_VFS_OP_MKDIR:
        if (slot->version >= UINT16_C(14) &&
            request->offset != ASTRA_VFS_MODE_DEFAULT &&
            request->offset > ASTRA_VFS_MODE_MASK) {
            reply->status = ASTRA_VFS_ERR_INVALID;
        } else {
            state_release(service);
            reply->status = service->backend.ops->mkdir(
                service->backend.context,
                (const char *)request->body.path,
                slot->version >= UINT16_C(14) ?
                    (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT);
            (void)state_acquire(service);
        }
        break;
    case ASTRA_VFS_OP_UNLINK:
        state_release(service);
        reply->status = service->backend.ops->unlink(
            service->backend.context, (const char *)request->body.path);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_RENAME_FROM:
        if (slot->version < UINT16_C(11)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        for (uint32_t index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
            slot->staged_path[index] = (char)request->body.path[index];
            if (request->body.path[index] == 0u)
                break;
        }
        slot->staged_operation = ASTRA_VFS_STAGE_RENAME;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_RENAME_TO:
        if (slot->version < UINT16_C(11)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        if (slot->staged_operation != ASTRA_VFS_STAGE_RENAME) {
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
            break;
        }
        slot->staged_operation = ASTRA_VFS_STAGE_NONE;
        state_release(service);
        reply->status = service->backend.ops->rename(
            service->backend.context, slot->staged_path,
            (const char *)request->body.path);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_RENAME: {
        const AstraVfsRenameRequest *rename =
            (const AstraVfsRenameRequest *)request;

        if (slot->version < UINT16_C(16)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        state_release(service);
        reply->status = service->backend.ops->rename(
            service->backend.context,
            (const char *)rename->request.body.path,
            (const char *)rename->to);
        (void)state_acquire(service);
        break;
    }
    case ASTRA_VFS_OP_CHMOD:
        if (slot->version < UINT16_C(14)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        } else if (request->offset > ASTRA_VFS_MODE_MASK) {
            reply->status = ASTRA_VFS_ERR_INVALID;
        } else {
            state_release(service);
            reply->status = service->backend.ops->chmod(
                service->backend.context,
                (const char *)request->body.path,
                (uint16_t)request->offset);
            (void)state_acquire(service);
        }
        break;
    case ASTRA_VFS_OP_READLINK:
        if (slot->version < UINT16_C(14)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        } else {
            uint32_t length = 0u;
            uint32_t capacity = request->length > ASTRA_VFS_IO_MAX ?
                ASTRA_VFS_IO_MAX : request->length;

            state_release(service);
            reply->status = service->backend.ops->readlink(
                service->backend.context,
                (const char *)request->body.path, reply->payload, capacity,
                &length);
            (void)state_acquire(service);
            if (reply->status == ASTRA_VFS_OK) {
                if (length > capacity)
                    reply->status = ASTRA_VFS_ERR_PROTOCOL;
                else
                    reply->count = length;
            }
        }
        break;
    case ASTRA_VFS_OP_SYMLINK_TARGET:
        if (slot->version < UINT16_C(15) || request->body.path[0] == 0u) {
            reply->status = slot->version < UINT16_C(15) ?
                ASTRA_VFS_ERR_UNSUPPORTED : ASTRA_VFS_ERR_INVALID;
            break;
        }
        for (uint32_t index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
            slot->staged_path[index] = (char)request->body.path[index];
            if (request->body.path[index] == 0u)
                break;
        }
        slot->staged_operation = ASTRA_VFS_STAGE_SYMLINK;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_SYMLINK_TO:
        if (slot->version < UINT16_C(15)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        if (slot->staged_operation != ASTRA_VFS_STAGE_SYMLINK) {
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
            break;
        }
        slot->staged_operation = ASTRA_VFS_STAGE_NONE;
        state_release(service);
        reply->status = service->backend.ops->symlink(
            service->backend.context, slot->staged_path,
            (const char *)request->body.path);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_SYMLINK: {
        const AstraVfsRenameRequest *symlink =
            (const AstraVfsRenameRequest *)request;

        if (slot->version < UINT16_C(19)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        state_release(service);
        reply->status = service->backend.ops->symlink(
            service->backend.context,
            (const char *)symlink->request.body.path,
            (const char *)symlink->to);
        (void)state_acquire(service);
        break;
    }
    default:
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }

done:
    test_transition(service, ASTRA_VFS_TEST_BEFORE_REPLY);
    if (reply->status != ASTRA_VFS_OK) {
        ++service->stats.replies_failed;
    }
    if (reserved) {
        finish_session_request(service, slot);
    }
}

void
astra_vfs_service_dispatch_from(AstraVfsService *service, uint32_t owner,
                                uint32_t operation,
                                const AstraVfsRequest *request,
                                AstraVfsReply *reply)
{
    if (service == NULL || reply == NULL)
        return;
    if (!state_acquire(service)) {
        clear_reply(reply, ASTRA_VFS_VERSION);
        reply->status = ASTRA_VFS_ERR_IO;
        return;
    }
    dispatch_from_unlocked(service, owner, operation, request, reply);
    state_release(service);
}

void
astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                           const AstraVfsRequest *request,
                           AstraVfsReply *reply)
{
    astra_vfs_service_dispatch_from(service, 0u, operation, request, reply);
}

int
astra_vfs_service_session_owned(const AstraVfsService *service,
                                uint32_t session, uint32_t owner)
{
    int owned = 0;
    AstraVfsService *mutable = (AstraVfsService *)(uintptr_t)service;

    if (service == NULL || session == ASTRA_VFS_SESSION_INVALID)
        return 0;
    if (!state_acquire(mutable))
        return 0;
    for (uint32_t index = 0u; index < service->session_capacity; ++index)
        if (service->sessions[index].id == session) {
            owned = service->sessions[index].owner == owner;
            break;
        }
    state_release(mutable);
    return owned;
}

int
astra_vfs_service_session_capacity_reached(const AstraVfsService *service)
{
    int full;
    AstraVfsService *mutable = (AstraVfsService *)(uintptr_t)service;

    if (service == NULL || !state_acquire(mutable))
        return 0;
    full = service->open_sessions == service->session_capacity;
    state_release(mutable);
    return full;
}

const AstraVfsServiceStats *
astra_vfs_service_stats(const AstraVfsService *service)
{
    return service != NULL ? &service->stats : NULL;
}
