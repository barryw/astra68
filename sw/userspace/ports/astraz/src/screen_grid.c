#include "screen_grid.h"

#include <string.h>

static uint32_t colour(int32_t index)
{
    static const uint16_t palette[] = {
        0u, 0u, 0x0000u, 0xf800u, 0x07e0u, 0xffe0u,
        0x001fu, 0xf81fu, 0x07ffu, 0xffffu, 0x8410u
    };
    return index >= ASTRAZ_COLOUR_BLACK && index <= ASTRAZ_COLOUR_GREY ?
        palette[index] : ASTRA_TEXT_COLOR_DEFAULT;
}

static uint32_t cell_row(const AstraZScreenGrid *grid, int32_t coordinate)
{
    if (coordinate <= 0) return 0u;
    return grid->version == 6u ?
        ((uint32_t)coordinate - 1u) / 20u + 1u : (uint32_t)coordinate;
}

static uint32_t cell_column(const AstraZScreenGrid *grid, int32_t coordinate)
{
    if (coordinate <= 0) return 0u;
    return grid->version == 6u ?
        ((uint32_t)coordinate - 1u) / 10u + 1u : (uint32_t)coordinate;
}

static AstraTextCell blank(const AstraZScreenGrid *grid, uint32_t window)
{
    return (AstraTextCell){' ', grid->foreground[window],
        grid->background[window], 0u, 1u, 0u};
}

static void reset(AstraZScreenGrid *grid, uint32_t version)
{
    memset(grid, 0, sizeof(*grid));
    grid->version = version;
    for (uint32_t window = 0u; window < 8u; ++window) {
        grid->row[window] = grid->column[window] = 1u;
        grid->foreground[window] = ASTRA_TEXT_COLOR_DEFAULT;
        grid->background[window] = ASTRA_TEXT_COLOR_DEFAULT;
    }
    for (uint32_t at = 0u; at < ASTRAZ_GRID_ROWS * ASTRAZ_GRID_COLUMNS; ++at)
        grid->cells[at] = blank(grid, 1u);
}

static int upper(int32_t window)
{
    return window >= 1 && window <= 6;
}

