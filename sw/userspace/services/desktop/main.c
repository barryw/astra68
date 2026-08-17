#include <astra/application.h>
#include <astra/bundle.h>
#include <astra/gui.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/input.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#define DESKTOP_WIDTH 1280u
#define DESKTOP_TOP 34u
#define DESKTOP_BOTTOM 678u
#define DESKTOP_HEIGHT (DESKTOP_BOTTOM - DESKTOP_TOP)
#define TERMINAL_BUNDLE "APPS:Terminal.app"
#define ICON_BYTES_MAX 8192u
#define TERMINAL_ICON_LEFT 32
#define TERMINAL_ICON_TOP 28
#define TERMINAL_ICON_RIGHT 112
#define TERMINAL_ICON_BOTTOM 132

enum {
    DESKTOP_FAIL_FILESYSTEM = ASTRA_STATUS_PROGRAM_FIRST,
    DESKTOP_FAIL_INTERFACE,
    DESKTOP_FAIL_GRAPHICS,
    DESKTOP_FAIL_MANIFEST,
    DESKTOP_FAIL_ICON,
    DESKTOP_FAIL_SURFACE,
    DESKTOP_FAIL_WINDOW
};

typedef struct IconRun {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t matched;
} IconRun;

ASTRA_PROGRAM("desktop", 0, 3, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraLibraryHandle *graphics_handle;
static AstraLibraryHandle *interface_handle;
static const AstraGraphicsLibraryV1 *graphics_library;
static const AstraInterfaceLibraryV1 *interface_library;
static char manifest_text[ASTRA_BUNDLE_MANIFEST_MAX + 1u];
static uint8_t icon_bytes[ICON_BYTES_MAX];

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup, const char *name)
{
    const AstraStartupCapability *entries =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;

    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(entries[index].name, name))
            return &entries[index];
    return NULL;
}

static void ready(uint32_t bootstrap, uint32_t status)
{
    AstraServiceReady message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    (void)astra_port_send(bootstrap, &message, sizeof(message), NULL, 0u);
}

static int append(char *path, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (at < capacity && path[at] != '\0') ++at;
    while (*text != '\0') {
        if (at + 1u >= capacity) return 0;
        path[at++] = *text++;
    }
    path[at] = '\0';
    return 1;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static void launch_error(uint32_t gui, AstraResult failure)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    const char *message = failure == ASTRA_ERROR_NO_RESOURCES ?
        "There are not enough resources to start Terminal." :
        "Terminal could not be started.";

    if (interface_library == NULL) return;
    (void)astra_log(failure == ASTRA_ERROR_NO_RESOURCES ?
                    "Terminal launch: no resources" :
                    "Terminal launch: request failed");
    info.kind = ASTRA_ALERT_ERROR;
    info.title = "Application Error";
    info.title_length = 17u;
    info.message = message;
    info.message_length = (uint16_t)text_length(message);
    info.button = "OK";
    info.button_length = 2u;
    (void)interface_library->show_alert(gui, &info);
}

static uint16_t icon_color(const AstraAicon *icon, uint16_t index)
{
    uint8_t rgba[4];

    if (graphics_library->aicon_palette(icon, index, rgba) != ASTRA_BUNDLE_OK)
        return 0u;
    return astra_surface_rgb565(rgba[0], rgba[1], rgba[2]);
}

static void flush_run(AstraSurfaceView *surface, const AstraAicon *icon,
                      uint16_t color_index, const IconRun *run)
{
    astra_surface_fill(surface, 38 + run->x, 34 + run->y,
                       run->width, run->height,
                       icon_color(icon, color_index));
}

/* Vertical coalescing keeps the 64px indexed icon inside 128 draw commands. */
static int draw_strike(AstraSurfaceView *surface, const AstraAicon *icon,
                       const AstraAiconStrike *strike)
{
    for (uint16_t color = 1u; color < icon->palette_count; ++color) {
        IconRun active[32];
        uint32_t active_count = 0u;

        for (uint16_t y = 0u; y < strike->height; ++y) {
            IconRun current[32];
            uint32_t current_count = 0u;
            uint16_t x = 0u;

            for (uint32_t at = 0u; at < active_count; ++at)
                active[at].matched = 0u;
            while (x < strike->width) {
                uint16_t start;
                uint32_t match = active_count;

                while (x < strike->width &&
                       strike->pixels[(uint32_t)y * strike->width + x] != color)
                    ++x;
                start = x;
                while (x < strike->width &&
                       strike->pixels[(uint32_t)y * strike->width + x] == color)
                    ++x;
                if (start == x) break;
                for (uint32_t at = 0u; at < active_count; ++at)
                    if (active[at].x == start &&
                        active[at].width == x - start &&
                        active[at].matched == 0u) {
                        match = at;
                        break;
                    }
                if (current_count == sizeof(current) / sizeof(current[0]))
                    return 0;
                if (match != active_count) {
                    active[match].matched = 1u;
                    ++active[match].height;
                    current[current_count++] = active[match];
                } else {
                    current[current_count++] = (IconRun){
                        start, y, (uint16_t)(x - start), 1u, 1u};
                }
            }
            for (uint32_t at = 0u; at < active_count; ++at)
                if (active[at].matched == 0u)
                    flush_run(surface, icon, color, &active[at]);
            active_count = current_count;
            for (uint32_t at = 0u; at < current_count; ++at)
                active[at] = current[at];
        }
        for (uint32_t at = 0u; at < active_count; ++at)
            flush_run(surface, icon, color, &active[at]);
    }
    return 1;
}

