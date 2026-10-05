#include "../../SDL_internal.h"

#if SDL_VIDEO_RENDER_ASTRA

/*
 * SDL renderer over Astra's managed graphics API: textures are GPU surfaces
 * in the graphics arena and every draw becomes an ADLT command executed by
 * the FPGA: fills, lines and plain copies by the blitter, and rotation,
 * geometry, color modulation, ADD/MOD/MUL and linear filtering by the
 * texture engine (docs/TEXTURE_ENGINE.md). Nothing here touches pixels on
 * the MC68040; whatever the hardware cannot do fails with an error.
 */

#include "SDL_hints.h"
#include "../SDL_sysrender.h"
#include "../../video/SDL_sysvideo.h"
#include "../../video/astra/SDL_astravideo.h"

#include <astra/runtime.h>

/* A vsync present waits at most this long for a vblank: a display that
 * stops signalling slows the program down, it does not hang it. */
#define ASTRA_VSYNC_WAIT_NS UINT64_C(100000000)

typedef struct ASTRA_TextureData {
    AstraSurface surface;
    SDL_Rect locked;
    uint32_t lock_pitch;
    /* The lock is rows of the display's staging area at lock_offset that
       unlock hands to the frame as an upload. */
    int lock_staged;
    uint32_t lock_offset;
    /* Otherwise a private copy, uploaded from at unlock: a lock made while
       another is open and staging is full cannot move staging under it. */
    uint8_t *lock_pixels;
} ASTRA_TextureData;

/* Rectangles or segments gathered into one astra_draw_rectangles or
   astra_draw_lines call. */
#define ASTRA_GATHER_MAX 4096u

/*
 * A frame is one draw list. Each flush appends to it, marking where the
 * target changes, and so does each texture upload; present posts it -- one
 * submission the game does not wait for, as SDL's Haiku backend posts its
 * frame to the window thread -- and anything the service must see in
 * order (a read, a destroyed texture) posts what is pending first.
 *
 * An upload's rows lie in the display's staging area, where a streaming
 * lock hands them to the game to write (a BBitmap the game draws into,
 * SDL_bframebuffer.cc). The frame's uploads lie there side by side from
 * staging_used down; the area is reused once the service has read the
 * frame, which is the list coming back.
 */
typedef struct ASTRA_RenderData {
    ASTRA_WindowData *window;
    AstraSurface content;
    AstraDrawList list;
    /* Commands appended since the list was last posted. */
    int list_pending;
    /* The list was posted and has not come back: its uploads' rows are
       still the service's. */
    int list_posted;
    /* Staging bytes the frame's uploads and the open locks hold. */
    uint32_t staging_used;
    /* Textures locked into staging and not yet unlocked. */
    int staged_locks;
    /* ASTRA_GATHER_MAX of each, allocated on first use. */
    AstraRectI32 *rects;
    AstraPointI32 *segments; /* two endpoints per segment */
    /* Present waits for the display's next vblank. */
    int vsync;
} ASTRA_RenderData;

/*
 * A queued copy: a blit of source to destination, or, when vertices is
 * nonzero, that many AstraVertex (two triangles, viewport-relative) after
 * the record. Geometry queues the same record with its own vertices.
 */
typedef struct ASTRA_CopyVertices {
    SDL_Rect source;
    SDL_Rect destination;
    Uint32 flags; /* ASTRA_BLIT_FILTER_LINEAR */
    Uint32 vertices;
} ASTRA_CopyVertices;

/* Vertex x/y must stay inside the texture engine's +-32768 pixels. */
#define ASTRA_VERTEX_LIMIT (1 << 23)

static void ASTRA_Ignore(AstraResult result)
{
    /* Teardown and best-effort paths report nothing further. */
    (void)result;
}

static SDL_bool ASTRA_SupportsBlendMode(SDL_Renderer *renderer,
                                        SDL_BlendMode blend)
{
    (void)renderer;
    /* The five SDL modes are the hardware's; composed modes are not. */
    return blend == SDL_BLENDMODE_NONE || blend == SDL_BLENDMODE_BLEND ||
           blend == SDL_BLENDMODE_ADD || blend == SDL_BLENDMODE_MOD ||
           blend == SDL_BLENDMODE_MUL;
}

static uint32_t ASTRA_Blend(SDL_BlendMode blend)
{
    switch (blend) {
    case SDL_BLENDMODE_BLEND: return ASTRA_BLEND_ALPHA;
    case SDL_BLENDMODE_ADD: return ASTRA_BLEND_ADD;
    case SDL_BLENDMODE_MOD: return ASTRA_BLEND_MODULATE;
    case SDL_BLENDMODE_MUL: return ASTRA_BLEND_MULTIPLY;
    default: return ASTRA_BLEND_NONE;
    }
}

static uint32_t ASTRA_Filter(const SDL_Texture *texture)
{
    return texture->scaleMode == SDL_ScaleModeNearest ?
           0u : (uint32_t)ASTRA_BLIT_FILTER_LINEAR;
}

/* Rounded fixed point, clamped far outside every valid coordinate so the
   range checks below see an out-of-range value, not a wrapped one. */
static Sint32 ASTRA_Fixed(double value, double unit)
{
    double fixed = SDL_floor(value * unit + 0.5);

    if (fixed < -1073741824.0)
        return -1073741824;
    if (fixed > 1073741823.0)
        return 1073741823;
    return (Sint32)fixed;
}

static uint16_t ASTRA_PixelFormat(Uint32 format)
{
    switch (format) {
    case SDL_PIXELFORMAT_ARGB8888: return ASTRA_PIXEL_FORMAT_ARGB8888;
    case SDL_PIXELFORMAT_RGB888: return ASTRA_PIXEL_FORMAT_XRGB8888;
    case SDL_PIXELFORMAT_RGB565: return ASTRA_PIXEL_FORMAT_RGB565;
    default: return 0u;
    }
}

static int ASTRA_Failed(const char *what, AstraResult result)
{
    return SDL_SetError("Astra renderer: %s failed (%d)", what, result);
}

/* Hands the service whatever the frame has drawn so far, so a request
   made outside the list happens after it. */
static int ASTRA_PostPending(ASTRA_RenderData *data)
{
    AstraResult result;

    if (data->list_pending == 0)
        return 0;
    data->list_pending = 0;
    result = astra_draw_post(&data->list, 0u);
    if (result != ASTRA_OK)
        return ASTRA_Failed("post", result);
    data->list_posted = 1;
    return 0;
}

/* Staging is free again once the service has read every posted frame:
   the list is back. A change since the post took it back already; an
   untouched list is taken back here, waiting if the service is behind.
   Open locks keep their rows. */
