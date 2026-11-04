#include <astra/bytes.h>
#include <astra/config_library.h>
#include <astra/limits.h>
#include <astra/network.h>
#include <astra/network_core.h>
#include <astra/program.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/volume_network.h>
#include <astra/volume.h>

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
    uint32_t shared_size, may_listen, may_admin;
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
static uint32_t admin_receive, admin_send;
static uint32_t ethernet_owner;
static uint64_t ethernet_owner_until;
static uint64_t ethernet_recovery_at = ASTRA_DEADLINE_FOREVER;
static AstraNetworkEthernetIPv4 ethernet_pending;
static AstraConfig network_config = ASTRA_CONFIG_INIT;
static AstraHandle volume_metadata, volume_admin;
static int mapping_store_ok;
static uint32_t *mapping_paused;
static uint32_t mapping_paused_count;
static uint32_t mapping_reconcile_index;
static uint64_t mapping_reconcile_at = ASTRA_DEADLINE_FOREVER;
static const AstraConfigField network_fields[] = {
    {"profile", 0u}, {"pending", 0u},
    {"mapping", 0u}, {"mapping_secret", ASTRA_CONFIG_FIELD_SENSITIVE},
    {"mapping_next", 0u}
};
static const AstraConfigSchema network_schema = {1u, network_fields, 5u};

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

        if (command->operation == ASTRA_NETWORK_HOST_RESOLVE) {
            if (moved > UINT32_MAX / sizeof(AstraNetworkAddress))
                return ASTRA_NETWORK_IO;
            moved *= sizeof(AstraNetworkAddress);
        } else if (command->operation == ASTRA_NETWORK_HOST_INTERFACES) {
            if (moved > UINT32_MAX /
                            sizeof(AstraNetworkInterfaceAddress))
                return ASTRA_NETWORK_IO;
            moved *= sizeof(AstraNetworkInterfaceAddress);
        } else if (command->operation == ASTRA_NETWORK_HOST_DEVICE_LIST) {
            if (moved > UINT32_MAX / sizeof(AstraNetworkInterfaceInfo))
                return ASTRA_NETWORK_IO;
            moved *= sizeof(AstraNetworkInterfaceInfo);
        } else if (command->operation == ASTRA_NETWORK_HOST_RECEIVE &&
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
    if (ethernet_owner == session->id) {
        AstraNetworkHostCommand command = {0};
        command.operation = ASTRA_NETWORK_HOST_ETHERNET_REVERT;
        (void)host_execute(&command, NULL, 0u, NULL, 0u);
        ethernet_owner = 0u;
        ethernet_owner_until = 0u;
        (void)memset(&ethernet_pending, 0, sizeof(ethernet_pending));
    }
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
                         int mode,
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
    session->may_listen = mode == 1;
    session->may_admin = mode == 2;
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
    command.operation = request->header.operation ==
        ASTRA_NETWORK_REVERSE_RESOLVE ?
        ASTRA_NETWORK_HOST_REVERSE_RESOLVE : ASTRA_NETWORK_HOST_RESOLVE;
    command.value = request->value;
    if (request->value == 0u) {
        if (request->header.operation == ASTRA_NETWORK_REVERSE_RESOLVE) {
            if (request->address.size != sizeof(request->address) ||
                (request->address.family != ASTRA_NETWORK_FAMILY_IPV4 &&
                 request->address.family != ASTRA_NETWORK_FAMILY_IPV6))
                return ASTRA_NETWORK_INVALID;
            command.address = request->address;
            status = host_execute(&command, NULL, 0u, NULL, 0u);
            reply->value = command.result_value;
            return status;
        }
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

static AstraNetworkStatus dispatch_interfaces(
    NetworkSession *session, const AstraNetworkRequestMessage *request,
    AstraNetworkReplyMessage *reply)
{
    AstraNetworkHostCommand command;
    uint8_t *bytes;
    uint32_t length;
    uint32_t unit = request->header.operation == ASTRA_NETWORK_DEVICES ?
        sizeof(AstraNetworkInterfaceInfo) :
        sizeof(AstraNetworkInterfaceAddress);
    AstraNetworkStatus status;

    if (!shared_slot(session, request, 0, &bytes, &length) ||
        length % unit != 0u)
        return ASTRA_NETWORK_INVALID;
    (void)memset(&command, 0, sizeof(command));
    command.operation = request->header.operation == ASTRA_NETWORK_DEVICES ?
        ASTRA_NETWORK_HOST_DEVICE_LIST : ASTRA_NETWORK_HOST_INTERFACES;
    status = host_execute(&command, NULL, 0u, bytes, length);
    reply->value = command.result_value;
    reply->transferred = command.result_value;
    return status;
}

static void ethernet_clear_owner(void)
{
    ethernet_owner = 0u;
    ethernet_owner_until = 0u;
    (void)memset(&ethernet_pending, 0, sizeof(ethernet_pending));
}

static AstraNetworkStatus ethernet_promote(const char *next,
                                           const char *name)
{
    char previous[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX];
    uint32_t count = 0u, index, length = 0u;
    AstraConfigChange changes[2] = {
        {"profile", next, 0u, ASTRA_CONFIG_CHANGE_APPEND},
        {"pending", NULL, 0u, ASTRA_CONFIG_CHANGE_REMOVE}
    };

    if (astra_config_count(&network_config, "profile", &count) !=
            ASTRA_CONFIG_OK)
        return ASTRA_NETWORK_IO;
    for (index = 0u; index < count; ++index) {
        AstraNetworkEthernetIPv4 saved;
        if (astra_config_get_string(&network_config, "profile", index,
                previous, sizeof(previous), &length) != ASTRA_CONFIG_OK ||
            !astra_network_ethernet_profile_decode(previous, &saved))
            return ASTRA_NETWORK_IO;
        if (strcmp(saved.name, name) == 0) {
            changes[0].operation = ASTRA_CONFIG_CHANGE_SET;
            changes[0].index = index;
            break;
        }
    }
    return astra_config_apply(&network_config, changes, 2u) ==
        ASTRA_CONFIG_OK ? ASTRA_NETWORK_OK : ASTRA_NETWORK_IO;
}

static void ethernet_recover(void)
{
    char text[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX];
    AstraNetworkEthernetIPv4 profile;
    AstraNetworkHostCommand command = {0};
    uint32_t count = 0u, length = 0u;
    AstraNetworkStatus status;

    if (ethernet_owner != 0u) return;
    ethernet_recovery_at = ASTRA_DEADLINE_FOREVER;
    if (astra_config_count(&network_config, "pending", &count) !=
            ASTRA_CONFIG_OK || count != 1u ||
        astra_config_get_string(&network_config, "pending", 0u, text,
            sizeof(text), &length) != ASTRA_CONFIG_OK ||
        !astra_network_ethernet_profile_decode(text, &profile)) {
        (void)astra_log_failure("network pending profile invalid",
                                ASTRA_NETWORK_IO);
        return;
    }
    command.operation = ASTRA_NETWORK_HOST_ETHERNET_STAGE;
    command.value = 120u;
    status = host_execute(&command, &profile, sizeof(profile), NULL, 0u);
    if (status == ASTRA_NETWORK_OK) {
        command.operation = ASTRA_NETWORK_HOST_ETHERNET_CONFIRM;
        status = host_execute(&command, NULL, 0u, NULL, 0u);
        if (status == ASTRA_NETWORK_OK)
            status = ethernet_promote(text, profile.name);
        else {
            command.operation = ASTRA_NETWORK_HOST_ETHERNET_REVERT;
            (void)host_execute(&command, NULL, 0u, NULL, 0u);
        }
    }
    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure("network pending profile recovery", status);
        ethernet_recovery_at = astra_clock_monotonic() +
            UINT64_C(30000000000);
    }
}

static AstraNetworkStatus dispatch_ethernet(
    NetworkSession *session, const AstraNetworkRequestMessage *request)
{
    AstraNetworkHostCommand command = {0};
    const void *input = NULL;
    uint32_t input_size = 0u;
    uint8_t *bytes;
    uint32_t length;
    uint64_t staged_at = 0u;
    AstraNetworkStatus status;

    if (session->may_admin == 0u || request->endpoint != 0u)
        return ASTRA_NETWORK_ACCESS;
    if (request->header.operation == ASTRA_NETWORK_ETHERNET_SAVED) {
        AstraNetworkEthernetIPv4 query;
        uint32_t count = 0u;
        char text[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX];
        if (!shared_slot(session, request, 1, &bytes, &length) ||
            length != sizeof(query)) return ASTRA_NETWORK_INVALID;
        (void)memcpy(&query, bytes, sizeof(query));
        if (astra_network_ethernet_ipv4_validate(&query) != ASTRA_NETWORK_OK ||
            astra_config_count(&network_config, "profile", &count) !=
                ASTRA_CONFIG_OK) return ASTRA_NETWORK_IO;
        for (uint32_t at = 0u; at < count; ++at) {
            AstraNetworkEthernetIPv4 saved;
            uint32_t text_length = 0u;
            if (astra_config_get_string(&network_config, "profile", at,
                    text, sizeof(text), &text_length) != ASTRA_CONFIG_OK ||
                !astra_network_ethernet_profile_decode(text, &saved))
                return ASTRA_NETWORK_IO;
            if (strcmp(saved.name, query.name) == 0) {
                (void)memcpy(bytes, &saved, sizeof(saved));
                return ASTRA_NETWORK_OK;
            }
        }
        return ASTRA_NETWORK_NAME_NOT_FOUND;
    }
    switch (request->header.operation) {
    case ASTRA_NETWORK_ETHERNET_PROBE:
    case ASTRA_NETWORK_ETHERNET_STAGE:
        if (!shared_slot(session, request, 1, &bytes, &length) ||
            length != sizeof(AstraNetworkEthernetIPv4) ||
            astra_network_ethernet_ipv4_validate(
                (const AstraNetworkEthernetIPv4 *)(const void *)bytes) !=
                    ASTRA_NETWORK_OK)
            return ASTRA_NETWORK_INVALID;
        input = bytes;
        input_size = length;
        if (request->header.operation == ASTRA_NETWORK_ETHERNET_STAGE) {
            uint32_t pending_count = 0u;
            if (astra_config_count(&network_config, "pending",
                    &pending_count) != ASTRA_CONFIG_OK)
                return ASTRA_NETWORK_IO;
            if (pending_count != 0u)
                return ASTRA_NETWORK_IN_PROGRESS;
            if (ethernet_owner != 0u &&
                astra_clock_monotonic() >= ethernet_owner_until) {
                ethernet_clear_owner();
            }
            if (ethernet_owner != 0u)
                return ASTRA_NETWORK_RESOURCE_LIMIT;
            command.operation = ASTRA_NETWORK_HOST_ETHERNET_STAGE;
            command.value = request->value;
        } else command.operation = ASTRA_NETWORK_HOST_ETHERNET_PROBE;
        break;
    case ASTRA_NETWORK_ETHERNET_CONFIRM:
    case ASTRA_NETWORK_ETHERNET_REVERT:
        if (request->length != 0u || ethernet_owner != session->id)
            return ASTRA_NETWORK_ACCESS;
        command.operation = request->header.operation ==
            ASTRA_NETWORK_ETHERNET_CONFIRM ?
                ASTRA_NETWORK_HOST_ETHERNET_CONFIRM :
                ASTRA_NETWORK_HOST_ETHERNET_REVERT;
        break;
    default:
        return ASTRA_NETWORK_INVALID;
    }
    if (command.operation == ASTRA_NETWORK_HOST_ETHERNET_CONFIRM) {
        char next[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX];
        if (!astra_network_ethernet_profile_encode(&ethernet_pending, next) ||
            astra_config_append_string(&network_config, "pending", next) !=
                ASTRA_CONFIG_OK) return ASTRA_NETWORK_IO;
        status = host_execute(&command, NULL, 0u, NULL, 0u);
        if (status == ASTRA_NETWORK_OK)
            status = ethernet_promote(next, ethernet_pending.name);
        else {
            command.operation = ASTRA_NETWORK_HOST_ETHERNET_REVERT;
            (void)host_execute(&command, NULL, 0u, NULL, 0u);
        }
        if (status != ASTRA_NETWORK_OK) {
            ethernet_recovery_at = astra_clock_monotonic() +
                UINT64_C(30000000000);
            status = ASTRA_NETWORK_IN_PROGRESS;
        }
    } else {
        if (command.operation == ASTRA_NETWORK_HOST_ETHERNET_STAGE)
            staged_at = astra_clock_monotonic();
        status = host_execute(&command, input, input_size, NULL, 0u);
    }
    if (status == ASTRA_NETWORK_OK) {
        if (command.operation == ASTRA_NETWORK_HOST_ETHERNET_STAGE) {
            ethernet_owner = session->id;
            ethernet_owner_until = staged_at +
                (uint64_t)request->value * UINT64_C(1000000000);
            ethernet_pending = *(const AstraNetworkEthernetIPv4 *)input;
        } else if (command.operation == ASTRA_NETWORK_HOST_ETHERNET_CONFIRM ||
                   command.operation == ASTRA_NETWORK_HOST_ETHERNET_REVERT) {
            ethernet_clear_owner();
        }
    } else if (status == ASTRA_NETWORK_IN_PROGRESS) {
        ethernet_clear_owner();
    }
    return status;
}

static int mapping_count(uint32_t *count)
{
    uint32_t secrets = 0u, next = 0u;
    return astra_config_count(&network_config, "mapping", count) ==
            ASTRA_CONFIG_OK &&
        astra_config_count(&network_config, "mapping_secret", &secrets) ==
            ASTRA_CONFIG_OK &&
        astra_config_count(&network_config, "mapping_next", &next) ==
            ASTRA_CONFIG_OK && *count == secrets && next <= 1u &&
        (*count == 0u || next == 1u);
}

static int mapping_read(uint32_t index, AstraNetworkMapping *mapping)
{
    char text[ASTRA_NETWORK_MAPPING_TEXT_MAX];
    char secret[sizeof(((AstraNetworkMappingSave *)0)->password) + 3u] = {0};
    uint32_t length = 0u;
    int valid = astra_config_get_string(&network_config, "mapping", index,
            text, sizeof(text), &length) == ASTRA_CONFIG_OK &&
        astra_network_mapping_decode(text, mapping) &&
        astra_config_get_string(&network_config, "mapping_secret", index,
            secret, sizeof(secret), &length) == ASTRA_CONFIG_OK;
    if (valid && secret[0] == 'p' && secret[1] == ':')
        mapping->flags |= ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL;
    else if (!valid || strcmp(secret, "n") != 0)
        valid = 0;
    if (valid)
        valid = astra_network_mapping_valid(mapping, 0, 1);
    astra_secret_wipe(secret, sizeof(secret));
    return valid;
}

static int mapping_integrity(void)
{
    uint32_t count = 0u, next_count = 0u, previous = 0u;
    uint64_t next = 0u;
    if (!mapping_count(&count) ||
        astra_config_count(&network_config, "mapping_next", &next_count) !=
            ASTRA_CONFIG_OK) return 0;
    for (uint32_t index = 0u; index < count; ++index) {
        AstraNetworkMapping mapping;
        if (!mapping_read(index, &mapping) || mapping.id <= previous)
            return 0;
        previous = mapping.id;
    }
    return next_count == 0u ? count == 0u :
        astra_config_get_u64(&network_config, "mapping_next", 0u, &next) ==
            ASTRA_CONFIG_OK && next > previous && next <= UINT32_MAX;
}

static AstraNetworkStatus mapping_find(uint32_t id, uint32_t *index,
    AstraNetworkMapping *mapping)
{
    uint32_t count = 0u;
    if (id == 0u || !mapping_count(&count)) return ASTRA_NETWORK_INVALID;
    for (uint32_t at = 0u; at < count; ++at) {
        if (!mapping_read(at, mapping)) return ASTRA_NETWORK_IO;
        if (mapping->id == id) {
            *index = at;
            return ASTRA_NETWORK_OK;
        }
    }
    return ASTRA_NETWORK_NAME_NOT_FOUND;
}

static int mapping_is_paused(uint32_t id)
{
    for (uint32_t at = 0u; at < mapping_paused_count; ++at)
        if (mapping_paused[at] == id) return 1;
    return 0;
}

static int mapping_pause(uint32_t id, int paused)
{
    for (uint32_t at = 0u; at < mapping_paused_count; ++at)
        if (mapping_paused[at] == id) {
            if (paused) return 1;
            if (at + 1u < mapping_paused_count)
                (void)memmove(&mapping_paused[at], &mapping_paused[at + 1u],
                    (mapping_paused_count - at - 1u) * sizeof(*mapping_paused));
            --mapping_paused_count;
            return 1;
        }
    if (!paused) return 1;
    if (mapping_paused_count >= UINT32_MAX / sizeof(*mapping_paused) - 1u)
        return 0;
    uint32_t *next = astra_runtime_reallocate(mapping_paused,
        (mapping_paused_count + 1u) * sizeof(*mapping_paused));
    if (next == NULL) return 0;
    mapping_paused = next;
    mapping_paused[mapping_paused_count++] = id;
    return 1;
}

static AstraNetworkStatus volume_status(AstraResult result)
{
    switch (result) {
    case ASTRA_OK: return ASTRA_NETWORK_OK;
    case ASTRA_ERROR_BUSY: return ASTRA_NETWORK_BUSY;
    case ASTRA_ERROR_PERMISSION: return ASTRA_NETWORK_ACCESS;
    case ASTRA_ERROR_INVALID_ARGUMENT: return ASTRA_NETWORK_INVALID;
    case ASTRA_ERROR_INVALID_HANDLE:
    case ASTRA_ERROR_NOT_PRESENT: return ASTRA_NETWORK_NAME_NOT_FOUND;
    case ASTRA_ERROR_NO_RESOURCES: return ASTRA_NETWORK_RESOURCE_LIMIT;
    case ASTRA_ERROR_OUT_OF_MEMORY: return ASTRA_NETWORK_OUT_OF_MEMORY;
    case ASTRA_ERROR_TIMEOUT: return ASTRA_NETWORK_TIMED_OUT;
    case ASTRA_ERROR_PEER_DEAD: return ASTRA_NETWORK_PEER_DEAD;
    default: return ASTRA_NETWORK_IO;
    }
}

static AstraNetworkStatus mapping_volume_state(const AstraNetworkMapping *mapping,
    AstraNetworkMappingVolume *volume)
{
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result;
    (void)memset(volume, 0, sizeof(*volume));
    volume->size = sizeof(*volume);
    volume->state = ASTRA_VOLUME_UNMOUNTED;
    result = astra_volume_managed_network_source(volume_admin, &mapping->source,
        mapping->id, &volume->source);
    if (result != ASTRA_OK) return volume_status(result);
    result = astra_volume_snapshot_open(volume_metadata, &snapshot);
    if (result != ASTRA_OK) return volume_status(result);
    if (astra_volume_snapshot_find(&snapshot, &volume->source) == NULL) {
        astra_volume_snapshot_close(&snapshot);
        return ASTRA_NETWORK_IO;
    }
    for (uint32_t at = 0u; at < snapshot.count; ++at) {
        const AstraVolumeRecord *row = &snapshot.records[at];
        if (row->kind != ASTRA_VOLUME_MOUNT ||
            row->parent != volume->source.id ||
            row->parent_generation != volume->source.generation)
            continue;
        if (volume->mount.id != 0u) {
            astra_volume_snapshot_close(&snapshot);
            return ASTRA_NETWORK_BUSY;
        }
        volume->mount = (AstraVolumeReference){snapshot.epoch, row->id,
                                               row->generation};
        volume->state = row->state;
        volume->status = row->status;
    }
    astra_volume_snapshot_close(&snapshot);
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus mapping_mount(const AstraNetworkMapping *mapping,
    uint32_t index,
    AstraNetworkMappingVolume *volume)
{
    AstraNetworkStatus status = mapping_volume_state(mapping, volume);
    if (status != ASTRA_NETWORK_OK) return status;
    if (volume->mount.id != 0u)
        return volume->state == ASTRA_VOLUME_MOUNTED ||
               volume->state == ASTRA_VOLUME_MOUNTING ?
               ASTRA_NETWORK_OK : ASTRA_NETWORK_BUSY;
    AstraResult result;
    if (mapping->source.format == ASTRA_VOLUME_SMB && mapping->account[0] != '\0') {
        AstraVolumeMountAuth auth = {.source = volume->source,
            .flags = (mapping->flags & ASTRA_NETWORK_MAPPING_READ_ONLY) != 0u ?
                ASTRA_VOLUME_READ_ONLY : 0u};
        char secret[sizeof(auth.password) + 3u] = {0};
        uint32_t length = 0u;
        (void)memcpy(auth.account, mapping->account, sizeof(auth.account));
        (void)memcpy(auth.domain, mapping->domain, sizeof(auth.domain));
        if (astra_config_get_string(&network_config, "mapping_secret", index,
                secret, sizeof(secret), &length) == ASTRA_CONFIG_OK &&
            ((secret[0] == 'p' && secret[1] == ':') ||
             (secret[0] == 'n' && secret[1] == '\0'))) {
            if (secret[0] == 'p')
                (void)memcpy(auth.password, secret + 2u,
                             strlen(secret + 2u) + 1u);
            result = astra_volume_mount_auth(volume_admin, &auth,
                                              &volume->mount);
            status = volume_status(result);
        } else
            status = ASTRA_NETWORK_ACCESS;
        astra_secret_wipe(&auth, sizeof(auth));
        astra_secret_wipe(secret, sizeof(secret));
    } else {
        AstraVolumeControl control = {.reference = volume->source,
            .operation = ASTRA_VOLUME_MOUNT_SOURCE,
            .flags = (mapping->flags & ASTRA_NETWORK_MAPPING_READ_ONLY) != 0u ?
                ASTRA_VOLUME_READ_ONLY : 0u};
        result = astra_volume_control(volume_admin, &control, &volume->mount);
        status = volume_status(result);
    }
    if (status == ASTRA_NETWORK_OK) volume->state = ASTRA_VOLUME_MOUNTING;
    return status;
}

static AstraNetworkStatus mapping_unmount(const AstraNetworkMapping *mapping,
    AstraNetworkMappingVolume *volume)
{
    AstraNetworkStatus status = mapping_volume_state(mapping, volume);
    AstraVolumeReference admitted = {0};
    AstraVolumeControl control;
    if (status != ASTRA_NETWORK_OK) return status;
    if (volume->mount.id == 0u || volume->state == ASTRA_VOLUME_UNMOUNTED)
        return ASTRA_NETWORK_OK;
    if (volume->state == ASTRA_VOLUME_QUIESCING)
        return ASTRA_NETWORK_IN_PROGRESS;
    if (volume->state != ASTRA_VOLUME_MOUNTED &&
        volume->state != ASTRA_VOLUME_FAILED)
        return ASTRA_NETWORK_BUSY;
    control = (AstraVolumeControl){.reference = volume->mount,
        .operation = ASTRA_VOLUME_UNMOUNT};
    status = volume_status(astra_volume_control(volume_admin, &control,
                                               &admitted));
    if (status == ASTRA_NETWORK_OK) volume->state = ASTRA_VOLUME_QUIESCING;
    return status;
}

static void mapping_reconcile(void)
{
    uint32_t count = 0u;
    mapping_reconcile_at = astra_clock_monotonic() +
        UINT64_C(10000000000);
    if (!mapping_store_ok) return;
    if (!mapping_count(&count)) return;
    if (count == 0u) {
        mapping_reconcile_at = ASTRA_DEADLINE_FOREVER;
        return;
    }
    if (mapping_reconcile_index >= count) mapping_reconcile_index = 0u;
    for (uint32_t scanned = 0u; scanned < count; ++scanned) {
        uint32_t index = mapping_reconcile_index++;
        AstraNetworkMapping mapping;
        AstraNetworkMappingVolume volume;
        if (mapping_reconcile_index == count) mapping_reconcile_index = 0u;
        if (!mapping_read(index, &mapping)) return;
        if ((mapping.flags & ASTRA_NETWORK_MAPPING_AUTO_CONNECT) == 0u ||
            mapping_is_paused(mapping.id)) continue;
        AstraNetworkStatus status = mapping_mount(&mapping, index, &volume);
        if (status == ASTRA_NETWORK_BUSY && volume.mount.id != 0u &&
            volume.state == ASTRA_VOLUME_FAILED)
            (void)mapping_unmount(&mapping, &volume);
        else if (status != ASTRA_NETWORK_OK &&
                 status != ASTRA_NETWORK_BUSY)
            (void)astra_log_failure("network mapping reconnect", status);
        if (mapping_reconcile_index != 0u)
            mapping_reconcile_at = astra_clock_monotonic() +
                UINT64_C(250000000);
        return;
    }
    mapping_reconcile_at = ASTRA_DEADLINE_FOREVER;
}

static AstraNetworkStatus mapping_release(const AstraNetworkMapping *mapping)
{
    AstraNetworkMappingVolume volume;
    AstraVolumeReference admitted = {0};
    AstraNetworkStatus status = mapping_volume_state(mapping, &volume);
    if (status != ASTRA_NETWORK_OK) return status;
    if (volume.mount.id != 0u && volume.state != ASTRA_VOLUME_UNMOUNTED)
        return ASTRA_NETWORK_BUSY;
    AstraVolumeControl control = {.reference = volume.source,
        .operation = ASTRA_VOLUME_RELEASE_NETWORK_SOURCE};
    return volume_status(astra_volume_control(volume_admin, &control,
                                               &admitted));
}

static AstraNetworkStatus mapping_save(const AstraNetworkMappingSave *input,
    uint32_t *saved_id)
{
    AstraNetworkMapping mapping = input->mapping, previous = {0};
    AstraConfigChange changes[3];
    char text[ASTRA_NETWORK_MAPPING_TEXT_MAX];
    char secret[sizeof(input->password) + 3u] = {0};
    char next_text[16];
    uint32_t count = 0u, index = 0u, next_count = 0u;
    uint64_t next = 1u;
    uint32_t change_count = 0u;
    AstraNetworkStatus status = ASTRA_NETWORK_INVALID;
    int has_password = 0;

    if (!astra_network_mapping_valid(&mapping, 1, 0) ||
        input->password_action > ASTRA_NETWORK_PASSWORD_CLEAR ||
        (input->password_action == ASTRA_NETWORK_PASSWORD_REPLACE &&
         (mapping.source.format != ASTRA_VOLUME_SMB ||
          mapping.account[0] == '\0' ||
          !astra_volume_auth_field_valid(input->password,
              sizeof(input->password), 0, 0)))) return status;
    if (input->password_action != ASTRA_NETWORK_PASSWORD_REPLACE)
        for (uint32_t at = 0u; at < sizeof(input->password); ++at)
            if (input->password[at] != '\0') return status;
    if (!mapping_count(&count) ||
        astra_config_count(&network_config, "mapping_next", &next_count) !=
            ASTRA_CONFIG_OK) return ASTRA_NETWORK_IO;
    if (mapping.id == 0u) {
        if (next_count != 0u &&
            astra_config_get_u64(&network_config, "mapping_next", 0u,
                                 &next) != ASTRA_CONFIG_OK)
            return ASTRA_NETWORK_IO;
        if (next == 0u || next >= UINT32_MAX)
            return ASTRA_NETWORK_RESOURCE_LIMIT;
        mapping.id = (uint32_t)next;
        index = count;
    } else {
        status = mapping_find(mapping.id, &index, &previous);
        if (status != ASTRA_NETWORK_OK) return status;
        if (input->password_action == ASTRA_NETWORK_PASSWORD_KEEP &&
            (strcmp(mapping.account, previous.account) != 0 ||
             strcmp(mapping.domain, previous.domain) != 0 ||
             mapping.source.format != previous.source.format ||
             strcmp(mapping.source.server, previous.source.server) != 0 ||
             strcmp(mapping.source.share, previous.source.share) != 0) &&
            (previous.flags & ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL) != 0u)
            return ASTRA_NETWORK_INVALID;
        if (memcmp(&mapping.source, &previous.source,
                   sizeof(mapping.source)) != 0) {
            status = mapping_release(&previous);
            if (status != ASTRA_NETWORK_OK) return status;
        }
    }
    has_password = input->password_action == ASTRA_NETWORK_PASSWORD_REPLACE ||
        (input->password_action == ASTRA_NETWORK_PASSWORD_KEEP &&
         (previous.flags & ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL) != 0u);
    if ((mapping.flags & ASTRA_NETWORK_MAPPING_AUTO_CONNECT) != 0u &&
        mapping.source.format == ASTRA_VOLUME_SMB &&
        mapping.account[0] != '\0' && !has_password)
        return ASTRA_NETWORK_INVALID;
    if (!astra_network_mapping_encode(&mapping, text))
        return ASTRA_NETWORK_INVALID;
    changes[change_count++] = (AstraConfigChange){"mapping", text, index,
        index == count ? ASTRA_CONFIG_CHANGE_APPEND : ASTRA_CONFIG_CHANGE_SET};
    if (input->password_action != ASTRA_NETWORK_PASSWORD_KEEP || index == count) {
        if (has_password) {
            secret[0] = 'p'; secret[1] = ':';
            (void)memcpy(secret + 2u, input->password,
                         strlen(input->password) + 1u);
        } else (void)memcpy(secret, "n", 2u);
        changes[change_count++] = (AstraConfigChange){"mapping_secret",
            secret, index, index == count ? ASTRA_CONFIG_CHANGE_APPEND :
                                       ASTRA_CONFIG_CHANGE_SET};
    }
    if (index == count) {
        AstraString next_string;
        astra_string_init(&next_string, next_text, sizeof(next_text));
        if (!astra_string_append_u64(&next_string, mapping.id + 1u)) {
            astra_secret_wipe(secret, sizeof(secret));
            return ASTRA_NETWORK_IO;
        }
        changes[change_count++] = (AstraConfigChange){"mapping_next",
            next_text, 0u, next_count == 0u ? ASTRA_CONFIG_CHANGE_APPEND :
                                             ASTRA_CONFIG_CHANGE_SET};
    }
    status = astra_config_apply(&network_config, changes, change_count) ==
        ASTRA_CONFIG_OK ? ASTRA_NETWORK_OK : ASTRA_NETWORK_IO;
    astra_secret_wipe(secret, sizeof(secret));
    if (status == ASTRA_NETWORK_OK) *saved_id = mapping.id;
    return status;
}

static AstraNetworkStatus dispatch_mapping(NetworkSession *session,
    const AstraNetworkRequestMessage *request, AstraNetworkReplyMessage *reply)
{
    uint32_t count = 0u, index = 0u, length = 0u;
    uint8_t *bytes = NULL;
    AstraNetworkMapping mapping;
    AstraNetworkStatus status;
    if (session->may_admin == 0u || request->endpoint != 0u)
        return ASTRA_NETWORK_ACCESS;
    if (!mapping_store_ok || !mapping_count(&count))
        return ASTRA_NETWORK_IO;
    switch (request->header.operation) {
    case ASTRA_NETWORK_MAPPING_READ:
        if (!shared_slot(session, request, 0, &bytes, &length) ||
            length != sizeof(mapping)) return ASTRA_NETWORK_INVALID;
        reply->value = count;
        if (request->value >= count)
            return ASTRA_NETWORK_NAME_NOT_FOUND;
        if (!mapping_read(request->value, &mapping))
            return ASTRA_NETWORK_IO;
        (void)memcpy(bytes, &mapping, sizeof(mapping));
        reply->transferred = sizeof(mapping);
        return ASTRA_NETWORK_OK;
    case ASTRA_NETWORK_MAPPING_SAVE: {
        AstraNetworkMappingSave input;
        if (!shared_slot(session, request, 1, &bytes, &length) ||
            length != sizeof(input)) return ASTRA_NETWORK_INVALID;
        (void)memcpy(&input, bytes, sizeof(input));
        status = mapping_save(&input, &reply->value);
        astra_secret_wipe(&input, sizeof(input));
        astra_secret_wipe(bytes, sizeof(input));
        if (status == ASTRA_NETWORK_OK) {
            (void)mapping_pause(reply->value, 0);
            mapping_reconcile_at = 0u;
        }
        return status;
    }
    case ASTRA_NETWORK_MAPPING_REMOVE: {
        AstraConfigChange changes[2];
        if (request->length != 0u || request->value == 0u)
            return ASTRA_NETWORK_INVALID;
        status = mapping_find(request->value, &index, &mapping);
        if (status != ASTRA_NETWORK_OK) return status;
        status = mapping_release(&mapping);
        if (status != ASTRA_NETWORK_OK) return status;
        changes[0] = (AstraConfigChange){"mapping", NULL, index,
            ASTRA_CONFIG_CHANGE_REMOVE};
        changes[1] = (AstraConfigChange){"mapping_secret", NULL, index,
            ASTRA_CONFIG_CHANGE_REMOVE};
        if (astra_config_apply(&network_config, changes, 2u) !=
            ASTRA_CONFIG_OK) return ASTRA_NETWORK_IO;
        (void)mapping_pause(request->value, 0);
        mapping_reconcile_at = 0u;
        return ASTRA_NETWORK_OK;
    }
    case ASTRA_NETWORK_MAPPING_MOUNT:
    case ASTRA_NETWORK_MAPPING_UNMOUNT:
    case ASTRA_NETWORK_MAPPING_STATE: {
        AstraNetworkMappingVolume volume;
        if (request->value == 0u ||
            !shared_slot(session, request, 0, &bytes, &length) ||
            length != sizeof(volume)) return ASTRA_NETWORK_INVALID;
        status = mapping_find(request->value, &index, &mapping);
        if (status != ASTRA_NETWORK_OK) return status;
        if (request->header.operation == ASTRA_NETWORK_MAPPING_MOUNT)
            status = mapping_mount(&mapping, index, &volume);
        else if (request->header.operation == ASTRA_NETWORK_MAPPING_UNMOUNT) {
            int was_paused = mapping_is_paused(mapping.id);
            if (!mapping_pause(mapping.id, 1))
                return ASTRA_NETWORK_RESOURCE_LIMIT;
            status = mapping_unmount(&mapping, &volume);
            if (status != ASTRA_NETWORK_OK &&
                status != ASTRA_NETWORK_IN_PROGRESS && !was_paused)
                (void)mapping_pause(mapping.id, 0);
        } else status = mapping_volume_state(&mapping, &volume);
        if (request->header.operation == ASTRA_NETWORK_MAPPING_MOUNT &&
            status == ASTRA_NETWORK_OK) {
            (void)mapping_pause(mapping.id, 0);
            mapping_reconcile_at = 0u;
        }
        if (volume.source.id != 0u &&
            (status == ASTRA_NETWORK_OK || status == ASTRA_NETWORK_BUSY ||
             status == ASTRA_NETWORK_IN_PROGRESS)) {
            (void)memcpy(bytes, &volume, sizeof(volume));
            reply->transferred = sizeof(volume);
        }
        return status;
    }
    default:
        return ASTRA_NETWORK_INVALID;
    }
}

static void process_request(uint32_t receive, int mode,
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
            open_session(&request, mode, handles, handle_count);
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
                request.header.operation == ASTRA_NETWORK_REVERSE_RESOLVE ||
                request.header.operation == ASTRA_NETWORK_CANCEL)) {
        reply.status = dispatch_resolve(session, &request, &reply);
    } else if (bound_endpoint == NULL &&
               (request.header.operation == ASTRA_NETWORK_INTERFACES ||
                request.header.operation == ASTRA_NETWORK_DEVICES)) {
        reply.status = dispatch_interfaces(session, &request, &reply);
    } else if (bound_endpoint == NULL &&
               request.header.operation >= ASTRA_NETWORK_ETHERNET_PROBE &&
               request.header.operation <= ASTRA_NETWORK_ETHERNET_SAVED) {
        reply.status = dispatch_ethernet(session, &request);
    } else if (bound_endpoint == NULL &&
               request.header.operation >= ASTRA_NETWORK_MAPPING_READ &&
               request.header.operation <= ASTRA_NETWORK_MAPPING_STATE) {
        reply.status = dispatch_mapping(session, &request, &reply);
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
                request.header.operation != ASTRA_NETWORK_REVERSE_RESOLVE &&
                request.header.operation != ASTRA_NETWORK_INTERFACES &&
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
    const AstraStartupCapability *device, *irq, *metadata, *administrator;
    AstraNetworkLeaseInfo lease;
    AstraDmaBufferInfo dma;
    uint32_t status;

    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_NETWORK_DEVICE);
    irq = astra_startup_capability(startup, ASTRA_CAPABILITY_NETWORK_IRQ);
    metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    administrator = astra_startup_capability(startup,
                                              ASTRA_CAPABILITY_VOLUME_ADMIN);
    if (device == NULL || irq == NULL || metadata == NULL ||
        administrator == NULL)
        return NETWORK_FAIL_DEVICE;
    volume_metadata = metadata->handle;
    volume_admin = administrator->handle;
    if (astra_config_open_system(startup, &network_schema,
            ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE,
            &network_config, NULL) != ASTRA_CONFIG_OK)
        return NETWORK_FAIL_DEVICE;
    mapping_store_ok = mapping_integrity();
    if (!mapping_store_ok)
        (void)astra_log_failure("network mapping store invalid",
                                ASTRA_NETWORK_IO);
    {
        uint32_t pending_count = 0u;
        if (astra_config_count(&network_config, "pending", &pending_count) !=
                ASTRA_CONFIG_OK)
            return NETWORK_FAIL_DEVICE;
        if (pending_count != 0u)
            ethernet_recovery_at = 0u;
    }
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
            &listen_receive, &listen_send) != ASTRA_SYSCALL_OK ||
        astra_rt_port_create(
            ASTRA_PORT_MESSAGES_MAX,
            ASTRA_PORT_MESSAGES_MAX * sizeof(AstraNetworkRequestMessage),
            &admin_receive, &admin_send) != ASTRA_SYSCALL_OK)
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
    uint32_t published[3], status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = network_start(startup);
    published[0] = outbound_send;
    published[1] = listen_send;
    published[2] = admin_send;
    if (astra_service_ready(bootstrap->handle, status, published, 3u) !=
            ASTRA_SYSCALL_OK && status == ASTRA_STATUS_OK)
        status = NETWORK_FAIL_READY;
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    mapping_reconcile_at = 0u;
    for (;;) {
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        NetworkSession *wait_sessions[ASTRA_WAIT_MULTIPLE_MAX];
        NetworkEndpoint *wait_endpoints[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t count = 4u, index = ASTRA_WAIT_INDEX_NONE;

        if (ethernet_recovery_at != ASTRA_DEADLINE_FOREVER &&
            astra_clock_monotonic() >= ethernet_recovery_at)
            ethernet_recover();
        if (mapping_reconcile_at != ASTRA_DEADLINE_FOREVER &&
            astra_clock_monotonic() >= mapping_reconcile_at)
            mapping_reconcile();

        waits[0] = outbound_receive;
        waits[1] = listen_receive;
        waits[2] = irq_handle;
        waits[3] = admin_receive;
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
        uint64_t deadline = ethernet_recovery_at < mapping_reconcile_at ?
            ethernet_recovery_at : mapping_reconcile_at;
        status = astra_wait_multiple(waits, count, deadline,
                                     &index, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT)
            continue;
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
        else if (index == 3u)
            process_request(admin_receive, 2, NULL, NULL);
        else if (index < count && wait_sessions[index] != NULL)
            process_request(waits[index], 0, wait_sessions[index], NULL);
        else if (index < count && wait_endpoints[index] != NULL)
            process_request(waits[index], 0, NULL, wait_endpoints[index]);
    }
}
