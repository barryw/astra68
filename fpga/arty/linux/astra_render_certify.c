// SPDX-License-Identifier: MIT
// Certify the bounded Astraea render transport and complete blitter on hardware.

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "astra_graphics_hw.h"
#include "astra_render_protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SUBMISSION_RING_OFFSET = 0x00400000u,
    COMPLETION_RING_OFFSET = 0x00410000u,
    SCRATCH_DESCRIPTOR_OFFSET = 0x00418000u,
    FRAME_DESCRIPTOR_OFFSET = 0x00418020u,
    INDEX_SOURCE_DESCRIPTOR_OFFSET = 0x00418040u,
    INDEX_DEST_DESCRIPTOR_OFFSET = 0x00418060u,
    MASK_DESCRIPTOR_OFFSET = 0x00418080u,
    RGB565_SOURCE_DESCRIPTOR_OFFSET = 0x004180a0u,
    XRGB_DEST_DESCRIPTOR_OFFSET = 0x004180c0u,
    ARGB_SOURCE_DESCRIPTOR_OFFSET = 0x004180e0u,
    RGB565_DEST_DESCRIPTOR_OFFSET = 0x00418100u,
    PALETTE_SOURCE_DESCRIPTOR_OFFSET = 0x00418120u,
    GEOMETRY_DESCRIPTOR_OFFSET = 0x00418140u,
    FLOOD_DESCRIPTOR_OFFSET = 0x00418160u,
    FLOOD_WORKSPACE_DESCRIPTOR_OFFSET = 0x00418180u,
    GLYPH_MASK_DESCRIPTOR_OFFSET = 0x004181a0u,
    GLYPH_A4_DESCRIPTOR_OFFSET = 0x004181c0u,
    GLYPH_A8_DESCRIPTOR_OFFSET = 0x004181e0u,
    GLYPH_INDEX4_DESCRIPTOR_OFFSET = 0x00418200u,
    GLYPH_INDEX8_DESCRIPTOR_OFFSET = 0x00418220u,
    GLYPH_RGB_DEST_DESCRIPTOR_OFFSET = 0x00418240u,
    GLYPH_XRGB_DEST_DESCRIPTOR_OFFSET = 0x00418260u,
    GLYPH_INDEX_DEST_DESCRIPTOR_OFFSET = 0x00418280u,
    GLYPH_DESCRIPTOR_ARRAY_OFFSET = 0x00418300u,
    GLYPH_DESCRIPTOR_RECORDS = 7u,
    QUEUE_REGION_BYTES = GLYPH_DESCRIPTOR_ARRAY_OFFSET +
        GLYPH_DESCRIPTOR_RECORDS * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES -
        SUBMISSION_RING_OFFSET,
    SCRATCH_DATA_OFFSET = 0x00600000u,
    SCRATCH_WIDTH = 64u,
    SCRATCH_HEIGHT = 16u,
    SCRATCH_PITCH = SCRATCH_WIDTH,
    SCRATCH_BYTES = SCRATCH_PITCH * SCRATCH_HEIGHT,
    SCRATCH_REGION_BYTES = 0x00010000u,
    INDEX_SOURCE_DATA_OFFSET = 0x00601000u,
    INDEX_DEST_DATA_OFFSET = 0x00602000u,
    MASK_DATA_OFFSET = 0x00603000u,
    PALETTE_DATA_OFFSET = 0x00604000u,
    RGB565_SOURCE_DATA_OFFSET = 0x00605000u,
    XRGB_DEST_DATA_OFFSET = 0x00606000u,
    ARGB_SOURCE_DATA_OFFSET = 0x00607000u,
    RGB565_DEST_DATA_OFFSET = 0x00608000u,
    GEOMETRY_DATA_OFFSET = 0x00609000u,
    FLOOD_DATA_OFFSET = 0x0060a000u,
    FLOOD_WORKSPACE_DATA_OFFSET = 0x0060b000u,
    GLYPH_MASK_DATA_OFFSET = 0x0060c000u,
    GLYPH_A4_DATA_OFFSET = 0x0060c100u,
    GLYPH_A8_DATA_OFFSET = 0x0060c200u,
    GLYPH_INDEX4_DATA_OFFSET = 0x0060c300u,
    GLYPH_INDEX8_DATA_OFFSET = 0x0060c400u,
    GLYPH_RGB_DEST_DATA_OFFSET = 0x0060d000u,
    GLYPH_XRGB_DEST_DATA_OFFSET = 0x0060d100u,
    GLYPH_INDEX_DEST_DATA_OFFSET = 0x0060d200u,
    GLYPH_INDEX4_PALETTE_OFFSET = 0x0060e000u,
    GLYPH_INDEX8_PALETTE_OFFSET = 0x0060f000u,
    GEOMETRY_SURFACE_BYTES = 16u * 16u,
    FLOOD_WORKSPACE_BYTES = 16u * 16u * 4u,
    FEATURE_WIDTH = 16u,
    FEATURE_HEIGHT = 8u,
    FEATURE_INDEX_PITCH = 16u,
    FEATURE_MASK_PITCH = 2u,
    FEATURE_XRGB_PITCH = 32u,
    FEATURE_PIXEL_COUNT = 43u,
    FRAME_DATA_OFFSET = 0x00800000u,
    RESOURCE_GENERATION = 0x00000017u,
    BASIC_COMMAND_COUNT = 6u,
    COMMAND_SCALE_REFLECT = 6u,
    COMMAND_SOURCE_KEY = 7u,
    COMMAND_ROP_FIRST = 8u,
    COMMAND_ROP_LAST = 23u,
    COMMAND_RGB565_TO_XRGB = 24u,
    COMMAND_ARGB_TO_RGB565 = 25u,
    COMMAND_SOURCE_OVER = 26u,
    COMMAND_PALETTE_MASK = 27u,
    COMMAND_MASK_REJECT = 28u,
    COMMAND_COUNT = 29u,
    VIRTUAL_SPRITE_GROUP_FIRST = COMMAND_COUNT,
    VIRTUAL_SPRITE_GROUP_COUNT = 64u,
    TOTAL_COMMAND_COUNT = COMMAND_COUNT + VIRTUAL_SPRITE_GROUP_COUNT,
    GEOMETRY_COMMAND_FIRST = TOTAL_COMMAND_COUNT,
    GEOMETRY_COMMAND_COUNT = 6u,
    GEOMETRY_COMMAND_END = GEOMETRY_COMMAND_FIRST + GEOMETRY_COMMAND_COUNT,
    GLYPH_COMMAND_FIRST = GEOMETRY_COMMAND_END,
    GLYPH_SUCCESS_COMMAND_COUNT = 5u,
    GLYPH_FAILURE_COMMAND = GLYPH_COMMAND_FIRST +
        GLYPH_SUCCESS_COMMAND_COUNT,
    GLYPH_COMMAND_END = GLYPH_FAILURE_COMMAND + 1u,
    FLOOD_OVERFLOW_COMMAND = GLYPH_COMMAND_END,
    CERTIFICATION_COMMAND_END = FLOOD_OVERFLOW_COMMAND + 1u,
    SCREEN_OFFSET_COMMAND = CERTIFICATION_COMMAND_END,
    SCREEN_OFFSET_COMMAND_END = SCREEN_OFFSET_COMMAND + 1u,
    SCREEN_OFFSET_SOURCE_Y = 76u,
    SCREEN_OFFSET_HEIGHT = 644u,
    VIRTUAL_SPRITE_WIDTH = 16u,
    VIRTUAL_SPRITE_HEIGHT = 16u,
    VIRTUAL_SPRITE_COLUMNS = 16u,
    VIRTUAL_SPRITE_X_ORIGIN = 16u,
    VIRTUAL_SPRITE_Y_ORIGIN = 80u,
    VIRTUAL_SPRITE_X_STRIDE = 78u,
    VIRTUAL_SPRITE_Y_STRIDE = 80u,
    COMMAND_DEADLINE_US = ASTRA_RENDER_MAX_DEADLINE_US,
    ENGINE_TIMEOUT_NS = 2000000000u,
    COMPLETION_TIMEOUT_NS = 3000000000u,
    COLOR_BACKGROUND = 0x0841u,
    COLOR_HEADER = 0x07ffu,
    COLOR_LEFT = 0xfd20u,
    COLOR_RIGHT = 0x981fu,
    COLOR_CLIPPED = 0xffe0u,
};

_Static_assert(ASTRA_RENDER_RING_ENTRIES == 1024u,
               "hardware ring indexing assumes 1024 entries");
_Static_assert(TOTAL_COMMAND_COUNT <=
                   (unsigned)ASTRA_RENDER_RING_ENTRIES,
               "certification commands exceed the bounded ring");
_Static_assert(CERTIFICATION_COMMAND_END <=
                   (unsigned)ASTRA_RENDER_RING_ENTRIES,
               "geometry commands exceed the bounded ring");
_Static_assert(SCREEN_OFFSET_COMMAND_END <=
                   (unsigned)ASTRA_RENDER_RING_ENTRIES,
               "screen-offset command exceeds the bounded ring");
_Static_assert(GLYPH_DESCRIPTOR_RECORDS >=
                   GLYPH_SUCCESS_COMMAND_COUNT + 2u,
               "glyph certification needs success and malformed records");
_Static_assert(COMPLETION_RING_OFFSET == SUBMISSION_RING_OFFSET +
                   ASTRA_RENDER_COMMAND_BYTES * ASTRA_RENDER_RING_ENTRIES,
               "completion ring must follow the submission ring");
_Static_assert(SCRATCH_DESCRIPTOR_OFFSET == COMPLETION_RING_OFFSET +
                   ASTRA_RENDER_COMPLETION_BYTES * ASTRA_RENDER_RING_ENTRIES,
               "surface descriptors must follow the completion ring");
_Static_assert(FRAME_DATA_OFFSET + ASTRA_FRAMEBUFFER_BYTES <=
                   ASTRA_GRAPHICS_ARENA_BYTES,
               "certification framebuffer exceeds the graphics arena");
_Static_assert(SCRATCH_REGION_BYTES >=
                   FLOOD_WORKSPACE_DATA_OFFSET + FLOOD_WORKSPACE_BYTES -
                       SCRATCH_DATA_OFFSET,
               "complete blitter fixtures exceed the scratch mapping");
_Static_assert(SCRATCH_REGION_BYTES >=
                   GLYPH_INDEX8_PALETTE_OFFSET + 256u * 4u -
                       SCRATCH_DATA_OFFSET,
               "glyph fixtures exceed the scratch mapping");

struct render_maps {
    struct astra_graphics_memory_map queues;
    struct astra_graphics_memory_map scratch;
    struct astra_graphics_memory_map frame;
};

struct scene_state {
    uint32_t global_control;
    uint32_t backdrop;
    uint32_t framebuffer_base;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_size;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t framebuffer_control;
    uint32_t framebuffer_key;
    uint32_t tile0_control;
    uint32_t tile1_control;
    uint32_t sprite_control;
};

struct engine_counters {
    uint32_t submitted;
    uint32_t completed;
    uint32_t failed;
    uint32_t backpressure;
    uint32_t timeouts;
    uint32_t resets;
};

static uint16_t expected_opcode(unsigned index)
{
    return index == 0u || index >= BASIC_COMMAND_COUNT ?
        ASTRA_RENDER_OP_BLIT : ASTRA_RENDER_OP_FILL;
}

static uint32_t expected_pixel_count(unsigned index)
{
    switch (index) {
    case 0u:
        return 48u * SCRATCH_HEIGHT;
    case 1u:
        return ASTRA_FRAMEBUFFER_WIDTH * ASTRA_FRAMEBUFFER_HEIGHT;
    case 2u:
        return ASTRA_FRAMEBUFFER_WIDTH * 48u;
    case 3u:
        return 400u * 240u;
    case 4u:
        return 360u * 280u;
    case 5u:
        return 160u * 100u;
    case COMMAND_SCALE_REFLECT:
        return 20u;
    case COMMAND_SOURCE_KEY:
        return 2u;
    case COMMAND_RGB565_TO_XRGB:
        return 2u;
    case COMMAND_ARGB_TO_RGB565:
    case COMMAND_SOURCE_OVER:
    case COMMAND_PALETTE_MASK:
        return 1u;
    case COMMAND_MASK_REJECT:
        return 0u;
    default:
        return index >= COMMAND_ROP_FIRST && index <= COMMAND_ROP_LAST ?
            1u : UINT32_MAX;
    }
}

