#include <astra/interface_library.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/fonts.h>
#include <astra/library.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/window.h>
#include <string.h>

#include "control_internal.h"

#define ALERT_WIDTH 420u
#define ALERT_HEIGHT 150u
#define ALERT_LEFT 22u
#define ALERT_TOP 22u
#define ALERT_RIGHT 20u
#define ALERT_BOTTOM 20u
#define ALERT_GAP 12u
#define ALERT_BUTTON_HEIGHT 32u
#define ALERT_SCROLLBAR_WIDTH 12u
#define ALERT_BODY_GAP 4u
#define ALERT_BODY_HEIGHT (ALERT_HEIGHT - ALERT_TOP - ALERT_BOTTOM - ALERT_GAP - ALERT_BUTTON_HEIGHT)
#define ALERT_TEXT_WIDTH (ALERT_WIDTH - ALERT_LEFT - ALERT_RIGHT - ALERT_SCROLLBAR_WIDTH - ALERT_BODY_GAP)
#define ALERT_CONTROL_COUNT 7u

typedef struct AlertMessage {
    AstraTextModel model;
    AstraScrollModel scroll;
    void *storage;
} AlertMessage;

ASTRA_DYNAMIC_LIBRARY("interface.library.12", 12, 14, 0,
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
           info->reserved16 == 0u &&
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

static AstraResult alert_message(const AstraAlertInfo *info, AlertMessage *message)
{
    uint32_t initial_line = 0u;
    AstraTextModel empty = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelRequirements needed = ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;
    model.metadata_arena = &initial_line;
    model.metadata_arena_bytes = sizeof(initial_line);
    AstraResult result = astra_text_model_init(&empty, &model);
    if (result != ASTRA_OK) return result;
    result = astra_text_model_replace_requirements(&empty, 0u, 0u,
        info->message, info->message_length, &needed);
    if (result != ASTRA_OK) return result;
    uint32_t content = (needed.content_arena_bytes + 3u) & ~UINT32_C(3);
    if (content < needed.content_arena_bytes || content > UINT32_MAX - needed.metadata_arena_bytes)
        return ASTRA_ERROR_OUT_OF_MEMORY;
    void *storage = astra_runtime_allocate(content + needed.metadata_arena_bytes);
    if (storage == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    *message = (AlertMessage){.model = ASTRA_TEXT_MODEL_INIT,
        .scroll = ASTRA_SCROLL_MODEL_INIT, .storage = storage};
    model.text = info->message; model.text_bytes = info->message_length;
    model.content_arena = storage; model.content_arena_bytes = content;
    model.metadata_arena = (uint8_t *)storage + content;
    model.metadata_arena_bytes = needed.metadata_arena_bytes;
    result = astra_text_model_init(&message->model, &model);
    AstraScrollModelInfo scrolling = ASTRA_SCROLL_MODEL_INFO_INIT;
    scrolling.line_height = ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT + 4u;
    if (result == ASTRA_OK) result = astra_scroll_init(&message->scroll, &scrolling);
    if (result != ASTRA_OK) {
        astra_runtime_deallocate(storage);
        message->storage = NULL;
    }
    return result;
}

static AstraResult alert_controls(const AstraAlertInfo *info,
    const char *cancel, uint16_t cancel_length, AlertMessage *message,
    AstraControl controls[ALERT_CONTROL_COUNT], AstraUIContext *context)
{
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraTextViewInfo text = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraScrollbarInfo bar = ASTRA_SCROLLBAR_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    row.id = 1u; row.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    row.layout.main_gap = ALERT_BODY_GAP;
    AstraResult result = astra_interface_container_init(&controls[0], &row);
    item.minimum_height = item.maximum_height = ALERT_BODY_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[0], &item);
    view.id = 5u; view.model = &message->scroll;
    view.preferred_width = ALERT_TEXT_WIDTH; view.preferred_height = ALERT_BODY_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_scroll_view_init(&controls[1], &view);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 1u; item.grow = 1u;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[1], &item);
    text.id = 6u; text.model = &message->model;
    if (result == ASTRA_OK) result = astra_interface_text_view_init(&controls[2], &text);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 5u;
    item.minimum_width = item.maximum_width = ALERT_TEXT_WIDTH;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[2], &item);
    bar.id = 7u; bar.model = &message->scroll;
    if (result == ASTRA_OK) result = astra_interface_scrollbar_init(&controls[3], &bar);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 1u; item.minimum_width = item.maximum_width = ALERT_SCROLLBAR_WIDTH;
    item.maximum_height = ALERT_BODY_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[3], &item);
    row = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    row.id = 4u; row.layout.justify = ASTRA_FLEX_JUSTIFY_END;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER; row.layout.main_gap = 10u;
    if (result == ASTRA_OK) result = astra_interface_container_init(&controls[4], &row);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_height = item.maximum_height = ALERT_BUTTON_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[4], &item);
    uint32_t affirmative = cancel != NULL ? 6u : 5u;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 4u; item.minimum_width = 96u; item.minimum_height = ALERT_BUTTON_HEIGHT;
    if (cancel != NULL) {
        button.id = 3u; button.text = cancel; button.text_length = cancel_length;
        if (result == ASTRA_OK) result = astra_interface_button_init(&controls[5], &button);
        if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[5], &item);
    }
    button.id = 2u; button.text = info->button; button.text_length = info->button_length;
    button.variant = info->kind == ASTRA_ALERT_ERROR ? ASTRA_BUTTON_DESTRUCTIVE :
        info->kind == ASTRA_ALERT_WARNING ? ASTRA_BUTTON_WARNING : ASTRA_BUTTON_PRIMARY;
    if (result == ASTRA_OK) result = astra_interface_button_init(&controls[affirmative], &button);
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&controls[affirmative], &item);
    if (result == ASTRA_OK) result = astra_interface_ui_init(context, controls, affirmative + 1u,
        ALERT_WIDTH, ALERT_HEIGHT);
    if (result == ASTRA_OK) result = astra_interface_text_view_set_mode(context, &controls[2], ASTRA_TEXT_SURFACE_FLOW);
    if (result == ASTRA_OK) result = astra_interface_text_view_set_wrap_width(context, &controls[2], ALERT_TEXT_WIDTH);
    if (result != ASTRA_OK) return result;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.main_gap = ALERT_GAP;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = ALERT_LEFT;
    layout.padding_top = ALERT_TOP;
    layout.padding_right = ALERT_RIGHT;
    layout.padding_bottom = ALERT_BOTTOM;
    return astra_interface_ui_layout(context, &layout);
}

