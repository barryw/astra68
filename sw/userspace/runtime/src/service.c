#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>

uint32_t
astra_service_ready(uint32_t bootstrap, uint32_t status,
                    const uint32_t *handles, uint32_t handle_count)
{
    AstraServiceReady message = {0};

    if (status != ASTRA_STATUS_OK) {
        handles = NULL;
        handle_count = 0u;
    } else if (handle_count > ASTRA_MESSAGE_HANDLES_MAX ||
               (handle_count != 0u && handles == NULL)) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_message_header_set(&message.header, sizeof(message),
                             ASTRA_SERVICE_PROTOCOL, ASTRA_SERVICE_VERSION,
                             ASTRA_SERVICE_READY, 0u);
    message.status = status;
    return astra_port_send(bootstrap, &message, sizeof(message), handles,
                           handle_count);
}
