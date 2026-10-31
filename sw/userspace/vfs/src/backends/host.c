#include <astra/vfs_host_backend.h>
#include <astra/volume_port.h>
#include <astra/endian.h>
#include <astra/vfs_host_transport.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <string.h>

_Static_assert(ASTRA_VFS_PATH_MAX <= ASTRA_HOST_FS_PATH_MAX,
               "a storage path must fit a host path operand");

static uint32_t path_operand(AstraVfsHostRequest *request, int second,
                             const char *path)
{
    size_t length;

    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    length = strlen(path);
    if (length >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (second) {
        request->path2 = path;
        request->path2_length = (uint32_t)length;
    } else {
        request->path = path;
        request->path_length = (uint32_t)length;
    }
    return ASTRA_VFS_OK;
}

static size_t operand_bytes(const char *path)
{
    return path != NULL ? strlen(path) : 0u;
}

/* Begins a command in the backend's session. The lane holds the operands
 * ahead of the data, so it is asked for both. */
static uint32_t command_begin(AstraVfsHostBackend *backend,
                              AstraVfsHostRequest *request,
                              uint16_t operation, uint32_t data_capacity,
                              const char *path, const char *path2)
{
    AstraHostCommand *command;
    size_t operands = operand_bytes(path) + operand_bytes(path2);

    if ((path != NULL || path2 != NULL) && data_capacity < ASTRA_VFS_IO_MAX)
        data_capacity = ASTRA_VFS_IO_MAX;
    uint32_t status;

    if (operands > (ASTRA_HOST_FS_PATH_MAX - 1u) * 2u ||
        data_capacity > UINT32_MAX - (uint32_t)operands)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_host_transport_begin(
        backend->transport, backend->scope,
        data_capacity + (uint32_t)operands, request);
    if (status == ASTRA_VFS_ERR_PEER) {
        __atomic_store_n(&backend->lost, 1u, __ATOMIC_RELEASE);
        status = ASTRA_VFS_ERR_LOST;
    }
    if (status != ASTRA_VFS_OK)
        return status;
    command = request->command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command);
    command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command->operation = operation;
    command->generation = backend->generation;
    command->session = backend->session;
    if ((path != NULL && path_operand(request, 0, path) != ASTRA_VFS_OK) ||
        (path2 != NULL && path_operand(request, 1, path2) != ASTRA_VFS_OK))
        return ASTRA_VFS_ERR_INVALID;
    return ASTRA_VFS_OK;
}

static void publish_info(const AstraHostCommand *command,
                         AstraVfsNodeInfo *info)
{
    const AstraHostNode *node = &command->node;
    AstraVfsNodeMeta *meta;
    uint32_t valid = node->valid;

    memset(info, 0, sizeof(*info));
    info->size = ((uint64_t)node->size_hi << 32) | node->size_lo;
    info->mtime = (int64_t)(((uint64_t)(uint32_t)node->mtime.seconds_hi << 32) |
                            node->mtime.seconds_lo);
    info->uid = node->uid;
    info->gid = node->gid;
    info->kind = node->kind;
    info->mode = node->mode;
    info->nlink = node->nlink > UINT16_MAX ? UINT16_MAX : (uint16_t)node->nlink;
    meta = &info->meta;
    if (valid & ASTRA_HOST_NODE_DEVICE) {
        meta->volume = ((uint64_t)node->device_hi << 32) | node->device_lo;
        if (meta->volume != 0u)
            meta->valid |= ASTRA_VFS_META_VOLUME;
    }
    if (valid & ASTRA_HOST_NODE_INODE) {
        meta->inode = ((uint64_t)node->inode_hi << 32) | node->inode_lo;
        meta->valid |= ASTRA_VFS_META_INODE;
    }
    if ((valid & ASTRA_HOST_NODE_BTIME) && node->btime.nanoseconds < 1000000000u) {
        uint64_t seconds = ((uint64_t)(uint32_t)node->btime.seconds_hi << 32) |
                           node->btime.seconds_lo;

        if (seconds < UINT64_MAX / UINT64_C(1000000000) ||
            (seconds == UINT64_MAX / UINT64_C(1000000000) &&
             node->btime.nanoseconds <= UINT64_MAX % UINT64_C(1000000000))) {
            meta->generation = seconds * UINT64_C(1000000000) + node->btime.nanoseconds;
            meta->valid |= ASTRA_VFS_META_GENERATION;
        }
    }
    if (valid & ASTRA_HOST_NODE_BLOCKS) {
        meta->blocks = ((uint64_t)node->blocks_hi << 32) | node->blocks_lo;
        meta->block_size = node->block_size;
        meta->valid |= ASTRA_VFS_META_BLOCKS;
    }
    if ((valid & ASTRA_HOST_NODE_ATIME) && node->atime.nanoseconds < 1000000000u) {
        meta->atime = (int64_t)(((uint64_t)(uint32_t)node->atime.seconds_hi << 32) |
                                node->atime.seconds_lo);
        meta->atime_nsec = node->atime.nanoseconds;
        meta->valid |= ASTRA_VFS_META_ATIME;
    }
    if ((valid & ASTRA_HOST_NODE_MTIME) && node->mtime.nanoseconds < 1000000000u) {
        meta->mtime_nsec = node->mtime.nanoseconds;
        meta->valid |= ASTRA_VFS_META_MTIME;
    }
    if ((valid & ASTRA_HOST_NODE_CTIME) && node->ctime.nanoseconds < 1000000000u) {
        meta->ctime = (int64_t)(((uint64_t)(uint32_t)node->ctime.seconds_hi << 32) |
                                node->ctime.seconds_lo);
        meta->ctime_nsec = node->ctime.nanoseconds;
        meta->valid |= ASTRA_VFS_META_CTIME;
    }
    if (valid & ASTRA_HOST_NODE_MODE)
        meta->valid |= ASTRA_VFS_META_MODE;
    if (valid & ASTRA_HOST_NODE_NLINK) {
        meta->nlink = node->nlink;
        meta->valid |= ASTRA_VFS_META_NLINK;
    }
    if ((valid & (ASTRA_HOST_NODE_UID | ASTRA_HOST_NODE_GID)) ==
        (ASTRA_HOST_NODE_UID | ASTRA_HOST_NODE_GID))
        meta->valid |= ASTRA_VFS_META_OWNER;
}

