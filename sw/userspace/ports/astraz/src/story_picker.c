#include "story_picker.h"

#include <astra/filesystem_kit.h>
#include <astra/config_library.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/theme.h>

#include "../../../common/directory_snapshot.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PICKER_WIDTH 720u
#define PICKER_HEIGHT 500u
#define STORIES_DIRECTORY "/home/Stories"
#define LAST_DIRECTORY_KEY "last_story_directory"
static const AstraConfigField config_fields[] = {{LAST_DIRECTORY_KEY, 0u}};
static const AstraConfigSchema config_schema = {1u, config_fields, 1u};

typedef struct StoryPicker {
    AstraProcessFilesystem files;
    AstraWindow window;
    AstraSharedSurface surface;
    AstraControl controls[ASTRA_FILE_PANEL_CONTROL_COUNT];
    AstraUIContext ui;
    AstraFilePanel panel;
    AstraCollectionModel collection;
    AstraScrollModel scroll;
    AstraDirectorySnapshot snapshots[2];
    AstraCollectionItem *items[2];
    const AstraSurface **icons;
    uint32_t capacity;
    uint32_t bank;
    AstraCollectionItem place_items[5];
    AstraConfig config;
    char initial_directory[ASTRA_VFS_PATH_MAX];
    AstraHandle gui;
    uint8_t files_open;
    uint8_t config_open;
} StoryPicker;

static StoryPicker picker;

static const AstraFilePanelPlace places[] = {
    {"Volumes", 7u, "/"}, {"Home", 4u, "/home"},
    {"Stories", 7u, STORIES_DIRECTORY}, {"Work", 4u, "/work"},
    {"DH0", 3u, "/dh0"}
};

static AstraResult allocate_items(uint32_t capacity,
    AstraCollectionItem **first, AstraCollectionItem **second,
    const AstraSurface ***icons)
{
    if (capacity == 0u || capacity > SIZE_MAX / sizeof(**first) ||
        capacity > SIZE_MAX / sizeof(**icons))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    *first = astra_runtime_allocate((size_t)capacity * sizeof(**first));
    *second = astra_runtime_allocate((size_t)capacity * sizeof(**second));
    *icons = astra_runtime_allocate((size_t)capacity * sizeof(**icons));
    if (*first == NULL || *second == NULL || *icons == NULL) {
        astra_runtime_deallocate(*first);
        astra_runtime_deallocate(*second);
        astra_runtime_deallocate((void *)*icons);
        *first = *second = NULL;
        *icons = NULL;
        return ASTRA_ERROR_NO_RESOURCES;
    }
    memset((void *)*icons, 0, (size_t)capacity * sizeof(**icons));
    return ASTRA_OK;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraWindowFrame frame = {0u, 0u, PICKER_WIDTH, PICKER_HEIGHT};
    if (!astra_draw_list_view_init(&picker.surface.view,
            picker.surface.view.pixels, picker.surface.view.byte_size,
            PICKER_WIDTH, PICKER_HEIGHT)) return ASTRA_ERROR_IO;
    astra_surface_clear(&picker.surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&picker.ui, &picker.surface.view) != ASTRA_OK ||
        astra_window_present_region(&picker.window, &frame) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    astra_interface_ui_damage_clear(&picker.ui);
    return ASTRA_OK;
}

static AstraResult navigate(void)
{
    uint32_t next = 1u - picker.bank;
    uint32_t status = astra_directory_snapshot_load(&picker.snapshots[next],
        &picker.files.filesystem, picker.panel.target, NULL);
    if (status != ASTRA_VFS_OK)
        return astra_interface_control_set_text(&picker.ui,
            &picker.controls[ASTRA_FILE_PANEL_TITLE],
            "Cannot open folder", sizeof("Cannot open folder") - 1u);
    if (picker.snapshots[next].count > picker.capacity) {
        AstraCollectionItem *first = NULL, *second = NULL;
        const AstraSurface **icons = NULL;
        uint32_t capacity = picker.snapshots[next].capacity;
        AstraResult result = allocate_items(capacity, &first, &second,
            &icons);
        if (result != ASTRA_OK) return result;
        result = astra_interface_file_panel_rebind_storage(&picker.panel,
            &picker.ui, picker.controls, first, capacity, second,
            capacity, icons, capacity);
        if (result != ASTRA_OK) {
            astra_runtime_deallocate(first);
            astra_runtime_deallocate(second);
            astra_runtime_deallocate((void *)icons);
            return result;
        }
        astra_runtime_deallocate(picker.items[0]);
        astra_runtime_deallocate(picker.items[1]);
        astra_runtime_deallocate((void *)picker.icons);
        picker.items[0] = first;
        picker.items[1] = second;
        picker.icons = icons;
        picker.capacity = capacity;
        result = astra_interface_file_panel_set_directory(&picker.panel,
            &picker.ui, picker.controls, picker.panel.target,
            picker.snapshots[next].entries, picker.snapshots[next].count,
            second, capacity);
        if (result != ASTRA_OK) return result;
    } else {
        AstraCollectionItem *spare = picker.panel.items == picker.items[0] ?
            picker.items[1] : picker.items[0];
        AstraResult result = astra_interface_file_panel_set_directory(
            &picker.panel, &picker.ui, picker.controls, picker.panel.target,
            picker.snapshots[next].entries, picker.snapshots[next].count,
            spare, picker.capacity);
        if (result != ASTRA_OK) return result;
    }
    astra_directory_snapshot_dispose(&picker.snapshots[picker.bank]);
    picker.bank = next;
    return astra_interface_control_set_text(&picker.ui,
        &picker.controls[ASTRA_FILE_PANEL_TITLE],
        "Open Story", sizeof("Open Story") - 1u);
}

