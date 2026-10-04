#ifndef ASTRA_NETWORK_H
#define ASTRA_NETWORK_H

/**
 * @file network.h
 * @brief Stable Astra network ABI.
 *
 * Host descriptors, sockaddr layouts, errno values, and backend-private
 * pointers never cross this boundary. Three layers share this file:
 *  - the kernel's `ASTRA_SYSCALL_NETWORK_QUERY` / `ASTRA_SYSCALL_NETWORK_EXECUTE`
 *    device syscalls, which hand a batch of ::AstraNetworkHostCommand entries
 *    to the host network backend through a DMA buffer
 *    (::AstraNetworkTransportRequest / ::AstraNetworkTransportCompletion);
 *  - the network service (`sw/userspace/services/network`), which issues
 *    those commands on behalf of client processes and answers them over a
 *    message-port protocol (::AstraNetworkRequestMessage /
 *    ::AstraNetworkReplyMessage) plus a shared transfer area
 *    (::AstraNetworkSharedHeader / ::AstraNetworkSharedSlot);
 *  - client code (`sw/userspace/network`, the POSIX socket layer), which
 *    speaks that message-port protocol.
 */

#include <astra/compiler.h>
#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/syscall.h>

/** Device class identifier (the ASCII characters "NETW") for the network device. */
#define ASTRA_DEVICE_CLASS_NETWORK UINT32_C(0x4e455457) /* NETW */
/** Device identifier of the one network device instance. */
#define ASTRA_DEVICE_ID_NETWORK0   UINT32_C(0x4e450001)

/** Capability name for the leased network device handle. */
#define ASTRA_CAPABILITY_NETWORK_DEVICE "NETWORK_DEVICE"
/** Capability name for the network device's interrupt handle. */
#define ASTRA_CAPABILITY_NETWORK_IRQ    "NETWORK_IRQ"
/** Capability name granting a client-only network session (no bind/listen). */
#define ASTRA_CAPABILITY_NETWORK        "NETWORK"
/** Capability name granting a network session that may bind and listen. */
#define ASTRA_CAPABILITY_NETWORK_LISTEN "NETWORK_LISTEN"

/** Message-port protocol identifier (the ASCII characters "NETW") for the network service. */
#define ASTRA_NETWORK_PROTOCOL UINT32_C(0x4e455457) /* NETW */
/** Wire-format version of the network service's message-port protocol. */
#define ASTRA_NETWORK_VERSION 1u

/** Unspecified address family; valid only for an all-zero address. */
#define ASTRA_NETWORK_FAMILY_UNSPEC 0u
/** IPv4 address family. */
#define ASTRA_NETWORK_FAMILY_IPV4   1u
/** IPv6 address family. */
#define ASTRA_NETWORK_FAMILY_IPV6   2u

/** Reliable, connection-oriented byte-stream endpoint type (TCP-like). */
#define ASTRA_NETWORK_TYPE_STREAM   1u
/** Connectionless datagram endpoint type (UDP-like). */
#define ASTRA_NETWORK_TYPE_DATAGRAM 2u

/** No specific transport protocol; let the endpoint type pick the default. */
#define ASTRA_NETWORK_PROTOCOL_DEFAULT 0u
/** IANA protocol number for ICMP. */
#define ASTRA_NETWORK_PROTOCOL_ICMP    1u
/** IANA protocol number for TCP. */
#define ASTRA_NETWORK_PROTOCOL_TCP     6u
/** IANA protocol number for UDP. */
#define ASTRA_NETWORK_PROTOCOL_UDP     17u
/** IANA protocol number for ICMPv6. */
#define ASTRA_NETWORK_PROTOCOL_ICMPV6  58u

/** Longest RFC 1035 presentation-form domain name, excluding the terminating NUL. */
#define ASTRA_NETWORK_NAME_MAX 253u

/**
 * Outcome of a network operation.
 *
 * Reported by the kernel's network device syscalls, the host backend's
 * ::AstraNetworkHostCommand results, and the network service's
 * ::AstraNetworkReplyMessage; the POSIX socket layer (`sw/userspace/posix`)
 * maps every value to an errno.
 */
