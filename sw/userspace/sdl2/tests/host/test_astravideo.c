#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SDL_VIDEO_DRIVER_ASTRA 1
#include "../../build/vendor/SDL2/src/video/astra/SDL_astravideo.c"

static uint16_t backing[ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT];
static AstraStartupCapability gui_capability;
static int has_gui;
static int create_failure;
static int activation_failure;
static int errors;
static int surface_closes;
static int area_closes;
static int display_closes;
static int writes;
static AstraRectI32 last_write;
static uint32_t last_write_offset;
static uint32_t last_write_pitch;
static int window_closes;
static int activations;
static int minimizations;
static int fullscreens;
static int restores;
static int presents;
static AstraWindowFrame last_damage;
static SDL_VideoDisplay display;
static AstraWindowEvent events[9];
static size_t event_count;
static size_t event_next;
static int motions;
static int buttons;
static int wheels;
static int keys;
static int texts;
static int closes;
static int focus_gains;
static int focus_losses;
static SDL_Window *keyboard_focus;
/* The mouse: SDL's state the driver sees, and what it asked of the window. */
static SDL_VideoDevice *video_device;
static SDL_Mouse mouse;
static SDL_Cursor *default_cursor;
static SDL_Window *mouse_focus;
static int last_relative;
static int last_motion_x;
static int last_motion_y;
static int shape_calls;
static AstraPointerShape last_shape;
static int image_calls;
static AstraHardwarePointerImage last_image;
static AstraColorRGBA8 last_pixels[32 * 32];
static int grab_calls;
static uint32_t last_grab;
static int last_grab_rect;
static AstraWindowFrame last_grab_frame;
static int warps;
static int32_t last_warp_x;
static int32_t last_warp_y;
static AstraResult warp_result = ASTRA_OK;

void *SDL_calloc(size_t count, size_t size) { return calloc(count, size); }
void SDL_free(void *value) { free(value); }
size_t SDL_strlen(const char *value) { return strlen(value); }
void *SDL_memset(void *value, int byte, size_t size)
{
    return memset(value, byte, size);
}
int SDL_SetError(const char *format, ...)
{
    (void)format;
    ++errors;
    return -1;
}
int SDL_Error(SDL_errorcode code) { (void)code; return -1; }
int SDL_AddBasicVideoDisplay(const SDL_DisplayMode *mode)
{
    assert(mode->format == SDL_PIXELFORMAT_RGB565);
    return 0;
}
SDL_bool SDL_AddDisplayMode(SDL_VideoDisplay *item,
                             const SDL_DisplayMode *mode)
{
    assert(item == &display && mode->w == ASTRA_DISPLAY_WIDTH);
    return SDL_TRUE;
}
const AstraStartupInfo *astra_posix_startup(void) { return NULL; }
const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *startup, const char *name)
{
    (void)startup;
    assert(strcmp(name, ASTRA_CAPABILITY_GUI) == 0);
    return has_gui ? &gui_capability : NULL;
}
AstraResult astra_area_create(uint32_t size, uint32_t rights,
                              AstraArea *area)
{
    assert(size == 4096u && (rights & ASTRA_RIGHT_TRANSFER) != 0u);
    area->handle = 42u;
    return ASTRA_OK;
}
AstraResult astra_area_close(AstraArea *area)
{
    assert(area->handle == 42u);
    ++area_closes;
    area->handle = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}
