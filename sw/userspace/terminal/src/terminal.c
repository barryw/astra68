#include <astra/terminal.h>
#include <astra/utf8.h>

#include <stdint.h>

/*
 * Nothing in this file knows what a cell looks like on a screen. It decides
 * which cell holds which scalar and attributes, and which cells changed; a renderer decides
 * what that means in pixels or in MMIO. Storage belongs to the caller so its
 * normal process accounting, recovery reserve and teardown rules apply.
 */

#define TERMINAL_BLANK ' '
#define CSI_MISSING UINT32_MAX

enum {
    PARSER_GROUND = 0u,
    PARSER_ESCAPE,
    PARSER_CSI,
    PARSER_OSC,
    PARSER_OSC_ESCAPE,
    PARSER_CHARSET,
};

static AstraTerminalCell blank_cell(const AstraTerminal *terminal)
{
    AstraTerminalCell blank = {
        TERMINAL_BLANK,
        terminal->foreground,
        terminal->background,
        terminal->attributes,
        1u,
        0u,
    };

    return blank;
}

static AstraTerminalCell *cell(AstraTerminal *terminal, uint32_t row,
                               uint32_t column)
{
    return terminal->cells + (size_t)row * terminal->capacity_columns +
           column;
}

static const AstraTerminalCell *const_cell(const AstraTerminal *terminal,
                                           uint32_t row, uint32_t column)
{
    return terminal->cells + (size_t)row * terminal->capacity_columns +
           column;
}

static void mark(AstraTerminal *terminal, uint32_t row, uint32_t column)
{
    if (terminal->dirty_first[row] > terminal->dirty_last[row]) {
        terminal->dirty_first[row] = column;
        terminal->dirty_last[row] = column;
        return;
    }
    if (column < terminal->dirty_first[row])
        terminal->dirty_first[row] = column;
    if (column > terminal->dirty_last[row])
        terminal->dirty_last[row] = column;
}

static void mark_row(AstraTerminal *terminal, uint32_t row)
{
    terminal->dirty_first[row] = 0u;
    terminal->dirty_last[row] = terminal->columns - 1u;
}

/* An empty range is first > last, which no real range can be. */
static void clear_damage(AstraTerminal *terminal)
{
    for (uint32_t row = 0u; row < terminal->rows; ++row) {
        terminal->dirty_first[row] = 1u;
        terminal->dirty_last[row] = 0u;
    }
}

static uint32_t first_dirty_row(const AstraTerminal *terminal)
{
    for (uint32_t row = 0u; row < terminal->rows; ++row)
        if (terminal->dirty_first[row] <= terminal->dirty_last[row])
            return row;
    return terminal->rows;
}

static void scroll_full_one(AstraTerminal *terminal)
{
    uint32_t dirty = first_dirty_row(terminal);

    for (uint32_t row = 1u; row < terminal->rows; ++row)
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row - 1u, column) =
                *cell(terminal, row, column);
    for (uint32_t column = 0u; column < terminal->columns; ++column)
        *cell(terminal, terminal->rows - 1u, column) = blank_cell(terminal);
    if (terminal->scroll != NULL) {
        if (terminal->pending_scrolls < terminal->rows)
            ++terminal->pending_scrolls;
        if (terminal->pending_scrolls >= terminal->rows || dirty == 0u)
            terminal->scroll_redraw_from = 0u;
        else
            terminal->scroll_redraw_from = dirty == terminal->rows ?
                terminal->rows - 1u : dirty - 1u;
        clear_damage(terminal);
        for (uint32_t row = terminal->scroll_redraw_from;
             row < terminal->rows; ++row)
            mark_row(terminal, row);
    } else {
        for (uint32_t row = 0u; row < terminal->rows; ++row)
            mark_row(terminal, row);
    }
    ++terminal->scrolls;
}

static void scroll_up(AstraTerminal *terminal, uint32_t top, uint32_t bottom,
                      uint32_t count)
{
    uint32_t height = bottom - top + 1u;

    if (count > height)
        count = height;
    if (top == 0u && bottom + 1u == terminal->rows && count == 1u) {
        scroll_full_one(terminal);
        return;
    }
    terminal->pending_scrolls = 0u;
    for (uint32_t row = top; row + count <= bottom; ++row)
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row, column) =
                *cell(terminal, row + count, column);
    for (uint32_t row = bottom - count + 1u; row <= bottom; ++row)
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row, column) = blank_cell(terminal);
    for (uint32_t row = top; row <= bottom; ++row)
        mark_row(terminal, row);
    terminal->scrolls += count;
}

static void scroll_down(AstraTerminal *terminal, uint32_t top,
                        uint32_t bottom, uint32_t count)
{
    uint32_t height = bottom - top + 1u;

    if (count > height)
        count = height;
    terminal->pending_scrolls = 0u;
    for (uint32_t row = bottom + 1u - count; row-- > top;)
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row + count, column) =
                *cell(terminal, row, column);
    for (uint32_t row = top; row < top + count; ++row)
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row, column) = blank_cell(terminal);
    for (uint32_t row = top; row <= bottom; ++row)
        mark_row(terminal, row);
}

