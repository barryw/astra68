#include <astra/interface_library.h>
#include <astra/runtime.h>
#include <astra/gui.h>
#include <astra/bytes.h>

#include "control_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static int valid(const AstraComboBox *combo)
{
    return combo != NULL && combo->_private_size == sizeof(*combo);
}

static int in_context(const AstraUIContext *context,
    const AstraControl *controls)
{
    if (context == NULL || context->_private_controls == NULL ||
        context->_private_control_count < 3u) return 0;
    for (uint32_t at = 0u; at <= context->_private_control_count - 3u; ++at)
        if (&context->_private_controls[at] == controls) return 1;
    return 0;
}

static void clear(AstraComboBox *combo)
{
    astra_runtime_deallocate(combo->_private_visible);
    combo->_private_visible = NULL;
    combo->_private_visible_count = 0u;
    combo->_private_request = 0u;
    combo->_private_window = NULL;
    combo->_private_context = NULL;
}

AstraResult astra_interface_combo_box_init(AstraComboBox *combo,
    AstraControl *controls, const AstraComboBoxInfo *info)
{
    static const char menu_label[] = "Choices";
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem flex = ASTRA_FLEX_ITEM_INIT;
    AstraResult result;

    if (!valid(combo) || combo->_private_controls != NULL ||
        controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->first_id > UINT32_MAX - 2u ||
        info->model == NULL || info->items == NULL || info->item_count == 0u ||
        info->preferred_columns == 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->item_count; ++at)
        if (info->items[at].text_length == 0u ||
            !astra_interface_single_line_text_valid(info->items[at].text,
                info->items[at].text_length) ||
            !astra_words_zero(info->items[at].reserved, 2u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    row.id = info->first_id;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    field.id = info->first_id + 1u;
    field.model = info->model;
    field.preferred_columns = info->preferred_columns;
    field.state = info->state;
    result = astra_interface_field_init(&controls[1], &field);
    if (result != ASTRA_OK) return result;
    flex.parent_id = row.id;
    flex.grow = 1u;
    result = astra_interface_control_set_flex(&controls[1], &flex);
    if (result != ASTRA_OK) return result;
    button.id = info->first_id + 2u;
    button.text = menu_label;
    button.text_length = sizeof(menu_label) - 1u;
    button.variant = ASTRA_BUTTON_SPLIT_MENU;
    button.state = info->state & ASTRA_CONTROL_DISABLED;
    result = astra_interface_button_init(&controls[2], &button);
    if (result != ASTRA_OK) return result;
    flex = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    flex.parent_id = row.id;
    flex.shrink = 0u;
    result = astra_interface_control_set_flex(&controls[2], &flex);
    if (result != ASTRA_OK) return result;
    combo->_private_controls = controls;
    combo->_private_model = info->model;
    combo->_private_items = info->items;
    combo->_private_item_count = info->item_count;
    return ASTRA_OK;
}

static uint8_t fold_ascii(uint8_t byte)
{
    return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte;
}

static int starts_with(const AstraChoiceItem *item,
    const char *query, uint32_t query_bytes)
{
    if (query_bytes > item->text_length) return 0;
    for (uint32_t at = 0u; at < query_bytes; ++at)
        if (fold_ascii((uint8_t)item->text[at]) !=
            fold_ascii((uint8_t)query[at])) return 0;
    return 1;
}

AstraResult astra_interface_combo_box_open(AstraComboBox *combo,
    AstraUIContext *context, AstraWindow *window)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraControlFrame frame, clip;
    AstraMenuItem *rows = NULL;
    uint32_t *visible = NULL;
    char *query = NULL;
    uint32_t bytes, count = 0u, control_index = 0u, request;
    AstraResult result;

    if (!valid(combo) || combo->_private_controls == NULL ||
        !in_context(context, combo->_private_controls) || window == NULL ||
        (combo->_private_controls[2]._private_state &
         (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (combo->_private_request != 0u) return ASTRA_ERROR_BUSY;
    result = astra_text_model_get_state(combo->_private_model, &state);
    if (result != ASTRA_OK || state.line_count != 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (state.text_bytes != 0u) {
        query = astra_runtime_allocate(state.text_bytes);
        if (query == NULL) return ASTRA_ERROR_NO_RESOURCES;
        result = astra_text_model_copy(combo->_private_model, 0u,
            state.text_bytes, query, state.text_bytes, &bytes);
        if (result != ASTRA_OK) goto done;
    }
    if (combo->_private_item_count > UINT32_MAX / sizeof(*rows) ||
        (uint64_t)combo->_private_item_count * ASTRA_GUI_MENU_ROW_BYTES +
            ASTRA_GUI_MENU_HEADER_BYTES > ASTRA_GUI_MENU_BYTES_MAX) {
        result = ASTRA_ERROR_NO_RESOURCES;
        goto done;
    }
    visible = astra_runtime_allocate(
        (size_t)combo->_private_item_count * sizeof(*visible));
    rows = astra_runtime_allocate(
        (size_t)combo->_private_item_count * sizeof(*rows));
    if (visible == NULL || rows == NULL) {
        result = ASTRA_ERROR_NO_RESOURCES;
        goto done;
    }
    for (uint32_t at = 0u; at < combo->_private_item_count; ++at) {
        const AstraChoiceItem *item = &combo->_private_items[at];
        if (!starts_with(item, query, state.text_bytes)) continue;
        visible[count] = at;
        rows[count++] = (AstraMenuItem){item->text, item->text_length,
            state.text_bytes == item->text_length &&
            memcmp(item->text, query, state.text_bytes) == 0 ?
                ASTRA_MENU_CHECKED : 0u};
    }
    if (count == 0u) { result = ASTRA_ERROR_NOT_PRESENT; goto done; }
    while (&context->_private_controls[control_index] !=
           &combo->_private_controls[2]) ++control_index;
    result = astra_interface_ui_geometry(context, control_index,
        &frame, &clip);
    if (result != ASTRA_OK) goto done;
    request = UINT32_C(0xa0000000) |
        ((combo->_private_serial + 1u) & UINT32_C(0x0fffffff));
    if ((request & UINT32_C(0x0fffffff)) == 0u) ++request;
    result = astra_window_menu_open(window, rows, count,
        frame.x, frame.y + (int32_t)frame.height, request);
    if (result != ASTRA_OK) goto done;
    combo->_private_serial = request & UINT32_C(0x0fffffff);
    combo->_private_request = request;
    combo->_private_window = window;
    combo->_private_context = context;
    combo->_private_visible = visible;
    combo->_private_visible_count = count;
    visible = NULL;
done:
    astra_runtime_deallocate(query);
    astra_runtime_deallocate(rows);
    astra_runtime_deallocate(visible);
    return result;
}

AstraResult astra_interface_combo_box_handle_event(AstraComboBox *combo,
    const AstraWindowEvent *event, AstraUIAction *action, int *handled)
{
    uint32_t selection, source;
    AstraUIContext *context;
    AstraResult result;
    AstraControl *field;
    const AstraChoiceItem *item;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;

    if (handled != NULL) *handled = 0;
    if (!valid(combo) || event == NULL || action == NULL || handled == NULL ||
        event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (event->type != ASTRA_WINDOW_EVENT_MENU_RESULT ||
        combo->_private_request == 0u ||
        event->data.menu_result.request != combo->_private_request)
        return ASTRA_OK;
    *handled = 1;
    *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u) {
        clear(combo);
        return ASTRA_ERROR_IO;
    }
    result = astra_result_from_service(event->data.menu_result.status);
    selection = event->data.menu_result.selection;
    if (result != ASTRA_OK || selection == 0u) {
        clear(combo);
        return result;
    }
    if (selection > combo->_private_visible_count) {
        clear(combo);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    source = combo->_private_visible[selection - 1u];
    item = &combo->_private_items[source];
    context = combo->_private_context;
    field = &combo->_private_controls[1];
    clear(combo);
    result = astra_text_model_get_state(combo->_private_model, &state);
    if (result != ASTRA_OK) return result;
    result = astra_text_model_replace(combo->_private_model, 0u,
        state.text_bytes, item->text, item->text_length);
    if (result != ASTRA_OK) return result;
    result = astra_interface_field_refresh(context, field);
    if (result != ASTRA_OK) return result;
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = field->_private_id;
    action->value = source;
    return ASTRA_OK;
}

AstraResult astra_interface_combo_box_close(AstraComboBox *combo)
{
    AstraResult result;

    if (!valid(combo)) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (combo->_private_request == 0u) return ASTRA_OK;
    result = astra_window_menu_close(combo->_private_window,
        combo->_private_request);
    if (result == ASTRA_OK) clear(combo);
    return result;
}
