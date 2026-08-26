#ifndef ASTRA_MESSAGE_ABI_H
#define ASTRA_MESSAGE_ABI_H

/* One message-port contract shared by Axiom, the runtime, and the NDK. */
#define ASTRA_PORT_MESSAGES_MAX 16u
#define ASTRA_MESSAGE_HEADER_SIZE 24u
#define ASTRA_MESSAGE_INLINE_MAX 1024u
#define ASTRA_PORT_BYTES_MAX \
    (ASTRA_PORT_MESSAGES_MAX * \
     (ASTRA_MESSAGE_HEADER_SIZE + ASTRA_MESSAGE_INLINE_MAX))
#define ASTRA_MESSAGE_SIZE_MAX \
    (ASTRA_MESSAGE_HEADER_SIZE + ASTRA_MESSAGE_INLINE_MAX)
#define ASTRA_MESSAGE_HANDLES_MAX 8u

#ifndef __ASSEMBLER__
#include <stdint.h>

/** Common prefix of every Astra message protocol. */
typedef struct AstraMessageHeader {
    uint32_t total_size;
    uint16_t header_size;
    uint16_t flags;
    uint32_t protocol;
    uint16_t protocol_version;
    uint16_t reserved;
    uint32_t operation;
    uint32_t transaction_id;
} AstraMessageHeader;

_Static_assert(sizeof(AstraMessageHeader) == ASTRA_MESSAGE_HEADER_SIZE,
               "message ABI header size changed");

static inline void
astra_message_header_set(AstraMessageHeader *header, uint32_t total_size,
                         uint32_t protocol, uint16_t protocol_version,
                         uint32_t operation, uint32_t transaction_id)
{
    header->total_size = total_size;
    header->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    header->flags = 0u;
    header->protocol = protocol;
    header->protocol_version = protocol_version;
    header->reserved = 0u;
    header->operation = operation;
    header->transaction_id = transaction_id;
}
#endif

#endif
