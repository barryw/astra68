#include "astra_window_scene.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/window_scene.h>
#include <astra/rounded.h>

enum {
    WIDTH = 8,
    HEIGHT = 4,
    ARENA_BYTES = 0x100000,
    OUTPUT_OFFSET = 0x80000,
};

static uint32_t load_be32(const uint8_t *at)
{
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | at[3];
}

static void store_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

static void make_header(uint8_t *request, uint32_t layers,
                        uint32_t output_capacity)
{
    memset(request, 0, 512u);
    store_be32(request + 0u, ASTRA_WINDOW_SCENE_MAGIC);
    store_be32(request + 4u, ASTRA_WINDOW_SCENE_VERSION);
    store_be32(request + 8u, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                               layers * ASTRA_WINDOW_SCENE_LAYER_BYTES);
    store_be32(request + 12u, 7u);
    store_be32(request + 16u, WIDTH << 16 | HEIGHT);
    store_be32(request + 20u, layers);
    store_be32(request + 24u, ASTRA_WINDOW_SCENE_HEADER_BYTES);
    store_be32(request + 28u, OUTPUT_OFFSET);
    store_be32(request + 32u, output_capacity);
    store_be32(request + 36u, 0x1234u);
}

static void make_layer(uint8_t *request, uint32_t index,
                       uint32_t source_offset, int16_t x, int16_t y,
                       uint16_t width, uint16_t height, uint16_t radius)
{
    uint8_t *layer = request + ASTRA_WINDOW_SCENE_HEADER_BYTES +
                     index * ASTRA_WINDOW_SCENE_LAYER_BYTES;
    uint32_t pitch = ((uint32_t)width * 2u + 63u) & ~63u;

    store_be32(layer + 0u, source_offset);
    store_be32(layer + 4u, pitch * height);
    store_be32(layer + 8u, pitch);
    store_be32(layer + 12u, (uint32_t)width << 16 | height);
    store_be32(layer + 16u, (uint32_t)(uint16_t)x << 16 | (uint16_t)y);
    store_be32(layer + 20u, ASTRA_WINDOW_SCENE_LAYER_VISIBLE | radius);
}

static const uint8_t *line_at(const uint8_t *output, uint32_t y)
{
    return output + load_be32(output + 24u) +
           y * ASTRA_WINDOW_SCENE_LINE_BYTES;
}

static const uint8_t *span_at(const uint8_t *output, uint32_t index)
{
    return output + load_be32(output + 28u) +
           index * ASTRA_WINDOW_SCENE_SPAN_BYTES;
}

static void expect_span(const uint8_t *output, uint32_t index,
                        uint16_t x, uint16_t length, uint32_t source,
                        uint16_t source_x, uint16_t source_y, uint32_t flags)
{
    const uint8_t *span = span_at(output, index);

    assert(load_be32(span + 0u) == source);
    assert(load_be32(span + 12u) ==
           ((uint32_t)source_x << 16 | source_y));
    assert(load_be32(span + 16u) == ((uint32_t)x << 16 | length));
    assert(load_be32(span + 20u) == flags);
}

static uint32_t random_state = 1u;

static uint32_t next_random(void)
{
    random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return random_state;
}

