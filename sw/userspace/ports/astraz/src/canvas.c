#include "canvas.h"
#include "picture.h"

#include <astra/image_view.h>
#include <astra/utf8.h>
#include <astra/text_style.h>
#include <SDL.h>

#include <string.h>
#include <limits.h>

static const AstraRectI32 bounds = {0, 0, ASTRAZ_CANVAS_WIDTH,
                                    ASTRAZ_CANVAS_HEIGHT};

static AstraColorRGBA8 color(int32_t index, int background)
{
    static const AstraColorRGBA8 palette[] = {
        {255, 255, 255, 255}, {255, 255, 255, 255},
        {0, 0, 0, 255}, {200, 0, 0, 255},
        {0, 160, 0, 255}, {200, 160, 0, 255},
        {0, 0, 200, 255}, {160, 0, 160, 255},
        {0, 160, 160, 255}, {255, 255, 255, 255},
        {128, 128, 128, 255}
    };
    if (index == ASTRAZ_COLOUR_DEFAULT)
        index = background ? ASTRAZ_COLOUR_WHITE : ASTRAZ_COLOUR_BLACK;
    if (index < ASTRAZ_COLOUR_BLACK || index > ASTRAZ_COLOUR_GREY)
        index = background ? ASTRAZ_COLOUR_WHITE : ASTRAZ_COLOUR_BLACK;
    return palette[index];
}

static AstraResult ensure_list(AstraZCanvas *canvas)
{
    if (canvas->list._private_handle != ASTRA_INVALID_HANDLE) return ASTRA_OK;
    return astra_draw_list_create(&canvas->surface, &bounds, &canvas->list);
}

