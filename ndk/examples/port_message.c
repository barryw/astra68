#include <astra/ndk.h>

enum {
    EXAMPLE_PROTOCOL = 0x4558414du,
    EXAMPLE_PROTOCOL_VERSION = 1u,
    EXAMPLE_OPERATION_PING = 1u
};

typedef struct ExamplePing {
    AstraMessageHeader header;
    uint32_t value;
} ExamplePing;

AstraResult send_example_ping(AstraHandle endpoint,
                              uint32_t transaction_id,
                              uint32_t value,
                              AstraMonotonicDeadline deadline)
{
    ExamplePing message;
    AstraResult result = astra_message_header_init(
        &message.header, sizeof(message), EXAMPLE_PROTOCOL,
        EXAMPLE_PROTOCOL_VERSION, EXAMPLE_OPERATION_PING,
        transaction_id);

    if (result != ASTRA_OK)
        return result;
    message.value = value;
    return astra_port_send_until(endpoint, &message, sizeof(message),
                                 0, 0, deadline);
}
