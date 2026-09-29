#ifndef ASTRA_DESKTOP_LAYOUT_H
#define ASTRA_DESKTOP_LAYOUT_H

#include <astra/theme.h>

#include <stdint.h>

/*
 * The desktop's application grid, in desktop-window coordinates. Cells fill
 * a column top to bottom, then the next column to the right. A cell holds a
 * 64-pixel icon centred over its label; the whole cell is the hit target.
 */
#define ASTRA_DESKTOP_ICON_CELL_LEFT 32u
#define ASTRA_DESKTOP_ICON_CELL_WIDTH 80u
#define ASTRA_DESKTOP_ICON_SIZE 64u
#define ASTRA_DESKTOP_GRID_TOP 26u
#define ASTRA_DESKTOP_CELL_PITCH_X 96u
#define ASTRA_DESKTOP_CELL_PITCH_Y 112u
#define ASTRA_DESKTOP_CELL_HEIGHT 104u
#define ASTRA_DESKTOP_ICON_INSET_Y 8u
#define ASTRA_DESKTOP_ICON_X \
    (ASTRA_DESKTOP_ICON_CELL_LEFT + \
     (ASTRA_DESKTOP_ICON_CELL_WIDTH - ASTRA_DESKTOP_ICON_SIZE) / 2u)
#define ASTRA_DESKTOP_ICON_Y \
    (ASTRA_DESKTOP_GRID_TOP + ASTRA_DESKTOP_ICON_INSET_Y)
#define ASTRA_DESKTOP_LABEL_Y \
    (ASTRA_DESKTOP_ICON_Y + ASTRA_DESKTOP_ICON_SIZE + 8u)
#define ASTRA_DESKTOP_LABEL_FONT_HEIGHT \
    ASTRA_THEME_SYSTEM_TITLE_FONT_HEIGHT

/* Cells that fit a desktop of @p width x @p height. */
static inline uint32_t astra_desktop_rows(uint32_t height)
{
    return height < ASTRA_DESKTOP_GRID_TOP + ASTRA_DESKTOP_CELL_HEIGHT ? 0u :
        (height - ASTRA_DESKTOP_GRID_TOP - ASTRA_DESKTOP_CELL_HEIGHT) /
            ASTRA_DESKTOP_CELL_PITCH_Y + 1u;
}

static inline uint32_t astra_desktop_columns(uint32_t width)
{
    return width < ASTRA_DESKTOP_ICON_CELL_LEFT +
                       ASTRA_DESKTOP_ICON_CELL_WIDTH ? 0u :
        (width - ASTRA_DESKTOP_ICON_CELL_LEFT -
         ASTRA_DESKTOP_ICON_CELL_WIDTH) / ASTRA_DESKTOP_CELL_PITCH_X + 1u;
}

static inline uint32_t astra_desktop_capacity(uint32_t width, uint32_t height)
{
    return astra_desktop_rows(height) * astra_desktop_columns(width);
}

/* Offset of cell @p index from cell 0; add it to every ICON_/LABEL_ origin. */
static inline void astra_desktop_cell_offset(uint32_t index, uint32_t height,
                                             uint32_t *dx, uint32_t *dy)
{
    uint32_t rows = astra_desktop_rows(height);

    *dx = rows == 0u ? 0u : index / rows * ASTRA_DESKTOP_CELL_PITCH_X;
    *dy = rows == 0u ? 0u : index % rows * ASTRA_DESKTOP_CELL_PITCH_Y;
}

/* The cell under (x, y), or UINT32_MAX between and outside cells. */
static inline uint32_t astra_desktop_cell_at(int32_t x, int32_t y,
                                             uint32_t width, uint32_t height)
{
    uint32_t column;
    uint32_t row;
    uint32_t local_x;
    uint32_t local_y;

    if (x < (int32_t)ASTRA_DESKTOP_ICON_CELL_LEFT ||
        y < (int32_t)ASTRA_DESKTOP_GRID_TOP)
        return UINT32_MAX;
    local_x = (uint32_t)x - ASTRA_DESKTOP_ICON_CELL_LEFT;
    local_y = (uint32_t)y - ASTRA_DESKTOP_GRID_TOP;
    column = local_x / ASTRA_DESKTOP_CELL_PITCH_X;
    row = local_y / ASTRA_DESKTOP_CELL_PITCH_Y;
    if (local_x % ASTRA_DESKTOP_CELL_PITCH_X >=
            ASTRA_DESKTOP_ICON_CELL_WIDTH ||
        local_y % ASTRA_DESKTOP_CELL_PITCH_Y >= ASTRA_DESKTOP_CELL_HEIGHT ||
        column >= astra_desktop_columns(width) ||
        row >= astra_desktop_rows(height))
        return UINT32_MAX;
    return column * astra_desktop_rows(height) + row;
}

static inline int32_t astra_desktop_centered_label_x(uint32_t text_width)
{
    if (text_width >= ASTRA_DESKTOP_ICON_CELL_WIDTH)
        return (int32_t)ASTRA_DESKTOP_ICON_CELL_LEFT;
    return (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT +
        (ASTRA_DESKTOP_ICON_CELL_WIDTH - text_width) / 2u);
}

#endif
