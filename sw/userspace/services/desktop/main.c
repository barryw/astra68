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
#include <astra/service.h>
#include <astra/service_manager.h>
#include <astra/status.h>
#include <astra/string.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/text_style.h>
#include <astra/window.h>

#include "desktop_layout.h"

#define DESKTOP_WIDTH ASTRA_DISPLAY_WIDTH
#define DESKTOP_TOP 34u
#define DESKTOP_BOTTOM (ASTRA_DISPLAY_HEIGHT - 42u)
#define DESKTOP_HEIGHT (DESKTOP_BOTTOM - DESKTOP_TOP)
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
    AstraSurface *icons; /* one per shown entry; unused when closed */
    uint32_t shown; /* entries that fit the grid */
} DesktopApps;

ASTRA_PROGRAM("desktop", 0, 4, 0, "Barry Walker",
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

static void launch_error(uint32_t gui, const AstraApplicationEntry *entry,
                         AstraResult failure)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    AstraResult alert_result;
    char storage[ASTRA_BUNDLE_NAME_MAX + 64u];
    AstraString message;

    (void)astra_log_failure("desktop launch", (uint32_t)(-failure));
    (void)astra_log(entry->bundle);
    astra_string_init(&message, storage, sizeof(storage));
    if (failure == ASTRA_ERROR_NO_RESOURCES)
        (void)astra_string_append(
            &message, "There are not enough resources to start ");
    (void)astra_string_append(&message, entry->name);
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
static AstraResult load_icon(AstraDisplay *display,
                             const AstraApplicationEntry *entry,
                             AstraSurface *surface)
{
    static uint32_t pixels[ASTRA_DESKTOP_ICON_SIZE * ASTRA_DESKTOP_ICON_SIZE];
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraAicon icon;
    AstraAiconStrike strike;
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    AstraResult result;

    if (astra_process_read_file_alloc(&process_filesystem, entry->icon,
                                      (void **)&bytes, &length) !=
            ASTRA_VFS_OK ||
        astra_aicon_open(bytes, length, &icon) != ASTRA_BUNDLE_OK ||
        astra_aicon_strike(&icon, ASTRA_DESKTOP_ICON_SIZE, &strike) !=
            ASTRA_BUNDLE_OK ||
        astra_aicon_strike_argb(&icon, &strike, pixels,
                                ASTRA_DESKTOP_ICON_SIZE) != ASTRA_BUNDLE_OK) {
        astra_runtime_deallocate(bytes);
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    astra_runtime_deallocate(bytes);
    create.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    create.width = ASTRA_DESKTOP_ICON_SIZE;
    create.height = ASTRA_DESKTOP_ICON_SIZE;
    create.format = ASTRA_PIXEL_FORMAT_ARGB8888;
    result = astra_surface_create(display, &create, surface);
    if (result == ASTRA_OK)
        result = astra_surface_write(
            display, surface,
            &(AstraRectI32){ 0, 0, ASTRA_DESKTOP_ICON_SIZE,
                             ASTRA_DESKTOP_ICON_SIZE },
            pixels, ASTRA_DESKTOP_ICON_SIZE * 4u);
    if (result != ASTRA_OK &&
        surface->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_surface_close(surface);

        (void)ignored;
    }
    return result;
}

/* "desktop icon /apps/X.app LEFT TOP WIDTH HEIGHT" in screen pixels: where
   a person, or a gate, double-clicks to open that application. */
static void log_icon(const AstraApplicationEntry *entry, uint32_t dx,
                     uint32_t dy)
{
    char storage[ASTRA_APPLICATION_PATH_MAX + 64u];
    AstraString line;
    const uint32_t box[4] = {
        ASTRA_DESKTOP_ICON_CELL_LEFT + dx,
        DESKTOP_TOP + ASTRA_DESKTOP_GRID_TOP + dy,
        ASTRA_DESKTOP_ICON_CELL_WIDTH, ASTRA_DESKTOP_CELL_HEIGHT
    };

    astra_string_init(&line, storage, sizeof(storage));
    (void)astra_string_append(&line, "desktop icon ");
    (void)astra_string_append(&line, entry->bundle);
    for (uint32_t at = 0u; at < 4u; ++at) {
        (void)astra_string_append_char(&line, ' ');
        (void)astra_string_append_u64(&line, box[at]);
    }
    (void)astra_log(storage);
}

static void load_apps(AstraDisplay *display, DesktopApps *apps)
{
    uint32_t status = astra_application_catalog_load(
        &process_filesystem, APPLICATIONS_DIRECTORY, &apps->catalog);
    uint32_t capacity = astra_desktop_capacity(DESKTOP_WIDTH, DESKTOP_HEIGHT);

    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("desktop application catalog", status);
    if (apps->catalog.skipped != 0u)
        (void)astra_log_failure("desktop bundles skipped",
                                apps->catalog.skipped);
    apps->shown = apps->catalog.count < capacity ? apps->catalog.count :
                                                   capacity;
    if (apps->catalog.count > capacity)
        (void)astra_log_failure("desktop applications not shown",
                                apps->catalog.count - capacity);
    if (apps->shown == 0u)
        return;
    apps->icons = astra_runtime_allocate(
        (size_t)apps->shown * sizeof(*apps->icons));
    if (apps->icons == NULL) {
        (void)astra_log("desktop icons: no memory");
        apps->shown = 0u;
        return;
    }
    for (uint32_t at = 0u; at < apps->shown; ++at) {
        uint32_t dx;
        uint32_t dy;

        apps->icons[at] = (AstraSurface)ASTRA_SURFACE_INIT;
        if (load_icon(display, &apps->catalog.entries[at],
                      &apps->icons[at]) != ASTRA_OK)
            (void)astra_log(apps->catalog.entries[at].icon);
        astra_desktop_cell_offset(at, DESKTOP_HEIGHT, &dx, &dy);
        log_icon(&apps->catalog.entries[at], dx, dy);
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
    AstraResult result;

    (void)display;
    canvas.foreground = theme.canvas;
    blend.blend = ASTRA_BLEND_ALPHA;
    result = astra_draw_list_create(content, &whole, &list);
    if (result == ASTRA_OK)
        result = astra_draw_rectangle(&list, &whole, 1, &canvas);
    for (uint32_t at = 0u; result == ASTRA_OK && at < apps->shown; ++at) {
        const AstraApplicationEntry *entry = &apps->catalog.entries[at];
        const char *label = entry->name;
        /* As window titles do, a label longer than its cell is clipped at
           a whole scalar. */
        uint32_t length = astra_surface_ui_text_fit(
            label, (uint32_t)strlen(label), ASTRA_DESKTOP_LABEL_FONT_HEIGHT,
            ASTRA_DESKTOP_ICON_CELL_WIDTH);
        uint32_t dx;
        uint32_t dy;

        astra_desktop_cell_offset(at, DESKTOP_HEIGHT, &dx, &dy);
        if (apps->icons[at]._private_handle != ASTRA_INVALID_HANDLE)
            result = astra_draw_blit(
                &list, &apps->icons[at],
                &(AstraRectI32){ 0, 0, ASTRA_DESKTOP_ICON_SIZE,
                                 ASTRA_DESKTOP_ICON_SIZE },
                &(AstraRectI32){ (int32_t)(ASTRA_DESKTOP_ICON_X + dx),
                                 (int32_t)(ASTRA_DESKTOP_ICON_Y + dy),
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
                    (int32_t)(ASTRA_DESKTOP_LABEL_Y + dy) },
                label, length, ASTRA_DESKTOP_LABEL_FONT_HEIGHT, 0u,
                theme.text_primary);
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
    DesktopApps apps = { ASTRA_APPLICATION_CATALOG_INIT, NULL, 0u };
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    const AstraStartupCapability *launcher;
    const AstraStartupCapability *manager;
    const AstraStartupCapability *pcm;
    uint32_t status;

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
    if (bootstrap == NULL || gui == NULL || launcher == NULL ||
        manager == NULL || (manager->rights & ASTRA_RIGHT_SIGNAL) == 0u)
        return ASTRA_STATUS_BAD_HANDLE;
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

        load_apps(&display, &apps);
        result = paint(&display, &content, &apps);
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
        AstraResult result = astra_window_event_wait(
            &window, &event, ASTRA_DEADLINE_INFINITE);

        if (result != ASTRA_OK) return (int)(-result);
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
            event.data.pointer.button == ASTRA_INPUT_BUTTON_LEFT &&
            event.data.pointer.click_count == 2u) {
            uint32_t cell = astra_desktop_cell_at(
                event.data.pointer.x, event.data.pointer.y, DESKTOP_WIDTH,
                DESKTOP_HEIGHT);

            if (cell < apps.shown) {
                const AstraApplicationEntry *entry =
                    &apps.catalog.entries[cell];
                uint32_t process_id;
                AstraResult launch_result = astra_application_launch(
                    launcher->handle, entry->bundle,
                    (uint16_t)strlen(entry->bundle), &process_id);

                if (launch_result != ASTRA_OK)
                    launch_error(gui->handle, entry, launch_result);
            }
        }
    }
}
