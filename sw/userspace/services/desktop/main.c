#include <astra/application.h>
#include <astra/fonts.h>
#include <astra/area.h>
#include <astra/bundle.h>
#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/draw_list.h>
#include <astra/event_emit.h>
#include <astra/gui.h>
#include <astra/graphics.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/input.h>
#include <astra/icon_tile.h>
#include <astra/inline_edit.h>
#include <astra/pcm.h>
#include <astra/pcm_format.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/result.h>
#include <astra/service.h>
#include <astra/service_manager.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/text_style.h>
#include <astra/window.h>
#include <astra/volume.h>
#include <astra/ascii.h>

#include "desktop_layout.h"
#include "desktop_usage.h"
#include "../../common/directory_snapshot.h"
#include "../../common/folder_ops.h"

#define DESKTOP_WIDTH ASTRA_DISPLAY_WIDTH
#define DESKTOP_TOP 34u
#define DESKTOP_BOTTOM (ASTRA_DISPLAY_HEIGHT - 42u)
#define DESKTOP_HEIGHT (DESKTOP_BOTTOM - DESKTOP_TOP)
#define DESKTOP_SCROLLBAR_WIDTH 12u
#define DESKTOP_GRID_WIDTH (DESKTOP_WIDTH - DESKTOP_SCROLLBAR_WIDTH)
#define DESKTOP_DIRECTORY "/home/Desktop"
#define DESKTOP_TRASH_DIRECTORY "/home/.Trash"

enum {
    DESKTOP_FAIL_FILESYSTEM = ASTRA_STATUS_PROGRAM_FIRST,
    DESKTOP_FAIL_INTERFACE,
    DESKTOP_FAIL_GRAPHICS,
    DESKTOP_FAIL_MANIFEST_READ,
    DESKTOP_FAIL_MANIFEST_PARSE,
    DESKTOP_FAIL_ICON_PATH,
    DESKTOP_FAIL_ICON,
    DESKTOP_FAIL_SURFACE,
    DESKTOP_FAIL_WINDOW
};

typedef struct DesktopApps {
    AstraDirectorySnapshot folders;
    AstraSurface folder_icon;
    AstraSurface applications_icon;
    uint32_t shown; /* Applications drawer plus Desktop folders. */
    AstraSurface volume_icon;
    uint32_t volume_visible;
    uint32_t *volume_indices; /* icon order into complete volume graph */
    AstraVolumeSnapshot volumes;
} DesktopApps;

static AstraProcessFilesystem process_filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraInlineEdit name_edit = ASTRA_INLINE_EDIT_INIT;
static uint32_t name_edit_cell = UINT32_MAX;
static char name_edit_path[ASTRA_VFS_PATH_MAX];
static _Alignas(4) uint32_t name_edit_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];

static const AstraVolumeRecord *visible_volume(const DesktopApps *apps, uint32_t cell)
{
    return &apps->volumes.records[apps->volume_indices[cell]];
}

static const AstraDirectoryEntry *visible_folder(const DesktopApps *apps, uint32_t cell)
{
    uint32_t index = cell - apps->volume_visible - 1u;
    return index < apps->folders.count ? &apps->folders.entries[index] : NULL;
}

static uint32_t read_folders(DesktopApps *apps)
{
    uint32_t status = astra_directory_snapshot_load(&apps->folders,
        &process_filesystem.filesystem, DESKTOP_DIRECTORY, NULL);
    if (status != ASTRA_VFS_OK) {
        apps->shown = 1u;
        return status;
    }
    uint32_t kept = 0u;
    for (uint32_t at = 0u; at < apps->folders.count; ++at)
        if (apps->folders.entries[at].kind == ASTRA_VFS_KIND_DIRECTORY)
            apps->folders.entries[kept++] = apps->folders.entries[at];
    apps->folders.count = kept;
    apps->shown = 1u + kept;
    return ASTRA_VFS_OK;
}

static AstraHandle volume_metadata;
static AstraHandle volume_administrator;
static AstraHandle volume_inspector;
static AstraVolumeWatch volume_watch = ASTRA_VOLUME_WATCH_INIT;
static uint32_t volume_refresh;
static uint64_t volume_retry;
static AstraScrollModel icon_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraControl icon_controls[3];
static AstraUIContext icon_ui = ASTRA_UI_CONTEXT_INIT;
/* The standard scrollbar painter targets hardware commands. Input
   remains in icon_ui; both views borrow the same canonical scroll model. */
static AstraControl bar_control;
static AstraUIContext bar_ui = ASTRA_UI_CONTEXT_INIT;
static _Alignas(4) uint32_t bar_commands[ASTRA_DRAW_LIST_AREA_BYTES / sizeof(uint32_t)];
static AstraSurface bar_surface = ASTRA_SURFACE_INIT;
typedef struct DesktopRemoval {
    struct DesktopRemoval *next;
    AstraVolumeRemoval operation;
    AstraVolumeReference eject_source;
    uint32_t failure;
    uint32_t reported;
    uint32_t eject_admitted;
    uint32_t eject_failed;
} DesktopRemoval;
static DesktopRemoval *removals;
static uint64_t removal_retry;
static void removal_complete(const AstraVolumeSnapshot *catalog);

static AstraResult layout_icons(uint32_t count)
{
    AstraFlexItem child = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    uint32_t height = astra_desktop_content_height(count, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT);

    if (height == 0u) return ASTRA_ERROR_NO_RESOURCES;
    child.parent_id = 1u;
    child.minimum_width = child.maximum_width = DESKTOP_GRID_WIDTH;
    child.minimum_height = child.maximum_height = height;
    AstraResult result = astra_interface_control_set_flex(&icon_controls[1], &child);
    root.direction = ASTRA_FLEX_ROW;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (result == ASTRA_OK) result = astra_interface_ui_layout(&icon_ui, &root);
    return result;
}

static AstraResult create_icon_controls(AstraDisplay *display)
{
    AstraScrollModelInfo model = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraContainerInfo child = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollbarInfo bar = ASTRA_SCROLLBAR_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraSurfaceCreateInfo surface = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraResult result;

    model.line_width = ASTRA_DESKTOP_CELL_PITCH_X;
    model.line_height = ASTRA_DESKTOP_CELL_PITCH_Y;
    result = astra_scroll_init(&icon_scroll, &model);
    view.id = 1u;
    view.model = &icon_scroll;
    view.preferred_width = DESKTOP_GRID_WIDTH;
    view.preferred_height = DESKTOP_HEIGHT;
    if (result == ASTRA_OK) result = astra_interface_scroll_view_init(&icon_controls[0], &view);
    item.grow = 1u;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&icon_controls[0], &item);
    child.id = 2u;
    if (result == ASTRA_OK) result = astra_interface_container_init(&icon_controls[1], &child);
    /* UI admission builds the parent tree; the viewport must already own
       its one content child before that admission, not during later layout. */
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = view.id;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&icon_controls[1], &item);
    bar.id = 3u;
    bar.model = &icon_scroll;
    if (result == ASTRA_OK) result = astra_interface_scrollbar_init(&icon_controls[2], &bar);
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = item.maximum_width = DESKTOP_SCROLLBAR_WIDTH;
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&icon_controls[2], &item);
    if (result == ASTRA_OK) result = astra_interface_ui_init(&icon_ui, icon_controls, 3u, DESKTOP_WIDTH, DESKTOP_HEIGHT);
    if (result == ASTRA_OK) result = layout_icons(0u);
    if (result == ASTRA_OK) result = astra_interface_scrollbar_init(&bar_control, &bar);
    if (result == ASTRA_OK) result = astra_interface_control_set_flex(&bar_control, &item);
    if (result == ASTRA_OK) result = astra_interface_ui_init(&bar_ui, &bar_control, 1u, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT);
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (result == ASTRA_OK) result = astra_interface_ui_layout(&bar_ui, &root);
    surface.width = DESKTOP_SCROLLBAR_WIDTH;
    surface.height = DESKTOP_HEIGHT;
    surface.format = ASTRA_PIXEL_FORMAT_RGB565;
    surface.flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_DRAW_SOURCE;
    if (result == ASTRA_OK) result = astra_surface_create(display, &surface, &bar_surface);
    return result;
}

