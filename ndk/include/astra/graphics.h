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
    /** The 64-entry hardware sprite compositor is available. */
    ASTRA_GRAPHICS_CAP_SPRITES = 1u << 2,
    /** Validated beam-synchronized raster programs are available. */
    ASTRA_GRAPHICS_CAP_RASTER_PROGRAM = 1u << 3,
    /** Asynchronous copy, fill, key, and mask blits are available. */
    ASTRA_GRAPHICS_CAP_BLITTER = 1u << 4,
    /** Hardware line and shape drawing is available. */
    ASTRA_GRAPHICS_CAP_GEOMETRY = 1u << 5,
    /** Hardware bitmap-glyph expansion is available. */
    ASTRA_GRAPHICS_CAP_GLYPHS = 1u << 6,
    /** Bounded hardware flood fill is available. */
    ASTRA_GRAPHICS_CAP_FLOOD_FILL = 1u << 7,
    /** A copied 256-entry display palette is available. */
    ASTRA_GRAPHICS_CAP_PALETTE = 1u << 8,
    /** Logical scenes are scaled to the fixed physical output in hardware. */
    ASTRA_GRAPHICS_CAP_DISPLAY_SCALER = 1u << 9,
    /** A native-resolution 32 by 32 ARGB pointer plane is available. */
    ASTRA_GRAPHICS_CAP_HARDWARE_POINTER = 1u << 10
};

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

/** Sticky and frame-local flags returned by ::AstraDisplayStatus. */
enum {
    /** A scanline missed its display-fetch deadline. */
    ASTRA_DISPLAY_STATUS_VIDEO_UNDERRUN = 1u << 0,
    /** The per-line sprite pixel budget was exceeded this frame. */
    ASTRA_DISPLAY_STATUS_SPRITE_OVERFLOW = 1u << 1,
    /** Vega rejected one or more active hardware descriptors. */
    ASTRA_DISPLAY_STATUS_CONFIG_ERROR = 1u << 2
};

/** Static physical-output and hardware-sprite limits. */
enum {
    /** Fixed physical output width. */
    ASTRA_GRAPHICS_OUTPUT_WIDTH = 1920,
    /** Fixed physical output height. */
    ASTRA_GRAPHICS_OUTPUT_HEIGHT = 1080,
    /** Number of hardware sprite descriptors. */
    ASTRA_GRAPHICS_SPRITE_COUNT = 64,
    /** Native hardware-pointer image width. */
    ASTRA_HARDWARE_POINTER_WIDTH = 32,
    /** Native hardware-pointer image height. */
    ASTRA_HARDWARE_POINTER_HEIGHT = 32,
    /** Maximum INDEX8 source width. */
    ASTRA_SPRITE_SOURCE_WIDTH_MAX = 128,
    /** Maximum INDEX8 source height. */
    ASTRA_SPRITE_SOURCE_HEIGHT_MAX = 128,
    /** Maximum destination width or height after scaling. */
    ASTRA_SPRITE_DESTINATION_EXTENT_MAX = 2047,
    /** Maximum complete sprite spans admitted on one scanline. */
    ASTRA_SPRITES_PER_LINE = 16,
    /** Guaranteed aggregate admitted sprite pixels per scanline. */
    ASTRA_SPRITE_PIXELS_PER_LINE = 2048,
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
    ASTRA_RASTER_TARGET_FRAMEBUFFER_BASE = 3
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
    uint32_t _private_destination;
    void *_private_commands;
    uint32_t _private_bytes;
    uint32_t _private_sealed;
    AstraRectI32 _private_clip;
    /** @endcond */
} AstraDrawList;
/** Mutable copied 256-entry display palette. */
typedef struct AstraPalette {
    /** Private NDK handle; applications must not inspect this field. */
    AstraHandle _private_handle;
} AstraPalette;
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
#define ASTRA_DISPLAY_INIT { ASTRA_INVALID_HANDLE, 0, ASTRA_INVALID_HANDLE, 0, 0 }
/** Initializer for an empty ::AstraSurface. */
#define ASTRA_SURFACE_INIT \
    { ASTRA_INVALID_HANDLE, 0, 0, 0, 0, 0, 0, 0, 0 }
/** Initializer for an empty ::AstraDrawList. */
#define ASTRA_DRAW_LIST_INIT \
    { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0, 0 } }
