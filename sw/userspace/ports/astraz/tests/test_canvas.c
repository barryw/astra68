#include "canvas.h"
#include "picture.h"

#include <astra/image_view.h>

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static AstraSurfaceCreateInfo created;
static AstraRectI32 painted;
static AstraColorRGBA8 paint_color;
static AstraPointI32 text_origin;
static AstraColorRGBA8 text_color;
static uint32_t text_height;
static char text_face[64];
static char last_text[64];
static AstraImageViewInfo picture_view;
static AstraRectI32 scroll_source, scroll_destination;
static uint32_t paints, texts, pictures, scrolls, overlay_texts;

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

AstraResult astra_draw_font_text(AstraDrawList *list, AstraPointI32 origin,
    const char *face_id, uint32_t id_bytes, const char *utf8, uint32_t bytes,
    uint32_t height, uint32_t style, AstraColorRGBA8 color)
{
    (void)list;
    assert(id_bytes < sizeof(text_face));
    memcpy(text_face, face_id, id_bytes);
    text_face[id_bytes] = '\0';
    assert(style == 0u);
    assert(bytes < sizeof(last_text));
    memcpy(last_text, utf8, bytes);
    last_text[bytes] = '\0';
    text_origin = origin;
    text_height = height;
    text_color = color;
    ++texts;
    return ASTRA_OK;
}

