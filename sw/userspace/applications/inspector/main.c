#include <astra/application.h>
#include <astra/application_catalog.h>
#include <astra/civil.h>
#include <astra/control.h>
#include <astra/file_type.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/vfs_path.h>
#include <astra/volume.h>
#include <astra/window.h>

#include <stdint.h>
#include <string.h>

#define INSPECTOR_WIDTH 600u
#define INSPECTOR_HEIGHT 500u
#define INSPECTOR_LINE_COUNT 8u
#define INSPECTOR_LINE_BYTES (ASTRA_VFS_PATH_MAX + 32u)

enum {
    INSPECTOR_TITLE = 1u,
    INSPECTOR_DESCRIPTION,
    INSPECTOR_BODY,
    INSPECTOR_ACTIONS,
    INSPECTOR_TABS,
    INSPECTOR_FIRST_LINE,
    INSPECTOR_OPEN_WITH = INSPECTOR_FIRST_LINE + INSPECTOR_LINE_COUNT,
    INSPECTOR_CLOSE,
    INSPECTOR_CONTROL_COUNT = INSPECTOR_CLOSE
};

enum {
    INSPECTOR_GENERAL,
    INSPECTOR_HANDLERS,
    INSPECTOR_BYTES,
    INSPECTOR_BLOCKS,
    INSPECTOR_VOLUME,
    INSPECTOR_PAGE_COUNT
};

_Static_assert(INSPECTOR_DESCRIPTION == INSPECTOR_TITLE +
    ASTRA_SETTINGS_PANE_DESCRIPTION &&
    INSPECTOR_BODY == INSPECTOR_TITLE + ASTRA_SETTINGS_PANE_BODY &&
    INSPECTOR_ACTIONS == INSPECTOR_TITLE + ASTRA_SETTINGS_PANE_ACTIONS,
    "Inspector uses shared Settings pane composition");

