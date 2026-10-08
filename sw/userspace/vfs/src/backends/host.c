#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>

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
    uint32_t status;

    if (operands > (ASTRA_HOST_FS_PATH_MAX - 1u) * 2u ||
        data_capacity > UINT32_MAX - (uint32_t)operands)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_host_transport_begin(
        backend->transport, backend->scope,
        data_capacity + (uint32_t)operands, request);
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

    memset(info, 0, sizeof(*info));
    info->size = ((uint64_t)node->size_hi << 32) | node->size_lo;
    info->mtime = (int64_t)(((uint64_t)(uint32_t)node->mtime.seconds_hi << 32) |
                            node->mtime.seconds_lo);
    info->uid = node->uid;
    info->gid = node->gid;
    info->kind = node->kind;
    info->mode = node->mode;
    info->nlink = node->nlink > UINT16_MAX ? UINT16_MAX : (uint16_t)node->nlink;
}

static uint32_t submit(AstraVfsHostBackend *backend,
                       AstraVfsHostRequest *request, const void *input,
                       uint32_t input_size, void *output,
                       uint32_t output_capacity)
{
    return astra_vfs_host_transport_submit(
        backend->transport, request, input, input_size, output,
        output_capacity);
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

static uint32_t host_read(void *context, uintptr_t node, uint64_t offset,
                          void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *moved = 0u;
    status = command_begin(backend, &request, ASTRA_HOST_FS_READ, length,
                           NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)node;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    status = submit(backend, &request, NULL, 0u, buffer, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *moved = command->result_length;
    return ASTRA_VFS_OK;
}

static uint32_t host_write(void *context, uintptr_t node, uint64_t offset,
                           uint32_t flags, const void *buffer, uint32_t length,
                           uint32_t *moved, uint64_t *position)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *moved = 0u;
    *position = offset;
    status = command_begin(backend, &request, ASTRA_HOST_FS_WRITE, length,
                           NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)node;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u)
        command->flags = ASTRA_HOST_FS_WRITE_APPEND;
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
                                 const char *path, AstraVfsNodeInfo *info)
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
    return host_stat_common(context, 0u, path, info);
}

