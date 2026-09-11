#ifndef ASTRA_SURFACE_H
#define ASTRA_SURFACE_H

/** @file surface.h @brief Caller-owned raster and draw-list surface views. */

#include <stdint.h>

#include <astra/text_style.h>

/** One clipped RGB565 or hardware draw-list destination. */
typedef struct AstraSurfaceView {
    /** Mapped pixels or serialized draw-list storage. */
    uint16_t *pixels;
    /** Available bytes at @ref pixels. */
    uint32_t byte_size;
    /** RGB565 row pitch in bytes; zero for draw lists. */
    uint32_t pitch;
    /** Logical width in pixels. */
    uint16_t width;
    /** Logical height in pixels. */
    uint16_t height;
    /** ASTRA_SURFACE_VIEW_* representation kind. */
    uint16_t kind;
    /** Inclusive clip left edge. */
    uint16_t clip_left;
    /** Inclusive clip top edge. */
    uint16_t clip_top;
    /** Exclusive clip right edge. */
    uint16_t clip_right;
    /** Exclusive clip bottom edge. */
    uint16_t clip_bottom;
} AstraSurfaceView;

/** Rectangular text viewport over an existing surface. */
typedef struct AstraTextBox {
    /** Borrowed destination surface. */
    AstraSurfaceView *surface;
    /** Left edge in destination coordinates. */
    uint32_t x;
    /** Top edge in destination coordinates. */
    uint32_t y;
    /** Width in pixels. */
    uint32_t width;
    /** Height in pixels. */
    uint32_t height;
} AstraTextBox;

/** Surface storage representation. */
enum {
    ASTRA_SURFACE_VIEW_RGB565 = 1u,
    ASTRA_SURFACE_VIEW_DRAW_LIST = 2u,
};

/** Shared-area-backed surface and its mapped client view. */
typedef struct AstraSharedSurface {
    /** Drawing view over @ref mapping. */
    AstraSurfaceView view;
    /** Owned shared-area handle. */
    uint32_t area;
    /** Owned local area mapping. */
    void *mapping;
} AstraSharedSurface;

/** Initialize a checked RGB565 view. @param surface View to initialize. @param pixels Pixel storage. @param byte_size Storage bytes. @param width Pixel width. @param height Pixel height. @param pitch Row stride in bytes. @return Nonzero when valid. */
int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch);
/** Replace the clipping rectangle, intersected with the surface. @param surface Initialized view. @param x Left edge. @param y Top edge. @param width Width. @param height Height. @return Nonzero when the resulting clip is nonempty. */
int astra_surface_clip(AstraSurfaceView *surface, int32_t x, int32_t y,
                       uint32_t width, uint32_t height);
/** Initialize and clear a hardware draw-list view. @param surface View to initialize. @param storage Command storage. @param byte_size Storage bytes. @param width Logical width. @param height Logical height. @return Nonzero when valid. */
int astra_draw_list_view_init(AstraSurfaceView *surface, void *storage,
                              uint32_t byte_size, uint16_t width,
                              uint16_t height);
/** Adopt an existing hardware draw list. @param surface View to initialize. @param storage Existing command storage. @param byte_size Storage bytes. @param width Logical width. @param height Logical height. @return Nonzero when valid. */
int astra_draw_list_view_adopt(AstraSurfaceView *surface, void *storage,
                               uint32_t byte_size, uint16_t width,
                               uint16_t height);
/** Pack 8-bit RGB as RGB565. @param red Red channel. @param green Green channel. @param blue Blue channel. @return Packed RGB565 pixel. */
uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue);
/** Clear the clipped surface. @param surface Initialized view. @param color RGB565 color. */
void astra_surface_clear(AstraSurfaceView *surface, uint16_t color);
/** Fill a clipped rectangle. @param surface Initialized view. @param x Left edge. @param y Top edge. @param width Width. @param height Height. @param color RGB565 color. */
void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color);
/** Fill a clipped rounded rectangle. @param surface Initialized view. @param x Left edge. @param y Top edge. @param width Width. @param height Height. @param radius Corner radius. @param color RGB565 color. */
void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color);
/** Copy a source view. @param destination Destination view. @param x Destination x. @param y Destination y. @param source Source view. */
void astra_surface_blit(AstraSurfaceView *destination, int32_t x, int32_t y,
                        const AstraSurfaceView *source);
/** Copy through a rounded mask. @param destination Destination view. @param x Destination x. @param y Destination y. @param source Source view. @param radius Corner radius. */
void astra_surface_blit_round(AstraSurfaceView *destination, int32_t x,
                              int32_t y, const AstraSurfaceView *source,
                              uint16_t radius);
/** Copy with only bottom corners rounded. @param destination Destination view. @param x Destination x. @param y Destination y. @param source Source view. @param radius Corner radius. */
void astra_surface_blit_round_bottom(AstraSurfaceView *destination, int32_t x,
                                     int32_t y,
                                     const AstraSurfaceView *source,
                                     uint16_t radius);
