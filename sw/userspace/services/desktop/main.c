#include <astra/application.h>
#include <astra/application_catalog.h>
#include <astra/area.h>
#include <astra/bundle.h>
#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/event_emit.h>
#include <astra/gui.h>
#include <astra/graphics.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/input.h>
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

#define DESKTOP_WIDTH ASTRA_DISPLAY_WIDTH
#define DESKTOP_TOP 34u
#define DESKTOP_BOTTOM (ASTRA_DISPLAY_HEIGHT - 42u)
#define DESKTOP_HEIGHT (DESKTOP_BOTTOM - DESKTOP_TOP)
#define DESKTOP_SCROLLBAR_WIDTH 12u
#define DESKTOP_GRID_WIDTH (DESKTOP_WIDTH - DESKTOP_SCROLLBAR_WIDTH)
#define APPLICATIONS_DIRECTORY "/apps"

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

/* The installed applications and the icon surface each one shows. */
typedef struct DesktopApps {
    AstraApplicationCatalog catalog;
    AstraSurface *icons; /* one per installed entry; unused when closed */
    uint32_t shown; /* entries available in the scrollable grid */
    AstraSurface volume_icon;
    uint32_t volume_visible;
    AstraVolumeSnapshot volumes;
} DesktopApps;

static AstraHandle volume_metadata;
static AstraHandle volume_administrator;
static AstraHandle volume_inspector;
static AstraVolumeWatch volume_watch = ASTRA_VOLUME_WATCH_INIT;
static uint32_t volume_refresh;
static uint64_t volume_retry;
static AstraScrollModel icon_scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraControl icon_controls[3];
static AstraUIContext icon_ui = ASTRA_UI_CONTEXT_INIT;
/* The standard scrollbar painter targets this small raster surface. Input
   remains in icon_ui; both views borrow the same canonical scroll model. */
static AstraControl bar_control;
static AstraUIContext bar_ui = ASTRA_UI_CONTEXT_INIT;
static uint16_t bar_pixels[DESKTOP_SCROLLBAR_WIDTH * DESKTOP_HEIGHT];
static AstraSurface bar_surface = ASTRA_SURFACE_INIT;
typedef struct DesktopRemoval {
    struct DesktopRemoval *next;
    AstraVolumeRemoval operation;
    uint32_t failure;
    uint32_t reported;
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
    surface.flags = ASTRA_SURFACE_CPU_WRITE | ASTRA_SURFACE_DRAW_SOURCE;
    if (result == ASTRA_OK) result = astra_surface_create(display, &surface, &bar_surface);
    return result;
}

static AstraResult read_volumes(AstraVolumeSnapshot *snapshot, uint32_t *count)
{
    AstraResult result = astra_volume_snapshot_open(volume_metadata, snapshot);
    *count = 0u;
    if (result != ASTRA_OK) return result;
    removal_complete(snapshot);
    for (uint32_t at = 0u; at < snapshot->count; ++at) {
        const AstraVolumeRecord *record = &snapshot->records[at];
        if (record->kind == ASTRA_VOLUME_MOUNT &&
            (record->state == ASTRA_VOLUME_MOUNTED || record->state == ASTRA_VOLUME_QUIESCING) &&
            (record->flags & ASTRA_VOLUME_TEMPORARY) == 0u)
            snapshot->records[(*count)++] = *record;
    }
    return ASTRA_OK;
}

ASTRA_PROGRAM("desktop", 0, 5, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;

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
    const DesktopApps *apps;
    uint32_t gui;
    uint32_t launcher;
    uint32_t *selection;
    DesktopUsage *usage;
} DesktopCommands;