static uint32_t host_stat_at(void *context, uintptr_t directory,
                             const char *path, AstraVfsNodeInfo *info)
{
    if (directory == 0u || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_stat_common(context, directory, path, info);
}

static uint32_t host_stat_node(void *context, uintptr_t node,
                                AstraVfsNodeInfo *info)
{
    if (node == 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_stat_common(context, node, NULL, info);
}

/* An open directory's next entry (the handle alone), or a path's. */
static uint32_t host_readdir(void *context, uintptr_t directory,
                             const char *path, uint64_t cookie, char *name,
                             uint32_t name_capacity, AstraVfsNodeInfo *info,
                             uint64_t *next)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    if (directory > UINT32_MAX || (directory == 0u && path == NULL))
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_READDIR,
                           name_capacity, directory != 0u ? NULL : path,
                           NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)directory;
    command->offset_hi = (uint32_t)(cookie >> 32);
    command->offset_lo = (uint32_t)cookie;
    status = submit(backend, &request, NULL, 0u, name, name_capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length >= name_capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    name[command->result_length] = '\0';
    *next = ((uint64_t)command->value_hi << 32) | command->value_lo;
    publish_info(command, info);
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

static uint32_t host_mkdir(void *context, const char *path,
                           uint16_t create_mode)
{
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, 0u, path, ASTRA_HOST_FS_MKDIR, 0u,
                          create_mode == ASTRA_VFS_MODE_DEFAULT ? 0777u :
                          create_mode);
}

/* A path unlink removes whichever kind the path names. */
static uint32_t host_unlink(void *context, const char *path)
{
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, 0u, path, ASTRA_HOST_FS_UNLINK, 0u, 0u);
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
        (flags & ~ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(
        context, directory, path, ASTRA_HOST_FS_UNLINK,
        (flags & ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u ?
            ASTRA_HOST_FS_UNLINK_DIRECTORY : ASTRA_HOST_FS_UNLINK_FILE,
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

/* The provider never follows a final symlink (Task 4), so NOFOLLOW is the
 * only behaviour it has and the wire carries no flag. */
static uint32_t host_chmod_at(void *context, uintptr_t directory,
                              const char *path, uint16_t mode,
                              uint32_t flags)
{
    if (directory == 0u || path == NULL ||
        (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
        (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    return host_path_mode(context, directory, path, ASTRA_HOST_FS_CHMOD, 0u,
                          mode);
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
static uint32_t host_two_paths(void *context, const char *left,
                               const char *right, uint16_t operation)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status;

    if (left == NULL || right == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, operation, 0u, left, right);
    if (status != ASTRA_VFS_OK)
        return status;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_rename(void *context, const char *from, const char *to)
{
    return host_two_paths(context, from, to, ASTRA_HOST_FS_RENAME);
}

static uint32_t host_readlink(void *context, const char *path, void *buffer,
                              uint32_t capacity, uint32_t *length)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    *length = 0u;
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = command_begin(backend, &request, ASTRA_HOST_FS_READLINK,
                           capacity, path, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    status = submit(backend, &request, NULL, 0u, buffer, capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->result_length > capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    *length = command->result_length;
    return ASTRA_VFS_OK;
}

static uint32_t host_symlink(void *context, const char *target,
                             const char *path)
{
    return host_two_paths(context, target, path, ASTRA_HOST_FS_SYMLINK);
}

static uint32_t host_link(void *context, const char *from, const char *to)
{
    return host_two_paths(context, from, to, ASTRA_HOST_FS_LINK);
}

static const AstraVfsBackendOps host_ops = {
    host_open, host_close, host_read, host_write, host_sync, host_truncate,
    host_stat, host_readdir, host_mkdir, host_unlink, host_rename, host_chmod,
    host_readlink, host_symlink, host_link, host_open_at, host_unlink_at,
    host_chmod_node, host_chmod_at, host_filesystem_info, host_stat_at,
    host_stat_node
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
    return 1;
}

const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    return &host_ops;
}

/* One provider control command on the transport's unbound channel. */
static uint32_t control(AstraVfsHostTransport *transport,
                        uint32_t generation, uint16_t operation,
                        uint16_t flags, uint32_t session, uint32_t volume,
                        const char *path, uint32_t *result)
{
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    size_t length = path != NULL ? strlen(path) : 0u;
    uint32_t status;

    if (length >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_host_transport_begin(transport, 0u, (uint32_t)length,
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
    command->value_lo = volume;
    request.path = path;
    request.path_length = (uint32_t)length;
    status = astra_vfs_host_transport_submit(transport, &request, NULL, 0u,
                                             NULL, 0u);
    if (status == ASTRA_VFS_OK && result != NULL)
        *result = command->result_value;
    return status;
}

uint32_t astra_vfs_host_claim(AstraVfsHostTransport *transport,
                              uint32_t generation, int release)
{
    return control(transport, generation, ASTRA_HOST_FS_CLAIM,
                   release ? ASTRA_HOST_FS_CLAIM_RELEASE : 0u, 0u, 0u,
                   NULL, NULL);
}

uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *transport,
                                     uint32_t generation, uint32_t parent,
                                     uint32_t volume, const char *root,
                                     uint16_t flags, uint32_t *session)
{
    uint32_t status;

    if (session == NULL || root == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *session = 0u;
    status = control(transport, generation, ASTRA_HOST_FS_SESSION_OPEN,
                     flags, parent, parent == 0u ? volume : 0u, root,
                     session);
    return status == ASTRA_VFS_OK && *session == 0u ?
        ASTRA_VFS_ERR_PROTOCOL : status;
}

uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *transport,
                                      uint32_t generation, uint32_t session)
{
    return control(transport, generation, ASTRA_HOST_FS_SESSION_CLOSE, 0u,
                   session, 0u, NULL, NULL);
}
