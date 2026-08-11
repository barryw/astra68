#ifndef ASTRA_RENDER_BATCH_H
#define ASTRA_RENDER_BATCH_H

#include <stdint.h>

/*
 * A render batch is copied unchanged into Astraea's graphics arena. Commands,
 * descriptors, glyph masks, and source pixels therefore use the hardware's
 * native big-endian records and arena-relative offsets. The 68030 builds the
 * records; it never expands them into framebuffer pixels.
 */
#define ASTRA_RENDER_BATCH_MAGIC UINT32_C(0x41524254) /* ARBT */
#define ASTRA_RENDER_BATCH_VERSION_1_0 UINT32_C(0x00010000)
#define ASTRA_RENDER_BATCH_HEADER_BYTES 64u
#define ASTRA_RENDER_BATCH_ARENA_OFFSET UINT32_C(0x00400000)
#define ASTRA_RENDER_BATCH_SUBMISSION_OFFSET UINT32_C(0x00401000)
#define ASTRA_RENDER_BATCH_COMPLETION_OFFSET UINT32_C(0x00411000)
#define ASTRA_RENDER_BATCH_RESOURCE_OFFSET UINT32_C(0x00419000)
#define ASTRA_RENDER_BATCH_SCANOUT0_OFFSET UINT32_C(0x00000000)
#define ASTRA_RENDER_BATCH_SCANOUT1_OFFSET UINT32_C(0x00200000)
#define ASTRA_RENDER_CURSOR_OFFSET UINT32_C(0x00600000)
#define ASTRA_RENDER_CURSOR_WIDTH 16u
#define ASTRA_RENDER_CURSOR_HEIGHT 24u
#define ASTRA_RENDER_CURSOR_PALETTE_BANK 15u
#define ASTRA_RENDER_BATCH_MIN_BYTES \
    (ASTRA_RENDER_BATCH_RESOURCE_OFFSET - ASTRA_RENDER_BATCH_ARENA_OFFSET)
#define ASTRA_RENDER_BATCH_MAX_BYTES (1280u * 720u * 2u)

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