static int ASTRA_StagingRecycle(ASTRA_RenderData *data)
{
    AstraResult result;

    if (data->list_posted && data->list_pending == 0 &&
        data->list._private_handle != ASTRA_INVALID_HANDLE) {
        result = astra_draw_list_reset(&data->list);
        if (result != ASTRA_OK)
            return ASTRA_Failed("frame list", result);
    }
    data->list_posted = 0;
    if (data->staged_locks == 0)
        data->staging_used = 0u;
    return 0;
}

/*
 * @p bytes of staging for one upload of the frame, at *offset, mapped at
 * *pixels. Returns 1 when they do not fit while a lock is open: the rows a
 * lock holds cannot move, so the caller does without staging.
 */
static int ASTRA_StagingReserve(ASTRA_RenderData *data, uint32_t bytes,
                                uint32_t *offset, uint8_t **pixels)
{
    AstraDisplay *display = &data->window->display;
    uint32_t need = (bytes + 3u) & ~3u;
    void *base = NULL;
    uint32_t size = 0u;
    AstraResult result;

    if (data->list_posted && ASTRA_StagingRecycle(data) < 0)
        return -1;
    result = astra_display_staging(display, 1u, &base, &size);
    if (result != ASTRA_OK)
        return ASTRA_Failed("staging", result);
    if (need > size - data->staging_used) {
        if (data->staged_locks != 0)
            return 1;
        /* The frame so far goes first; staging then starts over, and
           grows only while nothing uses it. */
        if (ASTRA_PostPending(data) < 0 || ASTRA_StagingRecycle(data) < 0)
            return -1;
        result = astra_display_staging(display, need, &base, &size);
        if (result != ASTRA_OK)
            return ASTRA_Failed("staging", result);
    }
    *offset = data->staging_used;
    *pixels = (uint8_t *)base + data->staging_used;
    data->staging_used += need;
    return 0;
}

static void ASTRA_DropWindowList(ASTRA_RenderData *data)
{
    if (data->list._private_handle != ASTRA_INVALID_HANDLE) {
        (void)ASTRA_PostPending(data);
        ASTRA_Ignore(astra_draw_list_close(&data->list));
    }
    if (data->content._private_handle != ASTRA_INVALID_HANDLE)
        ASTRA_Ignore(astra_surface_close(&data->content));
    data->list_pending = 0;
    /* Closing the list is a call the service answers after reading what
       was posted before it. */
    data->list_posted = 0;
    if (data->staged_locks == 0)
        data->staging_used = 0u;
}

/* The window content surface, borrowed on first use. */
static AstraResult ASTRA_Content(ASTRA_RenderData *data)
{
    if (data->content._private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_OK;
    return astra_window_surface(&data->window->native, &data->content);
}

/* The frame list, created on first use. */
static AstraResult ASTRA_FrameList(ASTRA_RenderData *data)
{
    AstraResult result = ASTRA_Content(data);
    AstraRectI32 whole;

    if (result != ASTRA_OK ||
        data->list._private_handle != ASTRA_INVALID_HANDLE)
        return result;
    whole = (AstraRectI32){ 0, 0, data->content._private_width,
                            data->content._private_height };
    return astra_draw_list_create(&data->content, &whole, &data->list);
}

/* The frame list, drawing next into the current target. */
static AstraDrawList *ASTRA_TargetList(SDL_Renderer *renderer,
                                       uint16_t *width, uint16_t *height)
{
    ASTRA_RenderData *data = renderer->driverdata;
    SDL_Texture *target = renderer->target;
    const AstraSurface *surface;
    AstraResult result = ASTRA_FrameList(data);

    if (result != ASTRA_OK) {
        ASTRA_DropWindowList(data);
        ASTRA_Failed("frame list", result);
        return NULL;
    }
    surface = target != NULL ?
        &((ASTRA_TextureData *)target->driverdata)->surface : &data->content;
    result = astra_draw_list_set_target(&data->list, surface);
    if (result != ASTRA_OK) {
        ASTRA_Failed("render target", result);
        return NULL;
    }
    *width = surface->_private_width;
    *height = surface->_private_height;
    return &data->list;
}

static void ASTRA_WindowEvent(SDL_Renderer *renderer,
                              const SDL_WindowEvent *event)
{
    if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED)
        /* The content surface and every list built for it changed size. */
        ASTRA_DropWindowList(renderer->driverdata);
}

static int ASTRA_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    SDL_Texture *target = renderer->target;

    if (target != NULL) {
        *w = target->w;
        *h = target->h;
    } else {
        SDL_GetWindowSize(renderer->window, w, h);
    }
    return 0;
}

static int ASTRA_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    ASTRA_RenderData *data = renderer->driverdata;
    ASTRA_TextureData *texture_data;
    AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraResult result;

    info.width = (uint16_t)texture->w;
    info.height = (uint16_t)texture->h;
    info.format = ASTRA_PixelFormat(texture->format);
    info.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    /* A target can be read back with SDL_RenderReadPixels. */
    if (texture->access == SDL_TEXTUREACCESS_TARGET)
        info.flags |= ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_CPU_READ;
    if (info.format == 0u)
        return SDL_SetError("Astra renderer: unsupported texture format %s",
                            SDL_GetPixelFormatName(texture->format));
    texture_data = SDL_calloc(1u, sizeof(*texture_data));
    if (texture_data == NULL)
        return SDL_OutOfMemory();
    texture_data->surface = (AstraSurface)ASTRA_SURFACE_INIT;
    result = astra_surface_create(&data->window->display, &info,
                                  &texture_data->surface);
    if (result != ASTRA_OK) {
        SDL_free(texture_data);
        return ASTRA_Failed("texture creation", result);
    }
    texture->driverdata = texture_data;
    return 0;
}

/* The frame uploads @p rect of the texture from staging rows at @p offset;
   draws already in the frame read the texture as it was. */
static int ASTRA_QueueUpload(ASTRA_RenderData *data,
                             ASTRA_TextureData *texture_data,
                             const SDL_Rect *rect, uint32_t offset,
                             uint32_t pitch)
{
    AstraRectI32 target = { rect->x, rect->y, (uint32_t)rect->w,
                            (uint32_t)rect->h };
    AstraResult result = ASTRA_FrameList(data);

    if (result == ASTRA_OK)
        result = astra_draw_upload(&data->list, &texture_data->surface,
                                   &target, offset, pitch);
    if (result != ASTRA_OK)
        return ASTRA_Failed("texture upload", result);
    data->list_pending = 1;
    return 0;
}