static uint32_t submit(AstraVfsHostBackend *backend,
                       AstraVfsHostRequest *request, const void *input,
                       uint32_t input_size, void *output,
                       uint32_t output_capacity)
{
    uint8_t scratch[ASTRA_VFS_IO_MAX];
    int buffered = (request->path != NULL || request->path2 != NULL) &&
                   output_capacity < sizeof(scratch);
    uint32_t status = astra_vfs_storage_status(astra_vfs_host_transport_submit(
        backend->transport, request, input, input_size,
        buffered ? scratch : output,
        buffered ? (uint32_t)sizeof(scratch) : output_capacity));

    if (status == ASTRA_VFS_ERR_LOST &&
        (request->command->handle == 0u || request->command->result_value != 0u))
        __atomic_store_n(&backend->lost, 1u, __ATOMIC_RELEASE);

    if (buffered && status == ASTRA_VFS_OK) {
        if (request->command->result_length > output_capacity)
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        if (request->command->result_length != 0u)
            memcpy(output, scratch, request->command->result_length);
    }
    return status;
}

/* OPEN, absolute beneath the session root (directory 0) or relative
 * beneath an open directory. */
static uint32_t host_open_common(void *context, uintptr_t directory,
                                 const char *path, uint32_t flags,
                                 uint16_t create_mode, uintptr_t *node,
                                 AstraVfsNodeInfo *info)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    status = command_begin(backend, &request, ASTRA_HOST_FS_OPEN, 0u, path,
                           NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->flags = (uint16_t)flags;
    command->handle = (uint32_t)directory;
    command->value_lo = create_mode == ASTRA_VFS_MODE_DEFAULT ? 0666u :
                        create_mode;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->handle == 0u || command->handle == (uint32_t)directory)
        return ASTRA_VFS_ERR_PROTOCOL;
    *node = command->handle;
    publish_info(command, info);
    return ASTRA_VFS_OK;
}

static uint32_t host_open(void *context, const char *path, uint32_t flags,
                          uint16_t create_mode, uintptr_t *node,
                          AstraVfsNodeInfo *info)
{
    return host_open_common(context, 0u, path, flags, create_mode, node,
                            info);
}

static uint32_t host_open_at(void *context, uintptr_t directory,
                             const char *path, uint32_t flags,
                             uint16_t create_mode, uintptr_t *node,
                             AstraVfsNodeInfo *info)
{
    if (directory == 0u || directory > UINT32_MAX)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return host_open_common(context, directory, path, flags, create_mode,
                            node, info);
}

static uint32_t host_close(void *context, uintptr_t node)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_CLOSE,
                                    0u, NULL, NULL);

    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_read_common(void *context, uintptr_t node, uint64_t offset,
                                 uint32_t flags, void *buffer, uint32_t length,
                                 uint32_t *moved, uint64_t *position)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *moved = 0u;
    *position = offset;
    uint32_t limit = astra_vfs_host_transport_payload_limit(backend->transport);
    if (limit == 0u)
        return ASTRA_VFS_ERR_LIMIT;
    if (length > limit)
        length = limit;
    status = command_begin(backend, &request, ASTRA_HOST_FS_READ, length,
                           NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)node;
    command->flags = flags;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    status = submit(backend, &request, NULL, 0u, buffer, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *moved = command->result_length;
    *position = ((uint64_t)command->value_hi << 32) | command->value_lo;
    return ASTRA_VFS_OK;
}

static uint32_t host_read(void *context, uintptr_t node, uint64_t offset,
                          void *buffer, uint32_t length, uint32_t *moved)
{
    uint64_t position;

    return host_read_common(context, node, offset, 0u, buffer, length, moved,
                             &position);
}

static uint32_t host_read_sequential(void *context, uintptr_t node,
                                     void *buffer, uint32_t length,
                                     uint32_t *moved, uint64_t *position)
{
    return host_read_common(context, node, 0u, ASTRA_HOST_FS_IO_SEQUENTIAL,
                             buffer, length, moved, position);
}

static uint32_t host_write(void *context, uintptr_t node, uint64_t offset,
                           uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
                           uint32_t *moved, uint64_t *position)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *moved = 0u;
    *position = offset;
    uint32_t limit = astra_vfs_host_transport_payload_limit(backend->transport);
    if (limit == 0u)
        return ASTRA_VFS_ERR_LIMIT;
    if (length > limit)
        length = limit;
    status = command_begin(backend, &request, ASTRA_HOST_FS_WRITE, length,
                           NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)node;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    command->flags =
        (flags & ASTRA_VFS_WRITE_APPEND ? ASTRA_HOST_FS_WRITE_APPEND : 0u) |
        (flags & ASTRA_VFS_IO_SEQUENTIAL ? ASTRA_HOST_FS_IO_SEQUENTIAL : 0u);
    command->value_hi = (uint32_t)(max_end >> 32);
    command->value_lo = (uint32_t)max_end;
    status = submit(backend, &request, buffer, length, NULL, 0u);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *moved = command->result_length;
    *position = ((uint64_t)command->value_hi << 32) | command->value_lo;
    return ASTRA_VFS_OK;
}

