#include <astra/runtime.h>

#include <string.h>

uint32_t astra_shutdown_request(uint32_t lifecycle_send,
                                uint32_t transaction,
                                uint32_t *reply_receive)
{
    AstraShutdownRequest request = {0};
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t status;

    if (reply_receive == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *reply_receive = 0u;
    if (lifecycle_send == 0u || transaction == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_rt_port_create(1u, sizeof(AstraShutdownReply),
                                  &receive, &send);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_SHUTDOWN_PROTOCOL,
                             ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REQUEST, transaction);
    status = astra_port_send(lifecycle_send, &request, sizeof(request),
                             &send, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(send);
        (void)astra_close(receive);
        return status;
    }
    (void)astra_close(send);
    *reply_receive = receive;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_shutdown_receive(uint32_t lifecycle_receive,
                                AstraShutdownRequest *request,
                                uint32_t *reply_send)
{
    uint32_t carried = 0u;
    uint32_t size = 0u;
    uint32_t handles = 0u;
    uint32_t status;

    if (request == NULL || reply_send == NULL || lifecycle_receive == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *reply_send = 0u;
    memset(request, 0, sizeof(*request));
    status = astra_port_receive(lifecycle_receive, request, sizeof(*request),
                                &carried, 1u, &size, &handles);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (size != sizeof(*request) || carried == 0u ||
        !astra_shutdown_request_valid(request,
                                      request->header.transaction_id,
                                      handles)) {
        if (carried != 0u)
            (void)astra_close(carried);
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *reply_send = carried;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_shutdown_respond(uint32_t reply_send, uint32_t transaction,
                                uint32_t decision, uint32_t reason)
{
    AstraShutdownReply reply = {0};
    uint32_t status;

    if (reply_send == 0u || transaction == 0u ||
        (decision != ASTRA_SHUTDOWN_CANCEL &&
         (decision != ASTRA_SHUTDOWN_READY || reason != 0u)))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_SHUTDOWN_PROTOCOL,
                             ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REPLY, transaction);
    reply.decision = decision;
    reply.reason = reason;
    status = astra_port_send(reply_send, &reply, sizeof(reply), NULL, 0u);
    (void)astra_close(reply_send);
    return status;
}

uint32_t astra_shutdown_receive_reply(uint32_t reply_receive,
                                      uint32_t transaction,
                                      AstraShutdownReply *reply)
{
    uint32_t size = 0u;
    uint32_t handles = 0u;
    uint32_t status;

    if (reply_receive == 0u || transaction == 0u || reply == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    memset(reply, 0, sizeof(*reply));
    status = astra_port_receive(reply_receive, reply, sizeof(*reply),
                                NULL, 0u, &size, &handles);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    return size == sizeof(*reply) &&
        astra_shutdown_reply_valid(reply, transaction, handles) ?
        ASTRA_SYSCALL_OK : ASTRA_SYSCALL_INVALID_ARGUMENT;
}
