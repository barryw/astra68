#include <astra/gui.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/window.h>

#define GALLERY_WINDOW_COUNT 4u

enum {
    DESKTOP_FAIL_SURFACE_FIRST = ASTRA_STATUS_PROGRAM_FIRST,
    DESKTOP_FAIL_WINDOW_FIRST =
        ASTRA_STATUS_PROGRAM_FIRST + GALLERY_WINDOW_COUNT,
    DESKTOP_FAIL_MANAGE = ASTRA_STATUS_PROGRAM_FIRST + 0x80u
};

typedef struct GalleryWindow {
    AstraSharedSurface surface;
    AstraWindow window;
} GalleryWindow;

typedef struct GallerySpec {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t flags;
    uint32_t gadgets;
    uint8_t type;
    uint8_t close_state;
    uint8_t minimize_state;
    uint8_t maximize_state;
    const char *title;
    uint16_t title_length;
    const char *label;
    uint16_t label_length;
} GallerySpec;

static const GallerySpec gallery[GALLERY_WINDOW_COUNT] = {
    { 300, 380, 250, 125, 0, 0, ASTRA_WINDOW_POPOVER,
      ASTRA_GADGET_NORMAL, ASTRA_GADGET_NORMAL, ASTRA_GADGET_NORMAL,
      0, 0, "POPOVER / BORDER ONLY", 21 },
    { 600, 80, 360, 145, 0, ASTRA_WINDOW_GADGET_CLOSE,
      ASTRA_WINDOW_UTILITY, ASTRA_GADGET_FOCUSED, ASTRA_GADGET_NORMAL,
      ASTRA_GADGET_NORMAL, "INSPECTOR", 9, "UTILITY / FOCUSED", 17 },
    { 520, 300, 400, 190, ASTRA_WINDOW_MODAL,
      ASTRA_WINDOW_GADGET_CLOSE, ASTRA_WINDOW_DIALOG,
      ASTRA_GADGET_DISABLED, ASTRA_GADGET_NORMAL, ASTRA_GADGET_NORMAL,
      "SAVE CHANGES?", 13, "DIALOG / DISABLED", 17 },
    { 100, 100, 550, 280, ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE,
      ASTRA_WINDOW_GADGET_CLOSE | ASTRA_WINDOW_GADGET_MINIMIZE |
          ASTRA_WINDOW_GADGET_MAXIMIZE,
      ASTRA_WINDOW_STANDARD, ASTRA_GADGET_PRESSED, ASTRA_GADGET_NORMAL,
      ASTRA_GADGET_HOVER, "WINDOW GALLERY", 14, "STANDARD / ACTIVE", 17 }
};

ASTRA_PROGRAM("desktop", 0, 2, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

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

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static void text(AstraSurfaceView *surface, int32_t x, int32_t y,
                 const char *utf8, uint32_t length, uint16_t value)
{
    astra_surface_ui_text(surface, x, y, utf8, length,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, value);
}

static void centered_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                          uint32_t width, const char *utf8, uint32_t length,
                          uint16_t value)
{
    uint32_t text_width = astra_surface_ui_text_width(
        utf8, length, ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT);

    text(surface, x + (int32_t)(width > text_width ?
         (width - text_width) / 2u : 0u), y, utf8, length, value);
}

