#include <astra/surface.h>

#include <astra/draw_list.h>
#include <astra/ui_font.h>

#include <stddef.h>
#include <string.h>

#include "astra_font8x8.inc"

#include "astra_ui_font.inc"
#include "astra_mono_font.inc"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef struct AstraFontBank {
    const uint32_t *codepoints;
    const uint16_t *glyph_ids;
    uint32_t cmap_count;
    const AstraUiStrike *strikes;
    uint32_t strike_count;
    const AstraUiGlyph *glyphs;
    const uint8_t *bitmap;
} AstraFontBank;

static const AstraFontBank ui_font = {
    astra_ui_cmap_codepoints, astra_ui_cmap_glyphs,
    ARRAY_COUNT(astra_ui_cmap_codepoints), astra_ui_strikes,
    ARRAY_COUNT(astra_ui_strikes), astra_ui_glyphs, astra_ui_bitmap
};

static const AstraFontBank mono_font = {
    astra_mono_cmap_codepoints, astra_mono_cmap_glyphs,
    ARRAY_COUNT(astra_mono_cmap_codepoints), astra_mono_strikes,
    ARRAY_COUNT(astra_mono_strikes), astra_mono_glyphs, astra_mono_bitmap
};

static uint16_t *row(AstraSurfaceView *surface, uint32_t y)
{
    return (uint16_t *)((uint8_t *)surface->pixels + y * surface->pitch);
}

static AstraDrawListHeader *draw_header(AstraSurfaceView *surface)
{
    return (AstraDrawListHeader *)(void *)surface->pixels;
}

static AstraDrawListCommand *draw_commands(AstraSurfaceView *surface)
{
    return (AstraDrawListCommand *)((uint8_t *)(void *)surface->pixels +
                                    sizeof(AstraDrawListHeader));
}

static AstraDrawListCommand *append_command(AstraSurfaceView *surface,
                                            uint32_t operation)
{
    AstraDrawListHeader *header;
    AstraDrawListCommand *command;

    if (surface == NULL || surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST)
        return NULL;
    header = draw_header(surface);
    if (header->command_count >= ASTRA_DRAW_LIST_COMMAND_MAX)
        return NULL;
    command = &draw_commands(surface)[header->command_count++];
    *command = (AstraDrawListCommand){0};
    command->operation = operation;
    return command;
}

static const uint16_t *const_row(const AstraSurfaceView *surface, uint32_t y)
{
    return (const uint16_t *)((const uint8_t *)surface->pixels +
                             y * surface->pitch);
}

int astra_surface_view_init(AstraSurfaceView *surface, void *pixels,
                            uint32_t byte_size, uint16_t width,
                            uint16_t height, uint32_t pitch)
{
    uint32_t minimum_pitch;

    if (surface == NULL || pixels == NULL || width == 0u || height == 0u)
        return 0;
    minimum_pitch = (uint32_t)width * sizeof(uint16_t);
    if (pitch < minimum_pitch || (pitch & 1u) != 0u ||
        (uint64_t)pitch * height > byte_size)
        return 0;
    surface->pixels = pixels;
    surface->byte_size = byte_size;
    surface->pitch = pitch;
    surface->width = width;
    surface->height = height;
    surface->kind = ASTRA_SURFACE_VIEW_RGB565;
    surface->reserved = 0u;
    return 1;
}

int astra_draw_list_view_init(AstraSurfaceView *surface, void *storage,
                              uint32_t byte_size, uint16_t width,
                              uint16_t height)
{
    AstraDrawListHeader *header;

    if (surface == NULL || storage == NULL ||
        byte_size < ASTRA_DRAW_LIST_AREA_BYTES || width == 0u || height == 0u)
        return 0;
    surface->pixels = storage;
    surface->byte_size = byte_size;
    surface->pitch = 0u;
    surface->width = width;
    surface->height = height;
    surface->kind = ASTRA_SURFACE_VIEW_DRAW_LIST;
    surface->reserved = 0u;
    header = draw_header(surface);
    *header = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_0,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = width,
        .height = height,
    };
    return 1;
}

