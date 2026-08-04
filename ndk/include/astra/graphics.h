#ifndef ASTRA_GRAPHICS_H
#define ASTRA_GRAPHICS_H

/**
 * @file graphics.h
 * @brief Managed display surfaces, drawing, sprites, raster changes, and fences.
 */

#include <stdint.h>

#include <astra/font.h>
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

/** Graphics capabilities reported by ::AstraGraphicsInfo. */
enum {
    /** Direct framebuffer scanout is available. */
    ASTRA_GRAPHICS_CAP_FRAMEBUFFER = 1u << 0,
    /** Framebuffer base changes can retire at vertical blank. */
    ASTRA_GRAPHICS_CAP_PAGE_FLIP = 1u << 1,
    /** Two scrolling tile layers are available. */
    ASTRA_GRAPHICS_CAP_TILE_LAYERS = 1u << 2,
    /** The 64-entry hardware sprite compositor is available. */
    ASTRA_GRAPHICS_CAP_SPRITES = 1u << 3,
    /** Validated beam-synchronized raster programs are available. */
    ASTRA_GRAPHICS_CAP_RASTER_PROGRAM = 1u << 4,
    /** Asynchronous copy, fill, key, and mask blits are available. */
    ASTRA_GRAPHICS_CAP_BLITTER = 1u << 5,
    /** Hardware line and shape drawing is available. */
    ASTRA_GRAPHICS_CAP_GEOMETRY = 1u << 6,
    /** Hardware bitmap-glyph expansion is available. */
    ASTRA_GRAPHICS_CAP_GLYPHS = 1u << 7,
    /** Bounded hardware flood fill is available. */
    ASTRA_GRAPHICS_CAP_FLOOD_FILL = 1u << 8,
    /** A copied 256-entry display palette is available. */
    ASTRA_GRAPHICS_CAP_PALETTE = 1u << 9
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
    /** Big-endian 16-bit Vega tile-map entries. */
    ASTRA_SURFACE_FORMAT_TILE16 = 6
};

/** Surface creation and access flags. */
enum {
    /** Surface may be presented for display scanout. */
    ASTRA_SURFACE_SCANOUT = 1u << 0,
    /** Surface may receive draw-list output. */
    ASTRA_SURFACE_DRAW_TARGET = 1u << 1,
    /** Surface may be read by graphics operations. */
    ASTRA_SURFACE_DRAW_SOURCE = 1u << 2,
    /** Surface contains validated 16-bit tile-map entries. */
    ASTRA_SURFACE_TILE_MAP = 1u << 3,
    /** Process may map the surface read-only. */
    ASTRA_SURFACE_CPU_READ = 1u << 4,
    /** Process may map the surface for writes. */
    ASTRA_SURFACE_CPU_WRITE = 1u << 5
};

/** Drawing behavior flags used by ::AstraDrawPaint. */
enum {
    /** Zero mask/pattern bits write the paint background. */
    ASTRA_DRAW_OPAQUE_BACKGROUND = 1u << 0
};

/** Sprite update flags. */
enum {
    /** Sprite participates in composition. */
    ASTRA_SPRITE_VISIBLE = 1u << 0,
    /** Reflect the source rectangle horizontally. */
    ASTRA_SPRITE_FLIP_X = 1u << 1,
    /** Reflect the source rectangle vertically. */
    ASTRA_SPRITE_FLIP_Y = 1u << 2,
    /** Composite below the framebuffer instead of above it. */
    ASTRA_SPRITE_BEHIND_FRAMEBUFFER = 1u << 3,
    /** Include this sprite in collision detection. */
    ASTRA_SPRITE_COLLISION_ENABLE = 1u << 4
};

/** Tile-layer update flags. */
enum {
    /** Layer participates in composition. */
    ASTRA_TILE_LAYER_VISIBLE = 1u << 0,
    /** Composite above the framebuffer and sprites. */
    ASTRA_TILE_LAYER_ABOVE_FRAMEBUFFER = 1u << 1,
    /** Wrap horizontal tile-map coordinates. */
    ASTRA_TILE_LAYER_WRAP_X = 1u << 2,
    /** Wrap vertical tile-map coordinates. */
    ASTRA_TILE_LAYER_WRAP_Y = 1u << 3
};

