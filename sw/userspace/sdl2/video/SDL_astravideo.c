#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_ASTRA

#include "SDL_video.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"

#include <astra/area.h>
#include <astra/display.h>
#include <astra/gui.h>
#include <astra/input.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#include "SDL_astravideo.h"

static void ASTRA_Ignore(AstraResult result)
{
    /* Teardown and best-effort paths report nothing further. */
    (void)result;
}

static uint16_t ASTRA_TitleLength(const char *title)
{
    size_t length;

    if (title == NULL)
        return 0u;
    length = SDL_strlen(title);
    if (length > ASTRA_WINDOW_TITLE_MAX) {
        length = ASTRA_WINDOW_TITLE_MAX;
        while (length != 0u && ((unsigned char)title[length] & 0xc0u) == 0x80u)
            --length;
    }
    return (uint16_t)length;
}

/*
 * The mouse, as SDL's Haiku backend drives app_server
 * (src/video/haiku/SDL_bvideo.cc:145-266) with what that backend leaves to
 * SDL's fallbacks done by the display service: a cursor is a system shape
 * or a 32x32 image of the window; hiding it is the NONE shape; warp,
 * confinement, capture and relative mode are the window's pointer grab,
 * which holds only while the window is active. Relative motion is the
 * device's own (the motion totals of each event), not Haiku's warp to the
 * window's centre after every move.
 */
typedef struct ASTRA_CursorData {
    AstraPointerShape shape;
    /* CUSTOM: the image, scaled to fit the pointer plane. */
    AstraColorRGBA8 pixels[ASTRA_HARDWARE_POINTER_WIDTH *
                           ASTRA_HARDWARE_POINTER_HEIGHT];
    uint16_t width;
    uint16_t height;
    int32_t hot_x;
    int32_t hot_y;
} ASTRA_CursorData;

/* The cursor SDL last showed, applied to each window as it opens. */
static const SDL_Cursor *ASTRA_current_cursor;
static SDL_bool ASTRA_cursor_set;
static SDL_bool ASTRA_relative;
static SDL_Window *ASTRA_captured;
/* The pointer's last screen position any window was told. */
static int32_t ASTRA_screen_x;
static int32_t ASTRA_screen_y;

static SDL_Cursor *ASTRA_NewCursor(ASTRA_CursorData **data)
{
    SDL_Cursor *cursor = SDL_calloc(1u, sizeof(*cursor));

    *data = SDL_calloc(1u, sizeof(**data));
    if (cursor == NULL || *data == NULL) {
        SDL_free(cursor);
        SDL_free(*data);
        SDL_OutOfMemory();
        return NULL;
    }
    cursor->driverdata = *data;
    return cursor;
}

static SDL_Cursor *ASTRA_CreateSystemCursor(SDL_SystemCursor id)
{
    static const AstraPointerShape shapes[SDL_NUM_SYSTEM_CURSORS] = {
        [SDL_SYSTEM_CURSOR_ARROW] = ASTRA_POINTER_SHAPE_DEFAULT,
        [SDL_SYSTEM_CURSOR_IBEAM] = ASTRA_POINTER_SHAPE_TEXT,
        [SDL_SYSTEM_CURSOR_WAIT] = ASTRA_POINTER_SHAPE_WAIT,
        [SDL_SYSTEM_CURSOR_CROSSHAIR] = ASTRA_POINTER_SHAPE_CROSSHAIR,
        [SDL_SYSTEM_CURSOR_WAITARROW] = ASTRA_POINTER_SHAPE_PROGRESS,
        [SDL_SYSTEM_CURSOR_SIZENWSE] = ASTRA_POINTER_SHAPE_RESIZE_NW_SE,
        [SDL_SYSTEM_CURSOR_SIZENESW] = ASTRA_POINTER_SHAPE_RESIZE_NE_SW,
        [SDL_SYSTEM_CURSOR_SIZEWE] = ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL,
        [SDL_SYSTEM_CURSOR_SIZENS] = ASTRA_POINTER_SHAPE_RESIZE_VERTICAL,
        [SDL_SYSTEM_CURSOR_SIZEALL] = ASTRA_POINTER_SHAPE_MOVE,
        [SDL_SYSTEM_CURSOR_NO] = ASTRA_POINTER_SHAPE_NOT_ALLOWED,
        [SDL_SYSTEM_CURSOR_HAND] = ASTRA_POINTER_SHAPE_HAND,
    };
    ASTRA_CursorData *data;
    SDL_Cursor *cursor;

    if ((int)id < 0 || id >= SDL_NUM_SYSTEM_CURSORS) {
        SDL_SetError("Astra has no system cursor %d", (int)id);
        return NULL;
    }
    cursor = ASTRA_NewCursor(&data);
    if (cursor != NULL)
        data->shape = shapes[id];
    return cursor;
}

