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

/* One synchronous thread, one in-flight VFS call, one reply channel. */
static uint32_t reply_receive;
static uint32_t reply_send;

/*
 * How long a client waits for a reply before deciding there is nobody there.
 *
 * A wait with no deadline is the hang this whole return value exists to
 * prevent: a service that took a request and will never answer is
 * indistinguishable, from here, from one that is merely slow -- and only one of
 * those is worth waiting for. Ten seconds is far beyond anything real (the
 * machine's entire boot is under a tenth of one) and far short of a person
 * deciding the machine is dead.
 */
#define VFS_PORT_REPLY_DEADLINE_NS 10000000000ull

static void
fill_header(AstraMessageHeader *header, uint32_t operation, uint32_t size,
            uint32_t transaction)
{
    header->total_size = size;
    header->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    header->flags = 0u;
    header->protocol = ASTRA_VFS_PROTOCOL;
    header->protocol_version = ASTRA_VFS_VERSION;
    header->reserved = 0u;
    header->operation = operation;
    header->transaction_id = transaction;
}

static void
port_client_reset(AstraVfsClient *client)
{
    if (client->port_reply_send != 0u)
        (void)astra_close(client->port_reply_send);
    if (client->port_area_send != 0u)
        (void)astra_close(client->port_area_send);
    if (client->port_area_address != NULL)
        (void)astra_rt_area_unmap(client->port_area_address);
    if (client->port_area != 0u)
        (void)astra_close(client->port_area);
    client->port_reply_send = 0u;
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
}

static uint32_t
reply_channel(void)
{
    if (reply_receive != 0u)
        return ASTRA_SYSCALL_OK;
    return astra_rt_port_create(VFS_PORT_REPLY_MESSAGES,
                             (uint32_t)sizeof(AstraVfsReplyMessage),
                             &reply_receive, &reply_send);
}

static uint32_t
duplicate_reply_send(uint32_t *duplicate)
{
    uint32_t status = reply_channel();

    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_rt_handle_duplicate(reply_send,
                                    ASTRA_RIGHT_SIGNAL |
                                        ASTRA_RIGHT_TRANSFER,
                                    duplicate);
    if (status != ASTRA_SYSCALL_INVALID_HANDLE)
        return status;

    /* A test reset or a closed channel: recreate once, then report honestly. */
    (void)astra_close(reply_send);
    (void)astra_close(reply_receive);
    reply_send = 0u;
    reply_receive = 0u;
    status = reply_channel();
    return status == ASTRA_SYSCALL_OK ?
        astra_rt_handle_duplicate(reply_send,
                               ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER,
                               duplicate) : status;
}

uint32_t
astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    uint32_t status;

    if (client == NULL || service == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->port_service = service;
    client->port_reply_send = 0u;
    client->port_area = 0u;
    client->port_area_send = 0u;
    client->port_area_address = NULL;
    client->port_area_size = 0u;
    status = astra_vfs_connect(client, astra_vfs_port_transport, client);
    if (status != ASTRA_VFS_OK)
        port_client_reset(client);
    return status;
}