static AstraResult read_volumes(AstraVolumeSnapshot *snapshot, uint32_t *count,
                                 uint32_t **indices)
{
    AstraResult result = astra_volume_snapshot_open(volume_metadata, snapshot);
    *count = 0u;
    *indices = NULL;
    if (result != ASTRA_OK) return result;
    removal_complete(snapshot);
    if (snapshot->count > SIZE_MAX / sizeof(**indices))
        return ASTRA_ERROR_NO_RESOURCES;
    if (snapshot->count != 0u) {
        *indices = astra_runtime_allocate((size_t)snapshot->count * sizeof(**indices));
        if (*indices == NULL) return ASTRA_ERROR_NO_RESOURCES;
    }
    for (uint32_t at = 0u; at < snapshot->count; ++at) {
        const AstraVolumeRecord *record = &snapshot->records[at];
        int empty_disk = record->kind == ASTRA_VOLUME_DEVICE &&
            record->state == ASTRA_VOLUME_DISCOVERED &&
            (record->class_id == ASTRA_VOLUME_USB ||
             record->class_id == ASTRA_VOLUME_FIXED) &&
            (record->flags & ASTRA_VOLUME_SYSTEM) == 0u;
        for (uint32_t source = 0u; empty_disk && source < snapshot->count; ++source) {
            const AstraVolumeRecord *child = &snapshot->records[source];
            if (child->kind != ASTRA_VOLUME_SOURCE ||
                child->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
                child->state == ASTRA_VOLUME_LOST) continue;
            const AstraVolumeReference reference = {snapshot->epoch,
                child->id, child->generation};
            if (astra_volume_snapshot_ancestor(snapshot, &reference,
                ASTRA_VOLUME_DEVICE) == record) empty_disk = 0;
        }
        if ((record->kind == ASTRA_VOLUME_MOUNT &&
            (record->state == ASTRA_VOLUME_MOUNTED || record->state == ASTRA_VOLUME_QUIESCING) &&
            (record->flags & ASTRA_VOLUME_TEMPORARY) == 0u) ||
            astra_volume_source_issue(record) != ASTRA_VOLUME_SOURCE_READY || empty_disk)
            (*indices)[(*count)++] = at;
    }
    return ASTRA_OK;
}

ASTRA_PROGRAM("desktop", 0, 5, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static void play_startup_sound(uint32_t pcm_service)
{
    AstraPcmStream stream = ASTRA_PCM_STREAM_INIT;
    uint8_t *frames = NULL;
    uint32_t length = 0u;
    uint32_t frame_count;
    uint32_t sent = 0u;
    uint64_t deadline;
    AstraPcmStatus state = {0};

    if (astra_process_read_file_alloc(
            &process_filesystem, "/system/media/startup.pcm",
            (void **)&frames, &length) != ASTRA_VFS_OK ||
        length == 0u || (length % 4u) != 0u) {
        (void)astra_log("startup sound asset unavailable");
        goto done;
    }
    if (astra_pcm_open(pcm_service, ASTRA_PCM_FORMAT_S16BE_STEREO,
                       &stream) != ASTRA_OK) {
        (void)astra_log("startup sound service unavailable");
        goto done;
    }
    frame_count = length / 4u;
    deadline = astra_clock_monotonic() +
        (uint64_t)frame_count * UINT64_C(1000000000) / ASTRA_PCM_RATE +
        UINT64_C(2000000000);
    while (sent < frame_count) {
        uint32_t accepted = 0u;
        AstraResult result = astra_pcm_write(
            &stream, frames + sent * 4u,
            frame_count - sent, &accepted);

        sent += accepted;
        if (result == ASTRA_ERROR_BUSY &&
            astra_clock_monotonic() < deadline) {
            uint32_t room = frame_count - sent;

            if (room > ASTRA_PCM_QUEUE_FRAMES / 2u)
                room = ASTRA_PCM_QUEUE_FRAMES / 2u;
            if (astra_pcm_wait(&stream, room) != ASTRA_OK)
                goto done;
        } else if (result != ASTRA_OK) {
            goto done;
        }
    }
    if (astra_pcm_finish(&stream) != ASTRA_OK)
        goto done;
    while (astra_clock_monotonic() < deadline) {
        if (astra_pcm_status(&stream, &state) != ASTRA_OK)
            break;
        if (state.queued_frames == 0u && state.hardware_frames == 0u) {
            (void)astra_log("startup sound complete");
            break;
        }
        (void)astra_rt_thread_sleep(UINT64_C(2000000),
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
done:
    if (stream.control != 0u) {
        AstraResult ignored = astra_pcm_close(&stream);

        (void)ignored;
    }
    astra_runtime_deallocate(frames);
}

static void launch_error(uint32_t gui, const char *name, const char *path,
                         AstraResult failure)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    AstraResult alert_result;
    char storage[ASTRA_BUNDLE_NAME_MAX + 64u];
    AstraString message;

    (void)astra_log_failure("desktop launch", (uint32_t)(-failure));
    (void)astra_log(path);
    astra_string_init(&message, storage, sizeof(storage));
    if (failure == ASTRA_ERROR_NO_RESOURCES)
        (void)astra_string_append(
            &message, "There are not enough resources to start ");
    (void)astra_string_append(&message, name);
    (void)astra_string_append(&message,
                              failure == ASTRA_ERROR_NO_RESOURCES ?
                                  "." : " could not be started.");
    info.kind = ASTRA_ALERT_ERROR;
    info.title = "Application Error";
    info.title_length = 17u;
    info.message = storage;
    info.message_length = (uint16_t)message.length;
    info.button = "OK";
    info.button_length = 2u;
    alert_result = astra_interface_show_alert(gui, &info);
    (void)alert_result;
}

typedef struct DesktopCommands {
    DesktopApps *apps;
    uint32_t gui;
    uint32_t launcher;
    uint32_t *selection;
    DesktopUsage *usage;
    AstraDisplay *display;
    AstraSurface *content;
} DesktopCommands;

static AstraResult paint(AstraSurface *content, const DesktopApps *apps);
static void log_visible_icons(const DesktopApps *apps);

static void folder_error(uint32_t gui, const char *message, uint32_t status)
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

static AstraResult refresh_folders(DesktopCommands *context)
{
    uint32_t status = read_folders(context->apps);
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("desktop folder reload", status);
        return astra_result_from_service(status);
    }
    uint64_t count = (uint64_t)context->apps->volume_visible + context->apps->shown;
    if (count > UINT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
    AstraResult result = layout_icons((uint32_t)count);
    if (result != ASTRA_OK)
        (void)astra_log_failure("desktop folder layout", (uint32_t)(-result));
    if (result == ASTRA_OK) log_visible_icons(context->apps);
    if (result == ASTRA_OK) result = paint(context->content, context->apps);
    if (result != ASTRA_OK)
        (void)astra_log_failure("desktop folder repaint", (uint32_t)(-result));
    return result;
}

static AstraResult begin_folder_edit(DesktopCommands *context, uint32_t cell)
{
    const AstraDirectoryEntry *folder = visible_folder(context->apps, cell);
    if (folder == NULL || astra_folder_path(DESKTOP_DIRECTORY, folder->name,
        name_edit_path, sizeof(name_edit_path)) != ASTRA_VFS_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_interface_inline_edit_start(&name_edit, context->display, folder->name);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("desktop folder editor start", (uint32_t)(-result));
        return result;
    }
    name_edit_cell = cell;
    result = paint(context->content, context->apps);
    if (result != ASTRA_OK)
        (void)astra_log_failure("desktop folder editor paint", (uint32_t)(-result));
    return result;
}

static AstraResult new_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"New", 3u, 1u, 0u};
    return ASTRA_OK;
}