static void test_random_scenes(void)
{
    enum {
        RANDOM_WIDTH = 64,
        RANDOM_HEIGHT = 32,
        RANDOM_LAYERS = 20,
        RANDOM_OUTPUT_OFFSET = 0x80000,
        RANDOM_OUTPUT_BYTES = 0x20000,
    };
    static uint8_t request[4096];
    static uint8_t output[RANDOM_OUTPUT_BYTES];
    int16_t x[RANDOM_LAYERS];
    int16_t y[RANDOM_LAYERS];
    uint16_t width[RANDOM_LAYERS];
    uint16_t height[RANDOM_LAYERS];
    uint16_t radius[RANDOM_LAYERS];
    uint8_t visible[RANDOM_LAYERS];

    for (uint32_t iteration = 0u; iteration < 100u; ++iteration) {
        uint32_t written;

        memset(request, 0, sizeof(request));
        store_be32(request + 0u, ASTRA_WINDOW_SCENE_MAGIC);
        store_be32(request + 4u, ASTRA_WINDOW_SCENE_VERSION);
        store_be32(request + 8u, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                                   RANDOM_LAYERS *
                                       ASTRA_WINDOW_SCENE_LAYER_BYTES);
        store_be32(request + 12u, iteration + 1u);
        store_be32(request + 16u,
                   RANDOM_WIDTH << 16 | RANDOM_HEIGHT);
        store_be32(request + 20u, RANDOM_LAYERS);
        store_be32(request + 24u, ASTRA_WINDOW_SCENE_HEADER_BYTES);
        store_be32(request + 28u, RANDOM_OUTPUT_OFFSET);
        store_be32(request + 32u, RANDOM_OUTPUT_BYTES);
        store_be32(request + 36u, 0x55aau);
        for (uint32_t layer = 0u; layer < RANDOM_LAYERS; ++layer) {
            uint8_t *record = request + ASTRA_WINDOW_SCENE_HEADER_BYTES +
                              layer * ASTRA_WINDOW_SCENE_LAYER_BYTES;
            uint32_t source_offset = layer * 0x1000u;
            uint32_t pitch;

            width[layer] = (uint16_t)(1u + next_random() % 32u);
            height[layer] = (uint16_t)(1u + next_random() % 16u);
            x[layer] = (int16_t)((int32_t)(next_random() % 96u) - 16);
            y[layer] = (int16_t)((int32_t)(next_random() % 48u) - 8);
            radius[layer] = (uint16_t)(next_random() %
                ((width[layer] < height[layer] ? width[layer] :
                                                    height[layer]) /
                     2u +
                 1u));
            visible[layer] = (next_random() & 3u) != 0u;
            pitch = ((uint32_t)width[layer] * 2u + 63u) & ~63u;
            store_be32(record + 0u, source_offset);
            store_be32(record + 4u, pitch * height[layer]);
            store_be32(record + 8u, pitch);
            store_be32(record + 12u,
                       (uint32_t)width[layer] << 16 | height[layer]);
            store_be32(record + 16u,
                       (uint32_t)(uint16_t)x[layer] << 16 |
                           (uint16_t)y[layer]);
            store_be32(record + 20u, radius[layer] |
                (visible[layer] ? ASTRA_WINDOW_SCENE_LAYER_VISIBLE : 0u));
        }
        assert(astra_window_scene_compile(
                   request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                                RANDOM_LAYERS *
                                    ASTRA_WINDOW_SCENE_LAYER_BYTES,
                   output, sizeof(output), ARENA_BYTES, &written) ==
               ASTRA_WINDOW_SCENE_OK);
        assert(written == load_be32(output + 8u));
        for (uint32_t row = 0u; row < RANDOM_HEIGHT; ++row) {
            const uint8_t *line = line_at(output, row);
            uint32_t first = load_be32(line + 0u);
            uint32_t count = load_be32(line + 4u);
            uint32_t cursor = 0u;

            assert(count != 0u);
            for (uint32_t at = 0u; at < count; ++at) {
                const uint8_t *span = span_at(output, first + at);
                uint32_t packed = load_be32(span + 16u);
                uint32_t start = packed >> 16;
                uint32_t length = packed & 0xffffu;

                assert(start == cursor && length != 0u &&
                       start + length <= RANDOM_WIDTH);
                for (uint32_t column = start;
                     column < start + length; ++column) {
                    int32_t owner = -1;

                    for (uint32_t layer = 0u; layer < RANDOM_LAYERS;
                         ++layer) {
                        int32_t source_y = (int32_t)row - y[layer];
                        uint32_t inset;

                        if (!visible[layer] || source_y < 0 ||
                            source_y >= height[layer])
                            continue;
                        inset = astra_graphics_rounded_inset(
                            (uint32_t)source_y, height[layer], radius[layer]);
                        if ((int32_t)column >= x[layer] + (int32_t)inset &&
                            (int32_t)column < x[layer] + width[layer] -
                                                  (int32_t)inset)
                            owner = (int32_t)layer;
                    }
                    if (owner < 0) {
                        assert(load_be32(span + 20u) ==
                               ASTRA_WINDOW_SCENE_SPAN_SOLID);
                        assert(load_be32(span + 24u) == 0x55aau);
                    } else {
                        uint32_t source_xy = load_be32(span + 12u);

                        assert(load_be32(span + 20u) == 0u);
                        assert(load_be32(span + 0u) ==
                               (uint32_t)owner * 0x1000u);
                        assert((source_xy >> 16) + column - start ==
                               (uint32_t)((int32_t)column - x[owner]));
                        assert((source_xy & 0xffffu) ==
                               (uint32_t)((int32_t)row - y[owner]));
                    }
                }
                cursor += length;
            }
            assert(cursor == RANDOM_WIDTH);
        }
    }
}

