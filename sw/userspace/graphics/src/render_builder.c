#include <astra/render_builder.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/endian.h>
#include <astra/font.h>
#include <astra/render_batch.h>
#include <astra/window_scene.h>
#include <astra/surface.h>
#include <astra/ui_font.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <astra/rounded.h>

enum {
    DATA_ARENA_OFFSET = ASTRA_RENDER_BATCH_DATA_OFFSET,
    SURFACE_ARENA_LIMIT = ASTRA_RENDER_BATCH_WORKSPACE_LIMIT,
    COMMAND_DEADLINE_US = 500000u,
};

_Static_assert(ASTRA_RENDER_BATCH_ARENA_OFFSET +
                   ASTRA_RENDER_BUILDER_BYTES == SURFACE_ARENA_LIMIT,
               "builder must span the complete render workspace");

static uint32_t pair_u16(uint16_t high, uint16_t low)
{
    return ((uint32_t)high << 16) | low;
}

static uint32_t pair_s16(int32_t high, int32_t low)
{
    return ((uint32_t)(uint16_t)(int16_t)high << 16) |
           (uint16_t)(int16_t)low;
}

static uint32_t relative(uint32_t arena_offset)
{
    return arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint8_t *allocate_data(AstraRenderBuilder *builder, uint32_t bytes,
                              uint32_t alignment, uint32_t *arena_offset)
{
    uint32_t cursor = align_up(builder->data_cursor, alignment);
    uint32_t limit = builder->surface_cursor -
                     ASTRA_RENDER_BATCH_ARENA_OFFSET;

    if (cursor > limit || bytes > limit - cursor) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_DATA;
        return NULL;
    }
    *arena_offset = ASTRA_RENDER_BATCH_ARENA_OFFSET + cursor;
    builder->data_cursor = cursor + bytes;
    memset(builder->bytes + cursor, 0, bytes);
    return builder->bytes + cursor;
}

static uint32_t descriptor(AstraRenderBuilder *builder, uint32_t data_offset,
                           uint32_t data_bytes, uint32_t pitch,
                           uint16_t width, uint16_t height, uint8_t format,
                           uint8_t flags)
{
    uint32_t offset;
    uint8_t *record;

    if (width == 0u || height == 0u) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_DESCRIPTOR;
        return 0u;
    }
    record = allocate_data(builder, ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES,
                           ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES, &offset);
    if (record == NULL)
        return 0u;
    astra_store_be32(record + 0u, pair_u16(ASTRA_RENDER_ABI_VERSION,
                                ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES));
    astra_store_be32(record + 4u, builder->generation);
    astra_store_be32(record + 8u, data_offset);
    astra_store_be32(record + 12u, data_bytes);
    astra_store_be32(record + 16u, pitch);
    astra_store_be32(record + 20u, pair_u16(width, height));
    astra_store_be32(record + 24u, ((uint32_t)format << 24) |
                        ((uint32_t)flags << 16));
    return offset;
}

static int descriptor_dimensions(const AstraRenderBuilder *builder,
                                 uint32_t address, uint16_t *width,
                                 uint16_t *height)
{
    uint32_t offset;
    const uint8_t *record;

    if (address < ASTRA_RENDER_BATCH_DATA_OFFSET)
        return 0;
    offset = relative(address);
    if ((offset & (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) != 0u ||
        offset > builder->data_cursor ||
        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES > builder->data_cursor - offset)
        return 0;
    record = builder->bytes + offset;
    if (astra_load_be32(record) !=
            pair_u16(ASTRA_RENDER_ABI_VERSION,
                     ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES) ||
        astra_load_be32(record + 4u) != builder->generation)
        return 0;
    *width = (uint16_t)(astra_load_be32(record + 20u) >> 16);
    *height = (uint16_t)astra_load_be32(record + 20u);
    return *width != 0u && *height != 0u;
}

static const uint8_t *descriptor_record(const AstraRenderBuilder *builder,
                                        uint32_t address)
{
    uint32_t offset;

    if (builder == NULL || address < ASTRA_RENDER_BATCH_DATA_OFFSET)
        return NULL;
    offset = relative(address);
    if ((offset & (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) != 0u ||
        offset > builder->data_cursor ||
        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES > builder->data_cursor - offset)
        return NULL;
    return builder->bytes + offset;
}

static int command(AstraRenderBuilder *builder, uint16_t opcode,
                   uint16_t flags, uint32_t destination,
                   int32_t clip_left, int32_t clip_top,
                   int32_t clip_right, int32_t clip_bottom,
                   uint8_t **record_out)
{
    uint32_t offset;
    uint8_t *record;
    uint16_t destination_width;
    uint16_t destination_height;

    *record_out = NULL;
    if (!descriptor_dimensions(builder, destination, &destination_width,
                               &destination_height)) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_DESTINATION;
        return 0;
    }
    if (clip_left < 0)
        clip_left = 0;
    if (clip_top < 0)
        clip_top = 0;
    if (clip_right > destination_width)
        clip_right = destination_width;
    if (clip_bottom > destination_height)
        clip_bottom = destination_height;
    if (clip_left > destination_width)
        clip_left = destination_width;
    if (clip_top > destination_height)
        clip_top = destination_height;
    if (clip_right < 0)
        clip_right = 0;
    if (clip_bottom < 0)
        clip_bottom = 0;
    if (clip_left >= clip_right || clip_top >= clip_bottom)
        return 1;
    if (builder->command_count >= ASTRA_RENDER_RING_ENTRIES) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_COMMAND_CAPACITY;
        return 0;
    }
    offset = relative(ASTRA_RENDER_BATCH_SUBMISSION_OFFSET) +
             builder->command_count * ASTRA_RENDER_COMMAND_BYTES;
    record = builder->bytes + offset;
    memset(record, 0, ASTRA_RENDER_COMMAND_BYTES);
    astra_store_be32(record + 0u,
          pair_u16(ASTRA_RENDER_ABI_VERSION, ASTRA_RENDER_COMMAND_BYTES));
    astra_store_be32(record + 4u, pair_u16(opcode, flags));
    astra_store_be32(record + 8u, builder->command_count + 1u);
    astra_store_be32(record + 12u, builder->generation);
    astra_store_be32(record + 16u, COMMAND_DEADLINE_US);
    astra_store_be32(record + 24u, pair_s16(clip_left, clip_top));
    astra_store_be32(record + 28u, pair_s16(clip_right, clip_bottom));
    astra_store_be32(record + 32u, destination);
    ++builder->command_count;
    *record_out = record;
    return 1;
}

