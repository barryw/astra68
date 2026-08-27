#ifndef ASTRA_NETWORK_H
#define ASTRA_NETWORK_H

/*
 * Stable Astra network ABI.  Host descriptors, sockaddr layouts, errno values,
 * and backend-private pointers never cross this boundary.
 */

#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/syscall.h>

#define ASTRA_DEVICE_CLASS_NETWORK UINT32_C(0x4e455457) /* NETW */
#define ASTRA_DEVICE_ID_NETWORK0   UINT32_C(0x4e450001)

#define ASTRA_CAPABILITY_NETWORK_DEVICE "NETWORK_DEVICE"
#define ASTRA_CAPABILITY_NETWORK_IRQ    "NETWORK_IRQ"
#define ASTRA_CAPABILITY_NETWORK        "NETWORK"
#define ASTRA_CAPABILITY_NETWORK_LISTEN "NETWORK_LISTEN"

#define ASTRA_NETWORK_PROTOCOL UINT32_C(0x4e455457) /* NETW */
#define ASTRA_NETWORK_VERSION 1u

#define ASTRA_NETWORK_FAMILY_UNSPEC 0u
#define ASTRA_NETWORK_FAMILY_IPV4   1u
#define ASTRA_NETWORK_FAMILY_IPV6   2u

#define ASTRA_NETWORK_TYPE_STREAM   1u
#define ASTRA_NETWORK_TYPE_DATAGRAM 2u

#define ASTRA_NETWORK_PROTOCOL_DEFAULT 0u
#define ASTRA_NETWORK_PROTOCOL_ICMP    1u
#define ASTRA_NETWORK_PROTOCOL_TCP     6u
#define ASTRA_NETWORK_PROTOCOL_UDP     17u
#define ASTRA_NETWORK_PROTOCOL_ICMPV6  58u

/* RFC 1035 presentation form, excluding the terminating NUL. */
#define ASTRA_NETWORK_NAME_MAX 253u

typedef enum AstraNetworkStatus {
    ASTRA_NETWORK_OK = 0u,
    ASTRA_NETWORK_WOULD_BLOCK = 1u,
    ASTRA_NETWORK_IN_PROGRESS = 2u,
    ASTRA_NETWORK_CANCELLED = 3u,
    ASTRA_NETWORK_TIMED_OUT = 4u,
    ASTRA_NETWORK_REFUSED = 5u,
    ASTRA_NETWORK_RESET = 6u,
    ASTRA_NETWORK_UNREACHABLE = 7u,
    ASTRA_NETWORK_ADDRESS_IN_USE = 8u,
    ASTRA_NETWORK_ADDRESS_NOT_AVAILABLE = 9u,
    ASTRA_NETWORK_NAME_NOT_FOUND = 10u,
    ASTRA_NETWORK_NAME_TEMPORARY = 11u,
    ASTRA_NETWORK_PEER_CLOSED = 12u,
    ASTRA_NETWORK_INVALID = 13u,
    ASTRA_NETWORK_ACCESS = 14u,
    ASTRA_NETWORK_RESOURCE_LIMIT = 15u,
    ASTRA_NETWORK_OUT_OF_MEMORY = 16u,
    ASTRA_NETWORK_UNSUPPORTED = 17u,
    ASTRA_NETWORK_IO = 18u,
    ASTRA_NETWORK_PEER_DEAD = 19u,
    ASTRA_NETWORK_BUFFER_TOO_SMALL = 20u,
    ASTRA_NETWORK_FORKED = 21u
} AstraNetworkStatus;

#define ASTRA_NETWORK_READY_READABLE    (1u << 0)
#define ASTRA_NETWORK_READY_WRITABLE    (1u << 1)
#define ASTRA_NETWORK_READY_CONNECTED   (1u << 2)
#define ASTRA_NETWORK_READY_ACCEPTABLE  (1u << 3)
#define ASTRA_NETWORK_READY_PEER_CLOSED (1u << 4)
#define ASTRA_NETWORK_READY_ERROR       (1u << 5)

#define ASTRA_NETWORK_SHUTDOWN_READ  (1u << 0)
#define ASTRA_NETWORK_SHUTDOWN_WRITE (1u << 1)

#define ASTRA_NETWORK_MESSAGE_PEEK       (1u << 0)
#define ASTRA_NETWORK_MESSAGE_WAIT_ALL   (1u << 1)
#define ASTRA_NETWORK_MESSAGE_TRUNCATE   (1u << 2)

