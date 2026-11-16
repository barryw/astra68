/*
 * The storage service core.
 *
 * Everything here is validation, handle bookkeeping and dispatch. The bytes
 * belong to a backend and the transport belongs to a caller; see
 * astra/vfs_backend.h and astra/vfs_service_core.h for why the three are apart.
 */

#include <astra/vfs_service_core.h>
#include <astra/vfs_reply.h>

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
file_acquire_for(AstraVfsService *service, AstraVfsOpenFile *file,
                 uint8_t allowed_state)
{
    uint16_t generation = file->generation;
    AstraVfsFileIO *io = file->io;

    if (file->state != allowed_state || io == NULL)
        return 0;
    while (io->active != NULL) {
        uint32_t expected = io->sequence;
        int waited;

        if (service->state_wait == NULL || io->waiters == UINT16_MAX)
            return 0;
        ++io->waiters;
        waited = service->state_wait(service->state_lock_context,
                                     &io->sequence, expected);
        --io->waiters;
        if (!waited)
            return 0;
        if (file->state != allowed_state || file->generation != generation)
            return 0;
    }
    io->active = file;
    return 1;
}

static int
file_acquire(AstraVfsService *service, AstraVfsOpenFile *file)
{
    return file_acquire_for(service, file, ASTRA_VFS_FILE_OPEN);
}

static void
file_release(AstraVfsService *service, AstraVfsOpenFile *file)
{
    AstraVfsFileIO *io = file->io;
    io->active = NULL;
    ++io->sequence;
    if (io->waiters != 0u && service->state_wake != NULL)
        service->state_wake(service->state_lock_context, &io->sequence);
}

static int
file_wait_idle(AstraVfsService *service, AstraVfsOpenFile *file)
{
    AstraVfsFileIO *io = file->io;
    while (io->active == file) {
        uint32_t expected = io->sequence;
        int waited;

        if (service->state_wait == NULL || io->waiters == UINT16_MAX)
            return 0;
        ++io->waiters;
        waited = service->state_wait(service->state_lock_context,
                                     &io->sequence, expected);
        --io->waiters;
        if (!waited)
            return 0;
    }
    return 1;
}