AstraResult astraz_canvas_flush(AstraZCanvas *canvas)
{
    if (!canvas || canvas->surface._private_handle == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (canvas->list._private_handle == ASTRA_INVALID_HANDLE) return ASTRA_OK;
    AstraResult result = astra_draw_post(&canvas->list, 0u);
    AstraResult closed = astra_draw_list_close(&canvas->list);
    canvas->operations = 0u;
    return result == ASTRA_OK ? closed : result;
}

static AstraResult command_done(AstraZCanvas *canvas, AstraResult result)
{
    if (result != ASTRA_OK) return result;
    return ++canvas->operations >= 128u ? astraz_canvas_flush(canvas) :
        ASTRA_OK;
}

static AstraResult fill(AstraZCanvas *canvas, const AstraRectI32 *rect,
                        AstraColorRGBA8 foreground)
{
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    paint.foreground = foreground;
    AstraResult result = ensure_list(canvas);
    if (result == ASTRA_OK)
        result = astra_draw_rectangle(&canvas->list, rect, 1, &paint);
    return command_done(canvas, result);
}

int astraz_canvas_open(AstraZCanvas *canvas, AstraDisplay *display)
{
    if (!canvas || !display) return 0;
    memset(canvas, 0, sizeof(*canvas));
    canvas->surface = (AstraSurface)ASTRA_SURFACE_INIT;
    canvas->list = (AstraDrawList)ASTRA_DRAW_LIST_INIT;
    canvas->display = display;
    AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
    info.flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_DRAW_SOURCE;
    info.width = ASTRAZ_CANVAS_WIDTH;
    info.height = ASTRAZ_CANVAS_HEIGHT;
    info.format = ASTRA_PIXEL_FORMAT_RGB565;
    if (astra_surface_create(display, &info, &canvas->surface) != ASTRA_OK)
        return 0;
    for (uint32_t at = 0u; at < 8u; ++at) {
        canvas->row[at] = canvas->column[at] = 1;
        canvas->foreground[at] = ASTRAZ_COLOUR_BLACK;
        canvas->background[at] = ASTRAZ_COLOUR_WHITE;
    }
    if (fill(canvas, &bounds, color(ASTRAZ_COLOUR_WHITE, 1)) != ASTRA_OK ||
        astraz_canvas_flush(canvas) != ASTRA_OK) {
        astraz_canvas_close(canvas);
        return 0;
    }
    return 1;
}

static AstraResult picture(AstraZCanvas *canvas, const AstraZEvent *event)
{
    if (event->data.picture.resource.format == UINT32_C(0x52656374))
    {
        SDL_SetError("Blorb Rect has dimensions only; it cannot be drawn");
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    AstraSurface image = ASTRA_SURFACE_INIT;
    int width = 0, height = 0;
    AstraResult result = astraz_picture_upload(canvas->display,
        &event->data.picture.resource, &image, &height, &width);
    if (result == ASTRA_OK) result = ensure_list(canvas);
    if (result == ASTRA_OK) {
        AstraImageViewInfo info = ASTRA_IMAGE_VIEW_INFO_INIT;
        info.frame = (AstraRectI32){event->data.picture.column - 1,
            event->data.picture.row - 1, (uint32_t)width,
            (uint32_t)height};
        info.clip = bounds;
        info.source_rect = (AstraRectI32){0, 0, (uint32_t)width,
                                           (uint32_t)height};
        info.image = &image;
        result = astra_interface_image_view_render(&canvas->list, &info);
    }
    if (result == ASTRA_OK) result = astraz_canvas_flush(canvas);
    if (image._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_surface_close(&image);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

static AstraResult character(AstraZCanvas *canvas, const AstraZEvent *event)
{
    int32_t window = event->data.text.window;
    if (window < 0 || window >= 8) return ASTRA_OK;
    uint32_t scalar = event->data.text.character;
    if (scalar == '\n' || scalar == '\r') {
        canvas->row[window] += 8;
        canvas->column[window] = 1;
        return ASTRA_OK;
    }
    if (scalar < 32u) return ASTRA_OK;
    char bytes[4];
    uint32_t length = astra_utf8_encode(scalar, bytes);
    if (!length) return ASTRA_ERROR_INVALID_ARGUMENT;
    int32_t x = canvas->column[window] - 1;
    int32_t y = canvas->row[window] - 1;
    canvas->column[window] += 8;
    if (x >= (int32_t)bounds.width || y >= (int32_t)bounds.height ||
        x + 8 <= 0 || y + 8 <= 0) return ASTRA_OK;
    uint32_t style = canvas->style[window];
    AstraColorRGBA8 foreground = color(canvas->foreground[window], 0);
    AstraColorRGBA8 background = color(canvas->background[window], 1);
    if ((style & ASTRAZ_STYLE_REVERSE) != 0u) {
        AstraColorRGBA8 exchange = foreground;
        foreground = background;
        background = exchange;
    }
    AstraResult result = ASTRA_OK;
    if (canvas->background[window] != ASTRAZ_COLOUR_TRANSPARENT) {
        AstraRectI32 cell = {x, y, 8u, 8u};
        result = fill(canvas, &cell, background);
    }
    if (result == ASTRA_OK) result = ensure_list(canvas);
    if (result == ASTRA_OK) {
        result = astra_draw_ui_text(&canvas->list, (AstraPointI32){x, y},
            bytes, length, 8u,
            ((style & ASTRAZ_STYLE_BOLD) ? ASTRA_TEXT_STYLE_BOLD : 0u) |
            ((style & ASTRAZ_STYLE_EMPHASIS) ? ASTRA_TEXT_STYLE_ITALIC : 0u),
            foreground);
    }
    return command_done(canvas, result);
}

static AstraResult scroll_region(AstraZCanvas *canvas,
    const AstraZEvent *event)
{
    int32_t left = event->data.scroll.left - 1;
    int32_t top = event->data.scroll.top - 1;
    int32_t right = event->data.scroll.right;
    int32_t bottom = event->data.scroll.bottom;
    int32_t units = event->data.scroll.units;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int32_t)bounds.width) right = (int32_t)bounds.width;
    if (bottom > (int32_t)bounds.height) bottom = (int32_t)bounds.height;
    if (left >= right || top >= bottom || units == 0) return ASTRA_OK;
    int32_t magnitude = units > 0 ? units :
        units == INT32_MIN ? INT32_MAX : -units;
    AstraRectI32 area = {left, top, (uint32_t)(right - left),
                         (uint32_t)(bottom - top)};
    if (magnitude >= bottom - top)
        return fill(canvas, &area, color(ASTRAZ_COLOUR_WHITE, 1));
    AstraRectI32 source = {left, units > 0 ? top + units : top,
        area.width, (uint32_t)(bottom - top - magnitude)};
    AstraRectI32 destination = {left, units > 0 ? top : top + magnitude,
        source.width, source.height};
    AstraResult result = ensure_list(canvas);
    if (result == ASTRA_OK)
        result = astra_draw_list_set_clip(&canvas->list, &area);
    if (result == ASTRA_OK)
        result = astra_draw_blit(&canvas->list, &canvas->surface,
            &source, &destination, NULL);
    if (result == ASTRA_OK)
        result = astra_draw_list_set_clip(&canvas->list, &bounds);
    if (result != ASTRA_OK) return result;
    result = command_done(canvas, ASTRA_OK);
    if (result != ASTRA_OK) return result;
    AstraRectI32 exposed = {left, units > 0 ? bottom - magnitude : top,
        area.width, (uint32_t)magnitude};
    return fill(canvas, &exposed, color(ASTRAZ_COLOUR_WHITE, 1));
}

int astraz_canvas_event(AstraZCanvas *canvas, const AstraZEvent *event)
{
    if (!canvas || !event ||
        canvas->surface._private_handle == ASTRA_INVALID_HANDLE) return 0;
    AstraResult result = ASTRA_OK;
    int32_t window;
    switch (event->kind) {
    case ASTRAZ_EVENT_SCREEN_BEGIN:
        for (uint32_t at = 0u; at < 8u; ++at) {
            canvas->row[at] = canvas->column[at] = 1;
            canvas->style[at] = 0u;
            canvas->foreground[at] = ASTRAZ_COLOUR_BLACK;
            canvas->background[at] = ASTRAZ_COLOUR_WHITE;
        }
        result = fill(canvas, &bounds, color(ASTRAZ_COLOUR_WHITE, 1));
        break;
    case ASTRAZ_EVENT_RESTART:
        if (event->data.restart.stage == ASTRAZ_RESTART_BEGIN)
            result = fill(canvas, &bounds, color(ASTRAZ_COLOUR_WHITE, 1));
        break;
    case ASTRAZ_EVENT_PICTURE:
        result = picture(canvas, event);
        break;
    case ASTRAZ_EVENT_CURSOR:
        window = event->data.cursor.window;
        if (window >= 0 && window < 8) {
            canvas->row[window] = event->data.cursor.row;
            canvas->column[window] = event->data.cursor.column;
        }
        break;
    case ASTRAZ_EVENT_CHARACTER:
        result = character(canvas, event);
        break;
    case ASTRAZ_EVENT_STYLE:
        window = event->data.style.window;
        if (window >= 0 && window < 8) {
            canvas->style[window] = (uint32_t)event->data.style.style;
        }
        break;
    case ASTRAZ_EVENT_COLOUR:
        window = event->data.colour.window;
        if (window >= 0 && window < 8) {
            canvas->foreground[window] = event->data.colour.foreground;
            canvas->background[window] = event->data.colour.background;
        }
        break;
    case ASTRAZ_EVENT_ERASE:
        if (event->data.erase.window == -2)
            result = fill(canvas, &bounds, color(ASTRAZ_COLOUR_WHITE, 1));
        else if (event->data.erase.right >= event->data.erase.left &&
                 event->data.erase.bottom >= event->data.erase.top) {
            AstraRectI32 rect = {
                event->data.erase.left - 1, event->data.erase.top - 1,
                (uint32_t)(event->data.erase.right - event->data.erase.left + 1),
                (uint32_t)(event->data.erase.bottom - event->data.erase.top + 1)};
            result = fill(canvas, &rect, color(ASTRAZ_COLOUR_WHITE, 1));
        }
        break;
    case ASTRAZ_EVENT_SCREEN_END:
    case ASTRAZ_EVENT_REPAINT:
        result = astraz_canvas_flush(canvas);
        break;
    case ASTRAZ_EVENT_SCROLL:
        result = scroll_region(canvas, event);
        break;
    default:
        break;
    }
    return result == ASTRA_OK;
}

void astraz_canvas_close(AstraZCanvas *canvas)
{
    if (!canvas) return;
    if (canvas->list._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_draw_list_close(&canvas->list);
        (void)closed;
    }
    if (canvas->surface._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_surface_close(&canvas->surface);
        (void)closed;
    }
    canvas->display = NULL;
}
