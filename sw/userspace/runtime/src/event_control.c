#include <astra/event.h>
#include <astra/event_control.h>
#include <astra/event_emit.h>
#include <astra/runtime.h>
#include <astra/status.h>

#define EVENT_CONTROL_DEADLINE_NS 10000000000ull

static void
fill_header(AstraMessageHeader *header, uint32_t size, uint32_t transaction)
{
    header->total_size = size;
    header->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    header->flags = 0u;
    header->protocol = ASTRA_EVENT_CONTROL_PROTOCOL;
    header->protocol_version = ASTRA_EVENT_CONTROL_VERSION;
    header->reserved = 0u;
    header->operation = ASTRA_EVENT_CONTROL_SET;
    header->transaction_id = transaction;
}

uint32_t
astra_event_control_set(uint32_t handle, uint32_t subsystem, uint32_t level)
{
    static AstraEventControlRequest request;
    static AstraEventControlReply reply;
    uint32_t carried;
    uint32_t receive = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint64_t deadline;

    if (handle == 0u || subsystem >= ASTRA_EVENT_SUBSYSTEM_MAX ||
        level > ASTRA_EVENT_LEVEL_ERROR)
        return ASTRA_STATUS_INVALID;
    status = astra_port_create(1u, sizeof(reply), &receive, &carried);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_LIMIT;
    fill_header(&request.header, sizeof(request), astra_activity_current());
    request.subsystem = subsystem;
    request.level = level;
    status = astra_port_send(handle, &request, sizeof(request), &carried, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(carried);
        (void)astra_close(receive);
        return status == ASTRA_SYSCALL_WOULD_BLOCK ? ASTRA_STATUS_BUSY :
               status == ASTRA_SYSCALL_INVALID_HANDLE ?
                   ASTRA_STATUS_BAD_HANDLE : ASTRA_STATUS_PEER_DEAD;
    }
    deadline = astra_clock_monotonic() + EVENT_CONTROL_DEADLINE_NS;
    for (;;) {
        status = astra_port_receive(receive, &reply, sizeof(reply), NULL, 0u,
                                    &size, NULL);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK ||
            astra_wait_one(receive, deadline, NULL) != ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            return ASTRA_STATUS_PEER_DEAD;
        }
    }
    (void)astra_close(receive);
    if (size != sizeof(reply) || reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.protocol != ASTRA_EVENT_CONTROL_PROTOCOL ||
        reply.header.protocol_version != ASTRA_EVENT_CONTROL_VERSION ||
        reply.header.operation != ASTRA_EVENT_CONTROL_SET)
        return ASTRA_STATUS_PROTOCOL;
    return reply.status;
}

static uint32_t
pump(uint32_t receive, uint32_t target, uint32_t budget)
{
    static AstraEventControlRequest request;
    static AstraEventControlReply reply;
    uint32_t processed = 0u;

    if (receive == 0u)
        return 0u;
    while (processed < budget) {
        uint32_t carried = 0u;
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t transaction = 0u;
        uint32_t status = astra_port_receive(
            receive, &request, sizeof(request), &carried, 1u, &size,
            &handle_count);

        if (status != ASTRA_SYSCALL_OK)
            break;
        status = ASTRA_STATUS_PROTOCOL;
        if (size == sizeof(request) &&
            request.header.total_size == sizeof(request) &&
            request.header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
            request.header.protocol == ASTRA_EVENT_CONTROL_PROTOCOL &&
            request.header.protocol_version == ASTRA_EVENT_CONTROL_VERSION &&
            request.header.operation == ASTRA_EVENT_CONTROL_SET &&
            handle_count == 1u) {
            transaction = request.header.transaction_id;
            status = target != 0u ?
                astra_event_control_set(target, request.subsystem,
                                        request.level) :
                astra_event_level_set(request.subsystem, request.level);
        }
        if (handle_count == 1u) {
            fill_header(&reply.header, sizeof(reply), transaction);
            reply.status = status;
            (void)astra_port_send(carried, &reply, sizeof(reply), NULL, 0u);
            (void)astra_close(carried);
        }
        ++processed;
    }
    return processed;
}

uint32_t
astra_event_control_pump(uint32_t receive, uint32_t budget)
{
    return pump(receive, 0u, budget);
}

uint32_t
astra_event_control_proxy_pump(uint32_t receive, uint32_t target,
                               uint32_t budget)
{
    return target == 0u ? 0u : pump(receive, target, budget);
}
