#include <astra/application.h>
#include <astra/application_payload.h>
#include <astra/area.h>
#include <astra/service_payload.h>

#include <astra/port.h>
#include <astra/runtime.h>
#include <astra/utf8.h>
#include <string.h>

#include "internal/status.h"

AstraResult astra_application_launch(AstraHandle launcher,
                                     const char *bundle_path,
                                     uint16_t path_length,
                                     uint32_t *process_id)
{
    return astra_application_launch_with_arguments(
        launcher, bundle_path, path_length, ASTRA_LAUNCH_SOURCE_DESKTOP,
        NULL, 0u, process_id);
}

static AstraResult application_launch(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, AstraHandle *process_handle,
    uint32_t *process_id)
{
    AstraApplicationLaunchRequest request = {0};
    AstraApplicationLaunchReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle payload = ASTRA_INVALID_HANDLE;
    AstraHandle transferred = ASTRA_INVALID_HANDLE;
    AstraCall call = {0};
    AstraResult result;

    if (launcher == ASTRA_INVALID_HANDLE || bundle_path == 0 ||
        process_id == 0 ||
        (process_handle != 0 && *process_handle != ASTRA_INVALID_HANDLE) ||
        path_length == 0u ||
        argument_count + 1u > ASTRA_APPLICATION_ARGUMENT_MAX ||
        (argument_count != 0u && arguments == NULL) ||
        (source != ASTRA_LAUNCH_SOURCE_SHELL &&
         source != ASTRA_LAUNCH_SOURCE_DESKTOP))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (path_length >= ASTRA_APPLICATION_PATH_MAX) return ASTRA_ERROR_NAME_TOO_LONG;
    if (!astra_utf8_validate(bundle_path, path_length, 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *process_id = 0u;
    if (process_handle != 0)
        *process_handle = ASTRA_INVALID_HANDLE;
    {
        uint32_t length = (uint32_t)path_length + 1u;
        for (uint32_t index = 0u; index < argument_count; ++index) {
            const char *value = arguments[index];
            uint32_t at = 0u;
            if (value == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
            while (value[at] != '\0') {
                if (length + 2u > ASTRA_APPLICATION_ARGUMENT_BYTES)
                    return ASTRA_ERROR_NO_RESOURCES;
                ++length; ++at;
            }
            if (!astra_utf8_validate(value, at, 0u))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            ++length;
        }
        if (length > ASTRA_APPLICATION_ARGUMENT_BYTES) return ASTRA_ERROR_NO_RESOURCES;
        request.arguments.count = argument_count + 1u;
        request.arguments.length = (uint16_t)length;
        request.arguments.source = (uint16_t)source;
        result = astra_area_create(length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
        if (result == ASTRA_OK) result = astra_area_map(&area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
        if (result != ASTRA_OK) goto done;
        char *bytes = area.address;
        memcpy(bytes, bundle_path, path_length); bytes[path_length] = '\0';
        uint32_t used = (uint32_t)path_length + 1u;
        for (uint32_t index = 0u; index < argument_count; ++index) {
            uint32_t at = 0u;
            for (;;) {
                if (used == length) { result = ASTRA_ERROR_INVALID_ARGUMENT; goto done; }
                char value = arguments[index][at++]; bytes[used++] = value;
                if (value == '\0') break;
            }
        }
        result = used == length ? astra_application_arguments_validate(&request.arguments, bytes, length) :
            ASTRA_ERROR_INVALID_ARGUMENT;
        if (result == ASTRA_OK) result = astra_service_payload_finish(&area, &payload);
        if (result != ASTRA_OK) goto done;
    }
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_APPLICATION_PROTOCOL,
        ASTRA_APPLICATION_VERSION, ASTRA_APPLICATION_LAUNCH, 1u);
    if (result != ASTRA_OK)
        goto done;
    call.request = &request;
    call.request_size = sizeof(request);
    call.reply = &reply;
    call.reply_capacity = sizeof(reply);
    call.handles = &payload;
    call.handle_count = 1u;
    call.reply_index = 1u;
    call.reply_handles = &transferred;
    call.reply_handle_capacity = 1u;
    result = astra_port_call(launcher, &call, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (call.reply_size != sizeof(reply) ||
        reply.header.total_size != sizeof(reply) ||
        reply.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply.header.flags != 0u || reply.header.reserved != 0u ||
        reply.header.protocol != ASTRA_APPLICATION_PROTOCOL ||
        reply.header.protocol_version != ASTRA_APPLICATION_VERSION ||
        reply.header.operation != ASTRA_APPLICATION_LAUNCHED ||
        reply.header.transaction_id != request.header.transaction_id) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = astra_internal_service_result(reply.status);
    if ((result == ASTRA_OK &&
         (call.reply_handle_count != 1u ||
          transferred == ASTRA_INVALID_HANDLE)) ||
        (result != ASTRA_OK && call.reply_handle_count != 0u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    if (result == ASTRA_OK) {
        *process_id = reply.process_id;
        if (process_handle != 0) {
            *process_handle = transferred;
            transferred = ASTRA_INVALID_HANDLE;
        }
    }

done:
    {
        AstraResult close_result;

        if (transferred != ASTRA_INVALID_HANDLE) {
            close_result = astra_handle_close(&transferred);
            if (result == ASTRA_OK && close_result != ASTRA_OK)
                result = close_result;
        }
        if (payload != ASTRA_INVALID_HANDLE) {
            close_result = astra_handle_close(&payload);
            if (result == ASTRA_OK && close_result != ASTRA_OK) result = close_result;
        }
        if (area.handle != ASTRA_INVALID_HANDLE || area.address != NULL) {
            close_result = astra_area_close(&area);
            if (result == ASTRA_OK && close_result != ASTRA_OK) result = close_result;
        }
    }
    return result;
}

AstraResult astra_application_launch_with_arguments(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, uint32_t *process_id)
{
    return application_launch(launcher, bundle_path, path_length, source,
                              arguments, argument_count, 0, process_id);
}

AstraResult astra_application_launch_waitable(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, AstraHandle *process_handle,
    uint32_t *process_id)
{
    if (process_handle == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return application_launch(launcher, bundle_path, path_length, source,
                              arguments, argument_count, process_handle,
                              process_id);
}
