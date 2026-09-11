/** @file graphics_library.h @brief Graphics Kit shared-library ABI. */
#ifndef ASTRA_GRAPHICS_LIBRARY_H
#define ASTRA_GRAPHICS_LIBRARY_H

#include <stdint.h>

#include <astra/bundle.h>
#include <astra/surface.h>

/** Graphics Kit export-table ABI major version. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR 2u
/** Graphics Kit export-table ABI minor version. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MINOR 0u

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
} AstraGraphicsLibraryV2;

#endif