static AstraResult folder_query(void *opaque, AstraCommandState *state)
{
    (void)opaque;
    *state = (AstraCommandState){"Folder", 6u, 1u, 0u};
    return ASTRA_OK;
}

static AstraResult folder_action_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Rename", 6u,
        *context->selection > context->apps->volume_visible &&
        *context->selection < context->apps->volume_visible + context->apps->shown, 0u};
    return ASTRA_OK;
}

static AstraResult folder_delete_query(void *opaque, AstraCommandState *state)
{
    AstraResult result = folder_action_query(opaque, state);
    if (result == ASTRA_OK) {
        state->label = "Delete";
        state->label_length = 6u;
    }
    return result;
}

static AstraResult new_folder(void *opaque, const AstraCommandArguments *args)
{
    (void)args;
    DesktopCommands *context = opaque;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_folder_create_numbered(&process_filesystem.filesystem,
        DESKTOP_DIRECTORY, "Folder", path, sizeof(path));
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("desktop folder create", status);
        folder_error(context->gui, "Folder could not be created", status);
        return astra_result_from_service(status);
    }
    AstraResult result = refresh_folders(context);
    if (result != ASTRA_OK) return result;
    const char *name = astra_folder_leaf(path);
    for (uint32_t at = 0u; at < context->apps->folders.count; ++at)
        if (strcmp(context->apps->folders.entries[at].name, name) == 0)
            return begin_folder_edit(context,
                context->apps->volume_visible + 1u + at);
    return ASTRA_ERROR_IO;
}

static AstraResult rename_folder(void *opaque, const AstraCommandArguments *args)
{
    DesktopCommands *context = opaque;
    uint32_t cell;
    if (args == NULL || args->length != sizeof(cell)) return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    return begin_folder_edit(context, cell);
}

static AstraResult delete_folder(void *opaque, const AstraCommandArguments *args)
{
    DesktopCommands *context = opaque;
    uint32_t cell;
    char source[ASTRA_VFS_PATH_MAX], moved[ASTRA_VFS_PATH_MAX];
    if (args == NULL || args->length != sizeof(cell)) return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    const AstraDirectoryEntry *folder = visible_folder(context->apps, cell);
    if (folder == NULL || astra_folder_path(DESKTOP_DIRECTORY, folder->name,
        source, sizeof(source)) != ASTRA_VFS_OK) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t status = astra_entry_move_to_trash(&process_filesystem.filesystem,
        source, DESKTOP_TRASH_DIRECTORY, moved, sizeof(moved));
    if (status != ASTRA_VFS_OK) {
        folder_error(context->gui, "Folder could not be deleted", status);
        return astra_result_from_service(status);
    }
    astra_interface_inline_edit_stop(&name_edit);
    name_edit_cell = UINT32_MAX;
    return refresh_folders(context);
}

static AstraResult finish_folder_edit(DesktopCommands *context, int commit)
{
    if (!name_edit.active) return ASTRA_OK;
    if (commit) {
        char name[ASTRA_VFS_NAME_MAX], moved[ASTRA_VFS_PATH_MAX];
        AstraResult result = astra_interface_inline_edit_text(&name_edit, name, sizeof(name));
        if (result != ASTRA_OK || !astra_folder_name_valid(name)) {
            folder_error(context->gui, "Choose a valid folder name", ASTRA_VFS_ERR_INVALID);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        uint32_t status = astra_entry_rename_safe(&process_filesystem.filesystem,
            name_edit_path, DESKTOP_DIRECTORY, name, moved, sizeof(moved));
        if (status != ASTRA_VFS_OK) {
            folder_error(context->gui, "Folder could not be renamed", status);
            return astra_result_from_service(status);
        }
    }
    astra_interface_inline_edit_stop(&name_edit);
    name_edit_cell = UINT32_MAX;
    return refresh_folders(context);
}

static int can_unmount(const DesktopApps *apps, uint32_t cell)
{
    if (cell >= apps->volume_visible) return 0;
    const AstraVolumeRecord *record = visible_volume(apps, cell);
    if (record->kind != ASTRA_VOLUME_MOUNT || record->state != ASTRA_VOLUME_MOUNTED ||
        (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u) return 0;
    const AstraVolumeReference reference = {apps->volumes.epoch,
        record->id, record->generation};
    for (DesktopRemoval *at = removals; at != NULL; at = at->next)
        if (astra_volume_reference_equal(&at->operation.mount, &reference))
            return 0;
    return 1;
}

static const AstraVolumeRecord *eject_source(const DesktopApps *apps, uint32_t cell)
{
    if (!can_unmount(apps, cell)) return NULL;
    const AstraVolumeRecord *mount = visible_volume(apps, cell);
    const AstraVolumeReference reference = {apps->volumes.epoch,
        mount->id, mount->generation};
    const AstraVolumeRecord *source = astra_volume_snapshot_ancestor(
        &apps->volumes, &reference, ASTRA_VOLUME_SOURCE);
    return source != NULL && source->origin == ASTRA_VOLUME_ORIGIN_DEVICE &&
        source->class_id == ASTRA_VOLUME_OPTICAL ? source : NULL;
}

static int can_disk_setup(const DesktopApps *apps, uint32_t cell)
{
    if (cell >= apps->volume_visible) return 0;
    const AstraVolumeRecord *record = visible_volume(apps, cell);
    if (record->kind == ASTRA_VOLUME_DEVICE)
        return record->class_id == ASTRA_VOLUME_USB ||
            record->class_id == ASTRA_VOLUME_FIXED;
    if (record->kind == ASTRA_VOLUME_SOURCE)
        return record->origin == ASTRA_VOLUME_ORIGIN_DEVICE;
    if (record->kind != ASTRA_VOLUME_MOUNT) return 0;
    const AstraVolumeReference reference = {apps->volumes.epoch,
        record->id, record->generation};
    const AstraVolumeRecord *source = astra_volume_snapshot_ancestor(
        &apps->volumes, &reference, ASTRA_VOLUME_SOURCE);
    return source != NULL && source->origin == ASTRA_VOLUME_ORIGIN_DEVICE;
}

static AstraResult disk_setup_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Disk Setup...", 13u,
        can_disk_setup(context->apps, *context->selection), 0u};
    return ASTRA_OK;
}

static AstraResult unmount_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Unmount", 7u,
        volume_administrator != 0u && volume_inspector != 0u && can_unmount(context->apps, *context->selection), 0u};
    return ASTRA_OK;
}

static AstraResult eject_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Eject", 5u,
        volume_administrator != 0u && volume_inspector != 0u &&
        eject_source(context->apps, *context->selection) != NULL, 0u};
    return ASTRA_OK;
}

static void volume_error(uint32_t gui, const char *message, AstraResult failure)
{
    char text[256]; AstraString out;
    astra_string_init(&out, text, sizeof(text));
    (void)astra_string_append(&out, message); (void)astra_string_append(&out, " (status ");
    (void)astra_string_append_i64(&out, failure); (void)astra_string_append(&out, ").");
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.kind = ASTRA_ALERT_ERROR; info.title = "Volume operation"; info.title_length = 16u;
    info.message = text; info.message_length = (uint16_t)out.length; info.button = "Close"; info.button_length = 5u;
    AstraResult ignored = astra_interface_show_alert(gui, &info); (void)ignored;
}

