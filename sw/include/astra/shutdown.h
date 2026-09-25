#ifndef ASTRA_SHUTDOWN_H
#define ASTRA_SHUTDOWN_H

#include <astra/message_abi.h>

/* One request goes to one process. Its sole attached handle is a single-use
 * reply port. READY is not sufficient by itself: the requester must also
 * observe a zero-status process exit before stopping dependencies. */
#define ASTRA_SHUTDOWN_PROTOCOL 0x5348444eu /* SHDN */
#define ASTRA_SHUTDOWN_VERSION 1u
#define ASTRA_SHUTDOWN_REQUEST 1u
#define ASTRA_SHUTDOWN_REPLY 2u
#define ASTRA_SHUTDOWN_READY 1u
#define ASTRA_SHUTDOWN_CANCEL 2u

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef struct AstraShutdownRequest {
    AstraMessageHeader header;
} AstraShutdownRequest;

typedef struct AstraShutdownReply {
    AstraMessageHeader header;
    uint32_t decision;
    uint32_t reason; /* Zero for user cancellation; otherwise ASTRA_STATUS_*. */
} AstraShutdownReply;

_Static_assert(sizeof(AstraShutdownRequest) == ASTRA_MESSAGE_HEADER_SIZE,
               "shutdown request wire size changed");
_Static_assert(sizeof(AstraShutdownReply) == ASTRA_MESSAGE_HEADER_SIZE + 8u,
               "shutdown reply wire size changed");

static inline int astra_shutdown_header_valid(const AstraMessageHeader *header,
                                               uint32_t size,
                                               uint32_t operation,
                                               uint32_t transaction)
{
    return header != 0 && transaction != 0u &&
           header->total_size == size &&
           header->header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           header->flags == 0u &&
           header->protocol == ASTRA_SHUTDOWN_PROTOCOL &&
           header->protocol_version == ASTRA_SHUTDOWN_VERSION &&
           header->reserved == 0u &&
           header->operation == operation &&
           header->transaction_id == transaction;
}

static inline int astra_shutdown_request_valid(const AstraShutdownRequest *request,
                                                uint32_t transaction,
                                                uint32_t handle_count)
{
    return request != 0 && handle_count == 1u &&
           astra_shutdown_header_valid(&request->header, sizeof(*request),
                                       ASTRA_SHUTDOWN_REQUEST, transaction);
}

static inline int astra_shutdown_reply_valid(const AstraShutdownReply *reply,
                                              uint32_t transaction,
                                              uint32_t handle_count)
{
    return reply != 0 && handle_count == 0u &&
           astra_shutdown_header_valid(&reply->header, sizeof(*reply),
                                       ASTRA_SHUTDOWN_REPLY, transaction) &&
           (reply->decision == ASTRA_SHUTDOWN_CANCEL ||
            (reply->decision == ASTRA_SHUTDOWN_READY && reply->reason == 0u));
}
#endif

#endif