int main(void)
{
    uint8_t request[512];
    uint8_t output[4096];
    uint32_t written = 0u;
    int status;

    make_header(request, 0u, sizeof(output));
    status = astra_window_scene_compile(
        request, ASTRA_WINDOW_SCENE_HEADER_BYTES, output, sizeof(output),
        ARENA_BYTES, &written);
    assert(status == ASTRA_WINDOW_SCENE_OK && written != 0u);
    assert(load_be32(output + 0u) == ASTRA_WINDOW_SCENE_COMPILED_MAGIC);
    assert(load_be32(output + 12u) == 7u);
    assert(load_be32(output + 20u) == HEIGHT);
    assert(load_be32(output + 32u) == HEIGHT);
    for (uint32_t y = 0u; y < HEIGHT; ++y) {
        assert(load_be32(line_at(output, y) + 0u) == y);
        assert(load_be32(line_at(output, y) + 4u) == 1u);
        expect_span(output, y, 0u, WIDTH, 0u, 0u, 0u,
                    ASTRA_WINDOW_SCENE_SPAN_SOLID);
        assert(load_be32(span_at(output, y) + 24u) == 0x1234u);
    }

    make_header(request, 2u, sizeof(output));
    make_layer(request, 0u, 0x10000u, 1, 1, 6u, 2u, 0u);
    make_layer(request, 1u, 0x20000u, 3, 1, 2u, 2u, 0u);
    status = astra_window_scene_compile(
        request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                     2u * ASTRA_WINDOW_SCENE_LAYER_BYTES,
        output, sizeof(output), ARENA_BYTES, &written);
    assert(status == ASTRA_WINDOW_SCENE_OK);
    assert(load_be32(line_at(output, 0u) + 4u) == 1u);
    assert(load_be32(line_at(output, 1u) + 4u) == 5u);
    {
        uint32_t first = load_be32(line_at(output, 1u));

        expect_span(output, first + 0u, 0u, 1u, 0u, 0u, 0u,
                    ASTRA_WINDOW_SCENE_SPAN_SOLID);
        expect_span(output, first + 1u, 1u, 2u, 0x10000u, 0u, 0u, 0u);
        expect_span(output, first + 2u, 3u, 2u, 0x20000u, 0u, 0u, 0u);
        expect_span(output, first + 3u, 5u, 2u, 0x10000u, 4u, 0u, 0u);
        expect_span(output, first + 4u, 7u, 1u, 0u, 0u, 0u,
                    ASTRA_WINDOW_SCENE_SPAN_SOLID);
    }

    make_header(request, 1u, sizeof(output));
    make_layer(request, 0u, 0x10000u, 1, 0, 6u, 4u, 2u);
    status = astra_window_scene_compile(
        request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                     ASTRA_WINDOW_SCENE_LAYER_BYTES,
        output, sizeof(output), ARENA_BYTES, &written);
    assert(status == ASTRA_WINDOW_SCENE_OK);
    assert(load_be32(line_at(output, 0u) + 4u) == 3u);
    {
        uint32_t first = load_be32(line_at(output, 0u));

        expect_span(output, first + 1u, 2u, 4u, 0x10000u, 1u, 0u, 0u);
    }

    make_header(request, 1u, 64u);
    make_layer(request, 0u, 0x10000u, 1, 0, 6u, 4u, 0u);
    assert(astra_window_scene_compile(
               request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                            ASTRA_WINDOW_SCENE_LAYER_BYTES,
               output, sizeof(output), ARENA_BYTES, &written) ==
           ASTRA_WINDOW_SCENE_NO_SPACE);

    make_header(request, 1u, sizeof(output));
    make_layer(request, 0u, OUTPUT_OFFSET, 1, 0, 6u, 4u, 0u);
    assert(astra_window_scene_compile(
               request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                            ASTRA_WINDOW_SCENE_LAYER_BYTES,
               output, sizeof(output), ARENA_BYTES, &written) ==
           ASTRA_WINDOW_SCENE_BAD_LAYER);

    make_header(request, 1u, sizeof(output));
    make_layer(request, 0u, 0u, 1, 0, 6u, 4u, 0u);
    assert(astra_window_scene_compile(
               request, ASTRA_WINDOW_SCENE_HEADER_BYTES +
                            ASTRA_WINDOW_SCENE_LAYER_BYTES,
               output, sizeof(output), ARENA_BYTES, &written) ==
           ASTRA_WINDOW_SCENE_OK);

    test_random_scenes();

    puts("window scene compiler tests: PASS");
    return 0;
}
