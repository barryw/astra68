#include <astra/gui.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/window.h>

#define DESKTOP_WIDTH  900u
#define DESKTOP_HEIGHT 500u

ASTRA_PROGRAM("desktop", 0, 1, 0, "Barry Walker",
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

static void paint(AstraSurfaceView *surface)
{
    static const uint8_t word[5][8] = {
        {0x18u, 0x24u, 0x42u, 0x7eu, 0x42u, 0x42u, 0x42u, 0x00u},
        {0x3cu, 0x42u, 0x40u, 0x3cu, 0x02u, 0x42u, 0x3cu, 0x00u},
        {0x7eu, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0x18u, 0x00u},
        {0x7cu, 0x42u, 0x42u, 0x7cu, 0x48u, 0x44u, 0x42u, 0x00u},
        {0x18u, 0x24u, 0x42u, 0x7eu, 0x42u, 0x42u, 0x42u, 0x00u},
    };
    uint16_t white = astra_surface_rgb565(235u, 243u, 245u);

    astra_surface_clear(surface, astra_surface_rgb565(232u, 236u, 237u));
    astra_surface_fill(surface, 0, 0, 190u, DESKTOP_HEIGHT,
                       astra_surface_rgb565(14u, 27u, 36u));
    astra_surface_fill(surface, 190, 0, DESKTOP_WIDTH - 190u, 62u,
                       astra_surface_rgb565(213u, 221u, 224u));
    astra_surface_fill_round(surface, 220, 94, 630u, 160u, 10u,
                             astra_surface_rgb565(250u, 251u, 251u));
    astra_surface_fill_round(surface, 220, 280, 300u, 172u, 10u,
                             astra_surface_rgb565(250u, 251u, 251u));
    astra_surface_fill_round(surface, 550, 280, 300u, 172u, 10u,
                             astra_surface_rgb565(250u, 251u, 251u));
    astra_surface_fill_round(surface, 28, 104, 134u, 34u, 8u,
                             astra_surface_rgb565(33u, 148u, 164u));
    for (uint32_t index = 0u; index < 5u; ++index)
        astra_surface_glyph8x8(surface, 32 + (int32_t)index * 11, 32,
                               word[index], white);
    for (uint32_t row = 0u; row < 4u; ++row)
        astra_surface_fill(surface, 250, 126 + (int32_t)row * 26,
                           440u - row * 42u, 9u,
                           astra_surface_rgb565(91u, 111u, 122u));
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraSharedSurface surface = {0};
    AstraWindow window;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    uint32_t idle_receive = 0u;
    uint32_t idle_send = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    gui = capability(startup, ASTRA_CAPABILITY_GUI);
    if (bootstrap == NULL || gui == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_shared_surface_create(&surface, DESKTOP_WIDTH,
                                         DESKTOP_HEIGHT);
    if (status == ASTRA_SYSCALL_OK) {
        paint(&surface.view);
        status = astra_window_open(gui->handle, &surface, 190u, 140u, &window);
    }
    if (status == ASTRA_SYSCALL_OK)
        status = astra_port_create(1u, ASTRA_MESSAGE_HEADER_SIZE,
                                   &idle_receive, &idle_send);
    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) {
        if (surface.area != 0u)
            (void)astra_shared_surface_close(&surface);
        return (int)status;
    }
    (void)window;
    for (;;) {
        status = astra_wait_one(idle_receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            return (int)status;
    }
}
