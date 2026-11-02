#include "policy.h"

#include <stddef.h>

int credential_request_valid(const AstraCredentialRequest *request,
                             uint32_t bytes, uint32_t handles,
                             uint32_t reader)
{
    uint32_t at = 0u;
    if (request == NULL || bytes != sizeof(*request) ||
        request->header.total_size != sizeof(*request) ||
        request->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request->header.flags != 0u ||
        request->header.protocol != ASTRA_CREDENTIAL_PROTOCOL ||
        request->header.protocol_version != ASTRA_CREDENTIAL_VERSION ||
        request->header.transaction_id == 0u ||
        request->header.reserved != 0u || request->reserved != 0u)
        return 0;
    while (at <= ASTRA_CREDENTIAL_KEY_MAX && request->key[at] != '\0') ++at;
    if (at == 0u || at > ASTRA_CREDENTIAL_KEY_MAX) return 0;
    while (++at < sizeof(request->key))
        if (request->key[at] != '\0') return 0;
    if (reader)
        return request->header.operation == ASTRA_CREDENTIAL_GET &&
               handles == 1u && request->length <=
                   ASTRA_CREDENTIAL_VALUE_MAX;
    if (request->header.operation == ASTRA_CREDENTIAL_SET)
        return handles == 2u && request->length != 0u &&
               request->length <= ASTRA_CREDENTIAL_VALUE_MAX;
    return request->header.operation == ASTRA_CREDENTIAL_REMOVE &&
           handles == 1u && request->length == 0u;
}
