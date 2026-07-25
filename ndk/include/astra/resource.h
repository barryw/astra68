#ifndef ASTRA_RESOURCE_H
#define ASTRA_RESOURCE_H

/**
 * @file resource.h
 * @brief Opaque handles and shared-resource acquisition options.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/**
 * @defgroup astra_resources Resource management
 * @brief Process-owned handles, access rights, and acquisition policy.
 *
 * Applications treat handles as opaque capabilities. A handle may be wrapped
 * in a device-specific type, but its numeric value is never an address or a
 * hardware register index.
 *
 * @{
 */

/** Opaque capability token owned by the current process. @since 0.1.0 */
typedef uint32_t AstraHandle;

/** Sentinel representing no resource ownership. */
#define ASTRA_INVALID_HANDLE ((AstraHandle)0)

/** Rights that may be granted by a resource handle. */
#ifndef ASTRA_RIGHTS_DEFINED
#define ASTRA_RIGHTS_DEFINED 1
enum {
    /** Observe resource state or read data. */
    ASTRA_RIGHT_READ = 1u << 0,
    /** Modify resource state or write data. */
    ASTRA_RIGHT_WRITE = 1u << 1,
    /** Map the resource into the process address space. */
    ASTRA_RIGHT_MAP = 1u << 2,
    /** Signal an event, semaphore, or other waitable object. */
    ASTRA_RIGHT_SIGNAL = 1u << 3,
    /** Wait for the resource to become signaled or ready. */
    ASTRA_RIGHT_WAIT = 1u << 4,
    /** Transfer the capability to another process or subsystem. */
    ASTRA_RIGHT_TRANSFER = 1u << 5,
    /** Change resource configuration or lifecycle state. */
    ASTRA_RIGHT_ADMINISTER = 1u << 6,
    /** Use privileged diagnostics associated with the resource. */
    ASTRA_RIGHT_DEBUG = 1u << 7
};
#endif

/** Flags controlling resource acquisition. */
enum {
    /** Return immediately instead of waiting for a busy resource. */
    ASTRA_ACQUIRE_NONBLOCK = 1u << 0,
    /** Request ownership compatible with other shared owners. */
    ASTRA_ACQUIRE_SHARED = 1u << 1,
    /** Request ownership that excludes every other owner. */
    ASTRA_ACQUIRE_EXCLUSIVE = 1u << 2
};

/**
 * Policy supplied to a resource-acquisition operation.
 *
 * Initialize this structure with ::ASTRA_ACQUIRE_OPTIONS_INIT, then modify
 * supported fields. Reserved fields must remain zero.
 *
 * @since 0.1.0
 */
typedef struct AstraAcquireOptions {
    /** Size of this structure in bytes, including reserved fields. */
    uint32_t size;
    /** Bitwise combination of acquisition flags. */
    uint32_t flags;
    /** Maximum wait in milliseconds when blocking is permitted. */
    uint32_t timeout_ms;
    /** Reserved for source-compatible growth; initialize to zero. */
    uint32_t reserved[5];
} AstraAcquireOptions;

/** Infinite acquisition deadline for APIs that permit blocking. */
#define ASTRA_TIMEOUT_INFINITE UINT32_C(0xffffffff)
/** Default exclusive, immediate acquisition options initializer. */
#define ASTRA_ACQUIRE_OPTIONS_INIT \
    { sizeof(AstraAcquireOptions), ASTRA_ACQUIRE_EXCLUSIVE, 0, { 0, 0, 0, 0, 0 } }

/**
 * Close one process-owned capability.
 *
 * On success, the function replaces @p handle with ::ASTRA_INVALID_HANDLE.
 * Closing the final capability may wake waiters or notify a peer according to
 * the object's contract. Numeric copies of a handle do not duplicate it; they
 * become stale when the capability is closed or transferred.
 *
 * @param[in,out] handle Capability to close and invalidate.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_INVALID_ARGUMENT, or
 *         ::ASTRA_ERROR_INVALID_HANDLE.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_handle_close(AstraHandle *handle);

/**
 * Duplicate one cloneable capability while reducing its rights.
 *
 * The requested rights must be a nonzero subset of the source rights, and the
 * source must grant ::ASTRA_RIGHT_TRANSFER. The source remains owned by the
 * caller. Only object classes with an explicit retain operation are
 * cloneable; move-only endpoints return ::ASTRA_ERROR_PERMISSION.
 *
 * @param source Existing source capability.
 * @param rights Reduced rights for the new capability.
 * @param[out] duplicate Receives the independently owned capability.
 * @return ::ASTRA_OK or a validation, permission, peer-death, or resource
 *         error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_handle_duplicate(
    AstraHandle source,
    uint32_t rights,
    AstraHandle *duplicate);

/** @} */

ASTRA_EXTERN_C_END

#endif