static uint32_t expected_total_pixels(void)
{
    uint32_t total = 0u;
    unsigned index;

    for (index = 0u; index < COMMAND_COUNT; ++index)
        total += expected_pixel_count(index);
    return total;
}

static uint64_t deadline_after(uint64_t interval_ns)
{
    uint64_t now = astra_monotonic_nanoseconds();

    return interval_ns > UINT64_MAX - now ? UINT64_MAX : now + interval_ns;
}

static int sleep_nanoseconds(long nanoseconds)
{
    struct timespec delay = { .tv_sec = 0, .tv_nsec = nanoseconds };

    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            return -1;
        }
    }
    return 0;
}

static int sleep_milliseconds(unsigned milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };

    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            return -1;
        }
    }
    return 0;
}

static volatile uint8_t *queue_address(const struct render_maps *maps,
                                       uint32_t arena_offset, size_t bytes)
{
    uint64_t relative;

    if (arena_offset < SUBMISSION_RING_OFFSET)
        return NULL;
    relative = (uint64_t)arena_offset - SUBMISSION_RING_OFFSET;
    if (relative + bytes > maps->queues.data_bytes)
        return NULL;
    return maps->queues.data + (size_t)relative;
}

static volatile uint8_t *scratch_address(const struct render_maps *maps,
                                         uint32_t arena_offset, size_t bytes)
{
    uint64_t relative;

    if (arena_offset < SCRATCH_DATA_OFFSET)
        return NULL;
    relative = (uint64_t)arena_offset - SCRATCH_DATA_OFFSET;
    if (relative + bytes > maps->scratch.data_bytes)
        return NULL;
    return maps->scratch.data + (size_t)relative;
}