static uint32_t host_handle_u64(void *context, uintptr_t node, uint16_t op,
                                uint64_t value)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, op, 0u, NULL, NULL);

    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    request.command->value_hi = (uint32_t)(value >> 32);
    request.command->value_lo = (uint32_t)value;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_sync(void *context, uintptr_t node)
{
    return host_handle_u64(context, node, ASTRA_HOST_FS_SYNC, 0u);
}

static uint32_t host_truncate(void *context, uintptr_t node, uint64_t size)
{
    return host_handle_u64(context, node, ASTRA_HOST_FS_TRUNCATE, size);
}

/* STAT of a path beneath `directory` (0: the session root), or of the
 * open `directory` itself when `path` is NULL. */
static uint32_t host_stat_common(void *context, uintptr_t directory,
                                 const char *path, uint32_t flags, AstraVfsNodeInfo *info)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (directory > UINT32_MAX || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_STAT, 0u, path,
                           NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)directory;
    request.command->flags = flags;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK)
        publish_info(request.command, info);
    return status;
}

static uint32_t host_stat(void *context, const char *path,
                          AstraVfsNodeInfo *info)
{
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_stat_common(context, 0u, path, 0u, info);
}

static uint32_t host_stat_at(void *context, uintptr_t directory,
                             const char *path, uint32_t flags, AstraVfsNodeInfo *info)
{
    if (directory == 0u || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_stat_common(context, directory, path, flags, info);
}

static uint32_t host_stat_node(void *context, uintptr_t node,
                                AstraVfsNodeInfo *info)
{
    if (node == 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_stat_common(context, node, NULL, 0u, info);
}

/* One transaction owns the shared cursor for the complete returned batch. */
static uint32_t host_readdir_batch(void *context, uintptr_t directory,
                                   const char *path, uint64_t cookie,
                                   uint32_t entry_limit, uint8_t *buffer,
                                   uint32_t capacity, uint32_t *used,
                                   uint64_t *next)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (directory > UINT32_MAX || (directory == 0u && path == NULL) ||
        entry_limit == 0u || buffer == NULL || used == NULL || next == NULL ||
        capacity < ASTRA_VFS_DIRENT_HEADER + 1u)
        return ASTRA_VFS_ERR_INVALID;
    *used = 0u;
    *next = cookie;
    status = command_begin(backend, &request, ASTRA_HOST_FS_READDIR,
                           capacity, directory != 0u ? NULL : path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)directory;
    request.command->offset_hi = (uint32_t)(cookie >> 32);
    request.command->offset_lo = (uint32_t)cookie;
    request.command->value_lo = entry_limit;
    status = submit(backend, &request, NULL, 0u, buffer, capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    if (request.command->result_length == 0u ||
        request.command->result_length > capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    *used = request.command->result_length;
    *next = ((uint64_t)request.command->value_hi << 32) |
            request.command->value_lo;
    return ASTRA_VFS_OK;
}

/* The single-entry backend spelling adapts the same packed provider operation. */
static uint32_t host_readdir(void *context, uintptr_t directory,
                             const char *path, uint64_t cookie, char *name,
                             uint32_t name_capacity, AstraVfsNodeInfo *info,
                             uint64_t *next)
{
    uint8_t wire[ASTRA_VFS_DIRENT_HEADER + ASTRA_VFS_NAME_MAX - 1u];
    uint32_t used;
    uint32_t capacity;
    uint32_t length;
    uint32_t status;

    if (name == NULL || info == NULL || name_capacity < 2u)
        return ASTRA_VFS_ERR_INVALID;
    capacity = name_capacity > ASTRA_VFS_NAME_MAX ? sizeof(wire) :
        ASTRA_VFS_DIRENT_HEADER + name_capacity - 1u;
    status = host_readdir_batch(context, directory, path, cookie, 1u, wire,
                                 capacity, &used, next);
    if (status != ASTRA_VFS_OK)
        return status;
    if (used < ASTRA_VFS_DIRENT_HEADER + 1u)
        return ASTRA_VFS_ERR_PROTOCOL;
    length = wire[6];
    if (length == 0u || length >= name_capacity || wire[7] != 0u ||
        used != ASTRA_VFS_DIRENT_HEADER + length ||
        !astra_utf8_validate(wire + ASTRA_VFS_DIRENT_HEADER, length, 0u))
        return ASTRA_VFS_ERR_PROTOCOL;
    memcpy(name, wire + ASTRA_VFS_DIRENT_HEADER, length);
    name[length] = '\0';
    memset(info, 0, sizeof(*info));
    info->kind = astra_load_be16(wire);
    info->mode = astra_load_be16(wire + 2u);
    info->nlink = astra_load_be16(wire + 4u);
    info->uid = astra_load_be32(wire + 8u);
    info->gid = astra_load_be32(wire + 12u);
    info->size = astra_load_be64(wire + 16u);
    info->mtime = (int64_t)astra_load_be64(wire + 24u);
    return ASTRA_VFS_OK;
}

static uint32_t host_path_mode(void *context, uintptr_t directory,
                               const char *path, uint16_t op, uint16_t flags,
                               uint16_t mode)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (directory > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, op, 0u, path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)directory;
    request.command->flags = flags;
    request.command->value_lo = mode;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_mkdir_at(void *context, uintptr_t directory,
                              const char *path, uint16_t create_mode)
{
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, directory, path, ASTRA_HOST_FS_MKDIR, 0u,
                          create_mode == ASTRA_VFS_MODE_DEFAULT ? 0777u :
                          create_mode);
}

static uint32_t host_mkdir(void *context, const char *path, uint16_t mode)
{
    return host_mkdir_at(context, 0u, path, mode);
}

static uint32_t host_unlink(void *context, const char *path, uint32_t flags)
{
    if (path == NULL || (flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, 0u, path, ASTRA_HOST_FS_UNLINK,
        flags & ASTRA_VFS_UNLINK_DIRECTORY ? ASTRA_HOST_FS_UNLINK_DIRECTORY :
                                             0u, 0u);
}

static uint32_t host_chmod(void *context, const char *path, uint16_t mode)
{
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, 0u, path, ASTRA_HOST_FS_CHMOD, 0u, mode);
}

static uint32_t host_unlink_at(void *context, uintptr_t directory,
                               const char *path, uint32_t flags)
{
    if (directory == 0u || path == NULL ||
        (flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(
        context, directory, path, ASTRA_HOST_FS_UNLINK,
        (flags & ASTRA_VFS_UNLINK_DIRECTORY) != 0u ?
            ASTRA_HOST_FS_UNLINK_DIRECTORY : 0u,
        0u);
}

static uint32_t host_chmod_node(void *context, uintptr_t node, uint16_t mode)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (node == 0u || node > UINT32_MAX ||
        (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_CHMOD, 0u, NULL,
                           NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    request.command->value_lo = mode;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

/* The provider applies the same final-link policy as the native adapter. */
static uint32_t host_chmod_at(void *context, uintptr_t directory,
                              const char *path, uint16_t mode,
                              uint32_t flags)
{
    if (directory == 0u || path == NULL ||
        (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
        (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, directory, path, ASTRA_HOST_FS_CHMOD,
                          flags, mode);
}

static uint64_t host_info_u64(uint32_t high, uint32_t low)
{
    return ((uint64_t)high << 32) | low;
}

static uint32_t host_filesystem_info(void *context, uintptr_t node,
                                     const char *path,
                                     AstraVfsFilesystemInfo *info)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostFilesystemInfo host_info;
    uint32_t status;

    if (info == NULL || node > UINT32_MAX ||
        ((node == 0u) == (path == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request,
                           ASTRA_HOST_FS_FILESYSTEM_INFO,
                           sizeof(host_info), path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    status = submit(backend, &request, NULL, 0u, &host_info,
                    sizeof(host_info));
    if (status != ASTRA_VFS_OK)
        return status;
    if (request.command->result_length != sizeof(host_info) ||
        host_info.size != sizeof(host_info) || host_info.reserved != 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->flags = host_info.flags;
    info->block_size = host_info.block_size;
    info->fragment_size = host_info.fragment_size;
    info->blocks = host_info_u64(host_info.blocks_hi, host_info.blocks_lo);
    info->blocks_free = host_info_u64(host_info.blocks_free_hi,
                                      host_info.blocks_free_lo);
    info->blocks_available = host_info_u64(host_info.blocks_available_hi,
                                           host_info.blocks_available_lo);
    info->files = host_info_u64(host_info.files_hi, host_info.files_lo);
    info->files_free = host_info_u64(host_info.files_free_hi,
                                     host_info.files_free_lo);
    info->name_max = host_info.name_max;
    return ASTRA_VFS_OK;
}

/* Both operands absolute beneath the session root: handle 0, value_lo 0. */
static uint32_t host_two_paths(void *context, uintptr_t left_base,
                               const char *left, uintptr_t right_base,
                               const char *right, uint16_t operation,
                               uint32_t flags)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (left == NULL || right == NULL ||
        left_base > UINT32_MAX || right_base > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, operation, 0u, left, right);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)left_base;
    request.command->value_lo = (uint32_t)right_base;
    request.command->flags = flags;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_rename(void *context, const char *from, const char *to)
{
    return host_two_paths(context, 0u, from, 0u, to, ASTRA_HOST_FS_RENAME, 0u);
}

static uint32_t host_rename_noreplace(void *context, const char *from,
    const char *to)
{
    return host_two_paths(context, 0u, from, 0u, to,
        ASTRA_HOST_FS_RENAME, ASTRA_HOST_FS_RENAME_NOREPLACE);
}

static uint32_t host_text(void *context, uintptr_t directory, const char *path,
                          uint16_t operation, void *buffer,
                          uint32_t capacity, uint32_t *length)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *length = 0u;
    if (directory > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, operation,
                           capacity, path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)directory;
    status = submit(backend, &request, NULL, 0u, buffer, capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length > capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    *length = command->result_length;
    return ASTRA_VFS_OK;
}

static uint32_t host_readlink_at(void *context, uintptr_t directory,
                                const char *path, void *buffer,
                                uint32_t capacity, uint32_t *length)
{
    return host_text(context, directory, path, ASTRA_HOST_FS_READLINK,
                     buffer, capacity, length);
}

static uint32_t host_readlink(void *context, const char *path, void *buffer,
                              uint32_t capacity, uint32_t *length)
{
    return host_readlink_at(context, 0u, path, buffer, capacity, length);
}

static uint32_t host_directory_path(void *context, uintptr_t directory,
                                     void *buffer, uint32_t capacity,
                                     uint32_t *length)
{
    return host_text(context, directory, NULL, ASTRA_HOST_FS_PATH,
                     buffer, capacity, length);
}

static uint32_t host_rename_at(void *context, uintptr_t from_base,
                               const char *from, uintptr_t to_base,
                               const char *to)
{
    return host_two_paths(context, from_base, from, to_base, to,
        ASTRA_HOST_FS_RENAME, 0u);
}

static uint32_t host_rename_noreplace_at(void *context, uintptr_t from_base,
    const char *from, uintptr_t to_base, const char *to)
{
    return host_two_paths(context, from_base, from, to_base, to,
        ASTRA_HOST_FS_RENAME, ASTRA_HOST_FS_RENAME_NOREPLACE);
}

static uint32_t host_link_at(void *context, uintptr_t from_base,
                             const char *from, uintptr_t to_base,
                             const char *to)
{
    return host_two_paths(context, from_base, from, to_base, to,
        ASTRA_HOST_FS_LINK, 0u);
}

static uint32_t host_symlink_at(void *context, const char *target,
                                 uintptr_t directory, const char *path)
{
    return host_two_paths(context, directory, target, 0u, path,
        ASTRA_HOST_FS_SYMLINK, 0u);
}

static uint32_t host_symlink(void *context, const char *target,
                             const char *path)
{
    return host_two_paths(context, 0u, target, 0u, path,
        ASTRA_HOST_FS_SYMLINK, 0u);
}

static uint32_t host_link(void *context, const char *from, const char *to)
{
    return host_two_paths(context, 0u, from, 0u, to, ASTRA_HOST_FS_LINK, 0u);
}

static uint32_t host_lookup(void *context, uintptr_t directory, const char *path, uint32_t flags,
                             uint32_t links_left, AstraVfsNodeInfo *info,
                             AstraVfsEscape *escape)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (escape == NULL || (escape->capacity != 0u && escape->path == NULL))
        return ASTRA_VFS_ERR_INVALID;
    uint8_t *continuation = escape->path;
    uint32_t continuation_capacity = escape->capacity;
    *escape = (AstraVfsEscape){.path = continuation, .capacity = continuation_capacity};
    if ((flags & ~(ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_MISSING |
                   ASTRA_VFS_LOOKUP_ENTRY)) != 0u ||
        (flags & (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY)) ==
            (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY) ||
        links_left > ASTRA_VFS_LINK_MAX || directory > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_STAT,
                            continuation_capacity, path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->flags = ASTRA_HOST_FS_STAT_BUDGET |
        ((flags & ASTRA_VFS_LOOKUP_FOLLOW) ? ASTRA_HOST_FS_STAT_FOLLOW : 0u) |
        ((flags & ASTRA_VFS_LOOKUP_MISSING) ? ASTRA_HOST_FS_STAT_MISSING : 0u) |
        ((flags & ASTRA_VFS_LOOKUP_ENTRY) ? ASTRA_HOST_FS_STAT_ENTRY : 0u);
    request.command->offset_lo = links_left;
    request.command->handle = (uint32_t)directory;
    status = submit(backend, &request, NULL, 0u, continuation,
                     continuation_capacity);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_ESCAPED)
        return status;
    escape->links = request.command->value_hi;
    if (escape->links > links_left)
        return ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK) {
        publish_info(request.command, info);
        return status;
    }
    if (request.command->result_length > escape->capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    if ((request.command->value_lo != ASTRA_VFS_ESCAPE_PARENT &&
         request.command->value_lo != ASTRA_VFS_ESCAPE_SYMLINK) ||
        request.command->result_value == 0u ||
        request.command->result_value > strlen(path) ||
        request.command->result_length == 0u || continuation[0] != '/' ||
        !astra_utf8_validate(continuation, request.command->result_length, 0u))
        return ASTRA_VFS_ERR_PROTOCOL;
    escape->kind = (uint16_t)request.command->value_lo;
    escape->consumed = request.command->result_value;
    escape->length = (uint16_t)request.command->result_length;
    return status;
}

static uint32_t host_seek(void *context, uintptr_t node, int64_t delta,
                           uint32_t origin, uint64_t *position)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (node == 0u || node > UINT32_MAX || origin > 2u || position == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_SEEK, 0u, NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    request.command->flags = origin;
    request.command->offset_hi = (uint32_t)((uint64_t)delta >> 32);
    request.command->offset_lo = (uint32_t)delta;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK)
        *position = ((uint64_t)request.command->value_hi << 32) |
                     request.command->value_lo;
    return status;
}

static uint32_t host_share(void *context, uintptr_t node, uint64_t *token)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (node == 0u || node > UINT32_MAX || token == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *token = 0u;
    status = command_begin(backend, &request, ASTRA_HOST_FS_SHARE, 0u, NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK) {
        *token = ((uint64_t)request.command->value_hi << 32) |
                  request.command->value_lo;
        if (*token == 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
    }
    return status;
}

static uint32_t host_adopt(void *context, uint64_t token, uint32_t flags,
                            uintptr_t *node)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (token == 0u || flags & ~ASTRA_VFS_ADOPT_DISCARD ||
        (flags == 0u && node == NULL))
        return ASTRA_VFS_ERR_INVALID;
    if (node != NULL)
        *node = 0u;
    status = command_begin(backend, &request, ASTRA_HOST_FS_ADOPT, 0u, NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->flags = flags & ASTRA_VFS_ADOPT_DISCARD ?
                             ASTRA_HOST_FS_ADOPT_DISCARD : 0u;
    request.command->offset_hi = (uint32_t)(token >> 32);
    request.command->offset_lo = (uint32_t)token;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK && flags == 0u) {
        if (request.command->handle == 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
        *node = request.command->handle;
    }
    return status;
}

static uint32_t host_file_flags(void *context, uintptr_t node, uint32_t mask,
                                 uint32_t flags, uint32_t *result)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (node == 0u || node > UINT32_MAX || result == NULL ||
        mask & ~ASTRA_VFS_FILE_STATUS_MASK || flags & ~mask)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_FILE_FLAGS, 0u,
                            NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    request.command->handle = (uint32_t)node;
    request.command->flags = mask;
    request.command->value_lo = flags;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK)
        *result = request.command->value_lo;
    return status;
}

static const AstraVfsBackendOps host_ops = {
    .open = host_open, .close = host_close, .read = host_read,
    .write = host_write, .sync = host_sync, .truncate = host_truncate,
    .stat = host_stat, .readdir = host_readdir, .mkdir = host_mkdir,
    .unlink = host_unlink, .rename = host_rename,
    .rename_noreplace = host_rename_noreplace, .chmod = host_chmod,
    .readlink = host_readlink, .symlink = host_symlink, .link = host_link,
    .open_at = host_open_at, .unlink_at = host_unlink_at,
    .chmod_node = host_chmod_node, .chmod_at = host_chmod_at,
    .filesystem_info = host_filesystem_info, .stat_at = host_stat_at,
    .stat_node = host_stat_node, .lookup = host_lookup,
    .read_sequential = host_read_sequential, .seek = host_seek,
    .share = host_share, .adopt = host_adopt, .file_flags = host_file_flags,
    .readdir_batch = host_readdir_batch, .mkdir_at = host_mkdir_at,
    .rename_at = host_rename_at,
    .rename_noreplace_at = host_rename_noreplace_at,
    .link_at = host_link_at, .symlink_at = host_symlink_at,
    .readlink_at = host_readlink_at, .directory_path = host_directory_path,
};

int astra_vfs_host_init(AstraVfsHostBackend *backend,
                        AstraVfsHostTransport *transport,
                        uint32_t generation, uint32_t session)
{
    if (backend == NULL || transport == NULL || generation == 0u ||
        session == 0u)
        return 0;
    backend->transport = transport;
    backend->generation = generation;
    backend->session = session;
    backend->scope = 0u;
    backend->lost = 0u;
    return 1;
}

const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    return &host_ops;
}

/* One provider control command on the transport's unbound channel. */
static uint32_t control(AstraVfsHostTransport *transport,
                        uint32_t generation, uint16_t operation,
                        uint16_t flags, uint32_t session, const AstraVolumeReference *volume,
                        const char *path, uint32_t requester, uint32_t *result)
{
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    size_t length = path != NULL ? strlen(path) : 0u;
    uint32_t status;

    uint8_t target[sizeof(AstraHostSessionOpen)] = {0};
    uint32_t target_size = operation == ASTRA_HOST_FS_SESSION_OPEN ? sizeof(target) : 0u;
    if (length >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    if (volume != NULL) {
        if (volume->epoch == 0u || volume->id == 0u || volume->generation == 0u)
            return ASTRA_VFS_ERR_INVALID;
        astra_store_be64(target, volume->epoch);
        astra_store_be64(target + 8u, volume->id);
        astra_store_be64(target + 16u, volume->generation);
    }
    if (target_size != 0u) astra_store_be32(target + 24u, requester);
    status = astra_vfs_host_transport_begin(transport, 0u, (uint32_t)length + target_size,
                                            &request);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command);
    command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command->operation = operation;
    command->flags = flags;
    command->generation = generation;
    command->session = session;
    request.path = path;
    request.path_length = (uint32_t)length;
    status = astra_vfs_host_transport_submit(transport, &request, target, target_size,
                                             NULL, 0u);
    if (status == ASTRA_VFS_OK && result != NULL)
        *result = command->result_value;
    return status;
}

uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport,
                              uint32_t generation, int release,
                              uint32_t *epoch)
{
    return control(transport, generation, ASTRA_HOST_FS_CLAIM,
                   release ? ASTRA_HOST_FS_CLAIM_RELEASE : 0u, 0u, NULL,
                   NULL, 0u, epoch);
}

uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport,
                                     uint32_t generation, uint32_t parent,
                                     const AstraVolumeReference *volume, const char *root,
                                     uint16_t flags, uint32_t requester, uint32_t *session)
{
    uint32_t status;

    if (session == NULL || root == NULL || (parent == 0u ? volume == NULL : volume != NULL))
        return ASTRA_VFS_ERR_INVALID;
    *session = 0u;
    status = control(transport, generation, ASTRA_HOST_FS_SESSION_OPEN,
                     flags, parent, volume, root,
                     requester, session);
    return status == ASTRA_VFS_OK && *session == 0u ?
        ASTRA_VFS_ERR_PROTOCOL : status;
}

uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport,
                                      uint32_t generation, uint32_t session)
{
    return control(transport, generation, ASTRA_HOST_FS_SESSION_CLOSE, 0u,
                   session, NULL, NULL, 0u, NULL);
}

uint32_t astra_vfs_host_catalog(AstraVfsHostTransport *transport, uint32_t generation,
                                const AstraVolumeCatalogQuery *query,
                                AstraVolumeCatalogPage *page)
{
    uint8_t input[sizeof(AstraVolumeCatalogQuery)];
    AstraVfsHostRequest request;
    uint32_t status;
    if (transport == NULL || query == NULL || page == NULL ||
        !astra_volume_query_valid(query))
        return ASTRA_VFS_ERR_INVALID;
    astra_store_be32(input, query->version);
    astra_store_be32(input + 4u, query->size);
    astra_store_be64(input + 8u, query->epoch);
    astra_store_be64(input + 16u, query->sequence);
    astra_store_be64(input + 24u, query->after);
    memset(page, 0, sizeof(*page));
    status = astra_vfs_host_transport_begin(transport, 0u, sizeof(*page), &request);
    if (status != ASTRA_VFS_OK)
        return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command);
    command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES;
    command->operation = ASTRA_HOST_VOLUME_CATALOG;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, input, sizeof(input),
                                             page, sizeof(*page));
    if (status != ASTRA_VFS_OK)
        return status;
    uint8_t *wire = (uint8_t *)page;
    page->version = astra_load_be32(wire);
    page->size = astra_load_be32(wire + 4u);
    page->epoch = astra_load_be64(wire + 8u);
    page->sequence = astra_load_be64(wire + 16u);
    page->next = astra_load_be64(wire + 24u);
    page->count = astra_load_be32(wire + 32u);
    page->reserved = astra_load_be32(wire + 36u);
    if (command->result_length != sizeof(*page) || page->count > ASTRA_VOLUME_PAGE_RECORDS)
        return ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t at = 0u; at < page->count; ++at) {
        AstraVolumeRecord *row = &page->records[at];
        wire = (uint8_t *)row;
#define VOLUME64(field) row->field = astra_load_be64(wire + offsetof(AstraVolumeRecord, field))
#define VOLUME32(field) row->field = astra_load_be32(wire + offsetof(AstraVolumeRecord, field))
        VOLUME64(id); VOLUME64(generation); VOLUME64(parent); VOLUME64(parent_generation);
        VOLUME64(byte_limit); VOLUME64(inode_limit); VOLUME64(backing_object);
        VOLUME64(capacity_bytes); VOLUME64(offset_bytes);
        VOLUME32(kind); VOLUME32(class_id); VOLUME32(state); VOLUME32(format);
        VOLUME32(origin); VOLUME32(flags); VOLUME32(partition_number);
        VOLUME32(partition_map); VOLUME32(status); VOLUME32(reserved);
#undef VOLUME64
#undef VOLUME32
    }
    if (!astra_volume_page_valid(query, page))
        return ASTRA_VFS_ERR_PROTOCOL;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_volume_control(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumeControl *control,
    AstraVolumeReference *reference)
{
    uint8_t input[sizeof(AstraHostVolumeControl)] = {0};
    uint8_t output[sizeof(AstraVolumeReference)] = {0};
    AstraVfsHostRequest request;
    if (transport == NULL || requester == 0u || reference == NULL || !astra_volume_control_valid(control))
        return ASTRA_VFS_ERR_INVALID;
    memset(reference, 0, sizeof(*reference));
    astra_store_be64(input, control->reference.epoch);
    astra_store_be64(input + 8u, control->reference.id);
    astra_store_be64(input + 16u, control->reference.generation);
    astra_store_be32(input + 24u, control->operation);
    astra_store_be32(input + 28u, control->flags);
    astra_store_be64(input + 32u, control->byte_limit);
    astra_store_be64(input + 40u, control->inode_limit);
    astra_store_be64(input + 48u, control->image_share);
    astra_store_be32(input + 56u, requester);
    uint32_t status = astra_vfs_host_transport_begin(transport, 0u, sizeof(input), &request);
    if (status != ASTRA_VFS_OK) return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command); command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES; command->operation = ASTRA_HOST_VOLUME_CONTROL;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, input, sizeof(input), output, sizeof(output));
    if (status != ASTRA_VFS_OK) return status;
    if (command->result_length != sizeof(output)) return ASTRA_VFS_ERR_PROTOCOL;
    *reference = (AstraVolumeReference){astra_load_be64(output), astra_load_be64(output + 8u),
        astra_load_be64(output + 16u)};
    if (reference->epoch == 0u || reference->id == 0u || reference->generation == 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_volume_format(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumeFormatRequest *format,
    uint32_t *outcome)
{
    uint8_t input[sizeof(AstraHostVolumeFormat)] = {0};
    uint8_t output[sizeof(uint32_t)] = {0};
    AstraVfsHostRequest request;
    if (transport == NULL || requester == 0u || outcome == NULL ||
        !astra_volume_format_valid(format)) return ASTRA_VFS_ERR_INVALID;
    *outcome = 0u;
    astra_store_be64(input, format->source.epoch);
    astra_store_be64(input + 8u, format->source.id);
    astra_store_be64(input + 16u, format->source.generation);
    astra_store_be32(input + 24u, format->header.operation == ASTRA_VOLUME_FORMAT ?
        ASTRA_HOST_VOLUME_FORMAT_START : ASTRA_HOST_VOLUME_FORMAT_STATUS);
    astra_store_be32(input + 28u, format->format);
    memcpy(input + 32u, format->label, sizeof(format->label));
    astra_store_be32(input + 44u, requester);
    uint32_t status = astra_vfs_host_transport_begin(transport, 0u, sizeof(input), &request);
    if (status != ASTRA_VFS_OK) return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command); command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES; command->operation = ASTRA_HOST_VOLUME_FORMAT;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, input, sizeof(input),
        output, sizeof(output));
    if (status != ASTRA_VFS_OK) return status;
    if (command->result_length != sizeof(output)) return ASTRA_VFS_ERR_PROTOCOL;
    *outcome = astra_load_be32(output);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_volume_partition(AstraVfsHostTransport *transport,
    uint32_t generation, uint32_t requester, const AstraVolumePartitionRequest *partition,
    uint32_t *outcome)
{
    uint8_t input[sizeof(AstraHostVolumePartition)] = {0};
    uint8_t output[sizeof(uint32_t)] = {0};
    AstraVfsHostRequest request;
    if (transport == NULL || requester == 0u || outcome == NULL ||
        !astra_volume_partition_valid(partition)) return ASTRA_VFS_ERR_INVALID;
    *outcome = 0u;
    astra_store_be64(input, partition->device.epoch);
    astra_store_be64(input + 8u, partition->device.id);
    astra_store_be64(input + 16u, partition->device.generation);
    astra_store_be32(input + 24u, partition->header.operation == ASTRA_VOLUME_PARTITION ?
        ASTRA_HOST_VOLUME_PARTITION_START : ASTRA_HOST_VOLUME_PARTITION_STATUS);
    astra_store_be32(input + 28u, requester);
    astra_store_be32(input + 32u, partition->edit.operation);
    astra_store_be32(input + 36u, partition->edit.map);
    astra_store_be32(input + 40u, partition->edit.format);
    astra_store_be32(input + 44u, partition->edit.number);
    astra_store_be64(input + 48u, partition->edit.offset_bytes);
    astra_store_be64(input + 56u, partition->edit.length_bytes);
    uint32_t status = astra_vfs_host_transport_begin(transport, 0u, sizeof(input), &request);
    if (status != ASTRA_VFS_OK) return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command); command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES; command->operation = ASTRA_HOST_VOLUME_PARTITION;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, input, sizeof(input),
        output, sizeof(output));
    if (status != ASTRA_VFS_OK) return status;
    if (command->result_length != sizeof(output)) return ASTRA_VFS_ERR_PROTOCOL;
    *outcome = astra_load_be32(output);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_usage(AstraVfsHostTransport *transport, uint32_t generation,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page)
{
    uint8_t input[sizeof(AstraVolumeUsageQuery)] = {0};
    AstraVfsHostRequest request;
    if (transport == NULL || page == NULL || !astra_volume_usage_query_valid(query))
        return ASTRA_VFS_ERR_INVALID;
    memset(page, 0, sizeof(*page));
    astra_store_be32(input, query->version); astra_store_be32(input + 4u, query->size);
    astra_store_be64(input + 8u, query->volume.epoch); astra_store_be64(input + 16u, query->volume.id);
    astra_store_be64(input + 24u, query->volume.generation); astra_store_be64(input + 32u, query->sequence);
    astra_store_be64(input + 40u, query->catalog_sequence); astra_store_be64(input + 48u, query->after);
    astra_store_be32(input + 56u, query->after_kind);
    astra_store_be32(input + 60u, query->after_process);
    uint32_t status = astra_vfs_host_transport_begin(transport, 0u, sizeof(*page), &request);
    if (status != ASTRA_VFS_OK) return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command); command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES; command->operation = ASTRA_HOST_VOLUME_USAGE;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, input, sizeof(input), page, sizeof(*page));
    if (status != ASTRA_VFS_OK) return status;
    uint8_t *wire = (uint8_t *)page;
