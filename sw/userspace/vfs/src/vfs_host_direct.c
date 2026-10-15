#include <astra/vfs_host_direct.h>
#include <astra/vfs_path_payload.h>

#include <astra/host.h>
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
    uint32_t generation;     /* the provider generation the lane began in */
    AstraVfsHostBackend backend;
} AstraVfsHostDirect;

#define ASTRA_VFS_HOST_DIRECT_MAGIC UINT32_C(0x56484452)
#define ASTRA_VFS_HOST_DIRECT_VERSION 10u

/*
 * One host transport per process. A thread has one host channel, so every
 * host-volume client of the process shares the thread's lane; each client
 * names its own scope per command (AstraVfsHostBackend.scope), and the lane
 * attaches it on first use. The first scope connected answers HOST_QUERY at
 * init, and then the transport keeps no client's handle: that client may
 * leave first, and its number may come back naming another scope.
 */
static AstraVfsHostTransport lane_transport;
static uint32_t lane_users;
static uint32_t lane_lock __attribute__((aligned(4)));
static uint32_t lane_setup __attribute__((aligned(4)));

static uint32_t lane_acquire(uint32_t scope, uint32_t *generation)
{
    uint32_t status = ASTRA_VFS_OK;

    (void)astra_vfs_state_lock_acquire(&lane_setup);
    if (lane_users == 0u) {
        if (astra_vfs_host_transport_init(&lane_transport, scope,
                                          astra_vfs_state_lock_acquire,
                                          astra_vfs_state_lock_release,
                                          &lane_lock))
            lane_transport.device = 0u;
        else
            status = ASTRA_VFS_ERR_UNSUPPORTED;
    }
    if (status == ASTRA_VFS_OK) {
        ++lane_users;
        *generation = lane_transport.generation;
    }
    astra_vfs_state_lock_release(&lane_setup);
    return status;
}

static void lane_release(void)
{
    (void)astra_vfs_state_lock_acquire(&lane_setup);
    if (lane_users != 0u && --lane_users == 0u)
        astra_vfs_host_transport_destroy(&lane_transport);
    astra_vfs_state_lock_release(&lane_setup);
}

/* A fork child: the parent's channels and DMA handles did not come across.
 * Its clients keep their scopes and open new lanes on first use. Called
 * once, before any client's astra_vfs_host_direct_after_fork. */
void astra_vfs_host_direct_fork_child(void)
{
    astra_vfs_host_transport_after_fork(&lane_transport);
    lane_lock = 0u;
    lane_setup = 0u;
}

static void client_clear(AstraVfsClient *client)
{
    client->port_direct_address = NULL;
    client->port_direct_area = 0u;
    client->port_direct_scope = 0u;
    client->port_direct_session = ASTRA_VFS_SESSION_INVALID;
    client->port_direct_detached = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;
}

static uint32_t lane_enter(AstraVfsClient *client)
{
    uint32_t status;

    for (;;) {
        AstraVfsHostDirect *direct;

        status = astra_vfs_port_client_enter(client);
        if (status != ASTRA_VFS_OK)
            return status;
        direct = client->port_direct_address;
        if (direct == NULL || !__atomic_load_n(&direct->backend.lost, __ATOMIC_ACQUIRE))
            return ASTRA_VFS_OK;
        astra_vfs_port_client_leave(client);
        status = astra_vfs_port_refresh(client);
        if (status != ASTRA_VFS_OK)
            return status;
    }
}

static void client_bind(AstraVfsClient *client, AstraVfsHostDirect *direct,
                        uint32_t area, uint32_t scope, uint32_t session)
{
    client->port_direct_address = direct;
    client->port_direct_area = area;
    client->port_direct_scope = scope;
    client->port_direct_session = session;
    client->port_direct_detached = 0u;
    client->direct_backend_ops = astra_vfs_host_ops();
    client->direct_backend_context = &direct->backend;
    client->direct_backend_enter = lane_enter;
    client->direct_backend_leave = astra_vfs_port_client_leave;
}

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

