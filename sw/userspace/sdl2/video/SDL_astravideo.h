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
} ASTRA_WindowData;

#endif
