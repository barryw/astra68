#ifndef ASTRA_DRAW_LIST_H
#define ASTRA_DRAW_LIST_H

#include <stdint.h>

/*
 * ADLT is the one wire format for display-service drawing: a window's
 * retained list and a session's submitted list are both ADLT and both lower
 * through astra_render_builder_replay. See docs/MANAGED_GRAPHICS.md.
 */
#define ASTRA_DRAW_LIST_MAGIC UINT32_C(0x41444c54) /* ADLT */
#define ASTRA_DRAW_LIST_VERSION_1_5 UINT32_C(0x00010005)
#define ASTRA_DRAW_LIST_HEADER_BYTES 64u
#define ASTRA_DRAW_LIST_COMMAND_BYTES 64u
/* A retained window list is one fixed 16 KiB area of 128 commands. */
#define ASTRA_DRAW_LIST_AREA_BYTES 16384u
#define ASTRA_DRAW_LIST_COMMAND_MAX 128u
/* A submitted session list may grow up to 1 MiB. */
#define ASTRA_DRAW_LIST_SESSION_BYTES_MAX UINT32_C(0x00100000)
#define ASTRA_DRAW_LIST_PAYLOAD_OFFSET \
    (ASTRA_DRAW_LIST_HEADER_BYTES + \
     ASTRA_DRAW_LIST_COMMAND_MAX * ASTRA_DRAW_LIST_COMMAND_BYTES)
#define ASTRA_DRAW_LIST_PAYLOAD_BYTES \
    (ASTRA_DRAW_LIST_AREA_BYTES - ASTRA_DRAW_LIST_PAYLOAD_OFFSET)

enum {
    ASTRA_DRAW_LIST_FILL = 1u,
    ASTRA_DRAW_LIST_FILL_ROUNDED = 2u,
    ASTRA_DRAW_LIST_TEXT = 3u,
    ASTRA_DRAW_LIST_MONO_TEXT = 4u,
    ASTRA_DRAW_LIST_BLIT = 5u,
    ASTRA_DRAW_LIST_LINE = 6u,
    /* payload: 3 * n AstraDrawListVertex; source: surface id, or 0 for
       untextured triangles colored by the vertices alone. */
    ASTRA_DRAW_LIST_TRIANGLES = 7u,
    /* payload: n AstraDrawListRect filled with the one color in the
       command's blend mode; each is a FILL with the command's clip. */
    ASTRA_DRAW_LIST_FILL_RECTS = 8u,
    /* payload: n AstraDrawListSegment in the one color, each a LINE with
       the command's clip and blend field. */
    ASTRA_DRAW_LIST_LINES = 9u,
};

/* Command flags. TEXT and MONO_TEXT use ASTRA_TEXT_RENDER_STYLE_*. */
enum {
    /* BLIT only. */
    ASTRA_DRAW_LIST_FLIP_X = 1u << 0,
    ASTRA_DRAW_LIST_FLIP_Y = 1u << 1,
    /* FILL, FILL_RECTS, LINE, LINES, BLIT, and TRIANGLES: one
       ASTRA_DRAW_LIST_BLEND_* mode, with SDL2's straight-alpha equations
       (docs/TEXTURE_ENGINE.md §6). */
    ASTRA_DRAW_LIST_BLEND_SHIFT = 4u,
    ASTRA_DRAW_LIST_BLEND_MASK = 7u << 4,
    /* BLIT and textured TRIANGLES: bilinear sampling. */
    ASTRA_DRAW_LIST_FILTER_LINEAR = 1u << 8,
};

/* Blend modes; the values are the hardware's TRIANGLE_OPTION_BLEND_*. */
enum {
    ASTRA_DRAW_LIST_BLEND_NONE = 0u,
    ASTRA_DRAW_LIST_BLEND_BLEND = 1u,
    ASTRA_DRAW_LIST_BLEND_ADD = 2u,
    ASTRA_DRAW_LIST_BLEND_MOD = 3u,
    ASTRA_DRAW_LIST_BLEND_MUL = 4u,
};

