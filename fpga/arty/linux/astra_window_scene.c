#include "astra_window_scene.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <astra/display.h>
#include <astra/rounded.h>
#include <astra/window_scene.h>

struct row_span {
    uint32_t source_offset;
    uint32_t source_bytes;
    uint32_t source_pitch;
    uint16_t source_x;
    uint16_t source_y;
    uint16_t start;
    uint16_t end;
    uint32_t flags;
    uint16_t value;
};

struct scene_layer {
    uint32_t source_offset;
    uint32_t source_bytes;
    uint32_t source_pitch;
    uint16_t width;
    uint16_t height;
    int16_t x;
    int16_t y;
    uint16_t radius;
    uint8_t visible;
};

static uint32_t load_be32(const uint8_t *at)
{
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | at[3];
}

static void store_be32(volatile uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

static uint32_t align64(uint32_t value)
{
    return (value + 63u) & ~UINT32_C(63);
}

static int ranges_overlap(uint32_t left, uint32_t left_bytes,
                          uint32_t right, uint32_t right_bytes)
{
    return left < (uint64_t)right + right_bytes &&
           right < (uint64_t)left + left_bytes;
}

static int same_run(const struct row_span *left,
                    const struct row_span *right)
{
    if (left->end != right->start || left->flags != right->flags ||
        left->value != right->value)
        return 0;
    if ((left->flags & ASTRA_WINDOW_SCENE_SPAN_SOLID) != 0u)
        return 1;
    return left->source_offset == right->source_offset &&
           left->source_bytes == right->source_bytes &&
           left->source_pitch == right->source_pitch &&
           left->source_y == right->source_y &&
           left->source_x + (left->end - left->start) == right->source_x;
}

static int append_span(struct row_span *spans, uint32_t *count,
                       uint32_t capacity, struct row_span span)
{
    if (span.start >= span.end)
        return 1;
    if (*count != 0u && same_run(&spans[*count - 1u], &span)) {
        spans[*count - 1u].end = span.end;
        return 1;
    }
    if (*count == capacity)
        return 0;
    spans[(*count)++] = span;
    return 1;
}

static struct row_span slice_span(const struct row_span *span,
                                  uint16_t start, uint16_t end)
{
    struct row_span sliced = *span;

    if ((span->flags & ASTRA_WINDOW_SCENE_SPAN_SOLID) == 0u)
        sliced.source_x = (uint16_t)(span->source_x + start - span->start);
    sliced.start = start;
    sliced.end = end;
    return sliced;
}

static int overlay(struct row_span *destination, uint32_t *destination_count,
                   const struct row_span *source, uint32_t source_count,
                   struct row_span layer, uint32_t capacity)
{
    uint32_t count = 0u;
    int inserted = 0;

    for (uint32_t index = 0u; index < source_count; ++index) {
        const struct row_span *current = &source[index];

        if (current->end <= layer.start || current->start >= layer.end) {
            if (!append_span(destination, &count, capacity, *current))
                return 0;
            continue;
        }
        if (current->start < layer.start &&
            !append_span(destination, &count, capacity,
                         slice_span(current, current->start, layer.start)))
            return 0;
        if (!inserted) {
            if (!append_span(destination, &count, capacity, layer))
                return 0;
            inserted = 1;
        }
        if (current->end > layer.end &&
            !append_span(destination, &count, capacity,
                         slice_span(current, layer.end, current->end)))
            return 0;
    }
    *destination_count = count;
    return inserted;
}

static int decode_layer(const uint8_t *record, uint32_t arena_bytes,
                        uint32_t output_offset, uint32_t output_capacity,
                        struct scene_layer *layer)
{
    uint32_t size = load_be32(record + 12u);
    uint32_t position = load_be32(record + 16u);
    uint32_t radius_flags = load_be32(record + 20u);
    uint32_t minimum_pitch;
    uint64_t required;

    layer->source_offset = load_be32(record + 0u);
    layer->source_bytes = load_be32(record + 4u);
    layer->source_pitch = load_be32(record + 8u);
    layer->width = (uint16_t)(size >> 16);
    layer->height = (uint16_t)size;
    layer->x = (int16_t)(position >> 16);
    layer->y = (int16_t)position;
    layer->radius = (uint16_t)(radius_flags &
                               ASTRA_WINDOW_SCENE_LAYER_RADIUS_MASK);
    layer->visible = (radius_flags & ASTRA_WINDOW_SCENE_LAYER_VISIBLE) != 0u;
    minimum_pitch = (uint32_t)layer->width * 2u;
    required = (uint64_t)layer->source_pitch * layer->height;

    if ((radius_flags & ~(ASTRA_WINDOW_SCENE_LAYER_VISIBLE |
                          ASTRA_WINDOW_SCENE_LAYER_RADIUS_MASK)) != 0u ||
        load_be32(record + 24u) != 0u || load_be32(record + 28u) != 0u ||
        (layer->source_offset & 63u) != 0u ||
        layer->source_bytes == 0u || layer->source_pitch < minimum_pitch ||
        layer->width == 0u ||
        layer->height == 0u || layer->width > ASTRA_DISPLAY_WIDTH ||
        layer->height > ASTRA_DISPLAY_HEIGHT ||
        layer->radius > layer->width / 2u ||
        layer->radius > layer->height / 2u ||
        required > layer->source_bytes ||
        (uint64_t)layer->source_offset + layer->source_bytes > arena_bytes ||
        ranges_overlap(layer->source_offset, layer->source_bytes,
                       output_offset, output_capacity))
        return 0;
    return 1;
}

static int write_span(volatile uint8_t *output, uint32_t output_capacity,
                      uint32_t span_offset, uint32_t index,
                      const struct row_span *span)
{
    uint64_t offset = (uint64_t)span_offset +
                      (uint64_t)index * ASTRA_WINDOW_SCENE_SPAN_BYTES;
    volatile uint8_t *record;

    if (offset + ASTRA_WINDOW_SCENE_SPAN_BYTES > output_capacity)
        return 0;
    record = output + (uint32_t)offset;
    store_be32(record + 0u, span->source_offset);
    store_be32(record + 4u, span->source_bytes);
    store_be32(record + 8u, span->source_pitch);
    store_be32(record + 12u,
               (uint32_t)span->source_x << 16 | span->source_y);
    store_be32(record + 16u,
               (uint32_t)span->start << 16 | (span->end - span->start));
    store_be32(record + 20u, span->flags);
    store_be32(record + 24u, span->value);
    store_be32(record + 28u, 0u);
    return 1;
}

int astra_window_scene_compile(const void *request_pointer,
                               size_t request_bytes,
                               volatile void *output_pointer,
                               size_t output_bytes, uint32_t arena_bytes,
                               uint32_t *written_out)
{
    const uint8_t *request = request_pointer;
    volatile uint8_t *output = output_pointer;
    struct row_span rows[2][ASTRA_DISPLAY_WIDTH];
    struct scene_layer layer;
    uint32_t total_bytes;
    uint32_t generation;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t layer_count;
    uint32_t layer_offset;
    uint32_t output_offset;
    uint32_t output_capacity;
    uint32_t line_offset = ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES;
    uint32_t span_offset;
    uint32_t span_count = 0u;
    uint16_t backdrop;

    if (written_out != NULL)
        *written_out = 0u;
    if (request == NULL || output == NULL || written_out == NULL ||
        request_bytes < ASTRA_WINDOW_SCENE_HEADER_BYTES)
        return ASTRA_WINDOW_SCENE_BAD_REQUEST;
    total_bytes = load_be32(request + 8u);
    generation = load_be32(request + 12u);
    size = load_be32(request + 16u);
    width = size >> 16;
    height = size & 0xffffu;
    layer_count = load_be32(request + 20u);
    layer_offset = load_be32(request + 24u);
    output_offset = load_be32(request + 28u);
    output_capacity = load_be32(request + 32u);
    backdrop = (uint16_t)load_be32(request + 36u);
    if (load_be32(request + 0u) != ASTRA_WINDOW_SCENE_MAGIC ||
        load_be32(request + 4u) != ASTRA_WINDOW_SCENE_VERSION ||
        total_bytes != request_bytes || generation == 0u || width == 0u ||
        height == 0u || width > ASTRA_DISPLAY_WIDTH ||
        height > ASTRA_DISPLAY_HEIGHT ||
        layer_offset < ASTRA_WINDOW_SCENE_HEADER_BYTES ||
        (layer_offset & 31u) != 0u ||
        (uint64_t)layer_offset +
                (uint64_t)layer_count * ASTRA_WINDOW_SCENE_LAYER_BYTES >
            total_bytes ||
        (output_offset & 63u) != 0u ||
        output_capacity > output_bytes || output_capacity <
            ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES ||
        (uint64_t)output_offset + output_capacity > arena_bytes ||
        load_be32(request + 36u) > UINT16_MAX ||
        load_be32(request + 40u) != 0u)
        return ASTRA_WINDOW_SCENE_BAD_REQUEST;
    for (uint32_t index = 0u; index < 5u; ++index)
        if (load_be32(request + 44u + index * 4u) != 0u)
            return ASTRA_WINDOW_SCENE_BAD_REQUEST;
    if ((uint64_t)line_offset +
            (uint64_t)height * ASTRA_WINDOW_SCENE_LINE_BYTES > UINT32_MAX)
        return ASTRA_WINDOW_SCENE_NO_SPACE;
    span_offset = align64(line_offset +
                          height * ASTRA_WINDOW_SCENE_LINE_BYTES);
    if ((uint64_t)span_offset +
            (uint64_t)height * ASTRA_WINDOW_SCENE_SPAN_BYTES >
        output_capacity)
        return ASTRA_WINDOW_SCENE_NO_SPACE;
    for (uint32_t index = 0u; index < layer_count; ++index)
        if (!decode_layer(request + layer_offset +
                              index * ASTRA_WINDOW_SCENE_LAYER_BYTES,
                          arena_bytes, output_offset, output_capacity,
                          &layer))
            return ASTRA_WINDOW_SCENE_BAD_LAYER;

    /* A valid header is published last; an interrupted compile is inert. */
    store_be32(output, 0u);
    for (uint32_t y = 0u; y < height; ++y) {
        struct row_span *current = rows[0];
        struct row_span *next = rows[1];
        uint32_t current_count = 1u;

        current[0] = (struct row_span){
            .start = 0u,
            .end = (uint16_t)width,
            .flags = ASTRA_WINDOW_SCENE_SPAN_SOLID,
            .value = backdrop,
        };
        for (uint32_t index = 0u; index < layer_count; ++index) {
            const uint8_t *record = request + layer_offset +
                                    index * ASTRA_WINDOW_SCENE_LAYER_BYTES;
            int32_t left;
            int32_t right;
            uint32_t source_y;
            uint32_t inset;
            uint32_t next_count = 0u;
            struct row_span incoming;
            struct row_span *swap;

            (void)decode_layer(record, arena_bytes, output_offset,
                               output_capacity, &layer);
            if (!layer.visible || (int32_t)y < layer.y ||
                (int32_t)y >= layer.y + (int32_t)layer.height)
                continue;
            source_y = (uint32_t)((int32_t)y - layer.y);
            inset = astra_graphics_rounded_inset(
                source_y, layer.height, layer.radius);
            left = layer.x + (int32_t)inset;
            right = layer.x + (int32_t)layer.width - (int32_t)inset;
            if (left < 0)
                left = 0;
            if (right > (int32_t)width)
                right = (int32_t)width;
            if (left >= right)
                continue;
            incoming = (struct row_span){
                .source_offset = layer.source_offset,
                .source_bytes = layer.source_bytes,
                .source_pitch = layer.source_pitch,
                .source_x = (uint16_t)(left - layer.x),
                .source_y = (uint16_t)source_y,
                .start = (uint16_t)left,
                .end = (uint16_t)right,
            };
            if (!overlay(next, &next_count, current, current_count, incoming,
                         width))
                return ASTRA_WINDOW_SCENE_NO_SPACE;
            swap = current;
            current = next;
            next = swap;
            current_count = next_count;
        }
        {
            volatile uint8_t *line = output + line_offset +
                y * ASTRA_WINDOW_SCENE_LINE_BYTES;

            store_be32(line + 0u, span_count);
            store_be32(line + 4u, current_count);
        }
        for (uint32_t index = 0u; index < current_count; ++index)
            if (!write_span(output, output_capacity, span_offset,
                            span_count++, &current[index]))
                return ASTRA_WINDOW_SCENE_NO_SPACE;
    }
    total_bytes = span_offset + span_count * ASTRA_WINDOW_SCENE_SPAN_BYTES;
    for (uint32_t offset = 0u;
         offset < ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES; offset += 4u)
        store_be32(output + offset, 0u);
    store_be32(output + 4u, ASTRA_WINDOW_SCENE_COMPILED_VERSION);
    store_be32(output + 8u, total_bytes);
    store_be32(output + 12u, generation);
    store_be32(output + 16u, width << 16 | height);
    store_be32(output + 20u, height);
    store_be32(output + 24u, line_offset);
    store_be32(output + 28u, span_offset);
    store_be32(output + 32u, span_count);
    store_be32(output + 0u, ASTRA_WINDOW_SCENE_COMPILED_MAGIC);
    *written_out = total_bytes;
    return ASTRA_WINDOW_SCENE_OK;
}