static void removal_complete(const AstraVolumeSnapshot *catalog)
{
    DesktopRemoval **link = &removals;
    while (*link != NULL) {
        DesktopRemoval *pending = *link;
        if (pending->eject_failed) { link = &pending->next; continue; }
        AstraResult result = astra_volume_removal_advance(
            volume_administrator, catalog, &pending->operation);
        if (result != ASTRA_OK) {
            (void)astra_log_failure("desktop volume removal", (uint32_t)(-result));
            link = &pending->next;
            continue;
        }
        if (pending->operation.state == ASTRA_VOLUME_REMOVAL_REFUSED)
            pending->failure = pending->operation.status;
        if (pending->operation.state == ASTRA_VOLUME_REMOVAL_RETAINED &&
            pending->operation.status != 0u && !pending->reported) {
            pending->failure = pending->operation.status;
            pending->reported = 1u;
        }
        if (pending->operation.state != ASTRA_VOLUME_REMOVAL_COMPLETE &&
            pending->operation.state != ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED) {
            link = &pending->next;
            continue;
        }
        if (pending->operation.state == ASTRA_VOLUME_REMOVAL_COMPLETE &&
            pending->eject_source.id != 0u) {
            const AstraVolumeReference source = pending->eject_source;
            if (!pending->eject_admitted) {
                AstraVolumeControl control = {.reference = source,
                    .operation = ASTRA_VOLUME_EJECT};
                AstraVolumeReference admitted = {0};
                result = astra_volume_control(volume_administrator, &control, &admitted);
                if (result == ASTRA_OK) pending->eject_admitted = 1u;
                else {
                    pending->failure = astra_status_from_result(result);
                    pending->eject_failed = 1u;
                }
                link = &pending->next;
                continue;
            }
            const AstraVolumeRecord *record = NULL;
            if (catalog->epoch == source.epoch)
                for (uint32_t at = 0u; at < catalog->count; ++at)
                    if (catalog->records[at].id == source.id) {
                        record = &catalog->records[at];
                        break;
                    }
            if (record != NULL && record->generation == source.generation &&
                record->state == ASTRA_VOLUME_UNMOUNTED && record->status != 0u) {
                pending->failure = record->status;
                pending->eject_failed = 1u;
            }
            if (record != NULL && record->generation == source.generation &&
                record->state != ASTRA_VOLUME_LOST) {
                link = &pending->next;
                continue;
            }
        }
        int same_provider = pending->operation.state == ASTRA_VOLUME_REMOVAL_COMPLETE;
        int ejected = pending->eject_source.id != 0u;
        *link = pending->next; astra_runtime_deallocate(pending);
        (void)astra_log(!same_provider ? "desktop volume provider retired" :
            ejected ? "desktop volume ejected" : "desktop volume unmounted");
    }
    removal_retry = astra_clock_monotonic() + UINT64_C(50000000);
}

static AstraResult remove_icon(void *opaque, const AstraCommandArguments *args, int eject)
{
    const DesktopCommands *context = opaque;
    uint32_t cell;
    if (args == NULL || args->length != sizeof(cell)) return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    if (!can_unmount(context->apps, cell)) return ASTRA_ERROR_INVALID_HANDLE;
    const AstraVolumeRecord *record = visible_volume(context->apps, cell);
    const AstraVolumeRecord *source = eject ? eject_source(context->apps, cell) : NULL;
    if (eject && source == NULL) return ASTRA_ERROR_INVALID_HANDLE;
    DesktopRemoval *pending = astra_runtime_callocate(1u, sizeof(*pending));
    if (pending == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    pending->operation.mount = (AstraVolumeReference){context->apps->volumes.epoch,
        record->id, record->generation};
    if (source != NULL)
        pending->eject_source = (AstraVolumeReference){context->apps->volumes.epoch,
            source->id, source->generation};
    if (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE)
        pending->operation.image_source = (AstraVolumeReference){
            pending->operation.mount.epoch, record->parent, record->parent_generation};
    AstraVolumeControl control = {.reference = pending->operation.mount,
        .operation = ASTRA_VOLUME_UNMOUNT};
    AstraVolumeReference admitted = {0};
    AstraResult result = astra_volume_control(volume_administrator, &control, &admitted);
    if (result == ASTRA_OK) {
        pending->next = removals; removals = pending;
        volume_refresh = 1u; volume_retry = 0u; removal_retry = 0u;
        (void)astra_log(eject ? "desktop volume eject admitted" : "desktop volume unmount admitted");
        return ASTRA_OK;
    }
    AstraVolumeReference reference = pending->operation.mount;
    astra_runtime_deallocate(pending);
    if (result == ASTRA_ERROR_BUSY) {
        result = usage_open(context->usage, context->gui, volume_inspector, &process_filesystem, &reference);
        if (result == ASTRA_OK) { (void)astra_log("desktop volume busy dialog opened"); return ASTRA_OK; }
        volume_error(context->gui, result == ASTRA_ERROR_BUSY ? "Resources changed repeatedly during inspection; retry Unmount" :
            "The exact resource list could not be read", result);
    } else volume_error(context->gui, "Unmount was refused", result);
    return result;
}

static AstraResult unmount_icon(void *opaque, const AstraCommandArguments *args)
{
    return remove_icon(opaque, args, 0);
}

static AstraResult eject_icon(void *opaque, const AstraCommandArguments *args)
{
    return remove_icon(opaque, args, 1);
}

static AstraResult open_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Open", 4u,
        context->apps->shown + context->apps->volume_visible != 0u, 0u};
    return ASTRA_OK;
}

static void source_open_error(uint32_t gui, const AstraVolumeRecord *record,
                              uint32_t issue)
{
    if (issue == ASTRA_VOLUME_SOURCE_PROBE_ERROR) {
        volume_error(gui, "Astra could not inspect this disk", (AstraResult)record->status);
        return;
    }
    char text[256];
    AstraString message;
    astra_string_init(&message, text, sizeof(text));
    if (issue == ASTRA_VOLUME_SOURCE_NDOS)
        (void)astra_string_append(&message,
            "Astra cannot open this disk: no supported filesystem was found.");
    else if (issue == ASTRA_VOLUME_SOURCE_AMBIGUOUS)
        (void)astra_string_append(&message,
            "Astra found conflicting filesystem signatures on this disk.");
    else {
        (void)astra_string_append(&message, "Astra recognized ");
        (void)astra_string_append(&message, record->detected_type);
        (void)astra_string_append(&message,
            ", but this filesystem is not available for mounting.");
    }
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.kind = ASTRA_ALERT_ERROR;
    info.title = "Disk unavailable";
    info.title_length = 16u;
    info.message = text;
    info.message_length = (uint16_t)message.length;
    info.button = "Close";
    info.button_length = 5u;
    (void)astra_interface_show_alert(gui, &info);
}

static AstraResult launch_volume_application(const DesktopCommands *context,
    const AstraVolumeRecord *record, const char *bundle, const char *name,
    const char *option)
{
    char epoch[24], id[24], generation[24];
    AstraString number;
    uint32_t process_id;
    astra_string_init(&number, epoch, sizeof(epoch));
    (void)astra_string_append_u64(&number, context->apps->volumes.epoch);
    astra_string_init(&number, id, sizeof(id));
    (void)astra_string_append_u64(&number, record->id);
    astra_string_init(&number, generation, sizeof(generation));
    (void)astra_string_append_u64(&number, record->generation);
    const char *arguments[] = {option, epoch, id, generation};
    AstraResult result = astra_application_launch_with_arguments(
        context->launcher, bundle, (uint16_t)strlen(bundle),
        ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 4u, &process_id);
    if (result != ASTRA_OK)
        launch_error(context->gui, name, bundle, result);
    return result;
}

static AstraResult disk_setup_icon(void *opaque, const AstraCommandArguments *args)
{
    const DesktopCommands *context = opaque;
    uint32_t cell;
    if (args == NULL || args->length != sizeof(cell))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    if (!can_disk_setup(context->apps, cell))
        return ASTRA_ERROR_INVALID_HANDLE;
    return launch_volume_application(context,
        visible_volume(context->apps, cell), "/apps/DiskSetup.app",
        "Disk Setup", "--target");
}

