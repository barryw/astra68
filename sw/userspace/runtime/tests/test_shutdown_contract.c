#include <astra/runtime.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t queued[sizeof(AstraShutdownReply)];
static uint32_t queued_size;
static uint32_t queued_handle;
static uint32_t queued_handles;
static uint32_t sent_size;
static uint32_t sent_handle;
static uint32_t sent_handles;
static uint32_t send_status;
static uint32_t closed[5];
static uint32_t close_count;

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    assert(messages == 1u && bytes == sizeof(AstraShutdownReply));
    *receive = 10u;
    *send = 11u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t endpoint, const void *message,
                         uint32_t size, const uint32_t *handles,
                         uint32_t handle_count)
{
    (void)endpoint;
    assert(message != NULL && size <= sizeof(queued));
    sent_size = size;
    sent_handles = handle_count;
    sent_handle = handle_count != 0u ? handles[0] : 0u;
    return send_status;
}

uint32_t astra_port_receive(uint32_t endpoint, void *message,
                            uint32_t capacity, uint32_t *handles,
                            uint32_t handle_capacity, uint32_t *size,
                            uint32_t *handle_count)
{
    (void)endpoint;
    if (queued_size > capacity || queued_handles > handle_capacity)
        return ASTRA_SYSCALL_BUFFER_TOO_SMALL;
    memcpy(message, queued, queued_size);
    if (handles != NULL && queued_handles != 0u)
        handles[0] = queued_handle;
    *size = queued_size;
    *handle_count = queued_handles;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    assert(close_count < 5u);
    closed[close_count++] = handle;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraShutdownRequest request = {0};
    AstraShutdownReply reply = {0};

    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_SHUTDOWN_PROTOCOL, ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REQUEST, 42u);
    assert(astra_shutdown_request_valid(&request, 42u, 1u));
    assert(!astra_shutdown_request_valid(&request, 43u, 1u));
    assert(!astra_shutdown_request_valid(&request, 42u, 0u));
    request.header.flags = 1u;
    assert(!astra_shutdown_request_valid(&request, 42u, 1u));

    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_SHUTDOWN_PROTOCOL, ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REPLY, 42u);
    reply.decision = ASTRA_SHUTDOWN_READY;
    assert(astra_shutdown_reply_valid(&reply, 42u, 0u));
    reply.reason = 11u;
    assert(!astra_shutdown_reply_valid(&reply, 42u, 0u));
    reply.decision = ASTRA_SHUTDOWN_CANCEL;
    assert(astra_shutdown_reply_valid(&reply, 42u, 0u));
    assert(!astra_shutdown_reply_valid(&reply, 42u, 1u));
    assert(!astra_shutdown_reply_valid(&reply, 43u, 0u));
    reply.decision = 0u;
    assert(!astra_shutdown_reply_valid(&reply, 42u, 0u));

    close_count = 0u;
    send_status = ASTRA_SYSCALL_OK;
    {
        uint32_t reply_receive = 0u;

        assert(astra_shutdown_request(20u, 42u, &reply_receive) ==
               ASTRA_SYSCALL_OK);
        assert(reply_receive == 10u && sent_size == sizeof(request));
        assert(sent_handles == 1u && sent_handle == 11u);
        assert(close_count == 1u && closed[0] == 11u);
        send_status = ASTRA_SYSCALL_WOULD_BLOCK;
        assert(astra_shutdown_request(20u, 42u, &reply_receive) ==
               ASTRA_SYSCALL_WOULD_BLOCK);
        assert(reply_receive == 0u && close_count == 3u);
        assert(closed[1] == 11u && closed[2] == 10u);
        send_status = ASTRA_SYSCALL_OK;
    }

    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_SHUTDOWN_PROTOCOL, ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REQUEST, 42u);
    memcpy(queued, &request, sizeof(request));
    queued_size = sizeof(request);
    queued_handle = 31u;
    queued_handles = 1u;
    {
        uint32_t reply_send = 0u;

        assert(astra_shutdown_receive(30u, &request, &reply_send) ==
               ASTRA_SYSCALL_OK);
        assert(reply_send == 31u && request.header.transaction_id == 42u);
        request.header.flags = 1u;
        memcpy(queued, &request, sizeof(request));
        assert(astra_shutdown_receive(30u, &request, &reply_send) ==
               ASTRA_SYSCALL_INVALID_ARGUMENT);
        assert(reply_send == 0u && closed[3] == 31u);
    }

    assert(astra_shutdown_respond(40u, 42u, ASTRA_SHUTDOWN_READY, 0u) ==
           ASTRA_SYSCALL_OK);
    assert(sent_size == sizeof(reply) && sent_handles == 0u);
    assert(closed[4] == 40u);
    assert(astra_shutdown_respond(40u, 42u, ASTRA_SHUTDOWN_READY, 11u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);

    reply.decision = ASTRA_SHUTDOWN_CANCEL;
    reply.reason = 0u;
    reply.header.transaction_id = 41u;
    memcpy(queued, &reply, sizeof(reply));
    queued_size = sizeof(reply);
    queued_handles = 0u;
    assert(astra_shutdown_receive_reply(41u, 42u, &reply) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_SHUTDOWN_PROTOCOL, ASTRA_SHUTDOWN_VERSION,
                             ASTRA_SHUTDOWN_REPLY, 42u);
    reply.decision = ASTRA_SHUTDOWN_CANCEL;
    memcpy(queued, &reply, sizeof(reply));
    assert(astra_shutdown_receive_reply(41u, 42u, &reply) ==
           ASTRA_SYSCALL_OK);
    assert(reply.decision == ASTRA_SHUTDOWN_CANCEL);
    puts("shutdown contract: PASS");
    return 0;
}