/** Sticky and frame-local flags returned by ::AstraDisplayStatus. */
enum {
    /** A scanline missed its display-fetch deadline. */
    ASTRA_DISPLAY_STATUS_VIDEO_UNDERRUN = 1u << 0,
    /** The per-line sprite pixel budget was exceeded this frame. */
    ASTRA_DISPLAY_STATUS_SPRITE_OVERFLOW = 1u << 1,
    /** Vega rejected one or more active hardware descriptors. */
    ASTRA_DISPLAY_STATUS_CONFIG_ERROR = 1u << 2
};

/** Static hardware sprite limits for the primary 720-pixel scanout mode. */
enum {
    /** Number of hardware sprite descriptors. */
    ASTRA_GRAPHICS_SPRITE_COUNT = 64,
    /** Maximum INDEX8 source width. */
    ASTRA_SPRITE_SOURCE_WIDTH_MAX = 128,
    /** Maximum INDEX8 source height. */
    ASTRA_SPRITE_SOURCE_HEIGHT_MAX = 128,
    /** Maximum destination width or height after scaling. */
    ASTRA_SPRITE_DESTINATION_EXTENT_MAX = 1024,
    /** Guaranteed aggregate admitted sprite pixels per scanline. */
    ASTRA_SPRITE_PIXELS_PER_LINE = 8192,
    /** Number of independently selectable 256-entry palette banks. */
    ASTRA_SPRITE_PALETTE_BANK_COUNT = 16
};

/** Raster-program targets exposed by the validated display service. */
enum {
    /** Change the 24-bit backdrop color. */
    ASTRA_RASTER_TARGET_BACKDROP = 1,
    /** Change one 24-bit display-palette entry. */
    ASTRA_RASTER_TARGET_PALETTE = 2,
    /** Stage a framebuffer base for the next vertical blank. */
    ASTRA_RASTER_TARGET_FRAMEBUFFER_BASE = 3,
    /** Change tile layer zero's packed signed scroll value. */
    ASTRA_RASTER_TARGET_TILE0_SCROLL = 4,
    /** Change tile layer one's packed signed scroll value. */
    ASTRA_RASTER_TARGET_TILE1_SCROLL = 5
};