#define PAGE32(field) page->field = astra_load_be32(wire + offsetof(AstraVolumeUsagePage, field))
#define PAGE64(field) page->field = astra_load_be64(wire + offsetof(AstraVolumeUsagePage, field))
    PAGE32(version); PAGE32(size); PAGE64(volume.epoch); PAGE64(volume.id); PAGE64(volume.generation);
    PAGE64(sequence); PAGE64(catalog_sequence); PAGE64(next); PAGE32(next_kind); PAGE32(count); PAGE32(next_process); PAGE32(reserved);
#undef PAGE32
#undef PAGE64
    if (command->result_length != sizeof(*page) || page->count > ASTRA_VOLUME_USAGE_PAGE_RECORDS)
        return ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t at = 0u; at < page->count; ++at) {
        AstraVolumeUsageRecord *row = &page->records[at];
        wire = (uint8_t *)row;
#define ROW32(field) row->field = astra_load_be32(wire + offsetof(AstraVolumeUsageRecord, field))
#define ROW64(field) row->field = astra_load_be64(wire + offsetof(AstraVolumeUsageRecord, field))
        ROW64(volume.epoch); ROW64(volume.id); ROW64(volume.generation); ROW64(handle); ROW64(inode);
        ROW32(process); ROW32(kind); ROW32(rights); ROW32(flags); ROW32(path_length); ROW32(reserved);
#undef ROW32
#undef ROW64
    }
    return astra_volume_usage_page_valid(query, page) ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

