#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/ascii.h>
#include <astra/civil.h>
#include <astra/runtime.h>
#include <astra/string.h>
#include <astra/utf8.h>
#include <string.h>
#include "panel_internal.h"
#include "control_internal.h"

#define FILE_PANEL_TABLE_TRAILING_COLUMNS 3u
#define FILE_PANEL_TABLE_TEXT_BYTES 56u

static const AstraTableColumn file_panel_table_columns[] = {
    {"Name", 4u, 240u, 0u},
    {"Size", 4u, 112u, 0u},
    {"Kind", 4u, 112u, 0u},
    {"Modified", 8u, 176u, 0u}
};

/* One allocation keeps row-major cells and their borrowed strings together. */
static AstraResult file_panel_table_rows(const AstraDirectoryEntry *entries,
    const AstraCollectionItem *items, uint32_t count, void **block)
{
    AstraTableCell *cells;
    char *texts;
    uint64_t bytes;

    if (block == NULL || (count != 0u && (entries == NULL || items == NULL)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *block = NULL;
    if (count == 0u) return ASTRA_OK;
    bytes = (uint64_t)count *
        (FILE_PANEL_TABLE_TRAILING_COLUMNS * sizeof(AstraTableCell) +
         FILE_PANEL_TABLE_TEXT_BYTES);
    if (bytes > SIZE_MAX) return ASTRA_ERROR_NO_RESOURCES;
    cells = astra_runtime_allocate((size_t)bytes);
    if (cells == NULL) return ASTRA_ERROR_NO_RESOURCES;
    texts = (char *)(cells + count * FILE_PANEL_TABLE_TRAILING_COLUMNS);
    for (uint32_t row = 0u; row < count; ++row) {
        const AstraDirectoryEntry *entry = &entries[(uint32_t)items[row].id - 1u];
        AstraTableCell *next = cells + row * FILE_PANEL_TABLE_TRAILING_COLUMNS;
        char *size_text = texts + row * FILE_PANEL_TABLE_TEXT_BYTES;
        char *date_text = size_text + 32u;
        const char *kind = entry->kind == ASTRA_VFS_KIND_DIRECTORY ?
            "Folder" : "File";
        AstraString size;

        astra_string_init(&size, size_text, 32u);
        if (entry->kind == ASTRA_VFS_KIND_DIRECTORY)
            (void)astra_string_append(&size, "-");
        else if (!astra_string_append_u64(&size, entry->byte_size) ||
                 !astra_string_append(&size, " B")) {
            astra_runtime_deallocate(cells);
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        }
        if (entry->mtime <= 0 ||
            (uint64_t)entry->mtime > UINT64_MAX / UINT64_C(1000000000) ||
            astra_civil_iso8601((uint64_t)entry->mtime * UINT64_C(1000000000),
                date_text, 24u) == 0u)
            (void)astra_string_copy(date_text, 24u, "-");
        else {
            date_text[10] = ' ';
            date_text[16] = 'Z';
            date_text[17] = '\0';
        }
        next[0] = (AstraTableCell){size_text,
            (uint32_t)strlen(size_text), 0u};
        next[1] = (AstraTableCell){kind,
            (uint32_t)strlen(kind), 0u};
        next[2] = (AstraTableCell){date_text,
            (uint32_t)strlen(date_text), 0u};
    }
    *block = cells;
    return ASTRA_OK;
}

static AstraResult file_panel_primary_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const void *rows)
{
    return astra_interface_table_set_rows(context, control, items, count,
        rows, count * FILE_PANEL_TABLE_TRAILING_COLUMNS);
}

void astra_interface_file_panel_dispose(AstraFilePanel *panel)
{
    if (panel == NULL) return;
    astra_runtime_deallocate(panel->list_rows);
    panel->list_rows = NULL;
    panel->list_table.cells = NULL;
    panel->list_table.cell_count = 0u;
}

AstraResult astra_interface_file_panel_context_entry(
    const AstraFilePanel *panel, const AstraUIAction *action,
    uint32_t *entry_index, uint32_t *depth)
{
    const AstraCollectionItem *items = NULL;
    uint32_t count = 0u, entry_count = 0u, at_depth = 0u;

    if (panel == NULL || panel->size != sizeof(*panel) ||
        action == NULL || action->size != sizeof(*action) ||
        action->type != ASTRA_UI_ACTION_CONTEXT_MENU ||
        entry_index == NULL || depth == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *entry_index = UINT32_MAX;
    *depth = 0u;
    if (panel->view_mode == ASTRA_FILE_PANEL_VIEW_LIST &&
        action->control_id == panel->first_id + ASTRA_FILE_PANEL_LIST) {
        items = panel->items;
        count = panel->item_count;
        entry_count = panel->entry_count;
    } else if (panel->view_mode == ASTRA_FILE_PANEL_VIEW_GRID &&
        action->control_id == panel->first_id + ASTRA_FILE_PANEL_GRID) {
        items = panel->items;
        count = panel->item_count;
        entry_count = panel->entry_count;
    } else if (panel->view_mode == ASTRA_FILE_PANEL_VIEW_COLUMNS) {
        for (uint32_t at = 0u; at < panel->column_count; ++at) {
            if (action->control_id != panel->first_id +
                    ASTRA_FILE_PANEL_CONTROL_COUNT + 2u + 2u * at)
                continue;
            const AstraFilePanelColumnSnapshot *snapshot =
                &panel->column_snapshots[at];
            items = snapshot->items;
            count = snapshot->item_count;
            entry_count = snapshot->entry_count;
            at_depth = at;
            break;
        }
    }
    if (items == NULL || action->value < 0 ||
        (uint64_t)action->value >= count)
        return ASTRA_OK;
    uint64_t id = items[(uint32_t)action->value].id;
    if (id == 0u || id > entry_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    *entry_index = (uint32_t)id - 1u;
    *depth = at_depth;
    return ASTRA_OK;
}

static int contains_query(const char *name, uint32_t name_length,
    const char *query, uint32_t query_length)
{
    if (query_length == 0u) return 1;
    if (query_length > name_length) return 0;
    for (uint32_t at = 0u; at + query_length <= name_length; ++at) {
        uint32_t matching = 0u;
        while (matching < query_length &&
            astra_ascii_lower(name[at + matching]) ==
                astra_ascii_lower(query[matching]))
            ++matching;
        if (matching == query_length) return 1;
    }
    return 0;
}

static AstraResult file_panel_items(const AstraFilePanel *panel,
    const AstraDirectoryEntry *entries,
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
        if (entries[at].kind != ASTRA_VFS_KIND_DIRECTORY &&
            (panel->type_accept != NULL &&
             panel->type_accept(panel->type_context, &entries[at],
                 panel->selected_filter) == 0u))
            continue;
        if (!contains_query(entries[at].name, length,
                panel->search_query, panel->search_length))
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

static AstraResult file_panel_container(AstraControl *control,
    uint32_t id, uint32_t parent, uint32_t direction)
{
    AstraContainerInfo info = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;
    info.id = id;
    info.layout.direction = direction;
    info.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    info.layout.main_gap = 8u;
    result = astra_interface_container_init(control, &info);
    if (result != ASTRA_OK || parent == 0u) return result;
    return astra_panel_child(control, parent, 0u);
}

static AstraResult file_panel_scroll(AstraControl *control, uint32_t id,
    uint32_t parent, AstraScrollModel *model, uint32_t width,
    uint32_t state, uint32_t grow)
{
    AstraScrollViewInfo info = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraResult result;
    info.id = id;
    info.model = model;
    info.preferred_width = width;
    info.preferred_height = 280u;
    info.state = state;
    result = astra_interface_scroll_view_init(control, &info);
    if (result != ASTRA_OK) return result;
    return astra_panel_child(control, parent, grow);
}

static AstraResult file_panel_list(AstraControl *control, uint32_t id,
    uint32_t parent, AstraCollectionModel *model)
{
    AstraListInfo info = ASTRA_LIST_INFO_INIT;
    AstraResult result;
    info.id = id;
    info.model = model;
    result = astra_interface_list_init(control, &info);
    if (result != ASTRA_OK) return result;
    return astra_panel_child(control, parent, 0u);
}

static void file_panel_fill_icons(AstraFilePanel *panel,
    const AstraCollectionItem *items, uint32_t count,
    const AstraSurface *const *entry_icons)
{
    for (uint32_t at = 0u; at < count; ++at)
        panel->grid_icons[at] = entry_icons == NULL ? NULL :
            entry_icons[(uint32_t)items[at].id - 1u];
}

static uint32_t file_panel_column_list_index(uint32_t depth)
{
    return ASTRA_FILE_PANEL_CONTROL_COUNT + 2u + depth * 2u;
}

static AstraResult file_panel_button(AstraControl *control, uint32_t id,
    uint32_t parent, const char *text, uint32_t primary)
{
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
    AstraResult result;
    info.id = id;
    info.text = text;
    info.text_length = (uint32_t)strlen(text);
    info.variant = primary != 0u ? ASTRA_BUTTON_PRIMARY : ASTRA_BUTTON_STANDARD;
    result = astra_interface_button_init(control, &info);
    if (result != ASTRA_OK) return result;
    return astra_panel_child(control, parent, 0u);
}

static AstraResult breadcrumb_parse(AstraBreadcrumb *breadcrumb,
    const char *path)
{
    uint32_t total = 1u;
    uint32_t index = 0u;
    uint32_t skip;
    uint32_t length;
    if (breadcrumb == NULL || path == NULL || path[0] != '/')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    length = (uint32_t)strlen(path);
    if (length == 0u || length >= ASTRA_VFS_PATH_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 1u; at < length;) {
        while (at < length && path[at] == '/') ++at;
        if (at == length) break;
        ++total;
        while (at < length && path[at] != '/') ++at;
    }
    skip = total > ASTRA_BREADCRUMB_VISIBLE ?
        total - ASTRA_BREADCRUMB_VISIBLE : 0u;
    breadcrumb->path = path;
    breadcrumb->visible_count = total - skip;
    if (skip == 0u) {
        breadcrumb->start[index] = 0u;
        breadcrumb->end[index++] = 1u;
    }
    total = 1u;
    for (uint32_t at = 1u; at < length;) {
        uint32_t start;
        while (at < length && path[at] == '/') ++at;
        if (at == length) break;
        start = at;
        while (at < length && path[at] != '/') ++at;
        if (total++ < skip) continue;
        breadcrumb->start[index] = start;
        breadcrumb->end[index++] = at;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_breadcrumb_init(AstraBreadcrumb *breadcrumb,
    AstraControl *controls, uint32_t first_id, uint32_t parent_id,
    const char *path)
{
    AstraResult result;
    if (breadcrumb == NULL || controls == NULL || first_id == 0u ||
        parent_id == 0u || first_id > UINT32_MAX - 7u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *breadcrumb = (AstraBreadcrumb)ASTRA_BREADCRUMB_INIT;
    breadcrumb->first_id = first_id;
    breadcrumb->parent_id = parent_id;
    result = breadcrumb_parse(breadcrumb, path);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < ASTRA_BREADCRUMB_VISIBLE; ++at) {
        AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
        button.id = first_id + at;
        button.text = at < breadcrumb->visible_count ?
            path + breadcrumb->start[at] : "/";
        button.text_length = at < breadcrumb->visible_count ?
            breadcrumb->end[at] - breadcrumb->start[at] : 1u;
        button.state = at < breadcrumb->visible_count ? 0u :
            ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_button_init(&controls[at], &button);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(&controls[at], parent_id, 0u);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_breadcrumb_set_path(AstraBreadcrumb *breadcrumb,
    AstraUIContext *context, AstraControl *controls, const char *path)
{
    AstraBreadcrumb next;
    AstraResult result;
    if (breadcrumb == NULL || breadcrumb->size < sizeof(*breadcrumb) ||
        context == NULL || controls == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = *breadcrumb;
    result = breadcrumb_parse(&next, path);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < ASTRA_BREADCRUMB_VISIBLE; ++at) {
        const char *label = at < next.visible_count ?
            next.path + next.start[at] : "/";
        uint32_t length = at < next.visible_count ?
            next.end[at] - next.start[at] : 1u;
        result = astra_interface_control_set_text(context, &controls[at],
            label, length);
        if (result != ASTRA_OK) break;
        result = astra_interface_ui_set_state(context, &controls[at],
            at < next.visible_count ? 0u : ASTRA_CONTROL_COLLAPSED);
        if (result != ASTRA_OK) break;
    }
    if (result != ASTRA_OK) {
        for (uint32_t at = 0u; at < ASTRA_BREADCRUMB_VISIBLE; ++at) {
            const char *label = at < breadcrumb->visible_count ?
                breadcrumb->path + breadcrumb->start[at] : "/";
            uint32_t length = at < breadcrumb->visible_count ?
                breadcrumb->end[at] - breadcrumb->start[at] : 1u;
            (void)astra_interface_control_set_text(context,
                &controls[at], label, length);
            (void)astra_interface_ui_set_state(context,
                &controls[at], at < breadcrumb->visible_count ?
                    0u : ASTRA_CONTROL_COLLAPSED);
        }
        return result;
    }
    *breadcrumb = next;
    return ASTRA_OK;
}

AstraResult astra_interface_breadcrumb_target(
    const AstraBreadcrumb *breadcrumb, uint32_t control_id,
    char *target, uint32_t capacity)
{
    uint32_t at, length;
    if (breadcrumb == NULL || breadcrumb->size < sizeof(*breadcrumb) ||
        target == NULL || control_id < breadcrumb->first_id ||
        control_id - breadcrumb->first_id >= breadcrumb->visible_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    at = control_id - breadcrumb->first_id;
    length = breadcrumb->end[at];
    if (length + 1u > capacity) return ASTRA_ERROR_BUFFER_TOO_SMALL;
    memcpy(target, breadcrumb->path, length);
    target[length] = '\0';
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_init(AstraFilePanel *panel,
    AstraControl *controls, const AstraFilePanelInfo *info)
{
    static const AstraChoiceItem view_items[] = {
        {"List", 4u, {0u, 0u}}, {"Grid", 4u, {0u, 0u}},
        {"Columns", 7u, {0u, 0u}}
    };
    static const AstraChoiceItem all_files[] = {
        {"All files", 9u, {0u, 0u}}
    };
    AstraTextModelInfo search_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraChoiceInfo choice = ASTRA_CHOICE_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraIconGridInfo grid = ASTRA_ICON_GRID_INFO_INIT;
    AstraColumnsInfo column_info = ASTRA_COLUMNS_INFO_INIT;
    AstraResult result;
    uint32_t visible_count = 0u;

    if (panel == NULL || controls == NULL || info == NULL ||
        info->size < sizeof(*info) || info->first_id == 0u ||
        info->first_id > UINT32_MAX - (ASTRA_FILE_PANEL_CONTROL_COUNT - 1u) ||
        (info->mode != ASTRA_FILE_PANEL_OPEN &&
         info->mode != ASTRA_FILE_PANEL_SAVE &&
         info->mode != ASTRA_FILE_PANEL_BROWSE) ||
        info->collection == NULL || info->scroll == NULL ||
        (info->mode == ASTRA_FILE_PANEL_SAVE && info->name_model == NULL) ||
        info->spare_item_storage == NULL ||
        info->spare_item_storage == info->item_storage ||
        info->spare_item_capacity < info->item_capacity ||
        info->grid_icons == NULL ||
        info->grid_icon_capacity < info->item_capacity ||
        (info->place_count != 0u &&
         (info->places == NULL || info->place_items == NULL ||
          info->place_capacity < info->place_count)) ||
        ((info->type_filter_count != 0u && info->type_filters == NULL) ||
         (info->type_filter_count > 1u && info->type_accept == NULL)) ||
        (info->column_count != 0u &&
         (info->column_count > 64u || info->columns == NULL ||
          info->column_snapshots == NULL ||
          info->control_capacity <
              ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(info->column_count) ||
          info->first_id > UINT32_MAX -
              ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(info->column_count))) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *panel = (AstraFilePanel)ASTRA_FILE_PANEL_INIT;
    panel->type_filters = info->type_filter_count != 0u ?
        info->type_filters : all_files;
    panel->type_filter_count = info->type_filter_count != 0u ?
        info->type_filter_count : 1u;
    panel->type_accept = info->type_accept;
    panel->type_context = info->type_context;
    panel->spare_items = info->spare_item_storage;
    panel->spare_capacity = info->spare_item_capacity;
    panel->grid_icons = info->grid_icons;
    panel->grid_icon_capacity = info->grid_icon_capacity;
    panel->places = info->places;
    panel->place_count = info->place_count;
    panel->place_items = info->place_items;
    panel->columns = info->columns;
    panel->column_snapshots = info->column_snapshots;
    panel->column_count = info->column_count;
    if (!astra_string_copy(panel->directory_storage[0],
                           sizeof(panel->directory_storage[0]),
                           info->directory) ||
        panel->directory_storage[0][0] != '/')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    panel->directory = panel->directory_storage[0];
    result = file_panel_items(panel, info->entries, info->entry_count,
        info->item_storage, info->item_capacity, &visible_count);
    if (result != ASTRA_OK) return result;
    file_panel_fill_icons(panel, info->item_storage, visible_count, NULL);
    result = astra_interface_collection_init(info->collection,
        info->item_storage, visible_count, 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_collection_init(&panel->grid_collection,
        info->item_storage, visible_count, 0u);
    if (result != ASTRA_OK) return result;
    for (uint32_t depth = 0u; depth < panel->column_count; ++depth) {
        AstraCollectionModel *model = panel->columns[depth].model;
        if (model == NULL || model == info->collection ||
            model == &panel->grid_collection)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = astra_interface_collection_init(model,
            depth == 0u ? info->item_storage : NULL,
            depth == 0u ? visible_count : 0u, 0u);
        if (result != ASTRA_OK) return result;
        panel->column_snapshots[depth] = (AstraFilePanelColumnSnapshot){0};
    }
    if (panel->column_count != 0u)
        panel->column_snapshots[0] = (AstraFilePanelColumnSnapshot){
            panel->directory, info->entries, info->entry_count,
            info->item_storage, visible_count, info->item_capacity};
    for (uint32_t at = 0u; at < info->place_count; ++at) {
        if (info->places[at].label == NULL ||
            info->places[at].label_length == 0u ||
            info->places[at].path == NULL ||
            info->places[at].path[0] != '/' ||
            !astra_utf8_validate(info->places[at].label,
                info->places[at].label_length, 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        info->place_items[at] = (AstraCollectionItem){
            (uint64_t)at + 1u, info->places[at].label,
            info->places[at].label_length, {0u, 0u}};
    }
    result = astra_interface_collection_init(&panel->places_collection,
        info->place_items, info->place_count, 0u);
    if (result != ASTRA_OK) return result;
    search_info.content_arena = panel->search_content;
    search_info.content_arena_bytes = sizeof(panel->search_content);
    search_info.metadata_arena = panel->search_metadata;
    search_info.metadata_arena_bytes = sizeof(panel->search_metadata);
    panel->search_model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    result = astra_text_model_init(&panel->search_model, &search_info);
    if (result != ASTRA_OK) return result;
    scroll_info.line_width = 8u;
    scroll_info.line_height = 16u;
    result = astra_scroll_init(&panel->places_scroll, &scroll_info);
    if (result != ASTRA_OK) return result;
    result = astra_scroll_init(&panel->grid_scroll, &scroll_info);
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
    if (info->mode == ASTRA_FILE_PANEL_SAVE) {
        AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
        AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
        uint32_t bytes;
        result = astra_text_model_get_state(info->name_model, &state);
        if (result != ASTRA_OK) return result;
        if (state.text_bytes >= sizeof(panel->leaf))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = astra_text_model_copy(info->name_model, 0u,
            state.text_bytes, panel->leaf, sizeof(panel->leaf) - 1u, &bytes);
        if (result != ASTRA_OK) return result;
        panel->leaf[bytes] = '\0';
        selection.focus = bytes;
        for (uint32_t at = 1u; at < bytes; ++at)
            if (panel->leaf[at] == '.') selection.focus = at;
        result = astra_text_model_set_selection(info->name_model, &selection);
        if (result != ASTRA_OK) return result;
        panel->leaf[0] = '\0';
    }

    result = file_panel_container(&controls[ASTRA_FILE_PANEL_ROOT],
        info->first_id, 0u, ASTRA_FLEX_COLUMN);
    if (result != ASTRA_OK) return result;
    result = file_panel_label(&controls[ASTRA_FILE_PANEL_TITLE],
        info->first_id + ASTRA_FILE_PANEL_TITLE,
        info->mode == ASTRA_FILE_PANEL_SAVE ? "Save file" : "Open file",
        9u, info->mode == ASTRA_FILE_PANEL_BROWSE ?
            ASTRA_CONTROL_COLLAPSED : 0u, info->first_id);
    if (result != ASTRA_OK) return result;
    result = file_panel_label(&controls[ASTRA_FILE_PANEL_NAME_LABEL],
        info->first_id + ASTRA_FILE_PANEL_NAME_LABEL, "Name", 4u,
        info->mode == ASTRA_FILE_PANEL_SAVE ? 0u : ASTRA_CONTROL_COLLAPSED,
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
    result = file_panel_container(&controls[ASTRA_FILE_PANEL_LOCATION_ROW],
        info->first_id + ASTRA_FILE_PANEL_LOCATION_ROW, info->first_id,
        ASTRA_FLEX_ROW);
    if (result != ASTRA_OK) return result;
    result = file_panel_button(&controls[ASTRA_FILE_PANEL_UP],
        info->first_id + ASTRA_FILE_PANEL_UP,
        info->first_id + ASTRA_FILE_PANEL_LOCATION_ROW, "Up", 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_breadcrumb_init(&panel->breadcrumb,
        &controls[ASTRA_FILE_PANEL_CRUMB0],
        info->first_id + ASTRA_FILE_PANEL_CRUMB0,
        info->first_id + ASTRA_FILE_PANEL_LOCATION_ROW,
        panel->directory);
    if (result != ASTRA_OK) return result;
    result = file_panel_label(&controls[ASTRA_FILE_PANEL_LOCATION],
        info->first_id + ASTRA_FILE_PANEL_LOCATION,
        "Where", 5u, ASTRA_CONTROL_COLLAPSED,
        info->first_id + ASTRA_FILE_PANEL_LOCATION_ROW);
    if (result != ASTRA_OK) return result;
    result = file_panel_container(&controls[ASTRA_FILE_PANEL_TOOLS],
        info->first_id + ASTRA_FILE_PANEL_TOOLS, info->first_id,
        ASTRA_FLEX_ROW);
    if (result != ASTRA_OK) return result;
    choice.id = info->first_id + ASTRA_FILE_PANEL_VIEW;
    choice.items = view_items;
    choice.item_count = panel->column_count != 0u ? 3u : 2u;
    result = astra_interface_segmented_init(&controls[ASTRA_FILE_PANEL_VIEW],
        &choice);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_VIEW],
        info->first_id + ASTRA_FILE_PANEL_TOOLS, 0u);
    if (result != ASTRA_OK) return result;
    field = (AstraFieldInfo)ASTRA_FIELD_INFO_INIT;
    field.id = info->first_id + ASTRA_FILE_PANEL_SEARCH;
    field.model = &panel->search_model;
    field.flags = ASTRA_FIELD_SEARCH;
    field.preferred_columns = 18u;
    result = astra_interface_search_field_init(
        &controls[ASTRA_FILE_PANEL_SEARCH], &field);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_SEARCH],
        info->first_id + ASTRA_FILE_PANEL_TOOLS, 1u);
    if (result != ASTRA_OK) return result;
    choice = (AstraChoiceInfo)ASTRA_CHOICE_INFO_INIT;
    choice.id = info->first_id + ASTRA_FILE_PANEL_TYPE;
    choice.items = panel->type_filters;
    choice.item_count = panel->type_filter_count;
    result = astra_interface_popup_init(&controls[ASTRA_FILE_PANEL_TYPE],
        &choice);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_TYPE],
        info->first_id + ASTRA_FILE_PANEL_TOOLS, 0u);
    if (result != ASTRA_OK) return result;
    if (info->mode == ASTRA_FILE_PANEL_BROWSE)
        controls[ASTRA_FILE_PANEL_TYPE]._private_state |=
            ASTRA_CONTROL_COLLAPSED;
    result = file_panel_container(&controls[ASTRA_FILE_PANEL_CONTENT],
        info->first_id + ASTRA_FILE_PANEL_CONTENT, info->first_id,
        ASTRA_FLEX_ROW);
    if (result != ASTRA_OK) return result;
    if (info->mode == ASTRA_FILE_PANEL_BROWSE) {
        AstraFlexItem content = ASTRA_FLEX_ITEM_INIT;
        content.parent_id = info->first_id;
        content.grow = 1u;
        result = astra_interface_control_set_flex(
            &controls[ASTRA_FILE_PANEL_CONTENT], &content);
        if (result != ASTRA_OK) return result;
    }
    result = file_panel_scroll(&controls[ASTRA_FILE_PANEL_PLACES_SCROLL],
        info->first_id + ASTRA_FILE_PANEL_PLACES_SCROLL,
        info->first_id + ASTRA_FILE_PANEL_CONTENT,
        &panel->places_scroll, 130u, 0u, 0u);
    if (result != ASTRA_OK) return result;
    result = file_panel_list(&controls[ASTRA_FILE_PANEL_PLACES_LIST],
        info->first_id + ASTRA_FILE_PANEL_PLACES_LIST,
        info->first_id + ASTRA_FILE_PANEL_PLACES_SCROLL,
        &panel->places_collection);
    if (result != ASTRA_OK) return result;
    result = file_panel_scroll(&controls[ASTRA_FILE_PANEL_SCROLL],
        info->first_id + ASTRA_FILE_PANEL_SCROLL,
        info->first_id + ASTRA_FILE_PANEL_CONTENT,
        info->scroll, 320u, 0u, 1u);
    if (result != ASTRA_OK) return result;
    result = file_panel_list(&controls[ASTRA_FILE_PANEL_LIST],
        info->first_id + ASTRA_FILE_PANEL_LIST,
        info->first_id + ASTRA_FILE_PANEL_SCROLL, info->collection);
    if (result != ASTRA_OK) return result;
    result = file_panel_scroll(&controls[ASTRA_FILE_PANEL_GRID_SCROLL],
        info->first_id + ASTRA_FILE_PANEL_GRID_SCROLL,
        info->first_id + ASTRA_FILE_PANEL_CONTENT,
        &panel->grid_scroll, 320u, ASTRA_CONTROL_COLLAPSED, 1u);
    if (result != ASTRA_OK) return result;
    panel->grid_model = (AstraIconGridModel)ASTRA_ICON_GRID_MODEL_INIT;
    panel->grid_model.collection = &panel->grid_collection;
    panel->grid_model.icons = info->grid_icons;
    panel->grid_model.icon_count = visible_count;
    panel->grid_model.columns = 4u;
    grid.id = info->first_id + ASTRA_FILE_PANEL_GRID;
    grid.model = &panel->grid_model;
    result = astra_interface_icon_grid_init(&controls[ASTRA_FILE_PANEL_GRID],
        &grid);
    if (result != ASTRA_OK) return result;
    result = astra_panel_child(&controls[ASTRA_FILE_PANEL_GRID],
        info->first_id + ASTRA_FILE_PANEL_GRID_SCROLL, 0u);
    if (result != ASTRA_OK) return result;
    result = file_panel_container(&controls[ASTRA_FILE_PANEL_ACTIONS],
        info->first_id + ASTRA_FILE_PANEL_ACTIONS, info->first_id,
        ASTRA_FLEX_ROW);
    if (result != ASTRA_OK) return result;
    if (info->mode == ASTRA_FILE_PANEL_BROWSE)
        controls[ASTRA_FILE_PANEL_ACTIONS]._private_state |=
            ASTRA_CONTROL_COLLAPSED;
    for (uint32_t at = ASTRA_FILE_PANEL_CANCEL;
         at <= ASTRA_FILE_PANEL_PRIMARY; ++at) {
        const char *text = at == ASTRA_FILE_PANEL_CANCEL ? "Cancel" :
            (info->mode == ASTRA_FILE_PANEL_OPEN ? "Open" : "Save");
        result = file_panel_button(&controls[at], info->first_id + at,
            info->first_id + ASTRA_FILE_PANEL_ACTIONS, text,
            at == ASTRA_FILE_PANEL_PRIMARY);
        if (result != ASTRA_OK) return result;
    }
    if (panel->column_count != 0u) {
        column_info.first_id = info->first_id + ASTRA_FILE_PANEL_CONTROL_COUNT;
        column_info.columns = panel->columns;
        column_info.column_count = panel->column_count;
        column_info.viewport_height = 280u;
        column_info.state = ASTRA_CONTROL_COLLAPSED;
        result = astra_interface_columns_init(
            &controls[ASTRA_FILE_PANEL_CONTROL_COUNT], &column_info);
        if (result != ASTRA_OK) return result;
        result = astra_panel_child(
            &controls[ASTRA_FILE_PANEL_CONTROL_COUNT],
            info->first_id + ASTRA_FILE_PANEL_CONTENT, 1u);
        if (result != ASTRA_OK) return result;
    }
    result = file_panel_table_rows(info->entries, info->item_storage,
        visible_count, &panel->list_rows);
    if (result != ASTRA_OK) return result;
    panel->list_table = (AstraTableModel)ASTRA_TABLE_MODEL_INIT;
    panel->list_table.collection = info->collection;
    panel->list_table.columns = file_panel_table_columns;
    panel->list_table.column_count = sizeof(file_panel_table_columns) /
        sizeof(file_panel_table_columns[0]);
    panel->list_table.cells = panel->list_rows;
    panel->list_table.cell_count = visible_count *
        FILE_PANEL_TABLE_TRAILING_COLUMNS;
    AstraTableInfo table = ASTRA_TABLE_INFO_INIT;
    table.id = info->first_id + ASTRA_FILE_PANEL_LIST;
    table.model = &panel->list_table;
    result = astra_interface_table_init(&controls[ASTRA_FILE_PANEL_LIST],
        &table);
    if (result == ASTRA_OK)
        result = astra_panel_child(&controls[ASTRA_FILE_PANEL_LIST],
            info->first_id + ASTRA_FILE_PANEL_SCROLL, 0u);
    if (result != ASTRA_OK) astra_interface_file_panel_dispose(panel);
    return result;
}

AstraResult astra_interface_file_panel_set_directory(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, const char *directory,
    const AstraDirectoryEntry *entries, uint32_t entry_count,
    AstraCollectionItem *item_storage, uint32_t item_capacity)
{
    char *next;
    const char *previous;
    void *next_rows = NULL;
    AstraResult result;
    uint32_t visible_count = 0u;

    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        panel->first_id == 0u || panel->active_directory > 1u ||
        controls == NULL ||
        (item_storage == panel->items && panel->item_count != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = panel->directory_storage[1u - panel->active_directory];
    if (!astra_string_copy(next, ASTRA_VFS_PATH_MAX, directory) ||
        next[0] != '/')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = file_panel_items(panel, entries, entry_count, item_storage,
        item_capacity, &visible_count);
    if (result != ASTRA_OK) return result;
    if (visible_count > panel->grid_icon_capacity)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    result = file_panel_table_rows(entries, item_storage, visible_count,
        &next_rows);
    if (result != ASTRA_OK) return result;
    file_panel_fill_icons(panel, item_storage, visible_count, NULL);
    previous = panel->directory;
    result = astra_interface_breadcrumb_set_path(&panel->breadcrumb,
        context, &controls[ASTRA_FILE_PANEL_CRUMB0], next);
    if (result != ASTRA_OK) goto rollback;
    result = astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], item_storage, visible_count,
        panel->grid_icons);
    if (result != ASTRA_OK) goto rollback;
    result = file_panel_primary_rows(context,
        &controls[ASTRA_FILE_PANEL_LIST], item_storage, visible_count,
        next_rows);
    if (result != ASTRA_OK) goto rollback;
    result = astra_interface_control_set_value(context,
        &controls[ASTRA_FILE_PANEL_LIST], -1);
    if (result != ASTRA_OK) goto rollback;
    result = astra_interface_control_set_value(context,
        &controls[ASTRA_FILE_PANEL_GRID], -1);
    if (result != ASTRA_OK) goto rollback;
    if (panel->column_count != 0u) {
        result = astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(0u)],
            item_storage, visible_count);
        if (result != ASTRA_OK) goto rollback;
        (void)astra_interface_control_set_value(context,
            &controls[file_panel_column_list_index(0u)], -1);
        for (uint32_t depth = 1u; depth < panel->column_count; ++depth) {
            result = astra_interface_list_set_items(context,
                &controls[file_panel_column_list_index(depth)], NULL, 0u);
            if (result != ASTRA_OK) goto rollback;
        }
    }
    astra_runtime_deallocate(panel->list_rows);
    panel->list_rows = next_rows;
    panel->active_directory = 1u - panel->active_directory;
    panel->directory = next;
    panel->entries = entries;
    panel->entry_icons = NULL;
    panel->entry_count = entry_count;
    panel->item_count = visible_count;
    panel->spare_items = panel->items;
    panel->spare_capacity = panel->item_capacity;
    panel->items = item_storage;
    panel->item_capacity = item_capacity;
    panel->selected_depth = 0u;
    if (panel->column_count != 0u) {
        panel->column_snapshots[0] = (AstraFilePanelColumnSnapshot){
            next, entries, entry_count, item_storage, visible_count,
            item_capacity};
        for (uint32_t depth = 1u; depth < panel->column_count; ++depth)
            panel->column_snapshots[depth] =
                (AstraFilePanelColumnSnapshot){0};
    }
    panel->leaf[0] = '\0';
    panel->target[0] = '\0';
    return ASTRA_OK;

rollback:
    file_panel_fill_icons(panel, panel->items, panel->item_count,
        panel->entry_icons);
    (void)file_panel_primary_rows(context, &controls[ASTRA_FILE_PANEL_LIST],
        panel->items, panel->item_count, panel->list_rows);
    (void)astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], panel->items, panel->item_count,
        panel->grid_icons);
    for (uint32_t depth = 0u; depth < panel->column_count; ++depth)
        (void)astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(depth)],
            panel->column_snapshots[depth].items,
            panel->column_snapshots[depth].item_count);
    (void)astra_interface_breadcrumb_set_path(&panel->breadcrumb,
        context, &controls[ASTRA_FILE_PANEL_CRUMB0], previous);
    astra_runtime_deallocate(next_rows);
    return result;
}

static AstraResult file_panel_refresh(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls)
{
    AstraCollectionItem *next = panel->spare_items;
    uint32_t next_capacity = panel->spare_capacity;
    void *next_rows = NULL;
    uint32_t count;
    AstraResult result;
    if (next == NULL || next == panel->items)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = file_panel_items(panel, panel->entries, panel->entry_count,
        next, panel->spare_capacity, &count);
    if (result != ASTRA_OK) return result;
    if (count > panel->grid_icon_capacity)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    result = file_panel_table_rows(panel->entries, next, count, &next_rows);
    if (result != ASTRA_OK) return result;
    file_panel_fill_icons(panel, next, count, panel->entry_icons);
    result = astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], next, count,
        panel->grid_icons);
    if (result != ASTRA_OK) goto rollback_refresh;
    result = file_panel_primary_rows(context,
        &controls[ASTRA_FILE_PANEL_LIST], next, count, next_rows);
    if (result != ASTRA_OK) goto rollback_refresh;
    if (panel->column_count != 0u) {
        result = astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(0u)], next, count);
        if (result != ASTRA_OK) goto rollback_refresh;
        for (uint32_t depth = 1u; depth < panel->column_count; ++depth) {
            result = astra_interface_list_set_items(context,
                &controls[file_panel_column_list_index(depth)], NULL, 0u);
            if (result != ASTRA_OK) goto rollback_refresh;
        }
    }
    astra_runtime_deallocate(panel->list_rows);
    panel->list_rows = next_rows;
    panel->spare_items = panel->items;
    panel->spare_capacity = panel->item_capacity;
    panel->items = next;
    panel->item_capacity = next_capacity;
    panel->item_count = count;
    panel->selected_depth = 0u;
    if (panel->column_count != 0u) {
        panel->column_snapshots[0].items = next;
        panel->column_snapshots[0].item_count = count;
        panel->column_snapshots[0].item_capacity = next_capacity;
        for (uint32_t depth = 1u; depth < panel->column_count; ++depth)
            panel->column_snapshots[depth] =
                (AstraFilePanelColumnSnapshot){0};
    }
    return ASTRA_OK;