AstraResult astra_draw_ui_text(AstraDrawList *list, AstraPointI32 origin,
    const char *utf8, uint32_t bytes, uint32_t height,
    uint32_t style, AstraColorRGBA8 color)
{
    (void)list;
    static const char label[] = "More — press a key";
    assert(bytes == sizeof(label) - 1u &&
           memcmp(utf8, label, bytes) == 0);
    assert(height == 13u && style == 0u);
    assert(origin.x == 240 && origin.y == 396);
    assert(color.red == 255u && color.green == 255u &&
           color.blue == 255u);
    ++overlay_texts;
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
    assert(options == NULL);
    scroll_source = *from;
    scroll_destination = *to;
    ++scrolls;
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
    uint32_t fitted_width = 0u, fitted_height = 0u;
    assert(astraz_canvas_view_fit(800u, 500u,
        &fitted_width, &fitted_height));
    assert(fitted_width == 800u && fitted_height == 500u);
    assert(astraz_canvas_view_fit(1200u, 500u,
        &fitted_width, &fitted_height));
    assert(fitted_width == 800u && fitted_height == 500u);
    assert(astraz_canvas_view_fit(100u, 300u,
        &fitted_width, &fitted_height));
    assert(fitted_width == 100u && fitted_height == 62u);
    assert(!astraz_canvas_view_fit(0u, 300u,
        &fitted_width, &fitted_height));
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraZCanvas canvas;
    display._private_handle = 4u;
    assert(astraz_canvas_open(&canvas, &display));
    assert(created.width == 320u && created.height == 200u);
    assert(ASTRAZ_STORY_WIDTH == created.width);
    assert(ASTRAZ_STORY_HEIGHT == created.height);
    assert(ASTRAZ_CANVAS_VIEW_WIDTH == created.width * 2u);
    assert(ASTRAZ_CANVAS_VIEW_HEIGHT == created.height * 2u);

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
    assert(strcmp(text_face, ASTRAZ_PIXEL_FACE_REGULAR) == 0);
    assert(text_color.red == 0u && text_color.green == 0u &&
           text_color.blue == 0u);

    AstraZEvent style = {.kind = ASTRAZ_EVENT_STYLE};
    style.data.style.window = 0;
    style.data.style.style = ASTRAZ_STYLE_BOLD;
    assert(astraz_canvas_event(&canvas, &style));
    assert(astraz_canvas_event(&canvas, &character));
    assert(strcmp(text_face, ASTRAZ_PIXEL_FACE_BOLD) == 0);
    style.data.style.style = ASTRAZ_STYLE_EMPHASIS;
    assert(astraz_canvas_event(&canvas, &style));
    assert(astraz_canvas_event(&canvas, &character));
    assert(strcmp(text_face, ASTRAZ_PIXEL_FACE_ITALIC) == 0);
    style.data.style.style = ASTRAZ_STYLE_BOLD | ASTRAZ_STYLE_EMPHASIS;
    assert(astraz_canvas_event(&canvas, &style));
    assert(astraz_canvas_event(&canvas, &character));
    assert(strcmp(text_face, ASTRAZ_PIXEL_FACE_BOLD_ITALIC) == 0);
    AstraZEvent font = {.kind = ASTRAZ_EVENT_FONT};
    font.data.font.window = 0;
    font.data.font.font = ASTRAZ_FONT_GRAPHICS;
    assert(astraz_canvas_event(&canvas, &font));
    assert(astraz_canvas_event(&canvas, &character));
    assert(strcmp(text_face, ASTRAZ_GRAPHICS_FACE) == 0);
    font.data.font.font = ASTRAZ_FONT_TEXT;
    assert(astraz_canvas_event(&canvas, &font));
    assert(astraz_canvas_event(&canvas, &character));
    assert(strcmp(text_face, ASTRAZ_PIXEL_FACE_BOLD_ITALIC) == 0);

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
    AstraZEvent scroll = {.kind = ASTRAZ_EVENT_SCROLL};
    scroll.data.scroll.top = scroll.data.scroll.left = 1;
    scroll.data.scroll.bottom = ASTRAZ_STORY_HEIGHT;
    scroll.data.scroll.right = ASTRAZ_STORY_WIDTH;
    scroll.data.scroll.units = 8;
    assert(astraz_canvas_event(&canvas, &scroll));
    assert(scrolls == 1u);
    assert(scroll_source.y == 8 && scroll_destination.y == 0);
    assert(scroll_source.height == 192u &&
           scroll_destination.height == 192u);
    cursor.data.cursor.row = INT_MAX;
    cursor.data.cursor.column = INT_MAX;
    assert(astraz_canvas_event(&canvas, &cursor));
    assert(astraz_canvas_event(&canvas, &character));
    assert(texts == 6u);
    picture.data.picture.row = INT_MAX;
    picture.data.picture.column = INT_MAX;
    assert(astraz_canvas_event(&canvas, &picture));
    assert(pictures == 1u);
    AstraDrawList output = ASTRA_DRAW_LIST_INIT;
    output._private_handle = 5u;
    AstraRectI32 frame = {10, 20, 640u, 400u};
    assert(astraz_canvas_more_overlay(&output, &frame, &frame) == ASTRA_OK);
    assert(overlay_texts == 1u);
    assert(painted.x == 230 && painted.y == 392 &&
           painted.width == 200u && painted.height == 24u);
    assert(paint_color.red == 0u && paint_color.green == 0u &&
           paint_color.blue == 0u);
    cursor.data.cursor.row = 193;
    cursor.data.cursor.column = 44;
    assert(astraz_canvas_event(&canvas, &cursor));
    assert(astraz_canvas_input_begin(&canvas, 64u));
    assert(astraz_canvas_input_render(&canvas, "look", 4u, 4u, 1));
    assert(strcmp(last_text, "look") == 0);
    assert(text_origin.x == 43 && text_origin.y == 192);
    assert(painted.x == 75 && painted.y == 198 &&
           painted.width == 8u && painted.height == 2u);
    assert(astraz_canvas_input_render(&canvas, "loo", 3u, 3u, 0));
    assert(strcmp(last_text, "loo") == 0);
    assert(painted.x == 43 && painted.y == 192 &&
           painted.width == 64u && painted.height == 8u);
    astraz_canvas_input_end(&canvas);
    assert(!canvas.input_active);
    cursor.data.cursor.row = 201;
    cursor.data.cursor.column = 44;
    assert(astraz_canvas_event(&canvas, &cursor));
    assert(astraz_canvas_input_begin(&canvas, 64u));
    assert(astraz_canvas_input_render(&canvas, "", 0u, 0u, 1));
    assert(painted.y == 198 && painted.height == 2u);
    astraz_canvas_input_end(&canvas);
    AstraRectI32 small = {0, 0, 199u, 27u};
    assert(astraz_canvas_more_overlay(&output, &small, &small) == ASTRA_OK);
    assert(overlay_texts == 1u);
    astraz_canvas_close(&canvas);
    return 0;
}
