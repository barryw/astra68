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
    message.header.total_size = sizeof(message);
    message.header.header_size = sizeof(message.header);
    message.header.flags = 0u;
    message.header.protocol = ASTRA_INPUT_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_INPUT_SERVICE_VERSION;
    message.header.reserved = 0u;
    message.header.operation = ASTRA_INPUT_OPERATION_EVENT;
    message.header.transaction_id = event->sequence;
    message.event = *event;
    result = sink->send(sink->context, sink->send_handle, &message,
                        sizeof(message));
    if (result == ASTRA_INPUT_PORT_SEND_OK)
        return ASTRA_INPUT_DELIVERY_OK;
    if (result == ASTRA_INPUT_PORT_SEND_FULL)
        return ASTRA_INPUT_DELIVERY_FULL;
    return ASTRA_INPUT_DELIVERY_DEAD;
}
