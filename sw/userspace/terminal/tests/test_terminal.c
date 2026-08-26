#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/terminal.h>

/*
 * The cell model against a recording renderer.
 *
 * What is being tested is that the model never depends on what draws it: the
 * renderer here keeps its own grid, and every assertion compares the two. A
 * model that reached past its own dimensions, or forgot to report a change,
 * shows up as a divergence rather than as a wrong pixel somewhere later.
 */

#define TEST_COLUMNS 20u
#define TEST_ROWS 4u

static AstraTerminalCell rendered[TEST_ROWS][TEST_COLUMNS];
static uint8_t terminal_storage[
    ASTRA_TERMINAL_STORAGE_BYTES(TEST_COLUMNS, TEST_ROWS)];
static uint32_t render_calls;
static uint32_t rendered_cells;
static int render_should_fail;
static uint32_t scroll_calls;
static uint32_t scrolled_rows;
static uint32_t preserved_rows;

static int record(void *context, uint32_t row, uint32_t column,
                  const AstraTerminalCell *cells, uint32_t count)
{
    uint32_t index;

    (void)context;
    if (render_should_fail) {
        return 0;
    }
    /* A run must never cross a row or leave the grid. */
    assert(row < TEST_ROWS);
    assert(column < TEST_COLUMNS);
    assert(count != 0u);
    assert(column + count <= TEST_COLUMNS);
    ++render_calls;
    rendered_cells += count;
    for (index = 0u; index < count; ++index) {
        rendered[row][column + index] = cells[index];
    }
    return 1;
}

static int record_scroll(void *context, uint32_t rows, uint32_t preserved)
{
    (void)context;
    assert(rows != 0u && rows + preserved == TEST_ROWS);
    ++scroll_calls;
    scrolled_rows = rows;
    preserved_rows = preserved;
    memmove(rendered[0], rendered[rows],
            preserved * TEST_COLUMNS * sizeof(rendered[0][0]));
    for (uint32_t row = preserved; row < TEST_ROWS; ++row)
        for (uint32_t column = 0u; column < TEST_COLUMNS; ++column)
            rendered[row][column].codepoint = ' ';
    return 1;
}

static void reset(AstraTerminal *terminal)
{
    memset(rendered, 0, sizeof(rendered));
    render_calls = 0u;
    rendered_cells = 0u;
    render_should_fail = 0;
    scroll_calls = 0u;
    scrolled_rows = 0u;
    preserved_rows = 0u;
    assert(astra_terminal_init(terminal, TEST_COLUMNS, TEST_ROWS,
                               terminal_storage, sizeof(terminal_storage),
                               record, NULL) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_flush(terminal) == ASTRA_TERMINAL_OK);
    render_calls = 0u;
    rendered_cells = 0u;
}

/* The renderer's grid must equal the model's, cell for cell. */
static void assert_matches(const AstraTerminal *terminal)
{
    uint32_t row;
    uint32_t column;

    for (row = 0u; row < TEST_ROWS; ++row) {
        for (column = 0u; column < TEST_COLUMNS; ++column) {
            assert(rendered[row][column].codepoint ==
                   astra_terminal_cell(terminal, row, column));
        }
    }
}

static void test_rejects_impossible_geometry(void)
{
    AstraTerminal terminal;
    size_t bytes;

    assert(astra_terminal_storage_size(TEST_COLUMNS, TEST_ROWS, &bytes) ==
           ASTRA_TERMINAL_OK);
    assert(bytes == sizeof(terminal_storage));
    assert(astra_terminal_init(NULL, TEST_COLUMNS, TEST_ROWS,
                               terminal_storage, sizeof(terminal_storage),
                               record, NULL) ==
           ASTRA_TERMINAL_INVALID_ARGUMENT);
    assert(astra_terminal_init(&terminal, 0u, TEST_ROWS, terminal_storage,
                               sizeof(terminal_storage), record, NULL) ==
           ASTRA_TERMINAL_INVALID_ARGUMENT);
    assert(astra_terminal_init(&terminal, TEST_COLUMNS, 0u,
                               terminal_storage, sizeof(terminal_storage),
                               record, NULL) ==
           ASTRA_TERMINAL_INVALID_ARGUMENT);
    assert(astra_terminal_init(&terminal, TEST_COLUMNS, TEST_ROWS,
                               terminal_storage,
                               sizeof(terminal_storage) -
                                   ASTRA_TERMINAL_STORAGE_ALIGNMENT,
                               record, NULL) ==
           ASTRA_TERMINAL_STORAGE_TOO_SMALL);
}

