#include <astra/render_builder.h>

#include <astra/display.h>
#include <astra/render_batch.h>
#include <astra/ui_font.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
    DESCRIPTOR_ARENA_OFFSET = ASTRA_RENDER_BATCH_RESOURCE_OFFSET,
    DESCRIPTOR_MAX = 128u,
    GLYPH_ARENA_OFFSET = 0x0041a000u,
    GLYPH_MAX = 2048u,
    DATA_ARENA_OFFSET = 0x00422000u,
    SURFACE_ARENA_OFFSET = 0x00800000u,
    SURFACE_ARENA_LIMIT = 0x01000000u,
    COMMAND_DEADLINE_US = 500000u,
};

static void put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

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

static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~UINT32_C(3);
}

static int words_zero(const uint32_t *words, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
        if (words[index] != 0u)
            return 0;
    return 1;
}

static uint8_t *allocate_data(AstraRenderBuilder *builder, uint32_t bytes,
                              uint32_t *arena_offset)
{
    uint32_t cursor = align4(builder->data_cursor);

    if (bytes > builder->capacity - cursor) {
        builder->failed = 1u;
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
    uint32_t index;
    uint32_t offset;
    uint8_t *record;

    if (builder->descriptor_count >= DESCRIPTOR_MAX || width == 0u ||
        height == 0u) {
        builder->failed = 1u;
        return 0u;
    }
    index = builder->descriptor_count++;
    offset = DESCRIPTOR_ARENA_OFFSET + index *
             ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
    builder->descriptor_width[index] = width;
    builder->descriptor_height[index] = height;
    record = builder->bytes + relative(offset);
    memset(record, 0, ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
    put32(record + 0u, pair_u16(ASTRA_RENDER_ABI_VERSION,
                                ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES));
    put32(record + 4u, builder->generation);
    put32(record + 8u, data_offset);
    put32(record + 12u, data_bytes);
    put32(record + 16u, pitch);
    put32(record + 20u, pair_u16(width, height));
    put32(record + 24u, ((uint32_t)format << 24) |
                        ((uint32_t)flags << 16));
    return offset;
}

static uint8_t *command(AstraRenderBuilder *builder, uint16_t opcode,
                        uint16_t flags, uint32_t destination,
                        int32_t clip_left, int32_t clip_top,
                        int32_t clip_right, int32_t clip_bottom)
{
    uint32_t descriptor_index;
    uint32_t offset;
    uint8_t *record;

    if (destination < DESCRIPTOR_ARENA_OFFSET ||
        (destination - DESCRIPTOR_ARENA_OFFSET) %
            ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES != 0u) {
        builder->failed = 1u;
        return NULL;
    }
    descriptor_index = (destination - DESCRIPTOR_ARENA_OFFSET) /
                       ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
    if (descriptor_index >= builder->descriptor_count) {
        builder->failed = 1u;
        return NULL;
    }
    if (clip_left < 0)
        clip_left = 0;
    if (clip_top < 0)
        clip_top = 0;
    if (clip_right > builder->descriptor_width[descriptor_index])
        clip_right = builder->descriptor_width[descriptor_index];
    if (clip_bottom > builder->descriptor_height[descriptor_index])
        clip_bottom = builder->descriptor_height[descriptor_index];
    if (builder->command_count >= ASTRA_RENDER_RING_ENTRIES ||
        clip_left >= clip_right || clip_top >= clip_bottom ||
        clip_left < INT16_MIN || clip_left > INT16_MAX ||
        clip_top < INT16_MIN || clip_top > INT16_MAX ||
        clip_right < INT16_MIN || clip_right > INT16_MAX ||
        clip_bottom < INT16_MIN || clip_bottom > INT16_MAX) {
        builder->failed = 1u;
        return NULL;
    }
    offset = relative(ASTRA_RENDER_BATCH_SUBMISSION_OFFSET) +
             builder->command_count * ASTRA_RENDER_COMMAND_BYTES;
    record = builder->bytes + offset;
    memset(record, 0, ASTRA_RENDER_COMMAND_BYTES);
    put32(record + 0u,
          pair_u16(ASTRA_RENDER_ABI_VERSION, ASTRA_RENDER_COMMAND_BYTES));
    put32(record + 4u, pair_u16(opcode, flags));
    put32(record + 8u, builder->command_count + 1u);
    put32(record + 12u, builder->generation);
    put32(record + 16u, COMMAND_DEADLINE_US);
    put32(record + 24u, pair_s16(clip_left, clip_top));
    put32(record + 28u, pair_s16(clip_right, clip_bottom));
    put32(record + 32u, destination);
    ++builder->command_count;
    return record;
}

int astra_render_builder_init(AstraRenderBuilder *builder, void *storage,
                              uint32_t bytes, uint32_t generation)
{
    uint32_t scanout = (generation & 1u) != 0u ?
        ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
        ASTRA_RENDER_BATCH_SCANOUT0_OFFSET;
    uint32_t frame;

    if (builder == NULL || storage == NULL ||
        bytes < ASTRA_RENDER_BUILDER_BYTES || generation == 0u)
        return 0;
    memset(storage, 0, ASTRA_RENDER_BUILDER_BYTES);
    *builder = (AstraRenderBuilder){
        .bytes = storage,
        .capacity = ASTRA_RENDER_BUILDER_BYTES,
        .generation = generation,
        .data_cursor = relative(DATA_ARENA_OFFSET),
        .surface_cursor = SURFACE_ARENA_OFFSET,
    };
    frame = descriptor(builder, scanout,
                       ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * 2u,
                       ASTRA_DISPLAY_WIDTH * 2u, ASTRA_DISPLAY_WIDTH,
                       ASTRA_DISPLAY_HEIGHT, ASTRA_RENDER_FORMAT_RGB565,
                       ASTRA_RENDER_SURFACE_READ |
                           ASTRA_RENDER_SURFACE_WRITE);
    return frame == DESCRIPTOR_ARENA_OFFSET;
}

uint32_t astra_render_builder_frame(const AstraRenderBuilder *builder)
{
    return builder == NULL || builder->failed != 0u ? 0u :
           DESCRIPTOR_ARENA_OFFSET;
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
            builder->failed = 1u;
        return 0u;
    }
    bytes = (uint32_t)width * height * 2u;
    data = (builder->surface_cursor + 63u) & ~UINT32_C(63);
    if (data > SURFACE_ARENA_LIMIT ||
        bytes > SURFACE_ARENA_LIMIT - data) {
        builder->failed = 1u;
        return 0u;
    }
    builder->surface_cursor = data + bytes;
    return descriptor(builder, data, bytes, (uint32_t)width * 2u,
                      width, height, ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                          ASTRA_RENDER_SURFACE_WRITE);
}

uint32_t astra_render_builder_surface_at(AstraRenderBuilder *builder,
                                         uint32_t data_offset,
                                         uint16_t width, uint16_t height)
{
    uint32_t bytes;

    if (builder == NULL || width == 0u || height == 0u ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        data_offset < SURFACE_ARENA_LIMIT || (data_offset & 63u) != 0u) {
        if (builder != NULL)
            builder->failed = 1u;
        return 0u;
    }
    bytes = (uint32_t)width * height * 2u;
    if (bytes > ASTRA_RENDER_BATCH_MAX_BYTES ||
        data_offset > UINT32_MAX - bytes) {
        builder->failed = 1u;
        return 0u;
    }
    return descriptor(builder, data_offset, bytes, (uint32_t)width * 2u,
                      width, height, ASTRA_RENDER_FORMAT_RGB565,
                      ASTRA_RENDER_SURFACE_READ |
                          ASTRA_RENDER_SURFACE_WRITE);
}

int astra_render_builder_fill(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t color)
{
    uint8_t *record;

    if (builder == NULL || destination == 0u || width == 0u || height == 0u ||
        width > UINT16_MAX || height > UINT16_MAX)
        return 0;
    record = command(builder, ASTRA_RENDER_OP_FILL, 0u, destination,
                     INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX);
    if (record == NULL)
        return 0;
    put32(record + 48u, pair_s16(x, y));
    put32(record + 56u, pair_u16((uint16_t)width, (uint16_t)height));
    put32(record + 60u, color);
    return 1;
}

static int circle(AstraRenderBuilder *builder, uint32_t destination,
                  int32_t center_x, int32_t center_y, uint16_t radius,
                  int32_t left, int32_t top, int32_t right, int32_t bottom,
                  uint16_t color)
{
    uint8_t *record = command(builder, ASTRA_RENDER_OP_CIRCLE,
                              ASTRA_RENDER_GEOMETRY_FLAG_FILLED,
                              destination, left, top, right, bottom);

    if (record == NULL)
        return 0;
    put32(record + 44u, pair_s16(center_x, center_y));
    put32(record + 52u, pair_u16(radius, 0u));
    put32(record + 60u, color);
    return 1;
}

int astra_render_builder_rounded(AstraRenderBuilder *builder,
                                 uint32_t destination, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 uint16_t radius, uint16_t color)
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
        return astra_render_builder_fill(builder, destination, x, y,
                                         width, height, color);
    right = x + (int32_t)width;
    bottom = y + (int32_t)height;
    if (width > bounded * 2u)
        horizontal = astra_render_builder_fill(
            builder, destination, x + (int32_t)bounded, y,
            width - bounded * 2u, height, color);
    /* Side bands, not a full-width one: a full-width band would repaint every
       pixel the horizontal band already covered, and fill costs real time. */
    if (height > bounded * 2u)
        vertical = astra_render_builder_fill(
                       builder, destination, x, y + (int32_t)bounded,
                       bounded, height - bounded * 2u, color) &&
                   astra_render_builder_fill(
                       builder, destination, right - (int32_t)bounded,
                       y + (int32_t)bounded, bounded,
                       height - bounded * 2u, color);
    return horizontal && vertical &&
           circle(builder, destination, x + bounded, y + bounded,
                  bounded, x, y, right, bottom, color) &&
           circle(builder, destination, right - bounded - 1,
                  y + bounded, bounded, x, y, right, bottom, color) &&
           circle(builder, destination, x + bounded,
                  bottom - bounded - 1, bounded,
                  x, y, right, bottom, color) &&
           circle(builder, destination, right - bounded - 1,
                  bottom - bounded - 1, bounded,
                  x, y, right, bottom, color);
}

