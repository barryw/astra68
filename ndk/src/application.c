#include <astra/application.h>

#include <astra/port.h>
#include <astra/runtime.h>

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

AstraResult astra_application_launch_with_arguments(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, uint32_t *process_id)
{
    AstraApplicationLaunchRequest request = {0};
    AstraApplicationLaunchReply reply = {0};
    AstraPort reply_port = ASTRA_PORT_INIT;
    const char *values[ASTRA_LAUNCH_ARGUMENT_MAX];
    char path[ASTRA_APPLICATION_PATH_MAX];
    AstraHandle transferred;
    uint32_t reply_size = 0u;
    uint32_t reply_handles = 0u;
    AstraResult result;

    if (launcher == ASTRA_INVALID_HANDLE || bundle_path == 0 ||
        process_id == 0 || path_length == 0u ||
        path_length >= ASTRA_APPLICATION_PATH_MAX ||
        argument_count + 1u > ASTRA_LAUNCH_ARGUMENT_MAX ||
        (argument_count != 0u && arguments == NULL) ||
        (source != ASTRA_LAUNCH_SOURCE_SHELL &&
         source != ASTRA_LAUNCH_SOURCE_DESKTOP))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *process_id = 0u;
    for (uint32_t at = 0u; at < path_length; ++at)
        path[at] = bundle_path[at];
    path[path_length] = '\0';
    values[0] = path;
    for (uint32_t at = 0u; at < argument_count; ++at)
        values[at + 1u] = arguments[at];
    {
        uint32_t status = astra_launch_arguments_pack(
            &request.arguments, source, argument_count + 1u, values);

        if (status != ASTRA_SYSCALL_OK)
            return status == ASTRA_SYSCALL_RESOURCE_LIMIT ?
                ASTRA_ERROR_NO_RESOURCES : ASTRA_ERROR_INVALID_ARGUMENT;
    }
    result = astra_port_create(1u, sizeof(reply), &reply_port);
    if (result != ASTRA_OK)
        return result;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_APPLICATION_PROTOCOL,
        ASTRA_APPLICATION_VERSION, ASTRA_APPLICATION_LAUNCH, 1u);
    if (result != ASTRA_OK)
        goto done;
    transferred = reply_port.send;
    result = astra_port_send_until(launcher, &request, sizeof(request),
                                   &transferred, 1u,
                                   ASTRA_DEADLINE_INFINITE);
    reply_port.send = transferred;
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        reply_port.receive, &reply, sizeof(reply), 0, 0, &reply_size,
        &reply_handles, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (reply_size != sizeof(reply) || reply_handles != 0u ||
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
    if (result == ASTRA_OK)
        *process_id = reply.process_id;

done:
    {
        AstraResult close_result = astra_port_close(&reply_port);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}
