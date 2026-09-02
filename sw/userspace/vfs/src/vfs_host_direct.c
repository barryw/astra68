#include <astra/vfs_host_direct.h>

#include <astra/runtime.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_port_transport.h>

#include <stddef.h>
#include <string.h>

typedef struct AstraVfsHostDirect {
    uint32_t magic;
    uint32_t version;
    uint32_t byte_size;
    AstraVfsHostTransport transport;
    AstraVfsHostBackend backend;
    char staged_path[ASTRA_VFS_PATH_MAX];
    uint8_t staged_operation;
} AstraVfsHostDirect;

#define ASTRA_VFS_HOST_DIRECT_MAGIC UINT32_C(0x56484452)
#define ASTRA_VFS_HOST_DIRECT_VERSION 6u

static const AstraVfsPortAcceleratorOps host_accelerator_ops = {
    astra_vfs_host_direct_connect,
    astra_vfs_host_direct_disconnect,
    astra_vfs_host_direct_abandon,
    astra_vfs_host_direct_transport,
    astra_vfs_host_direct_bulk
};

uint32_t astra_vfs_host_port_connect(AstraVfsClient *client,
                                     uint32_t service)
{
    return astra_vfs_port_connect_with_accelerator(
        client, service, &host_accelerator_ops);
}

uint32_t astra_vfs_host_port_connect_lazy(AstraVfsClient *client,
                                          uint32_t service)
{
    return astra_vfs_port_connect_lazy_with_accelerator(
        client, service, &host_accelerator_ops);
}