static int builder_text(AstraRenderBuilder *builder, uint32_t destination,
                        int32_t x, int32_t y, const char *utf8,
                        uint32_t length, uint16_t pixel_height,
                        uint16_t cell_width, uint16_t color)
{
    const AstraUiStrike *strike = cell_width != 0u ?
        astra_mono_font_strike(pixel_height) :
        astra_ui_font_strike(pixel_height);
    uint32_t count = 0u;
    uint32_t max_width = 0u;
    uint32_t at = 0u;
    uint32_t cell_pitch;
    uint32_t cell_bytes;
    uint32_t source_bytes;
    uint32_t source_data;
    uint32_t source_descriptor;
    uint32_t glyph_offset;
    uint8_t *source;
    uint8_t *record;
    int32_t pen = x;

    if (builder == NULL || destination == 0u || utf8 == NULL ||
        strike == NULL)
        return 0;
    while (at < length) {
        uint32_t consumed;
        uint32_t scalar = astra_ui_font_scalar(
            utf8 + at, length - at, &consumed);
        const AstraUiGlyph *glyph = cell_width != 0u ?
            astra_mono_font_glyph(strike, scalar) :
            astra_ui_font_glyph(strike, scalar);

        if (glyph->width > max_width)
            max_width = glyph->width;
        ++count;
        at += consumed;
    }
    if (count == 0u)
        return 1;
    if (builder->glyph_count + count > GLYPH_MAX)
        return builder->failed = 1u, 0;
    cell_pitch = (max_width + 7u) / 8u;
    cell_bytes = cell_pitch * strike->height;
    source_bytes = cell_bytes * count;
    source = allocate_data(builder, source_bytes, &source_data);
    if (source == NULL)
        return 0;
    source_descriptor = descriptor(
        builder, source_data, source_bytes, cell_pitch,
        (uint16_t)max_width, strike->height, ASTRA_RENDER_FORMAT_MASK1,
        ASTRA_RENDER_SURFACE_READ);
    if (source_descriptor == 0u)
        return 0;
    glyph_offset = GLYPH_ARENA_OFFSET + builder->glyph_count *
                   ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES;
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
        uint8_t *glyph_record = builder->bytes +
            relative(GLYPH_ARENA_OFFSET + builder->glyph_count *
                     ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES);

        for (uint32_t row = 0u; row < glyph->height; ++row)
            memcpy(source + index * cell_bytes + row * cell_pitch,
                   bitmap + row * glyph->pitch, glyph->pitch);
        put32(glyph_record + 0u, index * cell_bytes);
        put32(glyph_record + 4u, 0u);
        put32(glyph_record + 8u,
              pair_s16(pen + glyph->bearing_x / 64,
                       y + strike->ascent - glyph->bearing_y / 64));
        put32(glyph_record + 12u,
              pair_u16(glyph->width, glyph->height));
        ++builder->glyph_count;
        pen += cell_width != 0u ? cell_width : glyph->advance_x / 64;
        at += consumed;
    }
    record = command(builder, ASTRA_RENDER_OP_GLYPH_RUN, 0u, destination,
                     INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX);
    if (record == NULL)
        return 0;
    put32(record + 36u, source_descriptor);
    put32(record + 40u, glyph_offset);
    put32(record + 44u, count);
    put32(record + 48u, color);
    return 1;
}