static int ASTRA_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_Rect *rect, const void *pixels,
                               int pitch)
{
    ASTRA_RenderData *data = renderer->driverdata;
    ASTRA_TextureData *texture_data = texture->driverdata;
    uint32_t row = (uint32_t)rect->w *
                   (uint32_t)SDL_BYTESPERPIXEL(texture->format);
    uint32_t offset = 0u;
    uint8_t *staged = NULL;
    AstraResult result;
    int status;

    if (rect->w <= 0 || rect->h <= 0)
        return 0;
    status = ASTRA_StagingReserve(data, row * (uint32_t)rect->h, &offset,
                                  &staged);
    if (status > 0)
        return SDL_SetError("Astra renderer: staging is full while a "
                            "texture is locked");
    if (status < 0)
        return -1;
    result = astra_display_stage(&data->window->display, offset, pixels,
                                 (uint32_t)pitch, row, (uint32_t)rect->h);
    if (result != ASTRA_OK)
        return ASTRA_Failed("texture upload", result);
    return ASTRA_QueueUpload(data, texture_data, rect, offset, row);
}

/* A lock is written straight into staging, where the device reads it: no
   copy between the game and the device but its own. */
static int ASTRA_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, void **pixels, int *pitch)
{
    ASTRA_RenderData *data = renderer->driverdata;
    ASTRA_TextureData *texture_data = texture->driverdata;
    uint32_t bytes_per_pixel = (uint32_t)SDL_BYTESPERPIXEL(texture->format);
    uint8_t *staged = NULL;
    int status;

    texture_data->locked = *rect;
    texture_data->lock_pitch = (uint32_t)rect->w * bytes_per_pixel;
    status = ASTRA_StagingReserve(data,
                                  texture_data->lock_pitch * (uint32_t)rect->h,
                                  &texture_data->lock_offset, &staged);
    if (status < 0)
        return -1;
    if (status == 0) {
        texture_data->lock_staged = 1;
        ++data->staged_locks;
        *pixels = staged;
        *pitch = (int)texture_data->lock_pitch;
        return 0;
    }
    texture_data->lock_pitch = (uint32_t)texture->w * bytes_per_pixel;
    if (texture_data->lock_pixels == NULL) {
        texture_data->lock_pixels = SDL_malloc(
            (size_t)texture_data->lock_pitch * (size_t)texture->h);
        if (texture_data->lock_pixels == NULL)
            return SDL_OutOfMemory();
    }
    *pixels = texture_data->lock_pixels +
              (size_t)rect->y * texture_data->lock_pitch +
              (size_t)rect->x * bytes_per_pixel;
    *pitch = (int)texture_data->lock_pitch;
    return 0;
}

static void ASTRA_UnlockTexture(SDL_Renderer *renderer,
                                SDL_Texture *texture)
{
    ASTRA_RenderData *data = renderer->driverdata;
    ASTRA_TextureData *texture_data = texture->driverdata;
    const SDL_Rect *rect = &texture_data->locked;

    if (texture_data->lock_staged) {
        texture_data->lock_staged = 0;
        --data->staged_locks;
        if (rect->w > 0 && rect->h > 0)
            (void)ASTRA_QueueUpload(data, texture_data, rect,
                                    texture_data->lock_offset,
                                    texture_data->lock_pitch);
        return;
    }
    (void)ASTRA_UpdateTexture(
        renderer, texture, rect,
        texture_data->lock_pixels +
            (size_t)rect->y * texture_data->lock_pitch +
            (size_t)rect->x * (size_t)SDL_BYTESPERPIXEL(texture->format),
        (int)texture_data->lock_pitch);
}

static void ASTRA_SetTextureScaleMode(SDL_Renderer *renderer,
                                      SDL_Texture *texture,
                                      SDL_ScaleMode mode)
{
    /* SDL calls this unconditionally; each copy reads texture->scaleMode
       when it is queued. */
    (void)renderer;
    (void)texture;
    (void)mode;
}

static int ASTRA_SetRenderTarget(SDL_Renderer *renderer,
                                 SDL_Texture *texture)
{
    (void)renderer;
    if (texture != NULL && texture->access != SDL_TEXTUREACCESS_TARGET)
        return SDL_SetError("Astra renderer: texture is not a target");
    return 0;
}

static int ASTRA_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return 0;
}

/* Points are one-pixel rectangles, queued as FILL_RECTS queues them. */
static int ASTRA_QueueDrawPoints(SDL_Renderer *renderer,
                                 SDL_RenderCommand *cmd,
                                 const SDL_FPoint *points, int count)
{
    AstraRectI32 *vertices = SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(AstraRectI32), 0,
        &cmd->data.draw.first);

    if (vertices == NULL)
        return -1;
    cmd->data.draw.count = (size_t)count;
    for (int index = 0; index < count; ++index)
        vertices[index] = (AstraRectI32){ (int32_t)points[index].x,
                                          (int32_t)points[index].y, 1u, 1u };
    return 0;
}

static int ASTRA_QueueDrawLines(SDL_Renderer *renderer,
                                SDL_RenderCommand *cmd,
                                const SDL_FPoint *points, int count)
{
    SDL_Point *vertices = SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(SDL_Point), 0,
        &cmd->data.draw.first);

    if (vertices == NULL)
        return -1;
    cmd->data.draw.count = (size_t)count;
    for (int index = 0; index < count; ++index) {
        vertices[index].x = (int)points[index].x;
        vertices[index].y = (int)points[index].y;
    }
    return 0;
}

static int ASTRA_QueueFillRects(SDL_Renderer *renderer,
                                SDL_RenderCommand *cmd,
                                const SDL_FRect *rects, int count)
{
    AstraRectI32 *vertices = SDL_AllocateRenderVertices(
        renderer, (size_t)count * sizeof(AstraRectI32), 0,
        &cmd->data.draw.first);

    if (vertices == NULL)
        return -1;
    cmd->data.draw.count = (size_t)count;
    for (int index = 0; index < count; ++index)
        vertices[index] = (AstraRectI32){
            (int32_t)rects[index].x, (int32_t)rects[index].y,
            (uint32_t)SDL_max((int)rects[index].w, 1),
            (uint32_t)SDL_max((int)rects[index].h, 1) };
    return 0;
}

static int ASTRA_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                           SDL_Texture *texture, const SDL_Rect *srcrect,
                           const SDL_FRect *dstrect)
{
    ASTRA_CopyVertices *vertices;

    vertices = SDL_AllocateRenderVertices(renderer, sizeof(*vertices), 0,
                                          &cmd->data.draw.first);
    if (vertices == NULL)
        return -1;
    cmd->data.draw.count = 1u;
    vertices->flags = ASTRA_Filter(texture);
    vertices->vertices = 0u;
    vertices->source = *srcrect;
    vertices->destination.x = (int)dstrect->x;
    vertices->destination.y = (int)dstrect->y;
    vertices->destination.w = (int)dstrect->w;
    vertices->destination.h = (int)dstrect->h;
    return 0;
}