int astra_draw_list_view_adopt(AstraSurfaceView *surface, void *storage,
                               uint32_t byte_size, uint16_t width,
                               uint16_t height)
{
    const AstraDrawListHeader *header = storage;

    if (surface == NULL || storage == NULL ||
        byte_size < ASTRA_DRAW_LIST_AREA_BYTES ||
        header->magic != ASTRA_DRAW_LIST_MAGIC ||
        header->version != ASTRA_DRAW_LIST_VERSION_1_0 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->width != width || header->height != height ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES)
        return 0;
    surface->pixels = storage;
    surface->byte_size = byte_size;
    surface->pitch = 0u;
    surface->width = width;
    surface->height = height;
    surface->kind = ASTRA_SURFACE_VIEW_DRAW_LIST;
    surface->reserved = 0u;
    return 1;
}

uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8u) |
                      ((uint16_t)(green & 0xfcu) << 3u) |
                      ((uint16_t)blue >> 3u));
}

void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color)
{
    AstraDrawListCommand *command;
    int64_t right;
    int64_t bottom;
    uint32_t first_x;
    uint32_t first_y;
    uint32_t last_x;
    uint32_t last_y;

    if (surface != NULL && surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        if (width == 0u || height == 0u)
            return;
        command = append_command(surface, ASTRA_DRAW_LIST_FILL);
        if (command != NULL) {
            command->x = x;
            command->y = y;
            command->width = width;
            command->height = height;
            command->foreground = color;
        }
        return;
    }
    if (surface == NULL || surface->pixels == NULL || width == 0u ||
        height == 0u)
        return;
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    if (right <= 0 || bottom <= 0 || x >= surface->width ||
        y >= surface->height)
        return;
    first_x = x < 0 ? 0u : (uint32_t)x;
    first_y = y < 0 ? 0u : (uint32_t)y;
    last_x = right > surface->width ? surface->width : (uint32_t)right;
    last_y = bottom > surface->height ? surface->height : (uint32_t)bottom;
    for (uint32_t at_y = first_y; at_y < last_y; ++at_y) {
        uint16_t *pixels = row(surface, at_y);

        for (uint32_t at_x = first_x; at_x < last_x; ++at_x)
            pixels[at_x] = color;
    }
}

void astra_surface_clear(AstraSurfaceView *surface, uint16_t color)
{
    if (surface != NULL)
        astra_surface_fill(surface, 0, 0, surface->width, surface->height,
                           color);
}

int astra_draw_list_copy(AstraSurfaceView *surface, uint32_t source_x,
                         uint32_t source_y, uint32_t destination_x,
                         uint32_t destination_y, uint32_t width,
                         uint32_t height)
{
    AstraDrawListCommand *command;

    if (surface == NULL || surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        width == 0u || height == 0u ||
        source_x > surface->width || width > surface->width - source_x ||
        source_y > surface->height || height > surface->height - source_y ||
        destination_x > surface->width ||
        width > surface->width - destination_x ||
        destination_y > surface->height ||
        height > surface->height - destination_y)
        return 0;
    command = append_command(surface, ASTRA_DRAW_LIST_COPY);
    if (command == NULL)
        return 0;
    command->x = (int32_t)destination_x;
    command->y = (int32_t)destination_y;
    command->width = width;
    command->height = height;
    command->foreground = source_x;
    command->background = source_y;
    return 1;
}

int astra_text_box_scroll(AstraTextBox *text_box, int32_t pixels)
{
    AstraSurfaceView *surface;
    uint32_t distance;

    if (text_box == NULL || (surface = text_box->surface) == NULL ||
        text_box->width == 0u || text_box->height == 0u ||
        text_box->x > surface->width ||
        text_box->width > surface->width - text_box->x ||
        text_box->y > surface->height ||
        text_box->height > surface->height - text_box->y)
        return 0;
    if (pixels == 0)
        return 1;
    distance = pixels > 0 ? (uint32_t)pixels :
        (uint32_t)(-(int64_t)pixels);
    if (distance >= text_box->height)
        return 1;
    if (pixels > 0)
        return astra_draw_list_copy(
            surface, text_box->x, text_box->y + distance,
            text_box->x, text_box->y, text_box->width,
            text_box->height - distance);
    return astra_draw_list_copy(
        surface, text_box->x, text_box->y,
        text_box->x, text_box->y + distance, text_box->width,
        text_box->height - distance);
}

