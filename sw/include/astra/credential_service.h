#ifndef ASTRA_CREDENTIAL_SERVICE_H
#define ASTRA_CREDENTIAL_SERVICE_H

/** @file credential_service.h @brief Capability-separated credential service protocol. */

#include <astra/message_abi.h>

#define ASTRA_CREDENTIAL_PROTOCOL UINT32_C(0x43524544) /* CRED */
#define ASTRA_CREDENTIAL_VERSION UINT16_C(1)
#define ASTRA_CAPABILITY_CREDENTIAL_WRITE "CRED_WRITE"
#define ASTRA_CAPABILITY_CREDENTIAL_READ "CRED_READ"
#define ASTRA_CREDENTIAL_KEY_MAX 120u
#define ASTRA_CREDENTIAL_VALUE_MAX 1024u

enum {
    ASTRA_CREDENTIAL_SET = 1u,
    ASTRA_CREDENTIAL_GET = 2u,
    ASTRA_CREDENTIAL_REMOVE = 3u,
    ASTRA_CREDENTIAL_REPLY = 4u
};

typedef struct AstraCredentialRequest {
    AstraMessageHeader header;
    char key[124u];
    uint32_t length;
    uint32_t reserved;
} AstraCredentialRequest;

typedef struct AstraCredentialReply {
    AstraMessageHeader header;
    uint32_t status; /* ASTRA_CONFIG_* */
    uint32_t length;
    uint32_t reserved;
} AstraCredentialReply;

_Static_assert(sizeof(AstraCredentialRequest) == 156u,
               "credential request ABI");
_Static_assert(sizeof(AstraCredentialReply) == 36u,
               "credential reply ABI");

#endif