/* A cursor from any surface. One larger than the pointer plane is scaled
   down to fit, keeping its proportions and its hotspot: each pixel is the
   average of the source pixels it covers, alpha-weighted so transparent
   pixels add no colour. */
static SDL_Cursor *ASTRA_CreateCursor(SDL_Surface *surface, int hot_x,
                                      int hot_y)
{
    const int plane = ASTRA_HARDWARE_POINTER_WIDTH;
    ASTRA_CursorData *data;
    SDL_Cursor *cursor;
    SDL_Surface *rgba;
    int largest;
    int width;
    int height;

    if (surface == NULL || surface->w <= 0 || surface->h <= 0 ||
        hot_x < 0 || hot_y < 0 || hot_x >= surface->w ||
        hot_y >= surface->h) {
        SDL_SetError("Astra cursor needs an image and a hotspot inside it");
        return NULL;
    }
    rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (rgba == NULL)
        return NULL;
    cursor = ASTRA_NewCursor(&data);
    if (cursor == NULL) {
        SDL_FreeSurface(rgba);
        return NULL;
    }
    largest = rgba->w > rgba->h ? rgba->w : rgba->h;
    width = largest <= plane ? rgba->w :
            SDL_max(1, (rgba->w * plane + largest / 2) / largest);
    height = largest <= plane ? rgba->h :
             SDL_max(1, (rgba->h * plane + largest / 2) / largest);
    data->shape = ASTRA_POINTER_SHAPE_CUSTOM;
    data->width = (uint16_t)width;
    data->height = (uint16_t)height;
    data->hot_x = SDL_min(width - 1, hot_x * width / rgba->w);
    data->hot_y = SDL_min(height - 1, hot_y * height / rgba->h);
    for (int y = 0; y < height; ++y) {
        int top = y * rgba->h / height;
        int bottom = SDL_max(top + 1, (y + 1) * rgba->h / height);

        for (int x = 0; x < width; ++x) {
            int left = x * rgba->w / width;
            int right = SDL_max(left + 1, (x + 1) * rgba->w / width);
            Uint32 red = 0u, green = 0u, blue = 0u, alpha = 0u;
            Uint32 count = 0u;

            for (int sy = top; sy < bottom; ++sy) {
                const Uint8 *row = (const Uint8 *)rgba->pixels +
                                   (size_t)sy * (size_t)rgba->pitch;

                for (int sx = left; sx < right; ++sx) {
                    const Uint8 *pixel = row + (size_t)sx * 4u;

                    red += (Uint32)pixel[0] * pixel[3];
                    green += (Uint32)pixel[1] * pixel[3];
                    blue += (Uint32)pixel[2] * pixel[3];
                    alpha += pixel[3];
                    ++count;
                }
            }
            data->pixels[y * plane + x] = (AstraColorRGBA8){
                (uint8_t)(alpha != 0u ? red / alpha : 0u),
                (uint8_t)(alpha != 0u ? green / alpha : 0u),
                (uint8_t)(alpha != 0u ? blue / alpha : 0u),
                (uint8_t)((alpha + count / 2u) / count),
            };
        }
    }
    SDL_FreeSurface(rgba);
    return cursor;
}

static void ASTRA_FreeCursor(SDL_Cursor *cursor)
{
    SDL_VideoDevice *device = SDL_GetVideoDevice();

    if (cursor == NULL)
        return;
    if (ASTRA_current_cursor == cursor)
        ASTRA_current_cursor = NULL;
    /* A window showing it keeps the image the service copied; it is never
       named again. */
    for (SDL_Window *window = device != NULL ? device->windows : NULL;
         window != NULL; window = window->next) {
        ASTRA_WindowData *data = window->driverdata;

        if (data != NULL && data->cursor == cursor)
            data->cursor = NULL, data->cursor_applied = 0u;
    }
    SDL_free(cursor->driverdata);
    SDL_free(cursor);
}

