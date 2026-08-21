#ifndef ASTRA_TERMINAL_H
#define ASTRA_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * The terminal cell model.
 *
 * This owns what a terminal *is* -- a grid of cells, a cursor, and the rules
 * for what a byte does to them -- and nothing about what draws it. It has no
 * geometry constant, no pixel metric, and no device access, because the thing
 * that renders it is expected to change: the character plane the kernel owns
 * today, glyphs blitted by a display service later. A renderer is a callback
 * that receives a run of cells and their position; everything above it stays.
 *
 * Dimensions are supplied at initialisation and bounded by the caller's own
 * storage, so the same model serves an 90x30 hardware plane and whatever a
 * window turns out to be.
 */

#define ASTRA_TERMINAL_COLUMNS_MAX 128u
#define ASTRA_TERMINAL_ROWS_MAX 64u
#define ASTRA_TERMINAL_TAB_WIDTH 8u

typedef enum AstraTerminalStatus {
    ASTRA_TERMINAL_OK = 0,
    ASTRA_TERMINAL_INVALID_ARGUMENT = 1,
    ASTRA_TERMINAL_TOO_LARGE = 2,
    ASTRA_TERMINAL_RENDER_FAILED = 3
} AstraTerminalStatus;

/*
 * Draws one run of cells starting at (row, column). Returns non-zero on
 * success. A run never crosses a row, so a renderer never has to reason about
 * wrapping.
 */
typedef int (*AstraTerminalRender)(void *context, uint32_t row,
                                   uint32_t column, const uint8_t *cells,
                                   uint32_t count);

/* Moves already-rendered rows upward before changed rows are redrawn. */
typedef int (*AstraTerminalScroll)(void *context, uint32_t rows,
                                   uint32_t preserved_rows);

/*
 * Receives each completed line as it is written, before anything draws it.
 * A terminal on a screen is only observable by looking at the screen, and
 * once the renderer is glyphs in a window there is no longer any text for a
 * harness, a serial line or a log to read. The model already sees every byte
 * exactly once, so this is where that text comes from. A line is delivered on
 * a newline or when it fills the width; `length` counts printable bytes only
 * and the buffer is not terminated by this call's contract.
 */
typedef void (*AstraTerminalEcho)(void *context, const char *line,
                                  uint32_t length);

typedef struct AstraTerminal {
    uint8_t cells[ASTRA_TERMINAL_ROWS_MAX][ASTRA_TERMINAL_COLUMNS_MAX];
    /* Inclusive range of columns changed since the last flush, per row. */
    uint16_t dirty_first[ASTRA_TERMINAL_ROWS_MAX];
    uint16_t dirty_last[ASTRA_TERMINAL_ROWS_MAX];
    uint32_t columns;
    uint32_t rows;
    uint32_t cursor_row;
    uint32_t cursor_column;
    uint32_t scrolls;
    uint32_t pending_scrolls;
    uint32_t scroll_redraw_from;
    AstraTerminalRender render;
    AstraTerminalScroll scroll;
    void *render_context;
    AstraTerminalEcho echo;
    void *echo_context;
    /* One line being assembled for `echo`; never read by the model itself. */
    char echo_line[ASTRA_TERMINAL_COLUMNS_MAX];
    uint32_t echo_length;
} AstraTerminal;

AstraTerminalStatus astra_terminal_init(AstraTerminal *terminal,
                                        uint32_t columns, uint32_t rows,
                                        AstraTerminalRender render,
                                        void *render_context);
AstraTerminalStatus astra_terminal_resize(AstraTerminal *terminal,
                                          uint32_t columns, uint32_t rows);

/* Clears every cell and homes the cursor. */
void astra_terminal_clear(AstraTerminal *terminal);

/*
 * Applies one byte. Handles \n, \r, \b and \t; anything else printable
 * occupies a cell. Bytes outside 0x20..0x7e are drawn as '.' rather than
 * dropped, so a bad read is visible instead of invisible.
 */
void astra_terminal_putc(AstraTerminal *terminal, uint8_t value);

/*
 * Installs the echo, or clears it with a NULL callback. Independent of the
 * renderer on purpose: what draws a terminal and what records it are two
 * different jobs, and a headless machine wants the second without the first.
 */
void astra_terminal_set_echo(AstraTerminal *terminal, AstraTerminalEcho echo,
                             void *context);
void astra_terminal_set_scroll(AstraTerminal *terminal,
                               AstraTerminalScroll scroll);

void astra_terminal_write(AstraTerminal *terminal, const char *text);
void astra_terminal_write_bytes(AstraTerminal *terminal, const uint8_t *bytes,
                                size_t count);

/* Draws every cell changed since the last flush and clears the damage. */
AstraTerminalStatus astra_terminal_flush(AstraTerminal *terminal);

/* Redraws everything, for a renderer that has just been attached. */
AstraTerminalStatus astra_terminal_redraw(AstraTerminal *terminal);

uint8_t astra_terminal_cell(const AstraTerminal *terminal, uint32_t row,
                            uint32_t column);

#endif
