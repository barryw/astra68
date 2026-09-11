#ifndef ASTRA_TERMINAL_H
#define ASTRA_TERMINAL_H

/** @file terminal.h @brief UTF-8 ECMA-48 terminal cell model. */

#include <stddef.h>
#include <stdint.h>

#include <astra/text_surface.h>

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

/** Default tab-stop width in character cells. */
#define ASTRA_TERMINAL_TAB_WIDTH 8u
/** Required alignment of terminal backing storage. */
#define ASTRA_TERMINAL_STORAGE_ALIGNMENT 4u
/** Maximum CSI parameter count retained by the parser. */
#define ASTRA_TERMINAL_CSI_PARAMETERS 32u
/** Constant-expression storage requirement for fixed dimensions. */
#define ASTRA_TERMINAL_STORAGE_BYTES(columns, rows)                         \
    ((size_t)(ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u) +                     \
     (size_t)(rows) * (size_t)(columns) *                                  \
         2u * sizeof(AstraTextCell) +                                      \
     (size_t)(rows) * 2u * sizeof(uint32_t) +                              \
     (size_t)(columns) * 4u)

/** Terminal model status. */
typedef enum AstraTerminalStatus {
    ASTRA_TERMINAL_OK = 0,
    ASTRA_TERMINAL_INVALID_ARGUMENT = 1,
    ASTRA_TERMINAL_STORAGE_TOO_SMALL = 2,
    ASTRA_TERMINAL_RENDER_FAILED = 3
} AstraTerminalStatus;

/**
 * Draws one run of cells starting at (row, column). Returns non-zero on
 * success. A run never crosses a row, so a renderer never has to reason about
 * wrapping.
 * @param context Renderer-defined context.
 * @param row Cell row.
 * @param column First cell column.
 * @param cells Cells in the run.
 * @param count Cell count.
 * @return Nonzero on success.
 */
typedef int (*AstraTerminalRender)(void *context, uint32_t row,
                                   uint32_t column,
                                   const AstraTextCell *cells,
                                   uint32_t count);

/** Move already-rendered rows upward before redraw.
 * @param context Renderer-defined context.
 * @param rows Rows to scroll.
 * @param preserved_rows Rows preserved by the operation.
 * @return Nonzero on success.
 */
typedef int (*AstraTerminalScroll)(void *context, uint32_t rows,
                                   uint32_t preserved_rows);

/**
 * Receives each completed line as it is written, before anything draws it.
 * A terminal on a screen is only observable by looking at the screen, and
 * once the renderer is glyphs in a window there is no longer any text for a
 * harness, a serial line or a log to read. The model already sees every byte
 * exactly once, so this is where that text comes from. A line is delivered on
 * a newline or when it fills the width; `length` counts printable bytes only
 * and the buffer is not terminated by this call's contract.
 * @param context Observer-defined context.
 * @param line Completed line bytes.
 * @param length Printable byte count.
 */
typedef void (*AstraTerminalEcho)(void *context, const char *line,
                                  uint32_t length);
/** Send a terminal-generated response. @param context Sink context. @param bytes Response bytes. @param length Byte count. @return Nonzero on success. */
typedef int (*AstraTerminalReply)(void *context, const uint8_t *bytes,
                                  uint32_t length);
/** Observe a semantic prompt boundary. @param context Observer context. */
typedef void (*AstraTerminalPrompt)(void *context);