int astra_render_builder_text(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              const char *utf8, uint32_t length,
                              uint16_t pixel_height, uint16_t color)
{
    return builder_text(builder, destination, x, y, utf8, length,
                        pixel_height, 0u, color);
}

int astra_render_builder_mono_text(AstraRenderBuilder *builder,
                                   uint32_t destination, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color)
{
    return cell_width != 0u &&
           builder_text(builder, destination, x, y, utf8, length,
                        pixel_height, cell_width, color);
}

static int command_valid(const AstraDrawListHeader *header,
                         const AstraDrawListCommand *item)
{
    uint64_t end = (uint64_t)item->payload_offset + item->payload_bytes;

    if (item->flags != 0u || item->reserved16 != 0u ||
        !words_zero(item->reserved, 4u))
        return 0;
    if (item->operation == ASTRA_DRAW_LIST_FILL ||
        item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED)
        return item->width != 0u && item->height != 0u &&
               item->payload_offset == 0u && item->payload_bytes == 0u &&
               item->font_height == 0u;
    if (item->operation == ASTRA_DRAW_LIST_TEXT ||
        item->operation == ASTRA_DRAW_LIST_MONO_TEXT)
        return (item->operation == ASTRA_DRAW_LIST_TEXT ?
                    item->width == 0u : item->width != 0u) &&
               item->height == 0u &&
               item->radius == 0u && item->payload_bytes != 0u &&
               item->payload_offset >= ASTRA_DRAW_LIST_PAYLOAD_OFFSET &&
               end <= (uint64_t)ASTRA_DRAW_LIST_PAYLOAD_OFFSET +
                         header->payload_bytes;
    return 0;
}