static void test_writes_land_where_the_cursor_is(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "hi");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 'h');
    assert(astra_terminal_cell(&terminal, 0u, 1u) == 'i');
    assert(terminal.cursor_row == 0u && terminal.cursor_column == 2u);
    assert_matches(&terminal);

    /* Only the two changed cells were drawn. */
    assert(rendered_cells == 2u);
}

static void test_newline_and_carriage_return(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "ab\ncd");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 'a');
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 'c');
    assert(terminal.cursor_row == 1u && terminal.cursor_column == 2u);

    /* A carriage return returns without erasing what is already there. */
    astra_terminal_write(&terminal, "\rZ");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 'Z');
    assert(astra_terminal_cell(&terminal, 1u, 1u) == 'd');
    assert_matches(&terminal);
}

static void test_backspace_moves_without_erasing(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "ab");
    astra_terminal_putc(&terminal, '\b');
    assert(terminal.cursor_column == 1u);
    assert(astra_terminal_cell(&terminal, 0u, 1u) == 'b');

    /* The erasing sequence a shell actually sends. */
    astra_terminal_write(&terminal, " \b");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 0u, 1u) == ' ');
    assert(terminal.cursor_column == 1u);
    assert_matches(&terminal);

    /* At the left edge it steps back onto the previous row. */
    astra_terminal_write(&terminal, "\nx");
    astra_terminal_putc(&terminal, '\b');
    astra_terminal_putc(&terminal, '\b');
    assert(terminal.cursor_row == 0u);
    assert(terminal.cursor_column == TEST_COLUMNS - 1u);
}

static void test_tab_stops(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_putc(&terminal, 'a');
    astra_terminal_putc(&terminal, '\t');
    assert(terminal.cursor_column == ASTRA_TERMINAL_TAB_WIDTH);
    astra_terminal_putc(&terminal, '\t');
    assert(terminal.cursor_column == 2u * ASTRA_TERMINAL_TAB_WIDTH);
}

static void test_wrap_at_the_right_edge(void)
{
    AstraTerminal terminal;
    uint32_t index;

    reset(&terminal);
    for (index = 0u; index < TEST_COLUMNS; ++index)
        astra_terminal_putc(&terminal, 'x');
    assert(terminal.cursor_row == 0u);
    /* The wrap happens on the next character, not on filling the row. */
    astra_terminal_putc(&terminal, 'y');
    assert(terminal.cursor_row == 1u);
    assert(terminal.cursor_column == 1u);
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 'y');
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert_matches(&terminal);
}

static void test_scroll_preserves_content_and_redraws(void)
{
    AstraTerminal terminal;
    uint32_t row;

    reset(&terminal);
    for (row = 0u; row < TEST_ROWS; ++row) {
        astra_terminal_putc(&terminal, (uint8_t)('0' + row));
        astra_terminal_putc(&terminal, '\n');
    }
    /* The last newline scrolled: row 0 is gone and the rest moved up. */
    assert(terminal.scrolls == 1u);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == '1');
    assert(astra_terminal_cell(&terminal, TEST_ROWS - 2u, 0u) ==
           (uint8_t)('0' + TEST_ROWS - 1u));
    assert(astra_terminal_cell(&terminal, TEST_ROWS - 1u, 0u) == ' ');
    assert(terminal.cursor_row == TEST_ROWS - 1u);

    /* Everything moved, so everything is redrawn. */
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(rendered_cells >= TEST_ROWS * TEST_COLUMNS);
    assert_matches(&terminal);
}

static void test_controls_and_invalid_utf8(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_putc(&terminal, 0x00u);
    astra_terminal_putc(&terminal, 0x1fu);
    astra_terminal_putc(&terminal, 0x7fu);
    astra_terminal_putc(&terminal, 0xffu);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 0xfffdu);
    assert(astra_terminal_cell(&terminal, 0u, 1u) == ' ');
}