static void file_io_release(AstraVfsService *service, AstraVfsOpenFile *file)
{
    AstraVfsFileIO *io = file->io;
    --io->references;
    ++io->sequence;
    file->io = NULL;
    if (service->state_wake != NULL)
        service->state_wake(service->state_lock_context, &io->sequence);
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

static uint64_t backend_incarnation(AstraVfsService *service,
    const AstraVfsBackend *backend)
{
    return service->backend_bindings == NULL ? 1u :
        service->backend_bindings->incarnation(service->backend_binding_context, backend);
}

static AstraVfsFileIO *find_node_io(AstraVfsService *service,
    const AstraVfsBackend *backend, const AstraVfsNodeInfo *info)
{
    if (info->kind != ASTRA_VFS_KIND_FILE ||
        !astra_vfs_node_identity_equal(&info->meta, &info->meta)) return NULL;
    uint64_t incarnation = backend_incarnation(service, backend);
    if (incarnation == 0u) return NULL;
    for (uint32_t at = 0u; at < service->file_high_water; ++at) {
        AstraVfsFileIO *io = &service->files[at].io_storage;
        if (io->references != 0u && io->incarnation == incarnation &&
            astra_vfs_node_identity_equal(&io->identity, &info->meta)) return io;
    }
    return NULL;
}

/* Called under the native state lock; permissions and allocation stay fresh. */
static uint32_t cache_node_info(AstraVfsService *service,
    const AstraVfsBackend *backend, const AstraVfsFileIO *io, AstraVfsNodeInfo *info)
{
    if (service->cached_nodes == 0u) return ASTRA_VFS_OK;
    if (io == NULL) io = find_node_io(service, backend, info);
    if (io == NULL || io->cache == NULL) return ASTRA_VFS_OK;
    if (info->kind != ASTRA_VFS_KIND_FILE ||
        (astra_vfs_node_identity_equal(&io->identity, &io->identity) &&
         !astra_vfs_node_identity_equal(&io->identity, &info->meta)))
        return ASTRA_VFS_ERR_LOST;
    info->size = io->cache->size;
    if (io->cache->written) {
        info->mtime = (int64_t)(io->cache->modified_ns / UINT64_C(1000000000));
        info->meta.mtime_nsec = (uint32_t)(io->cache->modified_ns % UINT64_C(1000000000));
        const AstraVfsNodeMeta *backing = &io->cache->backing.meta;
        if (((info->meta.valid ^ backing->valid) & ASTRA_VFS_META_CTIME) == 0u &&
            (!(info->meta.valid & ASTRA_VFS_META_CTIME) ||
             (info->meta.ctime == backing->ctime && info->meta.ctime_nsec == backing->ctime_nsec))) {
            info->meta.ctime = info->mtime;
            info->meta.ctime_nsec = info->meta.mtime_nsec;
            info->meta.valid |= ASTRA_VFS_META_CTIME;
        }
        info->meta.valid |= ASTRA_VFS_META_MTIME;
    }
    return ASTRA_VFS_OK;
}

/* Node I/O stays owned; callbacks and backend I/O run without the state lock. */
static uint32_t read_file_info(AstraVfsService *service,
    const AstraVfsOpenFile *file, AstraVfsNodeInfo *info)
{
    if (file->kind == ASTRA_VFS_KIND_FILE && service->cache_ops != NULL &&
        service->cache_ops->stat != NULL)
        return service->cache_ops->stat(service->cache_context, file, info);
    return file->backend.ops->stat_node(file->backend.context, file->node, info);
}

static int stat_owned_file(AstraVfsService *service,
    AstraVfsOpenFile *file, AstraVfsNodeInfo *info, uint32_t *status);

static int backend_ops_valid(const AstraVfsBackendOps *ops)
{
    return ops != NULL && ops->open != NULL && ops->close != NULL &&
        ops->read != NULL && ops->write != NULL && ops->sync != NULL &&
        ops->truncate != NULL && ops->stat != NULL && ops->readdir != NULL &&
        ops->mkdir != NULL && ops->unlink != NULL && ops->rename != NULL &&
        ops->chmod != NULL && ops->readlink != NULL && ops->symlink != NULL &&
        ops->link != NULL && ops->open_at != NULL && ops->unlink_at != NULL &&
        ops->chmod_node != NULL && ops->chmod_at != NULL &&
        ops->filesystem_info != NULL && ops->stat_at != NULL && ops->stat_node != NULL;
}

static void release_backend(AstraVfsService *service, AstraVfsBackend *backend)
{
    if (backend->ops != NULL && service->backend_bindings != NULL)
        service->backend_bindings->release(service->backend_binding_context, backend);
    *backend = (AstraVfsBackend){0};
}

static uint32_t acquire_backend(AstraVfsService *service, uint32_t owner,
    uint32_t grant, AstraVfsBackend *backend)
{
    uint32_t status;

    *backend = service->backend;
    if (service->backend_bindings == NULL) return ASTRA_VFS_OK;
    *backend = (AstraVfsBackend){0};
    status = service->backend_bindings->acquire(service->backend_binding_context,
        owner, grant, backend);
    if (status != ASTRA_VFS_OK) return status;
    if (backend_ops_valid(backend->ops)) return ASTRA_VFS_OK;
    service->backend_bindings->release(service->backend_binding_context, backend);
    *backend = (AstraVfsBackend){0};
    return ASTRA_VFS_ERR_PROTOCOL;
}

uint32_t astra_vfs_backend_description_operation(const AstraVfsBackend *backend,
    uintptr_t node, uint32_t operation, const AstraVfsRequest *request,
    AstraVfsReply *reply, const uint64_t *logical_end)
{
    uint64_t position = 0u;
    uint32_t result = 0u, status;

    if (backend == NULL || backend->ops == NULL || request == NULL || reply == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (request->write_limit != 0u)
        return ASTRA_VFS_ERR_INVALID;
    switch (operation) {
    case ASTRA_VFS_OP_SEEK:
        if (request->flags > 2u || request->length != 0u)
            return ASTRA_VFS_ERR_INVALID;
        if (backend->ops->seek == NULL)
            return ASTRA_VFS_ERR_UNSUPPORTED;
        if (request->flags == 2u && logical_end != NULL) {
            status = astra_vfs_seek_offset(*logical_end, (int64_t)request->offset,
                INT64_MAX, &position);
            if (status != ASTRA_VFS_OK) return status;
            status = backend->ops->seek(backend->context, node,
                (int64_t)position, 0u, &position);
        } else
            status = backend->ops->seek(backend->context, node,
                (int64_t)request->offset, request->flags, &position);
        if (status != ASTRA_VFS_OK) return status;
        if (position > INT64_MAX) return ASTRA_VFS_ERR_PROTOCOL;
        reply->position = position;
        return ASTRA_VFS_OK;
    case ASTRA_VFS_OP_FILE_FLAGS:
        if (request->length != 0u || request->offset > UINT32_MAX ||
            (request->flags & ~ASTRA_VFS_FILE_STATUS_MASK) != 0u ||
            ((uint32_t)request->offset & ~request->flags) != 0u)
            return ASTRA_VFS_ERR_INVALID;
        if (backend->ops->file_flags == NULL)
            return ASTRA_VFS_ERR_UNSUPPORTED;
        status = backend->ops->file_flags(backend->context, node,
            request->flags, (uint32_t)request->offset, &result);
        if (status != ASTRA_VFS_OK) return status;
        if ((result & ~ASTRA_VFS_OPEN_MASK) != 0u ||
            (result & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u ||
            (result & request->flags) != (uint32_t)request->offset)
            return ASTRA_VFS_ERR_PROTOCOL;
        reply->open_flags = result;
        return ASTRA_VFS_OK;
    case ASTRA_VFS_OP_POSITION_ADVANCE:
        if (request->flags != 0u || request->offset > INT64_MAX ||
            request->length > INT64_MAX - request->offset)
            return ASTRA_VFS_ERR_INVALID;
        if (backend->ops->position_advance == NULL)
            return ASTRA_VFS_ERR_UNSUPPORTED;
        status = backend->ops->position_advance(backend->context, node,
            request->offset, request->length, &position, &result);
        if (status != ASTRA_VFS_OK) return status;
        if (!astra_vfs_position_advance_valid(request->offset,
                request->length, position, result))
            return ASTRA_VFS_ERR_PROTOCOL;
        reply->position = position;
        reply->count = result;
        return ASTRA_VFS_OK;
    default:
        return ASTRA_VFS_ERR_UNSUPPORTED;
    }
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

static uint32_t validate_path_operation(uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsPathOperands *paths,
    uintptr_t base, uintptr_t second_base)
{
    const char *path = paths->first == NULL ? "" : paths->first;
    const char *to = paths->second;
    uint32_t allowed = operation == ASTRA_VFS_OP_LOOKUP ?
        ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_MISSING | ASTRA_VFS_LOOKUP_ENTRY :
        operation == ASTRA_VFS_OP_RENAME ? ASTRA_VFS_RENAME_NOREPLACE :
        operation == ASTRA_VFS_OP_UNLINK || operation == ASTRA_VFS_OP_UNLINK_AT ?
        ASTRA_VFS_UNLINK_DIRECTORY :
        operation == ASTRA_VFS_OP_CHMOD_AT ? ASTRA_VFS_AT_SYMLINK_NOFOLLOW : 0u;
    if ((request->flags & ~allowed) != 0u ||
        (operation != ASTRA_VFS_OP_DIRECTORY_PATH && base != 0u &&
         ((operation == ASTRA_VFS_OP_SYMLINK ? to : path)[0] == '\0' ||
          (operation == ASTRA_VFS_OP_SYMLINK ? to : path)[0] == '/')) ||
        ((operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK) &&
         second_base != 0u && (to[0] == '\0' || to[0] == '/')))
        return ASTRA_VFS_ERR_INVALID;
    if ((operation == ASTRA_VFS_OP_CHMOD || operation == ASTRA_VFS_OP_CHMOD_AT) &&
        request->offset > ASTRA_VFS_MODE_MASK) return ASTRA_VFS_ERR_INVALID;
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
    if (status != ASTRA_VFS_OK) return status;
    status = validate_path_operation(operation, request, &paths, base, second_base);
    if (status != ASTRA_VFS_OK) return status;
    const char *path = paths.first == NULL ? "" : paths.first;
    const char *to = paths.second;
    uint32_t capacity = request->length;
    status = ASTRA_VFS_ERR_UNSUPPORTED;
    switch (operation) {
    case ASTRA_VFS_OP_UNLINK:
    case ASTRA_VFS_OP_UNLINK_AT:
        return base == 0u ? ops->unlink(backend->context, path, request->flags) :
            ops->unlink_at(backend->context, base, path, request->flags);
    case ASTRA_VFS_OP_CHMOD:
    case ASTRA_VFS_OP_CHMOD_AT:
        return base == 0u ? ops->chmod(backend->context, path, request->offset) :
            ops->chmod_at(backend->context, base, path, request->offset, request->flags);
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
        astra_store_be32(&buffer[used + 32u], info.meta.valid &
            (ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
             ASTRA_VFS_META_GENERATION));
        astra_store_be32(&buffer[used + 36u], 0u);
        astra_store_be64(&buffer[used + 40u],
            (info.meta.valid & ASTRA_VFS_META_VOLUME) ? info.meta.volume : 0u);
        astra_store_be64(&buffer[used + 48u],
            (info.meta.valid & ASTRA_VFS_META_INODE) ? info.meta.inode : 0u);
        astra_store_be64(&buffer[used + 56u],
            (info.meta.valid & ASTRA_VFS_META_GENERATION) ?
                info.meta.generation : 0u);
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

static void handle_readdir_batch(AstraVfsService *service, const AstraVfsBackend *backend,
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
        reply->status = astra_vfs_backend_readdir_into(backend, 0u,
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

static void notify_holders_changed(AstraVfsService *service)
{
    if (service->holders_changed != NULL)
        service->holders_changed(service->holders_changed_context);
}

static void
finish_file_close(AstraVfsService *service, AstraVfsOpenFile *file,
                  uint8_t already_detached)
{
    AstraVfsSessionSlot *slot = find_session(service, file->session);

    release_backend(service, &file->backend);
    file->session = 0u;
    file->token = 0u;
    file->flags = 0u;
    file->kind = ASTRA_VFS_KIND_UNKNOWN;
    file->state = ASTRA_VFS_FILE_FREE;
    file->retained = 0u;
    file_io_release(service, file);
    ++file->generation; /* Zero retires this token slot; its I/O cell stays usable. */
    --service->open_files;
    if (!already_detached) {
        if (slot != NULL && slot->open_files != 0u)
            --slot->open_files;
        ++service->stats.files_closed;
    }
    notify_holders_changed(service);
}

/* Called with the state lock held; returns with it held. */
static uint32_t
close_file(AstraVfsService *service, AstraVfsOpenFile *file)
{
    uintptr_t node = file->node;
    uint64_t token = file->token;
    uint8_t state = file->state;
    uint32_t status;

    if (state != ASTRA_VFS_FILE_OPEN && state != ASTRA_VFS_FILE_SHARED)
        return ASTRA_VFS_ERR_BUSY;
    file->state = state == ASTRA_VFS_FILE_SHARED ?
        ASTRA_VFS_FILE_TRANSFERRING : ASTRA_VFS_FILE_CLOSING;
    if (file->retained == 0u && !file_wait_idle(service, file)) {
        file->state = state;
        return ASTRA_VFS_ERR_IO;
    }
    if (file->retained != 0u) {
        AstraVfsSessionSlot *slot = find_session(service, file->session);

        file->session = 0u;
        file->state = ASTRA_VFS_FILE_DETACHED;
        if (slot != NULL && slot->open_files != 0u)
            --slot->open_files;
        ++service->stats.files_closed;
        notify_holders_changed(service);
        return file->io->cache == NULL ? ASTRA_VFS_OK : file->io->cache->error;
    }
    state_release(service);
    test_transition(service, ASTRA_VFS_TEST_DURING_CLOSE);
    status = state == ASTRA_VFS_FILE_SHARED ?
        file->backend.ops->adopt(file->backend.context, token, ASTRA_VFS_ADOPT_DISCARD, NULL) :
        file->backend.ops->close(file->backend.context, node);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    if (state == ASTRA_VFS_FILE_SHARED && status != ASTRA_VFS_OK &&
        status != ASTRA_VFS_ERR_BAD_HANDLE && status != ASTRA_VFS_ERR_LOST) {
        file->state = state;
        return status;
    }
    uint32_t cache_error = file->io->cache == NULL ? ASTRA_VFS_OK : file->io->cache->error;
    finish_file_close(service, file, 0u);
    return state == ASTRA_VFS_FILE_SHARED ? ASTRA_VFS_OK :
        status == ASTRA_VFS_OK ? cache_error : status;
}

int
astra_vfs_service_init(AstraVfsService *service, const AstraVfsBackendOps *ops,
                       void *context, AstraVfsSessionSlot *sessions,
                       uint32_t session_capacity, AstraVfsOpenFile *files,
                       uint32_t file_capacity)
{
    uint32_t index;

    if (service == NULL || !backend_ops_valid(ops) ||
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
astra_vfs_service_set_holders_changed(AstraVfsService *service,
    AstraVfsHoldersChanged changed, void *context)
{
    if (service == NULL || changed == NULL || service->open_sessions != 0u ||
        service->holders_changed != NULL) return 0;
    service->holders_changed = changed;
    service->holders_changed_context = context;
    return 1;
}

int astra_vfs_service_set_cache_ops(AstraVfsService *service,
    const AstraVfsCacheOps *ops, void *context)
{
    if (service == NULL || ops == NULL || ops->write == NULL ||
        service->open_sessions != 0u || service->open_files != 0u ||
        service->cache_ops != NULL) return 0;
    service->cache_ops = ops;
    service->cache_context = context;
    return 1;
}

/* The caller holds this node's I/O owner and the state lock. */
static uint32_t flush_cache(AstraVfsService *service,
    const AstraVfsOpenFile *file, uint32_t *remaining)
{
    if (file->io->cache == NULL) return ASTRA_VFS_OK;
    if (service->cache_ops == NULL || service->cache_ops->sync == NULL) {
        uint32_t error = file->io->cache->error;
        return error == ASTRA_VFS_OK ? ASTRA_VFS_ERR_BUSY : error;
    }
    state_release(service);
    uint32_t status = service->cache_ops->sync(service->cache_context, file, remaining);
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK &&
        file->io->cache->size != file->io->cache->backing.size)
        status = ASTRA_VFS_ERR_BUSY;
    if (status == ASTRA_VFS_OK) file->io->cache->written = 0u;
    /* Bounded work may defer; backing conflicts are recorded by the writer. */
    if (remaining == NULL || status != ASTRA_VFS_ERR_BUSY)
        astra_vfs_file_cache_record_status(file->io->cache, status);
    return status;
}

int astra_vfs_service_set_backend_bindings(AstraVfsService *service,
    const AstraVfsBackendBindingOps *bindings, void *context)
{
    if (service == NULL || bindings == NULL || bindings->acquire == NULL ||
        bindings->retain == NULL || bindings->release == NULL || bindings->incarnation == NULL ||
        service->backend_bindings != NULL || service->open_sessions != 0u ||
        service->open_files != 0u) return 0;
    service->backend_bindings = bindings;
    service->backend_binding_context = context;
    return 1;
}

int
astra_vfs_service_change_snapshot(AstraVfsService *service,
                                  AstraVfsChangeSnapshot *snapshot)
{
    if (service == NULL || snapshot == NULL || !state_acquire(service))
        return 0;
    *snapshot = service->changes;
    state_release(service);
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

int astra_vfs_service_set_owner_files(AstraVfsService *service, uint32_t files)
{
    if (service == NULL || !state_acquire(service)) return 0;
    service->owner_file_limit = files;
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
            if (service->backend_bindings != NULL) {
                AstraVfsBackend backend;
                if (slot->inflight != 0u) {
                    reply->status = ASTRA_VFS_ERR_BUSY;
                    return;
                }
                reply->status = acquire_backend(service, owner, grant, &backend);
                if (reply->status != ASTRA_VFS_OK) return;
                release_backend(service, &slot->backend);
                slot->backend = backend;
            }
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
    if (service->next_session == 0u) {
        reply->status = ASTRA_VFS_ERR_LIMIT;
        return;
    }
    for (index = 0u; index < service->session_capacity; ++index) {
        if (service->sessions[index].id == 0u) {
            AstraVfsBackend backend;
            reply->status = acquire_backend(service, owner, grant, &backend);
            if (reply->status != ASTRA_VFS_OK) return;
            service->sessions[index].id = service->next_session++;
            service->sessions[index].backend = backend;
            service->sessions[index].owner = owner;
            service->sessions[index].grant = grant;
            service->sessions[index].open_files = 0u;
            service->sessions[index].version = reply->version;
            service->sessions[index].inflight = 0u;
            service->sessions[index].closing = 0u;
            reply->session = service->sessions[index].id;
            reply->status = ASTRA_VFS_OK;
            reply->features = features;
            ++service->open_sessions;
            ++service->stats.sessions_opened;
            notify_holders_changed(service);
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

    ++slot->inflight;
    for (;;) {
        AstraVfsOpenFile *file = NULL;

        for (uint32_t index = 0u; index < service->file_high_water; ++index) {
            if (service->files[index].session == session &&
                (service->files[index].state == ASTRA_VFS_FILE_OPEN ||
                 service->files[index].state == ASTRA_VFS_FILE_SHARED)) {
                file = &service->files[index];
                break;
            }
        }
        if (file == NULL)
            break;
        if (close_file(service, file) != ASTRA_VFS_OK &&
            file->state != ASTRA_VFS_FILE_FREE &&
            file->state != ASTRA_VFS_FILE_DETACHED)
            break;
    }
    if (slot->open_files != 0u) {
        --slot->inflight;
        return;
    }
    release_backend(service, &slot->backend);
    slot->id = 0u;
    slot->owner = 0u;
    slot->grant = 0u;
    slot->open_files = 0u;
    slot->inflight = 0u;
    slot->closing = 0u;
    ++slot->generation;
    --service->open_sessions;
    ++service->stats.sessions_closed;
    notify_holders_changed(service);
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
astra_vfs_service_readdir_into(AstraVfsService *service, uint32_t session,
    const char *path, uint64_t cursor, uint32_t entry_limit, uint8_t *buffer,
    uint32_t capacity, uint32_t *used, uint64_t *next)
{
    AstraVfsSessionSlot *slot;
    uint32_t status;

    if (service == NULL || !state_acquire(service)) return ASTRA_VFS_ERR_IO;
    slot = begin_session_request(service, session, &status);
    if (slot == NULL) { state_release(service); return status; }
    state_release(service);
    status = astra_vfs_backend_readdir_into(&slot->backend, 0u, path,
        cursor, entry_limit, buffer, capacity, used, next);
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    finish_session_request(service, slot);
    state_release(service);
    return status;
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
        &file->backend, file->node, path, cursor, entry_limit, buffer,
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

void astra_vfs_service_reap(AstraVfsService *service)
{
    if (service == NULL || !state_acquire(service)) return;
    for (uint32_t at = 0u; at < service->session_capacity; ++at) {
        AstraVfsSessionSlot *slot = &service->sessions[at];
        if (slot->id != 0u && slot->closing && slot->inflight == 0u)
            reap_session(service, slot);
    }
    state_release(service);
}

static int owner_files_full(AstraVfsService *service, uint32_t owner)
{
    uint32_t held = 0u;
    if (owner == 0u || service->owner_file_limit == 0u) return 0;
    for (uint32_t at = 0u; at < service->session_capacity; ++at)
        if (service->sessions[at].id != 0u && service->sessions[at].owner == owner)
            held += service->sessions[at].open_files;
    if (held < service->owner_file_limit) return 0;
    ++service->stats.owner_quota_denied;
    return 1;
}

static AstraVfsOpenFile *grow_file_storage(AstraVfsService *service)
{
    if (service->file_high_water == service->file_capacity) return NULL;
    AstraVfsOpenFile *file = &service->files[service->file_high_water++];
    memset(file, 0, sizeof(*file));
    file->generation = 1u;
    return file;
}

/* Handles and shared I/O cells use the same lazily initialized storage budget. */
static AstraVfsOpenFile *reserve_file(AstraVfsService *service,
    AstraVfsSessionSlot *slot, const AstraVfsBackend *backend,
    AstraVfsFileIO *io, uint32_t *status)
{
    uint32_t index;
    *status = ASTRA_VFS_ERR_LIMIT;
    if (owner_files_full(service, slot->owner)) return NULL;
    for (index = 0u; index < service->file_high_water; ++index) {
        if (service->files[index].state == ASTRA_VFS_FILE_FREE &&
            service->files[index].generation != 0u) {
            break;
        }
    }
    if (index == service->file_high_water) {
        if (grow_file_storage(service) == NULL) return NULL;
    }
    if (index >= service->file_capacity) {
        return NULL;
    }
    if (io == NULL) {
        for (uint32_t at = 0u; at < service->file_high_water; ++at)
            if (service->files[at].io_storage.references == 0u &&
                service->files[at].io_storage.waiters == 0u &&
                service->files[at].io_storage.active == NULL) {
                io = &service->files[at].io_storage;
                break;
            }
        if (io == NULL) {
            AstraVfsOpenFile *storage = grow_file_storage(service);
            if (storage == NULL) return NULL;
            io = &storage->io_storage;
        }
        io->incarnation = 0u;
        memset(&io->identity, 0, sizeof(io->identity));
        io->cache = NULL;
    }
    *status = service->backend_bindings == NULL ? ASTRA_VFS_OK :
        service->backend_bindings->retain(service->backend_binding_context, backend);
    if (*status != ASTRA_VFS_OK) return NULL;
    service->files[index].backend = *backend;
    service->files[index].session = slot->id;
    service->files[index].state = ASTRA_VFS_FILE_OPENING;
    ++io->references;
    service->files[index].io = io;
    ++service->open_files;
    ++slot->open_files;
    test_transition(service, ASTRA_VFS_TEST_AFTER_SESSION_RESERVE);
    return &service->files[index];
}

static void cancel_file_reservation(AstraVfsService *service, AstraVfsOpenFile *file)
{
    AstraVfsSessionSlot *slot = find_session(service, file->session);
    release_backend(service, &file->backend);
    file->session = 0u;
    file->token = 0u;
    file_io_release(service, file);
    file->state = ASTRA_VFS_FILE_FREE;
    --service->open_files;
    --slot->open_files;
}

static void file_opened(AstraVfsService *service)
{
    ++service->stats.files_opened;
    if (service->open_files > service->stats.peak_open_files)
        service->stats.peak_open_files = service->open_files;
    notify_holders_changed(service);
}

/* Opening owns a private cell until exact identity permits joining a node. */
static void join_file_io(AstraVfsService *service, AstraVfsOpenFile *file,
    const AstraVfsNodeInfo *info)
{
    if (info->kind != ASTRA_VFS_KIND_FILE ||
        !astra_vfs_node_identity_equal(&info->meta, &info->meta)) return;
    uint64_t incarnation = backend_incarnation(service, &file->backend);
    if (incarnation == 0u) return;
    AstraVfsFileIO *io = find_node_io(service, &file->backend, info);
    if (io != NULL && io != file->io) {
        file_io_release(service, file);
        ++io->references;
        file->io = io;
        return;
    }
    file->io->identity = info->meta;
    file->io->incarnation = incarnation;
}

static uint32_t truncate_file(AstraVfsService *service, AstraVfsOpenFile *file,
    uint8_t state, uint64_t size, AstraVfsNodeInfo *info)
{
    AstraVfsNodeInfo after = {0};
    uint32_t status;

    if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u) return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY) return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire_for(service, file, state)) return ASTRA_VFS_ERR_IO;
    status = flush_cache(service, file, NULL);
    if (status != ASTRA_VFS_OK) {
        file_release(service, file);
        return status;
    }
    state_release(service);
    status = file->backend.ops->truncate(file->backend.context, file->node, size);
    int changed = status == ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK)
        status = read_file_info(service, file, &after);
    if (status == ASTRA_VFS_OK && (after.kind != ASTRA_VFS_KIND_FILE || after.size != size ||
        (astra_vfs_node_identity_equal(&file->io->identity, &file->io->identity) &&
         !astra_vfs_node_identity_equal(&file->io->identity, &after.meta))))
        status = ASTRA_VFS_ERR_IO;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    if (file->io->cache != NULL) {
        if (status == ASTRA_VFS_OK) {
            file->io->cache->backing = after;
            file->io->cache->size = after.size;
            file->io->cache->written = 0u;
        }
        astra_vfs_file_cache_record_status(file->io->cache, status);
    }
    if (changed) ++service->changes.nodes;
    file_release(service, file);
    if (status == ASTRA_VFS_OK && info != NULL) *info = after;
    return status;
}

static void
handle_open(AstraVfsService *service, uint32_t session,
            const AstraVfsRequest *request, const char *path, AstraVfsOpenFile *directory,
            AstraVfsReply *reply)
{
    AstraVfsSessionSlot *session_slot = find_session(service, session);
    AstraVfsNodeInfo info;
    AstraVfsBackend backend;
    AstraVfsOpenFile *file;
    uintptr_t node = 0u;
    uint32_t status;

    if (session_slot == NULL) {
        reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
        return;
    }
    if ((request->flags & ~ASTRA_VFS_OPEN_MASK) != 0u ||
        ((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
         (request->flags & ASTRA_VFS_OPEN_CREATE) == 0u) ||
        ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u &&
         request->offset != ASTRA_VFS_MODE_DEFAULT &&
         request->offset > ASTRA_VFS_MODE_MASK)) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    if ((request->flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u &&
        (request->flags & ASTRA_VFS_OPEN_WRITE) == 0u) {
        reply->status = ASTRA_VFS_ERR_ACCESS;
        return;
    }
    backend = directory != NULL ? directory->backend : session_slot->backend;
    file = reserve_file(service, session_slot, &backend, NULL, &reply->status);
    if (file == NULL) return;

    node_info_clear(&info);
    state_release(service);
    if (directory == NULL) {
        status = backend.ops->open(
            backend.context, path,
            request->flags & ~ASTRA_VFS_OPEN_TRUNCATE,
            (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u ?
                (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT,
            &node, &info);
    } else {
        status = backend.ops->open_at(
            backend.context, directory->node,
            path, request->flags & ~ASTRA_VFS_OPEN_TRUNCATE,
            (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u ?
                (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT,
            &node, &info);
    }
    if (!state_acquire(service)) {
        reply->status = ASTRA_VFS_ERR_IO;
        return;
    }
    if (status != ASTRA_VFS_OK) {
        cancel_file_reservation(service, file);
        reply->status = status;
        return;
    }
    file->node = node;
    file->flags = request->flags;
    file->kind = info.kind;
    join_file_io(service, file, &info);
    if ((request->flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) {
        status = truncate_file(service, file, ASTRA_VFS_FILE_OPENING, 0u, &info);
    } else if (file->kind == ASTRA_VFS_KIND_FILE && service->cache_ops != NULL &&
               service->cache_ops->stat != NULL) {
        if (!file_acquire_for(service, file, ASTRA_VFS_FILE_OPENING))
            status = ASTRA_VFS_ERR_BUSY;
        else {
            if (!stat_owned_file(service, file, &info, &status)) {
                reply->status = ASTRA_VFS_ERR_IO;
                return;
            }
            file_release(service, file);
        }
    }
    if (status == ASTRA_VFS_OK)
        status = cache_node_info(service, &file->backend, file->io, &info);
    if (status != ASTRA_VFS_OK) {
        state_release(service);
        (void)backend.ops->close(backend.context, node);
        (void)state_acquire(service);
        cancel_file_reservation(service, file);
        reply->status = status;
        return;
    }
    file->state = ASTRA_VFS_FILE_OPEN;
    reply->file = HANDLE_MAKE((uint32_t)(file - service->files), file->generation);
    astra_vfs_node_info_publish(reply, &info);
    reply->status = ASTRA_VFS_OK;
    file_opened(service);
}

static void handle_share(AstraVfsService *service, AstraVfsSessionSlot *slot,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsOpenFile *source, *pending;
    uint64_t token = 0u;

    if (!astra_vfs_description_transfer_valid(ASTRA_VFS_OP_SHARE, request)) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    source = find_file(service, slot->id, request->file, &reply->status);
    if (source == NULL) return;
    if (source->backend.ops->share == NULL || source->backend.ops->adopt == NULL) {
        reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        return;
    }
    if (!file_acquire(service, source)) { reply->status = ASTRA_VFS_ERR_BUSY; return; }
    pending = reserve_file(service, slot, &source->backend, source->io, &reply->status);
    if (pending == NULL) { file_release(service, source); return; }
    pending->flags = source->flags;
    pending->kind = source->kind;
    state_release(service);
    uint32_t status = source->backend.ops->share(source->backend.context, source->node, &token);
    if (!state_acquire(service)) { reply->status = ASTRA_VFS_ERR_IO; return; }
    file_release(service, source);
    if (status == ASTRA_VFS_OK && token == 0u) status = ASTRA_VFS_ERR_PROTOCOL;
    if (status != ASTRA_VFS_OK) cancel_file_reservation(service, pending);
    else {
        pending->token = token;
        pending->state = ASTRA_VFS_FILE_SHARED;
        reply->token = token;
        file_opened(service);
    }
    reply->status = status;
}

static int shared_token_matches(const AstraVfsOpenFile *file, uint64_t token)
{
    return (file->state == ASTRA_VFS_FILE_SHARED || file->state == ASTRA_VFS_FILE_TRANSFERRING) &&
        file->token == token;
}

static void handle_adopt(AstraVfsService *service, AstraVfsSessionSlot *slot,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsOpenFile *pending = NULL;
    AstraVfsSessionSlot *source = NULL;
    uintptr_t node = 0u;
    uint32_t status;

    if (!astra_vfs_description_transfer_valid(ASTRA_VFS_OP_ADOPT, request)) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
    for (uint32_t at = 0u; at < service->file_high_water; ++at) {
        AstraVfsOpenFile *candidate = &service->files[at];
        if (!shared_token_matches(candidate, request->offset))
            continue;
        AstraVfsSessionSlot *owner = find_session(service, candidate->session);
        if (owner == NULL) { reply->status = ASTRA_VFS_ERR_PROTOCOL; return; }
        if (owner->grant != slot->grant ||
            (request->flags != 0u && owner->owner != slot->owner)) {
            reply->status = ASTRA_VFS_ERR_ACCESS;
            continue;
        }
        if (request->flags == 0u && (owner->closing ||
            candidate->backend.ops != slot->backend.ops ||
            candidate->backend.context != slot->backend.context)) {
            reply->status = ASTRA_VFS_ERR_LOST;
            continue;
        }
        if (candidate->state == ASTRA_VFS_FILE_TRANSFERRING || astra_vfs_open_file_active(candidate)) {
            reply->status = ASTRA_VFS_ERR_BUSY;
            return;
        }
        pending = candidate; source = owner;
        break;
    }
    if (pending == NULL) return;
    if (request->flags != 0u) reply->status = close_file(service, pending);
    else {
        if (source->owner != slot->owner && owner_files_full(service, slot->owner)) {
            reply->status = ASTRA_VFS_ERR_LIMIT;
            return;
        }
        int reserve_target = source->owner != slot->owner;
        if (reserve_target) ++slot->open_files;
        /* The original owner stays charged while its token is in flight. */
        pending->state = ASTRA_VFS_FILE_TRANSFERRING;
        state_release(service);
        status = pending->backend.ops->adopt(pending->backend.context,
            request->offset, 0u, &node);
        if (!state_acquire(service)) { reply->status = ASTRA_VFS_ERR_IO; return; }
        if (reserve_target) --slot->open_files;
        if (status != ASTRA_VFS_OK) pending->state = ASTRA_VFS_FILE_SHARED;
        else if (node == 0u) {
            finish_file_close(service, pending, 0u);
            status = ASTRA_VFS_ERR_PROTOCOL;
        } else {
            --source->open_files;
            ++slot->open_files;
            pending->session = slot->id;
            pending->token = 0u;
            pending->node = node;
            pending->state = ASTRA_VFS_FILE_OPEN;
            notify_holders_changed(service);
            reply->file = HANDLE_MAKE((uint32_t)(pending - service->files), pending->generation);
        }
        reply->status = status;
    }
    if (source != slot && source->closing && source->inflight == 0u)
        reap_session(service, source);
}

uint32_t astra_vfs_service_borrow_share(AstraVfsService *service,
    uint32_t owner, uint64_t token, AstraVfsSharedOperation operation, void *context)
{
    uint32_t status = ASTRA_VFS_ERR_BAD_HANDLE;
    if (service == NULL || token == 0u || operation == NULL) return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    for (uint32_t at = 0u; at < service->file_high_water; ++at) {
        AstraVfsOpenFile *file = &service->files[at];
        if (!shared_token_matches(file, token)) continue;
        AstraVfsSessionSlot *slot = find_session(service, file->session);
        if (slot == NULL) { status = ASTRA_VFS_ERR_PROTOCOL; break; }
        if (slot->owner != owner) { status = ASTRA_VFS_ERR_ACCESS; continue; }
        if (file->state != ASTRA_VFS_FILE_SHARED) { status = ASTRA_VFS_ERR_BUSY; break; }
        slot = begin_session_request(service, slot->id, &status);
        if (slot == NULL) break;
        if (!file_acquire_for(service, file, ASTRA_VFS_FILE_SHARED)) {
            status = ASTRA_VFS_ERR_BUSY;
        } else {
            state_release(service);
            status = operation(context, file);
            if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
            file_release(service, file);
        }
        finish_session_request(service, slot);
        break;
    }
    state_release(service);
    return status;
}

/* Node I/O stays owned; the state lock is not held across this byte operation. */
static uint32_t
read_owned_data(AstraVfsService *service, AstraVfsOpenFile *file,
                   uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
                   uint32_t *moved, uint64_t *position)
{
    uint32_t status;

    *moved = 0u;
    *position = offset;
    if ((flags & ~ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((service->cache_ops == NULL || service->cache_ops->read == NULL) &&
        file->io->cache != NULL)
        return file->io->cache->error == ASTRA_VFS_OK ? ASTRA_VFS_ERR_BUSY :
            file->io->cache->error;
    status = service->cache_ops != NULL && service->cache_ops->read != NULL ?
        service->cache_ops->read(service->cache_context, file, offset, flags,
            buffer, length, moved, position) :
        astra_vfs_backend_read_position(&file->backend, file->node,
            offset, flags, buffer, length, moved, position);
    if (status == ASTRA_VFS_ERR_PROTOCOL ||
        !astra_vfs_io_result_valid(status, flags, length, *moved, *position)) {
        *moved = 0u;
        *position = offset;
        return ASTRA_VFS_ERR_IO;
    }
    return status;
}

static uint32_t
read_acquired_file(AstraVfsService *service, AstraVfsOpenFile *file,
    uint64_t offset, uint32_t flags, void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{
    state_release(service);
    uint32_t status = read_owned_data(service, file, offset, flags, buffer,
        length, moved, position);
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    return status;
}

static uint32_t
read_file(AstraVfsService *service, AstraVfsOpenFile *file, uint64_t offset,
          uint32_t flags, void *buffer, uint32_t length, uint32_t *moved,
          uint64_t *position)
{
    *moved = 0u;
    *position = offset;
    if ((file->flags & ASTRA_VFS_OPEN_READ) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire(service, file))
        return ASTRA_VFS_ERR_IO;
    return read_acquired_file(service, file, offset, flags, buffer, length, moved, position);
}

static void
handle_read(AstraVfsService *service, AstraVfsOpenFile *file,
            const AstraVfsRequest *request, AstraVfsReply *reply)
{
    uint32_t length = request->length > ASTRA_VFS_IO_MAX ?
        ASTRA_VFS_IO_MAX : request->length;

    uint32_t moved = 0u;
    uint64_t position = request->offset;
    reply->status = read_file(service, file, request->offset, request->flags,
                              reply->payload, length, &moved, &position);
    reply->count = moved;
    reply->position = position;
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

static uint32_t
read_into_unlocked(AstraVfsService *service, uint32_t session,
                   AstraVfsFile handle, uint64_t offset, uint32_t flags, void *buffer,
                   uint32_t length, uint32_t *moved, uint64_t *position)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || buffer == NULL || moved == NULL || position == NULL || length == 0u) {
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
    return read_file(service, file, offset, flags, buffer, length, moved, position);
}

uint32_t
astra_vfs_service_read_position_into(AstraVfsService *service, uint32_t session,
                            AstraVfsFile handle, uint64_t offset, uint32_t flags,
                            void *buffer, uint32_t length, uint32_t *moved, uint64_t *position)
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
    status = read_into_unlocked(service, session, handle, offset, flags, buffer,
                                length, moved, position);
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

static AstraVfsOpenFile *
find_retained(AstraVfsService *service, AstraVfsFile token)
{
    uint32_t slot;
    AstraVfsOpenFile *file;

    if (token == ASTRA_VFS_FILE_INVALID ||
        (token & HANDLE_SLOT_MASK) == 0u)
        return NULL;
    slot = HANDLE_SLOT(token);
    if (slot >= service->file_high_water)
        return NULL;
    file = &service->files[slot];
    if (file->retained == 0u ||
        file->generation != HANDLE_GENERATION(token) ||
        (file->state != ASTRA_VFS_FILE_OPEN &&
         file->state != ASTRA_VFS_FILE_DETACHED))
        return NULL;
    return file;
}

/* Waiting for a busy file can release the lock while its slot is retired. */
static uint32_t
acquire_retained(AstraVfsService *service, AstraVfsFile token,
                 uint32_t access, AstraVfsOpenFile **result)
{
    AstraVfsOpenFile *file = find_retained(service, token);
    uint8_t state;

    if (file == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if ((file->flags & access) != access)
        return ASTRA_VFS_ERR_ACCESS;
    state = file->state;
    if (!file_acquire_for(service, file, state))
        return ASTRA_VFS_ERR_BUSY;
    if (find_retained(service, token) != file) {
        file_release(service, file);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    *result = file;
    return ASTRA_VFS_OK;
}

static uint32_t retain_file_access(AstraVfsOpenFile *file, uint32_t access)
{
    if (file->kind != ASTRA_VFS_KIND_FILE || (file->flags & access) != access)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->retained == UINT16_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    ++file->retained;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_service_retain_file(AstraVfsService *service, uint32_t session,
                              AstraVfsFile handle, uint32_t access,
                              AstraVfsFile *retained)
{
    AstraVfsOpenFile *file;
    uint32_t status;

    if (retained != NULL)
        *retained = ASTRA_VFS_FILE_INVALID;
    if (service == NULL || retained == NULL || access == 0u ||
        (access & ~(ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    AstraVfsSessionSlot *slot = begin_session_request(service, session, &status);
    if (slot == NULL) {
        state_release(service);
        return status;
    }
    file = find_file(service, session, handle, &status);
    if (file != NULL) {
        status = retain_file_access(file, access);
        if (status == ASTRA_VFS_OK)
            *retained = handle;
    }
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

uint32_t
astra_vfs_service_retain_active(AstraVfsService *service,
    const AstraVfsOpenFile *active, AstraVfsFile *copy)
{
    if (copy != NULL) *copy = ASTRA_VFS_FILE_INVALID;
    if (service == NULL || active == NULL || copy == NULL) return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    AstraVfsOpenFile *file = NULL;
    uint32_t at;
    for (at = 0u; at < service->file_high_water; ++at)
        if (&service->files[at] == active) { file = &service->files[at]; break; }
    uint32_t status = file == NULL || !astra_vfs_open_file_active(file) ||
        (file->state != ASTRA_VFS_FILE_OPEN && file->state != ASTRA_VFS_FILE_DETACHED) ?
        ASTRA_VFS_ERR_BAD_HANDLE :
        retain_file_access(file, 0u);
    if (status == ASTRA_VFS_OK) *copy = HANDLE_MAKE(at, file->generation);
    state_release(service);
    return status;
}

uint32_t
astra_vfs_service_read_retained(AstraVfsService *service,
                                AstraVfsFile retained, uint64_t offset,
                                void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsOpenFile *file;
    uint32_t status;
    uint64_t position = offset;

    if (service == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    status = acquire_retained(service, retained, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) {
        state_release(service);
        return status;
    }
    status = read_acquired_file(service, file, offset, 0u, buffer, length, moved, &position);
    state_release(service);
    return status;
}

uint32_t
astra_vfs_backend_read_page(const AstraVfsBackend *backend,
                            uintptr_t node, uint32_t access,
                            const AstraVfsNodeMeta *expected_identity,
                            const AstraVfsNodeInfo *expected_revision,
                            uint64_t offset, void *page,
                            uint32_t *valid_bytes)
{
    AstraVfsNodeInfo before = {0}, after = {0};
    uint64_t provider_nodes_before = 0u, provider_nodes_after = 0u;
    uint64_t provider_entries;
    uint32_t status, wanted, copied = 0u;
    uint32_t (*read_bytes)(void *, uintptr_t, uint64_t, void *, uint32_t, uint32_t *);

    if (valid_bytes != NULL)
        *valid_bytes = 0u;
    if (backend == NULL || backend->ops == NULL ||
        backend->ops->stat_node == NULL || !astra_vfs_page_access_valid(access) ||
        node == 0u || page == NULL || valid_bytes == NULL ||
        (offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        (expected_identity != NULL &&
         !astra_vfs_node_identity_equal(expected_identity,
                                        expected_identity)))
        return ASTRA_VFS_ERR_INVALID;
    read_bytes = access == ASTRA_VFS_OPEN_WRITE ?
        backend->ops->read_for_write : backend->ops->read;
    if (read_bytes == NULL)
        return access == ASTRA_VFS_OPEN_WRITE ? ASTRA_VFS_ERR_UNSUPPORTED : ASTRA_VFS_ERR_INVALID;
    if (backend->ops->change_snapshot != NULL) {
        status = backend->ops->change_snapshot(
            backend->context, &provider_nodes_before, &provider_entries);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    status = backend->ops->stat_node(backend->context, node, &before);
    if (status != ASTRA_VFS_OK)
        return status;
    if (before.kind != ASTRA_VFS_KIND_FILE ||
        (offset >= before.size && expected_revision == NULL))
        return ASTRA_VFS_ERR_INVALID;
    if (expected_identity != NULL &&
        !astra_vfs_node_identity_equal(expected_identity, &before.meta))
        return ASTRA_VFS_ERR_LOST;
    if (expected_revision != NULL) {
        if (astra_vfs_node_identity_equal(&expected_revision->meta, &expected_revision->meta) &&
            !astra_vfs_node_identity_equal(&expected_revision->meta, &before.meta))
            return ASTRA_VFS_ERR_LOST;
        if (!astra_vfs_node_revision_equal(expected_revision, &before))
            return ASTRA_VFS_ERR_BUSY;
    }
    wanted = offset >= before.size ? 0u : before.size - offset < ASTRA_MEMORY_PAGE_SIZE ?
        (uint32_t)(before.size - offset) : ASTRA_MEMORY_PAGE_SIZE;
    while (copied < wanted) {
        uint32_t moved = 0u;

        status = read_bytes(backend->context, node, offset + copied,
                                    (uint8_t *)page + copied,
                                    wanted - copied, &moved);
        if (status != ASTRA_VFS_OK)
            return status;
        if (moved == 0u || moved > wanted - copied)
            return ASTRA_VFS_ERR_IO;
        copied += moved;
    }
    status = backend->ops->stat_node(backend->context, node, &after);
    if (status != ASTRA_VFS_OK)
        return status;
    if (backend->ops->change_snapshot != NULL) {
        status = backend->ops->change_snapshot(
            backend->context, &provider_nodes_after, &provider_entries);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (after.kind != ASTRA_VFS_KIND_FILE ||
        (expected_identity != NULL &&
         !astra_vfs_node_identity_equal(expected_identity, &after.meta)) ||
        (astra_vfs_node_identity_equal(&before.meta, &before.meta) &&
         !astra_vfs_node_identity_equal(&before.meta, &after.meta)))
        return ASTRA_VFS_ERR_LOST;
    /* shortcut: unrelated writes can force retry; use per-node generation
     * once cache objects own mutation state. */
    if (!astra_vfs_node_revision_equal(&before, &after) ||
        provider_nodes_after != provider_nodes_before)
        return ASTRA_VFS_ERR_BUSY;
    *valid_bytes = copied;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_service_read_page_retained(AstraVfsService *service,
                                     AstraVfsFile retained, uint32_t access,
                                     const AstraVfsNodeMeta *expected_identity,
                                     uint64_t offset, void *page,
                                     uint32_t *valid_bytes)
{
    AstraVfsChangeSnapshot changes_before, changes_after;
    AstraVfsOpenFile *file;
    AstraVfsFileCache cache = {0};
    AstraVfsFile borrowed = retained;
    uint32_t status, copied = 0u, sequence;
    uintptr_t node;

    if (valid_bytes != NULL)
        *valid_bytes = 0u;
    if (service == NULL || page == NULL || valid_bytes == NULL ||
        !astra_vfs_page_access_valid(access) ||
        (offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        (expected_identity != NULL &&
         !astra_vfs_node_identity_equal(expected_identity,
                                        expected_identity)))
        return ASTRA_VFS_ERR_INVALID;
    if (!astra_vfs_service_change_snapshot(service, &changes_before))
        return ASTRA_VFS_ERR_IO;
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file = find_retained(service, retained);
    status = file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
        retain_file_access(file, access);
    if (status != ASTRA_VFS_OK) {
        state_release(service);
        return status;
    }
    node = file->node;
    sequence = file->io->sequence;
    if (file->io->cache != NULL) cache = *file->io->cache;
    state_release(service);
    status = cache.writer != ASTRA_VFS_FILE_INVALID && offset >= cache.size ?
        ASTRA_VFS_ERR_INVALID : astra_vfs_backend_read_page(&file->backend, node, access,
            expected_identity, cache.writer != ASTRA_VFS_FILE_INVALID ? &cache.backing : NULL,
            offset, page, &copied);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    int changed = file->io->active != NULL || file->io->sequence != sequence;
    state_release(service);
    uint32_t released = astra_vfs_service_release_retained(service, &borrowed);
    if (cache.writer != ASTRA_VFS_FILE_INVALID && changed) return ASTRA_VFS_ERR_BUSY;
    if (status != ASTRA_VFS_OK)
        return status;
    if (released != ASTRA_VFS_OK)
        return released;
    if (changed)
        return ASTRA_VFS_ERR_BUSY;
    if (!astra_vfs_service_change_snapshot(service, &changes_after))
        return ASTRA_VFS_ERR_IO;
    /* shortcut: unrelated writes can force retry; use per-node generation
     * once cache objects own mutation state. */
    if (changes_after.nodes != changes_before.nodes)
        return ASTRA_VFS_ERR_BUSY;
    if (cache.writer != ASTRA_VFS_FILE_INVALID) {
        uint32_t valid = cache.size - offset < ASTRA_MEMORY_PAGE_SIZE ?
            (uint32_t)(cache.size - offset) : ASTRA_MEMORY_PAGE_SIZE;
        if (copied > valid) return ASTRA_VFS_ERR_BUSY;
        memset((uint8_t *)page + copied, 0, valid - copied);
        copied = valid;
    }
    *valid_bytes = copied;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_backend_write_page(const AstraVfsBackend *backend, uintptr_t node,
                             const AstraVfsNodeInfo *expected, uint64_t offset,
                             uint64_t logical_size,
                             const AstraVfsNodeInfo *modified,
                             const void *page, uint32_t *moved,
                             AstraVfsNodeInfo *current)
{
    AstraVfsNodeInfo previous = expected != NULL ? *expected :
                                                 (AstraVfsNodeInfo){0};
    AstraVfsNodeInfo before = {0}, after = {0};
    uint32_t status, checked, wanted;

    if (moved != NULL) *moved = 0u;
    if (current != NULL) node_info_clear(current);
    if (backend == NULL || backend->ops == NULL ||
        backend->ops->stat_node == NULL || backend->ops->write == NULL ||
        backend->ops->sync == NULL || node == 0u || expected == NULL ||
        page == NULL || moved == NULL || current == NULL ||
        previous.kind != ASTRA_VFS_KIND_FILE || offset >= logical_size ||
        (offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (modified != NULL && backend->ops->set_file_times == NULL)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (modified != NULL && !astra_vfs_file_times_valid(ASTRA_VFS_META_MTIME,
            0u, modified->meta.mtime_nsec))
        return ASTRA_VFS_ERR_INVALID;
    status = backend->ops->stat_node(backend->context, node, &before);
    if (status != ASTRA_VFS_OK)
        return status;
    if (before.kind != ASTRA_VFS_KIND_FILE ||
        (astra_vfs_node_identity_equal(&previous.meta, &previous.meta) &&
         !astra_vfs_node_identity_equal(&previous.meta, &before.meta)))
        return ASTRA_VFS_ERR_LOST;
    if (!astra_vfs_node_revision_equal(&previous, &before))
        return ASTRA_VFS_ERR_BUSY;
    wanted = logical_size - offset < ASTRA_MEMORY_PAGE_SIZE ?
        (uint32_t)(logical_size - offset) : ASTRA_MEMORY_PAGE_SIZE;
    uint64_t resulting_size = previous.size > offset + wanted ?
        previous.size : offset + wanted;
    while (*moved < wanted) {
        uint32_t accepted = 0u;
        uint64_t position = offset + *moved;

        status = backend->ops->write(backend->context, node, position, 0u,
            logical_size, (const uint8_t *)page + *moved, wanted - *moved,
            &accepted, &position);
        if (accepted > wanted - *moved ||
            position != offset + *moved + accepted) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        *moved += accepted;
        if (status != ASTRA_VFS_OK)
            break;
        if (accepted == 0u) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
    }
    if (status == ASTRA_VFS_OK && modified != NULL)
        status = backend->ops->set_file_times(backend->context, node,
            modified, ASTRA_VFS_META_MTIME);
    if (status == ASTRA_VFS_OK)
        status = backend->ops->sync(backend->context, node);
    /* A failed prefix may still change metadata; its owner needs that fact. */
    checked = backend->ops->stat_node(backend->context, node, &after);
    if (checked != ASTRA_VFS_OK)
        return status != ASTRA_VFS_OK ? status : checked;
    if (after.kind != ASTRA_VFS_KIND_FILE ||
        (astra_vfs_node_identity_equal(&before.meta, &before.meta) &&
         !astra_vfs_node_identity_equal(&before.meta, &after.meta)))
        return ASTRA_VFS_ERR_LOST;
    *current = after;
    if (status != ASTRA_VFS_OK)
        return status;
    return after.size == resulting_size ? ASTRA_VFS_OK : ASTRA_VFS_ERR_BUSY;
}

uint32_t
astra_vfs_service_write_page_retained(AstraVfsService *service,
    AstraVfsFile retained, const AstraVfsOpenFile *active,
    const AstraVfsNodeInfo *expected, uint64_t offset,
    uint64_t logical_size,
    const void *page, uint32_t *moved, AstraVfsNodeInfo *current)
{
    AstraVfsOpenFile *file;
    AstraVfsNodeInfo modified = {0};
    const AstraVfsNodeInfo *times = NULL;
    uint32_t status;

    if (moved != NULL) *moved = 0u;
    if (service == NULL || expected == NULL || page == NULL ||
        moved == NULL || current == NULL) {
        if (current != NULL) node_info_clear(current);
        return ASTRA_VFS_ERR_INVALID;
    }
    if (!state_acquire(service)) {
        node_info_clear(current);
        return ASTRA_VFS_ERR_IO;
    }
    if (active == NULL)
        status = acquire_retained(service, retained, ASTRA_VFS_OPEN_WRITE, &file);
    else {
        file = find_retained(service, retained);
        status = file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
            (file->flags & ASTRA_VFS_OPEN_WRITE) == 0u || file->io->active != active ?
                ASTRA_VFS_ERR_ACCESS : ASTRA_VFS_OK;
    }
    if (status != ASTRA_VFS_OK) {
        node_info_clear(current);
        state_release(service);
        return status;
    }
    if (file->io->cache != NULL &&
        (logical_size != file->io->cache->size ||
         !astra_vfs_node_revision_equal(expected, &file->io->cache->backing))) {
        node_info_clear(current);
        if (active == NULL) file_release(service, file);
        state_release(service);
        return ASTRA_VFS_ERR_BUSY;
    }
    if (file->io->cache != NULL && file->io->cache->written &&
        !file->io->cache->external_writes) {
        modified.mtime = (int64_t)(file->io->cache->modified_ns / UINT64_C(1000000000));
        modified.meta.mtime_nsec = (uint32_t)(file->io->cache->modified_ns % UINT64_C(1000000000));
        times = &modified;
    }
    state_release(service);
    status = astra_vfs_backend_write_page(&file->backend, file->node,
        expected, offset, logical_size, times, page, moved, current);
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    if (file->io->cache != NULL) {
        if (current->kind == ASTRA_VFS_KIND_FILE)
            file->io->cache->backing = *current;
        astra_vfs_file_cache_record_status(file->io->cache, status);
    }
    if (*moved != 0u) ++service->changes.nodes;
    if (active == NULL) file_release(service, file);
    state_release(service);
    return status;
}

/* Returns whether the state lock was restored; node I/O stays owned. */
static int stat_owned_file(AstraVfsService *service,
    AstraVfsOpenFile *file, AstraVfsNodeInfo *info, uint32_t *status)
{
    state_release(service);
    *status = read_file_info(service, file, info);
    if (!state_acquire(service)) {
        *status = ASTRA_VFS_ERR_IO;
        return 0;
    }
    if (*status == ASTRA_VFS_OK)
        *status = cache_node_info(service, &file->backend, file->io, info);
    return 1;
}

uint32_t
astra_vfs_service_stat_retained(AstraVfsService *service,
                                AstraVfsFile retained, AstraVfsNodeInfo *info)
{
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    node_info_clear(info);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    status = acquire_retained(service, retained, 0u, &file);
    if (status != ASTRA_VFS_OK) {
        state_release(service);
        return status;
    }
    if (!stat_owned_file(service, file, info, &status)) return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    state_release(service);
    return status;
}

/* Called with the state lock held; returns with it held. */
static uint32_t release_retained_locked(AstraVfsService *service,
    AstraVfsFile *retained)
{
    AstraVfsOpenFile *file;
    uintptr_t node;
    uint32_t status;

    file = find_retained(service, *retained);
    if (file == NULL) {
        *retained = ASTRA_VFS_FILE_INVALID;
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    if (file->retained == 1u && file->io->cache != NULL &&
        file->io->cache->writer == *retained)
        return ASTRA_VFS_ERR_BUSY;
    if (file->state == ASTRA_VFS_FILE_OPEN || file->retained > 1u) {
        --file->retained;
        *retained = ASTRA_VFS_FILE_INVALID;
        return ASTRA_VFS_OK;
    }
    file->state = ASTRA_VFS_FILE_CLOSING;
    if (!file_wait_idle(service, file)) {
        file->state = ASTRA_VFS_FILE_DETACHED;
        return ASTRA_VFS_ERR_IO;
    }
    node = file->node;
    state_release(service);
    status = file->backend.ops->close(file->backend.context, node);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    finish_file_close(service, file, 1u);
    *retained = ASTRA_VFS_FILE_INVALID;
    return status;
}

uint32_t
astra_vfs_service_release_retained(AstraVfsService *service,
                                    AstraVfsFile *retained)
{
    if (retained == NULL) return ASTRA_VFS_ERR_INVALID;
    if (service == NULL || !state_acquire(service)) return ASTRA_VFS_ERR_IO;
    uint32_t status = release_retained_locked(service, retained);
    state_release(service);
    return status;
}

uint32_t
astra_vfs_service_read_path(AstraVfsService *service, uint32_t session, const char *path,
    void *buffer, uint32_t capacity, uint32_t *moved, uint64_t *node_size)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file = NULL;
    AstraVfsRequest request = {.flags = ASTRA_VFS_OPEN_READ};
    AstraVfsReply reply = {0};
    AstraVfsNodeInfo info = {0};
    AstraVfsFile retained = ASTRA_VFS_FILE_INVALID;
    uint32_t status;

    if (moved != NULL) *moved = 0u;
    if (node_size != NULL) *node_size = 0u;
    if (service == NULL || path == NULL || buffer == NULL || capacity == 0u ||
        moved == NULL || node_size == NULL) return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    slot = begin_session_request(service, session, &status);
    if (slot == NULL) { state_release(service); return status; }
    handle_open(service, session, &request, path, NULL, &reply);
    status = reply.status;
    if (status != ASTRA_VFS_OK) goto done;
    file = find_file(service, session, reply.file, &status);
    if (file == NULL) goto done;
    *node_size = reply.node_size;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY) status = ASTRA_VFS_ERR_IS_DIR;
    else status = retain_file_access(file, ASTRA_VFS_OPEN_READ);
    if (status == ASTRA_VFS_OK) retained = reply.file;
    if (status == ASTRA_VFS_OK)
        status = acquire_retained(service, retained, ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_OK) {
        if (!stat_owned_file(service, file, &info, &status)) return ASTRA_VFS_ERR_IO;
        if (status == ASTRA_VFS_OK) {
            *node_size = info.size;
            if (info.kind != ASTRA_VFS_KIND_FILE) status = ASTRA_VFS_ERR_IO;
            else if (*node_size > capacity) status = ASTRA_VFS_ERR_LIMIT;
        }
        if (status == ASTRA_VFS_OK) {
            state_release(service);
            while (*moved < *node_size) {
                uint32_t chunk = 0u;
                uint64_t position = *moved;
                status = read_owned_data(service, file, *moved, 0u,
                    (uint8_t *)buffer + *moved, (uint32_t)*node_size - *moved,
                    &chunk, &position);
                *moved += chunk;
                if (status != ASTRA_VFS_OK) break;
                if (chunk == 0u) { status = ASTRA_VFS_ERR_IO; break; }
            }
            if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
        }
        file_release(service, file);
    }
    if (file->state == ASTRA_VFS_FILE_OPEN) {
        uint32_t closed = close_file(service, file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (retained != ASTRA_VFS_FILE_INVALID) {
        uint32_t closed = release_retained_locked(service, &retained);
        if (status == ASTRA_VFS_OK) status = closed;
    }
done:
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

uint32_t astra_vfs_service_file_area(AstraVfsService *service,
    uint32_t session, AstraVfsFile handle, uint64_t base, uint32_t span,
    AstraVfsFileAreaCreate create, void *context,
    uint32_t *area, uint64_t *file_size)
{
    AstraVfsNodeInfo info = {0};
    AstraVfsFile owned = ASTRA_VFS_FILE_INVALID;
    uint32_t status;
    if (area != NULL) *area = 0u;
    if (file_size != NULL) *file_size = 0u;
    if (service == NULL || create == NULL || area == NULL || file_size == NULL || span == 0u ||
        span > ASTRA_AREA_SIZE_MAX ||
        ((base | span) & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        span > ASTRA_AREA_SIZE_MAX - base % ASTRA_AREA_SIZE_MAX ||
        base > UINT64_MAX - span)
        return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    AstraVfsSessionSlot *slot = begin_session_request(service, session, &status);
    if (slot == NULL) { state_release(service); return status; }
    AstraVfsOpenFile *file = find_file(service, session, handle, &status);
    if (file == NULL) goto done;
    if ((file->flags & ASTRA_VFS_OPEN_READ) == 0u || file->kind != ASTRA_VFS_KIND_FILE) {
        status = ASTRA_VFS_ERR_ACCESS;
        goto done;
    }
    if (!file_acquire(service, file)) { status = ASTRA_VFS_ERR_BUSY; goto done; }
    status = retain_file_access(file, ASTRA_VFS_OPEN_READ);
    if (status == ASTRA_VFS_OK) {
        owned = handle;
        if (!stat_owned_file(service, file, &info, &status)) return ASTRA_VFS_ERR_IO;
        if (status == ASTRA_VFS_OK && info.kind != ASTRA_VFS_KIND_FILE) status = ASTRA_VFS_ERR_IO;
        if (status == ASTRA_VFS_OK) {
            state_release(service);
            status = create(context, file, &owned, &info, base, span, area);
            if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
            if (status == ASTRA_VFS_OK && *area == 0u) status = ASTRA_VFS_ERR_IO;
        }
    }
    file_release(service, file);
    if (owned != ASTRA_VFS_FILE_INVALID) {
        uint32_t released = release_retained_locked(service, &owned);
        if (status == ASTRA_VFS_OK) status = released;
    }
    if (status == ASTRA_VFS_OK) *file_size = info.size;
done:
    finish_session_request(service, slot);
    state_release(service);
    return status;
}

uint32_t astra_vfs_service_cache_prepare(AstraVfsService *service,
    const AstraVfsOpenFile *file, AstraVfsFile *owned, uint32_t flags,
    AstraVfsFileCache *storage, AstraVfsFileCache *snapshot)
{
    AstraVfsNodeInfo backing = {0};
    if (service == NULL || file == NULL || owned == NULL || snapshot == NULL ||
        (flags & ~ASTRA_VFS_CACHE_EXTERNAL_WRITES) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    int admitted = find_retained(service, *owned) == file &&
        astra_vfs_open_file_active(file) && (file->flags & ASTRA_VFS_OPEN_WRITE) != 0u;
    uint32_t admission = !admitted ? ASTRA_VFS_ERR_ACCESS :
        file->io->incarnation == 0u ||
        !astra_vfs_node_identity_equal(&file->io->identity, &file->io->identity) ?
            ASTRA_VFS_ERR_UNSUPPORTED : ASTRA_VFS_OK;
    state_release(service);
    if (admission != ASTRA_VFS_OK) return admission;
    if (file->backend.ops->set_file_times == NULL) return ASTRA_VFS_ERR_UNSUPPORTED;
    uint32_t status = file->backend.ops->stat_node(file->backend.context, file->node, &backing);
    if (status != ASTRA_VFS_OK) return status;
    if (backing.kind != ASTRA_VFS_KIND_FILE || backing.size > INT64_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    AstraVfsFileIO *io = file->io;
    if (astra_vfs_node_identity_equal(&io->identity, &io->identity) &&
        !astra_vfs_node_identity_equal(&io->identity, &backing.meta))
        status = ASTRA_VFS_ERR_LOST;
    else if (io->cache != NULL &&
        !astra_vfs_node_revision_equal(&io->cache->backing, &backing))
        status = ASTRA_VFS_ERR_BUSY;
    else if (io->cache == NULL && storage == NULL)
        status = ASTRA_VFS_ERR_LIMIT;
    else if (io->cache == NULL && storage->writer != ASTRA_VFS_FILE_INVALID)
        status = ASTRA_VFS_ERR_BUSY;
    else {
        if (io->cache == NULL) {
            *storage = (AstraVfsFileCache){.backing = backing,
                .size = backing.size, .writer = *owned};
            io->cache = storage;
            *owned = ASTRA_VFS_FILE_INVALID;
            ++service->cached_nodes;
        }
        if ((flags & ASTRA_VFS_CACHE_EXTERNAL_WRITES) != 0u)
            io->cache->external_writes = 1u;
        *snapshot = *io->cache;
    }
    state_release(service);
    return status;
}

uint32_t astra_vfs_service_cache_publish(AstraVfsService *service,
    const AstraVfsOpenFile *file, uint64_t size, uint64_t modified_ns)
{
    if (service == NULL || file == NULL || size > INT64_MAX) return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    uint32_t status = ASTRA_VFS_ERR_ACCESS;
    if (astra_vfs_open_file_active(file) &&
        file->io->cache != NULL &&
        (file->flags & ASTRA_VFS_OPEN_WRITE) != 0u) {
        AstraVfsOpenFile *writer = find_retained(service, file->io->cache->writer);
        if (writer == NULL || writer->io != file->io) status = ASTRA_VFS_ERR_ACCESS;
        else if (size < file->io->cache->size) status = ASTRA_VFS_ERR_INVALID;
        else {
            file->io->cache->size = size;
            file->io->cache->modified_ns = modified_ns;
            file->io->cache->written = 1u;
            status = ASTRA_VFS_OK;
        }
    }
    state_release(service);
    return status;
}

uint32_t astra_vfs_service_cache_snapshot(AstraVfsService *service,
    AstraVfsFile retained, AstraVfsFileCache *snapshot, uint32_t *sequence)
{
    if (service == NULL || snapshot == NULL || sequence == NULL) return ASTRA_VFS_ERR_INVALID;
    if (!state_acquire(service)) return ASTRA_VFS_ERR_IO;
    AstraVfsOpenFile *file = find_retained(service, retained);
    if (file != NULL) {
        *snapshot = file->io->cache == NULL ? (AstraVfsFileCache){0} : *file->io->cache;
        *sequence = file->io->sequence;
    }
    state_release(service);
    return file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE : ASTRA_VFS_OK;
}

uint32_t astra_vfs_service_cache_retire(AstraVfsService *service,
    AstraVfsFile retained, uint32_t sequence)
{
    if (service == NULL || !state_acquire(service)) return ASTRA_VFS_ERR_IO;
    AstraVfsOpenFile *file = find_retained(service, retained);
    uint32_t status = file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE : ASTRA_VFS_OK;
    if (file != NULL && file->io->cache != NULL) {
        AstraVfsFileIO *io = file->io;
        AstraVfsFileCache *cache = io->cache;
        if (io->active != NULL || io->sequence != sequence ||
            cache->size != cache->backing.size)
            status = ASTRA_VFS_ERR_BUSY;
        else if (cache->error != ASTRA_VFS_OK) status = cache->error;
        else {
            status = acquire_retained(service, retained, 0u, &file);
            if (status == ASTRA_VFS_OK) {
                AstraVfsFile writer = cache->writer;
                io->cache = NULL;
                memset(cache, 0, sizeof(*cache));
                --service->cached_nodes;
                file_release(service, file);
                status = release_retained_locked(service, &writer);
            }
        }
    }
    state_release(service);
    return status;
}

static uint32_t
write_file(AstraVfsService *service, AstraVfsOpenFile *file, uint64_t offset,
           uint8_t state, AstraVfsFile retained, uint32_t flags, uint64_t max_end,
           const void *buffer, uint32_t length,
           uint32_t *moved, uint64_t *position)
{
    uint32_t status;
    AstraVfsFile owned = ASTRA_VFS_FILE_INVALID;

    *moved = 0u;
    *position = offset;
    if ((flags & ~(ASTRA_VFS_WRITE_APPEND | ASTRA_VFS_IO_SEQUENTIAL)) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (!file_acquire_for(service, file, state))
        return retained == ASTRA_VFS_FILE_INVALID ? ASTRA_VFS_ERR_IO :
            ASTRA_VFS_ERR_BUSY;
    if (retained != ASTRA_VFS_FILE_INVALID &&
        find_retained(service, retained) != file) {
        file_release(service, file);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    if (service->cache_ops != NULL) {
        status = retain_file_access(file, ASTRA_VFS_OPEN_WRITE);
        if (status != ASTRA_VFS_OK) {
            file_release(service, file);
            return status;
        }
        owned = HANDLE_MAKE((uint32_t)(file - service->files), file->generation);
    }
    state_release(service);
    if (service->cache_ops != NULL)
        status = service->cache_ops->write(service->cache_context, file, &owned,
            offset, flags, max_end, buffer, length, moved, position);
    else
        status = file->backend.ops->write(file->backend.context, file->node,
            offset, flags, max_end, buffer, length, moved, position);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file_release(service, file);
    if (*moved != 0u) ++service->changes.nodes;
    if (*moved > length) {
        *moved = 0u;
        *position = offset;
        status = ASTRA_VFS_ERR_IO;
    }
    if (owned != ASTRA_VFS_FILE_INVALID) {
        uint32_t released = release_retained_locked(service, &owned);
        if (status == ASTRA_VFS_OK) status = released;
    }
    return status;
}

uint32_t
astra_vfs_service_write_retained(AstraVfsService *service,
                                 AstraVfsFile retained, uint64_t offset,
                                 uint64_t max_end, const void *buffer,
                                 uint32_t length, uint32_t *moved)
{
    AstraVfsOpenFile *file;
    uint64_t position = offset;
    uint32_t status;

    if (service == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file = find_retained(service, retained);
    status = file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
        write_file(service, file, offset, file->state, retained, 0u,
                   max_end, buffer, length, moved, &position);
    state_release(service);
    return status;
}

/* Called with the service state lock held; returns with it held. */
static uint32_t
sync_file(AstraVfsService *service, AstraVfsOpenFile *file,
          uint8_t state, AstraVfsFile retained, uint32_t *remaining)
{
    uint32_t status;

    if (!file_acquire_for(service, file, state))
        return retained == ASTRA_VFS_FILE_INVALID ? ASTRA_VFS_ERR_IO :
            ASTRA_VFS_ERR_BUSY;
    if (retained != ASTRA_VFS_FILE_INVALID &&
        find_retained(service, retained) != file) {
        file_release(service, file);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    status = flush_cache(service, file, remaining);
    if (status != ASTRA_VFS_OK) {
        file_release(service, file);
        return status;
    }
    state_release(service);
    status = file->backend.ops->sync(file->backend.context, file->node);
    if (!state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    astra_vfs_file_cache_record_status(file->io->cache, status);
    file_release(service, file);
    return status;
}

uint32_t
astra_vfs_service_sync_retained(AstraVfsService *service,
                                AstraVfsFile retained, uint32_t *remaining)
{
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || !state_acquire(service))
        return ASTRA_VFS_ERR_IO;
    file = find_retained(service, retained);
    status = file == NULL ? ASTRA_VFS_ERR_BAD_HANDLE :
        remaining != NULL && (*remaining == 0u || file->io->active != NULL) ? ASTRA_VFS_ERR_BUSY :
        sync_file(service, file, file->state, retained, remaining);
    state_release(service);
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
    return write_file(service, file, offset, ASTRA_VFS_FILE_OPEN,
                      ASTRA_VFS_FILE_INVALID,
                      flags, max_end, buffer, length, moved,
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

/* Directory owners precede regular-file owners; pairs use the same address order. */
static void metadata_release(AstraVfsService *service,
    AstraVfsOpenFile **files, uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) file_release(service, files[i]);
    for (uint32_t i = 0u; i < count; ++i) {
        AstraVfsFile pin = HANDLE_MAKE((uint32_t)(files[i] - service->files),
            files[i]->generation);
        (void)release_retained_locked(service, &pin);
    }
}

static uint32_t metadata_acquire(AstraVfsService *service,
    const AstraVfsBackend *backend, const uintptr_t *bases,
    const AstraVfsPathOperands *paths, uint32_t count, uint32_t flags,
    AstraVfsOpenFile **files, uint32_t *file_count)
{
    AstraVfsNodeInfo info[2] = {{0}, {0}};
    const char *names[2] = {paths->first, paths->second};
    uint32_t status = ASTRA_VFS_OK, acquired = 0u;
    *file_count = 0u;
    if (service->cache_ops == NULL) return ASTRA_VFS_OK;
    state_release(service);
    for (uint32_t i = 0u; i < count; ++i) {
        status = backend->ops->stat_at(backend->context, bases[i], names[i], flags, &info[i]);
        if (status == ASTRA_VFS_ERR_NOT_FOUND && i == 1u) status = ASTRA_VFS_OK;
        if (status != ASTRA_VFS_OK) break;
    }
    (void)state_acquire(service);
    if (status != ASTRA_VFS_OK) return status;
    for (uint32_t i = 0u; i < count; ++i) {
        AstraVfsFileIO *io = find_node_io(service, backend, &info[i]);
        if (io == NULL ||
            (*file_count != 0u && files[0]->io == io)) continue;
        AstraVfsOpenFile *file = io->cache != NULL ? find_retained(service, io->cache->writer) : NULL;
        if (io->cache == NULL)
            for (uint32_t at = 0u; at < service->file_high_water; ++at)
                if (service->files[at].io == io &&
                    (service->files[at].state == ASTRA_VFS_FILE_OPEN ||
                     (service->files[at].state == ASTRA_VFS_FILE_DETACHED && service->files[at].retained != 0u))) {
                    file = &service->files[at]; break;
                }
        if (file == NULL) { status = ASTRA_VFS_ERR_LOST; break; }
        status = retain_file_access(file, 0u);
        if (status != ASTRA_VFS_OK) break;
        files[(*file_count)++] = file;
    }
    if (*file_count == 2u && (uintptr_t)files[1]->io < (uintptr_t)files[0]->io) {
        AstraVfsOpenFile *swap = files[0]; files[0] = files[1]; files[1] = swap;
    }
    while (status == ASTRA_VFS_OK && acquired < *file_count) {
        AstraVfsOpenFile *file = files[acquired];
        AstraVfsFile pin = HANDLE_MAKE((uint32_t)(file - service->files), file->generation);
        status = acquire_retained(service, pin, 0u, &file);
        if (status == ASTRA_VFS_OK) ++acquired;
    }
    for (uint32_t i = 0u; status == ASTRA_VFS_OK && i < acquired; ++i)
        status = flush_cache(service, files[i], NULL);
    if (status != ASTRA_VFS_OK) {
        for (uint32_t i = 0u; i < acquired; ++i) file_release(service, files[i]);
        for (uint32_t i = 0u; i < *file_count; ++i) {
            AstraVfsFile pin = HANDLE_MAKE((uint32_t)(files[i] - service->files), files[i]->generation);
            (void)release_retained_locked(service, &pin);
        }
        *file_count = 0u;
    }
    return status;
}

static void handle_path_operation(AstraVfsService *service, AstraVfsSessionSlot *slot,
    uint32_t operation, const AstraVfsRequest *request,
    const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsOpenFile *bases[2] = {NULL, NULL};
    AstraVfsFileIO *identities[2] = {NULL, NULL};
    uintptr_t nodes[2] = {0u, 0u};
    uint32_t ids[2] = {operation == ASTRA_VFS_OP_UNLINK || operation == ASTRA_VFS_OP_CHMOD ?
        0u : request->file,
        operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK ?
            request->second_file : 0u};
    uint32_t status = ASTRA_VFS_OK;
    uint32_t first = 0u;
    AstraVfsBackend backend = slot->backend;
    AstraVfsOpenFile *affected[2] = {NULL, NULL};
    uint32_t affected_count = 0u;
    AstraVfsPathOperands paths;

    status = astra_vfs_path_request(operation, request, transfer, &paths);
    if (status != ASTRA_VFS_OK ||
        ((operation == ASTRA_VFS_OP_UNLINK_AT || operation == ASTRA_VFS_OP_CHMOD_AT) && ids[0] == 0u)) {
        reply->status = status != ASTRA_VFS_OK ? status : ASTRA_VFS_ERR_BAD_HANDLE;
        return;
    }

    for (uint32_t i = 0u; i < 2u; ++i) {
        if (ids[i] == 0u)
            continue;
        AstraVfsOpenFile *file = find_file(service, slot->id, ids[i], &status);
        if (file == NULL)
            break;
        if (file->kind != ASTRA_VFS_KIND_DIRECTORY) {
            status = ASTRA_VFS_ERR_NOT_DIR;
            break;
        }
        identities[i] = file->io;
    }
    /* Reverse rename/link requests must acquire their shared owners identically. */
    if (identities[0] != NULL && identities[1] != NULL &&
        (uintptr_t)identities[1] < (uintptr_t)identities[0]) first = 1u;
    for (uint32_t pass = 0u; pass < 2u && status == ASTRA_VFS_OK; ++pass) {
        uint32_t i = first ^ pass, other = i ^ 1u;
        if (ids[i] == 0u) continue;
        /* The previous acquisition may have waited while this handle closed. */
        AstraVfsOpenFile *file = find_file(service, slot->id, ids[i], &status);
        if (file == NULL) break;
        if (bases[other] != NULL && file->io == bases[other]->io) {
            nodes[i] = nodes[other];
            bases[i] = bases[other];
            continue;
        }
        if (!file_acquire(service, file)) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        bases[i] = file;
        nodes[i] = file->node;
    }
    if (status == ASTRA_VFS_OK) {
        backend = bases[0] != NULL ? bases[0]->backend : slot->backend;
        if (operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK) {
            const AstraVfsBackend other = bases[1] != NULL ? bases[1]->backend : slot->backend;
            if (backend.ops != other.ops || backend.context != other.context)
                status = ASTRA_VFS_ERR_CROSS_DEVICE;
        }
    }
    if (status == ASTRA_VFS_OK)
        status = validate_path_operation(operation, request, &paths, nodes[0], nodes[1]);
    if (status == ASTRA_VFS_OK &&
        (operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK ||
         operation == ASTRA_VFS_OP_UNLINK || operation == ASTRA_VFS_OP_UNLINK_AT ||
         operation == ASTRA_VFS_OP_CHMOD || operation == ASTRA_VFS_OP_CHMOD_AT)) {
        uint32_t flags = operation == ASTRA_VFS_OP_CHMOD ||
            (operation == ASTRA_VFS_OP_CHMOD_AT &&
                (request->flags & ASTRA_VFS_AT_SYMLINK_NOFOLLOW) == 0u) ? ASTRA_VFS_STAT_FOLLOW : 0u;
        status = metadata_acquire(service, &backend, nodes, &paths,
            operation == ASTRA_VFS_OP_RENAME ? 2u : 1u, flags, affected, &affected_count);
    }
    if (status == ASTRA_VFS_OK) {
        state_release(service);
        status = astra_vfs_backend_path_operation(&backend, operation,
            request, transfer, nodes[0], nodes[1], reply);
        (void)state_acquire(service);
        if (status == ASTRA_VFS_OK && operation == ASTRA_VFS_OP_LOOKUP) {
            AstraVfsNodeInfo info = {.size = reply->node_size, .mtime = reply->mtime,
                .uid = reply->uid, .gid = reply->gid, .kind = reply->kind,
                .mode = reply->mode, .nlink = reply->nlink, .meta = reply->meta};
            status = cache_node_info(service, &backend, NULL, &info);
            if (status == ASTRA_VFS_OK) astra_vfs_node_info_publish(reply, &info);
        }
    }
    metadata_release(service, affected, affected_count);
    for (uint32_t i = 0u; i < 2u; ++i)
        if (bases[i] != NULL && (i == 0u || bases[1] != bases[0]))
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
    reply->status = write_file(service, file, request->offset,
                               ASTRA_VFS_FILE_OPEN, ASTRA_VFS_FILE_INVALID,
                               request->flags, request->write_limit,
                               request->body.payload, request->length,
                               &moved, &position);
    reply->count = moved;
    reply->position = position;
}

static void handle_readdir(const AstraVfsBackend *backend,
    const AstraVfsRequest *request, const char *path,
    const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t length = 0u;
    node_info_clear(&info);
    name[0] = '\0';
    reply->status = backend->ops->readdir(backend->context,
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
    int namespace_held = 0;
    AstraVfsOpenFile namespace_owner;

    if (service == NULL || reply == NULL) {
        return;
    }
    astra_vfs_reply_init(reply, ASTRA_VFS_VERSION, 0u);
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

    /* Keep path resolution and mutation atomic against new native descriptions.
     * Existing descriptions still serialize through their regular node owners. */
    if (service->cache_ops != NULL) {
        switch (operation) {
        case ASTRA_VFS_OP_OPEN:
        case ASTRA_VFS_OP_OPEN_AT:
        case ASTRA_VFS_OP_MKDIR:
        case ASTRA_VFS_OP_UNLINK:
        case ASTRA_VFS_OP_UNLINK_AT:
        case ASTRA_VFS_OP_RENAME:
        case ASTRA_VFS_OP_LINK:
        case ASTRA_VFS_OP_SYMLINK:
        case ASTRA_VFS_OP_CHMOD:
        case ASTRA_VFS_OP_CHMOD_AT:
            namespace_owner = (AstraVfsOpenFile){.state = ASTRA_VFS_FILE_OPEN,
                .io = &service->namespace_io};
            namespace_held = file_acquire(service, &namespace_owner);
            if (!namespace_held) { reply->status = ASTRA_VFS_ERR_BUSY; goto done; }
            break;
        default:
            break;
        }
    }
    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        slot->closing = 1u;
        reply->session = ASTRA_VFS_SESSION_INVALID;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_OPEN:
        handle_open(service, slot->id, request, paths.first, NULL, reply);
        break;
    case ASTRA_VFS_OP_SHARE:
        handle_share(service, slot, request, reply);
        break;
    case ASTRA_VFS_OP_ADOPT:
        handle_adopt(service, slot, request, reply);
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
    case ASTRA_VFS_OP_SEEK:
    case ASTRA_VFS_OP_FILE_FLAGS:
    case ASTRA_VFS_OP_POSITION_ADVANCE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL)
            reply->status = status;
        else if (!file_acquire(service, file))
            reply->status = ASTRA_VFS_ERR_IO;
        else {
            state_release(service);
            reply->status = astra_vfs_backend_description_operation(
                &file->backend, file->node, operation, request, reply,
                file->io->cache != NULL ? &file->io->cache->size : NULL);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_SYNC:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
        } else {
            reply->status = sync_file(service, file, ASTRA_VFS_FILE_OPEN,
                                      ASTRA_VFS_FILE_INVALID, NULL);
        }
        break;
    case ASTRA_VFS_OP_TRY_LOCK:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
        } else if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u) {
            reply->status = ASTRA_VFS_ERR_ACCESS;
        } else if (file->backend.ops->try_lock == NULL) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        } else if (!file_acquire(service, file)) {
            reply->status = ASTRA_VFS_ERR_IO;
        } else {
            state_release(service);
            reply->status = file->backend.ops->try_lock(
                file->backend.context, file->node);
            (void)state_acquire(service);
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_TRUNCATE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
        } else {
            reply->status = truncate_file(service, file, ASTRA_VFS_FILE_OPEN,
                                           request->offset, NULL);
        }
        break;
    case ASTRA_VFS_OP_LOOKUP:
        handle_path_operation(service, slot, operation, request, transfer, reply);
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info;

        node_info_clear(&info);
        state_release(service);
        reply->status = slot->backend.ops->stat(
            slot->backend.context, paths.first, &info);
        (void)state_acquire(service);
        if (reply->status == ASTRA_VFS_OK) {
            reply->status = cache_node_info(service, &slot->backend, NULL, &info);
        }
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
            reply->status = file->backend.ops->stat_at(
                file->backend.context, file->node,
                paths.first, request->flags, &info);
            (void)state_acquire(service);
            if (reply->status == ASTRA_VFS_OK)
                reply->status = cache_node_info(service, &file->backend, NULL, &info);
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
            if (!stat_owned_file(service, file, &info, &reply->status)) return;
            file_release(service, file);
            if (reply->status == ASTRA_VFS_OK)
                astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        state_release(service);
        handle_readdir(&slot->backend, request, paths.first, transfer, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
    case ASTRA_VFS_OP_READDIR_AREA:
        state_release(service);
        handle_readdir_batch(service, &slot->backend, request, paths.first, transfer, reply);
        (void)state_acquire(service);
        break;
    case ASTRA_VFS_OP_MKDIR:
    case ASTRA_VFS_OP_RENAME:
    case ASTRA_VFS_OP_LINK:
    case ASTRA_VFS_OP_SYMLINK:
    case ASTRA_VFS_OP_READLINK:
    case ASTRA_VFS_OP_DIRECTORY_PATH:
    case ASTRA_VFS_OP_UNLINK:
    case ASTRA_VFS_OP_UNLINK_AT:
    case ASTRA_VFS_OP_CHMOD:
    case ASTRA_VFS_OP_CHMOD_AT:
        handle_path_operation(service, slot, operation, request, transfer, reply);
        break;
    case ASTRA_VFS_OP_SET_FILE_TIMES: {
        AstraVfsNodeInfo times = {0}, after = {0};
        const uint8_t *wire = request->body.payload;
        times.meta.atime = (int64_t)astra_load_be64(wire);
        times.meta.atime_nsec = astra_load_be32(wire + 8u);
        times.mtime = (int64_t)astra_load_be64(wire + 12u);
        times.meta.mtime_nsec = astra_load_be32(wire + 20u);
        if (request->length != ASTRA_VFS_FILE_TIMES_SIZE || request->offset != 0u ||
            request->write_limit != 0u || !astra_vfs_file_times_valid(request->flags,
                times.meta.atime_nsec, times.meta.mtime_nsec)) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) { reply->status = status; break; }
        if (!(file->flags & ASTRA_VFS_OPEN_WRITE)) {
            reply->status = ASTRA_VFS_ERR_ACCESS; break;
        }
        if (file->backend.ops->set_file_times == NULL) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED; break;
        }
        if (!file_acquire(service, file)) { reply->status = ASTRA_VFS_ERR_IO; break; }
        reply->status = flush_cache(service, file, NULL);
        if (reply->status == ASTRA_VFS_OK) {
            state_release(service);
            reply->status = file->backend.ops->set_file_times(file->backend.context,
                file->node, &times, request->flags);
            if (reply->status == ASTRA_VFS_OK)
                reply->status = file->backend.ops->stat_node(file->backend.context, file->node, &after);
            if (reply->status == ASTRA_VFS_OK &&
                astra_vfs_node_identity_equal(&file->io->identity, &file->io->identity) &&
                !astra_vfs_node_identity_equal(&file->io->identity, &after.meta))
                reply->status = ASTRA_VFS_ERR_LOST;
            (void)state_acquire(service);
            if (file->io->cache != NULL) {
                if (reply->status == ASTRA_VFS_OK) {
                    file->io->cache->backing = after;
                    file->io->cache->written = 0u;
                }
                astra_vfs_file_cache_record_status(file->io->cache, reply->status);
            }
        }
        file_release(service, file);
        break;
    }
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
            reply->status = flush_cache(service, file, NULL);
            if (reply->status == ASTRA_VFS_OK) {
                state_release(service);
                reply->status = file->backend.ops->chmod_node(
                    file->backend.context, file->node,
                    (uint16_t)request->offset);
                (void)state_acquire(service);
            }
            file_release(service, file);
        }
        break;
    case ASTRA_VFS_OP_FILESYSTEM_INFO:
        if (request->file == ASTRA_VFS_FILE_INVALID) {
            state_release(service);
            reply->status = astra_vfs_backend_filesystem_info_into(
                &slot->backend, 0u, paths.first,
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
                &file->backend, file->node, NULL, reply->payload,
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
    if (reserved && reply->status == ASTRA_VFS_OK) {
        switch (operation) {
        case ASTRA_VFS_OP_OPEN:
        case ASTRA_VFS_OP_OPEN_AT:
            /* shortcut: CREATE may open an existing file; invalidate its
             * directory until backends report whether creation occurred. */
            if ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u)
                ++service->changes.entries;
            break;
        case ASTRA_VFS_OP_MKDIR:
        case ASTRA_VFS_OP_UNLINK:
        case ASTRA_VFS_OP_UNLINK_AT:
        case ASTRA_VFS_OP_RENAME:
        case ASTRA_VFS_OP_LINK:
        case ASTRA_VFS_OP_SYMLINK:
            ++service->changes.entries;
            break;
        case ASTRA_VFS_OP_CHMOD:
        case ASTRA_VFS_OP_SET_FILE_TIMES:
        case ASTRA_VFS_OP_CHMOD_FILE:
        case ASTRA_VFS_OP_CHMOD_AT:
            ++service->changes.nodes;
            break;
        default:
            break;
        }
    }
    if (namespace_held) file_release(service, &namespace_owner);
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
        astra_vfs_reply_init(reply, ASTRA_VFS_VERSION, 0u);
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
            bound = !service->sessions[index].closing &&
                    service->sessions[index].owner == owner &&
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
    full = service->next_session == 0u ||
        service->open_sessions == service->session_capacity;
    state_release(mutable);
    return full;
}

const AstraVfsServiceStats *
astra_vfs_service_stats(const AstraVfsService *service)
{
    return service != NULL ? &service->stats : NULL;
}
