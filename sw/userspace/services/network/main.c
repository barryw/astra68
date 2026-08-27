#include <astra/bytes.h>
#include <astra/limits.h>
#include <astra/network.h>
#include <astra/network_core.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

ASTRA_PROGRAM("network-service", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    NETWORK_CONTROL_QUEUE_DEPTH = 1u,
    NETWORK_FAIL_DEVICE = ASTRA_STATUS_PROGRAM_FIRST,
    NETWORK_FAIL_DMA,
    NETWORK_FAIL_PORT,
    NETWORK_FAIL_IRQ = 0x4e490000u, /* "NI" | Astra syscall status */
    NETWORK_FAIL_READY
};

typedef struct NetworkSession {
    uint32_t active, id, generation;
    uint32_t control, reply, area, notify;
    void *shared;
    uint32_t shared_size, may_listen;
} NetworkSession;

typedef struct NetworkEndpoint {
    uint32_t active, id, generation;
    uint32_t control, readiness_event, interest, readiness;
    uint16_t family;
    uint8_t type, protocol;
} NetworkEndpoint;

static NetworkSession sessions[ASTRA_HANDLE_COUNT_MAX];
static NetworkEndpoint endpoints[ASTRA_HANDLE_COUNT_MAX];
static uint32_t next_session, next_generation;
static uint32_t device_handle, irq_handle, dma_handle;
static uint8_t *dma_bytes;
static uint32_t dma_size;
static uint32_t outbound_receive, outbound_send;
static uint32_t listen_receive, listen_send;

static uint32_t next_nonzero(uint32_t *value)
{
    ++*value;
    if (*value == 0u)
        ++*value;
    return *value;
}

static uint32_t remaining_interest(uint32_t interest, uint32_t readiness)
{
    if ((readiness & (ASTRA_NETWORK_READY_ERROR |
                      ASTRA_NETWORK_READY_PEER_CLOSED)) != 0u)
        return 0u;
    if ((readiness & (ASTRA_NETWORK_READY_READABLE |
                      ASTRA_NETWORK_READY_ACCEPTABLE)) != 0u)
        interest &= ~(ASTRA_NETWORK_READY_READABLE |
                      ASTRA_NETWORK_READY_ACCEPTABLE);
    if ((readiness & (ASTRA_NETWORK_READY_WRITABLE |
                      ASTRA_NETWORK_READY_CONNECTED)) != 0u)
        interest &= ~(ASTRA_NETWORK_READY_WRITABLE |
                      ASTRA_NETWORK_READY_CONNECTED);
    return interest;
}

static void close_handle(uint32_t *handle)
{
    if (*handle != 0u)
        (void)astra_close(*handle);
    *handle = 0u;
}

static void close_received(uint32_t *handles, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
        close_handle(&handles[index]);
}

static void fill_reply(AstraNetworkReplyMessage *reply,
                       const AstraNetworkRequestMessage *request,
                       uint32_t session, AstraNetworkStatus status)
{
    (void)memset(reply, 0, sizeof(*reply));
    astra_message_header_set(&reply->header, sizeof(*reply),
                             ASTRA_NETWORK_PROTOCOL, ASTRA_NETWORK_VERSION,
                             request->header.operation,
                             request->header.transaction_id);
    reply->status = status;
    reply->session = session;
}

static uint32_t send_reply(uint32_t handle,
                           const AstraNetworkRequestMessage *request,
                           AstraNetworkReplyMessage *reply,
                           const uint32_t *handles, uint32_t count)
{
    (void)request;
    return astra_port_send(handle, reply, sizeof(*reply), handles, count);
}

static NetworkSession *find_session(uint32_t id)
{
    for (uint32_t index = 0u; index < ASTRA_HANDLE_COUNT_MAX; ++index)
        if (sessions[index].active != 0u && sessions[index].id == id)
            return &sessions[index];
    return NULL;
}

