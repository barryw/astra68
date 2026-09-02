#ifndef ASTRA_INPUT_PORT_SINK_H
#define ASTRA_INPUT_PORT_SINK_H

#include <astra/input_service_core.h>

#include <stdint.h>

typedef enum AstraInputPortSendResult {
    ASTRA_INPUT_PORT_SEND_OK = 0,
    ASTRA_INPUT_PORT_SEND_FULL,
    ASTRA_INPUT_PORT_SEND_PEER_DEAD,
    ASTRA_INPUT_PORT_SEND_ERROR
} AstraInputPortSendResult;

typedef AstraInputPortSendResult (*AstraInputPortSend)(
    void *context, uint32_t send_handle, const void *message,
    uint32_t message_size);
typedef AstraInputPortSendResult (*AstraInputPortWait)(
    void *context, uint32_t send_handle);

typedef struct AstraInputPortSink {
    AstraInputPortSend send;
    AstraInputPortWait wait;
    void *context;
    uint32_t send_handle;
} AstraInputPortSink;

AstraInputDeliveryResult astra_input_port_deliver(
    void *context, const AstraLogicalInputEvent *event);

#endif
