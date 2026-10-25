#include <astra/service_manager.h>
#include <astra/service_definition_payload.h>
#include <astra/service_payload.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include "internal/status.h"
#include <string.h>

static AstraResult exchange(AstraHandle manager, uint32_t operation,
    const char *name, uint32_t value, const AstraServiceListCursor *cursor,
    AstraHandle *sent, uint32_t payload_size, AstraServiceInfo *info,
    AstraServiceDefinition *definition, AstraServiceListCursor *next_cursor)
{
    AstraServiceManagerRequest request = {0};
    AstraServiceManagerReply reply = {0};
    AstraCall call = {0};
    AstraHandle received = ASTRA_INVALID_HANDLE;
    char *bytes = NULL;
    AstraServiceInfo decoded = ASTRA_SERVICE_INFO_INIT;
    AstraResult result;
    if (manager == ASTRA_INVALID_HANDLE || (name != NULL && !astra_service_name_valid(name)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (name != NULL) strcpy(request.name, name);
    if (cursor != NULL) request.cursor = *cursor;
    request.value = value; request.payload_size = payload_size;
    result = astra_message_header_init(&request.header, sizeof(request),
        ASTRA_SERVICE_MANAGER_PROTOCOL, ASTRA_SERVICE_MANAGER_VERSION, operation, 1u);
    if (result != ASTRA_OK) return result;
    call.request = &request; call.request_size = sizeof(request);
    call.handles = sent; call.handle_count = sent != NULL ? 1u : 0u;
    call.reply_index = 0u;
    call.reply = &reply; call.reply_capacity = sizeof(reply);
    call.reply_handles = &received; call.reply_handle_capacity = 1u;
    result = astra_port_call(manager, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK) goto done;
    if (call.reply_size != sizeof(reply) || reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE || reply.header.flags != 0u ||
        reply.header.reserved != 0u || reply.header.protocol != ASTRA_SERVICE_MANAGER_PROTOCOL ||
        reply.header.protocol_version != ASTRA_SERVICE_MANAGER_VERSION ||
        reply.header.operation != ASTRA_SERVICE_MANAGER_REPLY ||
        reply.header.transaction_id != request.header.transaction_id || reply.reserved != 0u ||
        reply.next_cursor.reserved != 0u || reply.next_cursor.source > ASTRA_SERVICE_LIST_SOURCE_DONE) {
        result = ASTRA_ERROR_IO; goto done;
    }
    result = astra_internal_service_result(reply.status);
    if (result != ASTRA_OK) {
        if (call.reply_handle_count != 0u || reply.payload_size != 0u || reply.payload_kind != 0u)
            result = ASTRA_ERROR_IO;
        goto done;
    }
    uint32_t wanted = operation == ASTRA_SERVICE_MANAGER_INSPECT ? 2u :
        operation == ASTRA_SERVICE_MANAGER_REMOVE || operation == ASTRA_SERVICE_MANAGER_SHUTDOWN ||
        operation == ASTRA_SERVICE_MANAGER_SYSTEM_RESTART ? 0u : 1u;
    if (reply.payload_kind != wanted || call.reply_handle_count != (wanted != 0u ? 1u : 0u) ||
        (wanted == 0u && reply.payload_size != 0u) ||
        (wanted != 0u && (received == ASTRA_INVALID_HANDLE || reply.payload_size == 0u ||
            reply.payload_size > (wanted == 1u ? ASTRA_VFS_PATH_MAX : ASTRA_SERVICE_DEFINITION_PAYLOAD_MAX)))) {
        result = ASTRA_ERROR_IO; goto done;
    }
    if (wanted != 0u) {
        bytes = astra_runtime_allocate(reply.payload_size);
        if (bytes == NULL) { result = ASTRA_ERROR_OUT_OF_MEMORY; goto done; }
        result = astra_service_payload_copy(&received, bytes, reply.payload_size, reply.payload_size);
        if (result != ASTRA_OK) goto done;
        const char *path = bytes; uint32_t length = reply.payload_size;
        if (wanted == 2u) {
            if (definition == NULL || reply.payload_size < sizeof(AstraServiceDefinitionRecord)) {
                result = ASTRA_ERROR_IO; goto done;
            }
            const AstraServiceDefinitionRecord *record = (const void *)bytes;
            if (record->executable_offset != sizeof(*record) ||
                record->executable_length != reply.payload_size - sizeof(*record)) {
                result = ASTRA_ERROR_IO; goto done;
            }
            path = bytes + record->executable_offset; length = record->executable_length;
        }
        result = astra_service_info_decode(&reply.info, path, length, &decoded);
        if (result != ASTRA_OK) goto done;
        if (name != NULL && strcmp(decoded.name, name) != 0) {
            result = ASTRA_ERROR_IO; goto done;
        }
        if (wanted == 2u) {
            const AstraServiceDefinitionRecord *record = (const void *)bytes;
            if (record->name[sizeof(record->name) - 1u] != '\0' || strcmp(record->name, decoded.name) != 0) {
                result = ASTRA_ERROR_IO; goto done;
            }
            result = astra_service_definition_decode(bytes, reply.payload_size, definition);
            if (result != ASTRA_OK) goto done;
        }
    }
    if (info != NULL) {
        astra_service_info_destroy(info); *info = decoded;
        decoded = (AstraServiceInfo)ASTRA_SERVICE_INFO_INIT;
    }
    if (next_cursor != NULL) *next_cursor = reply.next_cursor;
done:
    if (received != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&received);
        if (result == ASTRA_OK) result = closed;
    }
    astra_service_info_destroy(&decoded);
    astra_runtime_deallocate(bytes);
    return result;
}

AstraResult astra_service_list(AstraHandle manager, const AstraServiceListCursor *cursor,
    AstraServiceInfo *info, AstraServiceListCursor *next_cursor)
{
    if (cursor == NULL || info == NULL || next_cursor == NULL ||
        cursor->source > ASTRA_SERVICE_LIST_SOURCE_DONE || cursor->reserved != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(manager, ASTRA_SERVICE_MANAGER_LIST, NULL, 0u, cursor,
        NULL, 0u, info, NULL, next_cursor);
}
AstraResult astra_service_inspect(AstraHandle manager, const char *name,
    AstraServiceInfo *info, AstraServiceDefinition *definition)
{
    if (name == NULL || info == NULL || definition == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(manager, ASTRA_SERVICE_MANAGER_INSPECT, name, 0u, NULL,
        NULL, 0u, info, definition, NULL);
}
AstraResult astra_service_add(AstraHandle manager, const AstraServiceDefinition *definition)
{
    uint32_t size = astra_service_definition_payload_size(definition);
    if (size == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    char *bytes = astra_runtime_allocate(size);
    if (bytes == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    AstraHandle payload = ASTRA_INVALID_HANDLE;
    AstraResult result = astra_service_definition_encode(definition, bytes, size);
    if (result == ASTRA_OK) result = astra_service_payload_create(bytes, size, &payload);
    astra_runtime_deallocate(bytes);
    if (result == ASTRA_OK) result = exchange(manager, ASTRA_SERVICE_MANAGER_ADD,
        definition->name, 0u, NULL, &payload, size, NULL, NULL, NULL);
    if (payload != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&payload);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}
AstraResult astra_service_control(AstraHandle manager, uint32_t operation,
    const char *name, AstraServiceInfo *info)
{
    if (operation < ASTRA_SERVICE_MANAGER_REMOVE || operation > ASTRA_SERVICE_MANAGER_DISABLE || name == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(manager, operation, name, 0u, NULL, NULL, 0u, info, NULL, NULL);
}
AstraResult astra_service_set_restart(AstraHandle manager, const char *name,
    uint32_t policy, AstraServiceInfo *info)
{
    if (name == NULL || policy > ASTRA_SERVICE_RESTART_ALWAYS) return ASTRA_ERROR_INVALID_ARGUMENT;
    return exchange(manager, ASTRA_SERVICE_MANAGER_SET_RESTART, name, policy, NULL,
        NULL, 0u, info, NULL, NULL);
}
AstraResult astra_system_shutdown_request(AstraHandle manager)
{
    return exchange(manager, ASTRA_SERVICE_MANAGER_SHUTDOWN, NULL, 0u, NULL,
        NULL, 0u, NULL, NULL, NULL);
}
AstraResult astra_system_restart_request(AstraHandle manager)
{
    return exchange(manager, ASTRA_SERVICE_MANAGER_SYSTEM_RESTART, NULL, 0u, NULL,
        NULL, 0u, NULL, NULL, NULL);
}