/* Shows @p cursor in one window, or hides the pointer over it. */
static int ASTRA_ApplyCursor(SDL_Window *window, const SDL_Cursor *cursor)
{
    ASTRA_WindowData *data = window->driverdata;
    const ASTRA_CursorData *cursor_data =
        cursor != NULL ? cursor->driverdata : NULL;
    AstraResult result;

    if (data == NULL || (data->cursor_applied != 0u &&
                         data->cursor == cursor))
        return 0;
    if (cursor_data == NULL) {
        result = astra_window_set_pointer_shape(&data->native,
                                                ASTRA_POINTER_SHAPE_NONE);
    } else if (cursor_data->shape == ASTRA_POINTER_SHAPE_CUSTOM) {
        AstraHardwarePointerImage image = ASTRA_HARDWARE_POINTER_IMAGE_INIT;

        image.pixels = cursor_data->pixels;
        image.width = cursor_data->width;
        image.height = cursor_data->height;
        image.pitch = ASTRA_HARDWARE_POINTER_WIDTH *
                      sizeof(cursor_data->pixels[0]);
        image.hotspot = (AstraPointI32){ cursor_data->hot_x,
                                         cursor_data->hot_y };
        result = astra_window_set_pointer_image(&data->native, &image);
    } else {
        result = astra_window_set_pointer_shape(&data->native,
                                                cursor_data->shape);
    }
    if (result != ASTRA_OK)
        return SDL_SetError("Astra cursor failed (%d)", result);
    data->cursor = cursor;
    data->cursor_applied = 1u;
    return 0;
}

static int ASTRA_ShowCursor(SDL_Cursor *cursor)
{
    SDL_VideoDevice *device = SDL_GetVideoDevice();
    int status = 0;

    ASTRA_current_cursor = cursor;
    ASTRA_cursor_set = SDL_TRUE;
    for (SDL_Window *window = device != NULL ? device->windows : NULL;
         window != NULL; window = window->next)
        if (ASTRA_ApplyCursor(window, cursor) < 0)
            status = -1;
    return status;
}

/* The window's pointer grab from SDL's state: relative mode locks the
   pointer, a mouse grab confines it to the content and a mouse rectangle
   to that rectangle, and a capture takes the pointer's events wherever it
   is. */
static int ASTRA_UpdateGrab(SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    const SDL_Rect *rect = &window->mouse_rect;
    AstraWindowFrame frame = { 0u, 0u, 0u, 0u };
    uint32_t flags = 0u;
    AstraResult result;

    if (data == NULL)
        return 0;
    if (ASTRA_relative)
        flags |= ASTRA_WINDOW_POINTER_LOCK;
    if (ASTRA_captured == window)
        flags |= ASTRA_WINDOW_POINTER_CAPTURE;
    if (!SDL_RectEmpty(rect)) {
        SDL_Rect content = { 0, 0, window->w, window->h };
        SDL_Rect inside;

        if (SDL_IntersectRect(rect, &content, &inside)) {
            flags |= ASTRA_WINDOW_POINTER_CONFINE;
            frame = (AstraWindowFrame){ (uint16_t)inside.x,
                                        (uint16_t)inside.y,
                                        (uint16_t)inside.w,
                                        (uint16_t)inside.h };
        }
    } else if ((window->flags & SDL_WINDOW_MOUSE_GRABBED) != 0u) {
        flags |= ASTRA_WINDOW_POINTER_CONFINE;
    }
    if (flags == data->grab_flags &&
        SDL_memcmp(&frame, &data->grab_rect, sizeof(frame)) == 0)
        return 0;
    result = astra_window_set_pointer_grab(
        &data->native, flags, frame.width != 0u ? &frame : NULL);
    if (result != ASTRA_OK)
        return SDL_SetError("Astra pointer grab failed (%d)", result);
    data->grab_flags = flags;
    data->grab_rect = frame;
    return 0;
}

