#ifndef ASTRA_PORT_H
#define ASTRA_PORT_H

/**
 * @file port.h
 * @brief Bounded message ports and atomic capability transfer.
 */

#include <stdint.h>

#include <astra/message_abi.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/**
 * @defgroup astra_ports Message ports
 * @brief Bounded datagrams, waitable endpoints, and move-only handles.
 *
 * A port has one nontransferable receive endpoint and one transferable send
 * endpoint. Messages are FIFO and contain a fixed header, up to 1024 inline
 * payload bytes, and up to eight transferred capabilities. Bulk data belongs
 * in shared areas rather than copied messages.
 *
 * Every queue has fixed message and byte limits. A full queue returns
 * ::ASTRA_ERROR_WOULD_BLOCK or blocks only until its unchanged absolute
 * deadline. A successful send consumes all attached handles atomically; every
 * failure preserves them. A receive owns every returned handle only after the
 * complete message and handle vector have been copied successfully.
 *
 * @{
 */

/** Message flags accepted by ABI revision 0.1. */
enum {
    ASTRA_MESSAGE_FLAGS_NONE = 0
};

/** Owned pair returned by ::astra_port_create. @since 0.1.0 */
typedef struct AstraPort {
    /** Sole receive endpoint; not transferable. */
    AstraHandle receive;
    /** Send endpoint; transferable and duplicable only by transfer policy. */
    AstraHandle send;
} AstraPort;

/** Empty initializer for an unowned port pair. */
#define ASTRA_PORT_INIT \
    { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE }

/**
 * Initialize a message header and clear every reserved field.
 *
 * @param[out] header Header to initialize.
 * @param total_size Complete message size from 24 through 1048 bytes.
 * @param protocol Protocol identifier.
 * @param protocol_version Protocol revision.
 * @param operation Protocol operation code.
 * @param transaction_id Request/reply correlation value.
 * @return ::ASTRA_OK or ::ASTRA_ERROR_INVALID_ARGUMENT.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_message_header_init(
    AstraMessageHeader *header,
    uint32_t total_size,
    uint32_t protocol,
    uint16_t protocol_version,
    uint32_t operation,
    uint32_t transaction_id);

/**
 * Create a receiver-owned bounded message port.
 *
 * Capacity is reserved at creation, so later sends never allocate queue
 * storage. The caller must pass an empty ::AstraPort.
 *
 * @param maximum_messages Queue limit from 1 through 16 datagrams.
 * @param maximum_bytes Queue byte limit from 24 through
 *        ::ASTRA_PORT_BYTES_MAX bytes.
 * @param[out] port Receives both owned endpoints atomically.
 * @return ::ASTRA_OK or a documented argument/resource error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_create(uint32_t maximum_messages,
                                               uint32_t maximum_bytes,
                                               AstraPort *port);

/**
 * Close every endpoint still owned by a port pair.
 *
 * Both closes are attempted even if one endpoint is stale. Closing the receive
 * endpoint discards queued messages and wakes senders with
 * ::ASTRA_ERROR_PEER_DEAD. Closing the final send endpoint lets the receiver
 * drain queued messages, then reports ::ASTRA_ERROR_PEER_DEAD.
 *
 * @param[in,out] port Owned endpoint pair to close and invalidate.
 * @return ::ASTRA_OK when every live endpoint closed, otherwise the first
 *         close error after both endpoints have been attempted.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_close(AstraPort *port);

/**
 * Cleanup target used by ::ASTRA_AUTO_PORT.
 * @param[in,out] port Port pair to close; NULL is ignored as an error result.
 */
void astra_port_cleanup(AstraPort *port);

/** Automatically close a local ::AstraPort at normal scope exit. */
#define ASTRA_AUTO_PORT ASTRA_CLEANUP(astra_port_cleanup)

/**
 * Try to enqueue one complete datagram without blocking.
 *
 * On success, every entry in @p handles is replaced with
 * ::ASTRA_INVALID_HANDLE. On any failure the array and source capabilities are
 * unchanged.
 *
 * @param send_endpoint Send capability with signal rights.
 * @param message Naturally aligned complete message bytes.
 * @param message_size Complete size matching the message header.
 * @param[in,out] handles Capabilities moved on success, or NULL for none.
 * @param handle_count Number of capabilities from 0 through 8.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_WOULD_BLOCK,
 *         ::ASTRA_ERROR_PEER_DEAD, or a validation/resource error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_send_try(
    AstraHandle send_endpoint,
    const void *message,
    uint32_t message_size,
    AstraHandle *handles,
    uint32_t handle_count);

/**
 * Enqueue one datagram by an absolute monotonic deadline.
 *
 * The wrapper retries ::astra_port_send_try around the kernel's wait primitive
 * using @p deadline_ns unchanged. It never retains a message or handle pointer
 * while blocked. Use ::ASTRA_DEADLINE_POLL for one try or
 * ::ASTRA_DEADLINE_INFINITE for no finite deadline.
 *
 * @copydetails astra_port_send_try
 * @param deadline_ns Signed absolute monotonic nanosecond deadline.
 * @return The try-send results plus ::ASTRA_ERROR_TIMEOUT,
 *         ::ASTRA_ERROR_CANCELLED, or ::ASTRA_ERROR_CLOSED.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_send_until(
    AstraHandle send_endpoint,
    const void *message,
    uint32_t message_size,
    AstraHandle *handles,
    uint32_t handle_count,
    AstraMonotonicDeadline deadline_ns);

/**
 * Try to dequeue one complete datagram without blocking.
 *
 * On success, @p message receives the complete datagram and @p handles owns
 * every transferred capability. If either capacity is insufficient, the call
 * returns ::ASTRA_ERROR_BUFFER_TOO_SMALL, reports both required capacities,
 * and leaves the message queued. A zero-capacity NULL output pair is therefore
 * a safe size probe.
 *
 * @param receive_endpoint Receive capability with read rights.
 * @param[out] message Naturally aligned output bytes, or NULL when capacity is
 *        zero.
 * @param message_capacity Available output bytes, at most 1048.
 * @param[out] handles Capability output, or NULL when capacity is zero.
 * @param handle_capacity Available handle entries, at most 8.
 * @param[out] message_size Actual or required complete message size.
 * @param[out] handle_count Actual or required transferred-handle count.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_WOULD_BLOCK,
 *         ::ASTRA_ERROR_BUFFER_TOO_SMALL, ::ASTRA_ERROR_PEER_DEAD, or a
 *         validation/resource error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_receive_try(
    AstraHandle receive_endpoint,
    void *message,
    uint32_t message_capacity,
    AstraHandle *handles,
    uint32_t handle_capacity,
    uint32_t *message_size,
    uint32_t *handle_count);

/**
 * Dequeue one datagram by an absolute monotonic deadline.
 *
 * The wrapper retries ::astra_port_receive_try around the kernel's wait
 * primitive using @p deadline_ns unchanged. User pointers are presented only
 * to each bounded nonblocking receive attempt.
 *
 * @copydetails astra_port_receive_try
 * @param deadline_ns Signed absolute monotonic nanosecond deadline.
 * @return The try-receive results plus ::ASTRA_ERROR_TIMEOUT,
 *         ::ASTRA_ERROR_CANCELLED, or ::ASTRA_ERROR_CLOSED.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_port_receive_until(
    AstraHandle receive_endpoint,
    void *message,
    uint32_t message_capacity,
    AstraHandle *handles,
    uint32_t handle_capacity,
    uint32_t *message_size,
    uint32_t *handle_count,
    AstraMonotonicDeadline deadline_ns);

/** @} */

ASTRA_EXTERN_C_END

#endif