static void test_utf8_attributes_and_truecolor(void)
{
    AstraTerminal terminal;
    const AstraTerminalCell *styled;

    reset(&terminal);
    astra_terminal_write(&terminal, "A\xc3\xa9\xf0\x9f\x98\x80");
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 'A');
    assert(astra_terminal_cell(&terminal, 0u, 1u) == 0xe9u);
    assert(astra_terminal_cell(&terminal, 0u, 2u) == 0x1f600u);
    astra_terminal_write(
        &terminal, "\x1b[1;3;4;38;5;196;48;2;1;2;3mX");
    styled = astra_terminal_cell_at(&terminal, 0u, 3u);
    assert(styled != NULL && styled->codepoint == 'X');
    assert((styled->attributes & (ASTRA_TERMINAL_BOLD |
                                  ASTRA_TERMINAL_ITALIC |
                                  ASTRA_TERMINAL_UNDERLINE)) ==
           (ASTRA_TERMINAL_BOLD | ASTRA_TERMINAL_ITALIC |
            ASTRA_TERMINAL_UNDERLINE));
    assert(styled->foreground == 196u);
    assert(styled->background == ASTRA_TERMINAL_COLOR_RGB(1u, 2u, 3u));
}

static void test_cursor_erase_and_alternate_screen(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "primary");
    astra_terminal_write(&terminal, "\x1b[2;3HXY\x1b[1K");
    assert(terminal.cursor_row == 1u && terminal.cursor_column == 4u);
    assert(astra_terminal_cell(&terminal, 1u, 2u) == ' ');
    astra_terminal_write(&terminal, "\x1b[?1049halt");
    assert(terminal.alternate_screen &&
           astra_terminal_cell(&terminal, 0u, 0u) == 'a');
    astra_terminal_write(&terminal, "\x1b[?1049l");
    assert(!terminal.alternate_screen &&
           astra_terminal_cell(&terminal, 0u, 0u) == 'p');
}

static void test_scroll_region_and_osc_are_bounded(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "top\nA\nB\nbottom");
    astra_terminal_write(&terminal, "\x1b[2;3r\x1b[3;1H\n");
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 't');
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 'B');
    assert(astra_terminal_cell(&terminal, 2u, 0u) == ' ');
    astra_terminal_write(&terminal, "\x1b]0;ignored title\x07Z");
    assert(astra_terminal_cell(&terminal, 2u, 0u) == 'Z');
}

static void test_flush_reports_only_changes(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "abc");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(rendered_cells == 3u);

    /* Nothing changed since, so nothing is drawn again. */
    render_calls = 0u;
    rendered_cells = 0u;
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(render_calls == 0u);
    assert(rendered_cells == 0u);

    /* A redraw is unconditional, for a renderer just attached. */
    assert(astra_terminal_redraw(&terminal) == ASTRA_TERMINAL_OK);
    assert(rendered_cells == TEST_ROWS * TEST_COLUMNS);
    assert_matches(&terminal);
}

static void test_scroll_moves_rendered_rows_once(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_set_scroll(&terminal, record_scroll);
    astra_terminal_write(&terminal, "one\ntwo\nthree\nfour");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    render_calls = 0u;
    rendered_cells = 0u;

    astra_terminal_write(&terminal, "\nfive\nsix");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(scroll_calls == 1u && scrolled_rows == 2u && preserved_rows == 2u);
    assert(render_calls == 2u && rendered_cells == 2u * TEST_COLUMNS);
    assert_matches(&terminal);
}

static void test_render_failure_is_reported(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "abc");
    render_should_fail = 1;
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_RENDER_FAILED);

    /*
     * The damage survives a failed draw, so the next flush tries again rather
     * than losing the characters the screen never got.
     */
    render_should_fail = 0;
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(rendered_cells >= 3u);
    assert_matches(&terminal);
}

static void test_model_works_without_a_renderer(void)
{
    AstraTerminal terminal;

    assert(astra_terminal_init(&terminal, TEST_COLUMNS, TEST_ROWS,
                               terminal_storage, sizeof(terminal_storage),
                               NULL, NULL) == ASTRA_TERMINAL_OK);
    astra_terminal_write(&terminal, "headless");
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 'h');
}