static AstraNetworkStatus host_execute(AstraNetworkHostCommand *source,
                                       const void *input, uint32_t input_size,
                                       void *output, uint32_t output_size)
{
    AstraNetworkTransportRequest request;
    AstraNetworkHostCommand *command;
    uint32_t data_size = input_size > output_size ? input_size : output_size;
    uint32_t executed = 0u;
    uint32_t status;

    if (source == NULL || data_size > dma_size - sizeof(*command))
        return ASTRA_NETWORK_INVALID;
    command = (AstraNetworkHostCommand *)(void *)dma_bytes;
    (void)memset(dma_bytes, 0, sizeof(*command) + data_size);
    *command = *source;
    command->size = sizeof(*command);
    command->version = ASTRA_NETWORK_HOST_COMMAND_VERSION;
    if (data_size != 0u)
        command->data_offset = sizeof(*command);
    command->data_length = input_size;
    command->data_capacity = output_size;
    if (input_size != 0u)
        (void)memcpy(dma_bytes + sizeof(*command), input, input_size);
    (void)memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.buffer = dma_handle;
    request.byte_size = sizeof(*command) + data_size;
    request.command_count = 1u;
    status = astra_network_lease_execute(device_handle, &request, &executed);
    if (status != ASTRA_SYSCALL_OK)
        return astra_network_status_from_syscall(status);
    if (executed != 1u)
        return ASTRA_NETWORK_IO;
    *source = *command;
    if (output != NULL && command->result_status == ASTRA_NETWORK_OK) {
        uint32_t moved = command->result_value;

        if (command->operation == ASTRA_NETWORK_HOST_RESOLVE &&
            moved <= UINT32_MAX / sizeof(AstraNetworkAddress))
            moved *= sizeof(AstraNetworkAddress);
        else if (command->operation == ASTRA_NETWORK_HOST_RECEIVE &&
                 (command->flags & ASTRA_NETWORK_MESSAGE_TRUNCATE) != 0u &&
                 moved > output_size)
            moved = output_size;
        if (moved > output_size)
            return ASTRA_NETWORK_IO;
        if (moved != 0u)
            (void)memcpy(output, dma_bytes + sizeof(*command), moved);
    }
    return (AstraNetworkStatus)command->result_status;
}

static void host_close(NetworkEndpoint *endpoint)
{
    AstraNetworkHostCommand command;

    (void)memset(&command, 0, sizeof(command));
    command.operation = ASTRA_NETWORK_HOST_CLOSE;
    command.endpoint = endpoint->id;
    command.endpoint_generation = endpoint->generation;
    (void)host_execute(&command, NULL, 0u, NULL, 0u);
}

static void endpoint_release(NetworkEndpoint *endpoint)
{
    if (endpoint->active != 0u)
        host_close(endpoint);
    close_handle(&endpoint->control);
    close_handle(&endpoint->readiness_event);
    (void)memset(endpoint, 0, sizeof(*endpoint));
}

static void session_release(NetworkSession *session)
{
    if (session->shared != NULL)
        (void)astra_rt_area_unmap(session->shared);
    close_handle(&session->control);
    close_handle(&session->notify);
    close_handle(&session->area);
    close_handle(&session->reply);
    (void)memset(session, 0, sizeof(*session));
}

static int request_valid(const AstraNetworkRequestMessage *request,
                         uint32_t size)
{
    return size == sizeof(*request) &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == sizeof(request->header) &&
           request->header.protocol == ASTRA_NETWORK_PROTOCOL &&
           request->header.protocol_version == ASTRA_NETWORK_VERSION;
}

static NetworkSession *session_slot(void)
{
    for (uint32_t index = 0u; index < ASTRA_HANDLE_COUNT_MAX; ++index)
        if (sessions[index].active == 0u)
            return &sessions[index];
    return NULL;
}

