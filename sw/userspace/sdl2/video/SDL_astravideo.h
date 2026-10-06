#ifndef SDL_astravideo_h_
#define SDL_astravideo_h_

#include <astra/graphics.h>
#include <astra/window.h>

/*
 * One SDL window: a CONTENT_SURFACE Astra window. Its content is a GPU
 * surface; the window framebuffer path writes it from the display's CPU
 * staging area and the Astra renderer draws it with submitted draw lists.
 */
typedef struct ASTRA_WindowData {
    AstraWindow native;
    AstraDisplay display;
    /* The window content, sized when the framebuffer was last created. */
    AstraSurface content;
    uint8_t *framebuffer;
    uint32_t framebuffer_pitch;
    /* The window's ASTRA_WINDOW_SUBSCRIBE_* mask; the renderer adds VBLANK
     * while it presents with vsync. */
    uint32_t event_mask;
    /* The device motion totals of the last motion event, for relative
       mode's deltas; and where the content begins on the screen, as the
       last pointer event put it. */
    int32_t motion_x;
    int32_t motion_y;
    int32_t origin_x;
    int32_t origin_y;
    uint8_t motion_valid;
    uint8_t origin_valid;
    /* What the window was last given: its pointer grab and rectangle, and
       its cursor (NULL hides it). */
    uint8_t cursor_applied;
    uint32_t grab_flags;
    AstraWindowFrame grab_rect;
    const void *cursor;
} ASTRA_WindowData;

#endif
