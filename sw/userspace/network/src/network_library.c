#include <astra/network_library.h>

#include <astra/bytes.h>
#include <astra/library.h>
#include <astra/network_core.h>
#include <astra/runtime.h>

ASTRA_LIBRARY("network.library", 1, 0, 0,
              ASTRA_NETWORK_LIBRARY_ABI_MAJOR,
              ASTRA_NETWORK_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

static void clear_bytes(void *memory, uint32_t size)
{
    (void)memset(memory, 0, size);
}

static void request_init(AstraNetworkSession *session,
                         AstraNetworkRequestMessage *request,
                         uint32_t operation)
{
    clear_bytes(request, sizeof(*request));
    ++session->_private_transaction;
    if (session->_private_transaction == 0u)
        ++session->_private_transaction;
    astra_message_header_set(&request->header, sizeof(*request),
                             ASTRA_NETWORK_PROTOCOL,
                             ASTRA_NETWORK_VERSION, operation,
                             session->_private_transaction);
    request->session = session->_private_id;
}

static int reply_valid(const AstraNetworkReplyMessage *reply,
                       uint32_t operation, uint32_t transaction,
                       uint32_t size)
{
    return size == sizeof(*reply) &&
           reply->header.total_size == sizeof(*reply) &&
           reply->header.header_size == sizeof(reply->header) &&
           reply->header.protocol == ASTRA_NETWORK_PROTOCOL &&
           reply->header.protocol_version == ASTRA_NETWORK_VERSION &&
           reply->header.operation == operation &&
           reply->header.transaction_id == transaction;
}

static AstraNetworkStatus session_lock(AstraNetworkSession *session)
{
    if (session == NULL || session->_private_lock == 0u)
        return ASTRA_NETWORK_INVALID;
    return astra_network_status_from_syscall(
        astra_wait_one(session->_private_lock, ASTRA_DEADLINE_FOREVER, NULL));
}

static void session_unlock(AstraNetworkSession *session)
{
    (void)astra_rt_signal(session->_private_lock, 1u, NULL);
}

static AstraNetworkStatus exchange_locked(
    AstraNetworkSession *session, uint32_t control,
    AstraNetworkRequestMessage *request, AstraNetworkReplyMessage *reply,
    uint32_t *received_handles, uint32_t handle_capacity,
    uint32_t *received_count)
{
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;

    if (received_count != NULL)
        *received_count = 0u;
    if (received_handles != NULL)
        clear_bytes(received_handles,
                    handle_capacity * (uint32_t)sizeof(*received_handles));
    status = astra_port_send(control, request, sizeof(*request), NULL, 0u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("network exchange send",
                                (1u << 16) | status);
        return astra_network_status_from_syscall(status);
    }
    status = astra_wait_one(session->_private_reply,
                            ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("network exchange wait",
                                (2u << 16) | status);
        (void)astra_log_failure("network reply handle",
                                session->_private_reply);
        return astra_network_status_from_syscall(status);
    }
    clear_bytes(reply, sizeof(*reply));
    status = astra_port_receive(
        session->_private_reply, reply, sizeof(*reply), received_handles,
        handle_capacity, &size, received_handles == NULL ? NULL : &count);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_log_failure("network exchange receive",
                                (3u << 16) | status);
        return astra_network_status_from_syscall(status);
    }
    if (!reply_valid(reply, request->header.operation,
                     request->header.transaction_id, size) ||
        reply->session != session->_private_id || count > handle_capacity) {
        uint32_t reason = !reply_valid(
            reply, request->header.operation,
            request->header.transaction_id, size) ? 1u :
            reply->session != session->_private_id ? 2u : 3u;

        (void)astra_log_failure("network exchange reply",
                                (reason << 16) | (count & UINT32_C(0xffff)));
        for (uint32_t index = 0u; index < count; ++index)
            (void)astra_close(received_handles[index]);
        return ASTRA_NETWORK_INVALID;
    }
    if (received_count != NULL)
        *received_count = count;
    return (AstraNetworkStatus)reply->status;
}