/** Caller-owned terminal model and rendering callbacks. */
typedef struct AstraTerminal {
    AstraTextCell *cells; /**< Currently active cell buffer. */
    AstraTextCell *primary_cells; /**< Primary-screen cells. */
    AstraTextCell *alternate_cells; /**< Alternate-screen cells. */
    /* Inclusive range of columns changed since the last flush, per row. */
    uint32_t *dirty_first; /**< First damaged column per row. */
    uint32_t *dirty_last; /**< Last damaged column per row. */
    char *echo_line; /**< Line assembly buffer for the echo callback. */
    void *storage; /**< Caller-owned backing storage. */
    size_t storage_size; /**< Bytes available in storage. */
    uint32_t capacity_columns; /**< Maximum columns without reallocation. */
    uint32_t capacity_rows; /**< Maximum rows without reallocation. */
    uint32_t columns; /**< Visible columns. */
    uint32_t rows; /**< Visible rows. */
    uint32_t cursor_row; /**< Cursor row. */
    uint32_t cursor_column; /**< Cursor column. */
    uint32_t scrolls; /**< Completed full-screen scroll count. */
    uint32_t pending_scrolls; /**< Scrolls awaiting renderer consumption. */
    uint32_t scroll_redraw_from; /**< First row requiring redraw after scroll. */
    uint32_t scroll_top; /**< Inclusive scroll-region top. */
    uint32_t scroll_bottom; /**< Inclusive scroll-region bottom. */
    uint32_t saved_cursor_row; /**< Saved cursor row. */
    uint32_t saved_cursor_column; /**< Saved cursor column. */
    uint32_t csi_parameters[ASTRA_TERMINAL_CSI_PARAMETERS]; /**< CSI parameters. */
    uint32_t csi_parameter_count; /**< Parsed CSI parameter count. */
    uint32_t utf8_codepoint; /**< UTF-8 decoder accumulator. */
    uint32_t foreground; /**< Current foreground RGB color. */
    uint32_t background; /**< Current background RGB color. */
    uint16_t attributes; /**< Current ASTRA_TEXT_* attributes. */
    uint8_t parser_state; /**< ECMA-48 parser state. */
    uint8_t csi_private; /**< Nonzero for a private CSI sequence. */
    uint8_t csi_overflow; /**< Nonzero after excess CSI parameters. */
    uint8_t utf8_remaining; /**< Continuation bytes remaining. */
    uint8_t utf8_expected; /**< Continuation bytes expected. */
    uint8_t osc_prompt_match; /**< OSC 133 prompt-marker parser state. */
    uint8_t cursor_visible; /**< Nonzero when the cursor should render. */
    uint8_t alternate_screen; /**< Nonzero when alternate cells are active. */
    uint8_t echo_carriage_return; /**< Echo line has a pending carriage return. */
    AstraTerminalRender render; /**< Cell rendering callback. */
    AstraTerminalScroll scroll; /**< Hardware/software scroll callback. */
    void *render_context; /**< Context passed to render and scroll. */
    AstraTerminalEcho echo; /**< Completed-line observer. */
    void *echo_context; /**< Context passed to echo. */
    AstraTerminalReply reply; /**< Terminal response sink. */
    void *reply_context; /**< Context passed to reply. */
    AstraTerminalPrompt prompt; /**< Semantic prompt observer. */
    void *prompt_context; /**< Context passed to prompt. */
    /* One line being assembled for `echo`; never read by the model itself. */
    uint32_t echo_length; /**< Bytes assembled in echo_line. */
    uint32_t echo_columns; /**< Display columns assembled in echo_line. */
    uint32_t reply_failures; /**< Failed terminal-response deliveries. */
} AstraTerminal;

/** Calculate required backing storage. @param columns Maximum columns. @param rows Maximum rows. @param bytes Receives required bytes. @return Terminal status. */
AstraTerminalStatus astra_terminal_storage_size(uint32_t columns,
                                                uint32_t rows,
                                                size_t *bytes);
/** Initialize a terminal at fixed capacity. @param terminal Model to initialize. @param columns Columns. @param rows Rows. @param storage Backing storage. @param storage_size Backing bytes. @param render Render callback. @param render_context Callback context. @return Terminal status. */
AstraTerminalStatus astra_terminal_init(AstraTerminal *terminal,
                                        uint32_t columns, uint32_t rows,
                                        void *storage, size_t storage_size,
                                        AstraTerminalRender render,
                                        void *render_context);