enum {
    ASTRA_NETWORK_OPTION_ERROR = 1u,
    ASTRA_NETWORK_OPTION_TYPE = 2u,
    ASTRA_NETWORK_OPTION_REUSE_ADDRESS = 3u,
    ASTRA_NETWORK_OPTION_KEEPALIVE = 4u,
    ASTRA_NETWORK_OPTION_SEND_BUFFER = 5u,
    ASTRA_NETWORK_OPTION_RECEIVE_BUFFER = 6u,
    ASTRA_NETWORK_OPTION_TCP_NO_DELAY = 7u,
    ASTRA_NETWORK_OPTION_IPV6_ONLY = 8u
};

#define ASTRA_NETWORK_CAP_IPV4    (1u << 0)
#define ASTRA_NETWORK_CAP_IPV6    (1u << 1)
#define ASTRA_NETWORK_CAP_TCP     (1u << 2)
#define ASTRA_NETWORK_CAP_UDP     (1u << 3)
#define ASTRA_NETWORK_CAP_RESOLVE (1u << 4)
#define ASTRA_NETWORK_CAP_ICMP    (1u << 5)

#define ASTRA_NETWORK_STATE_LINK_UP (1u << 0)

#define ASTRA_NETWORK_ADDRESS_SIZE 28u
typedef struct AstraNetworkAddress {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t family;
    uint16_t port;
    uint32_t scope_id;
    uint8_t address[16];
} AstraNetworkAddress;

_Static_assert(sizeof(AstraNetworkAddress) == ASTRA_NETWORK_ADDRESS_SIZE,
               "network address ABI changed");

/* Broker-to-device operations. Values append once shipped. */
enum {
    ASTRA_NETWORK_HOST_ENDPOINT_OPEN = 1u,
    ASTRA_NETWORK_HOST_BIND = 2u,
    ASTRA_NETWORK_HOST_CONNECT = 3u,
    ASTRA_NETWORK_HOST_LISTEN = 4u,
    ASTRA_NETWORK_HOST_ACCEPT = 5u,
    ASTRA_NETWORK_HOST_SEND = 6u,
    ASTRA_NETWORK_HOST_RECEIVE = 7u,
    ASTRA_NETWORK_HOST_RESOLVE = 8u,
    ASTRA_NETWORK_HOST_GET_LOCAL_ADDRESS = 9u,
    ASTRA_NETWORK_HOST_GET_PEER_ADDRESS = 10u,
    ASTRA_NETWORK_HOST_GET_OPTION = 11u,
    ASTRA_NETWORK_HOST_SET_OPTION = 12u,
    ASTRA_NETWORK_HOST_SHUTDOWN = 13u,
    ASTRA_NETWORK_HOST_ARM = 14u,
    ASTRA_NETWORK_HOST_CLOSE = 15u,
    ASTRA_NETWORK_HOST_CANCEL = 16u
};

#define ASTRA_NETWORK_HOST_COMMAND_VERSION 1u
#define ASTRA_NETWORK_HOST_COMMAND_SIZE 128u
typedef struct AstraNetworkHostCommand {
    uint32_t size;
    uint16_t version;
    uint16_t operation;
    uint32_t flags;
    uint32_t endpoint;
    uint32_t endpoint_generation;
    uint16_t family;
    uint8_t type;
    uint8_t protocol;
    uint32_t value;
    AstraNetworkAddress address;
    uint32_t data_offset;
    uint32_t data_length;
    uint32_t data_capacity;
    uint32_t result_status;
    uint32_t result_endpoint;
    uint32_t result_generation;
    uint32_t result_value;
    AstraNetworkAddress result_address;
    uint32_t reserved[4];
} AstraNetworkHostCommand;

_Static_assert(sizeof(AstraNetworkHostCommand) ==
                   ASTRA_NETWORK_HOST_COMMAND_SIZE,
               "network host command ABI changed");

#define ASTRA_NETWORK_LEASE_INFO_SIZE 32u
typedef struct AstraNetworkLeaseInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t capabilities;
    uint32_t state_flags;
    uint32_t host_generation;
    uint32_t queue_depth;
    uint32_t maximum_transfer;
    uint32_t active_endpoints;
    uint32_t reserved;
} AstraNetworkLeaseInfo;

#define ASTRA_NETWORK_TRANSPORT_REQUEST_SIZE 24u
typedef struct AstraNetworkTransportRequest {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t buffer;
    uint32_t buffer_offset;
    uint32_t byte_size;
    uint32_t command_count;
    uint32_t reserved;
} AstraNetworkTransportRequest;

#define ASTRA_NETWORK_TRANSPORT_COMPLETION_SIZE 32u
typedef struct AstraNetworkTransportCompletion {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t request;
    uint32_t status;
    uint32_t endpoint;
    uint32_t endpoint_generation;
    uint32_t readiness;
    uint32_t host_generation;
    uint32_t flags;
} AstraNetworkTransportCompletion;

