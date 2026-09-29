// SPDX-License-Identifier: MIT
// Generates tb_astra_render_texture vectors from the bit-exact reference model
// (sw/userspace/graphics/src/texture_reference.c).
//
//   texture_vectors OUT_DIR
//
// writes OUT_DIR/texture_memory.hex (initial image, sparse $readmemh bytes),
// OUT_DIR/texture_expected.hex (final image) and OUT_DIR/texture_cases.hex
// (case count, then CASE_WORDS words per case).

#include <astra/texture_reference.h>

#include "astra_render_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MEMORY_BYTES = 1u << 20,
    TEXTURE_BASE = 0x00000u,
    VERTEX_BASE = 0x10000u,
    DESTINATION_BASE = 0x40000u,
    CASE_WORDS = 24u,
    MAX_CASES = 512u,
    TEXTURE_WIDTH = 13u, /* odd widths exercise every beat lane */
    TEXTURE_HEIGHT = 11u,
    DEST_WIDTH = 23u,
    DEST_HEIGHT = 17u,
};

static uint8_t initial[MEMORY_BYTES];
static uint8_t memory[MEMORY_BYTES];
static uint32_t cases[MAX_CASES][CASE_WORDS];
static unsigned case_count;
static uint32_t vertex_cursor = VERTEX_BASE;
static uint32_t destination_cursor = DESTINATION_BASE;
static uint64_t rng = 0x9e3779b97f4a7c15u;

