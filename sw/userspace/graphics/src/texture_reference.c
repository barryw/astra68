// SPDX-License-Identifier: MIT
// Bit-exact reference model of the Astraea texture engine. Every rule here is
// written the obvious way; the RTL computes the same values incrementally.

#include <astra/endian.h>
#include <astra/texture_reference.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "astra_render_protocol.h"
#pragma GCC diagnostic pop

#include <stddef.h>

/* Interpolation numerators reach 2^84, so they are carried as portable
 * two's-complement 128-bit values (32-bit ARM has no __int128). */
typedef struct Wide {
    uint64_t high;
    uint64_t low;
} Wide;

static Wide wide(int64_t value)
{
    Wide out = {value < 0 ? UINT64_MAX : 0u, (uint64_t)value};
    return out;
}

static Wide wide_add(Wide a, Wide b)
{
    Wide sum = {a.high + b.high, a.low + b.low};

    sum.high += sum.low < a.low;
    return sum;
}

static Wide wide_negate(Wide a)
{
    Wide inverted = {~a.high, ~a.low};
    return wide_add(inverted, wide(1));
}

static Wide wide_multiply(int64_t a, int64_t b)
{
    uint64_t x = a < 0 ? 0u - (uint64_t)a : (uint64_t)a;
    uint64_t y = b < 0 ? 0u - (uint64_t)b : (uint64_t)b;
    uint64_t x0 = x & 0xffffffffu, x1 = x >> 32;
    uint64_t y0 = y & 0xffffffffu, y1 = y >> 32;
    uint64_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
    uint64_t middle = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    Wide product = {p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32),
                    middle << 32 | (p00 & 0xffffffffu)};

    return (a < 0) != (b < 0) ? wide_negate(product) : product;
}

/* floor(n / d) for 0 < d < 2^62 and a quotient that fits in int64_t. */
static int64_t wide_floor_divide(Wide n, uint64_t d)
{
    int negative = (int)(n.high >> 63);
    Wide magnitude = negative ? wide_negate(n) : n;
    uint64_t quotient = 0, remainder = 0;

    for (int bit = 127; bit >= 0; --bit) {
        uint64_t word = bit >= 64 ? magnitude.high : magnitude.low;

        remainder = remainder << 1 | (word >> (bit & 63) & 1u);
        quotient <<= 1;
        if (remainder >= d) {
            remainder -= d;
            quotient |= 1u;
        }
    }
    if (!negative)
        return (int64_t)quotient;
    return remainder ? -(int64_t)quotient - 1 : -(int64_t)quotient;
}

uint8_t astra_texture_mul255(uint8_t a, uint8_t b)
{
    return (uint8_t)(((unsigned)a * b + 127u) / 255u);
}

static uint8_t channel(uint32_t argb, unsigned shift)
{
    return (uint8_t)(argb >> shift);
}

static uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b)
{
    return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static unsigned expand5(unsigned c) { return c << 3 | c >> 2; }
static unsigned expand6(unsigned c) { return c << 2 | c >> 4; }

static uint32_t rgb565_argb(unsigned p)
{
    return argb(255u, expand5(p >> 11 & 31u), expand6(p >> 5 & 63u),
                expand5(p & 31u));
}

static uint32_t texel(const AstraTextureSurface *s, int32_t x, int32_t y)
{
    const uint8_t *row = s->data + (uint32_t)y * s->pitch;

    switch (s->format) {
    case ASTRA_RENDER_FORMAT_RGB565:
        return rgb565_argb((unsigned)row[x * 2] << 8 | row[x * 2 + 1]);
    case ASTRA_RENDER_FORMAT_XRGB8888:
        return astra_load_be32(row + x * 4) | UINT32_C(0xff000000);
    case ASTRA_RENDER_FORMAT_ARGB8888:
        return astra_load_be32(row + x * 4);
    case ASTRA_RENDER_FORMAT_INDEX8:
        return astra_load_be32(s->palette + row[x] * 4u);
    default: /* A8 */
        return argb(row[x], 255u, 255u, 255u);
    }
}

static int32_t clamp(int64_t value, uint16_t extent)
{
    return value < 0 ? 0 : value >= extent ? extent - 1 : (int32_t)value;
}

uint32_t astra_texture_sample(const AstraTextureSurface *s, int32_t u,
                              int32_t v, int linear)
{
    if (!linear)
        return texel(s, clamp((int64_t)u >> 16, s->width),
                     clamp((int64_t)v >> 16, s->height));

    int64_t us = (int64_t)u - 0x8000, vs = (int64_t)v - 0x8000;
    int64_t x0 = us >> 16, y0 = vs >> 16;
    unsigned fx = (unsigned)(us >> 8) & 255u, fy = (unsigned)(vs >> 8) & 255u;
    uint32_t a = texel(s, clamp(x0, s->width), clamp(y0, s->height));
    uint32_t b = texel(s, clamp(x0 + 1, s->width), clamp(y0, s->height));
    uint32_t c = texel(s, clamp(x0, s->width), clamp(y0 + 1, s->height));
    uint32_t d = texel(s, clamp(x0 + 1, s->width), clamp(y0 + 1, s->height));
    uint32_t out = 0;

    for (unsigned shift = 0; shift < 32; shift += 8) {
        uint32_t top = channel(a, shift) * (256u - fx) + channel(b, shift) * fx;
        uint32_t bottom =
            channel(c, shift) * (256u - fx) + channel(d, shift) * fx;
        out |= ((top * (256u - fy) + bottom * fy + 32768u) >> 16) << shift;
    }
    return out;
}

static unsigned saturate(unsigned value)
{
    return value > 255u ? 255u : value;
}

uint32_t astra_texture_blend(unsigned mode, uint32_t s, uint32_t d)
{
    uint8_t sa = channel(s, 24), da = channel(d, 24), out[3];

    if (mode == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE)
        return s;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned shift = 16u - 8u * i;
        uint8_t sc = channel(s, shift), dc = channel(d, shift);

        switch (mode) {
        case ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND:
            out[i] = (uint8_t)saturate(astra_texture_mul255(sc, sa) +
                                       astra_texture_mul255(dc, 255u - sa));
            break;
        case ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD:
            out[i] = (uint8_t)saturate(astra_texture_mul255(sc, sa) + dc);
            break;
        case ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MOD:
            out[i] = astra_texture_mul255(sc, dc);
            break;
        default: /* MUL */
            out[i] = (uint8_t)saturate(astra_texture_mul255(sc, dc) +
                                       astra_texture_mul255(dc, 255u - sa));
            break;
        }
    }
    if (mode == ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND)
        da = (uint8_t)saturate(sa + astra_texture_mul255(da, 255u - sa));
    return argb(da, out[0], out[1], out[2]);
}

uint32_t astra_texture_pack(uint8_t format, uint32_t c)
{
    switch (format) {
    case ASTRA_RENDER_FORMAT_RGB565:
        return (channel(c, 16) * 31u + 127u) / 255u << 11 |
               (channel(c, 8) * 63u + 127u) / 255u << 5 |
               (channel(c, 0) * 31u + 127u) / 255u;
    case ASTRA_RENDER_FORMAT_XRGB8888:
        return c | UINT32_C(0xff000000);
    default:
        return c;
    }
}

static uint32_t load_destination(const AstraTextureSurface *d, int32_t x,
                                 int32_t y)
{
    return d->format == ASTRA_RENDER_FORMAT_ARGB8888
               ? texel(d, x, y)
               : texel(d, x, y) | UINT32_C(0xff000000);
}

static void store_destination(const AstraTextureSurface *d, int32_t x,
                              int32_t y, uint32_t value)
{
    uint8_t *row = d->data + (uint32_t)y * d->pitch;

    if (d->format == ASTRA_RENDER_FORMAT_RGB565) {
        row[x * 2] = (uint8_t)(value >> 8);
        row[x * 2 + 1] = (uint8_t)value;
        return;
    }
    for (unsigned i = 0; i < 4; ++i)
        row[x * 4 + i] = (uint8_t)(value >> (24u - 8u * i));
}