static void newline(AstraTerminal *terminal)
{
    terminal->cursor_column = 0u;
    if (terminal->cursor_row < terminal->scroll_top)
        terminal->cursor_row = terminal->scroll_top;
    if (terminal->cursor_row < terminal->scroll_bottom) {
        ++terminal->cursor_row;
        return;
    }
    if (terminal->cursor_row == terminal->scroll_bottom)
        scroll_up(terminal, terminal->scroll_top, terminal->scroll_bottom, 1u);
}

static int add_size(size_t *total, size_t count, size_t width)
{
    if (count != 0u && width > (SIZE_MAX - *total) / count)
        return 0;
    *total += count * width;
    return 1;
}

AstraTerminalStatus astra_terminal_storage_size(uint32_t columns,
                                                uint32_t rows,
                                                size_t *bytes)
{
    size_t total = ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u;
    size_t cells = 0u;

    if (columns == 0u || rows == 0u || bytes == NULL)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (!add_size(&cells, rows, columns) ||
        !add_size(&total, rows, 2u * sizeof(uint32_t)) ||
        !add_size(&total, cells, sizeof(AstraTerminalCell)) ||
        !add_size(&total, cells, sizeof(AstraTerminalCell)) ||
        !add_size(&total, columns, 4u))
        return ASTRA_TERMINAL_STORAGE_TOO_SMALL;
    *bytes = total;
    return ASTRA_TERMINAL_OK;
}

static int bind_storage(AstraTerminal *terminal, uint32_t columns,
                        uint32_t rows, void *storage, size_t storage_size)
{
    uintptr_t start = (uintptr_t)storage;
    uintptr_t aligned = (start + ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u) &
                        ~(uintptr_t)(ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u);
    size_t padding;
    size_t required;
    uint8_t *at;

    if (storage == NULL || aligned < start ||
        astra_terminal_storage_size(columns, rows, &required) !=
            ASTRA_TERMINAL_OK)
        return 0;
    padding = (size_t)(aligned - start);
    required -= ASTRA_TERMINAL_STORAGE_ALIGNMENT - 1u;
    if (padding > storage_size || required > storage_size - padding)
        return 0;
    at = (uint8_t *)aligned;
    terminal->dirty_first = (uint32_t *)(void *)at;
    at += (size_t)rows * sizeof(uint32_t);
    terminal->dirty_last = (uint32_t *)(void *)at;
    at += (size_t)rows * sizeof(uint32_t);
    terminal->primary_cells = (AstraTerminalCell *)(void *)at;
    at += (size_t)rows * columns * sizeof(AstraTerminalCell);
    terminal->alternate_cells = (AstraTerminalCell *)(void *)at;
    at += (size_t)rows * columns * sizeof(AstraTerminalCell);
    terminal->cells = terminal->primary_cells;
    terminal->echo_line = (char *)(void *)at;
    terminal->storage = storage;
    terminal->storage_size = storage_size;
    terminal->capacity_columns = columns;
    terminal->capacity_rows = rows;
    return 1;
}

AstraTerminalStatus astra_terminal_init(AstraTerminal *terminal,
                                        uint32_t columns, uint32_t rows,
                                        void *storage, size_t storage_size,
                                        AstraTerminalRender render,
                                        void *render_context)
{
    return astra_terminal_init_capacity(
        terminal, columns, rows, columns, rows, storage, storage_size,
        render, render_context);
}

AstraTerminalStatus astra_terminal_init_capacity(
    AstraTerminal *terminal, uint32_t columns, uint32_t rows,
    uint32_t capacity_columns, uint32_t capacity_rows,
    void *storage, size_t storage_size, AstraTerminalRender render,
    void *render_context)
{
    if (terminal == NULL || columns == 0u || rows == 0u ||
        columns > capacity_columns || rows > capacity_rows)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (!bind_storage(terminal, capacity_columns, capacity_rows,
                      storage, storage_size))
        return ASTRA_TERMINAL_STORAGE_TOO_SMALL;
    terminal->columns = columns;
    terminal->rows = rows;
    terminal->render = render;
    terminal->scroll = NULL;
    terminal->render_context = render_context;
    terminal->echo = NULL;
    terminal->echo_context = NULL;
    terminal->reply = NULL;
    terminal->reply_context = NULL;
    terminal->echo_length = 0u;
    terminal->echo_columns = 0u;
    terminal->echo_carriage_return = 0u;
    terminal->reply_failures = 0u;
    terminal->foreground = ASTRA_TERMINAL_COLOR_DEFAULT;
    terminal->background = ASTRA_TERMINAL_COLOR_DEFAULT;
    terminal->attributes = 0u;
    terminal->parser_state = 0u;
    terminal->csi_parameter_count = 0u;
    terminal->csi_private = 0u;
    terminal->csi_overflow = 0u;
    terminal->utf8_remaining = 0u;
    terminal->cursor_visible = 1u;
    terminal->alternate_screen = 0u;
    terminal->saved_cursor_row = 0u;
    terminal->saved_cursor_column = 0u;
    terminal->scroll_top = 0u;
    terminal->scroll_bottom = rows - 1u;
    terminal->scrolls = 0u;
    terminal->pending_scrolls = 0u;
    terminal->scroll_redraw_from = 0u;
    astra_terminal_clear(terminal);
    terminal->cells = terminal->alternate_cells;
    astra_terminal_clear(terminal);
    terminal->cells = terminal->primary_cells;
    astra_terminal_clear(terminal);
    return ASTRA_TERMINAL_OK;
}

