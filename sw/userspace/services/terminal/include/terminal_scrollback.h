#ifndef ASTRA_TERMINAL_SCROLLBACK_H
#define ASTRA_TERMINAL_SCROLLBACK_H

#include <stddef.h>
#include <stdint.h>

#include <astra/scroll.h>
#include <astra/text_surface.h>

typedef void *(*TerminalScrollbackReallocate)(void *pointer, size_t size);

typedef struct TerminalScrollbackRow {
    uint32_t offset;
    uint32_t columns;
} TerminalScrollbackRow;

typedef struct TerminalScrollback {
    TerminalScrollbackRow *rows;
    AstraTextCell *cells;
    TerminalScrollbackReallocate reallocate;
    AstraScrollModel scroll;
    uint32_t row_count;
    uint32_t row_capacity;
    uint32_t cell_count;
    uint32_t cell_capacity;
    uint32_t viewport_rows;
    uint32_t line_height;
} TerminalScrollback;

int terminal_scrollback_init(TerminalScrollback *backlog,
                             uint32_t viewport_width,
                             uint32_t viewport_rows,
                             uint32_t line_height,
                             TerminalScrollbackReallocate reallocate);
void terminal_scrollback_destroy(TerminalScrollback *backlog);
int terminal_scrollback_append(TerminalScrollback *backlog,
                               const AstraTextCell *cells,
                               uint32_t columns);
int terminal_scrollback_resize(TerminalScrollback *backlog,
                               uint32_t viewport_width,
                               uint32_t viewport_rows);
int terminal_scrollback_wheel(TerminalScrollback *backlog,
                              int32_t delta_x, int32_t delta_y);
int terminal_scrollback_to_bottom(TerminalScrollback *backlog);
int terminal_scrollback_at_bottom(const TerminalScrollback *backlog);
const AstraTextCell *terminal_scrollback_row(
    const TerminalScrollback *backlog, uint32_t row, uint32_t *columns);

#endif