/* A copy at any other angle is two textured triangles around @p center,
   mirrors applied to the texel coordinates. */
static int ASTRA_QueueRotated(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                              SDL_Texture *texture, const SDL_Rect *srcquad,
                              const SDL_FRect *dstrect, double turn,
                              const SDL_FPoint *center,
                              SDL_RendererFlip flip, float scale_x,
                              float scale_y)
{
    static const Uint8 corners[6] = { 0, 1, 3, 1, 2, 3 };
    ASTRA_CopyVertices *record;
    AstraVertex *out;
    double radians = turn * (M_PI / 180.0);
    double c = SDL_cos(radians);
    double s = SDL_sin(radians);
    double center_x = (double)dstrect->x + center->x;
    double center_y = (double)dstrect->y + center->y;
    double x[4];
    double y[4];
    Sint32 u[4];
    Sint32 v[4];
    Sint32 left = srcquad->x * 65536;
    Sint32 right = (srcquad->x + srcquad->w) * 65536;
    Sint32 top = srcquad->y * 65536;
    Sint32 bottom = (srcquad->y + srcquad->h) * 65536;
    AstraColorRGBA8 color = { cmd->data.draw.r, cmd->data.draw.g,
                              cmd->data.draw.b, cmd->data.draw.a };

    if ((flip & SDL_FLIP_HORIZONTAL) != 0) {
        Sint32 swap = left;

        left = right;
        right = swap;
    }
    if ((flip & SDL_FLIP_VERTICAL) != 0) {
        Sint32 swap = top;

        top = bottom;
        bottom = swap;
    }
    /* Corners clockwise from the top left, rotated clockwise on screen. */
    x[0] = x[3] = dstrect->x;
    x[1] = x[2] = (double)dstrect->x + dstrect->w;
    y[0] = y[1] = dstrect->y;
    y[2] = y[3] = (double)dstrect->y + dstrect->h;
    u[0] = u[3] = left;
    u[1] = u[2] = right;
    v[0] = v[1] = top;
    v[2] = v[3] = bottom;
    record = SDL_AllocateRenderVertices(
        renderer, sizeof(*record) + 6u * sizeof(AstraVertex), 0,
        &cmd->data.draw.first);
    if (record == NULL)
        return -1;
    cmd->data.draw.count = 1u;
    *record = (ASTRA_CopyVertices){ .flags = ASTRA_Filter(texture),
                                    .vertices = 6u };
    out = (AstraVertex *)(void *)(record + 1);
    for (int index = 0; index < 6; ++index) {
        int corner = corners[index];
        double dx = x[corner] - center_x;
        double dy = y[corner] - center_y;

        out[index] = (AstraVertex){
            ASTRA_Fixed((c * dx - s * dy + center_x) * scale_x, 256.0),
            ASTRA_Fixed((s * dx + c * dy + center_y) * scale_y, 256.0),
            u[corner], v[corner], color };
    }
    return 0;
}

static int ASTRA_QueueGeometry(SDL_Renderer *renderer,
                               SDL_RenderCommand *cmd, SDL_Texture *texture,
                               const float *xy, int xy_stride,
                               const SDL_Color *color, int color_stride,
                               const float *uv, int uv_stride,
                               int num_vertices, const void *indices,
                               int num_indices, int size_indices,
                               float scale_x, float scale_y)
{
    int count = indices != NULL ? num_indices : num_vertices;
    ASTRA_CopyVertices *record = SDL_AllocateRenderVertices(
        renderer, sizeof(*record) + (size_t)count * sizeof(AstraVertex), 0,
        &cmd->data.draw.first);
    AstraVertex *out;

    if (record == NULL)
        return -1;
    cmd->data.draw.count = 1u;
    *record = (ASTRA_CopyVertices){
        .flags = texture != NULL ? ASTRA_Filter(texture) : 0u,
        .vertices = (Uint32)count };
    out = (AstraVertex *)(void *)(record + 1);
    for (int index = 0; index < count; ++index) {
        int at = index;
        const float *position;
        const SDL_Color *tint;

        if (indices != NULL && size_indices == 4)
            at = (int)((const Uint32 *)indices)[index];
        else if (indices != NULL && size_indices == 2)
            at = ((const Uint16 *)indices)[index];
        else if (indices != NULL)
            at = ((const Uint8 *)indices)[index];
        position = (const float *)(const void *)
                       ((const char *)xy + at * xy_stride);
        tint = (const SDL_Color *)(const void *)
                   ((const char *)color + at * color_stride);
        out[index] = (AstraVertex){
            ASTRA_Fixed((double)position[0] * scale_x, 256.0),
            ASTRA_Fixed((double)position[1] * scale_y, 256.0), 0, 0,
            { tint->r, tint->g, tint->b, tint->a } };
        if (texture != NULL) {
            const float *texel = (const float *)(const void *)
                                     ((const char *)uv + at * uv_stride);

            out[index].u = ASTRA_Fixed((double)texel[0] * texture->w,
                                       65536.0);
            out[index].v = ASTRA_Fixed((double)texel[1] * texture->h,
                                       65536.0);
        }
    }
    return 0;
}

static int ASTRA_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                             SDL_Texture *texture, const SDL_Rect *srcquad,
                             const SDL_FRect *dstrect, const double angle,
                             const SDL_FPoint *center,
                             const SDL_RendererFlip flip, float scale_x,
                             float scale_y)
{
    ASTRA_CopyVertices *vertices;
    double turn = SDL_fmod(angle, 360.0);
    SDL_FRect placed = *dstrect;
    SDL_RendererFlip mirror = flip;

    if (turn < 0.0)
        turn += 360.0;
    if (turn != 0.0 && turn != 180.0)
        return ASTRA_QueueRotated(renderer, cmd, texture, srcquad, dstrect,
                                  turn, center, flip, scale_x, scale_y);
    if (turn == 180.0) {
        /* A half turn about the center is both mirrors, moved so the
           rectangle rotates about that center: an exact blit. */
        placed.x = dstrect->x + 2.0f * center->x - dstrect->w;
        placed.y = dstrect->y + 2.0f * center->y - dstrect->h;
        mirror = (SDL_RendererFlip)(flip ^ (SDL_FLIP_HORIZONTAL |
                                            SDL_FLIP_VERTICAL));
    }
    vertices = SDL_AllocateRenderVertices(renderer, sizeof(*vertices), 0,
                                          &cmd->data.draw.first);
    if (vertices == NULL)
        return -1;
    cmd->data.draw.count = 1u;
    vertices->flags = ASTRA_Filter(texture);
    vertices->vertices = 0u;
    vertices->source = *srcquad;
    vertices->destination.x = (int)(placed.x * scale_x);
    vertices->destination.y = (int)(placed.y * scale_y);
    vertices->destination.w = (int)(placed.w * scale_x);
    vertices->destination.h = (int)(placed.h * scale_y);
    /* The mirrors travel in the otherwise unused vertex sign bits. */
    if ((mirror & SDL_FLIP_HORIZONTAL) != 0)
        vertices->source.w = -vertices->source.w;
    if ((mirror & SDL_FLIP_VERTICAL) != 0)
        vertices->source.h = -vertices->source.h;
    return 0;
}