int astra_render_builder_init(AstraRenderBuilder *builder, void *storage,
                              uint32_t bytes, uint32_t generation)
{
    uint32_t scanout = (generation & 1u) != 0u ?
        ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
        ASTRA_RENDER_BATCH_SCANOUT0_OFFSET;
    uint32_t frame;

    if (builder == NULL || storage == NULL ||
        bytes < ASTRA_RENDER_BATCH_MIN_BYTES +
                    ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES ||
        bytes > ASTRA_RENDER_BUILDER_BYTES || generation == 0u)
        return 0;
    memset(storage, 0, ASTRA_RENDER_BATCH_HEADER_BYTES);
    *builder = (AstraRenderBuilder){
        .bytes = storage,
        .generation = generation,
        .data_cursor = relative(DATA_ARENA_OFFSET),
        .surface_cursor = ASTRA_RENDER_BATCH_ARENA_OFFSET + bytes,
    };
    frame = descriptor(builder, scanout,
                       ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * 2u,
                       ASTRA_DISPLAY_WIDTH * 2u, ASTRA_DISPLAY_WIDTH,
                       ASTRA_DISPLAY_HEIGHT, ASTRA_RENDER_FORMAT_RGB565,
                       ASTRA_RENDER_SURFACE_READ |
                           ASTRA_RENDER_SURFACE_WRITE);
    return frame == ASTRA_RENDER_BATCH_RESOURCE_OFFSET;
}

uint32_t astra_render_builder_frame(const AstraRenderBuilder *builder)
{
    return builder == NULL || builder->failed != 0u ? 0u :
           ASTRA_RENDER_BATCH_RESOURCE_OFFSET;
}

int astra_render_builder_cursor(AstraRenderBuilder *builder, uint32_t x,
                                uint32_t y, uint32_t flags)
{
    if (builder == NULL || builder->bytes == NULL ||
        x >= ASTRA_DISPLAY_WIDTH ||
        y >= ASTRA_DISPLAY_HEIGHT ||
        (flags & ~ASTRA_DISPLAY_CURSOR_FLAGS_MASK) != 0u ||
        ((flags & ASTRA_DISPLAY_CURSOR_SHAPE_MASK) >>
             ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT) >= ASTRA_POINTER_SHAPE_COUNT) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_PRESENTATION;
        return 0;
    }
    astra_store_be32(builder->bytes + 32u,
                     ASTRA_RENDER_BATCH_PRESENT_CURSOR);
    astra_store_be32(builder->bytes + 36u, x);
    astra_store_be32(builder->bytes + 40u, y);
    astra_store_be32(builder->bytes + 44u, flags);
    return 1;
}

int astra_render_builder_window_scene(AstraRenderBuilder *builder,
                                      uint32_t output_offset,
                                      uint32_t output_capacity,
                                      uint16_t backdrop_rgb565)
{
    uint32_t scene_offset;
    uint8_t *scene;
    uint32_t minimum_span_offset = align_up(
        ASTRA_WINDOW_SCENE_HEADER_BYTES +
            ASTRA_DISPLAY_HEIGHT * ASTRA_WINDOW_SCENE_LINE_BYTES,
        64u);

    if (builder == NULL || builder->bytes == NULL ||
        builder->scene_offset != 0u || (output_offset & 63u) != 0u ||
        (output_capacity & 63u) != 0u ||
        output_offset < ASTRA_RENDER_BATCH_WORKSPACE_LIMIT ||
        output_offset > ASTRA_RENDER_BATCH_MEDIA_LIMIT ||
        output_capacity < minimum_span_offset +
                              ASTRA_DISPLAY_HEIGHT *
                                  ASTRA_WINDOW_SCENE_SPAN_BYTES ||
        output_capacity > ASTRA_RENDER_BATCH_MEDIA_LIMIT - output_offset) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SCENE;
        return 0;
    }
    scene = allocate_data(builder, ASTRA_WINDOW_SCENE_HEADER_BYTES, 32u,
                          &scene_offset);
    if (scene == NULL)
        return 0;
    builder->scene_offset = scene_offset;
    builder->scene_output_capacity = output_capacity;
    astra_store_be32(scene + 0u, ASTRA_WINDOW_SCENE_MAGIC);
    astra_store_be32(scene + 4u, ASTRA_WINDOW_SCENE_VERSION);
    astra_store_be32(scene + 12u, builder->generation);
    astra_store_be32(scene + 16u,
                     pair_u16(ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT));
    astra_store_be32(scene + 24u, ASTRA_WINDOW_SCENE_HEADER_BYTES);
    astra_store_be32(scene + 28u, output_offset);
    astra_store_be32(scene + 32u, output_capacity);
    astra_store_be32(scene + 36u, backdrop_rgb565);
    return 1;
}

