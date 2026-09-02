#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>

#include <stddef.h>
#include <string.h>

_Static_assert(ASTRA_HOST_FS_PATH_MAX == ASTRA_VFS_PATH_MAX,
               "host and VFS path contracts differ");

static uint32_t copy_path(char out[ASTRA_HOST_FS_PATH_MAX], const char *path)
{
    uint32_t index = 0u;

    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    while (index < ASTRA_HOST_FS_PATH_MAX && path[index] != '\0') {
        out[index] = path[index];
        ++index;
    }
    if (index == ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    out[index] = '\0';
    return ASTRA_VFS_OK;
}

static uint32_t command_begin(AstraVfsHostBackend *backend,
                              AstraVfsHostRequest *request,
                              uint16_t operation, uint32_t data_capacity)
{
    AstraHostCommand *command;
    uint32_t status = astra_vfs_host_transport_begin(
        backend->transport, data_capacity, request);

    if (status != ASTRA_VFS_OK)
        return status;
    command = request->command;
    memset(command, 0, offsetof(AstraHostCommand, path));
    command->size = sizeof(*command);
    command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command->operation = operation;
    command->generation = backend->generation;
    return ASTRA_VFS_OK;
}

static void publish_info(const AstraHostCommand *command,
                         AstraVfsNodeInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = ((uint64_t)command->node_size_hi << 32) |
                 command->node_size_lo;
    info->mtime = (int64_t)(((uint64_t)command->mtime_hi << 32) |
                            command->mtime_lo);
    info->uid = command->uid;
    info->gid = command->gid;
    info->kind = command->kind;
    info->mode = command->mode;
    info->nlink = command->nlink;
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

static uint32_t host_open(void *context, const char *path, uint32_t flags,
                          uint16_t create_mode, uintptr_t *node,
                          AstraVfsNodeInfo *info)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status;

    status = command_begin(backend, &request, ASTRA_HOST_FS_OPEN, 0u);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    if (copy_path(command->path, path) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    command->flags = (uint16_t)flags;
    command->value_lo = create_mode == ASTRA_VFS_MODE_DEFAULT ? 0666u :
                        create_mode;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status != ASTRA_VFS_OK)
        return status;
    if (command->handle == 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    *node = command->handle;
    publish_info(command, info);
    return ASTRA_VFS_OK;
}

static uint32_t host_close(void *context, uintptr_t node)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_CLOSE,
                                    0u);

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
    status = command_begin(backend, &request, ASTRA_HOST_FS_READ, length);
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
    status = command_begin(backend, &request, ASTRA_HOST_FS_WRITE, length);
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)node;
    command->offset_hi = (uint32_t)(offset >> 32);
    command->offset_lo = (uint32_t)offset;
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u)
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
    uint32_t status = command_begin(backend, &request, op, 0u);

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

static uint32_t host_stat(void *context, const char *path,
                          AstraVfsNodeInfo *info)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_STAT,
                                    0u);

    if (status != ASTRA_VFS_OK)
        return status;
    if (copy_path(request.command->path, path) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    status = submit(backend, &request, NULL, 0u, NULL, 0u);
    if (status == ASTRA_VFS_OK)
        publish_info(request.command, info);
    return status;
}

static uint32_t host_readdir(void *context, uintptr_t directory,
                             const char *path, uint64_t cookie, char *name,
                             uint32_t name_capacity, AstraVfsNodeInfo *info,
                             uint64_t *next)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    AstraHostCommand *command;
    uint32_t status = command_begin(backend, &request, ASTRA_HOST_FS_READDIR,
                                    name_capacity);

    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    command->handle = (uint32_t)directory;
    if (copy_path(command->path, path) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
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

static uint32_t host_path_mode(void *context, const char *path, uint16_t op,
                               uint16_t mode)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, op, 0u);

    if (status != ASTRA_VFS_OK)
        return status;
    if (copy_path(request.command->path, path) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    request.command->value_lo = mode;
    return submit(backend, &request, NULL, 0u, NULL, 0u);
}

static uint32_t host_mkdir(void *context, const char *path,
                           uint16_t create_mode)
{
    return host_path_mode(context, path, ASTRA_HOST_FS_MKDIR,
                          create_mode == ASTRA_VFS_MODE_DEFAULT ? 0777u :
                          create_mode);
}

static uint32_t host_unlink(void *context, const char *path)
{
    return host_path_mode(context, path, ASTRA_HOST_FS_UNLINK, 0u);
}

static uint32_t host_chmod(void *context, const char *path, uint16_t mode)
{
    return host_path_mode(context, path, ASTRA_HOST_FS_CHMOD, mode);
}

static uint32_t host_two_paths(void *context, const char *left,
                               const char *right, uint16_t operation)
{
    AstraVfsHostBackend *backend = context;
    AstraVfsHostRequest request;
    uint32_t status = command_begin(backend, &request, operation, 0u);

    if (status != ASTRA_VFS_OK)
        return status;
    if (copy_path(request.command->path, left) != ASTRA_VFS_OK ||
        copy_path(request.command->path2, right) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
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
    uint32_t status = command_begin(backend, &request,
                                    ASTRA_HOST_FS_READLINK, capacity);

    *length = 0u;
    if (status != ASTRA_VFS_OK)
        return status;
    command = request.command;
    if (copy_path(command->path, path) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
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

static const AstraVfsBackendOps host_ops = {
    host_open, host_close, host_read, host_write, host_sync, host_truncate,
    host_stat, host_readdir, host_mkdir, host_unlink, host_rename, host_chmod,
    host_readlink, host_symlink
};

int astra_vfs_host_init(AstraVfsHostBackend *backend,
                        AstraVfsHostTransport *transport,
                        uint32_t generation)
{
    if (backend == NULL || transport == NULL || generation == 0u)
        return 0;
    backend->transport = transport;
    backend->generation = generation;
    return 1;
}

const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    return &host_ops;
}
