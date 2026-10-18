/* Test-only native client: refuse bad retained frames without losing Display. */
#include <astra/draw_list.h>
#include <astra/graphics_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/window.h>

ASTRA_PROGRAM("display-frame-probe", 0, 1, 0, "Astra68 contributors", "Astra native test");

int astra_main(const AstraStartupInfo *startup)
{
    AstraSharedSurface surface = {0};
    AstraWindow window = ASTRA_WINDOW_INIT;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraWindowFrame frame = {0u, 0u, 320u, 160u};
    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    const AstraStartupCapability *gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    const AstraStartupCapability *ready = astra_startup_capability(startup, ASTRA_CAPABILITY_SERVICE_READY);
    if (gui == NULL || ready == NULL) return ASTRA_STATUS_BAD_HANDLE;
    uint32_t status = ASTRA_STATUS_OK;
    if (astra_shared_draw_list_create(&surface, frame.width, frame.height) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK) {
        astra_surface_clear(&surface.view, 0x1234u);
        info.x = 300u; info.y = 180u;
        info.width = frame.width; info.height = frame.height;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.title = "Frame refusal probe";
        info.title_length = sizeof("Frame refusal probe") - 1u;
        if (astra_window_create(gui->handle, surface.area, &info, &window) != ASTRA_OK)
            status = ASTRA_STATUS_IO;
    }
    for (uint32_t test = 0u; status == ASTRA_STATUS_OK && test < 3u; ++test) {
        AstraDrawListHeader *list = (void *)surface.view.pixels;
        AstraDrawListCommand *draw = (void *)(list + 1);
        if (test == 0u) draw[0].width = UINT32_MAX;
        else if (test == 1u) list->magic = 0u;
        else draw[0].operation = UINT16_MAX;
        if (astra_window_present_region(&window, &frame) != ASTRA_ERROR_IO) {
            status = ASTRA_STATUS_PROGRAM_FIRST;
            break;
        }
        if (!astra_draw_list_view_init(&surface.view, surface.view.pixels,
            surface.view.byte_size, frame.width, frame.height)) {
            status = ASTRA_STATUS_INVALID;
            break;
        }
        astra_surface_clear(&surface.view, (uint16_t)(0x1234u + test));
        if (astra_window_present_region(&window, &frame) != ASTRA_OK)
            status = ASTRA_STATUS_PROGRAM_FIRST + 1u;
    }
    if (window._private_control != ASTRA_INVALID_HANDLE && astra_window_close(&window) != ASTRA_OK)
        status = ASTRA_STATUS_IO;
    if (surface.area != ASTRA_INVALID_HANDLE && astra_shared_surface_close(&surface) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_IO;
    if (status == ASTRA_STATUS_OK) (void)astra_log("display frame refusal probe PASS");
    (void)astra_service_ready(ready->handle, status, NULL, 0u);
    (void)astra_close(ready->handle);
    return (int)status;
}