typedef enum AstraNetworkStatus {
    /** The operation completed successfully. */
    ASTRA_NETWORK_OK = 0u,
    /** The operation would block; retry later (POSIX `EAGAIN`). */
    ASTRA_NETWORK_WOULD_BLOCK = 1u,
    /** A non-blocking connect is still in progress (POSIX `EINPROGRESS`). */
    ASTRA_NETWORK_IN_PROGRESS = 2u,
    /** The operation was cancelled before it completed (POSIX `EINTR`). */
    ASTRA_NETWORK_CANCELLED = 3u,
    /** The operation timed out (POSIX `ETIMEDOUT`). */
    ASTRA_NETWORK_TIMED_OUT = 4u,
    /** The peer refused the connection (POSIX `ECONNREFUSED`). */
    ASTRA_NETWORK_REFUSED = 5u,
    /** The peer reset the connection (POSIX `ECONNRESET`). */
    ASTRA_NETWORK_RESET = 6u,
    /** The destination host or network is unreachable (POSIX `EHOSTUNREACH`). */
    ASTRA_NETWORK_UNREACHABLE = 7u,
    /** The requested local address is already in use (POSIX `EADDRINUSE`). */
    ASTRA_NETWORK_ADDRESS_IN_USE = 8u,
    /** The requested local address is not available (POSIX `EADDRNOTAVAIL`). */
    ASTRA_NETWORK_ADDRESS_NOT_AVAILABLE = 9u,
    /** Name resolution found no such name (POSIX `EAI_NONAME`). */
    ASTRA_NETWORK_NAME_NOT_FOUND = 10u,
    /** Name resolution failed temporarily; retry later (POSIX `EAI_AGAIN`). */
    ASTRA_NETWORK_NAME_TEMPORARY = 11u,
    /** The peer closed its end in an orderly shutdown; not treated as an error. */
    ASTRA_NETWORK_PEER_CLOSED = 12u,
    /** An argument was invalid (POSIX `EINVAL`). */
    ASTRA_NETWORK_INVALID = 13u,
    /** The caller lacks the rights or capability for this operation (POSIX `EACCES`). */
    ASTRA_NETWORK_ACCESS = 14u,
    /** A resource limit (endpoints, sessions, host queue depth) was reached (POSIX `ENOBUFS`). */
    ASTRA_NETWORK_RESOURCE_LIMIT = 15u,
    /** The operation ran out of memory (POSIX `ENOMEM`). */
    ASTRA_NETWORK_OUT_OF_MEMORY = 16u,
    /** The operation is not supported (POSIX `EOPNOTSUPP`). */
    ASTRA_NETWORK_UNSUPPORTED = 17u,
    /** An unclassified transport or device I/O failure (POSIX `EIO`). */
    ASTRA_NETWORK_IO = 18u,
    /** The network device, host backend, or service on the other end of this handle is gone (POSIX `EPIPE`). */
    ASTRA_NETWORK_PEER_DEAD = 19u,
    /** The caller's buffer was too small for the result (POSIX `ENOBUFS`). */
    ASTRA_NETWORK_BUFFER_TOO_SMALL = 20u,
    /**
     * Reserved for an operation attempted on a network session or endpoint
     * after the owning process forked. Not currently produced by the
     * kernel, host backend, or network service; the POSIX socket layer maps
     * it to `ENOTSUP`.
     */
    ASTRA_NETWORK_FORKED = 21u
} AstraNetworkStatus;

/** Readiness bit: the endpoint has data available to read. */
#define ASTRA_NETWORK_READY_READABLE    (1u << 0)
/** Readiness bit: the endpoint can accept a write without blocking. */
#define ASTRA_NETWORK_READY_WRITABLE    (1u << 1)
/** Readiness bit: a non-blocking connect has completed. */
#define ASTRA_NETWORK_READY_CONNECTED   (1u << 2)
/** Readiness bit: a listening endpoint has a connection to accept. */
#define ASTRA_NETWORK_READY_ACCEPTABLE  (1u << 3)
/** Readiness bit: the peer closed its end. */
#define ASTRA_NETWORK_READY_PEER_CLOSED (1u << 4)
/** Readiness bit: the endpoint has a pending error (see `ASTRA_NETWORK_OPTION_ERROR`). */
#define ASTRA_NETWORK_READY_ERROR       (1u << 5)

/** `ASTRA_NETWORK_SHUTDOWN` bit: disable further reads on the endpoint. */
#define ASTRA_NETWORK_SHUTDOWN_READ  (1u << 0)
/** `ASTRA_NETWORK_SHUTDOWN` bit: disable further writes on the endpoint. */
#define ASTRA_NETWORK_SHUTDOWN_WRITE (1u << 1)

/** `ASTRA_NETWORK_RECEIVE` flag: read the data without removing it from the queue (like POSIX `MSG_PEEK`). */
#define ASTRA_NETWORK_MESSAGE_PEEK       (1u << 0)
/** `ASTRA_NETWORK_RECEIVE` flag: wait for the full requested length (like POSIX `MSG_WAITALL`). */
#define ASTRA_NETWORK_MESSAGE_WAIT_ALL   (1u << 1)
/** `ASTRA_NETWORK_RECEIVE` flag: the datagram was longer than the buffer and was truncated (like POSIX `MSG_TRUNC`). */
#define ASTRA_NETWORK_MESSAGE_TRUNCATE   (1u << 2)

/**
 * Socket-option identifiers for `ASTRA_NETWORK_GET_OPTION` /
 * `ASTRA_NETWORK_SET_OPTION`, mirrored by the POSIX `getsockopt`/`setsockopt`
 * translation in `sw/userspace/posix`.
 */
