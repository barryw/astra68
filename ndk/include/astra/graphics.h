#ifndef ASTRA_GRAPHICS_H
#define ASTRA_GRAPHICS_H

/**
 * @file graphics.h
 * @brief Window display surfaces, draw lists, and fences.
 */

#include <stdint.h>

#include <astra/resource.h>

ASTRA_EXTERN_C_BEGIN

/**
 * @defgroup astra_graphics Graphics and display
 * @brief Protected access to Vega scanout and Astraea drawing resources.
 *
 * Applications never submit physical addresses or program chipset MMIO. The
 * display service validates commands, pins referenced storage, schedules the
 * shared engines, and signals a fence after every referenced object is safe to
 * reuse or close. Live wrappers are move-only by convention.
 *
 * @{
 */

/** Logical-scene placement policies for the fixed physical output. */
enum {
    /** Prefer useful integer enlargement, otherwise use aspect-preserving fit. */
    ASTRA_DISPLAY_SCALE_AUTO = 0,
    /** Use the largest integer enlargement and letterbox the remainder. */
    ASTRA_DISPLAY_SCALE_INTEGER = 1,
    /** Fill as much output as possible without cropping or distorting. */
    ASTRA_DISPLAY_SCALE_FIT = 2,
    /** Fill the output and symmetrically crop the logical scene as needed. */
    ASTRA_DISPLAY_SCALE_FILL = 3
};

/** Pixel storage formats accepted by surfaces and source images. */
enum {
    /** Packed 4-bit indices, high nibble first. */
    ASTRA_PIXEL_FORMAT_INDEX4 = 1,
    /** One 8-bit palette index per pixel. */
    ASTRA_PIXEL_FORMAT_INDEX8 = 2,
    /** Big-endian 5:6:5 direct-color pixels. */
    ASTRA_PIXEL_FORMAT_RGB565 = 3,
    /** One-bit glyph mask, most-significant bit first. */
    ASTRA_PIXEL_FORMAT_MASK1 = 4,
    /** Four-bit glyph coverage, high nibble first. */
    ASTRA_PIXEL_FORMAT_A4 = 5,
    /** Big-endian `XX RR GG BB` direct-color pixels. */
    ASTRA_PIXEL_FORMAT_XRGB8888 = 6,
    /** Big-endian `AA RR GG BB` straight-alpha pixels. */
    ASTRA_PIXEL_FORMAT_ARGB8888 = 7
};

/** Surface creation and access flags. */
enum {
    /** Surface may be presented for display scanout. */
    ASTRA_SURFACE_SCANOUT = 1u << 0,
    /** Surface may receive draw-list output. */
    ASTRA_SURFACE_DRAW_TARGET = 1u << 1,
    /** Surface may be read by graphics operations. */
    ASTRA_SURFACE_DRAW_SOURCE = 1u << 2,
    /** Process may read the surface back with ::astra_surface_read. */
    ASTRA_SURFACE_CPU_READ = 1u << 3,
    /** Process may write the surface with ::astra_surface_write. */
    ASTRA_SURFACE_CPU_WRITE = 1u << 4
};

/**
 * How drawn pixels combine with the destination: the `blend` of
 * ::AstraDrawPaint and ::AstraBlitOptions, and of ::astra_draw_triangles.
 * These are SDL2's straight-alpha equations (docs/TEXTURE_ENGINE.md §6),
 * where src is the texel times the modulation or vertex color.
 */
enum {
    /** Replace: dst = src, including alpha on ARGB8888 targets. */
    ASTRA_BLEND_NONE = 0,
    /** Source-over: dst = src * a + dst * (1 - a). */
    ASTRA_BLEND_ALPHA = 1,
    /** Additive: dst = src * a + dst. */
    ASTRA_BLEND_ADD = 2,
    /** Modulate: dst = src * dst. */
    ASTRA_BLEND_MODULATE = 3,
    /** Multiply: dst = src * dst + dst * (1 - a). */
    ASTRA_BLEND_MULTIPLY = 4
};

