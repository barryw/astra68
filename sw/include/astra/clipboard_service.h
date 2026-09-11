#ifndef ASTRA_CLIPBOARD_SERVICE_H
#define ASTRA_CLIPBOARD_SERVICE_H

#include <stdint.h>

#include <astra/message_abi.h>

#define ASTRA_CAPABILITY_CLIPBOARD "CLIPBOARD"

#define ASTRA_CLIPBOARD_PROTOCOL UINT32_C(0x434c4950) /* CLIP */
#define ASTRA_CLIPBOARD_PROTOCOL_VERSION UINT16_C(1)

enum {
    ASTRA_CLIPBOARD_OPERATION_SET = 1u,
    ASTRA_CLIPBOARD_OPERATION_GET = 2u,
    ASTRA_CLIPBOARD_OPERATION_CLEAR = 3u,
    ASTRA_CLIPBOARD_OPERATION_REPLY = 4u
};

typedef struct AstraClipboardRequest {
    AstraMessageHeader header;
    uint32_t document_size;
    uint32_t reserved[3];
} AstraClipboardRequest;

typedef struct AstraClipboardReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t generation;
    uint32_t document_size;
    uint32_t reserved;
} AstraClipboardReply;

_Static_assert(sizeof(AstraClipboardRequest) == 40u,
               "clipboard request ABI changed");
_Static_assert(sizeof(AstraClipboardReply) == 40u,
               "clipboard reply ABI changed");

#endif