static uint32_t rounded_inset(uint32_t at_y, uint32_t height,
                              uint32_t radius)
{
    uint32_t corner_y;
    uint32_t diameter;
    uint32_t inset = 0u;

    if (radius == 0u || (at_y >= radius && at_y < height - radius))
        return 0u;
    corner_y = at_y < radius ? at_y : height - at_y - 1u;
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

void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color)
{
    AstraDrawListCommand *command;
    uint32_t bounded_radius = radius;

    if (width == 0u || height == 0u)
        return;
    if (bounded_radius > width / 2u)
        bounded_radius = width / 2u;
    if (bounded_radius > height / 2u)
        bounded_radius = height / 2u;
    if (surface != NULL && surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        command = append_command(surface, ASTRA_DRAW_LIST_FILL_ROUNDED);
        if (command != NULL) {
            command->x = x;
            command->y = y;
            command->width = width;
            command->height = height;
            command->radius = bounded_radius;
            command->foreground = color;
        }
        return;
    }
    for (uint32_t at_y = 0u; at_y < height; ++at_y) {
        uint32_t inset = rounded_inset(at_y, height, bounded_radius);

        astra_surface_fill(surface, x + (int32_t)inset, y + (int32_t)at_y,
                           width - inset * 2u, 1u, color);
    }
}

void astra_surface_blit(AstraSurfaceView *destination, int32_t x, int32_t y,
                        const AstraSurfaceView *source)
{
    uint32_t source_x = x < 0 ? (uint32_t)(-(int64_t)x) : 0u;
    uint32_t source_y = y < 0 ? (uint32_t)(-(int64_t)y) : 0u;
    uint32_t destination_x = x < 0 ? 0u : (uint32_t)x;
    uint32_t destination_y = y < 0 ? 0u : (uint32_t)y;
    uint32_t width;
    uint32_t height;

    if (destination == NULL || source == NULL || destination->pixels == NULL ||
        source->pixels == NULL || destination_x >= destination->width ||
        destination_y >= destination->height || source_x >= source->width ||
        source_y >= source->height)
        return;
    width = source->width - source_x;
    height = source->height - source_y;
    if (width > destination->width - destination_x)
        width = destination->width - destination_x;
    if (height > destination->height - destination_y)
        height = destination->height - destination_y;
    for (uint32_t at_y = 0u; at_y < height; ++at_y) {
        uint16_t *out = row(destination, destination_y + at_y) + destination_x;
        const uint16_t *in = const_row(source, source_y + at_y) + source_x;

        for (uint32_t at_x = 0u; at_x < width; ++at_x)
            out[at_x] = in[at_x];
    }
}

void astra_surface_blit_round(AstraSurfaceView *destination, int32_t x,
                              int32_t y, const AstraSurfaceView *source,
                              uint16_t radius)
{
    uint32_t bounded_radius = radius;

    if (destination == NULL || source == NULL || destination->pixels == NULL ||
        source->pixels == NULL)
        return;
    if (bounded_radius > source->width / 2u)
        bounded_radius = source->width / 2u;
    if (bounded_radius > source->height / 2u)
        bounded_radius = source->height / 2u;
    for (uint32_t source_y = 0u; source_y < source->height; ++source_y) {
        int64_t destination_y = (int64_t)y + source_y;
        uint32_t inset = rounded_inset(source_y, source->height,
                                       bounded_radius);
        const uint16_t *in = const_row(source, source_y);
        uint16_t *out;

        if (destination_y < 0 || destination_y >= destination->height)
            continue;
        out = row(destination, (uint32_t)destination_y);
        for (uint32_t source_x = inset;
             source_x < (uint32_t)source->width - inset; ++source_x) {
            int64_t destination_x = (int64_t)x + source_x;

            if (destination_x >= 0 && destination_x < destination->width)
                out[destination_x] = in[source_x];
        }
    }
}

