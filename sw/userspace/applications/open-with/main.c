#include <astra/application.h>
#include <astra/application_catalog.h>
#include <astra/config_library.h>
#include <astra/control.h>
#include <astra/file_type.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/scroll.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define WIDTH 560u
#define HEIGHT 340u
#define PREFERENCE_KEY "preferred"

enum { TITLE = 1u, DESCRIPTION, BODY, ACTIONS, SCROLL, APPLICATIONS,
       ALWAYS, CANCEL, OPEN, CONTROL_COUNT = OPEN };

_Static_assert(DESCRIPTION == TITLE + ASTRA_SETTINGS_PANE_DESCRIPTION &&
    BODY == TITLE + ASTRA_SETTINGS_PANE_BODY &&
    ACTIONS == TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Open With uses shared Settings pane composition");

ASTRA_PROGRAM("Open With", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraApplicationCatalog catalog = ASTRA_APPLICATION_CATALOG_INIT;
static AstraConfig config = ASTRA_CONFIG_INIT;
static AstraFileTypeInfo type = ASTRA_FILE_TYPE_INFO_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraControl controls[CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraCollectionItem *items;
static AstraCollectionModel list_model = ASTRA_COLLECTION_MODEL_INIT;
static uint32_t *indices, item_count, chosen;
static uint32_t always;
static const char *document;
static const char *notice;
static AstraHandle launcher;

#define CONTROL(id) (&controls[(id) - 1u])

static uint32_t preferred(char *id, uint32_t capacity, int *present)
{
    uint32_t count = 0u;
    *present = 0;
    if (type.type[0] == '\0') return ASTRA_CONFIG_OK;
    uint32_t status = astra_config_count(&config, PREFERENCE_KEY, &count);
    if (status != ASTRA_CONFIG_OK) return status;
    for (uint32_t at = 0u; at < count; ++at) {
        char value[ASTRA_FILE_TYPE_MAX + ASTRA_BUNDLE_ID_MAX + 2u];
        uint32_t length = 0u;
        status = astra_config_get_string(&config, PREFERENCE_KEY, at,
                                         value, sizeof(value), &length);
        if (status != ASTRA_CONFIG_OK) return status;
        size_t prefix = strlen(type.type);
        if (strncmp(value, type.type, prefix) == 0 && value[prefix] == ' ') {
            size_t length = strlen(value + prefix + 1u);
            if (length == 0u || length >= capacity) return ASTRA_CONFIG_MALFORMED;
            memcpy(id, value + prefix + 1u, length + 1u);
            *present = 1;
            return ASTRA_CONFIG_OK;
        }
    }
    return ASTRA_CONFIG_OK;
}

static uint32_t save_preferred(const char *id)
{
    uint32_t count = 0u;
    char value[ASTRA_FILE_TYPE_MAX + ASTRA_BUNDLE_ID_MAX + 2u];
    AstraString text;
    astra_string_init(&text, value, sizeof(value));
    if (!astra_string_append(&text, type.type) ||
        !astra_string_append_char(&text, ' ') ||
        !astra_string_append(&text, id)) return ASTRA_CONFIG_INVALID;
    uint32_t status = astra_config_reload(&config, NULL);
    if (status != ASTRA_CONFIG_OK) return status;
    status = astra_config_count(&config, PREFERENCE_KEY, &count);
    if (status != ASTRA_CONFIG_OK) return status;
    for (uint32_t at = 0u; at < count; ++at) {
        char prior[sizeof(value)];
        uint32_t length = 0u;
        status = astra_config_get_string(&config, PREFERENCE_KEY, at,
                                         prior, sizeof(prior), &length);
        if (status != ASTRA_CONFIG_OK) return status;
        size_t prefix = strlen(type.type);
        if (strncmp(prior, type.type, prefix) == 0 && prior[prefix] == ' ')
            return astra_config_set_string(&config, PREFERENCE_KEY, at, value);
    }
    return astra_config_append_string(&config, PREFERENCE_KEY, value);
}

static AstraResult launch(uint32_t index)
{
    if (index >= item_count) return ASTRA_ERROR_INVALID_ARGUMENT;
    const char *bundle = catalog.entries[indices[index]].bundle;
    const char *arguments[] = {document};
    uint32_t pid = 0u;
    return astra_application_launch_with_arguments(launcher, bundle,
        (uint16_t)strlen(bundle), ASTRA_LAUNCH_SOURCE_DESKTOP,
        arguments, 1u, &pid);
}

static uint32_t load_candidates(void)
{
    uint32_t status;
    if (catalog.count == 0u) return ASTRA_VFS_OK;
    indices = astra_runtime_allocate(catalog.count * sizeof(*indices));
    items = astra_runtime_allocate(catalog.count * sizeof(*items));
    if (indices == NULL || items == NULL) return ASTRA_VFS_ERR_LIMIT;
    if (type.type[0] != '\0') {
        status = astra_application_catalog_handlers(&files, &catalog,
            type.type, indices, catalog.count, &item_count);
        if (status != ASTRA_VFS_OK) return status;
    } else {
        for (uint32_t at = 0u; at < catalog.count; ++at)
            if (strcmp(catalog.entries[at].id, "org.astra.open-with") != 0)
                indices[item_count++] = at;
    }
    for (uint32_t at = 0u; at < item_count; ++at) {
        const char *name = catalog.entries[indices[at]].name;
        items[at] = (AstraCollectionItem){
            (uint64_t)indices[at] + 1u, name, (uint32_t)strlen(name),
            {0u, 0u}};
    }
    chosen = UINT32_MAX;
    return ASTRA_VFS_OK;
}

static AstraResult build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraToggleInfo toggle = ASTRA_TOGGLE_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    for (uint32_t at = 0u; at < CONTROL_COUNT; ++at)
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    pane.first_id = TITLE;
    pane.title = "Open With";
    pane.title_length = 9u;
    pane.description = notice;
    pane.description_length = (uint32_t)strlen(notice);
    if (astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    view.id = SCROLL;
    view.model = &scroll;
    view.preferred_width = WIDTH - 48u;
    view.preferred_height = 120u;
    if (astra_interface_scroll_view_init(CONTROL(SCROLL), &view) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = BODY;
    item.grow = 1u;
    item.minimum_height = view.preferred_height;
    if (astra_interface_control_set_flex(CONTROL(SCROLL), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    list.id = APPLICATIONS;
    if (astra_interface_collection_init(&list_model, items, item_count,
        chosen < item_count ? items[chosen].id : 0u) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    list.model = &list_model;
    if (astra_interface_list_init(CONTROL(APPLICATIONS), &list) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SCROLL;
    item.minimum_width = view.preferred_width;
    if (astra_interface_control_set_flex(CONTROL(APPLICATIONS), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    toggle.id = ALWAYS;
    toggle.text = "Always open files of this type with this app";
    toggle.text_length = (uint32_t)strlen(toggle.text);
    toggle.state = type.type[0] == '\0' ? ASTRA_CONTROL_COLLAPSED :
                   always ? ASTRA_CONTROL_SELECTED : 0u;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = BODY;
    if (astra_interface_checkbox_init(CONTROL(ALWAYS), &toggle) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(ALWAYS), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = CANCEL;
    button.text = "Cancel";
    button.text_length = 6u;
    item.parent_id = ACTIONS;
    if (astra_interface_button_init(CONTROL(CANCEL), &button) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(CANCEL), &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = OPEN;
    button.text = "Open";
    button.text_length = 4u;
    button.state = chosen == UINT32_MAX ? ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(OPEN), &button) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(OPEN), &item) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, CONTROL_COUNT,
            WIDTH, HEIGHT) != ASTRA_OK ||
        astra_interface_ui_layout(&ui, &layout) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
        surface.view.byte_size, WIDTH, HEIGHT)) return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    const AstraWindowFrame frame = {0u, 0u, WIDTH, HEIGHT};
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
            &selected, NULL) != ASTRA_SYSCALL_OK) return ASTRA_ERROR_IO;
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (result != ASTRA_OK) return result;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) return ASTRA_OK;
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(gui, &event) != ASTRA_OK) return ASTRA_ERROR_IO;
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (action.type == ASTRA_UI_ACTION_ACTIVATE && action.control_id == CANCEL)
            return ASTRA_OK;
        if (action.control_id == APPLICATIONS &&
            (action.type == ASTRA_UI_ACTION_VALUE_CHANGED ||
             action.type == ASTRA_UI_ACTION_ACTIVATE) &&
            action.value >= 0 && (uint64_t)action.value < item_count)
            chosen = (uint32_t)action.value;
        if (action.control_id == ALWAYS &&
            action.type == ASTRA_UI_ACTION_VALUE_CHANGED)
            always = action.value != 0;
        if ((action.control_id == OPEN || action.control_id == APPLICATIONS) &&
            action.type == ASTRA_UI_ACTION_ACTIVATE && chosen < item_count) {
            result = launch(chosen);
            if (result == ASTRA_OK) {
                if (always && type.type[0] != '\0' &&
                    save_preferred(catalog.entries[indices[chosen]].id) != ASTRA_CONFIG_OK) {
                    AstraAlertInfo alert = ASTRA_ALERT_INFO_INIT;
                    alert.kind = ASTRA_ALERT_ERROR;
                    alert.title = "Preference Not Saved";
                    alert.title_length = 20u;
                    alert.message = "File opened, but its default application could not be saved.";
                    alert.message_length = (uint16_t)strlen(alert.message);
                    alert.button = "Close";
                    alert.button_length = 5u;
                    (void)astra_interface_show_alert(gui, &alert);
                }
                return ASTRA_OK;
            }
            notice = "Could not open file with that application. Choose another.";
            if (build_controls() != ASTRA_OK || present() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            continue;
        }
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED) {
            if (build_controls() != ASTRA_OK || present() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            continue;
        }
        AstraControlFrame damage;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK &&
            present() != ASTRA_OK) return ASTRA_ERROR_IO;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    uint32_t status = ASTRA_STATUS_OK;
    uint32_t automatic = UINT32_MAX;
    uint32_t ready_reported = 0u;
    uint32_t force_choose = 0u;
    const char *failure = NULL;
    if (!astra_startup_validate(startup) ||
        (startup->argc != 2u && startup->argc != 3u))
        return ASTRA_STATUS_INVALID;
    if (startup->argc == 3u) {
        const char *option = astra_startup_argument(startup, 1u);
        if (option == NULL || strcmp(option, "--choose") != 0)
            return ASTRA_STATUS_INVALID;
        force_choose = 1u;
    }
    document = astra_startup_argument(startup, force_choose != 0u ? 2u : 1u);
    const AstraStartupCapability *gui = astra_startup_capability(startup,
        ASTRA_CAPABILITY_GUI);
    const AstraStartupCapability *app = astra_startup_capability(startup,
        ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    const AstraStartupCapability *ready = astra_startup_capability(startup,
        ASTRA_CAPABILITY_SERVICE_READY);
    if (document == NULL || document[0] != '/' ||
        strlen(document) >= ASTRA_VFS_PATH_MAX || gui == NULL || app == NULL ||
        ready == NULL)
        return ASTRA_STATUS_INVALID;
    launcher = app->handle;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    scroll_info.line_height = 24u;
    if (astra_scroll_init(&scroll, &scroll_info) != ASTRA_OK)
        failure = "scroll setup";
    else if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        failure = "font setup";
    else if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        failure = "filesystem access";
    else if (astra_config_open(startup, 1u, ASTRA_CONFIG_OPEN_READ |
            ASTRA_CONFIG_OPEN_WRITE, &config, NULL) != ASTRA_CONFIG_OK)
        failure = "settings access";
    else if (astra_application_catalog_load(&files, "/apps", &catalog) != ASTRA_VFS_OK)
        failure = "application discovery";
    else if (astra_file_type_resolve(&files, &catalog, document, &type) != ASTRA_VFS_OK)
        failure = "file identification";
    else if (load_candidates() != ASTRA_VFS_OK)
        failure = "handler discovery";
    if (failure != NULL) status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK && !force_choose && type.type[0] != '\0') {
        char id[ASTRA_BUNDLE_ID_MAX];
        int has_preference = 0;
        if (preferred(id, sizeof(id), &has_preference) != ASTRA_CONFIG_OK) {
            failure = "preferred application";
            status = ASTRA_STATUS_IO;
        }
        else if (has_preference) {
            for (uint32_t at = 0u; at < item_count; ++at)
                if (strcmp(catalog.entries[indices[at]].id, id) == 0) {
                    automatic = at;
                    break;
                }
        } else if (item_count == 1u) {
            automatic = 0u;
        }
    }
    if (status == ASTRA_STATUS_OK && automatic != UINT32_MAX) {
        (void)astra_service_ready(ready->handle, ASTRA_STATUS_OK, NULL, 0u);
        (void)astra_close(ready->handle);
        ready_reported = 1u;
        if (launch(automatic) == ASTRA_OK) goto done;
        notice = "Default application could not open this file. Choose another.";
        automatic = UINT32_MAX;
    }
    if (status == ASTRA_STATUS_OK && automatic == UINT32_MAX) {
        if (notice == NULL) notice = item_count == 0u ?
            "No installed application handles this file type." :
            type.type[0] == '\0' ?
            "File type is unknown. Choose an application for this file." :
            "Choose an application for this file type.";
        if (build_controls() != ASTRA_OK) {
            failure = "control layout";
            status = ASTRA_STATUS_IO;
        } else if (astra_shared_draw_list_create(&surface, WIDTH, HEIGHT) !=
                ASTRA_SYSCALL_OK) {
            failure = "surface creation";
            status = ASTRA_STATUS_IO;
        }
    }
    if (status == ASTRA_STATUS_OK && automatic == UINT32_MAX) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 440u; info.y = 220u;
        info.width = WIDTH; info.height = HEIGHT;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = "Open With";
        info.title_length = 9u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_KEY;
        if (astra_window_create(gui->handle, surface.area, &info, &window) !=
                ASTRA_OK || astra_window_set_application_name(&window,
                "Open With", 9u) != ASTRA_OK ||
            astra_window_activate(&window) != ASTRA_OK) {
            failure = "window creation";
            status = ASTRA_STATUS_IO;
        } else if (present() != ASTRA_OK) {
            failure = "window rendering";
            status = ASTRA_STATUS_IO;
        }
    }
    if (!ready_reported) {
        (void)astra_service_ready(ready->handle, status, NULL, 0u);
        (void)astra_close(ready->handle);
    }
    if (status == ASTRA_STATUS_OK && run(gui->handle) != ASTRA_OK) {
        failure = "window events";
        status = ASTRA_STATUS_IO;
    }
done:
    if (failure != NULL) {
        AstraAlertInfo alert = ASTRA_ALERT_INFO_INIT;
        alert.kind = ASTRA_ALERT_ERROR;
        alert.title = "Open With Error";
        alert.title_length = 15u;
        alert.message = failure;
        alert.message_length = (uint16_t)strlen(failure);
        alert.button = "Close";
        alert.button_length = 5u;
        (void)astra_interface_show_alert(gui->handle, &alert);
    }
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_window_close(&window);
        if (closed != ASTRA_OK && status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_IO;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_runtime_deallocate(items);
    astra_runtime_deallocate(indices);
    astra_application_catalog_destroy(&catalog);
    astra_config_close(&config);
    astra_process_filesystem_close(&files);
    return (int)status;
}
