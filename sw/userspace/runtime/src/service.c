#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>

static uint32_t service_ready(uint32_t bootstrap, uint32_t status,
                              const uint32_t *handles, uint32_t handle_count,
                              uint32_t shutdown_send)
{
    AstraServiceReady message = {0};
    uint32_t published[ASTRA_MESSAGE_HANDLES_MAX];
    uint32_t flags = shutdown_send != 0u ?
        ASTRA_SERVICE_READY_SHUTDOWN_MANAGED : 0u;

    if (status != ASTRA_STATUS_OK) {
        handles = NULL;
        handle_count = 0u;
        flags = 0u;
    } else if (handle_count > ASTRA_MESSAGE_HANDLES_MAX - (flags != 0u) ||
               (handle_count != 0u && handles == NULL)) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0u; index < handle_count; ++index)
        published[index] = handles[index];
    if (flags != 0u)
        published[handle_count++] = shutdown_send;
    astra_message_header_set(&message.header, sizeof(message),
                             ASTRA_SERVICE_PROTOCOL, ASTRA_SERVICE_VERSION,
                             ASTRA_SERVICE_READY, 0u);
    message.status = status;
    message.flags = status == ASTRA_STATUS_OK ? flags : 0u;
    return astra_port_send(bootstrap, &message, sizeof(message),
                           handle_count != 0u ? published : NULL,
                           handle_count);
}

uint32_t astra_service_ready(uint32_t bootstrap, uint32_t status,
                             const uint32_t *handles, uint32_t handle_count)
{
    return service_ready(bootstrap, status, handles, handle_count, 0u);
}

uint32_t astra_service_ready_managed(uint32_t bootstrap, uint32_t status,
                                     const uint32_t *handles,
                                     uint32_t handle_count,
                                     uint32_t shutdown_send)
{
    if (status == ASTRA_STATUS_OK && shutdown_send == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return service_ready(bootstrap, status, handles, handle_count,
                         shutdown_send);
}