static NetworkEndpoint *endpoint_slot(void)
{
    for (uint32_t index = 0u; index < ASTRA_HANDLE_COUNT_MAX; ++index)
        if (endpoints[index].active == 0u)
            return &endpoints[index];
    return NULL;
}

static void open_session(const AstraNetworkRequestMessage *request,
                         int may_listen,
                         uint32_t *handles, uint32_t handle_count)
{
    AstraNetworkReplyMessage reply;
    NetworkSession *session = NULL;
    void *shared = NULL;
    uint32_t mapped = 0u, notify_wait = 0u, control_send = 0u, status;

    fill_reply(&reply, request, 0u, ASTRA_NETWORK_INVALID);
    if (handle_count != 2u || handles[0] == 0u || handles[1] == 0u ||
        request->session != 0u || request->length == 0u)
        goto answer;
    session = session_slot();
    if (session == NULL) {
        reply.status = ASTRA_NETWORK_RESOURCE_LIMIT;
        goto answer;
    }
    status = astra_rt_area_map(handles[1],
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &shared, &mapped);
    if (status != ASTRA_SYSCALL_OK || mapped != request->length ||
        !astra_network_shared_valid(shared, mapped, 1u)) {
        reply.status = status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
            ASTRA_NETWORK_RESOURCE_LIMIT : ASTRA_NETWORK_INVALID;
        goto answer;
    }
    status = astra_rt_event_create(
        0u, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER |
                ASTRA_RIGHT_ADMINISTER, &session->notify);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_handle_duplicate(
            session->notify, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER,
            &notify_wait);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(
            NETWORK_CONTROL_QUEUE_DEPTH,
            NETWORK_CONTROL_QUEUE_DEPTH * sizeof(AstraNetworkRequestMessage),
            &session->control, &control_send);
    if (status != ASTRA_SYSCALL_OK) {
        reply.status = astra_network_status_from_syscall(status);
        goto answer;
    }
    session->id = next_nonzero(&next_session);
    session->generation = next_nonzero(&next_generation);
    session->reply = handles[0];
    session->area = handles[1];
    session->shared = shared;
    session->shared_size = mapped;
    session->may_listen = may_listen != 0;
    session->active = 1u;
    ((AstraNetworkSharedHeader *)shared)->generation = session->generation;
    handles[0] = 0u;
    handles[1] = 0u;
    reply.status = ASTRA_NETWORK_OK;
    reply.session = session->id;
    reply.generation = session->generation;
    {
        uint32_t transferred[2] = {notify_wait, control_send};

        status = send_reply(session->reply, request, &reply, transferred, 2u);
    }
    if (status == ASTRA_SYSCALL_OK)
        return;
    close_handle(&notify_wait);
    close_handle(&control_send);
    session_release(session);
    return;

answer:
    if (handles[0] != 0u)
        (void)send_reply(handles[0], request, &reply, NULL, 0u);
    if (shared != NULL)
        (void)astra_rt_area_unmap(shared);
    if (session != NULL)
        close_handle(&session->notify);
    close_handle(&notify_wait);
    close_handle(&control_send);
    close_received(handles, handle_count);
}

static int shared_slot(NetworkSession *session,
                       const AstraNetworkRequestMessage *request, int tx,
                       uint8_t **bytes, uint32_t *length)
{
    AstraNetworkSharedHeader *header = session->shared;
    AstraNetworkSharedSlot *slots, *slot;

    if (!astra_network_shared_valid(session->shared, session->shared_size,
                                    session->generation) ||
        request->slot >= header->slot_count)
        return 0;
    slots = astra_network_shared_slots(header);
    slot = &slots[request->slot];
    if (slot->generation != session->generation ||
        slot->endpoint != request->endpoint ||
        slot->offset > header->slot_size ||
        request->length > header->slot_size - slot->offset ||
        slot->length != request->length ||
        (tx && (request->slot >= header->tx_slot_count ||
                slot->state != ASTRA_NETWORK_SLOT_TX_READY)) ||
        (!tx && (request->slot < header->tx_slot_count ||
                 slot->state != ASTRA_NETWORK_SLOT_RX_READING)))
        return 0;
    *bytes = astra_network_shared_slot_bytes(header, request->slot) +
             slot->offset;
    *length = request->length;
    return 1;
}