uint32_t astra_vfs_client_connect_service(AstraVfsClient *client,
                                          uint32_t service)
{
    return astra_vfs_port_connect_lazy_with_accelerator(
        client, service, &host_accelerator_ops);
}

uint32_t astra_vfs_host_direct_connect(AstraVfsClient *client,
                                       uint32_t scope)
{
    AstraHostLeaseInfo lease = {0};
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t generation = 0u;
    uint32_t area = 0u;
    uint32_t span = 0u;
    uint32_t status;

    if (client == NULL || scope == 0u || client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    /* The kernel says which provider session this scope binds; the client
     * names it in every command, and the provider refuses any other. */
    status = astra_host_lease_query(scope, &lease);
    if (status != ASTRA_SYSCALL_OK)
        return astra_vfs_storage_status(astra_vfs_host_status_from_syscall(status));
    if ((lease.capabilities & (ASTRA_HOST_CAP_FS_SCOPE |
                               ASTRA_HOST_CAP_FS_DESCRIPTIONS |
                               (ASTRA_HOST_CAP_FS_DIRECTORY_BATCH | ASTRA_HOST_CAP_FS_PATHS))) !=
            (ASTRA_HOST_CAP_FS_SCOPE | ASTRA_HOST_CAP_FS_DESCRIPTIONS |
             (ASTRA_HOST_CAP_FS_DIRECTORY_BATCH | ASTRA_HOST_CAP_FS_PATHS)))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = lane_acquire(scope, &generation);
    if (status != ASTRA_VFS_OK)
        return status;
    status = ASTRA_VFS_ERR_LIMIT;
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
    if (!astra_vfs_host_init(&direct->backend, &lane_transport, generation,
                             lease.scope)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto fail;
    }
    direct->backend.scope = scope;
    direct->magic = ASTRA_VFS_HOST_DIRECT_MAGIC;
    direct->version = ASTRA_VFS_HOST_DIRECT_VERSION;
    direct->byte_size = span;
    direct->generation = generation;
    client_bind(client, direct, area, scope, 1u);
    return ASTRA_VFS_OK;

fail:
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    if (area != 0u)
        (void)astra_close(area);
    lane_release();
    return status;
}

static int direct_state_valid(const AstraVfsHostDirect *direct, uint32_t span,
                              uint32_t session)
{
    return direct->magic == ASTRA_VFS_HOST_DIRECT_MAGIC &&
           direct->version == ASTRA_VFS_HOST_DIRECT_VERSION &&
           direct->byte_size == span && session == 1u;
}

/* After an in-place exec: the area and the scope handle survived, the lanes
 * did not (exec closed the channels). The provider generation must not have
 * moved, or the files the old image opened are gone. */
uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t scope, uint32_t session)
{
    AstraVfsHostDirect *direct;
    void *address = NULL;
    uint32_t span = 0u;
    uint32_t generation = 0u;

    if (client == NULL || area == 0u || scope == 0u ||
        session == ASTRA_VFS_SESSION_INVALID ||
        client->port_direct_address != NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span < sizeof(AstraVfsHostDirect)) {
        if (address != NULL)
            (void)astra_rt_area_unmap(address);
        (void)astra_close(area);
        (void)astra_close(scope);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    direct = address;
    if (!direct_state_valid(direct, span, session) ||
        lane_acquire(scope, &generation) != ASTRA_VFS_OK)
        goto invalid;
    if (generation != direct->generation) {
        lane_release();
        goto invalid;
    }
    direct->backend.transport = &lane_transport;
    direct->backend.scope = scope;
    client_bind(client, direct, area, scope, session);
    client->port_accelerator_ops = &host_accelerator_ops;
    return ASTRA_VFS_OK;

invalid:
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    (void)astra_close(scope);
    return ASTRA_VFS_ERR_INVALID;
}

/* A fork child's client: drop the parent's lane state and port session,
 * keep the inherited scope and negotiated contract, and drive a lane of
 * the child's own on that scope. */
uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client)
{
    AstraVfsHostDirect *direct;
    uint32_t activity;
    uint32_t area;
    uint32_t scope;
    uint32_t service;
    uint32_t status;
    uint32_t features;
    uint16_t version;
    uint8_t area_capable;

    if (client == NULL || client->port_direct_address == NULL ||
        client->port_direct_area == 0u || client->port_direct_scope == 0u)
        return ASTRA_VFS_ERR_INVALID;
    activity = client->activity;
    direct = client->port_direct_address;
    area = client->port_direct_area;
    scope = client->port_direct_scope;
    service = client->port_service;
    features = client->features;
    version = client->version;
    area_capable = client->port_area_capable;
    client_clear(client);
    (void)astra_rt_area_unmap(direct);
    (void)astra_close(area);
    lane_release();
    client->port_accelerator_ops = NULL;
    astra_vfs_port_abandon(client);
    status = astra_vfs_client_connect_service(client, service);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(scope);
        return status;
    }
    client->activity = activity;
    client->features = features;
    client->version = version;
    client->port_area_capable = area_capable;
    status = astra_vfs_host_direct_connect(client, scope);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(scope);
        /* An unused parent client can still carry a pre-reset scope. */
        if (status == ASTRA_VFS_ERR_LOST)
            return astra_vfs_port_refresh(client);
        return status;
    }
    client->session = client->port_direct_session;
    client->port_direct_detached = 1u;
    return ASTRA_VFS_OK;
}

