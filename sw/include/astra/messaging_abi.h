/** @file messaging_abi.h @brief Broker directory and event wire protocol. */
#ifndef ASTRA_MESSAGING_ABI_H
#define ASTRA_MESSAGING_ABI_H

#include <stdint.h>
#include <astra/message_abi.h>

/** Public broker startup capability. */
#define ASTRA_CAPABILITY_MESSAGING "MESSAGING"
/** MSGG wire protocol identifier. */
#define ASTRA_MESSAGING_PROTOCOL UINT32_C(0x4d534747)
/** MSGG wire protocol version. */
#define ASTRA_MESSAGING_VERSION 1u
/** Maximum process name bytes including NUL. */
#define ASTRA_MESSAGING_NAME_MAX 256u
/** Maximum topic bytes including NUL. */
#define ASTRA_MESSAGING_TOPIC_MAX 64u
/** Maximum inline event payload bytes. */
#define ASTRA_MESSAGING_PAYLOAD_MAX 384u

enum {
    ASTRA_MESSAGING_IDENTIFY = 1u,
    ASTRA_MESSAGING_LIST,
    ASTRA_MESSAGING_LOOKUP,
    ASTRA_MESSAGING_REGISTER,
    ASTRA_MESSAGING_UNREGISTER,
    ASTRA_MESSAGING_SUBSCRIBE,
    ASTRA_MESSAGING_UNSUBSCRIBE,
    ASTRA_MESSAGING_PUBLISH,
    ASTRA_MESSAGING_REPLY,
    ASTRA_MESSAGING_EVENT
};

enum {
    ASTRA_MESSAGING_SERVICE = 1u,
    ASTRA_MESSAGING_APPLICATION,
    ASTRA_MESSAGING_COMMAND
};

/* Direct endpoints carry application-defined protocols. Event payloads are
 * opaque; the authenticated publisher identity is supplied by the broker. */
/** Broker request from a process with authenticated sender identity. */
typedef struct AstraMessagingRequest {
    /** Protocol header and requested operation. */
    AstraMessageHeader header;
    /** Publisher or target kind. */
    uint32_t kind;
    /** Optional exact target process ID. */
    uint32_t process_id;
    /** Previous process ID for directory enumeration. */
    uint64_t cursor;
    /** Caller-owned subscription for removal. */
    uint64_t subscription;
    /** Target name; ignored when publishing. */
    char name[ASTRA_MESSAGING_NAME_MAX];
    /** Publisher-local topic. */
    char topic[ASTRA_MESSAGING_TOPIC_MAX];
    /** Valid payload bytes. */
    uint32_t payload_size;
    /** Must be zero. */
    uint32_t reserved;
    /** Opaque publisher-defined event bytes. */
    uint8_t payload[ASTRA_MESSAGING_PAYLOAD_MAX];
} AstraMessagingRequest;

/** Broker result or process directory entry. */
typedef struct AstraMessagingReply {
    /** Protocol reply header. */
    AstraMessageHeader header;
    /** Service status. */
    uint32_t status;
    /** Identified process kind. */
    uint32_t kind;
    /** Identified process ID. */
    uint32_t process_id;
    /** Whether a direct endpoint is registered. */
    uint32_t has_endpoint;
    /** Cursor for next directory entry. */
    uint64_t cursor;
    /** New subscription ID. */
    uint64_t subscription;
    /** Successful event deliveries. */
    uint32_t delivered;
    /** Failed event deliveries. */
    uint32_t dropped;
    /** Identified process name. */
    char name[ASTRA_MESSAGING_NAME_MAX];
} AstraMessagingReply;

/** One event delivered to a subscriber. */
typedef struct AstraMessagingEvent {
    /** Protocol event header. */
    AstraMessageHeader header;
    /** Authenticated publisher kind. */
    uint32_t kind;
    /** Authenticated publisher process ID. */
    uint32_t process_id;
    /** Recipient's subscription ID. */
    uint64_t subscription;
    /** Monotonic delivery attempt number for this subscription. */
    uint64_t sequence;
    /** Authenticated publisher name. */
    char name[ASTRA_MESSAGING_NAME_MAX];
    /** Publisher-local topic. */
    char topic[ASTRA_MESSAGING_TOPIC_MAX];
    /** Valid payload bytes. */
    uint32_t payload_size;
    /** Opaque publisher-defined event bytes. */
    uint8_t payload[ASTRA_MESSAGING_PAYLOAD_MAX];
} AstraMessagingEvent;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraMessagingRequest) <= ASTRA_MESSAGE_SIZE_MAX,
    "messaging request fits port");
_Static_assert(sizeof(AstraMessagingEvent) <= ASTRA_MESSAGE_SIZE_MAX,
    "messaging event fits port");
/** @endcond */

#endif