void astra_surface_blit_round_bottom(AstraSurfaceView *destination, int32_t x,
                                     int32_t y,
                                     const AstraSurfaceView *source,
                                     uint16_t radius)
{
    uint32_t bounded_radius = radius;

    if (destination == NULL || source == NULL || destination->pixels == NULL ||
        source->pixels == NULL)
        return;
    if (bounded_radius > source->width / 2u)
        bounded_radius = source->width / 2u;
    if (bounded_radius > source->height / 2u)
        bounded_radius = source->height / 2u;
    for (uint32_t source_y = 0u; source_y < source->height; ++source_y) {
        int64_t destination_y = (int64_t)y + source_y;
        uint32_t inset = source_y < source->height - bounded_radius ? 0u :
                         rounded_inset(source_y, source->height,
                                       bounded_radius);
        const uint16_t *in = const_row(source, source_y);
        uint16_t *out;

        if (destination_y < 0 || destination_y >= destination->height)
            continue;
        out = row(destination, (uint32_t)destination_y);
        for (uint32_t source_x = inset;
             source_x < (uint32_t)source->width - inset; ++source_x) {
            int64_t destination_x = (int64_t)x + source_x;

            if (destination_x >= 0 && destination_x < destination->width)
                out[destination_x] = in[source_x];
        }
    }
}

void astra_surface_glyph8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                            const uint8_t rows[8], uint16_t color)
{
    if (surface == NULL || rows == NULL)
        return;
    for (uint32_t glyph_y = 0u; glyph_y < 8u; ++glyph_y) {
        uint8_t bits = rows[glyph_y];

        for (uint32_t glyph_x = 0u; glyph_x < 8u; ++glyph_x) {
            if ((bits & (uint8_t)(0x80u >> glyph_x)) != 0u)
                astra_surface_fill(surface, x + (int32_t)glyph_x,
                                   y + (int32_t)glyph_y, 1u, 1u, color);
        }
    }
}

void astra_surface_text8x8(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *text, uint32_t length, uint8_t scale,
                           uint16_t color)
{
    if (surface == NULL || text == NULL || scale == 0u)
        return;
    for (uint32_t character = 0u; character < length; ++character) {
        const uint8_t *glyph = &astra_font8x8[
            (uint32_t)(uint8_t)text[character] * 8u];

        for (uint32_t glyph_y = 0u; glyph_y < 8u; ++glyph_y)
            for (uint32_t glyph_x = 0u; glyph_x < 8u; ++glyph_x)
                if ((glyph[glyph_y] & (uint8_t)(0x80u >> glyph_x)) != 0u)
                    astra_surface_fill(
                        surface,
                        x + (int32_t)((character * 8u + glyph_x) * scale),
                        y + (int32_t)(glyph_y * scale), scale, scale, color);
    }
}

static const AstraUiStrike *font_strike(const AstraFontBank *font,
                                        uint16_t pixel_height)
{
    for (uint32_t index = 0u; index < font->strike_count; ++index)
        if (font->strikes[index].height == pixel_height)
            return &font->strikes[index];
    return NULL;
}

const AstraUiStrike *astra_ui_font_strike(uint16_t pixel_height)
{
    return font_strike(&ui_font, pixel_height);
}

const AstraUiStrike *astra_mono_font_strike(uint16_t pixel_height)
{
    return font_strike(&mono_font, pixel_height);
}

