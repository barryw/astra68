/** @file graphics_library.h @brief Graphics Kit shared-library ABI. */
#ifndef ASTRA_GRAPHICS_LIBRARY_H
#define ASTRA_GRAPHICS_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#include <astra/bundle.h>
#include <astra/surface.h>

/** Graphics Kit export-table ABI major version. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR 2u
/** Graphics Kit export-table ABI minor version. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MINOR 1u

/** Graphics Kit 2.x immutable export table. */
typedef struct AstraGraphicsLibraryV2 {
    uint16_t abi_major; /**< ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_GRAPHICS_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */
    /** Initialize an RGB565 surface view. */
    int (*surface_view_init)(AstraSurfaceView *, void *, uint32_t, uint16_t,
                             uint16_t, uint32_t);
    /** Initialize and clear a hardware draw-list view. */
    int (*draw_list_view_init)(AstraSurfaceView *, void *, uint32_t, uint16_t,
                               uint16_t);
    /** Adopt an existing hardware draw-list view. */
    int (*draw_list_view_adopt)(AstraSurfaceView *, void *, uint32_t,
                                uint16_t, uint16_t);
    /** Pack an RGB565 color. */
    uint16_t (*rgb565)(uint8_t, uint8_t, uint8_t);
    /** Clear a surface. */
    void (*clear)(AstraSurfaceView *, uint16_t);
    /** Fill a clipped rectangle. */
    void (*fill)(AstraSurfaceView *, int32_t, int32_t, uint32_t, uint32_t,
                 uint16_t);
    /** Allocate a shared RGB565 surface. */
    uint32_t (*shared_surface_create)(AstraSharedSurface *, uint16_t,
                                      uint16_t);
    /** Allocate a shared hardware draw list. */
    uint32_t (*shared_draw_list_create)(AstraSharedSurface *, uint16_t,
                                        uint16_t);
    /** Close a shared surface. */
    uint32_t (*shared_surface_close)(AstraSharedSurface *);
    /** Validate and adopt an AICON file. */
    uint32_t (*aicon_open)(const void *, uint32_t, AstraAicon *);
    /** Select an AICON raster strike. */
    uint32_t (*aicon_strike)(const AstraAicon *, uint16_t,
                             AstraAiconStrike *);
    /** Read an AICON palette entry. */
    uint32_t (*aicon_palette)(const AstraAicon *, uint16_t, uint8_t[4]);
    /** Append an overlap-safe rectangular copy. */
    int (*draw_list_copy)(AstraSurfaceView *, uint32_t, uint32_t, uint32_t,
                          uint32_t, uint32_t, uint32_t);
    /** Scroll retained text-box content. */
    int (*text_box_scroll)(AstraTextBox *, int32_t);
    /** Replace a surface clipping rectangle. */
    int (*surface_clip)(AstraSurfaceView *, int32_t, int32_t,
                        uint32_t, uint32_t);
    /** Draw one clipped line through both endpoints. */
    int (*line)(AstraSurfaceView *, int32_t, int32_t, int32_t, int32_t,
                uint16_t);
} AstraGraphicsLibraryV2;

/** Compute the export-table extent through one named member. */
#define ASTRA_GRAPHICS_LIBRARY_SIZE_THROUGH(member)                        \
    ((uint32_t)(offsetof(AstraGraphicsLibraryV2, member) +                  \
                sizeof(((AstraGraphicsLibraryV2 *)0)->member)))

/** Graphics Kit 2.0 export-table extent. */
#define ASTRA_GRAPHICS_LIBRARY_2_0_SIZE \
    ASTRA_GRAPHICS_LIBRARY_SIZE_THROUGH(surface_clip)

/** Graphics Kit 2.1 export-table extent. */
#define ASTRA_GRAPHICS_LIBRARY_2_1_SIZE \
    ASTRA_GRAPHICS_LIBRARY_SIZE_THROUGH(line)

/**
 * Verify one consumer's minimum compatible minor and table extent.
 * @param library Open Graphics Kit export table.
 * @param minimum_minor Oldest compatible 2.x minor required by the caller.
 * @param minimum_structure_size Required append-only table extent.
 * @return Nonzero when the library satisfies both requirements.
 */
static inline int astra_graphics_library_supports(
    const AstraGraphicsLibraryV2 *library, uint16_t minimum_minor,
    uint32_t minimum_structure_size)
{
    return library != NULL &&
           library->abi_major == ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR &&
           library->abi_minor >= minimum_minor &&
           library->structure_size >= minimum_structure_size;
}

#endif
