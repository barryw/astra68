#include "entropy_service.h"

#include <astra/status.h>

#include <string.h>

int
astra_entropy_request_valid(const AstraEntropyRequest *request,
                             uint32_t size, uint32_t handle_count)
{
    return request != NULL && size == sizeof(*request) && handle_count == 1u &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_ENTROPY_PROTOCOL &&
           request->header.protocol_version ==
               ASTRA_ENTROPY_PROTOCOL_VERSION &&
           request->header.reserved == 0u &&
           request->header.operation == ASTRA_ENTROPY_OPERATION_GET &&
           request->length != 0u && request->length <= ASTRA_ENTROPY_MAX &&
           request->reserved[0] == 0u && request->reserved[1] == 0u &&
           request->reserved[2] == 0u;
}

void
astra_entropy_reply_init(AstraEntropyReply *reply,
                         const AstraEntropyRequest *request,
                         uint32_t status, const void *bytes,
                         uint32_t length)
{
    uint32_t output_length = status == ASTRA_STATUS_OK ? length : 0u;

    (void)memset(reply, 0, sizeof(*reply));
    astra_message_header_set(&reply->header,
                             ASTRA_ENTROPY_REPLY_PREFIX_SIZE + output_length,
                             ASTRA_ENTROPY_PROTOCOL,
                             ASTRA_ENTROPY_PROTOCOL_VERSION,
                             ASTRA_ENTROPY_OPERATION_REPLY,
                             request->header.transaction_id);
    reply->status = status;
    reply->length = output_length;
    if (output_length != 0u)
        (void)memcpy(reply->data, bytes, output_length);
}