uint32_t astra_ui_font_scalar(const char *text, uint32_t length,
                              uint32_t *consumed)
{
    const uint8_t *bytes = (const uint8_t *)text;
    uint8_t first = bytes[0];

    *consumed = 1u;
    if (first < 0x80u)
        return first;
    if (first >= 0xc2u && first <= 0xdfu && length >= 2u &&
        (bytes[1] & 0xc0u) == 0x80u) {
        *consumed = 2u;
        return ((uint32_t)(first & 0x1fu) << 6u) |
               (bytes[1] & 0x3fu);
    }
    if (first >= 0xe0u && first <= 0xefu && length >= 3u &&
        (bytes[1] & 0xc0u) == 0x80u && (bytes[2] & 0xc0u) == 0x80u &&
        !(first == 0xe0u && bytes[1] < 0xa0u) &&
        !(first == 0xedu && bytes[1] >= 0xa0u)) {
        *consumed = 3u;
        return ((uint32_t)(first & 0x0fu) << 12u) |
               ((uint32_t)(bytes[1] & 0x3fu) << 6u) |
               (bytes[2] & 0x3fu);
    }
    if (first >= 0xf0u && first <= 0xf4u && length >= 4u &&
        (bytes[1] & 0xc0u) == 0x80u && (bytes[2] & 0xc0u) == 0x80u &&
        (bytes[3] & 0xc0u) == 0x80u &&
        !(first == 0xf0u && bytes[1] < 0x90u) &&
        !(first == 0xf4u && bytes[1] >= 0x90u)) {
        *consumed = 4u;
        return ((uint32_t)(first & 0x07u) << 18u) |
               ((uint32_t)(bytes[1] & 0x3fu) << 12u) |
               ((uint32_t)(bytes[2] & 0x3fu) << 6u) |
               (bytes[3] & 0x3fu);
    }
    return 0xfffdu;
}

static uint32_t font_glyph_id(const AstraFontBank *font, uint32_t scalar)
{
    uint32_t low = 0u;
    uint32_t high = font->cmap_count;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t candidate = font->codepoints[middle];

        if (candidate < scalar)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < font->cmap_count && font->codepoints[low] == scalar)
        return font->glyph_ids[low];
    return font->glyph_ids[font->cmap_count - 1u];
}

static const AstraUiGlyph *font_glyph(const AstraFontBank *font,
                                      const AstraUiStrike *strike,
                                      uint32_t scalar)
{
    uint32_t glyph_id = font_glyph_id(font, scalar);

    if (glyph_id >= strike->glyph_count)
        glyph_id = strike->glyph_count - 1u;
    return &font->glyphs[strike->glyph_first + glyph_id];
}

const AstraUiGlyph *astra_ui_font_glyph(const AstraUiStrike *strike,
                                        uint32_t scalar)
{
    return font_glyph(&ui_font, strike, scalar);
}

const AstraUiGlyph *astra_mono_font_glyph(const AstraUiStrike *strike,
                                          uint32_t scalar)
{
    return font_glyph(&mono_font, strike, scalar);
}

const uint8_t *astra_ui_font_bitmap(const AstraUiGlyph *glyph)
{
    return glyph == NULL ? NULL : &astra_ui_bitmap[glyph->bitmap_offset];
}

const uint8_t *astra_mono_font_bitmap(const AstraUiGlyph *glyph)
{
    return glyph == NULL ? NULL : &astra_mono_bitmap[glyph->bitmap_offset];
}

uint32_t astra_surface_ui_text_width(const char *utf8, uint32_t length,
                                     uint16_t pixel_height)
{
    const AstraUiStrike *strike = astra_ui_font_strike(pixel_height);
    uint32_t width = 0u;
    uint32_t at = 0u;

    if (utf8 == NULL || strike == NULL)
        return 0u;
    while (at < length) {
        uint32_t consumed;
        const AstraUiGlyph *glyph = astra_ui_font_glyph(
            strike, astra_ui_font_scalar(utf8 + at, length - at, &consumed));

        width += (uint32_t)glyph->advance_x >> 6u;
        at += consumed;
    }
    return width;
}

uint32_t astra_surface_ui_text_fit(const char *utf8, uint32_t length,
                                   uint16_t pixel_height,
                                   uint32_t maximum_width)
{
    const AstraUiStrike *strike = astra_ui_font_strike(pixel_height);
    uint32_t width = 0u;
    uint32_t at = 0u;

    if (utf8 == NULL || strike == NULL)
        return 0u;
    while (at < length) {
        uint32_t consumed;
        const AstraUiGlyph *glyph = astra_ui_font_glyph(
            strike, astra_ui_font_scalar(utf8 + at, length - at, &consumed));
        uint32_t advance = (uint32_t)glyph->advance_x >> 6u;

        if (advance > maximum_width - width)
            break;
        width += advance;
        at += consumed;
    }
    return at;
}