int astra_render_builder_window_scene_layer(AstraRenderBuilder *builder,
                                            uint32_t surface,
                                            int32_t x, int32_t y,
                                            uint16_t radius, int visible)
{
    const uint8_t *source = descriptor_record(builder, surface);
    uint32_t size;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t format_flags;
    uint16_t width;
    uint16_t height;
    uint32_t expected_offset;
    uint32_t layer_offset;
    uint8_t *layer;

    if (builder == NULL || builder->scene_offset == 0u || source == NULL) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SCENE;
        return 0;
    }
    size = astra_load_be32(source + 20u);
    width = (uint16_t)(size >> 16);
    height = (uint16_t)size;
    data_offset = astra_load_be32(source + 8u);
    data_bytes = astra_load_be32(source + 12u);
    format_flags = astra_load_be32(source + 24u);
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX ||
        width == 0u || height == 0u || radius > width / 2u ||
        radius > height / 2u ||
        (format_flags >> 24) != ASTRA_RENDER_FORMAT_RGB565 ||
        ((format_flags >> 16) & ASTRA_RENDER_SURFACE_READ) == 0u ||
        data_offset < ASTRA_RENDER_BATCH_WORKSPACE_LIMIT ||
        data_bytes == 0u ||
        data_offset > ASTRA_RENDER_BATCH_MEDIA_LIMIT - data_bytes ||
        (data_offset < astra_load_be32(
                           builder->bytes + relative(builder->scene_offset) +
                           28u) +
                           builder->scene_output_capacity &&
         astra_load_be32(builder->bytes + relative(builder->scene_offset) +
                         28u) < data_offset + data_bytes)) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SCENE;
        return 0;
    }
    expected_offset = builder->scene_offset + ASTRA_WINDOW_SCENE_HEADER_BYTES +
                      builder->scene_layer_count *
                          ASTRA_WINDOW_SCENE_LAYER_BYTES;
    layer = allocate_data(builder, ASTRA_WINDOW_SCENE_LAYER_BYTES, 32u,
                          &layer_offset);
    if (layer == NULL)
        return 0;
    if (layer_offset != expected_offset) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SCENE;
        return 0;
    }
    astra_store_be32(layer + 0u, data_offset);
    astra_store_be32(layer + 4u, data_bytes);
    astra_store_be32(layer + 8u, astra_load_be32(source + 16u));
    astra_store_be32(layer + 12u, size);
    astra_store_be32(layer + 16u, pair_s16(x, y));
    astra_store_be32(layer + 20u, radius |
        (visible ? ASTRA_WINDOW_SCENE_LAYER_VISIBLE : 0u));
    ++builder->scene_layer_count;
    return 1;
}

uint32_t astra_render_builder_scanout(AstraRenderBuilder *builder,
                                      uint32_t scanout_offset)
{
    if (builder == NULL ||
        (scanout_offset != ASTRA_RENDER_BATCH_SCANOUT0_OFFSET &&
         scanout_offset != ASTRA_RENDER_BATCH_SCANOUT1_OFFSET)) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    return descriptor(builder, scanout_offset,
                      ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * 2u,
                      ASTRA_DISPLAY_WIDTH * 2u, ASTRA_DISPLAY_WIDTH,
                      ASTRA_DISPLAY_HEIGHT, ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                          ASTRA_RENDER_SURFACE_WRITE);
}