int astra_draw_list_covers(const AstraDrawListHeader *header,
                           uint16_t width, uint16_t height)
{
    const AstraDrawListCommand *first;

    if (header == NULL || header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_0 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->command_count == 0u ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES ||
        !words_zero(header->reserved, 10u))
        return 0;
    first = (const AstraDrawListCommand *)(header + 1);
    if (!command_valid(header, first) ||
        first->operation != ASTRA_DRAW_LIST_FILL)
        return 0;
    return first->x <= 0 && first->y <= 0 &&
           (int64_t)first->x + first->width >= (int64_t)width &&
           (int64_t)first->y + first->height >= (int64_t)height;
}

int astra_render_builder_replay(AstraRenderBuilder *builder,
                                uint32_t destination,
                                const AstraDrawListHeader *header)
{
    const AstraDrawListCommand *commands;

    if (builder == NULL || destination == 0u || header == NULL ||
        header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_0 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES ||
        !words_zero(header->reserved, 10u))
        return 0;
    commands = (const AstraDrawListCommand *)(header + 1);
    for (uint32_t index = 0u; index < header->command_count; ++index) {
        const AstraDrawListCommand *item = &commands[index];
        int ok;

        if (!command_valid(header, item))
            return 0;
        if (item->operation == ASTRA_DRAW_LIST_FILL)
            ok = astra_render_builder_fill(
                builder, destination, item->x, item->y,
                item->width, item->height, (uint16_t)item->foreground);
        else if (item->operation == ASTRA_DRAW_LIST_FILL_ROUNDED)
            ok = astra_render_builder_rounded(
                builder, destination, item->x, item->y,
                item->width, item->height, (uint16_t)item->radius,
                (uint16_t)item->foreground);
        else if (item->operation == ASTRA_DRAW_LIST_TEXT)
            ok = astra_render_builder_text(
                builder, destination, item->x, item->y,
                (const char *)header + item->payload_offset,
                item->payload_bytes, item->font_height,
                (uint16_t)item->foreground);
        else
            ok = astra_render_builder_mono_text(
                builder, destination, item->x, item->y,
                (const char *)header + item->payload_offset,
                item->payload_bytes, item->font_height,
                (uint16_t)item->width, (uint16_t)item->foreground);
        if (!ok)
            return 0;
    }
    return 1;
}

