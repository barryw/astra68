#include <astra/application.h>
#include <astra/application_catalog.h>
#include <astra/area.h>
#include <astra/clipboard.h>
#include <astra/draw_list.h>
#include <astra/fonts.h>
#include <astra/graphics_library.h>
#include <astra/input.h>
#include <astra/icon_tile.h>
#include <astra/inline_edit.h>
#include <astra/interface_library.h>
#include <astra/menu.h>
#include <astra/program.h>
#include <astra/package_library.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/system_panels.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/vfs_clipboard.h>
#include <astra/vfs_path.h>
#include <astra/window.h>
#include <astra/volume.h>
#include <astra/ascii.h>
#include <astra/result.h>

#include "../../common/directory_snapshot.h"
#include "../../common/folder_ops.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CELL_WIDTH ASTRA_ICON_TILE_CLIENT_WIDTH
#define CELL_HEIGHT ASTRA_ICON_TILE_CLIENT_HEIGHT
#define GRID_PADDING 16u
#define GRID_GAP_X 8u
#define GRID_GAP_Y 14u
#define ICON_SOURCE_SIZE ASTRA_ICON_TILE_SOURCE_SIZE
#define TOOLBAR_HEIGHT (ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT + \
    2u * ASTRA_THEME_TOOLBAR_INSET)
#define TOOLBAR_FIRST_ID 10000u
#define TOOLBAR_CONTROL_COUNT 10u
#define BROWSER_TOOL_COUNT 2u
#define SORT_CONTROL_INDEX TOOLBAR_CONTROL_COUNT
#define HIDDEN_CONTROL_INDEX (TOOLBAR_CONTROL_COUNT + 1u)
#define SORT_CONTROL_ID (TOOLBAR_FIRST_ID + 100u)
#define HIDDEN_CONTROL_ID (TOOLBAR_FIRST_ID + 101u)
#define NAVIGATION_HISTORY_MAX 32u
#define NAVIGATION_STAY (UINT32_MAX - 1u)
#define NAVIGATION_PUSH UINT32_MAX
#define BROWSER_COLUMNS 3u
#define BROWSER_PANEL_CONTROL_COUNT ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(BROWSER_COLUMNS)
#define BROWSER_CONTROL_COUNT (BROWSER_PANEL_CONTROL_COUNT + ASTRA_STATUS_BAR_CONTROL_COUNT)
#define UI_CONTROL_COUNT (TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + BROWSER_CONTROL_COUNT)
#define BROWSER_CONTROL(index) (&controls[TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + (index)])
#define BROWSER_INITIAL_CAPACITY 32u

ASTRA_PROGRAM("Atlas", 0, 1, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraDirectorySnapshot directory = ASTRA_DIRECTORY_SNAPSHOT_INIT;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraDisplay display = ASTRA_DISPLAY_INIT;
static AstraSurface content = ASTRA_SURFACE_INIT;
static AstraSurface folder_icon = ASTRA_SURFACE_INIT;
static AstraSurface file_icon = ASTRA_SURFACE_INIT;
static AstraSurface installer_icon = ASTRA_SURFACE_INIT;
static AstraSurface *entry_icons;
static uint32_t entry_icon_count;
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraToolbarActions toolbar = ASTRA_TOOLBAR_ACTIONS_INIT;
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraControl *controls;
static AstraFilePanel browser = ASTRA_FILE_PANEL_INIT;
static AstraCollectionModel browser_collection = ASTRA_COLLECTION_MODEL_INIT;
static AstraSelectionSet browser_selection = ASTRA_SELECTION_SET_INIT;
static uint64_t *browser_selected_ids;
static AstraCollectionModel column_models[BROWSER_COLUMNS];
static AstraScrollModel column_scrolls[BROWSER_COLUMNS];
static AstraColumnView browser_columns[BROWSER_COLUMNS];
static AstraFilePanelColumnSnapshot column_views[BROWSER_COLUMNS];
static AstraDirectorySnapshot child_directories[BROWSER_COLUMNS - 1u];
static AstraCollectionItem *child_items[BROWSER_COLUMNS - 1u];
static char child_paths[BROWSER_COLUMNS - 1u][ASTRA_VFS_PATH_MAX];
static AstraCollectionItem *browser_items[2];
static const AstraSurface **browser_grid_icons;
static const AstraSurface **browser_entry_icons;
static uint32_t browser_capacity;
static AstraFilePanelPlace initial_places[3];
static AstraCollectionItem initial_place_items[3];
static AstraFilePanelPlace *browser_places = initial_places;
static AstraCollectionItem *browser_place_items = initial_place_items;
static AstraVolumeSnapshot sidebar_volumes = ASTRA_VOLUME_SNAPSHOT_INIT;
static _Alignas(4) uint32_t browser_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
static char browser_home[ASTRA_VFS_PATH_MAX];
static char browser_status[64];
static char browser_space[80];
static uint32_t worker, cancelled, completed, visible_data;
static uint16_t width = 1300u, height = 640u;
static uint32_t sort_mode = ASTRA_DIRECTORY_SORT_NAME, show_hidden;
static const char *root;
static char current_path[ASTRA_VFS_PATH_MAX];
static char *navigation_history[NAVIGATION_HISTORY_MAX];
static AstraVolumeReference navigation_history_volumes[NAVIGATION_HISTORY_MAX];
static uint32_t navigation_count, navigation_index;
static AstraVolumeReference viewed_volume;
static AstraVolumeWatch volume_watch = ASTRA_VOLUME_WATCH_INIT;
static AstraHandle volume_metadata, volume_grant;
static AstraHandle volume_read_broker, volume_write_broker;
static AstraHandle volume_ram_read_broker, volume_ram_write_broker;
static uint32_t volume_lost, volume_refresh;
static uint32_t volume_write_available, volume_writable;
static uint64_t volume_retry;
static char volume_path[32], volume_title[ASTRA_WINDOW_TITLE_MAX + 1u];
static AstraHandle launcher;
static AstraHandle clipboard_service;
static AstraInlineEdit name_edit = ASTRA_INLINE_EDIT_INIT;
static uint32_t edit_index = UINT32_MAX;
static uint32_t edit_depth;
static char edit_path[ASTRA_VFS_PATH_MAX];
static char edit_parent[ASTRA_VFS_PATH_MAX];
static _Alignas(4) uint32_t edit_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
static char selected_path[ASTRA_VFS_PATH_MAX];
static uint32_t selected_kind, selected_depth, selected_row = UINT32_MAX;

static void selection_clear(void)
{
    selected_path[0] = '\0';
    selected_kind = 0u;
    selected_depth = 0u;
    selected_row = UINT32_MAX;
}

static AstraResult selection_set(const char *parent,
    const AstraDirectoryEntry *entries, uint32_t count, uint32_t index,
    uint32_t depth)
{
    selection_clear();
    if (index >= count || entries == NULL || parent == NULL)
        return ASTRA_OK;
    uint32_t kind = entries[index].kind;
    if (kind != ASTRA_VFS_KIND_FILE && kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_OK;
    uint32_t status = astra_folder_path(parent, entries[index].name,
        selected_path, sizeof(selected_path));
    if (status != ASTRA_VFS_OK) return astra_result_from_service(status);
    selected_kind = kind;
    selected_depth = depth;
    selected_row = index;
    return ASTRA_OK;
}

enum AtlasMenuRow {
    ATLAS_MENU_FILE,
    ATLAS_MENU_VIEW,
    ATLAS_MENU_HELP,
    ATLAS_MENU_NEW,
    ATLAS_MENU_OPEN,
    ATLAS_MENU_COPY,
    ATLAS_MENU_PASTE,
    ATLAS_MENU_RENAME,
    ATLAS_MENU_DELETE,
    ATLAS_MENU_REFRESH,
    ATLAS_MENU_CLOSE,
    ATLAS_MENU_LIST,
    ATLAS_MENU_GRID,
    ATLAS_MENU_COLUMNS,
    ATLAS_MENU_SEPARATOR,
    ATLAS_MENU_SORT_NAME,
    ATLAS_MENU_SORT_SIZE,
    ATLAS_MENU_SORT_KIND,
    ATLAS_MENU_SORT_MODIFIED,
    ATLAS_MENU_HIDDEN,
    ATLAS_MENU_ABOUT,
    ATLAS_MENU_COUNT
};

static AstraMenuItem atlas_menu[ATLAS_MENU_COUNT] = {
    {"File", 4u, 0u}, {"View", 4u, 0u}, {"Help", 4u, 0u},
    {"New Drawer", 10u, ASTRA_MENU_PARENT(1u)},
    {"Open", 4u, ASTRA_MENU_PARENT(1u)},
    {"Copy", 4u, ASTRA_MENU_PARENT(1u)},
    {"Paste", 5u, ASTRA_MENU_PARENT(1u)},
    {"Rename", 6u, ASTRA_MENU_PARENT(1u)},
    {"Move to Trash", 13u, ASTRA_MENU_PARENT(1u)},
    {"Refresh", 7u, ASTRA_MENU_PARENT(1u)},
    {"Close Window", 12u, ASTRA_MENU_PARENT(1u)},
    {"as List", 7u, ASTRA_MENU_PARENT(2u)},
    {"as Grid", 7u, ASTRA_MENU_PARENT(2u)},
    {"as Columns", 10u, ASTRA_MENU_PARENT(2u)},
    {NULL, 0u, ASTRA_MENU_PARENT(2u) | ASTRA_MENU_SEPARATOR},
    {"Sort by Name", 12u, ASTRA_MENU_PARENT(2u)},
    {"Sort by Size", 12u, ASTRA_MENU_PARENT(2u)},
    {"Sort by Kind", 12u, ASTRA_MENU_PARENT(2u)},
    {"Sort by Modified", 16u, ASTRA_MENU_PARENT(2u)},
    {"Show Hidden Files", 17u, ASTRA_MENU_PARENT(2u)},
    {"About Atlas", 11u, ASTRA_MENU_PARENT(3u)}
};
static uint32_t atlas_menu_sent[ATLAS_MENU_COUNT];
static uint32_t atlas_menu_ready;

static const char *message(void);
static AstraResult init_toolbar(AstraHandle gui);
static AstraResult fit_toolbar(void);
static AstraResult update_window_title(void);
static int fixed_copy_path(const char *path);

static uint32_t prepare_directory(AstraDirectorySnapshot *snapshot)
{
    if (!show_hidden) astra_directory_snapshot_filter_hidden(snapshot);
    return astra_directory_snapshot_sort(snapshot, sort_mode);
}

static void navigation_dispose(void)
{
    for (uint32_t at = 0u; at < navigation_count; ++at)
        astra_runtime_deallocate(navigation_history[at]);
    navigation_count = 0u;
    navigation_index = 0u;
}

static void navigation_commit(char *copy,
    const AstraVolumeReference *reference)
{
    if (navigation_count == 0u) {
        navigation_history[0] = copy;
        navigation_history_volumes[0] = *reference;
        navigation_count = 1u;
        navigation_index = 0u;
        return;
    }
    for (uint32_t at = navigation_index + 1u; at < navigation_count; ++at)
        astra_runtime_deallocate(navigation_history[at]);
    navigation_count = navigation_index + 1u;
    if (navigation_count == NAVIGATION_HISTORY_MAX) {
        astra_runtime_deallocate(navigation_history[0]);
        memmove(&navigation_history[0], &navigation_history[1],
            (NAVIGATION_HISTORY_MAX - 1u) * sizeof(navigation_history[0]));
        memmove(&navigation_history_volumes[0],
            &navigation_history_volumes[1],
            (NAVIGATION_HISTORY_MAX - 1u) *
                sizeof(navigation_history_volumes[0]));
        --navigation_count;
    }
    navigation_history[navigation_count] = copy;
    navigation_history_volumes[navigation_count] = *reference;
    navigation_index = navigation_count++;
}

static AstraResult navigation_append(const char *path,
    const AstraVolumeReference *reference)
{
    size_t length = strlen(path);
    char *copy = astra_runtime_allocate(length + 1u);
    if (copy == NULL) return ASTRA_ERROR_NO_RESOURCES;
    memcpy(copy, path, length + 1u);
    navigation_commit(copy, reference);
    return ASTRA_OK;
}

static const char *writable_mount_root(void)
{
    if (strncmp(root, "/home", 5u) == 0 &&
        (root[5] == '\0' || root[5] == '/')) return "/home";
    if (viewed_volume.id != 0u && volume_path[0] != '\0' &&
        strncmp(root, volume_path, strlen(volume_path)) == 0 &&
        (root[strlen(volume_path)] == '\0' ||
         root[strlen(volume_path)] == '/'))
        return volume_writable ? volume_path : NULL;
    if (!volume_write_available) return NULL;
    for (uint32_t at = 0u; at < sidebar_volumes.count; ++at) {
        const AstraVolumeRecord *record = &sidebar_volumes.records[at];
        size_t length = strlen(record->name);
        if (record->kind == ASTRA_VOLUME_MOUNT &&
            record->state == ASTRA_VOLUME_MOUNTED &&
            (record->flags & (ASTRA_VOLUME_READ_ONLY | ASTRA_VOLUME_SYSTEM |
                ASTRA_VOLUME_TEMPORARY)) == 0u &&
            length != 0u && strncmp(root, record->name, length) == 0 &&
            (root[length] == '\0' || root[length] == '/'))
            return record->name;
    }
    return NULL;
}

static int writable_folder_root(void)
{
    return writable_mount_root() != NULL;
}

static AstraFilesystem *active_filesystem(void)
{
    return &filesystem.filesystem;
}

static AstraResult volume_observe(AstraVolumeRecord *record)
{
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(volume_metadata, &snapshot);
    if (result == ASTRA_OK) {
        result = ASTRA_ERROR_INVALID_HANDLE;
        if (snapshot.epoch == viewed_volume.epoch)
            for (uint32_t at = 0u; at < snapshot.count; ++at) {
                const AstraVolumeRecord *row = &snapshot.records[at];
                if (row->id == viewed_volume.id && row->generation == viewed_volume.generation &&
                    row->kind == ASTRA_VOLUME_MOUNT &&
                    (row->state == ASTRA_VOLUME_MOUNTED || row->state == ASTRA_VOLUME_QUIESCING)) {
                    if (record != NULL) *record = *row;
                    result = ASTRA_OK;
                    break;
                }
            }
    }
    astra_volume_snapshot_close(&snapshot);
    return result;
}

static void volume_release(void)
{
    if (volume_grant != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&volume_grant);
        (void)ignored;
    }
}

static void close_grant(AstraHandle *grant)
{
    if (*grant != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(grant);
        (void)ignored;
    }
}

static void volume_close(void)
{
    if (volume_watch.receive != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_volume_watch_close(volume_metadata,
            &volume_watch);
        (void)ignored;
    }
    volume_release();
}

static AstraResult volume_open(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    const AstraStartupCapability *broker = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_READ);
    if (metadata == NULL || broker == NULL) return ASTRA_ERROR_PERMISSION;
    volume_metadata = metadata->handle;
    AstraResult result = astra_volume_watch_open(volume_metadata, &volume_watch);
    AstraVolumeRecord record = {0};
    if (result == ASTRA_OK) result = volume_observe(&record);
    if (result == ASTRA_OK && record.name[0] != '/') result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) {
        memcpy(volume_path, record.name, sizeof(volume_path));
        root = volume_path;
        const char *label = record.label[0] != '\0' ? record.label : record.name + 1u;
        uint32_t length = (uint32_t)strlen(label);
        uint32_t kept = astra_string_utf8_prefix(label, length, ASTRA_WINDOW_TITLE_MAX);
        memcpy(volume_title, label, kept);
        volume_title[kept] = '\0';
        if (record.label[0] == '\0')
            for (uint32_t at = 0u; at < kept; ++at) volume_title[at] = astra_ascii_upper(volume_title[at]);
        if ((record.flags & ASTRA_VOLUME_SYSTEM) != 0u) {
            /* System browsing retains its explicit startup authority. Public
             * root brokers cannot expand an application's system grants. */
            if (strcmp(root, "/dh0") != 0) result = ASTRA_ERROR_PERMISSION;
            else viewed_volume = (AstraVolumeReference){0u, 0u, 0u};
        } else {
            AstraHandle read_broker = record.origin == ASTRA_VOLUME_ORIGIN_RAM ?
                volume_ram_read_broker : volume_read_broker;
            AstraHandle write_broker = record.origin == ASTRA_VOLUME_ORIGIN_RAM ?
                volume_ram_write_broker : volume_write_broker;
            volume_writable = write_broker != ASTRA_INVALID_HANDLE &&
                (record.flags & ASTRA_VOLUME_READ_ONLY) == 0u;
            result = astra_volume_root(read_broker, &viewed_volume,
                ASTRA_RIGHT_READ, &volume_grant);
        }
    }
    if (result != ASTRA_OK) volume_close();
    return result;
}