static AstraResult run(char *path, uint32_t capacity, int *selected)
{
    for (;;) {
        uint32_t handle = astra_window_event_wait_handle(&picker.window);
        uint32_t ready = 0u;
        AstraWindowEvent event = {0};
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        uint32_t outcome = ASTRA_FILE_PANEL_NONE;
        if (astra_wait_multiple(&handle, 1u, ASTRA_DEADLINE_FOREVER,
                &ready, NULL) != ASTRA_SYSCALL_OK) return ASTRA_ERROR_IO;
        AstraResult received = astra_window_event_try(&picker.window, &event);
        if (received == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (received != ASTRA_OK) return received;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) return ASTRA_OK;
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(picker.gui,
                &event) != ASTRA_OK) return ASTRA_ERROR_IO;
        if (astra_interface_ui_handle_event(&picker.ui, &event, &action) !=
                ASTRA_OK ||
            astra_interface_file_panel_action(&picker.panel, &picker.ui,
                picker.controls, &action, &outcome) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (outcome == ASTRA_FILE_PANEL_CANCELLED) return ASTRA_OK;
        if (outcome == ASTRA_FILE_PANEL_NAVIGATE && navigate() != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (outcome == ASTRA_FILE_PANEL_PICKED) {
            size_t length = strlen(picker.panel.target);
            if (length + 1u > capacity) return ASTRA_ERROR_BUFFER_TOO_SMALL;
            memcpy(path, picker.panel.target, length + 1u);
            *selected = 1;
            return ASTRA_OK;
        }
        AstraControlFrame damage;
        if (astra_interface_ui_damage(&picker.ui, &damage) == ASTRA_OK &&
            present() != ASTRA_OK) return ASTRA_ERROR_IO;
    }
}

AstraResult astraz_story_picker_open(const AstraStartupInfo *startup,
    AstraHandle gui, const AstraWindow *parent, const char *current,
    char *path, uint32_t capacity, int *selected)
{
    AstraFilePanelInfo info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowCreateInfo window_info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result = ASTRA_ERROR_IO;
    const char *step = "open filesystem";
    const char *start = STORIES_DIRECTORY;
    if (startup == NULL || parent == NULL || path == NULL || capacity == 0u ||
        selected == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    *selected = 0;
    memset(&picker, 0, sizeof(picker));
    picker.files = (AstraProcessFilesystem)ASTRA_PROCESS_FILESYSTEM_INIT;
    picker.config = (AstraConfig)ASTRA_CONFIG_INIT;
    picker.gui = gui;
    picker.window = (AstraWindow)ASTRA_WINDOW_INIT;
    picker.panel = (AstraFilePanel)ASTRA_FILE_PANEL_INIT;
    picker.ui = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    picker.collection = (AstraCollectionModel)ASTRA_COLLECTION_MODEL_INIT;
    picker.scroll = (AstraScrollModel)ASTRA_SCROLL_MODEL_INIT;
    picker.snapshots[0] = (AstraDirectorySnapshot)ASTRA_DIRECTORY_SNAPSHOT_INIT;
    picker.snapshots[1] = (AstraDirectorySnapshot)ASTRA_DIRECTORY_SNAPSHOT_INIT;
    if (astra_process_filesystem_open(&picker.files, startup) != ASTRA_VFS_OK)
        goto done;
    picker.files_open = 1u;
    (void)astra_filesystem_mkdir(&picker.files.filesystem, STORIES_DIRECTORY);
    picker.config_open = astra_config_open(startup, &config_schema,
        ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE,
        &picker.config, NULL) == ASTRA_CONFIG_OK;
    if (current != NULL && current[0] == '/') {
        const char *slash = strrchr(current, '/');
        size_t length = slash == NULL ? 0u : (size_t)(slash - current);
        if (length == 0u) length = 1u;
        if (length < sizeof(picker.initial_directory)) {
            memcpy(picker.initial_directory, current, length);
            picker.initial_directory[length] = '\0';
            start = picker.initial_directory;
        }
    } else if (picker.config_open) {
        uint32_t length = 0u;
        if (astra_config_get_string(&picker.config, LAST_DIRECTORY_KEY, 0u,
                picker.initial_directory, sizeof(picker.initial_directory),
                &length) == ASTRA_CONFIG_OK &&
            picker.initial_directory[0] == '/')
            start = picker.initial_directory;
    }
    step = "load directory";
    if (astra_directory_snapshot_load(&picker.snapshots[0],
            &picker.files.filesystem, start, NULL) != ASTRA_VFS_OK) {
        start = STORIES_DIRECTORY;
        if (astra_directory_snapshot_load(&picker.snapshots[0],
                &picker.files.filesystem, start, NULL) != ASTRA_VFS_OK) {
            start = "/home";
            if (astra_directory_snapshot_load(&picker.snapshots[0],
                    &picker.files.filesystem, start, NULL) != ASTRA_VFS_OK) {
                start = "/";
                if (astra_directory_snapshot_load(&picker.snapshots[0],
                        &picker.files.filesystem, start, NULL) != ASTRA_VFS_OK)
                    goto done;
            }
        }
    }
    picker.capacity = picker.snapshots[0].capacity == 0u ?
        32u : picker.snapshots[0].capacity;
    step = "allocate items";
    if (allocate_items(picker.capacity, &picker.items[0],
            &picker.items[1], &picker.icons) != ASTRA_OK) goto done;
    scroll_info.line_width = 8u;
    scroll_info.line_height = 16u;
    step = "initialize scroll";
    if (astra_scroll_init(&picker.scroll, &scroll_info) != ASTRA_OK)
        goto done;
    info.first_id = 1u;
    info.mode = ASTRA_FILE_PANEL_OPEN;
    info.directory = start;
    info.entries = picker.snapshots[0].entries;
    info.entry_count = picker.snapshots[0].count;
    info.item_storage = picker.items[0];
    info.item_capacity = picker.capacity;
    info.spare_item_storage = picker.items[1];
    info.spare_item_capacity = picker.capacity;
    info.grid_icons = picker.icons;
    info.grid_icon_capacity = picker.capacity;
    info.places = places;
    info.place_count = sizeof(places) / sizeof(places[0]);
    info.place_items = picker.place_items;
    info.place_capacity = sizeof(picker.place_items) /
        sizeof(picker.place_items[0]);
    info.collection = &picker.collection;
    info.scroll = &picker.scroll;
    for (uint32_t at = 0u; at < ASTRA_FILE_PANEL_CONTROL_COUNT; ++at)
        picker.controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = layout.padding_right = 16u;
    layout.padding_top = layout.padding_bottom = 16u;
    layout.main_gap = 8u;
    step = "initialize file panel";
    if (astra_interface_file_panel_init(&picker.panel, picker.controls,
            &info) != ASTRA_OK ||
        astra_interface_ui_init(&picker.ui, picker.controls,
            ASTRA_FILE_PANEL_CONTROL_COUNT, PICKER_WIDTH,
            PICKER_HEIGHT) != ASTRA_OK ||
        astra_interface_ui_layout(&picker.ui, &layout) != ASTRA_OK ||
        astra_interface_control_set_text(&picker.ui,
            &picker.controls[ASTRA_FILE_PANEL_TITLE], "Open Story",
            sizeof("Open Story") - 1u) != ASTRA_OK ||
        astra_shared_draw_list_create(&picker.surface, PICKER_WIDTH,
            PICKER_HEIGHT) != ASTRA_SYSCALL_OK) goto done;
    window_info.type = ASTRA_WINDOW_SHEET;
    window_info.flags = 0u;
    window_info.x = 40u;
    window_info.y = 30u;
    window_info.width = PICKER_WIDTH;
    window_info.height = PICKER_HEIGHT;
    window_info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    window_info.title = "Open Story";
    window_info.title_length = sizeof("Open Story") - 1u;
    window_info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
        ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT;
    step = "create sheet";
    result = astra_window_sheet_create(gui, picker.surface.area,
        &window_info, parent, &picker.window);
    if (result != ASTRA_OK) goto done;
    step = "activate sheet";
    result = astra_window_activate(&picker.window);
    if (result != ASTRA_OK) goto done;
    step = "present sheet";
    result = present();
    if (result != ASTRA_OK) goto done;
    step = "run file panel";
    result = run(path, capacity, selected);
    if (result == ASTRA_OK && *selected && picker.config_open) {
        const char *slash = strrchr(path, '/');
        size_t length = slash == NULL ? 0u :
            slash == path ? 1u : (size_t)(slash - path);
        if (length != 0u && length < sizeof(picker.initial_directory)) {
            memcpy(picker.initial_directory, path, length);
            picker.initial_directory[length] = '\0';
            (void)astra_config_set_string(&picker.config,
                LAST_DIRECTORY_KEY, 0u, picker.initial_directory);
        }
    }
done:
    if (result != ASTRA_OK) (void)astra_log(step);
    if (picker.window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_window_close(&picker.window);
        if (result == ASTRA_OK) result = closed;
    }
    if (picker.surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&picker.surface);
    astra_interface_file_panel_dispose(&picker.panel);
    astra_directory_snapshot_dispose(&picker.snapshots[0]);
    astra_directory_snapshot_dispose(&picker.snapshots[1]);
    astra_runtime_deallocate(picker.items[0]);
    astra_runtime_deallocate(picker.items[1]);
    astra_runtime_deallocate((void *)picker.icons);
    if (picker.config_open) astra_config_close(&picker.config);
    if (picker.files_open) astra_process_filesystem_close(&picker.files);
    return result;
}