static uint32_t rounded_inset(uint32_t row, uint32_t height,
                              uint32_t radius)
{
    uint32_t corner_y;
    uint32_t diameter;
    uint32_t inset = 0u;

    if (radius == 0u || (row >= radius && row < height - radius))
        return 0u;
    corner_y = row < radius ? row : height - row - 1u;
    diameter = radius * 2u;
    while (inset < radius) {
        uint32_t dx = diameter - inset * 2u - 1u;
        uint32_t dy = diameter - corner_y * 2u - 1u;

        if (dx * dx + dy * dy <= diameter * diameter)
            break;
        ++inset;
    }
    return inset;
}

static void mask_span(uint8_t *row, uint32_t left, uint32_t right)
{
    while (left < right && (left & 7u) != 0u) {
        row[left >> 3] |= (uint8_t)(0x80u >> (left & 7u));
        ++left;
    }
    while (left + 8u <= right) {
        row[left >> 3] = 0xffu;
        left += 8u;
    }
    while (left < right) {
        row[left >> 3] |= (uint8_t)(0x80u >> (left & 7u));
        ++left;
    }
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
    record = command(builder, ASTRA_RENDER_OP_BLIT, flags, destination,
                     clip_left, clip_top, clip_right, clip_bottom);
    if (record == NULL)
        return 0;
    put32(record + 36u, source);
    put32(record + 40u, mask);
    put32(record + 44u, pair_s16(source_x, source_y));
    put32(record + 48u, pair_s16(destination_x, destination_y));
    put32(record + 52u, pair_u16(width, height));
    put32(record + 56u, pair_u16(width, height));
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
                       width, height, 0, 0,
                       ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT);
}

