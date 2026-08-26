/*
 * The storage protocol across a process boundary.
 *
 * Both ends of one crossing. The client sends a request and waits for the
 * reply; the service receives, dispatches and replies. Nothing above either
 * end changes: the Kit's transport is a callback precisely so that the process
 * boundary could become a deployment decision rather than a rewrite, and this
 * is the file that cashes that in.
 *
 * Nothing here emits an event. This is the path the events service's own
 * clients take, so a transport that logged would be logging about logging --
 * and the rule has to be structural rather than careful, which is why this
 * file does not include astra/event_emit.h and must not grow an include of it.
 */

#include <astra/vfs_port_transport.h>

#include <astra/bytes.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>

/*
 * One reply at a time, one message deep. The protocol is synchronous, so a
 * deeper reply port would be capacity nothing can use.
 */
#define VFS_PORT_REPLY_MESSAGES 1u

int
astra_vfs_port_quota_storage(uint32_t element_size, void **storage,
                             uint32_t *capacity)
{
    uint32_t handle = 0u;
    uint32_t span = 0u;
    uint32_t count;
    void *address = NULL;

    if (element_size == 0u || storage == NULL || capacity == NULL)
        return 0;
    *storage = NULL;
    *capacity = 0u;
    if (astra_rt_area_create_flagged(
            ASTRA_AREA_SIZE_MAX,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            ASTRA_AREA_CREATE_RESERVED, &handle) != ASTRA_SYSCALL_OK)
        return 0;
    if (astra_rt_area_map(handle,
                          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL) {
        (void)astra_close(handle);
        return 0;
    }
    count = span / element_size;
    if (count > ASTRA_VFS_FILE_HANDLE_MAX)
        count = ASTRA_VFS_FILE_HANDLE_MAX;
    if (count == 0u) {
        (void)astra_rt_area_unmap(address);
        (void)astra_close(handle);
        return 0;
    }
    /* The service owns the mapping for its lifetime; process teardown closes
     * the retained capability and mapping together. */
    *storage = address;
    *capacity = count;
    return 1;
}

static void
fill_header(AstraMessageHeader *header, uint32_t operation, uint32_t size,
            uint32_t transaction)
{
    astra_message_header_set(header, size, ASTRA_VFS_PROTOCOL,
                             ASTRA_VFS_VERSION, operation, transaction);
}

static void
clear_service_reply(AstraVfsReply *reply, uint16_t version, uint32_t session)
{
    uint8_t *bytes = (uint8_t *)reply;

    for (uint32_t index = 0u; index < sizeof(*reply); ++index)
        bytes[index] = 0u;
    reply->size = ASTRA_VFS_REPLY_SIZE;
    reply->version = version;
    reply->session = session;
    reply->status = ASTRA_VFS_ERR_PROTOCOL;
}

static void
port_client_reset(AstraVfsClient *client)
{
    if (client->port_reply_send != 0u)
        (void)astra_close(client->port_reply_send);
    if (client->port_reply_source != 0u)
        (void)astra_close(client->port_reply_source);
    if (client->port_reply_receive != 0u)
        (void)astra_close(client->port_reply_receive);
    if (client->port_area_send != 0u)
        (void)astra_close(client->port_area_send);
    if (client->port_area_address != NULL)
        (void)astra_rt_area_unmap(client->port_area_address);
    if (client->port_area != 0u)
        (void)astra_close(client->port_area);
    client->port_reply_receive = 0u;
    client->port_reply_source = 0u;
    client->port_reply_send = 0u;
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
    client->port_area_capable = 0u;
}

static uint32_t
reply_channel(AstraVfsClient *client)
{
    if (client->port_reply_receive != 0u)
        return ASTRA_SYSCALL_OK;
    return astra_rt_port_create(VFS_PORT_REPLY_MESSAGES,
                                (uint32_t)sizeof(AstraVfsReplyMessage),
                                &client->port_reply_receive,
                                &client->port_reply_source);
}

static uint32_t
duplicate_reply_send(AstraVfsClient *client, uint32_t *duplicate)
{
    uint32_t status = reply_channel(client);

    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_rt_handle_duplicate(client->port_reply_source,
                                    ASTRA_RIGHT_SIGNAL |
                                        ASTRA_RIGHT_WAIT |
                                        ASTRA_RIGHT_TRANSFER,
                                    duplicate);
    if (status != ASTRA_SYSCALL_INVALID_HANDLE)
        return status;

    /* A test reset or a closed channel: recreate once, then report honestly. */
    (void)astra_close(client->port_reply_source);
    (void)astra_close(client->port_reply_receive);
    client->port_reply_source = 0u;
    client->port_reply_receive = 0u;
    status = reply_channel(client);
    return status == ASTRA_SYSCALL_OK ?
        astra_rt_handle_duplicate(client->port_reply_source,
                               ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                                   ASTRA_RIGHT_TRANSFER,
                               duplicate) : status;
}

uint32_t
astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    uint32_t status;

    if (client == NULL || service == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->port_service = service;
    client->port_reply_receive = 0u;
    client->port_reply_source = 0u;
    client->port_reply_send = 0u;
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
    status = astra_vfs_connect(client, astra_vfs_port_transport, client);
    if (status != ASTRA_VFS_OK) {
        port_client_reset(client);
    } else {
        client->port_area_capable = 1u;
    }
    return status;
}

uint32_t
astra_vfs_port_connect_lazy(AstraVfsClient *client, uint32_t service)
{
    if (client == NULL || service == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->transport = astra_vfs_port_transport;
    client->context = client;
    client->session = ASTRA_VFS_SESSION_INVALID;
    client->version = ASTRA_VFS_VERSION;
    client->port_area_capable = 1u;
    client->port_service = service;
    client->port_reply_receive = 0u;
    client->port_reply_source = 0u;
    client->port_reply_send = 0u;
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
    return ASTRA_VFS_OK;
}

static int
first_operation(uint32_t operation)
{
    return operation == ASTRA_VFS_OP_OPEN ||
           operation == ASTRA_VFS_OP_STAT ||
           operation == ASTRA_VFS_OP_READDIR ||
           operation == ASTRA_VFS_OP_MKDIR ||
           operation == ASTRA_VFS_OP_UNLINK ||
           operation == ASTRA_VFS_OP_CHMOD ||
           operation == ASTRA_VFS_OP_READLINK ||
           operation == ASTRA_VFS_OP_READDIR_BATCH ||
           operation == ASTRA_VFS_OP_READ_PATH;
}

static uint32_t ensure_area_size(AstraVfsClient *client, uint32_t needed);

uint32_t
astra_vfs_port_transport(void *context, uint32_t operation,
                         const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsClient *client = context;
    AstraVfsRequestMessage *outgoing;
    AstraVfsReplyMessage *incoming;
    AstraVfsRequest *area_request;
    uint32_t handles[1] = {0u};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint32_t wire_operation = operation;
    int fused_hello;

    if (client == NULL || client->port_service == 0u || request == NULL ||
        reply == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    outgoing = &client->port_outgoing;
    incoming = &client->port_incoming;
    area_request = &client->port_area_request;
    fused_hello = client->session == ASTRA_VFS_SESSION_INVALID &&
                  client->version >= UINT16_C(8) &&
                  first_operation(operation);
    if (operation != ASTRA_VFS_OP_HELLO &&
        client->session == ASTRA_VFS_SESSION_INVALID && !fused_hello) {
        AstraVfsRequest retry = *request;

        status = astra_vfs_connect(client, astra_vfs_port_transport, client);
        if (status != ASTRA_VFS_OK)
            return status;
        client->port_area_capable = 1u;
        retry.session = client->session;
        retry.version = client->version;
        return astra_vfs_port_transport(client, operation, &retry, reply);
    }
    if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        uint32_t record_max = ASTRA_VFS_DIRENT_HEADER +
                              ASTRA_VFS_NAME_MAX - 1u;

        if (client->version < UINT16_C(10))
            return ASTRA_VFS_ERR_UNSUPPORTED;
        if (request->length == 0u ||
            request->length > ASTRA_VFS_BULK_MAX / record_max)
            return ASTRA_VFS_ERR_INVALID;
        *area_request = *request;
        status = ensure_area_size(client, request->length * record_max);
        if (status != ASTRA_VFS_OK)
            return status;
        area_request->session = client->session;
        area_request->version = client->version;
        request = area_request;
    }
    if (fused_hello)
        wire_operation = ASTRA_VFS_OP_HELLO;
    /*
     * Version 3 moves the send handle once, during HELLO. If the peer negotiates
     * version 2, the reply below closes this pair and the next request creates
     * another one, preserving the old transport during a rolling update.
     */
    if (operation == ASTRA_VFS_OP_BIND_AREA) {
        if (client->port_area_send == 0u)
            return ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = client->port_area_send;
        handle_count = 1u;
    } else if (wire_operation == ASTRA_VFS_OP_HELLO ||
               client->version < UINT16_C(3)) {
        status = duplicate_reply_send(client, &client->port_reply_send);
        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = client->port_reply_send;
        handle_count = 1u;
    }
    fill_header(&outgoing->header, wire_operation, (uint32_t)sizeof(*outgoing),
                request->activity);
    outgoing->request = *request;
    if (fused_hello)
        outgoing->request.file = operation;

    status = astra_port_send(client->port_service, outgoing,
                             sizeof(*outgoing), handles, handle_count);
    if (status != ASTRA_SYSCALL_OK) {
        if (operation == ASTRA_VFS_OP_BIND_AREA) {
            if (client->port_area_send != 0u)
                (void)astra_close(client->port_area_send);
            client->port_area_send = 0u;
        } else if (handle_count != 0u) {
            port_client_reset(client);
        }
        /*
         * Which failure it was, because they call for different things. A full
         * port is a service that is behind and a caller may try again; a
         * refused message is this client's own mistake; anything else is a
         * service that is not there. Collapsing all three into "peer dead" is
         * what made a wrong message size look like a missing service, and cost
         * an afternoon finding out which.
         */
        if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
            return ASTRA_VFS_ERR_BUSY;
        }
        if (status == ASTRA_SYSCALL_INVALID_ARGUMENT ||
            status == ASTRA_SYSCALL_BAD_ADDRESS) {
            return ASTRA_VFS_ERR_INVALID;
        }
        if (status == ASTRA_SYSCALL_INVALID_HANDLE) {
            return ASTRA_VFS_ERR_BAD_HANDLE;
        }
        /*
         * The service is gone. This is the case the whole return value exists
         * for, and splitting the three above out of it left it falling into
         * the default -- so a caller whose service had died was told
         * ASTRA_VFS_ERR_NOT_FOUND, which to anything above here means the file
         * is not there rather than that nobody is. The receive path in this
         * same file has always answered ASTRA_VFS_ERR_PEER for it.
         */
        if (status == ASTRA_SYSCALL_PEER_DEAD ||
            status == ASTRA_SYSCALL_CLOSED) {
            port_client_reset(client);
            return ASTRA_VFS_ERR_PEER;
        }
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (handle_count != 0u) {
        if (operation == ASTRA_VFS_OP_BIND_AREA)
            client->port_area_send = 0u;
        else
            client->port_reply_send = 0u; /* moved into the service message */
    }
    /*
     * The request cannot be answered until this thread lets the service run.
     * Peer closure wakes the wait; elapsed time does not prove peer death.
     */
    status = astra_wait_one(client->port_reply_receive,
                            ASTRA_DEADLINE_FOREVER, NULL);
    if (status == ASTRA_SYSCALL_OK) {
        status = astra_port_receive(client->port_reply_receive, incoming,
                                    sizeof(*incoming), NULL, 0u, &size, NULL);
    }
    if (status != ASTRA_SYSCALL_OK) {
        port_client_reset(client);
        return ASTRA_VFS_ERR_PEER;
    }

    /*
     * A reply that is not this protocol is not an answer. It cannot be
     * forwarded to the caller as a status, because the caller would read a
     * field nobody filled in.
     */
    if (size != sizeof(*incoming) ||
        incoming->header.protocol != ASTRA_VFS_PROTOCOL ||
        incoming->header.operation != wire_operation ||
        incoming->reply.size != ASTRA_VFS_REPLY_SIZE) {
        port_client_reset(client);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *reply = incoming->reply;
    if (fused_hello && reply->session != ASTRA_VFS_SESSION_INVALID) {
        client->session = reply->session;
        client->version = reply->version;
        if (reply->version < UINT16_C(8) && reply->status == ASTRA_VFS_OK) {
            AstraVfsRequest retry = *request;

            retry.session = client->session;
            retry.version = client->version;
            return astra_vfs_port_transport(client, operation, &retry, reply);
        }
    }
    if (operation == ASTRA_VFS_OP_BYE)
        port_client_reset(client);
    return ASTRA_VFS_OK;
}

static void
prepare_request(AstraVfsClient *client, uint32_t operation)
{
    uint8_t *bytes = (uint8_t *)&client->request;
    uint32_t index;

    for (index = 0u; index < sizeof(client->request); ++index)
        bytes[index] = 0u;
    client->request.size = ASTRA_VFS_REQUEST_SIZE;
    client->request.version = client->version;
    client->request.session = client->session;
    client->request.activity = client->activity;
    (void)operation;
}

/*
 * The transfer area, sized to what has actually been asked for.
 *
 * A client that only ever reads manifests should not commit -- and the kernel
 * should not zero -- the largest transfer the protocol allows. Most clients
 * never grow past the first step; the ones that load a library pay one
 * rebind. Growth is one-way, because shrinking would trade a rebind for
 * nothing.
 */
/*
 * Measured: starting small and growing on first large read cost more than it
 * saved -- the grow is a rebind plus a repeated request, and it lands on
 * exactly the large reads that matter. One commit up front wins.
 */
/* Measured fast default, not a refusal ceiling; ensure_area_size grows it. */
#define VFS_PORT_AREA_INITIAL (512u * 1024u)

static void release_area(AstraVfsClient *client)
{
    if (client->port_area_send != 0u)
        (void)astra_close(client->port_area_send);
    if (client->port_area_address != NULL)
        (void)astra_rt_area_unmap(client->port_area_address);
    if (client->port_area != 0u)
        (void)astra_close(client->port_area);
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
}

static uint32_t
ensure_area_size(AstraVfsClient *client, uint32_t needed)
{
    uint32_t status;
    void *address = NULL;
    uint32_t size = 0u;
    uint32_t wanted = VFS_PORT_AREA_INITIAL;

    if (needed > ASTRA_VFS_BULK_MAX)
        needed = ASTRA_VFS_BULK_MAX;
    while (wanted < needed)
        wanted <<= 1;
    if (client->port_area_address != NULL) {
        if (client->port_area_size >= wanted)
            return ASTRA_VFS_OK;
        release_area(client);
    }
    status = astra_rt_area_create(
        wanted,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER,
        &client->port_area);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_rt_area_map(client->port_area,
                            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                            &address, &size);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    status = astra_rt_handle_duplicate(
        client->port_area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &client->port_area_send);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    client->port_area_address = address;
    client->port_area_size = size;
    prepare_request(client, ASTRA_VFS_OP_BIND_AREA);
    client->request.length = size;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_BIND_AREA,
                                      &client->request, &client->reply);
    if (status == ASTRA_VFS_OK)
        status = client->reply.status;
    if (status == ASTRA_VFS_OK)
        return status;

fail:
    if (client->port_area_send != 0u)
        (void)astra_close(client->port_area_send);
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    (void)astra_close(client->port_area);
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
    return status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ASTRA_VFS_ERR_LIMIT :
                                                    ASTRA_VFS_ERR_IO;
}

/* The client's own path copy; vfs_client.c keeps its set_path to itself. */
static int port_set_path(AstraVfsRequest *request, const char *path)
{
    uint32_t at = 0u;

    while (path[at] != '\0') {
        if (at + 1u >= ASTRA_VFS_PATH_MAX)
            return 0;
        request->body.path[at] = (uint8_t)path[at];
        ++at;
    }
    request->body.path[at] = 0u;
    return 1;
}

uint32_t
astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                         const uint8_t **bytes, uint32_t *moved,
                         uint64_t *node_size)
{
    uint32_t status;

    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL)
        *node_size = 0u;
    if (client->version < UINT16_C(5))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = ensure_area_size(client, VFS_PORT_AREA_INITIAL);
    if (status != ASTRA_VFS_OK)
        return status;
    /*
     * Two attempts at most. The first asks with whatever the area already is;
     * a file too big for it is answered with its size and nothing read, and
     * the second asks again with the area grown to fit. That costs a round
     * trip only for a file bigger than this client has ever read.
     */
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        prepare_request(client, ASTRA_VFS_OP_READ_PATH);
        if (!port_set_path(&client->request, path))
            return ASTRA_VFS_ERR_INVALID;
        client->request.length = client->port_area_size;
        status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_PATH,
                                          &client->request, &client->reply);
        if (status != ASTRA_VFS_OK)
            return status;
        if (node_size != NULL)
            *node_size = client->reply.node_size;
        if (client->reply.status != ASTRA_VFS_ERR_LIMIT ||
            attempt != 0u ||
            client->reply.node_size <= client->port_area_size ||
            client->reply.node_size > ASTRA_VFS_BULK_MAX)
            break;
        status = ensure_area_size(client,
                                  (uint32_t)client->reply.node_size);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (client->reply.status != ASTRA_VFS_OK)
        return client->reply.status;
    if (client->reply.count > client->port_area_size)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = client->port_area_address;
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_path_inline(AstraVfsClient *client, const char *path,
                                const uint8_t **bytes, uint32_t *moved,
                                uint64_t *node_size)
{
    uint32_t status;

    if (client == NULL || path == NULL || bytes == NULL || moved == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (node_size != NULL)
        *node_size = 0u;
    if (client->version < UINT16_C(7))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    prepare_request(client, ASTRA_VFS_OP_READ_PATH);
    if (!port_set_path(&client->request, path))
        return ASTRA_VFS_ERR_INVALID;
    client->request.length = ASTRA_VFS_IO_MAX;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_PATH,
                                      &client->request, &client->reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (node_size != NULL)
        *node_size = client->reply.node_size;
    if (client->reply.status != ASTRA_VFS_OK)
        return client->reply.status;
    if (client->reply.count > ASTRA_VFS_IO_MAX)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = client->reply.payload;
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_borrow(AstraVfsClient *client, AstraVfsFile file,
                           uint64_t offset, uint32_t length,
                           const uint8_t **bytes, uint32_t *moved)
{
    uint32_t status;

    if (client == NULL || bytes == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    if (client->version < UINT16_C(3))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (length > client->port_area_size)
        return ASTRA_VFS_ERR_LIMIT;
    prepare_request(client, ASTRA_VFS_OP_READ_AREA);
    client->request.file = file;
    client->request.offset = offset;
    client->request.length = length;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_AREA,
                                      &client->request, &client->reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (client->reply.status != ASTRA_VFS_OK)
        return client->reply.status;
    if (client->reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *bytes = client->port_area_address;
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, void *buffer, uint32_t length,
                         uint32_t *moved)
{
    uint8_t *out = buffer;
    const uint8_t *shared;
    uint32_t status;
    uint32_t index;

    if (client == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (client->version < UINT16_C(3))
        return astra_vfs_read(client, file, offset, buffer,
                              length < ASTRA_VFS_IO_MAX ? length :
                                                          ASTRA_VFS_IO_MAX,
                              moved);
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (length > client->port_area_size)
        length = client->port_area_size;
    prepare_request(client, ASTRA_VFS_OP_READ_AREA);
    client->request.file = file;
    client->request.offset = offset;
    client->request.length = length;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_READ_AREA,
                                      &client->request, &client->reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (client->reply.status != ASTRA_VFS_OK)
        return client->reply.status;
    if (client->reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    shared = client->port_area_address;
    (void)index;
    memcpy(out, shared, client->reply.count);
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_port_write_bulk(AstraVfsClient *client, AstraVfsFile file,
                          uint64_t offset, const void *buffer, uint32_t length,
                          uint32_t *moved)
{
    uint32_t status;

    if (client == NULL || buffer == NULL || moved == NULL || length == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    if (client->transport != astra_vfs_port_transport ||
        client->version < UINT16_C(9))
        return astra_vfs_write(client, file, offset, buffer,
                               length < ASTRA_VFS_IO_MAX ? length :
                                                           ASTRA_VFS_IO_MAX,
                               moved);
    status = ensure_area_size(client, length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (length > client->port_area_size)
        length = client->port_area_size;
    memcpy(client->port_area_address, buffer, length);
    prepare_request(client, ASTRA_VFS_OP_WRITE_AREA);
    client->request.file = file;
    client->request.offset = offset;
    client->request.length = length;
    status = astra_vfs_port_transport(client, ASTRA_VFS_OP_WRITE_AREA,
                                      &client->request, &client->reply);
    if (status != ASTRA_VFS_OK)
        return status;
    if (client->reply.status != ASTRA_VFS_OK)
        return client->reply.status;
    if (client->reply.count > length)
        return ASTRA_VFS_ERR_PROTOCOL;
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

static int
port_state_acquire(const AstraVfsPortService *host)
{
    return host->state_acquire == NULL ||
           host->state_acquire(host->state_lock_context);
}

static void
port_state_release(const AstraVfsPortService *host)
{
    if (host->state_release != NULL)
        host->state_release(host->state_lock_context);
}

static void
port_refuse(AstraVfsPortService *host)
{
    if (port_state_acquire(host)) {
        ++host->refused;
        port_state_release(host);
    }
}

typedef struct AstraVfsReplyLease {
    int slot;
    uint32_t session;
    uint32_t handle;
    uint8_t *area;
    uint32_t area_size;
    int active;
} AstraVfsReplyLease;

typedef struct AstraVfsReplyResources {
    uint32_t session;
    uint32_t reply_handle;
    uint32_t area_handle;
    uint8_t *area_address;
} AstraVfsReplyResources;

static int
reply_acquire(AstraVfsPortService *host, uint32_t session,
              AstraVfsReplyLease *lease)
{
    uint32_t index;

    lease->slot = -1;
    lease->session = session;
    lease->handle = 0u;
    lease->area = NULL;
    lease->area_size = 0u;
    lease->active = 0;
    if (session == ASTRA_VFS_SESSION_INVALID || !port_state_acquire(host))
        return 0;
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] != session)
            continue;
        ++host->reply_references[index];
        lease->slot = (int)index;
        lease->handle = host->reply_handles[index];
        lease->area = host->area_addresses[index];
        lease->area_size = host->area_sizes[index];
        if (host->reply_references[index] == 1u)
            lease->active = 1;
        break;
    }
    port_state_release(host);
    return lease->slot >= 0;
}

static int
reply_bind(AstraVfsPortService *host, uint32_t session, uint32_t handle)
{
    uint32_t index;

    if (!port_state_acquire(host))
        return -1;
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] == 0u) {
            host->reply_sessions[index] = session;
            host->reply_handles[index] = handle;
            host->reply_references[index] = 0u;
            host->reply_closing[index] = 0u;
            port_state_release(host);
            return (int)index;
        }
    }
    port_state_release(host);
    return -1;
}

static void
reply_resources_close(const AstraVfsReplyResources *resources)
{
    if (resources->area_address != NULL)
        (void)astra_rt_area_unmap(resources->area_address);
    if (resources->area_handle != 0u)
        (void)astra_close(resources->area_handle);
    if (resources->reply_handle != 0u)
        (void)astra_close(resources->reply_handle);
}

static void
reply_detach_locked(AstraVfsPortService *host, uint32_t index,
                    AstraVfsReplyResources *resources)
{
    resources->session = host->reply_sessions[index];
    resources->reply_handle = host->reply_handles[index];
    resources->area_handle = host->area_handles[index];
    resources->area_address = host->area_addresses[index];
    host->reply_sessions[index] = 0u;
    host->reply_handles[index] = 0u;
    host->area_handles[index] = 0u;
    host->area_addresses[index] = NULL;
    host->area_sizes[index] = 0u;
    host->reply_references[index] = 0u;
    host->reply_closing[index] = 0u;
}

static void
reply_finish(AstraVfsPortService *host, const AstraVfsReplyLease *lease,
             int close_session)
{
    AstraVfsReplyResources resources = {0};
    uint32_t index;

    if (lease->slot < 0 || !port_state_acquire(host))
        return;
    index = (uint32_t)lease->slot;
    if (host->reply_sessions[index] == lease->session) {
        if (host->reply_references[index] != 0u)
            --host->reply_references[index];
        if (close_session)
            host->reply_closing[index] = 1u;
        if (host->reply_closing[index] != 0u &&
            host->reply_references[index] == 0u)
            reply_detach_locked(host, index, &resources);
    }
    port_state_release(host);
    reply_resources_close(&resources);
    if (resources.session != 0u)
        astra_vfs_service_release_session(host->service, resources.session);
}

static void
reply_reap_dead(AstraVfsPortService *host)
{
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        AstraVfsReplyResources resources = {0};
        uint32_t handle;
        uint32_t session;
        uint32_t status;

        if (!port_state_acquire(host))
            return;
        session = host->reply_sessions[index];
        handle = host->reply_handles[index];
        if (host->reply_references[index] != 0u) {
            port_state_release(host);
            continue;
        }
        port_state_release(host);
        if (session == ASTRA_VFS_SESSION_INVALID)
            continue;
        status = astra_wait_one(handle, 0u, NULL);
        if (status != ASTRA_SYSCALL_PEER_DEAD &&
            status != ASTRA_SYSCALL_CLOSED)
            continue;
        if (!port_state_acquire(host))
            return;
        if (host->reply_sessions[index] == session &&
            host->reply_handles[index] == handle &&
            host->reply_references[index] == 0u)
            reply_detach_locked(host, index, &resources);
        port_state_release(host);
        reply_resources_close(&resources);
        if (resources.session != 0u)
            astra_vfs_service_release_session(host->service,
                                              resources.session);
    }
}

int
astra_vfs_port_service_init(AstraVfsPortService *host, uint32_t receive,
                            AstraVfsService *service)
{
    if (host == NULL || receive == 0u || service == NULL) {
        return 0;
    }
    host->receive = receive;
    host->service = service;
    host->requests = 0u;
    host->refused = 0u;
    host->dropped = 0u;
    host->stalled = 0u;
    host->state_acquire = NULL;
    host->state_release = NULL;
    host->state_lock_context = NULL;
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        host->reply_sessions[index] = 0u;
        host->reply_handles[index] = 0u;
        host->area_handles[index] = 0u;
        host->area_addresses[index] = NULL;
        host->area_sizes[index] = 0u;
        host->reply_references[index] = 0u;
        host->reply_closing[index] = 0u;
    }
    return 1;
}

int
astra_vfs_port_service_set_state_lock(AstraVfsPortService *host,
                                      AstraVfsStateAcquire acquire,
                                      AstraVfsStateRelease release,
                                      void *context)
{
    if (host == NULL || acquire == NULL || release == NULL ||
        host->state_acquire != NULL || host->state_release != NULL)
        return 0;
    host->state_acquire = acquire;
    host->state_release = release;
    host->state_lock_context = context;
    return 1;
}

uint32_t
astra_vfs_port_service_worker_pump(AstraVfsPortService *host,
                                   AstraVfsPortWorker *worker,
                                   uint32_t budget)
{
    AstraVfsRequestMessage *incoming;
    AstraVfsReplyMessage *outgoing;
    uint32_t answered = 0u;

    if (host == NULL || worker == NULL || host->receive == 0u ||
        host->service == NULL) {
        return 0u;
    }
    incoming = &worker->incoming;
    outgoing = &worker->outgoing;
    while (answered < budget) {
        AstraVfsReplyLease lease = {-1, 0u, 0u, NULL, 0u, 0};
        uint32_t handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t sender = 0u;
        uint32_t previous;
        uint32_t reply_handle;
        uint32_t operation;
        uint32_t wire_operation;
        int fused_hello;
        int adopted = 0;
        int sent;
        int slot = -1;
        uint32_t status = astra_port_receive_from(
            host->receive, incoming, sizeof(*incoming), handles, 1u,
            &size, &handle_count, &sender);

        if (status == ASTRA_SYSCALL_RESOURCE_LIMIT) {
            reply_reap_dead(host);
            status = astra_port_receive_from(
                host->receive, incoming, sizeof(*incoming), handles, 1u,
                &size, &handle_count, &sender);
        }

        if (status != ASTRA_SYSCALL_OK) {
            /* WOULD_BLOCK is an empty port and the ordinary way out. */
            if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                if (port_state_acquire(host)) {
                    host->stalled = status;
                    port_state_release(host);
                }
            }
            break;
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        if (size != sizeof(*incoming) ||
            incoming->header.protocol != ASTRA_VFS_PROTOCOL ||
            incoming->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            incoming->request.size != ASTRA_VFS_REQUEST_SIZE ||
            handle_count > 1u) {
            port_refuse(host);
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }

        wire_operation = incoming->header.operation;
        operation = wire_operation;
        fused_hello = wire_operation == ASTRA_VFS_OP_HELLO &&
                      incoming->request.version >= UINT16_C(8) &&
                      first_operation(incoming->request.file);
        int persistent_hello =
            wire_operation == ASTRA_VFS_OP_HELLO && !fused_hello &&
            incoming->request.version >= UINT16_C(3);
        int session_owned = wire_operation == ASTRA_VFS_OP_HELLO ||
            astra_vfs_service_session_owned(
                host->service, incoming->request.session, sender);

        if (!session_owned) {
            port_refuse(host);
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            continue;
        }

        if (wire_operation != ASTRA_VFS_OP_HELLO &&
            reply_acquire(host, incoming->request.session, &lease))
            slot = lease.slot;

        if (wire_operation == ASTRA_VFS_OP_HELLO) {
            if (handle_count != 1u) {
                port_refuse(host);
                continue;
            }
            if (astra_vfs_service_session_capacity_reached(host->service))
                reply_reap_dead(host);
            reply_handle = handles[0];
        } else if (slot >= 0) {
            uint32_t expected = operation ==
                ASTRA_VFS_OP_BIND_AREA ? 1u : 0u;

            if (handle_count != expected) {
                port_refuse(host);
                if (handle_count != 0u)
                    (void)astra_close(handles[0]);
                reply_finish(host, &lease, 0);
                continue;
            }
            reply_handle = lease.handle;
        } else {
            /* Version 2 transfers a fresh reply handle with every request. */
            if (handle_count != 1u) {
                port_refuse(host);
                continue;
            }
            reply_handle = handles[0];
        }

        clear_service_reply(&outgoing->reply, incoming->request.version,
                            incoming->request.session);
        if (slot >= 0 && !lease.active) {
            if (handle_count != 0u)
                (void)astra_close(handles[0]);
            outgoing->reply.status = ASTRA_VFS_ERR_BUSY;
            goto answer_ready;
        }

        /*
         * The service adopts the caller's activity for as long as it is
         * handling the request, so every event the service, the backend and
         * lwext4 emit underneath belongs to the story the caller began. This
         * is what astra_activity_adopt has existed unused for since the
         * activity landed: it was written for a boundary that did not exist
         * until now.
         */
        if (incoming->request.activity != 0u) {
            (void)astra_activity_exchange(incoming->request.activity,
                                          &previous);
            adopted = 1;
        }
        if (fused_hello) {
            AstraVfsRequest hello = incoming->request;

            hello.file = ASTRA_VFS_FILE_INVALID;
            astra_vfs_service_dispatch_from(
                host->service, sender, ASTRA_VFS_OP_HELLO, &hello,
                &outgoing->reply);
            if (outgoing->reply.status == ASTRA_VFS_OK) {
                slot = reply_bind(host, outgoing->reply.session, reply_handle);
                if (slot < 0) {
                    astra_vfs_service_release_session(
                        host->service, outgoing->reply.session);
                    outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                    outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                } else {
                    operation = incoming->request.file;
                    incoming->request.file = ASTRA_VFS_FILE_INVALID;
                    incoming->request.session = outgoing->reply.session;
                    if (!reply_acquire(host, outgoing->reply.session,
                                       &lease)) {
                        astra_vfs_service_release_session(
                            host->service, outgoing->reply.session);
                        outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                        outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                        slot = -1;
                    }
                }
            }
        }
        if (fused_hello && slot < 0) {
            /* HELLO already produced the refusal to return. */
        } else if (operation == ASTRA_VFS_OP_BIND_AREA && slot >= 0) {
            void *address = NULL;
            void *old_address = NULL;
            uint32_t old_handle = 0u;
            uint32_t mapped = 0u;
            uint32_t map_status;
            /*
             * A rebind replaces what was there. A client grows its transfer
             * area when it first reads something large, and refusing the
             * second bind left it able only to fail.
             */
            if (port_state_acquire(host)) {
                old_address = host->area_addresses[(uint32_t)slot];
                old_handle = host->area_handles[(uint32_t)slot];
                host->area_addresses[(uint32_t)slot] = NULL;
                host->area_handles[(uint32_t)slot] = 0u;
                host->area_sizes[(uint32_t)slot] = 0u;
                port_state_release(host);
            }
            if (old_address != NULL)
                (void)astra_rt_area_unmap(old_address);
            if (old_handle != 0u)
                (void)astra_close(old_handle);
            {
                map_status = astra_rt_area_map(
                    handles[0], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                    &address, &mapped);
                if (map_status == ASTRA_SYSCALL_OK &&
                    mapped >= incoming->request.length &&
                    incoming->request.length <= ASTRA_VFS_BULK_MAX) {
                    if (port_state_acquire(host)) {
                        host->area_handles[(uint32_t)slot] = handles[0];
                        host->area_addresses[(uint32_t)slot] = address;
                        host->area_sizes[(uint32_t)slot] =
                            incoming->request.length;
                        port_state_release(host);
                        outgoing->reply.status = ASTRA_VFS_OK;
                    } else {
                        (void)astra_rt_area_unmap(address);
                        (void)astra_close(handles[0]);
                        outgoing->reply.status = ASTRA_VFS_ERR_IO;
                    }
                } else {
                    if (address != NULL)
                        (void)astra_rt_area_unmap(address);
                    (void)astra_close(handles[0]);
                    outgoing->reply.status =
                        map_status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                            ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_INVALID;
                }
            }
        } else if (operation == ASTRA_VFS_OP_READDIR_AREA && slot >= 0) {
            uint32_t used = 0u;
            uint64_t next = incoming->request.offset;
            if (incoming->request.version < UINT16_C(10)) {
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
            } else if (lease.area == NULL ||
                       incoming->request.length == 0u) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
            } else {
                outgoing->reply.status = astra_vfs_service_readdir_into(
                    host->service,
                    (const char *)incoming->request.body.path,
                    incoming->request.offset, incoming->request.length,
                    lease.area, lease.area_size, &used, &next);
                if (outgoing->reply.status == ASTRA_VFS_OK) {
                    outgoing->reply.count = used;
                    outgoing->reply.cursor = next;
                }
            }
        } else if (operation == ASTRA_VFS_OP_READ_PATH &&
                   slot >= 0) {
            uint64_t node_size = 0u;
            uint32_t got = 0u;
            void *target = lease.area;
            uint32_t capacity = lease.area_size;

            if (target == NULL && incoming->request.version >= UINT16_C(7)) {
                target = outgoing->reply.payload;
                capacity = ASTRA_VFS_IO_MAX;
            }
            if (target == NULL) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status = astra_vfs_service_read_path(
                    host->service, (const char *)incoming->request.body.path,
                    target, capacity, &got, &node_size);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? got : 0u;
                outgoing->reply.node_size = node_size;
            }
        } else if (operation == ASTRA_VFS_OP_READ_AREA &&
                   slot >= 0) {
            AstraVfsRequest piece = incoming->request;
            uint32_t total = 0u;

            if (lease.area == NULL ||
                incoming->request.length == 0u ||
                incoming->request.length > lease.area_size) {
                astra_vfs_service_dispatch(host->service,
                                           ASTRA_VFS_OP_READ,
                                           &piece, &outgoing->reply);
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                /*
                 * Straight into the shared area, in one backend read. This
                 * loop used to drive the inline path, so a 16 KiB transfer was
                 * 86 dispatches and 32 KiB of byte-at-a-time copying; the area
                 * saved the messages and none of the work underneath them.
                 */
                outgoing->reply.status = astra_vfs_service_read_into(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    lease.area,
                    incoming->request.length, &total);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? total : 0u;
                (void)piece;
            }
        } else if (operation == ASTRA_VFS_OP_WRITE_AREA && slot >= 0) {
            uint32_t total = 0u;

            if (incoming->request.version < UINT16_C(9)) {
                outgoing->reply.status = ASTRA_VFS_ERR_UNSUPPORTED;
                outgoing->reply.count = 0u;
            } else if (lease.area == NULL ||
                       incoming->request.length == 0u ||
                       incoming->request.length > lease.area_size) {
                outgoing->reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing->reply.count = 0u;
            } else {
                outgoing->reply.status = astra_vfs_service_write_from(
                    host->service, incoming->request.session,
                    incoming->request.file, incoming->request.offset,
                    lease.area,
                    incoming->request.length, &total);
                outgoing->reply.count =
                    outgoing->reply.status == ASTRA_VFS_OK ? total : 0u;
            }
        } else {
            astra_vfs_service_dispatch_from(
                host->service, sender, operation, &incoming->request,
                &outgoing->reply);
        }
answer_ready:
        /*
         * Restored before the reply goes out, so the next thing this thread
         * does -- including serving somebody else -- is not still inside the
         * last caller's story.
         */
        if (adopted)
            (void)astra_activity_adopt(previous);

        fill_header(&outgoing->header, wire_operation,
                    (uint32_t)sizeof(*outgoing),
                    incoming->header.transaction_id);
        if (persistent_hello && outgoing->reply.status == ASTRA_VFS_OK) {
            slot = reply_bind(host, outgoing->reply.session, reply_handle);
            if (slot < 0) {
                astra_vfs_service_release_session(host->service,
                                                  outgoing->reply.session);
                outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
            } else if (!reply_acquire(host, outgoing->reply.session,
                                      &lease)) {
                astra_vfs_service_release_session(host->service,
                                                  outgoing->reply.session);
                outgoing->reply.session = ASTRA_VFS_SESSION_INVALID;
                outgoing->reply.status = ASTRA_VFS_ERR_LIMIT;
                slot = -1;
            }
        }
        sent = astra_port_send(reply_handle, outgoing, sizeof(*outgoing),
                               NULL, 0u) == ASTRA_SYSCALL_OK;
        if (sent) {
            if (port_state_acquire(host)) {
                ++host->requests;
                port_state_release(host);
            }
            ++answered;
        } else {
            /*
             * The work was done and the answer had nowhere to go: the caller
             * closed its reply port, or died holding it. Counted rather than
             * retried -- there is no second address to try.
             */
            if (port_state_acquire(host)) {
                ++host->dropped;
                port_state_release(host);
            }
        }
        if (lease.slot >= 0) {
            reply_finish(host, &lease,
                         operation == ASTRA_VFS_OP_BYE || !sent);
        } else {
            (void)astra_close(reply_handle);
        }
    }
    return answered;
}

uint32_t
astra_vfs_port_service_pump(AstraVfsPortService *host, uint32_t budget)
{
    return host == NULL ? 0u :
        astra_vfs_port_service_worker_pump(host, &host->adapter_worker,
                                           budget);
}
