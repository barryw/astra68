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
    assert(window != NULL && id == 0 && relative == 0 && x == 10 && y == 20);
    ++motions;
    return 0;
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
        assert(event == SDL_WINDOWEVENT_RESTORED && x == 0 && y == 0);
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
void SDL_SetMouseFocus(SDL_Window *window) { (void)window; }

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
           created->SetWindowFullscreen == ASTRA_SetWindowFullscreen);
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
    ASTRA_DestroyWindow(&device, &window);
    assert(window.driverdata == NULL && window_closes == 1 &&
           surface_closes == 1 && display_closes == 1 && area_closes == 2);
    return 0;
}