int astraz_screen_grid_event(AstraZScreenGrid *grid, const AstraZEvent *event)
{
    if (!grid || !event) return 0;
    if (event->kind == ASTRAZ_EVENT_SCREEN_BEGIN) {
        reset(grid, event->data.screen.version);
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_CURSOR &&
        upper(event->data.cursor.window)) {
        uint32_t window = (uint32_t)event->data.cursor.window;
        uint32_t row = cell_row(grid, event->data.cursor.row);
        uint32_t column = cell_column(grid, event->data.cursor.column);
        if (!row || row > ASTRAZ_GRID_ROWS || !column ||
            column > ASTRAZ_GRID_COLUMNS) return 0;
        grid->row[window] = row;
        grid->column[window] = column;
        if (grid->rows < row) grid->rows = row;
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_STYLE &&
        upper(event->data.style.window)) {
        uint32_t window = (uint32_t)event->data.style.window;
        uint32_t style = (uint32_t)event->data.style.style;
        grid->style[window] = (uint16_t)(
            ((style & ASTRAZ_STYLE_BOLD) ? ASTRA_TEXT_STYLE_BOLD : 0u) |
            ((style & ASTRAZ_STYLE_EMPHASIS) ? ASTRA_TEXT_STYLE_ITALIC : 0u) |
            ((style & ASTRAZ_STYLE_REVERSE) ? ASTRA_TEXT_STYLE_INVERSE : 0u));
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_COLOUR &&
        upper(event->data.colour.window)) {
        uint32_t window = (uint32_t)event->data.colour.window;
        grid->foreground[window] = colour(event->data.colour.foreground);
        grid->background[window] = colour(event->data.colour.background);
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_CHARACTER &&
        upper(event->data.text.window)) {
        uint32_t window = (uint32_t)event->data.text.window;
        uint32_t row = grid->row[window], column = grid->column[window];
        uint32_t scalar = event->data.text.character;
        if (scalar == '\n' || scalar == '\r') {
            if (row < ASTRAZ_GRID_ROWS) grid->row[window] = row + 1u;
            grid->column[window] = 1u;
            if (grid->rows < grid->row[window]) grid->rows = grid->row[window];
            return 1;
        }
        if (scalar < 32u || row == 0u || row > ASTRAZ_GRID_ROWS ||
            column == 0u || column > ASTRAZ_GRID_COLUMNS) return 0;
        AstraTextCell *cell = &grid->cells[(row - 1u) * ASTRAZ_GRID_COLUMNS +
                                            column - 1u];
        *cell = (AstraTextCell){scalar, grid->foreground[window],
            grid->background[window], grid->style[window], 1u, 0u};
        if (column < ASTRAZ_GRID_COLUMNS) ++grid->column[window];
        if (grid->rows < row) grid->rows = row;
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_ERASE) {
        int32_t window = event->data.erase.window;
        if (window == -2) {
            reset(grid, grid->version);
            return 1;
        }
        if (!upper(window) && window != -1) return 0;
        uint32_t first_row = cell_row(grid, event->data.erase.top);
        uint32_t last_row = cell_row(grid, event->data.erase.bottom);
        uint32_t first_column = cell_column(grid, event->data.erase.left);
        uint32_t last_column = cell_column(grid, event->data.erase.right);
        if (!first_row || !first_column || first_row > ASTRAZ_GRID_ROWS ||
            first_column > ASTRAZ_GRID_COLUMNS) return 0;
        if (last_row > ASTRAZ_GRID_ROWS) last_row = ASTRAZ_GRID_ROWS;
        if (last_column > ASTRAZ_GRID_COLUMNS)
            last_column = ASTRAZ_GRID_COLUMNS;
        if (window == -1 && last_row > grid->rows) last_row = grid->rows;
        if (last_row < first_row || last_column < first_column) return 0;
        uint32_t selected = upper(window) ? (uint32_t)window : 1u;
        for (uint32_t row = first_row; row <= last_row; ++row)
            for (uint32_t column = first_column;
                 column <= last_column; ++column)
                grid->cells[(row - 1u) * ASTRAZ_GRID_COLUMNS + column - 1u] =
                    blank(grid, selected);
        if (upper(window) && grid->rows < last_row) grid->rows = last_row;
        return 1;
    }
    if (event->kind == ASTRAZ_EVENT_SCROLL && grid->rows != 0u) {
        uint32_t top = cell_row(grid, event->data.scroll.top);
        uint32_t bottom = cell_row(grid, event->data.scroll.bottom);
        uint32_t left = cell_column(grid, event->data.scroll.left);
        uint32_t right = cell_column(grid, event->data.scroll.right);
        int32_t units = event->data.scroll.units;
        if (!top || top > grid->rows || bottom > grid->rows || !left ||
            left > ASTRAZ_GRID_COLUMNS || !units) return 0;
        if (right > ASTRAZ_GRID_COLUMNS) right = ASTRAZ_GRID_COLUMNS;
        if (bottom < top || right < left) return 0;
        if (grid->version == 6u) units /= 20;
        if (units > 0) {
            uint32_t shift = (uint32_t)units;
            for (uint32_t row = top; row <= bottom; ++row)
                for (uint32_t column = left; column <= right; ++column)
                    grid->cells[(row - 1u) * ASTRAZ_GRID_COLUMNS + column - 1u] =
                        row + shift <= bottom ?
                        grid->cells[(row + shift - 1u) * ASTRAZ_GRID_COLUMNS +
                                    column - 1u] : blank(grid, 1u);
        } else if (units < 0) {
            uint32_t shift = (uint32_t)(-(int64_t)units);
            for (uint32_t row = bottom; row >= top; --row) {
                for (uint32_t column = left; column <= right; ++column)
                    grid->cells[(row - 1u) * ASTRAZ_GRID_COLUMNS + column - 1u] =
                        row >= top + shift ?
                        grid->cells[(row - shift - 1u) * ASTRAZ_GRID_COLUMNS +
                                    column - 1u] : blank(grid, 1u);
                if (row == top) break;
            }
        }
        return 1;
    }
    return 0;
}
