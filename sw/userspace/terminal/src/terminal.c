#include <astra/terminal.h>

/*
 * Nothing in this file knows what a cell looks like on a screen. It decides
 * which cell holds which byte and which cells changed; a renderer decides
 * what that means in pixels or in MMIO.
 */

#define TERMINAL_BLANK ' '

static void mark(AstraTerminal *terminal, uint32_t row, uint32_t column)
{
    if (terminal->dirty_first[row] > terminal->dirty_last[row]) {
        terminal->dirty_first[row] = (uint16_t)column;
        terminal->dirty_last[row] = (uint16_t)column;
        return;
    }
    if (column < terminal->dirty_first[row])
        terminal->dirty_first[row] = (uint16_t)column;
    if (column > terminal->dirty_last[row])
        terminal->dirty_last[row] = (uint16_t)column;
}

static void mark_row(AstraTerminal *terminal, uint32_t row)
{
    terminal->dirty_first[row] = 0u;
    terminal->dirty_last[row] = (uint16_t)(terminal->columns - 1u);
}

/* An empty range is first > last, which no real range can be. */
static void clear_damage(AstraTerminal *terminal)
{
    uint32_t row;

    for (row = 0u; row < terminal->rows; ++row) {
        terminal->dirty_first[row] = 1u;
        terminal->dirty_last[row] = 0u;
    }
}

static void scroll(AstraTerminal *terminal)
{
    uint32_t row;
    uint32_t column;

    for (row = 1u; row < terminal->rows; ++row) {
        for (column = 0u; column < terminal->columns; ++column)
            terminal->cells[row - 1u][column] = terminal->cells[row][column];
    }
    for (column = 0u; column < terminal->columns; ++column)
        terminal->cells[terminal->rows - 1u][column] = TERMINAL_BLANK;
    for (row = 0u; row < terminal->rows; ++row)
        mark_row(terminal, row);
    ++terminal->scrolls;
}

static void newline(AstraTerminal *terminal)
{
    terminal->cursor_column = 0u;
    if (terminal->cursor_row + 1u < terminal->rows) {
        ++terminal->cursor_row;
        return;
    }
    scroll(terminal);
}

AstraTerminalStatus astra_terminal_init(AstraTerminal *terminal,
                                        uint32_t columns, uint32_t rows,
                                        AstraTerminalRender render,
                                        void *render_context)
{
    if (terminal == NULL || columns == 0u || rows == 0u)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (columns > ASTRA_TERMINAL_COLUMNS_MAX ||
        rows > ASTRA_TERMINAL_ROWS_MAX)
        return ASTRA_TERMINAL_TOO_LARGE;
    terminal->columns = columns;
    terminal->rows = rows;
    terminal->render = render;
    terminal->render_context = render_context;
    terminal->echo = NULL;
    terminal->echo_context = NULL;
    terminal->echo_length = 0u;
    terminal->scrolls = 0u;
    astra_terminal_clear(terminal);
    return ASTRA_TERMINAL_OK;
}

AstraTerminalStatus astra_terminal_resize(AstraTerminal *terminal,
                                          uint32_t columns, uint32_t rows)
{
    uint32_t old_columns;
    uint32_t old_rows;

    if (terminal == NULL || columns == 0u || rows == 0u)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (columns > ASTRA_TERMINAL_COLUMNS_MAX ||
        rows > ASTRA_TERMINAL_ROWS_MAX)
        return ASTRA_TERMINAL_TOO_LARGE;
    old_columns = terminal->columns;
    old_rows = terminal->rows;
    if (columns > old_columns)
        for (uint32_t row = 0u; row < old_rows && row < rows; ++row)
            for (uint32_t column = old_columns; column < columns; ++column)
                terminal->cells[row][column] = TERMINAL_BLANK;
    if (rows > old_rows)
        for (uint32_t row = old_rows; row < rows; ++row)
            for (uint32_t column = 0u; column < columns; ++column)
                terminal->cells[row][column] = TERMINAL_BLANK;
    terminal->columns = columns;
    terminal->rows = rows;
    if (terminal->cursor_row >= rows)
        terminal->cursor_row = rows - 1u;
    if (terminal->cursor_column >= columns)
        terminal->cursor_column = columns - 1u;
    for (uint32_t row = 0u; row < rows; ++row)
        mark_row(terminal, row);
    return ASTRA_TERMINAL_OK;
}

void astra_terminal_clear(AstraTerminal *terminal)
{
    uint32_t row;
    uint32_t column;

    if (terminal == NULL)
        return;
    for (row = 0u; row < terminal->rows; ++row) {
        for (column = 0u; column < terminal->columns; ++column)
            terminal->cells[row][column] = TERMINAL_BLANK;
        mark_row(terminal, row);
    }
    terminal->cursor_row = 0u;
    terminal->cursor_column = 0u;
}

