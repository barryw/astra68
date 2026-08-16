#ifndef ASTRA_USERSPACE_GRAPHICS_LIBRARY_H
#define ASTRA_USERSPACE_GRAPHICS_LIBRARY_H

#include <stdint.h>

#include <astra/surface.h>

#define ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR 1u
#define ASTRA_GRAPHICS_LIBRARY_ABI_MINOR 0u

typedef struct AstraGraphicsLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    int (*surface_view_init)(AstraSurfaceView *, void *, uint32_t, uint16_t,
                             uint16_t, uint32_t);
    int (*draw_list_view_init)(AstraSurfaceView *, void *, uint32_t, uint16_t,
                               uint16_t);
    int (*draw_list_view_adopt)(AstraSurfaceView *, void *, uint32_t,
                                uint16_t, uint16_t);
    uint16_t (*rgb565)(uint8_t, uint8_t, uint8_t);
    void (*clear)(AstraSurfaceView *, uint16_t);
    void (*fill)(AstraSurfaceView *, int32_t, int32_t, uint32_t, uint32_t,
                 uint16_t);
    uint32_t (*shared_surface_create)(AstraSharedSurface *, uint16_t,
                                      uint16_t);
    uint32_t (*shared_draw_list_create)(AstraSharedSurface *, uint16_t,
                                        uint16_t);
    uint32_t (*shared_surface_close)(AstraSharedSurface *);
} AstraGraphicsLibraryV1;

#endif
