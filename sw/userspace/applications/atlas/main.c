#include <astra/application.h>
#include <astra/application_catalog.h>
#include <astra/area.h>
#include <astra/draw_list.h>
#include <astra/fonts.h>
#include <astra/graphics_library.h>
#include <astra/input.h>
#include <astra/icon_tile.h>
#include <astra/inline_edit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/package_library.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
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
#define TOOLBAR_CONTROL_COUNT 8u

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
static AstraUIContext toolbar_ui = ASTRA_UI_CONTEXT_INIT;
static AstraToolbarActions toolbar = ASTRA_TOOLBAR_ACTIONS_INIT;
static AstraControl toolbar_controls[TOOLBAR_CONTROL_COUNT];
static uint32_t toolbar_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraControl *controls;
static uint32_t worker, cancelled, completed, visible_data;
static uint16_t width = 760u, height = 460u;
static const char *root;
static AstraVolumeReference viewed_volume;
static AstraVolumeWatch volume_watch = ASTRA_VOLUME_WATCH_INIT;
static AstraHandle volume_metadata, volume_grant;
static AstraAssignTable volume_assigns = ASTRA_ASSIGN_TABLE_INIT;
static AstraVfsClient volume_client;
static AstraFilesystem volume_filesystem = ASTRA_FILESYSTEM_INIT;
static uint32_t volume_connected, volume_lost, volume_refresh;
static uint64_t volume_retry;
static char volume_path[32], volume_title[ASTRA_WINDOW_TITLE_MAX + 1u];
static AstraHandle launcher;
static AstraInlineEdit name_edit = ASTRA_INLINE_EDIT_INIT;
static uint32_t edit_index = UINT32_MAX;
static char edit_path[ASTRA_VFS_PATH_MAX];
static uint32_t edit_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
static uint32_t selected_index = UINT32_MAX;

static int desktop_folder_root(void)
{
    return strncmp(root, "/home/Desktop", 13u) == 0 &&
        (root[13] == '\0' || root[13] == '/');
}

static AstraVfsClient *volume_client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    return assign->handle == volume_grant && volume_connected ? &volume_client : NULL;
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

static void volume_close(void)
{
    if (volume_watch.receive != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_volume_watch_close(volume_metadata, &volume_watch);
        (void)ignored;
    }
    astra_filesystem_detach(&volume_filesystem);
    if (volume_connected) { (void)astra_vfs_disconnect(&volume_client); volume_connected = 0u; }
    astra_assign_table_destroy(&volume_assigns);
    if (volume_grant != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&volume_grant);
        (void)ignored;
    }
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
        } else {
            result = astra_volume_root(broker->handle, &viewed_volume, ASTRA_RIGHT_READ, &volume_grant);
            if (result == ASTRA_OK)
                result = astra_result_from_service(astra_assign_bind(&volume_assigns,
                    root + 1u, volume_grant, ASTRA_RIGHT_READ, ""));
            if (result == ASTRA_OK) {
                result = astra_result_from_service(astra_vfs_client_connect_service(&volume_client, volume_grant));
                volume_connected = result == ASTRA_OK;
            }
            if (result == ASTRA_OK)
                result = astra_result_from_service(astra_filesystem_attach(&volume_filesystem,
                    &volume_assigns, volume_client_for, NULL, NULL));
        }
    }
    if (result != ASTRA_OK) volume_close();
    return result;
}

