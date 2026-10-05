#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SDL_VIDEO_RENDER_ASTRA 1
#include "../../build/vendor/SDL2/src/render/astra/SDL_astrarender.c"

/* Recorded Astra graphics calls. */
typedef struct Call {
    enum { FILL, BLIT, LINE, CLIP, TRIANGLES, RECTS, LINES } kind;
    AstraRectI32 rect;
    AstraRectI32 source;
    uint32_t flags;
    uint32_t blend;
    AstraColorRGBA8 color;
    const AstraSurface *texture;
    uint32_t count;
    AstraVertex vertices[12];
    AstraRectI32 rects[8];
} Call;

static Call calls[64];
static int call_count;
/* Posted frames: how many, the last one's flags, the last destination
   the list was pointed at, and the posts seen when uploads and reads
   happened -- what was drawn before them must reach the service first. */
static int posts;
static uint32_t last_post_flags;
static const AstraSurface *last_target;
static int targets;
static int posts_at_read;
static int errors;
static int lists_created;
static int surfaces_created;
static uint32_t created_flags;
static uint16_t created_format;
/* Staging: a pool the mock maps, of staging_size bytes; uploads appended
   to the frame, the rows staged by the copy engine, and the times the
   frame list was taken back. */
static uint8_t staging_pool[1u << 18];
static uint32_t staging_size = 4096u;
static int staging_grows;
static int uploads;
static AstraRectI32 last_upload;
static uint32_t last_upload_offset;
static uint32_t last_upload_pitch;
static int posts_at_upload;
static int stages;
static uint32_t last_stage_offset;
static uint32_t last_stage_pitch;
static uint32_t last_stage_row;
static int list_resets;
static const char *hint_value;
static uint8_t vertex_pool[1u << 17];
static AstraRectI32 last_read;
static uint32_t last_read_pitch;
static const AstraSurface *last_read_surface;
static int reads;
static Uint32 converted_from;
static Uint32 converted_to;
static int converted_pitch;
static size_t vertex_used;

int SDL_SetError(const char *format, ...)
{
    (void)format;
    ++errors;
    return -1;
}
int SDL_Error(SDL_errorcode code) { (void)code; return -1; }
void *SDL_calloc(size_t count, size_t size) { return calloc(count, size); }
void *SDL_malloc(size_t size) { return malloc(size); }
void SDL_free(void *value) { free(value); }
double SDL_fmod(double x, double y) { return fmod(x, y); }
double SDL_floor(double x) { return floor(x); }
double SDL_sin(double x) { return sin(x); }
double SDL_cos(double x) { return cos(x); }
int SDL_ConvertPixels(int width, int height, Uint32 src_format,
                      const void *src, int src_pitch, Uint32 dst_format,
                      void *dst, int dst_pitch)
{
    (void)src;
    (void)dst;
    assert(width > 0 && height > 0 && dst_pitch > 0);
    converted_from = src_format;
    converted_to = dst_format;
    converted_pitch = src_pitch;
    return 0;
}
int SDL_abs(int value) { return value < 0 ? -value : value; }
const char *SDL_GetPixelFormatName(Uint32 format)
{
    (void)format;
    return "test";
}
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    (void)window;
    *w = 320;
    *h = 200;
}
SDL_bool SDL_SetHintWithPriority(const char *name, const char *value,
                                 SDL_HintPriority priority)
{
    assert(strcmp(name, SDL_HINT_RENDER_LINE_METHOD) == 0 &&
           priority == SDL_HINT_DEFAULT);
    hint_value = value;
    return SDL_TRUE;
}
SDL_bool SDL_IntersectRect(const SDL_Rect *a, const SDL_Rect *b,
                           SDL_Rect *result)
{
    int left = a->x > b->x ? a->x : b->x;
    int top = a->y > b->y ? a->y : b->y;
    int right = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int bottom = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;

    *result = (SDL_Rect){ left, top, right - left, bottom - top };
    return right > left && bottom > top ? SDL_TRUE : SDL_FALSE;
}
void *SDL_AllocateRenderVertices(SDL_Renderer *renderer, const size_t bytes,
                                 const size_t alignment, size_t *offset)
{
    (void)renderer;
    (void)alignment;
    assert(vertex_used + bytes <= sizeof(vertex_pool));
    *offset = vertex_used;
    vertex_used += (bytes + 7u) & ~(size_t)7u;
    return vertex_pool + *offset;
}

