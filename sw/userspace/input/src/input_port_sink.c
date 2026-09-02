#include <astra/input_port_sink.h>

#include <stddef.h>

AstraInputDeliveryResult astra_input_port_deliver(
    void *context, const AstraLogicalInputEvent *event)
{
    AstraInputPortSink *sink = context;
    AstraInputEventMessage message;
    AstraInputPortSendResult result;

    if (sink == NULL || sink->send == NULL || sink->send_handle == 0u ||
        event == NULL)
        return ASTRA_INPUT_DELIVERY_DEAD;
    astra_message_header_set(&message.header, sizeof(message),
                             ASTRA_INPUT_SERVICE_PROTOCOL,
                             ASTRA_INPUT_SERVICE_VERSION,
                             ASTRA_INPUT_OPERATION_EVENT, event->sequence);
    message.event = *event;
    result = sink->send(sink->context, sink->send_handle, &message,
                        sizeof(message));
    while (result == ASTRA_INPUT_PORT_SEND_FULL && sink->wait != NULL &&
           event->type != ASTRA_INPUT_EVENT_POINTER_MOTION) {
        result = sink->wait(sink->context, sink->send_handle);
        if (result != ASTRA_INPUT_PORT_SEND_OK)
            break;
        result = sink->send(sink->context, sink->send_handle, &message,
                            sizeof(message));
    }
    if (result == ASTRA_INPUT_PORT_SEND_OK)
        return ASTRA_INPUT_DELIVERY_OK;
    if (result == ASTRA_INPUT_PORT_SEND_FULL)
        return ASTRA_INPUT_DELIVERY_FULL;
    return ASTRA_INPUT_DELIVERY_DEAD;
}
