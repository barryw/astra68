#ifndef ASTRA_CREDENTIAL_POLICY_H
#define ASTRA_CREDENTIAL_POLICY_H

#include <astra/credential_service.h>

int credential_request_valid(const AstraCredentialRequest *request,
                             uint32_t bytes, uint32_t handles,
                             uint32_t reader);

#endif
