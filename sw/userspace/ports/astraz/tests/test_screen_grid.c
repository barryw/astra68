#include "screen_grid.h"

#include <assert.h>

int main(void)
{
    assert(astraz_screen_style(ASTRAZ_STYLE_BOLD |
        ASTRAZ_STYLE_EMPHASIS | ASTRAZ_STYLE_REVERSE) ==
        (ASTRA_TEXT_STYLE_BOLD | ASTRA_TEXT_STYLE_ITALIC |
         ASTRA_TEXT_STYLE_INVERSE));
    assert(astraz_screen_colour(ASTRAZ_COLOUR_RED) == 0xf800u);
    AstraZScreenGrid grid = {0};
    AstraZEvent event = {.kind = ASTRAZ_EVENT_SCREEN_BEGIN};
    event.data.screen.version = 5u;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.window = 1;
    event.data.cursor.row = 2;
    event.data.cursor.column = 5;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_STYLE;
    event.data.style.window = 1;
    event.data.style.style = ASTRAZ_STYLE_BOLD | ASTRAZ_STYLE_REVERSE;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_COLOUR;
    event.data.colour.window = 1;
    event.data.colour.foreground = ASTRAZ_COLOUR_RED;
    event.data.colour.background = ASTRAZ_COLOUR_BLACK;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.window = 1;
    event.data.text.character = 'X';
    assert(astraz_screen_grid_event(&grid, &event));
    const AstraTextCell *cell = &grid.cells[1u * ASTRAZ_GRID_COLUMNS + 4u];
    assert(grid.rows == 2u && cell->codepoint == 'X');
    assert((cell->attributes & ASTRA_TEXT_STYLE_BOLD) != 0u);
    assert((cell->attributes & ASTRA_TEXT_STYLE_INVERSE) != 0u);
    assert(cell->foreground == 0xf800u && cell->background == 0u);
    event.data.text.window = 0;
    event.data.text.character = 'L';
    assert(!astraz_screen_grid_event(&grid, &event));
    assert(cell->codepoint == 'X');
    event.kind = ASTRAZ_EVENT_SCROLL;
    event.data.scroll.top = 1;
    event.data.scroll.bottom = 24;
    event.data.scroll.left = 1;
    event.data.scroll.right = ASTRAZ_GRID_COLUMNS;
    event.data.scroll.units = 1;
    assert(!astraz_screen_grid_event(&grid, &event));
    assert(cell->codepoint == 'X');
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.window = 2;
    event.data.cursor.row = 3;
    event.data.cursor.column = 5;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.window = 2;
    event.data.text.character = 'Y';
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_SCROLL;
    event.data.scroll.top = 2;
    event.data.scroll.bottom = 3;
    event.data.scroll.left = event.data.scroll.right = 5;
    event.data.scroll.units = 1;
    assert(astraz_screen_grid_event(&grid, &event));
    assert(cell->codepoint == 'Y');
    assert(grid.cells[2u * ASTRAZ_GRID_COLUMNS + 4u].codepoint == ' ');
    event.kind = ASTRAZ_EVENT_ERASE;
    event.data.erase.window = 1;
    event.data.erase.top = event.data.erase.bottom = 2;
    event.data.erase.left = event.data.erase.right = 5;
    assert(astraz_screen_grid_event(&grid, &event));
    assert(cell->codepoint == ' ');
    event.kind = ASTRAZ_EVENT_SCREEN_BEGIN;
    event.data.screen.version = 5u;
    assert(astraz_screen_grid_event(&grid, &event));
    assert(grid.rows == 0u && cell->codepoint == ' ');
    event.data.screen.version = 6u;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.window = 1;
    event.data.cursor.row = 21;
    event.data.cursor.column = 41;
    assert(astraz_screen_grid_event(&grid, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.window = 1;
    event.data.text.character = 'P';
    assert(astraz_screen_grid_event(&grid, &event));
    assert(cell->codepoint == 'P');
    return 0;
}
