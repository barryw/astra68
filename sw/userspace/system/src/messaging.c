#include <astra/messaging.h>
#include <astra/port.h>
#include <astra/resource.h>
#include <astra/result.h>

#include <string.h>

static AstraResult name_copy(char *target, uint32_t capacity, const char *source)
{
    if (source == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    size_t length = strlen(source);
    if (length >= capacity) return ASTRA_ERROR_NAME_TOO_LONG;
    memcpy(target, source, length + 1u);
    return ASTRA_OK;
}

static AstraResult exchange(AstraHandle broker, AstraMessagingRequest *request,
    AstraHandle *sent, AstraMessagingReply *reply, AstraHandle *endpoint,
    AstraMonotonicDeadline deadline)
{
    /* Port calls require four-byte aligned buffers on MC68040. */
    _Alignas(4) AstraMessagingRequest aligned_request;
    _Alignas(4) AstraMessagingReply local_reply = {0};
    _Alignas(4) AstraHandle transferred = sent != NULL ? *sent :
        ASTRA_INVALID_HANDLE;
    _Alignas(4) AstraHandle received = ASTRA_INVALID_HANDLE;
    AstraCall call = {0};
    AstraResult result;
    if (broker == ASTRA_INVALID_HANDLE || request == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    aligned_request = *request;
    result = astra_message_header_init(&aligned_request.header,
        sizeof(aligned_request),
        ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION,
        aligned_request.header.operation,
        aligned_request.header.transaction_id != 0u ?
            aligned_request.header.transaction_id : 1u);
    if (result != ASTRA_OK) return result;
    call.request = &aligned_request;
    call.request_size = sizeof(aligned_request);
    call.handles = sent != NULL ? &transferred : NULL;
    call.handle_count = sent != NULL ? 1u : 0u;
    call.reply_index = 0u;
    call.reply = &local_reply;
    call.reply_capacity = sizeof(local_reply);
    call.reply_handles = &received;
    call.reply_handle_capacity = 1u;
    result = astra_port_call(broker, &call, deadline);
    if (sent != NULL) *sent = transferred;
    if (result != ASTRA_OK) goto done;
    if (call.reply_size != sizeof(local_reply) ||
        local_reply.header.total_size != sizeof(local_reply) ||
        local_reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        local_reply.header.protocol != ASTRA_MESSAGING_PROTOCOL ||
        local_reply.header.protocol_version != ASTRA_MESSAGING_VERSION ||
        local_reply.header.operation != ASTRA_MESSAGING_REPLY ||
        local_reply.header.transaction_id !=
            aligned_request.header.transaction_id ||
        local_reply.header.flags != 0u || local_reply.header.reserved != 0u ||
        (local_reply.status == ASTRA_STATUS_OK &&
         aligned_request.header.operation == ASTRA_MESSAGING_LOOKUP ?
            call.reply_handle_count != 1u : call.reply_handle_count != 0u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_result_from_service(local_reply.status);
    if (result == ASTRA_OK && reply != NULL) *reply = local_reply;
    if (result == ASTRA_OK && endpoint != NULL) {
        *endpoint = received;
        received = ASTRA_INVALID_HANDLE;
    }
done:
    if (received != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&received);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

AstraResult astra_messaging_identify(AstraHandle broker,
    AstraMessagingReply *identity)
{
    AstraMessagingRequest request = {0};
    if (identity == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_IDENTIFY;
    return exchange(broker, &request, NULL, identity, NULL,
        ASTRA_DEADLINE_INFINITE);
}

AstraResult astra_messaging_list(AstraHandle broker, uint64_t cursor,
    AstraMessagingReply *entry)
{
    AstraMessagingRequest request = {0};
    if (entry == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_LIST;
    request.cursor = cursor;
    return exchange(broker, &request, NULL, entry, NULL,
        ASTRA_DEADLINE_INFINITE);
}

AstraResult astra_messaging_lookup(AstraHandle broker, uint32_t kind,
    const char *name, uint32_t process_id, AstraMessagingReply *entry,
    AstraHandle *endpoint)
{
    AstraMessagingRequest request = {0};
    AstraResult result;
    if (entry == NULL || endpoint == NULL || kind < ASTRA_MESSAGING_SERVICE ||
        kind > ASTRA_MESSAGING_COMMAND)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *endpoint = ASTRA_INVALID_HANDLE;
    result = name_copy(request.name, sizeof(request.name), name);
    if (result != ASTRA_OK) return result;
    if (kind != ASTRA_MESSAGING_COMMAND && request.name[0] == '\0')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_LOOKUP;
    request.kind = kind;
    request.process_id = process_id;
    return exchange(broker, &request, NULL, entry, endpoint,
        ASTRA_DEADLINE_INFINITE);
}

AstraResult astra_messaging_register(AstraHandle broker, AstraHandle *endpoint)
{
    AstraMessagingRequest request = {0};
    if (endpoint == NULL || *endpoint == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_REGISTER;
    return exchange(broker, &request, endpoint, NULL, NULL,
        ASTRA_DEADLINE_INFINITE);
}

AstraResult astra_messaging_unregister(AstraHandle broker)
{
    AstraMessagingRequest request = {0};
    request.header.operation = ASTRA_MESSAGING_UNREGISTER;
    return exchange(broker, &request, NULL, NULL, NULL,
        ASTRA_DEADLINE_INFINITE);
}

AstraResult astra_messaging_subscribe(AstraHandle broker, uint32_t kind,
    const char *name, uint32_t process_id, const char *topic,
    AstraHandle *event_send, uint64_t *subscription)
{
    AstraMessagingRequest request = {0};
    AstraMessagingReply reply = {0};
    AstraResult result;
    if (event_send == NULL || *event_send == ASTRA_INVALID_HANDLE ||
        subscription == NULL || kind < ASTRA_MESSAGING_SERVICE ||
        kind > ASTRA_MESSAGING_COMMAND)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = name_copy(request.name, sizeof(request.name), name);
    if (result != ASTRA_OK) return result;
    result = name_copy(request.topic, sizeof(request.topic), topic);
    if (result != ASTRA_OK) return result;
    if (request.topic[0] == '\0' ||
        (kind == ASTRA_MESSAGING_COMMAND ? process_id == 0u :
            request.name[0] == '\0'))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_SUBSCRIBE;
    request.kind = kind;
    request.process_id = process_id;
    result = exchange(broker, &request, event_send, &reply, NULL,
        ASTRA_DEADLINE_INFINITE);
    if (result == ASTRA_OK) *subscription = reply.subscription;
    return result;
}

AstraResult astra_messaging_unsubscribe(AstraHandle broker,
    uint64_t subscription)
{
    AstraMessagingRequest request = {0};
    if (subscription == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_UNSUBSCRIBE;
    request.subscription = subscription;
    return exchange(broker, &request, NULL, NULL, NULL,
        ASTRA_DEADLINE_INFINITE);
}

static AstraResult prepare_publish(AstraMessagingRequest *request,
    const char *topic, const void *payload, uint32_t payload_size,
    uint32_t transaction_id, uint32_t qos)
{
    AstraResult result;
    if ((payload_size != 0u && payload == NULL) ||
        payload_size > ASTRA_MESSAGING_PAYLOAD_MAX || transaction_id == 0u ||
        qos > ASTRA_MESSAGING_QOS_RELIABLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = name_copy(request->topic, sizeof(request->topic), topic);
    if (result != ASTRA_OK) return result;
    if (request->topic[0] == '\0') return ASTRA_ERROR_INVALID_ARGUMENT;
    request->header.operation = ASTRA_MESSAGING_PUBLISH;
    request->header.transaction_id = transaction_id;
    request->payload_size = payload_size;
    request->qos = qos;
    if (payload_size != 0u) memcpy(request->payload, payload, payload_size);
    return ASTRA_OK;
}

AstraResult astra_messaging_publish(AstraHandle broker, const char *topic,
    const void *payload, uint32_t payload_size, uint32_t transaction_id,
    AstraHandle *response_send, uint32_t *delivered, uint32_t *dropped)
{
    AstraMessagingRequest request = {0};
    AstraMessagingReply reply = {0};
    if ((response_send != NULL && *response_send == ASTRA_INVALID_HANDLE) ||
        delivered == NULL || dropped == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = prepare_publish(&request, topic, payload,
        payload_size, transaction_id, ASTRA_MESSAGING_QOS_BEST_EFFORT);
    if (result != ASTRA_OK) return result;
    result = exchange(broker, &request, response_send, &reply, NULL,
        ASTRA_DEADLINE_INFINITE);
    if (result == ASTRA_OK) {
        *delivered = reply.delivered;
        *dropped = reply.dropped;
    }
    return result;
}

AstraResult astra_messaging_publish_reliable(AstraHandle broker,
    const char *topic, const void *payload, uint32_t payload_size,
    uint32_t transaction_id, AstraMonotonicDeadline deadline,
    uint32_t *queued)
{
    AstraMessagingRequest request = {0};
    AstraMessagingReply reply = {0};
    if (queued == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = prepare_publish(&request, topic, payload,
        payload_size, transaction_id, ASTRA_MESSAGING_QOS_RELIABLE);
    if (result != ASTRA_OK) return result;
    result = exchange(broker, &request, NULL, &reply, NULL, deadline);
    if (result == ASTRA_OK) *queued = reply.delivered;
    return result;
}

AstraResult astra_messaging_ack(AstraHandle broker, uint64_t subscription,
    uint64_t sequence, AstraMonotonicDeadline deadline)
{
    AstraMessagingRequest request = {0};
    if (subscription == 0u || sequence == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_ACK;
    request.subscription = subscription;
    request.cursor = sequence;
    return exchange(broker, &request, NULL, NULL, NULL, deadline);
}

AstraResult astra_messaging_post(AstraHandle broker, const char *topic,
    const void *payload, uint32_t payload_size, uint32_t transaction_id)
{
    _Alignas(4) AstraMessagingRequest request = {0};
    if (broker == ASTRA_INVALID_HANDLE) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = prepare_publish(&request, topic, payload,
        payload_size, transaction_id, ASTRA_MESSAGING_QOS_BEST_EFFORT);
    if (result != ASTRA_OK) return result;
    result = astra_message_header_init(&request.header, sizeof(request),
        ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION,
        ASTRA_MESSAGING_PUBLISH, transaction_id);
    if (result != ASTRA_OK) return result;
    return astra_port_send_try(broker, &request, sizeof(request), NULL, 0u);
}

AstraResult astra_messaging_event_try(AstraHandle receive,
    AstraMessagingEvent *event, AstraHandle *response_send)
{
    _Alignas(4) AstraMessagingEvent wire = {0};
    _Alignas(4) AstraHandle incoming = ASTRA_INVALID_HANDLE;
    uint32_t size = 0u, count = 0u;
    AstraResult result;
    if (event == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (response_send != NULL) *response_send = ASTRA_INVALID_HANDLE;
    result = astra_port_receive_try(receive, &wire, sizeof(wire),
        &incoming, 1u, &size, &count);
    if (result != ASTRA_OK) return result;
    if (size != sizeof(wire) || wire.header.total_size != size ||
        wire.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        wire.header.protocol != ASTRA_MESSAGING_PROTOCOL ||
        wire.header.protocol_version != ASTRA_MESSAGING_VERSION ||
        (wire.header.operation != ASTRA_MESSAGING_EVENT &&
         wire.header.operation != ASTRA_MESSAGING_RELIABLE_EVENT) ||
        wire.header.flags != 0u || wire.header.reserved != 0u ||
        wire.payload_size > ASTRA_MESSAGING_PAYLOAD_MAX ||
        memchr(wire.name, '\0', sizeof(wire.name)) == NULL ||
        memchr(wire.topic, '\0', sizeof(wire.topic)) == NULL ||
        count > 1u || (wire.header.operation ==
            ASTRA_MESSAGING_RELIABLE_EVENT && count != 0u))
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) *event = wire;
    if (result == ASTRA_OK && response_send != NULL && count == 1u) {
        *response_send = incoming;
        incoming = ASTRA_INVALID_HANDLE;
    }
    if (incoming != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&incoming);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}