enum {
    /** The endpoint's pending error, read and cleared (like POSIX `SO_ERROR`). */
    ASTRA_NETWORK_OPTION_ERROR = 1u,
    /** The endpoint's type, `ASTRA_NETWORK_TYPE_STREAM` or `ASTRA_NETWORK_TYPE_DATAGRAM` (like POSIX `SO_TYPE`). */
    ASTRA_NETWORK_OPTION_TYPE = 2u,
    /** Allow binding a local address still in `TIME_WAIT` (like POSIX `SO_REUSEADDR`). */
    ASTRA_NETWORK_OPTION_REUSE_ADDRESS = 3u,
    /** Enable TCP keepalive probing (like POSIX `SO_KEEPALIVE`). */
    ASTRA_NETWORK_OPTION_KEEPALIVE = 4u,
    /** The endpoint's send buffer size in bytes (like POSIX `SO_SNDBUF`). */
    ASTRA_NETWORK_OPTION_SEND_BUFFER = 5u,
    /** The endpoint's receive buffer size in bytes (like POSIX `SO_RCVBUF`). */
    ASTRA_NETWORK_OPTION_RECEIVE_BUFFER = 6u,
    /** Disable Nagle's algorithm on a TCP endpoint (like POSIX `TCP_NODELAY`). */
    ASTRA_NETWORK_OPTION_TCP_NO_DELAY = 7u,
    /** Restrict an IPv6 endpoint to IPv6 only (like POSIX `IPV6_V6ONLY`). */
    ASTRA_NETWORK_OPTION_IPV6_ONLY = 8u
};

/** Device capability bit: IPv4 is supported. */
#define ASTRA_NETWORK_CAP_IPV4    (1u << 0)
/** Device capability bit: IPv6 is supported. */
#define ASTRA_NETWORK_CAP_IPV6    (1u << 1)
/** Device capability bit: TCP is supported. */
#define ASTRA_NETWORK_CAP_TCP     (1u << 2)
/** Device capability bit: UDP is supported. */
#define ASTRA_NETWORK_CAP_UDP     (1u << 3)
/** Device capability bit: name resolution (`ASTRA_NETWORK_HOST_RESOLVE` and friends) is supported. */
#define ASTRA_NETWORK_CAP_RESOLVE (1u << 4)
/** Device capability bit: ICMP is supported. */
#define ASTRA_NETWORK_CAP_ICMP    (1u << 5)

/** ::AstraNetworkLeaseInfo state-flags bit: the link is up. */
#define ASTRA_NETWORK_STATE_LINK_UP (1u << 0)

/** Wire size of ::AstraNetworkAddress. */
#define ASTRA_NETWORK_ADDRESS_SIZE 28u

/**
 * A network address: an IPv4 or IPv6 address, a port, and an IPv6 scope id.
 *
 * `address` holds the raw address bytes in network byte order, as in
 * `struct in_addr` / `struct in6_addr`: for ::ASTRA_NETWORK_FAMILY_IPV4 only
 * the first 4 bytes are significant and the rest must be zero; for
 * ::ASTRA_NETWORK_FAMILY_IPV6 all 16 bytes are significant. `port` is in
 * host byte order, unlike a POSIX `sin_port`/`sin6_port`.
 */
typedef struct AstraNetworkAddress {
    /** Must equal `sizeof(AstraNetworkAddress)`. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Address family: one of `ASTRA_NETWORK_FAMILY_*`. */
    uint16_t family;
    /** Port number, in host byte order. */
    uint16_t port;
    /** IPv6 zone/scope id (`sin6_scope_id`); zero for IPv4 or unspecified. */
    uint32_t scope_id;
    /** Raw address bytes, network byte order; see the struct description. */
    uint8_t address[16];
} AstraNetworkAddress;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkAddress) == ASTRA_NETWORK_ADDRESS_SIZE,
               "network address ABI changed");
/** @endcond */

/** Longest interface name, matching Linux `IFNAMSIZ` and including the terminating NUL. */
#define ASTRA_NETWORK_INTERFACE_NAME_SIZE 16u
/** Wire size of ::AstraNetworkInterfaceAddress. */
#define ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE 44u

/** One address assigned to one host network interface, as returned by `ASTRA_NETWORK_HOST_INTERFACES`. */
typedef struct AstraNetworkInterfaceAddress {
    /** The interface's address. */
    AstraNetworkAddress address;
    /** NUL-terminated interface name. */
    char name[ASTRA_NETWORK_INTERFACE_NAME_SIZE];
} AstraNetworkInterfaceAddress;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkInterfaceAddress) ==
                   ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE,
               "network interface-address ABI changed");
/** @endcond */

/**
 * Broker-to-device operations carried in `AstraNetworkHostCommand::operation`.
 *
 * Issued by the network service to the host backend through
 * `ASTRA_SYSCALL_NETWORK_EXECUTE`. Values append once shipped.
 */