typedef struct ASTRA_DrawState {
    ASTRA_RenderData *data;
    AstraDrawList *list;
    /* data->rects or data->segments [0, gathered) wait to be drawn in
       gather_paint; gather is what they are. */
    enum { ASTRA_GATHER_RECTS, ASTRA_GATHER_LINES } gather;
    Uint32 gathered;
    AstraDrawPaint gather_paint;
    uint16_t width;
    uint16_t height;
    SDL_Rect viewport;
    SDL_bool clip_enabled;
    SDL_Rect clip;
    SDL_bool clip_dirty;
} ASTRA_DrawState;

/* The gathered primitives as one command. Anything that draws otherwise
   or changes the clip sends them first, so drawing order is kept. */
static int ASTRA_Flush(ASTRA_DrawState *state)
{
    AstraResult result;

    if (state->gathered == 0u)
        return 0;
    result = state->gather == ASTRA_GATHER_RECTS ?
        astra_draw_rectangles(state->list, state->data->rects,
                              state->gathered, &state->gather_paint) :
        astra_draw_lines(state->list, state->data->segments,
                         state->gathered, &state->gather_paint);
    state->gathered = 0u;
    ++state->data->list_pending;
    return result == ASTRA_OK ? 0 : ASTRA_Failed("batch", result);
}

static int ASTRA_ApplyClip(ASTRA_DrawState *state)
{
    SDL_Rect clip;
    AstraRectI32 rect;
    AstraResult result;

    /* Every draw command asks; only a changed clip costs anything. */
    if (!state->clip_dirty)
        return 0;
    if (ASTRA_Flush(state) < 0)
        return -1;
    clip = state->viewport;
    if (state->clip_enabled) {
        SDL_Rect local = { state->clip.x + state->viewport.x,
                           state->clip.y + state->viewport.y,
                           state->clip.w, state->clip.h };

        if (!SDL_IntersectRect(&state->viewport, &local, &clip))
            clip = (SDL_Rect){ 0, 0, 0, 0 };
    }
    rect = (AstraRectI32){ clip.x, clip.y,
                           clip.w > 0 ? (uint32_t)clip.w : 1u,
                           clip.h > 0 ? (uint32_t)clip.h : 1u };
    /* An empty clip is placed outside the target so it draws nothing. */
    if (clip.w <= 0 || clip.h <= 0)
        rect.x = state->width;
    result = astra_draw_list_set_clip(state->list, &rect);
    state->clip_dirty = SDL_FALSE;
    return result == ASTRA_OK ? 0 : ASTRA_Failed("clip", result);
}

/* Gather the next @p kind in @p cmd's paint, sending what was gathered
   of another kind or in another paint first. SDL queues one command per
   SDL_RenderDrawPoint, so consecutive commands must share one call. */
static int ASTRA_Gather(ASTRA_DrawState *state, const SDL_RenderCommand *cmd,
                        int kind)
{
    AstraDrawPaint *paint = &state->gather_paint;
    ASTRA_RenderData *data = state->data;
    /* Lines are opaque here: BLEND at alpha 255 replaces. */
    uint32_t blend = kind == ASTRA_GATHER_LINES ? ASTRA_BLEND_NONE :
                     ASTRA_Blend(cmd->data.draw.blend);

    if (state->gathered != 0u && (int)state->gather == kind &&
        paint->foreground.red == cmd->data.draw.r &&
        paint->foreground.green == cmd->data.draw.g &&
        paint->foreground.blue == cmd->data.draw.b &&
        paint->foreground.alpha == cmd->data.draw.a &&
        paint->blend == blend)
        return 0;
    if (ASTRA_Flush(state) < 0)
        return -1;
    if (kind == ASTRA_GATHER_RECTS && data->rects == NULL)
        data->rects = SDL_malloc(ASTRA_GATHER_MAX * sizeof(*data->rects));
    if (kind == ASTRA_GATHER_LINES && data->segments == NULL)
        data->segments =
            SDL_malloc(ASTRA_GATHER_MAX * 2u * sizeof(*data->segments));
    if ((kind == ASTRA_GATHER_RECTS ? (void *)data->rects :
                                      (void *)data->segments) == NULL)
        return SDL_OutOfMemory();
    state->gather = kind;
    paint->foreground = (AstraColorRGBA8){ cmd->data.draw.r,
                                           cmd->data.draw.g,
                                           cmd->data.draw.b,
                                           cmd->data.draw.a };
    paint->blend = blend;
    return 0;
}

/* One viewport-relative filled rectangle into the gathered batch. */
static int ASTRA_Fill(ASTRA_DrawState *state, int x, int y, int w, int h)
{
    x += state->viewport.x;
    y += state->viewport.y;
    /* Cull what cannot reach the target; the ADLT carries 16-bit origins
       and extents, and 65535 from -32768 still spans every target. */
    if (w <= 0 || h <= 0 || x >= state->width || y >= state->height ||
        (Sint64)x + w <= 0 || (Sint64)y + h <= 0)
        return 0;
    if (x < INT16_MIN) {
        w += x - INT16_MIN;
        x = INT16_MIN;
    }
    if (y < INT16_MIN) {
        h += y - INT16_MIN;
        y = INT16_MIN;
    }
    if (state->gathered == ASTRA_GATHER_MAX && ASTRA_Flush(state) < 0)
        return -1;
    state->data->rects[state->gathered++] = (AstraRectI32){
        x, y, (uint32_t)SDL_min(w, UINT16_MAX),
        (uint32_t)SDL_min(h, UINT16_MAX) };
    return 0;
}

