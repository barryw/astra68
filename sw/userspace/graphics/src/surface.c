#include <astra/surface.h>

#if !defined(ASTRA_SURFACE_CORE) && !defined(ASTRA_SURFACE_FONT)
#define ASTRA_SURFACE_CORE 1
#define ASTRA_SURFACE_FONT 1
#endif

#if defined(ASTRA_SURFACE_FONT)
#include <astra/utf8.h>
#include <astra/ui_font.h>
#include <astra/fonts.h>
#include <astra/afnt.h>
#include <astra/runtime.h>
#include <astra/result.h>
#endif

#include <astra/draw_list.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(ASTRA_SURFACE_CORE)
#include <astra/rounded.h>
#endif

#if defined(ASTRA_SURFACE_FONT)
#include "astra_font8x8.inc"
#include "font_data.h"
#endif



static uint16_t *row(AstraSurfaceView *surface, uint32_t y);

#if defined(ASTRA_SURFACE_FONT)
static uint16_t blend_rgb565(uint16_t foreground, uint16_t background,
                             uint8_t coverage)
{
    uint32_t inverse = 255u - coverage;
    uint32_t red = ((((foreground >> 11u) & 31u) * coverage) +
                    (((background >> 11u) & 31u) * inverse) + 127u) / 255u;
    uint32_t green = ((((foreground >> 5u) & 63u) * coverage) +
                      (((background >> 5u) & 63u) * inverse) + 127u) / 255u;
    uint32_t blue = (((foreground & 31u) * coverage) +
                     ((background & 31u) * inverse) + 127u) / 255u;

    return (uint16_t)((red << 11u) | (green << 5u) | blue);
}

static uint8_t glyph_coverage(const AstraUiStrike *strike,
                              const AstraUiGlyph *glyph,
                              const uint8_t *bitmap, uint32_t x, uint32_t y)
{
    const uint8_t *pixel = bitmap + y * glyph->pitch;

    if (strike->bitmap_format == ASTRA_FONT_BITMAP_MASK1)
        return (pixel[x / 8u] & (uint8_t)(0x80u >> (x & 7u))) != 0u ?
               255u : 0u;
    if (strike->bitmap_format == ASTRA_FONT_BITMAP_A8)
        return pixel[x];
    return 0u;
}

static void blend_glyph_pixel(AstraSurfaceView *surface, int32_t x,
                              int32_t y, uint16_t color, uint8_t coverage)
{
    uint16_t *pixel;

    if (coverage == 0u || x < surface->clip_left || x >= surface->clip_right ||
        y < surface->clip_top || y >= surface->clip_bottom)
        return;
    pixel = row(surface, (uint32_t)y) + (uint32_t)x;
    *pixel = coverage == 255u ? color : blend_rgb565(color, *pixel, coverage);
}

static _Alignas(4) volatile uint32_t system_font_lock;
static AstraArea system_font_area = ASTRA_AREA_INIT;
static AstraAfntView system_font_view;
static char system_font_id[ASTRA_FONT_ID_MAX];