static AstraNetworkStatus exchange(AstraNetworkSession *session,
                                   uint32_t control,
                                   AstraNetworkRequestMessage *request,
                                   AstraNetworkReplyMessage *reply,
                                   uint32_t *received_handles,
                                   uint32_t handle_capacity,
                                   uint32_t *received_count)
{
    clear_bytes(reply, sizeof(*reply));
    AstraNetworkStatus status = session_lock(session);

    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure("network exchange lock", status);
        return status;
    }
    status = exchange_locked(session, control, request, reply,
                             received_handles, handle_capacity,
                             received_count);
    session_unlock(session);
    return status;
}

static void session_release(AstraNetworkSession *session)
{
    if (session->_private_shared != NULL)
        (void)astra_rt_area_unmap(session->_private_shared);
    if (session->_private_notify != 0u)
        (void)astra_close(session->_private_notify);
    if (session->_private_control != 0u)
        (void)astra_close(session->_private_control);
    if (session->_private_reply != 0u)
        (void)astra_close(session->_private_reply);
    if (session->_private_lock != 0u)
        (void)astra_close(session->_private_lock);
    if (session->_private_area != 0u)
        (void)astra_close(session->_private_area);
    clear_bytes(session, sizeof(*session));
}

static AstraNetworkStatus network_session_open(AstraHandle factory,
                                                AstraNetworkSession *session)
{
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    uint32_t reply_receive = 0u;
    uint32_t reply_source = 0u;
    uint32_t reply_send = 0u;
    uint32_t area_send = 0u;
    uint32_t handles[2];
    uint32_t received[2] = {0u, 0u};
    uint32_t size = 0u;
    uint32_t count = 0u;
    uint32_t status;
    uint32_t stage = 1u;

    if (factory == 0u || session == NULL)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(session, sizeof(*session));
    status = astra_rt_port_create(1u, sizeof(reply), &reply_receive,
                                  &reply_source);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    stage = 2u;
    status = astra_rt_handle_duplicate(
        reply_source, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                          ASTRA_RIGHT_TRANSFER, &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    stage = 3u;
    status = astra_rt_area_create_flagged(
        ASTRA_AREA_SIZE_MAX,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER,
        ASTRA_AREA_CREATE_RESERVED, &session->_private_area);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    stage = 4u;
    status = astra_rt_area_map(
        session->_private_area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
        &session->_private_shared, &session->_private_shared_size);
    if (status != ASTRA_SYSCALL_OK ||
        !astra_network_shared_initialize(session->_private_shared,
                                         session->_private_shared_size, 1u)) {
        status = ASTRA_SYSCALL_OUT_OF_MEMORY;
        goto fail;
    }
    stage = 5u;
    status = astra_rt_handle_duplicate(
        session->_private_area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        &area_send);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    clear_bytes(&request, sizeof(request));
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_NETWORK_PROTOCOL,
                             ASTRA_NETWORK_VERSION,
                             ASTRA_NETWORK_OPEN_SESSION, 1u);
    request.length = session->_private_shared_size;
    handles[0] = reply_send;
    handles[1] = area_send;
    stage = 6u;
    status = astra_port_send(factory, &request, sizeof(request), handles, 2u);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    reply_send = 0u;
    area_send = 0u;
    (void)astra_close(reply_source);
    reply_source = 0u;
    stage = 7u;
    status = astra_wait_one(reply_receive, ASTRA_DEADLINE_FOREVER, NULL);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    clear_bytes(&reply, sizeof(reply));
    stage = 8u;
    status = astra_port_receive(reply_receive, &reply, sizeof(reply), received,
                                2u, &size, &count);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    if (!reply_valid(&reply, ASTRA_NETWORK_OPEN_SESSION, 1u, size) ||
        reply.status != ASTRA_NETWORK_OK || reply.session == 0u ||
        reply.generation == 0u || count != 2u || received[0] == 0u ||
        received[1] == 0u) {
        stage = 9u;
        status = ASTRA_SYSCALL_IO_ERROR;
        goto fail;
    }
    stage = 10u;
    status = astra_rt_semaphore_create(
        1u, 1u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
        &session->_private_lock);
    if (status != ASTRA_SYSCALL_OK)
        goto fail;
    session->_private_control = received[1];
    received[1] = 0u;
    session->_private_factory = 0u;
    session->_private_reply = reply_receive;
    session->_private_notify = received[0];
    received[0] = 0u;
    session->_private_id = reply.session;
    session->_private_generation = reply.generation;
    session->_private_transaction = 1u;
    ((AstraNetworkSharedHeader *)session->_private_shared)->generation =
        reply.generation;
    return ASTRA_NETWORK_OK;

fail:
    (void)astra_log_failure("network session open",
                            (stage << 16) | (status & UINT32_C(0xffff)));
    if (received[0] != 0u)
        (void)astra_close(received[0]);
    if (received[1] != 0u)
        (void)astra_close(received[1]);
    if (area_send != 0u)
        (void)astra_close(area_send);
    if (reply_send != 0u)
        (void)astra_close(reply_send);
    if (reply_source != 0u)
        (void)astra_close(reply_source);
    if (reply_receive != 0u)
        (void)astra_close(reply_receive);
    session_release(session);
    return astra_network_status_from_syscall(status);
}

