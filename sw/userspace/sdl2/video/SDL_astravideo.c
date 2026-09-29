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
    AstraResult result;

    (void)_this;
    if (gui == NULL || window->x < 0 || window->y < 0 ||
        window->x > UINT16_MAX || window->y > UINT16_MAX ||
        window->w <= 0 || window->h <= 0 ||
        window->w > (int)ASTRA_DISPLAY_WIDTH ||
        window->h > (int)ASTRA_DISPLAY_HEIGHT)
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
    info.x = (uint16_t)window->x;
    info.y = (uint16_t)window->y;
    info.width = (uint16_t)window->w;
    info.height = (uint16_t)window->h;
    info.pitch = 0u;
    info.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    info.flags = (window->flags & SDL_WINDOW_RESIZABLE) != 0u ?
                 ASTRA_WINDOW_RESIZABLE : 0u;
    info.title = window->title;
    info.title_length = ASTRA_TitleLength(window->title);
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_ALL &
                      ~(ASTRA_WINDOW_SUBSCRIBE_VBLANK |
                        ASTRA_WINDOW_SUBSCRIBE_SYSTEM_ACTION);
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
    return 0;
}

static void ASTRA_DestroyWindow(_THIS, SDL_Window *window)
{
    ASTRA_WindowData *data = window->driverdata;
    AstraResult result;

    (void)_this;
    if (data == NULL)
        return;
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
                SDL_SendMouseMotion(window, 0, 0, event.data.pointer.x,
                                    event.data.pointer.y);
                break;
            case ASTRA_WINDOW_EVENT_POINTER_BUTTON: {
                Uint8 button = (Uint8)event.data.pointer.button;

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
    device->PumpEvents = ASTRA_PumpEvents;
    device->free = ASTRA_DeleteDevice;
    return device;
}

VideoBootStrap ASTRA_bootstrap = {
    "astra", "Astra native window and RGB565 video", ASTRA_CreateDevice, NULL
};

#endif
