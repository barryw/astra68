#ifndef ASTRA_AREA_H
#define ASTRA_AREA_H

/**
 * @file area.h
 * @brief Explicitly mapped, committed CPU-shared memory areas.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** @defgroup astra_areas Shared areas
 *  @brief Generation-safe handles for bounded shared process memory.
 *  @{
 */

#ifndef ASTRA_AREA_ABI_CONSTANTS_DEFINED
/** Internal one-definition guard shared with the raw trap ABI header. */
#define ASTRA_AREA_ABI_CONSTANTS_DEFINED 1
/** Maximum size of one shared or reserved area. */
#define ASTRA_AREA_SIZE_MAX UINT32_C(0x00400000)
/** Map readable pages. Every mapping must include this flag. */
#define ASTRA_AREA_MAP_READ (1u << 0)
/** Map writable pages; requires write rights on the area handle. */
#define ASTRA_AREA_MAP_WRITE (1u << 1)
#endif

/** Owned area capability and optional process-local mapping. @since 0.1.0 */
typedef struct AstraArea {
    /** Area capability, or ::ASTRA_INVALID_HANDLE. */
    AstraHandle handle;
    /** Kernel-selected mapping base, or NULL while unmapped. */
    void *address;
    /** Committed mapping size in bytes. */
    uint32_t size;
    /** Active mapping flags. */
    uint32_t map_flags;
} AstraArea;

/** Empty shared-area initializer. */
#define ASTRA_AREA_INIT \
    { ASTRA_INVALID_HANDLE, 0, 0u, 0u }

/**
 * Create a zero-filled, physically committed shared area.
 *
 * The complete rounded-up area is charged before this call succeeds. The
 * caller must provide an empty @p area and explicitly include every right that
 * later mappings, duplication, transfer, or ring creation will require.
 *
 * @param byte_size Requested bytes from 1 through ::ASTRA_AREA_SIZE_MAX.
 * @param rights Nonzero combination of read, write, map, transfer, and
 *        administer rights.
 * @param[out] area Empty view that receives the owned area handle.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_INVALID_ARGUMENT,
 *         ::ASTRA_ERROR_NO_RESOURCES, or ::ASTRA_ERROR_OUT_OF_MEMORY.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_area_create(uint32_t byte_size,
                                               uint32_t rights,
                                               AstraArea *area);

/**
 * Map an area once at its kernel-selected fixed logical address.
 *
 * Repeating an identical map is permitted by the kernel. This NDK view tracks
 * one local mapping and rejects a second local call until it is unmapped.
 *
 * @param[in,out] area Owned unmapped area view; receives address and size.
 * @param map_flags ::ASTRA_AREA_MAP_READ, optionally combined with
 *        ::ASTRA_AREA_MAP_WRITE.
 * @return ::ASTRA_OK or a validation, permission, peer-death, memory, or
 *         resource-limit error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_area_map(AstraArea *area,
                                            uint32_t map_flags);

/**
 * Remove an area's complete mapping from the current process.
 *
 * @param[in,out] area Mapped view to unmap and clear.
 * @return ::ASTRA_OK or a validation, peer-death, or I/O error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_area_unmap(AstraArea *area);

/**
 * Unmap and close every resource still owned by an area view.
 *
 * Both operations are attempted. If unmap fails but close succeeds, the view
 * retains its mapping fields so diagnostics can report the failed teardown.
 *
 * @param[in,out] area Area view to unmap and close.
 * @return ::ASTRA_OK when all owned state closes, otherwise the first error.
 * @since 0.1.0
 */
ASTRA_NODISCARD AstraResult astra_area_close(AstraArea *area);

/**
 * Cleanup target for ::ASTRA_AUTO_AREA.
 * @param[in,out] area Area view to close; NULL is ignored as an error result.
 */
void astra_area_cleanup(AstraArea *area);

/** Automatically unmap and close an ::AstraArea at normal scope exit. */
#define ASTRA_AUTO_AREA ASTRA_CLEANUP(astra_area_cleanup)

/** @} */

ASTRA_EXTERN_C_END

#endif
