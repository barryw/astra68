#ifndef ASTRA_DESKTOP_LAYOUT_H
#define ASTRA_DESKTOP_LAYOUT_H

#include <astra/theme.h>

#include <stdint.h>

/*
 * The desktop's application grid, in desktop-window coordinates. Cells fill
 * a column top to bottom, then the next column to the right. Further pages
 * continue vertically, preserving the first page's established positions.
 * A cell holds a
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
static inline void astra_desktop_cell_offset(uint32_t index, uint32_t width,
                                             uint32_t height,
                                             uint32_t *dx, uint32_t *dy)
{
    uint32_t rows = astra_desktop_rows(height);
    uint32_t capacity = astra_desktop_capacity(width, height);
    uint32_t page = capacity == 0u ? 0u : index / capacity;
    uint32_t cell = capacity == 0u ? 0u : index % capacity;

    *dx = rows == 0u ? 0u : cell / rows * ASTRA_DESKTOP_CELL_PITCH_X;
    *dy = rows == 0u ? 0u : page * height + cell % rows * ASTRA_DESKTOP_CELL_PITCH_Y;
}

/* Exact content height, or zero when the grid cannot represent the count. */
static inline uint32_t astra_desktop_content_height(uint32_t count,
                                                    uint32_t width,
                                                    uint32_t height)
{
    uint32_t rows = astra_desktop_rows(height);
    uint32_t capacity = astra_desktop_capacity(width, height);
    uint64_t total;
    uint32_t last;

    if (capacity == 0u) return 0u;
    if (count <= capacity) return height;
    last = (count - 1u) % capacity + 1u;
    if (last > rows) last = rows;
    total = (uint64_t)((count - 1u) / capacity) * height +
        ASTRA_DESKTOP_GRID_TOP + (last - 1u) * ASTRA_DESKTOP_CELL_PITCH_Y +
        ASTRA_DESKTOP_CELL_HEIGHT;
    return total > INT32_MAX ? 0u : (uint32_t)total;
}

/* The cell under (x, y), or UINT32_MAX between and outside cells. */
static inline uint32_t astra_desktop_cell_at(int32_t x, int32_t y,
                                             uint32_t width, uint32_t height,
                                             uint32_t offset_y)
{
    uint32_t column;
    uint32_t row;
    uint32_t local_x;
    uint32_t local_y;
    uint32_t content_y;
    uint32_t page;

    if (x < (int32_t)ASTRA_DESKTOP_ICON_CELL_LEFT ||
        (uint32_t)x >= width || y < 0 || (uint32_t)y >= height ||
        height == 0u || (uint64_t)(uint32_t)y + offset_y > INT32_MAX)
        return UINT32_MAX;
    content_y = (uint32_t)y + offset_y;
    page = content_y / height;
    content_y %= height;
    if (content_y < ASTRA_DESKTOP_GRID_TOP) return UINT32_MAX;
    local_x = (uint32_t)x - ASTRA_DESKTOP_ICON_CELL_LEFT;
    local_y = content_y - ASTRA_DESKTOP_GRID_TOP;
    column = local_x / ASTRA_DESKTOP_CELL_PITCH_X;
    row = local_y / ASTRA_DESKTOP_CELL_PITCH_Y;
    if (local_x % ASTRA_DESKTOP_CELL_PITCH_X >=
            ASTRA_DESKTOP_ICON_CELL_WIDTH ||
        local_y % ASTRA_DESKTOP_CELL_PITCH_Y >= ASTRA_DESKTOP_CELL_HEIGHT ||
        column >= astra_desktop_columns(width) ||
        row >= astra_desktop_rows(height))
        return UINT32_MAX;
    return page * astra_desktop_capacity(width, height) +
        column * astra_desktop_rows(height) + row;
}

static inline int32_t astra_desktop_centered_label_x(uint32_t text_width)
{
    if (text_width >= ASTRA_DESKTOP_ICON_CELL_WIDTH)
        return (int32_t)ASTRA_DESKTOP_ICON_CELL_LEFT;
    return (int32_t)(ASTRA_DESKTOP_ICON_CELL_LEFT +
        (ASTRA_DESKTOP_ICON_CELL_WIDTH - text_width) / 2u);
}

#endif