/** Initializer for an empty ::AstraPalette. */
#define ASTRA_PALETTE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraSpriteSet. */
#define ASTRA_SPRITE_SET_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraRasterProgram. */
#define ASTRA_RASTER_PROGRAM_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for an empty ::AstraFence. */
#define ASTRA_FENCE_INIT { ASTRA_INVALID_HANDLE }
/** Initializer for ::AstraGraphicsInfo. */
#define ASTRA_GRAPHICS_INFO_INIT \
    { sizeof(AstraGraphicsInfo), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
      { 0, 0, 0 } }
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
/** Initializer for ::AstraSpriteUpdate. */
#define ASTRA_SPRITE_UPDATE_INIT \
    { sizeof(AstraSpriteUpdate), 0, { 0, 0, 0, 0 }, { 0, 0 }, 0, \
      0, 0, 0, 255, 0, 0, 0, 0, { 0, 0 } }
/** Initializer for ::AstraDisplayStatus. */
#define ASTRA_DISPLAY_STATUS_INIT \
    { sizeof(AstraDisplayStatus), 0, 0, { 0, 0, 0, 0, 0 } }
/** Initializer for ::AstraPresentOptions. */
#define ASTRA_PRESENT_OPTIONS_INIT \
    { sizeof(AstraPresentOptions), 0, 0, 0, 0, { 0, 0, 0, 0 } }
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
/** Declare a palette handle that closes itself at normal scope exit. */
#define ASTRA_AUTO_PALETTE(name) \
    AstraPalette name ASTRA_CLEANUP(astra_palette_cleanup) = \
        ASTRA_PALETTE_INIT
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
    /** Largest supported sprite source width. */
    uint16_t max_sprite_width;
    /** Largest supported sprite source height. */
    uint16_t max_sprite_height;
    /** Guaranteed aggregate admitted sprite pixels per scanline. */
    uint16_t max_sprite_pixels_per_line;
    /** Number of independently selectable sprite palette banks. */
    uint16_t sprite_palette_bank_count;
    /** Maximum complete sprite spans admitted on one scanline. */
    uint16_t max_sprites_per_line;
    /** Reserved for compatible growth; initialize to zero. */
    uint16_t _reserved0;
    /** Reserved for compatible growth; initialize to zero. */
    uint32_t reserved[3];
} AstraGraphicsInfo;

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

/** Repeating 8 by 8 monochrome pattern, most-significant bit first. */
typedef struct AstraPattern8 {
    /** Row-major bits; bit 63 is row zero, column zero. */
    uint64_t bits;
    /** Signed horizontal pattern origin. */
    int32_t origin_x;
    /** Signed vertical pattern origin. */
    int32_t origin_y;
} AstraPattern8;

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
    /** Logical source beam line. */
    uint16_t beam_y;
    /** Logical source beam column. */
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
    /** Optional palette snapshot for indexed scanout and sprites. */
    const AstraPalette *palette;
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
/** Apply a logical mode atomically at vertical blank. @param display Open display. @param mode Requested mode. @param fence Optional completion fence. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_display_set_mode(
    const AstraDisplay *display, const AstraDisplayMode *mode,
    AstraFence *fence);
/** Replace the copied native hardware-pointer image at vertical blank. @param display Open display. @param image Validated image. @param fence Optional completion fence. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_display_set_pointer_image(
    const AstraDisplay *display, const AstraHardwarePointerImage *image,
    AstraFence *fence);
/** Move and enable or disable the native hardware pointer at vertical blank. @param display Open display. @param position Physical screen position. @param enabled Nonzero to enable. @param fence Optional completion fence. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_display_set_pointer_state(
    const AstraDisplay *display, AstraPointI32 position, int enabled,
    AstraFence *fence);

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
 * @param[in] paint Text color, decoration, and embedded-color policy.
 * @return ::ASTRA_OK on success or a negative ::AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_draw_text_layout(
    AstraDrawList *draw_list, const AstraTextLayout *layout,
    AstraPointI32 origin, const AstraTextPaint *paint);

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
 * Allocate a set containing up to 64 hardware sprites.
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
 * @param[in] index Sprite index from zero through 63.
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
/** Cleanup helper used by ::ASTRA_AUTO_SPRITE_SET. @param sprite_set Value to close. */
void astra_sprite_set_cleanup(AstraSpriteSet *sprite_set);
/** Cleanup helper used by ::ASTRA_AUTO_RASTER_PROGRAM. @param program Value to close. */
void astra_raster_program_cleanup(AstraRasterProgram *program);
/** Cleanup helper used by ::ASTRA_AUTO_FENCE. @param fence Value to close. */
void astra_fence_cleanup(AstraFence *fence);

/** @} */

ASTRA_EXTERN_C_END

#endif
