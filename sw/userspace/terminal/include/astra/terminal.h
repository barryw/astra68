#ifndef ASTRA_TERMINAL_H
#define ASTRA_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * The terminal cell model.
 *
 * This owns what a terminal *is* -- a grid of cells, a cursor, and the rules
 * for what an input stream does to them -- and nothing about what draws it. It has no
 * geometry constant, no pixel metric, and no device access, because the thing
 * that renders it is expected to change: the character plane the kernel owns
 * today, glyphs blitted by a display service later. A renderer is a callback
 * that receives a run of cells and their position; everything above it stays.
 *
 * Dimensions are supplied at initialisation and bounded by the caller's own
 * storage, so the same model serves an 90x30 hardware plane and whatever a
 * window turns out to be.
 */

#define ASTRA_TERMINAL_TAB_WIDTH 8u
#define ASTRA_TERMINAL_STORAGE_ALIGNMENT 4u
#define ASTRA_TERMINAL_CSI_PARAMETERS 32u
#define ASTRA_TERMINAL_COLOR_DEFAULT UINT32_MAX
#define ASTRA_TERMINAL_COLOR_RGB(red, green, blue)                          \
    (UINT32_C(0x01000000) | ((uint32_t)(red) << 16u) |                     \
     ((uint32_t)(green) << 8u) | (uint32_t)(blue))
#define ASTRA_TERMINAL_COLOR_IS_RGB(color)                                  \
    (((color) & UINT32_C(0xff000000)) == UINT32_C(0x01000000))

enum {
    ASTRA_TERMINAL_BOLD = 1u << 0,
    ASTRA_TERMINAL_FAINT = 1u << 1,
    ASTRA_TERMINAL_ITALIC = 1u << 2,
    ASTRA_TERMINAL_UNDERLINE = 1u << 3,
    ASTRA_TERMINAL_BLINK = 1u << 4,
    ASTRA_TERMINAL_INVERSE = 1u << 5,
    ASTRA_TERMINAL_HIDDEN = 1u << 6,
    ASTRA_TERMINAL_STRIKE = 1u << 7,
};

typedef struct AstraTerminalCell {
    uint32_t codepoint;
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
    uint8_t width;
    uint8_t reserved;
} AstraTerminalCell;

/* Constant-expression form for fixed test and bootstrap storage. */
#define ASTRA_TERMINAL_STORAGE_BYTES(columns, rows)                         \
    ((size_t)(ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u) +                     \
     (size_t)(rows) * (size_t)(columns) *                                  \
         2u * sizeof(AstraTerminalCell) +                                  \
     (size_t)(rows) * 2u * sizeof(uint32_t) +                              \
     (size_t)(columns) * 4u)

typedef enum AstraTerminalStatus {
    ASTRA_TERMINAL_OK = 0,
    ASTRA_TERMINAL_INVALID_ARGUMENT = 1,
    ASTRA_TERMINAL_STORAGE_TOO_SMALL = 2,
    ASTRA_TERMINAL_RENDER_FAILED = 3
} AstraTerminalStatus;

/*
 * Draws one run of cells starting at (row, column). Returns non-zero on
 * success. A run never crosses a row, so a renderer never has to reason about
 * wrapping.
 */
typedef int (*AstraTerminalRender)(void *context, uint32_t row,
                                   uint32_t column,
                                   const AstraTerminalCell *cells,
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
typedef int (*AstraTerminalReply)(void *context, const uint8_t *bytes,
                                  uint32_t length);

typedef struct AstraTerminal {
    AstraTerminalCell *cells;
    AstraTerminalCell *primary_cells;
    AstraTerminalCell *alternate_cells;
    /* Inclusive range of columns changed since the last flush, per row. */
    uint32_t *dirty_first;
    uint32_t *dirty_last;
    char *echo_line;
    void *storage;
    size_t storage_size;
    uint32_t capacity_columns;
    uint32_t capacity_rows;
    uint32_t columns;
    uint32_t rows;
    uint32_t cursor_row;
    uint32_t cursor_column;
    uint32_t scrolls;
    uint32_t pending_scrolls;
    uint32_t scroll_redraw_from;
    uint32_t scroll_top;
    uint32_t scroll_bottom;
    uint32_t saved_cursor_row;
    uint32_t saved_cursor_column;
    uint32_t csi_parameters[ASTRA_TERMINAL_CSI_PARAMETERS];
    uint32_t csi_parameter_count;
    uint32_t utf8_codepoint;
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
    uint8_t parser_state;
    uint8_t csi_private;
    uint8_t csi_overflow;
    uint8_t utf8_remaining;
    uint8_t utf8_expected;
    uint8_t cursor_visible;
    uint8_t alternate_screen;
    uint8_t echo_carriage_return;
    AstraTerminalRender render;
    AstraTerminalScroll scroll;
    void *render_context;
    AstraTerminalEcho echo;
    void *echo_context;
    AstraTerminalReply reply;
    void *reply_context;
    /* One line being assembled for `echo`; never read by the model itself. */
    uint32_t echo_length;
    uint32_t echo_columns;
    uint32_t reply_failures;
} AstraTerminal;

AstraTerminalStatus astra_terminal_storage_size(uint32_t columns,
                                                uint32_t rows,
                                                size_t *bytes);
AstraTerminalStatus astra_terminal_init(AstraTerminal *terminal,
                                        uint32_t columns, uint32_t rows,
                                        void *storage, size_t storage_size,
                                        AstraTerminalRender render,
                                        void *render_context);
AstraTerminalStatus astra_terminal_init_capacity(
    AstraTerminal *terminal, uint32_t columns, uint32_t rows,
    uint32_t capacity_columns, uint32_t capacity_rows,
    void *storage, size_t storage_size, AstraTerminalRender render,
    void *render_context);
AstraTerminalStatus astra_terminal_resize(AstraTerminal *terminal,
                                          uint32_t columns, uint32_t rows,
                                          void *storage,
                                          size_t storage_size);

/* Clears every cell and homes the cursor. */
void astra_terminal_clear(AstraTerminal *terminal);

/*
 * Applies one byte of a UTF-8/ECMA-48 stream. Invalid UTF-8 becomes U+FFFD;
 * control and escape bytes update terminal state without becoming glyphs.
 */
void astra_terminal_putc(AstraTerminal *terminal, uint8_t value);

/*
 * Installs the echo, or clears it with a NULL callback. Independent of the
 * renderer on purpose: what draws a terminal and what records it are two
 * different jobs, and a headless machine wants the second without the first.
 */
void astra_terminal_set_echo(AstraTerminal *terminal, AstraTerminalEcho echo,
                             void *context);
void astra_terminal_set_reply(AstraTerminal *terminal,
                              AstraTerminalReply reply, void *context);
void astra_terminal_set_scroll(AstraTerminal *terminal,
                               AstraTerminalScroll scroll);

void astra_terminal_write(AstraTerminal *terminal, const char *text);
void astra_terminal_write_bytes(AstraTerminal *terminal, const uint8_t *bytes,
                                size_t count);

/* Draws every cell changed since the last flush and clears the damage. */
AstraTerminalStatus astra_terminal_flush(AstraTerminal *terminal);

/* Redraws everything, for a renderer that has just been attached. */
AstraTerminalStatus astra_terminal_redraw(AstraTerminal *terminal);

uint32_t astra_terminal_cell(const AstraTerminal *terminal, uint32_t row,
                             uint32_t column);
const AstraTerminalCell *astra_terminal_cell_at(
    const AstraTerminal *terminal, uint32_t row, uint32_t column);

#endif
