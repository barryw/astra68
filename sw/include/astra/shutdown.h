#ifndef ASTRA_SHUTDOWN_H
#define ASTRA_SHUTDOWN_H

/**
 * @file shutdown.h
 * @brief Wire protocol the supervisor uses to ask one managed process to shut itself down.
 *
 * One request goes to one process. Its sole attached handle is a
 * single-use reply port: the handle the request carries is the only way
 * back, and the process spends it exactly once, in astra_shutdown_respond().
 * READY is not sufficient by itself: the requester must also observe a
 * zero-status process exit before stopping that process's dependencies.
 */

#include <astra/compiler.h>
#include <astra/message_abi.h>

/** `SHDN` protocol identifier carried by every shutdown message. */
#define ASTRA_SHUTDOWN_PROTOCOL 0x5348444eu /* SHDN */
/** Current shutdown wire protocol version. */
#define ASTRA_SHUTDOWN_VERSION 1u
/** Request operation: please shut down. */
#define ASTRA_SHUTDOWN_REQUEST 1u
/** Reply operation: here is my decision. */
#define ASTRA_SHUTDOWN_REPLY 2u
/** Reply decision: the process will exit; the requester may wait for it. */
#define ASTRA_SHUTDOWN_READY 1u
/** Reply decision: the process declines to shut down right now. */
#define ASTRA_SHUTDOWN_CANCEL 2u

#ifndef __ASSEMBLER__
#include <stdint.h>

/** Request sent to one process, asking it to shut down. */
typedef struct AstraShutdownRequest {
    /** Common message header; operation is ::ASTRA_SHUTDOWN_REQUEST. */
    AstraMessageHeader header;
} AstraShutdownRequest;

/** Reply to an ::AstraShutdownRequest, sent on its carried single-use reply port. */
typedef struct AstraShutdownReply {
    /** Common message header; operation is ::ASTRA_SHUTDOWN_REPLY. */
    AstraMessageHeader header;
    /** ::ASTRA_SHUTDOWN_READY or ::ASTRA_SHUTDOWN_CANCEL. */
    uint32_t decision;
    /** Zero for user cancellation; otherwise an `ASTRA_STATUS_*` code explaining a CANCEL. */
    uint32_t reason;
} AstraShutdownReply;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraShutdownRequest) == ASTRA_MESSAGE_HEADER_SIZE,
               "shutdown request wire size changed");
_Static_assert(sizeof(AstraShutdownReply) == ASTRA_MESSAGE_HEADER_SIZE + 8u,
               "shutdown reply wire size changed");
/** @endcond */

/**
 * Check that a shutdown message header carries the expected size, protocol, operation and transaction id.
 *
 * @param header Header to check.
 * @param size Expected total message size.
 * @param operation Expected operation code.
 * @param transaction Expected nonzero transaction id.
 * @return Nonzero if @p header is well-formed and matches every expectation.
 */
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

/**
 * Check that a received buffer is a well-formed ::AstraShutdownRequest carrying exactly one handle.
 *
 * @param request Request to check.
 * @param transaction Expected nonzero transaction id.
 * @param handle_count Number of handles received alongside @p request.
 * @return Nonzero if @p request is well-formed and @p handle_count is 1 (the single-use reply port).
 */
static inline int astra_shutdown_request_valid(const AstraShutdownRequest *request,
                                                uint32_t transaction,
                                                uint32_t handle_count)
{
    return request != 0 && handle_count == 1u &&
           astra_shutdown_header_valid(&request->header, sizeof(*request),
                                       ASTRA_SHUTDOWN_REQUEST, transaction);
}

/**
 * Check that a received buffer is a well-formed ::AstraShutdownReply carrying no handles.
 *
 * @param reply Reply to check.
 * @param transaction Expected nonzero transaction id.
 * @param handle_count Number of handles received alongside @p reply.
 * @return Nonzero if @p reply is well-formed, @p handle_count is 0, and its decision/reason pairing is valid.
 */
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