rollback_refresh:
    file_panel_fill_icons(panel, panel->items, panel->item_count,
        panel->entry_icons);
    (void)file_panel_primary_rows(context, &controls[ASTRA_FILE_PANEL_LIST],
        panel->items, panel->item_count, panel->list_rows);
    (void)astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], panel->items, panel->item_count,
        panel->grid_icons);
    for (uint32_t depth = 0u; depth < panel->column_count; ++depth)
        (void)astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(depth)],
            panel->column_snapshots[depth].items,
            panel->column_snapshots[depth].item_count);
    astra_runtime_deallocate(next_rows);
    return result;
}

AstraResult astra_interface_file_panel_rebind_storage(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    AstraCollectionItem *item_storage, uint32_t item_capacity,
    AstraCollectionItem *spare_item_storage, uint32_t spare_item_capacity,
    const AstraSurface **grid_icons, uint32_t grid_icon_capacity)
{
    AstraResult result;

    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u ||
        panel->item_count > panel->item_capacity ||
        panel->item_count > panel->grid_icon_capacity ||
        (panel->item_count != 0u &&
         (panel->items == NULL || panel->grid_icons == NULL)) ||
        (panel->column_count != 0u && panel->column_snapshots == NULL) ||
        context == NULL || controls == NULL || item_storage == NULL ||
        spare_item_storage == NULL || grid_icons == NULL ||
        item_storage == spare_item_storage ||
        item_storage == panel->items || item_storage == panel->spare_items ||
        spare_item_storage == panel->items ||
        spare_item_storage == panel->spare_items ||
        grid_icons == panel->grid_icons ||
        item_capacity < panel->item_count ||
        spare_item_capacity < panel->item_count ||
        grid_icon_capacity < panel->item_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (panel->item_count != 0u) {
        memmove(item_storage, panel->items,
            (size_t)panel->item_count * sizeof(*item_storage));
        memmove(grid_icons, panel->grid_icons,
            (size_t)panel->item_count * sizeof(*grid_icons));
    }
    result = astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], item_storage,
        panel->item_count, grid_icons);
    if (result != ASTRA_OK) return result;
    result = file_panel_primary_rows(context,
        &controls[ASTRA_FILE_PANEL_LIST], item_storage, panel->item_count,
        panel->list_rows);
    if (result != ASTRA_OK) {
        (void)astra_interface_icon_grid_set_items(context,
            &controls[ASTRA_FILE_PANEL_GRID], panel->items,
            panel->item_count, panel->grid_icons);
        return result;
    }
    if (panel->column_count != 0u) {
        result = astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(0u)], item_storage,
            panel->item_count);
        if (result != ASTRA_OK) {
            (void)file_panel_primary_rows(context,
                &controls[ASTRA_FILE_PANEL_LIST], panel->items,
                panel->item_count, panel->list_rows);
            (void)astra_interface_icon_grid_set_items(context,
                &controls[ASTRA_FILE_PANEL_GRID], panel->items,
                panel->item_count, panel->grid_icons);
            return result;
        }
        panel->column_snapshots[0].items = item_storage;
        panel->column_snapshots[0].item_capacity = item_capacity;
    }
    panel->items = item_storage;
    panel->item_capacity = item_capacity;
    panel->spare_items = spare_item_storage;
    panel->spare_capacity = spare_item_capacity;
    panel->grid_icons = grid_icons;
    panel->grid_icon_capacity = grid_icon_capacity;
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_set_places(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraFilePanelPlace *places, uint32_t count,
    AstraCollectionItem *item_storage, uint32_t item_capacity)
{
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        controls == NULL || (count != 0u && places == NULL) ||
        count > item_capacity || (count != 0u && item_storage == NULL) ||
        (item_storage == panel->place_items && panel->place_count != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at) {
        if (places[at].label == NULL || places[at].label_length == 0u ||
            places[at].path == NULL || places[at].path[0] != '/' ||
            !astra_utf8_validate(places[at].label,
                places[at].label_length, 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        item_storage[at] = (AstraCollectionItem){
            (uint64_t)at + 1u, places[at].label,
            places[at].label_length, {0u, 0u}};
    }
    result = astra_interface_list_set_items(context,
        &controls[ASTRA_FILE_PANEL_PLACES_LIST], item_storage, count);
    if (result != ASTRA_OK) return result;
    panel->places = places;
    panel->place_count = count;
    panel->place_items = item_storage;
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_set_icons(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraSurface *const *entry_icons, uint32_t count)
{
    const AstraSurface *const *previous;
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        controls == NULL || (entry_icons == NULL ? count != 0u :
            count != panel->entry_count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    previous = panel->entry_icons;
    file_panel_fill_icons(panel, panel->items, panel->item_count, entry_icons);
    result = astra_interface_icon_grid_set_items(context,
        &controls[ASTRA_FILE_PANEL_GRID], panel->items,
        panel->item_count, panel->grid_icons);
    if (result != ASTRA_OK) {
        file_panel_fill_icons(panel, panel->items, panel->item_count, previous);
        return result;
    }
    panel->entry_icons = entry_icons;
    return ASTRA_OK;
}

AstraResult astra_interface_file_panel_set_column(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, uint32_t depth,
    const char *directory, const AstraDirectoryEntry *entries,
    uint32_t entry_count, AstraCollectionItem *items, uint32_t capacity)
{
    uint32_t count = 0u;
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) || context == NULL ||
        controls == NULL || depth == 0u || depth >= panel->column_count ||
        (directory == NULL && (entries != NULL || entry_count != 0u)) ||
        (directory == NULL && (items != NULL || capacity != 0u)) ||
        (directory != NULL && directory[0] != '/') ||
        directory == panel->target ||
        (directory != NULL && depth > 1u &&
         panel->column_snapshots[depth - 1u].directory == NULL) ||
        (directory != NULL && (items == panel->items ||
            items == panel->spare_items)) ||
        (items == panel->column_snapshots[depth].items &&
         panel->column_snapshots[depth].item_count != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (directory != NULL)
        for (uint32_t other = 1u; other < panel->column_count; ++other)
            if (other != depth && items != NULL &&
                items == panel->column_snapshots[other].items &&
                panel->column_snapshots[other].item_count != 0u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
    if (directory != NULL) {
        result = file_panel_items(panel, entries, entry_count, items,
            capacity, &count);
        if (result != ASTRA_OK) return result;
    }
    result = astra_interface_list_set_items(context,
        &controls[file_panel_column_list_index(depth)], items, count);
    if (result != ASTRA_OK) return result;
    panel->column_snapshots[depth] = (AstraFilePanelColumnSnapshot){
        directory, entries, entry_count, items, count, capacity};
    if (directory == NULL && panel->selected_depth >= depth)
        panel->selected_depth = depth - 1u;
    for (uint32_t deeper = depth + 1u;
         deeper < panel->column_count; ++deeper) {
        result = astra_interface_list_set_items(context,
            &controls[file_panel_column_list_index(deeper)], NULL, 0u);
        if (result != ASTRA_OK) return result;
        panel->column_snapshots[deeper] =
            (AstraFilePanelColumnSnapshot){0};
    }
    return ASTRA_OK;
}

static AstraResult file_panel_target_at(AstraFilePanel *panel,
    const char *directory, const char *name)
{
    AstraString target;
    astra_string_init(&target, panel->target, sizeof(panel->target));
    if (!astra_string_append(&target, directory) ||
        (target.length != 0u && target.text[target.length - 1u] != '/' &&
         !astra_string_append_char(&target, '/')) ||
        !astra_string_append(&target, name))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    return ASTRA_OK;
}

static AstraResult file_panel_target(AstraFilePanel *panel,
    const char *name)
{
    return file_panel_target_at(panel, panel->directory, name);
}

static AstraResult file_panel_fill_name(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraResult result = astra_text_model_get_state(panel->name_model, &state);
    if (result != ASTRA_OK) return result;
    result = astra_text_model_replace(panel->name_model, 0u,
        state.text_bytes, panel->leaf, (uint32_t)strlen(panel->leaf));
    if (result != ASTRA_OK) return result;
    return astra_interface_field_refresh(context,
        &controls[ASTRA_FILE_PANEL_NAME]);
}

static AstraResult file_panel_select(AstraFilePanel *panel, uint32_t index,
    uint32_t *outcome)
{
    const AstraDirectoryEntry *entry;

    if (index >= panel->entry_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    entry = &panel->entries[index];
    if (!astra_string_copy(panel->leaf, sizeof(panel->leaf), entry->name))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (file_panel_target(panel, panel->leaf) != ASTRA_OK)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    if (entry->kind == ASTRA_VFS_KIND_DIRECTORY) {
        *outcome = ASTRA_FILE_PANEL_NAVIGATE;
        return ASTRA_OK;
    }
    if (entry->kind != ASTRA_VFS_KIND_FILE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (panel->mode == ASTRA_FILE_PANEL_OPEN ||
        panel->mode == ASTRA_FILE_PANEL_BROWSE)
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
    result = file_panel_target(panel, panel->leaf);
    if (result != ASTRA_OK) return result;
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
    uint32_t at;
    AstraResult result;
    if (panel == NULL || panel->size < sizeof(*panel) ||
        panel->first_id == 0u || context == NULL || controls == NULL ||
        action == NULL || action->size < sizeof(*action) || outcome == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *outcome = ASTRA_FILE_PANEL_NONE;
    if (panel->column_count != 0u &&
        action->control_id >= panel->first_id + ASTRA_FILE_PANEL_CONTROL_COUNT &&
        action->control_id - panel->first_id <
            ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(panel->column_count)) {
        uint32_t column_at = action->control_id - panel->first_id -
            ASTRA_FILE_PANEL_CONTROL_COUNT;
        uint32_t depth;
        AstraFilePanelColumnSnapshot *snapshot;
        const AstraDirectoryEntry *entry;
        uint32_t row;
        if (column_at < 2u || (column_at & 1u) != 0u ||
            (action->type != ASTRA_UI_ACTION_VALUE_CHANGED &&
             action->type != ASTRA_UI_ACTION_ACTIVATE))
            return ASTRA_OK;
        depth = (column_at - 2u) / 2u;
        snapshot = &panel->column_snapshots[depth];
        if (action->value < 0 ||
            (uint64_t)action->value >= snapshot->item_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        row = (uint32_t)snapshot->items[action->value].id - 1u;
        if (row >= snapshot->entry_count || snapshot->directory == NULL)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        entry = &snapshot->entries[row];
        if (!astra_string_copy(panel->leaf, sizeof(panel->leaf), entry->name))
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        result = file_panel_target_at(panel, snapshot->directory, panel->leaf);
        if (result != ASTRA_OK) return result;
        panel->selected_depth = depth;
        if (depth + 1u < panel->column_count) {
            result = astra_interface_file_panel_set_column(panel, context,
                controls, depth + 1u, NULL, NULL, 0u, NULL, 0u);
            if (result != ASTRA_OK) return result;
        }
        if (entry->kind == ASTRA_VFS_KIND_DIRECTORY) {
            *outcome = depth + 1u < panel->column_count ?
                ASTRA_FILE_PANEL_LOAD_CHILD : ASTRA_FILE_PANEL_NAVIGATE;
            return ASTRA_OK;
        }
        if (entry->kind != ASTRA_VFS_KIND_FILE)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (panel->mode == ASTRA_FILE_PANEL_SAVE)
            return file_panel_fill_name(panel, context, controls);
        if (action->type == ASTRA_UI_ACTION_ACTIVATE)
            *outcome = ASTRA_FILE_PANEL_PICKED;
        return ASTRA_OK;
    }
    if (action->control_id < panel->first_id ||
        action->control_id - panel->first_id >= ASTRA_FILE_PANEL_CONTROL_COUNT)
        return ASTRA_OK;
    at = action->control_id - panel->first_id;
    if (action->type == ASTRA_UI_ACTION_TEXT_CHANGED &&
        at == ASTRA_FILE_PANEL_SEARCH) {
        AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
        char previous[ASTRA_VFS_NAME_MAX];
        uint32_t old_length = panel->search_length;
        uint32_t bytes;
        result = astra_text_model_get_state(&panel->search_model, &state);
        if (result != ASTRA_OK) return result;
        if (state.text_bytes >= sizeof(panel->search_query))
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        memcpy(previous, panel->search_query, old_length + 1u);
        result = astra_text_model_copy(&panel->search_model, 0u,
            state.text_bytes, panel->search_query,
            sizeof(panel->search_query) - 1u, &bytes);
        if (result != ASTRA_OK) return result;
        panel->search_query[bytes] = '\0';
        panel->search_length = bytes;
        result = file_panel_refresh(panel, context, controls);
        if (result != ASTRA_OK) {
            memcpy(panel->search_query, previous, old_length + 1u);
            panel->search_length = old_length;
        }
        return result;
    }
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        at == ASTRA_FILE_PANEL_TYPE) {
        uint32_t previous = panel->selected_filter;
        if (action->value < 0 ||
            (uint64_t)action->value >= panel->type_filter_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        panel->selected_filter = (uint32_t)action->value;
        result = file_panel_refresh(panel, context, controls);
        if (result != ASTRA_OK) panel->selected_filter = previous;
        return result;
    }
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        at == ASTRA_FILE_PANEL_VIEW) {
        AstraCollectionModel *old_collection = panel->view_mode ==
            ASTRA_FILE_PANEL_VIEW_LIST ? panel->collection :
            panel->view_mode == ASTRA_FILE_PANEL_VIEW_GRID ?
                &panel->grid_collection : panel->columns[0].model;
        uint32_t transfer = panel->view_mode !=
            ASTRA_FILE_PANEL_VIEW_COLUMNS || panel->selected_depth == 0u;
        uint32_t next;
        if (action->value < 0 || action->value >
            (panel->column_count != 0u ? 2 : 1))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        next = (uint32_t)action->value;
        result = astra_interface_ui_set_state(context,
            &controls[ASTRA_FILE_PANEL_SCROLL],
            next == ASTRA_FILE_PANEL_VIEW_LIST ? 0u :
                ASTRA_CONTROL_COLLAPSED);
        if (result != ASTRA_OK) return result;
        result = astra_interface_ui_set_state(context,
            &controls[ASTRA_FILE_PANEL_GRID_SCROLL],
            next == ASTRA_FILE_PANEL_VIEW_GRID ? 0u :
                ASTRA_CONTROL_COLLAPSED);
        if (result != ASTRA_OK) return result;
        if (panel->column_count != 0u) {
            result = astra_interface_ui_set_state(context,
                &controls[ASTRA_FILE_PANEL_CONTROL_COUNT],
                next == ASTRA_FILE_PANEL_VIEW_COLUMNS ? 0u :
                    ASTRA_CONTROL_COLLAPSED);
            if (result != ASTRA_OK) return result;
        }
        panel->view_mode = next;
        if (transfer != 0u)
            return astra_interface_collection_mirror_primary(context,
                &controls[next == ASTRA_FILE_PANEL_VIEW_LIST ?
                    ASTRA_FILE_PANEL_LIST :
                    next == ASTRA_FILE_PANEL_VIEW_GRID ?
                        ASTRA_FILE_PANEL_GRID :
                        file_panel_column_list_index(0u)], old_collection);
        return ASTRA_OK;
    }
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        (at == ASTRA_FILE_PANEL_LIST || at == ASTRA_FILE_PANEL_GRID)) {
        AstraCollectionModel *source = at == ASTRA_FILE_PANEL_LIST ?
            panel->collection : &panel->grid_collection;
        return astra_interface_collection_mirror_primary(context,
            &controls[at == ASTRA_FILE_PANEL_LIST ?
                ASTRA_FILE_PANEL_GRID : ASTRA_FILE_PANEL_LIST], source);
    }
    if ((action->type == ASTRA_UI_ACTION_VALUE_CHANGED ||
         action->type == ASTRA_UI_ACTION_ACTIVATE) &&
        at == ASTRA_FILE_PANEL_PLACES_LIST) {
        if (action->value < 0 || (uint64_t)action->value >= panel->place_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (!astra_string_copy(panel->target, sizeof(panel->target),
                panel->places[action->value].path))
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        panel->leaf[0] = '\0';
        *outcome = ASTRA_FILE_PANEL_NAVIGATE;
        return ASTRA_OK;
    }
    if (action->type != ASTRA_UI_ACTION_ACTIVATE) return ASTRA_OK;
    if (at == ASTRA_FILE_PANEL_CANCEL) {
        panel->leaf[0] = '\0';
        panel->target[0] = '\0';
        *outcome = ASTRA_FILE_PANEL_CANCELLED;
        return ASTRA_OK;
    }
    if (at >= ASTRA_FILE_PANEL_CRUMB0 &&
        at <= ASTRA_FILE_PANEL_CRUMB7) {
        result = astra_interface_breadcrumb_target(&panel->breadcrumb,
            action->control_id, panel->target, sizeof(panel->target));
        if (result != ASTRA_OK) return result;
        panel->leaf[0] = '\0';
        *outcome = ASTRA_FILE_PANEL_NAVIGATE;
        return ASTRA_OK;
    }
    if (at == ASTRA_FILE_PANEL_UP) {
        const char *slash = NULL;
        size_t length;
        for (const char *cursor = panel->directory; *cursor != '\0'; ++cursor)
            if (*cursor == '/') slash = cursor;
        if (slash == NULL || panel->directory[1] == '\0')
            return ASTRA_OK;
        if (slash == panel->directory) {
            panel->target[0] = '/';
            panel->target[1] = '\0';
            panel->leaf[0] = '\0';
            *outcome = ASTRA_FILE_PANEL_NAVIGATE;
            return ASTRA_OK;
        }
        length = (size_t)(slash - panel->directory);
        if (length >= sizeof(panel->target))
            return ASTRA_ERROR_BUFFER_TOO_SMALL;
        memcpy(panel->target, panel->directory, length);
        panel->target[length] = '\0';
        panel->leaf[0] = '\0';
        *outcome = ASTRA_FILE_PANEL_NAVIGATE;
        return ASTRA_OK;
    }
    if (at == ASTRA_FILE_PANEL_LIST || at == ASTRA_FILE_PANEL_GRID) {
        if (action->value < 0 || (uint64_t)action->value >= panel->item_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = file_panel_select(panel,
            (uint32_t)panel->items[(uint32_t)action->value].id - 1u,
            outcome);
        if (result != ASTRA_OK || panel->mode != ASTRA_FILE_PANEL_SAVE ||
            *outcome == ASTRA_FILE_PANEL_NAVIGATE)
            return result;
        return file_panel_fill_name(panel, context, controls);
    }
    if (at != ASTRA_FILE_PANEL_PRIMARY)
        return ASTRA_OK;
    if (panel->mode == ASTRA_FILE_PANEL_SAVE)
        return file_panel_save_name(panel, outcome);
    if (panel->view_mode == ASTRA_FILE_PANEL_VIEW_COLUMNS) {
        AstraFilePanelColumnSnapshot *snapshot =
            &panel->column_snapshots[panel->selected_depth];
        AstraCollectionModel *model =
            panel->columns[panel->selected_depth].model;
        AstraUIAction selected = *action;
        if (model->selected_id == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t row = 0u; row < snapshot->item_count; ++row)
            if (snapshot->items[row].id == model->selected_id) {
                selected.control_id = panel->first_id +
                    file_panel_column_list_index(panel->selected_depth);
                selected.value = row;
                return astra_interface_file_panel_action(panel, context,
                    controls, &selected, outcome);
            }
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    {
    AstraCollectionModel *active = panel->view_mode == ASTRA_FILE_PANEL_VIEW_LIST ?
        panel->collection : &panel->grid_collection;
    if (active->selected_id == 0u ||
        active->selected_id > panel->entry_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return file_panel_select(panel,
        (uint32_t)active->selected_id - 1u, outcome);
    }
}