AstraResult astra_window_display(const AstraWindow *window,
                                 AstraDisplay *display)
{
    assert(window->_private_id == 1u);
    display->_private_handle = 9u;
    display->_private_window = 1u;
    return ASTRA_OK;
}
AstraResult astra_display_close(AstraDisplay *display)
{
    assert(display->_private_handle == 9u);
    ++display_closes;
    *display = (AstraDisplay)ASTRA_DISPLAY_INIT;
    return ASTRA_OK;
}
AstraResult astra_window_surface(AstraWindow *window, AstraSurface *surface)
{
    assert(window->_private_id == 1u &&
           surface->_private_handle == ASTRA_INVALID_HANDLE);
    surface->_private_handle = 9u;
    surface->_private_id = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID;
    surface->_private_width = 320u;
    surface->_private_height = 200u;
    return ASTRA_OK;
}
AstraResult astra_surface_close(AstraSurface *surface)
{
    assert(surface->_private_handle == 9u);
    ++surface_closes;
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    return ASTRA_OK;
}
AstraResult astra_display_staging(AstraDisplay *display,
                                  uint32_t minimum_bytes, void **pixels,
                                  uint32_t *bytes)
{
    assert(display->_private_handle == 9u &&
           minimum_bytes == 320u * 200u * 2u);
    *pixels = backing;
    *bytes = sizeof(backing);
    return ASTRA_OK;
}
AstraResult astra_surface_write_staged(const AstraSurface *surface,
                                       const AstraRectI32 *rectangle,
                                       uint32_t offset, uint32_t pitch)
{
    assert(surface->_private_id == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID);
    ++writes;
    last_write = *rectangle;
    last_write_offset = offset;
    last_write_pitch = pitch;
    return ASTRA_OK;
}
AstraResult astra_window_create(uint32_t gui, uint32_t area,
                                const AstraWindowCreateInfo *info,
                                AstraWindow *window)
{
    assert(gui == 7u && area == 42u && info->pitch == 0u);
    assert(info->content_format == ASTRA_WINDOW_CONTENT_SURFACE);
    assert(info->x == 200u && info->y == 100u &&
           info->width == 320u && info->height == 200u);
    assert(info->type == ASTRA_WINDOW_STANDARD);
    if (create_failure)
        return ASTRA_ERROR_IO;
    window->_private_id = 1u;
    return ASTRA_OK;
}
AstraResult astra_window_close(AstraWindow *window)
{
    assert(window->_private_id == 1u);
    ++window_closes;
    window->_private_id = 0u;
    return ASTRA_OK;
}
AstraResult astra_window_activate(AstraWindow *window)
{
    assert(window->_private_id == 1u);
    ++activations;
    return activation_failure ? ASTRA_ERROR_IO : ASTRA_OK;
}
AstraResult astra_window_minimize(AstraWindow *window)
{
    assert(window->_private_id == 1u);
    ++minimizations;
    return ASTRA_OK;
}
AstraResult astra_window_fullscreen(AstraWindow *window)
{
    assert(window->_private_id == 1u);
    ++fullscreens;
    return ASTRA_OK;
}
AstraResult astra_window_restore(AstraWindow *window)
{
    assert(window->_private_id == 1u);
    ++restores;
    return ASTRA_OK;
}
AstraResult astra_window_present_region(AstraWindow *window,
                                         const AstraWindowFrame *damage)
{
    assert(window->_private_id == 1u);
    ++presents;
    last_damage = *damage;
    return ASTRA_OK;
}
AstraResult astra_window_set_title(AstraWindow *window, const char *title,
                                    uint16_t length)
{
    assert(window->_private_id == 1u && title != NULL && length == 4u);
    return ASTRA_OK;
}
AstraResult astra_window_move(AstraWindow *window, uint16_t x, uint16_t y)
{
    assert(window->_private_id == 1u && x == 210u && y == 120u);
    return ASTRA_OK;
}
AstraResult astra_window_set_frame(AstraWindow *window,
                                    const AstraWindowFrame *frame)
{
    assert(window->_private_id == 1u && frame->width == 300u);
    return ASTRA_OK;
}
AstraResult astra_window_event_try(AstraWindow *window,
                                    AstraWindowEvent *event)
{
    (void)window;
    if (event_next < event_count) {
        *event = events[event_next++];
        return ASTRA_OK;
    }
    return ASTRA_ERROR_WOULD_BLOCK;
}
int SDL_SendMouseMotion(SDL_Window *window, SDL_MouseID id, int relative,
                        int x, int y)
{
    assert(window != NULL && id == 0);
    last_relative = relative;
    last_motion_x = x;
    last_motion_y = y;
    ++motions;
    return 0;
}
SDL_VideoDevice *SDL_GetVideoDevice(void) { return video_device; }
SDL_Mouse *SDL_GetMouse(void) { return &mouse; }
void SDL_SetDefaultCursor(SDL_Cursor *cursor) { default_cursor = cursor; }
SDL_Window *SDL_GetMouseFocus(void) { return mouse_focus; }
Uint32 SDL_GetMouseState(int *x, int *y)
{
    assert(x == NULL && y == NULL);
    return SDL_BUTTON_LMASK;
}
int SDL_memcmp(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size);
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
/* The test's surfaces are RGBA32 already: conversion hands back a copy of
   the header over the same pixels. */
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *surface,
                                      Uint32 pixel_format, Uint32 flags)
{
    SDL_Surface *copy = malloc(sizeof(*copy));

    assert(pixel_format == SDL_PIXELFORMAT_RGBA32 && flags == 0u);
    *copy = *surface;
    return copy;
}
void SDL_FreeSurface(SDL_Surface *surface) { free(surface); }
AstraResult astra_window_set_pointer_shape(AstraWindow *window,
                                           AstraPointerShape shape)
{
    assert(window->_private_id == 1u);
    ++shape_calls;
    last_shape = shape;
    return ASTRA_OK;
}
AstraResult astra_window_set_pointer_image(
    AstraWindow *window, const AstraHardwarePointerImage *image)
{
    assert(window->_private_id == 1u && image->size == sizeof(*image));
    ++image_calls;
    last_image = *image;
    for (uint32_t y = 0u; y < image->height; ++y)
        memcpy(&last_pixels[y * 32u],
               (const uint8_t *)image->pixels + y * image->pitch,
               image->width * sizeof(AstraColorRGBA8));
    return ASTRA_OK;
}
AstraResult astra_window_set_pointer_grab(AstraWindow *window,
                                          uint32_t flags,
                                          const AstraWindowFrame *rectangle)
{
    assert(window->_private_id == 1u);
    ++grab_calls;
    last_grab = flags;
    last_grab_rect = rectangle != NULL;
    if (rectangle != NULL)
        last_grab_frame = *rectangle;
    return ASTRA_OK;
}
AstraResult astra_window_warp_pointer(AstraWindow *window, int32_t x,
                                      int32_t y)
{
    assert(window->_private_id == 1u);
    ++warps;
    last_warp_x = x;
    last_warp_y = y;
    return warp_result;
}
int SDL_SendMouseButtonClicks(SDL_Window *window, SDL_MouseID id,
                              Uint8 state, Uint8 button, int clicks)
{
    assert(window != NULL && id == 0 && state == SDL_PRESSED &&
           button == SDL_BUTTON_X1 && clicks == 1);
    ++buttons;
    return 0;
}
int SDL_SendMouseWheel(SDL_Window *window, SDL_MouseID id, float x, float y,
                       SDL_MouseWheelDirection direction)
{
    assert(window != NULL && id == 0 && x == 1.0f && y == -2.0f &&
           direction == SDL_MOUSEWHEEL_NORMAL);
    ++wheels;
    return 0;
}
int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode)
{
    assert(state == SDL_PRESSED && scancode == SDL_SCANCODE_A);
    assert(keyboard_focus != NULL);
    ++keys;
    return 0;
}
int SDL_SendKeyboardText(const char *text)
{
    assert(strcmp(text, "a") == 0);
    ++texts;
    return 0;
}
char *SDL_UCS4ToUTF8(Uint32 codepoint, char *target)
{
    target[0] = (char)codepoint;
    target[1] = '\0';
    return target + 1;
}
int SDL_SendWindowEvent(SDL_Window *window, Uint8 event, int x, int y)
{
    assert(window != NULL);
    if (event == SDL_WINDOWEVENT_CLOSE) {
        assert(x == 0 && y == 0);
        ++closes;
    } else if (event == SDL_WINDOWEVENT_MOVED) {
        assert(x == 200 && y == 100);
    } else if (event == SDL_WINDOWEVENT_RESIZED) {
        assert(x == 320 && y == 200);
    } else {
        assert((event == SDL_WINDOWEVENT_RESTORED ||
                event == SDL_WINDOWEVENT_MINIMIZED) && x == 0 && y == 0);
    }
    return 0;
}
SDL_Window *SDL_GetKeyboardFocus(void) { return keyboard_focus; }
void SDL_SetKeyboardFocus(SDL_Window *window)
{
    if (window != keyboard_focus) {
        if (window != NULL)
            ++focus_gains;
        else
            ++focus_losses;
        keyboard_focus = window;
    }
}
void SDL_SetMouseFocus(SDL_Window *window) { mouse_focus = window; }

