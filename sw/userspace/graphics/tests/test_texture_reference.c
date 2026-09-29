// SPDX-License-Identifier: MIT
// Hand-computed checks of the texture-engine reference model.

#include <astra/texture_reference.h>

#include "astra_render_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define PX(p) ((int32_t)((p) * 256.0))
#define TX(t) ((int32_t)((t) * 65536.0))

static uint8_t pixels[16 * 16 * 4];
static uint8_t texels[16 * 16 * 4];

static AstraTextureSurface surface(uint8_t *data, uint8_t format,
                                   uint16_t width, uint16_t height)
{
    unsigned bpp = format == ASTRA_RENDER_FORMAT_RGB565 ? 2u
                   : format == ASTRA_RENDER_FORMAT_A8 ||
                           format == ASTRA_RENDER_FORMAT_INDEX8
                       ? 1u
                       : 4u;
    AstraTextureSurface s = {data, NULL, width * bpp, width, height, format};
    return s;
}

static uint32_t xrgb_at(const AstraTextureSurface *s, unsigned x, unsigned y)
{
    const uint8_t *p = s->data + y * s->pitch + x * 4u;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static AstraTextureVertex vertex(int32_t x, int32_t y, int32_t u, int32_t v,
                                 uint32_t color)
{
    AstraTextureVertex out = {x, y, u, v, color};
    return out;
}

static const AstraTextureClip everything = {0, 0, 16, 16};

static void test_mul255(void)
{
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b) {
            unsigned x = a * b;
            /* RTL divide_255_round: (x + 128 + ((x + 128) >> 8)) >> 8 */
            CHECK(astra_texture_mul255((uint8_t)a, (uint8_t)b) ==
                  ((x + 128u + ((x + 128u) >> 8)) >> 8));
        }
    CHECK(astra_texture_mul255(127, 1) == 0);
    CHECK(astra_texture_mul255(255, 128) == 128);
}

static void test_blend_modes(void)
{
    const uint32_t s = 0x80ff8000u, d = 0x40204080u;

    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE, s, d) ==
          s);
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND, s,
                              d) == 0xa0906040u);
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD, s, d) ==
          0x40a08080u);
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MOD, s, d) ==
          0x40202000u);
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MUL, s, d) ==
          0x40304040u);
    /* ADD and MUL saturate. */
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD,
                              0xffffffffu, 0xff808080u) == 0xffffffffu);
    CHECK(astra_texture_blend(ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MUL,
                              0x00ffffffu, 0xffc0c0c0u) == 0xffffffffu);
    CHECK(astra_texture_pack(ASTRA_RENDER_FORMAT_RGB565, 0xff808080u) ==
          0x8410u);
    CHECK(astra_texture_pack(ASTRA_RENDER_FORMAT_XRGB8888, 0x12345678u) ==
          0xff345678u);
}

static void test_linear_filter_and_clamp(void)
{
    AstraTextureSurface t = surface(texels, ASTRA_RENDER_FORMAT_XRGB8888, 2, 2);

    memset(texels, 0, sizeof(texels));
    put32(texels + 4, 0x00ff0000u);  /* (1,0) red 255 */
    put32(texels + 12, 0x00ff0000u); /* (1,1) red 255 */
    /* fx = 128: 255 * 128 / 256 = 127.5 rounds up. */
    CHECK(astra_texture_sample(&t, TX(1.0), TX(1.0), 1) == 0xff800000u);
    /* fx = 192: (255 * 192 * 256 + 32768) >> 16 = 191. */
    CHECK(astra_texture_sample(&t, TX(1.25), TX(1.0), 1) == 0xffbf0000u);
    /* Only (1,1) lit, fx = fy = 128: (255 * 128 * 128 + 32768) >> 16 = 64. */
    put32(texels + 4, 0);
    CHECK(astra_texture_sample(&t, TX(1.0), TX(1.0), 1) == 0xff400000u);
    /* Clamp: taps left of and above the surface collapse onto (0,0). */
    CHECK(astra_texture_sample(&t, TX(0.0), TX(0.0), 1) == 0xff000000u);
    CHECK(astra_texture_sample(&t, TX(-5.0), TX(-5.0), 0) == 0xff000000u);
    CHECK(astra_texture_sample(&t, TX(100.0), TX(100.0), 0) == 0xffff0000u);
    CHECK(astra_texture_sample(&t, TX(100.0), TX(100.0), 1) == 0xffff0000u);
    /* A8 is white with coverage alpha; RGB565 expands by replication. */
    AstraTextureSurface a8 = surface(texels, ASTRA_RENDER_FORMAT_A8, 1, 1);
    texels[0] = 0x5a;
    CHECK(astra_texture_sample(&a8, 0, 0, 0) == 0x5affffffu);
    AstraTextureSurface rgb = surface(texels, ASTRA_RENDER_FORMAT_RGB565, 1, 1);
    texels[0] = 0x84;
    texels[1] = 0x10;
    CHECK(astra_texture_sample(&rgb, 0, 0, 0) == 0xff848284u);
}

/* Two triangles sharing a diagonal through pixel centres: ADD by one must
 * leave every covered pixel at exactly one. */
