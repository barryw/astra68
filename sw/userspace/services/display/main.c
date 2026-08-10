#include <astra/display.h>
#include <astra/gui.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>

enum {
    DISPLAY_FAIL_ARM = ASTRA_STATUS_PROGRAM_FIRST,
    DISPLAY_FAIL_SUBMIT,
    DISPLAY_FAIL_WAIT,
    DISPLAY_FAIL_IRQ,
    DISPLAY_FAIL_COMPLETION,
    DISPLAY_FAIL_PROTOCOL,
};

ASTRA_PROGRAM("display", 0, 2, 0, "Barry Walker",
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

static uint32_t ready(uint32_t bootstrap, uint32_t status, uint32_t gui)
{
    AstraServiceReady message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    return astra_port_send(bootstrap, &message, sizeof(message),
                           status == ASTRA_STATUS_OK ? &gui : NULL,
                           status == ASTRA_STATUS_OK ? 1u : 0u);
}

static void compose(AstraSurfaceView *framebuffer,
                    const AstraGuiOpenWindow *request,
                    const AstraSurfaceView *window)
{
    uint16_t background = astra_surface_rgb565(22u, 45u, 58u);
    uint16_t panel = astra_surface_rgb565(9u, 16u, 23u);
    uint16_t accent = astra_surface_rgb565(45u, 174u, 184u);
    int32_t x = request->x;
    int32_t y = request->y;

    astra_surface_clear(framebuffer, background);
    astra_surface_fill(framebuffer, 0, 0, ASTRA_DISPLAY_WIDTH, 34u, panel);
    astra_surface_fill_round(framebuffer, 18, 11, 86u, 12u, 6u, accent);
    astra_surface_fill(framebuffer, 0, 678, ASTRA_DISPLAY_WIDTH, 42u, panel);
    astra_surface_fill_round(framebuffer, x - 4, y - 30,
                             (uint32_t)window->width + 8u,
                             (uint32_t)window->height + 34u, 14u,
                             astra_surface_rgb565(6u, 10u, 15u));
    astra_surface_fill_round(framebuffer, x, y - 26, window->width, 36u,
                             11u, accent);
    astra_surface_fill_round(framebuffer,
                             x + (int32_t)window->width - 20, y - 21,
                             12u, 12u, 6u,
                             astra_surface_rgb565(238u, 244u, 246u));
    astra_surface_blit_round_bottom(framebuffer, x, y, window, 11u);
}

static uint32_t present(uint32_t device, uint32_t irq,
                        const AstraDmaBufferInfo *buffer, uint32_t fence,
                        uint32_t *generation)
{
    AstraDisplayFrameRequest request = {
        .size = ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
        .operation = ASTRA_DISPLAY_FRAME_PRESENT_RGB565,
        .fence = fence,
        .source = buffer->handle,
        .pitch = ASTRA_DISPLAY_WIDTH * sizeof(uint16_t),
        .byte_size = ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT *
                     sizeof(uint16_t),
    };
    AstraDisplayFrameCompletion completion;
    AstraIrqRecord record;
    uint32_t status;

    if (astra_irq_arm(irq) != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_ARM;
    if (astra_display_submit(device, &request) != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_SUBMIT;
    if (astra_wait_one(irq, ASTRA_DEADLINE_FOREVER, NULL) !=
        ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_WAIT;
    status = astra_irq_read(irq, &record, NULL);
    if (status != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_IRQ;
    status = astra_display_collect(device, &completion);
    if (status != ASTRA_SYSCALL_OK ||
        astra_irq_ack(irq, record.sequence) != ASTRA_SYSCALL_OK)
        return DISPLAY_FAIL_COMPLETION;
    if (completion.size != ASTRA_DISPLAY_FRAME_COMPLETION_SIZE ||
        completion.fence != fence ||
        completion.status != ASTRA_DISPLAY_COMPLETION_OK ||
        completion.generation == 0u || completion.reserved != 0u)
        return DISPLAY_FAIL_COMPLETION;
    *generation = completion.generation;
    return ASTRA_STATUS_OK;
}

static int valid_request(const AstraGuiOpenWindow *request, uint32_t size,
                         uint32_t handles)
{
    return size == sizeof(*request) && handles == 2u &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u &&
           request->header.protocol == ASTRA_GUI_PROTOCOL &&
           request->header.protocol_version == ASTRA_GUI_VERSION &&
           request->header.reserved == 0u &&
           request->header.operation == ASTRA_GUI_OPEN_WINDOW &&
           request->header.transaction_id != 0u && request->reserved == 0u &&
           request->width != 0u && request->height != 0u &&
           request->pitch >= (uint32_t)request->width * sizeof(uint16_t) &&
           (request->pitch & 1u) == 0u && request->x >= 4u &&
           request->y >= 30u &&
           (uint32_t)request->x + request->width + 4u <=
               ASTRA_DISPLAY_WIDTH &&
           (uint32_t)request->y + request->height + 4u <=
               ASTRA_DISPLAY_HEIGHT;
}

static void reply(uint32_t handle, uint32_t transaction, uint32_t status,
                  uint32_t generation)
{
    AstraGuiWindowOpened message = {0};

    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_GUI_PROTOCOL;
    message.header.protocol_version = ASTRA_GUI_VERSION;
    message.header.operation = ASTRA_GUI_WINDOW_OPENED;
    message.header.transaction_id = transaction;
    message.status = status;
    message.window = status == ASTRA_STATUS_OK ? 1u : 0u;
    message.generation = status == ASTRA_STATUS_OK ? generation : 0u;
    (void)astra_port_send(handle, &message, sizeof(message), NULL, 0u);
}

static void serve_window(uint32_t device, uint32_t irq,
                         AstraDmaBufferInfo *framebuffer,
                         AstraSurfaceView *scanout, uint32_t receive)
{
    AstraSharedSurface live = {0};
    uint32_t next_fence = 1u;

    for (;;) {
        AstraGuiOpenWindow request = {0};
        AstraSharedSurface candidate = {0};
        uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
        uint32_t handle_count = 0u;
        uint32_t size = 0u;
        uint32_t generation = 0u;
        uint32_t status;

        status = astra_wait_one(receive, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK)
            astra_process_exit(DISPLAY_FAIL_WAIT);
        status = astra_port_receive(receive, &request, sizeof(request),
                                    handles, ASTRA_MESSAGE_HANDLES_MAX,
                                    &size, &handle_count);
        if (status != ASTRA_SYSCALL_OK)
            continue;
        status = valid_request(&request, size, handle_count) ?
            ASTRA_STATUS_OK : DISPLAY_FAIL_PROTOCOL;
        if (status == ASTRA_STATUS_OK) {
            status = astra_shared_surface_adopt(
                &candidate, handles[0], request.width, request.height,
                request.pitch, ASTRA_AREA_MAP_READ);
            if (status == ASTRA_SYSCALL_OK) {
                handles[0] = 0u;
                compose(scanout, &request, &candidate.view);
                status = present(device, irq, framebuffer, next_fence++,
                                 &generation);
            }
        }
        if (handle_count == 2u)
            reply(handles[1], request.header.transaction_id, status,
                  generation);
        if (handles[1] != 0u) {
            (void)astra_close(handles[1]);
            handles[1] = 0u;
        }
        if (status == ASTRA_STATUS_OK) {
            if (live.area != 0u)
                (void)astra_shared_surface_close(&live);
            live = candidate;
            candidate = (AstraSharedSurface){0};
        }
        if (candidate.area != 0u)
            (void)astra_shared_surface_close(&candidate);
        for (uint32_t index = 0u; index < handle_count; ++index)
            if (handles[index] != 0u)
                (void)astra_close(handles[index]);

        /* ponytail: one live window; add a bounded z-list when a second
         * independently useful GUI client exists. */
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    AstraDmaBufferInfo framebuffer;
    AstraSurfaceView scanout;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    const AstraStartupCapability *irq;
    uint32_t gui_receive = 0u;
    uint32_t gui_send = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    device = capability(startup, ASTRA_CAPABILITY_DISPLAY_DEVICE);
    irq = capability(startup, ASTRA_CAPABILITY_DISPLAY_IRQ);
    if (bootstrap == NULL || device == NULL || irq == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_dma_create(
        ASTRA_DISPLAY_WIDTH * ASTRA_DISPLAY_HEIGHT * sizeof(uint16_t),
        &framebuffer);
    if (status == ASTRA_SYSCALL_OK &&
        !astra_surface_view_init(
            &scanout, (void *)(uintptr_t)framebuffer.virtual_base,
            framebuffer.byte_size, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT,
            ASTRA_DISPLAY_WIDTH * sizeof(uint16_t)))
        status = ASTRA_STATUS_INVALID;
    if (status == ASTRA_SYSCALL_OK)
        status = astra_port_create(4u, 4u * ASTRA_GUI_OPEN_WINDOW_SIZE,
                                   &gui_receive, &gui_send);
    if (status == ASTRA_SYSCALL_OK)
        status = ready(bootstrap->handle, ASTRA_STATUS_OK, gui_send);
    else
        (void)ready(bootstrap->handle, status, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) {
        if (gui_send != 0u)
            (void)astra_close(gui_send);
        if (gui_receive != 0u)
            (void)astra_close(gui_receive);
        (void)astra_device_reset(device->handle);
        return (int)status;
    }
    serve_window(device->handle, irq->handle, &framebuffer, &scanout,
                 gui_receive);
    return ASTRA_STATUS_OK;
}