enum {
    /**
     * Open a host-side endpoint of the family, type, and protocol given in
     * `family`/`type`/`protocol`, returning its identity in
     * `result_endpoint`/`result_generation`.
     */
    ASTRA_NETWORK_HOST_ENDPOINT_OPEN = 1u,
    /** Bind the endpoint named by `endpoint`/`endpoint_generation` to the local address in `address`. */
    ASTRA_NETWORK_HOST_BIND = 2u,
    /** Connect the endpoint named by `endpoint`/`endpoint_generation` to the remote address in `address`. */
    ASTRA_NETWORK_HOST_CONNECT = 3u,
    /** Mark the endpoint as listening, with `value` as the requested backlog. */
    ASTRA_NETWORK_HOST_LISTEN = 4u,
    /**
     * Accept one pending connection on a listening endpoint, returning the
     * new endpoint's identity in `result_endpoint`/`result_generation` and
     * the peer's address in `result_address`.
     */
    ASTRA_NETWORK_HOST_ACCEPT = 5u,
    /**
     * Send the bytes staged at `data_offset`/`data_length` (`flags` carries
     * `ASTRA_NETWORK_MESSAGE_*`, and for a datagram endpoint `address` is the
     * destination), returning the byte count sent in `result_value`.
     */
    ASTRA_NETWORK_HOST_SEND = 6u,
    /**
     * Receive into the buffer described by `data_offset`/`data_capacity`
     * (`flags` carries `ASTRA_NETWORK_MESSAGE_*`), returning the byte count
     * received in `result_value` and, for a datagram endpoint, the sender's
     * address in `result_address`.
     */
    ASTRA_NETWORK_HOST_RECEIVE = 7u,
    /**
     * Begin or poll a forward name lookup. Submitted with `value` zero and
     * the query name staged at `data_offset`/`data_length`, it returns a
     * pending token in `result_value`; polled by resubmitting with `value`
     * set to that token, it returns the resolved ::AstraNetworkAddress
     * array at `data_offset` and their count in `result_value` once
     * complete.
     */
    ASTRA_NETWORK_HOST_RESOLVE = 8u,
    /** Return the endpoint's local address in `result_address`. */
    ASTRA_NETWORK_HOST_GET_LOCAL_ADDRESS = 9u,
    /** Return the endpoint's connected peer address in `result_address`. */
    ASTRA_NETWORK_HOST_GET_PEER_ADDRESS = 10u,
    /** Return the current value of the `ASTRA_NETWORK_OPTION_*` option named by `value`, in `result_value`. */
    ASTRA_NETWORK_HOST_GET_OPTION = 11u,
    /** Set the `ASTRA_NETWORK_OPTION_*` option named by `value` to the new value carried in `flags`. */
    ASTRA_NETWORK_HOST_SET_OPTION = 12u,
    /** Disable the read and/or write half named by the `ASTRA_NETWORK_SHUTDOWN_*` bits in `flags`. */
    ASTRA_NETWORK_HOST_SHUTDOWN = 13u,
    /**
     * Report the endpoint's current `ASTRA_NETWORK_READY_*` bits in
     * `result_value`, for the interest bits requested in `flags`.
     */
    ASTRA_NETWORK_HOST_ARM = 14u,
    /** Release the endpoint named by `endpoint`/`endpoint_generation`. */
    ASTRA_NETWORK_HOST_CLOSE = 15u,
    /** Cancel an outstanding `ASTRA_NETWORK_HOST_RESOLVE` or `ASTRA_NETWORK_HOST_REVERSE_RESOLVE` identified by the pending token in `value`. */
    ASTRA_NETWORK_HOST_CANCEL = 16u,
    /**
     * Begin or poll a reverse name lookup of the address in `address`,
     * staged the same way as `ASTRA_NETWORK_HOST_RESOLVE` but returning the
     * resolved presentation-form name as text at `data_offset`, with its
     * byte length (including the terminating NUL) in `result_value`.
     */
    ASTRA_NETWORK_HOST_REVERSE_RESOLVE = 17u,
    /**
     * List the host's network interfaces as an array of
     * ::AstraNetworkInterfaceAddress at `data_offset`, returning their
     * count in `result_value`.
     */
    ASTRA_NETWORK_HOST_INTERFACES = 18u
};

/** Wire-format version of ::AstraNetworkHostCommand. */
#define ASTRA_NETWORK_HOST_COMMAND_VERSION 1u
/** Wire size of ::AstraNetworkHostCommand. */
#define ASTRA_NETWORK_HOST_COMMAND_SIZE 128u

/**
 * One operation sent from the network service to the host backend, and its
 * result, staged in a DMA buffer and executed with
 * `ASTRA_SYSCALL_NETWORK_EXECUTE`.
 *
 * Field meaning for `value`/`result_value`, `flags`, and `address` depends
 * on `operation`; see the `ASTRA_NETWORK_HOST_*` enumerators.
 */
