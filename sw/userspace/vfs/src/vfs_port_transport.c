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

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>

/*
 * One reply at a time, one message deep. The protocol is synchronous -- a
 * client has at most one request outstanding, which is why the Kit keeps one
 * request and one reply record -- so a deeper reply port would be capacity
 * nothing can use.
 */
#define VFS_PORT_REPLY_MESSAGES 1u

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
    const uint32_t *service = context;
    uint32_t handles[1];
    uint32_t receive = 0u;
    uint32_t size = 0u;
    uint32_t status;

    if (service == NULL || *service == 0u || request == NULL ||
        reply == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /*
     * A reply port per request, because attaching a handle to a message moves
     * it: a cached one names nothing after its first use. It is also what
     * makes the reply channel a capability the service holds for exactly one
     * answer and not a moment longer.
     */
    status = astra_port_create(VFS_PORT_REPLY_MESSAGES,
                               (uint32_t)sizeof(incoming), &receive,
                               &handles[0]);
    if (status != ASTRA_SYSCALL_OK) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    fill_header(&outgoing.header, operation, (uint32_t)sizeof(outgoing),
                request->activity);
    outgoing.request = *request;

    status = astra_port_send(*service, &outgoing, sizeof(outgoing), handles,
                             1u);
    if (status != ASTRA_SYSCALL_OK) {
        /* The send failed, so the handle did not go: both ends are still ours. */
        (void)astra_close(handles[0]);
        (void)astra_close(receive);
        /*
         * A full port is back pressure everywhere else on this machine and is
         * not here: the protocol is synchronous, so a caller with one request
         * outstanding cannot have filled anything, and a port that will not
         * take this one is a service that is not running.
         */
        return ASTRA_VFS_ERR_PEER;
    }
    for (;;) {
        status = astra_port_receive(receive, &incoming, sizeof(incoming),
                                    NULL, 0u, &size, NULL);
        if (status == ASTRA_SYSCALL_OK) {
            break;
        }
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            (void)astra_close(receive);
            /*
             * The service took the request and will not answer. Every way that
             * happens is the same fact to a caller: there is nobody there.
             */
            return ASTRA_VFS_ERR_PEER;
        }
        /*
         * Blocked on the reply port rather than spinning at it. A yield loop
         * would burn a scheduling slot for the whole of a disk read.
         */
        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            return ASTRA_VFS_ERR_PEER;
        }
    }
    (void)astra_close(receive);

    /*
     * A reply that is not this protocol is not an answer. It cannot be
     * forwarded to the caller as a status, because the caller would read a
     * field nobody filled in.
     */
    if (size != sizeof(incoming) ||
        incoming.header.protocol != ASTRA_VFS_PROTOCOL ||
        incoming.header.operation != operation ||
        incoming.reply.size != ASTRA_VFS_REPLY_SIZE) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *reply = incoming.reply;
    return ASTRA_VFS_OK;
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
        uint32_t status = astra_port_receive(host->receive, &incoming,
                                             sizeof(incoming), handles, 1u,
                                             &size, &handle_count);

        if (status != ASTRA_SYSCALL_OK) {
            break;   /* WOULD_BLOCK is an empty port and the ordinary way out */
        }
        /*
         * No reply handle, no reply. A request that did not say where the
         * answer goes cannot be answered, and there is nothing to close.
         */
        if (size != sizeof(incoming) ||
            incoming.header.protocol != ASTRA_VFS_PROTOCOL ||
            incoming.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
            incoming.request.size != ASTRA_VFS_REQUEST_SIZE ||
            handle_count != 1u) {
            ++host->refused;
            if (handle_count == 1u) {
                (void)astra_close(handles[0]);
            }
            continue;
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
        (void)astra_activity_adopt(incoming.request.activity);
        astra_vfs_service_dispatch(host->service, incoming.header.operation,
                                   &incoming.request, &outgoing.reply);
        /*
         * Restored before the reply goes out, so the next thing this thread
         * does -- including serving somebody else -- is not still inside the
         * last caller's story.
         */
        (void)astra_activity_adopt(previous);

        fill_header(&outgoing.header, incoming.header.operation,
                    (uint32_t)sizeof(outgoing),
                    incoming.header.transaction_id);
        if (astra_port_send(handles[0], &outgoing, sizeof(outgoing), NULL,
                            0u) == ASTRA_SYSCALL_OK) {
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
        (void)astra_close(handles[0]);
    }
    return answered;
}
