#include <astra/display.h>
#include <astra/input.h>
#include <astra/input_port_sink.h>
#include <astra/input_service.h>
#include <astra/input_service_core.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

ASTRA_PROGRAM("input", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup, const char *name)
{
    const AstraStartupCapability *entries =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;

    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(entries[index].name, name))
            return &entries[index];
    return NULL;
}

static uint32_t ready(uint32_t bootstrap, uint32_t status, uint32_t service)
{
    AstraServiceReady message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    return astra_port_send(bootstrap, &message, sizeof(message),
                           status == ASTRA_STATUS_OK ? &service : NULL,
                           status == ASTRA_STATUS_OK ? 1u : 0u);
}

static AstraInputPortSendResult send_event(void *context,
                                           uint32_t send_handle,
                                           const void *message,
                                           uint32_t message_size)
{
    uint32_t status;

    (void)context;
    status = astra_port_send(send_handle, message, message_size, NULL, 0u);
    if (status == ASTRA_SYSCALL_OK)
        return ASTRA_INPUT_PORT_SEND_OK;
    if (status == ASTRA_SYSCALL_WOULD_BLOCK)
        return ASTRA_INPUT_PORT_SEND_FULL;
    if (status == ASTRA_SYSCALL_PEER_DEAD || status == ASTRA_SYSCALL_CLOSED)
        return ASTRA_INPUT_PORT_SEND_PEER_DEAD;
    return ASTRA_INPUT_PORT_SEND_ERROR;
}

static void connected_reply(uint32_t handle, uint32_t transaction,
                            uint32_t status, uint32_t client,
                            uint32_t generation)
{
    AstraInputConnected reply = {0};

    reply.header.total_size = sizeof(reply);
    reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply.header.protocol = ASTRA_INPUT_SERVICE_PROTOCOL;
    reply.header.protocol_version = ASTRA_INPUT_SERVICE_VERSION;
    reply.header.operation = ASTRA_INPUT_OPERATION_CONNECTED;
    reply.header.transaction_id = transaction;
    reply.status = status;
    if (status == ASTRA_STATUS_OK) {
        reply.client = client;
        reply.generation = generation;
    }
    (void)astra_port_send(handle, &reply, sizeof(reply), NULL, 0u);
}

static int client_active(const AstraInputService *service, uint32_t id)
{
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index)
        if (service->clients[index].active != 0u &&
            service->clients[index].id == id)
            return 1;
    return 0;
}

static void reap_clients(AstraInputService *service,
                         AstraInputPortSink sinks[ASTRA_INPUT_CLIENT_MAX])
{
    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        if (sinks[index].send_handle != 0u &&
            !client_active(service, index + 1u)) {
            (void)astra_close(sinks[index].send_handle);
            sinks[index] = (AstraInputPortSink){0};
        }
    }
}