/* Keep snapshot strings alive as long as File Panel borrows sidebar rows. */
static AstraResult browser_update_places(void)
{
    AstraVolumeSnapshot next = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(volume_metadata, &next);
    if (result != ASTRA_OK) return result;
    if (next.count > (SIZE_MAX / (sizeof(AstraFilePanelPlace) +
            sizeof(next.records[0].name))) - 3u ||
        next.count > UINT32_MAX - 3u) {
        astra_volume_snapshot_close(&next);
        return ASTRA_ERROR_NO_RESOURCES;
    }
    uint32_t capacity = next.count + 3u;
    AstraFilePanelPlace *places = astra_runtime_allocate((size_t)capacity *
        (sizeof(*places) + sizeof(next.records[0].name)));
    AstraCollectionItem *items = astra_runtime_allocate(
        (size_t)capacity * sizeof(*items));
    if (places == NULL || items == NULL) {
        astra_runtime_deallocate(places);
        astra_runtime_deallocate(items);
        astra_volume_snapshot_close(&next);
        return ASTRA_ERROR_NO_RESOURCES;
    }
    uint32_t count = 0u;
    char *fallback_labels = (char *)(places + capacity);
    places[count++] = (AstraFilePanelPlace){"Home", 4u, "/home"};
    places[count++] = (AstraFilePanelPlace){"Applications", 12u, "/apps"};
    places[count++] = (AstraFilePanelPlace){"DH0", 3u, "/dh0"};
    for (uint32_t at = 0u; at < next.count; ++at) {
        const AstraVolumeRecord *record = &next.records[at];
        if (record->kind != ASTRA_VOLUME_MOUNT ||
            record->state != ASTRA_VOLUME_MOUNTED ||
            (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u ||
            record->origin == ASTRA_VOLUME_ORIGIN_EXPORT ||
            record->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE ||
            record->name[0] != '/') continue;
        const char *label = record->label;
        if (label[0] == '\0') {
            char *fallback = fallback_labels +
                (size_t)count * sizeof(record->name);
            if (!astra_string_copy(fallback, sizeof(record->name),
                    record->name + 1u)) continue;
            for (uint32_t index = 0u; fallback[index] != '\0'; ++index)
                fallback[index] = astra_ascii_upper(fallback[index]);
            label = fallback;
        }
        places[count++] = (AstraFilePanelPlace){label,
            (uint32_t)strlen(label), record->name};
    }
    result = astra_interface_file_panel_set_places(&browser, &ui,
        BROWSER_CONTROL(0), places, count, items, capacity);
    if (result == ASTRA_OK) {
        if (browser_places != initial_places)
            astra_runtime_deallocate(browser_places);
        if (browser_place_items != initial_place_items)
            astra_runtime_deallocate(browser_place_items);
        astra_volume_snapshot_close(&sidebar_volumes);
        browser_places = places;
        browser_place_items = items;
        sidebar_volumes = next;
        result = update_window_title();
    } else {
        astra_runtime_deallocate(places);
        astra_runtime_deallocate(items);
        astra_volume_snapshot_close(&next);
    }
    return result;
}

static void read_root(uint32_t argument)
{
    (void)argument;
    uint32_t status = astra_directory_snapshot_load(&directory,
                                &filesystem.filesystem, root,
                                &cancelled);
    if (status == ASTRA_VFS_OK) directory.status = prepare_directory(&directory);
    __atomic_store_n(&completed, 1u, __ATOMIC_RELEASE);
}

static AstraResult load_icon(const char *path, AstraSurface *surface)
{
    void *bytes = NULL;
    uint32_t length = 0u;
    AstraResult result;

    if (astra_process_read_file_alloc(&filesystem, path, &bytes, &length) !=
        ASTRA_VFS_OK)
        return ASTRA_ERROR_IO;
    result = astra_surface_from_aicon(&display, bytes, length,
                                      ICON_SOURCE_SIZE, surface);
    astra_runtime_deallocate(bytes);
    return result;
}

static void clear_entry_icons(void)
{
    for (uint32_t at = 0u; at < entry_icon_count; ++at)
        if (entry_icons[at]._private_handle != ASTRA_INVALID_HANDLE) {
            AstraResult closed = astra_surface_close(&entry_icons[at]);
            (void)closed;
        }
    astra_runtime_deallocate(entry_icons);
    entry_icons = NULL;
    entry_icon_count = 0u;
}

static void load_entry_icons(void)
{
    AstraApplicationCatalog catalog = ASTRA_APPLICATION_CATALOG_INIT;
    clear_entry_icons();
    if (directory.status != ASTRA_VFS_OK || directory.count == 0u ||
        directory.count > SIZE_MAX / sizeof(*entry_icons)) return;
    entry_icons = astra_runtime_allocate(directory.count * sizeof(*entry_icons));
    if (entry_icons == NULL) return;
    entry_icon_count = directory.count;
    for (uint32_t at = 0u; at < entry_icon_count; ++at)
        entry_icons[at] = (AstraSurface)ASTRA_SURFACE_INIT;
    (void)astra_application_catalog_load(&filesystem, root, &catalog);
    for (uint32_t at = 0u; at < directory.count; ++at) {
        const AstraDirectoryEntry *entry = &directory.entries[at];
        if (entry->kind == ASTRA_VFS_KIND_DIRECTORY &&
            astra_string_has_suffix(entry->name, ".app")) {
            for (uint32_t found = 0u; found < catalog.count; ++found) {
                const char *leaf = astra_folder_leaf(catalog.entries[found].bundle);
                if (strcmp(leaf, entry->name) == 0) {
                    (void)load_icon(catalog.entries[found].icon,
                                    &entry_icons[at]);
                    break;
                }
            }
        } else if (entry->kind == ASTRA_VFS_KIND_FILE &&
                   astra_string_has_suffix(entry->name, ".apkg")) {
            char path[ASTRA_VFS_PATH_MAX];
            void *bytes = NULL;
            uint32_t length = 0u;
            if (astra_folder_path(root, entry->name, path,
                    sizeof(path)) == ASTRA_VFS_OK &&
                astra_package_read_icon(&filesystem.filesystem, path, &bytes,
                    &length) == ASTRA_OK)
                (void)astra_surface_from_aicon(&display, bytes, length,
                    ICON_SOURCE_SIZE, &entry_icons[at]);
            astra_runtime_deallocate(bytes);
        }
    }
    astra_application_catalog_destroy(&catalog);
}

static AstraResult layout(void)
{
    AstraFlexLayout outer = ASTRA_FLEX_LAYOUT_INIT;
    uint32_t grid_width = width > 166u ? width - 166u : CELL_WIDTH;
    uint32_t columns = grid_width / CELL_WIDTH;
    if (columns == 0u) columns = 1u;
    browser.grid_model.columns = columns;
    outer.direction = ASTRA_FLEX_COLUMN;
    outer.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    outer.padding_left = outer.padding_right = 0u;
    outer.padding_top = outer.padding_bottom = 0u;
    return astra_interface_ui_layout(&ui, &outer);
}

static AstraResult build_view(AstraHandle gui)
{
    AstraFilePanelInfo info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraStatusBarInfo status = ASTRA_STATUS_BAR_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraScrollModelInfo column_scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraUIAction view_action = ASTRA_UI_ACTION_INIT;
    uint32_t outcome;

    controls = astra_runtime_allocate(UI_CONTROL_COUNT * sizeof(*controls));
    browser_items[0] = astra_runtime_allocate(BROWSER_INITIAL_CAPACITY *
        sizeof(*browser_items[0]));
    browser_items[1] = astra_runtime_allocate(BROWSER_INITIAL_CAPACITY *
        sizeof(*browser_items[1]));
    browser_grid_icons = astra_runtime_allocate(BROWSER_INITIAL_CAPACITY *
        sizeof(*browser_grid_icons));
    browser_selected_ids = astra_runtime_allocate(BROWSER_INITIAL_CAPACITY *
        sizeof(*browser_selected_ids));
    if (controls == NULL || browser_items[0] == NULL ||
        browser_items[1] == NULL || browser_grid_icons == NULL ||
        browser_selected_ids == NULL)
        return ASTRA_ERROR_NO_RESOURCES;
    browser_capacity = BROWSER_INITIAL_CAPACITY;
    for (uint32_t at = 0u; at < UI_CONTROL_COUNT; ++at)
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    if (init_toolbar(gui) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    static const AstraChoiceItem sort_choices[] = {
        {"Name", 4u, {0u, 0u}},
        {"Size", 4u, {0u, 0u}},
        {"Kind", 4u, {0u, 0u}},
        {"Modified", 8u, {0u, 0u}}
    };
    AstraChoiceInfo sort = ASTRA_CHOICE_INFO_INIT;
    AstraToggleInfo hidden = ASTRA_TOGGLE_INFO_INIT;
    sort.id = SORT_CONTROL_ID;
    sort.items = sort_choices;
    sort.item_count = sizeof(sort_choices) / sizeof(sort_choices[0]);
    sort.selected = sort_mode;
    sort.state = ASTRA_CONTROL_DISABLED;
    hidden.id = HIDDEN_CONTROL_ID;
    hidden.text = "Hidden files";
    hidden.text_length = sizeof("Hidden files") - 1u;
    hidden.state = ASTRA_CONTROL_DISABLED;
    item.parent_id = TOOLBAR_FIRST_ID;
    item.shrink = 0u;
    if (astra_interface_popup_init(&controls[SORT_CONTROL_INDEX], &sort) != ASTRA_OK ||
        astra_interface_checkbox_init(&controls[HIDDEN_CONTROL_INDEX], &hidden) != ASTRA_OK ||
        astra_interface_control_set_flex(&controls[SORT_CONTROL_INDEX], &item) != ASTRA_OK ||
        astra_interface_control_set_flex(&controls[HIDDEN_CONTROL_INDEX], &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < BROWSER_COLUMNS; ++at) {
        column_models[at] = (AstraCollectionModel)ASTRA_COLLECTION_MODEL_INIT;
        column_scrolls[at] = (AstraScrollModel)ASTRA_SCROLL_MODEL_INIT;
        column_scroll.line_width = 8u;
        column_scroll.line_height = 16u;
        if (astra_scroll_init(&column_scrolls[at], &column_scroll) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        browser_columns[at] = (AstraColumnView){
            &column_models[at], &column_scrolls[at], 160u, {0u, 0u}};
    }
    if (!astra_string_copy(browser_home, sizeof(browser_home),
            root))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    initial_places[0] = (AstraFilePanelPlace){
        volume_title[0] != '\0' ? volume_title : "DH0",
        (uint32_t)strlen(volume_title[0] != '\0' ? volume_title : "DH0"),
        browser_home};
    initial_places[1] = (AstraFilePanelPlace){"Home", 4u, "/home"};
    initial_places[2] = (AstraFilePanelPlace){"Applications", 12u,
        "/apps"};
    info.first_id = 1u;
    info.mode = ASTRA_FILE_PANEL_BROWSE;
    info.directory = root;
    info.item_storage = browser_items[0];
    info.item_capacity = browser_capacity;
    info.spare_item_storage = browser_items[1];
    info.spare_item_capacity = browser_capacity;
    info.grid_icons = browser_grid_icons;
    info.grid_icon_capacity = browser_capacity;
    info.places = browser_places;
    info.place_count = 3u;
    info.place_items = browser_place_items;
    info.place_capacity = 3u;
    info.collection = &browser_collection;
    info.scroll = &scroll;
    info.columns = browser_columns;
    info.column_snapshots = column_views;
    info.column_count = BROWSER_COLUMNS;
    info.control_capacity = BROWSER_PANEL_CONTROL_COUNT;
    if (astra_interface_file_panel_init(&browser,
            BROWSER_CONTROL(0), &info) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (astra_interface_selection_init(&browser_selection,
            browser_selected_ids, browser_capacity) != ASTRA_OK ||
        astra_interface_collection_multi_bind(&browser_collection,
            &browser_selection) != ASTRA_OK ||
        astra_interface_collection_multi_bind(&browser.grid_collection,
            &browser_selection) != ASTRA_OK ||
        astra_interface_collection_multi_bind(&column_models[0],
            &browser_selection) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status.first_id = 1u + BROWSER_PANEL_CONTROL_COUNT;
    status.left = "Reading volume...";
    status.left_length = sizeof("Reading volume...") - 1u;
    status.right = "Atlas";
    status.right_length = 5u;
    if (astra_interface_status_bar_init(
            BROWSER_CONTROL(BROWSER_PANEL_CONTROL_COUNT), &status) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = 1u;
    if (astra_interface_control_set_flex(
            BROWSER_CONTROL(BROWSER_PANEL_CONTROL_COUNT), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = ASTRA_FILE_PANEL_CRUMB0;
         at <= ASTRA_FILE_PANEL_CRUMB7; ++at) {
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = TOOLBAR_FIRST_ID;
        item.shrink = 0u;
        if (astra_interface_control_set_flex(BROWSER_CONTROL(at),
                &item) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t at = ASTRA_FILE_PANEL_VIEW;
         at <= ASTRA_FILE_PANEL_SEARCH; ++at) {
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = TOOLBAR_FIRST_ID;
        item.grow = at == ASTRA_FILE_PANEL_SEARCH ? 1u : 0u;
        item.shrink = at == ASTRA_FILE_PANEL_SEARCH ? 1u : 0u;
        if (astra_interface_control_set_flex(BROWSER_CONTROL(at),
                &item) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.grow = 1u;
    if (astra_interface_control_set_flex(BROWSER_CONTROL(ASTRA_FILE_PANEL_ROOT),
            &item) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, UI_CONTROL_COUNT,
            width, height) != ASTRA_OK ||
        astra_interface_ui_set_state(&ui,
            BROWSER_CONTROL(ASTRA_FILE_PANEL_LOCATION_ROW),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK ||
        astra_interface_ui_set_state(&ui,
            BROWSER_CONTROL(ASTRA_FILE_PANEL_TOOLS),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK ||
        layout() != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    view_action.type = ASTRA_UI_ACTION_VALUE_CHANGED;
    view_action.control_id = 1u + ASTRA_FILE_PANEL_VIEW;
    view_action.value = ASTRA_FILE_PANEL_VIEW_GRID;
    if (astra_interface_control_set_value(&ui,
            BROWSER_CONTROL(ASTRA_FILE_PANEL_VIEW), ASTRA_FILE_PANEL_VIEW_GRID) !=
            ASTRA_OK ||
        astra_interface_file_panel_action(&browser, &ui, BROWSER_CONTROL(0),
            &view_action, &outcome) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (astra_interface_toolbar_actions_refresh(&toolbar, &ui) != ASTRA_OK ||
        fit_toolbar() != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return layout();
}

static AstraResult browser_reserve(uint32_t count)
{
    AstraCollectionItem *next_items[2];
    const AstraSurface **next_icons;
    uint64_t *next_selected_ids;
    uint32_t capacity = browser_capacity;
    AstraResult result;

    if (count <= capacity) return ASTRA_OK;
    while (capacity < count && capacity <= UINT32_MAX / 2u)
        capacity *= 2u;
    if (capacity < count || capacity > SIZE_MAX / sizeof(*next_items[0]) ||
        capacity > SIZE_MAX / sizeof(*next_icons) ||
        capacity > SIZE_MAX / sizeof(*next_selected_ids))
        return ASTRA_ERROR_NO_RESOURCES;
    next_items[0] = astra_runtime_allocate((size_t)capacity *
        sizeof(*next_items[0]));
    next_items[1] = astra_runtime_allocate((size_t)capacity *
        sizeof(*next_items[1]));
    next_icons = astra_runtime_allocate((size_t)capacity *
        sizeof(*next_icons));
    next_selected_ids = astra_runtime_allocate((size_t)capacity *
        sizeof(*next_selected_ids));
    if (next_items[0] == NULL || next_items[1] == NULL ||
        next_icons == NULL || next_selected_ids == NULL) {
        astra_runtime_deallocate(next_items[0]);
        astra_runtime_deallocate(next_items[1]);
        astra_runtime_deallocate(next_icons);
        astra_runtime_deallocate(next_selected_ids);
        return ASTRA_ERROR_NO_RESOURCES;
    }
    memcpy(next_selected_ids, browser_selected_ids,
        (size_t)browser_selection.count * sizeof(*next_selected_ids));
    uint64_t *old_selected_ids = browser_selected_ids;
    uint32_t old_selection_capacity = browser_selection.capacity;
    browser_selection.ids = next_selected_ids;
    browser_selection.capacity = capacity;
    result = astra_interface_file_panel_rebind_storage(&browser, &ui,
        BROWSER_CONTROL(0), next_items[0], capacity, next_items[1], capacity,
        next_icons, capacity);
    if (result != ASTRA_OK) {
        browser_selection.ids = old_selected_ids;
        browser_selection.capacity = old_selection_capacity;
        astra_runtime_deallocate(next_items[0]);
        astra_runtime_deallocate(next_items[1]);
        astra_runtime_deallocate(next_icons);
        astra_runtime_deallocate(next_selected_ids);
        return result;
    }
    astra_runtime_deallocate(browser_items[0]);
    astra_runtime_deallocate(browser_items[1]);
    astra_runtime_deallocate(browser_grid_icons);
    astra_runtime_deallocate(browser_selected_ids);
    browser_items[0] = next_items[0];
    browser_items[1] = next_items[1];
    browser_grid_icons = next_icons;
    browser_selected_ids = next_selected_ids;
    browser_capacity = capacity;
    return ASTRA_OK;
}

static void browser_clear_children(void)
{
    for (uint32_t at = 0u; at < BROWSER_COLUMNS - 1u; ++at) {
        astra_directory_snapshot_dispose(&child_directories[at]);
        astra_runtime_deallocate(child_items[at]);
        child_items[at] = NULL;
    }
}

static AstraResult browser_update_status(void)
{
    AstraString line;
    astra_string_init(&line, browser_status, sizeof(browser_status));
    if (directory.status != ASTRA_VFS_OK) {
        (void)astra_string_append(&line, message());
    } else if (browser.search_length != 0u) {
        (void)astra_string_append_u64(&line, browser.item_count);
        (void)astra_string_append(&line, " of ");
        (void)astra_string_append_u64(&line, directory.count);
        (void)astra_string_append(&line, " items");
    } else if (browser_selection.count > 1u && selected_depth == 0u) {
        (void)astra_string_append_u64(&line, browser_selection.count);
        (void)astra_string_append(&line, " of ");
        (void)astra_string_append_u64(&line, directory.count);
        (void)astra_string_append(&line, " selected");
    } else if (selected_path[0] != '\0') {
        if (selected_depth == 0u) {
            (void)astra_string_append(&line, "1 of ");
            (void)astra_string_append_u64(&line, directory.count);
        } else (void)astra_string_append(&line, "1");
        (void)astra_string_append(&line, " selected · ");
        const char *name = astra_folder_leaf(selected_path);
        uint32_t length = (uint32_t)strlen(name);
        uint32_t remaining = sizeof(browser_status) - line.length - 1u;
        (void)astra_string_append_bytes(&line, name,
            astra_string_utf8_prefix(name, length, remaining));
    } else {
        (void)astra_string_append_u64(&line, directory.count);
        (void)astra_string_append(&line,
            directory.count == 1u ? " item" : " items");
    }
    return astra_interface_control_set_text(&ui,
        BROWSER_CONTROL(BROWSER_PANEL_CONTROL_COUNT + ASTRA_STATUS_BAR_LEFT),
        browser_status, line.length);
}

static AstraResult browser_update_space(void)
{
    AstraFilesystemInfo capacity = { .size = sizeof(AstraFilesystemInfo) };
    AstraString line;
    astra_string_init(&line, browser_space, sizeof(browser_space));
    if (astra_filesystem_capacity_path(&filesystem.filesystem, root, &capacity) ==
            ASTRA_VFS_OK && capacity.fragment_size != 0u &&
        capacity.blocks <= UINT64_MAX / capacity.fragment_size &&
        capacity.blocks_available <= UINT64_MAX / capacity.fragment_size) {
        uint64_t available = capacity.blocks_available *
            capacity.fragment_size / UINT64_C(1048576);
        uint64_t total = capacity.blocks * capacity.fragment_size /
            UINT64_C(1048576);
        (void)astra_string_append_u64(&line, available);
        (void)astra_string_append(&line, " MiB free of ");
        (void)astra_string_append_u64(&line, total);
        (void)astra_string_append(&line, " MiB");
    } else
        (void)astra_string_append(&line, "Atlas");
    return astra_interface_control_set_text(&ui,
        BROWSER_CONTROL(BROWSER_PANEL_CONTROL_COUNT + ASTRA_STATUS_BAR_RIGHT),
        browser_space, line.length);
}

static AstraResult browser_set_directory(const AstraDirectorySnapshot *next,
                                         const char *path)
{
    uint32_t count = next->status == ASTRA_VFS_OK ? next->count : 0u;
    AstraResult result = browser_reserve(count);
    if (result != ASTRA_OK) return result;
    result = astra_interface_file_panel_set_directory(&browser, &ui,
        BROWSER_CONTROL(0), path, count != 0u ? next->entries : NULL, count,
        browser.spare_items, browser.spare_capacity);
    if (result != ASTRA_OK) return result;
    int32_t selected_place = -1;
    size_t selected_length = 0u;
    for (uint32_t at = 0u; at < browser.place_count; ++at) {
        const char *place = browser.places[at].path;
        size_t length = strlen(place);
        if (length >= selected_length &&
            strncmp(path, place, length) == 0 &&
            (path[length] == '\0' || path[length] == '/')) {
            selected_place = (int32_t)at;
            selected_length = length;
        }
    }
    result = astra_interface_control_set_value(&ui,
        BROWSER_CONTROL(ASTRA_FILE_PANEL_PLACES_LIST), selected_place);
    if (result != ASTRA_OK) return result;
    browser_clear_children();
    astra_runtime_deallocate(browser_entry_icons);
    browser_entry_icons = NULL;
    return layout();
}

static AstraResult browser_set_icons(void)
{
    const AstraSurface **icons = NULL;
    AstraResult result;
    if (directory.count != 0u) {
        if (directory.count > SIZE_MAX / sizeof(*icons))
            return ASTRA_ERROR_NO_RESOURCES;
        icons = astra_runtime_allocate(directory.count * sizeof(*icons));
        if (icons == NULL) return ASTRA_ERROR_NO_RESOURCES;
        for (uint32_t at = 0u; at < directory.count; ++at)
            icons[at] = at < entry_icon_count &&
                entry_icons[at]._private_handle != ASTRA_INVALID_HANDLE ?
                &entry_icons[at] :
                directory.entries[at].kind == ASTRA_VFS_KIND_DIRECTORY ?
                &folder_icon : &file_icon;
    }
    result = astra_interface_file_panel_set_icons(&browser, &ui,
        BROWSER_CONTROL(0), icons, directory.count);
    if (result != ASTRA_OK) astra_runtime_deallocate(icons);
    else browser_entry_icons = icons;
    return result;
}

static const char *message(void)
{
    if (volume_lost) return "This volume is unavailable.";
    if (visible_data == 0u)
        return "Reading volume...";
    switch (directory.status) {
    case ASTRA_VFS_OK: return "This directory is empty.";
    case ASTRA_VFS_ERR_ACCESS: return "Access to this directory was refused.";
    case ASTRA_VFS_ERR_LIMIT: return "Not enough memory to show this directory.";
    case ASTRA_VFS_ERR_NOT_FOUND: return "This directory was not found.";
    case ASTRA_VFS_ERR_LOST: return "This volume is unavailable.";
    case ASTRA_VFS_ERR_NOT_DIR: return "This path is not a directory.";
    default: return "The directory could not be read.";
    }
}

static AstraResult paint(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraDrawPaint background = ASTRA_DRAW_PAINT_INIT;
    AstraRectI32 whole = {0, 0, width, height};
    AstraResult result;

    if (name_edit.active) {
        result = astra_interface_inline_edit_render(&name_edit, edit_commands,
            sizeof(edit_commands));
        if (result != ASTRA_OK) return result;
    }
    background.foreground = theme.client;
    result = astra_draw_list_create(&content, &whole, &list);
    if (result == ASTRA_OK)
        result = astra_draw_rectangle(&list, &whole, 1, &background);
    if (result == ASTRA_OK) {
        AstraControlFrame frame, clip;
        if (astra_interface_ui_geometry(&ui,
                TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + ASTRA_FILE_PANEL_CONTENT,
                &frame, &clip) == ASTRA_OK) {
            AstraRectI32 area = {frame.x, frame.y, frame.width, frame.height};
            background.foreground = theme.surface_raised;
            result = astra_draw_rectangle(&list, &area, 1, &background);
        }
        if (result == ASTRA_OK && astra_interface_ui_geometry(&ui,
                TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + ASTRA_FILE_PANEL_PLACES_SCROLL,
                &frame, &clip) == ASTRA_OK) {
            AstraRectI32 area = {frame.x, frame.y, frame.width, frame.height};
            background.foreground = theme.surface_inset;
            result = astra_draw_rectangle(&list, &area, 1, &background);
        }
    }
    if (result == ASTRA_OK) {
        AstraSurfaceView browser_view;
        for (uint32_t at = 0u; at < UI_CONTROL_COUNT && result == ASTRA_OK;
             ++at) {
            if (!astra_draw_list_view_init(&browser_view, browser_commands,
                    sizeof(browser_commands), width, height)) {
                result = ASTRA_ERROR_NO_RESOURCES;
                break;
            }
            result = astra_interface_ui_render_range(&ui, &browser_view,
                at, 1u);
            if (result != ASTRA_OK) {
                (void)astra_log_failure("atlas browser render",
                    (uint32_t)(-result));
                break;
            }
            result = astra_draw_list_append_view_with_sources(&list,
                &browser_view, browser_entry_icons, browser.entry_count);
            if (result != ASTRA_OK)
                (void)astra_log_failure("atlas browser append",
                    (uint32_t)(-result));
        }
    }
    if (result == ASTRA_OK && (visible_data == 0u || directory.count == 0u ||
                               directory.status != ASTRA_VFS_OK)) {
        const char *text = message();
        result = astra_draw_ui_text(&list,
            (AstraPointI32){220, TOOLBAR_HEIGHT + 94u}, text,
            (uint32_t)strlen(text), theme.title_font_height, 0u,
            !volume_lost && (visible_data == 0u || directory.status == ASTRA_VFS_OK) ? theme.client_text_secondary : theme.fault);
    }
    if (result == ASTRA_OK && name_edit.active &&
        browser.view_mode == ASTRA_FILE_PANEL_VIEW_GRID) {
        AstraControlFrame frame, clip;
        if (astra_interface_ui_geometry(&ui,
                TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + ASTRA_FILE_PANEL_GRID,
                &frame, &clip) == ASTRA_OK)
            for (uint32_t row = 0u; row < browser.item_count; ++row)
                if (browser.items[row].id == (uint64_t)edit_index + 1u) {
                    uint32_t columns = browser.grid_model.columns;
                    const AstraDirectoryEntry *entry = &directory.entries[edit_index];
                    AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;
                    tile.style = ASTRA_ICON_TILE_CLIENT;
                    tile.frame = (AstraRectI32){
                        frame.x + (int32_t)(row % columns) * CELL_WIDTH,
                        frame.y + (int32_t)(row / columns) * CELL_HEIGHT,
                        CELL_WIDTH, CELL_HEIGHT};
                    tile.clip = (AstraRectI32){clip.x, clip.y, clip.width,
                                               clip.height};
                    tile.icon = browser_entry_icons == NULL ? NULL :
                        browser_entry_icons[edit_index];
                    tile.label = entry->name;
                    tile.label_bytes = (uint32_t)strlen(entry->name);
                    tile.editor = &name_edit.surface;
                    result = astra_interface_icon_tile_render(&list, &tile);
                    break;
                }
    }
    if (result == ASTRA_OK && name_edit.active &&
        (browser.view_mode == ASTRA_FILE_PANEL_VIEW_LIST ||
         browser.view_mode == ASTRA_FILE_PANEL_VIEW_COLUMNS)) {
        const AstraCollectionItem *items = browser.items;
        uint32_t item_count = browser.item_count;
        uint32_t control_index = ASTRA_FILE_PANEL_LIST;
        if (browser.view_mode == ASTRA_FILE_PANEL_VIEW_COLUMNS) {
            const AstraFilePanelColumnSnapshot *snapshot =
                &browser.column_snapshots[edit_depth];
            items = snapshot->items;
            item_count = snapshot->item_count;
            control_index = ASTRA_FILE_PANEL_CONTROL_COUNT + 2u +
                2u * edit_depth;
        }
        AstraControlFrame frame, clip;
        if (astra_interface_ui_geometry(&ui,
                TOOLBAR_CONTROL_COUNT + BROWSER_TOOL_COUNT + control_index,
                &frame, &clip) == ASTRA_OK) {
            for (uint32_t row = 0u; row < item_count; ++row) {
                if (items[row].id != (uint64_t)edit_index + 1u) continue;
                AstraBlitOptions options = ASTRA_BLIT_OPTIONS_INIT;
                AstraRectI32 source = {0, 0, ASTRA_INLINE_EDIT_WIDTH,
                    ASTRA_INLINE_EDIT_HEIGHT};
                AstraRectI32 target = {
                    frame.x + theme.control_padding_x,
                    frame.y + (int32_t)(row * theme.control_height),
                    ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT};
                AstraRectI32 bounds = {clip.x, clip.y, clip.width,
                    clip.height};
                result = astra_draw_list_set_clip(&list, &bounds);
                if (result == ASTRA_OK)
                    result = astra_draw_blit(&list, &name_edit.surface,
                        &source, &target, &options);
                break;
            }
        }
    }
    if (result == ASTRA_OK)
        result = astra_draw_submit(&list, &fence);
    if (result != ASTRA_OK)
        (void)astra_log_failure("atlas draw submit", (uint32_t)(-result));
    if (fence._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_fence_close(&fence);
        (void)ignored;
    }
    if (list._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_draw_list_close(&list);
        (void)ignored;
    }
    if (result == ASTRA_OK)
        result = astra_window_present(&window);
    if (result == ASTRA_OK) {
        astra_interface_ui_damage_clear(&ui);
    }
    return result;
}

static void log_root(void)
{
    uint32_t capacity = (uint32_t)strlen(root) + 96u;
    char *bytes = astra_runtime_allocate(capacity);
    AstraString line;
    uint32_t folders = 0u;

    for (uint32_t at = 0u; at < directory.count; ++at)
        folders += directory.entries[at].kind == ASTRA_VFS_KIND_DIRECTORY;
    if (bytes == NULL) return;
    astra_string_init(&line, bytes, capacity);
    (void)astra_string_append(&line, "atlas root ");
    (void)astra_string_append(&line, root);
    (void)astra_string_append(&line, " entries ");
    (void)astra_string_append_u64(&line, directory.count);
    (void)astra_string_append(&line, " folders ");
    (void)astra_string_append_u64(&line, folders);
    (void)astra_string_append(&line, " status ");
    (void)astra_string_append_u64(&line, directory.status);
    (void)astra_log(bytes);
    astra_runtime_deallocate(bytes);
}

static void log_icons(void)
{
    AstraWindowInfo info = ASTRA_WINDOW_INFO_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControlFrame frame, clip;
    if (browser.view_mode != ASTRA_FILE_PANEL_VIEW_GRID ||
        astra_interface_ui_geometry(&ui, 1u + ASTRA_FILE_PANEL_GRID,
            &frame, &clip) != ASTRA_OK || browser.grid_model.columns == 0u)
        return;
    if (astra_window_get_info(&window, &info) != ASTRA_OK) return;
    int32_t origin_x = info.frame.x + theme.frame_width;
    int32_t origin_y = info.frame.y + theme.frame_width +
        theme.titlebar_height + theme.signal_height;
    for (uint32_t row = 0u; row < browser.item_count; ++row) {
        uint32_t at = (uint32_t)browser.items[row].id - 1u;
        char path[ASTRA_VFS_PATH_MAX], line[ASTRA_VFS_PATH_MAX + 96u];
        int32_t x = frame.x + (int32_t)(row % browser.grid_model.columns) * CELL_WIDTH;
        int32_t y = frame.y + (int32_t)(row / browser.grid_model.columns) * CELL_HEIGHT;
        if (at >= directory.count || clip.width == 0u || clip.height == 0u ||
            (int64_t)x + CELL_WIDTH <= clip.x ||
            x >= (int64_t)clip.x + clip.width ||
            (int64_t)y + CELL_HEIGHT <= clip.y ||
            y >= (int64_t)clip.y + clip.height ||
            astra_folder_path(root, directory.entries[at].name,
                path, sizeof(path)) != ASTRA_VFS_OK) continue;
        AstraString out;
        astra_string_init(&out, line, sizeof(line));
        (void)astra_string_append(&out, "atlas icon ");
        (void)astra_string_append(&out, path);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_i64(&out, origin_x + x);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_i64(&out, origin_y + y);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_u64(&out, CELL_WIDTH);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_u64(&out, CELL_HEIGHT);
        (void)astra_log(line);
    }
}

typedef struct AtlasCommands {
    AstraHandle gui;
} AtlasCommands;

static void folder_error(AstraHandle gui, const char *message, uint32_t status)
{
    char text[192];
    AstraString out;
    astra_string_init(&out, text, sizeof(text));
    (void)astra_string_append(&out, message);
    (void)astra_string_append(&out, ": ");
    const char *reason = astra_vfs_status_text(status);
    if (reason != NULL) (void)astra_string_append(&out, reason);
    else (void)astra_string_append_u64(&out, status);
    (void)astra_string_append_char(&out, '.');
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.kind = ASTRA_ALERT_ERROR;
    info.title = "File Operation";
    info.title_length = 14u;
    info.message = text;
    info.message_length = (uint16_t)out.length;
    info.button = "Close";
    info.button_length = 5u;
    (void)astra_interface_show_alert(gui, &info);
}

static void open_error(AstraHandle gui, AstraResult result)
{
    char text[96];
    AstraString out;
    astra_string_init(&out, text, sizeof(text));
    (void)astra_string_append(&out, "Could not open item. Astra status ");
    (void)astra_string_append_i64(&out, result);
    (void)astra_string_append_char(&out, '.');
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.kind = ASTRA_ALERT_ERROR;
    info.title = "Open Error";
    info.title_length = 10u;
    info.message = text;
    info.message_length = (uint16_t)out.length;
    info.button = "Close";
    info.button_length = 5u;
    (void)astra_interface_show_alert(gui, &info);
}

static AstraResult update_window_title(void)
{
    char title[ASTRA_WINDOW_TITLE_MAX + 1u];
    char fallback_label[sizeof(sidebar_volumes.records[0].name)];
    AstraString text;
    const char *label = NULL;
    const char *suffix = root;
    astra_string_init(&text, title, sizeof(title));
    if (!astra_string_append(&text, "Atlas — "))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    if (viewed_volume.id != 0u && volume_path[0] != '\0' &&
        strncmp(root, volume_path, strlen(volume_path)) == 0) {
        label = volume_title;
        suffix = root + strlen(volume_path);
    } else if (strncmp(root, "/dh0", 4u) == 0 &&
               (root[4] == '\0' || root[4] == '/')) {
        label = "DH0";
        suffix = root + 4u;
    } else if (strncmp(root, "/home", 5u) == 0 &&
               (root[5] == '\0' || root[5] == '/')) {
        label = "HOME";
        suffix = root + 5u;
    } else if (strncmp(root, "/apps", 5u) == 0 &&
               (root[5] == '\0' || root[5] == '/')) {
        label = "APPS";
        suffix = root + 5u;
    } else {
        for (uint32_t at = 0u; at < sidebar_volumes.count; ++at) {
            const AstraVolumeRecord *record = &sidebar_volumes.records[at];
            size_t length = strlen(record->name);
            if (record->kind == ASTRA_VOLUME_MOUNT && length != 0u &&
                strncmp(root, record->name, length) == 0 &&
                (root[length] == '\0' || root[length] == '/')) {
                label = record->label;
                if (label[0] == '\0' &&
                    astra_string_copy(fallback_label, sizeof(fallback_label),
                        record->name + 1u)) {
                    for (uint32_t index = 0u; fallback_label[index] != '\0';
                            ++index)
                        fallback_label[index] =
                            astra_ascii_upper(fallback_label[index]);
                    label = fallback_label;
                }
                suffix = root + length;
                break;
            }
        }
    }
    if (label != NULL &&
        (!astra_string_append(&text, label) ||
         !astra_string_append_char(&text, ':')))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    if (suffix[0] != '\0' && label != NULL &&
        !astra_string_append(&text, " "))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    uint32_t length = (uint32_t)strlen(suffix);
    uint32_t kept = astra_string_utf8_prefix(suffix, length,
        ASTRA_WINDOW_TITLE_MAX - (uint32_t)strlen(title));
    if (!astra_string_append_bytes(&text, suffix, kept))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    return astra_window_set_title(&window, title, (uint16_t)strlen(title));
}

static int same_volume_reference(const AstraVolumeReference *left,
    const AstraVolumeReference *right)
{
    return left->epoch == right->epoch && left->id == right->id &&
        left->generation == right->generation;
}

static const AstraVolumeRecord *volume_for_path(
    const AstraVolumeSnapshot *snapshot, const char *path)
{
    const AstraVolumeRecord *match = NULL;
    size_t longest = 0u;
    if (fixed_copy_path(path)) return NULL;
    for (uint32_t at = 0u; at < snapshot->count; ++at) {
        const AstraVolumeRecord *record = &snapshot->records[at];
        size_t length = strlen(record->name);
        if (record->kind != ASTRA_VOLUME_MOUNT ||
            record->state != ASTRA_VOLUME_MOUNTED ||
            (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u ||
            record->origin == ASTRA_VOLUME_ORIGIN_EXPORT ||
            record->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE ||
            length <= longest || length < 2u || record->name[0] != '/' ||
            strncmp(path, record->name, length) != 0 ||
            (path[length] != '\0' && path[length] != '/')) continue;
        match = record;
        longest = length;
    }
    return match;
}

static AstraResult navigate_to(const char *path, uint32_t history_target)
{
    AstraDirectorySnapshot next = ASTRA_DIRECTORY_SNAPSHOT_INIT;
    AstraScrollModelInfo model = ASTRA_SCROLL_MODEL_INFO_INIT;
    char target[ASTRA_VFS_PATH_MAX];
    char *history_copy = NULL;
    AstraVolumeSnapshot catalog = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraVolumeRecord record = {0};
    AstraVolumeReference candidate = {0u, 0u, 0u};
    AstraHandle next_grant = ASTRA_INVALID_HANDLE;
    uint32_t status;
    AstraResult result;

    if (path == NULL || path[0] != '/' ||
        !astra_string_copy(target, sizeof(target), path) ||
        astra_path_requires_walk(target))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_volume_snapshot_open(volume_metadata, &catalog);
    if (result != ASTRA_OK) return result;
    const AstraVolumeRecord *mounted = volume_for_path(&catalog, target);
    if (mounted != NULL) {
        record = *mounted;
        candidate = (AstraVolumeReference){catalog.epoch,
            mounted->id, mounted->generation};
    }
    astra_volume_snapshot_close(&catalog);
    if ((history_target < navigation_count &&
         !same_volume_reference(&candidate,
             &navigation_history_volumes[history_target])) ||
        (history_target == NAVIGATION_STAY && strcmp(target, root) == 0 &&
         !same_volume_reference(&candidate, &viewed_volume)))
        return ASTRA_ERROR_INVALID_HANDLE;
    if (candidate.id != 0u &&
        (!same_volume_reference(&candidate, &viewed_volume) ||
         volume_grant == ASTRA_INVALID_HANDLE)) {
        AstraHandle broker = record.origin == ASTRA_VOLUME_ORIGIN_RAM ?
            volume_ram_read_broker : volume_read_broker;
        if (broker == ASTRA_INVALID_HANDLE) return ASTRA_ERROR_PERMISSION;
        result = astra_volume_root(broker, &candidate, ASTRA_RIGHT_READ,
            &next_grant);
        if (result != ASTRA_OK) return result;
    }
    status = astra_directory_snapshot_load(&next,
        &filesystem.filesystem, target, NULL);
    if (status != ASTRA_VFS_OK) {
        close_grant(&next_grant);
        return astra_result_from_service(status);
    }
    status = prepare_directory(&next);
    if (status != ASTRA_VFS_OK) {
        astra_directory_snapshot_dispose(&next);
        close_grant(&next_grant);
        return astra_result_from_service(status);
    }
    if (history_target == NAVIGATION_PUSH &&
        (navigation_count == 0u || strcmp(target, root) != 0 ||
         !same_volume_reference(&candidate, &viewed_volume))) {
        size_t length = strlen(target);
        history_copy = astra_runtime_allocate(length + 1u);
        if (history_copy == NULL) {
            astra_directory_snapshot_dispose(&next);
            close_grant(&next_grant);
            return ASTRA_ERROR_NO_RESOURCES;
        }
        memcpy(history_copy, target, length + 1u);
    }
    result = browser_set_directory(&next, target);
    if (result != ASTRA_OK) {
        astra_runtime_deallocate(history_copy);
        astra_directory_snapshot_dispose(&next);
        close_grant(&next_grant);
        return result;
    }
    if (history_copy != NULL)
        navigation_commit(history_copy, &candidate);
    else if (history_target < navigation_count)
        navigation_index = history_target;
    if (next_grant != ASTRA_INVALID_HANDLE) {
        volume_release();
        volume_grant = next_grant;
    } else if (candidate.id == 0u)
        volume_release();
    viewed_volume = candidate;
    if (candidate.id != 0u) {
        (void)astra_string_copy(volume_path, sizeof(volume_path),
            record.name);
        const char *label = record.label[0] != '\0' ?
            record.label : record.name + 1u;
        uint32_t kept = astra_string_utf8_prefix(label,
            (uint32_t)strlen(label), ASTRA_WINDOW_TITLE_MAX);
        memcpy(volume_title, label, kept);
        volume_title[kept] = '\0';
        if (record.label[0] == '\0')
            for (uint32_t at = 0u; at < kept; ++at)
                volume_title[at] = astra_ascii_upper(volume_title[at]);
        volume_writable = (record.flags & ASTRA_VOLUME_READ_ONLY) == 0u &&
            (record.origin == ASTRA_VOLUME_ORIGIN_RAM ?
                volume_ram_write_broker : volume_write_broker) !=
                    ASTRA_INVALID_HANDLE;
    } else {
        volume_path[0] = '\0';
        volume_title[0] = '\0';
        volume_writable = 0u;
    }
    volume_lost = 0u;
    volume_refresh = 1u;
    volume_retry = 0u;
    clear_entry_icons();
    astra_directory_snapshot_dispose(&directory);
    directory = next;
    memcpy(current_path, target, strlen(target) + 1u);
    root = current_path;
    selection_clear();
    visible_data = 1u;
    model.line_height = CELL_HEIGHT + GRID_GAP_Y;
    result = astra_scroll_init(&scroll, &model);
    if (result != ASTRA_OK) return result;
    load_entry_icons();
    result = browser_set_icons();
    if (result != ASTRA_OK) return result;
    result = browser_update_status();
    if (result != ASTRA_OK) return result;
    result = browser_update_space();
    if (result != ASTRA_OK) return result;
    log_root();
    log_icons();
    if (result == ASTRA_OK) result = update_window_title();
    if (result == ASTRA_OK) result = paint();
    return result;
}

static AstraResult reload_directory(void)
{
    return navigate_to(root, NAVIGATION_STAY);
}

static AstraResult open_entry(const char *path, uint32_t kind)
{
    if (path == NULL || path[0] != '/')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t pid;
    size_t length = strlen(path);
    if (kind == ASTRA_VFS_KIND_FILE) {
        const char *arguments[] = {path};
        return astra_application_launch_with_arguments(launcher,
            "/apps/OpenWith.app", (uint16_t)strlen("/apps/OpenWith.app"),
            ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 1u, &pid);
    }
    if (kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (length >= 4u && strcmp(path + length - 4u, ".app") == 0)
        return astra_application_launch(launcher, path, (uint16_t)length, &pid);
    return navigate_to(path, NAVIGATION_PUSH);
}

static AstraResult browser_open_target(uint32_t outcome)
{
    size_t length = strlen(browser.target);
    if (length == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    return open_entry(browser.target, outcome == ASTRA_FILE_PANEL_PICKED ?
        ASTRA_VFS_KIND_FILE : ASTRA_VFS_KIND_DIRECTORY);
}

static AstraResult browser_load_child(void)
{
    uint32_t depth = browser.selected_depth + 1u;
    uint32_t slot = depth - 1u;
    uint32_t status;
    AstraResult result;
    if (depth == 0u || depth >= BROWSER_COLUMNS ||
        !astra_string_copy(child_paths[slot], sizeof(child_paths[slot]),
            browser.target))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_directory_snapshot_dispose(&child_directories[slot]);
    astra_runtime_deallocate(child_items[slot]);
    child_items[slot] = NULL;
    for (uint32_t later = slot + 1u; later < BROWSER_COLUMNS - 1u; ++later) {
        astra_directory_snapshot_dispose(&child_directories[later]);
        astra_runtime_deallocate(child_items[later]);
        child_items[later] = NULL;
    }
    status = astra_directory_snapshot_load(&child_directories[slot],
        &filesystem.filesystem, child_paths[slot], NULL);
    if (status != ASTRA_VFS_OK)
        return astra_result_from_service(status);
    status = prepare_directory(&child_directories[slot]);
    if (status != ASTRA_VFS_OK)
        return astra_result_from_service(status);
    uint32_t capacity = child_directories[slot].count == 0u ? 1u :
        child_directories[slot].count;
    if (capacity > SIZE_MAX / sizeof(*child_items[slot]))
        return ASTRA_ERROR_NO_RESOURCES;
    child_items[slot] = astra_runtime_allocate((size_t)capacity *
        sizeof(*child_items[slot]));
    if (child_items[slot] == NULL) return ASTRA_ERROR_NO_RESOURCES;
    result = astra_interface_file_panel_set_column(&browser, &ui,
        BROWSER_CONTROL(0), depth, child_paths[slot], child_directories[slot].entries,
        child_directories[slot].count, child_items[slot], capacity);
    if (result != ASTRA_OK) {
        astra_directory_snapshot_dispose(&child_directories[slot]);
        astra_runtime_deallocate(child_items[slot]);
        child_items[slot] = NULL;
    }
    return result;
}

static AstraResult start_edit(void)
{
    if (!writable_folder_root() || selected_path[0] == '\0' ||
        selected_row == UINT32_MAX ||
        (selected_kind != ASTRA_VFS_KIND_DIRECTORY &&
         selected_kind != ASTRA_VFS_KIND_FILE) ||
        !astra_string_copy(edit_path, sizeof(edit_path), selected_path))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const char *leaf = astra_folder_leaf(edit_path);
    if (leaf == NULL || !astra_string_copy(edit_parent,
            sizeof(edit_parent), edit_path)) return ASTRA_ERROR_INVALID_ARGUMENT;
    size_t parent_length = (size_t)(leaf - edit_path - 1u);
    edit_parent[parent_length == 0u ? 1u : parent_length] = '\0';
    AstraResult result = astra_interface_inline_edit_start(&name_edit, &display,
        leaf);
    if (result != ASTRA_OK) return result;
    edit_index = selected_row;
    edit_depth = selected_depth;
    return paint();
}

static AstraResult finish_edit(AstraHandle gui, int commit)
{
    if (!name_edit.active) return ASTRA_OK;
    char selected_name[ASTRA_VFS_NAME_MAX];
    const char *old_name = astra_folder_leaf(edit_path);
    if (!astra_string_copy(selected_name, sizeof(selected_name),
            old_name != NULL ? old_name : ""))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (commit) {
        char name[ASTRA_VFS_NAME_MAX], moved[ASTRA_VFS_PATH_MAX];
        AstraResult result = astra_interface_inline_edit_text(&name_edit, name, sizeof(name));
        if (result != ASTRA_OK || !astra_folder_name_valid(name)) {
            folder_error(gui, "Choose a valid name", ASTRA_VFS_ERR_INVALID);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        uint32_t status = astra_entry_rename_safe(active_filesystem(),
            edit_path, edit_parent, name, moved, sizeof(moved));
        if (status != ASTRA_VFS_OK) {
            folder_error(gui, "Item could not be renamed", status);
            return astra_result_from_service(status);
        }
        memcpy(selected_name, name, strlen(name) + 1u);
    }
    astra_interface_inline_edit_stop(&name_edit);
    edit_index = UINT32_MAX;
    AstraResult result = strcmp(edit_parent, root) == 0 ?
        reload_directory() : navigate_to(edit_parent, NAVIGATION_PUSH);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < browser.item_count; ++at) {
        uint32_t index = (uint32_t)browser.items[at].id - 1u;
        if (index >= directory.count ||
            strcmp(directory.entries[index].name, selected_name) != 0)
            continue;
        result = astra_interface_control_set_value(&ui,
            BROWSER_CONTROL(ASTRA_FILE_PANEL_GRID), at);
        if (result == ASTRA_OK)
            result = astra_interface_control_set_value(&ui,
                BROWSER_CONTROL(ASTRA_FILE_PANEL_LIST), at);
        if (result == ASTRA_OK && browser.column_count != 0u)
            result = astra_interface_control_set_value(&ui,
                BROWSER_CONTROL(ASTRA_FILE_PANEL_CONTROL_COUNT + 2u), at);
        if (result != ASTRA_OK) return result;
        result = selection_set(root, directory.entries, directory.count,
            index, 0u);
        if (result != ASTRA_OK) return result;
        return browser_update_status();
    }
    return ASTRA_OK;
}

static AstraResult menu_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"New", 3u, writable_folder_root() &&
        worker == 0u && directory.status == ASTRA_VFS_OK, 0u};
    return ASTRA_OK;
}

static AstraResult folder_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = menu_query(opaque, state);
    state->label = "Folder";
    state->label_length = 6u;
    return result;
}

static AstraResult selected_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"Open", 4u,
        selected_path[0] != '\0' &&
        (selected_kind == ASTRA_VFS_KIND_DIRECTORY ||
         selected_kind == ASTRA_VFS_KIND_FILE), 0u};
    return ASTRA_OK;
}

static AstraResult copy_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = selected_query(opaque, state);
    state->label = "Copy";
    state->label_length = 4u;
    state->enabled = state->enabled && clipboard_service != ASTRA_INVALID_HANDLE;
    return result;
}

static int clipboard_has_file(void)
{
    if (clipboard_service == ASTRA_INVALID_HANDLE) return 0;
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    if (astra_clipboard_read(clipboard_service, &item) != ASTRA_OK)
        return 0;
    const void *data = NULL;
    uint32_t length = 0u;
    AstraResult found = astra_clipboard_item_find(&item,
        ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE,
        sizeof(ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE) - 1u,
        &data, &length);
    int available = found == ASTRA_OK &&
        length == sizeof(AstraVfsClipboardReference);
    AstraResult closed = astra_clipboard_item_close(&item);
    return available && closed == ASTRA_OK;
}

static AstraResult paste_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = menu_query(opaque, state);
    state->label = "Paste";
    state->label_length = 5u;
    state->enabled = state->enabled && clipboard_has_file();
    return result;
}

static AstraResult rename_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = selected_query(opaque, state);
    state->label = "Rename";
    state->label_length = 6u;
    state->enabled = state->enabled && writable_folder_root();
    return result;
}

static AstraResult delete_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = rename_query(opaque, state);
    state->label = "Delete";
    state->label_length = 6u;
    return result;
}

static int selected_bundle(const AtlasCommands *context)
{
    (void)context;
    return selected_kind == ASTRA_VFS_KIND_DIRECTORY &&
        astra_string_has_suffix(selected_path, ".app") &&
        selected_path[0] != '\0';
}

static AstraResult package_query(void *opaque, AstraCommandState *state)
{
    const AtlasCommands *context = opaque;
    *state = (AstraCommandState){"Create installer", 16u,
        selected_bundle(context), 0u};
    return ASTRA_OK;
}

static AstraResult create_installer(void *opaque,
    const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    char bundle[ASTRA_VFS_PATH_MAX], archive[ASTRA_VFS_PATH_MAX];
    AstraString output;
    if (!selected_bundle(context) ||
        !astra_string_copy(bundle, sizeof(bundle), selected_path))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_string_init(&output, archive, sizeof(archive));
    const char *leaf = astra_folder_leaf(selected_path);
    uint32_t length = leaf != NULL ? (uint32_t)strlen(leaf) : 0u;
    if (length < 4u) return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_string_append(&output, "/home/Desktop/") ||
        !astra_string_append_bytes(&output, leaf,
            length - 4u) || !astra_string_append(&output, ".apkg"))
        return ASTRA_ERROR_NAME_TOO_LONG;
    AstraResult result = astra_package_create_bundle(&filesystem.filesystem,
        bundle, archive);
    if (result != ASTRA_OK) {
        folder_error(context->gui, "Installer could not be created",
            astra_status_from_result(result));
        return result;
    }
    if (strcmp(root, "/home/Desktop") == 0) return reload_directory();
    return ASTRA_OK;
}

static AstraResult create_folder(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    if (!writable_folder_root() || worker != 0u) return ASTRA_ERROR_PERMISSION;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_folder_create_numbered(active_filesystem(),
        root, "Folder", path, sizeof(path));
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Folder could not be created", status);
        return astra_result_from_service(status);
    }
    AstraResult result = reload_directory();
    if (result != ASTRA_OK) return result;
    const char *name = astra_folder_leaf(path);
    for (uint32_t at = 0u; at < directory.count; ++at)
        if (strcmp(directory.entries[at].name, name) == 0) {
            result = selection_set(root, directory.entries, directory.count,
                at, 0u);
            return result == ASTRA_OK ? start_edit() : result;
        }
    return ASTRA_ERROR_IO;
}

static AstraResult open_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    (void)opaque;
    return open_entry(selected_path, selected_kind);
}

static int fixed_copy_path(const char *path)
{
    static const char *fixed[] = {"/home", "/dh0", "/apps"};
    for (uint32_t at = 0u; at < sizeof(fixed) / sizeof(fixed[0]); ++at) {
        size_t length = strlen(fixed[at]);
        if (strncmp(path, fixed[at], length) == 0 &&
            (path[length] == '\0' || path[length] == '/')) return 1;
    }
    return 0;
}

static AstraResult copy_reference_for(const char *path,
    AstraVfsClipboardReference *reference)
{
    if (fixed_copy_path(path)) return ASTRA_OK;
    if (viewed_volume.id != 0u && volume_path[0] != '\0' &&
        strncmp(path, volume_path, strlen(volume_path)) == 0 &&
        (path[strlen(volume_path)] == '\0' ||
         path[strlen(volume_path)] == '/')) {
        reference->volume = viewed_volume;
        return ASTRA_OK;
    }
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(volume_metadata, &snapshot);
    if (result != ASTRA_OK) return result;
    result = ASTRA_ERROR_INVALID_HANDLE;
    for (uint32_t at = 0u; at < snapshot.count; ++at) {
        const AstraVolumeRecord *record = &snapshot.records[at];
        size_t length = strlen(record->name);
        if (record->kind == ASTRA_VOLUME_MOUNT &&
            record->state == ASTRA_VOLUME_MOUNTED &&
            (record->flags & ASTRA_VOLUME_SYSTEM) == 0u &&
            length != 0u && strncmp(path, record->name, length) == 0 &&
            (path[length] == '\0' || path[length] == '/')) {
            reference->volume = (AstraVolumeReference){
                snapshot.epoch, record->id, record->generation};
            result = ASTRA_OK;
            break;
        }
    }
    astra_volume_snapshot_close(&snapshot);
    return result;
}

static AstraResult copy_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    (void)opaque;
    AstraVfsClipboardReference reference =
        ASTRA_VFS_CLIPBOARD_REFERENCE_INIT;
    AstraClipboardRepresentation representation =
        ASTRA_CLIPBOARD_REPRESENTATION_INIT;
    if (clipboard_service == ASTRA_INVALID_HANDLE ||
        selected_path[0] == '\0' ||
        (selected_kind != ASTRA_VFS_KIND_FILE &&
         selected_kind != ASTRA_VFS_KIND_DIRECTORY))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_string_copy(reference.path, sizeof(reference.path),
            selected_path)) return ASTRA_ERROR_BUFFER_TOO_SMALL;
    AstraResult result = copy_reference_for(reference.path, &reference);
    if (result != ASTRA_OK) return result;
    representation.type = ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE;
    representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE) - 1u;
    representation.data = &reference;
    representation.data_length = sizeof(reference);
    return astra_clipboard_write(clipboard_service, &representation, 1u, NULL);
}

typedef struct AtlasCopyMount {
    AstraHandle grant;
    AstraAssignTable assigns;
    AstraVfsClient client;
    AstraFilesystem filesystem;
    uint32_t connected;
} AtlasCopyMount;

static AstraVfsClient *copy_mount_client(const AstraAssign *assign,
    void *context)
{
    AtlasCopyMount *mount = context;
    return mount->connected && assign->handle == mount->grant ?
        &mount->client : NULL;
}

static void copy_mount_close(AtlasCopyMount *mount)
{
    astra_filesystem_detach(&mount->filesystem);
    if (mount->connected) (void)astra_vfs_disconnect(&mount->client);
    astra_assign_table_destroy(&mount->assigns);
    if (mount->grant != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&mount->grant);
        (void)ignored;
    }
}

static AstraResult copy_mount_open(AtlasCopyMount *mount,
    const AstraVolumeReference *reference, const char *name, uint32_t origin)
{
    if (name[0] != '/' || name[1] == '\0')
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraHandle broker = origin == ASTRA_VOLUME_ORIGIN_RAM ?
        volume_ram_read_broker : volume_read_broker;
    AstraResult result = astra_volume_root(broker, reference,
        ASTRA_RIGHT_READ, &mount->grant);
    if (result == ASTRA_OK)
        result = astra_result_from_service(astra_assign_bind(&mount->assigns,
            name + 1u, mount->grant, ASTRA_RIGHT_READ, ""));
    if (result == ASTRA_OK) {
        result = astra_result_from_service(astra_vfs_client_connect_service(
            &mount->client, mount->grant));
        mount->connected = result == ASTRA_OK;
    }
    if (result == ASTRA_OK)
        result = astra_result_from_service(astra_filesystem_attach(
            &mount->filesystem, &mount->assigns, copy_mount_client, NULL, mount));
    if (result != ASTRA_OK) copy_mount_close(mount);
    return result;
}

static AstraResult paste_into_current(void *opaque,
    const AstraCommandArguments *args)
{
    (void)args;
    const AtlasCommands *context = opaque;
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    AstraVfsClipboardReference reference;
    const void *data = NULL;
    uint32_t bytes = 0u;
    char destination[ASTRA_VFS_PATH_MAX];
    if (!writable_folder_root() || clipboard_service == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_PERMISSION;
    AstraResult result = astra_clipboard_read(clipboard_service, &item);
    if (result != ASTRA_OK) return result;
    result = astra_clipboard_item_find(&item,
        ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE,
        sizeof(ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE) - 1u, &data, &bytes);
    if (result == ASTRA_OK) {
        if (bytes != sizeof(reference)) result = ASTRA_ERROR_INVALID_ARGUMENT;
        else memcpy(&reference, data, sizeof(reference));
    }
    AstraResult closed = astra_clipboard_item_close(&item);
    if (result == ASTRA_OK) result = closed;
    if (result != ASTRA_OK) return result;
    if (reference.size != sizeof(reference) ||
        reference.version != ASTRA_VFS_CLIPBOARD_REFERENCE_VERSION ||
        reference.path[0] != '/' ||
        astra_path_requires_walk(reference.path) ||
        memchr(reference.path, '\0', sizeof(reference.path)) == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const char *leaf = astra_folder_leaf(reference.path);
    if (!astra_folder_name_valid(leaf)) return ASTRA_ERROR_INVALID_ARGUMENT;
    AtlasCopyMount source_mount = {
        .grant = ASTRA_INVALID_HANDLE,
        .assigns = ASTRA_ASSIGN_TABLE_INIT,
        .filesystem = ASTRA_FILESYSTEM_INIT};
    AstraFilesystem *source_filesystem = &filesystem.filesystem;
    if (reference.volume.id != 0u) {
        AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
        result = astra_volume_snapshot_open(volume_metadata, &snapshot);
        if (result != ASTRA_OK) return result;
        const AstraVolumeRecord *record = astra_volume_snapshot_find(&snapshot,
            &reference.volume);
        size_t length = record != NULL ? strlen(record->name) : 0u;
        result = record != NULL && record->kind == ASTRA_VOLUME_MOUNT &&
            record->state == ASTRA_VOLUME_MOUNTED && length != 0u &&
            strncmp(reference.path, record->name, length) == 0 &&
            reference.path[length] == '/' ? ASTRA_OK : ASTRA_ERROR_INVALID_HANDLE;
        if (result == ASTRA_OK)
            result = copy_mount_open(&source_mount, &reference.volume,
                record->name, record->origin);
        astra_volume_snapshot_close(&snapshot);
        if (result != ASTRA_OK) return result;
        source_filesystem = &source_mount.filesystem;
    } else if (!fixed_copy_path(reference.path)) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    uint32_t status = astra_folder_path(root, leaf, destination,
        sizeof(destination));
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_copy_tree_between(source_filesystem,
            reference.path, active_filesystem(), destination);
    if (reference.volume.id != 0u) copy_mount_close(&source_mount);
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Item could not be copied", status);
        return astra_result_from_service(status);
    }
    return reload_directory();
}

static AstraResult rename_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    (void)opaque;
    return start_edit();
}

static AstraResult delete_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    if (!writable_folder_root() || selected_path[0] == '\0' ||
        (selected_kind != ASTRA_VFS_KIND_DIRECTORY &&
         selected_kind != ASTRA_VFS_KIND_FILE))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    char moved[ASTRA_VFS_PATH_MAX];
    char trash[ASTRA_VFS_PATH_MAX];
    const char *mount = writable_mount_root();
    if (mount == NULL || astra_folder_path(mount, ".Trash", trash,
            sizeof(trash)) != ASTRA_VFS_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t status = astra_entry_move_to_trash(active_filesystem(),
        selected_path, trash, moved, sizeof(moved));
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Item could not be moved to Trash", status);
        return astra_result_from_service(status);
    }
    astra_interface_inline_edit_stop(&name_edit);
    edit_index = UINT32_MAX;
    return reload_directory();
}

static AstraResult refresh_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"Refresh", 7u,
        worker == 0u && !volume_lost, 0u};
    return ASTRA_OK;
}