static void store_be32(volatile uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint32_t load_be32(volatile const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static uint16_t load_be16(volatile const uint8_t *source)
{
    return ((uint16_t)source[0] << 8) | source[1];
}

static void store_be16(volatile uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static uint32_t pair_u16(uint16_t high, uint16_t low)
{
    return ((uint32_t)high << 16) | low;
}

static uint32_t pair_s16(int16_t high, int16_t low)
{
    return ((uint32_t)(uint16_t)high << 16) | (uint16_t)low;
}

static int write_surface(const struct render_maps *maps,
                         uint32_t descriptor_offset,
                         uint32_t data_offset, uint32_t data_bytes,
                         uint32_t pitch, uint16_t width, uint16_t height,
                         uint8_t format, uint8_t flags,
                         uint32_t palette_offset)
{
    volatile uint8_t *descriptor = queue_address(
        maps, descriptor_offset, ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);

    if (descriptor == NULL)
        return -1;
    store_be32(descriptor + 0u,
               pair_u16(ASTRA_RENDER_ABI_VERSION,
                        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES));
    store_be32(descriptor + 4u, RESOURCE_GENERATION);
    store_be32(descriptor + 8u, data_offset);
    store_be32(descriptor + 12u, data_bytes);
    store_be32(descriptor + 16u, pitch);
    store_be32(descriptor + 20u, pair_u16(width, height));
    store_be32(descriptor + 24u,
               ((uint32_t)format << 24) | ((uint32_t)flags << 16));
    store_be32(descriptor + 28u, palette_offset);
    return 0;
}

static volatile uint8_t *command_record(const struct render_maps *maps,
                                        unsigned index)
{
    uint32_t offset = SUBMISSION_RING_OFFSET +
        (index & (ASTRA_RENDER_RING_ENTRIES - 1u)) *
        ASTRA_RENDER_COMMAND_BYTES;

    return queue_address(maps, offset, ASTRA_RENDER_COMMAND_BYTES);
}

static void clear_record(volatile uint8_t *record, size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index)
        record[index] = 0u;
}

static int write_common_command(const struct render_maps *maps,
                                unsigned index, uint16_t opcode,
                                uint16_t flags,
                                uint32_t sequence,
                                int16_t clip_left, int16_t clip_top,
                                int16_t clip_right, int16_t clip_bottom,
                                uint32_t destination_descriptor)
{
    volatile uint8_t *command = command_record(maps, index);

    if (command == NULL)
        return -1;
    clear_record(command, ASTRA_RENDER_COMMAND_BYTES);
    store_be32(command + 0u,
               pair_u16(ASTRA_RENDER_ABI_VERSION,
                        ASTRA_RENDER_COMMAND_BYTES));
    store_be32(command + 4u, pair_u16(opcode, flags));
    store_be32(command + 8u, sequence);
    store_be32(command + 12u, RESOURCE_GENERATION);
    store_be32(command + 16u, COMMAND_DEADLINE_US);
    store_be32(command + 24u, pair_s16(clip_left, clip_top));
    store_be32(command + 28u, pair_s16(clip_right, clip_bottom));
    store_be32(command + 32u, destination_descriptor);
    return 0;
}

static int write_fill(const struct render_maps *maps, unsigned index,
                      uint32_t sequence, int16_t x, int16_t y,
                      uint16_t width, uint16_t height, uint32_t color)
{
    volatile uint8_t *command;

    if (write_common_command(maps, index, ASTRA_RENDER_OP_FILL, 0u, sequence,
                             0, 0, ASTRA_FRAMEBUFFER_WIDTH,
                             ASTRA_FRAMEBUFFER_HEIGHT,
                             FRAME_DESCRIPTOR_OFFSET) != 0)
        return -1;
    command = command_record(maps, index);
    store_be32(command + 48u, pair_s16(x, y));
    store_be32(command + 56u, pair_u16(width, height));
    store_be32(command + 60u, color);
    return 0;
}

static int write_blit(const struct render_maps *maps, unsigned index,
                      uint16_t flags,
                      int16_t clip_left, int16_t clip_top,
                      int16_t clip_right, int16_t clip_bottom,
                      uint32_t destination_descriptor,
                      uint32_t source_descriptor,
                      uint32_t auxiliary_descriptor,
                      int16_t source_x, int16_t source_y,
                      int16_t destination_x, int16_t destination_y,
                      uint16_t source_width, uint16_t source_height,
                      uint16_t destination_width,
                      uint16_t destination_height,
                      uint32_t options)
{
    volatile uint8_t *command;

    if (write_common_command(maps, index, ASTRA_RENDER_OP_BLIT, flags,
                             index + 1u,
                             clip_left, clip_top, clip_right, clip_bottom,
                             destination_descriptor) != 0)
        return -1;
    command = command_record(maps, index);
    store_be32(command + 36u, source_descriptor);
    store_be32(command + 40u, auxiliary_descriptor);
    store_be32(command + 44u, pair_s16(source_x, source_y));
    store_be32(command + 48u, pair_s16(destination_x, destination_y));
    store_be32(command + 52u, pair_u16(source_width, source_height));
    store_be32(command + 56u,
               pair_u16(destination_width, destination_height));
    store_be32(command + 60u, options);
    return 0;
}

static int write_geometry(const struct render_maps *maps, unsigned index,
                          uint16_t opcode, uint16_t flags,
                          int16_t p0_x, int16_t p0_y,
                          int16_t p1_x, int16_t p1_y,
                          uint16_t radius_x, uint16_t radius_y,
                          uint64_t pattern, uint32_t background,
                          uint32_t foreground)
{
    volatile uint8_t *command;

    if (write_common_command(maps, index, opcode, flags, index + 1u,
                             0, 0, 16, 16,
                             GEOMETRY_DESCRIPTOR_OFFSET) != 0)
        return -1;
    command = command_record(maps, index);
    store_be32(command + 36u, (uint32_t)(pattern >> 32));
    store_be32(command + 40u, (uint32_t)pattern);
    store_be32(command + 44u, pair_s16(p0_x, p0_y));
    store_be32(command + 48u, pair_s16(p1_x, p1_y));
    store_be32(command + 52u, pair_u16(radius_x, radius_y));
    store_be32(command + 56u, background);
    store_be32(command + 60u, foreground);
    return 0;
}

static int write_flood(const struct render_maps *maps, unsigned index)
{
    volatile uint8_t *command;

    if (write_common_command(maps, index, ASTRA_RENDER_OP_FLOOD_FILL, 0u,
                             index + 1u, 0, 0, 16, 16,
                             FLOOD_DESCRIPTOR_OFFSET) != 0)
        return -1;
    command = command_record(maps, index);
    store_be32(command + 40u, FLOOD_WORKSPACE_DESCRIPTOR_OFFSET);
    store_be32(command + 44u, pair_s16(3, 3));
    store_be32(command + 60u, 7u);
    return 0;
}

static int write_glyph_descriptor(const struct render_maps *maps,
                                  unsigned record,
                                  uint32_t source_offset,
                                  uint16_t source_x, uint16_t source_y,
                                  int16_t destination_x,
                                  int16_t destination_y,
                                  uint16_t width, uint16_t height)
{
    uint32_t offset = GLYPH_DESCRIPTOR_ARRAY_OFFSET +
        record * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES;
    volatile uint8_t *descriptor = queue_address(
        maps, offset, ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES);

    if (descriptor == NULL)
        return -1;
    store_be32(descriptor + 0u, source_offset);
    store_be32(descriptor + 4u, pair_u16(source_x, source_y));
    store_be32(descriptor + 8u,
               pair_s16(destination_x, destination_y));
    store_be32(descriptor + 12u, pair_u16(width, height));
    return 0;
}

static int write_glyph(const struct render_maps *maps, unsigned index,
                       uint16_t flags, uint32_t destination_descriptor,
                       uint32_t source_descriptor,
                       unsigned first_record, uint32_t descriptor_count,
                       uint32_t foreground, uint32_t background,
                       uint8_t transparent_index)
{
    volatile uint8_t *command;

    if (write_common_command(maps, index, ASTRA_RENDER_OP_GLYPH_RUN,
                             flags, index + 1u, 0, 0, 16, 4,
                             destination_descriptor) != 0)
        return -1;
    command = command_record(maps, index);
    store_be32(command + 36u, source_descriptor);
    store_be32(command + 40u, GLYPH_DESCRIPTOR_ARRAY_OFFSET +
               first_record * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES);
    store_be32(command + 44u, descriptor_count);
    store_be32(command + 48u, foreground);
    store_be32(command + 52u, background);
    store_be32(command + 56u, transparent_index);
    return 0;
}

static int write_overlap_blit(const struct render_maps *maps)
{
    return write_blit(maps, 0u, 0u,
                      0, 0, SCRATCH_WIDTH, SCRATCH_HEIGHT,
                      SCRATCH_DESCRIPTOR_OFFSET, SCRATCH_DESCRIPTOR_OFFSET,
                      0u, 0, 0, 8, 0,
                      48u, SCRATCH_HEIGHT, 48u, SCRATCH_HEIGHT, 0u);
}

static int prepare_complete_blitter_data(struct render_maps *maps)
{
    volatile uint8_t *index_source = scratch_address(
        maps, INDEX_SOURCE_DATA_OFFSET,
        FEATURE_INDEX_PITCH * FEATURE_HEIGHT);
    volatile uint8_t *index_destination = scratch_address(
        maps, INDEX_DEST_DATA_OFFSET,
        FEATURE_INDEX_PITCH * FEATURE_HEIGHT);
    volatile uint8_t *mask = scratch_address(
        maps, MASK_DATA_OFFSET, FEATURE_MASK_PITCH * FEATURE_HEIGHT);
    volatile uint8_t *palette = scratch_address(
        maps, PALETTE_DATA_OFFSET, 256u * 4u);
    volatile uint8_t *rgb565_source = scratch_address(
        maps, RGB565_SOURCE_DATA_OFFSET, 4u);
    volatile uint8_t *xrgb_destination = scratch_address(
        maps, XRGB_DEST_DATA_OFFSET, FEATURE_XRGB_PITCH);
    volatile uint8_t *argb_source = scratch_address(
        maps, ARGB_SOURCE_DATA_OFFSET, 8u);
    volatile uint8_t *rgb565_destination = scratch_address(
        maps, RGB565_DEST_DATA_OFFSET, 2u);
    unsigned y;

    if (index_source == NULL || index_destination == NULL || mask == NULL ||
        palette == NULL || rgb565_source == NULL ||
        xrgb_destination == NULL || argb_source == NULL ||
        rgb565_destination == NULL)
        return -1;

    for (y = 0u; y < FEATURE_HEIGHT; ++y) {
        unsigned x;

        for (x = 0u; x < FEATURE_WIDTH; ++x) {
            index_source[y * FEATURE_INDEX_PITCH + x] =
                (uint8_t)(y * FEATURE_WIDTH + x);
            index_destination[y * FEATURE_INDEX_PITCH + x] = 0xeeu;
        }
    }
    index_source[6u * FEATURE_INDEX_PITCH + 0u] = 0x01u;
    index_source[6u * FEATURE_INDEX_PITCH + 1u] = 0x02u;
    index_source[6u * FEATURE_INDEX_PITCH + 2u] = 0x01u;
    index_source[6u * FEATURE_INDEX_PITCH + 3u] = 0x03u;
    index_source[6u * FEATURE_INDEX_PITCH + 4u] = 0x02u;
    index_source[6u * FEATURE_INDEX_PITCH + 5u] = 0x02u;
    for (y = 0u; y < FEATURE_WIDTH; ++y)
        index_source[7u * FEATURE_INDEX_PITCH + y] = 0x5au;

    for (y = 0u; y < 4u; ++y)
        index_destination[y] = 0xaau;
    for (y = 0u; y < FEATURE_WIDTH; ++y)
        index_destination[FEATURE_INDEX_PITCH + y] = 0xa5u;

    clear_record(mask, FEATURE_MASK_PITCH * FEATURE_HEIGHT);
    mask[6u * FEATURE_MASK_PITCH] = 0x08u;
    clear_record(palette, 256u * 4u);
    store_be32(palette + 8u, 0x80800000u);

    rgb565_source[0] = 0xf8u;
    rgb565_source[1] = 0x00u;
    rgb565_source[2] = 0x07u;
    rgb565_source[3] = 0xe0u;
    clear_record(xrgb_destination, FEATURE_XRGB_PITCH);
    store_be32(xrgb_destination + 2u * 4u, 0xff0000ffu);
    store_be32(xrgb_destination + 3u * 4u, 0xff0000ffu);
    store_be32(xrgb_destination + 4u * 4u, 0xff0000ffu);
    argb_source[0] = 0x80u;
    argb_source[1] = 0x40u;
    argb_source[2] = 0x20u;
    argb_source[3] = 0x10u;
    argb_source[4] = 0x80u;
    argb_source[5] = 0x80u;
    argb_source[6] = 0x00u;
    argb_source[7] = 0x00u;
    rgb565_destination[0] = 0xeeu;
    rgb565_destination[1] = 0xeeu;
    return 0;
}

static int write_complete_blitter_commands(const struct render_maps *maps)
{
    unsigned rop;

    if (write_blit(maps, COMMAND_SCALE_REFLECT,
                   ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
                   ASTRA_RENDER_FLAG_BLIT_REFLECT_Y,
                   1, 2, 6, 6,
                   INDEX_DEST_DESCRIPTOR_OFFSET,
                   INDEX_SOURCE_DESCRIPTOR_OFFSET, 0u,
                   1, 1, -1, 1, 4u, 3u, 8u, 6u, 0u) != 0 ||
        write_blit(maps, COMMAND_SOURCE_KEY,
                   ASTRA_RENDER_FLAG_BLIT_SOURCE_KEY,
                   0, 0, FEATURE_WIDTH, FEATURE_HEIGHT,
                   INDEX_DEST_DESCRIPTOR_OFFSET,
                   INDEX_SOURCE_DESCRIPTOR_OFFSET, 0u,
                   0, 6, 0, 0, 4u, 1u, 4u, 1u, 1u) != 0)
        return -1;

    for (rop = 0u; rop < 16u; ++rop) {
        uint16_t flags = (uint16_t)(ASTRA_RENDER_FLAG_BLIT_ROP_ENABLE |
            (rop << ASTRA_RENDER_FLAG_BLIT_ROP_SHIFT));
        unsigned command = COMMAND_ROP_FIRST + rop;

        if (write_blit(maps, command, flags,
                       0, 0, FEATURE_WIDTH, FEATURE_HEIGHT,
                       INDEX_DEST_DESCRIPTOR_OFFSET,
                       INDEX_SOURCE_DESCRIPTOR_OFFSET, 0u,
                       (int16_t)rop, 7, (int16_t)rop, 1,
                       1u, 1u, 1u, 1u, 0u) != 0)
            return -1;
    }

    if (write_blit(maps, COMMAND_RGB565_TO_XRGB, 0u,
                   0, 0, 5, 1,
                   XRGB_DEST_DESCRIPTOR_OFFSET,
                   RGB565_SOURCE_DESCRIPTOR_OFFSET, 0u,
                   0, 0, 0, 0, 2u, 1u, 2u, 1u, 0u) != 0 ||
        write_blit(maps, COMMAND_ARGB_TO_RGB565, 0u,
                   0, 0, 1, 1,
                   RGB565_DEST_DESCRIPTOR_OFFSET,
                   ARGB_SOURCE_DESCRIPTOR_OFFSET, 0u,
                   0, 0, 0, 0, 1u, 1u, 1u, 1u, 0u) != 0 ||
        write_blit(maps, COMMAND_SOURCE_OVER,
                   ASTRA_RENDER_FLAG_BLIT_ALPHA,
                   0, 0, 5, 1,
                   XRGB_DEST_DESCRIPTOR_OFFSET,
                   ARGB_SOURCE_DESCRIPTOR_OFFSET, 0u,
                   1, 0, 2, 0, 1u, 1u, 1u, 1u, 0x80000000u) != 0 ||
        write_blit(maps, COMMAND_PALETTE_MASK,
                   ASTRA_RENDER_FLAG_BLIT_PALETTE |
                   ASTRA_RENDER_FLAG_BLIT_ALPHA |
                   ASTRA_RENDER_FLAG_BLIT_MASK1,
                   0, 0, 5, 1,
                   XRGB_DEST_DESCRIPTOR_OFFSET,
                   PALETTE_SOURCE_DESCRIPTOR_OFFSET,
                   MASK_DESCRIPTOR_OFFSET,
                   4, 6, 3, 0, 1u, 1u, 1u, 1u, 0x80000000u) != 0 ||
        write_blit(maps, COMMAND_MASK_REJECT,
                   ASTRA_RENDER_FLAG_BLIT_PALETTE |
                   ASTRA_RENDER_FLAG_BLIT_ALPHA |
                   ASTRA_RENDER_FLAG_BLIT_MASK1,
                   0, 0, 5, 1,
                   XRGB_DEST_DESCRIPTOR_OFFSET,
                   PALETTE_SOURCE_DESCRIPTOR_OFFSET,
                   MASK_DESCRIPTOR_OFFSET,
                   5, 6, 4, 0, 1u, 1u, 1u, 1u, 0x80000000u) != 0)
        return -1;
    return 0;
}

static int write_virtual_sprite_group_commands(const struct render_maps *maps)
{
    unsigned item;

    for (item = 0u; item < VIRTUAL_SPRITE_GROUP_COUNT; ++item) {
        unsigned command = VIRTUAL_SPRITE_GROUP_FIRST + item;
        int16_t x = (int16_t)(VIRTUAL_SPRITE_X_ORIGIN +
            (item % VIRTUAL_SPRITE_COLUMNS) * VIRTUAL_SPRITE_X_STRIDE);
        int16_t y = (int16_t)(VIRTUAL_SPRITE_Y_ORIGIN +
            (item / VIRTUAL_SPRITE_COLUMNS) * VIRTUAL_SPRITE_Y_STRIDE);

        if (write_blit(maps, command, 0u,
                       0, 0, ASTRA_FRAMEBUFFER_WIDTH,
                       ASTRA_FRAMEBUFFER_HEIGHT,
                       FRAME_DESCRIPTOR_OFFSET,
                       RGB565_SOURCE_DESCRIPTOR_OFFSET, 0u,
                       0, 0, x, y, 2u, 1u,
                       VIRTUAL_SPRITE_WIDTH, VIRTUAL_SPRITE_HEIGHT,
                       0u) != 0)
            return -1;
    }
    return 0;
}

static int prepare_flood_topology(struct render_maps *maps)
{
    volatile uint8_t *destination = scratch_address(
        maps, FLOOD_DATA_OFFSET, GEOMETRY_SURFACE_BYTES);
    unsigned y;

    if (destination == NULL)
        return -1;
    clear_record(destination, GEOMETRY_SURFACE_BYTES);
    for (y = 2u; y <= 8u; ++y) {
        unsigned x;

        for (x = 2u; x <= 10u; ++x)
            destination[y * 16u + x] = 1u;
    }
    destination[4u * 16u + 5u] = 2u;
    destination[4u * 16u + 6u] = 2u;
    destination[5u * 16u + 5u] = 2u;
    return 0;
}

static int write_geometry_commands(const struct render_maps *maps)
{
    if (write_geometry(maps, GEOMETRY_COMMAND_FIRST,
                       ASTRA_RENDER_OP_LINE, 0u,
                       1, 1, 4, 1, 0u, 0u, 0u, 0u, 0x21u) != 0 ||
        write_geometry(maps, GEOMETRY_COMMAND_FIRST + 1u,
                       ASTRA_RENDER_OP_RECT,
                       ASTRA_RENDER_GEOMETRY_FLAG_FILLED,
                       2, 3, 3, 4, 0u, 0u, 0u, 0u, 0x32u) != 0 ||
        write_geometry(maps, GEOMETRY_COMMAND_FIRST + 2u,
                       ASTRA_RENDER_OP_CIRCLE, 0u,
                       8, 8, 0, 0, 2u, 0u, 0u, 0u, 0x43u) != 0 ||
        write_geometry(maps, GEOMETRY_COMMAND_FIRST + 3u,
                       ASTRA_RENDER_OP_ELLIPSE, 0u,
                       8, 8, 0, 0, 3u, 2u, 0u, 0u, 0x54u) != 0 ||
        write_geometry(maps, GEOMETRY_COMMAND_FIRST + 4u,
                       ASTRA_RENDER_OP_PATTERN_FILL,
                       ASTRA_RENDER_GEOMETRY_FLAG_PATTERN_OPAQUE,
                       12, 12, 13, 13, 0u, 0u, UINT64_MAX,
                       0x65u, 0x76u) != 0 ||
        write_flood(maps, GEOMETRY_COMMAND_FIRST + 5u) != 0)
        return -1;
    return 0;
}

static int prepare_glyph_workload(struct render_maps *maps)
{
    volatile uint8_t *mask = scratch_address(
        maps, GLYPH_MASK_DATA_OFFSET, 8u);
    volatile uint8_t *a4 = scratch_address(
        maps, GLYPH_A4_DATA_OFFSET, 32u);
    volatile uint8_t *a8 = scratch_address(
        maps, GLYPH_A8_DATA_OFFSET, 64u);
    volatile uint8_t *index4 = scratch_address(
        maps, GLYPH_INDEX4_DATA_OFFSET, 32u);
    volatile uint8_t *index8 = scratch_address(
        maps, GLYPH_INDEX8_DATA_OFFSET, 64u);
    volatile uint8_t *rgb = scratch_address(
        maps, GLYPH_RGB_DEST_DATA_OFFSET, 128u);
    volatile uint8_t *xrgb = scratch_address(
        maps, GLYPH_XRGB_DEST_DATA_OFFSET, 256u);
    volatile uint8_t *indexed = scratch_address(
        maps, GLYPH_INDEX_DEST_DATA_OFFSET, 64u);
    volatile uint8_t *palette4 = scratch_address(
        maps, GLYPH_INDEX4_PALETTE_OFFSET, 16u * 4u);
    volatile uint8_t *palette8 = scratch_address(
        maps, GLYPH_INDEX8_PALETTE_OFFSET, 256u * 4u);

    if (mask == NULL || a4 == NULL || a8 == NULL || index4 == NULL ||
        index8 == NULL || rgb == NULL || xrgb == NULL || indexed == NULL ||
        palette4 == NULL || palette8 == NULL)
        return -1;

    clear_record(mask, 8u);
    clear_record(a4, 32u);
    clear_record(a8, 64u);
    clear_record(index4, 32u);
    clear_record(index8, 64u);
    clear_record(rgb, 128u);
    clear_record(xrgb, 256u);
    clear_record(indexed, 64u);
    clear_record(palette4, 16u * 4u);
    clear_record(palette8, 256u * 4u);

    mask[0] = 0x80u;
    a4[0] = 0xf8u;
    a8[0] = 0xffu;
    a8[1] = 0x80u;
    index4[0] = 0x12u;
    index8[0] = 0x2au;
    index8[1] = 0x00u;
    store_be32(palette4 + 4u, 0xffff0000u);
    store_be32(palette4 + 8u, 0xff00ff00u);
    store_be32(palette8 + 0x2au * 4u, 0xff55aaeeu);

    /* Blue underneath the A8 samples proves destination read/blend/write. */
    store_be32(xrgb + 0u, 0xff0000ffu);
    store_be32(xrgb + 4u, 0xff0000ffu);
    indexed[0] = 0x55u;
    indexed[1] = 0x66u;
    rgb[3u * 32u] = 0x12u;
    rgb[3u * 32u + 1u] = 0x34u;

    if (write_surface(maps, GLYPH_MASK_DESCRIPTOR_OFFSET,
                      GLYPH_MASK_DATA_OFFSET, 8u, 2u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_MASK1,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, GLYPH_A4_DESCRIPTOR_OFFSET,
                      GLYPH_A4_DATA_OFFSET, 32u, 8u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_A4,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, GLYPH_A8_DESCRIPTOR_OFFSET,
                      GLYPH_A8_DATA_OFFSET, 64u, 16u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_A8,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, GLYPH_INDEX4_DESCRIPTOR_OFFSET,
                      GLYPH_INDEX4_DATA_OFFSET, 32u, 8u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_INDEX4,
                      ASTRA_RENDER_SURFACE_READ,
                      GLYPH_INDEX4_PALETTE_OFFSET) != 0 ||
        write_surface(maps, GLYPH_INDEX8_DESCRIPTOR_OFFSET,
                      GLYPH_INDEX8_DATA_OFFSET, 64u, 16u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ,
                      GLYPH_INDEX8_PALETTE_OFFSET) != 0 ||
        write_surface(maps, GLYPH_RGB_DEST_DESCRIPTOR_OFFSET,
                      GLYPH_RGB_DEST_DATA_OFFSET, 128u, 32u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, GLYPH_XRGB_DEST_DESCRIPTOR_OFFSET,
                      GLYPH_XRGB_DEST_DATA_OFFSET, 256u, 64u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_XRGB8888,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, GLYPH_INDEX_DEST_DESCRIPTOR_OFFSET,
                      GLYPH_INDEX_DEST_DATA_OFFSET, 64u, 16u, 16u, 4u,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0)
        return -1;

    if (write_glyph_descriptor(maps, 0u, 0u, 0u, 0u,
                               0, 0, 2u, 1u) != 0 ||
        write_glyph_descriptor(maps, 1u, 0u, 0u, 0u,
                               0, 1, 2u, 1u) != 0 ||
        write_glyph_descriptor(maps, 2u, 0u, 0u, 0u,
                               0, 0, 2u, 1u) != 0 ||
        write_glyph_descriptor(maps, 3u, 0u, 0u, 0u,
                               0, 2, 2u, 1u) != 0 ||
        write_glyph_descriptor(maps, 4u, 0u, 0u, 0u,
                               0, 0, 2u, 1u) != 0 ||
        write_glyph_descriptor(maps, 5u, 0u, 0u, 0u,
                               0, 3, 1u, 1u) != 0 ||
        write_glyph_descriptor(maps, 6u, 0u, 0u, 0u,
                               1, 3, 0u, 1u) != 0)
        return -1;

    if (write_glyph(maps, GLYPH_COMMAND_FIRST,
                    ASTRA_RENDER_GLYPH_FLAG_OPAQUE_BACKGROUND,
                    GLYPH_RGB_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_MASK_DESCRIPTOR_OFFSET, 0u, 1u,
                    0x0000f800u, 0x0000001fu, 0u) != 0 ||
        write_glyph(maps, GLYPH_COMMAND_FIRST + 1u, 0u,
                    GLYPH_RGB_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_A4_DESCRIPTOR_OFFSET, 1u, 1u,
                    0x0000ffffu, 0u, 0u) != 0 ||
        write_glyph(maps, GLYPH_COMMAND_FIRST + 2u, 0u,
                    GLYPH_XRGB_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_A8_DESCRIPTOR_OFFSET, 2u, 1u,
                    0xffff0000u, 0u, 0u) != 0 ||
        write_glyph(maps, GLYPH_COMMAND_FIRST + 3u, 0u,
                    GLYPH_RGB_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_INDEX4_DESCRIPTOR_OFFSET, 3u, 1u,
                    0u, 0u, 2u) != 0 ||
        write_glyph(maps, GLYPH_COMMAND_FIRST + 4u, 0u,
                    GLYPH_INDEX_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_INDEX8_DESCRIPTOR_OFFSET, 4u, 1u,
                    0u, 0u, 0u) != 0 ||
        write_glyph(maps, GLYPH_FAILURE_COMMAND, 0u,
                    GLYPH_RGB_DEST_DESCRIPTOR_OFFSET,
                    GLYPH_MASK_DESCRIPTOR_OFFSET, 5u, 2u,
                    0x0000ffffu, 0u, 0u) != 0)
        return -1;
    return 0;
}

static int prepare_workload(struct render_maps *maps)
{
    unsigned y;

    clear_record(maps->queues.data, maps->queues.data_bytes);
    clear_record(maps->scratch.data, maps->scratch.data_bytes);
    for (y = 0; y < SCRATCH_HEIGHT; ++y) {
        unsigned x;

        for (x = 0; x < SCRATCH_WIDTH; ++x)
            maps->scratch.data[y * SCRATCH_PITCH + x] =
                (uint8_t)(y * SCRATCH_WIDTH + x);
    }
    if (write_surface(maps, SCRATCH_DESCRIPTOR_OFFSET,
                      SCRATCH_DATA_OFFSET, SCRATCH_BYTES,
                      SCRATCH_PITCH, SCRATCH_WIDTH, SCRATCH_HEIGHT,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, FRAME_DESCRIPTOR_OFFSET,
                      FRAME_DATA_OFFSET, ASTRA_FRAMEBUFFER_BYTES,
                      ASTRA_FRAMEBUFFER_PITCH,
                      ASTRA_FRAMEBUFFER_WIDTH, ASTRA_FRAMEBUFFER_HEIGHT,
                      ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, INDEX_SOURCE_DESCRIPTOR_OFFSET,
                      INDEX_SOURCE_DATA_OFFSET,
                      FEATURE_INDEX_PITCH * FEATURE_HEIGHT,
                      FEATURE_INDEX_PITCH, FEATURE_WIDTH, FEATURE_HEIGHT,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, PALETTE_SOURCE_DESCRIPTOR_OFFSET,
                      INDEX_SOURCE_DATA_OFFSET,
                      FEATURE_INDEX_PITCH * FEATURE_HEIGHT,
                      FEATURE_INDEX_PITCH, FEATURE_WIDTH, FEATURE_HEIGHT,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ,
                      PALETTE_DATA_OFFSET) != 0 ||
        write_surface(maps, INDEX_DEST_DESCRIPTOR_OFFSET,
                      INDEX_DEST_DATA_OFFSET,
                      FEATURE_INDEX_PITCH * FEATURE_HEIGHT,
                      FEATURE_INDEX_PITCH, FEATURE_WIDTH, FEATURE_HEIGHT,
                      ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, MASK_DESCRIPTOR_OFFSET,
                      MASK_DATA_OFFSET,
                      FEATURE_MASK_PITCH * FEATURE_HEIGHT,
                      FEATURE_MASK_PITCH, FEATURE_WIDTH, FEATURE_HEIGHT,
                      ASTRA_RENDER_FORMAT_MASK1,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, RGB565_SOURCE_DESCRIPTOR_OFFSET,
                      RGB565_SOURCE_DATA_OFFSET, 4u, 4u, 2u, 1u,
                      ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, XRGB_DEST_DESCRIPTOR_OFFSET,
                      XRGB_DEST_DATA_OFFSET, FEATURE_XRGB_PITCH,
                      FEATURE_XRGB_PITCH, 5u, 1u,
                      ASTRA_RENDER_FORMAT_XRGB8888,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, ARGB_SOURCE_DESCRIPTOR_OFFSET,
                      ARGB_SOURCE_DATA_OFFSET, 8u, 8u, 2u, 1u,
                      ASTRA_RENDER_FORMAT_ARGB8888,
                      ASTRA_RENDER_SURFACE_READ, 0u) != 0 ||
        write_surface(maps, RGB565_DEST_DESCRIPTOR_OFFSET,
                      RGB565_DEST_DATA_OFFSET, 2u, 2u, 1u, 1u,
                      ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, GEOMETRY_DESCRIPTOR_OFFSET,
                      GEOMETRY_DATA_OFFSET, GEOMETRY_SURFACE_BYTES,
                      16u, 16u, 16u, ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, FLOOD_DESCRIPTOR_OFFSET,
                      FLOOD_DATA_OFFSET, GEOMETRY_SURFACE_BYTES,
                      16u, 16u, 16u, ASTRA_RENDER_FORMAT_INDEX8,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_surface(maps, FLOOD_WORKSPACE_DESCRIPTOR_OFFSET,
                      FLOOD_WORKSPACE_DATA_OFFSET, FLOOD_WORKSPACE_BYTES,
                      64u, 16u, 16u, ASTRA_RENDER_FORMAT_XRGB8888,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        prepare_complete_blitter_data(maps) != 0 ||
        prepare_flood_topology(maps) != 0 ||
        write_overlap_blit(maps) != 0 ||
        write_fill(maps, 1u, 2u, 0, 0,
                   ASTRA_FRAMEBUFFER_WIDTH, ASTRA_FRAMEBUFFER_HEIGHT,
                   COLOR_BACKGROUND) != 0 ||
        write_fill(maps, 2u, 3u, 0, 0,
                   ASTRA_FRAMEBUFFER_WIDTH, 48u, COLOR_HEADER) != 0 ||
        write_fill(maps, 3u, 4u, 80, 160, 400u, 240u,
                   COLOR_LEFT) != 0 ||
        write_fill(maps, 4u, 5u, 800, 280, 360u, 280u,
                   COLOR_RIGHT) != 0 ||
        write_fill(maps, 5u, 6u, -40, 620, 200u, 140u,
                   COLOR_CLIPPED) != 0 ||
        write_complete_blitter_commands(maps) != 0 ||
        write_virtual_sprite_group_commands(maps) != 0 ||
        write_geometry_commands(maps) != 0 ||
        prepare_glyph_workload(maps) != 0) {
        fprintf(stderr, "render workload layout is invalid\n");
        return -1;
    }
    astra_graphics_memory_barrier();
    return 0;
}

static int wait_for_idle(const struct astra_graphics_device *device,
                         uint64_t timeout_ns)
{
    uint64_t deadline = deadline_after(timeout_ns);

    for (;;) {
        uint32_t status = astra_mmio_read(device, ASTRA_REG_RENDER_STATUS);

        if ((status & (ASTRA_RENDER_ENGINE_BUSY |
                       ASTRA_RENDER_ENGINE_RESET_ACTIVE)) == 0u)
            return 0;
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr, "render engine did not become idle: "
                    "status=%08" PRIx32 " fault=%08" PRIx32 "\n",
                    status,
                    astra_mmio_read(device, ASTRA_REG_RENDER_LAST_FAULT));
            return -1;
        }
        if (sleep_nanoseconds(1000000L) != 0)
            return -1;
    }
}

static void capture_counters(const struct astra_graphics_device *device,
                             struct engine_counters *counters)
{
    counters->submitted = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMMANDS_SUBMITTED);
    counters->completed = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMMANDS_COMPLETED);
    counters->failed = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMMANDS_FAILED);
    counters->backpressure = astra_mmio_read(
        device, ASTRA_REG_RENDER_BACKPRESSURE_CYCLES);
    counters->timeouts = astra_mmio_read(
        device, ASTRA_REG_RENDER_TIMEOUT_COUNT);
    counters->resets = astra_mmio_read(
        device, ASTRA_REG_RENDER_RESET_COUNT);
}

