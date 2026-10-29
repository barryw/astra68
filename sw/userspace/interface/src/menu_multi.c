#include <astra/interface_library.h>
#include <astra/runtime.h>
#include <astra/gui.h>
#include <astra/bytes.h>

#include "control_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static int valid(const AstraMultiPopup *popup)
{
    return popup != NULL && popup->_private_size == sizeof(*popup);
}

static uint32_t control_index(const AstraUIContext *context,
    const AstraControl *control)
{
    if (context == NULL || context->_private_controls == NULL)
        return UINT32_MAX;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (&context->_private_controls[at] == control) return at;
    return UINT32_MAX;
}

static void clear(AstraMultiPopup *popup)
{
    popup->_private_request = 0u;
    popup->_private_window = NULL;
    popup->_private_context = NULL;
}

AstraResult astra_interface_multi_popup_init(AstraMultiPopup *popup,
    AstraControl *control, const AstraMultiPopupInfo *info)
{
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraResult result;

    if (!valid(popup) || popup->_private_control != NULL ||
        control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->label_length == 0u ||
        !astra_interface_single_line_text_valid(info->label,
            info->label_length) || info->scope == NULL ||
        info->items == NULL || info->item_count == 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->item_count; ++at)
        if (info->items[at].command == NULL ||
            info->items[at].command_length == 0u ||
            !astra_interface_single_line_text_valid(
                info->items[at].command,
                info->items[at].command_length))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = info->id;
    button.text = info->label;
    button.text_length = info->label_length;
    button.state = info->state;
    result = astra_interface_button_init(control, &button);
    if (result != ASTRA_OK) return result;
    popup->_private_control = control;
    popup->_private_items = info->items;
    popup->_private_item_count = info->item_count;
    popup->_private_scope = info->scope;
    return ASTRA_OK;
}

AstraResult astra_interface_multi_popup_refresh(AstraMultiPopup *popup,
    AstraUIContext *context, uint32_t *checked_count)
{
    uint32_t count = 0u, state;
    AstraResult result;

    if (!valid(popup) || popup->_private_control == NULL ||
        control_index(context, popup->_private_control) == UINT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < popup->_private_item_count; ++at) {
        AstraCommandState command = {0};
        const AstraContextMenuItem *item = &popup->_private_items[at];
        result = astra_interface_command_query(popup->_private_scope,
            item->command, item->command_length, &command);
        if (result != ASTRA_OK) return result;
        if (command.checked != 0u) ++count;
    }
    state = popup->_private_control->_private_state &
        (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED);
    if (count != 0u) state |= ASTRA_CONTROL_SELECTED;
    result = astra_interface_ui_set_state(context, popup->_private_control,
        state);
    if (result == ASTRA_OK && checked_count != NULL)
        *checked_count = count;
    return result;
}

