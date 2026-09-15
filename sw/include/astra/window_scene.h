#ifndef ASTRA_WINDOW_SCENE_H
#define ASTRA_WINDOW_SCENE_H

#include <astra/window_scene_protocol.h>
#include <limits.h>
#include <stdint.h>

typedef struct AstraWindowSceneHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_bytes;
    uint32_t generation;
    uint32_t width_height;
    uint32_t layer_count;
    uint32_t layer_offset;
    uint32_t output_offset;
    uint32_t output_capacity;
    uint32_t backdrop_rgb565;
    uint32_t flags;
    uint32_t reserved[5];
} AstraWindowSceneHeader;

typedef struct AstraWindowSceneLayer {
    uint32_t source_offset;
    uint32_t source_bytes;
    uint32_t source_pitch;
    uint32_t source_width_height;
    uint32_t destination_x_y;
    uint32_t radius_flags;
    uint32_t reserved[2];
} AstraWindowSceneLayer;

typedef struct AstraWindowSceneCompiledHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_bytes;
    uint32_t generation;
    uint32_t width_height;
    uint32_t line_count;
    uint32_t line_offset;
    uint32_t span_offset;
    uint32_t span_count;
    uint32_t flags;
    uint32_t reserved[6];
} AstraWindowSceneCompiledHeader;

typedef struct AstraWindowSceneLine {
    uint32_t first_span;
    uint32_t span_count;
} AstraWindowSceneLine;

typedef struct AstraWindowSceneSpan {
    uint32_t source_offset;
    uint32_t source_bytes;
    uint32_t source_pitch;
    uint32_t source_x_y;
    uint32_t destination_x_length;
    uint32_t flags;
    uint32_t value;
    uint32_t reserved;
} AstraWindowSceneSpan;

_Static_assert(sizeof(AstraWindowSceneHeader) ==
                   ASTRA_WINDOW_SCENE_HEADER_BYTES,
               "window scene header ABI changed");
_Static_assert(sizeof(AstraWindowSceneLayer) ==
                   ASTRA_WINDOW_SCENE_LAYER_BYTES,
               "window scene layer ABI changed");
_Static_assert(sizeof(AstraWindowSceneCompiledHeader) ==
                   ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES,
               "compiled window scene header ABI changed");
_Static_assert(sizeof(AstraWindowSceneLine) == ASTRA_WINDOW_SCENE_LINE_BYTES,
               "window scene line ABI changed");
_Static_assert(sizeof(AstraWindowSceneSpan) == ASTRA_WINDOW_SCENE_SPAN_BYTES,
               "window scene span ABI changed");

static inline uint32_t astra_window_scene_compiled_capacity(
    uint16_t width, uint16_t height, uint32_t layer_count)
{
    uint64_t spans_per_line = (uint64_t)layer_count * 2u + 1u;
    uint64_t line_end;
    uint64_t bytes;

    if (width == 0u || height == 0u)
        return 0u;
    if (spans_per_line > width)
        spans_per_line = width;
    line_end = ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES +
               (uint64_t)height * ASTRA_WINDOW_SCENE_LINE_BYTES;
    line_end = (line_end + 63u) & ~UINT64_C(63);
    bytes = line_end + (uint64_t)height * spans_per_line *
                       ASTRA_WINDOW_SCENE_SPAN_BYTES;
    bytes = (bytes + 63u) & ~UINT64_C(63);
    return bytes > UINT32_MAX ? 0u : (uint32_t)bytes;
}

#endif
