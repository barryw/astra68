/*
 * The storage service core.
 *
 * Everything here is validation, handle bookkeeping and dispatch. The bytes
 * belong to a backend and the transport belongs to a caller; see
 * astra/vfs_backend.h and astra/vfs_service_core.h for why the three are apart.
 */

#include <astra/vfs_service_core.h>

#include <astra/endian.h>
#include <astra/utf8.h>

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
 * A node's metadata, cleared and copied in one place each. Five call sites
 * cleared two fields by hand, and a sixth field added later would have been
 * zeroed in three of them.
 */
static void
node_info_clear(AstraVfsNodeInfo *info)
{
    memset(info, 0, sizeof(*info));
}

uint32_t
astra_vfs_backend_filesystem_info_into(const AstraVfsBackend *backend,
                                       uintptr_t node, const char *path,
                                       void *buffer, uint32_t capacity)
{
    AstraVfsFilesystemInfo info = {0};
    uint8_t *payload = buffer;
    uint32_t status;

    if (backend == NULL || backend->ops == NULL ||
        backend->ops->filesystem_info == NULL || buffer == NULL ||
        capacity < ASTRA_VFS_FILESYSTEM_INFO_SIZE ||
        ((node == 0u) == (path == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    status = backend->ops->filesystem_info(backend->context, node, path,
                                           &info);
    if (status != ASTRA_VFS_OK)
        return status;
    if (info.size != sizeof(info) || info.reserved != 0u)
        return ASTRA_VFS_ERR_PROTOCOL;

    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, size),
                     info.size);
    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, flags),
                     info.flags);
    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, block_size),
                     info.block_size);
    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, fragment_size),
                     info.fragment_size);
    astra_store_be64(payload + offsetof(AstraVfsFilesystemInfo, blocks),
                     info.blocks);
    astra_store_be64(payload + offsetof(AstraVfsFilesystemInfo, blocks_free),
                     info.blocks_free);
    astra_store_be64(
        payload + offsetof(AstraVfsFilesystemInfo, blocks_available),
        info.blocks_available);
    astra_store_be64(payload + offsetof(AstraVfsFilesystemInfo, files),
                     info.files);
    astra_store_be64(payload + offsetof(AstraVfsFilesystemInfo, files_free),
                     info.files_free);
    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, name_max),
                     info.name_max);
    astra_store_be32(payload + offsetof(AstraVfsFilesystemInfo, reserved),
                     0u);
    return ASTRA_VFS_OK;
}

