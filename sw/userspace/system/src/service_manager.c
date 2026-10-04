#include <astra/service_manager.h>

#include <astra/area.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include "internal/status.h"

#include <string.h>

static AstraResult exchange(AstraHandle manager, uint32_t operation,
                            const char *name, uint32_t value,
                            const AstraServiceListCursor *cursor,
                            AstraHandle *sent, AstraServiceManagerReply *reply,
                            AstraHandle *received, uint32_t *received_count)
{
    AstraServiceManagerRequest request = {0};
    AstraCall call = {0};
    AstraResult result;

    if (manager == ASTRA_INVALID_HANDLE || reply == NULL ||
        (name != NULL && !astra_service_name_valid(name)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (name != NULL)
        (void)strcpy(request.name, name);
    if (cursor != NULL)
        request.cursor = *cursor;
    request.value = value;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_SERVICE_MANAGER_PROTOCOL,
        ASTRA_SERVICE_MANAGER_VERSION, operation, 1u);
    if (result != ASTRA_OK)
        return result;
    call.request = &request;
    call.request_size = sizeof(request);
    call.handles = sent;
    call.handle_count = sent != NULL ? 1u : 0u;
    call.reply = reply;
    call.reply_capacity = sizeof(*reply);
    call.reply_handles = received;
    call.reply_handle_capacity = received != NULL ? 1u : 0u;
    result = astra_port_call(manager, &call, ASTRA_DEADLINE_INFINITE);
    *received_count = call.reply_handle_count;
    if (result != ASTRA_OK)
        return result;
    if (call.reply_size != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.protocol != ASTRA_SERVICE_MANAGER_PROTOCOL ||
        reply->header.protocol_version != ASTRA_SERVICE_MANAGER_VERSION ||
        reply->header.operation != ASTRA_SERVICE_MANAGER_REPLY ||
        reply->header.transaction_id != request.header.transaction_id ||
        reply->reserved != 0u)
        result = ASTRA_ERROR_IO;
    else
        result = astra_internal_service_result(reply->status);
    if (result == ASTRA_OK && operation == ASTRA_SERVICE_MANAGER_LIST &&
        (reply->next_cursor.source > ASTRA_SERVICE_LIST_SOURCE_DONE ||
         reply->next_cursor.reserved != 0u))
        result = ASTRA_ERROR_IO;
    return result;
}

AstraResult astra_service_list(AstraHandle manager,
                               const AstraServiceListCursor *cursor,
                               AstraServiceInfo *info,
                               AstraServiceListCursor *next_cursor)
{
    AstraServiceManagerReply reply = {0};
    uint32_t handles = 0u;
    AstraResult result;

    if (cursor == NULL || info == NULL || next_cursor == NULL ||
        cursor->source > ASTRA_SERVICE_LIST_SOURCE_DONE ||
        cursor->reserved != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(manager, ASTRA_SERVICE_MANAGER_LIST, NULL, 0u, cursor,
                      NULL, &reply, NULL, &handles);
    if (result == ASTRA_OK) {
        *info = reply.info;
        *next_cursor = reply.next_cursor;
    }
    return result;
}

AstraResult astra_service_inspect(AstraHandle manager, const char *name,
                                  AstraServiceInfo *info,
                                  AstraServiceDefinition *definition)
{
    AstraServiceManagerReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    uint32_t count = 0u;
    AstraResult result;

    if (info == NULL || definition == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(manager, ASTRA_SERVICE_MANAGER_INSPECT, name, 0u, NULL,
                      NULL, &reply, &area.handle, &count);
    if (result != ASTRA_OK)
        return result;
    if (count != 1u) {
        if (area.handle != ASTRA_INVALID_HANDLE) {
            AstraResult close_result = astra_area_close(&area);

            (void)close_result;
        }
        return ASTRA_ERROR_IO;
    }
    result = astra_area_map(&area, ASTRA_AREA_MAP_READ);
    if (result == ASTRA_OK) {
        if (area.size < sizeof(*definition))
            result = ASTRA_ERROR_IO;
        else {
            (void)memcpy(definition, area.address, sizeof(*definition));
            result = astra_service_definition_validate(definition);
        }
    }
    {
        AstraResult close_result = astra_area_close(&area);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    if (result == ASTRA_OK)
        *info = reply.info;
    return result;
}

AstraResult astra_service_add(AstraHandle manager,
                              const AstraServiceDefinition *definition)
{
    AstraServiceManagerReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    uint32_t handles = 0u;
    AstraResult result = astra_service_definition_validate(definition);

    if (result != ASTRA_OK)
        return result;
    result = astra_area_create(
        sizeof(*definition), ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
        ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
    if (result != ASTRA_OK)
        return result;
    result = astra_area_map(&area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result == ASTRA_OK)
        (void)memcpy(area.address, definition, sizeof(*definition));
    if (result == ASTRA_OK)
        result = astra_area_unmap(&area);
    if (result == ASTRA_OK)
        result = exchange(manager, ASTRA_SERVICE_MANAGER_ADD,
                          definition->name, 0u, NULL, &area.handle, &reply,
                          NULL, &handles);
    if (area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult close_result = astra_area_close(&area);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}

AstraResult astra_service_control(AstraHandle manager, uint32_t operation,
                                  const char *name, AstraServiceInfo *info)
{
    AstraServiceManagerReply reply = {0};
    uint32_t handles = 0u;
    AstraResult result;

    if (operation < ASTRA_SERVICE_MANAGER_REMOVE ||
        operation > ASTRA_SERVICE_MANAGER_DISABLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(manager, operation, name, 0u, NULL, NULL, &reply, NULL,
                      &handles);
    if (result == ASTRA_OK && info != NULL)
        *info = reply.info;
    return result;
}

AstraResult astra_service_set_restart(AstraHandle manager, const char *name,
                                      uint32_t policy, AstraServiceInfo *info)
{
    AstraServiceManagerReply reply = {0};
    uint32_t handles = 0u;
    AstraResult result;

    if (policy > ASTRA_SERVICE_RESTART_ALWAYS)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(manager, ASTRA_SERVICE_MANAGER_SET_RESTART, name,
                      policy, NULL, NULL, &reply, NULL, &handles);
    if (result == ASTRA_OK && info != NULL)
        *info = reply.info;
    return result;
}

AstraResult astra_system_shutdown_request(AstraHandle manager)
{
    AstraServiceManagerReply reply = {0};
    uint32_t handles = 0u;

    return exchange(manager, ASTRA_SERVICE_MANAGER_SHUTDOWN, NULL, 0u, NULL,
                    NULL, &reply, NULL, &handles);
}

AstraResult astra_system_restart_request(AstraHandle manager)
{
    AstraServiceManagerReply reply = {0};
    uint32_t handles = 0u;

    return exchange(manager, ASTRA_SERVICE_MANAGER_SYSTEM_RESTART, NULL, 0u,
                    NULL, NULL, &reply, NULL, &handles);
}
