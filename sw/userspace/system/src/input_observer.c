#include "internal/input_observer.h"
#include "internal/status.h"

#include <astra/port.h>

AstraResult astra_internal_input_observer_open(
    AstraHandle service, uint32_t subscriptions, uint32_t queue_messages,
    AstraHandle *events, uint32_t *client, uint32_t *generation)
{
    AstraInputConnect request = {0};
    AstraInputConnected reply = {0};
    AstraPort port = ASTRA_PORT_INIT;
    AstraCall call = {0};
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE || subscriptions == 0u ||
        (subscriptions & ~ASTRA_INPUT_SUBSCRIBE_ALL) != 0u ||
        queue_messages == 0u || events == 0 || client == 0 ||
        generation == 0 || *events != ASTRA_INVALID_HANDLE ||
        *client != 0u || *generation != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_create(queue_messages,
        queue_messages * sizeof(AstraInputEventMessage), &port);
    if (result != ASTRA_OK)
        return result;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_INPUT_SERVICE_PROTOCOL,
        ASTRA_INPUT_SERVICE_VERSION, ASTRA_INPUT_OPERATION_CONNECT, 1u);
    if (result != ASTRA_OK)
        goto done;
    request.subscriptions = subscriptions;
    call.request = &request;
    call.request_size = sizeof(request);
    call.handles = &port.send;
    call.handle_count = 1u;
    call.reply_index = 1u;
    call.reply = &reply;
    call.reply_capacity = sizeof(reply);
    result = astra_port_call(service, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (call.reply_size != sizeof(reply) || call.reply_handle_count != 0u ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u ||
        reply.header.protocol != ASTRA_INPUT_SERVICE_PROTOCOL ||
        reply.header.protocol_version != ASTRA_INPUT_SERVICE_VERSION ||
        reply.header.reserved != 0u ||
        reply.header.operation != ASTRA_INPUT_OPERATION_CONNECTED ||
        reply.header.transaction_id != request.header.transaction_id) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_internal_service_result(reply.status);
    if (result == ASTRA_OK) {
        if (reply.client == 0u || reply.generation == 0u) {
            result = ASTRA_ERROR_IO;
        } else {
            *events = port.receive;
            *client = reply.client;
            *generation = reply.generation;
            port.receive = ASTRA_INVALID_HANDLE;
        }
    } else if (reply.client != 0u || reply.generation != 0u) {
        result = ASTRA_ERROR_IO;
    }

done:
    {
        AstraResult close_result = astra_port_close(&port);

        if (result == ASTRA_OK && close_result != ASTRA_OK &&
            close_result != ASTRA_ERROR_INVALID_HANDLE)
            result = close_result;
    }
    return result;
}

AstraResult astra_internal_input_observer_receive(
    AstraHandle events, AstraLogicalInputEvent *event,
    AstraMonotonicDeadline deadline_ns)
{
    AstraInputEventMessage message = {0};
    uint32_t size = 0u;
    uint32_t handles = 0u;
    AstraResult result;

    if (events == ASTRA_INVALID_HANDLE || event == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_receive_until(events, &message, sizeof(message),
                                      0, 0, &size, &handles, deadline_ns);
    if (result != ASTRA_OK)
        return result;
    if (size != sizeof(message) || handles != 0u ||
        message.header.total_size != sizeof(message) ||
        message.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        message.header.flags != 0u ||
        message.header.protocol != ASTRA_INPUT_SERVICE_PROTOCOL ||
        message.header.protocol_version != ASTRA_INPUT_SERVICE_VERSION ||
        message.header.reserved != 0u ||
        message.header.operation != ASTRA_INPUT_OPERATION_EVENT ||
        message.event.size != sizeof(message.event) ||
        message.event.version != ASTRA_INPUT_SERVICE_VERSION ||
        (message.event.modifiers & ~ASTRA_INPUT_MOD_ALL) != 0u)
        return ASTRA_ERROR_IO;
    *event = message.event;
    return ASTRA_OK;
}

AstraResult astra_internal_input_observer_close(
    AstraHandle *events, uint32_t *client, uint32_t *generation)
{
    AstraResult result;

    if (events == 0 || client == 0 || generation == 0 ||
        *events == ASTRA_INVALID_HANDLE || *client == 0u ||
        *generation == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_handle_close(events);
    if (result == ASTRA_OK) {
        *client = 0u;
        *generation = 0u;
    }
    return result;
}