uint32_t
astra_vfs_port_transport(void *context, uint32_t operation,
                         const AstraVfsRequest *request, AstraVfsReply *reply)
{
    /*
     * Static, not automatic. A user thread gets one 4 KiB stack and these are
     * 248 and 256 bytes; the Kit moved its own request and reply off the stack
     * for exactly this reason, and a transport that put them back would undo
     * it. The protocol is synchronous and one thread has one request in
     * flight, which is what makes one copy of each enough.
     */
    static AstraVfsRequestMessage outgoing;
    static AstraVfsReplyMessage incoming;
    AstraVfsClient *client = context;
    uint32_t handles[1] = {0u};
    uint32_t handle_count = 0u;
    uint32_t size = 0u;
    uint32_t status;

    if (client == NULL || client->port_service == 0u || request == NULL ||
        reply == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
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
    } else if (operation == ASTRA_VFS_OP_HELLO ||
               client->version < UINT16_C(3)) {
        status = duplicate_reply_send(&client->port_reply_send);
        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_BAD_HANDLE;
        handles[0] = client->port_reply_send;
        handle_count = 1u;
    }
    fill_header(&outgoing.header, operation, (uint32_t)sizeof(outgoing),
                request->activity);
    outgoing.request = *request;

    status = astra_port_send(client->port_service, &outgoing,
                             sizeof(outgoing), handles, handle_count);
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
    {
        uint64_t deadline = astra_clock_monotonic() +
                            VFS_PORT_REPLY_DEADLINE_NS;

        /*
         * The request cannot be answered until this thread lets the service
         * run. Waiting also returns immediately when a reply is already
         * queued, so probing the empty port first only adds a syscall to every
         * filesystem operation.
         */
        status = astra_wait_one(reply_receive, deadline, NULL);
        if (status == ASTRA_SYSCALL_OK) {
            status = astra_port_receive(reply_receive, &incoming,
                                        sizeof(incoming), NULL, 0u, &size,
                                        NULL);
        }
        if (status != ASTRA_SYSCALL_OK) {
            port_client_reset(client);
            return ASTRA_VFS_ERR_PEER;
        }
    }

    /*
     * A reply that is not this protocol is not an answer. It cannot be
     * forwarded to the caller as a status, because the caller would read a
     * field nobody filled in.
     */
    if (size != sizeof(incoming) ||
        incoming.header.protocol != ASTRA_VFS_PROTOCOL ||
        incoming.header.operation != operation ||
        incoming.reply.size != ASTRA_VFS_REPLY_SIZE) {
        port_client_reset(client);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *reply = incoming.reply;
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
#define VFS_PORT_AREA_MIN ASTRA_VFS_BULK_MAX

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
    uint32_t wanted = VFS_PORT_AREA_MIN;

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
    status = ensure_area_size(client, VFS_PORT_AREA_MIN);
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

static int
reply_slot(const AstraVfsPortService *host, uint32_t session)
{
    uint32_t index;

    if (session == ASTRA_VFS_SESSION_INVALID)
        return -1;
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index)
        if (host->reply_sessions[index] == session)
            return (int)index;
    return -1;
}

static int
reply_bind(AstraVfsPortService *host, uint32_t session, uint32_t handle)
{
    uint32_t index;

    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (host->reply_sessions[index] == 0u) {
            host->reply_sessions[index] = session;
            host->reply_handles[index] = handle;
            return (int)index;
        }
    }
    return -1;
}

static void
reply_release(AstraVfsPortService *host, uint32_t index)
{
    if (host->area_addresses[index] != NULL)
        (void)astra_rt_area_unmap(host->area_addresses[index]);
    if (host->area_handles[index] != 0u)
        (void)astra_close(host->area_handles[index]);
    host->area_addresses[index] = NULL;
    host->area_handles[index] = 0u;
    host->area_sizes[index] = 0u;
    (void)astra_close(host->reply_handles[index]);
    host->reply_handles[index] = 0u;
    host->reply_sessions[index] = 0u;
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
    for (uint32_t index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        host->reply_sessions[index] = 0u;
        host->reply_handles[index] = 0u;
        host->area_handles[index] = 0u;
        host->area_addresses[index] = NULL;
        host->area_sizes[index] = 0u;
    }
    return 1;
}

