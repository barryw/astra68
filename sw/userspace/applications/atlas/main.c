#include <astra/area.h>
#include <astra/fonts.h>
#include <astra/graphics_library.h>
#include <astra/input.h>
#include <astra/interface_library.h>
#include <astra/program.h>
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

#include "directory.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CELL_WIDTH 104u
#define CELL_HEIGHT 88u
#define GRID_PADDING 16u
#define GRID_GAP_X 8u
#define GRID_GAP_Y 14u
#define ICON_SIZE 40u
#define ICON_SOURCE_SIZE 64u

ASTRA_PROGRAM("Atlas", 0, 1, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static AtlasDirectory directory = ATLAS_DIRECTORY_INIT;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraDisplay display = ASTRA_DISPLAY_INIT;
static AstraSurface content = ASTRA_SURFACE_INIT;
static AstraSurface folder_icon = ASTRA_SURFACE_INIT;
static AstraSurface file_icon = ASTRA_SURFACE_INIT;
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
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

static int reference_number(const char *text, uint64_t *out)
{
    uint64_t value = 0u;
    if (text == NULL || *text == '\0') return 0;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9' || value > (UINT64_MAX - (uint32_t)(*text - '0')) / 10u)
            return 0;
        value = value * 10u + (uint32_t)(*text - '0');
    }
    *out = value;
    return value != 0u;
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
    (void)atlas_directory_load(&directory,
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
    outer.padding_top = outer.padding_bottom = 12u;
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
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    AstraRectI32 whole = {0, 0, width, height};
    AstraResult result;

    background.foreground = theme.client;
    blend.blend = ASTRA_BLEND_ALPHA;
    result = astra_draw_list_create(&content, &whole, &list);
    if (result == ASTRA_OK)
        result = astra_draw_rectangle(&list, &whole, 1, &background);
    if (result == ASTRA_OK && (visible_data == 0u || directory.count == 0u ||
                               directory.status != ASTRA_VFS_OK)) {
        const char *text = message();
        result = astra_draw_ui_text(&list, (AstraPointI32){28, 32}, text,
            (uint32_t)strlen(text), theme.title_font_height, 0u,
            !volume_lost && (visible_data == 0u || directory.status == ASTRA_VFS_OK) ? theme.client_text_secondary : theme.fault);
    }
    for (uint32_t at = 0u; result == ASTRA_OK && visible_data != 0u &&
                           directory.status == ASTRA_VFS_OK && at < directory.count; ++at) {
        AstraControlFrame frame, clip;
        const AstraDirectoryEntry *entry = &directory.entries[at];
        uint32_t length;

        result = astra_interface_ui_geometry(&ui, at + 2u, &frame, &clip);
        if (result != ASTRA_OK)
            break;
        if (clip.width == 0u || clip.height == 0u ||
            (int64_t)frame.y + frame.height <= clip.y ||
            frame.y >= (int64_t)clip.y + clip.height)
            continue;
        result = astra_draw_list_set_clip(&list,
            &(AstraRectI32){clip.x, clip.y, clip.width, clip.height});
        if (result == ASTRA_OK)
            result = astra_draw_blit(&list,
                entry->kind == ASTRA_VFS_KIND_DIRECTORY ? &folder_icon : &file_icon,
                &(AstraRectI32){0, 0, ICON_SOURCE_SIZE, ICON_SOURCE_SIZE},
                &(AstraRectI32){frame.x + (int32_t)(CELL_WIDTH - ICON_SIZE) / 2,
                               frame.y + 8, ICON_SIZE, ICON_SIZE}, &blend);
        length = astra_surface_ui_text_fit(entry->name,
            (uint32_t)strlen(entry->name), theme.title_font_height, CELL_WIDTH - 8u);
        if (result == ASTRA_OK && length != 0u) {
            uint32_t text_width = astra_surface_ui_text_width(entry->name, length,
                                                             theme.title_font_height);
            result = astra_draw_ui_text(&list,
                (AstraPointI32){frame.x + (int32_t)(CELL_WIDTH - text_width) / 2,
                               frame.y + 56}, entry->name, length,
                theme.title_font_height, 0u, theme.client_text);
        }
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

static AstraResult run(AstraHandle gui)
{
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
                atlas_directory_dispose(&directory);
                directory.status = ASTRA_VFS_ERR_LOST;
                visible_data = 1u;
            }
            log_root();
            result = build_view();
            if (result != ASTRA_OK)
                return result;
            repaint = 1u;
        }
        result = astra_window_event_try(&window, &event);
        while (result == ASTRA_OK) {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
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
            if (astra_interface_ui_handle_event(&ui, &event, &action) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE && layout() != ASTRA_OK)
                return ASTRA_ERROR_IO;
            result = astra_window_event_try(&window, &event);
        }
        if (result != ASTRA_ERROR_WOULD_BLOCK)
            return result;
        if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK)
            repaint = 1u;
        if (repaint != 0u && paint() != ASTRA_OK)
            return ASTRA_ERROR_IO;
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui, *ready;
    AstraArea placeholder = ASTRA_AREA_INIT;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraScrollModelInfo model = ASTRA_SCROLL_MODEL_INFO_INIT;
    uint32_t status;
    AstraResult result = ASTRA_OK;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || ready == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return ASTRA_STATUS_IO;
    const char *first_argument = startup->argc > 1u ? astra_startup_argument(startup, 1u) : NULL;
    int exact_volume = startup->argc == 5u && first_argument != NULL && strcmp(first_argument, "--volume") == 0;
    root = exact_volume ? "/dh0" : startup->argc > 1u ? astra_startup_argument(startup, 1u) : "/dh0";
    if (root == NULL || root[0] != '/' || (!exact_volume && startup->argc > 2u) ||
        strlen(root) >= ASTRA_VFS_PATH_MAX)
        return ASTRA_STATUS_INVALID;
    if (exact_volume && (!reference_number(astra_startup_argument(startup, 2u), &viewed_volume.epoch) ||
        !reference_number(astra_startup_argument(startup, 3u), &viewed_volume.id) ||
        !reference_number(astra_startup_argument(startup, 4u), &viewed_volume.generation)))
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
            ASTRA_WINDOW_SUBSCRIBE_KEY;
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
        atlas_directory_dispose(&directory);
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