AstraTerminalStatus astra_terminal_resize(AstraTerminal *terminal,
                                          uint32_t columns, uint32_t rows,
                                          void *storage,
                                          size_t storage_size)
{
    uint32_t old_columns;
    uint32_t old_rows;

    if (terminal == NULL || columns == 0u || rows == 0u)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    old_columns = terminal->columns;
    old_rows = terminal->rows;
    if (columns > terminal->capacity_columns ||
        rows > terminal->capacity_rows) {
        AstraTerminal replacement = *terminal;
        int was_alternate = terminal->alternate_screen != 0u;

        if (!bind_storage(&replacement, columns, rows, storage,
                          storage_size))
            return ASTRA_TERMINAL_STORAGE_TOO_SMALL;
        for (uint32_t screen = 0u; screen < 2u; ++screen) {
            AstraTerminalCell *old_cells = screen == 0u ?
                terminal->primary_cells : terminal->alternate_cells;

            replacement.cells = screen == 0u ? replacement.primary_cells :
                                                replacement.alternate_cells;
            terminal->cells = old_cells;
            for (uint32_t row = 0u; row < rows; ++row)
                for (uint32_t column = 0u; column < columns; ++column)
                    *cell(&replacement, row, column) = blank_cell(&replacement);
            for (uint32_t row = 0u; row < old_rows && row < rows; ++row)
                for (uint32_t column = 0u;
                     column < old_columns && column < columns; ++column)
                    *cell(&replacement, row, column) =
                        *cell(terminal, row, column);
        }
        terminal->cells = was_alternate ? terminal->alternate_cells :
                                          terminal->primary_cells;
        replacement.cells = was_alternate ? replacement.alternate_cells :
                                             replacement.primary_cells;
        if (replacement.echo_columns > columns) {
            replacement.echo_length = 0u;
            replacement.echo_columns = 0u;
        }
        for (uint32_t index = 0u; index < replacement.echo_length; ++index)
            replacement.echo_line[index] = terminal->echo_line[index];
        *terminal = replacement;
    } else {
        AstraTerminalCell *active = terminal->cells;

        for (uint32_t screen = 0u; screen < 2u; ++screen) {
            terminal->cells = screen == 0u ? terminal->primary_cells :
                                             terminal->alternate_cells;
            if (columns > old_columns)
                for (uint32_t row = 0u; row < old_rows && row < rows; ++row)
                    for (uint32_t column = old_columns; column < columns;
                         ++column)
                        *cell(terminal, row, column) = blank_cell(terminal);
            if (rows > old_rows)
                for (uint32_t row = old_rows; row < rows; ++row)
                    for (uint32_t column = 0u; column < columns; ++column)
                        *cell(terminal, row, column) = blank_cell(terminal);
        }
        terminal->cells = active;
    }
    terminal->columns = columns;
    terminal->rows = rows;
    terminal->scroll_top = 0u;
    terminal->scroll_bottom = rows - 1u;
    if (terminal->cursor_row >= rows)
        terminal->cursor_row = rows - 1u;
    if (terminal->cursor_column >= columns)
        terminal->cursor_column = columns - 1u;
    for (uint32_t row = 0u; row < rows; ++row)
        mark_row(terminal, row);
    terminal->pending_scrolls = 0u;
    return ASTRA_TERMINAL_OK;
}

void astra_terminal_clear(AstraTerminal *terminal)
{
    if (terminal == NULL)
        return;
    for (uint32_t row = 0u; row < terminal->rows; ++row) {
        for (uint32_t column = 0u; column < terminal->columns; ++column)
            *cell(terminal, row, column) = blank_cell(terminal);
        mark_row(terminal, row);
    }
    terminal->cursor_row = 0u;
    terminal->cursor_column = 0u;
    terminal->pending_scrolls = 0u;
}

static uint32_t parameter(const AstraTerminal *terminal, uint32_t index,
                          uint32_t fallback, int zero_is_default)
{
    uint32_t value;

    if (index >= terminal->csi_parameter_count)
        return fallback;
    value = terminal->csi_parameters[index];
    if (value == CSI_MISSING || (zero_is_default && value == 0u))
        return fallback;
    return value;
}

static void erase_cells(AstraTerminal *terminal, uint32_t row,
                        uint32_t first, uint32_t last)
{
    if (first >= terminal->columns)
        return;
    if (last >= terminal->columns)
        last = terminal->columns - 1u;
    for (uint32_t column = first; column <= last; ++column)
        *cell(terminal, row, column) = blank_cell(terminal);
    mark(terminal, row, first);
    mark(terminal, row, last);
}