static void ASTRA_SetWindowMouseGrab(_THIS, SDL_Window *window,
                                     SDL_bool grabbed)
{
    (void)_this;
    (void)grabbed;
    (void)ASTRA_UpdateGrab(window);
}

static void ASTRA_SetWindowMouseRect(_THIS, SDL_Window *window)
{
    (void)_this;
    (void)ASTRA_UpdateGrab(window);
}

/* Relative mode is the whole program's; each window holds its lock only
   while it is active. */
static int ASTRA_SetRelativeMouseMode(SDL_bool enabled)
{
    SDL_VideoDevice *device = SDL_GetVideoDevice();
    int status = 0;

    ASTRA_relative = enabled;
    for (SDL_Window *window = device != NULL ? device->windows : NULL;
         window != NULL; window = window->next) {
        ASTRA_WindowData *data = window->driverdata;

        if (data != NULL)
            data->motion_valid = 0u;
        if (ASTRA_UpdateGrab(window) < 0)
            status = -1;
    }
    return status;
}

static int ASTRA_CaptureMouse(SDL_Window *window)
{
    SDL_Window *previous = ASTRA_captured;
    int status = 0;

    ASTRA_captured = window;
    if (previous != NULL && previous != window)
        status = ASTRA_UpdateGrab(previous);
    if (window != NULL && ASTRA_UpdateGrab(window) < 0)
        status = -1;
    return status;
}

static void ASTRA_WarpMouse(SDL_Window *window, int x, int y)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    if (data == NULL)
        return;
    result = astra_window_warp_pointer(&data->native, SDL_max(x, 0),
                                       SDL_max(y, 0));
    if (result != ASTRA_OK)
        SDL_SetError("Astra pointer warp failed (%d)", result);
}

/* Only the active window may move the pointer, so a global warp is a warp
   in the window with the keyboard, placed by where its content was last
   seen on the screen. */
static int ASTRA_WarpMouseGlobal(int x, int y)
{
    SDL_Window *window = SDL_GetKeyboardFocus();
    ASTRA_WindowData *data = window != NULL ? window->driverdata : NULL;
    AstraResult result;

    if (data == NULL || data->origin_valid == 0u)
        return SDL_SetError("Astra moves the pointer only in its active "
                            "window");
    result = astra_window_warp_pointer(&data->native,
                                       SDL_max(x - data->origin_x, 0),
                                       SDL_max(y - data->origin_y, 0));
    return result == ASTRA_OK ? 0 :
        SDL_SetError("Astra pointer warp failed (%d)", result);
}

static Uint32 ASTRA_GetGlobalMouseState(int *x, int *y)
{
    *x = ASTRA_screen_x;
    *y = ASTRA_screen_y;
    return SDL_GetMouseState(NULL, NULL);
}