static AstraResult open_icon(void *opaque, const AstraCommandArguments *args)
{
    const DesktopCommands *context = opaque;
    const DesktopApps *apps = context->apps;
    uint32_t cell, process_id;
    AstraResult result;

    if (args == NULL || args->length != sizeof(cell))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    if (cell < apps->volume_visible) {
        const AstraVolumeRecord *record = visible_volume(apps, cell);
        if (record->kind == ASTRA_VOLUME_DEVICE) {
            source_open_error(context->gui, record, ASTRA_VOLUME_SOURCE_NDOS);
            return ASTRA_OK;
        }
        uint32_t issue = astra_volume_source_issue(record);
        if (issue != ASTRA_VOLUME_SOURCE_READY) {
            source_open_error(context->gui, record, issue);
            return ASTRA_OK;
        }
        result = launch_volume_application(context, record, "/apps/Atlas.app",
            "Atlas", "--volume");
    } else if (cell < apps->volume_visible + apps->shown) {
        char path[ASTRA_VFS_PATH_MAX];
        const char *target = "/apps";
        if (cell > apps->volume_visible) {
            const AstraDirectoryEntry *folder = visible_folder(apps, cell);
            if (folder == NULL || astra_folder_path(DESKTOP_DIRECTORY,
                folder->name, path, sizeof(path)) != ASTRA_VFS_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            target = path;
        }
        const char *arguments[] = {target};
        result = astra_application_launch_with_arguments(context->launcher,
            "/apps/Atlas.app", (uint16_t)strlen("/apps/Atlas.app"),
            ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 1u, &process_id);
        if (result != ASTRA_OK) launch_error(context->gui, "Atlas", target, result);
    } else return ASTRA_ERROR_INVALID_ARGUMENT;
    return result;
}

static AstraResult show_about(uint32_t gui)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;

    info.title = "About This Astra";
    info.title_length = 16u;
    info.message = "Astra OS - Axiom kernel - MC68040";
    info.message_length = 33u;
    info.button = "Close";
    info.button_length = 5u;
    return astra_interface_show_alert(gui, &info);
}

static void power_error(uint32_t gui, int restart)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;

    info.kind = ASTRA_ALERT_ERROR;
    info.title = restart ? "Restart" : "Shut Down";
    info.title_length = (uint16_t)strlen(info.title);
    info.message = restart ? "Restart could not be started." :
                             "Shutdown could not be started.";
    info.message_length = (uint16_t)strlen(info.message);
    info.button = "OK";
    info.button_length = 2u;
    (void)astra_interface_show_alert(gui, &info);
}

/* One indexed icon source of the desktop window: uploaded once, palette
   expanded and blended by Astraea on every repaint. */
static AstraResult load_icon(AstraDisplay *display, const char *path,
                             AstraSurface *surface)
{
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    AstraResult result;

    if (astra_process_read_file_alloc(&process_filesystem, path,
                                      (void **)&bytes, &length) !=
            ASTRA_VFS_OK) {
        astra_runtime_deallocate(bytes);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    result = astra_surface_from_aicon(display, bytes, length,
                                     ASTRA_DESKTOP_ICON_SIZE, surface);
    astra_runtime_deallocate(bytes);
    return result;
}

/* "desktop icon /apps/X.app LEFT TOP WIDTH HEIGHT" in screen pixels: where
   a person, or a gate, double-clicks to open that application. */
static void log_icon(const char *path, uint32_t dx,
                     int32_t dy)
{
    uint32_t capacity = (uint32_t)strlen(path) + 64u;
    char *storage = astra_runtime_allocate(capacity);
    AstraString line;
    const uint32_t box[4] = {
        ASTRA_DESKTOP_ICON_CELL_LEFT + dx,
        (uint32_t)((int32_t)(DESKTOP_TOP + ASTRA_DESKTOP_GRID_TOP) + dy),
        ASTRA_DESKTOP_ICON_CELL_WIDTH, ASTRA_DESKTOP_CELL_HEIGHT
    };

    if (storage == NULL) { (void)astra_log_failure("desktop icon log allocation", ASTRA_STATUS_LIMIT); return; }
    astra_string_init(&line, storage, capacity);
    (void)astra_string_append(&line, "desktop icon ");
    (void)astra_string_append(&line, path);
    for (uint32_t at = 0u; at < 4u; ++at) {
        (void)astra_string_append_char(&line, ' ');
        (void)astra_string_append_u64(&line, box[at]);
    }
    (void)astra_log(storage);
    astra_runtime_deallocate(storage);
}

static void load_desktop(AstraDisplay *display, DesktopApps *apps)
{
    apps->volume_icon = (AstraSurface)ASTRA_SURFACE_INIT;
    apps->folder_icon = (AstraSurface)ASTRA_SURFACE_INIT;
    apps->applications_icon = (AstraSurface)ASTRA_SURFACE_INIT;
    AstraResult observed = read_volumes(&apps->volumes, &apps->volume_visible,
        &apps->volume_indices);
    uint32_t folders = astra_filesystem_mkdir(&process_filesystem.filesystem,
        DESKTOP_DIRECTORY);
    if (folders == ASTRA_VFS_OK || folders == ASTRA_VFS_ERR_EXISTS)
        folders = read_folders(apps);
    else apps->shown = 1u;
    ASTRA_EVENT3(ASTRA_EVENT_SUBSYSTEM_DISPLAY, ASTRA_EVENT_LEVEL_INFO,
                 "desktop inventory folders=%u volumes=%u folder_status=%u",
                 apps->folders.count, apps->volume_visible, folders);
    if (observed != ASTRA_OK)
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_DISPLAY, ASTRA_EVENT_LEVEL_WARNING,
                     "desktop volume snapshot failed status=%d", observed);
    if (observed != ASTRA_OK) {
        (void)astra_log_failure("desktop volume catalog", (uint32_t)(-observed));
        volume_refresh = 1u;
        volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
    }
    if (load_icon(display, "/apps/Atlas.app/resources/volume.aicon", &apps->volume_icon) != ASTRA_OK)
        (void)astra_log("desktop volume icon unavailable");
    if (load_icon(display, "/apps/Atlas.app/resources/drawer.aicon", &apps->folder_icon) != ASTRA_OK)
        (void)astra_log("desktop folder icon unavailable");
    if (load_icon(display, "/apps/Atlas.app/resources/Atlas.aicon", &apps->applications_icon) != ASTRA_OK)
        (void)astra_log("desktop applications icon unavailable");
    if (folders != ASTRA_VFS_OK)
        (void)astra_log_failure("desktop folder inventory", folders);
}

static const char *source_icon_name(const AstraVolumeRecord *record)
{
    uint32_t issue = astra_volume_source_issue(record);
    if (issue == ASTRA_VOLUME_SOURCE_NDOS) return "NDOS";
    if (issue == ASTRA_VOLUME_SOURCE_UNSUPPORTED)
        return record->detected_type[0] != '\0' ? record->detected_type : "Unsupported";
    if (issue == ASTRA_VOLUME_SOURCE_AMBIGUOUS) return "Ambiguous";
    return "Disk error";
}

static void log_visible_icons(const DesktopApps *apps)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (astra_scroll_get_state(&icon_scroll, &state) != ASTRA_OK) return;
    char storage[96];
    AstraString line;
    astra_string_init(&line, storage, sizeof(storage));
    (void)astra_string_append(&line, "desktop viewport ");
    (void)astra_string_append_u64(&line, state.offset_y);
    (void)astra_string_append_char(&line, ' ');
    (void)astra_string_append_u64(&line, state.maximum_y);
    (void)astra_string_append_char(&line, ' ');
    (void)astra_string_append_u64(&line, apps->volume_visible + apps->shown);
    (void)astra_log(storage);
    for (uint32_t at = 0u; at < apps->volume_visible + apps->shown; ++at) {
        uint32_t dx, dy;
        astra_desktop_cell_offset(at, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT, &dx, &dy);
        int64_t top = (int64_t)ASTRA_DESKTOP_GRID_TOP + dy - state.offset_y;
        /* Only complete hit targets get published, never offscreen positions. */
        if (top < 0 || top + ASTRA_DESKTOP_CELL_HEIGHT > DESKTOP_HEIGHT) continue;
        char path[ASTRA_VFS_PATH_MAX];
        const char *name = at < apps->volume_visible ?
            (visible_volume(apps, at)->kind == ASTRA_VOLUME_DEVICE ? "NDOS" :
             visible_volume(apps, at)->kind == ASTRA_VOLUME_SOURCE ?
                source_icon_name(visible_volume(apps, at)) :
                visible_volume(apps, at)->name) :
            at == apps->volume_visible ? "/apps" :
            visible_folder(apps, at)->name;
        if (at > apps->volume_visible &&
            astra_folder_path(DESKTOP_DIRECTORY, name, path, sizeof(path)) == ASTRA_VFS_OK)
            name = path;
        log_icon(name,
            dx, (int32_t)dy - (int32_t)state.offset_y);
    }
}

