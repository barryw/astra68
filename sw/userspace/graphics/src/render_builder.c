#include <astra/render_builder.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/endian.h>
#include <astra/render_batch.h>
#include <astra/window_scene.h>
#include <astra/surface.h>
#include <astra/texture_reference.h>
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

static uint8_t *reserve_data(AstraRenderBuilder *builder, uint32_t bytes,
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
    return builder->bytes + cursor;
}

static uint8_t *allocate_data(AstraRenderBuilder *builder, uint32_t bytes,
                              uint32_t alignment, uint32_t *arena_offset)
{
    uint8_t *data = reserve_data(builder, bytes, alignment, arena_offset);

    if (data != NULL)
        memset(data, 0, bytes);
    return data;
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

int astra_render_builder_window_scene(AstraRenderBuilder *builder,
                                      uint32_t output_offset,
                                      uint32_t output_capacity,
                                      uint16_t width, uint16_t height,
                                      uint16_t backdrop_rgb565)
{
    uint32_t scene_offset;
    uint8_t *scene;
    uint32_t minimum_span_offset = align_up(
        ASTRA_WINDOW_SCENE_HEADER_BYTES +
            (uint32_t)height * ASTRA_WINDOW_SCENE_LINE_BYTES,
        64u);

    if (builder == NULL || builder->bytes == NULL ||
        width == 0u || height == 0u ||
        width > ASTRA_DISPLAY_WIDTH || height > ASTRA_DISPLAY_HEIGHT ||
        builder->scene_offset != 0u || (output_offset & 63u) != 0u ||
        (output_capacity & 63u) != 0u ||
        output_offset < ASTRA_RENDER_BATCH_WORKSPACE_LIMIT ||
        output_offset > ASTRA_RENDER_BATCH_MEDIA_LIMIT ||
        output_capacity < minimum_span_offset +
                              (uint32_t)height *
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
                     pair_u16(width, height));
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

uint32_t astra_render_format_row_bytes(uint8_t format, uint32_t width)
{
    switch (format) {
    case ASTRA_RENDER_FORMAT_INDEX8:
    case ASTRA_RENDER_FORMAT_A8:
        return width;
    case ASTRA_RENDER_FORMAT_RGB565:
        return width * 2u;
    case ASTRA_RENDER_FORMAT_XRGB8888:
    case ASTRA_RENDER_FORMAT_ARGB8888:
        return width * 4u;
    case ASTRA_RENDER_FORMAT_MASK1:
        return (width + 7u) / 8u;
    case ASTRA_RENDER_FORMAT_A4:
    case ASTRA_RENDER_FORMAT_INDEX4:
        return (width + 1u) / 2u;
    default:
        return 0u;
    }
}

uint32_t astra_render_builder_surface_format_at(
    AstraRenderBuilder *builder, uint32_t data_offset,
    uint32_t data_capacity, uint16_t width, uint16_t height, uint32_t pitch,
    uint8_t format, uint8_t access)
{
    uint32_t row = astra_render_format_row_bytes(format, width);
    uint64_t bytes = (uint64_t)pitch * height;

    if (builder == NULL || width == 0u || height == 0u ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION || row == 0u ||
        pitch < row || access == 0u ||
        (access & ~(ASTRA_RENDER_SURFACE_READ |
                    ASTRA_RENDER_SURFACE_WRITE)) != 0u ||
        data_offset < SURFACE_ARENA_LIMIT || (data_offset & 63u) != 0u ||
        bytes > data_capacity ||
        data_offset > ASTRA_RENDER_BATCH_MEDIA_LIMIT ||
        bytes > ASTRA_RENDER_BATCH_MEDIA_LIMIT - data_offset) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    return descriptor(builder, data_offset, (uint32_t)bytes, pitch, width,
                      height, format, access);
}

uint32_t astra_render_builder_surface_at(AstraRenderBuilder *builder,
                                         uint32_t data_offset,
                                         uint32_t data_capacity,
                                         uint16_t width, uint16_t height)
{
    return astra_render_builder_surface_format_at(
        builder, data_offset, data_capacity, width, height,
        (uint32_t)width * 2u, ASTRA_RENDER_FORMAT_RGB565,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
}

uint32_t astra_render_builder_upload(AstraRenderBuilder *builder,
                                     const void *pixels,
                                     uint32_t source_pitch, uint16_t width,
                                     uint16_t height, uint8_t format)
{
    uint32_t pitch = astra_render_format_row_bytes(format, width);
    uint32_t bytes = pitch * height;
    uint32_t offset;
    uint8_t *target;
    const uint8_t *source = pixels;

    if (builder == NULL || pixels == NULL || width == 0u || height == 0u ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION || pitch == 0u ||
        source_pitch < pitch ||
        (height - 1u) > (UINT32_MAX - pitch) / source_pitch ||
        bytes > ASTRA_RENDER_BATCH_MAX_BYTES) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    target = reserve_data(builder, bytes, 64u, &offset);
    if (target == NULL)
        return 0u;
    for (uint32_t row = 0u; row < height; ++row)
        memcpy(target + row * pitch, source + row * source_pitch, pitch);
    return descriptor(builder, offset, bytes, pitch, width, height, format,
                      ASTRA_RENDER_SURFACE_READ);
}

uint32_t astra_render_builder_upload_reserve(AstraRenderBuilder *builder,
                                             uint32_t source_pitch,
                                             uint16_t width, uint16_t height,
                                             uint8_t format,
                                             uint32_t *batch_offset)
{
    uint32_t row = astra_render_format_row_bytes(format, width);
    uint32_t record;
    uint32_t offset;

    if (builder == NULL || batch_offset == NULL || width == 0u ||
        height == 0u || width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION || row == 0u ||
        source_pitch < row ||
        source_pitch > ASTRA_RENDER_BATCH_MAX_BYTES / height) {
        if (builder != NULL)
            builder->failed = ASTRA_RENDER_BUILDER_FAILURE_SURFACE;
        return 0u;
    }
    /* The descriptor first, so the pixels end the batch's data. */
    record = descriptor(builder, 0u, source_pitch * height, source_pitch,
                        width, height, format, ASTRA_RENDER_SURFACE_READ);
    if (record == 0u ||
        reserve_data(builder, source_pitch * height, 64u, &offset) == NULL)
        return 0u;
    astra_store_be32(builder->bytes + relative(record) + 8u, offset);
    *batch_offset = offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    return record;
}

uint32_t astra_render_builder_upload_rgb565(AstraRenderBuilder *builder,
                                             const void *pixels,
                                             uint32_t source_pitch,
                                             uint16_t width, uint16_t height)
{
    return astra_render_builder_upload(builder, pixels, source_pitch, width,
                                       height, ASTRA_RENDER_FORMAT_RGB565);
}

static int builder_fill(AstraRenderBuilder *builder, uint32_t destination,
                        int32_t x, int32_t y, uint32_t width,
                        uint32_t height, uint32_t color,
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
                        uint32_t color, int32_t clip_left, int32_t clip_top,
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

static int builder_rounded(AstraRenderBuilder *builder, uint32_t destination,
                           int32_t x, int32_t y, uint32_t width,
                           uint32_t height, uint16_t radius, uint32_t color,
                           int32_t clip_left, int32_t clip_top,
                           int32_t clip_right, int32_t clip_bottom)
{
    uint32_t bounded = radius;

    if (width == 0u || height == 0u || width > UINT16_MAX ||
        height > UINT16_MAX)
        return 0;
    if (bounded > width / 2u)
        bounded = width / 2u;
    if (bounded > height / 2u)
        bounded = height / 2u;
    if (bounded == 0u)
        return builder_fill(builder, destination, x, y, width, height, color,
                            clip_left, clip_top, clip_right, clip_bottom);
    /* Use the scene clip's exact row coverage, so no corner pixel exposes
       old data from the alternating window caches. */
    for (uint32_t row = 0u; row < height;) {
        uint32_t inset = astra_graphics_rounded_inset(row, height, bounded);
        uint32_t end = row + 1u;

        while (end < height &&
               astra_graphics_rounded_inset(end, height, bounded) == inset)
            ++end;
        if (!builder_fill(builder, destination, x + (int32_t)inset,
                          y + (int32_t)row, width - inset * 2u,
                          end - row, color, clip_left, clip_top,
                          clip_right, clip_bottom))
            return 0;
        row = end;
    }
    return 1;
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
                            uint32_t color, int32_t clip_left,
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
                        uint16_t cell_width, uint32_t color,
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

int astra_render_builder_text_styled(AstraRenderBuilder *builder,
                                     uint32_t destination, int32_t x,
                                     int32_t y, const char *utf8,
                                     uint32_t length,
                                     uint16_t pixel_height, uint16_t color,
                                     uint32_t style_flags)
{
    return builder_text(builder, destination, x, y, utf8, length,
                        pixel_height, 0u, color,
                        INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX,
                        style_flags);
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

static int int16_coordinate(int32_t value)
{
    return value >= INT16_MIN && value <= INT16_MAX;
}

static int header_valid(const AstraDrawListHeader *header,
                        uint32_t area_bytes)
{
    uint32_t payload_offset;

    if (header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_5 ||
        !astra_words_zero(header->reserved, 9u) ||
        header->width == 0u || header->height == 0u ||
        header->command_capacity == 0u ||
        header->command_capacity >
            (ASTRA_DRAW_LIST_SESSION_BYTES_MAX -
             ASTRA_DRAW_LIST_HEADER_BYTES) / ASTRA_DRAW_LIST_COMMAND_BYTES ||
        header->total_bytes > area_bytes ||
        header->total_bytes > ASTRA_DRAW_LIST_SESSION_BYTES_MAX ||
        header->command_count > header->command_capacity)
        return 0;
    payload_offset = astra_draw_list_payload_offset(header->command_capacity);
    return payload_offset <= header->total_bytes &&
           header->payload_bytes <= header->total_bytes - payload_offset;
}

static int no_source(const AstraDrawListCommand *item)
{
    return item->source == 0u && item->source_x == 0 &&
           item->source_y == 0 && item->source_width == 0u &&
           item->source_height == 0u;
}

static int blend_valid(uint32_t flags)
{
    return ASTRA_DRAW_LIST_BLEND_MODE(flags) <= ASTRA_DRAW_LIST_BLEND_MUL;
}

enum {
    TRIANGLE_LIST_BYTES = 3u * (uint32_t)sizeof(AstraDrawListVertex),
    TRIANGLE_RECORD_BYTES = 3u * ASTRA_RENDER_TRIANGLE_VERTEX_BYTES,
};

static int command_valid(const AstraDrawListHeader *header,
                         const AstraDrawListCommand *item)
{
    uint32_t payload_offset =
        astra_draw_list_payload_offset(header->command_capacity);
    uint64_t end = (uint64_t)item->payload_offset + item->payload_bytes;
    int no_payload = item->payload_offset == 0u && item->payload_bytes == 0u;
    int payload_inside = item->payload_bytes != 0u &&
                         item->payload_offset >= payload_offset &&
                         end <= (uint64_t)payload_offset +
                                    header->payload_bytes;

    if (item->reserved16 != 0u ||
        item->clip_left >= item->clip_right ||
        item->clip_right > header->width ||
        item->clip_top >= item->clip_bottom ||
        item->clip_bottom > header->height)
        return 0;
    if (item->operation == ASTRA_DRAW_LIST_FILL ||
        item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED)
        return (item->flags & ~(item->operation == ASTRA_DRAW_LIST_FILL ?
                                    ASTRA_DRAW_LIST_BLEND_MASK : 0u)) == 0u &&
               blend_valid(item->flags) &&
               item->width != 0u && item->height != 0u &&
               item->width <= UINT16_MAX && item->height <= UINT16_MAX &&
               int16_coordinate(item->x) && int16_coordinate(item->y) &&
               (item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED ||
                item->radius == 0u) &&
               no_source(item) && no_payload && item->font_height == 0u;
    if (item->operation == ASTRA_DRAW_LIST_TEXT ||
        item->operation == ASTRA_DRAW_LIST_MONO_TEXT)
        return (item->flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) == 0u &&
               (item->operation == ASTRA_DRAW_LIST_TEXT ?
                    item->width == 0u : item->width != 0u) &&
               item->width <= UINT16_MAX &&
               item->height == 0u && item->radius == 0u &&
               int16_coordinate(item->x) && int16_coordinate(item->y) &&
               no_source(item) && payload_inside;
    if (item->operation == ASTRA_DRAW_LIST_BLIT)
        return (item->flags & ~(ASTRA_DRAW_LIST_FLIP_X |
                                ASTRA_DRAW_LIST_FLIP_Y |
                                ASTRA_DRAW_LIST_BLEND_MASK |
                                ASTRA_DRAW_LIST_FILTER_LINEAR)) == 0u &&
               blend_valid(item->flags) &&
               item->width != 0u && item->height != 0u &&
               item->width <= UINT16_MAX && item->height <= UINT16_MAX &&
               int16_coordinate(item->x) && int16_coordinate(item->y) &&
               item->source_x >= 0 && item->source_y >= 0 &&
               item->source_width != 0u && item->source_height != 0u &&
               item->radius == 0u && no_payload && item->font_height == 0u;
    if (item->operation == ASTRA_DRAW_LIST_LINE)
        /* A LINE blends only as opaque: SDL lowers translucent lines to
           spans of blended fills. */
        return (item->flags & ~ASTRA_DRAW_LIST_BLEND_MASK) == 0u &&
               ASTRA_DRAW_LIST_BLEND_MODE(item->flags) <=
                   ASTRA_DRAW_LIST_BLEND_BLEND &&
               item->radius == 0u && no_source(item) && no_payload &&
               item->font_height == 0u &&
               int16_coordinate(item->x) && int16_coordinate(item->y) &&
               int16_coordinate((int32_t)item->width) &&
               int16_coordinate((int32_t)item->height);
    if (item->operation == ASTRA_DRAW_LIST_TRIANGLES)
        return (item->flags & ~(ASTRA_DRAW_LIST_BLEND_MASK |
                                ASTRA_DRAW_LIST_FILTER_LINEAR)) == 0u &&
               blend_valid(item->flags) &&
               (item->source != 0u ||
                (item->flags & ASTRA_DRAW_LIST_FILTER_LINEAR) == 0u) &&
               item->x == 0 && item->y == 0 && item->width == 0u &&
               item->height == 0u && item->color == 0u &&
               item->radius == 0u && item->font_height == 0u &&
               item->source_x == 0 && item->source_y == 0 &&
               item->source_width == 0u && item->source_height == 0u &&
               payload_inside && (item->payload_offset & 3u) == 0u &&
               item->payload_bytes % TRIANGLE_LIST_BYTES == 0u;
    if (item->operation == ASTRA_DRAW_LIST_FILL_RECTS)
        return (item->flags & ~ASTRA_DRAW_LIST_BLEND_MASK) == 0u &&
               blend_valid(item->flags) &&
               item->x == 0 && item->y == 0 && item->width == 0u &&
               item->height == 0u && item->radius == 0u &&
               item->font_height == 0u && no_source(item) &&
               payload_inside && (item->payload_offset & 3u) == 0u &&
               item->payload_bytes % sizeof(AstraDrawListRect) == 0u;
    if (item->operation == ASTRA_DRAW_LIST_LINES)
        /* Opaque only, as LINE. */
        return (item->flags & ~ASTRA_DRAW_LIST_BLEND_MASK) == 0u &&
               ASTRA_DRAW_LIST_BLEND_MODE(item->flags) <=
                   ASTRA_DRAW_LIST_BLEND_BLEND &&
               item->x == 0 && item->y == 0 && item->width == 0u &&
               item->height == 0u && item->radius == 0u &&
               item->font_height == 0u && no_source(item) &&
               payload_inside && (item->payload_offset & 3u) == 0u &&
               item->payload_bytes % sizeof(AstraDrawListSegment) == 0u;
    return 0;
}

int astra_draw_list_covers(const AstraDrawListHeader *shared,
                           uint16_t width, uint16_t height)
{
    AstraDrawListHeader header;
    AstraDrawListCommand first;

    if (shared == NULL)
        return 0;
    header = *shared;
    if (!header_valid(&header, ASTRA_DRAW_LIST_AREA_BYTES) ||
        header.command_count == 0u)
        return 0;
    first = *(const AstraDrawListCommand *)(shared + 1);
    if (!command_valid(&header, &first) ||
        first.operation != ASTRA_DRAW_LIST_FILL ||
        (ASTRA_DRAW_LIST_BLEND_MODE(first.flags) !=
             ASTRA_DRAW_LIST_BLEND_NONE &&
         (ASTRA_DRAW_LIST_BLEND_MODE(first.flags) !=
              ASTRA_DRAW_LIST_BLEND_BLEND ||
          (first.color >> 24) != 255u)))
        return 0;
    return first.clip_left == 0u && first.clip_top == 0u &&
           first.clip_right >= width && first.clip_bottom >= height &&
           first.x <= 0 && first.y <= 0 &&
           (int64_t)first.x + first.width >= (int64_t)width &&
           (int64_t)first.y + first.height >= (int64_t)height;
}

static int blit_region(AstraRenderBuilder *builder, uint32_t destination,
                       uint32_t source, uint32_t mask, uint16_t flags,
                       int32_t source_x, int32_t source_y,
                       int32_t destination_x, int32_t destination_y,
                       uint16_t width, uint16_t height,
                       int32_t clip_left, int32_t clip_top,
                       int32_t clip_right, int32_t clip_bottom);

static int blit_scaled(AstraRenderBuilder *builder, uint32_t destination,
                       uint32_t source, uint16_t flags, uint32_t options,
                       int32_t source_x, int32_t source_y,
                       uint16_t source_width, uint16_t source_height,
                       int32_t destination_x, int32_t destination_y,
                       uint16_t width, uint16_t height,
                       int32_t clip_left, int32_t clip_top,
                       int32_t clip_right, int32_t clip_bottom)
{
    uint8_t *record;

    if (!command(builder, ASTRA_RENDER_OP_BLIT, flags, destination,
                 clip_left, clip_top, clip_right, clip_bottom, &record))
        return 0;
    if (record == NULL)
        return 1;
    astra_store_be32(record + 36u, source);
    astra_store_be32(record + 44u, pair_s16(source_x, source_y));
    astra_store_be32(record + 48u, pair_s16(destination_x, destination_y));
    astra_store_be32(record + 52u, pair_u16(source_width, source_height));
    astra_store_be32(record + 56u, pair_u16(width, height));
    astra_store_be32(record + 60u, options);
    return 1;
}

static uint8_t descriptor_format(const uint8_t *record)
{
    return (uint8_t)(astra_load_be32(record + 24u) >> 24);
}

uint32_t astra_render_builder_destination_color(uint8_t format,
                                                uint32_t argb)
{
    uint32_t red = (argb >> 16) & 0xffu;
    uint32_t green = (argb >> 8) & 0xffu;
    uint32_t blue = argb & 0xffu;

    switch (format) {
    case ASTRA_RENDER_FORMAT_RGB565:
        /* Round to nearest, as the hardware converts. */
        return (((red * 31u + 127u) / 255u) << 11) |
               (((green * 63u + 127u) / 255u) << 5) |
               ((blue * 31u + 127u) / 255u);
    case ASTRA_RENDER_FORMAT_XRGB8888:
        return argb & UINT32_C(0x00ffffff);
    case ASTRA_RENDER_FORMAT_INDEX8:
        return blue;
    default:
        return argb;
    }
}

static int draw_target_format(uint8_t format)
{
    return format == ASTRA_RENDER_FORMAT_RGB565 ||
           format == ASTRA_RENDER_FORMAT_XRGB8888 ||
           format == ASTRA_RENDER_FORMAT_ARGB8888 ||
           format == ASTRA_RENDER_FORMAT_INDEX8;
}

/* Destinations of blending, the texture engine, and every direct-color
   operation (docs/TEXTURE_ENGINE.md sections 6 and 7). */
static int direct_format(uint8_t format)
{
    return format == ASTRA_RENDER_FORMAT_RGB565 ||
           format == ASTRA_RENDER_FORMAT_XRGB8888 ||
           format == ASTRA_RENDER_FORMAT_ARGB8888;
}

/* Glyph runs write opaque destination pixels; ARGB8888 targets are not
   part of that contract. */
static int text_format(uint8_t format)
{
    return format == ASTRA_RENDER_FORMAT_RGB565 ||
           format == ASTRA_RENDER_FORMAT_XRGB8888;
}

static uint32_t triangle_options(uint32_t flags)
{
    return ASTRA_DRAW_LIST_BLEND_MODE(flags) |
           ((flags & ASTRA_DRAW_LIST_FILTER_LINEAR) != 0u ?
                ASTRA_RENDER_TRIANGLE_OPTION_FILTER_LINEAR : 0u);
}

static int triangle_vertex_valid(const AstraDrawListVertex *vertex,
                                 int textured)
{
    return vertex->x >= -ASTRA_TEXTURE_COORD_LIMIT &&
           vertex->x < ASTRA_TEXTURE_COORD_LIMIT &&
           vertex->y >= -ASTRA_TEXTURE_COORD_LIMIT &&
           vertex->y < ASTRA_TEXTURE_COORD_LIMIT &&
           (textured || (vertex->u == 0 && vertex->v == 0));
}

/*
 * TRIANGLES commands of at most ASTRA_RENDER_MAX_TRIANGLES each, vertex
 * arrays in the batch data arena (docs/TEXTURE_ENGINE.md section 1). Each
 * vertex is copied once out of @p vertices, which may be client memory, and
 * validated on that private copy.
 */
static int builder_triangles(AstraRenderBuilder *builder,
                             uint32_t destination, uint32_t source,
                             uint32_t options,
                             const AstraDrawListVertex *vertices,
                             uint32_t triangles, int32_t clip_left,
                             int32_t clip_top, int32_t clip_right,
                             int32_t clip_bottom)
{
    for (uint32_t done = 0u; done < triangles;) {
        uint32_t count = triangles - done < ASTRA_RENDER_MAX_TRIANGLES ?
                         triangles - done : ASTRA_RENDER_MAX_TRIANGLES;
        uint32_t offset;
        uint8_t *record;
        uint8_t *out;

        if (!command(builder, ASTRA_RENDER_OP_TRIANGLES, 0u, destination,
                     clip_left, clip_top, clip_right, clip_bottom, &record))
            return 0;
        if (record == NULL)
            return 1;
        out = reserve_data(builder, count * TRIANGLE_RECORD_BYTES,
                           ASTRA_RENDER_TRIANGLE_VERTEX_BYTES, &offset);
        if (out == NULL)
            return 0;
        for (uint32_t index = 0u; index < count * 3u; ++index) {
            AstraDrawListVertex vertex = vertices[done * 3u + index];

            if (!triangle_vertex_valid(&vertex, source != 0u))
                return 0;
            astra_store_be32(out + 0u, (uint32_t)vertex.x);
            astra_store_be32(out + 4u, (uint32_t)vertex.y);
            astra_store_be32(out + 8u, (uint32_t)vertex.u);
            astra_store_be32(out + 12u, (uint32_t)vertex.v);
            astra_store_be32(out + 16u, vertex.color);
            astra_store_be32(out + 20u, 0u);
            astra_store_be32(out + 24u, 0u);
            astra_store_be32(out + 28u, 0u);
            out += ASTRA_RENDER_TRIANGLE_VERTEX_BYTES;
        }
        astra_store_be32(record + 36u, source);
        astra_store_be32(record + 40u, offset);
        astra_store_be32(record + 44u, count);
        astra_store_be32(record + 48u, options);
        done += count;
    }
    return 1;
}

/* A rectangle as two triangles sharing the right-top to left-bottom
   diagonal; the top-left rule draws each pixel once. (u0, v0) maps to the
   left/top edges and (u1, v1) to the right/bottom edges. */
static int builder_quad(AstraRenderBuilder *builder, uint32_t destination,
                        uint32_t source, uint32_t options, int32_t x,
                        int32_t y, uint32_t width, uint32_t height,
                        int32_t u0, int32_t v0, int32_t u1, int32_t v1,
                        uint32_t color, int32_t clip_left, int32_t clip_top,
                        int32_t clip_right, int32_t clip_bottom)
{
    int64_t x0 = x;
    int64_t y0 = y;
    int64_t x1 = (int64_t)x + width;
    int64_t y1 = (int64_t)y + height;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    /* An ADLT rectangle can reach past the engine's +-32768 pixel vertex
       range. Only then is it cut to the clip (which lies inside a surface),
       moving the texel edges proportionally. */
    if (x0 < -32768 || y0 < -32768 || x1 >= 32768 || y1 >= 32768) {
        int64_t cut_left = clip_left > x0 ? clip_left : x0;
        int64_t cut_top = clip_top > y0 ? clip_top : y0;
        int64_t cut_right = clip_right < x1 ? clip_right : x1;
        int64_t cut_bottom = clip_bottom < y1 ? clip_bottom : y1;
        int64_t du = (int64_t)u1 - u0;
        int64_t dv = (int64_t)v1 - v0;

        if (cut_right > ASTRA_RENDER_MAX_SURFACE_DIMENSION)
            cut_right = ASTRA_RENDER_MAX_SURFACE_DIMENSION;
        if (cut_bottom > ASTRA_RENDER_MAX_SURFACE_DIMENSION)
            cut_bottom = ASTRA_RENDER_MAX_SURFACE_DIMENSION;
        if (cut_left >= cut_right || cut_top >= cut_bottom)
            return 1;
        u1 = (int32_t)(u0 + du * (cut_right - x0) / (int64_t)width);
        u0 = (int32_t)(u0 + du * (cut_left - x0) / (int64_t)width);
        v1 = (int32_t)(v0 + dv * (cut_bottom - y0) / (int64_t)height);
        v0 = (int32_t)(v0 + dv * (cut_top - y0) / (int64_t)height);
        x0 = cut_left;
        y0 = cut_top;
        x1 = cut_right;
        y1 = cut_bottom;
    }
    left = (int32_t)x0 * 256;
    top = (int32_t)y0 * 256;
    right = (int32_t)x1 * 256;
    bottom = (int32_t)y1 * 256;
    const AstraDrawListVertex quad[6] = {
        { left, top, u0, v0, color },
        { right, top, u1, v0, color },
        { left, bottom, u0, v1, color },
        { right, top, u1, v0, color },
        { right, bottom, u1, v1, color },
        { left, bottom, u0, v1, color },
    };

    return builder_triangles(builder, destination, source, options, quad, 2u,
                             clip_left, clip_top, clip_right, clip_bottom);
}

/*
 * FILL_RECTS commands of at most ASTRA_RENDER_MAX_FILL_RECTS records in the
 * batch data arena, all in @p color: the destination format, or with
 * ASTRA_RENDER_FILL_RECTS_OPTION_BLEND straight-alpha ARGB8888. Each
 * rectangle is copied once out of @p rects, which may be client memory;
 * empty ones and ones wholly outside the clip are dropped, so a list that
 * draws nothing emits nothing.
 */
static int builder_fill_rects(AstraRenderBuilder *builder,
                              uint32_t destination, uint32_t color,
                              uint32_t options,
                              const AstraDrawListRect *rects, uint32_t total,
                              int32_t clip_left, int32_t clip_top,
                              int32_t clip_right, int32_t clip_bottom)
{
    for (uint32_t done = 0u; done < total;) {
        uint32_t limit = total - done < ASTRA_RENDER_MAX_FILL_RECTS ?
                         total - done : ASTRA_RENDER_MAX_FILL_RECTS;
        uint32_t cursor = builder->data_cursor;
        uint32_t offset;
        uint32_t count = 0u;
        uint8_t *record;
        uint8_t *out = reserve_data(builder,
                                    limit * ASTRA_RENDER_FILL_RECT_BYTES,
                                    ASTRA_RENDER_FILL_RECT_BYTES, &offset);

        if (out == NULL)
            return 0;
        for (; done < total && count < limit; ++done) {
            AstraDrawListRect rect = rects[done];

            if (rect.width == 0u || rect.height == 0u ||
                rect.x >= clip_right || rect.y >= clip_bottom ||
                rect.x + (int32_t)rect.width <= clip_left ||
                rect.y + (int32_t)rect.height <= clip_top)
                continue;
            astra_store_be32(out + 0u, pair_s16(rect.x, rect.y));
            astra_store_be32(out + 4u, pair_u16(rect.width, rect.height));
            astra_store_be32(out + 8u, 0u);
            astra_store_be32(out + 12u, 0u);
            out += ASTRA_RENDER_FILL_RECT_BYTES;
            ++count;
        }
        /* Give back what culling left unused; nothing follows it yet. */
        builder->data_cursor = relative(offset) +
                               count * ASTRA_RENDER_FILL_RECT_BYTES;
        if (count == 0u) {
            builder->data_cursor = cursor;
            continue;
        }
        if (!command(builder, ASTRA_RENDER_OP_FILL_RECTS, 0u, destination,
                     clip_left, clip_top, clip_right, clip_bottom, &record))
            return 0;
        if (record == NULL) {
            /* The clip misses the destination: nothing draws. */
            builder->data_cursor = cursor;
            return 1;
        }
        astra_store_be32(record + 40u, offset);
        astra_store_be32(record + 44u, count);
        astra_store_be32(record + 48u, options);
        astra_store_be32(record + 60u, color);
    }
    return 1;
}

/* The hardware blends FILL_RECTS into direct-color destinations whose rows
   start on eight bytes. */
static int rects_blend(AstraRenderBuilder *builder, uint32_t destination,
                       uint8_t format)
{
    const uint8_t *record = descriptor_record(builder, destination);

    return record != NULL && direct_format(format) &&
           (astra_load_be32(record + 16u) & 7u) == 0u;
}

/*
 * LINES commands of at most ASTRA_RENDER_MAX_LINE_SEGMENTS segments in the
 * batch data arena, all in @p color (destination format). Each segment is
 * copied once out of @p segments; ones whose bounds miss the clip are
 * dropped.
 */
static int builder_lines(AstraRenderBuilder *builder, uint32_t destination,
                         uint32_t color, const AstraDrawListSegment *segments,
                         uint32_t total, int32_t clip_left, int32_t clip_top,
                         int32_t clip_right, int32_t clip_bottom)
{
    for (uint32_t done = 0u; done < total;) {
        uint32_t limit = total - done < ASTRA_RENDER_MAX_LINE_SEGMENTS ?
                         total - done : ASTRA_RENDER_MAX_LINE_SEGMENTS;
        uint32_t cursor = builder->data_cursor;
        uint32_t offset;
        uint32_t count = 0u;
        uint8_t *record;
        uint8_t *out = reserve_data(builder,
                                    limit * ASTRA_RENDER_LINE_SEGMENT_BYTES,
                                    ASTRA_RENDER_LINE_SEGMENT_BYTES, &offset);

        if (out == NULL)
            return 0;
        for (; done < total && count < limit; ++done) {
            AstraDrawListSegment segment = segments[done];

            if ((segment.x0 < clip_left && segment.x1 < clip_left) ||
                (segment.y0 < clip_top && segment.y1 < clip_top) ||
                (segment.x0 >= clip_right && segment.x1 >= clip_right) ||
                (segment.y0 >= clip_bottom && segment.y1 >= clip_bottom))
                continue;
            astra_store_be32(out + 0u, pair_s16(segment.x0, segment.y0));
            astra_store_be32(out + 4u, pair_s16(segment.x1, segment.y1));
            astra_store_be32(out + 8u, 0u);
            astra_store_be32(out + 12u, 0u);
            out += ASTRA_RENDER_LINE_SEGMENT_BYTES;
            ++count;
        }
        builder->data_cursor = relative(offset) +
                               count * ASTRA_RENDER_LINE_SEGMENT_BYTES;
        if (count == 0u) {
            builder->data_cursor = cursor;
            continue;
        }
        if (!command(builder, ASTRA_RENDER_OP_LINES, 0u, destination,
                     clip_left, clip_top, clip_right, clip_bottom, &record))
            return 0;
        if (record == NULL) {
            builder->data_cursor = cursor;
            return 1;
        }
        astra_store_be32(record + 40u, offset);
        astra_store_be32(record + 44u, count);
        astra_store_be32(record + 60u, color);
    }
    return 1;
}

/* A translucent fill is a blended rectangle list of one; where the
   hardware cannot blend the list, a one-pixel ARGB source scaled over the
   rectangle on the blitter's source-over path, which it matches bit for
   bit. */
static int replay_fill(AstraRenderBuilder *builder, uint32_t destination,
                       uint8_t format, const AstraDrawListCommand *item)
{
    uint32_t mode = ASTRA_DRAW_LIST_BLEND_MODE(item->flags);
    uint32_t alpha = item->color >> 24;
    uint32_t data_offset;
    uint32_t source;
    uint8_t *pixel;

    if (mode > ASTRA_DRAW_LIST_BLEND_BLEND)
        /* ADD, MOD, and MUL need the texture engine's blend stage. */
        return direct_format(format) &&
               builder_quad(builder, destination, 0u, mode, item->x,
                            item->y, item->width, item->height, 0, 0, 0, 0,
                            item->color, item->clip_left, item->clip_top,
                            item->clip_right, item->clip_bottom);
    if (mode == ASTRA_DRAW_LIST_BLEND_NONE || alpha == 255u)
        return builder_fill(
            builder, destination, item->x, item->y, item->width,
            item->height,
            astra_render_builder_destination_color(format, item->color),
            item->clip_left, item->clip_top, item->clip_right,
            item->clip_bottom);
    if (!direct_format(format))
        return 0;
    if (alpha == 0u)
        return 1;
    if (rects_blend(builder, destination, format)) {
        const AstraDrawListRect rect = {
            (int16_t)item->x, (int16_t)item->y, (uint16_t)item->width,
            (uint16_t)item->height };

        return builder_fill_rects(builder, destination, item->color,
                                  ASTRA_RENDER_FILL_RECTS_OPTION_BLEND, &rect,
                                  1u, item->clip_left, item->clip_top,
                                  item->clip_right, item->clip_bottom);
    }
    pixel = reserve_data(builder, 4u, 64u, &data_offset);
    if (pixel == NULL)
        return 0;
    astra_store_be32(pixel, item->color);
    source = descriptor(builder, data_offset, 4u, 4u, 1u, 1u,
                        ASTRA_RENDER_FORMAT_ARGB8888,
                        ASTRA_RENDER_SURFACE_READ);
    return source != 0u &&
           blit_scaled(builder, destination, source,
                       ASTRA_RENDER_FLAG_BLIT_ALPHA,
                       UINT32_C(255) << ASTRA_RENDER_BLIT_OPTION_OPACITY_SHIFT,
                       0, 0, 1u, 1u, item->x, item->y,
                       (uint16_t)item->width, (uint16_t)item->height,
                       item->clip_left, item->clip_top, item->clip_right,
                       item->clip_bottom);
}

/* Opaque and translucent BLEND rectangles are one hardware list; ADD, MOD,
   and MUL, and BLEND the list cannot do, lower one by one as FILL does. */
static int replay_fill_rects(AstraRenderBuilder *builder,
                             uint32_t destination, uint8_t format,
                             const AstraDrawListHeader *header,
                             const AstraDrawListCommand *item)
{
    const AstraDrawListRect *rects = (const AstraDrawListRect *)(const void *)
        ((const uint8_t *)header + item->payload_offset);
    uint32_t total = item->payload_bytes / sizeof(AstraDrawListRect);
    uint32_t mode = ASTRA_DRAW_LIST_BLEND_MODE(item->flags);
    AstraDrawListCommand fill = *item;

    if (mode == ASTRA_DRAW_LIST_BLEND_NONE ||
        (mode == ASTRA_DRAW_LIST_BLEND_BLEND && (item->color >> 24) == 255u))
        return builder_fill_rects(
            builder, destination,
            astra_render_builder_destination_color(format, item->color), 0u,
            rects, total, item->clip_left, item->clip_top, item->clip_right,
            item->clip_bottom);
    if (mode == ASTRA_DRAW_LIST_BLEND_BLEND &&
        rects_blend(builder, destination, format))
        return (item->color >> 24) == 0u ||
               builder_fill_rects(
                   builder, destination, item->color,
                   ASTRA_RENDER_FILL_RECTS_OPTION_BLEND, rects, total,
                   item->clip_left, item->clip_top, item->clip_right,
                   item->clip_bottom);
    fill.operation = ASTRA_DRAW_LIST_FILL;
    fill.payload_offset = 0u;
    fill.payload_bytes = 0u;
    for (uint32_t index = 0u; index < total; ++index) {
        AstraDrawListRect rect = rects[index];

        if (rect.width == 0u || rect.height == 0u)
            continue;
        fill.x = rect.x;
        fill.y = rect.y;
        fill.width = rect.width;
        fill.height = rect.height;
        if (!replay_fill(builder, destination, format, &fill))
            return 0;
    }
    return 1;
}

/*
 * Linear filtering is a request, not a contract: the texture engine's
 * bilinear path costs about 74 cycles per pixel (TEXTURE_ENGINE.md, 2.2
 * Mpixel/s at 165 MHz), where a plain scaled copy runs in the blitter's
 * pixel mode at under two. A filtered copy is kept only while it is scaled
 * and its visible area fits this budget -- 16384 pixels, about 7.4 ms, half
 * a 60 Hz frame. Larger, it is drawn nearest: a full-screen filtered copy
 * would take over a second and miss the command deadline, and a program's
 * appetite must never cost the display.
 * ponytail: fixed budget; measure the engine's rate per board if it varies.
 */
#define LINEAR_FILTER_BUDGET_PIXELS 16384u

static uint32_t visible_pixels(const AstraDrawListCommand *item)
{
    int64_t left = item->x > item->clip_left ? item->x : item->clip_left;
    int64_t top = item->y > item->clip_top ? item->y : item->clip_top;
    int64_t right = (int64_t)item->x + item->width;
    int64_t bottom = (int64_t)item->y + item->height;

    if (right > item->clip_right)
        right = item->clip_right;
    if (bottom > item->clip_bottom)
        bottom = item->clip_bottom;
    if (right <= left || bottom <= top)
        return 0u;
    return (uint64_t)(right - left) * (uint64_t)(bottom - top) > UINT32_MAX ?
        UINT32_MAX : (uint32_t)((right - left) * (bottom - top));
}

static uint32_t affordable_blit_flags(const AstraDrawListCommand *item)
{
    int scaled = item->source_width != item->width ||
                 item->source_height != item->height;

    if ((item->flags & ASTRA_DRAW_LIST_FILTER_LINEAR) != 0u &&
        (!scaled || visible_pixels(item) > LINEAR_FILTER_BUDGET_PIXELS))
        return item->flags & ~(uint32_t)ASTRA_DRAW_LIST_FILTER_LINEAR;
    return item->flags;
}

static int replay_blit(AstraRenderBuilder *builder, uint32_t destination,
                       uint8_t format,
                       const AstraRenderSourceResolver *resolver,
                       const AstraDrawListCommand *item)
{
    uint32_t source = item->source == ASTRA_DRAW_LIST_SOURCE_DESTINATION ?
        destination :
        resolver != NULL && resolver->resolve != NULL ?
            resolver->resolve(resolver->context, builder, item->source) : 0u;
    const uint8_t *source_record = descriptor_record(builder, source);
    const uint8_t *destination_record =
        descriptor_record(builder, destination);
    uint32_t item_flags = affordable_blit_flags(item);
    uint32_t opacity = item->color >> 24;
    uint32_t mode = ASTRA_DRAW_LIST_BLEND_MODE(item_flags);
    uint32_t source_size;
    uint8_t source_format;
    uint16_t flags = 0u;
    uint32_t options = 0u;
    int scaled = item->source_width != item->width ||
                 item->source_height != item->height;
    /* Color modulation, ADD/MOD/MUL, and filtering are the texture
       engine's; plain and alpha copies keep the blitter's faster path. */
    int engine = (item->color & UINT32_C(0x00ffffff)) !=
                     UINT32_C(0x00ffffff) ||
                 mode > ASTRA_DRAW_LIST_BLEND_BLEND ||
                 (item_flags & ASTRA_DRAW_LIST_FILTER_LINEAR) != 0u;
    int same;

    if (source_record == NULL || destination_record == NULL)
        return 0;
    source_size = astra_load_be32(source_record + 20u);
    source_format = descriptor_format(source_record);
    same = astra_load_be32(source_record + 8u) ==
           astra_load_be32(destination_record + 8u);
    if ((uint32_t)item->source_x + item->source_width > (source_size >> 16) ||
        (uint32_t)item->source_y + item->source_height >
            (source_size & 0xffffu) ||
        source_format > ASTRA_RENDER_FORMAT_ARGB8888 ||
        (source_format == ASTRA_RENDER_FORMAT_INDEX8) !=
            (format == ASTRA_RENDER_FORMAT_INDEX8) ||
        (same && (scaled || item_flags != 0u || engine)))
        return 0;
    if (engine) {
        int32_t u0 = (int32_t)item->source_x * 65536;
        int32_t u1 = ((int32_t)item->source_x + item->source_width) * 65536;
        int32_t v0 = (int32_t)item->source_y * 65536;
        int32_t v1 = ((int32_t)item->source_y + item->source_height) *
                     65536;
        int flip_x = (item_flags & ASTRA_DRAW_LIST_FLIP_X) != 0u;
        int flip_y = (item_flags & ASTRA_DRAW_LIST_FLIP_Y) != 0u;

        if (!direct_format(format) || !direct_format(source_format))
            return 0;
        return builder_quad(
            builder, destination, source, triangle_options(item_flags),
            item->x, item->y, item->width, item->height,
            flip_x ? u1 : u0, flip_y ? v1 : v0, flip_x ? u0 : u1,
            flip_y ? v0 : v1, item->color, item->clip_left, item->clip_top,
            item->clip_right, item->clip_bottom);
    }
    if ((item_flags & ASTRA_DRAW_LIST_FLIP_X) != 0u)
        flags |= ASTRA_RENDER_FLAG_BLIT_REFLECT_X;
    if ((item_flags & ASTRA_DRAW_LIST_FLIP_Y) != 0u)
        flags |= ASTRA_RENDER_FLAG_BLIT_REFLECT_Y;
    if (mode == ASTRA_DRAW_LIST_BLEND_BLEND &&
        (opacity != 255u ||
         source_format == ASTRA_RENDER_FORMAT_ARGB8888)) {
        if (!direct_format(format))
            return 0;
        if (opacity == 0u)
            return 1;
        flags |= ASTRA_RENDER_FLAG_BLIT_ALPHA;
        options = opacity << ASTRA_RENDER_BLIT_OPTION_OPACITY_SHIFT;
    }
    return blit_scaled(builder, destination, source, flags, options,
                       item->source_x, item->source_y, item->source_width,
                       item->source_height, item->x, item->y,
                       (uint16_t)item->width, (uint16_t)item->height,
                       item->clip_left, item->clip_top, item->clip_right,
                       item->clip_bottom);
}

static int replay_triangles(AstraRenderBuilder *builder,
                            uint32_t destination, uint8_t format,
                            const AstraDrawListHeader *header,
                            const AstraRenderSourceResolver *resolver,
                            const AstraDrawListCommand *item)
{
    uint32_t source = 0u;

    if (!direct_format(format))
        return 0;
    if (item->source != 0u) {
        const uint8_t *source_record;
        const uint8_t *destination_record =
            descriptor_record(builder, destination);

        source = resolver != NULL && resolver->resolve != NULL ?
            resolver->resolve(resolver->context, builder, item->source) : 0u;
        source_record = descriptor_record(builder, source);
        /* INDEX8 needs a palette the service does not carry yet, and a
           surface cannot texture itself. */
        if (source_record == NULL || destination_record == NULL ||
            !direct_format(descriptor_format(source_record)) ||
            astra_load_be32(source_record + 8u) ==
                astra_load_be32(destination_record + 8u))
            return 0;
    }
    return builder_triangles(
        builder, destination, source, triangle_options(item->flags),
        (const AstraDrawListVertex *)(const void *)
            ((const uint8_t *)header + item->payload_offset),
        item->payload_bytes / TRIANGLE_LIST_BYTES, item->clip_left,
        item->clip_top, item->clip_right, item->clip_bottom);
}

static int replay_command(AstraRenderBuilder *builder, uint32_t destination,
                          uint8_t format, const AstraDrawListHeader *header,
                          const AstraRenderSourceResolver *resolver,
                          const AstraDrawListCommand *item)
{
    uint32_t color = astra_render_builder_destination_color(format,
                                                            item->color);

    switch (item->operation) {
    case ASTRA_DRAW_LIST_FILL:
        return replay_fill(builder, destination, format, item);
    case ASTRA_DRAW_LIST_FILL_ROUNDED:
        return builder_rounded(
            builder, destination, item->x, item->y, item->width,
            item->height, (uint16_t)item->radius, color, item->clip_left,
            item->clip_top, item->clip_right, item->clip_bottom);
    case ASTRA_DRAW_LIST_TEXT:
    case ASTRA_DRAW_LIST_MONO_TEXT:
        return text_format(format) &&
               builder_text(
                   builder, destination, item->x, item->y,
                   (const char *)header + item->payload_offset,
                   item->payload_bytes, item->font_height,
                   (uint16_t)item->width, color, item->clip_left,
                   item->clip_top, item->clip_right, item->clip_bottom,
                   item->flags);
    case ASTRA_DRAW_LIST_LINE:
        return (ASTRA_DRAW_LIST_BLEND_MODE(item->flags) ==
                    ASTRA_DRAW_LIST_BLEND_NONE ||
                (item->color >> 24) == 255u) &&
               builder_line(
                   builder, destination, item->x, item->y,
                   (int32_t)item->width, (int32_t)item->height, color,
                   item->clip_left, item->clip_top, item->clip_right,
                   item->clip_bottom);
    case ASTRA_DRAW_LIST_BLIT:
        return replay_blit(builder, destination, format, resolver, item);
    case ASTRA_DRAW_LIST_TRIANGLES:
        return replay_triangles(builder, destination, format, header,
                                resolver, item);
    case ASTRA_DRAW_LIST_FILL_RECTS:
        return replay_fill_rects(builder, destination, format, header, item);
    case ASTRA_DRAW_LIST_LINES:
        return (ASTRA_DRAW_LIST_BLEND_MODE(item->flags) ==
                    ASTRA_DRAW_LIST_BLEND_NONE ||
                (item->color >> 24) == 255u) &&
               builder_lines(
                   builder, destination, color,
                   (const AstraDrawListSegment *)(const void *)
                       ((const uint8_t *)header + item->payload_offset),
                   item->payload_bytes / sizeof(AstraDrawListSegment),
                   item->clip_left, item->clip_top, item->clip_right,
                   item->clip_bottom);
    default:
        return 0;
    }
}

static int capacity_failure(uint32_t failure)
{
    return failure == ASTRA_RENDER_BUILDER_FAILURE_DATA ||
           failure == ASTRA_RENDER_BUILDER_FAILURE_DESCRIPTOR ||
           failure == ASTRA_RENDER_BUILDER_FAILURE_COMMAND_CAPACITY;
}

int astra_render_builder_replay_range(
    AstraRenderBuilder *builder, uint32_t destination,
    const AstraDrawListHeader *shared, uint32_t area_bytes,
    const AstraRenderSourceResolver *resolver, uint32_t first,
    uint32_t *next)
{
    const uint8_t *record = descriptor_record(builder, destination);
    AstraDrawListHeader header;
    uint8_t format;

    if (next != NULL)
        *next = first;
    if (builder == NULL || record == NULL || shared == NULL ||
        next == NULL || area_bytes < ASTRA_DRAW_LIST_HEADER_BYTES)
        return ASTRA_RENDER_REPLAY_INVALID;
    /* The client can rewrite its area at any time: validate private copies,
       and lower only what was validated. */
    header = *shared;
    format = descriptor_format(record);
    if (!header_valid(&header, area_bytes) || !draw_target_format(format) ||
        first > header.command_count)
        return ASTRA_RENDER_REPLAY_INVALID;
    for (uint32_t index = first; index < header.command_count; ++index) {
        AstraDrawListCommand item =
            ((const AstraDrawListCommand *)(shared + 1))[index];
        AstraRenderBuilder saved = *builder;

        if (!command_valid(&header, &item))
            return ASTRA_RENDER_REPLAY_INVALID;
        if (!replay_command(builder, destination, format, shared, resolver,
                            &item)) {
            if (index == first || !capacity_failure(builder->failed))
                return ASTRA_RENDER_REPLAY_INVALID;
            *builder = saved;
            *next = index;
            return ASTRA_RENDER_REPLAY_FULL;
        }
    }
    *next = header.command_count;
    return ASTRA_RENDER_REPLAY_DONE;
}

int astra_render_builder_replay(AstraRenderBuilder *builder,
                                uint32_t destination,
                                const AstraDrawListHeader *header)
{
    uint32_t next;

    return header != NULL &&
           header->total_bytes == ASTRA_DRAW_LIST_AREA_BYTES &&
           header->command_capacity == ASTRA_DRAW_LIST_COMMAND_MAX &&
           astra_render_builder_replay_range(
               builder, destination, header, ASTRA_DRAW_LIST_AREA_BYTES,
               NULL, 0u, &next) == ASTRA_RENDER_REPLAY_DONE;
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

uint32_t astra_render_builder_finish_render_only(AstraRenderBuilder *builder)
{
    uint32_t bytes;

    if (builder == NULL || builder->failed != 0u ||
        builder->scene_offset != 0u || builder->command_count == 0u)
        return 0u;
    bytes = astra_render_builder_finish(builder);
    if (bytes != 0u)
        astra_store_be32(builder->bytes + 4u, ASTRA_RENDER_BATCH_VERSION_1_4);
    return bytes;
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
        uint32_t dimensions = astra_load_be32(scene + 16u);
        uint32_t required = astra_window_scene_compiled_capacity(
            (uint16_t)(dimensions >> 16), (uint16_t)dimensions,
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