/** Drawing behavior flags used by ::AstraDrawPaint. */
enum {
    /** Zero mask/pattern bits write the paint background. */
    ASTRA_DRAW_OPAQUE_BACKGROUND = 1u << 0
};

/** Blit and triangle sampling flags. */
enum {
    /** Mirror the source horizontally (blits only). */
    ASTRA_BLIT_FLIP_X = 1u << 0,
    /** Mirror the source vertically (blits only). */
    ASTRA_BLIT_FLIP_Y = 1u << 1,
    /** Sample bilinearly instead of nearest-neighbor. */
    ASTRA_BLIT_FILTER_LINEAR = 1u << 3
};

/** Hardware-pointer image limits. */
enum {
    /** Native hardware-pointer image width. */
    ASTRA_HARDWARE_POINTER_WIDTH = 32,
    /** Native hardware-pointer image height. */
    ASTRA_HARDWARE_POINTER_HEIGHT = 32
};

/** Signed integer point in destination pixels. */
typedef struct AstraPointI32 {
    /** Horizontal coordinate. */
    int32_t x;
    /** Vertical coordinate. */
    int32_t y;
} AstraPointI32;
/** Signed origin and nonnegative extent in pixels. */
typedef struct AstraRectI32 {
    /** Left coordinate. */
    int32_t x;
    /** Top coordinate. */
    int32_t y;
    /** Nonzero width. */
    uint32_t width;
    /** Nonzero height. */
    uint32_t height;
} AstraRectI32;

/**
 * Graphics connection of one window. It borrows the window's control
 * channel and must be closed before the window.
 */
typedef struct AstraDisplay {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_handle;
    uint32_t _private_window;
    AstraHandle _private_staging;
    void *_private_staging_pixels;
    uint32_t _private_staging_bytes;
    /** @endcond */
} AstraDisplay;
/** Opaque storage and format object. */
typedef struct AstraSurface {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_handle;
    uint32_t _private_window;
    uint32_t _private_id;
    uint32_t _private_flags;
    uint32_t _private_pitch;
    uint16_t _private_width;
    uint16_t _private_height;
    uint16_t _private_format;
    uint16_t _private_reserved;
    /** @endcond */
} AstraSurface;
/** Mutable command list until submission. */
typedef struct AstraDrawList {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_handle;
    AstraHandle _private_port;
    uint32_t _private_window;
    uint32_t _private_list;
    /* The destination of the list's first command. */
    uint32_t _private_destination;
    void *_private_commands;
    uint32_t _private_bytes;
    uint32_t _private_sealed;
    AstraRectI32 _private_clip;
    /* The event the service signals once it has read a posted list. */
    AstraHandle _private_release;
    /* The destination of the next command. */
    uint32_t _private_target;
    uint16_t _private_target_width;
    uint16_t _private_target_height;
    /** @endcond */
} AstraDrawList;
/** One-shot asynchronous completion object. */
typedef struct AstraFence {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraFence;

/** Initializer for an empty ::AstraDisplay. */
#define ASTRA_DISPLAY_INIT { ASTRA_INVALID_HANDLE, 0, ASTRA_INVALID_HANDLE, 0, 0 }
/** Initializer for an empty ::AstraSurface. */
#define ASTRA_SURFACE_INIT \
    { ASTRA_INVALID_HANDLE, 0, 0, 0, 0, 0, 0, 0, 0 }
/** Initializer for an empty ::AstraDrawList. */
#define ASTRA_DRAW_LIST_INIT \
    { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0 }, ASTRA_INVALID_HANDLE, 0, 0, 0 }
/** Initializer for an empty ::AstraFence. */
#define ASTRA_FENCE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for ::AstraSurfaceCreateInfo. */
#define ASTRA_SURFACE_CREATE_INFO_INIT \
    { sizeof(AstraSurfaceCreateInfo), 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0, 0 } }
/** Initializer for ::AstraSurfaceInfo. */
#define ASTRA_SURFACE_INFO_INIT \
    { sizeof(AstraSurfaceInfo), 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0 } }