#if defined(ASTRA_INTERFACE_TEST)
int astra_interface_test_confirm_layout(const AstraAlertInfo *info,
    int32_t *button_x, int32_t *button_y)
{
    AstraControl controls[ALERT_CONTROL_COUNT] = {0};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AlertMessage message = {0};
    if (!valid(info) || alert_message(info, &message) != ASTRA_OK) return 0;
    AstraResult result = alert_controls(info, "Cancel", 6u, &message, controls, &context);
    astra_runtime_deallocate(message.storage);
    if (result != ASTRA_OK) return 0;
    if (button_x != NULL)
        *button_x = controls[6]._private_frame.x +
            (int32_t)(controls[6]._private_frame.width / 2u);
    if (button_y != NULL)
        *button_y = controls[6]._private_frame.y +
            (int32_t)(controls[6]._private_frame.height / 2u);
    return controls[5]._private_frame.y == controls[6]._private_frame.y &&
        (int64_t)controls[6]._private_frame.y + controls[6]._private_frame.height <= ALERT_HEIGHT &&
        (int64_t)controls[5]._private_frame.x + controls[5]._private_frame.width <= controls[6]._private_frame.x &&
        (int64_t)controls[6]._private_frame.x + controls[6]._private_frame.width <= ALERT_WIDTH;
}
#endif

static AstraResult paint(AstraSurfaceView *surface, const AstraAlertInfo *info,
                         AstraUIContext *context)
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
    AstraControl controls[ALERT_CONTROL_COUNT] = {0};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AlertMessage message = {0};
    if (alert_message(info, &message) != ASTRA_OK) return;
    if (alert_controls(info, NULL, 0u, &message, controls, &context) == ASTRA_OK)
        (void)paint(surface, info, &context);
    astra_runtime_deallocate(message.storage);
}
#endif

typedef struct AlertState {
    AstraHandle gui;
    AstraAlertInfo info;
    const char *cancel;
    uint16_t cancel_length;
    uint32_t created;
    uint32_t applied_pointer_shape;
    AstraSharedSurface surface;
    AstraWindow window;
    AlertMessage message;
    AstraControl controls[ALERT_CONTROL_COUNT];
    AstraUIContext context;
} AlertState;

static AstraResult alert_render(AlertState *state, const AstraWindowFrame *region)
{
    AstraSurfaceView *surface = &state->surface.view;
    if (!astra_draw_list_view_init(surface, surface->pixels, surface->byte_size,
            ALERT_WIDTH, ALERT_HEIGHT)) return ASTRA_ERROR_IO;
    AstraResult result = paint(surface, &state->info, &state->context);
    if (result == ASTRA_OK && state->created)
        result = astra_window_present_region(&state->window, region);
    if (result == ASTRA_OK) astra_interface_ui_damage_clear(&state->context);
    return result;
}