uint32_t astra_vfs_host_direct_connect(AstraVfsClient *client,
                                       uint32_t device)
{
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t area = 0u;
    uint32_t span = 0u;
    uint32_t status = ASTRA_VFS_ERR_LIMIT;

    if (client == NULL || device == 0u || client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    client->port_direct_lock = 0u;
    if (astra_rt_area_create_flagged(
            (uint32_t)sizeof(AstraVfsHostDirect),
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK ||
        astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span < sizeof(AstraVfsHostDirect))
        goto fail;
    direct = address;
    memset(direct, 0, sizeof(*direct));
    if (!astra_vfs_host_transport_init(
            &direct->transport, device, astra_vfs_state_lock_acquire,
            astra_vfs_state_lock_release, &client->port_direct_lock) ||
        !astra_vfs_host_init(&direct->backend,
                             &direct->transport,
                             direct->transport.generation))
        goto fail_transport;
    direct->magic = ASTRA_VFS_HOST_DIRECT_MAGIC;
    direct->version = ASTRA_VFS_HOST_DIRECT_VERSION;
    direct->byte_size = span;
    client->port_direct_address = address;
    client->port_direct_area = area;
    client->port_direct_device = device;
    client->port_direct_session = 1u;
    client->port_direct_detached = 0u;
    client->direct_backend_ops = astra_vfs_host_ops();
    client->direct_backend_context = &direct->backend;
    client->direct_backend_enter = astra_vfs_port_client_enter;
    client->direct_backend_leave = astra_vfs_port_client_leave;
    return ASTRA_VFS_OK;

fail_transport:
    if (address != NULL)
        astra_vfs_host_transport_destroy(
            &((AstraVfsHostDirect *)address)->transport);
fail:
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    if (area != 0u)
        (void)astra_close(area);
    client->port_direct_lock = 0u;
    return status;
}

static int direct_state_valid(const AstraVfsHostDirect *direct, uint32_t span,
                              uint32_t session)
{
    if (direct->magic != ASTRA_VFS_HOST_DIRECT_MAGIC ||
        direct->version != ASTRA_VFS_HOST_DIRECT_VERSION ||
        direct->byte_size != span || session != 1u ||
        direct->staged_operation > ASTRA_VFS_STAGE_SYMLINK)
        return 0;
    return 1;
}

uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t device, uint32_t session)
{
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t span = 0u;
    uint32_t old_generation;

    if (client == NULL || area == 0u || device == 0u ||
        session == ASTRA_VFS_SESSION_INVALID ||
        client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    client->port_direct_lock = 0u;
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span < sizeof(AstraVfsHostDirect)) {
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        (void)astra_close(area);
        (void)astra_close(device);
        client->port_direct_lock = 0u;
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    direct = address;
    if (!direct_state_valid(direct, span, session))
        goto invalid;
    old_generation = direct->transport.generation;
    if (!astra_vfs_host_transport_init(
            &direct->transport, device, astra_vfs_state_lock_acquire,
            astra_vfs_state_lock_release, &client->port_direct_lock) ||
        direct->transport.generation != old_generation ||
        !astra_vfs_host_init(&direct->backend,
                             &direct->transport,
                             direct->transport.generation))
        goto invalid;
    client->port_direct_address = address;
    client->port_direct_area = area;
    client->port_direct_device = device;
    client->port_direct_session = session;
    client->port_direct_detached = 0u;
    client->port_accelerator_ops = &host_accelerator_ops;
    client->direct_backend_ops = astra_vfs_host_ops();
    client->direct_backend_context = &direct->backend;
    client->direct_backend_enter = astra_vfs_port_client_enter;
    client->direct_backend_leave = astra_vfs_port_client_leave;
    return ASTRA_VFS_OK;

invalid:
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    (void)astra_close(device);
    client->port_direct_lock = 0u;
    return ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client)
{
    uint32_t activity;
    uint32_t area;
    uint32_t device;
    uint32_t service;
    uint32_t status;

    if (client == NULL || client->port_direct_address == NULL ||
        client->port_direct_area == 0u || client->port_direct_device == 0u)
        return ASTRA_VFS_ERR_INVALID;
    activity = client->activity;
    area = client->port_direct_area;
    device = client->port_direct_device;
    service = client->port_service;

    /* Fork clones capabilities but not mappings or process-local DMA state. */
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_device = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_lock = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
    (void)astra_close(area);

    /* Drop only the child's cloned port session, retaining its host device. */
    client->port_accelerator_ops = NULL;
    astra_vfs_port_abandon(client);
    status = astra_vfs_host_port_connect_lazy(client, service);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(device);
        return status;
    }
    client->activity = activity;
    status = astra_vfs_host_direct_connect(client, device);
    if (status != ASTRA_VFS_OK) {
        /* The clean lazy client remains a correct service-backed fallback. */
        (void)astra_close(device);
        return ASTRA_VFS_OK;
    }
    client->session = client->port_direct_session;
    client->port_direct_detached = 1u;
    return ASTRA_VFS_OK;
}

void astra_vfs_host_direct_disconnect(AstraVfsClient *client)
{
    AstraVfsHostDirect *direct;
    void *address;
    uint32_t area;
    uint32_t device;

    if (client == NULL || client->port_direct_address == NULL)
        return;
    address = client->port_direct_address;
    area = client->port_direct_area;
    device = client->port_direct_device;
    direct = address;
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_device = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_detached = 0u;
    client->port_direct_lock = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
    astra_vfs_host_transport_destroy(&direct->transport);
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    (void)astra_close(device);
}

void astra_vfs_host_direct_abandon(AstraVfsClient *client)
{
    uint32_t area;
    uint32_t device;

    if (client == NULL || client->port_direct_address == NULL)
        return;
    area = client->port_direct_area;
    device = client->port_direct_device;
    /*
     * A clone inherits cloneable capabilities, not shared-area mappings or
     * process-local DMA buffers.  The pointer therefore belongs to the
     * parent and must never be dereferenced here.  Closing the cloned area
     * and device handles drops all authority the child actually inherited;
     * the non-cloneable DMA handle remains solely the parent's.
     */
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_device = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_detached = 0u;
    client->port_direct_lock = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
    (void)astra_close(area);
    (void)astra_close(device);
}

static void direct_reply(AstraVfsClient *client, AstraVfsReply *reply)
{
    memset(reply, 0, sizeof(*reply));
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = client->version;
    reply->session = client->session;
    reply->status = ASTRA_VFS_ERR_INVALID;
}

static AstraVfsBackend direct_backend(AstraVfsHostDirect *direct)
{
    AstraVfsBackend backend = {astra_vfs_host_ops(), &direct->backend};

    return backend;
}

static void publish_info(AstraVfsReply *reply, const AstraVfsNodeInfo *info)
{
    reply->node_size = info->size;
    reply->mtime = info->mtime;
    reply->uid = info->uid;
    reply->gid = info->gid;
    reply->kind = info->kind;
    reply->mode = info->mode;
    reply->nlink = info->nlink;
}

static int stage_path(AstraVfsHostDirect *direct, const uint8_t *path)
{
    for (uint32_t index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        direct->staged_path[index] = (char)path[index];
        if (path[index] == 0u)
            return 1;
    }
    return 0;
}

static void direct_readdir(const AstraVfsBackend *backend,
                           const AstraVfsRequest *request,
                           AstraVfsReply *reply)
{
    AstraVfsNodeInfo info = {0};
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t index = 0u;

    reply->status = backend->ops->readdir(
        backend->context, 0u, (const char *)request->body.path,
        request->offset, name, (uint32_t)sizeof(name), &info, &next);
    if (reply->status != ASTRA_VFS_OK)
        return;
    while (index < ASTRA_VFS_NAME_MAX && name[index] != '\0') {
        reply->payload[index] = (uint8_t)name[index];
        ++index;
    }
    if (index == ASTRA_VFS_NAME_MAX) {
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        return;
    }
    reply->payload[index] = 0u;
    reply->count = index;
    reply->cursor = next;
    publish_info(reply, &info);
}

uint32_t astra_vfs_host_direct_transport(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsHostDirect *direct;
    AstraVfsBackend backend;
    const AstraVfsBackendOps *ops;
    const uint8_t *area;
    uint32_t area_size;

    if (client == NULL || request == NULL || reply == NULL ||
        client->port_direct_address == NULL)
        return ASTRA_VFS_ERR_INVALID;
    direct = client->port_direct_address;
    backend = direct_backend(direct);
    ops = backend.ops;
    direct_reply(client, reply);
    if (operation == ASTRA_VFS_OP_BIND_AREA ||
        operation == ASTRA_VFS_OP_BIND_LANE) {
        reply->status = ASTRA_VFS_OK;
        return ASTRA_VFS_OK;
    }
    if (operation == ASTRA_VFS_OP_READ_AREA ||
        operation == ASTRA_VFS_OP_WRITE_AREA ||
        operation == ASTRA_VFS_OP_READDIR_AREA ||
        operation == ASTRA_VFS_OP_READ_PATH) {
        area = astra_vfs_port_call_area(client, &area_size);
        if ((operation != ASTRA_VFS_OP_READ_PATH ||
             request->length == area_size) && area != NULL)
            return astra_vfs_host_direct_bulk(
                client, operation, request, (void *)area, area_size, reply);
    }
    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        reply->status = ASTRA_VFS_OK;
        reply->session = ASTRA_VFS_SESSION_INVALID;
        break;
    case ASTRA_VFS_OP_OPEN: {
        const uint32_t allowed =
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
            ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
            ASTRA_VFS_OPEN_DIRECTORY | ASTRA_VFS_OPEN_EXCLUSIVE |
            ASTRA_VFS_OPEN_APPEND;
        AstraVfsNodeInfo info = {0};
        uintptr_t node = 0u;
        uint16_t create_mode = ASTRA_VFS_MODE_DEFAULT;

        if ((request->flags & ~allowed) != 0u ||
            (request->flags & (ASTRA_VFS_OPEN_READ |
                               ASTRA_VFS_OPEN_WRITE)) == 0u ||
            ((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
             (request->flags & ASTRA_VFS_OPEN_CREATE) == 0u)) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        if (client->version >= UINT16_C(14) &&
            (request->flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
            if (request->offset != ASTRA_VFS_MODE_DEFAULT &&
                request->offset > ASTRA_VFS_MODE_MASK) {
                reply->status = ASTRA_VFS_ERR_INVALID;
                break;
            }
            create_mode = (uint16_t)request->offset;
        }
        reply->status = ops->open(
            backend.context, (const char *)request->body.path,
            request->flags, create_mode, &node, &info);
        if (reply->status == ASTRA_VFS_OK) {
            reply->file = (AstraVfsFile)node;
            publish_info(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_CLOSE:
        reply->status = ops->close(backend.context, request->file);
        break;
    case ASTRA_VFS_OP_READ: {
        uint32_t length = request->length > ASTRA_VFS_IO_MAX ?
            ASTRA_VFS_IO_MAX : request->length;

        reply->status = ops->read(
            backend.context, request->file, request->offset, reply->payload,
            length, &reply->count);
        if (reply->status == ASTRA_VFS_OK && reply->count > length)
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }
    case ASTRA_VFS_OP_WRITE: {
        uint64_t position = request->offset;

        if (request->length > ASTRA_VFS_IO_MAX) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        reply->status = ops->write(
            backend.context, request->file, request->offset, 0u,
            request->body.payload, request->length, &reply->count,
            &position);
        if (reply->status == ASTRA_VFS_OK) {
            if (reply->count > request->length)
                reply->status = ASTRA_VFS_ERR_PROTOCOL;
            else
                reply->node_size = position;
        }
        break;
    }
    case ASTRA_VFS_OP_SYNC:
        reply->status = client->version < UINT16_C(13) ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            ops->sync(backend.context, request->file);
        break;
    case ASTRA_VFS_OP_TRUNCATE:
        reply->status = client->version < UINT16_C(13) ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            ops->truncate(backend.context, request->file, request->offset);
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info = {0};

        reply->status = ops->stat(
            backend.context, (const char *)request->body.path, &info);
        if (reply->status == ASTRA_VFS_OK)
            publish_info(reply, &info);
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        direct_readdir(&backend, request, reply);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
        reply->status = client->version < UINT16_C(4) ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            astra_vfs_backend_readdir_into(
                &backend, 0u, (const char *)request->body.path,
                request->offset, request->length, reply->payload,
                ASTRA_VFS_IO_MAX, &reply->count, &reply->cursor);
        break;
    case ASTRA_VFS_OP_MKDIR:
        if (client->version >= UINT16_C(14) &&
            request->offset != ASTRA_VFS_MODE_DEFAULT &&
            request->offset > ASTRA_VFS_MODE_MASK)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->mkdir(
                backend.context, (const char *)request->body.path,
                client->version >= UINT16_C(14) ?
                    (uint16_t)request->offset : ASTRA_VFS_MODE_DEFAULT);
        break;
    case ASTRA_VFS_OP_UNLINK:
        reply->status = ops->unlink(
            backend.context, (const char *)request->body.path);
        break;
    case ASTRA_VFS_OP_RENAME_FROM:
        if (client->version < UINT16_C(11))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (!stage_path(direct, request->body.path))
            reply->status = ASTRA_VFS_ERR_INVALID;
        else {
            direct->staged_operation = ASTRA_VFS_STAGE_RENAME;
            reply->status = ASTRA_VFS_OK;
        }
        break;
    case ASTRA_VFS_OP_RENAME_TO:
        if (client->version < UINT16_C(11))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (direct->staged_operation != ASTRA_VFS_STAGE_RENAME)
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
        else {
            direct->staged_operation = ASTRA_VFS_STAGE_NONE;
            reply->status = ops->rename(
                backend.context, direct->staged_path,
                (const char *)request->body.path);
        }
        break;
    case ASTRA_VFS_OP_RENAME: {
        const AstraVfsRenameRequest *rename =
            (const AstraVfsRenameRequest *)request;

        reply->status = client->version < UINT16_C(16) ||
                                request->size != ASTRA_VFS_RENAME_REQUEST_SIZE ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            ops->rename(backend.context,
                        (const char *)rename->request.body.path,
                        (const char *)rename->to);
        break;
    }
    case ASTRA_VFS_OP_CHMOD:
        if (client->version < UINT16_C(14))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (request->offset > ASTRA_VFS_MODE_MASK)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->chmod(
                backend.context, (const char *)request->body.path,
                (uint16_t)request->offset);
        break;
    case ASTRA_VFS_OP_READLINK: {
        uint32_t capacity = request->length > ASTRA_VFS_IO_MAX ?
            ASTRA_VFS_IO_MAX : request->length;

        if (client->version < UINT16_C(14)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        reply->status = ops->readlink(
            backend.context, (const char *)request->body.path,
            reply->payload, capacity, &reply->count);
        if (reply->status == ASTRA_VFS_OK && reply->count > capacity)
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }
    case ASTRA_VFS_OP_SYMLINK_TARGET:
        if (client->version < UINT16_C(15))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (request->body.path[0] == 0u ||
                 !stage_path(direct, request->body.path))
            reply->status = ASTRA_VFS_ERR_INVALID;
        else {
            direct->staged_operation = ASTRA_VFS_STAGE_SYMLINK;
            reply->status = ASTRA_VFS_OK;
        }
        break;
    case ASTRA_VFS_OP_SYMLINK_TO:
        if (client->version < UINT16_C(15))
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
        else if (direct->staged_operation != ASTRA_VFS_STAGE_SYMLINK)
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
        else {
            direct->staged_operation = ASTRA_VFS_STAGE_NONE;
            reply->status = ops->symlink(
                backend.context, direct->staged_path,
                (const char *)request->body.path);
        }
        break;
    case ASTRA_VFS_OP_SYMLINK: {
        const AstraVfsRenameRequest *symlink =
            (const AstraVfsRenameRequest *)request;

        reply->status = client->version < UINT16_C(19) ||
                                request->size != ASTRA_VFS_RENAME_REQUEST_SIZE ?
            ASTRA_VFS_ERR_UNSUPPORTED :
            ops->symlink(backend.context,
                         (const char *)symlink->request.body.path,
                         (const char *)symlink->to);
        break;
    }
    default:
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_direct_bulk(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, void *buffer, uint32_t capacity,
    AstraVfsReply *reply)
{
    AstraVfsHostDirect *direct;
    AstraVfsBackend backend;
    uint32_t moved = 0u;
    uint64_t position = request != NULL ? request->offset : 0u;

    if (client == NULL || request == NULL || reply == NULL || buffer == NULL ||
        capacity == 0u || client->port_direct_address == NULL)
        return ASTRA_VFS_ERR_INVALID;
    direct = client->port_direct_address;
    backend = direct_backend(direct);
    direct_reply(client, reply);
    if (request->length == 0u || request->length > capacity)
        return ASTRA_VFS_OK;
    if (operation == ASTRA_VFS_OP_READ_AREA) {
        reply->status = backend.ops->read(
            backend.context, request->file, request->offset, buffer,
            request->length, &moved);
    } else if (operation == ASTRA_VFS_OP_WRITE_AREA) {
        reply->status = backend.ops->write(
            backend.context, request->file, request->offset,
            request->flags & ASTRA_VFS_OPEN_APPEND, buffer, request->length,
            &moved, &position);
        if (reply->status == ASTRA_VFS_OK)
            reply->node_size = position;
    } else if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        uint64_t next = request->offset;

        reply->status = astra_vfs_backend_readdir_into(
            &backend,
            request->file == ASTRA_VFS_FILE_INVALID ? 0u : request->file,
            (const char *)request->body.path, request->offset,
            request->length, buffer, capacity, &moved, &next);
        if (reply->status == ASTRA_VFS_OK)
            reply->cursor = next;
    } else if (operation == ASTRA_VFS_OP_READ_PATH) {
        reply->status = astra_vfs_backend_read_path(
            &backend, (const char *)request->body.path, buffer, capacity,
            &moved, &reply->node_size);
    } else {
        return ASTRA_VFS_ERR_UNSUPPORTED;
    }
    reply->count = reply->status == ASTRA_VFS_OK ? moved : 0u;
    return ASTRA_VFS_OK;
}