/* The whole desktop in one list: canvas, then each icon and its label. */
static AstraResult paint(AstraSurface *content,
                         const DesktopApps *apps)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraDrawPaint canvas = ASTRA_DRAW_PAINT_INIT;
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    const AstraRectI32 whole = { 0, 0, DESKTOP_WIDTH, DESKTOP_HEIGHT };
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraResult result;

    result = astra_scroll_get_state(&icon_scroll, &state);
    if (result != ASTRA_OK) return result;
    if (name_edit.active) {
        result = astra_interface_inline_edit_render(&name_edit, name_edit_commands,
            sizeof(name_edit_commands));
        if (result != ASTRA_OK) return result;
    }
    canvas.foreground = theme.canvas;
    blend.blend = ASTRA_BLEND_ALPHA;
    result = astra_draw_list_create(content, &whole, &list);
    if (result == ASTRA_OK)
        result = astra_draw_rectangle(&list, &whole, 1, &canvas);
    if (result == ASTRA_OK)
        result = astra_draw_list_set_clip(&list,
            &(AstraRectI32){0, 0, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT});
    for (uint32_t at = 0u; result == ASTRA_OK &&
                           at < apps->shown + apps->volume_visible; ++at) {
        uint32_t is_volume = at < apps->volume_visible;
        char fallback[32];
        if (is_volume) {
            const AstraVolumeRecord *record = visible_volume(apps, at);
            const char *name = record->kind == ASTRA_VOLUME_DEVICE ? "NDOS" :
                record->kind == ASTRA_VOLUME_SOURCE ?
                source_icon_name(record) : record->name + 1u;
            uint32_t byte = 0u;
            for (; name[byte] != '\0' && byte + 1u < sizeof(fallback); ++byte)
                fallback[byte] = astra_ascii_upper(name[byte]);
            fallback[byte] = '\0';
        }
        const char *label = is_volume != 0u ?
            (visible_volume(apps, at)->kind == ASTRA_VOLUME_DEVICE ||
             (visible_volume(apps, at)->kind == ASTRA_VOLUME_SOURCE &&
              astra_volume_source_issue(visible_volume(apps, at)) != ASTRA_VOLUME_SOURCE_READY) ?
                fallback : visible_volume(apps, at)->label[0] != '\0' ?
                visible_volume(apps, at)->label : fallback) :
            at == apps->volume_visible ? "Applications" :
            visible_folder(apps, at)->name;
        const AstraSurface *icon = is_volume != 0u ? &apps->volume_icon :
            at == apps->volume_visible ? &apps->applications_icon :
            &apps->folder_icon;
        uint32_t dx;
        uint32_t dy;

        astra_desktop_cell_offset(at, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT, &dx, &dy);
        int32_t top = (int32_t)dy - (int32_t)state.offset_y;
        if ((int64_t)ASTRA_DESKTOP_GRID_TOP + top + ASTRA_DESKTOP_CELL_HEIGHT <= 0 ||
            (int64_t)ASTRA_DESKTOP_GRID_TOP + top >= DESKTOP_HEIGHT) continue;
        AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;
        tile.style = ASTRA_ICON_TILE_DESKTOP;
        tile.frame = (AstraRectI32){
            (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT + dx),
            (int32_t)ASTRA_DESKTOP_GRID_TOP + top,
            ASTRA_DESKTOP_ICON_CELL_WIDTH, ASTRA_DESKTOP_CELL_HEIGHT};
        tile.clip = (AstraRectI32){0, 0, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT};
        tile.icon = icon;
        tile.label = label;
        tile.label_bytes = (uint32_t)strlen(label);
        tile.editor = name_edit.active && at == name_edit_cell ?
            &name_edit.surface : NULL;
        result = astra_interface_icon_tile_render(&list, &tile);
    }
    if (result == ASTRA_OK && state.maximum_y != 0u) {
        AstraSurfaceView commands;
        if (!astra_draw_list_view_init(&commands, bar_commands,
                sizeof(bar_commands), DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT))
            result = ASTRA_ERROR_INVALID_ARGUMENT;
        else {
            astra_surface_clear(&commands, astra_surface_rgb565(
                theme.canvas.red, theme.canvas.green, theme.canvas.blue));
            result = astra_interface_ui_render(&bar_ui, &commands);
        }
        if (result == ASTRA_OK)
            result = astra_draw_list_set_target(&list, &bar_surface);
        if (result == ASTRA_OK)
            result = astra_draw_list_append_view(&list, &commands);
        if (result == ASTRA_OK)
            result = astra_draw_list_set_target(&list, content);
        if (result == ASTRA_OK) result = astra_draw_list_set_clip(&list, &whole);
        if (result == ASTRA_OK) result = astra_draw_blit(&list, &bar_surface,
            &(AstraRectI32){0, 0, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT},
            &(AstraRectI32){DESKTOP_GRID_WIDTH, 0, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT},
            &blend);
    }
    if (result == ASTRA_OK)
        result = astra_draw_post(&list, ASTRA_DRAW_POST_PRESENT);
    if (list._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_draw_list_close(&list);

        (void)ignored;
    }
    return result;
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraArea placeholder = ASTRA_AREA_INIT;
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface content = ASTRA_SURFACE_INIT;
    DesktopApps apps = {.folders = ASTRA_DIRECTORY_SNAPSHOT_INIT,
        .folder_icon = ASTRA_SURFACE_INIT,
        .applications_icon = ASTRA_SURFACE_INIT,
        .volume_icon = ASTRA_SURFACE_INIT, .volumes = ASTRA_VOLUME_SNAPSHOT_INIT};
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    const AstraStartupCapability *launcher;
    const AstraStartupCapability *manager;
    const AstraStartupCapability *pcm;
    const AstraStartupCapability *metadata;
    const AstraStartupCapability *administrator;
    const AstraStartupCapability *inspector;
    uint32_t status;
    AstraContextMenu context_menu = ASTRA_CONTEXT_MENU_INIT;
    uint32_t menu_cell = UINT32_MAX;
    uint32_t clicked_cell = UINT32_MAX;
    DesktopUsage *busy_dialog = NULL;
    AstraCommandArguments menu_arguments = {1u, 1u, &menu_cell, sizeof(menu_cell)};
    AstraContextMenuItem menu_items[5] = {{"desktop.open", 12u, &menu_arguments}};
    uint16_t menu_parents[5] = {0u};
    DesktopCommands command_context = {&apps, 0u, 0u, &menu_cell, NULL, &display, &content};
    const AstraCommand commands[] = {{"desktop.open", 12u, 1u, 1u, open_query, open_icon, &command_context},
        {"desktop.disk-setup", 18u, 1u, 1u, disk_setup_query, disk_setup_icon, &command_context},
        {"desktop.unmount", 15u, 1u, 1u, unmount_query, unmount_icon, &command_context},
        {"desktop.eject", 13u, 1u, 1u, eject_query, eject_icon, &command_context},
        {"desktop.new", 11u, 0u, 0u, new_query, new_folder, &command_context},
        {"desktop.new-folder", 18u, 0u, 0u, folder_query, new_folder, &command_context},
        {"desktop.rename-folder", 21u, 1u, 1u, folder_action_query, rename_folder, &command_context},
        {"desktop.delete-folder", 21u, 1u, 1u, folder_delete_query, delete_folder, &command_context}};
    const AstraCommandScope command_scope = {NULL, commands, 8u};

    if (!astra_startup_validate(startup) || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    launcher = astra_startup_capability(
        startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    manager = astra_startup_capability(
        startup, ASTRA_CAPABILITY_SERVICE_MANAGER);
    pcm = astra_startup_capability(startup, ASTRA_CAPABILITY_PCM);
    metadata = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUMES);
    administrator = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_ADMIN);
    inspector = astra_startup_capability(startup, ASTRA_CAPABILITY_VOLUME_USAGE);
    if (bootstrap == NULL || gui == NULL || launcher == NULL ||
        manager == NULL || metadata == NULL || administrator == NULL || inspector == NULL ||
        (manager->rights & ASTRA_RIGHT_SIGNAL) == 0u)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return ASTRA_STATUS_IO;
    busy_dialog = astra_runtime_callocate(1u, sizeof(*busy_dialog));
    if (busy_dialog == NULL) return ASTRA_STATUS_LIMIT;
    command_context.usage = busy_dialog;
    command_context.gui = gui->handle;
    command_context.launcher = launcher->handle;
    volume_metadata = metadata->handle;
    volume_administrator = administrator->handle;
    volume_inspector = inspector->handle;
    if (astra_volume_watch_open(volume_metadata, &volume_watch) != ASTRA_OK)
        return DESKTOP_FAIL_FILESYSTEM;
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_STATUS_OK) status = DESKTOP_FAIL_FILESYSTEM;
    /* A GPU-surface window: its first shared area is only the initial
       staging area, which the display replaces when an upload needs more. */
    if (status == ASTRA_STATUS_OK &&
        astra_area_create(4096u,
                          ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                              ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
                          &placeholder) != ASTRA_OK)
        status = DESKTOP_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        AstraResult result;

        info.x = 0u;
        info.y = DESKTOP_TOP;
        info.width = DESKTOP_WIDTH;
        info.height = DESKTOP_HEIGHT;
        info.flags = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
        info.type = ASTRA_WINDOW_DESKTOP;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                          ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                          ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                          ASTRA_WINDOW_SUBSCRIBE_KEY |
                          ASTRA_WINDOW_SUBSCRIBE_TEXT |
                          ASTRA_WINDOW_SUBSCRIBE_SYSTEM_ACTION;
        result = astra_window_create(gui->handle, placeholder.handle, &info,
                                     &window);
        {
            AstraResult ignored = astra_area_close(&placeholder);

            (void)ignored;
        }
        if (result == ASTRA_OK)
            result = astra_window_display(&window, &display);
        if (result == ASTRA_OK)
            result = astra_window_surface(&window, &content);
        if (result != ASTRA_OK)
            status = DESKTOP_FAIL_WINDOW + (uint32_t)(-result);
    }
    if (status == ASTRA_STATUS_OK) {
        AstraResult result;

        result = create_icon_controls(&display);
        if (result == ASTRA_OK) {
            load_desktop(&display, &apps);
            result = (uint64_t)apps.shown + apps.volume_visible > UINT32_MAX ?
                ASTRA_ERROR_NO_RESOURCES : layout_icons(apps.shown + apps.volume_visible);
        }
        if (result == ASTRA_OK) {
            log_visible_icons(&apps);
            result = paint(&content, &apps);
        }
        if (result == ASTRA_OK)
            /* Startup readiness waits for the initial frame to be visible. */
            result = astra_window_present(&window);
        if (result != ASTRA_OK)
            status = DESKTOP_FAIL_GRAPHICS + (uint32_t)(-result);
    }
    if (status == ASTRA_STATUS_OK && pcm != NULL)
        play_startup_sound(pcm->handle);
    (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) return (int)status;
    ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_DISPLAY, ASTRA_EVENT_LEVEL_INFO,
                 "desktop ready");
    for (;;) {
        AstraWindowEvent event = {0};
        uint32_t waits[3] = {astra_window_event_wait_handle(&window), volume_watch.receive, 0u};
        uint32_t wait_count = volume_watch.receive != ASTRA_INVALID_HANDLE ? 2u : 1u;
        uint32_t busy_index = UINT32_MAX;
        AstraHandle busy_wait = astra_window_event_wait_handle(&busy_dialog->window);
        if (busy_wait != ASTRA_INVALID_HANDLE) { busy_index = wait_count; waits[wait_count++] = busy_wait; }
        uint32_t selected = 0u;
        uint64_t deadline = volume_refresh ? volume_retry : ASTRA_DEADLINE_FOREVER;
        if (removals != NULL && removal_retry < deadline) deadline = removal_retry;
        uint32_t waited = astra_wait_multiple(waits, wait_count, deadline, &selected, NULL);
        AstraResult result;
        if (waited == ASTRA_SYSCALL_OK && selected == 1u && volume_watch.receive != ASTRA_INVALID_HANDLE) {
            AstraVolumeChange change;
            result = astra_volume_watch_receive(&volume_watch, &change);
            if (result != ASTRA_OK || change.status != ASTRA_STATUS_OK) {
                AstraResult ignored = astra_volume_watch_close(volume_metadata, &volume_watch);
                (void)ignored;
            }
            volume_refresh = 1u;
            volume_retry = 0u;
        } else if (waited == ASTRA_SYSCALL_PEER_DEAD && selected == 1u && volume_watch.receive != ASTRA_INVALID_HANDLE) {
            AstraResult ignored = astra_volume_watch_close(volume_metadata, &volume_watch);
            (void)ignored;
            volume_refresh = 1u;
            volume_retry = 0u;
        } else if (waited != ASTRA_SYSCALL_OK && waited != ASTRA_SYSCALL_TIMED_OUT)
            return DESKTOP_FAIL_INTERFACE;
        if (removals != NULL && astra_clock_monotonic() >= removal_retry) volume_refresh = 1u;
        if (volume_refresh && astra_clock_monotonic() >= volume_retry) {
            AstraVolumeSnapshot replacement = ASTRA_VOLUME_SNAPSHOT_INIT;
            uint32_t count = 0u;
            uint32_t *indices = NULL;
            result = volume_watch.receive == ASTRA_INVALID_HANDLE ? ASTRA_ERROR_PEER_DEAD :
                read_volumes(&replacement, &count, &indices);
            if (result != ASTRA_OK && volume_watch.receive != ASTRA_INVALID_HANDLE) {
                volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
                if (removals != NULL) removal_retry = volume_retry;
            } else {
                volume_refresh = 0u;
                if (volume_watch.receive == ASTRA_INVALID_HANDLE && removals != NULL)
                    removal_retry = astra_clock_monotonic() + UINT64_C(1000000000);
                int changed = count != apps.volume_visible ||
                    replacement.epoch != apps.volumes.epoch ||
                    replacement.count != apps.volumes.count ||
                    (replacement.count != 0u && memcmp(replacement.records,
                        apps.volumes.records,
                        (size_t)replacement.count * sizeof(*replacement.records)) != 0);
                if (changed) {
                    result = astra_interface_context_menu_close(&context_menu);
                    if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                    menu_cell = UINT32_MAX;
                    clicked_cell = UINT32_MAX;
                    astra_volume_snapshot_close(&apps.volumes);
                    astra_runtime_deallocate(apps.volume_indices);
                    apps.volumes = replacement;
                    replacement = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
                    apps.volume_indices = indices;
                    indices = NULL;
                    apps.volume_visible = count;
                    if ((uint64_t)count + apps.shown > UINT32_MAX ||
                        layout_icons(count + apps.shown) != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                    log_visible_icons(&apps);
                    if (paint(&content, &apps) != ASTRA_OK) return DESKTOP_FAIL_GRAPHICS;
                }
            }
            astra_volume_snapshot_close(&replacement);
            astra_runtime_deallocate(indices);
        }
        /* Admission can still fail asynchronously in the provider. Preserve
         * mounted authority, report its exact status and permit a fresh retry. */
        if (astra_window_event_wait_handle(&busy_dialog->window) == ASTRA_INVALID_HANDLE) {
            DesktopRemoval **link = &removals;
            while (*link != NULL) {
                DesktopRemoval *failed = *link;
                if (failed->failure == 0u) { link = &failed->next; continue; }
                AstraVolumeReference reference = failed->operation.mount;
                uint32_t failure = failed->failure;
                int refused = failed->operation.state == ASTRA_VOLUME_REMOVAL_REFUSED;
                int eject_failed = failed->eject_failed;
                if (refused || eject_failed) {
                    *link = failed->next;
                    astra_runtime_deallocate(failed);
                } else {
                    failed->failure = 0u;
                    link = &failed->next;
                }
                result = !eject_failed && failure == ASTRA_STATUS_BUSY ?
                    usage_open(busy_dialog, gui->handle, volume_inspector, &process_filesystem, &reference) :
                    astra_result_from_service(failure);
                if (result != ASTRA_OK) volume_error(gui->handle,
                    eject_failed ? "Eject failed; the disc remains in the drive" :
                    refused ? "The storage provider refused to unmount; the volume remains mounted" :
                        "The storage provider could not finish removal; storage resources remain retained", result);
                break;
            }
        }
        if (waited == ASTRA_SYSCALL_OK && selected == busy_index) {
            result = usage_event(busy_dialog, gui->handle);
            if (result != ASTRA_OK) (void)astra_log_failure("desktop busy dialog", (uint32_t)(-result));
            continue;
        }
        result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;

        if (result != ASTRA_OK) return (int)(-result);
        if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
            AstraResult refreshed = astra_fonts_use_system(gui->handle, &event);
            if (refreshed != ASTRA_OK)
                (void)astra_log_failure("desktop font refresh", (uint32_t)refreshed);
            if (astra_interface_ui_handle_event(&icon_ui, &event, &action) != ASTRA_OK ||
                astra_interface_ui_handle_event(&bar_ui, &event, &action) != ASTRA_OK)
                return DESKTOP_FAIL_INTERFACE;
            if (paint(&content, &apps) != ASTRA_OK) return DESKTOP_FAIL_GRAPHICS;
            continue;
        }
        {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
            int handled = 0;
            result = astra_interface_context_menu_handle_event(&context_menu, &event, &handled);
            if (handled) {
                if (result != ASTRA_OK)
                    (void)astra_log_failure("desktop menu command", (uint32_t)(-result));
                continue;
            }
            if (name_edit.active && (event.type == ASTRA_WINDOW_EVENT_KEY ||
                                     event.type == ASTRA_WINDOW_EVENT_TEXT)) {
                result = astra_interface_inline_edit_event(&name_edit, &event, &action);
                if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                if (action.type == ASTRA_UI_ACTION_ACTIVATE ||
                    action.type == ASTRA_UI_ACTION_DISMISS) {
                    result = finish_folder_edit(&command_context,
                        action.type == ASTRA_UI_ACTION_ACTIVATE);
                    if (result != ASTRA_OK)
                        (void)astra_log_failure("desktop folder edit", (uint32_t)(-result));
                } else if (paint(&content, &apps) != ASTRA_OK)
                    return DESKTOP_FAIL_GRAPHICS;
                continue;
            }
            result = astra_interface_ui_handle_event(&icon_ui, &event, &action);
            if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
            if (action.type == ASTRA_UI_ACTION_SCROLL_CHANGED) {
                result = astra_interface_context_menu_close(&context_menu);
                if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                menu_cell = clicked_cell = UINT32_MAX;
                log_visible_icons(&apps);
                if (paint(&content, &apps) != ASTRA_OK) return DESKTOP_FAIL_GRAPHICS;
                continue;
            }
            if (result == ASTRA_OK && action.type == ASTRA_UI_ACTION_CONTEXT_MENU &&
                event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON) {
                AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
                if (astra_scroll_get_state(&icon_scroll, &state) != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                uint32_t cell = astra_desktop_cell_at(event.data.pointer.x, event.data.pointer.y,
                    DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT, state.offset_y);
                menu_cell = cell;
                uint32_t menu_count = 0u;
                memset(menu_parents, 0, sizeof(menu_parents));
                if (cell < apps.volume_visible + apps.shown) {
                    menu_items[menu_count++] = (AstraContextMenuItem){
                        "desktop.open", 12u, &menu_arguments};
                    if (can_disk_setup(&apps, cell))
                        menu_items[menu_count++] = (AstraContextMenuItem){
                            "desktop.disk-setup", 18u, &menu_arguments};
                    if (can_unmount(&apps, cell))
                        menu_items[menu_count++] = (AstraContextMenuItem){
                            "desktop.unmount", 15u, &menu_arguments};
                    if (eject_source(&apps, cell) != NULL)
                        menu_items[menu_count++] = (AstraContextMenuItem){
                            "desktop.eject", 13u, &menu_arguments};
                    if (cell > apps.volume_visible) {
                        menu_items[menu_count++] = (AstraContextMenuItem){
                            "desktop.rename-folder", 21u, &menu_arguments};
                        menu_items[menu_count++] = (AstraContextMenuItem){
                            "desktop.delete-folder", 21u, &menu_arguments};
                    }
                } else {
                    menu_items[menu_count++] = (AstraContextMenuItem){
                        "desktop.new", 11u, NULL};
                    menu_items[menu_count++] = (AstraContextMenuItem){
                        "desktop.new-folder", 18u, NULL};
                    menu_parents[1] = 1u;
                }
                result = astra_interface_context_menu_open_tree(&context_menu, &window,
                    &command_scope, menu_items, menu_parents, menu_count,
                    event.data.pointer.x, event.data.pointer.y);
                if (result != ASTRA_OK) {
                    menu_cell = UINT32_MAX;
                    (void)astra_log_failure("desktop context menu", (uint32_t)(-result));
                } else (void)astra_log("desktop context menu opened");
                continue;
            }
        }
        if (event.type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION &&
            event.data.system_action.action == ASTRA_SYSTEM_ACTION_ABOUT) {
            if (show_about(gui->handle) != ASTRA_OK)
                (void)astra_log("About This Astra could not open");
            continue;
        }
        if (event.type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION &&
            (event.data.system_action.action == ASTRA_SYSTEM_ACTION_SHUTDOWN ||
             event.data.system_action.action == ASTRA_SYSTEM_ACTION_RESTART)) {
            int restart = event.data.system_action.action ==
                ASTRA_SYSTEM_ACTION_RESTART;

            (void)astra_log(restart ? "desktop restart requested" :
                                      "desktop shutdown requested");
            AstraResult result = restart ?
                astra_system_restart_request(manager->handle) :
                astra_system_shutdown_request(manager->handle);

            if (result != ASTRA_OK) {
                (void)astra_log_failure("desktop power request",
                                        (uint32_t)(-result));
                power_error(gui->handle, restart);
            }
            continue;
        }
        if (event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
            (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
            event.data.pointer.button == ASTRA_INPUT_BUTTON_LEFT) {
            AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
            if (astra_scroll_get_state(&icon_scroll, &state) != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
            uint32_t cell = astra_desktop_cell_at(
                event.data.pointer.x, event.data.pointer.y, DESKTOP_GRID_WIDTH,
                DESKTOP_HEIGHT, state.offset_y);

            if (event.data.pointer.click_count != 2u) {
                clicked_cell = cell;
                continue;
            }
            if (cell != clicked_cell) continue;

            if (cell < apps.volume_visible + apps.shown) {
                AstraCommandArguments arguments = {1u, 1u, &cell, sizeof(cell)};
                result = astra_interface_command_invoke(&command_scope, "desktop.open", 12u, &arguments);
                if (result != ASTRA_OK)
                    (void)astra_log_failure("desktop open command", (uint32_t)(-result));
            }
        }
    }
}