/** Draw one monochrome 8x8 glyph. @param surface Destination. @param x Left edge. @param y Top edge. @param rows Eight bitmap rows. @param color RGB565 color. */
void astra_surface_glyph8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                            const uint8_t rows[8], uint16_t color);
/** Draw scaled legacy 8x8 text. @param surface Destination. @param x Left edge. @param y Top edge. @param text Text bytes. @param length Byte count. @param scale Integer scale. @param color RGB565 color. */
void astra_surface_text8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *text, uint32_t length, uint8_t scale,
                           uint16_t color);

/** Measure proportional UTF-8 text. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @return Advance width in pixels. */
uint32_t astra_surface_ui_text_width(const char *utf8, uint32_t length,
                                     uint16_t pixel_height);
/** Find the scalar-safe prefix fitting a width. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param maximum_width Available pixels. @return Fitting byte count. */
uint32_t astra_surface_ui_text_fit(const char *utf8, uint32_t length,
                                   uint16_t pixel_height,
                                   uint32_t maximum_width);
/** Draw proportional UTF-8 text. @param surface Destination. @param x Baseline origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param color RGB565 color. */
void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *utf8, uint32_t length,
                           uint16_t pixel_height, uint16_t color);
/** Draw styled proportional UTF-8 text. @param surface Destination. @param x Baseline origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param color RGB565 color. @param style_flags ASTRA_TEXT_STYLE_* mask. @return Nonzero on success. */
int astra_surface_ui_text_styled(AstraSurfaceView *surface, int32_t x,
                                 int32_t y, const char *utf8,
                                 uint32_t length, uint16_t pixel_height,
                                 uint16_t color, uint32_t style_flags);
/** Return monospace cell width for a font height. @param pixel_height Font height. @return Cell width. */
uint16_t astra_surface_mono_cell_width(uint16_t pixel_height);
/** Draw monospace UTF-8 text. @param surface Destination. @param x Cell origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param cell_width Cell width. @param color RGB565 color. */
void astra_surface_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                             const char *utf8, uint32_t length,
                             uint16_t pixel_height, uint16_t cell_width,
                             uint16_t color);
/** Draw styled monospace UTF-8 text. @param surface Destination. @param x Cell origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param cell_width Cell width. @param color RGB565 color. @param style_flags ASTRA_TEXT_STYLE_* mask. @return Nonzero on success. */
int astra_surface_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color,
                                   uint32_t style_flags);
/** Append monospace text to a hardware draw list. @param surface Draw-list destination. @param x Cell origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param cell_width Cell width. @param color RGB565 color. */
void astra_draw_list_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                               const char *utf8, uint32_t length,
                               uint16_t pixel_height, uint16_t cell_width,
                               uint16_t color);
/** Append styled monospace text to a draw list. @param surface Draw-list destination. @param x Cell origin x. @param y Baseline origin y. @param utf8 UTF-8 bytes. @param length Byte count. @param pixel_height Font height. @param cell_width Cell width. @param color RGB565 color. @param style_flags ASTRA_TEXT_STYLE_* mask. @return Nonzero on success. */
int astra_draw_list_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                     int32_t y, const char *utf8,
                                     uint32_t length, uint16_t pixel_height,
                                     uint16_t cell_width, uint16_t color,
                                     uint32_t style_flags);
/** Append an overlap-safe rectangular copy. @param surface Draw-list destination. @param source_x Source x. @param source_y Source y. @param destination_x Destination x. @param destination_y Destination y. @param width Width. @param height Height. @return Nonzero on success. */
int astra_draw_list_copy(AstraSurfaceView *surface, uint32_t source_x,
                         uint32_t source_y, uint32_t destination_x,
                         uint32_t destination_y, uint32_t width,
                         uint32_t height);
/** Scroll retained text-box content. @param text_box Surface viewport. @param pixels Positive moves up; negative moves down. @return Nonzero on success. */
int astra_text_box_scroll(AstraTextBox *text_box, int32_t pixels);

/** Allocate and map a shared RGB565 surface. @param surface Receives ownership. @param width Pixel width. @param height Pixel height. @return Astra status. */
uint32_t astra_shared_surface_create(AstraSharedSurface *surface,
                                     uint16_t width, uint16_t height);
/** Allocate and map a shared hardware draw list. @param surface Receives ownership. @param width Logical width. @param height Logical height. @return Astra status. */
uint32_t astra_shared_draw_list_create(AstraSharedSurface *surface,
                                       uint16_t width, uint16_t height);
/** Adopt a shared RGB565 area handle. @param surface Receives ownership. @param area Area handle. @param width Pixel width. @param height Pixel height. @param pitch Row stride. @param map_flags Area mapping permissions. @return Astra status. */
uint32_t astra_shared_surface_adopt(AstraSharedSurface *surface,
                                    uint32_t area, uint16_t width,
                                    uint16_t height, uint32_t pitch,
                                    uint32_t map_flags);
/** Adopt a shared draw-list area handle. @param surface Receives ownership. @param area Area handle. @param width Logical width. @param height Logical height. @param map_flags Area mapping permissions. @return Astra status. */
uint32_t astra_shared_draw_list_adopt(AstraSharedSurface *surface,
                                      uint32_t area, uint16_t width,
                                      uint16_t height, uint32_t map_flags);
/** Unmap and close a shared surface. @param surface Owned surface. @return Astra status. */
uint32_t astra_shared_surface_close(AstraSharedSurface *surface);

#endif