static AstraResult refresh_directory(void *opaque,
    const AstraCommandArguments *args)
{
    (void)opaque;
    (void)args;
    if (worker != 0u || volume_lost) return ASTRA_ERROR_BUSY;
    return reload_directory();
}

typedef struct AtlasSortCommand {
    uint32_t key;
    const char *label;
    uint32_t label_length;
} AtlasSortCommand;

static const AtlasSortCommand sort_commands[] = {
    {ASTRA_DIRECTORY_SORT_NAME, "Name", 4u},
    {ASTRA_DIRECTORY_SORT_SIZE, "Size", 4u},
    {ASTRA_DIRECTORY_SORT_KIND, "Kind", 4u},
    {ASTRA_DIRECTORY_SORT_MODIFIED, "Modified", 8u}
};

static AstraResult sort_query(void *opaque, AstraCommandState *state)
{
    const AtlasSortCommand *command = opaque;
    *state = (AstraCommandState){command->label, command->label_length,
        worker == 0u && visible_data && directory.status == ASTRA_VFS_OK,
        sort_mode == command->key};
    return ASTRA_OK;
}

static AstraResult sort_invoke(void *opaque,
    const AstraCommandArguments *args)
{
    (void)args;
    const AtlasSortCommand *command = opaque;
    uint32_t previous = sort_mode;
    sort_mode = command->key;
    AstraResult result = astra_interface_control_set_value(&ui,
        &controls[SORT_CONTROL_INDEX], sort_mode);
    if (result == ASTRA_OK) result = reload_directory();
    if (result != ASTRA_OK) {
        sort_mode = previous;
        (void)astra_interface_control_set_value(&ui,
            &controls[SORT_CONTROL_INDEX], previous);
    }
    return result;
}

