#ifndef ASTRA_MESSAGE_ABI_H
#define ASTRA_MESSAGE_ABI_H

#include <astra/limits.h>

/** @file message_abi.h
 *  @brief Message wire format shared by Axiom, the runtime, and the NDK.
 */
/** Complete queued-message count representable by the port ABI. */
#define ASTRA_PORT_MESSAGES_MAX 65535u
/** Wire size of AstraMessageHeader. */
#define ASTRA_MESSAGE_HEADER_SIZE 24u
/** Maximum bytes stored inline after a message header. */
#define ASTRA_MESSAGE_INLINE_MAX 1024u
/** Maximum aggregate byte capacity of a message port. */
#define ASTRA_PORT_BYTES_MAX \
    (ASTRA_PORT_MESSAGES_MAX * \
     (ASTRA_MESSAGE_HEADER_SIZE + ASTRA_MESSAGE_INLINE_MAX))
/** Maximum wire size of one message. */
#define ASTRA_MESSAGE_SIZE_MAX \
    (ASTRA_MESSAGE_HEADER_SIZE + ASTRA_MESSAGE_INLINE_MAX)
/** Complete per-process handle namespace; no message can attach more. */
#define ASTRA_MESSAGE_HANDLES_MAX ASTRA_HANDLE_COUNT_MAX

#ifndef __ASSEMBLER__
#include <stdint.h>

/** Common prefix of every Astra message protocol. */
typedef struct AstraMessageHeader {
    /** Total message bytes, including this header. */
    uint32_t total_size;
    /** Header bytes; set to ASTRA_MESSAGE_HEADER_SIZE. */
    uint16_t header_size;
    /** Protocol-defined flags; zero when no flags are defined. */
    uint16_t flags;
    /** Protocol identifier. */
    uint32_t protocol;
    /** Protocol wire-format version. */
    uint16_t protocol_version;
    /** Reserved for compatible extension; must be zero. */
    uint16_t reserved;
    /** Protocol operation identifier. */
    uint32_t operation;
    /** Request/reply correlation identifier. */
    uint32_t transaction_id;
} AstraMessageHeader;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraMessageHeader) == ASTRA_MESSAGE_HEADER_SIZE,
               "message ABI header size changed");
/** @endcond */

/** Initialize a message header with canonical sizes and zeroed reserved fields.
 *
 * @param header Header to initialize.
 * @param total_size Total message bytes, including the header.
 * @param protocol Protocol identifier.
 * @param protocol_version Protocol wire-format version.
 * @param operation Protocol operation identifier.
 * @param transaction_id Request/reply correlation identifier.
 */
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