static void test_resize_preserves_cells_and_clamps_cursor(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    astra_terminal_write(&terminal, "top\nsecond");
    terminal.cursor_row = TEST_ROWS - 1u;
    terminal.cursor_column = TEST_COLUMNS - 1u;
    assert(astra_terminal_resize(&terminal, 10u, 2u, NULL, 0u) ==
           ASTRA_TERMINAL_OK);
    assert(terminal.columns == 10u && terminal.rows == 2u);
    assert(terminal.cursor_row == 1u && terminal.cursor_column == 9u);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 't');
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 's');
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_resize(&terminal, TEST_COLUMNS, TEST_ROWS, NULL,
                                 0u) ==
           ASTRA_TERMINAL_OK);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 't');
    assert(astra_terminal_cell(&terminal, 1u, 0u) == 's');
    assert(astra_terminal_cell(&terminal, 2u, 0u) == ' ');
    assert(astra_terminal_flush(&terminal) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_resize(&terminal, 0u, 1u, NULL, 0u) ==
           ASTRA_TERMINAL_INVALID_ARGUMENT);
}

static void test_growth_rebinds_caller_storage(void)
{
    enum { GROWN_COLUMNS = 257, GROWN_ROWS = 65 };
    static uint8_t grown_storage[
        ASTRA_TERMINAL_STORAGE_BYTES(GROWN_COLUMNS, GROWN_ROWS)];
    AstraTerminal terminal;

    assert(astra_terminal_init(&terminal, TEST_COLUMNS, TEST_ROWS,
                               terminal_storage, sizeof(terminal_storage),
                               NULL, NULL) == ASTRA_TERMINAL_OK);
    astra_terminal_write(&terminal, "preserved");
    assert(astra_terminal_resize(&terminal, GROWN_COLUMNS, GROWN_ROWS,
                                 NULL, 0u) ==
           ASTRA_TERMINAL_STORAGE_TOO_SMALL);
    assert(astra_terminal_resize(&terminal, GROWN_COLUMNS, GROWN_ROWS,
                                 grown_storage, sizeof(grown_storage)) ==
           ASTRA_TERMINAL_OK);
    assert(terminal.columns == GROWN_COLUMNS && terminal.rows == GROWN_ROWS);
    assert(astra_terminal_cell(&terminal, 0u, 0u) == 'p');
    assert(astra_terminal_cell(&terminal, 0u, 8u) == 'd');
    assert(astra_terminal_cell(&terminal, GROWN_ROWS - 1u,
                               GROWN_COLUMNS - 1u) == ' ');
}

static void test_preallocated_resize(void)
{
    enum {
        CAPACITY_COLUMNS = TEST_COLUMNS + 20u,
        CAPACITY_ROWS = TEST_ROWS + 10u
    };
    uint8_t storage[
        ASTRA_TERMINAL_STORAGE_BYTES(CAPACITY_COLUMNS, CAPACITY_ROWS)];
    AstraTerminal terminal;

    assert(astra_terminal_init_capacity(
               &terminal, TEST_COLUMNS, TEST_ROWS,
               CAPACITY_COLUMNS, CAPACITY_ROWS, storage, sizeof(storage),
               record, NULL) == ASTRA_TERMINAL_OK);
    assert(astra_terminal_resize(&terminal, CAPACITY_COLUMNS, CAPACITY_ROWS,
                                 NULL, 0u) == ASTRA_TERMINAL_OK);
    assert(terminal.storage == storage &&
           terminal.capacity_columns == CAPACITY_COLUMNS &&
           terminal.capacity_rows == CAPACITY_ROWS);
}

static char echoed[8][64];
static uint32_t echoed_lines;
static char replies[8][32];
static uint32_t reply_count;

static void record_echo(void *context, const char *line, uint32_t length)
{
    (void)context;
    assert(echoed_lines < 8u);
    assert(length < sizeof(echoed[0]));
    memcpy(echoed[echoed_lines], line, length);
    echoed[echoed_lines][length] = '\0';
    ++echoed_lines;
}