/* DRAW_POINTS and FILL_RECTS, both queued as rectangles. */
static int ASTRA_FillRects(ASTRA_DrawState *state,
                           const SDL_RenderCommand *cmd,
                           const AstraRectI32 *rects)
{
    if (ASTRA_Gather(state, cmd, ASTRA_GATHER_RECTS) < 0)
        return -1;
    for (size_t index = 0u; index < cmd->data.draw.count; ++index)
        if (ASTRA_Fill(state, rects[index].x, rects[index].y,
                       (int)rects[index].width,
                       (int)rects[index].height) < 0)
            return -1;
    return 0;
}

/* A blended line is its Bresenham pixels merged into row spans of blended
   fills, so each pixel blends once; the end pixel is left to the next
   segment unless @p last. */
static int ASTRA_BlendedLine(ASTRA_DrawState *state, int x0, int y0, int x1,
                             int y1, SDL_bool last)
{
    int dx = SDL_abs(x1 - x0);
    int dy = -SDL_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int span_x = x0;
    int span_y = y0;
    int span_w = 0;

    for (;;) {
        int end = x0 == x1 && y0 == y1;
        int twice;

        if (!end || last) {
            if (span_w != 0 && y0 == span_y &&
                x0 == (sx > 0 ? span_x + span_w : span_x - 1)) {
                if (sx < 0)
                    span_x = x0;
                ++span_w;
            } else {
                if (span_w != 0 &&
                    ASTRA_Fill(state, span_x, span_y, span_w, 1) < 0)
                    return -1;
                span_x = x0;
                span_y = y0;
                span_w = 1;
            }
        }
        if (end)
            break;
        twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    return span_w != 0 ? ASTRA_Fill(state, span_x, span_y, span_w, 1) : 0;
}

/* A strip of points: opaque segments gathered for the line engine, or
   blended ones as their row spans. */
static int ASTRA_DrawLines(ASTRA_DrawState *state, const SDL_Point *points,
                           int count, const SDL_RenderCommand *cmd)
{
    uint32_t blend = ASTRA_Blend(cmd->data.draw.blend);
    /* The line engine replaces pixels; only an opaque BLEND equals that. */
    int blended = blend != ASTRA_BLEND_NONE &&
                  (blend != ASTRA_BLEND_ALPHA || cmd->data.draw.a != 0xff);

    if (ASTRA_Gather(state, cmd, blended ? ASTRA_GATHER_RECTS :
                                           ASTRA_GATHER_LINES) < 0)
        return -1;
    for (int index = 1; index < count; ++index) {
        int x0 = points[index - 1].x;
        int y0 = points[index - 1].y;
        int x1 = points[index].x;
        int y1 = points[index].y;
        AstraPointI32 *segment;

        if (blended) {
            if (ASTRA_BlendedLine(state, x0, y0, x1, y1,
                                  (SDL_bool)(index == count - 1)) < 0)
                return -1;
            continue;
        }
        x0 += state->viewport.x;
        y0 += state->viewport.y;
        x1 += state->viewport.x;
        y1 += state->viewport.y;
        if (x0 < INT16_MIN || x0 > INT16_MAX || y0 < INT16_MIN ||
            y0 > INT16_MAX || x1 < INT16_MIN || x1 > INT16_MAX ||
            y1 < INT16_MIN || y1 > INT16_MAX)
            continue;
        if (state->gathered == ASTRA_GATHER_MAX && ASTRA_Flush(state) < 0)
            return -1;
        segment = &state->data->segments[state->gathered++ * 2u];
        segment[0] = (AstraPointI32){ x0, y0 };
        segment[1] = (AstraPointI32){ x1, y1 };
    }
    return 0;
}

/*
 * Triangles in viewport coordinates: moved to the target, triangles wholly
 * outside it culled in place. A visible vertex beyond the engine's range is
 * an error, not a distortion.
 */
static int ASTRA_Triangles(ASTRA_DrawState *state,
                           const SDL_RenderCommand *cmd,
                           ASTRA_CopyVertices *record)
{
    SDL_Texture *texture = cmd->data.draw.texture;
    AstraVertex *vertices = (AstraVertex *)(void *)(record + 1);
    Sint64 width = (Sint64)state->width * 256;
    Sint64 height = (Sint64)state->height * 256;
    Uint32 kept = 0u;
    AstraResult result;

    for (Uint32 first = 0u; first + 3u <= record->vertices; first += 3u) {
        Sint64 left = INT64_MAX;
        Sint64 top = INT64_MAX;
        Sint64 right = INT64_MIN;
        Sint64 bottom = INT64_MIN;
        Sint64 x[3];
        Sint64 y[3];

        for (Uint32 corner = 0u; corner < 3u; ++corner) {
            x[corner] = (Sint64)vertices[first + corner].x +
                        (Sint64)state->viewport.x * 256;
            y[corner] = (Sint64)vertices[first + corner].y +
                        (Sint64)state->viewport.y * 256;
            left = SDL_min(left, x[corner]);
            right = SDL_max(right, x[corner]);
            top = SDL_min(top, y[corner]);
            bottom = SDL_max(bottom, y[corner]);
        }
        if (right <= 0 || bottom <= 0 || left >= width || top >= height)
            continue;
        if (left < -ASTRA_VERTEX_LIMIT || top < -ASTRA_VERTEX_LIMIT ||
            right >= ASTRA_VERTEX_LIMIT || bottom >= ASTRA_VERTEX_LIMIT)
            return SDL_SetError("Astra renderer: a visible vertex lies "
                                "beyond +-32768 pixels");
        for (Uint32 corner = 0u; corner < 3u; ++corner) {
            vertices[kept] = vertices[first + corner];
            vertices[kept].x = (Sint32)x[corner];
            vertices[kept].y = (Sint32)y[corner];
            ++kept;
        }
    }
    if (kept == 0u)
        return 0;
    result = astra_draw_triangles(
        state->list,
        texture != NULL ?
            &((ASTRA_TextureData *)texture->driverdata)->surface : NULL,
        vertices, kept, ASTRA_Blend(cmd->data.draw.blend), record->flags);
    ++state->data->list_pending;
    return result == ASTRA_OK ? 0 : ASTRA_Failed("triangles", result);
}

static int ASTRA_Copy(ASTRA_DrawState *state, const SDL_RenderCommand *cmd,
                      ASTRA_CopyVertices *vertices)
{
    ASTRA_TextureData *texture = cmd->data.draw.texture->driverdata;
    AstraBlitOptions options = ASTRA_BLIT_OPTIONS_INIT;
    SDL_Rect source = vertices->source;
    SDL_Rect target = vertices->destination;
    AstraRectI32 from;
    AstraRectI32 to;
    AstraResult result;

    if (vertices->vertices != 0u)
        return ASTRA_Triangles(state, cmd, vertices);
    if (source.w < 0) {
        source.w = -source.w;
        options.flags |= ASTRA_BLIT_FLIP_X;
    }
    if (source.h < 0) {
        source.h = -source.h;
        options.flags |= ASTRA_BLIT_FLIP_Y;
    }
    target.x += state->viewport.x;
    target.y += state->viewport.y;
    if (source.w <= 0 || source.h <= 0 || target.w <= 0 || target.h <= 0 ||
        target.x >= state->width || target.y >= state->height ||
        (Sint64)target.x + target.w <= 0 ||
        (Sint64)target.y + target.h <= 0 ||
        target.x < INT16_MIN || target.y < INT16_MIN ||
        target.w > 32767 || target.h > 32767)
        return 0;
    options.flags |= vertices->flags;
    options.blend = ASTRA_Blend(cmd->data.draw.blend);
    options.modulate = (AstraColorRGBA8){ cmd->data.draw.r, cmd->data.draw.g,
                                          cmd->data.draw.b,
                                          cmd->data.draw.a };
    from = (AstraRectI32){ source.x, source.y, (uint32_t)source.w,
                           (uint32_t)source.h };
    to = (AstraRectI32){ target.x, target.y, (uint32_t)target.w,
                         (uint32_t)target.h };
    result = astra_draw_blit(state->list, &texture->surface, &from, &to,
                             &options);
    ++state->data->list_pending;
    return result == ASTRA_OK ? 0 : ASTRA_Failed("copy", result);
}

static int ASTRA_RunCommandQueue(SDL_Renderer *renderer,
                                 SDL_RenderCommand *cmd, void *vertices,
                                 size_t vertsize)
{
    ASTRA_DrawState state = {0};
    AstraResult result;
    int status = 0;

    (void)vertsize;
    state.data = renderer->driverdata;
    state.gather_paint = (AstraDrawPaint)ASTRA_DRAW_PAINT_INIT;
    state.list = ASTRA_TargetList(renderer, &state.width, &state.height);
    if (state.list == NULL)
        return -1;
    state.viewport = (SDL_Rect){ 0, 0, state.width, state.height };
    state.clip_dirty = SDL_TRUE;
    for (; cmd != NULL && status == 0; cmd = cmd->next) {
        const Uint8 *base = (const Uint8 *)vertices + cmd->data.draw.first;

        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            state.viewport = cmd->data.viewport.rect;
            state.clip_dirty = SDL_TRUE;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            state.clip_enabled = cmd->data.cliprect.enabled;
            state.clip = cmd->data.cliprect.rect;
            state.clip_dirty = SDL_TRUE;
            break;
        case SDL_RENDERCMD_CLEAR: {
            AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
            AstraRectI32 whole = { 0, 0, state.width, state.height };

            /* By definition a clear ignores the viewport and clip. */
            paint.foreground = (AstraColorRGBA8){
                cmd->data.color.r, cmd->data.color.g, cmd->data.color.b,
                cmd->data.color.a };
            status = ASTRA_Flush(&state);
            if (status != 0)
                break;
            result = astra_draw_list_set_clip(state.list, &whole);
            if (result == ASTRA_OK)
                result = astra_draw_rectangle(state.list, &whole, 1,
                                              &paint);
            ++state.data->list_pending;
            state.clip_dirty = SDL_TRUE;
            status = result == ASTRA_OK ? 0 : ASTRA_Failed("clear", result);
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_FILL_RECTS:
            status = ASTRA_ApplyClip(&state);
            if (status == 0)
                status = ASTRA_FillRects(&state, cmd,
                                         (const AstraRectI32 *)(const void *)
                                             base);
            break;
        case SDL_RENDERCMD_DRAW_LINES:
            status = ASTRA_ApplyClip(&state);
            if (status == 0)
                status = ASTRA_DrawLines(&state, (const SDL_Point *)base,
                                         (int)cmd->data.draw.count, cmd);
            break;
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX:
            status = ASTRA_ApplyClip(&state);
            if (status == 0)
                status = ASTRA_Flush(&state);
            if (status == 0)
                status = ASTRA_Copy(&state, cmd,
                                    (ASTRA_CopyVertices *)(void *)
                                        ((Uint8 *)vertices +
                                         cmd->data.draw.first));
            break;
        case SDL_RENDERCMD_GEOMETRY:
            status = ASTRA_ApplyClip(&state);
            if (status == 0)
                status = ASTRA_Flush(&state);
            if (status == 0)
                status = ASTRA_Triangles(
                    &state, cmd,
                    (ASTRA_CopyVertices *)(void *)
                        ((Uint8 *)vertices + cmd->data.draw.first));
            break;
        default:
            break;
        }
    }
    if (status == 0)
        status = ASTRA_Flush(&state);
    return status;
}

/* Pixels come back from Media RAM in the target's own format; SDL converts
   only when the caller asked for another one. */
static int ASTRA_RenderReadPixels(SDL_Renderer *renderer,
                                  const SDL_Rect *rect, Uint32 format,
                                  void *pixels, int pitch)
{
    ASTRA_RenderData *data = renderer->driverdata;
    SDL_Texture *target = renderer->target;
    const AstraSurface *surface;
    Uint32 native = SDL_PIXELFORMAT_RGB565;
    AstraRectI32 from;
    AstraResult result;
    Uint8 *staged;
    int staged_pitch;
    int status;

    if (rect->w <= 0 || rect->h <= 0 || pitch <= 0)
        return SDL_SetError("Astra renderer: empty readback");
    /* A readback returns its rows through staging, where an open lock's
       rows are. */
    if (data->staged_locks != 0)
        return SDL_SetError("Astra renderer: readback while a texture is "
                            "locked");
    if (ASTRA_PostPending(data) < 0)
        return -1;
    if (target != NULL) {
        surface = &((ASTRA_TextureData *)target->driverdata)->surface;
        native = target->format;
    } else {
        result = ASTRA_Content(data);
        if (result != ASTRA_OK)
            return ASTRA_Failed("window surface", result);
        surface = &data->content;
    }
    from = (AstraRectI32){ rect->x, rect->y, (uint32_t)rect->w,
                           (uint32_t)rect->h };
    if (format == native) {
        result = astra_surface_read(&data->window->display, surface, &from,
                                    pixels, (uint32_t)pitch);
        return result == ASTRA_OK ? 0 : ASTRA_Failed("readback", result);
    }
    staged_pitch = rect->w * SDL_BYTESPERPIXEL(native);
    staged = SDL_malloc((size_t)staged_pitch * (size_t)rect->h);
    if (staged == NULL)
        return SDL_OutOfMemory();
    result = astra_surface_read(&data->window->display, surface, &from,
                                staged, (uint32_t)staged_pitch);
    status = result == ASTRA_OK ?
        SDL_ConvertPixels(rect->w, rect->h, native, staged, staged_pitch,
                          format, pixels, pitch) :
        ASTRA_Failed("readback", result);
    SDL_free(staged);
    return status;
}

/* Vsync is the window's vblank subscription: the display service signals
 * the window's coalescing vblank event every frame while it subscribes. */
static int ASTRA_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    ASTRA_RenderData *data = renderer->driverdata;
    ASTRA_WindowData *window = data->window;
    uint32_t mask = vsync ? window->event_mask | ASTRA_WINDOW_SUBSCRIBE_VBLANK :
                            window->event_mask & ~ASTRA_WINDOW_SUBSCRIBE_VBLANK;
    AstraResult result;

    if (mask != window->event_mask) {
        result = astra_window_set_event_mask(&window->native, mask);
        if (result != ASTRA_OK)
            return ASTRA_Failed("vsync", result);
        window->event_mask = mask;
    }
    data->vsync = vsync != 0;
    if (data->vsync)
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    else
        renderer->info.flags &= ~SDL_RENDERER_PRESENTVSYNC;
    return 0;
}