static void ASTRA_InitMouse(void)
{
    SDL_Mouse *mouse = SDL_GetMouse();

    mouse->CreateCursor = ASTRA_CreateCursor;
    mouse->CreateSystemCursor = ASTRA_CreateSystemCursor;
    mouse->ShowCursor = ASTRA_ShowCursor;
    mouse->FreeCursor = ASTRA_FreeCursor;
    mouse->WarpMouse = ASTRA_WarpMouse;
    mouse->WarpMouseGlobal = ASTRA_WarpMouseGlobal;
    mouse->SetRelativeMouseMode = ASTRA_SetRelativeMouseMode;
    mouse->CaptureMouse = ASTRA_CaptureMouse;
    mouse->GetGlobalMouseState = ASTRA_GetGlobalMouseState;
    SDL_SetDefaultCursor(ASTRA_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
}

/* A pointer event's position: where the content is on the screen, and
   where the pointer is. */
static void ASTRA_NotePointer(ASTRA_WindowData *data, int32_t x, int32_t y,
                              int32_t screen_x, int32_t screen_y)
{
    data->origin_x = screen_x - x;
    data->origin_y = screen_y - y;
    data->origin_valid = 1u;
    ASTRA_screen_x = screen_x;
    ASTRA_screen_y = screen_y;
}

/* Motion: in relative mode the device's own motion since the last event,
   otherwise the new position. */
static void ASTRA_PointerMotion(SDL_Window *window, ASTRA_WindowData *data,
                                const AstraWindowMotionEvent *motion)
{
    ASTRA_NotePointer(data, motion->x, motion->y, motion->screen_x,
                      motion->screen_y);
    if (ASTRA_relative) {
        int32_t dx = (int32_t)((uint32_t)motion->motion_x -
                               (uint32_t)data->motion_x);
        int32_t dy = (int32_t)((uint32_t)motion->motion_y -
                               (uint32_t)data->motion_y);

        data->motion_x = motion->motion_x;
        data->motion_y = motion->motion_y;
        if (data->motion_valid == 0u) {
            data->motion_valid = 1u;
            return;
        }
        if (dx != 0 || dy != 0) {
            if (SDL_GetMouseFocus() != window)
                SDL_SetMouseFocus(window);
            SDL_SendMouseMotion(window, 0, 1, dx, dy);
        }
        return;
    }
    data->motion_x = motion->motion_x;
    data->motion_y = motion->motion_y;
    data->motion_valid = 1u;
    SDL_SendMouseMotion(window, 0, 0, motion->x, motion->y);
}

static int ASTRA_VideoInit(_THIS)
{
    SDL_DisplayMode mode;

    if (astra_startup_capability(astra_posix_startup(),
                                 ASTRA_CAPABILITY_GUI) == NULL)
        return SDL_SetError("Astra GUI capability is unavailable");
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = ASTRA_DISPLAY_WIDTH;
    mode.h = ASTRA_DISPLAY_HEIGHT;
    mode.refresh_rate = 60;
    if (SDL_AddBasicVideoDisplay(&mode) < 0)
        return -1;
    SDL_AddDisplayMode(&_this->displays[0], &mode);
    ASTRA_InitMouse();
    return 0;
}

static void ASTRA_VideoQuit(_THIS)
{
    (void)_this;
}

static int ASTRA_CreateWindow(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraArea placeholder = ASTRA_AREA_INIT;
    const AstraStartupCapability *gui = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_GUI);
    /* SDL sizes a window created fullscreen to the display and asks for
     * fullscreen only once it exists (SetWindowFullscreen); the native
     * window starts at the frame it will return to. */
    const SDL_Rect *frame = &window->windowed;
    AstraResult result;

    (void)_this;
    if (gui == NULL || frame->x < 0 || frame->y < 0 ||
        frame->x > UINT16_MAX || frame->y > UINT16_MAX ||
        frame->w <= 0 || frame->h <= 0 ||
        frame->w > (int)ASTRA_DISPLAY_WIDTH ||
        frame->h > (int)ASTRA_DISPLAY_HEIGHT)
        return SDL_SetError("Astra window is outside the display");
    data = SDL_calloc(1u, sizeof(*data));
    if (data == NULL)
        return SDL_OutOfMemory();
    data->display = (AstraDisplay)ASTRA_DISPLAY_INIT;
    data->content = (AstraSurface)ASTRA_SURFACE_INIT;
    /* A surface window's shared area is only its first staging area; the
       display replaces it when a framebuffer or texture upload needs one. */
    result = astra_area_create(4096u,
                               ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                   ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
                               &placeholder);
    if (result != ASTRA_OK) {
        SDL_free(data);
        return SDL_SetError("Astra could not allocate a window area (%d)",
                            result);
    }
    info.x = (uint16_t)frame->x;
    info.y = (uint16_t)frame->y;
    info.width = (uint16_t)frame->w;
    info.height = (uint16_t)frame->h;
    info.pitch = 0u;
    info.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    info.flags = (window->flags & SDL_WINDOW_RESIZABLE) != 0u ?
                 ASTRA_WINDOW_RESIZABLE : 0u;
    info.title = window->title;
    info.title_length = ASTRA_TitleLength(window->title);
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_ALL &
                      ~(ASTRA_WINDOW_SUBSCRIBE_VBLANK |
                        ASTRA_WINDOW_SUBSCRIBE_SYSTEM_ACTION);
    data->event_mask = info.event_mask;
    result = astra_window_create(gui->handle, placeholder.handle, &info,
                                 &data->native);
    ASTRA_Ignore(astra_area_close(&placeholder));
    if (result == ASTRA_OK)
        result = astra_window_display(&data->native, &data->display);
    if (result != ASTRA_OK) {
        if (data->native._private_id != 0u)
            ASTRA_Ignore(astra_window_close(&data->native));
        SDL_free(data);
        return SDL_SetError("Astra window creation failed (%d)", result);
    }
    window->driverdata = data;
    /* The program's cursor and grab hold in every window it opens. */
    if (ASTRA_cursor_set)
        (void)ASTRA_ApplyCursor(window, ASTRA_current_cursor);
    (void)ASTRA_UpdateGrab(window);
    return 0;
}