typedef struct AstraNetworkHostCommand {
    /** Must equal `sizeof(AstraNetworkHostCommand)`. */
    uint32_t size;
    /** Must equal ::ASTRA_NETWORK_HOST_COMMAND_VERSION. */
    uint16_t version;
    /** Operation to perform: one of `ASTRA_NETWORK_HOST_*`. */
    uint16_t operation;
    /** Operation-specific flags; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    uint32_t flags;
    /** Target endpoint identifier; zero for an operation with no endpoint. */
    uint32_t endpoint;
    /** Target endpoint's generation, paired with `endpoint`. */
    uint32_t endpoint_generation;
    /** Address family for `ASTRA_NETWORK_HOST_ENDPOINT_OPEN`/`ASTRA_NETWORK_HOST_RESOLVE`. */
    uint16_t family;
    /** Endpoint type (`ASTRA_NETWORK_TYPE_*`) for `ASTRA_NETWORK_HOST_ENDPOINT_OPEN`/`ASTRA_NETWORK_HOST_RESOLVE`. */
    uint8_t type;
    /** Transport protocol for `ASTRA_NETWORK_HOST_ENDPOINT_OPEN`/`ASTRA_NETWORK_HOST_RESOLVE`. */
    uint8_t protocol;
    /** Operation-specific scalar input; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    uint32_t value;
    /** Operation-specific address input; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    AstraNetworkAddress address;
    /** Byte offset of the operation's data payload from the start of the DMA buffer; zero when there is none. */
    uint32_t data_offset;
    /** Byte length of the data payload supplied to the host (an input). */
    uint32_t data_length;
    /** Byte capacity of the data payload available for the host to fill (an input). */
    uint32_t data_capacity;
    /** Outcome of the operation: an ::AstraNetworkStatus value. */
    uint32_t result_status;
    /** Resulting endpoint identifier; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    uint32_t result_endpoint;
    /** Resulting endpoint's generation, paired with `result_endpoint`. */
    uint32_t result_generation;
    /** Operation-specific scalar result; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    uint32_t result_value;
    /** Operation-specific address result; see the `ASTRA_NETWORK_HOST_*` enumerators. */
    AstraNetworkAddress result_address;
    /** Reserved; must be zero. */
    uint32_t reserved[4];
} AstraNetworkHostCommand;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkHostCommand) ==
                   ASTRA_NETWORK_HOST_COMMAND_SIZE,
               "network host command ABI changed");
/** @endcond */

/** Wire size of ::AstraNetworkLeaseInfo. */
#define ASTRA_NETWORK_LEASE_INFO_SIZE 32u

/** Snapshot of the network device's capabilities and state, returned by `ASTRA_SYSCALL_NETWORK_QUERY`. */
typedef struct AstraNetworkLeaseInfo {
    /** Must equal `sizeof(AstraNetworkLeaseInfo)`. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Device capability bits: `ASTRA_NETWORK_CAP_*`. */
    uint32_t capabilities;
    /** Device state bits: `ASTRA_NETWORK_STATE_*`. */
    uint32_t state_flags;
    /** Device generation, bumped whenever the host backend resets. */
    uint32_t host_generation;
    /** Depth of the host's command queue. */
    uint32_t queue_depth;
    /** Largest byte size the host accepts in one `ASTRA_SYSCALL_NETWORK_EXECUTE` transfer. */
    uint32_t maximum_transfer;
    /** Number of endpoints currently open on the host. */
    uint32_t active_endpoints;
    /** Reserved; always zero. */
    uint32_t reserved;
} AstraNetworkLeaseInfo;

/** Wire size of ::AstraNetworkTransportRequest. */
#define ASTRA_NETWORK_TRANSPORT_REQUEST_SIZE 24u

/** A batch of ::AstraNetworkHostCommand entries submitted to `ASTRA_SYSCALL_NETWORK_EXECUTE`. */
typedef struct AstraNetworkTransportRequest {
    /** Must equal `sizeof(AstraNetworkTransportRequest)`. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Handle of the DMA buffer holding the command batch. */
    uint32_t buffer;
    /** Byte offset of the command batch within the DMA buffer. */
    uint32_t buffer_offset;
    /** Total byte size of the command batch, `command_count * ASTRA_NETWORK_HOST_COMMAND_SIZE` or more. */
    uint32_t byte_size;
    /** Number of ::AstraNetworkHostCommand entries packed in the batch. */
    uint32_t command_count;
    /** Reserved; must be zero. */
    uint32_t reserved;
} AstraNetworkTransportRequest;

/** Wire size of ::AstraNetworkTransportCompletion. */
#define ASTRA_NETWORK_TRANSPORT_COMPLETION_SIZE 32u

/**
 * One terminal result for a transport request submitted through
 * ::AstraNetworkTransportRequest.
 *
 * Declared for symmetry with the other device ABIs (compare
 * `AstraBlockCompletion`); the reference kernel and network service do not
 * currently produce or consume it, since `ASTRA_SYSCALL_NETWORK_EXECUTE`
 * instead reports its outcome directly through the syscall return value and
 * executed-command count.
 */