static AstraNetworkStatus create_endpoint(
    AstraNetworkReplyMessage *reply, uint32_t transferred[2],
    uint32_t *transferred_count,
    uint32_t host_id, uint32_t host_generation,
    uint16_t family, uint8_t type, uint8_t protocol)
{
    NetworkEndpoint *endpoint = endpoint_slot();
    uint32_t control_send = 0u;
    uint32_t status;

    if (endpoint == NULL)
        return ASTRA_NETWORK_RESOURCE_LIMIT;
    status = astra_rt_event_create(
        0u, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER |
                ASTRA_RIGHT_ADMINISTER, &endpoint->readiness_event);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_handle_duplicate(
            endpoint->readiness_event,
            ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER, &transferred[0]);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_port_create(
            NETWORK_CONTROL_QUEUE_DEPTH,
            NETWORK_CONTROL_QUEUE_DEPTH * sizeof(AstraNetworkRequestMessage),
            &endpoint->control, &control_send);
    if (status != ASTRA_SYSCALL_OK) {
        close_handle(&endpoint->readiness_event);
        close_handle(&endpoint->control);
        close_handle(&transferred[0]);
        close_handle(&control_send);
        return astra_network_status_from_syscall(status);
    }
    endpoint->id = host_id;
    endpoint->generation = host_generation;
    endpoint->family = family;
    endpoint->type = type;
    endpoint->protocol = protocol;
    endpoint->active = 1u;
    reply->endpoint = host_id;
    reply->generation = host_generation;
    transferred[1] = control_send;
    *transferred_count = 2u;
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus dispatch_endpoint(
    NetworkSession *session, const AstraNetworkRequestMessage *request,
    AstraNetworkReplyMessage *reply, uint32_t transferred[2],
    uint32_t *transferred_count, NetworkEndpoint *bound_endpoint)
{
    AstraNetworkHostCommand command;
    NetworkEndpoint *endpoint;
    AstraNetworkStatus status;
    uint8_t *shared_bytes = NULL;
    uint32_t shared_length = 0u;

    (void)memset(&command, 0, sizeof(command));
    if (request->header.operation == ASTRA_NETWORK_OPEN_ENDPOINT) {
        uint8_t type = (uint8_t)(request->flags >> 8);
        uint8_t protocol = (uint8_t)request->flags;

        command.operation = ASTRA_NETWORK_HOST_ENDPOINT_OPEN;
        command.family = request->address.family;
        command.type = type;
        command.protocol = protocol;
        status = host_execute(&command, NULL, 0u, NULL, 0u);
        if (status != ASTRA_NETWORK_OK) {
            reply->value = 1u;
            (void)astra_log_failure("network host endpoint open", status);
            return status;
        }
        status = create_endpoint(reply, transferred, transferred_count,
                                 command.result_endpoint,
                                 command.result_generation,
                                 request->address.family, type, protocol);
        if (status != ASTRA_NETWORK_OK) {
            reply->value = 2u;
            (void)astra_log_failure("network endpoint publish", status);
            NetworkEndpoint temporary = {0};
            temporary.active = 1u;
            temporary.id = command.result_endpoint;
            temporary.generation = command.result_generation;
            host_close(&temporary);
        }
        return status;
    }
    endpoint = bound_endpoint;
    if (endpoint == NULL)
        return ASTRA_NETWORK_INVALID;
    command.endpoint = endpoint->id;
    command.endpoint_generation = endpoint->generation;
    switch (request->header.operation) {
    case ASTRA_NETWORK_BIND:
    case ASTRA_NETWORK_CONNECT:
        if (!astra_network_address_valid(&request->address, 0))
            return ASTRA_NETWORK_INVALID;
        command.operation = request->header.operation == ASTRA_NETWORK_BIND ?
            ASTRA_NETWORK_HOST_BIND : ASTRA_NETWORK_HOST_CONNECT;
        command.address = request->address;
        break;
    case ASTRA_NETWORK_LISTEN:
        if (session->may_listen == 0u)
            return ASTRA_NETWORK_ACCESS;
        command.operation = ASTRA_NETWORK_HOST_LISTEN;
        command.value = request->value;
        break;
    case ASTRA_NETWORK_ACCEPT:
        if (session->may_listen == 0u)
            return ASTRA_NETWORK_ACCESS;
        command.operation = ASTRA_NETWORK_HOST_ACCEPT;
        status = host_execute(&command, NULL, 0u, NULL, 0u);
        if (status != ASTRA_NETWORK_OK)
            return status;
        reply->address = command.result_address;
        return create_endpoint(reply, transferred, transferred_count,
                               command.result_endpoint,
                               command.result_generation, endpoint->family,
                               endpoint->type, endpoint->protocol);
    case ASTRA_NETWORK_SEND:
        if (!shared_slot(session, request, 1, &shared_bytes, &shared_length))
            return ASTRA_NETWORK_INVALID;
        command.operation = ASTRA_NETWORK_HOST_SEND;
        command.flags = request->flags;
        command.address = request->address;
        status = host_execute(&command, shared_bytes, shared_length, NULL, 0u);
        reply->transferred = command.result_value;
        return status;
    case ASTRA_NETWORK_RECEIVE:
        if (!shared_slot(session, request, 0, &shared_bytes, &shared_length))
            return ASTRA_NETWORK_INVALID;
        command.operation = ASTRA_NETWORK_HOST_RECEIVE;
        command.flags = request->flags;
        status = host_execute(&command, NULL, 0u, shared_bytes, shared_length);
        reply->transferred = command.result_value;
        reply->address = command.result_address;
        return status;
    case ASTRA_NETWORK_GET_LOCAL_ADDRESS:
        command.operation = ASTRA_NETWORK_HOST_GET_LOCAL_ADDRESS;
        break;
    case ASTRA_NETWORK_GET_PEER_ADDRESS:
        command.operation = ASTRA_NETWORK_HOST_GET_PEER_ADDRESS;
        break;
    case ASTRA_NETWORK_GET_OPTION:
        command.operation = ASTRA_NETWORK_HOST_GET_OPTION;
        command.value = request->value;
        break;
    case ASTRA_NETWORK_SET_OPTION:
        command.operation = ASTRA_NETWORK_HOST_SET_OPTION;
        command.value = request->value;
        command.flags = request->flags;
        break;
    case ASTRA_NETWORK_SHUTDOWN:
        command.operation = ASTRA_NETWORK_HOST_SHUTDOWN;
        command.flags = request->flags;
        break;
    case ASTRA_NETWORK_DOORBELL:
        if (endpoint->readiness != 0u) {
            reply->readiness = endpoint->readiness;
            endpoint->readiness = 0u;
            return ASTRA_NETWORK_OK;
        }
        command.operation = ASTRA_NETWORK_HOST_ARM;
        command.flags = request->flags;
        status = host_execute(&command, NULL, 0u, NULL, 0u);
        if (status == ASTRA_NETWORK_OK) {
            reply->readiness = command.result_value;
            endpoint->interest = remaining_interest(
                request->flags, command.result_value);
        }
        return status;
    case ASTRA_NETWORK_CLOSE:
        endpoint_release(endpoint);
        return ASTRA_NETWORK_OK;
    default:
        return ASTRA_NETWORK_UNSUPPORTED;
    }
    status = host_execute(&command, NULL, 0u, NULL, 0u);
    reply->value = command.result_value;
    reply->address = command.result_address;
    return status;
}

static AstraNetworkStatus dispatch_resolve(
    NetworkSession *session, const AstraNetworkRequestMessage *request,
    AstraNetworkReplyMessage *reply)
{
    AstraNetworkHostCommand command;
    AstraNetworkStatus status;
    uint8_t *shared_bytes = NULL;
    uint32_t shared_length = 0u;

    (void)memset(&command, 0, sizeof(command));
    if (request->header.operation == ASTRA_NETWORK_CANCEL) {
        command.operation = ASTRA_NETWORK_HOST_CANCEL;
        command.value = request->value;
        return host_execute(&command, NULL, 0u, NULL, 0u);
    }
    command.operation = ASTRA_NETWORK_HOST_RESOLVE;
    command.value = request->value;
    if (request->value == 0u) {
        if (!shared_slot(session, request, 1, &shared_bytes, &shared_length) ||
            shared_length == 0u || shared_length > ASTRA_NETWORK_NAME_MAX ||
            request->address.family > ASTRA_NETWORK_FAMILY_IPV6)
            return ASTRA_NETWORK_INVALID;
        command.family = request->address.family;
        command.type = (uint8_t)(request->flags >> 8);
        command.protocol = (uint8_t)request->flags;
        command.address.size = sizeof(command.address);
        command.address.family = request->address.family;
        command.address.port = request->address.port;
        status = host_execute(&command, shared_bytes, shared_length, NULL, 0u);
    } else {
        if (!shared_slot(session, request, 0, &shared_bytes, &shared_length))
            return ASTRA_NETWORK_INVALID;
        status = host_execute(&command, NULL, 0u, shared_bytes, shared_length);
    }
    reply->value = command.result_value;
    if (request->value != 0u)
        reply->transferred = command.result_value;
    return status;
}

static void process_request(uint32_t receive, int may_listen,
                            NetworkSession *bound_session,
                            NetworkEndpoint *bound_endpoint)
{
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    uint32_t handles[2] = {0u, 0u};
    uint32_t transferred[2] = {0u, 0u};
    uint32_t handle_count = 0u, transferred_count = 0u;
    uint32_t size = 0u, owner = 0u;
    uint32_t status;
    NetworkSession *session;

    (void)memset(&request, 0, sizeof(request));
    status = astra_port_receive_from(receive, &request, sizeof(request),
                                     handles, 2u, &size, &handle_count,
                                     &owner);
    if (status != ASTRA_SYSCALL_OK)
        return;
    if (!request_valid(&request, size)) {
        close_received(handles, handle_count);
        return;
    }
    if (request.header.operation == ASTRA_NETWORK_OPEN_SESSION) {
        if (bound_session == NULL && bound_endpoint == NULL)
            open_session(&request, may_listen, handles, handle_count);
        else
            close_received(handles, handle_count);
        return;
    }
    (void)owner;
    close_received(handles, handle_count);
    session = bound_endpoint == NULL ? bound_session :
                                      find_session(request.session);
    if (session == NULL || session->active == 0u ||
        request.session != session->id)
        return;
    fill_reply(&reply, &request, session->id, ASTRA_NETWORK_INVALID);
    if (!astra_network_shared_valid(session->shared, session->shared_size,
                                    session->generation)) {
        reply.status = ASTRA_NETWORK_INVALID;
    } else if (bound_endpoint == NULL &&
               (request.header.operation == ASTRA_NETWORK_RESOLVE ||
                request.header.operation == ASTRA_NETWORK_CANCEL)) {
        reply.status = dispatch_resolve(session, &request, &reply);
    } else if (bound_endpoint == NULL &&
               request.header.operation == ASTRA_NETWORK_CLOSE &&
               request.endpoint == 0u) {
        reply.status = ASTRA_NETWORK_OK;
        (void)send_reply(session->reply, &request, &reply, NULL, 0u);
        session_release(session);
        return;
    } else if ((bound_endpoint == NULL &&
                request.header.operation == ASTRA_NETWORK_OPEN_ENDPOINT) ||
               (bound_endpoint != NULL &&
                request.endpoint == bound_endpoint->id &&
                request.generation == bound_endpoint->generation &&
                request.header.operation != ASTRA_NETWORK_OPEN_ENDPOINT &&
                request.header.operation != ASTRA_NETWORK_RESOLVE &&
                request.header.operation != ASTRA_NETWORK_CANCEL)) {
        reply.status = dispatch_endpoint(session, &request, &reply,
                                         transferred, &transferred_count,
                                         bound_endpoint);
    }
    status = send_reply(session->reply, &request, &reply, transferred,
                        transferred_count);
    if (status == ASTRA_SYSCALL_PEER_DEAD ||
             status == ASTRA_SYSCALL_CLOSED)
        session_release(session);
    if (status != ASTRA_SYSCALL_OK)
        close_received(transferred, transferred_count);
}

static void network_irq(void)
{
    AstraIrqRecord record;
    uint32_t events = 0u, status;

    for (;;) {
        (void)memset(&record, 0, sizeof(record));
        status = astra_irq_read(irq_handle, &record, &events);
        if (status == ASTRA_SYSCALL_WOULD_BLOCK)
            break;
        if (status != ASTRA_SYSCALL_OK ||
            astra_irq_ack(irq_handle, record.sequence) != ASTRA_SYSCALL_OK)
            return;
    }
    for (uint32_t index = 0u; index < ASTRA_HANDLE_COUNT_MAX; ++index)
        if (sessions[index].active != 0u)
            (void)astra_rt_signal(sessions[index].notify, 1u, NULL);
    for (uint32_t index = 0u; index < ASTRA_HANDLE_COUNT_MAX; ++index) {
        AstraNetworkHostCommand command;
        AstraNetworkStatus network_status;

        if (endpoints[index].active == 0u || endpoints[index].interest == 0u)
            continue;
        (void)memset(&command, 0, sizeof(command));
        command.operation = ASTRA_NETWORK_HOST_ARM;
        command.endpoint = endpoints[index].id;
        command.endpoint_generation = endpoints[index].generation;
        command.flags = endpoints[index].interest;
        network_status = host_execute(&command, NULL, 0u, NULL, 0u);
        endpoints[index].readiness = network_status == ASTRA_NETWORK_OK ?
            command.result_value : ASTRA_NETWORK_READY_ERROR;
        endpoints[index].interest = network_status == ASTRA_NETWORK_OK ?
            remaining_interest(endpoints[index].interest,
                               endpoints[index].readiness) : 0u;
        if (endpoints[index].readiness != 0u) {
            (void)astra_rt_signal(endpoints[index].readiness_event, 1u, NULL);
        }
    }
}

static uint32_t network_start(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device, *irq;
    AstraNetworkLeaseInfo lease;
    AstraDmaBufferInfo dma;
    uint32_t status;

    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_NETWORK_DEVICE);
    irq = astra_startup_capability(startup, ASTRA_CAPABILITY_NETWORK_IRQ);
    if (device == NULL || irq == NULL)
        return NETWORK_FAIL_DEVICE;
    device_handle = device->handle;
    irq_handle = irq->handle;
    (void)memset(&lease, 0, sizeof(lease));
    status = astra_network_lease_query(device_handle, &lease);
    if (status != ASTRA_SYSCALL_OK || lease.size != sizeof(lease) ||
        (lease.capabilities & (ASTRA_NETWORK_CAP_IPV4 |
                               ASTRA_NETWORK_CAP_TCP |
                               ASTRA_NETWORK_CAP_UDP |
                               ASTRA_NETWORK_CAP_RESOLVE)) !=
            (ASTRA_NETWORK_CAP_IPV4 | ASTRA_NETWORK_CAP_TCP |
             ASTRA_NETWORK_CAP_UDP | ASTRA_NETWORK_CAP_RESOLVE) ||
        lease.maximum_transfer <
            sizeof(AstraNetworkHostCommand) + ASTRA_NETWORK_SLOT_BYTES)
        return NETWORK_FAIL_DEVICE;
    (void)memset(&dma, 0, sizeof(dma));
    status = astra_dma_create(lease.maximum_transfer, &dma);
    if (status != ASTRA_SYSCALL_OK || dma.size != ASTRA_DMA_BUFFER_INFO_SIZE ||
        dma.handle == 0u || dma.virtual_base == 0u ||
        dma.byte_size < lease.maximum_transfer)
        return NETWORK_FAIL_DMA;
    dma_handle = dma.handle;
    dma_bytes = (uint8_t *)(uintptr_t)dma.virtual_base;
    dma_size = dma.byte_size;
    if (astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX * sizeof(AstraNetworkRequestMessage),
            &outbound_receive, &outbound_send) != ASTRA_SYSCALL_OK ||
        astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX * sizeof(AstraNetworkRequestMessage),
            &listen_receive, &listen_send) != ASTRA_SYSCALL_OK)
        return NETWORK_FAIL_PORT;
    status = astra_irq_arm(irq_handle);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("network irq arm", status);
        return NETWORK_FAIL_IRQ | status;
    }
    return ASTRA_STATUS_OK;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    uint32_t published[2], status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = network_start(startup);
    published[0] = outbound_send;
    published[1] = listen_send;
    if (astra_service_ready(bootstrap->handle, status, published, 2u) !=
            ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
        status = NETWORK_FAIL_READY;
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        NetworkSession *wait_sessions[ASTRA_WAIT_MULTIPLE_MAX];
        NetworkEndpoint *wait_endpoints[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t count = 3u, index = ASTRA_WAIT_INDEX_NONE;

        waits[0] = outbound_receive;
        waits[1] = listen_receive;
        waits[2] = irq_handle;
        (void)memset(wait_sessions, 0, sizeof(wait_sessions));
        (void)memset(wait_endpoints, 0, sizeof(wait_endpoints));
        for (uint32_t slot = 0u; slot < ASTRA_HANDLE_COUNT_MAX; ++slot) {
            if (sessions[slot].active == 0u)
                continue;
            if (count == ASTRA_WAIT_MULTIPLE_MAX)
                return NETWORK_FAIL_PORT;
            waits[count] = sessions[slot].control;
            wait_sessions[count] = &sessions[slot];
            ++count;
        }
        for (uint32_t slot = 0u; slot < ASTRA_HANDLE_COUNT_MAX; ++slot) {
            if (endpoints[slot].active == 0u)
                continue;
            if (count == ASTRA_WAIT_MULTIPLE_MAX)
                return NETWORK_FAIL_PORT;
            waits[count] = endpoints[slot].control;
            wait_endpoints[count] = &endpoints[slot];
            ++count;
        }
        status = astra_wait_multiple(waits, count, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status == ASTRA_SYSCALL_PEER_DEAD ||
            status == ASTRA_SYSCALL_CLOSED) {
            if (index < count && wait_endpoints[index] != NULL)
                endpoint_release(wait_endpoints[index]);
            else if (index < count && wait_sessions[index] != NULL)
                session_release(wait_sessions[index]);
            continue;
        }
        if (status != ASTRA_SYSCALL_OK)
            return NETWORK_FAIL_PORT;
        if (index == 0u)
            process_request(outbound_receive, 0, NULL, NULL);
        else if (index == 1u)
            process_request(listen_receive, 1, NULL, NULL);
        else if (index == 2u)
            network_irq();
        else if (index < count && wait_sessions[index] != NULL)
            process_request(waits[index], 0, wait_sessions[index], NULL);
        else if (index < count && wait_endpoints[index] != NULL)
            process_request(waits[index], 0, NULL, wait_endpoints[index]);
    }
}