AstraResult astra_fonts_context_id(char id[ASTRA_FONT_ID_MAX])
{
    if (id == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    id[0] = '\0';
    if (astra_mutex_lock(&system_font_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_ERROR_IO;
    memcpy(id, system_font_id, sizeof(system_font_id));
    (void)astra_mutex_unlock(&system_font_lock);
    return ASTRA_OK;
}

static AstraResult adopt_font_context(AstraArea *area,
                                      const AstraAfntView *view, const char *id)
{
    AstraArea previous;
    uint32_t id_length = 0u;
    if (id == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    while (id_length < sizeof(system_font_id) && id[id_length] != '\0')
        ++id_length;
    if (id_length == sizeof(system_font_id)) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (astra_mutex_lock(&system_font_lock) != ASTRA_SYSCALL_OK)
        return ASTRA_ERROR_IO;
    if (area == NULL && system_font_area.handle != ASTRA_INVALID_HANDLE) {
        (void)astra_mutex_unlock(&system_font_lock);
        return ASTRA_ERROR_BUSY;
    }
    previous = system_font_area;
    system_font_area = area == NULL ? (AstraArea)ASTRA_AREA_INIT : *area;
    if (area != NULL) *area = (AstraArea)ASTRA_AREA_INIT;
    system_font_view = *view;
    memcpy(system_font_id, id, id_length + 1u);
    /* Duplicate handles share one process mapping, without map references. */
    if (previous.address == system_font_area.address) {
        previous.address = NULL;
        previous.size = previous.map_flags = 0u;
    }
    (void)astra_mutex_unlock(&system_font_lock);
    return previous.handle == ASTRA_INVALID_HANDLE ? ASTRA_OK :
           astra_area_close(&previous);
}

AstraResult astra_fonts_use_borrowed(const void *bytes, uint32_t length,
                                    const char *id)
{
    AstraAfntView view;
    if (bytes == NULL || length == 0u || length > ASTRA_FONT_IMPORT_MAX ||
        id == NULL || id[0] == '\0' || !astra_afnt_validate(bytes, length, &view))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return adopt_font_context(NULL, &view, id);
}

AstraResult astra_fonts_use_snapshot(AstraArea *area, uint32_t length,
                                   const char *id)
{
    AstraAfntView view = {0};
    if (area == NULL || id == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (length > ASTRA_FONT_IMPORT_MAX ||
        ((length != 0u) != (id[0] != '\0')) ||
        (length == 0u && (area->handle != ASTRA_INVALID_HANDLE ||
            area->address != NULL || area->size != 0u || area->map_flags != 0u)) ||
        (length != 0u &&
         (area->handle == ASTRA_INVALID_HANDLE ||
          area->map_flags != ASTRA_AREA_MAP_READ ||
          area->address == NULL || area->size < length ||
          !astra_afnt_validate(area->address, length, &view))))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return adopt_font_context(area, &view, id);
}
#endif

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

static int clip_valid(const AstraSurfaceView *surface);
static int clip_empty(const AstraSurfaceView *surface);

/* ADLT colors are ARGB8888; 5:6:5 expands by bit replication and returns
   to the identical RGB565 value when the service converts it back. */
static uint32_t draw_color(uint16_t color)
{
    uint32_t red = (color >> 11u) & 31u;
    uint32_t green = (color >> 5u) & 63u;
    uint32_t blue = color & 31u;

    return UINT32_C(0xff000000) |
           (((red << 3u) | (red >> 2u)) << 16u) |
           (((green << 2u) | (green >> 4u)) << 8u) |
           ((blue << 3u) | (blue >> 2u));
}

static AstraDrawListCommand *append_command(AstraSurfaceView *surface,
                                            uint32_t operation)
{
    AstraDrawListHeader *header;
    AstraDrawListCommand *command;

    if (!clip_valid(surface) || clip_empty(surface) ||
        surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST)
        return NULL;
    header = draw_header(surface);
    if (header->command_count >= header->command_capacity)
        return NULL;
    command = &draw_commands(surface)[header->command_count++];
    *command = (AstraDrawListCommand){0};
    command->operation = operation;
    command->clip_left = surface->clip_left;
    command->clip_top = surface->clip_top;
    command->clip_right = surface->clip_right;
    command->clip_bottom = surface->clip_bottom;
    return command;
}

#if defined(ASTRA_SURFACE_CORE)
static const uint16_t *const_row(const AstraSurfaceView *surface, uint32_t y)
{
    return (const uint16_t *)((const uint8_t *)surface->pixels +
                             y * surface->pitch);
}

static void full_clip(AstraSurfaceView *surface)
{
    surface->clip_left = 0u;
    surface->clip_top = 0u;
    surface->clip_right = surface->width;
    surface->clip_bottom = surface->height;
}
#endif

static int clip_valid(const AstraSurfaceView *surface)
{
    return surface != NULL && surface->pixels != NULL &&
           surface->clip_left <= surface->clip_right &&
           surface->clip_right <= surface->width &&
           surface->clip_top <= surface->clip_bottom &&
           surface->clip_bottom <= surface->height;
}

static int clip_empty(const AstraSurfaceView *surface)
{
    return surface->clip_left == surface->clip_right ||
           surface->clip_top == surface->clip_bottom;
}

#if defined(ASTRA_SURFACE_CORE)
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
    full_clip(surface);
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
    full_clip(surface);
    header = draw_header(surface);
    *header = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_8,
        .total_bytes = ASTRA_DRAW_LIST_AREA_BYTES,
        .width = width,
        .height = height,
        .command_capacity = ASTRA_DRAW_LIST_COMMAND_MAX,
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
        header->version != ASTRA_DRAW_LIST_VERSION_1_8 ||
        header->total_bytes != ASTRA_DRAW_LIST_AREA_BYTES ||
        header->width != width || header->height != height ||
        header->command_capacity != ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->command_count > ASTRA_DRAW_LIST_COMMAND_MAX ||
        header->payload_bytes > ASTRA_DRAW_LIST_PAYLOAD_BYTES)
        return 0;
    surface->pixels = storage;
    surface->byte_size = byte_size;
    surface->pitch = 0u;
    surface->width = width;
    surface->height = height;
    surface->kind = ASTRA_SURFACE_VIEW_DRAW_LIST;
    full_clip(surface);
    return 1;
}

int astra_surface_clip(AstraSurfaceView *surface, int32_t x, int32_t y,
                       uint32_t width, uint32_t height)
{
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + width;
    int64_t bottom = top + height;

    if (!clip_valid(surface))
        return 0;
    if (left < surface->clip_left) left = surface->clip_left;
    if (top < surface->clip_top) top = surface->clip_top;
    if (left > surface->clip_right) left = surface->clip_right;
    if (top > surface->clip_bottom) top = surface->clip_bottom;
    if (right > surface->clip_right) right = surface->clip_right;
    if (bottom > surface->clip_bottom) bottom = surface->clip_bottom;
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    surface->clip_left = (uint16_t)left;
    surface->clip_top = (uint16_t)top;
    surface->clip_right = (uint16_t)right;
    surface->clip_bottom = (uint16_t)bottom;
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
        if (!clip_valid(surface) || clip_empty(surface) ||
            width == 0u || height == 0u)
            return;
        command = append_command(surface, ASTRA_DRAW_LIST_FILL);
        if (command != NULL) {
            command->x = x;
            command->y = y;
            command->width = width;
            command->height = height;
            command->color = draw_color(color);
        }
        return;
    }
    if (!clip_valid(surface) || width == 0u ||
        height == 0u)
        return;
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    if (right <= surface->clip_left || bottom <= surface->clip_top ||
        x >= surface->clip_right || y >= surface->clip_bottom)
        return;
    first_x = x < surface->clip_left ? surface->clip_left : (uint32_t)x;
    first_y = y < surface->clip_top ? surface->clip_top : (uint32_t)y;
    last_x = right > surface->clip_right ? surface->clip_right :
                                                (uint32_t)right;
    last_y = bottom > surface->clip_bottom ? surface->clip_bottom :
                                                  (uint32_t)bottom;
    for (uint32_t at_y = first_y; at_y < last_y; ++at_y) {
        uint16_t *pixels = row(surface, at_y);

        for (uint32_t at_x = first_x; at_x < last_x; ++at_x)
            pixels[at_x] = color;
    }
}

enum {
    LINE_LEFT = 1u,
    LINE_RIGHT = 2u,
    LINE_TOP = 4u,
    LINE_BOTTOM = 8u,
};

static uint32_t line_outcode(const AstraSurfaceView *surface,
                             int64_t x, int64_t y)
{
    uint32_t code = 0u;

    if (x < surface->clip_left) code |= LINE_LEFT;
    else if (x >= surface->clip_right) code |= LINE_RIGHT;
    if (y < surface->clip_top) code |= LINE_TOP;
    else if (y >= surface->clip_bottom) code |= LINE_BOTTOM;
    return code;
}

static int clip_line(const AstraSurfaceView *surface,
                     int64_t *x0, int64_t *y0, int64_t *x1, int64_t *y1)
{
    uint32_t first = line_outcode(surface, *x0, *y0);
    uint32_t last = line_outcode(surface, *x1, *y1);

    while ((first | last) != 0u) {
        uint32_t outside = first != 0u ? first : last;
        int64_t x;
        int64_t y;

        if ((first & last) != 0u)
            return 0;
        if ((outside & LINE_TOP) != 0u) {
            y = surface->clip_top;
            x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0);
        } else if ((outside & LINE_BOTTOM) != 0u) {
            y = (int64_t)surface->clip_bottom - 1;
            x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0);
        } else if ((outside & LINE_RIGHT) != 0u) {
            x = (int64_t)surface->clip_right - 1;
            y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0);
        } else {
            x = surface->clip_left;
            y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0);
        }
        if (outside == first) {
            *x0 = x;
            *y0 = y;
            first = line_outcode(surface, x, y);
        } else {
            *x1 = x;
            *y1 = y;
            last = line_outcode(surface, x, y);
        }
    }
    return 1;
}