typedef struct AstraNetworkTransportCompletion {
    /** Must equal `sizeof(AstraNetworkTransportCompletion)`. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Identifies the ::AstraNetworkTransportRequest this completion answers. */
    uint32_t request;
    /** Outcome of the request: an ::AstraNetworkStatus value. */
    uint32_t status;
    /** Endpoint the completion pertains to. */
    uint32_t endpoint;
    /** Endpoint's generation, paired with `endpoint`. */
    uint32_t endpoint_generation;
    /** Readiness bits: `ASTRA_NETWORK_READY_*`. */
    uint32_t readiness;
    /** Device generation at the time of completion. */
    uint32_t host_generation;
    /** Reserved flags; must be zero. */
    uint32_t flags;
} AstraNetworkTransportCompletion;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkLeaseInfo) == ASTRA_NETWORK_LEASE_INFO_SIZE,
               "network lease ABI changed");
_Static_assert(sizeof(AstraNetworkTransportRequest) ==
                   ASTRA_NETWORK_TRANSPORT_REQUEST_SIZE,
               "network request ABI changed");
_Static_assert(sizeof(AstraNetworkTransportCompletion) ==
                   ASTRA_NETWORK_TRANSPORT_COMPLETION_SIZE,
               "network completion ABI changed");
/** @endcond */

/**
 * Session- and endpoint-level operations carried in
 * `AstraNetworkRequestMessage::header::operation`.
 *
 * Sent by client code to the network service over a message port.
 */
enum {
    /**
     * Open a session on a service port, handing over a reply-port handle
     * and a shared transfer-area handle; `session` is zero, since the
     * session does not exist yet.
     */
    ASTRA_NETWORK_OPEN_SESSION = 1u,
    /**
     * Open a new endpoint within the session, with `flags` packing the
     * endpoint type in bits 8-15 and the transport protocol in bits 0-7,
     * and `address.family` giving the address family.
     */
    ASTRA_NETWORK_OPEN_ENDPOINT = 2u,
    /** Bind the endpoint to the local address in `address`. */
    ASTRA_NETWORK_BIND = 3u,
    /** Connect the endpoint to the remote address in `address`. */
    ASTRA_NETWORK_CONNECT = 4u,
    /** Mark a bound endpoint as listening, with `value` as the requested backlog; requires the session's listen capability. */
    ASTRA_NETWORK_LISTEN = 5u,
    /** Accept one pending connection on a listening endpoint; requires the session's listen capability. */
    ASTRA_NETWORK_ACCEPT = 6u,
    /** Send the bytes staged in the shared transmit slot named by `slot`/`length` (and, for a datagram endpoint, the destination in `address`). */
    ASTRA_NETWORK_SEND = 7u,
    /** Receive into the shared receive slot named by `slot`/`length`. */
    ASTRA_NETWORK_RECEIVE = 8u,
    /**
     * Begin or poll a forward name lookup: a session-level operation, not
     * scoped to an endpoint. The query name is staged in a shared transmit
     * slot and the resolved addresses are delivered into a shared receive
     * slot.
     */
    ASTRA_NETWORK_RESOLVE = 9u,
    /** Return the endpoint's local address. */
    ASTRA_NETWORK_GET_LOCAL_ADDRESS = 10u,
    /** Return the endpoint's connected peer address. */
    ASTRA_NETWORK_GET_PEER_ADDRESS = 11u,
    /** Return the current value of the `ASTRA_NETWORK_OPTION_*` option named by `value`. */
    ASTRA_NETWORK_GET_OPTION = 12u,
    /** Set the `ASTRA_NETWORK_OPTION_*` option named by `value` to the value carried in `flags`. */
    ASTRA_NETWORK_SET_OPTION = 13u,
    /** Disable the read and/or write half named by the `ASTRA_NETWORK_SHUTDOWN_*` bits in `flags`. */
    ASTRA_NETWORK_SHUTDOWN = 14u,
    /** Cancel an outstanding `ASTRA_NETWORK_RESOLVE` or `ASTRA_NETWORK_REVERSE_RESOLVE` identified by the pending token in `value`. */
    ASTRA_NETWORK_CANCEL = 15u,
    /** Close the endpoint, or, when `endpoint` is zero, the whole session. */
    ASTRA_NETWORK_CLOSE = 16u,
    /** Reserved operation code; not currently sent by the network library or matched by the network service. */
    ASTRA_NETWORK_REPLY = 17u,
    /**
     * Poll or arm readiness: returns the endpoint's already-pending
     * readiness bits immediately, or asks the service to re-arm the
     * interest bits in `flags` with the host and signal the endpoint's
     * readiness event once one becomes ready.
     */
    ASTRA_NETWORK_DOORBELL = 18u,
    /** Begin or poll a reverse name lookup of the address in `address`; a session-level operation, staged like `ASTRA_NETWORK_RESOLVE`. */
    ASTRA_NETWORK_REVERSE_RESOLVE = 19u,
    /** List the host's network interfaces into a shared receive slot; a session-level operation. */
    ASTRA_NETWORK_INTERFACES = 20u
};