static int configure_engine(const struct astra_graphics_device *device)
{
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_SOFT_RESET);
    if (wait_for_idle(device, ENGINE_TIMEOUT_NS) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_CONSUMER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET,
                     SUBMISSION_RING_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET,
                     COMPLETION_RING_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_RESOURCE_GENERATION,
                     RESOURCE_GENERATION);
    astra_mmio_write(device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_REBASE);
    if (astra_mmio_read(device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) != 0u ||
        astra_mmio_read(device, ASTRA_REG_RENDER_COMPLETION_PRODUCER) != 0u ||
        astra_mmio_read(device, ASTRA_REG_RENDER_RESOURCE_GENERATION) !=
            RESOURCE_GENERATION) {
        fprintf(stderr, "render queue rebase did not establish origin\n");
        return -1;
    }
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_ENABLE);
    return 0;
}

static int wait_for_completions(const struct astra_graphics_device *device,
                                uint32_t expected_producer)
{
    uint64_t deadline = deadline_after(COMPLETION_TIMEOUT_NS);

    for (;;) {
        uint32_t producer = astra_mmio_read(
            device, ASTRA_REG_RENDER_COMPLETION_PRODUCER);
        uint32_t status = astra_mmio_read(device, ASTRA_REG_RENDER_STATUS);

        if (producer == expected_producer)
            return 0;
        if ((status & ASTRA_RENDER_ENGINE_CONFIG_FAULT) != 0u ||
            astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "render completion wait failed: producer=%" PRIu32
                    "/%" PRIu32 " consumer=%" PRIu32
                    " status=%08" PRIx32 " fault=%08" PRIx32 "\n",
                    producer, expected_producer,
                    astra_mmio_read(device,
                        ASTRA_REG_RENDER_SUBMISSION_CONSUMER),
                    status,
                    astra_mmio_read(device, ASTRA_REG_RENDER_LAST_FAULT));
            return -1;
        }
        if (sleep_nanoseconds(1000000L) != 0)
            return -1;
    }
}

