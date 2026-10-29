#include "canvas.h"
#include "picture.h"

#include <astra/image_view.h>

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static AstraSurfaceCreateInfo created;
static AstraRectI32 painted;
static AstraColorRGBA8 paint_color;
static AstraPointI32 text_origin;
static AstraColorRGBA8 text_color;
static uint32_t text_height;
static AstraImageViewInfo picture_view;
static uint32_t paints, texts, pictures;

int SDL_SetError(const char *format, ...)
{
    (void)format;
    return -1;
}

AstraResult astra_surface_create(const AstraDisplay *display,
    const AstraSurfaceCreateInfo *info, AstraSurface *surface)
{
    (void)display;
    created = *info;
    surface->_private_handle = 1u;
    return ASTRA_OK;
}

AstraResult astra_surface_close(AstraSurface *surface)
{
    surface->_private_handle = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}

AstraResult astra_draw_list_create(const AstraSurface *surface,
    const AstraRectI32 *clip, AstraDrawList *list)
{
    (void)surface;
    (void)clip;
    list->_private_handle = 2u;
    return ASTRA_OK;
}

AstraResult astra_draw_list_close(AstraDrawList *list)
{
    list->_private_handle = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}

AstraResult astra_draw_post(AstraDrawList *list, uint32_t flags)
{
    (void)list;
    (void)flags;
    return ASTRA_OK;
}

AstraResult astra_draw_rectangle(AstraDrawList *list,
    const AstraRectI32 *rect, int filled, const AstraDrawPaint *paint)
{
    (void)list;
    assert(filled == 1);
    painted = *rect;
    paint_color = paint->foreground;
    ++paints;
    return ASTRA_OK;
}

AstraResult astra_draw_ui_text(AstraDrawList *list, AstraPointI32 origin,
    const char *utf8, uint32_t bytes, uint32_t height,
    uint32_t style, AstraColorRGBA8 color)
{
    (void)list;
    (void)style;
    assert(bytes == 1u && utf8[0] == 'A');
    text_origin = origin;
    text_height = height;
    text_color = color;
    ++texts;
    return ASTRA_OK;
}

AstraResult astra_draw_list_set_clip(AstraDrawList *list,
    const AstraRectI32 *clip)
{
    (void)list;
    (void)clip;
    return ASTRA_OK;
}

AstraResult astra_draw_blit(AstraDrawList *list, const AstraSurface *source,
    const AstraRectI32 *from, const AstraRectI32 *to,
    const AstraBlitOptions *options)
{
    (void)list;
    (void)source;
    (void)from;
    (void)to;
    (void)options;
    return ASTRA_OK;
}

AstraResult astraz_picture_upload(AstraDisplay *display,
    const AstraZResource *resource, AstraSurface *surface,
    int *height, int *width)
{
    (void)display;
    assert(resource->format == UINT32_C(0x504e4720));
    surface->_private_handle = 3u;
    *height = 166;
    *width = 37;
    return ASTRA_OK;
}

AstraResult astra_interface_image_view_render(AstraDrawList *list,
    const AstraImageViewInfo *info)
{
    (void)list;
    picture_view = *info;
    ++pictures;
    return ASTRA_OK;
}

int main(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraZCanvas canvas;
    display._private_handle = 4u;
    assert(astraz_canvas_open(&canvas, &display));
    assert(created.width == 320u && created.height == 200u);
    assert(ASTRAZ_CANVAS_VIEW_WIDTH == 2u * created.width);
    assert(ASTRAZ_CANVAS_VIEW_HEIGHT == 2u * created.height);

    AstraZEvent colour = {.kind = ASTRAZ_EVENT_COLOUR};
    colour.data.colour.window = 0;
    colour.data.colour.foreground = ASTRAZ_COLOUR_DEFAULT;
    colour.data.colour.background = ASTRAZ_COLOUR_DEFAULT;
    assert(astraz_canvas_event(&canvas, &colour));
    AstraZEvent cursor = {.kind = ASTRAZ_EVENT_CURSOR};
    cursor.data.cursor.window = 0;
    cursor.data.cursor.row = 40;
    cursor.data.cursor.column = 44;
    assert(astraz_canvas_event(&canvas, &cursor));
    AstraZEvent character = {.kind = ASTRAZ_EVENT_CHARACTER};
    character.data.text.window = 0;
    character.data.text.character = 'A';
    assert(astraz_canvas_event(&canvas, &character));
    assert(paints >= 2u && texts == 1u);
    assert(painted.x == 43 && painted.y == 39);
    assert(painted.width == 8u && painted.height == 8u);
    assert(paint_color.red == 255u && paint_color.green == 255u &&
           paint_color.blue == 255u);
    assert(text_origin.x == 43 && text_origin.y == 39);
    assert(text_height == 8u);
    assert(text_color.red == 0u && text_color.green == 0u &&
           text_color.blue == 0u);

    AstraZEvent picture = {.kind = ASTRAZ_EVENT_PICTURE};
    uint8_t png[] = {1u};
    picture.data.picture.row = 35;
    picture.data.picture.column = 284;
    picture.data.picture.resource = (AstraZResource){
        UINT32_C(0x504e4720), png, sizeof(png)};
    assert(astraz_canvas_event(&canvas, &picture));
    assert(pictures == 1u);
    assert(picture_view.frame.x == 283 && picture_view.frame.y == 34);
    assert(picture_view.frame.width == 37u &&
           picture_view.frame.height == 166u);
    assert(picture_view.source_rect.width == 37u &&
           picture_view.source_rect.height == 166u);
    assert(picture_view.clip.width == 320u &&
           picture_view.clip.height == 200u);
    astraz_canvas_close(&canvas);
    return 0;
}
