#ifndef ASTRA_GRAPHICS_ROUNDED_H
#define ASTRA_GRAPHICS_ROUNDED_H

#include <stdint.h>

static inline uint32_t
astra_graphics_rounded_inset(uint32_t row, uint32_t height, uint32_t radius)
{
    uint32_t corner_y;
    uint32_t diameter;
    uint32_t inset = 0u;

    if (radius == 0u || (row >= radius && row < height - radius))
        return 0u;
    corner_y = row < radius ? row : height - row - 1u;
    diameter = radius * 2u;
    while (inset < radius) {
        uint32_t dx = diameter - inset * 2u - 1u;
        uint32_t dy = diameter - corner_y * 2u - 1u;

        if (dx * dx + dy * dy <= diameter * diameter)
            break;
        ++inset;
    }
    return inset;
}

#endif