static uint32_t paint(AstraSurfaceView *surface)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraBundleManifest manifest;
    AstraAicon icon;
    AstraAiconStrike strike;
    char icon_path[ASTRA_VFS_PATH_MAX] = TERMINAL_BUNDLE "/";
    uint32_t length = 0u;
    uint32_t line = 0u;

    astra_surface_clear(surface, astra_surface_rgb565(
        theme.canvas.red, theme.canvas.green, theme.canvas.blue));
    if (astra_process_read_file(
            &process_filesystem, TERMINAL_BUNDLE "/manifest", manifest_text,
            ASTRA_BUNDLE_MANIFEST_MAX, &length) != ASTRA_VFS_OK)
        return DESKTOP_FAIL_MANIFEST;
    manifest_text[length] = '\0';
    if (astra_bundle_manifest_parse(manifest_text, length, &manifest, &line) !=
            ASTRA_BUNDLE_OK || !append(icon_path, sizeof(icon_path),
                                      manifest.icon))
        return DESKTOP_FAIL_MANIFEST;
    if (astra_process_read_file(&process_filesystem, icon_path, icon_bytes,
                                sizeof(icon_bytes), &length) !=
            ASTRA_VFS_OK || graphics_library->aicon_open(
                icon_bytes, length, &icon) != ASTRA_BUNDLE_OK ||
            graphics_library->aicon_strike(&icon, 64u, &strike) !=
                ASTRA_BUNDLE_OK)
        return DESKTOP_FAIL_ICON;
    if (!draw_strike(surface, &icon, &strike)) return DESKTOP_FAIL_ICON;
    astra_surface_ui_text(surface, 40, 104, manifest.name,
                          text_length(manifest.name),
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT,
                          astra_surface_rgb565(theme.text_primary.red,
                                               theme.text_primary.green,
                                               theme.text_primary.blue));
    return ASTRA_STATUS_OK;
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraSharedSurface surface = {0};
    AstraWindow window = ASTRA_WINDOW_INIT;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    const AstraStartupCapability *launcher;
    uint32_t status;

    if (!astra_startup_validate(startup) || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    gui = capability(startup, ASTRA_CAPABILITY_GUI);
    launcher = capability(startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    if (bootstrap == NULL || gui == NULL || launcher == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_STATUS_OK) status = DESKTOP_FAIL_FILESYSTEM;
    if (status == ASTRA_STATUS_OK) {
        interface_handle = OpenLibrary(ASTRA_INTERFACE_LIBRARY_NAME,
                                       ASTRA_INTERFACE_LIBRARY_VERSION);
        if (interface_handle == NULL) status = DESKTOP_FAIL_INTERFACE;
        else {
            interface_library = interface_handle->exports;
            if (interface_library->abi_major !=
                    ASTRA_INTERFACE_LIBRARY_ABI_MAJOR ||
                interface_library->structure_size <
                    sizeof(*interface_library))
                status = DESKTOP_FAIL_INTERFACE;
        }
    }
    if (status == ASTRA_STATUS_OK) {
        graphics_handle = OpenLibrary(ASTRA_GRAPHICS_LIBRARY_NAME,
                                      ASTRA_GRAPHICS_LIBRARY_VERSION);
        if (graphics_handle == NULL)
            status = DESKTOP_FAIL_GRAPHICS;
        else {
            graphics_library = graphics_handle->exports;
            if (graphics_library->abi_major !=
                    ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR ||
                graphics_library->abi_minor <
                    ASTRA_GRAPHICS_LIBRARY_ABI_MINOR ||
                graphics_library->structure_size < sizeof(*graphics_library))
                status = DESKTOP_FAIL_GRAPHICS;
        }
    }
    if (status == ASTRA_STATUS_OK &&
        astra_shared_draw_list_create(&surface, DESKTOP_WIDTH,
                                      DESKTOP_HEIGHT) != ASTRA_SYSCALL_OK)
        status = DESKTOP_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) status = paint(&surface.view);
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        AstraResult result;

        info.x = 0u;
        info.y = DESKTOP_TOP;
        info.width = DESKTOP_WIDTH;
        info.height = DESKTOP_HEIGHT;
        info.flags = 0u;
        info.gadgets = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.type = ASTRA_WINDOW_DESKTOP;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON;
        result = astra_window_create(gui->handle, surface.area, &info, &window);
        if (result != ASTRA_OK)
            status = DESKTOP_FAIL_WINDOW + (uint32_t)(-result);
    }
    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) return (int)status;
    for (;;) {
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_wait(
            &window, &event, ASTRA_DEADLINE_INFINITE);

        if (result != ASTRA_OK) return (int)(-result);
        if (event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
            (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
            event.data.pointer.button == ASTRA_INPUT_BUTTON_LEFT &&
            event.data.pointer.click_count == 2u &&
            event.data.pointer.x >= TERMINAL_ICON_LEFT &&
            event.data.pointer.x < TERMINAL_ICON_RIGHT &&
            event.data.pointer.y >= TERMINAL_ICON_TOP &&
            event.data.pointer.y < TERMINAL_ICON_BOTTOM) {
            uint32_t process_id;
            AstraResult launch_result;

            launch_result = astra_application_launch(
                launcher->handle, TERMINAL_BUNDLE,
                (uint16_t)(sizeof(TERMINAL_BUNDLE) - 1u), &process_id);
            if (launch_result != ASTRA_OK) {
                launch_error(gui->handle, launch_result);
            }
        }
    }
}