void astra_vfs_host_direct_disconnect(AstraVfsClient *client)
{
    void *address;
    uint32_t area;
    uint32_t scope;

    if (client == NULL || client->port_direct_address == NULL)
        return;
    address = client->port_direct_address;
    area = client->port_direct_area;
    scope = client->port_direct_scope;
    client_clear(client);
    (void)astra_rt_area_unmap(address);
    (void)astra_close(area);
    astra_vfs_host_transport_forget_scope(&lane_transport, scope);
    (void)astra_close(scope);
    lane_release();
}

void astra_vfs_host_direct_abandon(AstraVfsClient *client)
{
    astra_vfs_host_direct_disconnect(client);
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

static void direct_readdir(const AstraVfsBackend *backend,
                           const AstraVfsRequest *request, const char *path,
                           const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    AstraVfsNodeInfo info = {0};
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t index = 0u;

    reply->status = backend->ops->readdir(
        backend->context, request->file, path,
        request->offset, name, (uint32_t)sizeof(name), &info, &next);
    if (reply->status != ASTRA_VFS_OK)
        return;
    while (index < ASTRA_VFS_NAME_MAX && name[index] != '\0') {
        ++index;
    }
    if (index == ASTRA_VFS_NAME_MAX) {
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        return;
    }
    if (transfer == NULL || transfer->output == NULL ||
        index >= transfer->output_capacity) {
        reply->status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        return;
    }
    memcpy(transfer->output, name, index + 1u);
    reply->count = index;
    reply->cursor = next;
    astra_vfs_node_info_publish(reply, &info);
}

uint32_t astra_vfs_host_direct_transport(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
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
    AstraVfsPathOperands paths = {0};
    reply->status = astra_vfs_path_request(operation, request, transfer, &paths);
    if (reply->status != ASTRA_VFS_OK)
        return ASTRA_VFS_OK;
    if (operation == ASTRA_VFS_OP_BIND_AREA ||
        operation == ASTRA_VFS_OP_BIND_LANE) {
        reply->status = ASTRA_VFS_OK;
        return ASTRA_VFS_OK;
    }
    if (operation == ASTRA_VFS_OP_READ_AREA ||
        operation == ASTRA_VFS_OP_WRITE_AREA ||
        operation == ASTRA_VFS_OP_READ_PATH) {
        area = astra_vfs_port_call_area(client, &area_size);
        if (area != NULL && request->length <= area_size)
            return astra_vfs_host_direct_bulk(
                client, operation, request, transfer, (void *)area, area_size, reply);
    }
    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        reply->status = ASTRA_VFS_OK;
        reply->session = ASTRA_VFS_SESSION_INVALID;
        break;
    case ASTRA_VFS_OP_OPEN: {
        const uint32_t allowed =
            ASTRA_VFS_OPEN_MASK;
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
        if ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
            if (request->offset != ASTRA_VFS_MODE_DEFAULT &&
                request->offset > ASTRA_VFS_MODE_MASK) {
                reply->status = ASTRA_VFS_ERR_INVALID;
                break;
            }
            create_mode = (uint16_t)request->offset;
        }
        reply->status = ops->open(
            backend.context, paths.first,
            request->flags, create_mode, &node, &info);
        if (reply->status == ASTRA_VFS_OK) {
            reply->file = (AstraVfsFile)node;
            astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_OPEN_AT: {
        const uint32_t allowed =
            ASTRA_VFS_OPEN_MASK;
        AstraVfsNodeInfo info = {0};
        uintptr_t node = 0u;
        uint16_t create_mode = ASTRA_VFS_MODE_DEFAULT;

        if (request->file == ASTRA_VFS_FILE_INVALID ||
            paths.first[0] == '\0' || paths.first[0] == '/' ||
            (request->flags & ~allowed) != 0u ||
            (request->flags & (ASTRA_VFS_OPEN_READ |
                               ASTRA_VFS_OPEN_WRITE)) == 0u ||
            ((request->flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
             (request->flags & ASTRA_VFS_OPEN_CREATE) == 0u) ||
            ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u &&
             request->offset != ASTRA_VFS_MODE_DEFAULT &&
             request->offset > ASTRA_VFS_MODE_MASK)) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        if ((request->flags & ASTRA_VFS_OPEN_CREATE) != 0u)
            create_mode = (uint16_t)request->offset;
        reply->status = ops->open_at(
            backend.context, request->file,
            paths.first, request->flags, create_mode,
            &node, &info);
        if (reply->status == ASTRA_VFS_OK) {
            reply->file = (AstraVfsFile)node;
            astra_vfs_node_info_publish(reply, &info);
        }
        break;
    }
    case ASTRA_VFS_OP_CLOSE:
        reply->status = ops->close(backend.context, request->file);
        break;
    case ASTRA_VFS_OP_READ: {
        uint32_t length = request->length > ASTRA_VFS_IO_MAX ?
            ASTRA_VFS_IO_MAX : request->length;

        reply->status = astra_vfs_backend_read_position(
            &backend, request->file, request->offset, request->flags,
            reply->payload, length, &reply->count, &reply->position);
        break;
    }
    case ASTRA_VFS_OP_WRITE: {
        uint64_t position = request->offset;

        if (request->length > ASTRA_VFS_IO_MAX ||
            (request->flags & ~(ASTRA_VFS_WRITE_APPEND |
                                 ASTRA_VFS_IO_SEQUENTIAL)) != 0u) {
            reply->status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        reply->status = ops->write(
            backend.context, request->file, request->offset, request->flags, request->write_limit,
            request->body.payload, request->length, &reply->count,
            &position);
        if (reply->status == ASTRA_VFS_OK) {
            if (reply->count > request->length)
                reply->status = ASTRA_VFS_ERR_PROTOCOL;
            else
                reply->position = position;
        }
        break;
    }
    case ASTRA_VFS_OP_SEEK:
        reply->status = ops->seek == NULL ? ASTRA_VFS_ERR_UNSUPPORTED :
            request->flags > 2u ? ASTRA_VFS_ERR_INVALID :
            ops->seek(backend.context, request->file,
                      (int64_t)request->offset, request->flags,
                      &reply->position);
        break;
    case ASTRA_VFS_OP_SHARE:
        reply->status = ops->share == NULL ? ASTRA_VFS_ERR_UNSUPPORTED :
            request->flags != 0u ? ASTRA_VFS_ERR_INVALID :
            ops->share(backend.context, request->file, &reply->token);
        break;
    case ASTRA_VFS_OP_ADOPT: {
        uintptr_t node = 0u;

        reply->status = ops->adopt == NULL ? ASTRA_VFS_ERR_UNSUPPORTED :
            request->file != ASTRA_VFS_FILE_INVALID ||
            (request->flags & ~ASTRA_VFS_ADOPT_DISCARD) != 0u ?
                ASTRA_VFS_ERR_INVALID :
            ops->adopt(backend.context, request->offset, request->flags,
                       &node);
        if (reply->status == ASTRA_VFS_OK)
            reply->file = (AstraVfsFile)node;
        break;
    }
    case ASTRA_VFS_OP_FILE_FLAGS:
        reply->status = ops->file_flags == NULL ? ASTRA_VFS_ERR_UNSUPPORTED :
            request->offset > UINT32_MAX ? ASTRA_VFS_ERR_INVALID :
            ops->file_flags(backend.context, request->file, request->flags,
                            (uint32_t)request->offset, &reply->open_flags);
        break;
    case ASTRA_VFS_OP_SYNC:
        reply->status = ops->sync(backend.context, request->file);
        break;
    case ASTRA_VFS_OP_TRUNCATE:
        reply->status = ops->truncate(backend.context, request->file,
                                      request->offset);
        break;
    case ASTRA_VFS_OP_LOOKUP:
        reply->status = astra_vfs_backend_path_operation(&backend, operation,
            request, transfer, request->file, 0u, reply);
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info = {0};

        reply->status = ops->stat(
            backend.context, paths.first, &info);
        if (reply->status == ASTRA_VFS_OK)
            astra_vfs_node_info_publish(reply, &info);
        break;
    }
    case ASTRA_VFS_OP_STAT_AT: {
        AstraVfsNodeInfo info = {0};

        if (request->file == ASTRA_VFS_FILE_INVALID ||
            paths.first[0] == '\0' ||
            paths.first[0] == '/' ||
            (request->flags & ~ASTRA_VFS_STAT_FOLLOW) != 0u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->stat_at(
                backend.context, request->file,
                paths.first, request->flags, &info);
        if (reply->status == ASTRA_VFS_OK)
            astra_vfs_node_info_publish(reply, &info);
        break;
    }
    case ASTRA_VFS_OP_STAT_FILE: {
        AstraVfsNodeInfo info = {0};

        if (request->file == ASTRA_VFS_FILE_INVALID ||
            request->flags != 0u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->stat_node(backend.context, request->file,
                                           &info);
        if (reply->status == ASTRA_VFS_OK)
            astra_vfs_node_info_publish(reply, &info);
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        direct_readdir(&backend, request, paths.first, transfer, reply);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
    case ASTRA_VFS_OP_READDIR_AREA:
        reply->status = astra_vfs_backend_readdir_into(
            &backend, request->file, paths.first,
            request->offset, request->length,
            transfer == NULL ? NULL : transfer->output,
            transfer == NULL ? 0u : transfer->output_capacity, &reply->count, &reply->cursor);
        break;
    case ASTRA_VFS_OP_MKDIR:
    case ASTRA_VFS_OP_RENAME:
    case ASTRA_VFS_OP_LINK:
    case ASTRA_VFS_OP_SYMLINK:
    case ASTRA_VFS_OP_READLINK:
    case ASTRA_VFS_OP_DIRECTORY_PATH:
        reply->status = astra_vfs_backend_path_operation(&backend, operation,
            request, transfer, request->file,
            operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK ?
                (uintptr_t)request->second_file : 0u, reply);
        break;
    case ASTRA_VFS_OP_UNLINK:
        reply->status = (request->flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u ?
            ASTRA_VFS_ERR_INVALID : ops->unlink(
                backend.context, paths.first,
                request->flags);
        break;
    case ASTRA_VFS_OP_UNLINK_AT:
        if (request->file == ASTRA_VFS_FILE_INVALID ||
            paths.first[0] == '\0' ||
            paths.first[0] == '/' ||
            (request->flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->unlink_at(
                backend.context, request->file,
                paths.first, request->flags);
        break;

    case ASTRA_VFS_OP_CHMOD:
        if (request->offset > ASTRA_VFS_MODE_MASK)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->chmod(
                backend.context, paths.first,
                (uint16_t)request->offset);
        break;
    case ASTRA_VFS_OP_CHMOD_FILE:
        if (request->file == ASTRA_VFS_FILE_INVALID ||
            request->offset > ASTRA_VFS_MODE_MASK)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->chmod_node(
                backend.context, request->file, (uint16_t)request->offset);
        break;
    case ASTRA_VFS_OP_CHMOD_AT:
        if (request->file == ASTRA_VFS_FILE_INVALID ||
            paths.first[0] == '\0' ||
            paths.first[0] == '/' ||
            request->offset > ASTRA_VFS_MODE_MASK ||
            (request->flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
            reply->status = ASTRA_VFS_ERR_INVALID;
        else
            reply->status = ops->chmod_at(
                backend.context, request->file,
                paths.first, (uint16_t)request->offset,
                request->flags);
        break;



    case ASTRA_VFS_OP_FILESYSTEM_INFO:
        reply->status = astra_vfs_backend_filesystem_info_into(
            &backend, request->file,
            request->file == ASTRA_VFS_FILE_INVALID ?
                paths.first : NULL,
            reply->payload, sizeof(reply->payload));
        if (reply->status == ASTRA_VFS_OK)
            reply->count = ASTRA_VFS_FILESYSTEM_INFO_SIZE;
        break;
    default:
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_direct_bulk(
    AstraVfsClient *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, void *buffer, uint32_t capacity,
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
    AstraVfsPathOperands paths = {0};
    reply->status = astra_vfs_path_request(operation, request, transfer, &paths);
    if (reply->status != ASTRA_VFS_OK)
        return ASTRA_VFS_OK;
    if (request->length == 0u ||
        (operation != ASTRA_VFS_OP_READDIR_AREA && request->length > capacity))
        return ASTRA_VFS_OK;
    if (operation == ASTRA_VFS_OP_READ_AREA) {
        reply->status = astra_vfs_backend_read_position(
            &backend, request->file, request->offset, request->flags,
            buffer, request->length, &moved, &reply->position);
    } else if (operation == ASTRA_VFS_OP_WRITE_AREA) {
        reply->status =
            (request->flags & ~(ASTRA_VFS_WRITE_APPEND |
                                 ASTRA_VFS_IO_SEQUENTIAL)) != 0u ?
            ASTRA_VFS_ERR_INVALID : backend.ops->write(
            backend.context, request->file, request->offset,
            request->flags, request->write_limit, buffer, request->length,
            &moved, &position);
        if (reply->status == ASTRA_VFS_OK)
            reply->position = position;
    } else if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        uint64_t next = request->offset;

        reply->status = astra_vfs_backend_readdir_into(
            &backend,
            request->file == ASTRA_VFS_FILE_INVALID ? 0u : request->file,
            paths.first, request->offset,
            request->length, buffer, capacity, &moved, &next);
        if (reply->status == ASTRA_VFS_OK)
            reply->cursor = next;
    } else if (operation == ASTRA_VFS_OP_READ_PATH) {
        reply->status = astra_vfs_backend_read_path(
            &backend, paths.first, buffer, request->length,
            &moved, &reply->node_size);
    } else {
        return ASTRA_VFS_ERR_UNSUPPORTED;
    }
    reply->count = reply->status == ASTRA_VFS_OK ? moved : 0u;
    return ASTRA_VFS_OK;
}