/** Initialize with visible and maximum dimensions. @param terminal Model to initialize. @param columns Visible columns. @param rows Visible rows. @param capacity_columns Maximum columns. @param capacity_rows Maximum rows. @param storage Backing storage. @param storage_size Backing bytes. @param render Render callback. @param render_context Callback context. @return Terminal status. */
AstraTerminalStatus astra_terminal_init_capacity(
    AstraTerminal *terminal, uint32_t columns, uint32_t rows,
    uint32_t capacity_columns, uint32_t capacity_rows,
    void *storage, size_t storage_size, AstraTerminalRender render,
    void *render_context);
/** Resize, optionally replacing backing storage. @param terminal Initialized model. @param columns New columns. @param rows New rows. @param storage Backing storage. @param storage_size Backing bytes. @return Terminal status. */
AstraTerminalStatus astra_terminal_resize(AstraTerminal *terminal,
                                          uint32_t columns, uint32_t rows,
                                          void *storage,
                                          size_t storage_size);

/** Clear every cell and home the cursor. @param terminal Initialized model. */
void astra_terminal_clear(AstraTerminal *terminal);

/**
 * Applies one byte of a UTF-8/ECMA-48 stream. Invalid UTF-8 becomes U+FFFD;
 * control and escape bytes update terminal state without becoming glyphs.
 * @param terminal Initialized model.
 * @param value Next byte.
 */
void astra_terminal_putc(AstraTerminal *terminal, uint8_t value);

/**
 * Installs the echo, or clears it with a NULL callback. Independent of the
 * renderer on purpose: what draws a terminal and what records it are two
 * different jobs, and a headless machine wants the second without the first.
 * @param terminal Initialized model.
 * @param echo Observer callback, or NULL.
 * @param context Observer context.
 */
void astra_terminal_set_echo(AstraTerminal *terminal, AstraTerminalEcho echo,
                             void *context);
/** Install terminal-response delivery. @param terminal Initialized model. @param reply Reply callback, or NULL. @param context Callback context. */
void astra_terminal_set_reply(AstraTerminal *terminal,
                              AstraTerminalReply reply, void *context);
/** Observe OSC 133;B prompt-end markers. @param terminal Initialized model. @param prompt Observer callback, or NULL. @param context Observer context. */
void astra_terminal_set_prompt(AstraTerminal *terminal,
                               AstraTerminalPrompt prompt, void *context);
/** Install accelerated scroll delivery. @param terminal Initialized model. @param scroll Scroll callback, or NULL. */
void astra_terminal_set_scroll(AstraTerminal *terminal,
                               AstraTerminalScroll scroll);

/** Write a NUL-terminated UTF-8 stream. @param terminal Initialized model. @param text Text to apply. */
void astra_terminal_write(AstraTerminal *terminal, const char *text);
/** Write explicit UTF-8/ECMA-48 bytes. @param terminal Initialized model. @param bytes Bytes to apply. @param count Byte count. */
void astra_terminal_write_bytes(AstraTerminal *terminal, const uint8_t *bytes,
                                size_t count);

/** Draw changed cells and clear damage. @param terminal Initialized model. @return Terminal status. */
AstraTerminalStatus astra_terminal_flush(AstraTerminal *terminal);

/** Redraw every cell. @param terminal Initialized model. @return Terminal status. */
AstraTerminalStatus astra_terminal_redraw(AstraTerminal *terminal);

/** Read one packed cell. @param terminal Initialized model. @param row Cell row. @param column Cell column. @return Packed cell, or zero out of range. */
uint32_t astra_terminal_cell(const AstraTerminal *terminal, uint32_t row,
                             uint32_t column);
/** Borrow one cell. @param terminal Initialized model. @param row Cell row. @param column Cell column. @return Cell pointer, or NULL out of range. */
const AstraTextCell *astra_terminal_cell_at(
    const AstraTerminal *terminal, uint32_t row, uint32_t column);

#endif
