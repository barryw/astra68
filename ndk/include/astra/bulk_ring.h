#ifndef ASTRA_BULK_RING_H
#define ASTRA_BULK_RING_H

/**
 * @file bulk_ring.h
 * @brief Bounded zero-copy SPSC rings backed by shared areas.
 */

#include <stdint.h>

#include <astra/object_abi.h>
#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** @defgroup astra_bulk_rings Bulk IPC rings
 *  @brief Batched payload transfer with waitable kernel doorbells.
 *  @{
 */

/* Ring constants and ::AstraBulkRingHeader are in astra/object_abi.h. */

/** Move-only endpoint pair returned by ring creation. @since 0.1.0 */
typedef struct AstraBulkRingEndpoints {
    /** Move-only producer endpoint. */
    AstraHandle producer;
    /** Move-only consumer endpoint. */
    AstraHandle consumer;
} AstraBulkRingEndpoints;

/** Empty endpoint-pair initializer. */
#define ASTRA_BULK_RING_ENDPOINTS_INIT \
    { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE }

/**
 * Process-local view of one move-only producer or consumer endpoint.
 *
 * One thread at a time may operate on a view. A process may provide external
 * serialization, but sharing one view concurrently without it is invalid.
 * Applications must not modify fields directly.
 *
 * @since 0.1.0
 */
typedef struct AstraBulkRing {
    /** Owned move-only kernel endpoint. */
    AstraHandle endpoint;
    /** Shared header at the attached area offset. */
    volatile AstraBulkRingHeader *header;
    /** First fixed-size payload element. */
    volatile uint8_t *data;
    /** Mapped area's complete byte size. */
    uint32_t area_size;
    /** Ring's byte offset within the area. */
    uint32_t offset;
    /** Validated fixed element size. */
    uint32_t element_size;
    /** Validated power-of-two element capacity. */
    uint32_t capacity;
    /** Validated ring generation. */
    uint32_t generation;
    /** Process-local caller-owned monotonic position. */
    uint32_t local_position;
    /** Last kernel-returned opposite monotonic position. */
    uint32_t peer_position;
    /** Position associated with the outstanding borrowed element. */
    uint32_t reservation_position;
    /** Producer or consumer endpoint role. */
    uint8_t role;
    /** Nonzero while one borrowed element awaits commit. */
    uint8_t reservation_active;
    /** Nonzero after local or kernel-reported ring corruption. */
    uint8_t failed;
    /** Reserved; applications must leave zero. */
    uint8_t reserved;
} AstraBulkRing;

/** Empty local ring-view initializer. */
#define ASTRA_BULK_RING_INIT \
    { ASTRA_INVALID_HANDLE, 0, 0, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, \
      0u, 0u, 0u, 0u }

/**
 * Create one ring in an area's nonoverlapping byte range.
 *
 * @param area Area handle with administer rights.
 * @param offset 64-byte-aligned ring offset within the area.
 * @param element_size Fixed four-byte-aligned element size. The header and
 * payload must fit in the backing area and the 32-bit ABI size field.
 * @param capacity Power-of-two element count. The complete ring must fit in
 * the backing area and the 32-bit ABI size field.
 * @param[out] endpoints Empty pair receiving both move-only endpoints.
 * @return ::ASTRA_OK or a validation, permission, peer-death, overlap, or
 *         resource-limit error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_create(
    AstraHandle area,
    uint32_t offset,
    uint32_t element_size,
    uint32_t capacity,
    AstraBulkRingEndpoints *endpoints);

/**
 * Close both endpoints still owned by an endpoint pair.
 *
 * @param[in,out] endpoints Endpoint pair to close and invalidate.
 * @return ::ASTRA_OK when every live endpoint closes, otherwise the first
 *         close error after both endpoints have been attempted.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_endpoints_close(
    AstraBulkRingEndpoints *endpoints);

/**
 * Cleanup target for ::ASTRA_AUTO_BULK_RING_ENDPOINTS.
 * @param[in,out] endpoints Endpoint pair to close.
 */
void astra_bulk_ring_endpoints_cleanup(AstraBulkRingEndpoints *endpoints);

/** Automatically close an endpoint pair at normal scope exit. */
#define ASTRA_AUTO_BULK_RING_ENDPOINTS \
    ASTRA_CLEANUP(astra_bulk_ring_endpoints_cleanup)

/**
 * Attach one endpoint to a writable mapping and consume its handle on success.
 *
 * @param[out] ring Empty process-local ring view.
 * @param[in,out] endpoint Owned endpoint, invalidated only on success.
 * @param area Read/write mapped area containing the ring.
 * @param offset Ring byte offset used at creation.
 * @param role ::ASTRA_BULK_RING_PRODUCER or ::ASTRA_BULK_RING_CONSUMER.
 * @return ::ASTRA_OK or a validation, handle, peer-death, or corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_attach(
    AstraBulkRing *ring,
    AstraHandle *endpoint,
    const AstraArea *area,
    uint32_t offset,
    uint8_t role);

/**
 * Refresh kernel shadow positions and notify the peer after a local batch.
 *
 * @param[in,out] ring Attached endpoint view with no active reservation.
 * @return ::ASTRA_OK or a handle, peer-death, closure, or corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_notify(AstraBulkRing *ring);

/**
 * Wait until this endpoint can reserve at least one element.
 *
 * @param[in,out] ring Attached endpoint view with no active reservation.
 * @param deadline_ns Signed absolute monotonic deadline.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_WOULD_BLOCK, ::ASTRA_ERROR_TIMEOUT,
 *         ::ASTRA_ERROR_PEER_DEAD, or another endpoint error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_wait_until(
    AstraBulkRing *ring,
    AstraMonotonicDeadline deadline_ns);

/**
 * Reserve one writable producer element without advancing the ring.
 *
 * @param[in,out] ring Attached producer view.
 * @param[out] element Borrowed writable element valid until commit or close.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_WOULD_BLOCK, or a state/corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_write_reserve(
    AstraBulkRing *ring,
    void **element);

/**
 * Publish the producer element returned by the preceding reserve.
 *
 * @param[in,out] ring Producer view with one outstanding reservation.
 * @return ::ASTRA_OK or a state/corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_write_commit(
    AstraBulkRing *ring);

/**
 * Reserve one readable consumer element without releasing its slot.
 *
 * @param[in,out] ring Attached consumer view.
 * @param[out] element Borrowed readable element valid until commit or close.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_WOULD_BLOCK, or a state/corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_read_reserve(
    AstraBulkRing *ring,
    const void **element);

/**
 * Release the consumer element returned by the preceding reserve.
 *
 * @param[in,out] ring Consumer view with one outstanding reservation.
 * @return ::ASTRA_OK or a state/corruption error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_read_commit(
    AstraBulkRing *ring);

/**
 * Close the owned endpoint and clear a local ring view.
 *
 * Any outstanding borrowed element becomes invalid without publication.
 *
 * @param[in,out] ring Attached endpoint view to close and clear.
 * @return ::ASTRA_OK or a validation/handle error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_bulk_ring_close(AstraBulkRing *ring);

/**
 * Cleanup target for ::ASTRA_AUTO_BULK_RING.
 * @param[in,out] ring Endpoint view to close.
 */
void astra_bulk_ring_cleanup(AstraBulkRing *ring);

/** Automatically close a local ring endpoint at normal scope exit. */
#define ASTRA_AUTO_BULK_RING ASTRA_CLEANUP(astra_bulk_ring_cleanup)

/** @} */

ASTRA_EXTERN_C_END

#endif