static void draw_list_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *utf8, uint32_t length,
                           uint16_t pixel_height, uint16_t cell_width,
                           uint16_t color, uint16_t operation)
{
    AstraDrawListHeader *header;
    AstraDrawListCommand *command;

    if (surface == NULL || utf8 == NULL ||
        surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST)
        return;
    header = draw_header(surface);
    if (length > ASTRA_DRAW_LIST_PAYLOAD_BYTES - header->payload_bytes)
        return;
    command = append_command(surface, operation);
    if (command == NULL)
        return;
    command->x = x;
    command->y = y;
    command->foreground = color;
    command->font_height = pixel_height;
    command->width = cell_width;
    command->payload_offset = ASTRA_DRAW_LIST_PAYLOAD_OFFSET +
                              header->payload_bytes;
    command->payload_bytes = length;
    memcpy((uint8_t *)(void *)surface->pixels + command->payload_offset,
           utf8, length);
    header->payload_bytes += length;
}

static void font_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                      const char *utf8, uint32_t length,
                      uint16_t pixel_height, uint16_t cell_width,
                      uint16_t color, const AstraFontBank *font)
{
    const AstraUiStrike *strike;
    int32_t pen = x;
    uint32_t at = 0u;

    if (surface == NULL || utf8 == NULL)
        return;
    if (surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        draw_list_text(surface, x, y, utf8, length, pixel_height, cell_width,
                       color, font == &mono_font ? ASTRA_DRAW_LIST_MONO_TEXT :
                                                  ASTRA_DRAW_LIST_TEXT);
        return;
    }
    strike = font_strike(font, pixel_height);
    if (strike == NULL)
        return;
    while (at < length) {
        uint32_t consumed;
        const AstraUiGlyph *glyph = font_glyph(
            font, strike,
            astra_ui_font_scalar(utf8 + at, length - at, &consumed));
        int32_t glyph_x = pen + glyph->bearing_x / 64;
        int32_t glyph_y = y + strike->ascent - glyph->bearing_y / 64;
        const uint8_t *bitmap = &font->bitmap[glyph->bitmap_offset];

        for (uint32_t row_index = 0u; row_index < glyph->height; ++row_index)
            for (uint32_t column = 0u; column < glyph->width; ++column)
                if ((bitmap[row_index * glyph->pitch + column / 8u] &
                     (uint8_t)(0x80u >> (column & 7u))) != 0u)
                    astra_surface_fill(surface,
                                       glyph_x + (int32_t)column,
                                       glyph_y + (int32_t)row_index,
                                       1u, 1u, color);
        pen += cell_width != 0u ? cell_width : glyph->advance_x / 64;
        at += consumed;
    }
}

void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *utf8, uint32_t length,
                           uint16_t pixel_height, uint16_t color)
{
    font_text(surface, x, y, utf8, length, pixel_height, 0u, color,
              &ui_font);
}

uint16_t astra_surface_mono_cell_width(uint16_t pixel_height)
{
    const AstraUiStrike *strike = astra_mono_font_strike(pixel_height);
    const AstraUiGlyph *space;

    if (strike == NULL)
        return 0u;
    space = astra_mono_font_glyph(strike, ' ');
    return (uint16_t)(space->advance_x / 64);
}

void astra_surface_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                             const char *utf8, uint32_t length,
                             uint16_t pixel_height, uint16_t cell_width,
                             uint16_t color)
{
    if (cell_width != 0u)
        font_text(surface, x, y, utf8, length, pixel_height, cell_width,
                  color, &mono_font);
}

void astra_draw_list_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                               const char *utf8, uint32_t length,
                               uint16_t pixel_height, uint16_t cell_width,
                               uint16_t color)
{
    if (cell_width != 0u)
        draw_list_text(surface, x, y, utf8, length, pixel_height, cell_width,
                       color, ASTRA_DRAW_LIST_MONO_TEXT);
}
