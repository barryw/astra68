#ifndef ASTRA_DRAW_LIST_H
#define ASTRA_DRAW_LIST_H

#include <stdint.h>

#define ASTRA_DRAW_LIST_MAGIC UINT32_C(0x41444c54) /* ADLT */
#define ASTRA_DRAW_LIST_VERSION_1_0 UINT32_C(0x00010000)
#define ASTRA_DRAW_LIST_AREA_BYTES 16384u
#define ASTRA_DRAW_LIST_COMMAND_MAX 128u
#define ASTRA_DRAW_LIST_PAYLOAD_OFFSET 8256u
#define ASTRA_DRAW_LIST_PAYLOAD_BYTES \
    (ASTRA_DRAW_LIST_AREA_BYTES - ASTRA_DRAW_LIST_PAYLOAD_OFFSET)

enum {
    ASTRA_DRAW_LIST_FILL = 1u,
    ASTRA_DRAW_LIST_FILL_ROUNDED = 2u,
    ASTRA_DRAW_LIST_TEXT = 3u,
};

typedef struct AstraDrawListHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_bytes;
    uint32_t command_count;
    uint32_t payload_bytes;
    uint16_t width;
    uint16_t height;
    uint32_t reserved[10];
} AstraDrawListHeader;

typedef struct AstraDrawListCommand {
    uint32_t operation;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t foreground;
    uint32_t background;
    uint32_t radius;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint16_t font_height;
    uint16_t reserved16;
    uint32_t reserved[4];
} AstraDrawListCommand;

_Static_assert(sizeof(AstraDrawListHeader) == 64u,
               "draw-list header ABI changed");
_Static_assert(sizeof(AstraDrawListCommand) == 64u,
               "draw-list command ABI changed");
_Static_assert(64u + ASTRA_DRAW_LIST_COMMAND_MAX * 64u ==
                   ASTRA_DRAW_LIST_PAYLOAD_OFFSET,
               "draw-list payload layout changed");

#endif