static void accept_client(
    uint32_t receive, AstraInputService *service,
    AstraInputPortSink sinks[ASTRA_INPUT_CLIENT_MAX])
{
    AstraInputConnect request = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t client_index = ASTRA_INPUT_CLIENT_MAX;
    uint32_t client_id = 0u;
    uint32_t status = astra_port_receive(
        receive, &request, sizeof(request), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &size, &count);

    if (status != ASTRA_SYSCALL_OK)
        return;
    status = size == sizeof(request) && count == 2u &&
             request.header.total_size == sizeof(request) &&
             request.header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
             request.header.flags == 0u &&
             request.header.protocol == ASTRA_INPUT_SERVICE_PROTOCOL &&
             request.header.protocol_version == ASTRA_INPUT_SERVICE_VERSION &&
             request.header.reserved == 0u &&
             request.header.operation == ASTRA_INPUT_OPERATION_CONNECT &&
             request.header.transaction_id != 0u &&
             request.subscriptions != 0u &&
             (request.subscriptions & ~ASTRA_INPUT_SUBSCRIBE_ALL) == 0u &&
             (request.flags & ~ASTRA_INPUT_CONNECT_SEAT_OWNER) == 0u ?
             ASTRA_STATUS_OK : ASTRA_STATUS_PROTOCOL;
    if (status == ASTRA_STATUS_OK &&
        (request.flags & ASTRA_INPUT_CONNECT_SEAT_OWNER) == 0u &&
        (request.subscriptions &
         ~(ASTRA_INPUT_SUBSCRIBE_POINTER_MOTION |
           ASTRA_INPUT_SUBSCRIBE_POINTER_BUTTON |
           ASTRA_INPUT_SUBSCRIBE_POINTER_WHEEL)) != 0u)
        status = ASTRA_STATUS_ACCESS;
    if (status == ASTRA_STATUS_OK &&
        (request.flags & ASTRA_INPUT_CONNECT_SEAT_OWNER) != 0u &&
        service->focus_id != 0u)
        status = ASTRA_STATUS_BUSY;
    reap_clients(service, sinks);
    if (status == ASTRA_STATUS_OK) {
        for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
            if (sinks[index].send_handle == 0u) {
                client_index = index;
                break;
            }
        }
        if (client_index == ASTRA_INPUT_CLIENT_MAX)
            status = ASTRA_STATUS_LIMIT;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraInputPortSink *sink = &sinks[client_index];

        client_id = client_index + 1u;
        sink->send = send_event;
        sink->send_handle = handles[0];
        if (!astra_input_service_attach(service, client_id,
                                        astra_input_port_deliver, sink) ||
            !astra_input_service_subscribe(service, client_id,
                                           request.subscriptions, 0u) ||
            ((request.flags & ASTRA_INPUT_CONNECT_SEAT_OWNER) != 0u &&
             !astra_input_service_set_focus(service, client_id, 0u))) {
            (void)astra_input_service_detach(service, client_id);
            *sink = (AstraInputPortSink){0};
            status = ASTRA_STATUS_LIMIT;
        } else {
            handles[0] = 0u;
        }
    }
    if (count == 2u)
        connected_reply(
            handles[1], request.header.transaction_id, status, client_id,
            status == ASTRA_STATUS_OK ?
                service->clients[client_index].generation : 0u);
    for (uint32_t index = 0u; index < count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

static uint32_t now_ms(uint64_t now_ns)
{
    return (uint32_t)(now_ns / UINT64_C(1000000));
}

static void serve(uint32_t receive, uint32_t input, uint32_t irq,
                  AstraInputService *service)
{
    AstraInputPortSink sinks[ASTRA_INPUT_CLIENT_MAX] = {0};

    if (astra_irq_arm(irq) != ASTRA_SYSCALL_OK)
        astra_process_exit(ASTRA_STATUS_IO);
    for (;;) {
        uint32_t waits[2] = { receive, irq };
        uint64_t now = astra_clock_monotonic();
        uint32_t delay = astra_input_service_next_delay(service, now_ms(now));
        uint64_t deadline = delay == ASTRA_INPUT_REPEAT_DISABLED ?
            ASTRA_DEADLINE_FOREVER : now + (uint64_t)delay * UINT64_C(1000000);
        uint32_t selected = 0u;
        uint32_t status = astra_wait_multiple(
            waits, 2u, deadline, &selected, NULL);

        if (status == ASTRA_SYSCALL_TIMED_OUT) {
            astra_input_service_tick(
                service, now_ms(astra_clock_monotonic()));
            continue;
        }
        if (status != ASTRA_SYSCALL_OK || selected > 1u)
            astra_process_exit(ASTRA_STATUS_IO);
        if (selected == 0u) {
            accept_client(receive, service, sinks);
        } else {
            AstraInputEvent events[ASTRA_INPUT_READ_BATCH_MAX];
            AstraIrqRecord record;

            status = astra_irq_read(irq, &record, NULL);
            if (status != ASTRA_SYSCALL_OK)
                astra_process_exit(ASTRA_STATUS_IO);
            for (;;) {
                uint32_t count = 0u;
                uint32_t flags = 0u;

                status = astra_input_read(
                    input, events, ASTRA_INPUT_READ_BATCH_MAX, &count, &flags);
                if (status == ASTRA_SYSCALL_WOULD_BLOCK)
                    break;
                if (status != ASTRA_SYSCALL_OK)
                    astra_process_exit(ASTRA_STATUS_IO);
                astra_input_service_ingest_batch(
                    service, events, count,
                    (flags & ASTRA_INPUT_READ_OVERFLOW) != 0u);
            }
            if (astra_irq_ack(irq, record.sequence) != ASTRA_SYSCALL_OK)
                astra_process_exit(ASTRA_STATUS_IO);
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraInputServiceConfig config = {
        .pointer_width = ASTRA_DISPLAY_WIDTH,
        .pointer_height = ASTRA_DISPLAY_HEIGHT,
        .repeat_delay_ms = 500u,
        .repeat_interval_ms = 40u,
        .acceleration_threshold = 4u,
        .acceleration_numerator = 2u,
        .acceleration_denominator = 1u,
    };
    AstraInputService service;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *input;
    const AstraStartupCapability *irq;
    uint32_t receive = 0u;
    uint32_t send = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    input = capability(startup, ASTRA_CAPABILITY_INPUT_DEVICE);
    irq = capability(startup, ASTRA_CAPABILITY_INPUT_IRQ);
    if (bootstrap == NULL || input == NULL || irq == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_input_service_init(&service, &config) ?
        astra_rt_port_create(8u, 8u * sizeof(AstraInputConnect),
                            &receive, &send) : ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (status == ASTRA_SYSCALL_OK)
        status = ready(bootstrap->handle, ASTRA_STATUS_OK, send);
    else
        (void)ready(bootstrap->handle, ASTRA_STATUS_IO, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    serve(receive, input->handle, irq->handle, &service);
    return ASTRA_STATUS_OK;
}
