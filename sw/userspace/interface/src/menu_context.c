#include <astra/menu.h>
#include <astra/interface_library.h>
#include <astra/runtime.h>
#include <astra/result.h>

#include "control_internal.h"

#include <stddef.h>
#include <astra/bytes.h>

static int valid(const AstraContextMenu *menu)
{
    return menu != NULL && menu->_private_size == sizeof(*menu);
}

static void clear(AstraContextMenu *menu)
{
    uint32_t serial = menu->_private_serial;
    *menu = (AstraContextMenu)ASTRA_CONTEXT_MENU_INIT;
    menu->_private_serial = serial;
}

AstraResult astra_interface_context_menu_open_tree(AstraContextMenu *menu,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, const uint16_t *parents,
    uint32_t count, int32_t x, int32_t y)
{
    AstraMenuItem *rows;
    AstraResult result = ASTRA_OK;
    uint32_t request;
    uint32_t roots = 0u, last_parent = 0u;
    uint64_t length = ASTRA_GUI_MENU_HEADER_BYTES + (uint64_t)count * ASTRA_GUI_MENU_ROW_BYTES;

    if (!valid(menu) || window == NULL || scope == NULL || items == NULL ||
        count == 0u || length > ASTRA_GUI_MENU_BYTES_MAX || count > UINT32_MAX / sizeof(*rows) ||
        (menu->_private_request != 0u && menu->_private_window != window))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at) {
        uint32_t parent = parents == NULL ? 0u : parents[at];
        if (parent == 0u) {
            if (last_parent != 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
            ++roots;
        } else if (parent > roots || parent < last_parent || items[at].command == NULL)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (parent != 0u) last_parent = parent;
    }
    if (roots == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    rows = astra_runtime_allocate((size_t)count * sizeof(*rows));
    if (rows == NULL) return ASTRA_ERROR_NO_RESOURCES;
    memset(rows, 0, (size_t)count * sizeof(*rows));
    for (uint32_t at = 0u; at < count; ++at) {
        AstraCommandState state = {0};
        if (items[at].command == NULL) {
            if (items[at].command_length != 0u || items[at].arguments != NULL) {
                result = ASTRA_ERROR_INVALID_ARGUMENT;
                break;
            }
            rows[at].flags = ASTRA_MENU_SEPARATOR;
            continue;
        }
        result = astra_interface_command_query(scope, items[at].command,
                                               items[at].command_length, &state);
        if (result != ASTRA_OK) break;
        if (state.label_length > ASTRA_GUI_MENU_BYTES_MAX - length) {
            result = ASTRA_ERROR_NO_RESOURCES;
            break;
        }
        length += state.label_length;
        if (state.label_length == 0u ||
            !astra_interface_single_line_text_valid(state.label, state.label_length)) {
            result = ASTRA_ERROR_INVALID_ARGUMENT;
            break;
        }
        /* Query labels may share mutable scratch space: snapshot each now. */
        rows[at].label = astra_runtime_allocate(state.label_length);
        if (rows[at].label == NULL) {
            result = ASTRA_ERROR_NO_RESOURCES;
            break;
        }
        memcpy((void *)rows[at].label, state.label, state.label_length);
        rows[at].label_length = state.label_length;
        rows[at].flags = (state.enabled ? 0u : ASTRA_MENU_DISABLED) |
                         (state.checked ? ASTRA_MENU_CHECKED : 0u) |
                         ASTRA_MENU_PARENT(parents == NULL ? 0u : parents[at]);
    }
    request = menu->_private_serial + 1u;
    if (request == 0u) request = 1u;
    if (result == ASTRA_OK)
        result = astra_window_menu_open(window, rows, count, x, y, request);
    for (uint32_t at = 0u; at < count; ++at)
        astra_runtime_deallocate((void *)rows[at].label);
    astra_runtime_deallocate(rows);
    if (result != ASTRA_OK) return result;
    menu->_private_serial = request;
    menu->_private_request = request;
    menu->_private_window = window;
    menu->_private_scope = scope;
    menu->_private_items = items;
    menu->_private_count = count;
    return ASTRA_OK;
}

AstraResult astra_interface_context_menu_open(AstraContextMenu *menu,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, uint32_t count, int32_t x, int32_t y)
{
    return astra_interface_context_menu_open_tree(menu, window, scope,
        items, NULL, count, x, y);
}

AstraResult astra_interface_context_menu_handle_event(AstraContextMenu *menu,
    const AstraWindowEvent *event, int *handled)
{
    const AstraCommandScope *scope;
    AstraContextMenuItem item;

    if (handled != NULL) *handled = 0;
    if (!valid(menu) || event == NULL || handled == NULL ||
        event->size < sizeof(*event) || event->version != ASTRA_WINDOW_EVENT_VERSION)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *handled = event->type == ASTRA_WINDOW_EVENT_MENU_RESULT &&
        menu->_private_request != 0u &&
        event->data.menu_result.request == menu->_private_request;
    if (!*handled) return ASTRA_OK;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u) {
        clear(menu);
        return ASTRA_ERROR_IO;
    }
    if (event->data.menu_result.status != ASTRA_STATUS_OK) {
        clear(menu);
        return astra_result_from_service(event->data.menu_result.status);
    }
    if (event->data.menu_result.selection == 0u) {
        clear(menu);
        return ASTRA_OK;
    }
    if (event->data.menu_result.selection > menu->_private_count) {
        clear(menu);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    scope = menu->_private_scope;
    item = menu->_private_items[event->data.menu_result.selection - 1u];
    clear(menu);
    if (item.command == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_interface_command_invoke(scope, item.command,
                                          item.command_length, item.arguments);
}

AstraResult astra_interface_context_menu_close(AstraContextMenu *menu)
{
    AstraResult result;
    if (!valid(menu)) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (menu->_private_request == 0u) return ASTRA_OK;
    result = astra_window_menu_close(menu->_private_window, menu->_private_request);
    if (result == ASTRA_OK) {
        clear(menu);
        return ASTRA_OK;
    }
    return result;
}

static int popup_valid(const AstraPopup *popup)
{
    return popup != NULL && popup->_private_size == sizeof(*popup);
}

static void popup_clear(AstraPopup *popup)
{
    uint32_t serial = popup->_private_serial;
    *popup = (AstraPopup)ASTRA_POPUP_INIT;
    popup->_private_serial = serial;
}

AstraResult astra_interface_popup_open(AstraPopup *popup,
    AstraUIContext *context, AstraControl *control, AstraWindow *window)
{
    AstraControlFrame frame, clip;
    const AstraChoiceItem *items;
    AstraMenuItem *rows;
    AstraResult result;
    uint32_t index, count, serial, request;
    int64_t selected;
    int64_t anchor_y;

    if (!popup_valid(popup) || context == NULL || control == NULL ||
        window == NULL ||
        control->_private_control_kind != ASTRA_CONTROL_POPUP ||
        (control->_private_state & (ASTRA_CONTROL_DISABLED |
                                    ASTRA_CONTROL_COLLAPSED)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (popup->_private_request != 0u) return ASTRA_ERROR_BUSY;
    for (index = 0u; index < context->_private_control_count; ++index)
        if (&context->_private_controls[index] == control) break;
    if (index == context->_private_control_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_interface_ui_geometry(context, index, &frame, &clip);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_get_value(control, &selected, NULL);
    if (result != ASTRA_OK) return result;
    count = control->_private_text_length;
    if (count == 0u || count > UINT32_MAX / sizeof(*rows) ||
        (uint64_t)count * ASTRA_GUI_MENU_ROW_BYTES +
            ASTRA_GUI_MENU_HEADER_BYTES > ASTRA_GUI_MENU_BYTES_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    items = (const AstraChoiceItem *)(const void *)control->_private_text;
    rows = astra_runtime_allocate((size_t)count * sizeof(*rows));
    if (rows == NULL) return ASTRA_ERROR_NO_RESOURCES;
    for (uint32_t at = 0u; at < count; ++at) {
        rows[at].label = items[at].text;
        rows[at].label_length = items[at].text_length;
        rows[at].flags = at == (uint32_t)selected ? ASTRA_MENU_CHECKED : 0u;
    }
    serial = popup->_private_serial + 1u;
    if (serial == 0u || serial >= UINT32_C(0x7fffffff)) serial = 1u;
    request = UINT32_C(0x80000000) | serial;
    anchor_y = (int64_t)frame.y -
               selected * ASTRA_MENU_ROW_HEIGHT - ASTRA_MENU_PADDING;
    if (anchor_y < INT32_MIN) anchor_y = INT32_MIN;
    result = astra_window_menu_open(window, rows, count, frame.x,
        (int32_t)anchor_y, request);
    astra_runtime_deallocate(rows);
    if (result != ASTRA_OK) return result;
    popup->_private_serial = serial;
    popup->_private_request = request;
    popup->_private_window = window;
    popup->_private_context = context;
    popup->_private_control = control;
    popup->_private_count = count;
    return ASTRA_OK;
}

AstraResult astra_interface_popup_handle_event(AstraPopup *popup,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled)
{
    AstraUIContext *context;
    AstraControl *control;
    uint32_t selection;
    int64_t previous;
    AstraResult result;

    if (handled != NULL) *handled = 0;
    if (!popup_valid(popup) || event == NULL || action == NULL ||
        handled == NULL || event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (event->type != ASTRA_WINDOW_EVENT_MENU_RESULT ||
        popup->_private_request == 0u ||
        event->data.menu_result.request != popup->_private_request)
        return ASTRA_OK;
    *handled = 1;
    action->type = ASTRA_UI_ACTION_NONE;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u) {
        popup_clear(popup);
        return ASTRA_ERROR_IO;
    }
    if (event->data.menu_result.status != ASTRA_STATUS_OK) {
        AstraResult status = astra_result_from_service(
            event->data.menu_result.status);
        popup_clear(popup);
        return status;
    }
    selection = event->data.menu_result.selection;
    if (selection == 0u) {
        popup_clear(popup);
        return ASTRA_OK;
    }
    if (selection > popup->_private_count) {
        popup_clear(popup);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    context = popup->_private_context;
    control = popup->_private_control;
    popup_clear(popup);
    result = astra_interface_control_get_value(control, &previous, NULL);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_value(context, control,
                                               (int64_t)selection - 1);
    if (result != ASTRA_OK) return result;
    if (previous != (int64_t)selection - 1) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = control->_private_id;
        action->value = (int64_t)selection - 1;
        action->decimal_places = 0u;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_popup_close(AstraPopup *popup)
{
    AstraResult result;
    if (!popup_valid(popup)) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (popup->_private_request == 0u) return ASTRA_OK;
    result = astra_window_menu_close(popup->_private_window,
                                     popup->_private_request);
    if (result == ASTRA_OK) popup_clear(popup);
    return result;
}