static void erase_display(AstraTerminal *terminal, uint32_t mode)
{
    if (mode == 2u || mode == 3u) {
        for (uint32_t row = 0u; row < terminal->rows; ++row)
            erase_cells(terminal, row, 0u, terminal->columns - 1u);
    } else if (mode == 1u) {
        for (uint32_t row = 0u; row < terminal->cursor_row; ++row)
            erase_cells(terminal, row, 0u, terminal->columns - 1u);
        erase_cells(terminal, terminal->cursor_row, 0u,
                    terminal->cursor_column);
    } else {
        erase_cells(terminal, terminal->cursor_row, terminal->cursor_column,
                    terminal->columns - 1u);
        for (uint32_t row = terminal->cursor_row + 1u;
             row < terminal->rows; ++row)
            erase_cells(terminal, row, 0u, terminal->columns - 1u);
    }
}

static void erase_line(AstraTerminal *terminal, uint32_t mode)
{
    if (mode == 1u)
        erase_cells(terminal, terminal->cursor_row, 0u,
                    terminal->cursor_column);
    else if (mode == 2u)
        erase_cells(terminal, terminal->cursor_row, 0u,
                    terminal->columns - 1u);
    else
        erase_cells(terminal, terminal->cursor_row, terminal->cursor_column,
                    terminal->columns - 1u);
}

static void cursor_to(AstraTerminal *terminal, uint32_t row, uint32_t column)
{
    terminal->cursor_row = row < terminal->rows ? row : terminal->rows - 1u;
    terminal->cursor_column = column < terminal->columns ?
        column : terminal->columns - 1u;
}

static void save_cursor(AstraTerminal *terminal)
{
    terminal->saved_cursor_row = terminal->cursor_row;
    terminal->saved_cursor_column = terminal->cursor_column;
}

static void restore_cursor(AstraTerminal *terminal)
{
    cursor_to(terminal, terminal->saved_cursor_row,
              terminal->saved_cursor_column);
}

static void alternate_screen(AstraTerminal *terminal, int enabled)
{
    if (enabled == (terminal->alternate_screen != 0u))
        return;
    if (enabled) {
        save_cursor(terminal);
        terminal->cells = terminal->alternate_cells;
        terminal->alternate_screen = 1u;
        astra_terminal_clear(terminal);
    } else {
        terminal->cells = terminal->primary_cells;
        terminal->alternate_screen = 0u;
        restore_cursor(terminal);
        for (uint32_t row = 0u; row < terminal->rows; ++row)
            mark_row(terminal, row);
    }
    terminal->pending_scrolls = 0u;
}

static void insert_characters(AstraTerminal *terminal, uint32_t count)
{
    uint32_t available = terminal->columns - terminal->cursor_column;

    if (count > available)
        count = available;
    for (uint32_t column = terminal->columns; column-- >
         terminal->cursor_column + count;)
        *cell(terminal, terminal->cursor_row, column) =
            *cell(terminal, terminal->cursor_row, column - count);
    erase_cells(terminal, terminal->cursor_row, terminal->cursor_column,
                terminal->cursor_column + count - 1u);
    mark_row(terminal, terminal->cursor_row);
}

static void delete_characters(AstraTerminal *terminal, uint32_t count)
{
    uint32_t available = terminal->columns - terminal->cursor_column;

    if (count > available)
        count = available;
    for (uint32_t column = terminal->cursor_column;
         column + count < terminal->columns; ++column)
        *cell(terminal, terminal->cursor_row, column) =
            *cell(terminal, terminal->cursor_row, column + count);
    erase_cells(terminal, terminal->cursor_row,
                terminal->columns - count, terminal->columns - 1u);
    mark_row(terminal, terminal->cursor_row);
}