AstraResult astra_interface_multi_popup_open(AstraMultiPopup *popup,
    AstraUIContext *context, AstraWindow *window)
{
    AstraMenuItem *rows;
    AstraControlFrame frame, clip;
    uint64_t bytes;
    uint32_t index, request;
    AstraResult result = ASTRA_OK;

    if (!valid(popup) || popup->_private_control == NULL || window == NULL ||
        (popup->_private_control->_private_state &
         (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED)) != 0u ||
        (index = control_index(context, popup->_private_control)) == UINT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (popup->_private_request != 0u) return ASTRA_ERROR_BUSY;
    if (popup->_private_item_count > UINT32_MAX / sizeof(*rows) ||
        (uint64_t)popup->_private_item_count * ASTRA_GUI_MENU_ROW_BYTES +
            ASTRA_GUI_MENU_HEADER_BYTES > ASTRA_GUI_MENU_BYTES_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    result = astra_interface_ui_geometry(context, index, &frame, &clip);
    if (result != ASTRA_OK) return result;
    rows = astra_runtime_allocate(
        (size_t)popup->_private_item_count * sizeof(*rows));
    if (rows == NULL) return ASTRA_ERROR_NO_RESOURCES;
    memset(rows, 0, (size_t)popup->_private_item_count * sizeof(*rows));
    bytes = ASTRA_GUI_MENU_HEADER_BYTES +
        (uint64_t)popup->_private_item_count * ASTRA_GUI_MENU_ROW_BYTES;
    for (uint32_t at = 0u; at < popup->_private_item_count; ++at) {
        const AstraContextMenuItem *item = &popup->_private_items[at];
        AstraCommandState state = {0};
        result = astra_interface_command_query(popup->_private_scope,
            item->command, item->command_length, &state);
        if (result != ASTRA_OK) break;
        if (state.label_length == 0u ||
            !astra_interface_single_line_text_valid(state.label,
                state.label_length)) {
            result = ASTRA_ERROR_INVALID_ARGUMENT;
            break;
        }
        bytes += state.label_length;
        if (bytes > ASTRA_GUI_MENU_BYTES_MAX) {
            result = ASTRA_ERROR_NO_RESOURCES;
            break;
        }
        rows[at].label = astra_runtime_allocate(state.label_length);
        if (rows[at].label == NULL) {
            result = ASTRA_ERROR_NO_RESOURCES;
            break;
        }
        memcpy((void *)rows[at].label, state.label, state.label_length);
        rows[at].label_length = state.label_length;
        rows[at].flags = (state.enabled ? 0u : ASTRA_MENU_DISABLED) |
            (state.checked ? ASTRA_MENU_CHECKED : 0u);
    }
    request = UINT32_C(0x90000000) |
        ((popup->_private_serial + 1u) & UINT32_C(0x0fffffff));
    if ((request & UINT32_C(0x0fffffff)) == 0u) ++request;
    if (result == ASTRA_OK)
        result = astra_window_menu_open_persistent(window, rows,
            popup->_private_item_count, frame.x,
            frame.y + (int32_t)frame.height, request);
    for (uint32_t at = 0u; at < popup->_private_item_count; ++at)
        astra_runtime_deallocate((void *)rows[at].label);
    astra_runtime_deallocate(rows);
    if (result != ASTRA_OK) return result;
    popup->_private_serial = request & UINT32_C(0x0fffffff);
    popup->_private_request = request;
    popup->_private_window = window;
    popup->_private_context = context;
    return ASTRA_OK;
}

AstraResult astra_interface_multi_popup_handle_event(AstraMultiPopup *popup,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled)
{
    uint32_t selection, count;
    AstraResult result;

    if (handled != NULL) *handled = 0;
    if (!valid(popup) || event == NULL || action == NULL || handled == NULL ||
        event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (event->type != ASTRA_WINDOW_EVENT_MENU_RESULT ||
        popup->_private_request == 0u ||
        event->data.menu_result.request != popup->_private_request)
        return ASTRA_OK;
    *handled = 1;
    *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u) {
        clear(popup);
        return ASTRA_ERROR_IO;
    }
    result = astra_result_from_service(event->data.menu_result.status);
    selection = event->data.menu_result.selection;
    if (result != ASTRA_OK || selection == 0u) {
        if (selection == 0u &&
            (event->flags & ASTRA_WINDOW_EVENT_CONTINUED) != 0u) {
            (void)astra_interface_multi_popup_close(popup);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        clear(popup);
        return result;
    }
    if ((event->flags & ASTRA_WINDOW_EVENT_CONTINUED) == 0u ||
        selection > popup->_private_item_count) {
        clear(popup);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    result = astra_interface_command_invoke(popup->_private_scope,
        popup->_private_items[selection - 1u].command,
        popup->_private_items[selection - 1u].command_length,
        popup->_private_items[selection - 1u].arguments);
    if (result != ASTRA_OK) {
        (void)astra_interface_multi_popup_close(popup);
        return result;
    }
    result = astra_interface_multi_popup_refresh(popup,
        popup->_private_context, &count);
    if (result != ASTRA_OK) {
        (void)astra_interface_multi_popup_close(popup);
        return result;
    }
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = popup->_private_control->_private_id;
    action->value = count;
    return ASTRA_OK;
}

AstraResult astra_interface_multi_popup_close(AstraMultiPopup *popup)
{
    AstraResult result;

    if (!valid(popup)) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (popup->_private_request == 0u) return ASTRA_OK;
    result = astra_window_menu_close(popup->_private_window,
        popup->_private_request);
    if (result == ASTRA_OK) clear(popup);
    return result;
}