AstraResult astra_window_surface(AstraWindow *window, AstraSurface *surface)
{
    (void)window;
    surface->_private_handle = 9u;
    surface->_private_id = 1u;
    surface->_private_width = 320u;
    surface->_private_height = 200u;
    surface->_private_flags = ASTRA_SURFACE_DRAW_TARGET;
    return ASTRA_OK;
}
AstraResult astra_surface_create(const AstraDisplay *display,
                                 const AstraSurfaceCreateInfo *info,
                                 AstraSurface *surface)
{
    (void)display;
    ++surfaces_created;
    created_flags = info->flags;
    created_format = info->format;
    surface->_private_handle = 9u;
    surface->_private_id = 2u;
    surface->_private_width = info->width;
    surface->_private_height = info->height;
    surface->_private_flags = info->flags;
    return ASTRA_OK;
}
AstraResult astra_surface_close(AstraSurface *surface)
{
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    return ASTRA_OK;
}
AstraResult astra_display_staging(AstraDisplay *display,
                                  uint32_t minimum_bytes, void **pixels,
                                  uint32_t *bytes)
{
    (void)display;
    assert(minimum_bytes != 0u && minimum_bytes <= sizeof(staging_pool));
    if (staging_size < minimum_bytes) {
        staging_size = minimum_bytes;
        ++staging_grows;
    }
    *pixels = staging_pool;
    *bytes = staging_size;
    return ASTRA_OK;
}
AstraResult astra_display_stage(AstraDisplay *display, uint32_t offset,
                                const void *pixels, uint32_t pitch,
                                uint32_t row_bytes, uint32_t rows)
{
    (void)display;
    assert(pixels != NULL && rows != 0u &&
           offset + row_bytes * rows <= staging_size);
    ++stages;
    last_stage_offset = offset;
    last_stage_pitch = pitch;
    last_stage_row = row_bytes;
    return ASTRA_OK;
}
AstraResult astra_draw_upload(AstraDrawList *list,
                              const AstraSurface *surface,
                              const AstraRectI32 *rect,
                              uint32_t staging_offset, uint32_t pitch)
{
    assert(list->_private_handle == 11u && surface != NULL);
    ++uploads;
    posts_at_upload = posts;
    last_upload = *rect;
    last_upload_offset = staging_offset;
    last_upload_pitch = pitch;
    return ASTRA_OK;
}
AstraResult astra_draw_list_reset(AstraDrawList *list)
{
    assert(list->_private_handle == 11u);
    ++list_resets;
    return ASTRA_OK;
}
AstraResult astra_draw_list_create(const AstraSurface *destination,
                                   const AstraRectI32 *clip,
                                   AstraDrawList *list)
{
    (void)destination;
    (void)clip;
    ++lists_created;
    list->_private_handle = 11u;
    return ASTRA_OK;
}
AstraResult astra_draw_list_close(AstraDrawList *list)
{
    *list = (AstraDrawList)ASTRA_DRAW_LIST_INIT;
    return ASTRA_OK;
}
AstraResult astra_draw_list_set_target(AstraDrawList *list,
                                       const AstraSurface *destination)
{
    assert(list->_private_handle == 11u);
    last_target = destination;
    ++targets;
    return ASTRA_OK;
}
AstraResult astra_draw_post(AstraDrawList *list, uint32_t flags)
{
    assert(list->_private_handle == 11u);
    last_post_flags = flags;
    ++posts;
    return ASTRA_OK;
}
static void record(Call call)
{
    assert(call_count < 64);
    calls[call_count++] = call;
}
AstraResult astra_draw_list_set_clip(AstraDrawList *list,
                                     const AstraRectI32 *clip)
{
    (void)list;
    record((Call){ .kind = CLIP, .rect = *clip });
    return ASTRA_OK;
}
AstraResult astra_draw_rectangle(AstraDrawList *list,
                                 const AstraRectI32 *rect, int filled,
                                 const AstraDrawPaint *paint)
{
    (void)list;
    assert(filled == 1);
    record((Call){ .kind = FILL, .rect = *rect, .flags = paint->flags,
                   .blend = paint->blend, .color = paint->foreground });
    return ASTRA_OK;
}
/* RECTS: rect is the first rectangle, rects the first eight. */
AstraResult astra_draw_rectangles(AstraDrawList *list,
                                  const AstraRectI32 *rects, uint32_t count,
                                  const AstraDrawPaint *paint)
{
    Call call = { .kind = RECTS, .rect = rects[0], .flags = paint->flags,
                  .blend = paint->blend, .color = paint->foreground,
                  .count = count };

    (void)list;
    assert(count != 0u && paint->size == sizeof(*paint));
    memcpy(call.rects, rects, (count < 8u ? count : 8u) * sizeof(*rects));
    record(call);
    return ASTRA_OK;
}
/* LINES: rect is the first segment, rects the first eight as
   { x0, y0, x1, y1 }. */