static int direct_color(uint8_t format)
{
    return format == ASTRA_RENDER_FORMAT_RGB565 ||
           format == ASTRA_RENDER_FORMAT_XRGB8888 ||
           format == ASTRA_RENDER_FORMAT_ARGB8888;
}

unsigned astra_texture_validate(const AstraTextureSurface *destination,
                                const AstraTextureSurface *source,
                                uint32_t options,
                                const AstraTextureVertex *vertices,
                                uint32_t triangle_count)
{
    if ((options & ~(uint32_t)ASTRA_RENDER_TRIANGLE_OPTION_ALLOWED_MASK) ||
        (options & ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MASK) >
            ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MUL ||
        (!source && (options & ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR)))
        return ASTRA_RENDER_STATUS_BAD_FLAGS;
    if (triangle_count == 0u || triangle_count > ASTRA_RENDER_MAX_TRIANGLES)
        return ASTRA_RENDER_STATUS_BAD_RANGE;
    if (!direct_color(destination->format) ||
        (source && !direct_color(source->format) &&
         source->format != ASTRA_RENDER_FORMAT_INDEX8 &&
         source->format != ASTRA_RENDER_FORMAT_A8))
        return ASTRA_RENDER_STATUS_UNSUPPORTED;
    for (uint32_t i = 0; i < triangle_count * 3u; ++i) {
        const AstraTextureVertex *v = &vertices[i];

        if (v->x < -ASTRA_TEXTURE_COORD_LIMIT ||
            v->x >= ASTRA_TEXTURE_COORD_LIMIT ||
            v->y < -ASTRA_TEXTURE_COORD_LIMIT ||
            v->y >= ASTRA_TEXTURE_COORD_LIMIT ||
            (!source && (v->u != 0 || v->v != 0)))
            return ASTRA_RENDER_STATUS_BAD_RANGE;
    }
    return ASTRA_RENDER_STATUS_OK;
}

static int64_t edge(const AstraTextureVertex *a, const AstraTextureVertex *b,
                    int64_t px, int64_t py)
{
    return ((int64_t)b->x - a->x) * (py - a->y) -
           ((int64_t)b->y - a->y) * (px - a->x);
}

/* A top edge is horizontal with the interior below; a left edge has the
 * interior to its right. Interior is where edge() is positive. */
static int top_left(const AstraTextureVertex *a, const AstraTextureVertex *b)
{
    int64_t dx = (int64_t)b->x - a->x, dy = (int64_t)b->y - a->y;

    return dy < 0 || (dy == 0 && dx > 0);
}

static int covered(int64_t w, int top_left_edge)
{
    return w > 0 || (w == 0 && top_left_edge);
}

/* Barycentric value rounded to nearest, ties up:
 * floor((w0*a0 + w1*a1 + w2*a2) / area + 1/2), area > 0. */
static int64_t interpolate(const int64_t w[3], int64_t area, int64_t a0,
                           int64_t a1, int64_t a2)
{
    Wide n = wide_add(wide_add(wide_multiply(w[0], a0),
                               wide_multiply(w[1], a1)),
                      wide_multiply(w[2], a2));

    return wide_floor_divide(wide_add(wide_add(n, n), wide(area)),
                             2u * (uint64_t)area);
}

static uint32_t interpolate_color(const int64_t w[3], int64_t area,
                                  const AstraTextureVertex *v[3])
{
    uint32_t out = 0;

    for (unsigned shift = 0; shift < 32; shift += 8)
        out |= (uint32_t)interpolate(w, area, channel(v[0]->color, shift),
                                     channel(v[1]->color, shift),
                                     channel(v[2]->color, shift))
               << shift;
    return out;
}

static uint32_t modulate(uint32_t t, uint32_t c)
{
    return argb(astra_texture_mul255(channel(t, 24), channel(c, 24)),
                astra_texture_mul255(channel(t, 16), channel(c, 16)),
                astra_texture_mul255(channel(t, 8), channel(c, 8)),
                astra_texture_mul255(channel(t, 0), channel(c, 0)));
}