static void paint(AstraSurfaceView *surface, const GallerySpec *spec,
                  const AstraTheme *theme)
{
    uint16_t client = color(theme->client);
    uint16_t primary = color(theme->title_active);
    uint16_t muted = color(theme->text_muted);
    uint16_t accent = color(theme->accent);

    astra_surface_clear(surface, client);
    astra_surface_ui_text(surface, 18, 18, spec->label, spec->label_length,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, primary);
    astra_surface_fill(surface, 18, 36, surface->width - 36u, 2u, accent);
    if (spec->type == ASTRA_WINDOW_STANDARD) {
        astra_surface_fill_round(surface, 18, 58, 238u, 92u,
                                 theme->card_radius, 0xffffu);
        astra_surface_fill_round(surface, 276, 58, 256u, 92u,
                                 theme->card_radius, 0xffffu);
        text(surface, 34, 76, "NORMAL", 6u, muted);
        text(surface, 292, 76, "HOVER / PRESSED", 15u, muted);
        astra_surface_fill_round(surface, 18, 174, 118u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 148, 174, 118u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        centered_text(surface, 18, 186, 118u, "PRIMARY", 7u, 0xffffu);
        centered_text(surface, 148, 186, 118u, "SECONDARY", 9u, primary);
    } else if (spec->type == ASTRA_WINDOW_UTILITY) {
        astra_surface_fill_round(surface, 18, 58, surface->width - 36u, 48u,
                                 theme->control_radius, 0xffffu);
        text(surface, 34, 77, "COMPACT TOOL WINDOW", 19u, muted);
    } else if (spec->type == ASTRA_WINDOW_DIALOG) {
        text(surface, 18, 62, "CHANGES ARE READY TO SAVE.", 26u, muted);
        astra_surface_fill_round(surface, 128, 116, 112u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 252, 116, 112u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        centered_text(surface, 128, 128, 112u, "SAVE", 4u, 0xffffu);
        centered_text(surface, 252, 128, 112u, "CANCEL", 6u, primary);
    } else {
        text(surface, 18, 58, "NO TITLEBAR", 11u, primary);
        text(surface, 18, 78, "CONTEXTUAL CHROME", 17u, muted);
    }
}

static uint32_t management_status(AstraResult result, uint32_t step)
{
    return result == ASTRA_OK ? ASTRA_STATUS_OK :
           DESKTOP_FAIL_MANAGE + step * 16u + (uint32_t)(-result);
}

int astra_main(const AstraStartupInfo *startup)
{
    GalleryWindow windows[GALLERY_WINDOW_COUNT] = {0};
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    uint32_t idle_receive = 0u;
    uint32_t idle_send = 0u;
    uint32_t status = ASTRA_STATUS_OK;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    gui = capability(startup, ASTRA_CAPABILITY_GUI);
    if (bootstrap == NULL || gui == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    for (uint32_t index = 0u;
         index < GALLERY_WINDOW_COUNT && status == ASTRA_STATUS_OK; ++index) {
        const GallerySpec *spec = &gallery[index];
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;

        status = astra_shared_draw_list_create(&windows[index].surface,
                                               spec->width, spec->height);
        if (status != ASTRA_SYSCALL_OK) {
            status = DESKTOP_FAIL_SURFACE_FIRST + index;
            break;
        }
        paint(&windows[index].surface.view, spec, &theme);
        info.flags = spec->flags;
        info.x = spec->x;
        info.y = spec->y;
        info.width = spec->width;
        info.height = spec->height;
        info.pitch = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.gadgets = spec->gadgets;
        info.type = spec->type;
        info.close_state = spec->close_state;
        info.minimize_state = spec->minimize_state;
        info.maximize_state = spec->maximize_state;
        info.title = spec->title;
        info.title_length = spec->title_length;
        {
            AstraResult window_status = astra_window_create(
                gui->handle, windows[index].surface.area, &info,
                &windows[index].window);

            if (window_status != ASTRA_OK)
                status = DESKTOP_FAIL_WINDOW_FIRST + index * 16u +
                         (uint32_t)(-window_status);
        }
    }
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_move(&windows[3].window, 120u, 110u), 0u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_resize(&windows[3].window, 580u, 300u), 1u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_lower(&windows[3].window), 2u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_raise(&windows[3].window), 3u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_activate(&windows[2].window), 4u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_move(&windows[2].window, 540u, 320u), 5u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_minimize(&windows[0].window), 6u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_restore(&windows[0].window), 7u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_maximize(&windows[3].window), 8u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_restore(&windows[3].window), 9u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(astra_window_set_title(
            &windows[3].window, "ASTRA WORKBENCH", 15u), 10u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_deactivate(&windows[2].window), 11u);
    if (status == ASTRA_STATUS_OK)
        status = management_status(
            astra_window_activate(&windows[3].window), 12u);
    if (status == ASTRA_STATUS_OK) {
        AstraWindowInfo info = ASTRA_WINDOW_INFO_INIT;

        status = management_status(
            astra_window_get_info(&windows[3].window, &info), 13u);
        if (status == ASTRA_STATUS_OK &&
            (info.frame.x != 120u || info.frame.y != 110u ||
             info.frame.width != 580u || info.frame.height != 300u ||
             info.state != ASTRA_WINDOW_STATE_NORMAL ||
             (info.flags & ASTRA_WINDOW_ACTIVE) == 0u ||
             info.z_order != GALLERY_WINDOW_COUNT - 1u))
            status = DESKTOP_FAIL_MANAGE + 14u * 16u;
    }
    if (status == ASTRA_STATUS_OK)
        status = astra_rt_port_create(1u, ASTRA_MESSAGE_HEADER_SIZE,
                                   &idle_receive, &idle_send);
    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) {
        for (uint32_t index = 0u; index < GALLERY_WINDOW_COUNT; ++index)
            if (windows[index].surface.area != 0u)
                (void)astra_shared_surface_close(&windows[index].surface);
        return (int)status;
    }
    for (;;) {
        status = astra_wait_one(idle_receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
    }
}
