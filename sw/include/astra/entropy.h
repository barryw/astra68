#ifndef ASTRA_ENTROPY_H
#define ASTRA_ENTROPY_H

/** @file entropy.h
 *  @brief Capability-scoped cryptographic entropy protocol.
 */

#include <stdint.h>

#include <astra/message_abi.h>

/** Startup capability naming the system entropy service. */
#define ASTRA_CAPABILITY_ENTROPY "ENTROPY"
/** Entropy request/reply protocol identifier (`ENTR`). */
#define ASTRA_ENTROPY_PROTOCOL UINT32_C(0x454e5452)
/** Current entropy wire protocol version. */
#define ASTRA_ENTROPY_PROTOCOL_VERSION UINT16_C(1)
/** Request operation. */
#define ASTRA_ENTROPY_OPERATION_GET UINT32_C(1)
/** Reply operation. */
#define ASTRA_ENTROPY_OPERATION_REPLY UINT32_C(2)
/** POSIX getentropy(3) maximum request size. */
#define ASTRA_ENTROPY_MAX UINT32_C(256)

/** Request cryptographically secure bytes from the entropy service. */
typedef struct AstraEntropyRequest {
    AstraMessageHeader header; /**< Common request header. */
    uint32_t length;           /**< Requested bytes, at most 256. */
    uint32_t reserved[3];      /**< Must be zero. */
} AstraEntropyRequest;

/** Reply containing cryptographically secure bytes. */
typedef struct AstraEntropyReply {
    AstraMessageHeader header;       /**< Common reply header. */
    uint32_t status;                 /**< ASTRA_STATUS_* result. */
    uint32_t length;                 /**< Returned bytes on success. */
    uint32_t reserved[2];            /**< Must be zero. */
    uint8_t data[ASTRA_ENTROPY_MAX]; /**< Random bytes. */
} AstraEntropyReply;

#define ASTRA_ENTROPY_REQUEST_SIZE UINT32_C(40)
#define ASTRA_ENTROPY_REPLY_PREFIX_SIZE UINT32_C(40)
#define ASTRA_ENTROPY_REPLY_SIZE UINT32_C(296)

_Static_assert(sizeof(AstraEntropyRequest) == ASTRA_ENTROPY_REQUEST_SIZE,
               "entropy request ABI changed");
_Static_assert(sizeof(AstraEntropyReply) == ASTRA_ENTROPY_REPLY_SIZE,
               "entropy reply ABI changed");

#endif