int astra_surface_line(AstraSurfaceView *surface, int32_t x0, int32_t y0,
                       int32_t x1, int32_t y1, uint16_t color)
{
    AstraDrawListCommand *command;
    int64_t first_x = x0;
    int64_t first_y = y0;
    int64_t last_x = x1;
    int64_t last_y = y1;

    if (!clip_valid(surface))
        return 0;
    if (clip_empty(surface))
        return 1;
    if (surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        if (x0 < INT16_MIN || x0 > INT16_MAX || y0 < INT16_MIN ||
            y0 > INT16_MAX || x1 < INT16_MIN || x1 > INT16_MAX ||
            y1 < INT16_MIN || y1 > INT16_MAX)
            return 0;
        command = append_command(surface, ASTRA_DRAW_LIST_LINE);
        if (command == NULL)
            return 0;
        command->x = x0;
        command->y = y0;
        command->width = (uint32_t)x1;
        command->height = (uint32_t)y1;
        command->color = draw_color(color);
        return 1;
    }
    if (surface->kind != ASTRA_SURFACE_VIEW_RGB565 ||
        !clip_line(surface, &first_x, &first_y, &last_x, &last_y))
        return surface->kind == ASTRA_SURFACE_VIEW_RGB565;
    {
        int64_t dx = last_x >= first_x ? last_x - first_x : first_x - last_x;
        int64_t sx = first_x < last_x ? 1 : -1;
        int64_t dy = -(last_y >= first_y ? last_y - first_y :
                                              first_y - last_y);
        int64_t sy = first_y < last_y ? 1 : -1;
        int64_t error = dx + dy;

        for (;;) {
            row(surface, (uint32_t)first_y)[(uint32_t)first_x] = color;
            if (first_x == last_x && first_y == last_y)
                break;
            if (error * 2 >= dy) {
                error += dy;
                first_x += sx;
            }
            if (error * 2 <= dx) {
                error += dx;
                first_y += sy;
            }
        }
    }
    return 1;
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

    if (!clip_valid(surface) ||
        surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        width == 0u || height == 0u ||
        source_x > surface->width || width > surface->width - source_x ||
        source_y > surface->height || height > surface->height - source_y ||
        destination_x > surface->width ||
        width > surface->width - destination_x ||
        destination_y > surface->height ||
        height > surface->height - destination_y ||
        surface->width > INT16_MAX || surface->height > INT16_MAX)
        return 0;
    if (clip_empty(surface))
        return 1;
    command = append_command(surface, ASTRA_DRAW_LIST_BLIT);
    if (command == NULL)
        return 0;
    command->x = (int32_t)destination_x;
    command->y = (int32_t)destination_y;
    command->width = width;
    command->height = height;
    command->color = ASTRA_DRAW_LIST_COLOR_IDENTITY;
    command->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION;
    command->source_x = (int16_t)source_x;
    command->source_y = (int16_t)source_y;
    command->source_width = (uint16_t)width;
    command->source_height = (uint16_t)height;
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
            command->color = draw_color(color);
        }
        return;
    }
    for (uint32_t at_y = 0u; at_y < height; ++at_y) {
        uint32_t inset = astra_graphics_rounded_inset(
            at_y, height, bounded_radius);

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

    if (!clip_valid(destination) || source == NULL || source->pixels == NULL ||
        destination_x >= destination->clip_right ||
        destination_y >= destination->clip_bottom ||
        source_x >= source->width || source_y >= source->height)
        return;
    if (destination_x < destination->clip_left) {
        source_x += destination->clip_left - destination_x;
        destination_x = destination->clip_left;
    }
    if (destination_y < destination->clip_top) {
        source_y += destination->clip_top - destination_y;
        destination_y = destination->clip_top;
    }
    if (source_x >= source->width || source_y >= source->height)
        return;
    width = source->width - source_x;
    height = source->height - source_y;
    if (width > destination->clip_right - destination_x)
        width = destination->clip_right - destination_x;
    if (height > destination->clip_bottom - destination_y)
        height = destination->clip_bottom - destination_y;
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

    if (!clip_valid(destination) || source == NULL || source->pixels == NULL)
        return;
    if (bounded_radius > source->width / 2u)
        bounded_radius = source->width / 2u;
    if (bounded_radius > source->height / 2u)
        bounded_radius = source->height / 2u;
    for (uint32_t source_y = 0u; source_y < source->height; ++source_y) {
        int64_t destination_y = (int64_t)y + source_y;
        uint32_t inset = astra_graphics_rounded_inset(
            source_y, source->height, bounded_radius);
        const uint16_t *in = const_row(source, source_y);
        uint16_t *out;

        if (destination_y < destination->clip_top ||
            destination_y >= destination->clip_bottom)
            continue;
        out = row(destination, (uint32_t)destination_y);
        for (uint32_t source_x = inset;
             source_x < (uint32_t)source->width - inset; ++source_x) {
            int64_t destination_x = (int64_t)x + source_x;

            if (destination_x >= destination->clip_left &&
                destination_x < destination->clip_right)
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

    if (!clip_valid(destination) || source == NULL || source->pixels == NULL)
        return;
    if (bounded_radius > source->width / 2u)
        bounded_radius = source->width / 2u;
    if (bounded_radius > source->height / 2u)
        bounded_radius = source->height / 2u;
    for (uint32_t source_y = 0u; source_y < source->height; ++source_y) {
        int64_t destination_y = (int64_t)y + source_y;
        uint32_t inset = source_y < source->height - bounded_radius ? 0u :
                         astra_graphics_rounded_inset(
                             source_y, source->height, bounded_radius);
        const uint16_t *in = const_row(source, source_y);
        uint16_t *out;

        if (destination_y < destination->clip_top ||
            destination_y >= destination->clip_bottom)
            continue;
        out = row(destination, (uint32_t)destination_y);
        for (uint32_t source_x = inset;
             source_x < (uint32_t)source->width - inset; ++source_x) {
            int64_t destination_x = (int64_t)x + source_x;

            if (destination_x >= destination->clip_left &&
                destination_x < destination->clip_right)
                out[destination_x] = in[source_x];
        }
    }
}
#endif

#if defined(ASTRA_SURFACE_FONT)
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

static uint32_t ui_text_measure(const char *utf8, uint32_t length,
                                uint16_t pixel_height, uint64_t maximum_26_6,
                                uint32_t *width, uint32_t *height)
{
    const AstraUiStrike *strike = astra_ui_font_strike(pixel_height);
    uint64_t width_26_6 = 0u;
    uint32_t at = 0u;
    uint32_t selected = UINT32_MAX;

    *width = 0u;
    *height = 0u;
    if (utf8 == NULL ||
        astra_mutex_lock(&system_font_lock) != ASTRA_SYSCALL_OK)
        return 0u;
    if (system_font_id[0] != '\0') {
        for (uint32_t index = 0u; index < system_font_view.strike_count; ++index) {
            AstraAfntStrikeInfo info;
            if (astra_afnt_strike_info(&system_font_view, index, &info) &&
                info.pixel_height == pixel_height &&
                (info.bitmap_format == ASTRA_FONT_BITMAP_MASK1 ||
                 info.bitmap_format == ASTRA_FONT_BITMAP_A8)) {
                selected = index;
                *height = astra_ui_line_height(info.pixel_height,
                    info.ascent, info.descent, info.line_gap);
                break;
            }
        }
        if (selected == UINT32_MAX) goto done;
    } else if (strike == NULL) goto done;
    else *height = astra_ui_line_height(strike->pixel_height,
        strike->ascent, strike->descent, strike->line_gap);
    while (at < length) {
        uint32_t consumed;
        uint32_t scalar = astra_ui_font_scalar(utf8 + at, length - at, &consumed);
        uint32_t advance;

        if (selected != UINT32_MAX) {
            AstraAfntGlyphInfo glyph;
            if (!astra_afnt_glyph_info(&system_font_view, selected, scalar, &glyph) ||
                glyph.advance_x < 0) {
                at = 0u;
                goto done;
            }
            advance = (uint32_t)glyph.advance_x;
        } else {
            const AstraUiGlyph *glyph = astra_ui_font_glyph(strike, scalar);
            advance = (uint32_t)astra_ui_glyph_advance(glyph, 0u);
        }
        if (width_26_6 + advance > maximum_26_6) break;
        width_26_6 += advance;
        at += consumed;
    }
    width_26_6 = (width_26_6 + 63u) / 64u;
    *width = width_26_6 > UINT32_MAX ? UINT32_MAX : (uint32_t)width_26_6;
done:
    (void)astra_mutex_unlock(&system_font_lock);
    return at;
}

uint32_t astra_surface_ui_text_width(const char *utf8, uint32_t length,
                                     uint16_t pixel_height)
{
    uint32_t width, height;
    (void)ui_text_measure(utf8, length, pixel_height, UINT64_MAX, &width, &height);
    return width;
}

uint32_t astra_surface_ui_text_fit(const char *utf8, uint32_t length,
                                   uint16_t pixel_height,
                                   uint32_t maximum_width)
{
    uint32_t width, height;
    return ui_text_measure(utf8, length, pixel_height,
        (uint64_t)maximum_width * 64u, &width, &height);
}

uint32_t astra_surface_ui_text_height(uint16_t pixel_height)
{
    uint32_t width, height;
    (void)ui_text_measure("", 0u, pixel_height, UINT64_MAX, &width, &height);
    return height;
}

static int draw_list_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                          const char *utf8, uint32_t length,
                          uint16_t pixel_height, uint16_t cell_width,
                          uint16_t color, uint16_t operation,
                          uint32_t style_flags, const char *named_id,
                          uint32_t named_id_length)
{
    AstraDrawListHeader *header;
    AstraDrawListCommand *command;
    char id[ASTRA_FONT_ID_MAX] = {0};
    uint32_t id_length = named_id_length;
    const char *id_bytes = named_id == NULL ? id : named_id;

    if (!clip_valid(surface) || utf8 == NULL ||
        surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        (style_flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) != 0u)
        return 0;
    if (clip_empty(surface) || length == 0u)
        return 1;
    if (operation == ASTRA_DRAW_LIST_TEXT) {
        if (astra_fonts_context_id(id) != ASTRA_OK) return 0;
        id_length = (uint32_t)strlen(id);
        if (id_length != 0u) {
            operation = ASTRA_DRAW_LIST_NAMED_TEXT;
            /* shortcut: emphasis uses the selected designed face until
               family variants are resolved. Decorations remain independent. */
            style_flags &= ASTRA_TEXT_STYLE_UNDERLINE | ASTRA_TEXT_STYLE_STRIKETHROUGH;
        }
    }
    header = draw_header(surface);
    if ((uint64_t)length + id_length >
        ASTRA_DRAW_LIST_PAYLOAD_BYTES - header->payload_bytes)
        return 0;
    command = append_command(surface, operation);
    if (command == NULL)
        return 0;
    command->flags = style_flags;
    command->x = x;
    command->y = y;
    command->color = draw_color(color);
    command->font_height = pixel_height;
    command->width = cell_width;
    command->source = id_length;
    command->payload_offset = ASTRA_DRAW_LIST_PAYLOAD_OFFSET +
                              header->payload_bytes;
    command->payload_bytes = id_length + length;
    memcpy((uint8_t *)(void *)surface->pixels + command->payload_offset,
           id_bytes, id_length);
    memcpy((uint8_t *)(void *)surface->pixels + command->payload_offset + id_length,
           utf8, length);
    header->payload_bytes += command->payload_bytes;
    return 1;
}

int astra_surface_font_text(AstraSurfaceView *surface,
    const char *id, uint32_t id_length, int32_t x, int32_t y,
    const char *text, uint32_t length, uint16_t pixel_height, uint16_t color)
{
    return surface != NULL && id != NULL && id_length != 0u &&
        id_length < ASTRA_FONT_ID_MAX && memchr(id, '\0', id_length) == NULL &&
        pixel_height != 0u && astra_utf8_validate(text, length, 0u) &&
        draw_list_text(surface, x, y, text, length, pixel_height, 0u, color,
                       ASTRA_DRAW_LIST_NAMED_TEXT, 0u, id, id_length);
}

static void decorate_text(AstraSurfaceView *surface,
                          const AstraUiStrike *strike, int32_t x, int32_t y,
                          uint32_t width, uint16_t color,
                          uint32_t style_flags)
{
    int32_t baseline = y + astra_ui_fixed_floor(strike->ascent);

    if ((style_flags & ASTRA_TEXT_STYLE_UNDERLINE) != 0u)
        astra_surface_fill(
            surface, x,
            baseline + astra_ui_fixed_floor(strike->underline_position),
            width, (uint32_t)(strike->underline_thickness + 63) / 64u,
            color);
    if ((style_flags & ASTRA_TEXT_STYLE_STRIKETHROUGH) != 0u)
        astra_surface_fill(
            surface, x,
            baseline - astra_ui_fixed_floor(strike->strikeout_position),
            width, (uint32_t)(strike->strikeout_thickness + 63) / 64u,
            color);
}

static int font_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                     const char *utf8, uint32_t length,
                     uint16_t pixel_height, uint16_t cell_width,
                     uint16_t color, const AstraFontBank *font,
                     uint32_t style_flags)
{
    const AstraUiStrike *strike;
    int32_t pen_26_6 = x * 64;
    int32_t start_26_6 = pen_26_6;
    uint32_t at = 0u;

    if (surface == NULL || utf8 == NULL ||
        (style_flags & ~ASTRA_TEXT_RENDER_STYLE_MASK) != 0u)
        return 0;
    if (surface->kind == ASTRA_SURFACE_VIEW_DRAW_LIST) {
        return draw_list_text(
            surface, x, y, utf8, length, pixel_height, cell_width, color,
            font == &astra_mono_font_bank ? ASTRA_DRAW_LIST_MONO_TEXT :
                                 ASTRA_DRAW_LIST_TEXT,
            style_flags, NULL, 0u);
    }
    strike = astra_font_bank_strike(font, pixel_height, style_flags);
    if (strike == NULL ||
        (strike->bitmap_format != ASTRA_FONT_BITMAP_MASK1 &&
         strike->bitmap_format != ASTRA_FONT_BITMAP_A8))
        return 0;
    while (at < length) {
        uint32_t consumed;
        const AstraUiGlyph *glyph = astra_font_bank_glyph(
            font, strike,
            astra_ui_font_scalar(utf8 + at, length - at, &consumed));
        int32_t glyph_x = astra_ui_glyph_x(pen_26_6, glyph);
        int32_t glyph_y = astra_ui_glyph_y(y * 64 + strike->ascent, glyph);
        const uint8_t *bitmap = &font->bitmap[glyph->bitmap_offset];

        for (uint32_t row_index = 0u; row_index < glyph->height; ++row_index)
            for (uint32_t column = 0u; column < glyph->width; ++column)
                blend_glyph_pixel(surface, glyph_x + (int32_t)column,
                                  glyph_y + (int32_t)row_index, color,
                                  glyph_coverage(strike, glyph, bitmap,
                                                 column, row_index));
        pen_26_6 += astra_ui_glyph_advance(glyph, cell_width);
        at += consumed;
    }
    decorate_text(surface, strike, x, y,
                  (uint32_t)(pen_26_6 - start_26_6 + 63) / 64u,
                  color, style_flags);
    return 1;
}

