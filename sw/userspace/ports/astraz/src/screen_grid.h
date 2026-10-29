#ifndef ASTRAZ_SCREEN_GRID_H
#define ASTRAZ_SCREEN_GRID_H

#include <astraz/engine.h>
#include <astra/text_surface.h>

#define ASTRAZ_GRID_COLUMNS 80u
#define ASTRAZ_GRID_ROWS 24u

typedef struct AstraZScreenGrid {
    AstraTextCell cells[ASTRAZ_GRID_ROWS * ASTRAZ_GRID_COLUMNS];
    uint32_t row[8];
    uint32_t column[8];
    uint32_t foreground[8];
    uint32_t background[8];
    uint16_t style[8];
    uint32_t rows;
    uint32_t version;
} AstraZScreenGrid;

/* Return nonzero when upper-window presentation needs refresh. */
int astraz_screen_grid_event(AstraZScreenGrid *grid, const AstraZEvent *event);
/* Shared Z-machine palette and style mapping for grid and flowing text. */
uint32_t astraz_screen_colour(int32_t index);
uint16_t astraz_screen_style(uint32_t style);

#endif