static int ASTRA_RenderPresent(SDL_Renderer *renderer)
{
    ASTRA_RenderData *data = renderer->driverdata;
    AstraResult result = ASTRA_FrameList(data);

    /* The frame is handed over and the game goes on. SDL leaves the
       backbuffer undefined after a present; saying so spares the display
       service carrying each frame forward. */
    if (result == ASTRA_OK)
        result = astra_draw_post(&data->list, ASTRA_DRAW_POST_PRESENT |
                                                  ASTRA_DRAW_POST_DISCARD);
    data->list_pending = 0;
    if (result != ASTRA_OK)
        return ASTRA_Failed("present", result);
    data->list_posted = 1;
    /* The vblank event resets when a wait takes it, so this waits for the
     * first vblank since the previous present: a frame that is already
     * late is not held back a whole further frame. */
    if (data->vsync)
        (void)astra_wait_one(
            astra_window_vblank_wait_handle(&data->window->native),
            astra_clock_monotonic() + ASTRA_VSYNC_WAIT_NS, NULL);
    return 0;
}

static void ASTRA_DestroyTexture(SDL_Renderer *renderer,
                                 SDL_Texture *texture)
{
    ASTRA_TextureData *texture_data = texture->driverdata;

    if (texture_data == NULL)
        return;
    if (texture_data->lock_staged)
        --((ASTRA_RenderData *)renderer->driverdata)->staged_locks;
    /* Queued draws from or into it come first. */
    (void)ASTRA_PostPending(renderer->driverdata);
    ASTRA_Ignore(astra_surface_close(&texture_data->surface));
    SDL_free(texture_data->lock_pixels);
    SDL_free(texture_data);
    texture->driverdata = NULL;
}