_Static_assert(sizeof(AstraNetworkLeaseInfo) == ASTRA_NETWORK_LEASE_INFO_SIZE,
               "network lease ABI changed");
_Static_assert(sizeof(AstraNetworkTransportRequest) ==
                   ASTRA_NETWORK_TRANSPORT_REQUEST_SIZE,
               "network request ABI changed");
_Static_assert(sizeof(AstraNetworkTransportCompletion) ==
                   ASTRA_NETWORK_TRANSPORT_COMPLETION_SIZE,
               "network completion ABI changed");

enum {
    ASTRA_NETWORK_OPEN_SESSION = 1u,
    ASTRA_NETWORK_OPEN_ENDPOINT = 2u,
    ASTRA_NETWORK_BIND = 3u,
    ASTRA_NETWORK_CONNECT = 4u,
    ASTRA_NETWORK_LISTEN = 5u,
    ASTRA_NETWORK_ACCEPT = 6u,
    ASTRA_NETWORK_SEND = 7u,
    ASTRA_NETWORK_RECEIVE = 8u,
    ASTRA_NETWORK_RESOLVE = 9u,
    ASTRA_NETWORK_GET_LOCAL_ADDRESS = 10u,
    ASTRA_NETWORK_GET_PEER_ADDRESS = 11u,
    ASTRA_NETWORK_GET_OPTION = 12u,
    ASTRA_NETWORK_SET_OPTION = 13u,
    ASTRA_NETWORK_SHUTDOWN = 14u,
    ASTRA_NETWORK_CANCEL = 15u,
    ASTRA_NETWORK_CLOSE = 16u,
    ASTRA_NETWORK_REPLY = 17u,
    ASTRA_NETWORK_DOORBELL = 18u
};

/* One transfer slot covers the complete 16-bit IP packet length. */
#define ASTRA_NETWORK_SLOT_BYTES UINT32_C(0x00010000)
#define ASTRA_NETWORK_SHARED_MAGIC UINT32_C(0x4e534852) /* NSHR */
#define ASTRA_NETWORK_SHARED_VERSION 1u
#define ASTRA_NETWORK_SHARED_METADATA_BYTES 4096u

#define ASTRA_NETWORK_SLOT_FREE         0u
#define ASTRA_NETWORK_SLOT_TX_WRITING   1u
#define ASTRA_NETWORK_SLOT_TX_READY     2u
#define ASTRA_NETWORK_SLOT_TX_IN_FLIGHT 3u
#define ASTRA_NETWORK_SLOT_RX_AVAILABLE 4u
#define ASTRA_NETWORK_SLOT_RX_READING   5u

typedef struct AstraNetworkSharedHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t structure_size;
    uint32_t total_size;
    uint32_t generation;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t tx_slot_count;
    uint32_t rx_slot_count;
    uint32_t readiness_sequence;
    uint32_t dropped_datagrams;
    uint32_t reserved[6];
} AstraNetworkSharedHeader;

_Static_assert(sizeof(AstraNetworkSharedHeader) == 64u,
               "network shared header changed");

typedef struct AstraNetworkRequestMessage {
    AstraMessageHeader header;
    uint32_t session;
    uint32_t endpoint;
    uint32_t generation;
    uint32_t flags;
    uint32_t value;
    AstraNetworkAddress address;
    uint32_t slot;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved;
} AstraNetworkRequestMessage;

typedef struct AstraNetworkReplyMessage {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t session;
    uint32_t endpoint;
    uint32_t generation;
    uint32_t value;
    AstraNetworkAddress address;
    uint32_t transferred;
    uint32_t required;
    uint32_t readiness;
    uint32_t reserved;
} AstraNetworkReplyMessage;

#define ASTRA_NETWORK_REQUEST_MESSAGE_SIZE 88u
#define ASTRA_NETWORK_REPLY_MESSAGE_SIZE 88u

_Static_assert(sizeof(AstraNetworkRequestMessage) ==
                   ASTRA_NETWORK_REQUEST_MESSAGE_SIZE,
               "network request message changed");
_Static_assert(sizeof(AstraNetworkReplyMessage) ==
                   ASTRA_NETWORK_REPLY_MESSAGE_SIZE,
               "network reply message changed");

typedef struct AstraNetworkSharedSlot {
    uint32_t generation;
    uint32_t state;
    uint32_t endpoint;
    uint32_t flags;
    uint32_t offset;
    uint32_t length;
    AstraNetworkAddress address;
    uint32_t reserved[2];
} AstraNetworkSharedSlot;

_Static_assert(sizeof(AstraNetworkSharedSlot) == 60u,
               "network shared slot changed");

#endif