/* Root and handle-relative requests share validation and backend dispatch. */
uint32_t astra_vfs_backend_path_operation(const AstraVfsBackend *backend,
    uint32_t operation, const AstraVfsRequest *request,
    const AstraVfsTransfer *transfer, uintptr_t base,
    uintptr_t second_base, AstraVfsReply *reply)
{
    const AstraVfsBackendOps *ops = backend->ops;
    AstraVfsPathOperands paths;
    uint32_t status = astra_vfs_path_request(operation, request, transfer, &paths);
    const char *path;
    const char *to;
    uint32_t capacity = request->length;

    if (status != ASTRA_VFS_OK)
        return status;
    path = paths.first == NULL ? "" : paths.first;
    to = paths.second;
    status = ASTRA_VFS_ERR_UNSUPPORTED;
    if ((operation != ASTRA_VFS_OP_LOOKUP &&
         operation != ASTRA_VFS_OP_RENAME && request->flags != 0u) ||
        (operation != ASTRA_VFS_OP_DIRECTORY_PATH && base != 0u &&
         ((operation == ASTRA_VFS_OP_SYMLINK ? to : path)[0] == '\0' ||
          (operation == ASTRA_VFS_OP_SYMLINK ? to : path)[0] == '/')) ||
        ((operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK) &&
         second_base != 0u && (to[0] == '\0' || to[0] == '/')))
        return ASTRA_VFS_ERR_INVALID;
    switch (operation) {
    case ASTRA_VFS_OP_LOOKUP: {
        AstraVfsNodeInfo info = {0};
        AstraVfsEscape escape = {0};
        uint8_t *output = transfer == NULL ? NULL : transfer->output;
        if (output == NULL || transfer->output_capacity < ASTRA_VFS_ESCAPE_HEADER)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        escape.path = output + ASTRA_VFS_ESCAPE_HEADER;
        escape.capacity = transfer->output_capacity - ASTRA_VFS_ESCAPE_HEADER;
        if (escape.capacity > ASTRA_VFS_ESCAPE_PATH_MAX)
            escape.capacity = ASTRA_VFS_ESCAPE_PATH_MAX;

        if (request->length > ASTRA_VFS_LINK_MAX ||
            (request->flags & ~(ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_MISSING |
                                 ASTRA_VFS_LOOKUP_ENTRY)) != 0u ||
            (request->flags & (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY)) ==
                (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY))
            return ASTRA_VFS_ERR_INVALID;
        if (ops->lookup == NULL)
            return ASTRA_VFS_ERR_UNSUPPORTED;
        status = ops->lookup(backend->context, base, path, request->flags,
                             request->length, &info, &escape);
        if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_ESCAPED) {
            if (escape.length > escape.capacity)
                return ASTRA_VFS_ERR_PROTOCOL;
            reply->count = ASTRA_VFS_ESCAPE_HEADER + escape.length;
            astra_store_be16(output, escape.kind);
            astra_store_be16(output + 2u, escape.length);
            astra_store_be32(output + 4u, escape.consumed);
            astra_store_be32(output + 8u, escape.links);
            if (status == ASTRA_VFS_OK)
                astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_MKDIR:
        if (request->offset != ASTRA_VFS_MODE_DEFAULT && request->offset > ASTRA_VFS_MODE_MASK)
            return ASTRA_VFS_ERR_INVALID;
        if (base != 0u) {
            if (ops->mkdir_at != NULL)
                status = ops->mkdir_at(backend->context, base, path, request->offset);
        } else
            status = ops->mkdir(backend->context, path, request->offset);
        break;
    case ASTRA_VFS_OP_RENAME:
    case ASTRA_VFS_OP_LINK:
        if (operation == ASTRA_VFS_OP_RENAME &&
            (request->flags & ~ASTRA_VFS_RENAME_NOREPLACE) != 0u)
            return ASTRA_VFS_ERR_INVALID;
        if (operation == ASTRA_VFS_OP_LINK && request->flags != 0u)
            return ASTRA_VFS_ERR_INVALID;
        if (base != 0u || second_base != 0u) {
            if (operation == ASTRA_VFS_OP_RENAME) {
                if ((request->flags & ASTRA_VFS_RENAME_NOREPLACE) != 0u) {
                    if (ops->rename_noreplace_at != NULL)
                        status = ops->rename_noreplace_at(backend->context,
                            base, path, second_base, to);
                } else if (ops->rename_at != NULL)
                    status = ops->rename_at(backend->context, base, path,
                        second_base, to);
            }
            if (operation == ASTRA_VFS_OP_LINK && ops->link_at != NULL)
                status = ops->link_at(backend->context, base, path, second_base, to);
        } else if (operation == ASTRA_VFS_OP_RENAME)
            status = (request->flags & ASTRA_VFS_RENAME_NOREPLACE) != 0u ?
                ops->rename_noreplace != NULL ?
                    ops->rename_noreplace(backend->context, path, to) :
                    ASTRA_VFS_ERR_UNSUPPORTED :
                ops->rename(backend->context, path, to);
        else
            status = ops->link(backend->context, path, to);
        break;
    case ASTRA_VFS_OP_SYMLINK:
        if (path[0] == '\0')
            return ASTRA_VFS_ERR_INVALID;
        if (base != 0u) {
            if (ops->symlink_at != NULL)
                status = ops->symlink_at(backend->context, path, base, to);
        } else
            status = ops->symlink(backend->context, path, to);
        break;
    case ASTRA_VFS_OP_READLINK:
    case ASTRA_VFS_OP_DIRECTORY_PATH:
        if (capacity == 0u || capacity > ASTRA_VFS_PATH_MAX - 1u ||
            transfer == NULL || transfer->output == NULL ||
            capacity > transfer->output_capacity)
            return ASTRA_VFS_ERR_INVALID;
        if (operation == ASTRA_VFS_OP_DIRECTORY_PATH) {
            if (base == 0u || path[0] != '\0')
                return ASTRA_VFS_ERR_INVALID;
            if (ops->directory_path != NULL)
                status = ops->directory_path(backend->context, base, transfer->output,
                                              capacity, &reply->count);
        } else if (base != 0u) {
            if (ops->readlink_at != NULL)
                status = ops->readlink_at(backend->context, base, path, transfer->output,
                                         capacity, &reply->count);
        } else
            status = ops->readlink(backend->context, path, transfer->output, capacity, &reply->count);
        if (status == ASTRA_VFS_OK &&
            (reply->count > capacity || !astra_utf8_validate(transfer->output, reply->count, 0u)))
            status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    default:
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    return status;
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
        astra_vfs_path_measure(path, &(uint32_t){0}) != ASTRA_VFS_OK ||
        entry_limit == 0u || buffer == NULL || used_out == NULL ||
        next_out == NULL || capacity < ASTRA_VFS_DIRENT_HEADER + 1u)
        return ASTRA_VFS_ERR_INVALID;
    *used_out = 0u;
    *next_out = cursor;
    if (backend->ops->readdir_batch != NULL)
        return backend->ops->readdir_batch(backend->context, directory, path,
            cursor, entry_limit, buffer, capacity, used_out, next_out);
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
        if (length == 0u || length == ASTRA_VFS_NAME_MAX || next == cursor ||
            !astra_utf8_validate(name, length, 0u))
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
        if (cursor == 0u)
            break;
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

static void handle_readdir_batch(AstraVfsService *service,
    const AstraVfsRequest *request, const char *path,
    const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    if (request->file != ASTRA_VFS_FILE_INVALID)
        reply->status = astra_vfs_service_readdir_file_into(service,
            request->session, request->file, path, request->offset,
            request->length, transfer == NULL ? NULL : transfer->output,
            transfer == NULL ? 0u : transfer->output_capacity,
            &reply->count, &reply->cursor);
    else
        reply->status = astra_vfs_backend_readdir_into(&service->backend, 0u,
            path, request->offset, request->length,
            transfer == NULL ? NULL : transfer->output,
            transfer == NULL ? 0u : transfer->output_capacity,
            &reply->count, &reply->cursor);
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
        ops->readlink == NULL || ops->symlink == NULL || ops->link == NULL ||
        ops->open_at == NULL || ops->unlink_at == NULL ||
        ops->chmod_node == NULL || ops->chmod_at == NULL ||
        ops->filesystem_info == NULL || ops->stat_at == NULL ||
        ops->stat_node == NULL ||
        sessions == NULL ||
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
astra_vfs_service_set_session_changed(AstraVfsService *service,
    AstraVfsSessionChanged changed, void *context)
{
    if (service == NULL || changed == NULL || service->open_sessions != 0u ||
        service->session_changed != NULL) return 0;
    service->session_changed = changed;
    service->session_changed_context = context;
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

int
astra_vfs_service_set_owner_sessions(AstraVfsService *service, uint32_t sessions)
{
    if (service == NULL || !state_acquire(service))
        return 0;
    service->owner_session_limit = sessions;
    state_release(service);
    return 1;
}

static void
handle_hello(AstraVfsService *service, uint32_t owner, uint32_t grant,
             uint32_t features, const AstraVfsRequest *request,
             AstraVfsReply *reply)
{
    uint32_t index;

    /* Dispatch already validated the one current in-tree wire format. */
    reply->version = ASTRA_VFS_VERSION;

    /* A live control session survives replacement of its host scope. */
    if (request->session != ASTRA_VFS_SESSION_INVALID) {
        AstraVfsSessionSlot *slot = find_session(service, request->session);

        reply->status = slot == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
            slot->owner != owner || slot->grant != grant ? ASTRA_VFS_ERR_ACCESS :
            slot->closing ? ASTRA_VFS_ERR_BUSY : ASTRA_VFS_OK;
        if (reply->status == ASTRA_VFS_OK) {
            reply->session = slot->id;
            reply->version = slot->version;
            reply->features = features;
        }
        return;
    }

    if (owner != 0u && service->owner_session_limit != 0u) {
        uint32_t held = 0u;
        for (index = 0u; index < service->session_capacity; ++index)
            if (service->sessions[index].id != ASTRA_VFS_SESSION_INVALID &&
                service->sessions[index].owner == owner)
                ++held;
        if (held >= service->owner_session_limit) {
            ++service->stats.owner_session_quota_denied;
            reply->status = ASTRA_VFS_ERR_LIMIT;
            return;
        }
    }
    for (index = 0u; index < service->session_capacity; ++index) {
        if (service->sessions[index].id == 0u) {
            service->sessions[index].id = service->next_session++;
            service->sessions[index].owner = owner;
            service->sessions[index].grant = grant;
            service->sessions[index].open_files = 0u;
            service->sessions[index].version = reply->version;
            service->sessions[index].inflight = 0u;
            service->sessions[index].closing = 0u;
            if (service->next_session == 0u) {
                service->next_session = 1u;
            }
            reply->session = service->sessions[index].id;
            reply->status = ASTRA_VFS_OK;
            reply->features = features;
            ++service->open_sessions;
            ++service->stats.sessions_opened;
            if (service->session_changed != NULL)
                service->session_changed(service->session_changed_context);
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
    slot->grant = 0u;
    slot->open_files = 0u;
    slot->inflight = 0u;
    slot->closing = 0u;
    ++slot->generation;
    --service->open_sessions;
    ++service->stats.sessions_closed;
    if (service->session_changed != NULL)
        service->session_changed(service->session_changed_context);
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
    if (slot->closing != 0u || slot->inflight == UINT16_MAX) {
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

uint32_t
astra_vfs_service_shutdown(AstraVfsService *service,
                           const AstraVfsMountOps *mount, void *context)
{
    uint32_t status;

    if (service == NULL || mount == NULL || mount->unmount == NULL)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t index = 0u; index < service->session_capacity; ++index) {
        uint32_t session = service->sessions[index].id;

        if (session != 0u)
            astra_vfs_service_release_session(service, session);
    }
    if (service->open_sessions != 0u || service->open_files != 0u)
        return ASTRA_VFS_ERR_BUSY;
    if (mount->flush != NULL) {
        status = mount->flush(context);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    return mount->unmount(context);
}

static void
handle_open(AstraVfsService *service, uint32_t session,
            const AstraVfsRequest *request, const char *path, AstraVfsOpenFile *directory,
            AstraVfsReply *reply)
{
    AstraVfsSessionSlot *session_slot = find_session(service, session);
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t index;
    uint32_t status;

    if (session_slot == NULL) {
        reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
        return;
    }
    if ((request->flags & ~ASTRA_VFS_OPEN_MASK) != 0u ||
        ((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
         (request->flags & ASTRA_VFS_OPEN_CREATE) == 0u)) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    if ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u &&
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
    if (directory == NULL) {
        status = service->backend.ops->open(
            service->backend.context, path,
            request->flags,
            (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u ?
                (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT,
            &node, &info);
    } else {
        status = service->backend.ops->open_at(
            service->backend.context, directory->node,
            path, request->flags,
            (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u ?
                (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT,
            &node, &info);
    }
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
    astra_vfs_node_info_publish(reply, &info);
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

    if (request->flags != 0u) {
        reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        return;
    }
    reply->status = read_file(service, file, request->offset, reply->payload,
                              length, &reply->count);
    if (reply->status == ASTRA_VFS_OK)
        reply->position = request->offset + reply->count;
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
            if (status != ASTRA_VFS_OK || chunk == 0u ||
                chunk > (uint32_t)info.size - total) {
                if (status == ASTRA_VFS_OK)
                    status = ASTRA_VFS_ERR_IO;
                break;
            }
            total += chunk;
        }
        if (status == ASTRA_VFS_OK && total != (uint32_t)info.size) {
            status = ASTRA_VFS_ERR_IO;
        }
        *moved = total;
    }
    {
        uint32_t closed = backend->ops->close(backend->context, node);
        if (status == ASTRA_VFS_OK)
            status = closed;
    }
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
           uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
           uint32_t *moved, uint64_t *position)
{
    uint32_t status;

    *moved = 0u;
    *position = offset;
    if ((flags & ~ASTRA_VFS_WRITE_APPEND) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire(service, file))
        return ASTRA_VFS_ERR_IO;
    state_release(service);
    status = service->backend.ops->write(service->backend.context, file->node,
                                         offset, flags, max_end, buffer, length, moved,
                                         position);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    if (status == ASTRA_VFS_OK && *moved > length)
        *moved = length;
    return status;
}

static uint32_t
write_from_unlocked(AstraVfsService *service, uint32_t session,
                    AstraVfsFile handle, uint64_t offset, uint32_t flags, uint64_t max_end,
                    const void *buffer, uint32_t length, uint32_t *moved,
                    uint64_t *position)
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
    return write_file(service, file, offset, flags, max_end, buffer, length, moved,
                      position);
}

uint32_t
astra_vfs_service_write_position_from(
    AstraVfsService *service, uint32_t session, AstraVfsFile handle,
    uint64_t offset, uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
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
    status = write_from_unlocked(service, session, handle, offset, flags, max_end,
                                 buffer, length, moved, position);
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

static void handle_path_operation(AstraVfsService *service, uint32_t session,
    uint32_t operation, const AstraVfsRequest *request,
    const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsOpenFile *bases[2] = {NULL, NULL};
    uintptr_t nodes[2] = {0u, 0u};
    uint32_t ids[2] = {request->file,
        operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK ?
            request->second_file : 0u};
    uint32_t status = ASTRA_VFS_OK;

    for (uint32_t i = 0u; i < 2u; ++i) {
        if (ids[i] == 0u)
            continue;
        if (i == 1u && ids[1] == ids[0]) {
            nodes[1] = nodes[0];
            continue;
        }
        bases[i] = find_file(service, session, ids[i], &status);
        if (bases[i] == NULL)
            break;
        if (bases[i]->kind != ASTRA_VFS_KIND_DIRECTORY) {
            bases[i] = NULL;
            status = ASTRA_VFS_ERR_NOT_DIR;
            break;
        }
        if (!file_acquire(service, bases[i])) {
            bases[i] = NULL;
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        nodes[i] = bases[i]->node;
    }
    if (status == ASTRA_VFS_OK) {
        state_release(service);
        status = astra_vfs_backend_path_operation(&service->backend, operation,
            request, transfer, nodes[0], nodes[1], reply);
        (void)state_acquire(service);
    }
    for (uint32_t i = 0u; i < 2u; ++i)
        if (bases[i] != NULL)
            file_release(service, bases[i]);
    reply->status = status;
}


uint32_t
astra_vfs_service_write_from(AstraVfsService *service, uint32_t session,
                             AstraVfsFile handle, uint64_t offset, uint64_t max_end,
                             const void *buffer, uint32_t length,
                             uint32_t *moved)
{
    uint64_t position = offset;

    return astra_vfs_service_write_position_from(
        service, session, handle, offset, 0u, max_end, buffer, length, moved,
        &position);
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
    reply->status = write_file(service, file, request->offset, request->flags, request->write_limit,
                               request->body.payload, request->length,
                               &moved, &position);
    if (reply->status == ASTRA_VFS_OK) {
        reply->count = moved;
        reply->position = position;
    }
}

static void handle_readdir(AstraVfsService *service,
    const AstraVfsRequest *request, const char *path,
    const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t length = 0u;
    node_info_clear(&info);
    name[0] = '\0';
    reply->status = service->backend.ops->readdir(service->backend.context,
        0u, path, request->offset, name, sizeof(name), &info, &next);
    if (reply->status != ASTRA_VFS_OK)
        return;
    while (length < sizeof(name) && name[length] != '\0')
        ++length;
    if (length == 0u || length == sizeof(name) ||
        !astra_utf8_validate(name, length, 0u)) {
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        return;
    }
    if (transfer == NULL || transfer->output == NULL ||
        length >= transfer->output_capacity) {
        reply->status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        return;
    }
    memcpy(transfer->output, name, length + 1u);
    reply->cursor = next;
    reply->count = length;
    astra_vfs_node_info_publish(reply, &info);
}

static void
dispatch_unlocked(AstraVfsService *service, uint32_t owner, uint32_t grant,
                  uint32_t features, uint32_t operation,
                  const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
                  AstraVfsReply *reply)
{
    AstraVfsPathOperands paths = {0};
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
        request->version != ASTRA_VFS_VERSION) {
        ++service->stats.protocol_rejects;
        goto done;
    }

    status = astra_vfs_path_request(operation, request, transfer, &paths);
    if (status != ASTRA_VFS_OK) {
        ++service->stats.protocol_rejects;
        reply->status = status;
        goto done;
    }

    if (operation == ASTRA_VFS_OP_HELLO) {
        handle_hello(service, owner, grant, features, request, reply);
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
    /* A session is good only on the grant it said HELLO on (FS15). */
    if (slot->grant != grant) {
        ++service->stats.cross_session_denied;
        reply->status = ASTRA_VFS_ERR_ACCESS;
        goto done;
    }
    if (slot->closing != 0u || slot->inflight == UINT16_MAX) {
        reply->status = ASTRA_VFS_ERR_BUSY;
        goto done;
    }
    ++slot->inflight;
    reserved = 1;
    test_transition(service, ASTRA_VFS_TEST_AFTER_SESSION_RESERVE);
    reply->version = slot->version;
    reply->session = slot->id;

    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        slot->closing = 1u;
        reply->session = ASTRA_VFS_SESSION_INVALID;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_OPEN:
        handle_open(service, slot->id, request, paths.first, NULL, reply);
        break;
    case ASTRA_VFS_OP_OPEN_AT:
        if (paths.first[0] == '/' ||
            paths.first[0] == '\0') {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (file->kind != ASTRA_VFS_KIND_DIRECTORY)
            reply->status = ASTRA_VFS_ERR_NOT_DIR;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            handle_open(service, slot->id, request, paths.first, file, reply);
            file_release(service, file);
        }
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
    case ASTRA_VFS_OP_LOOKUP:
        handle_path_operation(service, slot->id, operation, request, transfer, reply);
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info;

        node_info_clear(&info);
        state_release(service);
        reply->status = service->backend.ops->stat(
            service->backend.context, paths.first, &info);
        (void)state_acquire(service);
        if (reply->status == ASTRA_VFS_OK) {
            astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_STAT_AT: {
        AstraVfsNodeInfo info;

        if (paths.first[0] == '/' ||
            paths.first[0] == '\0' ||
            (request->flags & ~ASTRA_VFS_STAT_FOLLOW) != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (file->kind != ASTRA_VFS_KIND_DIRECTORY)
            reply->status = ASTRA_VFS_ERR_NOT_DIR;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            node_info_clear(&info);
            state_release(service);
            reply->status = service->backend.ops->stat_at(
                service->backend.context, file->node,
                paths.first, request->flags, &info);
            (void)state_acquire(service);
            file_release(service, file);
            if (reply->status == ASTRA_VFS_OK)
                astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_STAT_FILE: {
        AstraVfsNodeInfo info;

        if (request->flags != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            node_info_clear(&info);
            state_release(service);
            reply->status = service->backend.ops->stat_node(
                service->backend.context, file->node, &info);
            (void)state_acquire(service);
            file_release(service, file);
            if (reply->status == ASTRA_VFS_OK)
                astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        state_release(service);
        handle_readdir(service, request, paths.first, transfer, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
    case ASTRA_VFS_OP_READDIR_AREA:
        state_release(service);
        handle_readdir_batch(service, request, paths.first, transfer, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_MKDIR:
    case ASTRA_VFS_OP_RENAME:
    case ASTRA_VFS_OP_LINK:
    case ASTRA_VFS_OP_SYMLINK:
    case ASTRA_VFS_OP_READLINK:
    case ASTRA_VFS_OP_DIRECTORY_PATH:
        handle_path_operation(service, slot->id, operation, request, transfer, reply);
        break;
    case ASTRA_VFS_OP_UNLINK:
        if ((request->flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        state_release(service);
        reply->status = service->backend.ops->unlink(
            service->backend.context, paths.first,
            request->flags);
        (void)state_acquire(service);
        break;

    case ASTRA_VFS_OP_CHMOD:
        if (request->offset > ASTRA_VFS_MODE_MASK) {
            reply->status = ASTRA_VFS_ERR_INVALID;
        } else {
            state_release(service);
            reply->status = service->backend.ops->chmod(
                service->backend.context,
                paths.first,
                (uint16_t)request->offset);
            (void)state_acquire(service);
        }
        break;



    case ASTRA_VFS_OP_UNLINK_AT:
        if (paths.first[0] == '/' ||
            paths.first[0] == '\0' ||
            (request->flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (file->kind != ASTRA_VFS_KIND_DIRECTORY)
            reply->status = ASTRA_VFS_ERR_NOT_DIR;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            state_release(service);
            reply->status = service->backend.ops->unlink_at(
                service->backend.context, file->node,
                paths.first, request->flags);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_CHMOD_FILE:
        if (request->offset > ASTRA_VFS_MODE_MASK) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            state_release(service);
            reply->status = service->backend.ops->chmod_node(
                service->backend.context, file->node,
                (uint16_t)request->offset);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_CHMOD_AT:
        if (paths.first[0] == '/' ||
            paths.first[0] == '\0' ||
            request->offset > ASTRA_VFS_MODE_MASK ||
            (request->flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (file->kind != ASTRA_VFS_KIND_DIRECTORY)
            reply->status = ASTRA_VFS_ERR_NOT_DIR;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            state_release(service);
            reply->status = service->backend.ops->chmod_at(
                service->backend.context, file->node,
                paths.first, (uint16_t)request->offset,
                request->flags);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_FILESYSTEM_INFO:
        if (request->file == ASTRA_VFS_FILE_INVALID) {
            state_release(service);
            reply->status = astra_vfs_backend_filesystem_info_into(
                &service->backend, 0u, paths.first,
                reply->payload, sizeof(reply->payload));
            (void)state_acquire(service);
        } else {
            file = find_file(service, slot->id, request->file, &status);
            if (file == NULL) {
                reply->status = status;
                break;
            }
            if (!file_acquire(service, file)) {
                reply->status = ASTRA_VFS_ERR_IO;
                break;
            }
            state_release(service);
            reply->status = astra_vfs_backend_filesystem_info_into(
                &service->backend, file->node, NULL, reply->payload,
                sizeof(reply->payload));
            (void)state_acquire(service);
            file_release(service, file);
        }
        if (reply->status == ASTRA_VFS_OK)
            reply->count = ASTRA_VFS_FILESYSTEM_INFO_SIZE;
        break;
    case ASTRA_VFS_OP_GRANT_DERIVE:
        /* Only a grant's port can carry the derived grant's handle. */
        reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        break;
    default:
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }

done:
    test_transition(service, ASTRA_VFS_TEST_BEFORE_REPLY);
    if (reply->status != ASTRA_VFS_OK) {
        ++service->stats.replies_failed;
        service->stats.last_failed_operation = operation;
        service->stats.last_failed_status = reply->status;
        service->stats.last_failed_owner = owner;
    }
    if (reserved) {
        finish_session_request(service, slot);
    }
}

void
astra_vfs_service_dispatch_grant(AstraVfsService *service, uint32_t owner,
                                 uint32_t grant, uint32_t features,
                                 uint32_t operation,
                                 const AstraVfsRequest *request,
                                 const AstraVfsTransfer *transfer,
                                 AstraVfsReply *reply)
{
    if (service == NULL || reply == NULL)
        return;
    if (!state_acquire(service)) {
        clear_reply(reply, ASTRA_VFS_VERSION);
        reply->status = ASTRA_VFS_ERR_IO;
        return;
    }
    dispatch_unlocked(service, owner, grant, features, operation, request,
                      transfer, reply);
    state_release(service);
}

void
astra_vfs_service_dispatch_from(AstraVfsService *service, uint32_t owner,
                                uint32_t operation,
                                const AstraVfsRequest *request,
                                const AstraVfsTransfer *transfer,
                                AstraVfsReply *reply)
{
    astra_vfs_service_dispatch_grant(service, owner, 0u, 0u, operation,
                                     request, transfer, reply);
}

void
astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                           const AstraVfsRequest *request,
                           const AstraVfsTransfer *transfer,
                           AstraVfsReply *reply)
{
    astra_vfs_service_dispatch_from(service, 0u, operation, request, transfer, reply);
}

int
astra_vfs_service_session_bound(const AstraVfsService *service,
                                uint32_t session, uint32_t owner,
                                uint32_t grant)
{
    AstraVfsService *mutable = (AstraVfsService *)(uintptr_t)service;
    int bound = 0;

    if (service == NULL || session == ASTRA_VFS_SESSION_INVALID ||
        !state_acquire(mutable))
        return 0;
    for (uint32_t index = 0u; index < service->session_capacity; ++index)
        if (service->sessions[index].id == session) {
            bound = service->sessions[index].owner == owner &&
                    service->sessions[index].grant == grant;
            break;
        }
    state_release(mutable);
    return bound;
}

uint32_t
astra_vfs_service_session_grant(const AstraVfsService *service,
                                uint32_t session)
{
    AstraVfsService *mutable = (AstraVfsService *)(uintptr_t)service;
    uint32_t grant = 0u;

    if (service == NULL || session == ASTRA_VFS_SESSION_INVALID ||
        !state_acquire(mutable))
        return 0u;
    for (uint32_t index = 0u; index < service->session_capacity; ++index)
        if (service->sessions[index].id == session) {
            grant = service->sessions[index].grant;
            break;
        }
    state_release(mutable);
    return grant;
}

void
astra_vfs_service_release_grant(AstraVfsService *service, uint32_t grant)
{
    if (service == NULL || grant == 0u)
        return;
    for (uint32_t index = 0u; index < service->session_capacity; ++index) {
        uint32_t session = 0u;

        if (!state_acquire(service))
            return;
        if (service->sessions[index].id != 0u &&
            service->sessions[index].grant == grant)
            session = service->sessions[index].id;
        state_release(service);
        if (session != 0u)
            astra_vfs_service_release_session(service, session);
    }
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