static void read_root(uint32_t argument)
{
    (void)argument;
    (void)astra_directory_snapshot_load(&directory,
                                volume_connected ? &volume_filesystem : &filesystem.filesystem, root,
                                &cancelled);
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
    if (!volume_connected)
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
            AstraFilesystem *source = volume_connected ?
                &volume_filesystem : &filesystem.filesystem;
            if (astra_folder_path(root, entry->name, path,
                    sizeof(path)) == ASTRA_VFS_OK &&
                astra_package_read_icon(source, path, &bytes,
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
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    uint32_t viewport_width = width > 24u ? width - 24u : 1u;
    uint32_t grid_width = viewport_width > CELL_WIDTH + 2u * GRID_PADDING ?
        viewport_width : CELL_WIDTH + 2u * GRID_PADDING;
    uint32_t columns = (grid_width - 2u * GRID_PADDING + GRID_GAP_X) /
                       (CELL_WIDTH + GRID_GAP_X);
    uint32_t count = visible_data != 0u ? directory.count : 0u;
    uint64_t rows = count / columns + (count % columns != 0u);
    uint64_t grid_height = rows * CELL_HEIGHT +
        (rows == 0u ? 0u : rows - 1u) * GRID_GAP_Y + 2u * GRID_PADDING;

    if (grid_height > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    item.parent_id = 1u;
    item.minimum_width = item.maximum_width = grid_width;
    item.minimum_height = item.maximum_height = (uint32_t)grid_height;
    if (astra_interface_control_set_flex(&controls[1], &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    outer.direction = ASTRA_FLEX_COLUMN;
    outer.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    outer.padding_left = outer.padding_right = 12u;
    outer.padding_top = TOOLBAR_HEIGHT + 12u;
    outer.padding_bottom = 12u;
    return astra_interface_ui_layout(&ui, &outer);
}

static AstraResult build_view(void)
{
    uint32_t count = visible_data != 0u ? directory.count : 0u;
    AstraControl *replacement;
    AstraUIContext replacement_ui = ASTRA_UI_CONTEXT_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraContainerInfo grid = ASTRA_CONTAINER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    if (count > SIZE_MAX / sizeof(*replacement) - 2u)
        return ASTRA_ERROR_NO_RESOURCES;
    replacement = astra_runtime_allocate((size_t)(count + 2u) * sizeof(*replacement));
    if (replacement == NULL)
        return ASTRA_ERROR_NO_RESOURCES;
    memset(replacement, 0, (size_t)(count + 2u) * sizeof(*replacement));
    for (uint32_t at = 0u; at < count + 2u; ++at)
        replacement[at] = (AstraControl)ASTRA_CONTROL_INIT;
    view.id = 1u;
    view.model = &scroll;
    view.preferred_width = width;
    view.preferred_height = height;
    if (astra_interface_scroll_view_init(&replacement[0], &view) != ASTRA_OK)
        goto invalid;
    item.grow = 1u;
    if (astra_interface_control_set_flex(&replacement[0], &item) != ASTRA_OK)
        goto invalid;
    grid.id = 2u;
    grid.layout.flags = ASTRA_FLEX_WRAP;
    grid.layout.padding_left = grid.layout.padding_right = GRID_PADDING;
    grid.layout.padding_top = grid.layout.padding_bottom = GRID_PADDING;
    grid.layout.main_gap = GRID_GAP_X;
    grid.layout.cross_gap = GRID_GAP_Y;
    if (astra_interface_container_init(&replacement[1], &grid) != ASTRA_OK)
        goto invalid;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = 1u;
    if (astra_interface_control_set_flex(&replacement[1], &item) != ASTRA_OK)
        goto invalid;
    for (uint32_t at = 0u; at < count; ++at) {
        AstraContainerInfo cell = ASTRA_CONTAINER_INFO_INIT;

        cell.id = at + 3u;
        if (astra_interface_container_init(&replacement[at + 2u], &cell) != ASTRA_OK)
            goto invalid;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = 2u;
        item.basis = CELL_WIDTH;
        item.shrink = 0u;
        item.minimum_width = item.maximum_width = CELL_WIDTH;
        item.minimum_height = item.maximum_height = CELL_HEIGHT;
        if (astra_interface_control_set_flex(&replacement[at + 2u], &item) != ASTRA_OK)
            goto invalid;
    }
    if (astra_interface_ui_init(&replacement_ui, replacement, count + 2u,
                                width, height) != ASTRA_OK)
        goto invalid;
    astra_runtime_deallocate(controls);
    controls = replacement;
    ui = replacement_ui;
    return layout();
invalid:
    astra_runtime_deallocate(replacement);
    return ASTRA_ERROR_INVALID_ARGUMENT;
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
    if (result == ASTRA_OK && (visible_data == 0u || directory.count == 0u ||
                               directory.status != ASTRA_VFS_OK)) {
        const char *text = message();
        result = astra_draw_ui_text(&list,
            (AstraPointI32){28, TOOLBAR_HEIGHT + 32u}, text,
            (uint32_t)strlen(text), theme.title_font_height, 0u,
            !volume_lost && (visible_data == 0u || directory.status == ASTRA_VFS_OK) ? theme.client_text_secondary : theme.fault);
    }
    if (result == ASTRA_OK) {
        AstraSurfaceView toolbar_view;

        if (!astra_draw_list_view_init(&toolbar_view, toolbar_commands,
                sizeof(toolbar_commands), width, height))
            result = ASTRA_ERROR_NO_RESOURCES;
        if (result == ASTRA_OK)
            result = astra_interface_ui_render(&toolbar_ui, &toolbar_view);
        if (result == ASTRA_OK)
            result = astra_draw_list_append_view(&list, &toolbar_view);
    }
    for (uint32_t at = 0u; result == ASTRA_OK && visible_data != 0u &&
                           directory.status == ASTRA_VFS_OK && at < directory.count; ++at) {
        AstraControlFrame frame, clip;
        const AstraDirectoryEntry *entry = &directory.entries[at];
        AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;

        result = astra_interface_ui_geometry(&ui, at + 2u, &frame, &clip);
        if (result != ASTRA_OK)
            break;
        if (clip.width == 0u || clip.height == 0u ||
            (int64_t)frame.y + frame.height <= clip.y ||
            frame.y >= (int64_t)clip.y + clip.height)
            continue;
        tile.frame = (AstraRectI32){frame.x, frame.y, frame.width, frame.height};
        tile.clip = (AstraRectI32){clip.x, clip.y, clip.width, clip.height};
        tile.icon = at < entry_icon_count &&
            entry_icons[at]._private_handle != ASTRA_INVALID_HANDLE ?
            &entry_icons[at] : entry->kind == ASTRA_VFS_KIND_DIRECTORY ?
            &folder_icon : &file_icon;
        if (entry->kind == ASTRA_VFS_KIND_FILE &&
            astra_string_has_suffix(entry->name, ".apkg"))
            tile.badge = &installer_icon;
        tile.label = entry->name;
        tile.label_bytes = (uint32_t)strlen(entry->name);
        tile.editor = name_edit.active && at == edit_index ? &name_edit.surface : NULL;
        result = astra_interface_icon_tile_render(&list, &tile);
    }
    if (result == ASTRA_OK)
        result = astra_draw_submit(&list, &fence);
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
    if (result == ASTRA_OK)
        astra_interface_ui_damage_clear(&ui);
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
    if (astra_window_get_info(&window, &info) != ASTRA_OK) return;
    int32_t origin_x = info.frame.x + theme.frame_width;
    int32_t origin_y = info.frame.y + theme.frame_width +
        theme.titlebar_height + theme.signal_height;
    for (uint32_t at = 0u; at < directory.count; ++at) {
        AstraControlFrame frame, clip;
        char path[ASTRA_VFS_PATH_MAX], line[ASTRA_VFS_PATH_MAX + 96u];
        if (astra_interface_ui_geometry(&ui, at + 2u, &frame, &clip) != ASTRA_OK ||
            clip.width == 0u || clip.height == 0u ||
            astra_folder_path(root, directory.entries[at].name,
                path, sizeof(path)) != ASTRA_VFS_OK) continue;
        AstraString out;
        astra_string_init(&out, line, sizeof(line));
        (void)astra_string_append(&out, "atlas icon ");
        (void)astra_string_append(&out, path);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_i64(&out, origin_x + frame.x);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_i64(&out, origin_y + frame.y);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_u64(&out, frame.width);
        (void)astra_string_append_char(&out, ' ');
        (void)astra_string_append_u64(&out, frame.height);
        (void)astra_log(line);
    }
}

typedef struct AtlasCommands {
    AstraHandle gui;
    uint32_t *selection;
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
    info.title = "Folder Error";
    info.title_length = 12u;
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

static AstraResult reload_directory(void)
{
    selected_index = UINT32_MAX;
    uint32_t status = astra_directory_snapshot_load(&directory,
        volume_connected ? &volume_filesystem : &filesystem.filesystem, root, NULL);
    if (status != ASTRA_VFS_OK) return astra_result_from_service(status);
    load_entry_icons();
    log_root();
    AstraResult result = build_view();
    if (result == ASTRA_OK) log_icons();
    if (result == ASTRA_OK) result = paint();
    return result;
}

static uint32_t hit_entry(int32_t x, int32_t y)
{
    if (!visible_data || directory.status != ASTRA_VFS_OK) return UINT32_MAX;
    for (uint32_t at = 0u; at < directory.count; ++at) {
        AstraControlFrame frame, clip;
        AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;
        if (astra_interface_ui_geometry(&ui, at + 2u, &frame, &clip) != ASTRA_OK)
            return UINT32_MAX;
        tile.frame = (AstraRectI32){frame.x, frame.y, frame.width, frame.height};
        tile.clip = (AstraRectI32){clip.x, clip.y, clip.width, clip.height};
        if (astra_interface_icon_tile_hit(&tile, x, y))
            return at;
    }
    return UINT32_MAX;
}

static AstraResult open_entry(uint32_t index)
{
    if (index >= directory.count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    char path[ASTRA_VFS_PATH_MAX];
    if (astra_folder_path(root, directory.entries[index].name,
        path, sizeof(path)) != ASTRA_VFS_OK) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t pid;
    size_t length = strlen(path);
    if (directory.entries[index].kind == ASTRA_VFS_KIND_FILE) {
        const char *arguments[] = {path};
        return astra_application_launch_with_arguments(launcher,
            "/apps/OpenWith.app", (uint16_t)strlen("/apps/OpenWith.app"),
            ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 1u, &pid);
    }
    if (directory.entries[index].kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (length >= 4u && strcmp(path + length - 4u, ".app") == 0)
        return astra_application_launch(launcher, path, (uint16_t)length, &pid);
    const char *arguments[] = {path};
    return astra_application_launch_with_arguments(launcher, "/apps/Atlas.app",
        (uint16_t)strlen("/apps/Atlas.app"), ASTRA_LAUNCH_SOURCE_DESKTOP,
        arguments, 1u, &pid);
}

static AstraResult start_edit(uint32_t index)
{
    if (!desktop_folder_root() || index >= directory.count ||
        directory.entries[index].kind != ASTRA_VFS_KIND_DIRECTORY ||
        astra_folder_path(root, directory.entries[index].name,
            edit_path, sizeof(edit_path)) != ASTRA_VFS_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_interface_inline_edit_start(&name_edit, &display,
        directory.entries[index].name);
    if (result != ASTRA_OK) return result;
    edit_index = index;
    return paint();
}

static AstraResult finish_edit(AstraHandle gui, int commit)
{
    if (!name_edit.active) return ASTRA_OK;
    if (commit) {
        char name[ASTRA_VFS_NAME_MAX], moved[ASTRA_VFS_PATH_MAX];
        AstraResult result = astra_interface_inline_edit_text(&name_edit, name, sizeof(name));
        if (result != ASTRA_OK || !astra_folder_name_valid(name)) {
            folder_error(gui, "Choose a valid folder name", ASTRA_VFS_ERR_INVALID);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        uint32_t status = astra_folder_rename_safe(&filesystem.filesystem,
            edit_path, root, name, moved, sizeof(moved));
        if (status != ASTRA_VFS_OK) {
            folder_error(gui, "Folder could not be renamed", status);
            return astra_result_from_service(status);
        }
    }
    astra_interface_inline_edit_stop(&name_edit);
    edit_index = UINT32_MAX;
    return reload_directory();
}

static AstraResult menu_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"New", 3u, desktop_folder_root() &&
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
    const AtlasCommands *context = opaque;
    uint32_t index = *context->selection;
    *state = (AstraCommandState){"Open", 4u,
        index < directory.count &&
        (directory.entries[index].kind == ASTRA_VFS_KIND_DIRECTORY ||
         directory.entries[index].kind == ASTRA_VFS_KIND_FILE), 0u};
    return ASTRA_OK;
}

static AstraResult rename_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = selected_query(opaque, state);
    state->label = "Rename";
    state->label_length = 6u;
    state->enabled = state->enabled && desktop_folder_root();
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
    uint32_t index = *context->selection;
    return index < directory.count &&
        directory.entries[index].kind == ASTRA_VFS_KIND_DIRECTORY &&
        astra_string_has_suffix(directory.entries[index].name, ".app") &&
        !volume_connected;
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
    uint32_t index = *context->selection;
    char bundle[ASTRA_VFS_PATH_MAX], archive[ASTRA_VFS_PATH_MAX];
    AstraString output;
    if (!selected_bundle(context) ||
        astra_folder_path(root, directory.entries[index].name,
            bundle, sizeof(bundle)) != ASTRA_VFS_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_string_init(&output, archive, sizeof(archive));
    uint32_t length = (uint32_t)strlen(directory.entries[index].name);
    if (!astra_string_append(&output, "/home/Desktop/") ||
        !astra_string_append_bytes(&output, directory.entries[index].name,
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
    if (!desktop_folder_root() || worker != 0u) return ASTRA_ERROR_PERMISSION;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_folder_create_numbered(&filesystem.filesystem,
        root, "Folder", path, sizeof(path));
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Folder could not be created", status);
        return astra_result_from_service(status);
    }
    AstraResult result = reload_directory();
    if (result != ASTRA_OK) return result;
    const char *name = astra_folder_leaf(path);
    for (uint32_t at = 0u; at < directory.count; ++at)
        if (strcmp(directory.entries[at].name, name) == 0)
            return start_edit(at);
    return ASTRA_ERROR_IO;
}

static AstraResult open_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    return open_entry(*context->selection);
}

static AstraResult rename_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    return start_edit(*context->selection);
}

static AstraResult delete_selected(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    AtlasCommands *context = opaque;
    uint32_t index = *context->selection;
    if (!desktop_folder_root() || index >= directory.count ||
        directory.entries[index].kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    char path[ASTRA_VFS_PATH_MAX], moved[ASTRA_VFS_PATH_MAX];
    if (astra_folder_path(root, directory.entries[index].name,
        path, sizeof(path)) != ASTRA_VFS_OK) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t status = astra_folder_move_to_trash(&filesystem.filesystem,
        path, "/home/.Trash", moved, sizeof(moved));
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Folder could not be deleted", status);
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

static AtlasCommands toolbar_command_context = {0, &selected_index};
static const AstraCommand toolbar_command_table[] = {
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
    NULL, toolbar_command_table, 4u};
static const AstraToolbarCommand toolbar_actions[] = {
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
    {"File", 4u, 0u, 3u, 0u, ASTRA_TOOLBAR_GROUP_PINNED, {0u, 0u}},
    {"View", 4u, 3u, 1u, 1u, 0u, {0u, 0u}}
};

static AstraResult fit_toolbar(void)
{
    AstraToolbarFit fit = ASTRA_TOOLBAR_FIT_INIT;
    AstraResult result = astra_interface_toolbar_actions_fit(&toolbar,
        &toolbar_ui, width, ASTRA_TOOLBAR_LABELS, &fit);

    return result == ASTRA_ERROR_NO_RESOURCES ? ASTRA_OK : result;
}

static AstraResult init_toolbar(AstraHandle gui)
{
    AstraToolbarActionsInfo info = ASTRA_TOOLBAR_ACTIONS_INFO_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    toolbar_command_context.gui = gui;
    for (uint32_t at = 0u; at < TOOLBAR_CONTROL_COUNT; ++at)
        toolbar_controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    info.first_id = TOOLBAR_FIRST_ID;
    info.groups = toolbar_groups;
    info.group_count = sizeof(toolbar_groups) / sizeof(toolbar_groups[0]);
    info.actions = toolbar_actions;
    info.action_count = sizeof(toolbar_actions) / sizeof(toolbar_actions[0]);
    info.scope = &toolbar_command_scope;
    if (astra_interface_toolbar_actions_control_count(&info) !=
            TOOLBAR_CONTROL_COUNT ||
        astra_interface_toolbar_actions_init(&toolbar, toolbar_controls,
            &info) != ASTRA_OK ||
        astra_interface_ui_init(&toolbar_ui, toolbar_controls,
            TOOLBAR_CONTROL_COUNT, width, height) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (astra_interface_ui_layout(&toolbar_ui, &layout) != ASTRA_OK ||
        astra_interface_toolbar_actions_refresh(&toolbar, &toolbar_ui) !=
            ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return fit_toolbar();
}

static AstraResult run(AstraHandle gui)
{
    AstraContextMenu context_menu = ASTRA_CONTEXT_MENU_INIT;
    uint32_t menu_index = UINT32_MAX, clicked_index = UINT32_MAX;
    AtlasCommands command_context = {gui, &menu_index};
    AstraContextMenuItem menu_items[5];
    uint16_t menu_parents[5] = {0u};
    const AstraCommand commands[] = {
        {"atlas.new", 9u, 0u, 0u, menu_query, create_folder, &command_context},
        {"atlas.folder", 12u, 0u, 0u, folder_query, create_folder, &command_context},
        {"atlas.open", 10u, 0u, 0u, selected_query, open_selected, &command_context},
        {"atlas.rename", 12u, 0u, 0u, rename_query, rename_selected, &command_context},
        {"atlas.delete", 12u, 0u, 0u, delete_query, delete_selected, &command_context},
        {"atlas.package", 13u, 0u, 0u, package_query, create_installer, &command_context}};
    const AstraCommandScope command_scope = {NULL, commands, 6u};
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
            if (result != ASTRA_OK || change.status != ASTRA_STATUS_OK) volume_lost = 1u;
            volume_refresh = 1u;
            volume_retry = 0u;
        }
        if (worker != 0u && waited == ASTRA_SYSCALL_OK && selected == worker_slot) {
            (void)astra_close(worker);
            worker = 0u;
            if (__atomic_load_n(&completed, __ATOMIC_ACQUIRE) == 0u)
                directory.status = ASTRA_VFS_ERR_IO;
            volume_refresh = viewed_volume.id != 0u;
            volume_retry = 0u;
            visible_data = !volume_refresh;
        }
        if (volume_refresh && astra_clock_monotonic() >= volume_retry) {
            result = volume_lost ? ASTRA_ERROR_INVALID_HANDLE : volume_observe(NULL);
            if (result == ASTRA_ERROR_BUSY || result == ASTRA_ERROR_OUT_OF_MEMORY || result == ASTRA_ERROR_IO) {
                volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
            } else {
                volume_refresh = 0u;
                if (result != ASTRA_OK) {
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
            load_entry_icons();
            result = build_view();
            if (result != ASTRA_OK)
                return result;
            log_icons();
            repaint = 1u;
        }
        result = astra_window_event_try(&window, &event);
        while (result == ASTRA_OK) {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
            AstraUIAction toolbar_action = ASTRA_UI_ACTION_INIT;
            int handled = 0;
            AstraResult menu_result = astra_interface_context_menu_handle_event(
                &context_menu, &event, &handled);
            if (handled) {
                if (menu_result != ASTRA_OK)
                    (void)astra_log_failure("atlas menu command", (uint32_t)(-menu_result));
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
            if (astra_interface_ui_handle_event(&toolbar_ui, &event,
                    &toolbar_action) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE &&
                fit_toolbar() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            AstraResult toolbar_result =
                astra_interface_toolbar_actions_handle_action(&toolbar,
                    &window, &toolbar_action, &handled);
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
            if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE && layout() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            if (event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
                event.data.pointer.y >= TOOLBAR_HEIGHT &&
                (action.type == ASTRA_UI_ACTION_CONTEXT_MENU ||
                 (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u)) {
                uint32_t index = hit_entry(event.data.pointer.x, event.data.pointer.y);
                if (action.type == ASTRA_UI_ACTION_CONTEXT_MENU) {
                    menu_index = index;
                    selected_index = index;
                    uint32_t count = 0u;
                    memset(menu_parents, 0, sizeof(menu_parents));
                    if (index == UINT32_MAX && desktop_folder_root()) {
                        menu_items[count++] = (AstraContextMenuItem){"atlas.new", 9u, NULL};
                        menu_items[count++] = (AstraContextMenuItem){"atlas.folder", 12u, NULL};
                        menu_parents[1] = 1u;
                    } else if (index < directory.count &&
                        (directory.entries[index].kind == ASTRA_VFS_KIND_DIRECTORY ||
                         directory.entries[index].kind == ASTRA_VFS_KIND_FILE)) {
                        menu_items[count++] = (AstraContextMenuItem){"atlas.open", 10u, NULL};
                        if (selected_bundle(&command_context))
                            menu_items[count++] = (AstraContextMenuItem){"atlas.package", 13u, NULL};
                        if (desktop_folder_root()) {
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
                } else if (event.data.pointer.button == ASTRA_INPUT_BUTTON_LEFT) {
                    if (event.data.pointer.click_count == 2u && index == clicked_index &&
                        index < directory.count &&
                        (directory.entries[index].kind == ASTRA_VFS_KIND_DIRECTORY ||
                         directory.entries[index].kind == ASTRA_VFS_KIND_FILE)) {
                        AstraResult opened = open_entry(index);
                        if (opened != ASTRA_OK) {
                            (void)astra_log_failure("atlas open", (uint32_t)(-opened));
                            open_error(gui, opened);
                        }
                    } else clicked_index = index;
                    selected_index = index;
                }
                if (astra_interface_toolbar_actions_refresh(&toolbar,
                        &toolbar_ui) != ASTRA_OK)
                    return ASTRA_ERROR_IO;
            }
            result = astra_window_event_try(&window, &event);
        }
        if (result != ASTRA_ERROR_WOULD_BLOCK)
            return result;
        if (astra_interface_toolbar_actions_refresh(&toolbar,
                &toolbar_ui) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK)
            repaint = 1u;
        if (astra_interface_ui_damage(&toolbar_ui, &damage) == ASTRA_OK)
            repaint = 1u;
        if (repaint != 0u && paint() != ASTRA_OK)
            return ASTRA_ERROR_IO;
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
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    launch_capability = astra_startup_capability(startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    if (gui == NULL || ready == NULL || launch_capability == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    launcher = launch_capability->handle;
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
    if (status == ASTRA_VFS_OK && exact_volume && volume_open(startup) != ASTRA_OK)
        status = ASTRA_VFS_ERR_LOST;
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
        result = build_view();
    }
    if (result == ASTRA_OK) {
        step = "toolbar";
        result = init_toolbar(gui->handle);
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
        clear_entry_icons();
        astra_directory_snapshot_dispose(&directory);
        astra_runtime_deallocate(controls);
        astra_process_filesystem_close(&filesystem);
        volume_close();
    }
    if (result != ASTRA_OK) {
        (void)astra_log(step);
        (void)astra_log_failure("Atlas window", (uint32_t)(-result));
    }
    return result == ASTRA_OK ? 0 : ASTRA_STATUS_PROGRAM_FIRST;
}