AstraResult astra_draw_lines(AstraDrawList *list,
                             const AstraPointI32 *endpoints, uint32_t count,
                             const AstraDrawPaint *paint)
{
    Call call = { .kind = LINES, .flags = paint->flags,
                  .blend = paint->blend, .color = paint->foreground,
                  .count = count };

    (void)list;
    assert(count != 0u && paint->size == sizeof(*paint));
    for (uint32_t index = 0u; index < count && index < 8u; ++index)
        call.rects[index] = (AstraRectI32){
            endpoints[index * 2u].x, endpoints[index * 2u].y,
            (uint32_t)endpoints[index * 2u + 1u].x,
            (uint32_t)endpoints[index * 2u + 1u].y };
    call.rect = call.rects[0];
    record(call);
    return ASTRA_OK;
}
AstraResult astra_draw_line(AstraDrawList *list, AstraPointI32 p0,
                            AstraPointI32 p1, const AstraDrawPaint *paint)
{
    (void)list;
    record((Call){ .kind = LINE,
                   .rect = { p0.x, p0.y, (uint32_t)p1.x, (uint32_t)p1.y },
                   .flags = paint->flags, .blend = paint->blend,
                   .color = paint->foreground });
    return ASTRA_OK;
}
AstraResult astra_draw_blit(AstraDrawList *list, const AstraSurface *source,
                            const AstraRectI32 *from, const AstraRectI32 *to,
                            const AstraBlitOptions *options)
{
    (void)list;
    (void)source;
    record((Call){ .kind = BLIT, .rect = *to, .source = *from,
                   .flags = options->flags, .blend = options->blend,
                   .color = options->modulate });
    return ASTRA_OK;
}
AstraResult astra_draw_triangles(AstraDrawList *list,
                                 const AstraSurface *texture,
                                 const AstraVertex *vertices, uint32_t count,
                                 uint32_t blend, uint32_t flags)
{
    Call call = { .kind = TRIANGLES, .flags = flags, .blend = blend,
                  .texture = texture, .count = count };

    (void)list;
    assert(count % 3u == 0u && count <= 12u);
    memcpy(call.vertices, vertices, count * sizeof(*vertices));
    record(call);
    return ASTRA_OK;
}
AstraResult astra_surface_read(AstraDisplay *display,
                               const AstraSurface *surface,
                               const AstraRectI32 *rect, void *pixels,
                               uint32_t pitch)
{
    (void)display;
    assert(pixels != NULL);
    ++reads;
    posts_at_read = posts;
    last_read = *rect;
    last_read_pitch = pitch;
    last_read_surface = surface;
    return ASTRA_OK;
}
static uint32_t window_event_mask;
static int vblank_waits;
AstraResult astra_window_set_event_mask(AstraWindow *window,
                                        uint32_t event_mask)
{
    (void)window;
    window_event_mask = event_mask;
    return ASTRA_OK;
}
AstraHandle astra_window_vblank_wait_handle(const AstraWindow *window)
{
    (void)window;
    return 77u;
}
uint64_t astra_clock_monotonic(void)
{
    return UINT64_C(1000);
}
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns,
                        uint32_t *detail)
{
    assert(handle == 77u && detail == NULL);
    assert(deadline_ns == UINT64_C(1000) + ASTRA_VSYNC_WAIT_NS);
    ++vblank_waits;
    return 0u;
}

static SDL_RenderCommand commands[16];
static int command_count;

static SDL_RenderCommand *command(SDL_RenderCommandType type)
{
    SDL_RenderCommand *cmd = &commands[command_count];

    memset(cmd, 0, sizeof(*cmd));
    cmd->command = type;
    if (command_count > 0)
        commands[command_count - 1].next = cmd;
    ++command_count;
    return cmd;
}

static void draw(SDL_RenderCommand *cmd, Uint8 r, Uint8 g, Uint8 b, Uint8 a,
                 SDL_BlendMode blend, SDL_Texture *texture)
{
    cmd->data.draw.r = r;
    cmd->data.draw.g = g;
    cmd->data.draw.b = b;
    cmd->data.draw.a = a;
    cmd->data.draw.blend = blend;
    cmd->data.draw.texture = texture;
}

static void run(SDL_Renderer *renderer)
{
    call_count = 0;
    assert(ASTRA_RunCommandQueue(renderer, &commands[0], vertex_pool,
                                 vertex_used) == 0);
    command_count = 0;
    vertex_used = 0u;
}

