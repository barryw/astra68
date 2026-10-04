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

ASTRA_PROGRAM("port_message", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");

/* Send one ping through a port this program owns, and receive it back. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    AstraPort port = ASTRA_PORT_INIT;
    AstraMonotonicDeadline deadline;
    ExamplePing received;
    uint32_t bytes = 0u;
    uint32_t handles = 0u;
    AstraResult result;

    if (output == 0 || astra_port_create(4u, 4u * sizeof(ExamplePing),
                                         &port) != ASTRA_OK)
        return 1;
    deadline = (AstraMonotonicDeadline)(astra_clock_monotonic() +
                                        UINT64_C(1000000000));
    result = send_example_ping(port.send, 7u, 42u, deadline);
    if (result == ASTRA_OK)
        result = astra_port_receive_until(port.receive, &received,
                                          sizeof(received), 0, 0, &bytes,
                                          &handles, deadline);
    if (astra_port_close(&port) != ASTRA_OK || result != ASTRA_OK ||
        bytes != sizeof(received) || handles != 0u ||
        received.header.protocol != EXAMPLE_PROTOCOL ||
        received.header.operation != EXAMPLE_OPERATION_PING)
        return 2;
    (void)astra_print(output->handle, "port_message: ping ");
    (void)astra_print_u32(output->handle, received.value);
    (void)astra_print(output->handle, " received as transaction ");
    (void)astra_print_u32(output->handle, received.header.transaction_id);
    (void)astra_print(output->handle, "\n");
    return 0;
}
