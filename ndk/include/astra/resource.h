#ifndef ASTRA_RESOURCE_H
#define ASTRA_RESOURCE_H

/**
 * @file resource.h
 * @brief Opaque handles and shared-resource acquisition options.
 */

#include <stdint.h>

#include <astra/object_abi.h>
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

/* Handle rights (ASTRA_RIGHT_*) are in astra/object_abi.h. */

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
