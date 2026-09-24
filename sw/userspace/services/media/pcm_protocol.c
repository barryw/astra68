#include "pcm_protocol.h"

int astra_pcm_request_valid(const AstraPcmRequest *request, uint32_t size,
                            int factory)
{
    uint32_t operation = request->header.operation;

    return size == sizeof(*request) &&
           request->header.total_size == size &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_PCM_PROTOCOL &&
           request->header.protocol_version == ASTRA_PCM_PROTOCOL_VERSION &&
           request->header.reserved == 0u &&
           request->header.transaction_id != 0u &&
           operation >= ASTRA_PCM_OPEN && operation <= ASTRA_PCM_CLEAR &&
           operation != ASTRA_PCM_REPLY &&
           (factory ? operation == ASTRA_PCM_OPEN : operation != ASTRA_PCM_OPEN);
}