static AstraResult show_hidden_set(uint32_t value)
{
    uint32_t previous = show_hidden;
    AstraResult result;

    if (worker != 0u || !visible_data || directory.status != ASTRA_VFS_OK)
        return ASTRA_ERROR_BUSY;
    show_hidden = value != 0u;
    result = astra_interface_ui_set_state(&ui,
        &controls[HIDDEN_CONTROL_INDEX],
        show_hidden ? ASTRA_CONTROL_SELECTED : 0u);
    if (result == ASTRA_OK) result = reload_directory();
    if (result != ASTRA_OK) {
        show_hidden = previous;
        (void)astra_interface_ui_set_state(&ui,
            &controls[HIDDEN_CONTROL_INDEX],
            previous ? ASTRA_CONTROL_SELECTED : 0u);
    }
    return result;
}

static AstraResult view_set(uint32_t value)
{
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint32_t outcome = ASTRA_FILE_PANEL_NONE;
    AstraResult result;

    if (value > ASTRA_FILE_PANEL_VIEW_COLUMNS)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action.type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action.control_id = 1u + ASTRA_FILE_PANEL_VIEW;
    action.value = value;
    result = astra_interface_control_set_value(&ui,
        BROWSER_CONTROL(ASTRA_FILE_PANEL_VIEW), value);
    if (result == ASTRA_OK)
        result = astra_interface_file_panel_action(&browser, &ui,
            BROWSER_CONTROL(0), &action, &outcome);
    if (result == ASTRA_OK) result = browser_update_status();
    return result;
}