void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *utf8, uint32_t length,
                           uint16_t pixel_height, uint16_t color)
{
    (void)font_text(surface, x, y, utf8, length, pixel_height, 0u, color,
                    &astra_ui_font_bank, 0u);
}

int astra_surface_ui_text_styled(AstraSurfaceView *surface, int32_t x,
                                 int32_t y, const char *utf8,
                                 uint32_t length, uint16_t pixel_height,
                                 uint16_t color, uint32_t style_flags)
{
    return font_text(surface, x, y, utf8, length, pixel_height, 0u, color,
                     &astra_ui_font_bank, style_flags);
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
        (void)font_text(surface, x, y, utf8, length, pixel_height, cell_width,
                        color, &astra_mono_font_bank, 0u);
}

int astra_surface_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color,
                                   uint32_t style_flags)
{
    return cell_width != 0u &&
           font_text(surface, x, y, utf8, length, pixel_height, cell_width,
                     color, &astra_mono_font_bank, style_flags);
}

void astra_draw_list_mono_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                               const char *utf8, uint32_t length,
                               uint16_t pixel_height, uint16_t cell_width,
                               uint16_t color)
{
    if (cell_width != 0u)
        (void)draw_list_text(surface, x, y, utf8, length, pixel_height,
                             cell_width, color, ASTRA_DRAW_LIST_MONO_TEXT,
                             0u, NULL, 0u);
}

int astra_draw_list_mono_text_styled(AstraSurfaceView *surface, int32_t x,
                                     int32_t y, const char *utf8,
                                     uint32_t length, uint16_t pixel_height,
                                     uint16_t cell_width, uint16_t color,
                                     uint32_t style_flags)
{
    return cell_width != 0u &&
           draw_list_text(surface, x, y, utf8, length, pixel_height,
                          cell_width, color, ASTRA_DRAW_LIST_MONO_TEXT,
                          style_flags, NULL, 0u);
}
#endif