static int record_reply(void *context, const uint8_t *bytes, uint32_t length)
{
    (void)context;
    assert(reply_count < 8u && length < sizeof(replies[0]));
    memcpy(replies[reply_count], bytes, length);
    replies[reply_count][length] = '\0';
    ++reply_count;
    return 1;
}

static int refuse_reply(void *context, const uint8_t *bytes, uint32_t length)
{
    (void)context;
    (void)bytes;
    (void)length;
    return 0;
}

static void test_terminal_queries_reply_on_the_input_path(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    reply_count = 0u;
    astra_terminal_set_reply(&terminal, record_reply, NULL);
    astra_terminal_write(&terminal, "abc\x1b[5n\x1b[6n\x1b[?6n"
                                      "\x1b[c\x1b[>c");
    assert(reply_count == 5u);
    assert(strcmp(replies[0], "\x1b[0n") == 0);
    assert(strcmp(replies[1], "\x1b[1;4R") == 0);
    assert(strcmp(replies[2], "\x1b[?1;4R") == 0);
    assert(strcmp(replies[3], "\x1b[?1;2c") == 0);
    assert(strcmp(replies[4], "\x1b[>0;1;0c") == 0);
    assert(terminal.reply_failures == 0u);

    astra_terminal_set_reply(&terminal, refuse_reply, NULL);
    astra_terminal_write(&terminal, "\x1b[5n");
    assert(terminal.reply_failures == 1u);
}

static void test_echo_reports_each_line_once(void)
{
    AstraTerminal terminal;

    reset(&terminal);
    echoed_lines = 0u;
    astra_terminal_set_echo(&terminal, record_echo, NULL);
    /* A line arrives on its newline, and never before. */
    astra_terminal_write(&terminal, "first");
    assert(echoed_lines == 0u);
    astra_terminal_write(&terminal, "\n");
    assert(echoed_lines == 1u && strcmp(echoed[0], "first") == 0);
    /* A backspace takes the byte back out of the line it is assembling. */
    astra_terminal_write(&terminal, "abc\bd\n");
    assert(echoed_lines == 2u && strcmp(echoed[1], "abd") == 0);
    /* A redraw replaces the line rather than recording every prefix of it. */
    astra_terminal_write(&terminal, "WORK:> l\rWORK:> ls\n");
    assert(echoed_lines == 3u && strcmp(echoed[2], "WORK:> ls") == 0);
    /* ONLCR's CR must not erase the completed line before LF records it. */
    astra_terminal_write(&terminal, "child output\r\n");
    assert(echoed_lines == 4u && strcmp(echoed[3], "child output") == 0);
    /* A blank line is not a line: nothing to record and nothing to read. */
    astra_terminal_write(&terminal, "\n");
    assert(echoed_lines == 4u);
    /* Installing another echo hands the partial line to the old one first. */
    astra_terminal_write(&terminal, "partial");
    astra_terminal_set_echo(&terminal, NULL, NULL);
    assert(echoed_lines == 5u && strcmp(echoed[4], "partial") == 0);
    /* And with no echo installed the model does not collect anything. */
    astra_terminal_write(&terminal, "silent\n");
    assert(echoed_lines == 5u);
}

int main(void)
{
    test_rejects_impossible_geometry();
    test_writes_land_where_the_cursor_is();
    test_newline_and_carriage_return();
    test_backspace_moves_without_erasing();
    test_tab_stops();
    test_wrap_at_the_right_edge();
    test_scroll_preserves_content_and_redraws();
    test_controls_and_invalid_utf8();
    test_utf8_attributes_and_truecolor();
    test_cursor_erase_and_alternate_screen();
    test_scroll_region_and_osc_are_bounded();
    test_flush_reports_only_changes();
    test_scroll_moves_rendered_rows_once();
    test_render_failure_is_reported();
    test_model_works_without_a_renderer();
    test_resize_preserves_cells_and_clamps_cursor();
    test_growth_rebinds_caller_storage();
    test_preallocated_resize();
    test_echo_reports_each_line_once();
    test_terminal_queries_reply_on_the_input_path();
    puts("astra terminal: PASS");
    return 0;
}