/** Byte size of one shared transfer slot: the complete 16-bit IP packet length. */
#define ASTRA_NETWORK_SLOT_BYTES UINT32_C(0x00010000)
/** Shared transfer-area signature (the ASCII characters "NSHR"). */
#define ASTRA_NETWORK_SHARED_MAGIC UINT32_C(0x4e534852) /* NSHR */
/** Wire-format version of the shared transfer area. */
#define ASTRA_NETWORK_SHARED_VERSION 1u
/** Byte size of the shared transfer area's fixed header-plus-slot-table region. */
#define ASTRA_NETWORK_SHARED_METADATA_BYTES 4096u

/** Shared-slot state: the slot is not in use by any pending transfer (the implicit state of newly initialized shared memory). */
#define ASTRA_NETWORK_SLOT_FREE         0u
/** Shared-slot state: reserved for a producer actively copying payload bytes into the slot before it is ready; not currently set by the reference client, which assigns `ASTRA_NETWORK_SLOT_TX_READY` directly. */
#define ASTRA_NETWORK_SLOT_TX_WRITING   1u
/** Shared-slot state: the payload is in place and the slot is ready for `ASTRA_NETWORK_SEND` to hand to the host. */
#define ASTRA_NETWORK_SLOT_TX_READY     2u
/** Shared-slot state: reserved for a send the service has handed to the host but not yet completed; not currently set, since the reference service completes `ASTRA_NETWORK_SEND` synchronously within one request/reply exchange. */
#define ASTRA_NETWORK_SLOT_TX_IN_FLIGHT 3u
/** Shared-slot state: reserved for a slot already holding data delivered to the client; not currently set, since the reference service uses `ASTRA_NETWORK_SLOT_RX_READING` for the handoff. */
#define ASTRA_NETWORK_SLOT_RX_AVAILABLE 4u
/** Shared-slot state: the client has claimed the slot for `ASTRA_NETWORK_RECEIVE` and the service may fill it with incoming bytes. */
#define ASTRA_NETWORK_SLOT_RX_READING   5u

/** Fixed header of a session's shared transfer area, immediately followed by its slot table. */
typedef struct AstraNetworkSharedHeader {
    /** Must equal ::ASTRA_NETWORK_SHARED_MAGIC. */
    uint32_t magic;
    /** Must equal ::ASTRA_NETWORK_SHARED_VERSION. */
    uint16_t version;
    /** Must equal `sizeof(AstraNetworkSharedHeader)`. */
    uint16_t structure_size;
    /** Total byte size of the shared area, header plus slot table plus slot payloads. */
    uint32_t total_size;
    /** Session generation stamped by the network service when the session opens; validates every subsequent access. */
    uint32_t generation;
    /** Byte size of one slot's payload; always ::ASTRA_NETWORK_SLOT_BYTES. */
    uint32_t slot_size;
    /** Total number of slots, `tx_slot_count + rx_slot_count`. */
    uint32_t slot_count;
    /** Number of slots, starting at index 0, reserved for outbound transfers. */
    uint32_t tx_slot_count;
    /** Number of slots, starting at index `tx_slot_count`, reserved for inbound transfers. */
    uint32_t rx_slot_count;
    /** Reserved counter for readiness-change sequencing; not currently written by the reference implementation. */
    uint32_t readiness_sequence;
    /** Reserved counter for dropped datagrams; not currently written by the reference implementation. */
    uint32_t dropped_datagrams;
    /** Reserved; always zero. */
    uint32_t reserved[6];
} AstraNetworkSharedHeader;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkSharedHeader) == 64u,
               "network shared header changed");
/** @endcond */

/**
 * A request from client code to the network service, sent over a session's
 * or endpoint's control message port.
 *
 * Field meaning for `flags`, `value`, and `address` depends on
 * `header.operation`; see the `ASTRA_NETWORK_*` enumerators.
 */
typedef struct AstraNetworkRequestMessage {
    /** Common message prefix; `operation` is one of `ASTRA_NETWORK_*`. */
    AstraMessageHeader header;
    /** Target session identifier; zero in an `ASTRA_NETWORK_OPEN_SESSION` request, which has no session yet. */
    uint32_t session;
    /** Target endpoint identifier; zero for a session-level operation. */
    uint32_t endpoint;
    /** Target endpoint's generation, paired with `endpoint`; must match the endpoint bound to this request's port. */
    uint32_t generation;
    /** Operation-specific flags; see the `ASTRA_NETWORK_*` enumerators. */
    uint32_t flags;
    /** Operation-specific scalar input; see the `ASTRA_NETWORK_*` enumerators. */
    uint32_t value;
    /** Operation-specific address input; see the `ASTRA_NETWORK_*` enumerators. */
    AstraNetworkAddress address;
    /** Index into the session's shared slot table, for an operation that transfers payload bytes. */
    uint32_t slot;
    /** Reserved byte offset of the payload within the slot; the reference client always leaves this zero and the service reads the slot's own `offset` field instead. */
    uint32_t offset;
    /** Byte length of the payload in `slot`. */
    uint32_t length;
    /** Reserved; must be zero. */
    uint32_t reserved;
} AstraNetworkRequestMessage;