static void test_shared_edge(void)
{
    AstraTextureSurface d = surface(pixels, ASTRA_RENDER_FORMAT_XRGB8888, 8, 8);
    const uint32_t c = 0xff010101u;
    AstraTextureVertex quad[6] = {
        vertex(PX(0.5), PX(0.5), 0, 0, c), vertex(PX(7.5), PX(0.5), 0, 0, c),
        vertex(PX(7.5), PX(7.5), 0, 0, c), vertex(PX(0.5), PX(0.5), 0, 0, c),
        vertex(PX(0.5), PX(7.5), 0, 0, c), vertex(PX(7.5), PX(7.5), 0, 0, c),
    };

    memset(pixels, 0, sizeof(pixels));
    CHECK(astra_texture_validate(&d, NULL,
                                 ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD, quad,
                                 2) == ASTRA_RENDER_STATUS_OK);
    CHECK(astra_texture_draw(&d, NULL, ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD,
                             everything, quad, 2) == 49u);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK(xrgb_at(&d, x, y) ==
                  (x < 7 && y < 7 ? 0xff010101u : 0u));

    /* Clip: only [2,5) x [1,3) is visited. */
    AstraTextureClip clip = {2, 1, 5, 3};
    memset(pixels, 0, sizeof(pixels));
    CHECK(astra_texture_draw(&d, NULL, ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD,
                             clip, quad, 2) == 6u);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            CHECK(xrgb_at(&d, x, y) ==
                  (x >= 2 && x < 5 && y >= 1 && y < 3 ? 0xff010101u : 0u));

    /* Zero area draws nothing. */
    AstraTextureVertex line[3] = {vertex(0, 0, 0, 0, c),
                                  vertex(PX(4), PX(4), 0, 0, c),
                                  vertex(PX(8), PX(8), 0, 0, c)};
    CHECK(astra_texture_draw(&d, NULL, 0, everything, line, 1) == 0u);
}

/* u runs 0..8 texels across 8 pixels, so row 0 is the texture itself; red
 * runs 0..255 and rounds half up at the midpoint. */
static void test_interpolation(void)
{
    AstraTextureSurface d = surface(pixels, ASTRA_RENDER_FORMAT_ARGB8888, 8, 8);
    AstraTextureSurface t = surface(texels, ASTRA_RENDER_FORMAT_ARGB8888, 8, 1);
    AstraTextureVertex tri[3] = {
        vertex(PX(0.5), PX(0.5), 0, 0, 0xff000000u),
        vertex(PX(8.5), PX(0.5), TX(8.0), 0, 0xffff0000u),
        vertex(PX(0.5), PX(8.5), 0, 0, 0xff000000u),
    };

    for (unsigned x = 0; x < 8; ++x)
        put32(texels + x * 4u, 0xf0000000u | x * 0x010101u);
    memset(pixels, 0, sizeof(pixels));
    CHECK(astra_texture_draw(&d, &t, ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE,
                             everything, tri, 1) == 36u);
    for (unsigned x = 0; x < 8; ++x) {
        unsigned red = (255u * x * 2u + 8u) / 16u; /* round half up */
        uint32_t tex = 0xf0000000u | x * 0x010101u;

        CHECK(xrgb_at(&d, x, 0) ==
              (0xf0000000u |
               (uint32_t)astra_texture_mul255((uint8_t)(tex >> 16), (uint8_t)red)
                   << 16));
    }
    CHECK(((255u * 4u * 2u + 8u) / 16u) == 128u);
    CHECK(xrgb_at(&d, 7, 1) == 0u); /* on the hypotenuse: right edge */

    /* Winding is irrelevant. */
    AstraTextureVertex flipped[3] = {tri[0], tri[2], tri[1]};
    uint8_t first[sizeof(pixels)];
    memcpy(first, pixels, sizeof(first));
    memset(pixels, 0, sizeof(pixels));
    CHECK(astra_texture_draw(&d, &t, 0, everything, flipped, 1) == 36u);
    CHECK(memcmp(first, pixels, sizeof(first)) == 0);
}

static void test_validation(void)
{
    AstraTextureSurface d = surface(pixels, ASTRA_RENDER_FORMAT_RGB565, 8, 8);
    AstraTextureSurface i8 = surface(pixels, ASTRA_RENDER_FORMAT_INDEX8, 8, 8);
    AstraTextureVertex tri[3] = {vertex(0, 0, 0, 0, 0),
                                 vertex(PX(4), 0, 0, 0, 0),
                                 vertex(0, PX(4), 0, 0, 0)};

    CHECK(astra_texture_validate(&d, NULL, 0, tri, 1) == ASTRA_RENDER_STATUS_OK);
    CHECK(astra_texture_validate(&d, NULL, 5, tri, 1) ==
          ASTRA_RENDER_STATUS_BAD_FLAGS);
    CHECK(astra_texture_validate(&d, NULL, 16, tri, 1) ==
          ASTRA_RENDER_STATUS_BAD_FLAGS);
    CHECK(astra_texture_validate(&d, NULL,
                                 ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR,
                                 tri, 1) == ASTRA_RENDER_STATUS_BAD_FLAGS);
    CHECK(astra_texture_validate(&d, NULL, 0, tri, 0) ==
          ASTRA_RENDER_STATUS_BAD_RANGE);
    CHECK(astra_texture_validate(&i8, NULL, 0, tri, 1) ==
          ASTRA_RENDER_STATUS_UNSUPPORTED);
    tri[1].u = 1;
    CHECK(astra_texture_validate(&d, NULL, 0, tri, 1) ==
          ASTRA_RENDER_STATUS_BAD_RANGE);
    tri[1].u = 0;
    tri[2].y = ASTRA_TEXTURE_COORD_LIMIT;
    CHECK(astra_texture_validate(&d, NULL, 0, tri, 1) ==
          ASTRA_RENDER_STATUS_BAD_RANGE);
    tri[2].y = -ASTRA_TEXTURE_COORD_LIMIT;
    CHECK(astra_texture_validate(&d, NULL, 0, tri, 1) ==
          ASTRA_RENDER_STATUS_OK);
}

int main(void)
{
    test_mul255();
    test_blend_modes();
    test_linear_filter_and_clamp();
    test_shared_edge();
    test_interpolation();
    test_validation();
    puts("PASS texture_reference");
    return 0;
}