static void ASTRA_DestroyWindow(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data == NULL)
        return;
    if (ASTRA_captured == window)
        ASTRA_captured = NULL;
    if (data->content._private_handle != ASTRA_INVALID_HANDLE)
        ASTRA_Ignore(astra_surface_close(&data->content));
    ASTRA_Ignore(astra_display_close(&data->display));
    result = astra_window_close(&data->native);
    if (result != ASTRA_OK)
        SDL_SetError("Astra window close failed (%d)", result);
    SDL_free(data);
    window->driverdata = NULL;
}

static void ASTRA_ShowWindow(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data == NULL)
        return;
    result = astra_window_activate(&data->native);
    if (result != ASTRA_OK)
        SDL_SetError("Astra window activation failed (%d)", result);
}

static void ASTRA_HideWindow(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data == NULL)
        return;
    result = astra_window_minimize(&data->native);
    if (result != ASTRA_OK)
        SDL_SetError("Astra window minimization failed (%d)", result);
}

static int ASTRA_CreateWindowFramebuffer(_THIS, SDL_Window *window,
                                         Uint32 *format, void **pixels,
                                         int *pitch)
{
    ASTRA_WindowData *data = window->driverdata;
    uint32_t bytes = 0u;
    void *staging = NULL;
    AstraResult result;

    (void)_this;
    if (data == NULL || window->w <= 0 || window->h <= 0)
        return SDL_SetError("Astra window has no surface");
    /* SDL recreates the framebuffer after a resize: refresh the content
       surface so writes are checked against the current size. */
    if (data->content._private_handle != ASTRA_INVALID_HANDLE)
        ASTRA_Ignore(astra_surface_close(&data->content));
    result = astra_window_surface(&data->native, &data->content);
    if (result == ASTRA_OK)
        result = astra_display_staging(
            &data->display, (uint32_t)window->w * (uint32_t)window->h * 2u,
            &staging, &bytes);
    if (result != ASTRA_OK)
        return SDL_SetError("Astra framebuffer allocation failed (%d)",
                            result);
    data->framebuffer = staging;
    data->framebuffer_pitch = (uint32_t)window->w * 2u;
    *format = SDL_PIXELFORMAT_RGB565;
    *pixels = staging;
    *pitch = (int)data->framebuffer_pitch;
    return 0;
}

static int ASTRA_UpdateWindowFramebuffer(_THIS, SDL_Window *window,
                                         const SDL_Rect *rects, int numrects)
{
    ASTRA_WindowData *data = window->driverdata;
    int width = data != NULL ? (int)data->content._private_width : 0;
    int height = data != NULL ? (int)data->content._private_height : 0;
    int left = width;
    int top = height;
    int right = 0;
    int bottom = 0;
    AstraRectI32 rect;
    AstraWindowFrame damage;
    AstraResult result;

    (void)_this;
    if (data == NULL || data->framebuffer == NULL || numrects < 0 ||
        (numrects != 0 && rects == NULL))
        return SDL_SetError("Invalid Astra framebuffer update");
    for (int index = 0; index < numrects; ++index) {
        const SDL_Rect *item = &rects[index];
        int x0 = SDL_max(item->x, 0);
        int y0 = SDL_max(item->y, 0);
        int x1 = (int)SDL_min((Sint64)item->x + item->w,
                              SDL_min(width, window->w));
        int y1 = (int)SDL_min((Sint64)item->y + item->h,
                              SDL_min(height, window->h));

        if (x0 >= x1 || y0 >= y1)
            continue;
        left = SDL_min(left, x0);
        top = SDL_min(top, y0);
        right = SDL_max(right, x1);
        bottom = SDL_max(bottom, y1);
    }
    if (left >= right || top >= bottom)
        return 0;
    rect = (AstraRectI32){ left, top, (uint32_t)(right - left),
                           (uint32_t)(bottom - top) };
    result = astra_surface_write_staged(
        &data->content, &rect,
        (uint32_t)top * data->framebuffer_pitch + (uint32_t)left * 2u,
        data->framebuffer_pitch);
    if (result != ASTRA_OK)
        return SDL_SetError("Astra framebuffer write failed (%d)", result);
    damage.x = (uint16_t)left;
    damage.y = (uint16_t)top;
    damage.width = (uint16_t)(right - left);
    damage.height = (uint16_t)(bottom - top);
    result = astra_window_present_region(&data->native, &damage);
    return result == ASTRA_OK ? 0 :
           SDL_SetError("Astra framebuffer present failed (%d)", result);
}

