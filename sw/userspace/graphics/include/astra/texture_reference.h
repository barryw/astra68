#ifndef ASTRA_USERSPACE_TEXTURE_REFERENCE_H
#define ASTRA_USERSPACE_TEXTURE_REFERENCE_H

/*
 * Bit-exact reference model of the Astraea texture engine
 * (docs/TEXTURE_ENGINE.md sections 2-6): triangle coverage with the
 * top-left rule, affine interpolation, sampling, modulation, blending and
 * destination conversion. It is the oracle for RTL simulation and for the
 * DE25 certification tool. Surfaces are addressed exactly as they sit in the
 * graphics arena: big-endian pixels, INDEX8 palettes as 256 big-endian
 * 0xAARRGGBB words.
 */

#include <stdint.h>

/* Vertex x and y must lie in [-LIMIT, LIMIT) in 24.8 units (+-32768 px). */
#define ASTRA_TEXTURE_COORD_LIMIT (INT32_C(1) << 23)

typedef struct AstraTextureVertex {
    int32_t x;      /* signed 24.8 destination pixels */
    int32_t y;
    int32_t u;      /* signed 16.16 source texels */
    int32_t v;
    uint32_t color; /* 0xAARRGGBB straight alpha */
} AstraTextureVertex;

typedef struct AstraTextureSurface {
    uint8_t *data;           /* byte address of pixel (0, 0) */
    const uint8_t *palette;  /* INDEX8 sources only */
    uint32_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t format;          /* ASTRA_RENDER_FORMAT_* */
} AstraTextureSurface;

/* Command clip; left/top inclusive, right/bottom exclusive. */
typedef struct AstraTextureClip {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
} AstraTextureClip;

/* (a * b + 127) / 255 */
uint8_t astra_texture_mul255(uint8_t a, uint8_t b);

/* Straight-alpha ARGB texel at 16.16 (u, v), nearest or linear. */
uint32_t astra_texture_sample(const AstraTextureSurface *source,
                              int32_t u, int32_t v, int linear);

/* One blend mode (ASTRA_RENDER_TRIANGLE_OPTION_BLEND_*) on ARGB values.
 * Destinations without alpha pass dst.a = 255. */
uint32_t astra_texture_blend(unsigned mode, uint32_t source,
                             uint32_t destination);

/* Rounded conversion of ARGB to the destination's canonical pixel value. */
uint32_t astra_texture_pack(uint8_t format, uint32_t argb);

/* Status the engine returns for these operands before any pixel write, or
 * ASTRA_RENDER_STATUS_OK. source may be NULL (untextured). */
unsigned astra_texture_validate(const AstraTextureSurface *destination,
                                const AstraTextureSurface *source,
                                uint32_t options,
                                const AstraTextureVertex *vertices,
                                uint32_t triangle_count);

/* Draws validated triangles; returns the number of pixels written. */
uint32_t astra_texture_draw(const AstraTextureSurface *destination,
                            const AstraTextureSurface *source,
                            uint32_t options, AstraTextureClip clip,
                            const AstraTextureVertex *vertices,
                            uint32_t triangle_count);

#endif