static AstraNetworkStatus network_session_close(AstraNetworkSession *session)
{
    if (session == NULL || session->_private_id == 0u)
        return ASTRA_NETWORK_INVALID;
    session_release(session);
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_endpoint_open(
    AstraNetworkSession *session, uint16_t family, uint8_t type,
    uint8_t protocol, AstraNetworkEndpoint *endpoint)
{
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;
    uint32_t handles[2] = {0u, 0u};
    uint32_t count = 0u;

    if (session == NULL || session->_private_id == 0u || endpoint == NULL)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(endpoint, sizeof(*endpoint));
    request_init(session, &request, ASTRA_NETWORK_OPEN_ENDPOINT);
    request.address.size = sizeof(request.address);
    request.address.family = family;
    request.flags = ((uint32_t)type << 8) | protocol;
    status = exchange(session, session->_private_control, &request, &reply,
                      handles, 2u, &count);
    if (status != ASTRA_NETWORK_OK) {
        (void)astra_log_failure(
            "network endpoint exchange",
            ((reply.value & UINT32_C(0xffff)) << 16) | status);
        return status;
    }
    if (reply.endpoint == 0u || reply.generation == 0u || count != 2u ||
        handles[0] == 0u || handles[1] == 0u) {
        (void)astra_log_failure("network endpoint reply", ASTRA_NETWORK_IO);
        if (handles[0] != 0u) (void)astra_close(handles[0]);
        if (handles[1] != 0u) (void)astra_close(handles[1]);
        return ASTRA_NETWORK_IO;
    }
    endpoint->_private_session = session;
    endpoint->_private_readiness = handles[0];
    endpoint->_private_control = handles[1];
    endpoint->_private_id = reply.endpoint;
    endpoint->_private_generation = reply.generation;
    endpoint->_private_family = family;
    endpoint->_private_type = type;
    endpoint->_private_protocol = protocol;
    return ASTRA_NETWORK_OK;
}

static void endpoint_request(AstraNetworkEndpoint *endpoint,
                             AstraNetworkRequestMessage *request,
                             uint32_t operation)
{
    request_init(endpoint->_private_session, request, operation);
    request->endpoint = endpoint->_private_id;
    request->generation = endpoint->_private_generation;
}

static AstraNetworkStatus endpoint_simple(AstraNetworkEndpoint *endpoint,
                                          uint32_t operation,
                                          const AstraNetworkAddress *address,
                                          uint32_t flags, uint32_t value,
                                          AstraNetworkReplyMessage *reply)
{
    AstraNetworkRequestMessage request;

    if (endpoint == NULL || endpoint->_private_session == NULL ||
        endpoint->_private_id == 0u)
        return ASTRA_NETWORK_INVALID;
    endpoint_request(endpoint, &request, operation);
    request.flags = flags;
    request.value = value;
    if (address != NULL)
        request.address = *address;
    return exchange(endpoint->_private_session, endpoint->_private_control,
                    &request, reply, NULL, 0u, NULL);
}

static AstraNetworkStatus network_endpoint_close(AstraNetworkEndpoint *endpoint)
{
    if (endpoint == NULL || endpoint->_private_id == 0u)
        return ASTRA_NETWORK_INVALID;
    if (endpoint->_private_control != 0u)
        (void)astra_close(endpoint->_private_control);
    if (endpoint->_private_readiness != 0u)
        (void)astra_close(endpoint->_private_readiness);
    clear_bytes(endpoint, sizeof(*endpoint));
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_bind(AstraNetworkEndpoint *endpoint,
                                       const AstraNetworkAddress *address)
{
    AstraNetworkReplyMessage reply;
    return endpoint_simple(endpoint, ASTRA_NETWORK_BIND, address, 0u, 0u,
                           &reply);
}

static AstraNetworkStatus network_connect(AstraNetworkEndpoint *endpoint,
                                          const AstraNetworkAddress *address)
{
    AstraNetworkReplyMessage reply;
    return endpoint_simple(endpoint, ASTRA_NETWORK_CONNECT, address, 0u, 0u,
                           &reply);
}

static AstraNetworkStatus network_listen(AstraNetworkEndpoint *endpoint,
                                         uint32_t backlog)
{
    AstraNetworkReplyMessage reply;
    return endpoint_simple(endpoint, ASTRA_NETWORK_LISTEN, NULL, 0u, backlog,
                           &reply);
}

static AstraNetworkStatus network_accept(AstraNetworkEndpoint *listener,
                                         AstraNetworkEndpoint *accepted,
                                         AstraNetworkAddress *address)
{
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;
    uint32_t handles[2] = {0u, 0u};
    uint32_t count = 0u;

    if (accepted == NULL)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(accepted, sizeof(*accepted));
    {
        AstraNetworkRequestMessage request;
        endpoint_request(listener, &request, ASTRA_NETWORK_ACCEPT);
        status = exchange(listener->_private_session,
                          listener->_private_control, &request, &reply,
                          handles, 2u, &count);
    }
    if (status != ASTRA_NETWORK_OK)
        return status;
    if (reply.endpoint == 0u || reply.generation == 0u || count != 2u ||
        handles[0] == 0u || handles[1] == 0u) {
        if (handles[0] != 0u) (void)astra_close(handles[0]);
        if (handles[1] != 0u) (void)astra_close(handles[1]);
        return ASTRA_NETWORK_IO;
    }
    accepted->_private_session = listener->_private_session;
    accepted->_private_readiness = handles[0];
    accepted->_private_control = handles[1];
    accepted->_private_id = reply.endpoint;
    accepted->_private_generation = reply.generation;
    accepted->_private_family = listener->_private_family;
    accepted->_private_type = listener->_private_type;
    accepted->_private_protocol = listener->_private_protocol;
    if (address != NULL)
        *address = reply.address;
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_transfer(
    AstraNetworkEndpoint *endpoint, uint32_t operation, void *buffer,
    size_t length, uint32_t flags, const AstraNetworkAddress *to,
    AstraNetworkAddress *from, size_t *transferred)
{
    AstraNetworkSession *session;
    AstraNetworkSharedHeader *header;
    AstraNetworkSharedSlot *slots;
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;
    uint32_t slot;
    uint32_t amount;
    uint8_t *bytes;

    if (endpoint == NULL || endpoint->_private_session == NULL ||
        transferred == NULL || (buffer == NULL && length != 0u))
        return ASTRA_NETWORK_INVALID;
    *transferred = 0u;
    if (length == 0u && endpoint->_private_type == ASTRA_NETWORK_TYPE_STREAM)
        return ASTRA_NETWORK_OK;
    session = endpoint->_private_session;
    status = session_lock(session);
    if (status != ASTRA_NETWORK_OK)
        return status;
    header = session->_private_shared;
    slots = astra_network_shared_slots(header);
    slot = operation == ASTRA_NETWORK_SEND ? 0u : header->tx_slot_count;
    amount = length > ASTRA_NETWORK_SLOT_BYTES ?
        ASTRA_NETWORK_SLOT_BYTES : (uint32_t)length;
    bytes = astra_network_shared_slot_bytes(header, slot);
    if (bytes == NULL) {
        session_unlock(session);
        return ASTRA_NETWORK_INVALID;
    }
    if (operation == ASTRA_NETWORK_SEND)
        (void)memcpy(bytes, buffer, amount);
    slots[slot].generation = session->_private_generation;
    slots[slot].state = operation == ASTRA_NETWORK_SEND ?
        ASTRA_NETWORK_SLOT_TX_READY : ASTRA_NETWORK_SLOT_RX_READING;
    slots[slot].endpoint = endpoint->_private_id;
    slots[slot].flags = flags;
    slots[slot].offset = 0u;
    slots[slot].length = amount;
    if (to != NULL)
        slots[slot].address = *to;
    endpoint_request(endpoint, &request, operation);
    request.flags = flags;
    request.slot = slot;
    request.length = amount;
    if (to != NULL)
        request.address = *to;
    status = exchange_locked(session, endpoint->_private_control, &request,
                             &reply, NULL, 0u, NULL);
    if (status == ASTRA_NETWORK_OK || status == ASTRA_NETWORK_PEER_CLOSED) {
        uint32_t copied = reply.transferred > amount ? amount :
                                                     reply.transferred;

        if (reply.transferred > amount &&
            !(operation == ASTRA_NETWORK_RECEIVE &&
              (flags & ASTRA_NETWORK_MESSAGE_TRUNCATE) != 0u)) {
            status = ASTRA_NETWORK_IO;
        } else {
            if (operation == ASTRA_NETWORK_RECEIVE && copied != 0u)
                (void)memcpy(buffer, bytes, copied);
            *transferred = reply.transferred;
            if (from != NULL)
                *from = reply.address;
        }
    }
    clear_bytes(&slots[slot], sizeof(slots[slot]));
    session_unlock(session);
    return status;
}

static AstraNetworkStatus network_send(AstraNetworkEndpoint *endpoint,
                                       const void *buffer, size_t length,
                                       uint32_t flags, size_t *transferred)
{
    return network_transfer(endpoint, ASTRA_NETWORK_SEND,
                            (void *)(uintptr_t)buffer, length, flags, NULL,
                            NULL, transferred);
}

static AstraNetworkStatus network_send_to(
    AstraNetworkEndpoint *endpoint, const void *buffer, size_t length,
    uint32_t flags, const AstraNetworkAddress *address, size_t *transferred)
{
    if (address == NULL)
        return ASTRA_NETWORK_INVALID;
    return network_transfer(endpoint, ASTRA_NETWORK_SEND,
                            (void *)(uintptr_t)buffer, length, flags, address,
                            NULL, transferred);
}

static AstraNetworkStatus network_receive(AstraNetworkEndpoint *endpoint,
                                          void *buffer, size_t capacity,
                                          uint32_t flags,
                                          size_t *transferred)
{
    return network_transfer(endpoint, ASTRA_NETWORK_RECEIVE, buffer, capacity,
                            flags, NULL, NULL, transferred);
}

static AstraNetworkStatus network_receive_from(
    AstraNetworkEndpoint *endpoint, void *buffer, size_t capacity,
    uint32_t flags, AstraNetworkAddress *address, size_t *transferred)
{
    return network_transfer(endpoint, ASTRA_NETWORK_RECEIVE, buffer, capacity,
                            flags, NULL, address, transferred);
}

static AstraNetworkStatus network_shutdown(AstraNetworkEndpoint *endpoint,
                                           uint32_t flags)
{
    AstraNetworkReplyMessage reply;
    return endpoint_simple(endpoint, ASTRA_NETWORK_SHUTDOWN, NULL, flags, 0u,
                           &reply);
}

static AstraNetworkStatus endpoint_address(AstraNetworkEndpoint *endpoint,
                                           uint32_t operation,
                                           AstraNetworkAddress *address)
{
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;

    if (address == NULL)
        return ASTRA_NETWORK_INVALID;
    status = endpoint_simple(endpoint, operation, NULL, 0u, 0u, &reply);
    if (status == ASTRA_NETWORK_OK)
        *address = reply.address;
    return status;
}

static AstraNetworkStatus network_local_address(AstraNetworkEndpoint *endpoint,
                                                AstraNetworkAddress *address)
{
    return endpoint_address(endpoint, ASTRA_NETWORK_GET_LOCAL_ADDRESS,
                            address);
}

static AstraNetworkStatus network_peer_address(AstraNetworkEndpoint *endpoint,
                                               AstraNetworkAddress *address)
{
    return endpoint_address(endpoint, ASTRA_NETWORK_GET_PEER_ADDRESS, address);
}

static AstraNetworkStatus network_get_option(AstraNetworkEndpoint *endpoint,
                                             uint32_t option,
                                             uint32_t *value)
{
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;

    if (value == NULL)
        return ASTRA_NETWORK_INVALID;
    status = endpoint_simple(endpoint, ASTRA_NETWORK_GET_OPTION, NULL, 0u,
                             option, &reply);
    if (status == ASTRA_NETWORK_OK)
        *value = reply.value;
    return status;
}

static AstraNetworkStatus network_set_option(AstraNetworkEndpoint *endpoint,
                                             uint32_t option,
                                             uint32_t value)
{
    AstraNetworkReplyMessage reply;
    return endpoint_simple(endpoint, ASTRA_NETWORK_SET_OPTION, NULL, value,
                           option, &reply);
}

static uint32_t bounded_length(const char *text, uint32_t limit)
{
    uint32_t length = 0u;

    if (text == NULL)
        return UINT32_MAX;
    while (length <= limit && text[length] != '\0')
        ++length;
    return length;
}

static AstraNetworkStatus network_resolve_start(
    AstraNetworkSession *session, const char *name, uint32_t port,
    uint16_t family, uint8_t type, uint8_t protocol,
    AstraNetworkRequest *pending)
{
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    AstraNetworkSharedHeader *header;
    AstraNetworkSharedSlot *slots;
    AstraNetworkStatus status;
    uint32_t length;
    uint8_t *bytes;

    if (session == NULL || session->_private_id == 0u || pending == NULL ||
        port > UINT16_MAX)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(pending, sizeof(*pending));
    length = bounded_length(name, ASTRA_NETWORK_NAME_MAX);
    if (length == 0u || length > ASTRA_NETWORK_NAME_MAX)
        return ASTRA_NETWORK_INVALID;
    status = session_lock(session);
    if (status != ASTRA_NETWORK_OK)
        return status;
    header = session->_private_shared;
    slots = astra_network_shared_slots(header);
    bytes = astra_network_shared_slot_bytes(header, 0u);
    (void)memcpy(bytes, name, length);
    slots[0].generation = session->_private_generation;
    slots[0].state = ASTRA_NETWORK_SLOT_TX_READY;
    slots[0].length = length;
    request_init(session, &request, ASTRA_NETWORK_RESOLVE);
    request.flags = ((uint32_t)type << 8) | protocol;
    request.address.size = sizeof(request.address);
    request.address.family = family;
    request.address.port = (uint16_t)port;
    request.slot = 0u;
    request.length = length;
    status = exchange_locked(session, session->_private_control, &request,
                             &reply, NULL, 0u, NULL);
    clear_bytes(&slots[0], sizeof(slots[0]));
    session_unlock(session);
    if (status != ASTRA_NETWORK_IN_PROGRESS || reply.value == 0u)
        return status;
    pending->_private_session = session;
    pending->_private_token = reply.value;
    pending->_private_transaction = request.header.transaction_id;
    pending->_private_generation = session->_private_generation;
    pending->_private_state = 1u;
    return ASTRA_NETWORK_IN_PROGRESS;
}

static AstraNetworkStatus network_request_try(
    AstraNetworkRequest *pending, AstraNetworkAddress *addresses,
    uint32_t capacity, uint32_t *count)
{
    AstraNetworkSession *session;
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    AstraNetworkSharedHeader *header;
    AstraNetworkSharedSlot *slots;
    AstraNetworkStatus status;
    uint32_t slot;
    uint32_t bytes_capacity;
    uint8_t *bytes;

    if (pending == NULL || pending->_private_state == 0u || count == NULL ||
        (capacity != 0u && addresses == NULL))
        return ASTRA_NETWORK_INVALID;
    *count = 0u;
    session = pending->_private_session;
    status = session_lock(session);
    if (status != ASTRA_NETWORK_OK)
        return status;
    header = session->_private_shared;
    slots = astra_network_shared_slots(header);
    slot = header->tx_slot_count;
    bytes = astra_network_shared_slot_bytes(header, slot);
    bytes_capacity = capacity > ASTRA_NETWORK_SLOT_BYTES /
                                    sizeof(AstraNetworkAddress) ?
        ASTRA_NETWORK_SLOT_BYTES : capacity * sizeof(AstraNetworkAddress);
    slots[slot].generation = session->_private_generation;
    slots[slot].state = ASTRA_NETWORK_SLOT_RX_READING;
    slots[slot].length = bytes_capacity;
    request_init(session, &request, ASTRA_NETWORK_RESOLVE);
    request.value = pending->_private_token;
    request.slot = slot;
    request.length = bytes_capacity;
    status = exchange_locked(session, session->_private_control, &request,
                             &reply, NULL, 0u, NULL);
    if (status == ASTRA_NETWORK_OK) {
        if (reply.transferred > capacity ||
            reply.transferred > ASTRA_NETWORK_SLOT_BYTES /
                                    sizeof(AstraNetworkAddress)) {
            status = ASTRA_NETWORK_IO;
        } else {
            *count = reply.transferred;
            if (reply.transferred != 0u)
                (void)memcpy(addresses, bytes,
                             reply.transferred * sizeof(*addresses));
            pending->_private_state = 0u;
        }
    } else if (status != ASTRA_NETWORK_WOULD_BLOCK &&
               status != ASTRA_NETWORK_BUFFER_TOO_SMALL) {
        pending->_private_state = 0u;
    }
    if (status == ASTRA_NETWORK_BUFFER_TOO_SMALL)
        *count = reply.transferred;
    clear_bytes(&slots[slot], sizeof(slots[slot]));
    session_unlock(session);
    return status;
}

static AstraNetworkStatus network_request_wait(
    AstraNetworkRequest *pending, AstraNetworkAddress *addresses,
    uint32_t capacity, uint32_t *count, uint64_t deadline)
{
    AstraNetworkStatus status;

    for (;;) {
        status = network_request_try(pending, addresses, capacity, count);
        if (status != ASTRA_NETWORK_WOULD_BLOCK)
            return status;
        status = astra_network_status_from_syscall(
            astra_wait_one(pending->_private_session->_private_notify,
                           deadline, NULL));
        if (status != ASTRA_NETWORK_OK)
            return status;
    }
}

static AstraNetworkStatus network_request_cancel(AstraNetworkRequest *pending)
{
    AstraNetworkRequestMessage request;
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;

    if (pending == NULL || pending->_private_state == 0u)
        return ASTRA_NETWORK_INVALID;
    request_init(pending->_private_session, &request, ASTRA_NETWORK_CANCEL);
    request.value = pending->_private_token;
    status = exchange(pending->_private_session,
                      pending->_private_session->_private_control,
                      &request, &reply, NULL, 0u, NULL);
    pending->_private_state = 0u;
    return status;
}

static AstraHandle network_readiness_handle(const AstraNetworkEndpoint *endpoint)
{
    return endpoint == NULL ? 0u : endpoint->_private_readiness;
}

static uint32_t network_readiness(const AstraNetworkEndpoint *constant_endpoint)
{
    AstraNetworkEndpoint *endpoint = (AstraNetworkEndpoint *)constant_endpoint;
    AstraNetworkReplyMessage reply;
    AstraNetworkStatus status;

    status = endpoint_simple(
        endpoint, ASTRA_NETWORK_DOORBELL, NULL,
        ASTRA_NETWORK_READY_READABLE | ASTRA_NETWORK_READY_WRITABLE |
            ASTRA_NETWORK_READY_CONNECTED | ASTRA_NETWORK_READY_ACCEPTABLE,
        0u, &reply);
    return status == ASTRA_NETWORK_OK ? reply.readiness : 0u;
}

static AstraNetworkStatus network_session_export(
    const AstraNetworkSession *session, AstraNetworkSessionState *state)
{
    if (session == NULL || state == NULL || session->_private_id == 0u ||
        session->_private_control == 0u || session->_private_area == 0u ||
        session->_private_lock == 0u || session->_private_reply == 0u ||
        session->_private_notify == 0u ||
        !astra_network_shared_valid(session->_private_shared,
                                    session->_private_shared_size,
                                    session->_private_generation))
        return ASTRA_NETWORK_INVALID;
    clear_bytes(state, sizeof(*state));
    state->size = sizeof(*state);
    state->control = session->_private_control;
    state->area = session->_private_area;
    state->lock = session->_private_lock;
    state->reply = session->_private_reply;
    state->notify = session->_private_notify;
    state->shared_size = session->_private_shared_size;
    state->id = session->_private_id;
    state->generation = session->_private_generation;
    state->transaction = session->_private_transaction;
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_session_import(
    const AstraNetworkSessionState *state, AstraNetworkSession *session)
{
    uint32_t mapped = 0u;
    uint32_t status;

    if (state == NULL || session == NULL || state->size != sizeof(*state) ||
        state->control == 0u || state->area == 0u || state->lock == 0u ||
        state->reply == 0u || state->notify == 0u ||
        state->shared_size == 0u || state->id == 0u ||
        state->generation == 0u)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(session, sizeof(*session));
    status = astra_rt_area_map(state->area,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &session->_private_shared, &mapped);
    if (status != ASTRA_SYSCALL_OK)
        return astra_network_status_from_syscall(status);
    if (mapped != state->shared_size ||
        !astra_network_shared_valid(session->_private_shared, mapped,
                                    state->generation)) {
        (void)astra_rt_area_unmap(session->_private_shared);
        clear_bytes(session, sizeof(*session));
        return ASTRA_NETWORK_INVALID;
    }
    session->_private_control = state->control;
    session->_private_area = state->area;
    session->_private_lock = state->lock;
    session->_private_reply = state->reply;
    session->_private_notify = state->notify;
    session->_private_shared_size = mapped;
    session->_private_id = state->id;
    session->_private_generation = state->generation;
    session->_private_transaction = state->transaction;
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_endpoint_export(
    const AstraNetworkEndpoint *endpoint, AstraNetworkEndpointState *state)
{
    if (endpoint == NULL || state == NULL ||
        endpoint->_private_session == NULL || endpoint->_private_control == 0u ||
        endpoint->_private_readiness == 0u || endpoint->_private_id == 0u ||
        endpoint->_private_generation == 0u)
        return ASTRA_NETWORK_INVALID;
    clear_bytes(state, sizeof(*state));
    state->size = sizeof(*state);
    state->control = endpoint->_private_control;
    state->readiness = endpoint->_private_readiness;
    state->id = endpoint->_private_id;
    state->generation = endpoint->_private_generation;
    state->family = endpoint->_private_family;
    state->type = endpoint->_private_type;
    state->protocol = endpoint->_private_protocol;
    return ASTRA_NETWORK_OK;
}

static AstraNetworkStatus network_endpoint_import(
    AstraNetworkSession *session, const AstraNetworkEndpointState *state,
    AstraNetworkEndpoint *endpoint)
{
    if (session == NULL || session->_private_id == 0u || state == NULL ||
        endpoint == NULL || state->size != sizeof(*state) ||
        state->control == 0u || state->readiness == 0u || state->id == 0u ||
        state->generation == 0u ||
        (state->family != ASTRA_NETWORK_FAMILY_IPV4 &&
         state->family != ASTRA_NETWORK_FAMILY_IPV6) ||
        (state->type != ASTRA_NETWORK_TYPE_STREAM &&
         state->type != ASTRA_NETWORK_TYPE_DATAGRAM))
        return ASTRA_NETWORK_INVALID;
    clear_bytes(endpoint, sizeof(*endpoint));
    endpoint->_private_session = session;
    endpoint->_private_control = state->control;
    endpoint->_private_readiness = state->readiness;
    endpoint->_private_id = state->id;
    endpoint->_private_generation = state->generation;
    endpoint->_private_family = state->family;
    endpoint->_private_type = state->type;
    endpoint->_private_protocol = state->protocol;
    return ASTRA_NETWORK_OK;
}

const AstraNetworkLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_NETWORK_LIBRARY_ABI_MAJOR,
    ASTRA_NETWORK_LIBRARY_ABI_MINOR,
    sizeof(AstraNetworkLibraryV1),
    network_session_open,
    network_session_close,
    network_endpoint_open,
    network_endpoint_close,
    network_bind,
    network_connect,
    network_listen,
    network_accept,
    network_send,
    network_send_to,
    network_receive,
    network_receive_from,
    network_shutdown,
    network_local_address,
    network_peer_address,
    network_get_option,
    network_set_option,
    network_resolve_start,
    network_request_try,
    network_request_wait,
    network_request_cancel,
    network_readiness_handle,
    network_readiness,
    network_session_export,
    network_session_import,
    network_endpoint_export,
    network_endpoint_import,
};