/* Every SDL mouse hook the driver sets, against the window it drives. */
static void test_mouse(SDL_VideoDevice *device, SDL_Window *window)
{
    static Uint8 rgba[48][64][4];
    SDL_Surface surface = {0};
    SDL_Cursor *hand;
    SDL_Cursor *image;
    int calls;
    int x = 0;
    int y = 0;

    video_device = device;
    assert(mouse.CreateCursor == ASTRA_CreateCursor &&
           mouse.ShowCursor == ASTRA_ShowCursor &&
           mouse.WarpMouse == ASTRA_WarpMouse &&
           mouse.WarpMouseGlobal == ASTRA_WarpMouseGlobal &&
           mouse.SetRelativeMouseMode == ASTRA_SetRelativeMouseMode &&
           mouse.CaptureMouse == ASTRA_CaptureMouse &&
           mouse.GetGlobalMouseState == ASTRA_GetGlobalMouseState &&
           mouse.FreeCursor == ASTRA_FreeCursor && default_cursor != NULL);
    /* System cursors are the display's own shapes; a hidden cursor is the
       NONE shape; showing what is shown sends nothing. */
    hand = ASTRA_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    assert(hand != NULL && ASTRA_ShowCursor(hand) == 0 &&
           last_shape == ASTRA_POINTER_SHAPE_HAND);
    calls = shape_calls;
    assert(ASTRA_ShowCursor(hand) == 0 && shape_calls == calls);
    assert(ASTRA_ShowCursor(NULL) == 0 && shape_calls == calls + 1 &&
           last_shape == ASTRA_POINTER_SHAPE_NONE);
    {
        static const struct { SDL_SystemCursor id; AstraPointerShape shape; }
            map[] = {
            { SDL_SYSTEM_CURSOR_CROSSHAIR, ASTRA_POINTER_SHAPE_CROSSHAIR },
            { SDL_SYSTEM_CURSOR_WAITARROW, ASTRA_POINTER_SHAPE_PROGRESS },
            { SDL_SYSTEM_CURSOR_SIZEALL, ASTRA_POINTER_SHAPE_MOVE },
            { SDL_SYSTEM_CURSOR_NO, ASTRA_POINTER_SHAPE_NOT_ALLOWED },
            { SDL_SYSTEM_CURSOR_IBEAM, ASTRA_POINTER_SHAPE_TEXT },
        };

        for (size_t at = 0u; at < sizeof(map) / sizeof(map[0]); ++at) {
            SDL_Cursor *cursor = ASTRA_CreateSystemCursor(map[at].id);

            assert(cursor != NULL &&
                   ((ASTRA_CursorData *)cursor->driverdata)->shape ==
                       map[at].shape);
            ASTRA_FreeCursor(cursor);
        }
        assert(ASTRA_CreateSystemCursor(SDL_NUM_SYSTEM_CURSORS) == NULL);
    }
    /* A 64x48 image is scaled to 32x24, its hotspot with it; each pixel
       averages what it covers, transparent pixels adding no colour. */
    for (int row = 0; row < 48; ++row)
        for (int column = 0; column < 64; ++column)
            if (column < 32 || (column < 33 && row < 2)) {
                rgba[row][column][0] = 200u;
                rgba[row][column][3] = 255u;
            }
    surface.w = 64;
    surface.h = 48;
    surface.pitch = 64 * 4;
    surface.pixels = rgba;
    image = ASTRA_CreateCursor(&surface, 40, 20);
    assert(image != NULL && ASTRA_ShowCursor(image) == 0 &&
           image_calls == 1 && last_image.width == 32u &&
           last_image.height == 24u && last_image.hotspot.x == 20 &&
           last_image.hotspot.y == 10);
    assert(last_pixels[0].red == 200u && last_pixels[0].alpha == 255u &&
           last_pixels[31].alpha == 0u &&
           last_pixels[16].red == 200u && last_pixels[16].alpha == 128u);
    assert(ASTRA_CreateCursor(&surface, 64, 0) == NULL);
    ASTRA_FreeCursor(image);
    ASTRA_FreeCursor(hand);

    /* Relative mode locks the pointer and reads the device's own motion:
       the first event sets the baseline, later ones are deltas. */
    assert(ASTRA_SetRelativeMouseMode(SDL_TRUE) == 0 &&
           last_grab == ASTRA_WINDOW_POINTER_LOCK);
    events[0] = (AstraWindowEvent){ .type = ASTRA_WINDOW_EVENT_POINTER_MOTION };
    events[0].data.motion = (AstraWindowMotionEvent){
        .x = 10, .y = 20, .screen_x = 210, .screen_y = 130,
        .motion_x = 100, .motion_y = 50 };
    events[1] = events[0];
    events[1].data.motion.motion_x = 103;
    events[1].data.motion.motion_y = 48;
    events[2] = events[1];
    events[2].data.motion.motion_x = INT32_MIN;
    events[2].data.motion.motion_y = 48;
    event_count = 2u;
    event_next = 0u;
    calls = motions;
    ASTRA_PumpEvents(device);
    assert(motions == calls + 1 && last_relative == 1 &&
           last_motion_x == 3 && last_motion_y == -2 &&
           mouse_focus == window);
    /* The totals wrap; the delta does not see it. */
    ((ASTRA_WindowData *)window->driverdata)->motion_x = INT32_MAX;
    events[0] = events[2];
    event_count = 1u;
    event_next = 0u;
    ASTRA_PumpEvents(device);
    assert(last_relative == 1 && last_motion_x == 1 && last_motion_y == 0);
    assert(ASTRA_SetRelativeMouseMode(SDL_FALSE) == 0 && last_grab == 0u);
    events[0] = events[1];
    event_count = 1u;
    event_next = 0u;
    ASTRA_PumpEvents(device);
    assert(last_relative == 0 && last_motion_x == 10 && last_motion_y == 20);
    assert(ASTRA_GetGlobalMouseState(&x, &y) == SDL_BUTTON_LMASK &&
           x == 210 && y == 130);

    /* A grab confines to the content, a mouse rectangle to itself inside
       the content, a capture takes events from anywhere. */
    window->flags |= SDL_WINDOW_MOUSE_GRABBED;
    ASTRA_SetWindowMouseGrab(device, window, SDL_TRUE);
    assert(last_grab == ASTRA_WINDOW_POINTER_CONFINE && !last_grab_rect);
    window->mouse_rect = (SDL_Rect){ 280, 190, 50, 40 };
    ASTRA_SetWindowMouseRect(device, window);
    assert(last_grab == ASTRA_WINDOW_POINTER_CONFINE && last_grab_rect &&
           last_grab_frame.x == 280u && last_grab_frame.y == 190u &&
           last_grab_frame.width == 20u && last_grab_frame.height == 10u);
    calls = grab_calls;
    ASTRA_SetWindowMouseRect(device, window);
    assert(grab_calls == calls);
    window->mouse_rect = (SDL_Rect){ 0, 0, 0, 0 };
    window->flags &= ~SDL_WINDOW_MOUSE_GRABBED;
    ASTRA_SetWindowMouseGrab(device, window, SDL_FALSE);
    assert(last_grab == 0u);
    assert(ASTRA_CaptureMouse(window) == 0 &&
           last_grab == ASTRA_WINDOW_POINTER_CAPTURE);
    assert(ASTRA_CaptureMouse(NULL) == 0 && last_grab == 0u);

    /* Warp is in content coordinates; a global warp needs the active
       window and where its content is on the screen. */
    ASTRA_WarpMouse(window, -5, 7);
    assert(warps == 1 && last_warp_x == 0 && last_warp_y == 7);
    keyboard_focus = NULL;
    assert(ASTRA_WarpMouseGlobal(250, 150) < 0 && warps == 1);
    keyboard_focus = window;
    assert(ASTRA_WarpMouseGlobal(250, 150) == 0 && warps == 2 &&
           last_warp_x == 50 && last_warp_y == 40);
    warp_result = ASTRA_ERROR_PERMISSION;
    assert(ASTRA_WarpMouseGlobal(250, 150) < 0);
    warp_result = ASTRA_OK;
    keyboard_focus = NULL;
    mouse_focus = NULL;
    event_count = 0u;
    event_next = 0u;
}