uint32_t
astra_vfs_port_service_pump(AstraVfsPortService *host, uint32_t budget)
{
    static AstraVfsRequestMessage incoming;
    static AstraVfsReplyMessage outgoing;
    uint32_t answered = 0u;

    if (host == NULL || host->receive == 0u || host->service == NULL) {
        return 0u;
    }
    while (answered < budget) {
        uint32_t handles[1] = {0u};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t previous;
        uint32_t reply_handle;
        int sent;
        uint32_t status = astra_port_receive(host->receive, &incoming,
                                             sizeof(incoming), handles, 1u,
                                             &size, &handle_count);

        if (status != ASTRA_SYSCALL_OK) {
            /* WOULD_BLOCK is an empty port and the ordinary way out. */
            if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                host->stalled = status;
            }
            break;
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        if (size != sizeof(incoming) ||
            incoming.header.protocol != ASTRA_VFS_PROTOCOL ||
            incoming.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            incoming.request.size != ASTRA_VFS_REQUEST_SIZE ||
            handle_count > 1u) {
            ++host->refused;
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
        }

        int slot = reply_slot(host, incoming.request.session);
        int persistent_hello =
            incoming.header.operation == ASTRA_VFS_OP_HELLO &&
            incoming.request.version >= UINT16_C(3);

        if (incoming.header.operation == ASTRA_VFS_OP_HELLO) {
            if (handle_count != 1u) {
                ++host->refused;
                continue;
            }
            reply_handle = handles[0];
        } else if (slot >= 0) {
            uint32_t expected = incoming.header.operation ==
                ASTRA_VFS_OP_BIND_AREA ? 1u : 0u;

            if (handle_count != expected) {
                ++host->refused;
                if (handle_count != 0u)
                    (void)astra_close(handles[0]);
                continue;
            }
            reply_handle = host->reply_handles[(uint32_t)slot];
        } else {
            /* Version 2 transfers a fresh reply handle with every request. */
            if (handle_count != 1u) {
                ++host->refused;
                continue;
            }
            reply_handle = handles[0];
        }

        /*
         * The service adopts the caller's activity for as long as it is
         * handling the request, so every event the service, the backend and
         * lwext4 emit underneath belongs to the story the caller began. This
         * is what astra_activity_adopt has existed unused for since the
         * activity landed: it was written for a boundary that did not exist
         * until now.
         */
        previous = astra_activity_current();
        if (incoming.request.activity != 0u)
            (void)astra_activity_adopt(incoming.request.activity);
        if (incoming.header.operation == ASTRA_VFS_OP_BIND_AREA && slot >= 0) {
            void *address = NULL;
            uint32_t mapped = 0u;
            uint32_t map_status;
            uint8_t *bytes = (uint8_t *)&outgoing.reply;

            for (uint32_t index = 0u; index < sizeof(outgoing.reply); ++index)
                bytes[index] = 0u;
            outgoing.reply.size = ASTRA_VFS_REPLY_SIZE;
            outgoing.reply.version = incoming.request.version;
            outgoing.reply.session = incoming.request.session;
            /*
             * A rebind replaces what was there. A client grows its transfer
             * area when it first reads something large, and refusing the
             * second bind left it able only to fail.
             */
            if (host->area_addresses[(uint32_t)slot] != NULL) {
                (void)astra_rt_area_unmap(host->area_addresses[(uint32_t)slot]);
                (void)astra_close(host->area_handles[(uint32_t)slot]);
                host->area_addresses[(uint32_t)slot] = NULL;
                host->area_handles[(uint32_t)slot] = 0u;
                host->area_sizes[(uint32_t)slot] = 0u;
            }
            {
                map_status = astra_rt_area_map(
                    handles[0], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                    &address, &mapped);
                if (map_status == ASTRA_SYSCALL_OK &&
                    mapped >= incoming.request.length &&
                    incoming.request.length <= ASTRA_VFS_BULK_MAX) {
                    host->area_handles[(uint32_t)slot] = handles[0];
                    host->area_addresses[(uint32_t)slot] = address;
                    host->area_sizes[(uint32_t)slot] =
                        incoming.request.length;
                    outgoing.reply.status = ASTRA_VFS_OK;
                } else {
                    if (address != NULL)
                        (void)astra_rt_area_unmap(address);
                    (void)astra_close(handles[0]);
                    outgoing.reply.status =
                        map_status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                            ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_INVALID;
                }
            }
        } else if (incoming.header.operation == ASTRA_VFS_OP_READ_PATH &&
                   slot >= 0) {
            uint64_t node_size = 0u;
            uint32_t got = 0u;

            if (host->area_addresses[(uint32_t)slot] == NULL) {
                outgoing.reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing.reply.count = 0u;
            } else {
                outgoing.reply.status = astra_vfs_service_read_path(
                    host->service, (const char *)incoming.request.body.path,
                    host->area_addresses[(uint32_t)slot],
                    host->area_sizes[(uint32_t)slot], &got, &node_size);
                outgoing.reply.count =
                    outgoing.reply.status == ASTRA_VFS_OK ? got : 0u;
                outgoing.reply.node_size = node_size;
            }
        } else if (incoming.header.operation == ASTRA_VFS_OP_READ_AREA &&
                   slot >= 0) {
            AstraVfsRequest piece = incoming.request;
            uint32_t total = 0u;

            if (host->area_addresses[(uint32_t)slot] == NULL ||
                incoming.request.length == 0u ||
                incoming.request.length > host->area_sizes[(uint32_t)slot]) {
                astra_vfs_service_dispatch(host->service,
                                           ASTRA_VFS_OP_READ,
                                           &piece, &outgoing.reply);
                outgoing.reply.status = ASTRA_VFS_ERR_INVALID;
                outgoing.reply.count = 0u;
            } else {
                /*
                 * Straight into the shared area, in one backend read. This
                 * loop used to drive the inline path, so a 16 KiB transfer was
                 * 86 dispatches and 32 KiB of byte-at-a-time copying; the area
                 * saved the messages and none of the work underneath them.
                 */
                outgoing.reply.status = astra_vfs_service_read_into(
                    host->service, incoming.request.session,
                    incoming.request.file, incoming.request.offset,
                    host->area_addresses[(uint32_t)slot],
                    incoming.request.length, &total);
                outgoing.reply.count =
                    outgoing.reply.status == ASTRA_VFS_OK ? total : 0u;
                (void)piece;
            }
        } else {
            astra_vfs_service_dispatch(host->service,
                                       incoming.header.operation,
                                       &incoming.request, &outgoing.reply);
        }
        /*
         * Restored before the reply goes out, so the next thing this thread
         * does -- including serving somebody else -- is not still inside the
         * last caller's story.
         */
        if (incoming.request.activity != 0u)
            (void)astra_activity_adopt(previous);

        fill_header(&outgoing.header, incoming.header.operation,
                    (uint32_t)sizeof(outgoing),
                    incoming.header.transaction_id);
        if (persistent_hello && outgoing.reply.status == ASTRA_VFS_OK) {
            slot = reply_bind(host, outgoing.reply.session, reply_handle);
            if (slot < 0) {
                astra_vfs_service_release_session(host->service,
                                                  outgoing.reply.session);
                outgoing.reply.session = ASTRA_VFS_SESSION_INVALID;
                outgoing.reply.status = ASTRA_VFS_ERR_LIMIT;
            }
        }
        sent = astra_port_send(reply_handle, &outgoing, sizeof(outgoing),
                               NULL, 0u) == ASTRA_SYSCALL_OK;
        if (sent) {
            ++host->requests;
            ++answered;
        } else {
            /*
             * The work was done and the answer had nowhere to go: the caller
             * closed its reply port, or died holding it. Counted rather than
             * retried -- there is no second address to try.
             */
            ++host->dropped;
        }
        if (slot >= 0 &&
            (incoming.header.operation == ASTRA_VFS_OP_BYE ||
             !sent)) {
            uint32_t session = host->reply_sessions[(uint32_t)slot];
            reply_release(host, (uint32_t)slot);
            astra_vfs_service_release_session(host->service, session);
        } else if (slot < 0) {
            (void)astra_close(reply_handle);
        }
    }
    return answered;
}