static int can_unmount(const DesktopApps *apps, uint32_t cell)
{
    if (cell >= apps->volume_visible) return 0;
    const AstraVolumeRecord *record = &apps->volumes.records[cell];
    if (record->kind != ASTRA_VOLUME_MOUNT || record->state != ASTRA_VOLUME_MOUNTED ||
        (record->flags & (ASTRA_VOLUME_SYSTEM | ASTRA_VOLUME_TEMPORARY)) != 0u) return 0;
    for (DesktopRemoval *at = removals; at != NULL; at = at->next)
        if (at->operation.mount.epoch == apps->volumes.epoch &&
            at->operation.mount.id == record->id &&
            at->operation.mount.generation == record->generation) return 0;
    return 1;
}

static AstraResult unmount_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Unmount", 7u,
        volume_administrator != 0u && volume_inspector != 0u && can_unmount(context->apps, *context->selection), 0u};
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
        int same_provider = pending->operation.state == ASTRA_VOLUME_REMOVAL_COMPLETE;
        *link = pending->next; astra_runtime_deallocate(pending);
        (void)astra_log(same_provider ? "desktop volume unmounted" : "desktop volume provider retired");
    }
    removal_retry = astra_clock_monotonic() + UINT64_C(50000000);
}

static AstraResult unmount_icon(void *opaque, const AstraCommandArguments *args)
{
    const DesktopCommands *context = opaque;
    uint32_t cell;
    if (args == NULL || args->length != sizeof(cell)) return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(&cell, args->bytes, sizeof(cell));
    if (!can_unmount(context->apps, cell)) return ASTRA_ERROR_INVALID_HANDLE;
    const AstraVolumeRecord *record = &context->apps->volumes.records[cell];
    DesktopRemoval *pending = astra_runtime_callocate(1u, sizeof(*pending));
    if (pending == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    pending->operation.mount = (AstraVolumeReference){context->apps->volumes.epoch,
        record->id, record->generation};
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
        (void)astra_log("desktop volume unmount admitted");
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

static AstraResult open_query(void *opaque, AstraCommandState *state)
{
    const DesktopCommands *context = opaque;
    *state = (AstraCommandState){"Open", 4u,
        context->apps->shown + context->apps->volume_visible != 0u, 0u};
    return ASTRA_OK;
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
        const AstraVolumeRecord *record = &apps->volumes.records[cell];
        char epoch[24], id[24], generation[24];
        AstraString number;
        astra_string_init(&number, epoch, sizeof(epoch));
        (void)astra_string_append_u64(&number, apps->volumes.epoch);
        astra_string_init(&number, id, sizeof(id));
        (void)astra_string_append_u64(&number, record->id);
        astra_string_init(&number, generation, sizeof(generation));
        (void)astra_string_append_u64(&number, record->generation);
        const char *arguments[] = {"--volume", epoch, id, generation};
        result = astra_application_launch_with_arguments(context->launcher,
            "/apps/Atlas.app", sizeof("/apps/Atlas.app") - 1u,
            ASTRA_LAUNCH_SOURCE_DESKTOP, arguments, 4u, &process_id);
        if (result != ASTRA_OK)
            launch_error(context->gui, "Atlas", "/apps/Atlas.app", result);
    } else if (cell >= apps->volume_visible && cell - apps->volume_visible < apps->shown) {
        const AstraApplicationEntry *entry = &apps->catalog.entries[cell - apps->volume_visible];
        result = astra_application_launch(context->launcher, entry->bundle,
            (uint16_t)strlen(entry->bundle), &process_id);
        if (result != ASTRA_OK) launch_error(context->gui, entry->name, entry->bundle, result);
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

/* One icon as an ARGB8888 surface of the desktop window: uploaded once,
   blended by the blitter on every repaint. */
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

static void load_apps(AstraDisplay *display, DesktopApps *apps)
{
    uint32_t status = astra_application_catalog_load(
        &process_filesystem, APPLICATIONS_DIRECTORY, &apps->catalog);
    apps->volume_icon = (AstraSurface)ASTRA_SURFACE_INIT;
    AstraResult observed = read_volumes(&apps->volumes, &apps->volume_visible);
    if (observed != ASTRA_OK) {
        (void)astra_log_failure("desktop volume catalog", (uint32_t)(-observed));
        volume_refresh = 1u;
        volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
    }
    if (load_icon(display, "/apps/Atlas.app/resources/volume.aicon", &apps->volume_icon) != ASTRA_OK)
        (void)astra_log("desktop volume icon unavailable");

    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("desktop application catalog", status);
    if (apps->catalog.skipped != 0u)
        (void)astra_log_failure("desktop bundles skipped",
                                apps->catalog.skipped);
    apps->shown = apps->catalog.count;
    if (apps->catalog.count == 0u)
        return;
    apps->icons = astra_runtime_allocate(
        (size_t)apps->catalog.count * sizeof(*apps->icons));
    if (apps->icons == NULL) {
        (void)astra_log("desktop icons: no memory");
        apps->shown = 0u;
        return;
    }
    for (uint32_t at = 0u; at < apps->catalog.count; ++at) {
        apps->icons[at] = (AstraSurface)ASTRA_SURFACE_INIT;
        if (load_icon(display, apps->catalog.entries[at].icon,
                      &apps->icons[at]) != ASTRA_OK)
            (void)astra_log(apps->catalog.entries[at].icon);
    }
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
        log_icon(at < apps->volume_visible ? apps->volumes.records[at].name :
            apps->catalog.entries[at - apps->volume_visible].bundle,
            dx, (int32_t)dy - (int32_t)state.offset_y);
    }
}

/* The whole desktop in one list: canvas, then each icon and its label. */
static AstraResult paint(AstraDisplay *display, AstraSurface *content,
                         const DesktopApps *apps)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraDrawPaint canvas = ASTRA_DRAW_PAINT_INIT;
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    const AstraRectI32 whole = { 0, 0, DESKTOP_WIDTH, DESKTOP_HEIGHT };
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraResult result;

    result = astra_scroll_get_state(&icon_scroll, &state);
    if (result != ASTRA_OK) return result;
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
            const char *name = apps->volumes.records[at].name + 1u;
            uint32_t byte = 0u;
            for (; name[byte] != '\0' && byte + 1u < sizeof(fallback); ++byte)
                fallback[byte] = astra_ascii_upper(name[byte]);
            fallback[byte] = '\0';
        }
        const char *label = is_volume != 0u ?
            (apps->volumes.records[at].label[0] != '\0' ? apps->volumes.records[at].label : fallback) :
            apps->catalog.entries[at - apps->volume_visible].name;
        const AstraSurface *icon = is_volume != 0u ? &apps->volume_icon :
            &apps->icons[at - apps->volume_visible];
        /* As window titles do, a label longer than its cell is clipped at
           a whole scalar. */
        uint32_t length = astra_surface_ui_text_fit(
            label, (uint32_t)strlen(label), ASTRA_DESKTOP_LABEL_FONT_HEIGHT,
            ASTRA_DESKTOP_ICON_CELL_WIDTH);
        uint32_t dx;
        uint32_t dy;

        astra_desktop_cell_offset(at, DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT, &dx, &dy);
        int32_t top = (int32_t)dy - (int32_t)state.offset_y;
        if ((int64_t)ASTRA_DESKTOP_GRID_TOP + top + ASTRA_DESKTOP_CELL_HEIGHT <= 0 ||
            (int64_t)ASTRA_DESKTOP_GRID_TOP + top >= DESKTOP_HEIGHT) continue;
        if (icon->_private_handle != ASTRA_INVALID_HANDLE)
            result = astra_draw_blit(
                &list, icon,
                &(AstraRectI32){ 0, 0, ASTRA_DESKTOP_ICON_SIZE,
                                 ASTRA_DESKTOP_ICON_SIZE },
                &(AstraRectI32){ (int32_t)(ASTRA_DESKTOP_ICON_X + dx),
                                 (int32_t)ASTRA_DESKTOP_ICON_Y + top,
                                 ASTRA_DESKTOP_ICON_SIZE,
                                 ASTRA_DESKTOP_ICON_SIZE },
                &blend);
        if (result == ASTRA_OK && length != 0u)
            result = astra_draw_ui_text(
                &list,
                (AstraPointI32){
                    astra_desktop_centered_label_x(
                        astra_surface_ui_text_width(
                            label, length,
                            ASTRA_DESKTOP_LABEL_FONT_HEIGHT)) +
                        (int32_t)dx,
                    (int32_t)ASTRA_DESKTOP_LABEL_Y + top },
                label, length, ASTRA_DESKTOP_LABEL_FONT_HEIGHT, 0u,
                theme.text_primary);
    }
    if (result == ASTRA_OK && state.maximum_y != 0u) {
        AstraSurfaceView raster;
        if (!astra_surface_view_init(&raster, bar_pixels, sizeof(bar_pixels),
                DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT, DESKTOP_SCROLLBAR_WIDTH * 2u))
            result = ASTRA_ERROR_INVALID_ARGUMENT;
        else {
            astra_surface_clear(&raster, astra_surface_rgb565(
                theme.canvas.red, theme.canvas.green, theme.canvas.blue));
            result = astra_interface_ui_render(&bar_ui, &raster);
        }
        if (result == ASTRA_OK) result = astra_surface_write(display, &bar_surface,
            &(AstraRectI32){0, 0, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT},
            bar_pixels, DESKTOP_SCROLLBAR_WIDTH * 2u);
        if (result == ASTRA_OK) result = astra_draw_list_set_clip(&list, &whole);
        if (result == ASTRA_OK) result = astra_draw_blit(&list, &bar_surface,
            &(AstraRectI32){0, 0, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT},
            &(AstraRectI32){DESKTOP_GRID_WIDTH, 0, DESKTOP_SCROLLBAR_WIDTH, DESKTOP_HEIGHT},
            &blend);
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
    return result;
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraArea placeholder = ASTRA_AREA_INIT;
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface content = ASTRA_SURFACE_INIT;
    DesktopApps apps = {.catalog = ASTRA_APPLICATION_CATALOG_INIT,
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
    const AstraContextMenuItem menu_items[] = {{"desktop.open", 12u, &menu_arguments},
        {"desktop.unmount", 15u, &menu_arguments}};
    DesktopCommands command_context = {&apps, 0u, 0u, &menu_cell, NULL};
    const AstraCommand commands[] = {{"desktop.open", 12u, 1u, 1u, open_query, open_icon, &command_context},
        {"desktop.unmount", 15u, 1u, 1u, unmount_query, unmount_icon, &command_context}};
    const AstraCommandScope command_scope = {NULL, commands, 2u};

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
            load_apps(&display, &apps);
            result = (uint64_t)apps.shown + apps.volume_visible > UINT32_MAX ?
                ASTRA_ERROR_NO_RESOURCES : layout_icons(apps.shown + apps.volume_visible);
        }
        if (result == ASTRA_OK) {
            log_visible_icons(&apps);
            result = paint(&display, &content, &apps);
        }
        if (result == ASTRA_OK)
            result = astra_window_present(&window);
        if (result != ASTRA_OK)
            status = DESKTOP_FAIL_GRAPHICS + (uint32_t)(-result);
    }
    (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) return (int)status;
    ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_DISPLAY, ASTRA_EVENT_LEVEL_INFO,
                 "desktop ready");
    if (pcm != NULL) {
        AstraThreadStart sound_start = {play_startup_sound, pcm->handle};

        if (astra_rt_thread_start_detached(
                &sound_start, ASTRA_PROCESS_PRIORITY_NORMAL - 4u) !=
            ASTRA_SYSCALL_OK)
            (void)astra_log("startup sound thread could not start");
    }
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
            result = volume_watch.receive == ASTRA_INVALID_HANDLE ? ASTRA_ERROR_PEER_DEAD : read_volumes(&replacement, &count);
            if (result != ASTRA_OK && volume_watch.receive != ASTRA_INVALID_HANDLE) {
                volume_retry = astra_clock_monotonic() + UINT64_C(50000000);
                if (removals != NULL) removal_retry = volume_retry;
            } else {
                volume_refresh = 0u;
                if (volume_watch.receive == ASTRA_INVALID_HANDLE && removals != NULL)
                    removal_retry = astra_clock_monotonic() + UINT64_C(1000000000);
                int changed = count != apps.volume_visible || replacement.epoch != apps.volumes.epoch ||
                    (count != 0u && memcmp(replacement.records, apps.volumes.records, count * sizeof(*replacement.records)) != 0);
                if (changed) {
                    result = astra_interface_context_menu_close(&context_menu);
                    if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                    menu_cell = UINT32_MAX;
                    clicked_cell = UINT32_MAX;
                    astra_volume_snapshot_close(&apps.volumes);
                    apps.volumes = replacement;
                    replacement = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
                    apps.volume_visible = count;
                    if ((uint64_t)count + apps.shown > UINT32_MAX ||
                        layout_icons(count + apps.shown) != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                    log_visible_icons(&apps);
                    if (paint(&display, &content, &apps) != ASTRA_OK) return DESKTOP_FAIL_GRAPHICS;
                }
            }
            astra_volume_snapshot_close(&replacement);
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
                if (refused) {
                    *link = failed->next;
                    astra_runtime_deallocate(failed);
                } else {
                    failed->failure = 0u;
                    link = &failed->next;
                }
                result = failure == ASTRA_STATUS_BUSY ?
                    usage_open(busy_dialog, gui->handle, volume_inspector, &process_filesystem, &reference) :
                    astra_result_from_service(failure);
                if (result != ASTRA_OK) volume_error(gui->handle,
                    refused ? "The storage provider refused to unmount; the volume remains mounted" :
                        "The storage provider could not finish removal; storage resources remain retained", result);
                break;
            }
        }
        if (waited == ASTRA_SYSCALL_OK && selected == busy_index) {
            result = usage_event(busy_dialog);
            if (result != ASTRA_OK) (void)astra_log_failure("desktop busy dialog", (uint32_t)(-result));
            continue;
        }
        result = astra_window_event_try(&window, &event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;

        if (result != ASTRA_OK) return (int)(-result);
        {
            AstraUIAction action = ASTRA_UI_ACTION_INIT;
            int handled = 0;
            result = astra_interface_context_menu_handle_event(&context_menu, &event, &handled);
            if (handled) {
                if (result != ASTRA_OK)
                    (void)astra_log_failure("desktop menu command", (uint32_t)(-result));
                continue;
            }
            result = astra_interface_ui_handle_event(&icon_ui, &event, &action);
            if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
            if (action.type == ASTRA_UI_ACTION_SCROLL_CHANGED) {
                result = astra_interface_context_menu_close(&context_menu);
                if (result != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                menu_cell = clicked_cell = UINT32_MAX;
                log_visible_icons(&apps);
                if (paint(&display, &content, &apps) != ASTRA_OK) return DESKTOP_FAIL_GRAPHICS;
                continue;
            }
            if (result == ASTRA_OK && action.type == ASTRA_UI_ACTION_CONTEXT_MENU &&
                event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON) {
                AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
                if (astra_scroll_get_state(&icon_scroll, &state) != ASTRA_OK) return DESKTOP_FAIL_INTERFACE;
                uint32_t cell = astra_desktop_cell_at(event.data.pointer.x, event.data.pointer.y,
                    DESKTOP_GRID_WIDTH, DESKTOP_HEIGHT, state.offset_y);
                if (cell < apps.volume_visible + apps.shown) {
                    menu_cell = cell;
                    result = astra_interface_context_menu_open(&context_menu, &window,
                        &command_scope, menu_items, can_unmount(&apps, cell) ? 2u : 1u,
                        event.data.pointer.x, event.data.pointer.y);
                    if (result != ASTRA_OK) {
                        menu_cell = UINT32_MAX;
                        (void)astra_log_failure("desktop context menu", (uint32_t)(-result));
                    } else (void)astra_log("desktop context menu opened");
                }
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