int main(void)
{
    SDL_VideoDevice device = {0};
    SDL_VideoDevice *created;
    SDL_Window window = {0};
    Uint32 format = 0u;
    void *pixels = NULL;
    int pitch = 0;
    SDL_Rect damage[] = {{2, 3, 8, 9}, {7, 8, 5, 6}};

    created = ASTRA_CreateDevice();
    assert(created != NULL && created->VideoQuit != NULL &&
           created->SetWindowFullscreen == ASTRA_SetWindowFullscreen &&
           created->SetWindowMouseGrab == ASTRA_SetWindowMouseGrab &&
           created->SetWindowMouseRect == ASTRA_SetWindowMouseRect);
    created->VideoQuit(created);
    created->free(created);
    device.displays = &display;
    assert(ASTRA_VideoInit(&device) < 0); /* no GUI grant */
    has_gui = 1;
    gui_capability.handle = 7u;
    assert(ASTRA_VideoInit(&device) == 0);
    /* As SDL creates a fullscreen window: sized to the display, with the
       frame it returns to in windowed. The native window starts there. */
    window.x = 0;
    window.y = 0;
    window.w = (int)ASTRA_DISPLAY_WIDTH;
    window.h = (int)ASTRA_DISPLAY_HEIGHT;
    window.windowed = (SDL_Rect){ 200, 100, 320, 200 };
    window.title = "Test";
    create_failure = 1;
    assert(ASTRA_CreateWindow(&device, &window) < 0);
    assert(window.driverdata == NULL && area_closes == 1);
    create_failure = 0;
    assert(ASTRA_CreateWindow(&device, &window) == 0);
    /* A new window takes the program's grab: none yet. */
    assert(grab_calls == 0 && shape_calls == 0);
    activation_failure = 1;
    {
        int before = errors;

        ASTRA_ShowWindow(&device, &window);
        assert(errors == before + 1);
    }
    activation_failure = 0;
    ASTRA_ShowWindow(&device, &window);
    ASTRA_HideWindow(&device, &window);
    assert(activations == 2 && minimizations == 1);
    ASTRA_SetWindowFullscreen(&device, &window, &display, SDL_TRUE);
    ASTRA_SetWindowFullscreen(&device, &window, &display, SDL_FALSE);
    assert(fullscreens == 1 && restores == 1);
    window.w = 320;
    window.h = 200;
    assert(ASTRA_CreateWindowFramebuffer(&device, &window, &format,
                                         &pixels, &pitch) == 0);
    assert(format == SDL_PIXELFORMAT_RGB565 && pixels == backing);
    assert(pitch == 320 * 2);
    assert(ASTRA_UpdateWindowFramebuffer(&device, &window, damage, 2) == 0);
    /* The damaged rectangle is written from staging, then presented. */
    assert(writes == 1 && last_write.x == 2 && last_write.y == 3 &&
           last_write.width == 10u && last_write.height == 11u &&
           last_write_pitch == 640u && last_write_offset == 3u * 640u + 4u);
    assert(presents == 1 && last_damage.x == 2u && last_damage.y == 3u &&
           last_damage.width == 10u && last_damage.height == 11u);
    assert(ASTRA_UpdateWindowFramebuffer(&device, &window, NULL, 1) < 0);
    assert(presents == 1 && writes == 1); /* malformed: nothing sent */
    ASTRA_SetWindowTitle(&device, &window);
    window.x = 210;
    window.y = 120;
    ASTRA_SetWindowPosition(&device, &window);
    window.w = 300;
    ASTRA_SetWindowSize(&device, &window);
    events[0].type = ASTRA_WINDOW_EVENT_STATE;
    events[0].data.state.frame = (AstraWindowFrame){200, 100, 320, 200};
    events[0].data.state.flags = ASTRA_WINDOW_ACTIVE;
    events[1].type = ASTRA_WINDOW_EVENT_POINTER_MOTION;
    events[1].data.pointer.x = 10;
    events[1].data.pointer.y = 20;
    events[2].type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    events[2].flags = ASTRA_WINDOW_EVENT_DOWN;
    events[2].data.pointer.button = ASTRA_INPUT_BUTTON_SIDE;
    events[2].data.pointer.click_count = 1u;
    events[3].type = ASTRA_WINDOW_EVENT_POINTER_WHEEL;
    events[3].data.wheel.delta_x = 1;
    events[3].data.wheel.delta_y = -2;
    events[4].type = ASTRA_WINDOW_EVENT_KEY;
    events[4].flags = ASTRA_WINDOW_EVENT_DOWN;
    events[4].data.key.usage = SDL_SCANCODE_A;
    events[5].type = ASTRA_WINDOW_EVENT_KEY;
    events[5].data.key.usage = SDL_NUM_SCANCODES;
    events[6].type = ASTRA_WINDOW_EVENT_TEXT;
    events[6].data.text.codepoint = 'a';
    events[7].type = ASTRA_WINDOW_EVENT_STATE;
    events[7].data.state.frame = (AstraWindowFrame){200, 100, 320, 200};
    events[8].type = ASTRA_WINDOW_EVENT_CLOSE_REQUEST;
    event_count = sizeof(events) / sizeof(events[0]);
    device.windows = &window;
    ASTRA_PumpEvents(&device);
    assert(event_next == event_count && motions == 1 && buttons == 1 &&
           wheels == 1 && keys == 1 && texts == 1 && closes == 1 &&
           focus_gains == 1 && focus_losses == 1 && keyboard_focus == NULL);
    /* The display's fullscreen state, however it came about, is SDL's. */
    assert((window.flags & SDL_WINDOW_FULLSCREEN) == 0u);
    events[0] = (AstraWindowEvent){ .type = ASTRA_WINDOW_EVENT_STATE };
    events[0].data.state.state = ASTRA_WINDOW_STATE_FULLSCREEN;
    events[0].data.state.frame = (AstraWindowFrame){200, 100, 320, 200};
    events[1] = events[0];
    events[1].data.state.state = ASTRA_WINDOW_STATE_MINIMIZED;
    event_count = 2u;
    event_next = 0u;
    ASTRA_PumpEvents(&device);
    assert((window.flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ==
           SDL_WINDOW_FULLSCREEN_DESKTOP);
    events[0].data.state.state = ASTRA_WINDOW_STATE_NORMAL;
    event_count = 1u;
    event_next = 0u;
    ASTRA_PumpEvents(&device);
    assert((window.flags & SDL_WINDOW_FULLSCREEN) == 0u);
    test_mouse(&device, &window);
    ASTRA_DestroyWindow(&device, &window);
    assert(window.driverdata == NULL && window_closes == 1 &&
           surface_closes == 1 && display_closes == 1 && area_closes == 2);
    return 0;
}