static void menu_state(uint32_t row, int enabled, int checked)
{
    atlas_menu[row].flags =
        (atlas_menu[row].flags & ASTRA_MENU_PARENT_MASK) |
        (enabled ? 0u : ASTRA_MENU_DISABLED) |
        (checked ? ASTRA_MENU_CHECKED : 0u);
}

static AstraResult menu_sync(void)
{
    uint32_t selected = selected_path[0] != '\0' &&
        (selected_kind == ASTRA_VFS_KIND_DIRECTORY ||
         selected_kind == ASTRA_VFS_KIND_FILE);
    uint32_t writable = writable_folder_root() && worker == 0u &&
        directory.status == ASTRA_VFS_OK;
    uint32_t active = worker == 0u && visible_data &&
        directory.status == ASTRA_VFS_OK;
    uint32_t changed = atlas_menu_ready == 0u;

    menu_state(ATLAS_MENU_NEW, writable, 0);
    menu_state(ATLAS_MENU_OPEN, selected, 0);
    menu_state(ATLAS_MENU_COPY, selected && clipboard_service != ASTRA_INVALID_HANDLE, 0);
    menu_state(ATLAS_MENU_PASTE, writable && clipboard_has_file(), 0);
    menu_state(ATLAS_MENU_RENAME, writable && selected, 0);
    menu_state(ATLAS_MENU_DELETE, writable && selected, 0);
    menu_state(ATLAS_MENU_REFRESH, worker == 0u && !volume_lost, 0);
    menu_state(ATLAS_MENU_LIST, 1, browser.view_mode == ASTRA_FILE_PANEL_VIEW_LIST);
    menu_state(ATLAS_MENU_GRID, 1, browser.view_mode == ASTRA_FILE_PANEL_VIEW_GRID);
    menu_state(ATLAS_MENU_COLUMNS, 1,
        browser.view_mode == ASTRA_FILE_PANEL_VIEW_COLUMNS);
    menu_state(ATLAS_MENU_SORT_NAME, active,
        sort_mode == ASTRA_DIRECTORY_SORT_NAME);
    menu_state(ATLAS_MENU_SORT_SIZE, active,
        sort_mode == ASTRA_DIRECTORY_SORT_SIZE);
    menu_state(ATLAS_MENU_SORT_KIND, active,
        sort_mode == ASTRA_DIRECTORY_SORT_KIND);
    menu_state(ATLAS_MENU_SORT_MODIFIED, active,
        sort_mode == ASTRA_DIRECTORY_SORT_MODIFIED);
    menu_state(ATLAS_MENU_HIDDEN, active, show_hidden != 0u);
    for (uint32_t at = 0u; at < ATLAS_MENU_COUNT; ++at)
        if (atlas_menu_sent[at] != atlas_menu[at].flags)
            changed = 1u;
    if (!changed) return ASTRA_OK;
    AstraResult result = astra_window_menu_bar_set(&window, atlas_menu,
        ATLAS_MENU_COUNT);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < ATLAS_MENU_COUNT; ++at)
        atlas_menu_sent[at] = atlas_menu[at].flags;
    atlas_menu_ready = 1u;
    return ASTRA_OK;
}

