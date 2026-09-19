#include <astra/service_manager.h>

#include <astra/area.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include "internal/status.h"

#include <string.h>

static AstraResult exchange(AstraHandle manager, uint32_t operation,
                            const char *name,
                            const AstraServiceListCursor *cursor,
                            AstraHandle *sent, AstraServiceManagerReply *reply,
                            AstraHandle *received, uint32_t *received_count)
{
    AstraServiceManagerRequest request = {0};
    AstraPort response = ASTRA_PORT_INIT;
    AstraHandle handles[2];
    uint32_t send_count = sent == NULL ? 1u : 2u;
    uint32_t size = 0u;
    AstraResult result;

    if (manager == ASTRA_INVALID_HANDLE || reply == NULL ||
        (name != NULL && !astra_service_name_valid(name)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_port_create(1u, sizeof(*reply), &response);
    if (result != ASTRA_OK)
        return result;
    if (name != NULL)
        (void)strcpy(request.name, name);
    if (cursor != NULL)
        request.cursor = *cursor;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_SERVICE_MANAGER_PROTOCOL,
        ASTRA_SERVICE_MANAGER_VERSION, operation, 1u);
    if (result != ASTRA_OK)
        goto done;
    handles[0] = response.send;
    if (sent != NULL)
        handles[1] = *sent;
    result = astra_port_send_until(manager, &request, sizeof(request),
                                   handles, send_count,
                                   ASTRA_DEADLINE_INFINITE);
    response.send = handles[0];
    if (sent != NULL)
        *sent = handles[1];
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        response.receive, reply, sizeof(*reply), received,
        received != NULL ? 1u : 0u, &size, received_count,
        ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (size != sizeof(*reply) ||
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
done:
    {
        AstraResult close_result = astra_port_close(&response);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
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
    result = exchange(manager, ASTRA_SERVICE_MANAGER_LIST, NULL, cursor,
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
    result = exchange(manager, ASTRA_SERVICE_MANAGER_INSPECT, name, NULL,
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
                          definition->name, NULL, &area.handle, &reply, NULL,
                          &handles);
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
    result = exchange(manager, operation, name, NULL, NULL, &reply, NULL,
                      &handles);
    if (result == ASTRA_OK && info != NULL)
        *info = reply.info;
    return result;
}
