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
    AstraHandle *sent, AstraMessagingReply *reply, AstraHandle *endpoint)
{
    AstraMessagingReply local_reply = {0};
    AstraHandle received = ASTRA_INVALID_HANDLE;
    AstraCall call = {0};
    AstraResult result;
    if (broker == ASTRA_INVALID_HANDLE || request == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_message_header_init(&request->header, sizeof(*request),
        ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION,
        request->header.operation,
        request->header.transaction_id != 0u ?
            request->header.transaction_id : 1u);
    if (result != ASTRA_OK) return result;
    call.request = request;
    call.request_size = sizeof(*request);
    call.handles = sent;
    call.handle_count = sent != NULL ? 1u : 0u;
    call.reply_index = 0u;
    call.reply = &local_reply;
    call.reply_capacity = sizeof(local_reply);
    call.reply_handles = &received;
    call.reply_handle_capacity = 1u;
    result = astra_port_call(broker, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK) goto done;
    if (call.reply_size != sizeof(local_reply) ||
        local_reply.header.total_size != sizeof(local_reply) ||
        local_reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        local_reply.header.protocol != ASTRA_MESSAGING_PROTOCOL ||
        local_reply.header.protocol_version != ASTRA_MESSAGING_VERSION ||
        local_reply.header.operation != ASTRA_MESSAGING_REPLY ||
        local_reply.header.transaction_id != request->header.transaction_id ||
        local_reply.header.flags != 0u || local_reply.header.reserved != 0u ||
        (local_reply.status == ASTRA_STATUS_OK &&
         request->header.operation == ASTRA_MESSAGING_LOOKUP ?
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
    return exchange(broker, &request, NULL, identity, NULL);
}

AstraResult astra_messaging_list(AstraHandle broker, uint64_t cursor,
    AstraMessagingReply *entry)
{
    AstraMessagingRequest request = {0};
    if (entry == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_LIST;
    request.cursor = cursor;
    return exchange(broker, &request, NULL, entry, NULL);
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
    return exchange(broker, &request, NULL, entry, endpoint);
}

AstraResult astra_messaging_register(AstraHandle broker, AstraHandle *endpoint)
{
    AstraMessagingRequest request = {0};
    if (endpoint == NULL || *endpoint == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_REGISTER;
    return exchange(broker, &request, endpoint, NULL, NULL);
}

AstraResult astra_messaging_unregister(AstraHandle broker)
{
    AstraMessagingRequest request = {0};
    request.header.operation = ASTRA_MESSAGING_UNREGISTER;
    return exchange(broker, &request, NULL, NULL, NULL);
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
            request.name[0] == '\0')) return ASTRA_ERROR_INVALID_ARGUMENT;
    request.header.operation = ASTRA_MESSAGING_SUBSCRIBE;
    request.kind = kind;
    request.process_id = process_id;
    result = exchange(broker, &request, event_send, &reply, NULL);
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
    return exchange(broker, &request, NULL, NULL, NULL);
}

static AstraResult prepare_publish(AstraMessagingRequest *request,
    const char *topic, const void *payload, uint32_t payload_size,
    uint32_t transaction_id)
{
    AstraResult result;
    if ((payload_size != 0u && payload == NULL) ||
        payload_size > ASTRA_MESSAGING_PAYLOAD_MAX || transaction_id == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = name_copy(request->topic, sizeof(request->topic), topic);
    if (result != ASTRA_OK) return result;
    if (request->topic[0] == '\0') return ASTRA_ERROR_INVALID_ARGUMENT;
    request->header.operation = ASTRA_MESSAGING_PUBLISH;
    request->header.transaction_id = transaction_id;
    request->payload_size = payload_size;
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
        payload_size, transaction_id);
    if (result != ASTRA_OK) return result;
    result = exchange(broker, &request, response_send, &reply, NULL);
    if (result == ASTRA_OK) {
        *delivered = reply.delivered;
        *dropped = reply.dropped;
    }
    return result;
}

AstraResult astra_messaging_post(AstraHandle broker, const char *topic,
    const void *payload, uint32_t payload_size, uint32_t transaction_id)
{
    AstraMessagingRequest request = {0};
    if (broker == ASTRA_INVALID_HANDLE) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = prepare_publish(&request, topic, payload,
        payload_size, transaction_id);
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
    AstraHandle incoming = ASTRA_INVALID_HANDLE;
    uint32_t size = 0u, count = 0u;
    AstraResult result;
    if (event == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (response_send != NULL) *response_send = ASTRA_INVALID_HANDLE;
    result = astra_port_receive_try(receive, event, sizeof(*event),
        &incoming, 1u, &size, &count);
    if (result != ASTRA_OK) return result;
    if (size != sizeof(*event) || event->header.total_size != size ||
        event->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        event->header.protocol != ASTRA_MESSAGING_PROTOCOL ||
        event->header.protocol_version != ASTRA_MESSAGING_VERSION ||
        event->header.operation != ASTRA_MESSAGING_EVENT ||
        event->header.flags != 0u || event->header.reserved != 0u ||
        event->payload_size > ASTRA_MESSAGING_PAYLOAD_MAX ||
        memchr(event->name, '\0', sizeof(event->name)) == NULL ||
        memchr(event->topic, '\0', sizeof(event->topic)) == NULL ||
        count > 1u) result = ASTRA_ERROR_IO;
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
