#ifndef ASTRA_USERSPACE_SURFACE_H
#define ASTRA_USERSPACE_SURFACE_H

#include <stdint.h>

typedef struct AstraSurfaceView {
    uint16_t *pixels;
    uint32_t byte_size;
    uint32_t pitch;
    uint16_t width;
    uint16_t height;
} AstraSurfaceView;

typedef struct AstraSharedSurface {
    AstraSurfaceView view;
    uint32_t area;
    void *mapping;
} AstraSharedSurface;

int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch);
uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue);
void astra_surface_clear(AstraSurfaceView *surface, uint16_t color);
void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color);
void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color);
void astra_surface_blit(AstraSurfaceView *destination, int32_t x, int32_t y,
                        const AstraSurfaceView *source);
void astra_surface_blit_round_bottom(AstraSurfaceView *destination, int32_t x,
                                     int32_t y,
                                     const AstraSurfaceView *source,
                                     uint16_t radius);
void astra_surface_glyph8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                            const uint8_t rows[8], uint16_t color);

uint32_t astra_shared_surface_create(AstraSharedSurface *surface,
                                     uint16_t width, uint16_t height);
uint32_t astra_shared_surface_adopt(AstraSharedSurface *surface,
                                    uint32_t area, uint16_t width,
                                    uint16_t height, uint32_t pitch,
                                    uint32_t map_flags);
uint32_t astra_shared_surface_close(AstraSharedSurface *surface);

#endif