int main(void)
{
    ASTRA_WindowData window_data = {0};
    SDL_Window window = {0};
    SDL_Renderer renderer = {0};
    SDL_Texture texture = {0};
    SDL_Texture argb_target = {0};
    SDL_RenderCommand *cmd;
    SDL_FPoint points[3];
    SDL_FRect rect;
    SDL_Rect source = { 0, 0, 16, 8 };
    SDL_FPoint center = { 16.0f, 8.0f };
    void *pixels;
    int pitch;

    window.driverdata = &window_data;
    assert(ASTRA_CreateRenderer(&renderer, &window, 0u) == 0);
    assert(hint_value != NULL && strcmp(hint_value, "2") == 0);
    assert(renderer.QueueGeometry == ASTRA_QueueGeometry &&
           renderer.SetTextureScaleMode == ASTRA_SetTextureScaleMode);
    /* A present posts the frame and does not wait for it. SDL's backbuffer
       is undefined after a present: the renderer says so, and the display
       service carries nothing forward. */
    assert(renderer.RenderPresent(&renderer) == 0 && posts == 1 &&
           lists_created == 1 &&
           last_post_flags == (ASTRA_DRAW_POST_PRESENT |
                               ASTRA_DRAW_POST_DISCARD));
    /* Without vsync a present does not wait. */
    assert(vblank_waits == 0 &&
           (renderer.info.flags & SDL_RENDERER_PRESENTVSYNC) == 0u);
    /* The driver offers vsync, so SDL never falls back to software for a
     * program that asks for it; the renderer then subscribes the window to
     * vblank and each present waits for one. */
    assert((ASTRA_RenderDriver.info.flags & SDL_RENDERER_PRESENTVSYNC) != 0u);
    {
        ASTRA_WindowData vsync_data = {0};
        SDL_Window vsync_window = {0};
        SDL_Renderer vsync_renderer = {0};

        vsync_data.event_mask = ASTRA_WINDOW_SUBSCRIBE_KEY;
        vsync_window.driverdata = &vsync_data;
        assert(ASTRA_CreateRenderer(&vsync_renderer, &vsync_window,
                                    SDL_RENDERER_PRESENTVSYNC) == 0);
        assert((vsync_renderer.info.flags & SDL_RENDERER_PRESENTVSYNC) != 0u);
        assert(window_event_mask == (ASTRA_WINDOW_SUBSCRIBE_KEY |
                                     ASTRA_WINDOW_SUBSCRIBE_VBLANK) &&
               vsync_data.event_mask == window_event_mask);
        assert(vsync_renderer.RenderPresent(&vsync_renderer) == 0 &&
               vblank_waits == 1);
        assert(vsync_renderer.SetVSync(&vsync_renderer, 0) == 0);
        assert(vsync_data.event_mask == ASTRA_WINDOW_SUBSCRIBE_KEY &&
               (vsync_renderer.info.flags & SDL_RENDERER_PRESENTVSYNC) == 0u);
        assert(vsync_renderer.RenderPresent(&vsync_renderer) == 0 &&
               vblank_waits == 1);
        vsync_renderer.DestroyRenderer(&vsync_renderer);
    }
    assert(ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_NONE) &&
           ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_BLEND) &&
           ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_ADD) &&
           ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_MOD) &&
           ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_MUL) &&
           !ASTRA_SupportsBlendMode(&renderer, SDL_BLENDMODE_INVALID) &&
           !ASTRA_SupportsBlendMode(&renderer, (SDL_BlendMode)0x05050505));

    /* Viewport and clip combine; a blended fill keeps its alpha. */
    cmd = command(SDL_RENDERCMD_SETVIEWPORT);
    cmd->data.viewport.rect = (SDL_Rect){ 10, 20, 100, 50 };
    cmd = command(SDL_RENDERCMD_SETCLIPRECT);
    cmd->data.cliprect.enabled = SDL_TRUE;
    cmd->data.cliprect.rect = (SDL_Rect){ 5, 5, 200, 20 };
    cmd = command(SDL_RENDERCMD_FILL_RECTS);
    draw(cmd, 1, 2, 3, 128, SDL_BLENDMODE_BLEND, NULL);
    rect = (SDL_FRect){ 0.0f, 0.0f, 4.0f, 4.0f };
    assert(ASTRA_QueueFillRects(&renderer, cmd, &rect, 1) == 0);
    /* A fill far outside the target is culled, not wrapped to 16 bits. */
    cmd = command(SDL_RENDERCMD_FILL_RECTS);
    draw(cmd, 1, 2, 3, 255, SDL_BLENDMODE_NONE, NULL);
    rect = (SDL_FRect){ 90000.0f, 0.0f, 4.0f, 4.0f };
    assert(ASTRA_QueueFillRects(&renderer, cmd, &rect, 1) == 0);
    run(&renderer);
    assert(call_count == 2);
    assert(calls[0].kind == CLIP && calls[0].rect.x == 15 &&
           calls[0].rect.y == 25 && calls[0].rect.width == 95u &&
           calls[0].rect.height == 20u);
    assert(calls[1].kind == RECTS && calls[1].count == 1u &&
           calls[1].rect.x == 10 && calls[1].rect.y == 20 &&
           calls[1].blend == ASTRA_BLEND_ALPHA &&
           calls[1].color.alpha == 128);
    /* A flush appends to the frame, into the current target; nothing is
       sent until something must follow it. */
    assert(posts == 3 && lists_created == 2 && targets == 1 &&
           last_target == &((ASTRA_RenderData *)
                                renderer.driverdata)->content);

    /* Clear ignores viewport and clip. */
    cmd = command(SDL_RENDERCMD_CLEAR);
    cmd->data.color.r = 9;
    cmd->data.color.a = 255;
    run(&renderer);
    assert(call_count == 2 && calls[0].kind == CLIP &&
           calls[0].rect.width == 320u && calls[1].kind == FILL &&
           calls[1].rect.width == 320u && calls[1].rect.height == 200u &&
           calls[1].blend == ASTRA_BLEND_NONE && calls[1].color.red == 9);

    /* A blended line becomes row spans; the shared vertex is drawn once. */
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 5, 6, 7, 100, SDL_BLENDMODE_BLEND, NULL);
    points[0] = (SDL_FPoint){ 0.0f, 0.0f };
    points[1] = (SDL_FPoint){ 5.0f, 0.0f };
    points[2] = (SDL_FPoint){ 7.0f, 2.0f };
    assert(ASTRA_QueueDrawLines(&renderer, cmd, points, 3) == 0);
    run(&renderer);
    /* ... gathered into one list of spans. */
    assert(call_count == 2 && calls[0].kind == CLIP);
    assert(calls[1].kind == RECTS && calls[1].count == 4u &&
           calls[1].rects[0].x == 0 && calls[1].rects[0].width == 5u &&
           calls[1].rects[0].height == 1u);
    assert(calls[1].rects[1].x == 5 && calls[1].rects[1].y == 0 &&
           calls[1].rects[1].width == 1u);
    assert(calls[1].rects[2].x == 6 && calls[1].rects[2].y == 1);
    assert(calls[1].rects[3].x == 7 && calls[1].rects[3].y == 2 &&
           calls[1].blend == ASTRA_BLEND_ALPHA);
    /* An opaque ADD line still blends: it takes the span path too. */
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 5, 6, 7, 255, SDL_BLENDMODE_ADD, NULL);
    assert(ASTRA_QueueDrawLines(&renderer, cmd, points, 3) == 0);
    run(&renderer);
    assert(call_count == 2 && calls[1].kind == RECTS &&
           calls[1].count == 4u && calls[1].blend == ASTRA_BLEND_ADD);
    /* Opaque lines are segments for the line engine: a strip repeats its
       inner point, and consecutive commands in one paint, as testdraw2
       draws them one SDL_RenderDrawLine at a time, share one list. */
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 5, 6, 7, 255, SDL_BLENDMODE_BLEND, NULL);
    assert(ASTRA_QueueDrawLines(&renderer, cmd, points, 3) == 0);
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 5, 6, 7, 255, SDL_BLENDMODE_NONE, NULL);
    assert(ASTRA_QueueDrawLines(&renderer, cmd, &points[1], 2) == 0);
    /* Beyond 16 bits a segment is dropped, not wrapped. */
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 5, 6, 7, 255, SDL_BLENDMODE_NONE, NULL);
    {
        SDL_FPoint far[2] = { { 0.0f, 0.0f }, { 40000.0f, 1.0f } };

        assert(ASTRA_QueueDrawLines(&renderer, cmd, far, 2) == 0);
    }
    run(&renderer);
    assert(call_count == 2 && calls[1].kind == LINES &&
           calls[1].count == 3u && calls[1].blend == ASTRA_BLEND_NONE &&
           calls[1].color.alpha == 255);
    assert(calls[1].rects[0].x == 0 && calls[1].rects[0].y == 0 &&
           calls[1].rects[0].width == 5u && calls[1].rects[0].height == 0u);
    assert(calls[1].rects[1].x == 5 && calls[1].rects[1].y == 0 &&
           calls[1].rects[1].width == 7u && calls[1].rects[1].height == 2u);
    assert(calls[1].rects[2].x == 5 && calls[1].rects[2].width == 7u);

    /* Points and fills in one paint are one list of rectangles across
       SDL commands, as testdraw2 draws them one at a time. */
    cmd = command(SDL_RENDERCMD_SETVIEWPORT);
    cmd->data.viewport.rect = (SDL_Rect){ 10, 20, 100, 50 };
    for (int index = 0; index < 3; ++index) {
        cmd = command(SDL_RENDERCMD_DRAW_POINTS);
        draw(cmd, 1, 2, 3, 255, SDL_BLENDMODE_NONE, NULL);
        points[0] = (SDL_FPoint){ (float)index, 2.0f * (float)index };
        assert(ASTRA_QueueDrawPoints(&renderer, cmd, points, 1) == 0);
    }
    cmd = command(SDL_RENDERCMD_FILL_RECTS);
    draw(cmd, 1, 2, 3, 255, SDL_BLENDMODE_NONE, NULL);
    rect = (SDL_FRect){ -40000.0f, 1.0f, 80000.0f, 2.0f };
    assert(ASTRA_QueueFillRects(&renderer, cmd, &rect, 1) == 0);
    /* Another paint starts another list... */
    cmd = command(SDL_RENDERCMD_DRAW_POINTS);
    draw(cmd, 1, 2, 4, 255, SDL_BLENDMODE_NONE, NULL);
    assert(ASTRA_QueueDrawPoints(&renderer, cmd, points, 1) == 0);
    /* ... and anything else drawn goes after what was gathered. */
    cmd = command(SDL_RENDERCMD_DRAW_LINES);
    draw(cmd, 1, 2, 4, 255, SDL_BLENDMODE_NONE, NULL);
    assert(ASTRA_QueueDrawLines(&renderer, cmd, points, 2) == 0);
    cmd = command(SDL_RENDERCMD_FILL_RECTS);
    draw(cmd, 1, 2, 4, 255, SDL_BLENDMODE_NONE, NULL);
    assert(ASTRA_QueueFillRects(&renderer, cmd, &rect, 1) == 0);
    /* A new clip sends what was gathered under the old one. */
    cmd = command(SDL_RENDERCMD_SETCLIPRECT);
    cmd->data.cliprect.rect = (SDL_Rect){ 0, 0, 10, 10 };
    cmd = command(SDL_RENDERCMD_FILL_RECTS);
    draw(cmd, 1, 2, 4, 255, SDL_BLENDMODE_NONE, NULL);
    assert(ASTRA_QueueFillRects(&renderer, cmd, &rect, 1) == 0);
    run(&renderer);
    assert(call_count == 7 && calls[0].kind == CLIP);
    assert(calls[1].kind == RECTS && calls[1].count == 4u &&
           calls[1].blend == ASTRA_BLEND_NONE && calls[1].color.blue == 3 &&
           calls[1].rects[0].x == 10 && calls[1].rects[0].y == 20 &&
           calls[1].rects[0].width == 1u && calls[1].rects[0].height == 1u &&
           calls[1].rects[2].x == 12 && calls[1].rects[2].y == 24);
    /* Clamped to 16 bits and still reaching across the target. */
    assert(calls[1].rects[3].x == INT16_MIN && calls[1].rects[3].y == 21 &&
           calls[1].rects[3].width == 65535u &&
           calls[1].rects[3].height == 2u);
    assert(calls[2].kind == RECTS && calls[2].count == 1u &&
           calls[2].color.blue == 4 && calls[3].kind == LINES &&
           calls[3].count == 1u &&
           calls[4].kind == RECTS && calls[4].count == 1u &&
           calls[5].kind == CLIP && calls[6].kind == RECTS &&
           calls[6].count == 1u);
    /* A long list goes out ASTRA_GATHER_MAX rectangles at a time. */
    {
        enum { POINTS = ASTRA_GATHER_MAX + 5u };
        static SDL_FPoint many[POINTS];

        for (Uint32 index = 0u; index < POINTS; ++index)
            many[index] = (SDL_FPoint){ (float)(index % 300u), 7.0f };
        cmd = command(SDL_RENDERCMD_DRAW_POINTS);
        draw(cmd, 1, 2, 3, 255, SDL_BLENDMODE_BLEND, NULL);
        assert(ASTRA_QueueDrawPoints(&renderer, cmd, many, POINTS) == 0);
        run(&renderer);
        assert(call_count == 3 && calls[1].kind == RECTS &&
               calls[1].count == ASTRA_GATHER_MAX &&
               calls[1].blend == ASTRA_BLEND_ALPHA &&
               calls[2].kind == RECTS && calls[2].count == 5u &&
               calls[2].rect.x == (int32_t)(ASTRA_GATHER_MAX % 300u));
    }

    /* Textures: formats map directly; targets are readable. */
    texture.format = SDL_PIXELFORMAT_ARGB8888;
    texture.access = SDL_TEXTUREACCESS_STREAMING;
    texture.w = 32;
    texture.h = 16;
    assert(ASTRA_CreateTexture(&renderer, &texture) == 0);
    assert(created_format == ASTRA_PIXEL_FORMAT_ARGB8888 &&
           created_flags == (ASTRA_SURFACE_DRAW_SOURCE |
                             ASTRA_SURFACE_CPU_WRITE));
    argb_target.format = SDL_PIXELFORMAT_ARGB8888;
    argb_target.access = SDL_TEXTUREACCESS_TARGET;
    argb_target.w = argb_target.h = 8;
    assert(ASTRA_CreateTexture(&renderer, &argb_target) == 0 &&
           surfaces_created == 2 && created_format ==
               ASTRA_PIXEL_FORMAT_ARGB8888 &&
           created_flags == (ASTRA_SURFACE_DRAW_SOURCE |
                             ASTRA_SURFACE_CPU_WRITE |
                             ASTRA_SURFACE_DRAW_TARGET |
                             ASTRA_SURFACE_CPU_READ));
    /* A lock is rows of staging; unlock appends their upload to the frame,
       after what it drew so far, and posts nothing. */
    {
        int before = posts;
        int resets = list_resets;

        assert(ASTRA_LockTexture(&renderer, &texture,
                                 &(SDL_Rect){ 4, 2, 8, 3 }, &pixels,
                                 &pitch) == 0 &&
               pitch == 32 && pixels == staging_pool);
        ASTRA_UnlockTexture(&renderer, &texture);
        assert(posts == before && uploads == 1 && last_upload.x == 4 &&
               last_upload.y == 2 && last_upload.width == 8u &&
               last_upload.height == 3u && last_upload_offset == 0u &&
               last_upload_pitch == 32u);
        /* The next upload of the frame lies beside it; caller rows reach
           staging through the copy engine, packed. */
        assert(ASTRA_UpdateTexture(&renderer, &texture,
                                   &(SDL_Rect){ 0, 0, 2, 2 },
                                   vertex_pool, 64) == 0);
        assert(uploads == 2 && stages == 1 && last_stage_offset == 96u &&
               last_stage_pitch == 64u && last_stage_row == 8u &&
               last_upload_offset == 96u && last_upload_pitch == 8u &&
               posts == before && list_resets == resets);
        /* A present hands the frame over. The next frame's uploads start
           staging over once the list is back, which they wait for: its
           uploads' rows were the service's until then. */
        assert(renderer.RenderPresent(&renderer) == 0 &&
               posts == before + 1);
        assert(ASTRA_LockTexture(&renderer, &texture,
                                 &(SDL_Rect){ 0, 0, 1, 1 }, &pixels,
                                 &pitch) == 0 &&
               pixels == staging_pool && list_resets == resets + 1);
        ASTRA_UnlockTexture(&renderer, &texture);
        /* An upload that does not fit sends the frame first, and staging
           grows while nothing uses it. */
        staging_size = 1024u;
        assert(ASTRA_LockTexture(&renderer, &texture,
                                 &(SDL_Rect){ 0, 0, 32, 16 }, &pixels,
                                 &pitch) == 0 &&
               pixels == staging_pool && pitch == 128 &&
               posts == before + 2 && list_resets == resets + 2 &&
               staging_grows == 1 && staging_size == 2048u);
        /* No readback while a lock holds rows of staging. */
        assert(ASTRA_RenderReadPixels(&renderer, &(SDL_Rect){ 0, 0, 1, 1 },
                                      SDL_PIXELFORMAT_RGB565, vertex_pool,
                                      2) < 0 && errors == 1);
        errors = 0;
        /* A second lock that does not fit beside an open one cannot move
           staging: it is a private copy, uploaded from at unlock. */
        {
            SDL_Texture second = texture;
            void *second_pixels;
            int second_pitch;

            second.driverdata = NULL;
            assert(ASTRA_CreateTexture(&renderer, &second) == 0);
            assert(ASTRA_LockTexture(&renderer, &second,
                                     &(SDL_Rect){ 0, 0, 32, 16 },
                                     &second_pixels, &second_pitch) == 0 &&
                   second_pitch == 128 &&
                   ((uint8_t *)second_pixels < staging_pool ||
                    (uint8_t *)second_pixels >=
                        staging_pool + sizeof(staging_pool)));
            ASTRA_UnlockTexture(&renderer, &texture);
            assert(uploads == 4 && last_upload_offset == 0u &&
                   last_upload_pitch == 128u && posts == before + 2);
            /* With nothing locked the frame goes first and staging starts
               over for the private copy's rows. */
            ASTRA_UnlockTexture(&renderer, &second);
            assert(uploads == 5 && stages == 2 && posts == before + 3 &&
                   list_resets == resets + 3 && last_upload_offset == 0u &&
                   last_upload_pitch == 128u && last_stage_pitch == 128u);
            ASTRA_DestroyTexture(&renderer, &second);
        }
    }

    /* Copy: blend and alpha modulation reach the blit. */
    cmd = command(SDL_RENDERCMD_COPY);
    draw(cmd, 255, 255, 255, 200, SDL_BLENDMODE_BLEND, &texture);
    rect = (SDL_FRect){ 40.0f, 30.0f, 32.0f, 16.0f };
    assert(ASTRA_QueueCopy(&renderer, cmd, &texture, &source, &rect) == 0);
    /* A half turn about the center is both mirrors in place. */
    cmd = command(SDL_RENDERCMD_COPY_EX);
    draw(cmd, 255, 255, 255, 255, SDL_BLENDMODE_NONE, &texture);
    assert(ASTRA_QueueCopyEx(&renderer, cmd, &texture, &source, &rect,
                             -180.0, &center, SDL_FLIP_HORIZONTAL, 1.0f,
                             1.0f) == 0);
    run(&renderer);
    assert(call_count == 3 && calls[1].kind == BLIT &&
           calls[1].rect.x == 40 && calls[1].rect.width == 32u &&
           calls[1].source.width == 16u && calls[1].flags == 0u &&
           calls[1].blend == ASTRA_BLEND_ALPHA &&
           calls[1].color.alpha == 200);
    assert(calls[2].kind == BLIT && calls[2].rect.x == 40 &&
           calls[2].rect.y == 30 && calls[2].flags == ASTRA_BLIT_FLIP_Y &&
           calls[2].blend == ASTRA_BLEND_NONE);

    /* Color modulation, MOD, and linear scaling reach the blit. */
    texture.scaleMode = SDL_ScaleModeLinear;
    cmd = command(SDL_RENDERCMD_COPY);
    draw(cmd, 128, 64, 32, 255, SDL_BLENDMODE_MOD, &texture);
    assert(ASTRA_QueueCopy(&renderer, cmd, &texture, &source, &rect) == 0);
    texture.scaleMode = SDL_ScaleModeNearest;
    run(&renderer);
    assert(call_count == 2 && calls[1].kind == BLIT &&
           calls[1].flags == ASTRA_BLIT_FILTER_LINEAR &&
           calls[1].blend == ASTRA_BLEND_MODULATE &&
           calls[1].color.red == 128 && calls[1].color.green == 64 &&
           calls[1].color.blue == 32);

    /* Any other angle is two triangles turned clockwise about the center,
       the mirror applied to the texels, then moved by the viewport. */
    cmd = command(SDL_RENDERCMD_SETVIEWPORT);
    cmd->data.viewport.rect = (SDL_Rect){ 3, 4, 320, 200 };
    cmd = command(SDL_RENDERCMD_COPY_EX);
    draw(cmd, 10, 20, 30, 40, SDL_BLENDMODE_ADD, &texture);
    assert(ASTRA_QueueCopyEx(&renderer, cmd, &texture, &source, &rect,
                             -270.0, &center, SDL_FLIP_HORIZONTAL, 1.0f,
                             1.0f) == 0);
    run(&renderer);
    assert(call_count == 2 && calls[1].kind == TRIANGLES &&
           calls[1].count == 6u && calls[1].blend == ASTRA_BLEND_ADD &&
           calls[1].flags == 0u);
    {
        /* Corners TL, TR, BR, BL of (40,30)-(72,46) about (56,38). */
        const Sint32 x[4] = { 64, 64, 48, 48 };
        const Sint32 y[4] = { 22, 54, 54, 22 };
        const Sint32 u[4] = { 16, 0, 0, 16 };
        const Sint32 v[4] = { 0, 0, 8, 8 };
        const int order[6] = { 0, 1, 3, 1, 2, 3 };

        for (int index = 0; index < 6; ++index) {
            const AstraVertex *vertex = &calls[1].vertices[index];
            int corner = order[index];

            assert(vertex->x == (x[corner] + 3) * 256 &&
                   vertex->y == (y[corner] + 4) * 256 &&
                   vertex->u == u[corner] * 65536 &&
                   vertex->v == v[corner] * 65536 &&
                   vertex->color.red == 10 && vertex->color.alpha == 40);
        }
    }

    /* Geometry honors indices, per-vertex colors, texel scale, and render
       scale; a triangle wholly off the target is culled. */
    {
        static const float xy[5][2] = {
            { 0.0f, 0.0f }, { 10.0f, 0.0f }, { 0.0f, 10.0f },
            { 10.0f, 10.0f }, { 5000.0f, 5000.0f } };
        static const float uv[5][2] = {
            { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.0f, 1.0f },
            { 0.5f, 1.0f }, { 1.0f, 1.0f } };
        static const SDL_Color colors[5] = {
            { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 },
            { 13, 14, 15, 16 }, { 17, 18, 19, 20 } };
        static const Uint16 indices[9] = { 0, 1, 2, 2, 1, 3, 4, 4, 4 };

        cmd = command(SDL_RENDERCMD_GEOMETRY);
        draw(cmd, 0, 0, 0, 0, SDL_BLENDMODE_BLEND, &texture);
        texture.scaleMode = SDL_ScaleModeLinear;
        assert(ASTRA_QueueGeometry(&renderer, cmd, &texture, &xy[0][0],
                                   8, colors, 4, &uv[0][0], 8, 5, indices,
                                   9, 2, 2.0f, 2.0f) == 0);
        texture.scaleMode = SDL_ScaleModeNearest;
        cmd = command(SDL_RENDERCMD_GEOMETRY);
        draw(cmd, 0, 0, 0, 0, SDL_BLENDMODE_MUL, NULL);
        assert(ASTRA_QueueGeometry(&renderer, cmd, NULL, &xy[0][0], 8,
                                   colors, 4, NULL, 0, 3, NULL, 0, 0, 1.0f,
                                   1.0f) == 0);
        run(&renderer);
        assert(call_count == 3 && calls[1].kind == TRIANGLES &&
               calls[1].count == 6u &&
               calls[1].texture == &((ASTRA_TextureData *)
                                         texture.driverdata)->surface &&
               calls[1].blend == ASTRA_BLEND_ALPHA &&
               calls[1].flags == ASTRA_BLIT_FILTER_LINEAR);
        /* Index 3 is the sixth vertex: (20, 20), texel (16, 16). */
        assert(calls[1].vertices[5].x == 20 * 256 &&
               calls[1].vertices[5].y == 20 * 256 &&
               calls[1].vertices[5].u == 16 * 65536 &&
               calls[1].vertices[5].v == 16 * 65536 &&
               calls[1].vertices[5].color.red == 13 &&
               calls[1].vertices[5].color.alpha == 16);
        assert(calls[1].vertices[1].u == 16 * 65536 &&
               calls[1].vertices[2].v == 16 * 65536);
        assert(calls[2].kind == TRIANGLES && calls[2].texture == NULL &&
               calls[2].count == 3u && calls[2].blend == ASTRA_BLEND_MULTIPLY &&
               calls[2].vertices[1].x == 10 * 256 &&
               calls[2].vertices[1].u == 0 && calls[2].vertices[2].v == 0);
        /* A visible vertex past the engine's range fails the queue. */
        cmd = command(SDL_RENDERCMD_GEOMETRY);
        draw(cmd, 0, 0, 0, 0, SDL_BLENDMODE_NONE, NULL);
        {
            static const float far[3][2] = {
                { 0.0f, 0.0f }, { 40000.0f, 0.0f }, { 0.0f, 10.0f } };

            assert(ASTRA_QueueGeometry(&renderer, cmd, NULL, &far[0][0], 8,
                                       colors, 4, NULL, 0, 3, NULL, 0, 0,
                                       1.0f, 1.0f) == 0);
        }
        call_count = 0;
        assert(ASTRA_RunCommandQueue(&renderer, &commands[0], vertex_pool,
                                     vertex_used) < 0);
        command_count = 0;
        vertex_used = 0u;
    }

    /* Readback: the target's own format goes straight to the caller;
       another format is converted by SDL from a staged copy. */
    assert(ASTRA_RenderReadPixels(&renderer, &(SDL_Rect){ 1, 2, 3, 4 },
                                  SDL_PIXELFORMAT_RGB565, pixels, 99) == 0);
    assert(reads == 1 && posts_at_read == posts &&
           ((ASTRA_RenderData *)renderer.driverdata)->list_pending == 0 &&
           last_read_pitch == 99u && last_read.x == 1 &&
           last_read.height == 4u &&
           last_read_surface == &((ASTRA_RenderData *)
                                      renderer.driverdata)->content);
    assert(ASTRA_RenderReadPixels(&renderer, &(SDL_Rect){ 1, 2, 3, 4 },
                                  SDL_PIXELFORMAT_ABGR8888, pixels, 99) ==
           0);
    assert(reads == 2 && last_read_pitch == 6u &&
           converted_from == SDL_PIXELFORMAT_RGB565 &&
           converted_to == SDL_PIXELFORMAT_ABGR8888 && converted_pitch == 6);
    renderer.target = &argb_target;
    converted_from = 0u;
    assert(ASTRA_RenderReadPixels(&renderer, &(SDL_Rect){ 0, 0, 2, 2 },
                                  SDL_PIXELFORMAT_ARGB8888, pixels, 8) == 0);
    assert(reads == 3 && converted_from == 0u && last_read_pitch == 8u &&
           last_read_surface == &((ASTRA_TextureData *)
                                      argb_target.driverdata)->surface);
    renderer.target = NULL;

    ASTRA_DestroyTexture(&renderer, &texture);
    ASTRA_DestroyTexture(&renderer, &argb_target);
    ASTRA_DestroyRenderer(&renderer);
    assert(renderer.driverdata == NULL && errors == 1);
    return 0;
}
