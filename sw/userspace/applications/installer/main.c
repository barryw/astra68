#include <astra/control.h>
#include <astra/filesystem_kit.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/package_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#include "../../common/directory_snapshot.h"

#define INSTALLER_REVIEW_WIDTH 560u
#define INSTALLER_REVIEW_HEIGHT 260u
#define INSTALLER_PICKER_WIDTH 720u
#define INSTALLER_PICKER_HEIGHT 560u

enum {
    TITLE = 1u, DESCRIPTION, BODY, ACTIONS,
    PACKAGE_ROW, PACKAGE_LABEL, PACKAGE_CONTENT, PACKAGE_VALUE, PACKAGE_HELP,
    PACKAGE_PATH,
    CANCEL, INSTALL, CONTROL_COUNT = INSTALL
};

_Static_assert(DESCRIPTION == TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    BODY == TITLE + ASTRA_SETTINGS_PANE_BODY &&
    ACTIONS == TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Installer uses the shared Settings pane composition");
_Static_assert((uint32_t)CONTROL_COUNT <=
    (uint32_t)ASTRA_FILE_PANEL_CONTROL_COUNT,
    "Review pane must fit the shared panel control storage");

ASTRA_PROGRAM("Installer", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraControl controls[ASTRA_FILE_PANEL_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraFilePanel picker = ASTRA_FILE_PANEL_INIT;
static AstraCollectionModel picker_collection = ASTRA_COLLECTION_MODEL_INIT;
static AstraScrollModel picker_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraDirectorySnapshot picker_snapshots[2] = {
    ASTRA_DIRECTORY_SNAPSHOT_INIT, ASTRA_DIRECTORY_SNAPSHOT_INIT
};
static AstraCollectionItem *picker_items[2];
static const AstraSurface **picker_icons;
static uint32_t picker_capacity;
static AstraCollectionItem picker_place_items[3];
static char chosen_archive[ASTRA_VFS_PATH_MAX];
static uint32_t picker_entry_bank;
static uint32_t picker_active;
static uint16_t content_width;
static uint16_t content_height;
static const char *archive;
static const char *notice = "Review package before installing into Applications.";
static uint32_t installed;

#define CONTROL(id) (&controls[(id) - 1u])

static const AstraFilePanelPlace picker_places[] = {
    {"Volumes", sizeof("Volumes") - 1u, "/"},
    {"Home", sizeof("Home") - 1u, "/home"},
    {"DH0", sizeof("DH0") - 1u, "/dh0"}
};

static const AstraChoiceItem picker_filters[] = {
    {"Astra packages", sizeof("Astra packages") - 1u, {0u, 0u}}
};

static uint32_t package_entry(void *context,
    const AstraDirectoryEntry *entry, uint32_t filter_index)
{
    const AstraFilePanel *panel = context;
    AstraFileTypeInfo type = ASTRA_FILE_TYPE_INFO_INIT;
    AstraString path;
    char storage[ASTRA_VFS_PATH_MAX];

    if (filter_index != 0u || panel == NULL || panel->directory == NULL ||
        entry == NULL) return 0u;
    astra_string_init(&path, storage, sizeof(storage));
    if (!astra_string_append(&path, panel->directory) ||
        (path.length != 1u && !astra_string_append_char(&path, '/')) ||
        !astra_string_append(&path, entry->name))
        return 0u;
    return astra_file_type_identify(&files.filesystem, storage, &type) ==
               ASTRA_VFS_OK &&
           strcmp(type.type, ASTRA_FILE_TYPE_PACKAGE) == 0;
}

static AstraResult allocate_picker_storage(uint32_t capacity,
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
        *first = NULL;
        *second = NULL;
        *icons = NULL;
        return ASTRA_ERROR_IO;
    }
    memset((void *)*icons, 0, (size_t)capacity * sizeof(**icons));
    return ASTRA_OK;
}

static AstraResult build_picker(void)
{
    AstraFilePanelInfo info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    const char *start = "/dh0";
    if (astra_directory_snapshot_load(&picker_snapshots[0],
            &files.filesystem, start, NULL) != ASTRA_VFS_OK) {
        start = "/";
        if (astra_directory_snapshot_load(&picker_snapshots[0],
                &files.filesystem, start, NULL) != ASTRA_VFS_OK)
            return ASTRA_ERROR_IO;
    }
    picker_capacity = picker_snapshots[0].capacity == 0u ?
        32u : picker_snapshots[0].capacity;
    if (allocate_picker_storage(picker_capacity, &picker_items[0],
            &picker_items[1], &picker_icons) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    scroll_info.line_width = 8u;
    scroll_info.line_height = 16u;
    if (astra_scroll_init(&picker_scroll, &scroll_info) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    info.first_id = 1u;
    info.mode = ASTRA_FILE_PANEL_OPEN;
    info.directory = start;
    info.entries = picker_snapshots[0].entries;
    info.entry_count = picker_snapshots[0].count;
    info.item_storage = picker_items[0];
    info.item_capacity = picker_capacity;
    info.spare_item_storage = picker_items[1];
    info.spare_item_capacity = picker_capacity;
    info.grid_icons = picker_icons;
    info.grid_icon_capacity = picker_capacity;
    info.places = picker_places;
    info.place_count = sizeof(picker_places) / sizeof(picker_places[0]);
    info.place_items = picker_place_items;
    info.place_capacity = sizeof(picker_place_items) /
                          sizeof(picker_place_items[0]);
    info.type_filters = picker_filters;
    info.type_filter_count = 1u;
    info.type_accept = package_entry;
    info.type_context = &picker;
    info.collection = &picker_collection;
    info.scroll = &picker_scroll;
    if (astra_interface_file_panel_init(&picker, controls, &info) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, ASTRA_FILE_PANEL_CONTROL_COUNT,
            content_width, content_height) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = layout.padding_right = 16u;
    layout.padding_top = layout.padding_bottom = 16u;
    layout.main_gap = 8u;
    if (astra_interface_ui_layout(&ui, &layout) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    picker_entry_bank = 0u;
    return ASTRA_OK;
}

static AstraResult navigate_picker(void)
{
    uint32_t next_bank = 1u - picker_entry_bank;
    uint32_t status = astra_directory_snapshot_load(
        &picker_snapshots[next_bank], &files.filesystem, picker.target, NULL);
    AstraResult result;

    if (status != ASTRA_VFS_OK)
        return astra_interface_control_set_text(&ui,
            &controls[ASTRA_FILE_PANEL_TITLE],
            status == ASTRA_VFS_ERR_LIMIT ? "Not enough memory for folder" :
            "Cannot open folder",
            status == ASTRA_VFS_ERR_LIMIT ?
                sizeof("Not enough memory for folder") - 1u :
                sizeof("Cannot open folder") - 1u);
    if (picker_snapshots[next_bank].count > picker_capacity) {
        AstraCollectionItem *first = NULL;
        AstraCollectionItem *second = NULL;
        const AstraSurface **icons = NULL;
        const AstraSurface **old_icons = picker_icons;
        uint32_t capacity = picker_snapshots[next_bank].capacity;

        result = allocate_picker_storage(capacity, &first, &second, &icons);
        if (result != ASTRA_OK)
            return astra_interface_control_set_text(&ui,
                &controls[ASTRA_FILE_PANEL_TITLE],
                "Not enough memory for folder",
                sizeof("Not enough memory for folder") - 1u);
        result = astra_interface_file_panel_rebind_storage(&picker, &ui,
            controls, first, capacity, second, capacity, icons, capacity);
        if (result != ASTRA_OK) {
            astra_runtime_deallocate(first);
            astra_runtime_deallocate(second);
            astra_runtime_deallocate((void *)icons);
            return result;
        }
        astra_runtime_deallocate(picker_items[0]);
        astra_runtime_deallocate(picker_items[1]);
        astra_runtime_deallocate((void *)old_icons);
        picker_items[0] = first;
        picker_items[1] = second;
        picker_icons = icons;
        picker_capacity = capacity;
        result = astra_interface_file_panel_set_directory(&picker, &ui,
            controls, picker.target, picker_snapshots[next_bank].entries,
            picker_snapshots[next_bank].count, second, capacity);
        if (result != ASTRA_OK) return result;
    } else {
        result = astra_interface_file_panel_set_directory(&picker, &ui,
            controls, picker.target, picker_snapshots[next_bank].entries,
            picker_snapshots[next_bank].count,
            picker.items == picker_items[0] ? picker_items[1] :
                picker_items[0], picker_capacity);
        if (result != ASTRA_OK) return result;
    }
    astra_directory_snapshot_dispose(&picker_snapshots[picker_entry_bank]);
    picker_entry_bank = next_bank;
    return astra_interface_control_set_text(&ui,
        &controls[ASTRA_FILE_PANEL_TITLE], "Open Astra package",
        sizeof("Open Astra package") - 1u);
}

static AstraResult build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraFormRowInfo row = ASTRA_FORM_ROW_INFO_INIT;
    AstraLabelInfo path = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    for (uint32_t at = 0u; at < CONTROL_COUNT; ++at)
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    pane.first_id = TITLE;
    pane.title = "Install application";
    pane.title_length = sizeof("Install application") - 1u;
    pane.description = notice;
    pane.description_length = (uint32_t)strlen(notice);
    if (astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row.first_id = PACKAGE_ROW;
    row.label = "Package";
    row.label_length = sizeof("Package") - 1u;
    row.message = "Review the application source before installing.";
    row.message_length = sizeof("Review the application source before installing.") - 1u;
    if (astra_interface_form_row_init(CONTROL(PACKAGE_ROW), &row) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = BODY;
    if (astra_interface_control_set_flex(CONTROL(PACKAGE_ROW), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    path.id = PACKAGE_PATH;
    path.text = archive;
    path.text_length = (uint32_t)strlen(archive);
    if (astra_interface_label_init(CONTROL(PACKAGE_PATH), &path) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = PACKAGE_VALUE;
    if (astra_interface_control_set_flex(CONTROL(PACKAGE_PATH), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = CANCEL;
    button.text = "Close";
    button.text_length = 5u;
    if (astra_interface_button_init(CONTROL(CANCEL), &button) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = INSTALL;
    button.text = "Install";
    button.text_length = 7u;
    button.state = installed ? ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(INSTALL), &button) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = ACTIONS;
    if (astra_interface_control_set_flex(CONTROL(CANCEL), &item) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(INSTALL), &item) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, CONTROL_COUNT,
            content_width, content_height) != ASTRA_OK ||
        astra_interface_ui_layout(&ui, &layout) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
        surface.view.byte_size, content_width, content_height))
        return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    const AstraWindowFrame frame = {0u, 0u, content_width,
        content_height};
    if (astra_window_present_region(&window, &frame) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    astra_interface_ui_damage_clear(&ui);
    return ASTRA_OK;
}

static AstraResult run(AstraHandle gui)
{
    for (;;) {
        uint32_t handle = astra_window_event_wait_handle(&window);
        uint32_t selected = 0u;
        if (astra_wait_multiple(&handle, 1u, ASTRA_DEADLINE_FOREVER,
            &selected, NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_ERROR_IO;
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (result != ASTRA_OK) return result;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) return ASTRA_OK;
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(gui, &event) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (event.type == ASTRA_WINDOW_EVENT_RESIZE) {
            content_width = event.data.resize.width;
            content_height = event.data.resize.height;
        }
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (picker_active) {
            uint32_t outcome = ASTRA_FILE_PANEL_NONE;
            if (astra_interface_file_panel_action(&picker, &ui, controls,
                    &action, &outcome) != ASTRA_OK)
                return ASTRA_ERROR_IO;
            if (outcome == ASTRA_FILE_PANEL_CANCELLED) return ASTRA_OK;
            if (outcome == ASTRA_FILE_PANEL_NAVIGATE) {
                if (navigate_picker() != ASTRA_OK) return ASTRA_ERROR_IO;
            } else if (outcome == ASTRA_FILE_PANEL_PICKED) {
                const AstraWindowFrame review = {440u, 260u,
                    INSTALLER_REVIEW_WIDTH, INSTALLER_REVIEW_HEIGHT};
                if (!astra_string_copy(chosen_archive,
                        sizeof(chosen_archive), picker.target) ||
                    !astra_string_has_suffix(chosen_archive, ".apkg"))
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                archive = chosen_archive;
                picker_active = 0u;
                content_width = INSTALLER_REVIEW_WIDTH;
                content_height = INSTALLER_REVIEW_HEIGHT;
                if (build_controls() != ASTRA_OK ||
                    astra_window_set_frame(&window, &review) != ASTRA_OK ||
                    present() != ASTRA_OK)
                    return ASTRA_ERROR_IO;
                continue;
            }
            AstraControlFrame picker_damage;
            if (astra_interface_ui_damage(&ui, &picker_damage) == ASTRA_OK &&
                present() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == CANCEL) return ASTRA_OK;
        if (!installed && action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == INSTALL) {
            result = astra_package_install_bundle(&files.filesystem,
                archive, "/apps");
            installed = result == ASTRA_OK;
            notice = result == ASTRA_OK ?
                "Installed. Open application from Applications." :
                result == ASTRA_ERROR_BUSY ?
                "Application already exists. Existing files were left untouched." :
                result == ASTRA_ERROR_INVALID_ARGUMENT ?
                "Package is damaged or unsupported. Nothing was installed." :
                result == ASTRA_ERROR_PERMISSION ?
                "Installation was refused by volume permissions." :
                "Installation failed. Nothing was published.";
            if (build_controls() != ASTRA_OK || present() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            continue;
        }
        AstraControlFrame damage;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK &&
            present() != ASTRA_OK)
            return ASTRA_ERROR_IO;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    if (!astra_startup_validate(startup) ||
        (startup->argc != 1u && startup->argc != 2u))
        return ASTRA_STATUS_INVALID;
    picker_active = startup->argc == 1u;
    if (!picker_active) {
        archive = astra_startup_argument(startup, 1u);
        if (archive == NULL || archive[0] != '/' ||
            !astra_string_has_suffix(archive, ".apkg") ||
            strlen(archive) >= ASTRA_VFS_PATH_MAX)
            return ASTRA_STATUS_INVALID;
    }
    content_width = picker_active ?
        INSTALLER_PICKER_WIDTH : INSTALLER_REVIEW_WIDTH;
    content_height = picker_active ?
        INSTALLER_PICKER_HEIGHT : INSTALLER_REVIEW_HEIGHT;
    const AstraStartupCapability *gui = astra_startup_capability(startup,
        ASTRA_CAPABILITY_GUI);
    const AstraStartupCapability *ready = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL) return ASTRA_STATUS_BAD_HANDLE;
    uint32_t status = ASTRA_STATUS_OK;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK ||
        astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK ||
        (picker_active ? build_picker() : build_controls()) != ASTRA_OK ||
        astra_shared_draw_list_create(&surface,
            INSTALLER_PICKER_WIDTH, INSTALLER_PICKER_HEIGHT) !=
                ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 440u; info.y = picker_active ? 180u : 260u;
        info.width = content_width; info.height = content_height;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = "Installer";
        info.title_length = 9u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
            ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT;
        if (astra_window_create(gui->handle, surface.area, &info, &window) !=
                ASTRA_OK ||
            astra_window_set_application_name(&window, "Installer", 9u) !=
                ASTRA_OK || astra_window_activate(&window) != ASTRA_OK)
            status = ASTRA_STATUS_IO;
    }
    if (ready != NULL) {
        (void)astra_service_ready(ready->handle, status, NULL, 0u);
        (void)astra_close(ready->handle);
    }
    if (status == ASTRA_STATUS_OK && present() != ASTRA_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK && run(gui->handle) != ASTRA_OK)
        status = ASTRA_STATUS_IO;
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_window_close(&window);
        if (closed != ASTRA_OK && status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_IO;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_interface_file_panel_dispose(&picker);
    astra_directory_snapshot_dispose(&picker_snapshots[0]);
    astra_directory_snapshot_dispose(&picker_snapshots[1]);
    astra_runtime_deallocate(picker_items[0]);
    astra_runtime_deallocate(picker_items[1]);
    astra_runtime_deallocate((void *)picker_icons);
    astra_process_filesystem_close(&files);
    return (int)status;
}
