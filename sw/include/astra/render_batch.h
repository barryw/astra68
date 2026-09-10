#ifndef ASTRA_RENDER_BATCH_H
#define ASTRA_RENDER_BATCH_H

#include <astra/display.h>
#include <stdint.h>

/*
 * A render batch is copied unchanged into Astraea's graphics arena. Commands,
 * descriptors, glyph masks, and source pixels therefore use the hardware's
 * native big-endian records and arena-relative offsets. The MC68040 builds the
 * records; it never expands them into framebuffer pixels.
 */
#define ASTRA_RENDER_BATCH_MAGIC UINT32_C(0x41524254) /* ARBT */
#define ASTRA_RENDER_BATCH_VERSION_1_0 UINT32_C(0x00010000)
#define ASTRA_RENDER_BATCH_HEADER_BYTES 64u
#define ASTRA_RENDER_BATCH_ARENA_OFFSET UINT32_C(0x00800000)
#define ASTRA_RENDER_BATCH_SUBMISSION_OFFSET UINT32_C(0x00801000)
#define ASTRA_RENDER_BATCH_COMPLETION_OFFSET UINT32_C(0x00811000)
#define ASTRA_RENDER_BATCH_RESOURCE_OFFSET UINT32_C(0x00819000)
#define ASTRA_RENDER_BATCH_GLYPH_OFFSET UINT32_C(0x0081a000)
#define ASTRA_RENDER_BATCH_DATA_OFFSET UINT32_C(0x00822000)
#define ASTRA_RENDER_BATCH_SCANOUT0_OFFSET UINT32_C(0x00000000)
#define ASTRA_RENDER_BATCH_SCANOUT1_OFFSET UINT32_C(0x00400000)
#define ASTRA_RENDER_BATCH_MIN_BYTES \
    (ASTRA_RENDER_BATCH_RESOURCE_OFFSET - ASTRA_RENDER_BATCH_ARENA_OFFSET)
#define ASTRA_RENDER_BATCH_MAX_BYTES \
    (ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * 2u)

typedef struct AstraRenderBatchHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_bytes;
    uint32_t command_count;
    uint32_t submission_ring_offset;
    uint32_t completion_ring_offset;
    uint32_t resource_generation;
    uint32_t scanout_offset;
    uint32_t reserved[8];
} AstraRenderBatchHeader;

_Static_assert(sizeof(AstraRenderBatchHeader) ==
                   ASTRA_RENDER_BATCH_HEADER_BYTES,
               "render batch header ABI changed");

#endif