#define ASTRA_DRAW_LIST_BLEND_FLAGS(mode) \
    ((uint32_t)(mode) << ASTRA_DRAW_LIST_BLEND_SHIFT)
#define ASTRA_DRAW_LIST_BLEND_MODE(flags) \
    (((uint32_t)(flags) & ASTRA_DRAW_LIST_BLEND_MASK) >> \
     ASTRA_DRAW_LIST_BLEND_SHIFT)

/* One TRIANGLES vertex, native byte order like the rest of the list. */
typedef struct AstraDrawListVertex {
    int32_t x;      /* signed 24.8 destination pixels */
    int32_t y;
    int32_t u;      /* signed 16.16 source texels; 0 when untextured */
    int32_t v;
    uint32_t color; /* 0xAARRGGBB straight alpha; multiplies the texel */
} AstraDrawListVertex;

/* One FILL_RECTS rectangle, native byte order; zero width or height
   draws nothing. */
typedef struct AstraDrawListRect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} AstraDrawListRect;

/* One LINES segment, native byte order; both endpoints are drawn. */
typedef struct AstraDrawListSegment {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
} AstraDrawListSegment;

/* The source surface id that names the list's own destination. */
#define ASTRA_DRAW_LIST_SOURCE_DESTINATION 0u
/* BLIT modulation that leaves the source unchanged. */
#define ASTRA_DRAW_LIST_COLOR_IDENTITY UINT32_C(0xffffffff)

typedef struct AstraDrawListHeader {
    uint32_t magic;
    uint32_t version;
    /* Complete list bytes: header, command_capacity commands, payload. */
    uint32_t total_bytes;
    uint32_t command_count;
    uint32_t payload_bytes;
    uint16_t width;
    uint16_t height;
    uint32_t command_capacity;
    uint32_t reserved[9];
} AstraDrawListHeader;

/*
 * Colors are 0xAARRGGBB with straight alpha. For BLIT, color modulates the
 * source per channel; its alpha is constant opacity. TRIANGLES carries its
 * colors in the vertices and leaves color, the rectangle, and the source
 * rectangle zero. FILL_RECTS and LINES carry their rectangles or segments
 * in the payload and the one color in color; their own rectangle is zero.
 */
typedef struct AstraDrawListCommand {
    uint32_t operation;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
    /* BLIT source surface id; ASTRA_DRAW_LIST_SOURCE_DESTINATION or a
       session surface. TRIANGLES: a session surface, or 0 for untextured.
       Zero for every other operation. */
    uint32_t source;
    uint32_t radius;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint16_t font_height;
    uint16_t reserved16;
    uint16_t clip_left;
    uint16_t clip_top;
    uint16_t clip_right;
    uint16_t clip_bottom;
    int16_t source_x;
    int16_t source_y;
    uint16_t source_width;
    uint16_t source_height;
} AstraDrawListCommand;

static inline uint32_t astra_draw_list_payload_offset(uint32_t capacity)
{
    return ASTRA_DRAW_LIST_HEADER_BYTES +
           capacity * ASTRA_DRAW_LIST_COMMAND_BYTES;
}

_Static_assert(sizeof(AstraDrawListHeader) == ASTRA_DRAW_LIST_HEADER_BYTES,
               "draw-list header ABI changed");
_Static_assert(sizeof(AstraDrawListCommand) == ASTRA_DRAW_LIST_COMMAND_BYTES,
               "draw-list command ABI changed");
_Static_assert(sizeof(AstraDrawListVertex) == 20u,
               "draw-list vertex ABI changed");
_Static_assert(sizeof(AstraDrawListRect) == 8u,
               "draw-list rectangle ABI changed");
_Static_assert(sizeof(AstraDrawListSegment) == 8u,
               "draw-list segment ABI changed");

#endif