static uint32_t next_random(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

static int32_t random_range(int32_t low, int32_t high)
{
    return low + (int32_t)(next_random() % (uint32_t)(high - low + 1));
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static unsigned bytes_per_pixel(uint8_t format)
{
    return format == ASTRA_RENDER_FORMAT_RGB565 ? 2u
           : format == ASTRA_RENDER_FORMAT_XRGB8888 ||
                   format == ASTRA_RENDER_FORMAT_ARGB8888
               ? 4u
               : 1u;
}

typedef struct Texture {
    AstraTextureSurface surface;
    uint32_t offset;
    uint32_t palette_offset;
} Texture;

static Texture textures[5];
static const uint8_t texture_formats[5] = {
    ASTRA_RENDER_FORMAT_RGB565, ASTRA_RENDER_FORMAT_XRGB8888,
    ASTRA_RENDER_FORMAT_ARGB8888, ASTRA_RENDER_FORMAT_INDEX8,
    ASTRA_RENDER_FORMAT_A8,
};

static void make_textures(void)
{
    uint32_t cursor = TEXTURE_BASE;

    for (unsigned t = 0; t < 5; ++t) {
        uint8_t format = texture_formats[t];
        uint32_t pitch = (TEXTURE_WIDTH * bytes_per_pixel(format) + 7u) & ~7u;
        Texture *out = &textures[t];

        out->offset = cursor;
        out->surface.data = memory + cursor;
        out->surface.pitch = pitch;
        out->surface.width = TEXTURE_WIDTH;
        out->surface.height = TEXTURE_HEIGHT;
        out->surface.format = format;
        for (uint32_t i = 0; i < pitch * TEXTURE_HEIGHT; ++i)
            memory[cursor + i] = (uint8_t)next_random();
        cursor = (cursor + pitch * TEXTURE_HEIGHT + 63u) & ~63u;
        if (format == ASTRA_RENDER_FORMAT_INDEX8) {
            out->palette_offset = cursor;
            out->surface.palette = memory + cursor;
            for (uint32_t i = 0; i < 1024u; ++i)
                memory[cursor + i] = (uint8_t)next_random();
            cursor += 1024u;
        }
    }
}

typedef struct Case {
    uint8_t destination_format;
    int texture; /* -1 untextured */
    uint32_t options;
    AstraTextureClip clip;
    AstraTextureVertex vertices[3 * 16];
    uint32_t triangle_count;
    uint32_t raw_word; /* nonzero: poke raw vertex word (index, value) */
    uint32_t raw_index;
    unsigned expected_status;
    unsigned stall;
    uint16_t width, height;
} Case;

static void emit_case(Case *c)
{
    uint32_t *w = cases[case_count];
    uint16_t width = c->width ? c->width : DEST_WIDTH;
    uint16_t height = c->height ? c->height : DEST_HEIGHT;
    uint32_t pitch = (width * bytes_per_pixel(c->destination_format) + 7u) & ~7u;
    uint32_t destination = destination_cursor;
    uint32_t vertices = vertex_cursor;
    AstraTextureSurface dst = {memory + destination, NULL, pitch, width,
                               height, c->destination_format};
    const Texture *tex = c->texture >= 0 ? &textures[c->texture] : NULL;

    if (case_count == MAX_CASES) {
        fprintf(stderr, "too many cases\n");
        exit(1);
    }
    destination_cursor = (destination + pitch * height + 63u) & ~63u;
    vertex_cursor = (vertices + c->triangle_count * 96u + 31u) & ~31u;
    if (destination_cursor > MEMORY_BYTES || vertex_cursor > DESTINATION_BASE) {
        fprintf(stderr, "vector memory exhausted\n");
        exit(1);
    }
    for (uint32_t i = 0; i < pitch * height; ++i)
        memory[destination + i] = (uint8_t)next_random();
    for (uint32_t i = 0; i < c->triangle_count * 3u; ++i) {
        uint8_t *v = memory + vertices + i * 32u;
        put32(v + 0, (uint32_t)c->vertices[i].x);
        put32(v + 4, (uint32_t)c->vertices[i].y);
        put32(v + 8, (uint32_t)c->vertices[i].u);
        put32(v + 12, (uint32_t)c->vertices[i].v);
        put32(v + 16, c->vertices[i].color);
    }
    if (c->raw_word)
        put32(memory + vertices + c->raw_index * 4u, c->raw_word);
    memcpy(initial + destination, memory + destination, pitch * height);
    memcpy(initial + vertices, memory + vertices, c->triangle_count * 96u);

    unsigned status = c->raw_word ? c->expected_status
                                  : astra_texture_validate(
                                        &dst, tex ? &tex->surface : NULL,
                                        c->options, c->vertices,
                                        c->triangle_count);
    uint32_t pixels = 0;
    if (status == ASTRA_RENDER_STATUS_OK)
        pixels = astra_texture_draw(&dst, tex ? &tex->surface : NULL,
                                    c->options, c->clip, c->vertices,
                                    c->triangle_count);
    if (c->expected_status != status) {
        fprintf(stderr, "case %u: model status %u, expected %u\n", case_count,
                status, c->expected_status);
        exit(1);
    }

    w[0] = destination;
    w[1] = pitch;
    w[2] = (uint32_t)width << 16 | height;
    w[3] = c->destination_format;
    w[4] = tex ? tex->offset : 0u;
    w[5] = tex ? tex->surface.pitch : 0u;
    w[6] = tex ? (uint32_t)tex->surface.width << 16 | tex->surface.height : 0u;
    w[7] = tex ? tex->surface.format : 0u;
    w[8] = tex ? tex->palette_offset : 0u;
    w[9] = c->options;
    w[10] = tex != NULL;
    w[11] = vertices;
    w[12] = c->triangle_count;
    w[13] = (uint32_t)(uint16_t)c->clip.left << 16 | (uint16_t)c->clip.top;
    w[14] = (uint32_t)(uint16_t)c->clip.right << 16 | (uint16_t)c->clip.bottom;
    w[15] = status;
    w[16] = pixels;
    w[17] = pitch * height;
    w[18] = c->stall;
    ++case_count;
}

static AstraTextureVertex random_vertex(int spread, int textured)
{
    AstraTextureVertex v;
    int32_t low = -spread * 256, high = (DEST_WIDTH + spread) * 256;

    v.x = random_range(low, high);
    v.y = random_range(-spread * 256, (DEST_HEIGHT + spread) * 256);
    if (next_random() & 1u) { /* land on pixel-centre grid: exercise ties */
        v.x = (v.x & ~255) | 128;
        v.y = (v.y & ~255) | 128;
    }
    v.u = textured ? random_range(-4 * 65536, (TEXTURE_WIDTH + 4) * 65536) : 0;
    v.v = textured ? random_range(-4 * 65536, (TEXTURE_HEIGHT + 4) * 65536) : 0;
    v.color = next_random() ^ next_random() << 16;
    if (next_random() % 4u == 0u)
        v.color |= 0xff000000u;
    return v;
}

static const AstraTextureClip full_clip = {0, 0, 32767, 32767};

static void random_case(uint8_t destination_format, int texture,
                        uint32_t options, uint32_t triangles, unsigned stall)
{
    Case c;

    memset(&c, 0, sizeof(c));
    c.destination_format = destination_format;
    c.texture = texture;
    c.options = options;
    c.clip = full_clip;
    if (next_random() % 3u == 0u) {
        c.clip.left = (int16_t)random_range(-3, 8);
        c.clip.top = (int16_t)random_range(-3, 6);
        c.clip.right = (int16_t)random_range(c.clip.left, DEST_WIDTH + 3);
        c.clip.bottom = (int16_t)random_range(c.clip.top, DEST_HEIGHT + 3);
    }
    c.triangle_count = triangles;
    for (uint32_t i = 0; i < triangles * 3u; ++i)
        c.vertices[i] = random_vertex(6, texture >= 0);
    /* Constant colour triangles take the constant-attribute shortcut. */
    if (next_random() % 3u == 0u)
        for (uint32_t i = 0; i < triangles * 3u; ++i)
            c.vertices[i].color = c.vertices[i - i % 3u].color;
    c.stall = stall;
    c.expected_status = ASTRA_RENDER_STATUS_OK;
    emit_case(&c);
}

static AstraTextureVertex vertex(double x, double y, double u, double v,
                                 uint32_t color)
{
    AstraTextureVertex out = {(int32_t)(x * 256.0), (int32_t)(y * 256.0),
                              (int32_t)(u * 65536.0), (int32_t)(v * 65536.0),
                              color};
    return out;
}

static void directed_cases(void)
{
    Case c;

    /* Shared diagonal through pixel centres, ADD: every pixel exactly once. */
    memset(&c, 0, sizeof(c));
    c.destination_format = ASTRA_RENDER_FORMAT_XRGB8888;
    c.texture = -1;
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD;
    c.clip = full_clip;
    c.triangle_count = 2;
    c.vertices[0] = vertex(0.5, 0.5, 0, 0, 0xff010101u);
    c.vertices[1] = vertex(20.5, 0.5, 0, 0, 0xff010101u);
    c.vertices[2] = vertex(20.5, 15.5, 0, 0, 0xff010101u);
    c.vertices[3] = vertex(0.5, 0.5, 0, 0, 0xff010101u);
    c.vertices[4] = vertex(0.5, 15.5, 0, 0, 0xff010101u);
    c.vertices[5] = vertex(20.5, 15.5, 0, 0, 0xff010101u);
    emit_case(&c);
    /* A fan of four triangles around a centre vertex on a pixel centre. */
    c.triangle_count = 4;
    c.vertices[0] = vertex(10.5, 8.5, 0, 0, 0xff010101u);
    c.vertices[1] = vertex(0.5, 0.5, 0, 0, 0xff010101u);
    c.vertices[2] = vertex(20.5, 0.5, 0, 0, 0xff010101u);
    c.vertices[3] = c.vertices[0];
    c.vertices[4] = c.vertices[2];
    c.vertices[5] = vertex(20.5, 16.5, 0, 0, 0xff010101u);
    c.vertices[6] = c.vertices[0];
    c.vertices[7] = c.vertices[5];
    c.vertices[8] = vertex(0.5, 16.5, 0, 0, 0xff010101u);
    c.vertices[9] = c.vertices[0];
    c.vertices[10] = c.vertices[8];
    c.vertices[11] = c.vertices[1];
    emit_case(&c);
    /* The same fan with BLEND: overlapping reads must see earlier writes. */
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND;
    for (unsigned i = 0; i < 12; ++i)
        c.vertices[i].color = 0x80ff4020u;
    c.stall = 1;
    emit_case(&c);
    /* Full overdraw: the same triangle three times with ADD. */
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD;
    c.triangle_count = 3;
    for (unsigned i = 0; i < 9; ++i)
        c.vertices[i] = vertex(i % 3 == 1 ? 22.0 : 1.25, i % 3 == 2 ? 16.0 : 0.75,
                               0, 0, 0xff102030u);
    emit_case(&c);
    c.stall = 0;

    /* Clip rectangle strictly inside the destination. */
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE;
    c.clip.left = 3;
    c.clip.top = 2;
    c.clip.right = 9;
    c.clip.bottom = 7;
    c.triangle_count = 2;
    c.vertices[0] = vertex(-5, -5, 0, 0, 0xff00ff00u);
    c.vertices[1] = vertex(40, -5, 0, 0, 0xff00ff00u);
    c.vertices[2] = vertex(-5, 40, 0, 0, 0xff00ff00u);
    c.vertices[3] = vertex(40, 40, 0, 0, 0xff0000ffu);
    c.vertices[4] = vertex(40, -5, 0, 0, 0xff0000ffu);
    c.vertices[5] = vertex(-5, 40, 0, 0, 0xff0000ffu);
    emit_case(&c);
    c.clip = full_clip;

    /* Zero area, collinear and coincident. */
    c.triangle_count = 2;
    c.vertices[0] = vertex(1, 1, 0, 0, 0xffffffffu);
    c.vertices[1] = vertex(10, 10, 0, 0, 0xffffffffu);
    c.vertices[2] = vertex(19, 19, 0, 0, 0xffffffffu);
    c.vertices[3] = vertex(5, 5, 0, 0, 0xffffffffu);
    c.vertices[4] = c.vertices[3];
    c.vertices[5] = c.vertices[3];
    emit_case(&c);

    /* Coordinates at the limits: huge triangles covering everything. */
    c.triangle_count = 2;
    c.vertices[0] = vertex(-32768.0, -32768.0, 0, 0, 0xff123456u);
    c.vertices[1] = vertex(32767.99, -32768.0, 0, 0, 0xff654321u);
    c.vertices[2] = vertex(-32768.0, 32767.99, 0, 0, 0x80abcdefu);
    c.vertices[3] = vertex(3.3, -32768.0, 0, 0, 0x10ffffffu);
    c.vertices[4] = vertex(32767.99, 32767.99, 0, 0, 0xfe000000u);
    c.vertices[5] = vertex(-1000.0, 32767.99, 0, 0, 0x00ff00ffu);
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND;
    emit_case(&c);
    /* ... textured, with texture coordinates at the 16.16 limits. */
    c.texture = 2;
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR |
                ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE;
    c.vertices[0].u = INT32_MIN;
    c.vertices[0].v = INT32_MAX;
    c.vertices[1].u = INT32_MAX;
    c.vertices[1].v = 3 * 65536;
    c.vertices[2].u = 5 * 65536 + 0x8000;
    c.vertices[2].v = INT32_MIN;
    c.vertices[3].u = 0;
    c.vertices[4].u = -7;
    c.vertices[5].v = 70000;
    c.destination_format = ASTRA_RENDER_FORMAT_ARGB8888;
    emit_case(&c);
    /* A sliver: long, one pixel wide, steep. */
    c.texture = 0;
    c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE;
    c.triangle_count = 1;
    c.vertices[0] = vertex(-20000.3, -30000.1, -100, 200, 0xffffffffu);
    c.vertices[1] = vertex(30000.7, 30000.9, 12.0 * 65536, 9.0 * 65536,
                           0x00000000u);
    c.vertices[2] = vertex(-19999.1, -30000.1, 5.0 * 65536, 1, 0x7f7f7f7fu);
    emit_case(&c);

    /* Axis-aligned textured quad, nearest, u = x: an exact copy. */
    for (unsigned t = 0; t < 5; ++t) {
        c.texture = (int)t;
        c.destination_format = ASTRA_RENDER_FORMAT_ARGB8888;
        c.options = ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE;
        c.triangle_count = 2;
        c.vertices[0] = vertex(2, 1, 0, 0, 0xffffffffu);
        c.vertices[1] = vertex(15, 1, 13, 0, 0xffffffffu);
        c.vertices[2] = vertex(15, 12, 13, 11, 0xffffffffu);
        c.vertices[3] = c.vertices[0];
        c.vertices[4] = vertex(2, 12, 0, 11, 0xffffffffu);
        c.vertices[5] = c.vertices[2];
        emit_case(&c);
    }

    /* A 20x14 sprite quad in four configurations: the performance probes
     * reported by tb_astra_render_texture +texture_perf (cases 14..17). */
    static const struct {
        int texture;
        uint32_t options;
        uint8_t format;
    } probes[4] = {
        {-1, ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE,
         ASTRA_RENDER_FORMAT_XRGB8888},
        {1, ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE,
         ASTRA_RENDER_FORMAT_XRGB8888},
        {1, ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR |
                ASTRA_RENDER_TRIANGLE_OPTION_BLEND_NONE,
         ASTRA_RENDER_FORMAT_XRGB8888},
        {2, ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR |
                ASTRA_RENDER_TRIANGLE_OPTION_BLEND_BLEND,
         ASTRA_RENDER_FORMAT_ARGB8888},
    };
    for (unsigned p = 0; p < 4; ++p) {
        c.texture = probes[p].texture;
        c.destination_format = probes[p].format;
        c.options = probes[p].options;
        c.triangle_count = 2;
        c.vertices[0] = vertex(1, 1, 0, 0, 0xffffffffu);
        c.vertices[1] = vertex(21, 1, 13, 0, 0xffffffffu);
        c.vertices[2] = vertex(21, 15, 13, 11, 0xffffffffu);
        c.vertices[3] = c.vertices[0];
        c.vertices[4] = vertex(1, 15, 0, 11, 0xffffffffu);
        c.vertices[5] = c.vertices[2];
        if (c.texture < 0)
            for (unsigned i = 0; i < 6; ++i)
                c.vertices[i].u = c.vertices[i].v = 0;
        emit_case(&c);
    }

    /* Rejections: the model and engine agree and nothing is written. */
    memset(&c, 0, sizeof(c));
    c.destination_format = ASTRA_RENDER_FORMAT_INDEX8;
    c.texture = -1;
    c.clip = full_clip;
    c.triangle_count = 1;
    c.vertices[0] = vertex(0, 0, 0, 0, 0xffffffffu);
    c.vertices[1] = vertex(10, 0, 0, 0, 0xffffffffu);
    c.vertices[2] = vertex(0, 10, 0, 0, 0xffffffffu);
    c.expected_status = ASTRA_RENDER_STATUS_UNSUPPORTED;
    emit_case(&c);
    c.destination_format = ASTRA_RENDER_FORMAT_RGB565;
    c.triangle_count = 3;
    c.vertices[3] = c.vertices[0];
    c.vertices[4] = c.vertices[1];
    c.vertices[5] = c.vertices[2];
    c.vertices[6] = c.vertices[0];
    c.vertices[7] = c.vertices[1];
    c.vertices[8] = vertex(0, 32768.0, 0, 0, 0xffffffffu); /* last vertex */
    c.expected_status = ASTRA_RENDER_STATUS_BAD_RANGE;
    emit_case(&c);
    c.vertices[8] = vertex(0, 10, 0, 0, 0xffffffffu);
    c.vertices[4].u = 1; /* untextured u must be zero */
    emit_case(&c);
    c.vertices[4].u = 0;
    c.raw_word = 0x00000001u; /* reserved vertex word 6 of vertex 7 */
    c.raw_index = 7u * 8u + 6u;
    emit_case(&c);
    c.raw_word = 0x80000000u; /* reserved word 5 of vertex 0 */
    c.raw_index = 5u;
    emit_case(&c);
}

static void write_image(const char *dir, const char *name, const uint8_t *image)
{
    char path[512];
    FILE *file;
    uint32_t previous = UINT32_MAX;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    file = fopen(path, "w");
    if (!file) {
        perror(path);
        exit(1);
    }
    for (uint32_t i = 0; i < MEMORY_BYTES; ++i) {
        if (!image[i])
            continue;
        if (previous + 1u != i)
            fprintf(file, "@%x\n", i);
        fprintf(file, "%02x\n", image[i]);
        previous = i;
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    char path[512];
    FILE *file;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT_DIR\n", argv[0]);
        return 2;
    }
    make_textures();
    memcpy(initial, memory, VERTEX_BASE);
    directed_cases();

    static const uint8_t destinations[3] = {ASTRA_RENDER_FORMAT_RGB565,
                                            ASTRA_RENDER_FORMAT_XRGB8888,
                                            ASTRA_RENDER_FORMAT_ARGB8888};
    /* Every blend x destination x filter x source format, plus untextured. */
    for (unsigned d = 0; d < 3; ++d)
        for (unsigned blend = 0; blend <= 4; ++blend) {
            random_case(destinations[d], -1, blend, 2, 0);
            for (unsigned t = 0; t < 5; ++t)
                for (unsigned linear = 0; linear < 2; ++linear)
                    random_case(destinations[d], (int)t,
                                blend | (linear
                                             ? ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR
                                             : 0u),
                                1u + next_random() % 3u, next_random() & 1u);
        }
    /* Many overlapping triangles in one command, blending, with stalls. */
    for (unsigned i = 0; i < 6; ++i)
        random_case(destinations[i % 3], (int)(i % 6) - 1,
                    (1u + i % 4u) | (i & 1u ? 8u : 0u), 16, 1);

    write_image(argv[1], "texture_memory.hex", initial);
    write_image(argv[1], "texture_expected.hex", memory);
    snprintf(path, sizeof(path), "%s/texture_cases.hex", argv[1]);
    file = fopen(path, "w");
    if (!file) {
        perror(path);
        return 1;
    }
    fprintf(file, "%08x\n", case_count);
    for (unsigned i = 0; i < MAX_CASES; ++i) /* fills the tb's array */
        for (unsigned j = 0; j < CASE_WORDS; ++j)
            fprintf(file, "%08x\n", cases[i][j]);
    fclose(file);
    printf("texture_vectors: %u cases\n", case_count);
    return 0;
}