static void ASTRA_SetWindowTitle(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data == NULL)
        return;
    result = astra_window_set_title(&data->native, window->title,
                                    ASTRA_TitleLength(window->title));
    if (result != ASTRA_OK)
        SDL_SetError("Astra window title failed (%d)", result);
}

static void ASTRA_SetWindowPosition(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data != NULL && window->x >= 0 && window->y >= 0 &&
        window->x <= UINT16_MAX && window->y <= UINT16_MAX) {
        result = astra_window_move(&data->native, (uint16_t)window->x,
                                   (uint16_t)window->y);
        if (result != ASTRA_OK)
            SDL_SetError("Astra window move failed (%d)", result);
    }
}

static void ASTRA_SetWindowSize(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraWindowFrame frame;
    AstraResult result;

    (void)_this;
    if (data == NULL || window->x < 0 || window->y < 0 ||
        window->w <= 0 || window->h <= 0 ||
        window->x > UINT16_MAX || window->y > UINT16_MAX ||
        window->w > UINT16_MAX || window->h > UINT16_MAX)
        return;
    frame = (AstraWindowFrame){ (uint16_t)window->x, (uint16_t)window->y,
                                (uint16_t)window->w, (uint16_t)window->h };
    result = astra_window_set_frame(&data->native, &frame);
    if (result != ASTRA_OK)
        SDL_SetError("Astra window resize failed (%d)", result);
}

/* Fullscreen is a window state of the display service: the whole display
 * without chrome, above the menu bar; restoring returns the window's own
 * state and frame. The service reports the new frame as a state event. */
static void ASTRA_SetWindowFullscreen(_THIS, SDL_Window *window,
                                      SDL_VideoDisplay *display,
                                      SDL_bool fullscreen)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    (void)display;
    if (data == NULL)
        return;
    result = fullscreen ? astra_window_fullscreen(&data->native) :
                          astra_window_restore(&data->native);
    if (result != ASTRA_OK)
        SDL_SetError("Astra window fullscreen failed (%d)", result);
}