int astra_render_builder_blit_clipped(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t radius,
    int round_top, int32_t clip_left, int32_t clip_top,
    int32_t clip_right, int32_t clip_bottom)
{
    uint32_t mask_descriptor = 0u;
    uint32_t bounded = radius;

    if (builder == NULL || destination == 0u || source == 0u || width == 0u ||
        height == 0u)
        return 0;
    if (bounded > width / 2u)
        bounded = width / 2u;
    if (bounded > height / 2u)
        bounded = height / 2u;
    if (bounded != 0u) {
        uint32_t pitch = ((uint32_t)width + 7u) / 8u;
        uint32_t bytes = pitch * height;
        uint32_t data_offset;
        uint8_t *mask = allocate_data(builder, bytes, &data_offset);

        if (mask == NULL)
            return 0;
        for (uint32_t row = 0u; row < height; ++row) {
            uint32_t inset = (!round_top &&
                              row < (uint32_t)height - bounded) ? 0u :
                             rounded_inset(row, height, bounded);

            mask_span(mask + row * pitch, inset, width - inset);
        }
        mask_descriptor = descriptor(
            builder, data_offset, bytes, pitch, width, height,
            ASTRA_RENDER_FORMAT_MASK1, ASTRA_RENDER_SURFACE_READ);
    }
    if (bounded == 0u)
        return blit_region(builder, destination, source, 0u, 0u,
                           0, 0, x, y, width, height,
                           clip_left, clip_top, clip_right, clip_bottom);

    return blit_region(
               builder, destination, source, 0u, 0u,
               bounded, 0, x + (int32_t)bounded, y,
               (uint16_t)(width - bounded * 2u), height,
               clip_left, clip_top, clip_right, clip_bottom) &&
           blit_region(
               builder, destination, source, 0u, 0u,
               0, round_top ? (int32_t)bounded : 0, x,
               y + (round_top ? (int32_t)bounded : 0),
               (uint16_t)bounded,
               (uint16_t)(height - bounded -
                          (round_top ? bounded : 0u)),
               clip_left, clip_top, clip_right, clip_bottom) &&
           blit_region(
               builder, destination, source, 0u, 0u,
               (int32_t)width - (int32_t)bounded,
               round_top ? (int32_t)bounded : 0,
               x + (int32_t)width - (int32_t)bounded,
               y + (round_top ? (int32_t)bounded : 0),
               (uint16_t)bounded,
               (uint16_t)(height - bounded -
                          (round_top ? bounded : 0u)),
               clip_left, clip_top, clip_right, clip_bottom) &&
           (!round_top ||
            (blit_region(
                 builder, destination, source, mask_descriptor,
                 ASTRA_RENDER_FLAG_BLIT_MASK1,
                 0, 0, x, y, (uint16_t)bounded, (uint16_t)bounded,
                 clip_left, clip_top, clip_right, clip_bottom) &&
             blit_region(
                 builder, destination, source, mask_descriptor,
                 ASTRA_RENDER_FLAG_BLIT_MASK1,
                 (int32_t)width - (int32_t)bounded, 0,
                 x + (int32_t)width - (int32_t)bounded, y,
                 (uint16_t)bounded, (uint16_t)bounded,
                 clip_left, clip_top, clip_right, clip_bottom))) &&
           blit_region(
               builder, destination, source, mask_descriptor,
               ASTRA_RENDER_FLAG_BLIT_MASK1,
               0, (int32_t)height - (int32_t)bounded,
               x, y + (int32_t)height - (int32_t)bounded,
               (uint16_t)bounded, (uint16_t)bounded,
               clip_left, clip_top, clip_right, clip_bottom) &&
           blit_region(
               builder, destination, source, mask_descriptor,
               ASTRA_RENDER_FLAG_BLIT_MASK1,
               (int32_t)width - (int32_t)bounded,
               (int32_t)height - (int32_t)bounded,
               x + (int32_t)width - (int32_t)bounded,
               y + (int32_t)height - (int32_t)bounded,
               (uint16_t)bounded, (uint16_t)bounded,
               clip_left, clip_top, clip_right, clip_bottom);
}

int astra_render_builder_blit(AstraRenderBuilder *builder,
                              uint32_t destination, uint32_t source,
                              int32_t x, int32_t y, uint16_t width,
                              uint16_t height, uint16_t radius,
                              int round_top)
{
    return astra_render_builder_blit_clipped(
        builder, destination, source, x, y, width, height, radius, round_top,
        0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT);
}

uint32_t astra_render_builder_finish(AstraRenderBuilder *builder)
{
    if (builder == NULL || builder->failed != 0u ||
        builder->command_count == 0u)
        return 0u;
    put32(builder->bytes + 0u, ASTRA_RENDER_BATCH_MAGIC);
    put32(builder->bytes + 4u, ASTRA_RENDER_BATCH_VERSION_1_0);
    put32(builder->bytes + 8u, ASTRA_RENDER_BUILDER_BYTES);
    put32(builder->bytes + 12u, builder->command_count);
    put32(builder->bytes + 16u, ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    put32(builder->bytes + 20u, ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    put32(builder->bytes + 24u, builder->generation);
    put32(builder->bytes + 28u,
          (builder->generation & 1u) != 0u ?
              ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
              ASTRA_RENDER_BATCH_SCANOUT0_OFFSET);
    return ASTRA_RENDER_BUILDER_BYTES;
}
