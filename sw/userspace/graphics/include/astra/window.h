#ifndef ASTRA_USERSPACE_WINDOW_H
#define ASTRA_USERSPACE_WINDOW_H

#include <stdint.h>

#include <astra/surface.h>

typedef struct AstraWindow {
    uint32_t id;
    uint32_t generation;
} AstraWindow;

uint32_t astra_window_open(uint32_t gui, const AstraSharedSurface *surface,
                           uint16_t x, uint16_t y, AstraWindow *window);

#endif