uint32_t astra_render_builder_surface(AstraRenderBuilder *builder,
                                      uint16_t width, uint16_t height)
{
    uint32_t bytes;
    uint32_t data;

    if (builder == NULL || width == 0u || height == 0u ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    bytes = (uint32_t)width * height * 2u;
    if (bytes > builder->surface_cursor - ASTRA_RENDER_BATCH_ARENA_OFFSET) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    data = (builder->surface_cursor - bytes) & ~UINT32_C(63);
    if (data < ASTRA_RENDER_BATCH_ARENA_OFFSET +
                   align_up(builder->data_cursor, 4u)) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    builder->surface_cursor = data;
    return descriptor(builder, data, bytes, (uint32_t)width * 2u,
                      width, height, ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                          ASTRA_RENDER_SURFACE_WRITE);
}

uint32_t astra_render_builder_surface_at(AstraRenderBuilder *builder,
                                         uint32_t data_offset,
                                         uint32_t data_capacity,
                                         uint16_t width, uint16_t height)
{
    uint32_t bytes;

    if (builder == NULL || width == 0u || height == 0u ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        data_offset < SURFACE_ARENA_LIMIT || (data_offset & 63u) != 0u) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    bytes = (uint32_t)width * height * 2u;
    if (bytes > data_capacity || bytes > ASTRA_RENDER_BATCH_MAX_BYTES ||
        data_offset > UINT32_MAX - bytes) {
        builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    return descriptor(builder, data_offset, bytes, (uint32_t)width * 2u,
                      width, height, ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                          ASTRA_RENDER_SURFACE_WRITE);
}

static int builder_fill(AstraRenderBuilder *builder, uint32_t destination,
                        int32_t x, int32_t y, uint32_t width,
                        uint32_t height, uint16_t color,
                        int32_t clip_left, int32_t clip_top,
                        int32_t clip_right, int32_t clip_bottom)
{
    uint8_t *record;

    if (builder == NULL || destination == 0u || width == 0u || height == 0u ||
        width > UINT16_MAX || height > UINT16_MAX)
        return 0;
    if (!command(builder, ASTRA_RENDER_OP_FILL, 0u, destination,
                 clip_left, clip_top, clip_right, clip_bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 48u, pair_s16(x, y));
    astra_store_be32(record + 56u, pair_u16((uint16_t)width, (uint16_t)height));
    astra_store_be32(record + 60u, color);
    return 1;
}

static int builder_line(AstraRenderBuilder *builder, uint32_t destination,
                        int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint16_t color, int32_t clip_left, int32_t clip_top,
                        int32_t clip_right, int32_t clip_bottom)
{
    uint8_t *record;

    if (!command(builder, ASTRA_RENDER_OP_LINE, 0u, destination,
                 clip_left, clip_top, clip_right, clip_bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 44u, pair_s16(x0, y0));
    astra_store_be32(record + 48u, pair_s16(x1, y1));
    astra_store_be32(record + 60u, color);
    return 1;
}

int astra_render_builder_fill(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t color)
{
    return builder_fill(builder, destination, x, y, width, height, color,
                        INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX);
}

static int circle(AstraRenderBuilder *builder, uint32_t destination,
                  int32_t center_x, int32_t center_y, uint16_t radius,
                  int32_t left, int32_t top, int32_t right, int32_t bottom,
                  uint16_t color)
{
    uint8_t *record;

    if (!command(builder, ASTRA_RENDER_OP_CIRCLE,
                 ASTRA_RENDER_GEOMETRY_FLAG_FILLED, destination,
                 left, top, right, bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 44u, pair_s16(center_x, center_y));
    astra_store_be32(record + 52u, pair_u16(radius, 0u));
    astra_store_be32(record + 60u, color);
    return 1;
}

static int builder_rounded(AstraRenderBuilder *builder, uint32_t destination,
                           int32_t x, int32_t y, uint32_t width,
                           uint32_t height, uint16_t radius, uint16_t color,
                           int32_t clip_left, int32_t clip_top,
                           int32_t clip_right, int32_t clip_bottom)
{
    uint32_t bounded = radius;
    int32_t right;
    int32_t bottom;
    int horizontal = 1;
    int vertical = 1;

    if (width == 0u || height == 0u)
        return 0;
    if (bounded > width / 2u)
        bounded = width / 2u;
    if (bounded > height / 2u)
        bounded = height / 2u;
    if (bounded == 0u)
        return builder_fill(builder, destination, x, y, width, height, color,
                            clip_left, clip_top, clip_right, clip_bottom);
    right = x + (int32_t)width;
    bottom = y + (int32_t)height;
    if (clip_left < x) clip_left = x;
    if (clip_top < y) clip_top = y;
    if (clip_right > right) clip_right = right;
    if (clip_bottom > bottom) clip_bottom = bottom;
    if (width > bounded * 2u)
        horizontal = builder_fill(
            builder, destination, x + (int32_t)bounded, y,
            width - bounded * 2u, height, color,
            clip_left, clip_top, clip_right, clip_bottom);
    /* Side bands, not a full-width one: a full-width band would repaint every
       pixel the horizontal band already covered, and fill costs real time. */
    if (height > bounded * 2u)
        vertical = builder_fill(
                       builder, destination, x, y + (int32_t)bounded,
                       bounded, height - bounded * 2u, color,
                       clip_left, clip_top, clip_right, clip_bottom) &&
                   builder_fill(
                       builder, destination, right - (int32_t)bounded,
                       y + (int32_t)bounded, bounded,
                       height - bounded * 2u, color,
                       clip_left, clip_top, clip_right, clip_bottom);
    return horizontal && vertical &&
           circle(builder, destination, x + bounded, y + bounded,
                  bounded, clip_left, clip_top, clip_right, clip_bottom,
                  color) &&
           circle(builder, destination, right - bounded - 1,
                  y + bounded, bounded, clip_left, clip_top,
                  clip_right, clip_bottom, color) &&
           circle(builder, destination, x + bounded,
                  bottom - bounded - 1, bounded,
                  clip_left, clip_top, clip_right, clip_bottom, color) &&
           circle(builder, destination, right - bounded - 1,
                  bottom - bounded - 1, bounded,
                  clip_left, clip_top, clip_right, clip_bottom, color);
}

int astra_render_builder_rounded(AstraRenderBuilder *builder,
                                 uint32_t destination, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 uint16_t radius, uint16_t color)
{
    return builder_rounded(builder, destination, x, y, width, height,
                           radius, color, INT16_MIN, INT16_MIN,
                           INT16_MAX, INT16_MAX);
}

static uint8_t strike_render_format(uint16_t bitmap_format)
{
    if (bitmap_format == ASTRA_FONT_BITMAP_MASK1)
        return ASTRA_RENDER_FORMAT_MASK1;
    if (bitmap_format == ASTRA_FONT_BITMAP_A8)
        return ASTRA_RENDER_FORMAT_A8;
    return UINT8_MAX;
}

static uint32_t strike_pitch(uint16_t bitmap_format, uint32_t width)
{
    return bitmap_format == ASTRA_FONT_BITMAP_MASK1 ?
           (width + 7u) / 8u : width;
}

static uint8_t source_coverage(uint16_t bitmap_format,
                               const uint8_t *bitmap, uint32_t pitch,
                               uint32_t x, uint32_t y)
{
    const uint8_t *pixel = bitmap + y * pitch;

    return bitmap_format == ASTRA_FONT_BITMAP_MASK1 ?
        ((pixel[x / 8u] & (uint8_t)(0x80u >> (x & 7u))) != 0u ? 255u : 0u) :
        pixel[x];
}

static void store_coverage(uint16_t bitmap_format, uint8_t *bitmap,
                           uint32_t pitch, uint32_t x, uint32_t y,
                           uint8_t coverage)
{
    uint8_t *pixel = bitmap + y * pitch;

    if (bitmap_format == ASTRA_FONT_BITMAP_MASK1) {
        if (coverage != 0u)
            pixel[x / 8u] |= (uint8_t)(0x80u >> (x & 7u));
    } else if (coverage > pixel[x]) {
        pixel[x] = coverage;
    }
}

static uint32_t glyph_table_capacity(uint32_t count)
{
    uint32_t capacity = 1u;

    while (capacity < count * 2u)
        capacity <<= 1u;
    return capacity;
}

static int builder_text_run(AstraRenderBuilder *builder,
                            uint32_t destination, int32_t y,
                            const char *utf8, uint32_t length,
                            uint16_t pixel_height, uint16_t cell_width,
                            uint16_t color, int32_t clip_left,
                            int32_t clip_top, int32_t clip_right,
                            int32_t clip_bottom, uint32_t style_flags,
                            int32_t *pen_26_6)
{
    const AstraUiStrike *strike = cell_width != 0u ?
        astra_mono_font_strike(pixel_height) :
        astra_ui_font_strike(pixel_height);
    uint32_t count = 0u;
    uint32_t max_width = 0u;
    uint32_t max_height = 0u;
    uint32_t at = 0u;
    uint32_t cell_pitch;
    uint32_t cell_bytes;
    uint32_t allocation_bytes;
    uint32_t allocation_data;
    uint32_t table_capacity;
    uint32_t table_bytes;
    uint32_t unique_count = 0u;
    uint32_t source_bytes;
    uint32_t source_data;
    uint32_t source_descriptor;
    uint32_t glyph_offset;
    uint8_t *glyph_records;
    uint8_t *allocation;
    uint8_t *source;
    uint32_t *table;
    uint8_t *record;
    uint32_t embolden = astra_ui_bold_strength(pixel_height, style_flags);
    uint8_t render_format;

    if (builder == NULL || destination == 0u || utf8 == NULL ||
        pen_26_6 == NULL ||
        strike == NULL ||
        (style_flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) != 0u)
        return 0;
    render_format = strike_render_format(strike->bitmap_format);
    if (render_format == UINT8_MAX)
        return 0;
    while (at < length) {
        uint32_t consumed;
        uint32_t scalar = astra_ui_font_scalar(
            utf8 + at, length - at, &consumed);
        const AstraUiGlyph *glyph = cell_width != 0u ?
            astra_mono_font_glyph(strike, scalar) :
            astra_ui_font_glyph(strike, scalar);

        int32_t minimum_shift;
        uint32_t styled_width;

        astra_ui_style_extents(glyph, style_flags, embolden, &minimum_shift,
                               &styled_width);
        (void)minimum_shift;
        if (styled_width > max_width)
            max_width = styled_width;
        if (glyph->height > max_height)
            max_height = glyph->height;
        ++count;
        at += consumed;
    }
    if (count == 0u)
        return 1;
    if (max_width > UINT16_MAX)
        return 0;
    cell_pitch = strike_pitch(strike->bitmap_format, max_width);
    if (cell_pitch > UINT32_MAX / max_height)
        return 0;
    cell_bytes = cell_pitch * max_height;
    if (count > UINT32_MAX / cell_bytes)
        return 0;
    source_bytes = cell_bytes * count;
    table_capacity = glyph_table_capacity(count);
    table_bytes = table_capacity * 2u * sizeof(uint32_t);
    if (source_bytes > UINT32_MAX - table_bytes)
        return 0;
    allocation_bytes = table_bytes + source_bytes;
    glyph_records = allocate_data(
        builder, count * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES,
        ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES, &glyph_offset);
    if (glyph_records == NULL)
        return 0;
    allocation = allocate_data(builder, allocation_bytes, 4u,
                               &allocation_data);
    if (allocation == NULL)
        return 0;
    table = (uint32_t *)(void *)allocation;
    source = allocation + table_bytes;
    at = 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t consumed;
        uint32_t scalar = astra_ui_font_scalar(
            utf8 + at, length - at, &consumed);
        const AstraUiGlyph *glyph = cell_width != 0u ?
            astra_mono_font_glyph(strike, scalar) :
            astra_ui_font_glyph(strike, scalar);
        const uint8_t *bitmap = cell_width != 0u ?
            astra_mono_font_bitmap(glyph) : astra_ui_font_bitmap(glyph);
        uint8_t *glyph_record = glyph_records +
            index * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES;

        int32_t minimum_shift;
        uint32_t styled_width;
        uint32_t table_index =
            (glyph->bitmap_offset * UINT32_C(2654435761)) &
            (table_capacity - 1u);
        uint32_t cell_index;
        uint8_t *styled;

        astra_ui_style_extents(glyph, style_flags, embolden, &minimum_shift,
                               &styled_width);
        while (table[table_index * 2u + 1u] != 0u &&
               table[table_index * 2u] != glyph->bitmap_offset)
            table_index = (table_index + 1u) & (table_capacity - 1u);
        if (table[table_index * 2u + 1u] == 0u) {
            table[table_index * 2u] = glyph->bitmap_offset;
            table[table_index * 2u + 1u] = ++unique_count;
            cell_index = unique_count - 1u;
            styled = source + cell_index * cell_bytes;
            if ((style_flags & (ASTRA_TEXT_STYLE_BOLD |
                                ASTRA_TEXT_STYLE_ITALIC)) == 0u) {
                for (uint32_t row = 0u; row < glyph->height; ++row)
                    memcpy(styled + row * cell_pitch,
                           bitmap + row * glyph->pitch, glyph->pitch);
            } else {
                for (uint32_t row = 0u; row < glyph->height; ++row) {
                    int32_t shift =
                        (style_flags & ASTRA_TEXT_STYLE_ITALIC) != 0u ?
                            astra_ui_italic_shift(glyph, row) : 0;
                    uint32_t origin = (uint32_t)(shift - minimum_shift);

                    for (uint32_t column = 0u; column < glyph->width;
                         ++column)
                        for (uint32_t weight = 0u; weight <= embolden;
                             ++weight)
                            store_coverage(
                                strike->bitmap_format, styled, cell_pitch,
                                origin + column + weight, row,
                                source_coverage(
                                    strike->bitmap_format, bitmap,
                                    glyph->pitch, column, row));
                }
            }
        } else {
            cell_index = table[table_index * 2u + 1u] - 1u;
        }
        astra_store_be32(glyph_record + 0u, cell_index * cell_bytes);
        astra_store_be32(glyph_record + 4u, 0u);
        astra_store_be32(glyph_record + 8u,
              pair_s16(astra_ui_glyph_x(*pen_26_6, glyph) + minimum_shift,
                       astra_ui_glyph_y(
                           y * 64 + strike->ascent, glyph)));
        astra_store_be32(glyph_record + 12u,
              pair_u16((uint16_t)styled_width, glyph->height));
        ++builder->glyph_count;
        *pen_26_6 += astra_ui_glyph_advance(glyph, cell_width);
        at += consumed;
    }
    source_bytes = unique_count * cell_bytes;
    memmove(allocation, source, source_bytes);
    source_data = allocation_data;
    builder->data_cursor = relative(source_data) + source_bytes;
    source_descriptor = descriptor(
        builder, source_data, source_bytes, cell_pitch,
        (uint16_t)max_width, (uint16_t)max_height, render_format,
        ASTRA_RENDER_SURFACE_READ);
    if (source_descriptor == 0u)
        return 0;
    if (!command(builder, ASTRA_RENDER_OP_GLYPH_RUN, 0u, destination,
                 clip_left, clip_top, clip_right, clip_bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 36u, source_descriptor);
    astra_store_be32(record + 40u, glyph_offset);
    astra_store_be32(record + 44u, count);
    astra_store_be32(record + 48u, color);
    return 1;
}

static int builder_text(AstraRenderBuilder *builder, uint32_t destination,
                        int32_t x, int32_t y, const char *utf8,
                        uint32_t length, uint16_t pixel_height,
                        uint16_t cell_width, uint16_t color,
                        int32_t clip_left, int32_t clip_top,
                        int32_t clip_right, int32_t clip_bottom,
                        uint32_t style_flags)
{
    const AstraUiStrike *strike = cell_width != 0u ?
        astra_mono_font_strike(pixel_height) :
        astra_ui_font_strike(pixel_height);
    int32_t pen_26_6 = x * 64;
    uint32_t at = 0u;

    if (builder == NULL || destination == 0u || utf8 == NULL ||
        strike == NULL ||
        (style_flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) != 0u)
        return 0;
    while (at < length) {
        uint32_t start = at;
        uint32_t count = 0u;

        while (at < length &&
               count < ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS) {
            uint32_t consumed;

            (void)astra_ui_font_scalar(utf8 + at, length - at, &consumed);
            at += consumed;
            ++count;
        }
        if (!builder_text_run(builder, destination, y, utf8 + start,
                              at - start, pixel_height, cell_width, color,
                              clip_left, clip_top, clip_right, clip_bottom,
                              style_flags, &pen_26_6))
            return 0;
    }
    if ((style_flags & ASTRA_TEXT_STYLE_UNDERLINE) != 0u &&
        !builder_fill(
            builder, destination, x,
            y + astra_ui_fixed_floor(strike->ascent) +
                astra_ui_fixed_floor(strike->underline_position),
            (uint32_t)(pen_26_6 - x * 64 + 63) / 64u,
            (uint32_t)(strike->underline_thickness + 63) / 64u, color,
            clip_left, clip_top, clip_right, clip_bottom))
        return 0;
    if ((style_flags & ASTRA_TEXT_STYLE_STRIKETHROUGH) != 0u &&
        !builder_fill(
            builder, destination, x,
            y + astra_ui_fixed_floor(strike->ascent) -
                astra_ui_fixed_floor(strike->strikeout_position),
            (uint32_t)(pen_26_6 - x * 64 + 63) / 64u,
            (uint32_t)(strike->strikeout_thickness + 63) / 64u, color,
            clip_left, clip_top, clip_right, clip_bottom))
        return 0;
    return 1;
}

int astra_render_builder_text(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              const char *utf8, uint32_t length,
                              uint16_t pixel_height, uint16_t color)
{
    return builder_text(builder, destination, x, y, utf8, length,
                        pixel_height, 0u, color,
                        INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX, 0u);
}

int astra_render_builder_mono_text(AstraRenderBuilder *builder,
                                   uint32_t destination, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color)
{
    return cell_width != 0u &&
           builder_text(builder, destination, x, y, utf8, length,
                        pixel_height, cell_width, color,
                        INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX, 0u);
}

static int command_valid(const AstraDrawListHeader *header,
                         const AstraDrawListCommand *item)
{
    uint64_t end = (uint64_t)item->payload_offset + item->payload_bytes;

    if (item->reserved16 != 0u ||
        !astra_words_zero(item->reserved, 2u) ||
        item->clip_left >= item->clip_right ||
        item->clip_right > header->width ||
        item->clip_top >= item->clip_bottom ||
        item->clip_bottom > header->height)
        return 0;
    if (item->operation == ASTRA_DRAW_LIST_FILL ||
        item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED)
        return item->flags == 0u && item->width != 0u && item->height != 0u &&
               item->payload_offset == 0u && item->payload_bytes == 0u &&
               item->font_height == 0u;
    if (item->operation == ASTRA_DRAW_LIST_TEXT ||
        item->operation == ASTRA_DRAW_LIST_MONO_TEXT)
        return (item->flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) == 0u &&
               (item->operation == ASTRA_DRAW_LIST_TEXT ?
                    item->width == 0u : item->width != 0u) &&
               item->height == 0u &&
               item->radius == 0u && item->payload_bytes != 0u &&
               item->payload_offset >= ASTRA_DRAW_LIST_PAYLOAD_OFFSET &&
               end <= (uint64_t)ASTRA_DRAW_LIST_PAYLOAD_OFFSET +
                         header->payload_bytes;
    if (item->operation == ASTRA_DRAW_LIST_COPY)
        return item->flags == 0u && item->x >= 0 && item->y >= 0 &&
               item->width != 0u &&
               item->height != 0u && item->radius == 0u &&
               item->payload_offset == 0u && item->payload_bytes == 0u &&
               item->font_height == 0u &&
               item->foreground <= header->width &&
               item->width <= header->width - item->foreground &&
               item->background <= header->height &&
               item->height <= header->height - item->background &&
               (uint32_t)item->x <= header->width &&
               item->width <= header->width - (uint32_t)item->x &&
               (uint32_t)item->y <= header->height &&
               item->height <= header->height - (uint32_t)item->y;
    if (item->operation == ASTRA_DRAW_LIST_LINE)
        return item->flags == 0u && item->radius == 0u &&
               item->background == 0u &&
               item->payload_offset == 0u && item->payload_bytes == 0u &&
               item->font_height == 0u && item->foreground <= UINT16_MAX &&
               (int32_t)(int16_t)item->x == item->x &&
               (int32_t)(int16_t)item->y == item->y &&
               (int32_t)(int16_t)item->width == (int32_t)item->width &&
               (int32_t)(int16_t)item->height == (int32_t)item->height;
    return 0;
}

int astra_draw_list_covers(const AstraDrawListHeader *header,
                           uint16_t width, uint16_t height)
{
    const AstraDrawListCommand *first;

    if (header == NULL || header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_2 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->command_count == 0u ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES ||
        !astra_words_zero(header->reserved, 10u))
        return 0;
    first = (const AstraDrawListCommand *)(header + 1);
    if (!command_valid(header, first) ||
        first->operation != ASTRA_DRAW_LIST_FILL)
        return 0;
    return first->clip_left == 0u && first->clip_top == 0u &&
           first->clip_right >= width && first->clip_bottom >= height &&
           first->x <= 0 && first->y <= 0 &&
           (int64_t)first->x + first->width >= (int64_t)width &&
           (int64_t)first->y + first->height >= (int64_t)height;
}

static int blit_region(AstraRenderBuilder *builder, uint32_t destination,
                       uint32_t source, uint32_t mask, uint16_t flags,
                       int32_t source_x, int32_t source_y,
                       int32_t destination_x, int32_t destination_y,
                       uint16_t width, uint16_t height,
                       int32_t clip_left, int32_t clip_top,
                       int32_t clip_right, int32_t clip_bottom);

int astra_render_builder_replay(AstraRenderBuilder *builder,
                                uint32_t destination,
                                const AstraDrawListHeader *header)
{
    const AstraDrawListCommand *commands;

    if (builder == NULL || destination == 0u || header == NULL ||
        header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_2 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES ||
        !astra_words_zero(header->reserved, 10u))
        return 0;
    commands = (const AstraDrawListCommand *)(header + 1);
    for (uint32_t index = 0u; index < header->command_count; ++index) {
        const AstraDrawListCommand *item = &commands[index];
        int ok;

        if (!command_valid(header, item))
            return 0;
        if (item->operation == ASTRA_DRAW_LIST_FILL)
            ok = builder_fill(
                builder, destination, item->x, item->y,
                item->width, item->height, (uint16_t)item->foreground,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom);
        else if (item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED)
            ok = builder_rounded(
                builder, destination, item->x, item->y,
                item->width, item->height, (uint16_t)item->radius,
                (uint16_t)item->foreground,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom);
        else if (item->operation == ASTRA_DRAW_LIST_TEXT)
            ok = builder_text(
                builder, destination, item->x, item->y,
                (const char *)header + item->payload_offset,
                item->payload_bytes, item->font_height,
                0u, (uint16_t)item->foreground,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom, item->flags);
        else if (item->operation == ASTRA_DRAW_LIST_MONO_TEXT)
            ok = builder_text(
                builder, destination, item->x, item->y,
                (const char *)header + item->payload_offset,
                item->payload_bytes, item->font_height,
                (uint16_t)item->width, (uint16_t)item->foreground,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom, item->flags);
        else if (item->operation == ASTRA_DRAW_LIST_LINE)
            ok = builder_line(
                builder, destination, item->x, item->y,
                (int32_t)item->width, (int32_t)item->height,
                (uint16_t)item->foreground,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom);
        else
            ok = blit_region(builder, destination, destination, 0u, 0u,
                (int32_t)item->foreground, (int32_t)item->background,
                item->x, item->y, (uint16_t)item->width,
                (uint16_t)item->height,
                item->clip_left, item->clip_top,
                item->clip_right, item->clip_bottom);
        if (!ok)
            return 0;
    }
    return 1;
}

static int blit_region(AstraRenderBuilder *builder, uint32_t destination,
                       uint32_t source, uint32_t mask, uint16_t flags,
                       int32_t source_x, int32_t source_y,
                       int32_t destination_x, int32_t destination_y,
                       uint16_t width, uint16_t height,
                       int32_t clip_left, int32_t clip_top,
                       int32_t clip_right, int32_t clip_bottom)
{
    uint8_t *record;

    if (width == 0u || height == 0u)
        return 1;
    if (!command(builder, ASTRA_RENDER_OP_BLIT, flags, destination,
                 clip_left, clip_top, clip_right, clip_bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 36u, source);
    astra_store_be32(record + 40u, mask);
    astra_store_be32(record + 44u, pair_s16(source_x, source_y));
    astra_store_be32(record + 48u, pair_s16(destination_x, destination_y));
    astra_store_be32(record + 52u, pair_u16(width, height));
    astra_store_be32(record + 56u, pair_u16(width, height));
    return 1;
}

int astra_render_builder_blit_region(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t source_x, int32_t source_y, int32_t destination_x,
    int32_t destination_y, uint16_t width, uint16_t height)
{
    if (builder == NULL || destination == 0u || source == 0u)
        return 0;
    return blit_region(builder, destination, source, 0u, 0u,
                       source_x, source_y, destination_x, destination_y,
                       width, height, INT16_MIN, INT16_MIN,
                       INT16_MAX, INT16_MAX);
}

int astra_render_builder_blit_clipped(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t radius,
    int round_top, int32_t clip_left, int32_t clip_top,
    int32_t clip_right, int32_t clip_bottom)
{
    uint32_t bounded = radius;

    if (builder == NULL || destination == 0u || source == 0u || width == 0u ||
        height == 0u)
        return 0;
    if (bounded > width / 2u)
        bounded = width / 2u;
    if (bounded > height / 2u)
        bounded = height / 2u;
    if (bounded == 0u)
        return blit_region(builder, destination, source, 0u, 0u,
                           0, 0, x, y, width, height,
                           clip_left, clip_top, clip_right, clip_bottom);
    if (!blit_region(
            builder, destination, source, 0u, 0u,
            bounded, 0, x + (int32_t)bounded, y,
            (uint16_t)(width - bounded * 2u), height,
            clip_left, clip_top, clip_right, clip_bottom) ||
        !blit_region(
            builder, destination, source, 0u, 0u,
            0, round_top ? (int32_t)bounded : 0, x,
            y + (round_top ? (int32_t)bounded : 0),
            (uint16_t)bounded,
            (uint16_t)(height - bounded - (round_top ? bounded : 0u)),
            clip_left, clip_top, clip_right, clip_bottom) ||
        !blit_region(
            builder, destination, source, 0u, 0u,
            (int32_t)width - (int32_t)bounded,
            round_top ? (int32_t)bounded : 0,
            x + (int32_t)width - (int32_t)bounded,
            y + (round_top ? (int32_t)bounded : 0),
            (uint16_t)bounded,
            (uint16_t)(height - bounded - (round_top ? bounded : 0u)),
            clip_left, clip_top, clip_right, clip_bottom))
        return 0;
    for (uint32_t row = 0u; row < bounded;) {
        uint32_t inset = astra_graphics_rounded_inset(row, height, bounded);
        uint32_t end = row + 1u;
        uint16_t span = (uint16_t)(bounded - inset);
        uint16_t rows;
        int32_t right = (int32_t)width - (int32_t)bounded;
        int32_t bottom;

        while (end < bounded &&
               astra_graphics_rounded_inset(end, height, bounded) == inset)
            ++end;
        rows = (uint16_t)(end - row);
        bottom = (int32_t)height - (int32_t)end;
        if ((round_top &&
             (!blit_region(builder, destination, source, 0u, 0u,
                           (int32_t)inset, (int32_t)row,
                           x + (int32_t)inset, y + (int32_t)row,
                           span, rows, clip_left, clip_top,
                           clip_right, clip_bottom) ||
              !blit_region(builder, destination, source, 0u, 0u,
                           right, (int32_t)row, x + right,
                           y + (int32_t)row, span, rows,
                           clip_left, clip_top, clip_right, clip_bottom))) ||
            !blit_region(builder, destination, source, 0u, 0u,
                         (int32_t)inset, bottom, x + (int32_t)inset,
                         y + bottom, span, rows, clip_left, clip_top,
                         clip_right, clip_bottom) ||
            !blit_region(builder, destination, source, 0u, 0u,
                         right, bottom, x + right, y + bottom,
                         span, rows, clip_left, clip_top,
                         clip_right, clip_bottom))
            return 0;
        row = end;
    }
    return 1;
}

int astra_render_builder_blit(AstraRenderBuilder *builder,
                              uint32_t destination, uint32_t source,
                              int32_t x, int32_t y, uint16_t width,
                              uint16_t height, uint16_t radius,
                              int round_top)
{
    return astra_render_builder_blit_clipped(
        builder, destination, source, x, y, width, height, radius, round_top,
        INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX);
}

uint32_t astra_render_builder_finish(AstraRenderBuilder *builder)
{
    uint32_t bytes;
    uint32_t scene_bytes = 0u;

    if (builder == NULL || builder->failed != 0u ||
        (builder->command_count == 0u && builder->scene_offset == 0u))
        return 0u;
    if (builder->scene_offset != 0u) {
        uint8_t *scene = builder->bytes + relative(builder->scene_offset);
        uint32_t required = astra_window_scene_compiled_capacity(
            ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT,
            builder->scene_layer_count);

        if (required == 0u || required > builder->scene_output_capacity) {
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SCENE;
            return 0u;
        }
        scene_bytes = ASTRA_WINDOW_SCENE_HEADER_BYTES +
                      builder->scene_layer_count *
                          ASTRA_WINDOW_SCENE_LAYER_BYTES;
        astra_store_be32(scene + 8u, scene_bytes);
        astra_store_be32(scene + 20u, builder->scene_layer_count);
    }
    bytes = align_up(builder->data_cursor, 4u);
    astra_store_be32(builder->bytes + 0u, ASTRA_RENDER_BATCH_MAGIC);
    astra_store_be32(builder->bytes + 4u,
                     builder->scene_offset != 0u ?
                         ASTRA_RENDER_BATCH_VERSION_1_3 :
                         ASTRA_RENDER_BATCH_VERSION_1_2);
    astra_store_be32(builder->bytes + 8u, bytes);
    astra_store_be32(builder->bytes + 12u, builder->command_count);
    astra_store_be32(builder->bytes + 16u, ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    astra_store_be32(builder->bytes + 20u, ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    astra_store_be32(builder->bytes + 24u, builder->generation);
    astra_store_be32(builder->bytes + 28u,
          (builder->generation & 1u) != 0u ?
              ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
              ASTRA_RENDER_BATCH_SCANOUT0_OFFSET);
    astra_store_be32(builder->bytes + 48u, builder->scene_offset);
    astra_store_be32(builder->bytes + 52u, scene_bytes);
    return bytes;
}
