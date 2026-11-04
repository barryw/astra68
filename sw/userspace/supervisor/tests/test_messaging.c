#include <messaging.h>
#include <astra/messaging_abi.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstraMessagingRequest incoming;
static AstraMessagingReply reply;
static AstraMessagingEvent event;
static uint32_t incoming_handles[3], incoming_count, sender;
static uint32_t event_count, event_reply, lookup_handle, closed[512];
static uint32_t blocked_send, next_handle = 300u;
static uint32_t command_live = 1u;
static SupervisorProcessRecord records[] = {
    {.id = 10u, .service_name = "hostfs"},
    {.id = 20u, .bundle = "/apps/Atlas.app"}
};
static SupervisorProcessTable processes = {
    .records = records, .count = 2u, .capacity = 2u
};

uint32_t supervisor_loader_process_handle(void) { return 1u; }
void *astra_runtime_allocate(size_t bytes) { return malloc(bytes); }
void *astra_runtime_callocate(size_t count, size_t bytes) { return calloc(count, bytes); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
    uint32_t *receive, uint32_t *send)
{
    assert(messages == 8u && bytes == 8u * sizeof(incoming));
    *receive = 100u; *send = 101u; return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_receive_from(uint32_t receive, void *message,
    uint32_t capacity, uint32_t *handles, uint32_t handle_capacity,
    uint32_t *size, uint32_t *count, uint32_t *owner)
{
    assert(receive == 100u && capacity == sizeof(incoming) &&
        handle_capacity == 3u);
    memcpy(message, &incoming, sizeof(incoming));
    memcpy(handles, incoming_handles, incoming_count * sizeof(*handles));
    *size = sizeof(incoming); *count = incoming_count; *owner = sender;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
    const uint32_t *handles, uint32_t count)
{
    if (handle == blocked_send) return ASTRA_SYSCALL_WOULD_BLOCK;
    if (handle == 50u) {
        assert(size == sizeof(reply) && count == 0u);
        memcpy(&reply, message, size);
    } else {
        assert(handle == 201u && size == sizeof(event) && count <= 1u);
        memcpy(&event, message, size);
        event_reply = count == 1u ? handles[0] : 0u;
        ++event_count;
    }
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_send_rights(uint32_t handle, const void *message,
    uint32_t size, const AstraHandleDisposition *handles, uint32_t count)
{
    assert(handle == 50u && size == sizeof(reply) && count == 1u);
    assert(handles[0].rights == (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT));
    lookup_handle = handles[0].handle;
    memcpy(&reply, message, size);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
    uint32_t *duplicate)
{
    assert(((handle == 200u || handle == 202u) && rights ==
        (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER)) ||
        (handle == 204u && rights ==
        (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER)));
    *duplicate = ++next_handle;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle)
{
    if (handle < sizeof(closed) / sizeof(closed[0])) ++closed[handle];
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    assert((handle == 200u || handle == 202u) && deadline == 0u &&
        detail == NULL);
    return ASTRA_SYSCALL_TIMED_OUT;
}
uint32_t astra_process_snapshot(uint32_t observer, AstraProcSnapshot *records,
    uint32_t capacity, uint32_t *count)
{
    assert(observer == 1u);
    *count = 2u + command_live;
    if (capacity < *count) return ASTRA_SYSCALL_BUFFER_TOO_SMALL;
    records[0].process.id = 10u;
    records[1].process.id = 20u;
    if (command_live != 0u) {
        records[2].process.id = 30u;
        memcpy(records[2].name, "volumes", 8u);
    }
    return ASTRA_SYSCALL_OK;
}

static void request(uint32_t owner, uint32_t operation, uint32_t extra)
{
    sender = owner;
    incoming = (AstraMessagingRequest){0};
    astra_message_header_set(&incoming.header, sizeof(incoming),
        ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION, operation, 17u);
    incoming_handles[0] = 50u;
    incoming_handles[1] = extra;
    incoming_count = extra != 0u ? 2u : 1u;
    reply = (AstraMessagingReply){0};
}
static void pump(void) { supervisor_messaging_pump(100u, &processes); }

int main(void)
{
    uint32_t receive = 0u, send = 0u;
    assert(supervisor_messaging_start(&receive, &send) == ASTRA_STATUS_OK);
    assert(receive == 100u && send == 101u);
    request(10u, ASTRA_MESSAGING_IDENTIFY, 0u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK &&
        reply.kind == ASTRA_MESSAGING_SERVICE &&
        strcmp(reply.name, "hostfs") == 0);
    request(30u, ASTRA_MESSAGING_IDENTIFY, 0u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK &&
        reply.kind == ASTRA_MESSAGING_COMMAND && reply.process_id == 30u &&
        strcmp(reply.name, "volumes") == 0);
    request(10u, ASTRA_MESSAGING_REGISTER, 200u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK && closed[200u] == 0u);
    request(30u, ASTRA_MESSAGING_REGISTER, 202u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK);
    request(20u, ASTRA_MESSAGING_LIST, 0u);
    pump();
    assert(reply.process_id == 10u && reply.has_endpoint == 1u);
    incoming.cursor = 20u;
    pump();
    assert(reply.process_id == 30u && reply.kind == ASTRA_MESSAGING_COMMAND &&
        strcmp(reply.name, "volumes") == 0);
    request(20u, ASTRA_MESSAGING_LOOKUP, 0u);
    incoming.kind = ASTRA_MESSAGING_COMMAND;
    memcpy(incoming.name, "volumes", 8u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK && reply.process_id == 30u &&
        strcmp(reply.name, "volumes") == 0);
    request(20u, ASTRA_MESSAGING_LOOKUP, 0u);
    incoming.kind = ASTRA_MESSAGING_SERVICE;
    memcpy(incoming.name, "hostfs", 7u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK && reply.process_id == 10u &&
        lookup_handle != 0u);
    request(20u, ASTRA_MESSAGING_SUBSCRIBE, 201u);
    incoming.kind = ASTRA_MESSAGING_SERVICE;
    memcpy(incoming.name, "hostfs", 7u);
    memcpy(incoming.topic, "volumes", 8u);
    pump();
    uint64_t subscription = reply.subscription;
    assert(reply.status == ASTRA_STATUS_OK && subscription != 0u);
    request(10u, ASTRA_MESSAGING_PUBLISH, 204u);
    memcpy(incoming.topic, "volumes", 8u);
    incoming.payload_size = 3u;
    memcpy(incoming.payload, "new", 3u);
    pump();
    assert(reply.status == ASTRA_STATUS_OK && reply.delivered == 1u &&
        event_count == 1u && event.sequence == 1u && event_reply != 0u &&
        event.process_id == 10u && event.subscription == subscription);
    blocked_send = 201u;
    pump();
    assert(reply.delivered == 0u && reply.dropped == 1u);
    blocked_send = 0u;
    pump();
    assert(reply.delivered == 1u && event.sequence == 3u);
    request(30u, ASTRA_MESSAGING_UNSUBSCRIBE, 0u);
    incoming.subscription = subscription;
    pump();
    assert(reply.status == ASTRA_STATUS_ACCESS);
    supervisor_messaging_remove(20u);
    assert(closed[201u] == 1u);
    supervisor_messaging_remove(10u);
    command_live = 0u;
    request(20u, ASTRA_MESSAGING_LIST, 0u);
    pump();
    assert(closed[200u] == 1u && closed[202u] == 1u && closed[204u] == 3u);
    puts("Messaging directory and fanout: PASS");
    return 0;
}