static void ASTRA_PumpEvents(_THIS)
{
    SDL_Window *window;

    for (window = _this->windows; window != NULL; window = window->next) {
        ASTRA_WindowData *data = window->driverdata;
        AstraWindowEvent event;

        if (data == NULL)
            continue;
        while (astra_window_event_try(&data->native, &event) == ASTRA_OK) {
            switch (event.type) {
            case ASTRA_WINDOW_EVENT_POINTER_MOTION:
                ASTRA_PointerMotion(window, data, &event.data.motion);
                break;
            case ASTRA_WINDOW_EVENT_POINTER_BUTTON: {
                Uint8 button = (Uint8)event.data.pointer.button;

                ASTRA_NotePointer(data, event.data.pointer.x,
                                  event.data.pointer.y,
                                  event.data.pointer.screen_x,
                                  event.data.pointer.screen_y);

                if (button == ASTRA_INPUT_BUTTON_SIDE)
                    button = SDL_BUTTON_X1;
                else if (button == ASTRA_INPUT_BUTTON_EXTRA)
                    button = SDL_BUTTON_X2;
                if (button <= SDL_BUTTON_X2)
                    SDL_SendMouseButtonClicks(
                        window, 0,
                        (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u ?
                            SDL_PRESSED : SDL_RELEASED,
                        button, (int)event.data.pointer.click_count);
                break;
            }
            case ASTRA_WINDOW_EVENT_POINTER_WHEEL:
                SDL_SendMouseWheel(window, 0,
                                   (float)event.data.wheel.delta_x,
                                   (float)event.data.wheel.delta_y,
                                   SDL_MOUSEWHEEL_NORMAL);
                break;
            case ASTRA_WINDOW_EVENT_KEY:
                if (event.data.key.usage < SDL_NUM_SCANCODES)
                    SDL_SendKeyboardKey(
                        (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u ?
                            SDL_PRESSED : SDL_RELEASED,
                        (SDL_Scancode)event.data.key.usage);
                break;
            case ASTRA_WINDOW_EVENT_TEXT:
                if (event.data.text.codepoint <= 0x10ffffu) {
                    char utf8[5] = {0};

                    SDL_UCS4ToUTF8(event.data.text.codepoint, utf8);
                    SDL_SendKeyboardText(utf8);
                }
                break;
            case ASTRA_WINDOW_EVENT_STATE:
                /* The person may take a window full screen and back from
                   the system (Ctrl+GUI+F) as well as the program: SDL's
                   flag follows the display, as on other window systems. */
                if (event.data.state.state == ASTRA_WINDOW_STATE_FULLSCREEN)
                    window->flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
                else if (event.data.state.state !=
                         ASTRA_WINDOW_STATE_MINIMIZED)
                    window->flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SendWindowEvent(window, SDL_WINDOWEVENT_MOVED,
                                    event.data.state.frame.x,
                                    event.data.state.frame.y);
                SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED,
                                    event.data.state.frame.width,
                                    event.data.state.frame.height);
                if ((event.data.state.flags & ASTRA_WINDOW_ACTIVE) != 0u)
                    SDL_SetKeyboardFocus(window);
                else if (SDL_GetKeyboardFocus() == window)
                    SDL_SetKeyboardFocus(NULL);
                SDL_SendWindowEvent(window,
                    event.data.state.state == ASTRA_WINDOW_STATE_MINIMIZED ?
                        SDL_WINDOWEVENT_MINIMIZED :
                    event.data.state.state == ASTRA_WINDOW_STATE_MAXIMIZED ?
                        SDL_WINDOWEVENT_MAXIMIZED : SDL_WINDOWEVENT_RESTORED,
                    0, 0);
                break;
            case ASTRA_WINDOW_EVENT_RESIZE:
                SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED,
                                    event.data.resize.width,
                                    event.data.resize.height);
                break;
            case ASTRA_WINDOW_EVENT_CLOSE_REQUEST:
                SDL_SendWindowEvent(window, SDL_WINDOWEVENT_CLOSE, 0, 0);
                break;
            case ASTRA_WINDOW_EVENT_STATE_RESET:
                SDL_SetKeyboardFocus(NULL);
                SDL_SetMouseFocus(NULL);
                break;
            default:
                break;
            }
        }
    }
}

static void ASTRA_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *ASTRA_CreateDevice(void)
{
    SDL_VideoDevice *device = SDL_calloc(1u, sizeof(*device));

    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }
    device->VideoInit = ASTRA_VideoInit;
    device->VideoQuit = ASTRA_VideoQuit;
    device->CreateSDLWindow = ASTRA_CreateWindow;
    device->DestroyWindow = ASTRA_DestroyWindow;
    device->ShowWindow = ASTRA_ShowWindow;
    device->HideWindow = ASTRA_HideWindow;
    device->CreateWindowFramebuffer = ASTRA_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = ASTRA_UpdateWindowFramebuffer;
    device->SetWindowTitle = ASTRA_SetWindowTitle;
    device->SetWindowPosition = ASTRA_SetWindowPosition;
    device->SetWindowSize = ASTRA_SetWindowSize;
    device->SetWindowFullscreen = ASTRA_SetWindowFullscreen;
    device->SetWindowMouseGrab = ASTRA_SetWindowMouseGrab;
    device->SetWindowMouseRect = ASTRA_SetWindowMouseRect;
    device->PumpEvents = ASTRA_PumpEvents;
    device->free = ASTRA_DeleteDevice;
    return device;
}

VideoBootStrap ASTRA_bootstrap = {
    "astra", "Astra native window and RGB565 video", ASTRA_CreateDevice, NULL
};

#endif