static AstraResult back_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"Back", 4u,
        navigation_count != 0u && navigation_index != 0u, 0u};
    return ASTRA_OK;
}

static AstraResult forward_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"Forward", 7u,
        navigation_index + 1u < navigation_count, 0u};
    return ASTRA_OK;
}

static AstraResult go_back(void *opaque, const AstraCommandArguments *args)
{
    (void)opaque;
    (void)args;
    if (navigation_count == 0u || navigation_index == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return navigate_to(navigation_history[navigation_index - 1u],
                       navigation_index - 1u);
}

static AstraResult go_forward(void *opaque, const AstraCommandArguments *args)
{
    (void)opaque;
    (void)args;
    if (navigation_index + 1u >= navigation_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return navigate_to(navigation_history[navigation_index + 1u],
                       navigation_index + 1u);
}

static AtlasCommands toolbar_command_context = {0};
static const AstraCommand toolbar_command_table[] = {
    {"atlas.back", 10u, 0u, 0u, back_query, go_back,
        &toolbar_command_context},
    {"atlas.forward", 13u, 0u, 0u, forward_query, go_forward,
        &toolbar_command_context},
    {"atlas.folder", 12u, 0u, 0u, folder_query, create_folder,
        &toolbar_command_context},
    {"atlas.open", 10u, 0u, 0u, selected_query, open_selected,
        &toolbar_command_context},
    {"atlas.delete", 12u, 0u, 0u, delete_query, delete_selected,
        &toolbar_command_context},
    {"atlas.refresh", 13u, 0u, 0u, refresh_query, refresh_directory,
        &toolbar_command_context}
};
static const AstraCommandScope toolbar_command_scope = {
    NULL, toolbar_command_table, 6u};
static const AstraToolbarCommand toolbar_actions[] = {
    {"atlas.back", 10u, "Back", 4u,
        ASTRA_TOOLBAR_ICON_BACK, {0u, 0u}},
    {"atlas.forward", 13u, "Forward", 7u,
        ASTRA_TOOLBAR_ICON_FORWARD, {0u, 0u}},
    {"atlas.folder", 12u, "New drawer", 10u,
        ASTRA_TOOLBAR_ICON_OPEN, {0u, 0u}},
    {"atlas.open", 10u, "Open", 4u,
        ASTRA_TOOLBAR_ICON_FORWARD, {0u, 0u}},
    {"atlas.delete", 12u, "Delete", 6u,
        ASTRA_TOOLBAR_ICON_TRASH, {0u, 0u}},
    {"atlas.refresh", 13u, "Refresh", 7u,
        ASTRA_TOOLBAR_ICON_RESTART, {0u, 0u}}
};
static const AstraToolbarGroup toolbar_groups[] = {
    {"Navigate", 8u, 0u, 2u, 0u, ASTRA_TOOLBAR_GROUP_PINNED, {0u, 0u}},
    {"File", 4u, 2u, 4u, 1u, 0u, {0u, 0u}}
};

static AstraResult fit_toolbar(void)
{
    AstraToolbarFit fit = ASTRA_TOOLBAR_FIT_INIT;
    uint32_t room = width > 430u ? width - 430u : 180u;
    AstraResult result = astra_interface_toolbar_actions_fit(&toolbar,
        &ui, room, ASTRA_TOOLBAR_LABELS, &fit);

    return result == ASTRA_ERROR_NO_RESOURCES ? ASTRA_OK : result;
}

static AstraResult init_toolbar(AstraHandle gui)
{
    AstraToolbarActionsInfo info = ASTRA_TOOLBAR_ACTIONS_INFO_INIT;

    toolbar_command_context.gui = gui;
    info.first_id = TOOLBAR_FIRST_ID;
    info.groups = toolbar_groups;
    info.group_count = sizeof(toolbar_groups) / sizeof(toolbar_groups[0]);
    info.actions = toolbar_actions;
    info.action_count = sizeof(toolbar_actions) / sizeof(toolbar_actions[0]);
    info.scope = &toolbar_command_scope;
    if (astra_interface_toolbar_actions_control_count(&info) !=
            TOOLBAR_CONTROL_COUNT ||
        astra_interface_toolbar_actions_init(&toolbar, controls,
            &info) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult menu_invoke(AstraHandle gui, uint32_t row)
{
    switch (row) {
    case ATLAS_MENU_NEW:
        return create_folder(&toolbar_command_context, NULL);
    case ATLAS_MENU_OPEN:
        return open_selected(&toolbar_command_context, NULL);
    case ATLAS_MENU_COPY:
        return copy_selected(&toolbar_command_context, NULL);
    case ATLAS_MENU_PASTE:
        return paste_into_current(&toolbar_command_context, NULL);
    case ATLAS_MENU_RENAME:
        return rename_selected(&toolbar_command_context, NULL);
    case ATLAS_MENU_DELETE:
        return delete_selected(&toolbar_command_context, NULL);
    case ATLAS_MENU_REFRESH:
        return refresh_directory(NULL, NULL);
    case ATLAS_MENU_LIST:
        return view_set(ASTRA_FILE_PANEL_VIEW_LIST);
    case ATLAS_MENU_GRID:
        return view_set(ASTRA_FILE_PANEL_VIEW_GRID);
    case ATLAS_MENU_COLUMNS:
        return view_set(ASTRA_FILE_PANEL_VIEW_COLUMNS);
    case ATLAS_MENU_SORT_NAME:
    case ATLAS_MENU_SORT_SIZE:
    case ATLAS_MENU_SORT_KIND:
    case ATLAS_MENU_SORT_MODIFIED:
        return sort_invoke((void *)&sort_commands[
            row - ATLAS_MENU_SORT_NAME], NULL);
    case ATLAS_MENU_HIDDEN:
        return show_hidden_set(!show_hidden);
    case ATLAS_MENU_ABOUT: {
        AstraAlertInfo alert = ASTRA_ALERT_INFO_INIT;

        alert.title = "About Atlas";
        alert.title_length = 11u;
        alert.message = "Browse volumes, drawers, and applications.";
        alert.message_length = sizeof("Browse volumes, drawers, and applications.") - 1u;
        alert.button = "OK";
        alert.button_length = 2u;
        return astra_interface_show_alert(gui, &alert);
    }
    default:
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
}

static AstraResult run(AstraHandle gui)
{
    AstraContextMenu context_menu = ASTRA_CONTEXT_MENU_INIT;
    AtlasCommands command_context = {gui};
    AstraContextMenuItem menu_items[5];
    uint16_t menu_parents[5] = {0u};
    const AstraCommand commands[] = {
        {"atlas.new", 9u, 0u, 0u, menu_query, create_folder, &command_context},
        {"atlas.folder", 12u, 0u, 0u, folder_query, create_folder, &command_context},
        {"atlas.open", 10u, 0u, 0u, selected_query, open_selected, &command_context},
        {"atlas.copy", 10u, 0u, 0u, copy_query, copy_selected, &command_context},
        {"atlas.paste", 11u, 0u, 0u, paste_query, paste_into_current, &command_context},
        {"atlas.rename", 12u, 0u, 0u, rename_query, rename_selected, &command_context},
        {"atlas.delete", 12u, 0u, 0u, delete_query, delete_selected, &command_context},
        {"atlas.package", 13u, 0u, 0u, package_query, create_installer, &command_context},
        {"atlas.sort.name", sizeof("atlas.sort.name") - 1u, 0u, 0u,
            sort_query, sort_invoke, (void *)&sort_commands[0]},
        {"atlas.sort.size", sizeof("atlas.sort.size") - 1u, 0u, 0u,
            sort_query, sort_invoke, (void *)&sort_commands[1]},
        {"atlas.sort.kind", sizeof("atlas.sort.kind") - 1u, 0u, 0u,
            sort_query, sort_invoke, (void *)&sort_commands[2]},
        {"atlas.sort.modified", sizeof("atlas.sort.modified") - 1u, 0u, 0u,
            sort_query, sort_invoke, (void *)&sort_commands[3]}};
    const AstraCommandScope command_scope = {
        &toolbar_command_scope, commands, 12u};
    static const AstraCommandShortcut shortcuts[] = {
        {0x06u, ASTRA_COMMAND_SHORTCUT_PRIMARY, "atlas.copy", sizeof("atlas.copy") - 1u},
        {0x19u, ASTRA_COMMAND_SHORTCUT_PRIMARY, "atlas.paste", sizeof("atlas.paste") - 1u},
        {0x11u, ASTRA_COMMAND_SHORTCUT_PRIMARY, "atlas.folder", sizeof("atlas.folder") - 1u},
        {0x12u, ASTRA_COMMAND_SHORTCUT_PRIMARY, "atlas.open", sizeof("atlas.open") - 1u},
        {0x15u, ASTRA_COMMAND_SHORTCUT_PRIMARY, "atlas.refresh", sizeof("atlas.refresh") - 1u},
        {0x50u, ASTRA_COMMAND_SHORTCUT_ALT, "atlas.back", sizeof("atlas.back") - 1u},
        {0x4fu, ASTRA_COMMAND_SHORTCUT_ALT, "atlas.forward", sizeof("atlas.forward") - 1u}
    };
    for (;;) {
        uint32_t waits[3] = {astra_window_event_wait_handle(&window)};
        uint32_t wait_count = 1u, worker_slot = UINT32_MAX, volume_slot = UINT32_MAX;
        if (worker != 0u) { worker_slot = wait_count; waits[wait_count++] = worker; }
        if (volume_watch.receive != ASTRA_INVALID_HANDLE && !volume_lost) {
            volume_slot = wait_count; waits[wait_count++] = volume_watch.receive;
        }
        uint32_t selected = 0u;
        AstraWindowEvent event = {0};
        AstraControlFrame damage;
        uint32_t repaint = 0u;
        AstraResult result;

        uint32_t waited = astra_wait_multiple(waits, wait_count,
            volume_refresh ? volume_retry : ASTRA_DEADLINE_FOREVER,
            &selected, NULL);
        if (waited != ASTRA_SYSCALL_OK && waited != ASTRA_SYSCALL_TIMED_OUT &&
            !(selected == volume_slot && waited == ASTRA_SYSCALL_PEER_DEAD))
            return ASTRA_ERROR_IO;
        if (waited != ASTRA_SYSCALL_TIMED_OUT && selected == volume_slot) {
            AstraVolumeChange change;
            result = astra_volume_watch_receive(&volume_watch, &change);
            if (result != ASTRA_OK || change.status != ASTRA_STATUS_OK) {
                if (viewed_volume.id != 0u) volume_lost = 1u;
                else {
                    AstraResult ignored = astra_volume_watch_close(
                        volume_metadata, &volume_watch);
                    (void)ignored;
                }
            }
            volume_refresh = 1u;
            volume_retry = 0u;
        }
        if (worker != 0u && waited == ASTRA_SYSCALL_OK && selected == worker_slot) {
            (void)astra_close(worker);
            worker = 0u;
            if (__atomic_load_n(&completed, __ATOMIC_ACQUIRE) == 0u)
                directory.status = ASTRA_VFS_ERR_IO;
            volume_refresh = volume_refresh || viewed_volume.id != 0u;
            volume_retry = 0u;
            visible_data = !volume_refresh;
        }
        if (volume_refresh && astra_clock_monotonic() >= volume_retry) {
            if (viewed_volume.id == 0u &&
                volume_watch.receive == ASTRA_INVALID_HANDLE)
                result = astra_volume_watch_open(volume_metadata,
                    &volume_watch);
            else result = ASTRA_OK;
            if (result == ASTRA_OK)
                result = volume_lost ? ASTRA_ERROR_INVALID_HANDLE :
                    viewed_volume.id != 0u ? volume_observe(NULL) : ASTRA_OK;
            if (result == ASTRA_OK)
                result = browser_update_places();
            if ((viewed_volume.id == 0u && result != ASTRA_OK) ||
                result == ASTRA_ERROR_BUSY || result == ASTRA_ERROR_OUT_OF_MEMORY ||
                result == ASTRA_ERROR_IO) {
                volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
            } else {
                volume_refresh = 0u;
                if (result != ASTRA_OK && viewed_volume.id != 0u) {
                    volume_lost = 1u;
                    __atomic_store_n(&cancelled, 1u, __ATOMIC_RELEASE);
                    visible_data = 0u;
                } else if (worker == 0u) visible_data = 1u;
                repaint = 1u;
            }
        }
        if (worker == 0u && (repaint || (waited == ASTRA_SYSCALL_OK && selected == worker_slot))) {
            if (volume_lost) {
                astra_directory_snapshot_dispose(&directory);
                directory.status = ASTRA_VFS_ERR_LOST;
                visible_data = 1u;
            }
            log_root();
            result = astra_interface_ui_set_state(&ui,
                &controls[SORT_CONTROL_INDEX],
                directory.status == ASTRA_VFS_OK ? 0u : ASTRA_CONTROL_DISABLED);
            if (result != ASTRA_OK) return result;
            result = astra_interface_ui_set_state(&ui,
                &controls[HIDDEN_CONTROL_INDEX],
                directory.status == ASTRA_VFS_OK ? 0u : ASTRA_CONTROL_DISABLED);
            if (result != ASTRA_OK) return result;
            result = browser_set_directory(&directory, root);
            if (result != ASTRA_OK) {
                (void)astra_log_failure("atlas browser directory", (uint32_t)(-result));
                return result;
            }
            load_entry_icons();
            result = browser_set_icons();
            if (result != ASTRA_OK) {
                (void)astra_log_failure("atlas browser icons", (uint32_t)(-result));
                return result;
            }
            result = browser_update_status();
            if (result != ASTRA_OK) return result;
            result = browser_update_space();
            if (result != ASTRA_OK) return result;
            log_icons();
            repaint = 1u;
        }
        result = astra_window_event_try(&window, &event);
        while (result == ASTRA_OK) {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
            int handled = 0;
            AstraResult menu_result = astra_interface_context_menu_handle_event(
                &context_menu, &event, &handled);
            if (handled) {
                if (menu_result != ASTRA_OK)
                    (void)astra_log_failure("atlas menu command", (uint32_t)(-menu_result));
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_MENU_RESULT &&
                event.data.menu_result.request == ASTRA_WINDOW_MENU_BAR_REQUEST) {
                uint32_t row = event.data.menu_result.selection == 0u ?
                    ATLAS_MENU_COUNT : event.data.menu_result.selection - 1u;
                if (row == ATLAS_MENU_CLOSE) return ASTRA_OK;
                if (row < ATLAS_MENU_COUNT &&
                    (atlas_menu[row].flags & ASTRA_MENU_PARENT_MASK) != 0u &&
                    (atlas_menu[row].flags & ASTRA_MENU_DISABLED) == 0u &&
                    row != ATLAS_MENU_SEPARATOR) {
                    AstraResult invoked = menu_invoke(gui, row);
                    if (invoked != ASTRA_OK) {
                        (void)astra_log_failure("atlas menu action",
                            (uint32_t)(-invoked));
                        open_error(gui, invoked);
                    }
                    repaint = 1u;
                }
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (astra_interface_toolbar_actions_handle_event(&toolbar,
                    &event, &handled) != ASTRA_OK)
                return ASTRA_ERROR_IO;
            if (handled) {
                repaint = 1u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
                return ASTRA_OK;
            if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
                AstraResult refreshed = astra_fonts_use_system(gui, &event);
                if (refreshed != ASTRA_OK)
                    (void)astra_log_failure("atlas font refresh", (uint32_t)refreshed);
            }
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE &&
                event.data.resize.width != 0u && event.data.resize.height != 0u &&
                (width != event.data.resize.width || height != event.data.resize.height)) {
                AstraSurfaceInfo target = ASTRA_SURFACE_INFO_INIT;

                result = astra_surface_close(&content);
                if (result == ASTRA_OK)
                    result = astra_window_surface(&window, &content);
                if (result == ASTRA_OK)
                    result = astra_surface_get_info(&content, &target);
                if (result != ASTRA_OK)
                    return result;
                width = target.width;
                height = target.height;
                event.data.resize.width = width;
                event.data.resize.height = height;
            }
            if (name_edit.active && (event.type == ASTRA_WINDOW_EVENT_KEY ||
                                     event.type == ASTRA_WINDOW_EVENT_TEXT)) {
                AstraResult edit_result = astra_interface_inline_edit_event(&name_edit, &event, &action);
                if (edit_result != ASTRA_OK) return edit_result;
                if (action.type == ASTRA_UI_ACTION_ACTIVATE ||
                    action.type == ASTRA_UI_ACTION_DISMISS)
                    edit_result = finish_edit(gui, action.type == ASTRA_UI_ACTION_ACTIVATE);
                else repaint = 1u;
                if (edit_result != ASTRA_OK)
                    (void)astra_log_failure("atlas folder edit", (uint32_t)(-edit_result));
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_KEY &&
                (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
                (event.flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u) {
                uint32_t focused = 0u;
                result = astra_interface_ui_focused_control(&ui, &focused);
                if (result != ASTRA_OK) return result;
                if (focused != 1u + ASTRA_FILE_PANEL_SEARCH) {
                    uint8_t shortcut_handled = 0u;
                    result = astra_interface_command_shortcut(&command_scope,
                        event.data.key.usage, event.data.key.modifiers,
                        shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0]),
                        &shortcut_handled);
                    if (shortcut_handled) {
                        if (result != ASTRA_OK) {
                            (void)astra_log_failure("atlas shortcut",
                                (uint32_t)(-result));
                            open_error(gui, result);
                        }
                        repaint = 1u;
                        result = astra_window_event_try(&window, &event);
                        continue;
                    }
                    if (result != ASTRA_OK) return result;
                }
            }
            if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE) {
                if (fit_toolbar() != ASTRA_OK || layout() != ASTRA_OK)
                    return ASTRA_ERROR_IO;
            }
            AstraResult toolbar_result =
                astra_interface_toolbar_actions_handle_action(&toolbar,
                    &window, &action, &handled);
            if (handled) {
                if (toolbar_result != ASTRA_OK) {
                    (void)astra_log_failure("atlas toolbar command",
                        (uint32_t)(-toolbar_result));
                    open_error(gui, toolbar_result);
                }
                repaint = 1u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (toolbar_result != ASTRA_OK) return toolbar_result;
            if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
                action.control_id == SORT_CONTROL_ID) {
                AstraControlFrame frame, clip;
                if (astra_interface_ui_geometry(&ui, SORT_CONTROL_INDEX,
                        &frame, &clip) == ASTRA_OK) {
                    const char *ids[] = {"atlas.sort.name", "atlas.sort.size",
                        "atlas.sort.kind", "atlas.sort.modified"};
                    for (uint32_t at = 0u; at < 4u; ++at) {
                        menu_items[at] = (AstraContextMenuItem){ids[at],
                            (uint32_t)strlen(ids[at]), NULL};
                        menu_parents[at] = 0u;
                    }
                    AstraResult opened = astra_interface_context_menu_open_tree(
                        &context_menu, &window, &command_scope, menu_items,
                        menu_parents, 4u, frame.x, frame.y + frame.height);
                    if (opened != ASTRA_OK)
                        (void)astra_log_failure("atlas sort menu", (uint32_t)(-opened));
                }
                repaint = 1u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
                (action.control_id == SORT_CONTROL_ID ||
                 action.control_id == HIDDEN_CONTROL_ID)) {
                AstraResult changed = action.control_id == SORT_CONTROL_ID &&
                    action.value >= 0 &&
                    action.value <= ASTRA_DIRECTORY_SORT_MODIFIED ?
                    sort_invoke((void *)&sort_commands[action.value], NULL) :
                    action.control_id == HIDDEN_CONTROL_ID ?
                        show_hidden_set(action.value != 0) :
                        ASTRA_ERROR_INVALID_ARGUMENT;
                if (changed != ASTRA_OK) {
                    (void)astra_log_failure("atlas sort/filter",
                        (uint32_t)(-changed));
                    open_error(gui, changed);
                }
                repaint = 1u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (action.type != ASTRA_UI_ACTION_NONE) {
                uint32_t outcome = ASTRA_FILE_PANEL_NONE;
                AstraResult browser_result = astra_interface_file_panel_action(
                    &browser, &ui, BROWSER_CONTROL(0), &action, &outcome);
                if (browser_result == ASTRA_OK &&
                    action.type == ASTRA_UI_ACTION_TEXT_CHANGED &&
                    action.control_id == 1u + ASTRA_FILE_PANEL_SEARCH)
                    selection_clear();
                if (browser_result == ASTRA_OK &&
                    action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
                    (action.control_id == 1u + ASTRA_FILE_PANEL_GRID ||
                     action.control_id == 1u + ASTRA_FILE_PANEL_LIST)) {
                    if (action.value >= 0 &&
                        (uint32_t)action.value < browser.item_count)
                        browser_result = selection_set(root, directory.entries,
                            directory.count,
                            (uint32_t)browser.items[action.value].id - 1u,
                            0u);
                    else selection_clear();
                }
                if (browser_result == ASTRA_OK &&
                    (action.type == ASTRA_UI_ACTION_VALUE_CHANGED ||
                     action.type == ASTRA_UI_ACTION_ACTIVATE) &&
                    action.control_id >= 1u + ASTRA_FILE_PANEL_CONTROL_COUNT + 2u &&
                    action.control_id < 1u +
                        ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(BROWSER_COLUMNS)) {
                    uint32_t offset = action.control_id - 1u -
                        ASTRA_FILE_PANEL_CONTROL_COUNT - 2u;
                    uint32_t depth = offset / 2u;
                    if ((offset & 1u) == 0u && depth < browser.column_count &&
                        action.value >= 0 &&
                        (uint32_t)action.value <
                            browser.column_snapshots[depth].item_count) {
                        const AstraFilePanelColumnSnapshot *snapshot =
                            &browser.column_snapshots[depth];
                        browser_result = selection_set(snapshot->directory,
                            snapshot->entries, snapshot->entry_count,
                            (uint32_t)snapshot->items[action.value].id - 1u,
                            depth);
                    }
                }
                if (browser_result == ASTRA_OK &&
                    action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
                    action.control_id == 1u + ASTRA_FILE_PANEL_VIEW &&
                    selected_depth != 0u)
                    selection_clear();
                if (browser_result == ASTRA_OK &&
                    (action.type == ASTRA_UI_ACTION_TEXT_CHANGED ||
                     action.type == ASTRA_UI_ACTION_VALUE_CHANGED))
                    browser_result = browser_update_status();
                if (browser_result == ASTRA_OK &&
                    outcome == ASTRA_FILE_PANEL_LOAD_CHILD)
                    browser_result = action.type == ASTRA_UI_ACTION_ACTIVATE &&
                        astra_string_has_suffix(browser.target, ".app") ?
                        browser_open_target(ASTRA_FILE_PANEL_NAVIGATE) :
                        browser_load_child();
                else if (browser_result == ASTRA_OK &&
                    (outcome == ASTRA_FILE_PANEL_NAVIGATE ||
                     outcome == ASTRA_FILE_PANEL_PICKED))
                    browser_result = browser_open_target(outcome);
                if (browser_result != ASTRA_OK) {
                    (void)astra_log_failure("atlas browser action",
                        (uint32_t)(-browser_result));
                    open_error(gui, browser_result);
                }
                if (astra_interface_toolbar_actions_refresh(&toolbar,
                        &ui) != ASTRA_OK)
                    return ASTRA_ERROR_IO;
                repaint = 1u;
            }
            if (event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
                event.data.pointer.y >= (int32_t)TOOLBAR_HEIGHT &&
                action.type == ASTRA_UI_ACTION_CONTEXT_MENU) {
                uint32_t depth = 0u, index = UINT32_MAX;
                if (visible_data && directory.status == ASTRA_VFS_OK) {
                    AstraResult hit = astra_interface_file_panel_context_entry(
                        &browser, &action, &index, &depth);
                    if (hit != ASTRA_OK) return hit;
                }
                {
                    const AstraFilePanelColumnSnapshot *snapshot =
                        depth < browser.column_count ?
                            &browser.column_snapshots[depth] : NULL;
                    AstraResult picked = depth == 0u ?
                        selection_set(root, directory.entries,
                            directory.count, index, 0u) :
                        snapshot != NULL ?
                            selection_set(snapshot->directory, snapshot->entries,
                                snapshot->entry_count, index, depth) :
                            ASTRA_ERROR_INVALID_ARGUMENT;
                    if (picked != ASTRA_OK) return picked;
                    uint32_t count = 0u;
                    memset(menu_parents, 0, sizeof(menu_parents));
                    if (index == UINT32_MAX && writable_folder_root()) {
                        menu_items[count++] = (AstraContextMenuItem){"atlas.new", 9u, NULL};
                        menu_items[count++] = (AstraContextMenuItem){"atlas.folder", 12u, NULL};
                        menu_parents[1] = 1u;
                        menu_items[count++] = (AstraContextMenuItem){"atlas.paste", 11u, NULL};
                    } else if (selected_path[0] != '\0') {
                        menu_items[count++] = (AstraContextMenuItem){"atlas.open", 10u, NULL};
                        menu_items[count++] = (AstraContextMenuItem){"atlas.copy", 10u, NULL};
                        if (selected_bundle(&command_context))
                            menu_items[count++] = (AstraContextMenuItem){"atlas.package", 13u, NULL};
                        if (writable_folder_root()) {
                            menu_items[count++] = (AstraContextMenuItem){"atlas.rename", 12u, NULL};
                            menu_items[count++] = (AstraContextMenuItem){"atlas.delete", 12u, NULL};
                        }
                    }
                    if (count != 0u) {
                        AstraResult opened = astra_interface_context_menu_open_tree(
                            &context_menu, &window, &command_scope, menu_items,
                            menu_parents, count, event.data.pointer.x,
                            event.data.pointer.y);
                        if (opened != ASTRA_OK)
                            (void)astra_log_failure("atlas context menu", (uint32_t)(-opened));
                    }
                }
                if (astra_interface_toolbar_actions_refresh(&toolbar,
                        &ui) != ASTRA_OK)
                    return ASTRA_ERROR_IO;
            }
            result = astra_window_event_try(&window, &event);
        }
        if (result != ASTRA_ERROR_WOULD_BLOCK)
            return result;
        if (astra_interface_toolbar_actions_refresh(&toolbar,
                &ui) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK)
            repaint = 1u;
        if (repaint != 0u) {
            AstraResult menu_status = menu_sync();
            if (menu_status != ASTRA_OK) return menu_status;
            AstraResult painted = paint();
            if (painted != ASTRA_OK) {
                (void)astra_log_failure("atlas paint", (uint32_t)(-painted));
                return painted;
            }
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui, *ready, *launch_capability;
    AstraArea placeholder = ASTRA_AREA_INIT;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraScrollModelInfo model = ASTRA_SCROLL_MODEL_INFO_INIT;
    uint32_t status;
    AstraResult result = ASTRA_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    const AstraStartupCapability *volume_write = astra_startup_capability(
        startup, ASTRA_CAPABILITY_VOLUME_WRITE);
    volume_write_broker = volume_write != NULL ? volume_write->handle :
        ASTRA_INVALID_HANDLE;
    volume_write_available = volume_write_broker != ASTRA_INVALID_HANDLE;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    launch_capability = astra_startup_capability(startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    if (gui == NULL || ready == NULL || launch_capability == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    launcher = launch_capability->handle;
    const AstraStartupCapability *volume_read = astra_startup_capability(startup,
        ASTRA_CAPABILITY_VOLUME_READ);
    volume_read_broker = volume_read != NULL ? volume_read->handle :
        ASTRA_INVALID_HANDLE;
    const AstraStartupCapability *ram_read = astra_startup_capability(startup,
        ASTRA_CAPABILITY_RAM_READ);
    const AstraStartupCapability *ram_write = astra_startup_capability(startup,
        ASTRA_CAPABILITY_RAM_WRITE);
    volume_ram_read_broker = ram_read != NULL ? ram_read->handle :
        ASTRA_INVALID_HANDLE;
    volume_ram_write_broker = ram_write != NULL ? ram_write->handle :
        ASTRA_INVALID_HANDLE;
    const AstraStartupCapability *clipboard = astra_startup_capability(startup,
        ASTRA_CAPABILITY_CLIPBOARD);
    clipboard_service = clipboard != NULL ? clipboard->handle :
        ASTRA_INVALID_HANDLE;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return ASTRA_STATUS_IO;
    const char *first_argument = startup->argc > 1u ? astra_startup_argument(startup, 1u) : NULL;
    int exact_volume = startup->argc == 5u && first_argument != NULL && strcmp(first_argument, "--volume") == 0;
    root = exact_volume ? "/dh0" : startup->argc > 1u ? astra_startup_argument(startup, 1u) : "/dh0";
    if (root == NULL || root[0] != '/' || (!exact_volume && startup->argc > 2u) ||
        strlen(root) >= ASTRA_VFS_PATH_MAX)
        return ASTRA_STATUS_INVALID;
    if (exact_volume && !astra_volume_reference_parse(
        astra_startup_argument(startup, 2u),
        astra_startup_argument(startup, 3u),
        astra_startup_argument(startup, 4u), &viewed_volume))
        return ASTRA_STATUS_INVALID;
    status = astra_process_filesystem_open(&filesystem, startup);
    if (status == ASTRA_VFS_OK) {
        if (exact_volume && volume_open(startup) != ASTRA_OK)
            status = ASTRA_VFS_ERR_LOST;
        else if (!exact_volume) {
            const AstraStartupCapability *metadata = astra_startup_capability(
                startup, ASTRA_CAPABILITY_VOLUMES);
            if (metadata != NULL) {
                volume_metadata = metadata->handle;
                if (astra_volume_watch_open(volume_metadata, &volume_watch) == ASTRA_OK) {
                    volume_refresh = 1u;
                    volume_retry = 0u;
                }
            }
        }
    }
    if (status == ASTRA_VFS_OK) {
        if (!astra_string_copy(current_path, sizeof(current_path), root))
            status = ASTRA_VFS_ERR_LIMIT;
        else {
            root = current_path;
            if (navigation_append(root, &viewed_volume) != ASTRA_OK)
                status = ASTRA_VFS_ERR_LIMIT;
        }
    }
    const char *step = "filesystem";

    if (status == ASTRA_VFS_OK) {
        step = "area create";
        result = astra_area_create(4096u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &placeholder);
    } else
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) {
        step = "window create";
        info.x = 360u;
        info.y = 120u;
        info.width = width;
        info.height = height;
        info.flags = ASTRA_WINDOW_RESIZABLE;
        info.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = exact_volume ? volume_title : strcmp(root, "/dh0") == 0 ? "DH0" : "Atlas";
        info.title_length = (uint16_t)strlen(info.title);
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON | ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
            ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT;
        result = astra_window_create(gui->handle, placeholder.handle, &info, &window);
    }
    if (placeholder.handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(&placeholder);
        (void)ignored;
    }
    if (result == ASTRA_OK) {
        step = "window name";
        result = astra_window_set_application_name(&window, "Atlas", 5u);
    }
    if (result == ASTRA_OK) {
        step = "window title";
        result = update_window_title();
    }
    if (result == ASTRA_OK) {
        step = "display open";
        result = astra_window_display(&window, &display);
    }
    if (result == ASTRA_OK) {
        step = "window surface";
        result = astra_window_surface(&window, &content);
    }
    if (result == ASTRA_OK) {
        step = "folder icon";
        result = load_icon("/apps/Atlas.app/resources/drawer.aicon", &folder_icon);
    }
    if (result == ASTRA_OK) {
        step = "file icon";
        result = load_icon("/apps/Atlas.app/resources/document.aicon", &file_icon);
    }
    if (result == ASTRA_OK) {
        step = "installer icon";
        result = load_icon("/apps/Atlas.app/resources/installer.aicon", &installer_icon);
    }
    model.line_height = CELL_HEIGHT + GRID_GAP_Y;
    if (result == ASTRA_OK) {
        step = "scroll model";
        result = astra_scroll_init(&scroll, &model);
    }
    if (result == ASTRA_OK) {
        step = "control tree";
        result = build_view(gui->handle);
    }
    if (result == ASTRA_OK) {
        step = "menu bar";
        result = menu_sync();
    }
    if (result == ASTRA_OK) {
        step = "first paint";
        result = paint();
    }
    if (result == ASTRA_OK) {
        step = "window activation";
        result = astra_window_activate(&window);
    }
    if (result == ASTRA_OK) {
        step = "directory worker";
        AstraThreadStart start = {read_root, 0u};
        if (astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
            ASTRA_RIGHT_WAIT | ASTRA_RIGHT_READ | ASTRA_RIGHT_ADMINISTER,
            &worker, NULL) != ASTRA_SYSCALL_OK)
            result = ASTRA_ERROR_NO_RESOURCES;
    }
    (void)astra_service_ready(ready->handle,
        result == ASTRA_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_PROGRAM_FIRST,
        NULL, 0u);
    (void)astra_close(ready->handle);
    if (result == ASTRA_OK)
        result = run(gui->handle);
    (void)astra_interface_toolbar_actions_close(&toolbar);
    __atomic_store_n(&cancelled, 1u, __ATOMIC_RELEASE);
    if (astra_window_event_wait_handle(&window) != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_window_close(&window);
        (void)ignored;
    }
    if (worker != 0u) {
        (void)astra_rt_cancel_wait(worker);
        if (astra_wait_one(worker, astra_clock_monotonic() + UINT64_C(500000000), NULL) ==
            ASTRA_SYSCALL_OK) {
            (void)astra_close(worker);
            worker = 0u;
        }
    }
    /* Native process teardown owns a worker still retiring; never free its state. */
    if (worker == 0u) {
        astra_interface_file_panel_dispose(&browser);
        browser_clear_children();
        astra_runtime_deallocate(browser_entry_icons);
        astra_runtime_deallocate(browser_grid_icons);
        astra_runtime_deallocate(browser_selected_ids);
        astra_runtime_deallocate(browser_items[0]);
        astra_runtime_deallocate(browser_items[1]);
        if (browser_places != initial_places)
            astra_runtime_deallocate(browser_places);
        if (browser_place_items != initial_place_items)
            astra_runtime_deallocate(browser_place_items);
        astra_volume_snapshot_close(&sidebar_volumes);
        clear_entry_icons();
        astra_directory_snapshot_dispose(&directory);
        astra_runtime_deallocate(controls);
        astra_process_filesystem_close(&filesystem);
        volume_close();
    }
    navigation_dispose();
    if (result != ASTRA_OK) {
        (void)astra_log(step);
        (void)astra_log_failure("Atlas window", (uint32_t)(-result));
    }
    return result == ASTRA_OK ? 0 : ASTRA_STATUS_PROGRAM_FIRST;
}