static int verify_completions(const struct render_maps *maps,
                              uint64_t *total_cycles_out)
{
    uint64_t total_cycles = 0u;
    unsigned index;

    for (index = 0; index < COMMAND_COUNT; ++index) {
        uint32_t offset = COMPLETION_RING_OFFSET +
            index * ASTRA_RENDER_COMPLETION_BYTES;
        volatile const uint8_t *completion = queue_address(
            maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
        uint32_t header;
        uint32_t opcode_status;
        uint32_t start_cycle;
        uint32_t end_cycle;

        if (completion == NULL)
            return -1;
        header = load_be32(completion + 0u);
        opcode_status = load_be32(completion + 4u);
        start_cycle = load_be32(completion + 16u);
        end_cycle = load_be32(completion + 20u);
        if (header != pair_u16(ASTRA_RENDER_ABI_VERSION,
                               ASTRA_RENDER_COMPLETION_BYTES) ||
            opcode_status != pair_u16(expected_opcode(index),
                                      ASTRA_RENDER_STATUS_OK) ||
            load_be32(completion + 8u) != index + 1u ||
            load_be32(completion + 12u) != expected_pixel_count(index) ||
            load_be32(completion + 24u) != 0u ||
            load_be32(completion + 28u) != RESOURCE_GENERATION) {
            fprintf(stderr,
                    "bad render completion[%u]: header=%08" PRIx32
                    " op/status=%08" PRIx32 " seq=%" PRIu32
                    " count=%" PRIu32 " fault=%08" PRIx32
                    " generation=%08" PRIx32 "\n",
                    index, header, opcode_status,
                    load_be32(completion + 8u),
                    load_be32(completion + 12u),
                    load_be32(completion + 24u),
                    load_be32(completion + 28u));
            return -1;
        }
        total_cycles += (uint32_t)(end_cycle - start_cycle);
    }
    *total_cycles_out = total_cycles;
    return 0;
}

static int verify_virtual_sprite_group_completions(
    const struct render_maps *maps, uint64_t *total_cycles_out)
{
    uint64_t total_cycles = 0u;
    unsigned item;

    for (item = 0u; item < VIRTUAL_SPRITE_GROUP_COUNT; ++item) {
        unsigned index = VIRTUAL_SPRITE_GROUP_FIRST + item;
        uint32_t offset = COMPLETION_RING_OFFSET +
            index * ASTRA_RENDER_COMPLETION_BYTES;
        volatile const uint8_t *completion = queue_address(
            maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
        uint32_t start_cycle;
        uint32_t end_cycle;

        if (completion == NULL)
            return -1;
        start_cycle = load_be32(completion + 16u);
        end_cycle = load_be32(completion + 20u);
        if (load_be32(completion + 0u) !=
                pair_u16(ASTRA_RENDER_ABI_VERSION,
                         ASTRA_RENDER_COMPLETION_BYTES) ||
            load_be32(completion + 4u) !=
                pair_u16(ASTRA_RENDER_OP_BLIT, ASTRA_RENDER_STATUS_OK) ||
            load_be32(completion + 8u) != index + 1u ||
            load_be32(completion + 12u) !=
                VIRTUAL_SPRITE_WIDTH * VIRTUAL_SPRITE_HEIGHT ||
            load_be32(completion + 24u) != 0u ||
            load_be32(completion + 28u) != RESOURCE_GENERATION) {
            fprintf(stderr,
                    "bad virtual-sprite completion[%u]: "
                    "op/status=%08" PRIx32 " seq=%" PRIu32
                    " count=%" PRIu32 " fault=%08" PRIx32 "\n",
                    item, load_be32(completion + 4u),
                    load_be32(completion + 8u),
                    load_be32(completion + 12u),
                    load_be32(completion + 24u));
            return -1;
        }
        total_cycles += (uint32_t)(end_cycle - start_cycle);
    }
    *total_cycles_out = total_cycles;
    return 0;
}

static int verify_geometry_completions(const struct render_maps *maps,
                                       uint64_t *total_cycles_out)
{
    static const uint16_t opcodes[GEOMETRY_COMMAND_COUNT] = {
        ASTRA_RENDER_OP_LINE,
        ASTRA_RENDER_OP_RECT,
        ASTRA_RENDER_OP_CIRCLE,
        ASTRA_RENDER_OP_ELLIPSE,
        ASTRA_RENDER_OP_PATTERN_FILL,
        ASTRA_RENDER_OP_FLOOD_FILL,
    };
    static const uint32_t exact_counts[GEOMETRY_COMMAND_COUNT] = {
        4u, 4u, UINT32_MAX, UINT32_MAX, 4u, 60u,
    };
    uint64_t total_cycles = 0u;
    unsigned item;

    for (item = 0u; item < GEOMETRY_COMMAND_COUNT; ++item) {
        unsigned index = GEOMETRY_COMMAND_FIRST + item;
        uint32_t offset = COMPLETION_RING_OFFSET +
            index * ASTRA_RENDER_COMPLETION_BYTES;
        volatile const uint8_t *completion = queue_address(
            maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
        uint32_t count;
        uint32_t start_cycle;
        uint32_t end_cycle;

        if (completion == NULL)
            return -1;
        count = load_be32(completion + 12u);
        start_cycle = load_be32(completion + 16u);
        end_cycle = load_be32(completion + 20u);
        if (load_be32(completion + 0u) !=
                pair_u16(ASTRA_RENDER_ABI_VERSION,
                         ASTRA_RENDER_COMPLETION_BYTES) ||
            load_be32(completion + 4u) !=
                pair_u16(opcodes[item], ASTRA_RENDER_STATUS_OK) ||
            load_be32(completion + 8u) != index + 1u ||
            (exact_counts[item] == UINT32_MAX ? count == 0u :
                count != exact_counts[item]) ||
            load_be32(completion + 24u) != 0u ||
            load_be32(completion + 28u) != RESOURCE_GENERATION) {
            fprintf(stderr,
                    "bad geometry completion[%u]: op/status=%08" PRIx32
                    " seq=%" PRIu32 " count=%" PRIu32
                    " fault=%08" PRIx32 "\n",
                    item, load_be32(completion + 4u),
                    load_be32(completion + 8u), count,
                    load_be32(completion + 24u));
            return -1;
        }
        total_cycles += (uint32_t)(end_cycle - start_cycle);
    }
    *total_cycles_out = total_cycles;
    return 0;
}

static int verify_geometry_pixels(const struct render_maps *maps)
{
    volatile const uint8_t *pixels = scratch_address(
        maps, GEOMETRY_DATA_OFFSET, GEOMETRY_SURFACE_BYTES);
    static const struct {
        uint8_t x;
        uint8_t y;
        uint8_t value;
    } checks[] = {
        { 1u, 1u, 0x21u }, { 4u, 1u, 0x21u },
        { 2u, 3u, 0x32u }, { 3u, 4u, 0x32u },
        { 6u, 8u, 0x43u }, { 10u, 8u, 0x43u },
        { 5u, 8u, 0x54u }, { 11u, 8u, 0x54u },
        { 12u, 12u, 0x76u }, { 13u, 13u, 0x76u },
    };
    unsigned item;

    if (pixels == NULL)
        return -1;
    for (item = 0u; item < sizeof(checks) / sizeof(checks[0]); ++item) {
        uint8_t actual = pixels[checks[item].y * 16u + checks[item].x];

        if (actual != checks[item].value) {
            fprintf(stderr,
                    "geometry mismatch x=%u y=%u expected=%02x actual=%02x\n",
                    checks[item].x, checks[item].y,
                    checks[item].value, actual);
            return -1;
        }
    }
    return 0;
}

static int verify_glyph_completions(const struct render_maps *maps,
                                    uint64_t *total_cycles_out)
{
    static const uint32_t counts[GLYPH_SUCCESS_COMMAND_COUNT] = {
        2u, 2u, 2u, 1u, 1u,
    };
    uint64_t total_cycles = 0u;
    unsigned item;

    for (item = 0u; item < GLYPH_SUCCESS_COMMAND_COUNT + 1u; ++item) {
        unsigned index = GLYPH_COMMAND_FIRST + item;
        uint32_t offset = COMPLETION_RING_OFFSET +
            index * ASTRA_RENDER_COMPLETION_BYTES;
        volatile const uint8_t *completion = queue_address(
            maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
        uint16_t expected_status = item < GLYPH_SUCCESS_COMMAND_COUNT ?
            ASTRA_RENDER_STATUS_OK : ASTRA_RENDER_STATUS_BAD_RANGE;
        uint32_t expected_count = item < GLYPH_SUCCESS_COMMAND_COUNT ?
            counts[item] : 0u;
        uint32_t start_cycle;
        uint32_t end_cycle;
        uint32_t fault;

        if (completion == NULL)
            return -1;
        start_cycle = load_be32(completion + 16u);
        end_cycle = load_be32(completion + 20u);
        fault = load_be32(completion + 24u);
        if (load_be32(completion + 0u) !=
                pair_u16(ASTRA_RENDER_ABI_VERSION,
                         ASTRA_RENDER_COMPLETION_BYTES) ||
            load_be32(completion + 4u) !=
                pair_u16(ASTRA_RENDER_OP_GLYPH_RUN, expected_status) ||
            load_be32(completion + 8u) != index + 1u ||
            load_be32(completion + 12u) != expected_count ||
            (expected_status == ASTRA_RENDER_STATUS_OK ? fault != 0u :
                                                       fault == 0u) ||
            load_be32(completion + 28u) != RESOURCE_GENERATION) {
            fprintf(stderr,
                    "bad glyph completion[%u]: op/status=%08" PRIx32
                    " seq=%" PRIu32 " count=%" PRIu32
                    " fault=%08" PRIx32 "\n",
                    item, load_be32(completion + 4u),
                    load_be32(completion + 8u),
                    load_be32(completion + 12u), fault);
            return -1;
        }
        total_cycles += (uint32_t)(end_cycle - start_cycle);
    }
    *total_cycles_out = total_cycles;
    return 0;
}

static int verify_glyph_pixels(const struct render_maps *maps)
{
    volatile const uint8_t *rgb = scratch_address(
        maps, GLYPH_RGB_DEST_DATA_OFFSET, 128u);
    volatile const uint8_t *xrgb = scratch_address(
        maps, GLYPH_XRGB_DEST_DATA_OFFSET, 256u);
    volatile const uint8_t *indexed = scratch_address(
        maps, GLYPH_INDEX_DEST_DATA_OFFSET, 64u);

    if (rgb == NULL || xrgb == NULL || indexed == NULL)
        return -1;
    if (load_be16(rgb + 0u) != 0xf800u ||
        load_be16(rgb + 2u) != 0x001fu ||
        load_be16(rgb + 32u) != 0xffffu ||
        load_be16(rgb + 34u) != 0x8c51u ||
        load_be16(rgb + 64u) != 0xf800u ||
        load_be16(rgb + 66u) != 0x0000u ||
        load_be16(rgb + 96u) != 0x1234u) {
        fprintf(stderr,
                "glyph RGB565 mismatch rows=%04x/%04x %04x/%04x "
                "%04x/%04x malformed=%04x\n",
                load_be16(rgb + 0u), load_be16(rgb + 2u),
                load_be16(rgb + 32u), load_be16(rgb + 34u),
                load_be16(rgb + 64u), load_be16(rgb + 66u),
                load_be16(rgb + 96u));
        return -1;
    }
    if (load_be32(xrgb + 0u) != 0xffff0000u ||
        load_be32(xrgb + 4u) != 0xff80007fu) {
        fprintf(stderr,
                "glyph XRGB mismatch expected=ffff0000/ff80007f "
                "actual=%08" PRIx32 "/%08" PRIx32 "\n",
                load_be32(xrgb + 0u), load_be32(xrgb + 4u));
        return -1;
    }
    if (indexed[0] != 0x2au || indexed[1] != 0x66u) {
        fprintf(stderr,
                "glyph indexed mismatch expected=2a/66 actual=%02x/%02x\n",
                indexed[0], indexed[1]);
        return -1;
    }
    return 0;
}

static int verify_flood_pixels(const struct render_maps *maps)
{
    volatile const uint8_t *pixels = scratch_address(
        maps, FLOOD_DATA_OFFSET, GEOMETRY_SURFACE_BYTES);
    unsigned y;

    if (pixels == NULL)
        return -1;
    for (y = 0u; y < 16u; ++y) {
        unsigned x;

        for (x = 0u; x < 16u; ++x) {
            bool obstacle = (x == 5u && y == 4u) ||
                (x == 6u && y == 4u) || (x == 5u && y == 5u);
            uint8_t expected = x >= 2u && x <= 10u &&
                y >= 2u && y <= 8u && !obstacle ? 7u :
                obstacle ? 2u : 0u;
            uint8_t actual = pixels[y * 16u + x];

            if (actual != expected) {
                fprintf(stderr,
                        "flood mismatch x=%u y=%u expected=%02x actual=%02x\n",
                        x, y, expected, actual);
                return -1;
            }
        }
    }
    return 0;
}

static int verify_flood_overflow_completion(const struct render_maps *maps,
                                            uint32_t *cycles_out)
{
    unsigned index = FLOOD_OVERFLOW_COMMAND;
    uint32_t offset = COMPLETION_RING_OFFSET +
        index * ASTRA_RENDER_COMPLETION_BYTES;
    volatile const uint8_t *completion = queue_address(
        maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
    uint32_t start_cycle;
    uint32_t end_cycle;

    if (completion == NULL)
        return -1;
    start_cycle = load_be32(completion + 16u);
    end_cycle = load_be32(completion + 20u);
    if (load_be32(completion + 0u) !=
            pair_u16(ASTRA_RENDER_ABI_VERSION,
                     ASTRA_RENDER_COMPLETION_BYTES) ||
        load_be32(completion + 4u) !=
            pair_u16(ASTRA_RENDER_OP_FLOOD_FILL,
                     ASTRA_RENDER_STATUS_WORK_OVERFLOW) ||
        load_be32(completion + 8u) != index + 1u ||
        load_be32(completion + 24u) == 0u ||
        load_be32(completion + 28u) != RESOURCE_GENERATION) {
        fprintf(stderr,
                "bad flood overflow completion: op/status=%08" PRIx32
                " seq=%" PRIu32 " count=%" PRIu32
                " fault=%08" PRIx32 "\n",
                load_be32(completion + 4u), load_be32(completion + 8u),
                load_be32(completion + 12u), load_be32(completion + 24u));
        return -1;
    }
    *cycles_out = (uint32_t)(end_cycle - start_cycle);
    return 0;
}

static uint8_t initial_scratch_byte(unsigned x, unsigned y)
{
    return (uint8_t)(y * SCRATCH_WIDTH + x);
}

static int verify_scratch(const struct render_maps *maps)
{
    unsigned y;

    for (y = 0; y < SCRATCH_HEIGHT; ++y) {
        unsigned x;

        for (x = 0; x < SCRATCH_WIDTH; ++x) {
            uint8_t expected_byte = initial_scratch_byte(
                x >= 8u && x < 56u ? x - 8u : x, y);
            uint8_t actual = maps->scratch.data[y * SCRATCH_PITCH + x];

            if (actual != expected_byte) {
                fprintf(stderr,
                        "overlap blit mismatch x=%u y=%u "
                        "expected=%02x actual=%02x\n",
                        x, y, expected_byte, actual);
                return -1;
            }
        }
    }
    return 0;
}

static uint8_t expected_rop8(unsigned rop, uint8_t source,
                             uint8_t destination)
{
    uint8_t not_source = (uint8_t)~source;
    uint8_t not_destination = (uint8_t)~destination;
    uint8_t result = 0u;

    if ((rop & 1u) != 0u)
        result |= not_source & not_destination;
    if ((rop & 2u) != 0u)
        result |= not_source & destination;
    if ((rop & 4u) != 0u)
        result |= source & not_destination;
    if ((rop & 8u) != 0u)
        result |= source & destination;
    return result;
}

static uint8_t expected_index_destination(unsigned x, unsigned y)
{
    if (y == 0u && x < 4u) {
        static const uint8_t keyed[4] = { 0xaau, 0x02u, 0xaau, 0x03u };

        return keyed[x];
    }
    if (y == 1u)
        return expected_rop8(x, 0x5au, 0xa5u);
    if (y >= 2u && y < 6u && x >= 1u && x < 6u) {
        unsigned source_y = 3u - (((y - 1u) * 3u) / 6u);
        unsigned source_x = 4u - (((x + 1u) * 4u) / 8u);

        return (uint8_t)(source_y * FEATURE_INDEX_PITCH + source_x);
    }
    return 0xeeu;
}

static int verify_complete_blitter(const struct render_maps *maps)
{
    static const uint32_t expected_xrgb[5] = {
        0xffff0000u,
        0xff00ff00u,
        0xff4000bfu,
        0xff4000bfu,
        0xff0000ffu,
    };
    volatile const uint8_t *index_destination = scratch_address(
        maps, INDEX_DEST_DATA_OFFSET,
        FEATURE_INDEX_PITCH * FEATURE_HEIGHT);
    volatile const uint8_t *xrgb_destination = scratch_address(
        maps, XRGB_DEST_DATA_OFFSET, FEATURE_XRGB_PITCH);
    volatile const uint8_t *rgb565_destination = scratch_address(
        maps, RGB565_DEST_DATA_OFFSET, 2u);
    unsigned y;

    if (index_destination == NULL || xrgb_destination == NULL ||
        rgb565_destination == NULL)
        return -1;

    for (y = 0u; y < FEATURE_HEIGHT; ++y) {
        unsigned x;

        for (x = 0u; x < FEATURE_WIDTH; ++x) {
            uint8_t expected = expected_index_destination(x, y);
            uint8_t actual = index_destination[
                y * FEATURE_INDEX_PITCH + x];

            if (actual != expected) {
                fprintf(stderr,
                        "complete INDEX8 mismatch x=%u y=%u "
                        "expected=%02x actual=%02x\n",
                        x, y, expected, actual);
                return -1;
            }
        }
    }
    for (y = 0u; y < 5u; ++y) {
        uint32_t actual = load_be32(xrgb_destination + y * 4u);

        if (actual != expected_xrgb[y]) {
            fprintf(stderr,
                    "complete XRGB mismatch x=%u expected=%08" PRIx32
                    " actual=%08" PRIx32 "\n",
                    y, expected_xrgb[y], actual);
            return -1;
        }
    }
    if (rgb565_destination[0] != 0x41u ||
        rgb565_destination[1] != 0x02u) {
        fprintf(stderr,
                "complete RGB565 mismatch expected=4102 actual=%02x%02x\n",
                rgb565_destination[0], rgb565_destination[1]);
        return -1;
    }
    return 0;
}

static uint16_t expected_frame_pixel(unsigned x, unsigned y)
{
    uint16_t color = COLOR_BACKGROUND;

    if (y < 48u)
        color = COLOR_HEADER;
    if (x >= 80u && x < 480u && y >= 160u && y < 400u)
        color = COLOR_LEFT;
    if (x >= 800u && x < 1160u && y >= 280u && y < 560u)
        color = COLOR_RIGHT;
    if (x < 160u && y >= 620u)
        color = COLOR_CLIPPED;
    return color;
}

static int verify_frame(const struct render_maps *maps)
{
    unsigned y;

    for (y = 0; y < ASTRA_FRAMEBUFFER_HEIGHT; ++y) {
        unsigned x;

        for (x = 0; x < ASTRA_FRAMEBUFFER_WIDTH; ++x) {
            size_t offset = (size_t)y * ASTRA_FRAMEBUFFER_PITCH +
                (size_t)x * 2u;
            uint16_t expected_pixel = expected_frame_pixel(x, y);
            uint16_t actual = ((uint16_t)maps->frame.data[offset] << 8) |
                maps->frame.data[offset + 1u];

            if (actual != expected_pixel) {
                fprintf(stderr,
                        "RGB565 fill mismatch x=%u y=%u "
                        "expected=%04x actual=%04x\n",
                        x, y, expected_pixel, actual);
                return -1;
            }
        }
    }
    return 0;
}

static uint16_t screen_offset_pixel(unsigned x, unsigned y)
{
    uint32_t value = x * 0x0421u + y * 0x1f3du + (x >> 8) * 0x0101u;

    return (uint16_t)(value ^ (x << 7) ^ (y << 11));
}

static int prepare_screen_offset_workload(struct render_maps *maps)
{
    unsigned y;

    for (y = 0u; y < ASTRA_FRAMEBUFFER_HEIGHT; ++y) {
        unsigned x;

        for (x = 0u; x < ASTRA_FRAMEBUFFER_WIDTH; ++x) {
            size_t offset = (size_t)y * ASTRA_FRAMEBUFFER_PITCH + x * 2u;

            store_be16(maps->frame.data + offset, screen_offset_pixel(x, y));
        }
    }
    if (write_blit(maps, SCREEN_OFFSET_COMMAND, 0u,
                   0, 0, ASTRA_FRAMEBUFFER_WIDTH, ASTRA_FRAMEBUFFER_HEIGHT,
                   FRAME_DESCRIPTOR_OFFSET, FRAME_DESCRIPTOR_OFFSET, 0u,
                   0, SCREEN_OFFSET_SOURCE_Y, 0, 0,
                   ASTRA_FRAMEBUFFER_WIDTH, SCREEN_OFFSET_HEIGHT,
                   ASTRA_FRAMEBUFFER_WIDTH, SCREEN_OFFSET_HEIGHT, 0u) != 0)
        return -1;
    astra_graphics_memory_barrier();
    return 0;
}

static int verify_screen_offset(const struct render_maps *maps,
                                uint32_t *cycles_out)
{
    uint32_t offset = COMPLETION_RING_OFFSET +
        SCREEN_OFFSET_COMMAND * ASTRA_RENDER_COMPLETION_BYTES;
    volatile const uint8_t *completion = queue_address(
        maps, offset, ASTRA_RENDER_COMPLETION_BYTES);
    unsigned y;

    if (completion == NULL ||
        load_be32(completion + 4u) !=
            pair_u16(ASTRA_RENDER_OP_BLIT, ASTRA_RENDER_STATUS_OK) ||
        load_be32(completion + 8u) != SCREEN_OFFSET_COMMAND_END ||
        load_be32(completion + 12u) !=
            ASTRA_FRAMEBUFFER_WIDTH * SCREEN_OFFSET_HEIGHT ||
        load_be32(completion + 24u) != 0u) {
        fprintf(stderr, "screen-offset completion is invalid\n");
        return -1;
    }
    *cycles_out = load_be32(completion + 20u) -
        load_be32(completion + 16u);
    for (y = 0u; y < ASTRA_FRAMEBUFFER_HEIGHT; ++y) {
        unsigned source_y = y < SCREEN_OFFSET_HEIGHT ?
            y + SCREEN_OFFSET_SOURCE_Y : y;
        unsigned x;

        for (x = 0u; x < ASTRA_FRAMEBUFFER_WIDTH; ++x) {
            size_t pixel_offset = (size_t)y * ASTRA_FRAMEBUFFER_PITCH + x * 2u;
            uint16_t expected = screen_offset_pixel(x, source_y);
            uint16_t actual = load_be16(maps->frame.data + pixel_offset);

            if (actual != expected) {
                fprintf(stderr,
                        "screen offset x=%u y=%u expected=%04x actual=%04x\n",
                        x, y, expected, actual);
                return -1;
            }
        }
    }
    return 0;
}

static int verify_virtual_sprite_group(const struct render_maps *maps)
{
    unsigned item;

    for (item = 0u; item < VIRTUAL_SPRITE_GROUP_COUNT; ++item) {
        unsigned origin_x = VIRTUAL_SPRITE_X_ORIGIN +
            (item % VIRTUAL_SPRITE_COLUMNS) * VIRTUAL_SPRITE_X_STRIDE;
        unsigned origin_y = VIRTUAL_SPRITE_Y_ORIGIN +
            (item / VIRTUAL_SPRITE_COLUMNS) * VIRTUAL_SPRITE_Y_STRIDE;
        unsigned y;

        for (y = 0u; y < VIRTUAL_SPRITE_HEIGHT; ++y) {
            unsigned x;

            for (x = 0u; x < VIRTUAL_SPRITE_WIDTH; ++x) {
                size_t offset = (size_t)(origin_y + y) *
                    ASTRA_FRAMEBUFFER_PITCH + (size_t)(origin_x + x) * 2u;
                uint16_t expected = x < VIRTUAL_SPRITE_WIDTH / 2u ?
                    0xf800u : 0x07e0u;
                uint16_t actual =
                    ((uint16_t)maps->frame.data[offset] << 8) |
                    maps->frame.data[offset + 1u];

                if (actual != expected) {
                    fprintf(stderr,
                            "virtual sprite mismatch item=%u x=%u y=%u "
                            "expected=%04x actual=%04x\n",
                            item, x, y, expected, actual);
                    return -1;
                }
            }
        }
    }
    return 0;
}

static void save_scene(const struct astra_graphics_device *device,
                       struct scene_state *scene)
{
    scene->global_control = astra_mmio_read(device,
                                            ASTRA_REG_GLOBAL_CONTROL);
    scene->backdrop = astra_mmio_read(device, ASTRA_REG_BACKDROP);
    scene->framebuffer_base = astra_mmio_read(device, ASTRA_REG_FB_BASE);
    scene->framebuffer_pitch = astra_mmio_read(device, ASTRA_REG_FB_PITCH);
    scene->framebuffer_size = astra_mmio_read(device, ASTRA_REG_FB_SIZE);
    scene->viewport_x = astra_mmio_read(device, ASTRA_REG_FB_VIEWPORT_X);
    scene->viewport_y = astra_mmio_read(device, ASTRA_REG_FB_VIEWPORT_Y);
    scene->framebuffer_control = astra_mmio_read(device,
                                                 ASTRA_REG_FB_CONTROL);
    scene->framebuffer_key = astra_mmio_read(device, ASTRA_REG_FB_KEY);
    scene->tile0_control = astra_mmio_read(device, ASTRA_REG_TILE0_CONTROL);
    scene->tile1_control = astra_mmio_read(device, ASTRA_REG_TILE1_CONTROL);
    scene->sprite_control = astra_mmio_read(device,
                                            ASTRA_REG_SPRITE_CONTROL);
}

static void write_scene(const struct astra_graphics_device *device,
                        const struct scene_state *scene)
{
    astra_mmio_write(device, ASTRA_REG_BACKDROP, scene->backdrop);
    astra_mmio_write(device, ASTRA_REG_FB_BASE, scene->framebuffer_base);
    astra_mmio_write(device, ASTRA_REG_FB_PITCH, scene->framebuffer_pitch);
    astra_mmio_write(device, ASTRA_REG_FB_SIZE, scene->framebuffer_size);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_X, scene->viewport_x);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_Y, scene->viewport_y);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL,
                     scene->framebuffer_control);
    astra_mmio_write(device, ASTRA_REG_FB_KEY, scene->framebuffer_key);
    astra_mmio_write(device, ASTRA_REG_TILE0_CONTROL, scene->tile0_control);
    astra_mmio_write(device, ASTRA_REG_TILE1_CONTROL, scene->tile1_control);
    astra_mmio_write(device, ASTRA_REG_SPRITE_CONTROL,
                     scene->sprite_control);
    astra_mmio_write(device, ASTRA_REG_GLOBAL_CONTROL,
                     scene->global_control);
}

static int present_result(const struct astra_graphics_device *device,
                          unsigned milliseconds)
{
    struct scene_state saved;
    struct scene_state certification;
    uint32_t generation;
    int result = -1;

    save_scene(device, &saved);
    certification = saved;
    certification.global_control = 1u;
    certification.backdrop = 0u;
    certification.framebuffer_base =
        ASTRA_GRAPHICS_ARENA_BASE + FRAME_DATA_OFFSET;
    certification.framebuffer_pitch = ASTRA_FRAMEBUFFER_PITCH;
    certification.framebuffer_size =
        (ASTRA_FRAMEBUFFER_HEIGHT << 16) | ASTRA_FRAMEBUFFER_WIDTH;
    certification.viewport_x = 0u;
    certification.viewport_y = 0u;
    certification.framebuffer_control = 3u;
    certification.framebuffer_key = 0u;
    certification.tile0_control = 0u;
    certification.tile1_control = 0u;
    certification.sprite_control = 0u;
    write_scene(device, &certification);
    if (astra_graphics_scene_commit(device, ENGINE_TIMEOUT_NS,
                                    &generation) != 0)
        goto restore;
    printf("ASTRA_RENDER_PRESENT generation=%" PRIu32
           " milliseconds=%u\n", generation, milliseconds);
    if (sleep_milliseconds(milliseconds) != 0)
        goto restore;
    result = 0;

restore:
    write_scene(device, &saved);
    if (astra_graphics_scene_commit(device, ENGINE_TIMEOUT_NS,
                                    &generation) != 0)
        result = -1;
    else
        printf("ASTRA_RENDER_RESTORE generation=%" PRIu32 "\n",
               generation);
    return result;
}

static int parse_present_milliseconds(int argc, char **argv,
                                      unsigned *milliseconds)
{
    char *end = NULL;
    unsigned long parsed;

    *milliseconds = 0u;
    if (argc == 1)
        return 0;
    if (argc != 3 || strcmp(argv[1], "--present-ms") != 0) {
        fprintf(stderr, "usage: %s [--present-ms 1..60000]\n", argv[0]);
        return -1;
    }
    errno = 0;
    parsed = strtoul(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' ||
        parsed == 0u || parsed > 60000u) {
        fprintf(stderr, "invalid presentation duration: %s\n", argv[2]);
        return -1;
    }
    *milliseconds = (unsigned)parsed;
    return 0;
}

static void init_maps(struct render_maps *maps)
{
    astra_graphics_memory_map_init(&maps->queues);
    astra_graphics_memory_map_init(&maps->scratch);
    astra_graphics_memory_map_init(&maps->frame);
}

static void close_maps(struct render_maps *maps)
{
    astra_graphics_memory_map_close(&maps->frame);
    astra_graphics_memory_map_close(&maps->scratch);
    astra_graphics_memory_map_close(&maps->queues);
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct render_maps maps;
    struct engine_counters before;
    struct engine_counters after;
    struct engine_counters group_before;
    struct engine_counters group_after;
    struct engine_counters geometry_before;
    struct engine_counters geometry_after;
    struct engine_counters glyph_before;
    struct engine_counters glyph_after;
    struct engine_counters overflow_before;
    struct engine_counters overflow_after;
    uint64_t command_cycles;
    uint64_t group_cycles;
    uint64_t geometry_cycles;
    uint64_t glyph_cycles;
    uint32_t overflow_cycles;
    uint32_t screen_offset_cycles;
    unsigned present_milliseconds;
    uint32_t capabilities;
    uint32_t status;
    int result = EXIT_FAILURE;

    if (parse_present_milliseconds(argc, argv, &present_milliseconds) != 0)
        return EXIT_FAILURE;
    astra_graphics_device_init(&device);
    init_maps(&maps);
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, false) != 0)
        goto done;
    capabilities = astra_mmio_read(&device, ASTRA_REG_CAPABILITIES);
    if ((capabilities & ASTRA_CAP_RENDER_ENGINE) == 0u) {
        fprintf(stderr, "Astra render engine is not present\n");
        goto done;
    }
    if (astra_mmio_read(&device, ASTRA_REG_ARENA_BASE) !=
            ASTRA_GRAPHICS_ARENA_BASE ||
        astra_mmio_read(&device, ASTRA_REG_ARENA_LIMIT) !=
            ASTRA_GRAPHICS_ARENA_LIMIT) {
        fprintf(stderr, "graphics arena does not match render contract\n");
        goto done;
    }
    if (astra_graphics_memory_map_open(
            &device, &maps.queues,
            ASTRA_GRAPHICS_ARENA_BASE + SUBMISSION_RING_OFFSET,
            QUEUE_REGION_BYTES) != 0) {
        perror("map render queues and descriptors");
        goto done;
    }
    if (astra_graphics_memory_map_open(
            &device, &maps.scratch,
            ASTRA_GRAPHICS_ARENA_BASE + SCRATCH_DATA_OFFSET,
            SCRATCH_REGION_BYTES) != 0) {
        perror("map render scratch surface");
        goto done;
    }
    if (astra_graphics_memory_map_open(
            &device, &maps.frame,
            ASTRA_GRAPHICS_ARENA_BASE + FRAME_DATA_OFFSET,
            ASTRA_FRAMEBUFFER_BYTES) != 0) {
        perror("map render certification framebuffer");
        goto done;
    }
    if (prepare_workload(&maps) != 0)
        goto done;
    if (configure_engine(&device) != 0)
        goto done;
    capture_counters(&device, &before);
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     COMMAND_COUNT);
    if (wait_for_completions(&device, COMMAND_COUNT) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_completions(&maps, &command_cycles) != 0 ||
        verify_scratch(&maps) != 0 ||
        verify_complete_blitter(&maps) != 0 || verify_frame(&maps) != 0)
        goto stop_engine;
    capture_counters(&device, &after);
    status = astra_mmio_read(&device, ASTRA_REG_RENDER_STATUS);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            COMMAND_COUNT ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            COMMAND_COUNT ||
        after.submitted - before.submitted != COMMAND_COUNT ||
        after.completed - before.completed != COMMAND_COUNT ||
        after.failed != before.failed || after.timeouts != before.timeouts ||
        after.resets != before.resets ||
        (status & (ASTRA_RENDER_ENGINE_BUSY |
                   ASTRA_RENDER_ENGINE_RESET_ACTIVE |
                   ASTRA_RENDER_ENGINE_CONFIG_FAULT)) != 0u ||
        (status & (ASTRA_RENDER_ENGINE_ENABLED |
                   ASTRA_RENDER_ENGINE_IRQ_PENDING)) !=
            (ASTRA_RENDER_ENGINE_ENABLED |
             ASTRA_RENDER_ENGINE_IRQ_PENDING)) {
        fprintf(stderr,
                "render accounting failed: status=%08" PRIx32
                " submitted=%" PRIu32 " completed=%" PRIu32
                " failed=%" PRIu32 " timeout=%" PRIu32
                " resets=%" PRIu32 " fence=%" PRIu32
                " sub_consumer=%" PRIu32 " fault=%08" PRIx32 "\n",
                status, after.submitted - before.submitted,
                after.completed - before.completed,
                after.failed - before.failed,
                after.timeouts - before.timeouts,
                after.resets - before.resets,
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE),
                astra_mmio_read(&device,
                    ASTRA_REG_RENDER_SUBMISSION_CONSUMER),
                astra_mmio_read(&device, ASTRA_REG_RENDER_LAST_FAULT));
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     COMMAND_COUNT);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_IRQ_PENDING) != 0u) {
        fprintf(stderr, "render completion interrupt did not acknowledge\n");
        goto stop_engine;
    }
    printf("ASTRA_RENDER_COMPLETE PASS commands=%u pixels=%" PRIu32
           " feature_pixels=%u"
           " cycles=%" PRIu64 " backpressure=%" PRIu32
           " scratch_region=%u frame=%u\n",
           COMMAND_COUNT, expected_total_pixels(), FEATURE_PIXEL_COUNT,
           command_cycles, after.backpressure - before.backpressure,
           SCRATCH_REGION_BYTES, ASTRA_FRAMEBUFFER_BYTES);
    capture_counters(&device, &group_before);
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     TOTAL_COMMAND_COUNT);
    if (wait_for_completions(&device, TOTAL_COMMAND_COUNT) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_virtual_sprite_group_completions(&maps, &group_cycles) != 0 ||
        verify_virtual_sprite_group(&maps) != 0)
        goto stop_engine;
    capture_counters(&device, &group_after);
    status = astra_mmio_read(&device, ASTRA_REG_RENDER_STATUS);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            TOTAL_COMMAND_COUNT ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            TOTAL_COMMAND_COUNT ||
        group_after.submitted - group_before.submitted !=
            VIRTUAL_SPRITE_GROUP_COUNT ||
        group_after.completed - group_before.completed !=
            VIRTUAL_SPRITE_GROUP_COUNT ||
        group_after.failed != group_before.failed ||
        group_after.timeouts != group_before.timeouts ||
        group_after.resets != group_before.resets ||
        (status & (ASTRA_RENDER_ENGINE_BUSY |
                   ASTRA_RENDER_ENGINE_RESET_ACTIVE |
                   ASTRA_RENDER_ENGINE_CONFIG_FAULT)) != 0u) {
        fprintf(stderr,
                "virtual-sprite group accounting failed: "
                "status=%08" PRIx32 " submitted=%" PRIu32
                " completed=%" PRIu32 " failed=%" PRIu32
                " timeout=%" PRIu32 " resets=%" PRIu32
                " fence=%" PRIu32 "\n",
                status,
                group_after.submitted - group_before.submitted,
                group_after.completed - group_before.completed,
                group_after.failed - group_before.failed,
                group_after.timeouts - group_before.timeouts,
                group_after.resets - group_before.resets,
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE));
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     TOTAL_COMMAND_COUNT);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_IRQ_PENDING) != 0u) {
        fprintf(stderr,
                "virtual-sprite completion interrupt did not acknowledge\n");
        goto stop_engine;
    }
    printf("ASTRA_VIRTUAL_SPRITES PASS commands=%u pixels=%u"
           " fence=%u cycles=%" PRIu64 " backpressure=%" PRIu32 "\n",
           VIRTUAL_SPRITE_GROUP_COUNT,
           VIRTUAL_SPRITE_GROUP_COUNT * VIRTUAL_SPRITE_WIDTH *
               VIRTUAL_SPRITE_HEIGHT,
           TOTAL_COMMAND_COUNT, group_cycles,
           group_after.backpressure - group_before.backpressure);
    capture_counters(&device, &geometry_before);
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     GEOMETRY_COMMAND_END);
    if (wait_for_completions(&device, GEOMETRY_COMMAND_END) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_geometry_completions(&maps, &geometry_cycles) != 0 ||
        verify_geometry_pixels(&maps) != 0 ||
        verify_flood_pixels(&maps) != 0)
        goto stop_engine;
    capture_counters(&device, &geometry_after);
    status = astra_mmio_read(&device, ASTRA_REG_RENDER_STATUS);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            GEOMETRY_COMMAND_END ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            GEOMETRY_COMMAND_END ||
        geometry_after.submitted - geometry_before.submitted !=
            GEOMETRY_COMMAND_COUNT ||
        geometry_after.completed - geometry_before.completed !=
            GEOMETRY_COMMAND_COUNT ||
        geometry_after.failed != geometry_before.failed ||
        geometry_after.timeouts != geometry_before.timeouts ||
        geometry_after.resets != geometry_before.resets ||
        (status & (ASTRA_RENDER_ENGINE_BUSY |
                   ASTRA_RENDER_ENGINE_RESET_ACTIVE |
                   ASTRA_RENDER_ENGINE_CONFIG_FAULT)) != 0u) {
        fprintf(stderr,
                "geometry accounting failed: status=%08" PRIx32
                " submitted=%" PRIu32 " completed=%" PRIu32
                " failed=%" PRIu32 " timeout=%" PRIu32
                " resets=%" PRIu32 " fence=%" PRIu32 "\n",
                status,
                geometry_after.submitted - geometry_before.submitted,
                geometry_after.completed - geometry_before.completed,
                geometry_after.failed - geometry_before.failed,
                geometry_after.timeouts - geometry_before.timeouts,
                geometry_after.resets - geometry_before.resets,
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE));
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     GEOMETRY_COMMAND_END);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_IRQ_PENDING) != 0u) {
        fprintf(stderr, "geometry completion interrupt did not acknowledge\n");
        goto stop_engine;
    }
    printf("ASTRA_GEOMETRY PASS commands=%u flood_pixels=60"
           " fence=%u cycles=%" PRIu64 " backpressure=%" PRIu32 "\n",
           GEOMETRY_COMMAND_COUNT, GEOMETRY_COMMAND_END, geometry_cycles,
           geometry_after.backpressure - geometry_before.backpressure);

    capture_counters(&device, &glyph_before);
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     GLYPH_COMMAND_END);
    if (wait_for_completions(&device, GLYPH_COMMAND_END) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_glyph_completions(&maps, &glyph_cycles) != 0 ||
        verify_glyph_pixels(&maps) != 0)
        goto stop_engine;
    capture_counters(&device, &glyph_after);
    status = astra_mmio_read(&device, ASTRA_REG_RENDER_STATUS);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            GLYPH_COMMAND_END ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            GLYPH_FAILURE_COMMAND ||
        glyph_after.submitted - glyph_before.submitted !=
            GLYPH_SUCCESS_COMMAND_COUNT + 1u ||
        glyph_after.completed - glyph_before.completed !=
            GLYPH_SUCCESS_COMMAND_COUNT + 1u ||
        glyph_after.failed - glyph_before.failed != 1u ||
        glyph_after.timeouts != glyph_before.timeouts ||
        glyph_after.resets != glyph_before.resets ||
        (status & (ASTRA_RENDER_ENGINE_BUSY |
                   ASTRA_RENDER_ENGINE_RESET_ACTIVE |
                   ASTRA_RENDER_ENGINE_CONFIG_FAULT)) != 0u) {
        fprintf(stderr,
                "glyph accounting failed: status=%08" PRIx32
                " submitted=%" PRIu32 " completed=%" PRIu32
                " failed=%" PRIu32 " timeout=%" PRIu32
                " resets=%" PRIu32 " fence=%" PRIu32 "\n",
                status,
                glyph_after.submitted - glyph_before.submitted,
                glyph_after.completed - glyph_before.completed,
                glyph_after.failed - glyph_before.failed,
                glyph_after.timeouts - glyph_before.timeouts,
                glyph_after.resets - glyph_before.resets,
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE));
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     GLYPH_COMMAND_END);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_IRQ_PENDING) != 0u) {
        fprintf(stderr, "glyph completion interrupt did not acknowledge\n");
        goto stop_engine;
    }
    printf("ASTRA_AFNT PASS formats=5 success=%u rejected=1"
           " fence=%u cycles=%" PRIu64 " backpressure=%" PRIu32 "\n",
           GLYPH_SUCCESS_COMMAND_COUNT, GLYPH_FAILURE_COMMAND,
           glyph_cycles,
           glyph_after.backpressure - glyph_before.backpressure);

    if (prepare_flood_topology(&maps) != 0 ||
        write_surface(&maps, FLOOD_WORKSPACE_DESCRIPTOR_OFFSET,
                      FLOOD_WORKSPACE_DATA_OFFSET, 4u,
                      4u, 1u, 1u, ASTRA_RENDER_FORMAT_XRGB8888,
                      ASTRA_RENDER_SURFACE_READ |
                      ASTRA_RENDER_SURFACE_WRITE, 0u) != 0 ||
        write_flood(&maps, FLOOD_OVERFLOW_COMMAND) != 0) {
        fprintf(stderr, "flood overflow workload layout is invalid\n");
        goto stop_engine;
    }
    astra_graphics_memory_barrier();
    capture_counters(&device, &overflow_before);
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     CERTIFICATION_COMMAND_END);
    if (wait_for_completions(&device, CERTIFICATION_COMMAND_END) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_flood_overflow_completion(&maps, &overflow_cycles) != 0)
        goto stop_engine;
    capture_counters(&device, &overflow_after);
    status = astra_mmio_read(&device, ASTRA_REG_RENDER_STATUS);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            CERTIFICATION_COMMAND_END ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            GLYPH_FAILURE_COMMAND ||
        overflow_after.submitted - overflow_before.submitted != 1u ||
        overflow_after.completed - overflow_before.completed != 1u ||
        overflow_after.failed - overflow_before.failed != 1u ||
        overflow_after.timeouts != overflow_before.timeouts ||
        overflow_after.resets != overflow_before.resets ||
        (status & (ASTRA_RENDER_ENGINE_BUSY |
                   ASTRA_RENDER_ENGINE_RESET_ACTIVE |
                   ASTRA_RENDER_ENGINE_CONFIG_FAULT)) != 0u) {
        fprintf(stderr,
                "flood overflow accounting failed: status=%08" PRIx32
                " submitted=%" PRIu32 " completed=%" PRIu32
                " failed=%" PRIu32 " timeout=%" PRIu32
                " resets=%" PRIu32 " fence=%" PRIu32 "\n",
                status,
                overflow_after.submitted - overflow_before.submitted,
                overflow_after.completed - overflow_before.completed,
                overflow_after.failed - overflow_before.failed,
                overflow_after.timeouts - overflow_before.timeouts,
                overflow_after.resets - overflow_before.resets,
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE));
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     CERTIFICATION_COMMAND_END);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_IRQ_PENDING) != 0u) {
        fprintf(stderr,
                "flood overflow completion interrupt did not acknowledge\n");
        goto stop_engine;
    }
    printf("ASTRA_FLOOD_BOUNDED PASS status=%u fence=%u cycles=%" PRIu32
           "\n", ASTRA_RENDER_STATUS_WORK_OVERFLOW,
           GLYPH_FAILURE_COMMAND, overflow_cycles);
    if (prepare_screen_offset_workload(&maps) != 0) {
        fprintf(stderr, "screen-offset workload layout is invalid\n");
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     SCREEN_OFFSET_COMMAND_END);
    if (wait_for_completions(&device, SCREEN_OFFSET_COMMAND_END) != 0 ||
        wait_for_idle(&device, ENGINE_TIMEOUT_NS) != 0 ||
        verify_screen_offset(&maps, &screen_offset_cycles) != 0)
        goto stop_engine;
    if (astra_mmio_read(&device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) !=
            SCREEN_OFFSET_COMMAND_END ||
        astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE) !=
            GLYPH_FAILURE_COMMAND) {
        fprintf(stderr,
                "screen-offset accounting failed: consumer=%" PRIu32
                " fence=%" PRIu32 " expected=%u\n",
                astra_mmio_read(&device,
                    ASTRA_REG_RENDER_SUBMISSION_CONSUMER),
                astra_mmio_read(&device, ASTRA_REG_RENDER_RETIRED_FENCE),
                GLYPH_FAILURE_COMMAND);
        goto stop_engine;
    }
    astra_mmio_write(&device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     SCREEN_OFFSET_COMMAND_END);
    astra_mmio_write(&device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
    printf("ASTRA_SCREEN_OFFSET PASS width=%u height=%u source_y=%u"
           " cycles=%" PRIu32 "\n",
           ASTRA_FRAMEBUFFER_WIDTH, SCREEN_OFFSET_HEIGHT,
           SCREEN_OFFSET_SOURCE_Y, screen_offset_cycles);
    if (present_milliseconds != 0u &&
        present_result(&device, present_milliseconds) != 0)
        goto stop_engine;
    result = EXIT_SUCCESS;

stop_engine:
    astra_mmio_write(&device, ASTRA_REG_RENDER_CONTROL, 0u);
    if (result != EXIT_SUCCESS)
        astra_mmio_write(&device, ASTRA_REG_RENDER_CONTROL,
                         ASTRA_RENDER_CONTROL_SOFT_RESET);

done:
    close_maps(&maps);
    astra_graphics_device_close(&device);
    return result;
}
