#include <astra/pointer.h>

#include <astra/input.h>
#include <astra/input_service.h>
#include <astra/port.h>

#include "internal/status.h"

static int observer_live(const AstraPointerObserver *observer)
{
    return observer != 0 &&
           observer->_private_events != ASTRA_INVALID_HANDLE &&
           observer->_private_client != 0u &&
           observer->_private_generation != 0u;
}

AstraResult astra_pointer_observer_open(AstraHandle input_service,
                                        uint32_t subscriptions,
                                        AstraPointerObserver *observer)
{
    AstraInputConnect request = {0};
    AstraInputConnected reply = {0};
    AstraPort events = ASTRA_PORT_INIT;
    AstraPort replies = ASTRA_PORT_INIT;
    AstraHandle transferred[2] = { ASTRA_INVALID_HANDLE,
                                   ASTRA_INVALID_HANDLE };
    uint32_t size = 0u;
    uint32_t handles = 0u;
    AstraResult result;

    if (input_service == ASTRA_INVALID_HANDLE || observer == 0 ||
        subscriptions == 0u ||
        (subscriptions & ~ASTRA_POINTER_SUBSCRIBE_ALL) != 0u ||
        observer_live(observer) ||
        observer->_private_events != ASTRA_INVALID_HANDLE ||
        observer->_private_client != 0u ||
        observer->_private_generation != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_create(8u, 8u * sizeof(AstraInputEventMessage),
                               &events);
    if (result != ASTRA_OK)
        return result;
    result = astra_port_create(1u, sizeof(reply), &replies);
    if (result != ASTRA_OK)
        goto done;
    transferred[0] = events.send;
    transferred[1] = replies.send;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_INPUT_SERVICE_PROTOCOL,
        ASTRA_INPUT_SERVICE_VERSION, ASTRA_INPUT_OPERATION_CONNECT, 1u);
    if (result != ASTRA_OK)
        goto done;
    request.subscriptions = subscriptions;
    result = astra_port_send_until(input_service, &request, sizeof(request),
                                   transferred, 2u,
                                   ASTRA_DEADLINE_INFINITE);
    events.send = transferred[0];
    replies.send = transferred[1];
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        replies.receive, &reply, sizeof(reply), 0, 0, &size, &handles,
        ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (size != sizeof(reply) || handles != 0u ||
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
            observer->_private_events = events.receive;
            observer->_private_client = reply.client;
            observer->_private_generation = reply.generation;
            events.receive = ASTRA_INVALID_HANDLE;
        }
    } else if (reply.client != 0u || reply.generation != 0u) {
        result = ASTRA_ERROR_IO;
    }

done:
    for (uint32_t index = 0u; index < 2u; ++index)
        if (transferred[index] != ASTRA_INVALID_HANDLE) {
            AstraResult ignored = astra_handle_close(&transferred[index]);
            (void)ignored;
        }
    {
        AstraResult close_result = astra_port_close(&events);

        if (result == ASTRA_OK && close_result != ASTRA_OK &&
            close_result != ASTRA_ERROR_INVALID_HANDLE)
            result = close_result;
    }
    {
        AstraResult close_result = astra_port_close(&replies);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}

static AstraResult receive_event(AstraPointerObserver *observer,
                                 AstraPointerEvent *event,
                                 AstraMonotonicDeadline deadline_ns)
{
    AstraInputEventMessage message = {0};
    uint32_t size = 0u;
    uint32_t handles = 0u;
    AstraResult result;

    if (!observer_live(observer) || event == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_receive_until(
        observer->_private_events, &message, sizeof(message), 0, 0,
        &size, &handles, deadline_ns);
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
        message.event.type < ASTRA_INPUT_EVENT_POINTER_MOTION ||
        message.event.type > ASTRA_INPUT_EVENT_STATE_RESET ||
        message.event.type == ASTRA_INPUT_EVENT_FOCUS)
        return ASTRA_ERROR_IO;
    *event = (AstraPointerEvent){
        .size = sizeof(*event),
        .version = ASTRA_POINTER_EVENT_VERSION,
        .type = message.event.type == ASTRA_INPUT_EVENT_POINTER_MOTION ?
                    ASTRA_POINTER_EVENT_MOTION :
                message.event.type == ASTRA_INPUT_EVENT_POINTER_BUTTON &&
                message.event.code >= ASTRA_INPUT_BUTTON_WHEEL_UP &&
                message.event.code <= ASTRA_INPUT_BUTTON_WHEEL_RIGHT ?
                    ASTRA_POINTER_EVENT_WHEEL :
                message.event.type == ASTRA_INPUT_EVENT_POINTER_BUTTON ?
                    ASTRA_POINTER_EVENT_BUTTON :
                    ASTRA_POINTER_EVENT_STATE_RESET,
        .flags =
            ((message.event.flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u ?
                 ASTRA_POINTER_EVENT_DOWN : 0u) |
            ((message.event.flags & ASTRA_INPUT_LOGICAL_SYNTHETIC) != 0u ?
                 ASTRA_POINTER_EVENT_SYNTHETIC : 0u) |
            ((message.event.flags & ASTRA_INPUT_LOGICAL_LOSS) != 0u ?
                 ASTRA_POINTER_EVENT_LOSS : 0u),
        .timestamp_ms = message.event.timestamp_ms,
        .sequence = message.event.sequence,
        .generation = message.event.focus_generation,
        .screen_x = message.event.value_x,
        .screen_y = message.event.value_y,
        .button = message.event.code,
        .wheel_x = message.event.code == ASTRA_INPUT_BUTTON_WHEEL_LEFT ? -1 :
                   message.event.code == ASTRA_INPUT_BUTTON_WHEEL_RIGHT ? 1 : 0,
        .wheel_y = message.event.code == ASTRA_INPUT_BUTTON_WHEEL_UP ? 1 :
                   message.event.code == ASTRA_INPUT_BUTTON_WHEEL_DOWN ? -1 : 0,
    };
    observer->_private_generation = message.event.focus_generation != 0u ?
        message.event.focus_generation : observer->_private_generation;
    return ASTRA_OK;
}

AstraResult astra_pointer_event_try(AstraPointerObserver *observer,
                                    AstraPointerEvent *event)
{
    return receive_event(observer, event, ASTRA_DEADLINE_POLL);
}

AstraResult astra_pointer_event_wait(AstraPointerObserver *observer,
                                     AstraPointerEvent *event,
                                     AstraMonotonicDeadline deadline_ns)
{
    return receive_event(observer, event, deadline_ns);
}

AstraResult astra_pointer_observer_close(AstraPointerObserver *observer)
{
    AstraResult result;

    if (!observer_live(observer))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_handle_close(&observer->_private_events);
    if (result == ASTRA_OK) {
        observer->_private_client = 0u;
        observer->_private_generation = 0u;
    }
    return result;
}
