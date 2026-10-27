#include <astra/control.h>
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

#define INSTALLER_WIDTH 560u
#define INSTALLER_HEIGHT 260u

enum {
    TITLE = 1u, DESCRIPTION, BODY, ACTIONS, PACKAGE_PATH,
    CANCEL, INSTALL, CONTROL_COUNT = INSTALL
};

_Static_assert(DESCRIPTION == TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    BODY == TITLE + ASTRA_SETTINGS_PANE_BODY &&
    ACTIONS == TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Installer uses the shared Settings pane composition");

ASTRA_PROGRAM("Installer", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraControl controls[CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static const char *archive;
static const char *notice = "Review package before installing into Applications.";
static uint32_t installed;

#define CONTROL(id) (&controls[(id) - 1u])

static AstraResult build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
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
    path.id = PACKAGE_PATH;
    path.text = archive;
    path.text_length = (uint32_t)strlen(archive);
    if (astra_interface_label_init(CONTROL(PACKAGE_PATH), &path) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = BODY;
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
            INSTALLER_WIDTH, INSTALLER_HEIGHT) != ASTRA_OK ||
        astra_interface_ui_layout(&ui, &layout) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
        surface.view.byte_size, INSTALLER_WIDTH, INSTALLER_HEIGHT))
        return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    const AstraWindowFrame frame = {0u, 0u, INSTALLER_WIDTH,
        INSTALLER_HEIGHT};
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
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return ASTRA_ERROR_IO;
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
    if (!astra_startup_validate(startup) || startup->argc != 2u)
        return ASTRA_STATUS_INVALID;
    archive = astra_startup_argument(startup, 1u);
    if (archive == NULL || archive[0] != '/' ||
        !astra_string_has_suffix(archive, ".apkg") ||
        strlen(archive) >= ASTRA_VFS_PATH_MAX)
        return ASTRA_STATUS_INVALID;
    const AstraStartupCapability *gui = astra_startup_capability(startup,
        ASTRA_CAPABILITY_GUI);
    const AstraStartupCapability *ready = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL) return ASTRA_STATUS_BAD_HANDLE;
    uint32_t status = ASTRA_STATUS_OK;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK ||
        astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK ||
        build_controls() != ASTRA_OK ||
        astra_shared_draw_list_create(&surface, INSTALLER_WIDTH,
            INSTALLER_HEIGHT) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 440u; info.y = 260u;
        info.width = INSTALLER_WIDTH; info.height = INSTALLER_HEIGHT;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = "Installer";
        info.title_length = 9u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_KEY;
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
    astra_process_filesystem_close(&files);
    return (int)status;
}
