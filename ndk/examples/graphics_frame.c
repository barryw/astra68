#include <astra/graphics.h>

/* Build, submit, and present one protected RGB565 frame. */
AstraResult draw_startup_frame(void)
{
    ASTRA_AUTO_DISPLAY(display);
    ASTRA_AUTO_SURFACE(surface);
    ASTRA_AUTO_DRAW_LIST(draw_list);
    ASTRA_AUTO_FENCE(draw_fence);
    ASTRA_AUTO_FENCE(present_fence);
    AstraSurfaceCreateInfo create_info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraPresentOptions present_options = ASTRA_PRESENT_OPTIONS_INIT;
    AstraDrawPaint background = ASTRA_DRAW_PAINT_INIT;
    AstraDrawPaint accent = ASTRA_DRAW_PAINT_INIT;
    AstraRectI32 screen = { 0, 0, 720, 480 };
    AstraResult completion;
    AstraResult status;

    if (!astra_graphics_present())
        return ASTRA_ERROR_NOT_PRESENT;

    status = astra_display_open(&display);
    if (status != ASTRA_OK)
        return status;

    create_info.flags = ASTRA_SURFACE_SCANOUT | ASTRA_SURFACE_DRAW_TARGET;
    create_info.width = 720;
    create_info.height = 480;
    create_info.format = ASTRA_PIXEL_FORMAT_RGB565;
    status = astra_surface_create(&display, &create_info, &surface);
    if (status != ASTRA_OK)
        return status;

    status = astra_draw_list_create(&surface, &screen, &draw_list);
    if (status != ASTRA_OK)
        return status;

    background.foreground = (AstraColorRGBA8){ 12, 20, 28, 255 };
    status = astra_draw_rectangle(&draw_list, &screen, 1, &background);
    if (status != ASTRA_OK)
        return status;

    accent.foreground = (AstraColorRGBA8){ 48, 196, 168, 255 };
    status = astra_draw_line(&draw_list, (AstraPointI32){ 80, 80 },
                             (AstraPointI32){ 640, 400 }, &accent);
    if (status != ASTRA_OK)
        return status;
    status = astra_draw_circle(&draw_list, (AstraPointI32){ 360, 240 },
                               96, 0, &accent);
    if (status != ASTRA_OK)
        return status;

    status = astra_draw_submit(&draw_list, &draw_fence);
    if (status != ASTRA_OK)
        return status;
    status = astra_fence_wait(&draw_fence, 1000, &completion);
    if (status != ASTRA_OK || completion != ASTRA_OK)
        return status != ASTRA_OK ? status : completion;

    status = astra_display_present_surface(&display, &surface,
                                            &present_options,
                                            &present_fence);
    if (status != ASTRA_OK)
        return status;
    status = astra_fence_wait(&present_fence, 1000, &completion);
    return status != ASTRA_OK ? status : completion;
}
