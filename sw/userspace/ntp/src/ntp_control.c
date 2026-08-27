#include <astra/ntp.h>
#include <astra/ntp_core.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <string.h>

uint32_t astra_ntp_sync(uint32_t service, const char *server,
                        AstraNtpControlReply *reply)
{
    AstraNtpControlRequest request;
    uint32_t receive = 0u, carried = 0u, size = 0u, status;

    if (service == 0u || reply == NULL)
        return ASTRA_NTP_INVALID;
    memset(&request, 0, sizeof(request));
    if (server != NULL) {
        size_t length = strlen(server);

        if (length > ASTRA_NETWORK_NAME_MAX)
            return ASTRA_NTP_INVALID;
        memcpy(request.server, server, length + 1u);
    }
    status = astra_rt_port_create(1u, sizeof(*reply), &receive, &carried);
    if (status != ASTRA_SYSCALL_OK)
        return ASTRA_NTP_IO;
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_NTP_CONTROL_PROTOCOL,
                             ASTRA_NTP_CONTROL_VERSION,
                             ASTRA_NTP_CONTROL_SYNC,
                             astra_activity_current());
    status = astra_port_send(service, &request, sizeof(request), &carried, 1u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(carried);
        (void)astra_close(receive);
        return ASTRA_NTP_IO;
    }
    for (;;) {
        status = astra_port_receive(receive, reply, sizeof(*reply), NULL, 0u,
                                    &size, NULL);
        if (status == ASTRA_SYSCALL_OK)
            break;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK ||
            astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL) !=
                ASTRA_SYSCALL_OK) {
            (void)astra_close(receive);
            return ASTRA_NTP_IO;
        }
    }
    (void)astra_close(receive);
    if (size != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.protocol != ASTRA_NTP_CONTROL_PROTOCOL ||
        reply->header.protocol_version != ASTRA_NTP_CONTROL_VERSION ||
        reply->header.operation != ASTRA_NTP_CONTROL_SYNC)
        return ASTRA_NTP_INVALID;
    return reply->status;
}