static void set_graphics(AstraTerminal *terminal)
{
    for (uint32_t index = 0u; index < terminal->csi_parameter_count; ++index) {
        uint32_t value = parameter(terminal, index, 0u, 0);
        uint32_t *color = NULL;

        switch (value) {
        case 0u:
            terminal->attributes = 0u;
            terminal->foreground = ASTRA_TERMINAL_COLOR_DEFAULT;
            terminal->background = ASTRA_TERMINAL_COLOR_DEFAULT;
            break;
        case 1u: terminal->attributes |= ASTRA_TERMINAL_BOLD; break;
        case 2u: terminal->attributes |= ASTRA_TERMINAL_FAINT; break;
        case 3u: terminal->attributes |= ASTRA_TERMINAL_ITALIC; break;
        case 4u: terminal->attributes |= ASTRA_TERMINAL_UNDERLINE; break;
        case 5u: terminal->attributes |= ASTRA_TERMINAL_BLINK; break;
        case 7u: terminal->attributes |= ASTRA_TERMINAL_INVERSE; break;
        case 8u: terminal->attributes |= ASTRA_TERMINAL_HIDDEN; break;
        case 9u: terminal->attributes |= ASTRA_TERMINAL_STRIKE; break;
        case 22u:
            terminal->attributes &=
                (uint16_t)~(ASTRA_TERMINAL_BOLD | ASTRA_TERMINAL_FAINT);
            break;
        case 23u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_ITALIC; break;
        case 24u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_UNDERLINE; break;
        case 25u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_BLINK; break;
        case 27u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_INVERSE; break;
        case 28u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_HIDDEN; break;
        case 29u: terminal->attributes &= (uint16_t)~ASTRA_TERMINAL_STRIKE; break;
        case 39u: terminal->foreground = ASTRA_TERMINAL_COLOR_DEFAULT; break;
        case 49u: terminal->background = ASTRA_TERMINAL_COLOR_DEFAULT; break;
        default:
            if (value >= 30u && value <= 37u)
                terminal->foreground = value - 30u;
            else if (value >= 40u && value <= 47u)
                terminal->background = value - 40u;
            else if (value >= 90u && value <= 97u)
                terminal->foreground = value - 90u + 8u;
            else if (value >= 100u && value <= 107u)
                terminal->background = value - 100u + 8u;
            else if (value == 38u)
                color = &terminal->foreground;
            else if (value == 48u)
                color = &terminal->background;
            if (color != NULL && index + 2u < terminal->csi_parameter_count &&
                parameter(terminal, index + 1u, 0u, 0) == 5u) {
                *color = parameter(terminal, index + 2u, 0u, 0) & 0xffu;
                index += 2u;
            } else if (color != NULL &&
                       index + 4u < terminal->csi_parameter_count &&
                       parameter(terminal, index + 1u, 0u, 0) == 2u) {
                *color = ASTRA_TERMINAL_COLOR_RGB(
                    parameter(terminal, index + 2u, 0u, 0) & 0xffu,
                    parameter(terminal, index + 3u, 0u, 0) & 0xffu,
                    parameter(terminal, index + 4u, 0u, 0) & 0xffu);
                index += 4u;
            }
            break;
        }
    }
}

static void set_private_mode(AstraTerminal *terminal, int enabled)
{
    for (uint32_t index = 0u; index < terminal->csi_parameter_count; ++index) {
        uint32_t mode = parameter(terminal, index, 0u, 0);

        if (mode == 25u)
            terminal->cursor_visible = (uint8_t)(enabled != 0);
        else if (mode == 47u || mode == 1047u || mode == 1049u)
            alternate_screen(terminal, enabled);
    }
}

static void send_reply(AstraTerminal *terminal, const uint8_t *bytes,
                       uint32_t length)
{
    if (terminal->reply != NULL &&
        !terminal->reply(terminal->reply_context, bytes, length))
        ++terminal->reply_failures;
}

static uint32_t decimal(char *out, uint32_t value)
{
    char reverse[10];
    uint32_t count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (uint32_t index = 0u; index < count; ++index)
        out[index] = reverse[count - index - 1u];
    return count;
}

static void reply_cursor_position(AstraTerminal *terminal, int private)
{
    char response[32];
    uint32_t at = 0u;

    response[at++] = '\x1b';
    response[at++] = '[';
    if (private)
        response[at++] = '?';
    at += decimal(response + at, terminal->cursor_row + 1u);
    response[at++] = ';';
    at += decimal(response + at, terminal->cursor_column + 1u);
    response[at++] = 'R';
    send_reply(terminal, (const uint8_t *)response, at);
}