/**
 * The network service's reply to an ::AstraNetworkRequestMessage.
 *
 * Field meaning for `value`, `address`, and `transferred` depends on the
 * request's operation; see the `ASTRA_NETWORK_*` enumerators.
 */
typedef struct AstraNetworkReplyMessage {
    /** Common message prefix; `operation` echoes the request's. */
    AstraMessageHeader header;
    /** Outcome of the operation: an ::AstraNetworkStatus value. */
    uint32_t status;
    /** Session identifier, echoed back (or newly assigned, for `ASTRA_NETWORK_OPEN_SESSION`). */
    uint32_t session;
    /** Resulting endpoint identifier; see the `ASTRA_NETWORK_*` enumerators. */
    uint32_t endpoint;
    /** Resulting generation: the session's generation for `ASTRA_NETWORK_OPEN_SESSION`, the endpoint's for `ASTRA_NETWORK_OPEN_ENDPOINT`/`ASTRA_NETWORK_ACCEPT`. */
    uint32_t generation;
    /** Operation-specific scalar result; see the `ASTRA_NETWORK_*` enumerators. */
    uint32_t value;
    /** Operation-specific address result; see the `ASTRA_NETWORK_*` enumerators. */
    AstraNetworkAddress address;
    /** Count of bytes or array entries transferred into or out of the shared slot; the unit depends on the operation. */
    uint32_t transferred;
    /** Reserved; not currently written by the reference network service. */
    uint32_t required;
    /** Readiness bits (`ASTRA_NETWORK_READY_*`) for `ASTRA_NETWORK_DOORBELL`. */
    uint32_t readiness;
    /** Reserved; must be zero. */
    uint32_t reserved;
} AstraNetworkReplyMessage;

/** Wire size of ::AstraNetworkRequestMessage. */
#define ASTRA_NETWORK_REQUEST_MESSAGE_SIZE 88u
/** Wire size of ::AstraNetworkReplyMessage. */
#define ASTRA_NETWORK_REPLY_MESSAGE_SIZE 88u

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkRequestMessage) ==
                   ASTRA_NETWORK_REQUEST_MESSAGE_SIZE,
               "network request message changed");
_Static_assert(sizeof(AstraNetworkReplyMessage) ==
                   ASTRA_NETWORK_REPLY_MESSAGE_SIZE,
               "network reply message changed");
/** @endcond */

/** One slot of a session's shared transfer area, describing one in-flight payload. */
typedef struct AstraNetworkSharedSlot {
    /** Must match the owning session's generation for the slot to be valid. */
    uint32_t generation;
    /** Slot state: one of `ASTRA_NETWORK_SLOT_*`. */
    uint32_t state;
    /** Endpoint the slot's payload belongs to. */
    uint32_t endpoint;
    /** Per-transfer flags, set from the triggering request's flags (for example `ASTRA_NETWORK_MESSAGE_*`). */
    uint32_t flags;
    /** Byte offset of the payload within the slot's fixed-size buffer. */
    uint32_t offset;
    /** Byte length of the payload. */
    uint32_t length;
    /** Peer address associated with the transfer: the destination for an outbound datagram, or the sender for an inbound one. */
    AstraNetworkAddress address;
    /** Reserved; always zero. */
    uint32_t reserved[2];
} AstraNetworkSharedSlot;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkSharedSlot) == 60u,
               "network shared slot changed");
/** @endcond */

/** Largest number of slots the fixed metadata page can describe, rather than a policy constant. */
#define ASTRA_NETWORK_SHARED_SLOT_MAX \
    ((ASTRA_NETWORK_SHARED_METADATA_BYTES - \
      (uint32_t)sizeof(AstraNetworkSharedHeader)) / \
     (uint32_t)sizeof(AstraNetworkSharedSlot))
/** Largest total byte size of a shared transfer area, metadata plus every describable slot's payload. */
#define ASTRA_NETWORK_SHARED_BYTES_MAX \
    (ASTRA_NETWORK_SHARED_METADATA_BYTES + \
     ASTRA_NETWORK_SHARED_SLOT_MAX * ASTRA_NETWORK_SLOT_BYTES)

/** @cond ASTRA_INTERNAL */
_Static_assert(ASTRA_NETWORK_SHARED_SLOT_MAX >= 2u,
               "network shared area needs transmit and receive slots");
_Static_assert(ASTRA_NETWORK_SHARED_BYTES_MAX <= ASTRA_AREA_SIZE_MAX,
               "network shared area exceeds one area mapping");
/** @endcond */

#endif