ASTRA_PROGRAM("System Inspector", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static const AstraChoiceItem pages[] = {
    {"General", 7u, {0u, 0u}},
    {"Open With", 9u, {0u, 0u}},
    {"Bytes", 5u, {0u, 0u}},
    {"Blocks", 6u, {0u, 0u}},
    {"Volume", 6u, {0u, 0u}}
};

static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraApplicationCatalog catalog = ASTRA_APPLICATION_CATALOG_INIT;
static AstraFileTypeInfo type = ASTRA_FILE_TYPE_INFO_INIT;
static AstraFileInfo metadata = ASTRA_FILE_INFO_INIT;
static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraControl controls[INSPECTOR_CONTROL_COUNT];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static char line_text[INSPECTOR_LINE_COUNT][INSPECTOR_LINE_BYTES];
static char window_title[ASTRA_WINDOW_TITLE_MAX + 1u];
static const char *target;
static uint32_t *handlers, handler_count, page;
static AstraHandle launcher;
static AstraHandle volume_grant;
static AstraVolumeReference exact_volume;
static AstraVolumeRecord volume_record, source_record;
static int volume_root;
static uint64_t volume_capacity_bytes, volume_available_bytes;
static int volume_capacity_valid;

#define CONTROL(id) (&controls[(id) - 1u])

static const char *kind_name(uint32_t kind)
{
    return kind == ASTRA_VFS_KIND_DIRECTORY ? "Folder" :
        kind == ASTRA_VFS_KIND_SYMLINK ? "Symbolic link" :
        kind == ASTRA_VFS_KIND_FILE ? "File" : "Other";
}

static AstraString line_start(uint32_t row, const char *label)
{
    AstraString line;
    astra_string_init(&line, line_text[row], sizeof(line_text[row]));
    (void)astra_string_append(&line, label);
    return line;
}

static void empty_lines(void)
{
    for (uint32_t at = 0u; at < INSPECTOR_LINE_COUNT; ++at)
        (void)astra_string_copy(line_text[at], sizeof(line_text[at]), " ");
}

static void page_general(void)
{
    AstraString line = line_start(0u, "Name: ");
    (void)astra_string_append(&line, astra_path_leaf(target));
    line = line_start(1u, "Path: ");
    (void)astra_string_append(&line, target);
    line = line_start(2u, "Kind: ");
    (void)astra_string_append(&line, volume_root ? "Volume" :
        kind_name(metadata.kind));
    line = line_start(3u, volume_root ? "Format: " : "File type: ");
    if (volume_root) {
        const AstraVolumeRecord *source = source_record.kind ==
            ASTRA_VOLUME_SOURCE ? &source_record : &volume_record;
        (void)astra_string_append(&line,
            astra_volume_format_name(source->format));
    } else (void)astra_string_append(&line,
        type.type[0] == '\0' ? "Unknown" : type.type);
    line = line_start(4u, volume_root ? "Capacity: " : "Size: ");
    if (volume_root && volume_capacity_valid) {
        (void)astra_string_append_u64(&line, volume_capacity_bytes);
        (void)astra_string_append(&line, " bytes");
    } else if (volume_root)
        (void)astra_string_append(&line, "Unknown");
    else if (metadata.kind == ASTRA_VFS_KIND_DIRECTORY)
        (void)astra_string_append(&line, "Folder");
    else {
        (void)astra_string_append_u64(&line, metadata.byte_size);
        (void)astra_string_append(&line, " bytes");
    }
    line = line_start(5u, "Modified: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_MTIME) != 0u &&
        metadata.mtime > 0 &&
        (uint64_t)metadata.mtime <= UINT64_MAX / UINT64_C(1000000000)) {
        char date[24];
        if (astra_civil_iso8601((uint64_t)metadata.mtime *
                UINT64_C(1000000000), date, sizeof(date)) != 0u)
            (void)astra_string_append(&line, date);
        else (void)astra_string_append(&line, "Unknown");
    } else (void)astra_string_append(&line, "Unknown");
    line = line_start(6u, "Owner: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_OWNER) != 0u) {
        (void)astra_string_append_u64(&line, metadata.uid);
        (void)astra_string_append_char(&line, ':');
        (void)astra_string_append_u64(&line, metadata.gid);
    } else (void)astra_string_append(&line, "Unknown");
    line = line_start(7u, "Mode: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_MODE) != 0u)
        (void)astra_string_append_hex(&line, metadata.mode, 4u);
    else (void)astra_string_append(&line, "Unknown");
}

static void page_handlers(void)
{
    AstraString line = line_start(0u, "File type: ");
    (void)astra_string_append(&line,
        type.type[0] == '\0' ? "Unknown" : type.type);
    line = line_start(1u, "Installed handlers: ");
    (void)astra_string_append_u64(&line, handler_count);
    if (metadata.kind != ASTRA_VFS_KIND_FILE) {
        (void)astra_string_copy(line_text[2u],
            sizeof(line_text[2u]), "This item has no application file type.");
        return;
    }
    if (handler_count == 0u) {
        (void)astra_string_copy(line_text[2u], sizeof(line_text[2u]),
            "No application declares this type. Open With can show all apps.");
        return;
    }
    uint32_t shown = handler_count > 6u ? 5u : handler_count;
    for (uint32_t at = 0u; at < shown; ++at) {
        line = line_start(at + 2u, "  ");
        (void)astra_string_append(&line,
            catalog.entries[handlers[at]].name);
    }
    if (handler_count > shown) {
        line = line_start(7u, "...and ");
        (void)astra_string_append_u64(&line, handler_count - shown);
        (void)astra_string_append(&line, " more");
    }
}

static int fixed_path(const char *path)
{
    return astra_path_beneath(path, "/dh0") ||
        astra_path_beneath(path, "/home") ||
        astra_path_beneath(path, "/apps");
}

static AstraResult bind_volume(const AstraStartupInfo *startup)
{
    if (astra_path_requires_walk(target))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const int system_root = strcmp(target, "/dh0") == 0 &&
        exact_volume.id == 0u;
    if (fixed_path(target) && !system_root && exact_volume.id == 0u)
        return ASTRA_OK;
    const AstraStartupCapability *metadata = astra_startup_capability(
        startup, ASTRA_CAPABILITY_VOLUMES);
    if (metadata == NULL)
        return system_root ? ASTRA_OK : ASTRA_ERROR_PERMISSION;
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraResult result = astra_volume_snapshot_open(metadata->handle,
        &snapshot);
    if (result != ASTRA_OK) return system_root ? ASTRA_OK : result;
    const AstraVolumeRecord *record = NULL;
    if (exact_volume.id != 0u)
        record = astra_volume_snapshot_find(&snapshot, &exact_volume);
    else {
        /* RAM is source-scoped and may be opened from a plain path. Public
         * removable volumes require the caller's exact reference. */
        for (uint32_t at = 0u; at < snapshot.count; ++at) {
            const AstraVolumeRecord *candidate = &snapshot.records[at];
            if (candidate->kind == ASTRA_VOLUME_MOUNT &&
                candidate->state == ASTRA_VOLUME_MOUNTED &&
                ((candidate->origin == ASTRA_VOLUME_ORIGIN_RAM &&
                  astra_path_beneath(target, candidate->name)) ||
                 (system_root && strcmp(candidate->name, "/dh0") == 0))) {
                record = candidate;
                exact_volume = (AstraVolumeReference){snapshot.epoch,
                    candidate->id, candidate->generation};
                break;
            }
        }
    }
    if (record != NULL && record->kind == ASTRA_VOLUME_MOUNT &&
        record->state == ASTRA_VOLUME_MOUNTED &&
        strcmp(target, record->name) == 0) {
        volume_record = *record;
        const AstraVolumeRecord *source = astra_volume_snapshot_ancestor(
            &snapshot, &exact_volume, ASTRA_VOLUME_SOURCE);
        if (source != NULL) source_record = *source;
        volume_root = 1;
    }
    if (system_root) result = ASTRA_OK;
    else if (record == NULL || record->kind != ASTRA_VOLUME_MOUNT ||
        record->state != ASTRA_VOLUME_MOUNTED ||
        !astra_path_beneath(target, record->name) ||
        (fixed_path(target) && record->origin != ASTRA_VOLUME_ORIGIN_RAM))
        result = ASTRA_ERROR_INVALID_HANDLE;
    else {
        const char *broker_name = record->origin == ASTRA_VOLUME_ORIGIN_RAM ?
            ASTRA_CAPABILITY_RAM_READ : ASTRA_CAPABILITY_VOLUME_READ;
        const AstraStartupCapability *broker = astra_startup_capability(
            startup, broker_name);
        result = broker == NULL ? ASTRA_ERROR_PERMISSION :
            astra_volume_root(broker->handle, &exact_volume,
                ASTRA_RIGHT_READ, &volume_grant);
    }
    astra_volume_snapshot_close(&snapshot);
    return result;
}

static void page_volume(void)
{
    const AstraVolumeRecord *source = source_record.kind ==
        ASTRA_VOLUME_SOURCE ? &source_record : &volume_record;
    AstraString line = line_start(0u, "Mount: ");
    (void)astra_string_append(&line, volume_record.name);
    line = line_start(1u, "Label: ");
    (void)astra_string_append(&line, source->label[0] != '\0' ?
        source->label : "None");
    line = line_start(2u, "Format: ");
    (void)astra_string_append(&line,
        astra_volume_format_name(source->format));
    line = line_start(3u, "Capacity: ");
    if (volume_capacity_valid) {
        (void)astra_string_append_u64(&line, volume_capacity_bytes);
        (void)astra_string_append(&line, " bytes");
    } else (void)astra_string_append(&line, "Unknown");
    line = line_start(4u, "Available: ");
    if (volume_capacity_valid) {
        (void)astra_string_append_u64(&line, volume_available_bytes);
        (void)astra_string_append(&line, " bytes");
    } else (void)astra_string_append(&line, "Unknown");
    line = line_start(5u, "Access: ");
    (void)astra_string_append(&line,
        (volume_record.flags & ASTRA_VOLUME_READ_ONLY) != 0u ?
        "Read-only" : "Read/write");
    line = line_start(6u, "Source: ");
    const char *origin = source->origin == ASTRA_VOLUME_ORIGIN_DEVICE ?
        "Device" : source->origin == ASTRA_VOLUME_ORIGIN_IMAGE ?
        "Disk image" : source->origin == ASTRA_VOLUME_ORIGIN_RAM ?
        "RAM" : source->origin == ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE ?
        "System image" : source->origin == ASTRA_VOLUME_ORIGIN_EXPORT ?
        "Export" : "Unknown";
    (void)astra_string_append(&line, origin);
    if (source->partition_number != 0u) {
        (void)astra_string_append(&line, " partition ");
        (void)astra_string_append_u64(&line, source->partition_number);
    }
    line = line_start(7u, "UUID: ");
    (void)astra_string_append(&line,
        source->uuid[0] != '\0' ? source->uuid : "None");
}

static void page_bytes(void)
{
    if (metadata.kind != ASTRA_VFS_KIND_FILE) {
        (void)astra_string_copy(line_text[0u], sizeof(line_text[0u]),
            "This item has no file byte stream.");
        return;
    }
    AstraFile file = ASTRA_FILE_INIT;
    uint8_t bytes[64];
    uint32_t moved = 0u;
    uint32_t status = astra_filesystem_open(&files.filesystem,
        target, ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_OK) {
        status = astra_filesystem_read_at(&file, 0u, bytes,
            sizeof(bytes), &moved);
        uint32_t close_status = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = close_status;
    }
    if (status != ASTRA_VFS_OK) {
        (void)astra_string_copy(line_text[0u], sizeof(line_text[0u]),
            "Bytes unavailable; source may have been removed.");
        return;
    }
    AstraString line = line_start(0u, "First ");
    (void)astra_string_append_u64(&line, moved);
    (void)astra_string_append(&line, " bytes:");
    for (uint32_t row = 0u; row < 4u; ++row) {
        if (row * 16u >= moved) break;
        line = line_start(row + 1u, "");
        (void)astra_string_append_hex(&line, row * 16u, 8u);
        (void)astra_string_append(&line, "  ");
        for (uint32_t at = row * 16u;
             at < moved && at < (row + 1u) * 16u; ++at) {
            (void)astra_string_append_hex(&line, bytes[at], 2u);
            (void)astra_string_append_char(&line, ' ');
        }
    }
    (void)astra_string_copy(line_text[6u], sizeof(line_text[6u]),
        "Read-only sample; no file handle remains open.");
}

static void page_blocks(void)
{
    AstraString line = line_start(0u, "Volume ID: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_VOLUME) != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.volume);
    else (void)astra_string_append(&line, "Unknown");
    line = line_start(1u, "Inode: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_INODE) != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.inode);
    else (void)astra_string_append(&line, "Unknown");
    line = line_start(2u, "Generation: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_GENERATION) != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.generation);
    else (void)astra_string_append(&line, "Unknown");
    line = line_start(3u, "Allocated 512-byte blocks: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_BLOCKS) != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.blocks);
    else (void)astra_string_append(&line, "Unknown");
    line = line_start(4u, "Preferred I/O block size: ");
    if (metadata.meta.block_size != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.block_size);
    else (void)astra_string_append(&line, "Unknown");
    line = line_start(5u, "Links: ");
    if ((metadata.meta.valid & ASTRA_VFS_META_NLINK) != 0u)
        (void)astra_string_append_u64(&line, metadata.meta.nlink);
    else (void)astra_string_append(&line, "Unknown");
}

static AstraResult build_controls(void)
{
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraChoiceInfo tabs = ASTRA_CHOICE_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    for (uint32_t at = 0u; at < INSPECTOR_CONTROL_COUNT; ++at)
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    empty_lines();
    if (page == INSPECTOR_GENERAL) page_general();
    else if (page == INSPECTOR_HANDLERS) page_handlers();
    else if (page == INSPECTOR_BYTES) page_bytes();
    else if (page == INSPECTOR_BLOCKS) page_blocks();
    else page_volume();
    pane.first_id = INSPECTOR_TITLE;
    pane.title = astra_path_leaf(target);
    pane.title_length = (uint32_t)strlen(pane.title);
    pane.description = target;
    pane.description_length = (uint32_t)strlen(target);
    if (astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tabs.id = INSPECTOR_TABS;
    tabs.items = pages;
    tabs.item_count = volume_root ? INSPECTOR_PAGE_COUNT :
        INSPECTOR_PAGE_COUNT - 1u;
    tabs.selected = page;
    if (astra_interface_tab_init(CONTROL(INSPECTOR_TABS), &tabs) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    item.parent_id = INSPECTOR_BODY;
    if (astra_interface_control_set_flex(CONTROL(INSPECTOR_TABS),
            &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < INSPECTOR_LINE_COUNT; ++at) {
        label.id = INSPECTOR_FIRST_LINE + at;
        label.text = line_text[at];
        label.text_length = (uint32_t)strlen(line_text[at]);
        label.text_role = at == 0u ? ASTRA_TEXT_CLIENT_PRIMARY :
            ASTRA_TEXT_CLIENT_SECONDARY;
        if (astra_interface_label_init(CONTROL(label.id), &label) != ASTRA_OK ||
            astra_interface_control_set_flex(CONTROL(label.id), &item) !=
                ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    item.parent_id = INSPECTOR_ACTIONS;
    button.id = INSPECTOR_OPEN_WITH;
    button.text = "Open With...";
    button.text_length = 12u;
    button.state = metadata.kind != ASTRA_VFS_KIND_FILE ?
        ASTRA_CONTROL_DISABLED : 0u;
    if (astra_interface_button_init(CONTROL(INSPECTOR_OPEN_WITH),
            &button) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(INSPECTOR_OPEN_WITH),
            &item) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    button.id = INSPECTOR_CLOSE;
    button.text = "Close";
    button.text_length = 5u;
    button.state = 0u;
    if (astra_interface_button_init(CONTROL(INSPECTOR_CLOSE),
            &button) != ASTRA_OK ||
        astra_interface_control_set_flex(CONTROL(INSPECTOR_CLOSE),
            &item) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, INSPECTOR_CONTROL_COUNT,
            INSPECTOR_WIDTH, INSPECTOR_HEIGHT) != ASTRA_OK ||
        astra_interface_ui_layout(&ui, &layout) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
            surface.view.byte_size, INSPECTOR_WIDTH, INSPECTOR_HEIGHT))
        return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    const AstraWindowFrame frame = {0u, 0u,
        INSPECTOR_WIDTH, INSPECTOR_HEIGHT};
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
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
            return ASTRA_OK;
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
            astra_fonts_use_system(gui, &event) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == INSPECTOR_CLOSE)
            return ASTRA_OK;
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == INSPECTOR_OPEN_WITH &&
            metadata.kind == ASTRA_VFS_KIND_FILE) {
            const char *arguments[] = {"--choose", target};
            uint32_t pid = 0u;
            result = astra_application_launch_with_arguments(launcher,
                "/apps/OpenWith.app", sizeof("/apps/OpenWith.app") - 1u,
                ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 2u, &pid);
            if (result != ASTRA_OK) return result;
        }
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == INSPECTOR_TABS &&
            action.value >= 0 &&
            (uint32_t)action.value < (volume_root ? INSPECTOR_PAGE_COUNT :
                INSPECTOR_PAGE_COUNT - 1u)) {
            page = (uint32_t)action.value;
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
    const AstraStartupCapability *gui, *app, *ready;
    uint32_t status = ASTRA_STATUS_OK;
    if (!astra_startup_validate(startup) ||
        (startup->argc != 2u && startup->argc != 6u))
        return ASTRA_STATUS_INVALID;
    if (startup->argc == 6u) {
        const char *option = astra_startup_argument(startup, 1u);
        if (option == NULL || strcmp(option, "--volume") != 0 ||
            !astra_volume_reference_parse(
                astra_startup_argument(startup, 2u),
                astra_startup_argument(startup, 3u),
                astra_startup_argument(startup, 4u), &exact_volume))
            return ASTRA_STATUS_INVALID;
    }
    target = astra_startup_argument(startup,
        startup->argc == 6u ? 5u : 1u);
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    app = astra_startup_capability(startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (target == NULL || target[0] != '/' ||
        strlen(target) >= ASTRA_VFS_PATH_MAX || gui == NULL || app == NULL ||
        ready == NULL)
        return ASTRA_STATUS_INVALID;
    launcher = app->handle;
    if (bind_volume(startup) != ASTRA_OK ||
        astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK ||
        astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK ||
        astra_filesystem_lstat(&files.filesystem, target,
            &metadata) != ASTRA_VFS_OK ||
        astra_application_catalog_load(&files, "/apps", &catalog) !=
            ASTRA_VFS_OK ||
        (metadata.kind == ASTRA_VFS_KIND_FILE &&
         astra_file_type_resolve(&files, &catalog, target, &type) !=
            ASTRA_VFS_OK))
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK && volume_root) {
        AstraFilesystemInfo capacity = {
            .size = sizeof(AstraFilesystemInfo)};
        if (astra_filesystem_capacity_path(&files.filesystem, target,
                &capacity) == ASTRA_VFS_OK &&
            astra_filesystem_capacity_bytes(&capacity,
                &volume_capacity_bytes, &volume_available_bytes)) {
            volume_capacity_valid = 1;
        }
    }
    if (status == ASTRA_STATUS_OK && metadata.kind == ASTRA_VFS_KIND_FILE &&
        type.type[0] != '\0' && catalog.count != 0u) {
        handlers = astra_runtime_allocate(catalog.count * sizeof(*handlers));
        if (handlers == NULL || astra_application_catalog_handlers(&files,
                &catalog, type.type, handlers, catalog.count,
                &handler_count) != ASTRA_VFS_OK)
            status = ASTRA_STATUS_IO;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraString title;
        astra_string_init(&title, window_title, sizeof(window_title));
        if (!astra_string_append(&title, "Inspector — ") ||
            !astra_string_append(&title, astra_path_leaf(target)))
            (void)astra_string_copy(window_title, sizeof(window_title),
                "System Inspector");
        if (build_controls() != ASTRA_OK ||
            astra_shared_draw_list_create(&surface, INSPECTOR_WIDTH,
                INSPECTOR_HEIGHT) != ASTRA_SYSCALL_OK)
            status = ASTRA_STATUS_IO;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        info.x = 480u;
        info.y = 200u;
        info.width = INSPECTOR_WIDTH;
        info.height = INSPECTOR_HEIGHT;
        info.flags = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_UTILITY;
        info.title = window_title;
        info.title_length = (uint16_t)strlen(window_title);
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
            ASTRA_WINDOW_SUBSCRIBE_KEY;
        if (astra_window_create(gui->handle, surface.area, &info,
                &window) != ASTRA_OK ||
            astra_window_set_application_name(&window,
                "System Inspector", sizeof("System Inspector") - 1u) !=
                ASTRA_OK || present() != ASTRA_OK)
            status = ASTRA_STATUS_IO;
    }
    (void)astra_service_ready(ready->handle, status, NULL, 0u);
    (void)astra_close(ready->handle);
    if (status == ASTRA_STATUS_OK && run(gui->handle) != ASTRA_OK)
        status = ASTRA_STATUS_IO;
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_window_close(&window);
        if (closed != ASTRA_OK && status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_IO;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_runtime_deallocate(handlers);
    astra_application_catalog_destroy(&catalog);
    astra_process_filesystem_close(&files);
    if (volume_grant != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&volume_grant);
        if (closed != ASTRA_OK && status == ASTRA_STATUS_OK)
            status = ASTRA_STATUS_IO;
    }
    return (int)status;
}