static void execute_csi(AstraTerminal *terminal, uint8_t command)
{
    uint32_t count = parameter(terminal, 0u, 1u, 1);

    switch (command) {
    case 'A':
        terminal->cursor_row = count > terminal->cursor_row ?
            0u : terminal->cursor_row - count;
        break;
    case 'B':
        cursor_to(terminal,
                  count >= terminal->rows - terminal->cursor_row ?
                      terminal->rows - 1u : terminal->cursor_row + count,
                  terminal->cursor_column);
        break;
    case 'C':
        cursor_to(terminal, terminal->cursor_row,
                  count >= terminal->columns - terminal->cursor_column ?
                      terminal->columns - 1u :
                      terminal->cursor_column + count);
        break;
    case 'D':
        terminal->cursor_column = count > terminal->cursor_column ?
            0u : terminal->cursor_column - count;
        break;
    case 'E':
        cursor_to(terminal,
                  count >= terminal->rows - terminal->cursor_row ?
                      terminal->rows - 1u : terminal->cursor_row + count,
                  0u);
        break;
    case 'F':
        terminal->cursor_row = count > terminal->cursor_row ?
            0u : terminal->cursor_row - count;
        terminal->cursor_column = 0u;
        break;
    case 'G':
        cursor_to(terminal, terminal->cursor_row, count - 1u);
        break;
    case 'H':
    case 'f':
        cursor_to(terminal,
                  parameter(terminal, 0u, 1u, 1) - 1u,
                  parameter(terminal, 1u, 1u, 1) - 1u);
        break;
    case 'd':
        cursor_to(terminal, count - 1u, terminal->cursor_column);
        break;
    case 'J': erase_display(terminal, parameter(terminal, 0u, 0u, 0)); break;
    case 'K': erase_line(terminal, parameter(terminal, 0u, 0u, 0)); break;
    case 'm': set_graphics(terminal); break;
    case 'n':
        if (count == 5u) {
            static const uint8_t status[] = "\x1b[0n";
            send_reply(terminal, status, sizeof(status) - 1u);
        } else if (count == 6u) {
            reply_cursor_position(terminal, terminal->csi_private == '?');
        }
        break;
    case 'c':
        if (terminal->csi_private == '>') {
            static const uint8_t secondary[] = "\x1b[>0;1;0c";
            send_reply(terminal, secondary, sizeof(secondary) - 1u);
        } else if (terminal->csi_private == 0u &&
                   parameter(terminal, 0u, 0u, 0) == 0u) {
            static const uint8_t primary[] = "\x1b[?1;2c";
            send_reply(terminal, primary, sizeof(primary) - 1u);
        }
        break;
    case 'r': {
        uint32_t top = parameter(terminal, 0u, 1u, 1);
        uint32_t bottom = parameter(terminal, 1u, terminal->rows, 1);

        if (top < bottom && bottom <= terminal->rows) {
            terminal->scroll_top = top - 1u;
            terminal->scroll_bottom = bottom - 1u;
            cursor_to(terminal, 0u, 0u);
        }
        break;
    }
    case 's': save_cursor(terminal); break;
    case 'u': restore_cursor(terminal); break;
    case 'L':
        if (terminal->cursor_row >= terminal->scroll_top &&
            terminal->cursor_row <= terminal->scroll_bottom)
            scroll_down(terminal, terminal->cursor_row,
                        terminal->scroll_bottom, count);
        break;
    case 'M':
        if (terminal->cursor_row >= terminal->scroll_top &&
            terminal->cursor_row <= terminal->scroll_bottom)
            scroll_up(terminal, terminal->cursor_row,
                      terminal->scroll_bottom, count);
        break;
    case '@': insert_characters(terminal, count); break;
    case 'P': delete_characters(terminal, count); break;
    case 'X':
        if (count > terminal->columns - terminal->cursor_column)
            count = terminal->columns - terminal->cursor_column;
        erase_cells(terminal, terminal->cursor_row, terminal->cursor_column,
                    terminal->cursor_column + count - 1u);
        break;
    case 'S':
        scroll_up(terminal, terminal->scroll_top, terminal->scroll_bottom,
                  count);
        break;
    case 'T':
        scroll_down(terminal, terminal->scroll_top, terminal->scroll_bottom,
                    count);
        break;
    case 'h':
    case 'l':
        if (terminal->csi_private == '?')
            set_private_mode(terminal, command == 'h');
        break;
    default:
        break;
    }
}

static void echo_flush(AstraTerminal *terminal)
{
    uint32_t length = terminal->echo_length;

    terminal->echo_length = 0u;
    terminal->echo_columns = 0u;
    terminal->echo_carriage_return = 0u;
    if (terminal->echo != NULL && length != 0u)
        terminal->echo(terminal->echo_context, terminal->echo_line, length);
}

static void echo_scalar(AstraTerminal *terminal, uint32_t scalar)
{
    uint32_t encoded;

    if (terminal->echo == NULL)
        return;
    if (terminal->echo_carriage_return) {
        terminal->echo_length = 0u;
        terminal->echo_columns = 0u;
        terminal->echo_carriage_return = 0u;
    }
    if (terminal->echo_columns >= terminal->columns)
        echo_flush(terminal);
    encoded = astra_utf8_encode(scalar,
        terminal->echo_line + terminal->echo_length);
    terminal->echo_length += encoded;
    ++terminal->echo_columns;
}

