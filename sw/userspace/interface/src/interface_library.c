#include <astra/interface_library.h>

#include <astra/library.h>
#include <astra/runtime.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/window.h>

#include "internal/status.h"
#include "internal/syscall.h"

#define ALERT_WIDTH 420u
#define ALERT_HEIGHT 150u
#define ALERT_BUTTON_X 304
#define ALERT_BUTTON_Y 98
#define ALERT_BUTTON_WIDTH 96u
#define ALERT_BUTTON_HEIGHT 32u

ASTRA_LIBRARY("interface.library", 1, 1, 0,
              ASTRA_INTERFACE_LIBRARY_ABI_MAJOR,
              ASTRA_INTERFACE_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

static int words_zero(const uint32_t *words, uint32_t count)
{
    for (uint32_t at = 0u; at < count; ++at)
        if (words[at] != 0u) return 0;
    return 1;
}

static int valid(const AstraAlertInfo *info)
{
    return info != NULL && info->size >= sizeof(*info) &&
           info->kind >= ASTRA_ALERT_INFORMATION &&
           info->kind <= ASTRA_ALERT_ERROR && info->title != NULL &&
           info->title_length != 0u &&
           info->title_length <= ASTRA_WINDOW_TITLE_MAX &&
           info->message != NULL && info->message_length != 0u &&
           info->button != NULL && info->button_length != 0u &&
           info->button_length <= 16u && info->reserved16 == 0u &&
           info->reserved16_2 == 0u && info->reserved16_3 == 0u &&
           words_zero(info->reserved, 4u);
}

#if defined(ASTRA_INTERFACE_TEST)
int astra_interface_test_valid(const AstraAlertInfo *info)
{
    return valid(info);
}
#endif

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static void paint(AstraSurfaceView *surface, const AstraAlertInfo *info)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraColorRGBA8 signal = info->kind == ASTRA_ALERT_ERROR ? theme.fault :
        (info->kind == ASTRA_ALERT_WARNING ? theme.warning : theme.accent);
    uint32_t button_text_width = astra_surface_ui_text_width(
        info->button, info->button_length, theme.body_font_height);

    astra_surface_clear(surface, color(theme.client));
    astra_surface_fill(surface, 0, 0, 6u, ALERT_HEIGHT, color(signal));
    astra_surface_ui_text(surface, 22, 30, info->message,
                          astra_surface_ui_text_fit(
                              info->message, info->message_length,
                              theme.body_font_height, ALERT_WIDTH - 44u),
                          theme.body_font_height, color(theme.frame));
    astra_surface_fill_round(surface, ALERT_BUTTON_X, ALERT_BUTTON_Y,
                             ALERT_BUTTON_WIDTH, ALERT_BUTTON_HEIGHT,
                             theme.control_radius, color(theme.accent));
    astra_surface_ui_text(
        surface,
        ALERT_BUTTON_X + (int32_t)(ALERT_BUTTON_WIDTH - button_text_width) / 2,
        ALERT_BUTTON_Y + 10, info->button, info->button_length,
        theme.body_font_height, color(theme.text_primary));
}

#if defined(ASTRA_INTERFACE_TEST)
void astra_interface_test_paint(AstraSurfaceView *surface,
                                const AstraAlertInfo *info)
{
    paint(surface, info);
}
#endif

static AstraResult show_alert(AstraHandle gui, const AstraAlertInfo *info)
{
    AstraSharedSurface surface = {0};
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result;
    uint32_t status;

    if (gui == ASTRA_INVALID_HANDLE || !valid(info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_shared_draw_list_create(&surface, ALERT_WIDTH, ALERT_HEIGHT);
    if (status != ASTRA_SYSCALL_OK)
        return astra_internal_result(status);
    paint(&surface.view, info);
    create.flags = ASTRA_WINDOW_MODAL | ASTRA_WINDOW_ACTIVE;
    create.x = 430u;
    create.y = 250u;
    create.width = ALERT_WIDTH;
    create.height = ALERT_HEIGHT;
    create.gadgets = ASTRA_WINDOW_GADGET_CLOSE;
    create.type = ASTRA_WINDOW_DIALOG;
    create.title = info->title;
    create.title_length = info->title_length;
    create.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    create.pitch = 0u;
    create.event_mask = ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST |
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON;
    result = astra_window_create(gui, surface.area, &create, &window);
    if (result == ASTRA_OK) {
        for (;;) {
            AstraWindowEvent event = {0};

            result = astra_window_event_wait(
                &window, &event, ASTRA_DEADLINE_INFINITE);
            if (result != ASTRA_OK ||
                event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST ||
                (event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
                 (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
                 event.data.pointer.x >= ALERT_BUTTON_X &&
                 event.data.pointer.x < ALERT_BUTTON_X +
                                                (int32_t)ALERT_BUTTON_WIDTH &&
                 event.data.pointer.y >= ALERT_BUTTON_Y &&
                 event.data.pointer.y < ALERT_BUTTON_Y +
                                               (int32_t)ALERT_BUTTON_HEIGHT))
                break;
        }
        {
            AstraResult close_result = astra_window_close(&window);
            if (result == ASTRA_OK) result = close_result;
        }
    }
    status = astra_shared_surface_close(&surface);
    if (result == ASTRA_OK && status != ASTRA_SYSCALL_OK)
        result = astra_internal_result(status);
    return result;
}

const AstraInterfaceLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_INTERFACE_LIBRARY_ABI_MAJOR,
    ASTRA_INTERFACE_LIBRARY_ABI_MINOR,
    sizeof(AstraInterfaceLibraryV1),
    show_alert,
    astra_window_create,
    astra_window_get_info,
    astra_window_set_frame,
    astra_window_move,
    astra_window_resize,
    astra_window_raise,
    astra_window_lower,
    astra_window_activate,
    astra_window_deactivate,
    astra_window_minimize,
    astra_window_maximize,
    astra_window_restore,
    astra_window_set_title,
    astra_window_set_event_mask,
    astra_window_present,
    astra_window_present_region,
    astra_window_close,
    astra_window_event_try,
    astra_window_event_wait,
    astra_window_event_wait_handle,
};