AstraResult astra_interface_alert_close(AstraAlert *alert)
{
    if (alert == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AlertState *state = alert->_private_state;
    if (state == NULL) return ASTRA_OK;
    if (state->created) {
        AstraResult result = astra_window_close(&state->window);
        if (result != ASTRA_OK) return result;
        state->created = 0u;
    }
    if (state->surface.area != 0u) {
        uint32_t status = astra_shared_surface_close(&state->surface);
        if (status != ASTRA_SYSCALL_OK) return astra_result_from_syscall(status);
    }
    astra_runtime_deallocate(state->message.storage);
    astra_runtime_deallocate(state);
    alert->_private_state = NULL;
    return ASTRA_OK;
}

AstraHandle astra_interface_alert_wait_handle(const AstraAlert *alert)
{
    const AlertState *state = alert == NULL ? NULL : alert->_private_state;
    return state == NULL || !state->created ? ASTRA_INVALID_HANDLE :
        astra_window_event_wait_handle(&state->window);
}

AstraResult astra_interface_alert_update(AstraAlert *alert,
    const AstraAlertInfo *info)
{
    AlertState *state = alert == NULL ? NULL : alert->_private_state;
    if (state == NULL || !valid(info)) return ASTRA_ERROR_INVALID_ARGUMENT;
    AlertMessage replacement = {0};
    AstraResult result = alert_message(info, &replacement);
    if (result != ASTRA_OK) return result;
    if (state->created) {
        result = astra_window_set_title(&state->window, info->title, info->title_length);
        if (result != ASTRA_OK) { astra_runtime_deallocate(replacement.storage); return result; }
    }
    AlertMessage previous_message = state->message;
    AstraControl previous_controls[ALERT_CONTROL_COUNT];
    memcpy(previous_controls, state->controls, sizeof(previous_controls));
    AstraUIContext previous_context = state->context;
    uint32_t affirmative = state->cancel != NULL ? 6u : 5u;
    uint32_t disabled = state->controls[affirmative]._private_state & ASTRA_CONTROL_DISABLED;
    state->message = replacement;
    result = alert_controls(info, state->cancel, state->cancel_length,
        &state->message, state->controls, &state->context);
    if (result == ASTRA_OK && disabled != 0u)
        result = astra_interface_ui_set_state(&state->context,
            &state->controls[affirmative], disabled);
    if (result != ASTRA_OK) {
        state->message = previous_message;
        memcpy(state->controls, previous_controls, sizeof(previous_controls));
        state->context = previous_context;
        astra_runtime_deallocate(replacement.storage);
        return result;
    }
    astra_runtime_deallocate(previous_message.storage);
    state->info = *info;
    const AstraWindowFrame region = {0u, 0u, ALERT_WIDTH, ALERT_HEIGHT};
    return alert_render(state, &region);
}

AstraResult astra_interface_alert_open(AstraHandle gui,
    const AstraAlertInfo *info, const char *cancel, uint16_t cancel_length,
    uint32_t flags, AstraAlert *alert)
{
    if (alert == NULL || alert->_private_state != NULL ||
        gui == ASTRA_INVALID_HANDLE || !valid(info) ||
        (flags & ~ASTRA_WINDOW_MODAL) != 0u ||
        (cancel == NULL ? cancel_length != 0u :
            cancel_length == 0u || !astra_utf8_validate(cancel, cancel_length, 0u)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_fonts_use_system(gui, NULL);
    if (result != ASTRA_OK) return result;
    AlertState *state = astra_runtime_allocate(sizeof(*state));
    if (state == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    memset(state, 0, sizeof(*state));
    state->gui = gui;
    state->cancel = cancel;
    state->cancel_length = cancel_length;
    state->window = (AstraWindow)ASTRA_WINDOW_INIT;
    state->applied_pointer_shape = UINT32_MAX;
    alert->_private_state = state;
    uint32_t status = astra_shared_draw_list_create(&state->surface, ALERT_WIDTH, ALERT_HEIGHT);
    result = astra_result_from_syscall(status);
    if (result == ASTRA_OK) result = astra_interface_alert_update(alert, info);
    AstraWindowCreateInfo create = ASTRA_WINDOW_CREATE_INFO_INIT;
    create.flags = flags;
    create.x = (ASTRA_DISPLAY_WIDTH - ALERT_WIDTH) / 2u;
    create.y = (ASTRA_DISPLAY_HEIGHT - ALERT_HEIGHT) / 2u;
    create.width = ALERT_WIDTH;
    create.height = ALERT_HEIGHT;
    create.type = ASTRA_WINDOW_DIALOG;
    create.title = info->title;
    create.title_length = info->title_length;
    create.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    create.event_mask = ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST |
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                        ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                        ASTRA_WINDOW_SUBSCRIBE_STATE |
                        ASTRA_WINDOW_SUBSCRIBE_KEY;
    if (result == ASTRA_OK) {
        result = astra_window_create(gui, state->surface.area, &create, &state->window);
        state->created = result == ASTRA_OK;
    }
    if (result == ASTRA_OK)
        result = astra_window_set_application_name(&state->window, "Astra", 5u);
    if (result == ASTRA_OK) result = astra_window_activate(&state->window);
    if (result != ASTRA_OK) {
        AstraResult closed = astra_interface_alert_close(alert);
        if (closed != ASTRA_OK) return closed;
    }
    return result;
}

static AstraResult alert_repaint_damage(AlertState *state)
{
    AstraControlFrame damage;
    if (astra_interface_ui_damage(&state->context, &damage) != ASTRA_OK) return ASTRA_OK;
    const AstraWindowFrame region = {(uint16_t)damage.x, (uint16_t)damage.y,
        (uint16_t)damage.width, (uint16_t)damage.height};
    return alert_render(state, &region);
}

static AstraResult alert_dispatch(AlertState *state,
    const AstraWindowEvent *event, uint32_t *choice)
{
    if (event->type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) {
        *choice = ASTRA_ALERT_CANCELLED;
        return ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
        AstraResult result = astra_fonts_use_system(state->gui, event);
        if (result != ASTRA_OK) return result;
    }
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraResult result = astra_interface_ui_handle_event(&state->context, event, &action);
    if (result == ASTRA_OK)
        result = astra_interface_ui_update_pointer(&state->context, &state->window,
            ASTRA_POINTER_SHAPE_AUTOMATIC, &state->applied_pointer_shape);
    if (result != ASTRA_OK) return result;
    if (action.type == ASTRA_UI_ACTION_ACTIVATE) {
        *choice = action.control_id == 2u ? ASTRA_ALERT_ACCEPTED : ASTRA_ALERT_CANCELLED;
        return ASTRA_OK;
    }
    return alert_repaint_damage(state);
}

AstraResult astra_interface_alert_set_enabled(AstraAlert *alert, uint32_t enabled)
{
    AlertState *state = alert == NULL ? NULL : alert->_private_state;
    if (state == NULL || !state->created || enabled > 1u) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraControl *button = &state->controls[state->cancel != NULL ? 6u : 5u];
    uint32_t flags = (button->_private_state & ~ASTRA_CONTROL_DISABLED) |
        (enabled ? 0u : ASTRA_CONTROL_DISABLED);
    AstraResult result = astra_interface_ui_set_state(&state->context, button, flags);
    if (result != ASTRA_OK) return result;
    return alert_repaint_damage(state);
}

AstraResult astra_interface_alert_poll(AstraAlert *alert, uint32_t *choice)
{
    AlertState *state = alert == NULL ? NULL : alert->_private_state;
    if (choice == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    *choice = ASTRA_ALERT_PENDING;
    if (state == NULL || !state->created) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraWindowEvent event = {0};
    AstraResult result = astra_window_event_try(&state->window, &event);
    return result == ASTRA_OK ? alert_dispatch(state, &event, choice) : result;
}

static AstraResult run_alert(AstraHandle gui, const AstraAlertInfo *info,
    const char *cancel, uint16_t cancel_length, uint32_t *confirmed)
{
    AstraAlert alert = ASTRA_ALERT_INIT;
    if (confirmed != NULL) *confirmed = 0u;
    AstraResult result = astra_interface_alert_open(gui, info, cancel,
        cancel_length, ASTRA_WINDOW_MODAL, &alert);
    if (result == ASTRA_OK) {
        AlertState *state = alert._private_state;
        uint32_t choice = ASTRA_ALERT_PENDING;
        while (choice == ASTRA_ALERT_PENDING) {
            AstraWindowEvent event = {0};
            result = astra_window_event_wait(&state->window, &event, ASTRA_DEADLINE_INFINITE);
            if (result == ASTRA_OK) result = alert_dispatch(state, &event, &choice);
            if (result != ASTRA_OK) break;
        }
        if (confirmed != NULL && result == ASTRA_OK)
            *confirmed = choice == ASTRA_ALERT_ACCEPTED;
    }
    AstraResult closed = astra_interface_alert_close(&alert);
    return result == ASTRA_OK ? closed : result;
}

AstraResult astra_interface_show_alert(AstraHandle gui,
    const AstraAlertInfo *info)
{
    return run_alert(gui, info, NULL, 0u, NULL);
}

AstraResult astra_interface_confirm(AstraHandle gui,
    const AstraAlertInfo *info, const char *cancel,
    uint16_t cancel_length, uint32_t *confirmed)
{
    if (confirmed == NULL || cancel == NULL || cancel_length == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return run_alert(gui, info, cancel, cancel_length, confirmed);
}
