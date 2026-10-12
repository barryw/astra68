#include <astra/menu.h>
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

AstraResult astra_interface_context_menu_open(AstraContextMenu *menu,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, uint32_t count, int32_t x, int32_t y)
{
    AstraMenuItem *rows;
    AstraResult result = ASTRA_OK;
    uint32_t request;
    uint64_t length = ASTRA_GUI_MENU_HEADER_BYTES + (uint64_t)count * ASTRA_GUI_MENU_ROW_BYTES;

    if (!valid(menu) || window == NULL || scope == NULL || items == NULL ||
        count == 0u || length > ASTRA_GUI_MENU_BYTES_MAX || count > UINT32_MAX / sizeof(*rows) ||
        (menu->_private_request != 0u && menu->_private_window != window))
        return ASTRA_ERROR_INVALID_ARGUMENT;
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
                         (state.checked ? ASTRA_MENU_CHECKED : 0u);
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

AstraResult astra_interface_context_menu_handle_event(AstraContextMenu *menu,
    const AstraWindowEvent *event, int *handled)
{
    const AstraCommandScope *scope;
    AstraContextMenuItem item;

    if (handled != NULL) *handled = 0;
    if (!valid(menu) || event == NULL || handled == NULL ||
        event->size < sizeof(*event) || event->version != ASTRA_WINDOW_EVENT_VERSION)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *handled = event->type == ASTRA_WINDOW_EVENT_MENU_RESULT;
    if (!*handled || menu->_private_request == 0u ||
        event->data.menu_result.request != menu->_private_request) return ASTRA_OK;
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
