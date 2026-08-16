#ifndef ASTRA_USERSPACE_SURFACE_H
#define ASTRA_USERSPACE_SURFACE_H

#include <stdint.h>

typedef struct AstraSurfaceView {
    uint16_t *pixels;
    uint32_t byte_size;
    uint32_t pitch;
    uint16_t width;
    uint16_t height;
    uint16_t kind;
    uint16_t reserved;
} AstraSurfaceView;

enum {
    ASTRA_SURFACE_VIEW_RGB565 = 1u,
    ASTRA_SURFACE_VIEW_DRAW_LIST = 2u,
};

typedef struct AstraSharedSurface {
    AstraSurfaceView view;
    uint32_t area;
    void *mapping;
} AstraSharedSurface;

int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch);
int astra_draw_list_view_init(AstraSurfaceView *surface, void *storage,
                              uint32_t byte_size, uint16_t width,
                              uint16_t height);
int astra_draw_list_view_adopt(AstraSurfaceView *surface, void *storage,
                               uint32_t byte_size, uint16_t width,
                               uint16_t height);
uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue);
void astra_surface_clear(AstraSurfaceView *surface, uint16_t color);
void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color);
void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color);
void astra_surface_blit(AstraSurfaceView *destination, int32_t x, int32_t y,
                        const AstraSurfaceView *source);
void astra_surface_blit_round(AstraSurfaceView *destination, int32_t x,
                              int32_t y, const AstraSurfaceView *source,
                              uint16_t radius);
void astra_surface_blit_round_bottom(AstraSurfaceView *destination, int32_t x,
                                     int32_t y,
                                     const AstraSurfaceView *source,
                                     uint16_t radius);
void astra_surface_glyph8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                            const uint8_t rows[8], uint16_t color);
void astra_surface_text8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *text, uint32_t length, uint8_t scale,
                           uint16_t color);

uint32_t astra_surface_ui_text_width(const char *utf8, uint32_t length,
                                     uint16_t pixel_height);
uint32_t astra_surface_ui_text_fit(const char *utf8, uint32_t length,
                                   uint16_t pixel_height,
                                   uint32_t maximum_width);
void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *utf8, uint32_t length,
                           uint16_t pixel_height, uint16_t color);
uint16_t astra_surface_mono_cell_width(uint16_t pixel_height);
void astra_surface_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                             const char *utf8, uint32_t length,
                             uint16_t pixel_height, uint16_t cell_width,
                             uint16_t color);
void astra_draw_list_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                               const char *utf8, uint32_t length,
                               uint16_t pixel_height, uint16_t cell_width,
                               uint16_t color);

uint32_t astra_shared_surface_create(AstraSharedSurface *surface,
                                     uint16_t width, uint16_t height);
uint32_t astra_shared_draw_list_create(AstraSharedSurface *surface,
                                       uint16_t width, uint16_t height);
uint32_t astra_shared_surface_adopt(AstraSharedSurface *surface,
                                    uint32_t area, uint16_t width,
                                    uint16_t height, uint32_t pitch,
                                    uint32_t map_flags);
uint32_t astra_shared_draw_list_adopt(AstraSharedSurface *surface,
                                      uint32_t area, uint16_t width,
                                      uint16_t height, uint32_t map_flags);
uint32_t astra_shared_surface_close(AstraSharedSurface *surface);

#endif
