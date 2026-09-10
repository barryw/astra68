#include <astra/interface_library.h>

#include <astra/bytes.h>
#include <astra/library.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/window.h>

#include "control_internal.h"

#define ALERT_WIDTH 420u
#define ALERT_HEIGHT 150u

ASTRA_LIBRARY("interface.library", 2, 2, 0,
              ASTRA_INTERFACE_LIBRARY_ABI_MAJOR,
              ASTRA_INTERFACE_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

static int valid(const AstraAlertInfo *info)
{
    return info != NULL && info->size >= sizeof(*info) &&
           info->kind >= ASTRA_ALERT_INFORMATION &&
           info->kind <= ASTRA_ALERT_ERROR && info->title != NULL &&
           info->title_length != 0u &&
           info->title_length <= ASTRA_WINDOW_TITLE_MAX &&
           astra_utf8_validate(info->title, info->title_length, 0u) &&
           info->message_length != 0u &&
           astra_utf8_validate(info->message, info->message_length, 0u) &&
           info->button_length != 0u &&
           astra_utf8_validate(info->button, info->button_length, 0u) &&
           info->button_length <= 16u && info->reserved16 == 0u &&
           info->reserved16_2 == 0u && info->reserved16_3 == 0u &&
           astra_words_zero(info->reserved, 4u);
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

static AstraResult alert_controls(const AstraAlertInfo *info,
                                  AstraControl controls[2],
                                  AstraUIContext *context)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    label.id = 1u;
    label.text = info->message;
    label.text_length = info->message_length;
    label.text_role = ASTRA_TEXT_CLIENT_PRIMARY;
    result = astra_interface_label_init(&controls[0], &label);
    if (result != ASTRA_OK) return result;
    button.id = 2u;
    button.text = info->button;
    button.text_length = info->button_length;
    button.variant = info->kind == ASTRA_ALERT_WARNING ?
        ASTRA_BUTTON_WARNING :
        (info->kind == ASTRA_ALERT_ERROR ? ASTRA_BUTTON_DESTRUCTIVE :
                                          ASTRA_BUTTON_PRIMARY);
    result = astra_interface_button_init(&controls[1], &button);
    if (result != ASTRA_OK) return result;
    item.minimum_width = 96u;
    item.minimum_height = 32u;
    item.align_self = ASTRA_FLEX_ALIGN_END;
    result = astra_interface_control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;
    result = astra_interface_ui_init(context, controls, 2u,
                                     ALERT_WIDTH, ALERT_HEIGHT);
    if (result != ASTRA_OK) return result;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.justify = ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = 22u;
    layout.padding_top = 22u;
    layout.padding_right = 20u;
    layout.padding_bottom = 20u;
    return astra_interface_ui_layout(context, &layout);
}

static AstraResult paint(AstraSurfaceView *surface, const AstraAlertInfo *info,
                         const AstraUIContext *context)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraColorRGBA8 signal = info->kind == ASTRA_ALERT_ERROR ? theme.fault :
        (info->kind == ASTRA_ALERT_WARNING ? theme.warning : theme.accent);

    astra_surface_clear(surface, color(theme.client));
    astra_surface_fill(surface, 0, 0, 6u, ALERT_HEIGHT, color(signal));
    return astra_interface_ui_render(context, surface);
}

#if defined(ASTRA_INTERFACE_TEST)
void astra_interface_test_paint(AstraSurfaceView *surface,
                                const AstraAlertInfo *info)
{
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;

    if (alert_controls(info, controls, &context) == ASTRA_OK)
        (void)paint(surface, info, &context);
}
#endif

static AstraResult show_alert(AstraHandle gui, const AstraAlertInfo *info)
{
    AstraSharedSurface surface = {0};
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraControl controls[2] = {ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result;
    uint32_t status;

    if (gui == ASTRA_INVALID_HANDLE || !valid(info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_shared_draw_list_create(&surface, ALERT_WIDTH, ALERT_HEIGHT);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    result = alert_controls(info, controls, &context);
    if (result == ASTRA_OK)
        result = paint(&surface.view, info, &context);
    if (result != ASTRA_OK) {
        (void)astra_shared_surface_close(&surface);
        return result;
    }
    astra_interface_ui_damage_clear(&context);
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
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                        ASTRA_WINDOW_SUBSCRIBE_FOCUS |
                        ASTRA_WINDOW_SUBSCRIBE_KEY;
    result = astra_window_create(gui, surface.area, &create, &window);
    if (result == ASTRA_OK) {
        for (;;) {
            AstraWindowEvent event = {0};
            AstraUIAction action = ASTRA_UI_ACTION_INIT;

            result = astra_window_event_wait(
                &window, &event, ASTRA_DEADLINE_INFINITE);
            if (result != ASTRA_OK ||
                event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
                break;
            result = astra_interface_ui_handle_event(
                &context, &event, &action);
            if (result != ASTRA_OK ||
                action.type == ASTRA_UI_ACTION_ACTIVATE)
                break;
            {
                AstraControlFrame damage;

                if (astra_interface_ui_damage(&context, &damage) == ASTRA_OK) {
                    AstraWindowFrame region = {
                        (uint16_t)damage.x, (uint16_t)damage.y,
                        (uint16_t)damage.width, (uint16_t)damage.height};

                    if (!astra_draw_list_view_init(
                            &surface.view, surface.view.pixels,
                            surface.view.byte_size, ALERT_WIDTH, ALERT_HEIGHT)) {
                        result = ASTRA_ERROR_IO;
                        break;
                    }
                    result = paint(&surface.view, info, &context);
                    if (result == ASTRA_OK)
                        result = astra_window_present_region(&window, &region);
                    if (result != ASTRA_OK)
                        break;
                    astra_interface_ui_damage_clear(&context);
                }
            }
        }
        {
            AstraResult close_result = astra_window_close(&window);
            if (result == ASTRA_OK) result = close_result;
        }
    }
    status = astra_shared_surface_close(&surface);
    if (result == ASTRA_OK && status != ASTRA_SYSCALL_OK)
        result = astra_result_from_syscall(status);
    return result;
}

const AstraInterfaceLibraryV2 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_INTERFACE_LIBRARY_ABI_MAJOR,
    ASTRA_INTERFACE_LIBRARY_ABI_MINOR,
    sizeof(AstraInterfaceLibraryV2),
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
    astra_interface_label_init,
    astra_interface_button_init,
    astra_interface_control_set_flex,
    astra_interface_ui_init,
    astra_interface_ui_measure,
    astra_interface_ui_layout,
    astra_interface_ui_set_state,
    astra_interface_ui_render,
    astra_interface_ui_handle_event,
    astra_interface_ui_damage,
    astra_interface_ui_damage_clear,
    astra_interface_checkbox_init,
    astra_interface_radio_init,
    astra_interface_switch_init,
    astra_interface_slider_init,
    astra_interface_control_set_value,
    astra_interface_control_get_value,
    astra_interface_progress_init,
    astra_interface_progress_set,
    astra_interface_ui_tick,
    astra_interface_control_set_text,
    astra_interface_container_init,
    astra_text_surface_init,
    astra_text_surface_render_cells,
    astra_text_surface_draw_caret,
    astra_text_surface_scroll,
    astra_text_surface_set_blink,
    astra_text_surface_render_grid,
    astra_text_surface_grid_hit_test,
};