/** Opaque connection to one display output. */
typedef struct AstraDisplay {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraDisplay;
/** Opaque storage and format object. */
typedef struct AstraSurface {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraSurface;
/** Mutable command list until submission. */
typedef struct AstraDrawList {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraDrawList;
/** Mutable copied 256-entry display palette. */
typedef struct AstraPalette {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraPalette;
/** Mutable validated pair of hardware tile layers. */
typedef struct AstraTileLayers {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraTileLayers;
/** Mutable validated hardware-sprite set. */
typedef struct AstraSpriteSet {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraSpriteSet;
/** Immutable, validated raster-change program. */
typedef struct AstraRasterProgram {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraRasterProgram;
/** One-shot asynchronous completion object. */
typedef struct AstraFence {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraFence;

/** Initializer for an empty ::AstraDisplay. */
#define ASTRA_DISPLAY_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraSurface. */
#define ASTRA_SURFACE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraDrawList. */
#define ASTRA_DRAW_LIST_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraPalette. */
#define ASTRA_PALETTE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraTileLayers. */
#define ASTRA_TILE_LAYERS_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraSpriteSet. */
#define ASTRA_SPRITE_SET_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraRasterProgram. */
#define ASTRA_RASTER_PROGRAM_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraFence. */
#define ASTRA_FENCE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for ::AstraGraphicsInfo. */
#define ASTRA_GRAPHICS_INFO_INIT \
    { sizeof(AstraGraphicsInfo), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0 } }
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
      { 0, 0, 0, 0 } }
/** Initializer for ::AstraTileLayerUpdate. */
#define ASTRA_TILE_LAYER_UPDATE_INIT \
    { sizeof(AstraTileLayerUpdate), 0, 0, 0, 0, 0, 8, 0, 0, \
      { 0, 0, 0, 0 } }
/** Initializer for ::AstraSpriteUpdate. */
#define ASTRA_SPRITE_UPDATE_INIT \
    { sizeof(AstraSpriteUpdate), 0, { 0, 0, 0, 0 }, { 0, 0 }, 0, \
      0, 0, 0, 255, 0, 0, 0, 0, { 0, 0 } }
/** Initializer for ::AstraDisplayStatus. */
#define ASTRA_DISPLAY_STATUS_INIT \
    { sizeof(AstraDisplayStatus), 0, 0, { 0, 0, 0, 0, 0 } }
/** Initializer for ::AstraPresentOptions. */
#define ASTRA_PRESENT_OPTIONS_INIT \
    { sizeof(AstraPresentOptions), 0, 0, 0, 0, 0, \
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
/** Declare a palette handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_PALETTE(name) \
    AstraPalette name ASTRA_CLEANUP(astra_palette_cleanup) = \
        ASTRA_PALETTE_INIT
/** Declare tile-layer handles that close themselves at normal scope exit. */
#define ASTRA_AUTO_TILE_LAYERS(name) \
    AstraTileLayers name ASTRA_CLEANUP(astra_tile_layers_cleanup) = \
        ASTRA_TILE_LAYERS_INIT
/** Declare a sprite-set handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_SPRITE_SET(name) \
    AstraSpriteSet name ASTRA_CLEANUP(astra_sprite_set_cleanup) = \
        ASTRA_SPRITE_SET_INIT
/** Declare a raster-program handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_RASTER_PROGRAM(name) \
    AstraRasterProgram name ASTRA_CLEANUP(astra_raster_program_cleanup) = \
        ASTRA_RASTER_PROGRAM_INIT
/** Declare a fence handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_FENCE(name) \
    AstraFence name ASTRA_CLEANUP(astra_fence_cleanup) = ASTRA_FENCE_INIT

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

/** Static graphics and output limits. */
typedef struct AstraGraphicsInfo {
    /** Structure size in bytes; initialize with `sizeof(AstraGraphicsInfo)`. */
    uint32_t size;
    /** Bitwise `ASTRA_GRAPHICS_CAP_*` values. */
    uint32_t capabilities;
    /** Physical output width in pixels. */
    uint16_t output_width;
    /** Physical output height in pixels. */
    uint16_t output_height;
    /** Largest supported surface width. */
    uint16_t max_surface_width;
    /** Largest supported surface height. */
    uint16_t max_surface_height;
    /** Number of hardware sprite descriptors. */
    uint16_t sprite_count;
    /** Number of hardware tile layers. */
    uint16_t tile_layer_count;
    /** Largest supported sprite source width. */
    uint16_t max_sprite_width;
    /** Largest supported sprite source height. */
    uint16_t max_sprite_height;
    /** Guaranteed aggregate admitted sprite pixels per scanline. */
    uint16_t max_sprite_pixels_per_line;
    /** Number of independently selectable sprite palette banks. */
    uint16_t sprite_palette_bank_count;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraGraphicsInfo;

/** Parameters for allocating a protected graphics surface. */
typedef struct AstraSurfaceCreateInfo {
    /** Structure size in bytes. */
    uint32_t size;
    /** Bitwise `ASTRA_SURFACE_*` usage rights. */
    uint32_t flags;
    /** Surface width in pixels or tile entries. */
    uint16_t width;
    /** Surface height in pixels or tile entries. */
    uint16_t height;
    /** One `ASTRA_PIXEL_FORMAT_*` or `ASTRA_SURFACE_FORMAT_*` value. */
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
    /** Surface width in pixels or tile entries. */
    uint16_t width;
    /** Surface height in pixels or tile entries. */
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
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraDrawPaint;

/** Repeating 8 by 8 monochrome pattern, most-significant bit first. */
typedef struct AstraPattern8 {
    /** Row-major bits; bit 63 is row zero, column zero. */
    uint64_t bits;
    /** Signed horizontal pattern origin. */
    int32_t origin_x;
    /** Signed vertical pattern origin. */
    int32_t origin_y;
} AstraPattern8;

/** One copied tile-layer configuration. */
typedef struct AstraTileLayerUpdate {
    /** Structure size in bytes. */
    uint32_t size;
    /** TILE16 map surface; copied and retained by the service. */
    const AstraSurface *map;
    /** INDEX4 tile-pattern surface; copied and retained by the service. */
    const AstraSurface *tiles;
    /** Bitwise `ASTRA_TILE_LAYER_*` values. */
    uint32_t flags;
    /** Signed horizontal scroll in pixels. */
    int32_t scroll_x;
    /** Signed vertical scroll in pixels. */
    int32_t scroll_y;
    /** Square tile size, either 8 or 16 pixels. */
    uint16_t tile_size;
    /** Transparent 4-bit pattern index. */
    uint8_t transparent_index;
    /** Reserved; initialize to zero. */
    uint8_t reserved8;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraTileLayerUpdate;

/** One copied hardware-sprite update. */
typedef struct AstraSpriteUpdate {
    /** Structure size in bytes. */
    uint32_t size;
    /** INDEX8 image surface retained by the service. */
    const AstraSurface *source;
    /** Source rectangle; width and height are independently 1 through 128. */
    AstraRectI32 source_rect;
    /** Signed top-left destination position. */
    AstraPointI32 destination;
    /** Bitwise `ASTRA_SPRITE_*` values. */
    uint32_t flags;
    /** Composition priority from zero through 255. */
    uint8_t priority;
    /** 256-entry palette bank from zero through 15. */
    uint8_t palette_bank;
    /** Transparent 8-bit source index. */
    uint8_t transparent_index;
    /** Global opacity from transparent zero through opaque 255. */
    uint8_t opacity;
    /** Scaled destination width from 1 through 1024. */
    uint16_t destination_width;
    /** Scaled destination height from 1 through 1024. */
    uint16_t destination_height;
    /** Collision class bits contributed by this sprite. */
    uint16_t collision_class;
    /** Collision classes eligible to collide with this sprite. */
    uint16_t collision_mask;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[2];
} AstraSpriteUpdate;

/** One validated beam-synchronized register change. */
typedef struct AstraRasterChange {
    /** Physical beam line from zero through 479. */
    uint16_t beam_y;
    /** Physical beam column from zero through 719. */
    uint16_t beam_x;
    /** One `ASTRA_RASTER_TARGET_*` value. */
    uint16_t target;
    /** Palette entry or other target-specific index. */
    uint16_t target_index;
    /** Target-specific 32-bit register value. */
    uint32_t value;
} AstraRasterChange;

/** Latest display diagnostics and sprite collision result. */
typedef struct AstraDisplayStatus {
    /** Structure size in bytes. */
    uint32_t size;
    /** Bitwise `ASTRA_DISPLAY_STATUS_*` values. */
    uint32_t flags;
    /** One bit per sprite that collided in the latest completed frame. */
    uint32_t sprite_collisions;
    /** Reserved for compatible growth; currently zero. */
    uint32_t reserved[5];
} AstraDisplayStatus;

/** Presentation policy for a page flip. */
typedef struct AstraPresentOptions {
    /** Structure size in bytes. */
    uint32_t size;
    /** Reserved presentation flags; initialize to zero. */
    uint32_t flags;
    /** Optional palette snapshot for indexed scanout, tiles, and sprites. */
    const AstraPalette *palette;
    /** Optional tile-layer snapshot. */
    const AstraTileLayers *tile_layers;
    /** Optional sprite-set snapshot. */
    const AstraSpriteSet *sprites;
    /** Optional immutable raster program. */
    const AstraRasterProgram *raster_program;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[4];
} AstraPresentOptions;

/**
 * Test whether a compatible graphics service is available.
 *
 * @return Nonzero when graphics services can be opened, otherwise zero.
 */
int astra_graphics_present(void);
/**
 * Query graphics limits and capabilities.
 *
 * @param[in,out] info Size-initialized structure that receives the result.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_graphics_get_info(AstraGraphicsInfo *info);

/**
 * Open the primary display output.
 *
 * @param[out] display Empty handle that receives the display.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_open(AstraDisplay *display);
/**
 * Close a display handle.
 *
 * @param[in,out] display Live display to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_close(AstraDisplay *display);

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
 * Create an empty draw list with one destination and mandatory clip rectangle.
 *
 * @param[in] destination RGB565 or INDEX8 draw-target surface.
 * @param[in] clip Nonempty destination clip rectangle.
 * @param[out] draw_list Empty handle that receives the list.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_list_create(
    const AstraSurface *destination,
    const AstraRectI32 *clip,
    AstraDrawList *draw_list);
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
 * Append an outlined or filled circle.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] center Circle center.
 * @param[in] radius Nonnegative radius no larger than 32767.
 * @param[in] filled Zero for an outline, one for a fill.
 * @param[in] paint Foreground color and behavior.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_circle(
    AstraDrawList *draw_list, AstraPointI32 center, uint32_t radius, int filled,
    const AstraDrawPaint *paint);
/**
 * Append an outlined or filled axis-aligned ellipse.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] center Ellipse center.
 * @param[in] radius_x Nonnegative horizontal radius no larger than 32767.
 * @param[in] radius_y Nonnegative vertical radius no larger than 32767.
 * @param[in] filled Zero for an outline, one for a fill.
 * @param[in] paint Foreground color and behavior.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_ellipse(
    AstraDrawList *draw_list, AstraPointI32 center,
    uint32_t radius_x, uint32_t radius_y, int filled,
    const AstraDrawPaint *paint);
/**
 * Append a repeating 8 by 8 monochrome pattern fill.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] rectangle Nonempty destination rectangle.
 * @param[in] pattern Pattern bits and stable signed origin.
 * @param[in] paint Foreground and optional opaque-background colors.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_pattern_fill(
    AstraDrawList *draw_list, const AstraRectI32 *rectangle,
    const AstraPattern8 *pattern, const AstraDrawPaint *paint);
/**
 * Append a bounded scanline flood fill; workspace is service-owned.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] seed Seed point within the list's clip rectangle.
 * @param[in] paint Replacement color.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_flood_fill(
    AstraDrawList *draw_list, AstraPointI32 seed,
    const AstraDrawPaint *paint);
/**
 * Append an immutable text layout at a baseline-relative origin.
 *
 * @param[in,out] draw_list Mutable destination list.
 * @param[in] layout Validated immutable text layout.
 * @param[in] origin Layout origin in destination pixels.
 * @param[in] paint Text color and embedded-color policy.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_text_layout(
    AstraDrawList *draw_list, const AstraTextLayout *layout,
    AstraPointI32 origin, const AstraTextPaint *paint);

/**
 * Seal and asynchronously submit a draw list.
 *
 * @param[in,out] draw_list Mutable list that becomes sealed on success.
 * @param[out] fence Empty handle that receives completion ownership.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_submit(
    AstraDrawList *draw_list, AstraFence *fence);

/**
 * Create a copied display palette with 1 through 256 opaque entries.
 *
 * @param[in] display Display that owns the palette.
 * @param[in] entries Opaque sRGB entries copied before return.
 * @param[in] entry_count Number of entries from one through 256.
 * @param[out] palette Empty handle that receives the palette.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_palette_create(
    const AstraDisplay *display,
    const AstraColorRGBA8 *entries,
    uint32_t entry_count,
    AstraPalette *palette);
/**
 * Replace a contiguous range in a palette.
 *
 * @param[in,out] palette Mutable palette to update.
 * @param[in] first_entry First destination entry from zero through 255.
 * @param[in] entries Opaque sRGB entries copied before return.
 * @param[in] entry_count Nonzero count that remains within 256 entries.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_palette_update(
    AstraPalette *palette,
    uint32_t first_entry,
    const AstraColorRGBA8 *entries,
    uint32_t entry_count);
/**
 * Close a palette after referencing presentation fences retire.
 *
 * @param[in,out] palette Palette to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_palette_close(AstraPalette *palette);

/**
 * Allocate a pair of disabled hardware tile layers.
 *
 * @param[in] display Display that owns the layers.
 * @param[out] tile_layers Empty handle that receives the layer set.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_tile_layers_create(
    const AstraDisplay *display, AstraTileLayers *tile_layers);
/**
 * Replace one tile layer; a null update disables it.
 *
 * @param[in,out] tile_layers Mutable pair of tile layers.
 * @param[in] index Layer index zero or one.
 * @param[in] update Copied layer state, or null to disable the layer.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_tile_layers_update(
    AstraTileLayers *tile_layers, uint32_t index,
    const AstraTileLayerUpdate *update);
/**
 * Close tile layers after referencing presentation fences retire.
 *
 * @param[in,out] tile_layers Layer set to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_tile_layers_close(
    AstraTileLayers *tile_layers);

/**
 * Allocate a set containing up to 32 hardware sprites.
 *
 * @param[in] display Display that owns the sprite set.
 * @param[out] sprite_set Empty handle that receives the set.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_sprite_set_create(
    const AstraDisplay *display, AstraSpriteSet *sprite_set);
/**
 * Replace one sprite entry; a null update disables it.
 *
 * @param[in,out] sprite_set Mutable sprite set.
 * @param[in] index Sprite index from zero through 31.
 * @param[in] update Copied sprite state, or null to disable the entry.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_sprite_set_update(
    AstraSpriteSet *sprite_set, uint32_t index,
    const AstraSpriteUpdate *update);
/**
 * Close a sprite set after referencing presentation fences retire.
 *
 * @param[in,out] sprite_set Sprite set to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_sprite_set_close(AstraSpriteSet *sprite_set);

/**
 * Create an immutable validated raster program from ordered changes.
 *
 * @param[in] display Display that owns the program.
 * @param[in] changes Beam-ordered changes copied before return.
 * @param[in] change_count Number of changes from one through 2047.
 * @param[out] program Empty handle that receives the program.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_raster_program_create(
    const AstraDisplay *display,
    const AstraRasterChange *changes,
    uint32_t change_count,
    AstraRasterProgram *program);
/**
 * Close a raster program after referencing presentation fences retire.
 *
 * @param[in,out] program Program to close; emptied on success.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_raster_program_close(
    AstraRasterProgram *program);

/**
 * Queue a vertical-blank-synchronized page flip and optional overlays.
 *
 * @param[in] display Destination display.
 * @param[in] surface Scanout-capable RGB565 or INDEX8 surface.
 * @param[in] options Optional size-initialized presentation snapshot.
 * @param[out] fence Empty handle signaled after presentation retires.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_present_surface(
    const AstraDisplay *display,
    const AstraSurface *surface,
    const AstraPresentOptions *options,
    AstraFence *fence);
/**
 * Read the latest sticky display diagnostics and collision bitmap.
 *
 * @param[in] display Display to query.
 * @param[in,out] status Size-initialized structure that receives the result.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_display_get_status(
    const AstraDisplay *display, AstraDisplayStatus *status);

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
/** Cleanup helper used by ::ASTRA_AUTO_PALETTE. @param palette Value to close. */
void astra_palette_cleanup(AstraPalette *palette);
/** Cleanup helper used by ::ASTRA_AUTO_TILE_LAYERS. @param tile_layers Value to close. */
void astra_tile_layers_cleanup(AstraTileLayers *tile_layers);
/** Cleanup helper used by ::ASTRA_AUTO_SPRITE_SET. @param sprite_set Value to close. */
void astra_sprite_set_cleanup(AstraSpriteSet *sprite_set);
/** Cleanup helper used by ::ASTRA_AUTO_RASTER_PROGRAM. @param program Value to close. */
void astra_raster_program_cleanup(AstraRasterProgram *program);
/** Cleanup helper used by ::ASTRA_AUTO_FENCE. @param fence Value to close. */
void astra_fence_cleanup(AstraFence *fence);

/** @} */

ASTRA_EXTERN_C_END

#endif
