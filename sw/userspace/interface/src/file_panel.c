#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/string.h>
#include <astra/utf8.h>
#include <string.h>
#include "panel_internal.h"

static AstraResult file_panel_items(const AstraDirectoryEntry *entries,
    uint32_t count, AstraCollectionItem *items, uint32_t capacity,
    uint32_t *visible_count)
{
    uint32_t visible = 0u;

    if (visible_count == NULL ||
        (count != 0u && entries == NULL) ||
        (capacity != 0u && items == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at) {
        const char *end = memchr(entries[at].name, '\0',
                                 sizeof(entries[at].name));
        uint32_t length;

        if (end == NULL) continue;
        length = (uint32_t)(end - entries[at].name);
        if (length == 0u ||
            (length == 1u && entries[at].name[0] == '.') ||
            (length == 2u && entries[at].name[0] == '.' &&
             entries[at].name[1] == '.') ||
            memchr(entries[at].name, '/', length) != NULL ||
            !astra_utf8_validate(entries[at].name, length, 0u) ||
            (entries[at].kind != ASTRA_VFS_KIND_FILE &&
             entries[at].kind != ASTRA_VFS_KIND_DIRECTORY))
            continue;
        if (visible == capacity) return ASTRA_ERROR_BUFFER_TOO_SMALL;
        items[visible++] = (AstraCollectionItem){
            (uint64_t)at + 1u, entries[at].name, length, { 0u, 0u }
        };
    }
    *visible_count = visible;
    return ASTRA_OK;
}

static AstraResult file_panel_label(AstraControl *control, uint32_t id,
    const char *text, uint32_t length, uint32_t state, uint32_t parent)
{
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;
    AstraResult result;

    info.id = id;
    info.text = text;
    info.text_length = length;
    info.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    info.state = state;
    result = astra_interface_label_init(control, &info);
    return result == ASTRA_OK ? astra_panel_child(control, parent, 0u) : result;
}

AstraResult astra_interface_file_panel_init(AstraFilePanel *panel,
    AstraControl *controls, const AstraFilePanelInfo *info)
{
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollViewInfo scroll = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraResult result;
    uint32_t visible_count = 0u;

    if (panel == NULL || controls == NULL || info == NULL ||
        info->size < sizeof(*info) || info->first_id == 0u ||
        info->first_id > UINT32_MAX - (ASTRA_FILE_PANEL_CONTROL_COUNT - 1u) ||
        (info->mode != ASTRA_FILE_PANEL_OPEN &&
         info->mode != ASTRA_FILE_PANEL_SAVE) ||
        info->collection == NULL || info->scroll == NULL ||
        (info->mode == ASTRA_FILE_PANEL_SAVE && info->name_model == NULL) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *panel = (AstraFilePanel)ASTRA_FILE_PANEL_INIT;
    if (!astra_string_copy(panel->directory_storage[0],
                           sizeof(panel->directory_storage[0]),
                           info->directory) ||
        panel->directory_storage[0][0] == '\0')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    panel->directory = panel->directory_storage[0];
    result = file_panel_items(info->entries, info->entry_count,
        info->item_storage, info->item_capacity, &visible_count);
    if (result != ASTRA_OK) return result;
    result = astra_interface_collection_init(info->collection,
        info->item_storage, visible_count, 0u);
    if (result != ASTRA_OK) return result;
    panel->first_id = info->first_id;
    panel->mode = info->mode;
    panel->entries = info->entries;
    panel->entry_count = info->entry_count;
    panel->item_count = visible_count;
    panel->items = info->item_storage;
    panel->item_capacity = info->item_capacity;
    panel->collection = info->collection;
    panel->scroll = info->scroll;
    panel->name_model = info->name_model;

    container.id = info->first_id + ASTRA_FILE_PANEL_ROOT;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 10u;
    result = astra_interface_container_init(&controls[ASTRA_FILE_PANEL_ROOT],
                                            &container);
    if (result != ASTRA_OK) return result;
    result = file_panel_label(&controls[ASTRA_FILE_PANEL_TITLE],
        info->first_id + ASTRA_FILE_PANEL_TITLE,
        info->mode == ASTRA_FILE_PANEL_OPEN ? "Open file" : "Save file",
        9u, 0u, info->first_id);
    if (result != ASTRA_OK) return result;
    result = file_panel_label(&controls[ASTRA_FILE_PANEL_LOCATION],
        info->first_id + ASTRA_FILE_PANEL_LOCATION,
        panel->directory, (uint32_t)strlen(panel->directory), 0u,
        info->first_id);
    if (result != ASTRA_OK) return result;

    scroll.id = info->first_id + ASTRA_FILE_PANEL_SCROLL;
    scroll.model = info->scroll;
    scroll.preferred_width = 320u;
    scroll.preferred_height = 280u;
    result = astra_interface_scroll_view_init(
        &controls[ASTRA_FILE_PANEL_SCROLL], &scroll);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_SCROLL],
                              info->first_id, 1u);
    if (result != ASTRA_OK) return result;
    list.id = info->first_id + ASTRA_FILE_PANEL_LIST;
    list.model = info->collection;
    result = astra_interface_list_init(&controls[ASTRA_FILE_PANEL_LIST],
                                       &list);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_LIST],
                              info->first_id + ASTRA_FILE_PANEL_SCROLL, 0u);
    if (result != ASTRA_OK) return result;

    result = file_panel_label(&controls[ASTRA_FILE_PANEL_NAME_LABEL],
        info->first_id + ASTRA_FILE_PANEL_NAME_LABEL, "Name", 4u,
        info->mode == ASTRA_FILE_PANEL_OPEN ? ASTRA_CONTROL_COLLAPSED : 0u,
        info->first_id);
    if (result != ASTRA_OK) return result;
    if (info->mode == ASTRA_FILE_PANEL_SAVE) {
        field.id = info->first_id + ASTRA_FILE_PANEL_NAME;
        field.model = info->name_model;
        field.preferred_columns = 32u;
        result = astra_interface_field_init(
            &controls[ASTRA_FILE_PANEL_NAME], &field);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[ASTRA_FILE_PANEL_NAME],
                                  info->first_id, 0u);
    } else {
        result = file_panel_label(&controls[ASTRA_FILE_PANEL_NAME],
            info->first_id + ASTRA_FILE_PANEL_NAME, " ", 1u,
            ASTRA_CONTROL_COLLAPSED, info->first_id);
    }
    if (result != ASTRA_OK) return result;

    container = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    container.id = info->first_id + ASTRA_FILE_PANEL_ACTIONS;
    container.layout.direction = ASTRA_FLEX_ROW;
    container.layout.main_gap = 8u;
    result = astra_interface_container_init(
        &controls[ASTRA_FILE_PANEL_ACTIONS], &container);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_ACTIONS],
                              info->first_id, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = ASTRA_FILE_PANEL_CANCEL;
         at <= ASTRA_FILE_PANEL_PRIMARY; ++at) {
        button = (AstraButtonInfo)ASTRA_BUTTON_INFO_INIT;
        button.id = info->first_id + at;
        button.text = at == ASTRA_FILE_PANEL_CANCEL ? "Cancel" :
            (info->mode == ASTRA_FILE_PANEL_OPEN ? "Open" : "Save");
        button.text_length = at == ASTRA_FILE_PANEL_CANCEL ? 6u : 4u;
        button.variant = at == ASTRA_FILE_PANEL_PRIMARY ?
            ASTRA_BUTTON_PRIMARY : ASTRA_BUTTON_STANDARD;
        result = astra_interface_button_init(&controls[at], &button);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[at],
            info->first_id + ASTRA_FILE_PANEL_ACTIONS, 0u);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_set_directory(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, const char *directory,
    const AstraDirectoryEntry *entries, uint32_t entry_count,
    AstraCollectionItem *item_storage, uint32_t item_capacity)
{
    char *next;
    const char *previous;
    uint64_t previous_id;
    AstraResult result;
    uint32_t visible_count = 0u;

    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        panel->first_id == 0u || panel->active_directory > 1u ||
        controls == NULL ||
        (item_storage == panel->items && panel->item_count != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = panel->directory_storage[1u - panel->active_directory];
    if (!astra_string_copy(next, ASTRA_VFS_PATH_MAX, directory) ||
        next[0] == '\0')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = file_panel_items(entries, entry_count, item_storage,
        item_capacity, &visible_count);
    if (result != ASTRA_OK) return result;
    previous = panel->directory;
    previous_id = panel->collection->selected_id;
    result = astra_interface_control_set_text(context,
        &controls[ASTRA_FILE_PANEL_LOCATION], next,
        (uint32_t)strlen(next));
    if (result != ASTRA_OK) return result;
    result = astra_interface_list_set_items(context,
        &controls[ASTRA_FILE_PANEL_LIST], item_storage, visible_count);
    if (result != ASTRA_OK) {
        (void)astra_interface_control_set_text(context,
            &controls[ASTRA_FILE_PANEL_LOCATION], previous,
            (uint32_t)strlen(previous));
        return result;
    }
    result = astra_interface_control_set_value(context,
        &controls[ASTRA_FILE_PANEL_LIST], -1);
    if (result != ASTRA_OK) {
        (void)astra_interface_list_set_items(context,
            &controls[ASTRA_FILE_PANEL_LIST], panel->items,
            panel->item_count);
        for (uint32_t at = 0u; at < panel->item_count; ++at)
            if (panel->items[at].id == previous_id) {
                (void)astra_interface_control_set_value(context,
                    &controls[ASTRA_FILE_PANEL_LIST], at);
                break;
            }
        (void)astra_interface_control_set_text(context,
            &controls[ASTRA_FILE_PANEL_LOCATION], previous,
            (uint32_t)strlen(previous));
        return result;
    }
    panel->active_directory = 1u - panel->active_directory;
    panel->directory = next;
    panel->entries = entries;
    panel->entry_count = entry_count;
    panel->item_count = visible_count;
    panel->items = item_storage;
    panel->item_capacity = item_capacity;
    panel->leaf[0] = '\0';
    return ASTRA_OK;
}

static AstraResult file_panel_select(AstraFilePanel *panel, uint32_t index,
    uint32_t *outcome)
{
    const AstraDirectoryEntry *entry;

    if (index >= panel->entry_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    entry = &panel->entries[index];
    if (!astra_string_copy(panel->leaf, sizeof(panel->leaf), entry->name))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (entry->kind == ASTRA_VFS_KIND_DIRECTORY) {
        *outcome = ASTRA_FILE_PANEL_NAVIGATE;
        return ASTRA_OK;
    }
    if (entry->kind != ASTRA_VFS_KIND_FILE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (panel->mode == ASTRA_FILE_PANEL_OPEN)
        *outcome = ASTRA_FILE_PANEL_PICKED;
    return ASTRA_OK;
}

static AstraResult file_panel_save_name(AstraFilePanel *panel,
    uint32_t *outcome)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t bytes = 0u;
    AstraResult result = astra_text_model_get_state(panel->name_model, &state);

    if (result != ASTRA_OK) return result;
    if (state.text_bytes == 0u || state.text_bytes >= sizeof(panel->leaf))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_text_model_copy(panel->name_model, 0u, state.text_bytes,
        panel->leaf, sizeof(panel->leaf) - 1u, &bytes);
    if (result != ASTRA_OK) return result;
    if (bytes != state.text_bytes) return ASTRA_ERROR_INVALID_ARGUMENT;
    panel->leaf[bytes] = '\0';
    if ((bytes == 1u && panel->leaf[0] == '.') ||
        (bytes == 2u && panel->leaf[0] == '.' && panel->leaf[1] == '.'))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < bytes; ++at)
        if (panel->leaf[at] == '/' || panel->leaf[at] == '\0' ||
            (uint8_t)panel->leaf[at] < 32u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_FILE_PANEL_PICKED;
    for (uint32_t at = 0u; at < panel->entry_count; ++at)
        if (strcmp(panel->leaf, panel->entries[at].name) == 0) {
            if (panel->entries[at].kind != ASTRA_VFS_KIND_FILE)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            *outcome = ASTRA_FILE_PANEL_OVERWRITE;
            break;
        }
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_action(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome)
{
    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u || context == NULL || controls == NULL ||
        action == NULL || action->size < sizeof(*action) || outcome == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_FILE_PANEL_NONE;
    if (action->control_id < panel->first_id ||
        action->control_id - panel->first_id >= ASTRA_FILE_PANEL_CONTROL_COUNT)
        return ASTRA_OK;
    if (action->type != ASTRA_UI_ACTION_ACTIVATE) return ASTRA_OK;
    if (action->control_id == panel->first_id + ASTRA_FILE_PANEL_CANCEL) {
        panel->leaf[0] = '\0';
        *outcome = ASTRA_FILE_PANEL_CANCELLED;
        return ASTRA_OK;
    }
    if (action->control_id == panel->first_id + ASTRA_FILE_PANEL_LIST) {
        AstraResult result;

        if (action->value < 0 || (uint64_t)action->value >= panel->item_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = file_panel_select(panel,
            (uint32_t)panel->items[(uint32_t)action->value].id - 1u,
            outcome);
        if (result != ASTRA_OK || panel->mode == ASTRA_FILE_PANEL_OPEN ||
            *outcome == ASTRA_FILE_PANEL_NAVIGATE)
            return result;
        {
            AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
            uint32_t bytes = (uint32_t)strlen(panel->leaf);

            result = astra_text_model_get_state(panel->name_model, &state);
            if (result != ASTRA_OK) return result;
            result = astra_text_model_replace(panel->name_model, 0u,
                state.text_bytes, panel->leaf, bytes);
            if (result != ASTRA_OK) return result;
            return astra_interface_field_refresh(context,
                &controls[ASTRA_FILE_PANEL_NAME]);
        }
    }
    if (action->control_id != panel->first_id + ASTRA_FILE_PANEL_PRIMARY)
        return ASTRA_OK;
    if (panel->mode == ASTRA_FILE_PANEL_SAVE)
        return file_panel_save_name(panel, outcome);
    if (panel->collection->selected_id == 0u ||
        panel->collection->selected_id > panel->entry_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return file_panel_select(panel,
        (uint32_t)panel->collection->selected_id - 1u, outcome);
}