/** Initializer for ::AstraDrawPaint. */
#define ASTRA_DRAW_PAINT_INIT \
    { sizeof(AstraDrawPaint), 0, { 0, 0, 0, 255 }, { 0, 0, 0, 0 }, \
      ASTRA_BLEND_NONE, { 0, 0, 0 } }
/** Initializer for ::AstraDisplayMode. */
#define ASTRA_DISPLAY_MODE_INIT \
    { sizeof(AstraDisplayMode), ASTRA_DISPLAY_SCALE_AUTO, 0, 0, \
      { 0, 0, 0, 0 } }
/** Initializer for ::AstraHardwarePointerImage. */
#define ASTRA_HARDWARE_POINTER_IMAGE_INIT \
    { sizeof(AstraHardwarePointerImage), 0, 0, 0, 0, { 0, 0 }, \
      { 0, 0, 0, 0 } }

/** Declare a display handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_DISPLAY(name) \
    AstraDisplay name ASTRA_CLEANUP(astra_display_cleanup) = ASTRA_DISPLAY_INIT
/** Declare a surface handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_SURFACE(name) \
    AstraSurface name ASTRA_CLEANUP(astra_surface_cleanup) = ASTRA_SURFACE_INIT
/** Declare a draw-list handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_DRAW_LIST(name) \
    AstraDrawList name ASTRA_CLEANUP(astra_draw_list_cleanup) = \
        ASTRA_DRAW_LIST_INIT
/** Declare a fence handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_FENCE(name) \
    AstraFence name ASTRA_CLEANUP(astra_fence_cleanup) = ASTRA_FENCE_INIT

/** Requested logical scene and hardware scaling policy. */
typedef struct AstraDisplayMode {
    /** Structure size in bytes. */
    uint32_t size;
    /** One `ASTRA_DISPLAY_SCALE_*` policy. */
    uint32_t scaling;
    /** Logical scene width in pixels. */
    uint16_t width;
    /** Logical scene height in pixels. */
    uint16_t height;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraDisplayMode;

/** Exact crop and viewport generated for the hardware scaler. */
typedef struct AstraDisplayLayout {
    uint16_t source_width; /**< Logical source width. */
    uint16_t source_height; /**< Logical source height. */
    uint16_t crop_x; /**< Source crop x. */
    uint16_t crop_y; /**< Source crop y. */
    uint16_t crop_width; /**< Source crop width. */
    uint16_t crop_height; /**< Source crop height. */
    uint16_t viewport_x; /**< Physical viewport x. */
    uint16_t viewport_y; /**< Physical viewport y. */
    uint16_t viewport_width; /**< Physical viewport width. */
    uint16_t viewport_height; /**< Physical viewport height. */
} AstraDisplayLayout;

/** Copied native-resolution hardware-pointer image. */
typedef struct AstraHardwarePointerImage {
    /** Structure size in bytes. */
    uint32_t size;
    /** Row-major RGBA pixels copied before return. */
    const AstraColorRGBA8 *pixels;
    /** Image width through ::ASTRA_HARDWARE_POINTER_WIDTH. */
    uint16_t width;
    /** Image height through ::ASTRA_HARDWARE_POINTER_HEIGHT. */
    uint16_t height;
    /** Source row pitch in bytes. */
    uint32_t pitch;
    /** Hotspot within the image. */
    AstraPointI32 hotspot;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraHardwarePointerImage;

/** Parameters for allocating a protected graphics surface. */
typedef struct AstraSurfaceCreateInfo {
    /** Structure size in bytes. */
    uint32_t size;
    /** Bitwise `ASTRA_SURFACE_*` usage rights. */
    uint32_t flags;
    /** Surface width in pixels. */
    uint16_t width;
    /** Surface height in pixels. */
    uint16_t height;
    /** One `ASTRA_PIXEL_FORMAT_*` value. */
    uint16_t format;
    /** Reserved; initialize to zero. */
    uint16_t reserved16;
    /** Requested bytes/row, or zero for a service-selected pitch. */
    uint32_t preferred_pitch;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[5];
} AstraSurfaceCreateInfo;

/** Information about one allocated surface. */
typedef struct AstraSurfaceInfo {
    /** Structure size in bytes. */
    uint32_t size;
    /** Granted `ASTRA_SURFACE_*` usage rights. */
    uint32_t flags;
    /** Surface width in pixels. */
    uint16_t width;
    /** Surface height in pixels. */
    uint16_t height;
    /** Surface storage format. */
    uint16_t format;
    /** Reserved; currently zero. */
    uint16_t reserved16;
    /** Actual bytes per row. */
    uint32_t pitch;
    /** Reserved for compatible growth; currently zero. */
    uint32_t reserved[5];
} AstraSurfaceInfo;

/** Foreground/background colors for one draw operation. */
typedef struct AstraDrawPaint {
    /** Structure size in bytes. */
    uint32_t size;
    /** Bitwise `ASTRA_DRAW_*` behavior flags. */
    uint32_t flags;
    /** Foreground, outline, or fill color. */
    AstraColorRGBA8 foreground;
    /** Background color used when opaque-background mode is selected. */
    AstraColorRGBA8 background;
    /** One `ASTRA_BLEND_*` mode. Lines take NONE, or ALPHA when opaque. */
    uint32_t blend;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[3];
} AstraDrawPaint;

/** Source treatment for ::astra_draw_blit. */
typedef struct AstraBlitOptions {
    /** Structure size in bytes. */
    uint32_t size;
    /** Bitwise `ASTRA_BLIT_*` flags. */
    uint32_t flags;
    /** Multiplies each source texel per channel; alpha is opacity. */
    AstraColorRGBA8 modulate;
    /** One `ASTRA_BLEND_*` mode. */
    uint32_t blend;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[3];
} AstraBlitOptions;

/** Initializer for ::AstraBlitOptions: an opaque, unmodulated copy. */
#define ASTRA_BLIT_OPTIONS_INIT \
    { sizeof(AstraBlitOptions), 0, { 255, 255, 255, 255 }, ASTRA_BLEND_NONE, \
      { 0, 0, 0 } }

/**
 * One triangle vertex for ::astra_draw_triangles, in the hardware's exact
 * fixed-point units, so a vertex means the same pixels on every path: x = 384
 * is 1.5 pixels and u = 32768 is half a texel. Pixel (px, py) is covered
 * when its center (px + 0.5, py + 0.5) is inside the triangle, with the
 * top-left rule on shared edges (docs/TEXTURE_ENGINE.md §2).
 */
typedef struct AstraVertex {
    /** Destination x in signed 24.8 pixels, within +-32768 pixels. */
    int32_t x;
    /** Destination y in signed 24.8 pixels, within +-32768 pixels. */
    int32_t y;
    /** Source x in signed 16.16 texels; zero when untextured. */
    int32_t u;
    /** Source y in signed 16.16 texels; zero when untextured. */
    int32_t v;
    /** Straight-alpha color that multiplies the texel, or the fill. */
    AstraColorRGBA8 color;
} AstraVertex;

/** Integer pixels to 24.8 vertex coordinates. */
#define ASTRA_VERTEX_PIXELS(pixels) ((int32_t)(pixels) * 256)
/** Integer texels to 16.16 texture coordinates. */
#define ASTRA_VERTEX_TEXELS(texels) ((int32_t)(texels) * 65536)

/**
 * Close a display handle.
 *
 * @param[in,out] display Live display to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_close(AstraDisplay *display);

/**
 * Calculate the exact scaler crop and viewport without floating-point math.
 *
 * The physical timing never changes. Integer modes and fractional modes use
 * the same nearest-neighbor hardware path; letterbox pixels are black.
 * @param mode Requested logical mode and scaling policy.
 * @param output_width Physical output width.
 * @param output_height Physical output height.
 * @param layout Receives exact crop and viewport values.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_layout_calculate(
    const AstraDisplayMode *mode, uint16_t output_width,
    uint16_t output_height, AstraDisplayLayout *layout);
/**
 * Allocate a protected surface owned by the caller.
 *
 * @param[in] display Display that owns the allocation domain.
 * @param[in] create_info Size-initialized dimensions, format, and usage rights.
 * @param[out] surface Empty handle that receives the surface.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_create(
    const AstraDisplay *display,
    const AstraSurfaceCreateInfo *create_info,
    AstraSurface *surface);
/**
 * Query one surface's granted format, dimensions, rights, and pitch.
 *
 * @param[in] surface Live surface to query.
 * @param[in,out] info Size-initialized structure that receives the result.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_get_info(
    const AstraSurface *surface,
    AstraSurfaceInfo *info);
/**
 * Close a surface handle after all referencing fences have retired.
 *
 * @param[in,out] surface Live surface to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_close(AstraSurface *surface);

/**
 * Map at least @p minimum_bytes of the display's CPU staging area. Pixels
 * written there reach a surface through ::astra_surface_write_staged with
 * one service copy and no NDK copy. Growing the area preserves nothing.
 *
 * @param[in,out] display Open window display.
 * @param minimum_bytes Bytes the caller needs.
 * @param[out] pixels Receives the writable mapping.
 * @param[out] bytes Receives the mapped size.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_staging(
    AstraDisplay *display, uint32_t minimum_bytes, void **pixels,
    uint32_t *bytes);

/**
 * Copy a rectangle of rows from the staging area into a surface.
 *
 * @param[in] surface CPU-writable surface, or a window content surface.
 * @param[in] rectangle Destination rectangle inside the surface.
 * @param offset Staging byte offset of the first row.
 * @param pitch Staging bytes between rows.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_write_staged(
    const AstraSurface *surface, const AstraRectI32 *rectangle,
    uint32_t offset, uint32_t pitch);

/**
 * Copy caller pixels into a surface through the display's staging area.
 *
 * @param[in,out] display Display that owns @p surface.
 * @param[in] surface CPU-writable surface, or a window content surface.
 * @param[in] rectangle Destination rectangle inside the surface.
 * @param[in] pixels First source row in the surface's format.
 * @param pitch Source bytes between rows.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_write(
    AstraDisplay *display, const AstraSurface *surface,
    const AstraRectI32 *rectangle, const void *pixels, uint32_t pitch);

/**
 * Copy a rectangle of a surface back into caller memory, in the surface's
 * format. The read happens after every list submitted before it completed.
 * The pixels travel from Media RAM through the display's staging area.
 *
 * @param[in,out] display Display that owns @p surface.
 * @param[in] surface ::ASTRA_SURFACE_CPU_READ surface, or a window content
 *                    surface.
 * @param[in] rectangle Source rectangle inside the surface.
 * @param[out] pixels First destination row.
 * @param pitch Destination bytes between rows.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_surface_read(
    AstraDisplay *display, const AstraSurface *surface,
    const AstraRectI32 *rectangle, void *pixels, uint32_t pitch);

/**
 * Create an empty draw list with one destination and mandatory clip rectangle.
 *
 * @param[in] destination RGB565, XRGB8888, ARGB8888, or INDEX8 draw-target
 *                        surface. INDEX8 takes no blending or triangles.
 * @param[in] clip Nonempty destination clip rectangle.
 * @param[out] draw_list Empty handle that receives the list.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_create(
    const AstraSurface *destination,
    const AstraRectI32 *clip,
    AstraDrawList *draw_list);
/**
 * Make @p destination the destination of the commands appended after this
 * call. One list then carries a frame for several surfaces -- a render
 * target, then the window -- and travels as one submission. The clip
 * becomes the whole of @p destination.
 *
 * @param[in,out] draw_list Mutable draw list.
 * @param[in] destination Draw-target surface of the list's window.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_set_target(
    AstraDrawList *draw_list, const AstraSurface *destination);
/**
 * Remove queued commands while preserving the destination and clip.
 *
 * @param[in,out] draw_list Mutable, non-submitted draw list.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_reset(AstraDrawList *draw_list);
/**
 * Close an unsubmitted or retired draw list.
 *
 * @param[in,out] draw_list Draw list to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_close(AstraDrawList *draw_list);
/**
 * Replace the clip applied to commands appended after this call.
 *
 * @param[in,out] draw_list Mutable draw list.
 * @param[in] clip Clip rectangle; it is intersected with the destination.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_set_clip(
    AstraDrawList *draw_list, const AstraRectI32 *clip);

/**
 * Append a blit of @p source_rect scaled to @p destination_rect.
 *
 * Scaling is nearest-neighbor unless ::ASTRA_BLIT_FILTER_LINEAR is set.
 * The source may be the list's destination only for an unscaled,
 * unflipped, unmodulated copy without blending.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] source Draw-source surface of the same window.
 * @param[in] source_rect Rectangle inside @p source.
 * @param[in] destination_rect Destination rectangle.
 * @param[in] options Flip, filter, blend, and modulation, or NULL for a
 *                    plain copy.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_blit(
    AstraDrawList *draw_list, const AstraSurface *source,
    const AstraRectI32 *source_rect, const AstraRectI32 *destination_rect,
    const AstraBlitOptions *options);

/**
 * Append textured or colored triangles, drawn by the texture engine.
 *
 * Each vertex color multiplies the texel (or is the color, untextured);
 * colors and texel coordinates are interpolated across each triangle.
 * Winding is irrelevant and zero-area triangles draw nothing.
 *
 * @param[in,out] draw_list Mutable list with a direct-color destination.
 * @param[in] texture Draw-source surface of the same window in RGB565,
 *                    XRGB8888, or ARGB8888, other than the destination; or
 *                    NULL for untextured triangles.
 * @param[in] vertices Three vertices per triangle.
 * @param vertex_count Nonzero multiple of three.
 * @param blend One `ASTRA_BLEND_*` mode.
 * @param flags Zero, or ::ASTRA_BLIT_FILTER_LINEAR with a texture.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_triangles(
    AstraDrawList *draw_list, const AstraSurface *texture,
    const AstraVertex *vertices, uint32_t vertex_count, uint32_t blend,
    uint32_t flags);

/**
 * Append a clipped Bresenham line including both endpoints.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] p0 First endpoint.
 * @param[in] p1 Second endpoint.
 * @param[in] paint Foreground color and behavior.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_line(
    AstraDrawList *draw_list, AstraPointI32 p0, AstraPointI32 p1,
    const AstraDrawPaint *paint);
/**
 * Append an outlined or filled rectangle.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] rectangle Nonempty destination rectangle.
 * @param[in] filled Zero for an outline, one for a fill.
 * @param[in] paint Foreground color and behavior.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_rectangle(
    AstraDrawList *draw_list, const AstraRectI32 *rectangle, int filled,
    const AstraDrawPaint *paint);
/**
 * Append line segments, all in one paint, as one draw-list command.
 *
 * Segment i joins @p endpoints[2i] and @p endpoints[2i+1] and draws exactly
 * as ::astra_draw_line would, in array order; one call costs the service
 * and the hardware one command, not one per segment. A connected strip of
 * n points is n - 1 segments that repeat each inner point.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] endpoints Two endpoints per segment, each coordinate a signed
 *                      16-bit value.
 * @param segment_count Number of segments; zero appends nothing.
 * @param[in] paint Line color; blend NONE, or ALPHA with an opaque color.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error; an
 *         invalid endpoint appends nothing.
 */
ASTRA_NODISCARD AstraResult astra_draw_lines(
    AstraDrawList *draw_list, const AstraPointI32 *endpoints,
    uint32_t segment_count, const AstraDrawPaint *paint);
/**
 * Append filled rectangles, all in one paint, as one draw-list command.
 *
 * Each rectangle draws exactly as a filled ::astra_draw_rectangle would, in
 * array order; one call costs the service and the hardware one command, not
 * one per rectangle. Rectangles with zero width or height are skipped.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] rectangles @p count rectangles, each with a signed 16-bit
 *                       origin and a width and height up to 65535.
 * @param count Number of rectangles; zero appends nothing.
 * @param[in] paint Fill color and blend mode.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error; an
 *         invalid rectangle appends nothing.
 */
ASTRA_NODISCARD AstraResult astra_draw_rectangles(
    AstraDrawList *draw_list, const AstraRectI32 *rectangles, uint32_t count,
    const AstraDrawPaint *paint);
/**
 * Append one line of text in the system UI font, drawn by the glyph engine.
 *
 * The font is the one every Astra window's chrome uses; its native strikes
 * are the heights ::astra_ui_font_strike (font.library) returns non-NULL for.
 * Any other height is rejected when the list is submitted. The destination
 * must be RGB565 or XRGB8888.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param origin Left edge and top of the text line, in destination pixels.
 * @param[in] utf8 Valid UTF-8 without NUL bytes.
 * @param utf8_bytes Byte length, 1 through 4096.
 * @param pixel_height UI font strike height.
 * @param style_flags `ASTRA_TEXT_STYLE_*` bits within
 *                    `ASTRA_TEXT_RENDER_STYLE_MASK`.
 * @param color Opaque text color; alpha must be 255.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_ui_text(
    AstraDrawList *draw_list, AstraPointI32 origin, const char *utf8,
    uint32_t utf8_bytes, uint32_t pixel_height, uint32_t style_flags,
    AstraColorRGBA8 color);

/**
 * Seal and asynchronously submit a draw list.
 *
 * @param[in,out] draw_list Mutable list that becomes sealed on success.
 * @param[out] fence Empty handle that receives completion ownership.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_submit(
    AstraDrawList *draw_list, AstraFence *fence);

/** ::astra_draw_post flag: present the window after the list's commands. */
#define ASTRA_DRAW_POST_PRESENT 1u
/** ::astra_draw_post flag, with ::ASTRA_DRAW_POST_PRESENT: the next frame
    redraws every content pixel, so this one need not be carried into it. */
#define ASTRA_DRAW_POST_DISCARD 2u

/**
 * Hand the list's commands to the display service and return without
 * waiting for them, optionally presenting the window after them -- the
 * frame a game hands over before it goes on with the next. Commands run in
 * the order everything is sent to the window. The list empties itself the
 * next time it is changed, waiting then, if it must, until the service has
 * read it.
 *
 * @param[in,out] draw_list Mutable list of a window's surfaces.
 * @param flags ::ASTRA_DRAW_POST_PRESENT, optionally with
 *              ::ASTRA_DRAW_POST_DISCARD.
 * @return ::ASTRA_OK once handed over, or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_post(AstraDrawList *draw_list,
                                            uint32_t flags);

/**
 * Poll a fence without blocking.
 *
 * @param[in] fence Live completion fence.
 * @param[out] signaled Receives zero or one.
 * @param[out] completion_result Receives job status when signaled.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_fence_poll(
    const AstraFence *fence, int *signaled, AstraResult *completion_result);
/**
 * Wait for a fence or deadline.
 *
 * @param[in] fence Live completion fence.
 * @param[in] timeout_ms Maximum wait in milliseconds; zero only polls.
 * @param[out] completion_result Receives the completed job status.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_TIMEOUT, or another negative error.
 */
ASTRA_NODISCARD AstraResult astra_fence_wait(
    const AstraFence *fence, uint32_t timeout_ms,
    AstraResult *completion_result);
/**
 * Close a fence handle without cancelling submitted work.
 *
 * @param[in,out] fence Fence to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_fence_close(AstraFence *fence);

/** Cleanup helper used by ::ASTRA_AUTO_DISPLAY. @param display Value to close. */
void astra_display_cleanup(AstraDisplay *display);
/** Cleanup helper used by ::ASTRA_AUTO_SURFACE. @param surface Value to close. */
void astra_surface_cleanup(AstraSurface *surface);
/** Cleanup helper used by ::ASTRA_AUTO_DRAW_LIST. @param draw_list Value to close. */
void astra_draw_list_cleanup(AstraDrawList *draw_list);
/** Cleanup helper used by ::ASTRA_AUTO_FENCE. @param fence Value to close. */
void astra_fence_cleanup(AstraFence *fence);

/** @} */

ASTRA_EXTERN_C_END

#endif