static void ASTRA_DestroyRenderer(SDL_Renderer *renderer)
{
    ASTRA_RenderData *data = renderer->driverdata;

    /* SDL frees the renderer itself. */
    if (data != NULL) {
        ASTRA_DropWindowList(data);
        SDL_free(data->rects);
        SDL_free(data->segments);
        SDL_free(data);
    }
    renderer->driverdata = NULL;
}

static int ASTRA_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window,
                                Uint32 flags)
{
    ASTRA_RenderData *data;

    if (window->driverdata == NULL)
        return SDL_SetError("Astra renderer needs an Astra window");
    data = SDL_calloc(1u, sizeof(*data));
    if (data == NULL)
        return SDL_OutOfMemory();
    data->window = window->driverdata;
    data->content = (AstraSurface)ASTRA_SURFACE_INIT;
    data->list = (AstraDrawList)ASTRA_DRAW_LIST_INIT;
    /* Hardware lines, not SDL's per-pixel point expansion. */
    SDL_SetHintWithPriority(SDL_HINT_RENDER_LINE_METHOD, "2",
                            SDL_HINT_DEFAULT);
    renderer->WindowEvent = ASTRA_WindowEvent;
    renderer->GetOutputSize = ASTRA_GetOutputSize;
    renderer->SupportsBlendMode = ASTRA_SupportsBlendMode;
    renderer->CreateTexture = ASTRA_CreateTexture;
    renderer->UpdateTexture = ASTRA_UpdateTexture;
    renderer->LockTexture = ASTRA_LockTexture;
    renderer->UnlockTexture = ASTRA_UnlockTexture;
    renderer->SetTextureScaleMode = ASTRA_SetTextureScaleMode;
    renderer->SetRenderTarget = ASTRA_SetRenderTarget;
    renderer->QueueSetViewport = ASTRA_QueueNoOp;
    renderer->QueueSetDrawColor = ASTRA_QueueNoOp;
    renderer->QueueDrawPoints = ASTRA_QueueDrawPoints;
    renderer->QueueDrawLines = ASTRA_QueueDrawLines;
    renderer->QueueFillRects = ASTRA_QueueFillRects;
    renderer->QueueCopy = ASTRA_QueueCopy;
    renderer->QueueCopyEx = ASTRA_QueueCopyEx;
    renderer->QueueGeometry = ASTRA_QueueGeometry;
    renderer->RunCommandQueue = ASTRA_RunCommandQueue;
    renderer->RenderReadPixels = ASTRA_RenderReadPixels;
    renderer->RenderPresent = ASTRA_RenderPresent;
    renderer->DestroyTexture = ASTRA_DestroyTexture;
    renderer->DestroyRenderer = ASTRA_DestroyRenderer;
    renderer->SetVSync = ASTRA_SetVSync;
    renderer->info = ASTRA_RenderDriver.info;
    renderer->info.flags &= ~SDL_RENDERER_PRESENTVSYNC;
    renderer->driverdata = data;
    renderer->window = window;
    if ((flags & SDL_RENDERER_PRESENTVSYNC) != 0u &&
        ASTRA_SetVSync(renderer, 1) != 0)
        return -1;
    return 0;
}

SDL_RenderDriver ASTRA_RenderDriver = {
    ASTRA_CreateRenderer,
    {
        "astra",
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
            SDL_RENDERER_TARGETTEXTURE,
        3,
        { SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGB888,
          SDL_PIXELFORMAT_RGB565 },
        4096,
        4096
    }
};

#endif /* SDL_VIDEO_RENDER_ASTRA */