uint32_t astra_vfs_host_usage_holders_changed(AstraVfsHostTransport *transport,
    uint32_t generation)
{
    AstraVfsHostRequest request;
    uint32_t status = astra_vfs_host_transport_begin(transport, 0u, 0u, &request);
    if (status != ASTRA_VFS_OK) return status;
    AstraHostCommand *command = request.command;
    memset(command, 0, sizeof(*command));
    command->size = sizeof(*command); command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_VOLUMES;
    command->operation = ASTRA_HOST_VOLUME_HOLDERS_CHANGED;
    command->generation = generation;
    status = astra_vfs_host_transport_submit(transport, &request, NULL, 0u, NULL, 0u);
    return status == ASTRA_VFS_OK && command->result_length != 0u ? ASTRA_VFS_ERR_PROTOCOL : status;
}

uint32_t astra_vfs_host_lookup_mount(AstraVfsHostTransport *transport, uint32_t generation,
                                     const char *name, uint32_t required_flags,
                                     AstraVolumeCatalogPage *scratch,
                                     AstraVolumeReference *volume)
{
    if (name == NULL || name[0] != '/' || scratch == NULL || volume == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *volume = (AstraVolumeReference){0};
    AstraVolumeCatalogQuery query = {.version = ASTRA_VOLUME_CATALOG_VERSION, .size = sizeof(query)};
    do {
        uint32_t status = astra_vfs_host_catalog(transport, generation, &query, scratch);
        if (status != ASTRA_VFS_OK)
            return status;
        query.epoch = scratch->epoch;
        query.sequence = scratch->sequence;
        query.after = scratch->next;
        for (uint32_t at = 0u; at < scratch->count; ++at) {
            const AstraVolumeRecord *row = &scratch->records[at];
            if ((row->kind == ASTRA_VOLUME_MOUNT || row->kind == ASTRA_VOLUME_BINDING) &&
                row->state == ASTRA_VOLUME_MOUNTED && (row->flags & required_flags) == required_flags &&
                strcmp(row->name, name) == 0) {
                if (volume->id != 0u)
                    return ASTRA_VFS_ERR_PROTOCOL;
                *volume = (AstraVolumeReference){query.epoch, row->id, row->generation};
            }
        }
    } while (query.after != 0u);
    return volume->id == 0u ? ASTRA_VFS_ERR_NOT_FOUND : ASTRA_VFS_OK;
}