/* Hands the assembled line to the echo and starts the next one. */
static void echo_flush(AstraTerminal *terminal)
{
    uint32_t length = terminal->echo_length;

    terminal->echo_length = 0u;
    if (terminal->echo != NULL && length != 0u)
        terminal->echo(terminal->echo_context, terminal->echo_line, length);
}

static void echo_putc(AstraTerminal *terminal, uint8_t value)
{
    if (terminal->echo == NULL)
        return;
    if (terminal->echo_length >= sizeof(terminal->echo_line))
        echo_flush(terminal);
    terminal->echo_line[terminal->echo_length++] = (char)value;
}

void astra_terminal_set_echo(AstraTerminal *terminal, AstraTerminalEcho echo,
                             void *context)
{
    if (terminal == NULL)
        return;
    echo_flush(terminal);
    terminal->echo = echo;
    terminal->echo_context = context;
}

void astra_terminal_putc(AstraTerminal *terminal, uint8_t value)
{
    if (terminal == NULL)
        return;
    switch (value) {
    case '\n':
        echo_flush(terminal);
        newline(terminal);
        return;
    case '\r':
        /*
         * The line being echoed is discarded, not delivered: a carriage
         * return means what was written is about to be written over, and a
         * line editor redrawing its line after every keystroke would
         * otherwise record every prefix of it. What reaches the echo is the
         * last version -- which is what the screen shows.
         */
        terminal->echo_length = 0u;
        terminal->cursor_column = 0u;
        return;
    case '\b':
        /* The line being echoed loses the byte the cursor is leaving. */
        if (terminal->echo_length != 0u)
            --terminal->echo_length;
        /*
         * Moves back without erasing. A shell that wants the character gone
         * writes space and backs up again, which is what a real terminal
         * makes it do.
         */
        if (terminal->cursor_column != 0u)
            --terminal->cursor_column;
        else if (terminal->cursor_row != 0u) {
            --terminal->cursor_row;
            terminal->cursor_column = terminal->columns - 1u;
        }
        return;
    case '\t': {
        uint32_t next = (terminal->cursor_column / ASTRA_TERMINAL_TAB_WIDTH +
                         1u) * ASTRA_TERMINAL_TAB_WIDTH;

        while (terminal->cursor_column < next &&
               terminal->cursor_column < terminal->columns)
            astra_terminal_putc(terminal, ' ');
        return;
    }
    default:
        break;
    }

    /*
     * Anything unprintable is drawn rather than dropped: a byte that should
     * not be there is a fact worth seeing on the screen.
     */
    if (value < 0x20u || value > 0x7eu)
        value = (uint8_t)'.';

    echo_putc(terminal, value);
    if (terminal->cursor_column >= terminal->columns)
        newline(terminal);
    terminal->cells[terminal->cursor_row][terminal->cursor_column] = value;
    mark(terminal, terminal->cursor_row, terminal->cursor_column);
    ++terminal->cursor_column;
}

void astra_terminal_write_bytes(AstraTerminal *terminal, const uint8_t *bytes,
                                size_t count)
{
    size_t index;

    if (terminal == NULL || bytes == NULL)
        return;
    for (index = 0u; index < count; ++index)
        astra_terminal_putc(terminal, bytes[index]);
}

void astra_terminal_write(AstraTerminal *terminal, const char *text)
{
    if (terminal == NULL || text == NULL)
        return;
    while (*text != '\0')
        astra_terminal_putc(terminal, (uint8_t)*text++);
}

AstraTerminalStatus astra_terminal_flush(AstraTerminal *terminal)
{
    uint32_t row;

    if (terminal == NULL)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (terminal->render == NULL) {
        clear_damage(terminal);
        return ASTRA_TERMINAL_OK;
    }
    for (row = 0u; row < terminal->rows; ++row) {
        uint32_t first = terminal->dirty_first[row];
        uint32_t last = terminal->dirty_last[row];

        if (first > last)
            continue;
        if (!terminal->render(terminal->render_context, row, first,
                              &terminal->cells[row][first],
                              last - first + 1u))
            return ASTRA_TERMINAL_RENDER_FAILED;
        terminal->dirty_first[row] = 1u;
        terminal->dirty_last[row] = 0u;
    }
    return ASTRA_TERMINAL_OK;
}

AstraTerminalStatus astra_terminal_redraw(AstraTerminal *terminal)
{
    uint32_t row;

    if (terminal == NULL)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    for (row = 0u; row < terminal->rows; ++row)
        mark_row(terminal, row);
    return astra_terminal_flush(terminal);
}

uint8_t astra_terminal_cell(const AstraTerminal *terminal, uint32_t row,
                            uint32_t column)
{
    if (terminal == NULL || row >= terminal->rows ||
        column >= terminal->columns)
        return 0u;
    return terminal->cells[row][column];
}