static int32_t floor_div256(int64_t value)
{
    return (int32_t)(value >> 8);
}

static uint32_t draw_triangle(const AstraTextureSurface *dst,
                              const AstraTextureSurface *src,
                              uint32_t options, AstraTextureClip clip,
                              const AstraTextureVertex *tri)
{
    const AstraTextureVertex *v[3] = {&tri[0], &tri[1], &tri[2]};
    int64_t area = edge(v[0], v[1], v[2]->x, v[2]->y);
    unsigned mode = options & ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MASK;
    int linear = (options & ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR) != 0;
    uint32_t written = 0;

    if (area == 0)
        return 0;
    if (area < 0) { /* winding is irrelevant: make the interior positive */
        const AstraTextureVertex *swap = v[1];
        v[1] = v[2];
        v[2] = swap;
        area = -area;
    }

    int64_t min_x = v[0]->x, max_x = v[0]->x, min_y = v[0]->y,
            max_y = v[0]->y;
    for (unsigned i = 1; i < 3; ++i) {
        min_x = v[i]->x < min_x ? v[i]->x : min_x;
        max_x = v[i]->x > max_x ? v[i]->x : max_x;
        min_y = v[i]->y < min_y ? v[i]->y : min_y;
        max_y = v[i]->y > max_y ? v[i]->y : max_y;
    }
    /* Pixels whose centres (p * 256 + 128) lie inside the bounding box. */
    int32_t left = floor_div256(min_x + 127), right = floor_div256(max_x - 128);
    int32_t top = floor_div256(min_y + 127), bottom = floor_div256(max_y - 128);
    int32_t clip_left = clip.left < 0 ? 0 : clip.left;
    int32_t clip_top = clip.top < 0 ? 0 : clip.top;
    int32_t clip_right = clip.right < dst->width ? clip.right : dst->width;
    int32_t clip_bottom = clip.bottom < dst->height ? clip.bottom : dst->height;

    left = left > clip_left ? left : clip_left;
    top = top > clip_top ? top : clip_top;
    right = right < clip_right - 1 ? right : clip_right - 1;
    bottom = bottom < clip_bottom - 1 ? bottom : clip_bottom - 1;

    int tl[3] = {top_left(v[1], v[2]), top_left(v[2], v[0]),
                 top_left(v[0], v[1])};

    for (int32_t py = top; py <= bottom; ++py) {
        for (int32_t px = left; px <= right; ++px) {
            int64_t cx = (int64_t)px * 256 + 128, cy = (int64_t)py * 256 + 128;
            int64_t w[3] = {edge(v[1], v[2], cx, cy), edge(v[2], v[0], cx, cy),
                            edge(v[0], v[1], cx, cy)};

            if (!covered(w[0], tl[0]) || !covered(w[1], tl[1]) ||
                !covered(w[2], tl[2]))
                continue;

            uint32_t color = interpolate_color(w, area, v);
            uint32_t s = color;

            if (src) {
                int32_t u = (int32_t)interpolate(w, area, v[0]->u, v[1]->u,
                                                 v[2]->u);
                int32_t t = (int32_t)interpolate(w, area, v[0]->v, v[1]->v,
                                                 v[2]->v);
                s = modulate(astra_texture_sample(src, u, t, linear), color);
            }
            uint32_t out = astra_texture_blend(mode, s,
                                               load_destination(dst, px, py));
            store_destination(dst, px, py, astra_texture_pack(dst->format, out));
            ++written;
        }
    }
    return written;
}

uint32_t astra_texture_draw(const AstraTextureSurface *destination,
                            const AstraTextureSurface *source,
                            uint32_t options, AstraTextureClip clip,
                            const AstraTextureVertex *vertices,
                            uint32_t triangle_count)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < triangle_count; ++i)
        written += draw_triangle(destination, source, options, clip,
                                 &vertices[i * 3u]);
    return written;
}