static void echo_backspace(AstraTerminal *terminal)
{
    if (terminal->echo_length == 0u)
        return;
    do {
        --terminal->echo_length;
    } while (terminal->echo_length != 0u &&
             ((uint8_t)terminal->echo_line[terminal->echo_length] & 0xc0u) ==
                 0x80u);
    if (terminal->echo_columns != 0u)
        --terminal->echo_columns;
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

void astra_terminal_set_reply(AstraTerminal *terminal,
                              AstraTerminalReply reply, void *context)
{
    if (terminal == NULL)
        return;
    terminal->reply = reply;
    terminal->reply_context = context;
}

void astra_terminal_set_scroll(AstraTerminal *terminal,
                               AstraTerminalScroll scroll_callback)
{
    if (terminal == NULL)
        return;
    terminal->scroll = scroll_callback;
    terminal->pending_scrolls = 0u;
}

static void put_scalar(AstraTerminal *terminal, uint32_t scalar)
{
    echo_scalar(terminal, scalar);
    if (terminal->cursor_column >= terminal->columns)
        newline(terminal);
    *cell(terminal, terminal->cursor_row, terminal->cursor_column) =
        (AstraTerminalCell){scalar, terminal->foreground,
                            terminal->background, terminal->attributes,
                            1u, 0u};
    mark(terminal, terminal->cursor_row, terminal->cursor_column);
    ++terminal->cursor_column;
}

static void reverse_index(AstraTerminal *terminal)
{
    if (terminal->cursor_row == terminal->scroll_top)
        scroll_down(terminal, terminal->scroll_top,
                    terminal->scroll_bottom, 1u);
    else if (terminal->cursor_row != 0u)
        --terminal->cursor_row;
}

static void control(AstraTerminal *terminal, uint8_t value)
{
    switch (value) {
    case '\n':
    case '\v':
    case '\f':
        echo_flush(terminal);
        newline(terminal);
        break;
    case '\r':
        terminal->echo_carriage_return = 1u;
        terminal->cursor_column = 0u;
        break;
    case '\b':
        echo_backspace(terminal);
        if (terminal->cursor_column != 0u)
            --terminal->cursor_column;
        else if (terminal->cursor_row != 0u) {
            --terminal->cursor_row;
            terminal->cursor_column = terminal->columns - 1u;
        }
        break;
    case '\t': {
        uint32_t next = (terminal->cursor_column / ASTRA_TERMINAL_TAB_WIDTH +
                         1u) * ASTRA_TERMINAL_TAB_WIDTH;

        while (terminal->cursor_column < next &&
               terminal->cursor_column < terminal->columns)
            put_scalar(terminal, ' ');
        break;
    }
    default:
        break;
    }
}

static void begin_csi(AstraTerminal *terminal)
{
    terminal->parser_state = PARSER_CSI;
    terminal->csi_parameter_count = 1u;
    terminal->csi_parameters[0] = CSI_MISSING;
    terminal->csi_private = 0u;
    terminal->csi_overflow = 0u;
}

static void reset_terminal(AstraTerminal *terminal)
{
    alternate_screen(terminal, 0);
    terminal->foreground = ASTRA_TERMINAL_COLOR_DEFAULT;
    terminal->background = ASTRA_TERMINAL_COLOR_DEFAULT;
    terminal->attributes = 0u;
    terminal->scroll_top = 0u;
    terminal->scroll_bottom = terminal->rows - 1u;
    terminal->cursor_visible = 1u;
    terminal->parser_state = PARSER_GROUND;
    terminal->utf8_remaining = 0u;
    astra_terminal_clear(terminal);
}

static void escape_byte(AstraTerminal *terminal, uint8_t value)
{
    terminal->parser_state = PARSER_GROUND;
    switch (value) {
    case '[': begin_csi(terminal); break;
    case ']': terminal->parser_state = PARSER_OSC; break;
    case '(':
    case ')': terminal->parser_state = PARSER_CHARSET; break;
    case '7': save_cursor(terminal); break;
    case '8': restore_cursor(terminal); break;
    case 'D':
        if (terminal->cursor_row == terminal->scroll_bottom)
            scroll_up(terminal, terminal->scroll_top,
                      terminal->scroll_bottom, 1u);
        else if (terminal->cursor_row + 1u < terminal->rows)
            ++terminal->cursor_row;
        break;
    case 'E': newline(terminal); break;
    case 'M': reverse_index(terminal); break;
    case 'c': reset_terminal(terminal); break;
    default: break;
    }
}

static void csi_byte(AstraTerminal *terminal, uint8_t value)
{
    uint32_t index = terminal->csi_parameter_count - 1u;

    if ((value == '?' || value == '>') && index == 0u &&
        terminal->csi_parameters[0] == CSI_MISSING) {
        terminal->csi_private = value;
        return;
    }
    if (value >= '0' && value <= '9') {
        uint32_t digit = value - '0';
        uint32_t current = terminal->csi_parameters[index];

        if (current == CSI_MISSING)
            current = 0u;
        if (current > (UINT32_MAX - digit) / 10u)
            terminal->csi_overflow = 1u;
        else
            terminal->csi_parameters[index] = current * 10u + digit;
        return;
    }
    if (value == ';' || value == ':') {
        if (terminal->csi_parameter_count >=
            ASTRA_TERMINAL_CSI_PARAMETERS) {
            terminal->csi_overflow = 1u;
        } else {
            terminal->csi_parameters[terminal->csi_parameter_count++] =
                CSI_MISSING;
        }
        return;
    }
    if (value >= 0x40u && value <= 0x7eu) {
        if (!terminal->csi_overflow)
            execute_csi(terminal, value);
        terminal->parser_state = PARSER_GROUND;
        return;
    }
    if (value < 0x20u)
        control(terminal, value);
}

static void utf8_byte(AstraTerminal *terminal, uint8_t value)
{
    if (terminal->utf8_remaining != 0u) {
        if ((value & 0xc0u) != 0x80u) {
            terminal->utf8_remaining = 0u;
            put_scalar(terminal, 0xfffdu);
            astra_terminal_putc(terminal, value);
            return;
        }
        terminal->utf8_codepoint =
            (terminal->utf8_codepoint << 6u) | (value & 0x3fu);
        if (--terminal->utf8_remaining == 0u) {
            uint32_t scalar = terminal->utf8_codepoint;
            uint32_t minimum = terminal->utf8_expected == 2u ? 0x80u :
                               terminal->utf8_expected == 3u ? 0x800u :
                                                               0x10000u;

            if (scalar < minimum || scalar > 0x10ffffu ||
                (scalar >= 0xd800u && scalar <= 0xdfffu))
                scalar = 0xfffdu;
            put_scalar(terminal, scalar);
        }
        return;
    }
    if (value < 0x80u) {
        put_scalar(terminal, value);
    } else if (value >= 0xc2u && value <= 0xdfu) {
        terminal->utf8_codepoint = value & 0x1fu;
        terminal->utf8_remaining = 1u;
        terminal->utf8_expected = 2u;
    } else if (value >= 0xe0u && value <= 0xefu) {
        terminal->utf8_codepoint = value & 0x0fu;
        terminal->utf8_remaining = 2u;
        terminal->utf8_expected = 3u;
    } else if (value >= 0xf0u && value <= 0xf4u) {
        terminal->utf8_codepoint = value & 0x07u;
        terminal->utf8_remaining = 3u;
        terminal->utf8_expected = 4u;
    } else {
        put_scalar(terminal, 0xfffdu);
    }
}

void astra_terminal_putc(AstraTerminal *terminal, uint8_t value)
{
    if (terminal == NULL)
        return;
    if (terminal->parser_state == PARSER_OSC) {
        if (value == 0x07u)
            terminal->parser_state = PARSER_GROUND;
        else if (value == 0x1bu)
            terminal->parser_state = PARSER_OSC_ESCAPE;
        return;
    }
    if (terminal->parser_state == PARSER_OSC_ESCAPE) {
        terminal->parser_state = value == '\\' ? PARSER_GROUND : PARSER_OSC;
        return;
    }
    if (value == 0x1bu) {
        terminal->utf8_remaining = 0u;
        terminal->parser_state = PARSER_ESCAPE;
        return;
    }
    if (terminal->parser_state == PARSER_ESCAPE) {
        escape_byte(terminal, value);
        return;
    }
    if (terminal->parser_state == PARSER_CSI) {
        csi_byte(terminal, value);
        return;
    }
    if (terminal->parser_state == PARSER_CHARSET) {
        terminal->parser_state = PARSER_GROUND;
        return;
    }
    if (value < 0x20u || value == 0x7fu) {
        control(terminal, value);
        return;
    }
    utf8_byte(terminal, value);
}

void astra_terminal_write_bytes(AstraTerminal *terminal, const uint8_t *bytes,
                                size_t count)
{
    if (terminal == NULL || bytes == NULL)
        return;
    for (size_t index = 0u; index < count; ++index)
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
    if (terminal == NULL)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    if (terminal->render == NULL) {
        clear_damage(terminal);
        terminal->pending_scrolls = 0u;
        return ASTRA_TERMINAL_OK;
    }
    if (terminal->pending_scrolls != 0u) {
        if (terminal->scroll_redraw_from != 0u &&
            !terminal->scroll(terminal->render_context,
                              terminal->pending_scrolls,
                              terminal->rows - terminal->pending_scrolls)) {
            terminal->pending_scrolls = 0u;
            for (uint32_t row = 0u; row < terminal->rows; ++row)
                mark_row(terminal, row);
            return ASTRA_TERMINAL_RENDER_FAILED;
        }
        terminal->pending_scrolls = 0u;
    }
    for (uint32_t row = 0u; row < terminal->rows; ++row) {
        uint32_t first = terminal->dirty_first[row];
        uint32_t last = terminal->dirty_last[row];

        if (first > last)
            continue;
        if (!terminal->render(terminal->render_context, row, first,
                              const_cell(terminal, row, first),
                              last - first + 1u))
            return ASTRA_TERMINAL_RENDER_FAILED;
        terminal->dirty_first[row] = 1u;
        terminal->dirty_last[row] = 0u;
    }
    return ASTRA_TERMINAL_OK;
}

AstraTerminalStatus astra_terminal_redraw(AstraTerminal *terminal)
{
    if (terminal == NULL)
        return ASTRA_TERMINAL_INVALID_ARGUMENT;
    terminal->pending_scrolls = 0u;
    for (uint32_t row = 0u; row < terminal->rows; ++row)
        mark_row(terminal, row);
    return astra_terminal_flush(terminal);
}

uint32_t astra_terminal_cell(const AstraTerminal *terminal, uint32_t row,
                             uint32_t column)
{
    if (terminal == NULL || row >= terminal->rows ||
        column >= terminal->columns)
        return 0u;
    return const_cell(terminal, row, column)->codepoint;
}

const AstraTerminalCell *astra_terminal_cell_at(
    const AstraTerminal *terminal, uint32_t row, uint32_t column)
{
    if (terminal == NULL || row >= terminal->rows ||
        column >= terminal->columns)
        return NULL;
    return const_cell(terminal, row, column);
}
