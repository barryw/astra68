#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <terminal_scrollback.h>

static unsigned allocation_calls;
static unsigned fail_on_call;

static void *test_reallocate(void *pointer, size_t size)
{
    if (size == 0u) {
        free(pointer);
        return NULL;
    }
    if (++allocation_calls == fail_on_call)
        return NULL;
    return realloc(pointer, size);
}

static AstraTextCell row(uint32_t value)
{
    return (AstraTextCell){value, value + 1u, value + 2u,
                           (uint16_t)value, 1u, 0u};
}

static void test_rows_grow_and_wheel_clamps(void)
{
    TerminalScrollback backlog;
    AstraTextCell first[] = {row('a'), row('b')};
    AstraTextCell second[] = {row('c'), row('d'), row('e')};
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    const AstraTextCell *stored;
    uint32_t columns;

    allocation_calls = 0u;
    fail_on_call = 0u;
    assert(terminal_scrollback_init(&backlog, 80u, 2u, 10u,
                                    test_reallocate));
    assert(terminal_scrollback_append(&backlog, first, 2u));
    assert(terminal_scrollback_append(&backlog, second, 3u));
    stored = terminal_scrollback_row(&backlog, 1u, &columns);
    assert(stored != NULL && columns == 3u);
    assert(stored[0].codepoint == 'c' && stored[2].background == 'e' + 2u);
    assert(terminal_scrollback_at_bottom(&backlog));

    assert(terminal_scrollback_wheel(&backlog, 0, 1));
    assert(astra_scroll_get_state(&backlog.scroll, &state) == ASTRA_OK);
    assert(state.offset_y == 10u && state.maximum_y == 20u);
    assert(terminal_scrollback_wheel(&backlog, 0, INT32_MAX));
    assert(astra_scroll_get_state(&backlog.scroll, &state) == ASTRA_OK);
    assert(state.offset_y == 0u);
    assert(terminal_scrollback_wheel(&backlog, 0, INT32_MIN));
    assert(astra_scroll_get_state(&backlog.scroll, &state) == ASTRA_OK);
    assert(state.offset_y == state.maximum_y);
    terminal_scrollback_destroy(&backlog);
}

static void test_allocation_failure_preserves_existing_rows(void)
{
    TerminalScrollback backlog;
    AstraTextCell first[] = {row('x')};
    AstraTextCell second[] = {row('y'), row('z')};
    const AstraTextCell *stored;
    uint32_t columns;

    allocation_calls = 0u;
    fail_on_call = 0u;
    assert(terminal_scrollback_init(&backlog, 80u, 2u, 10u,
                                    test_reallocate));
    assert(terminal_scrollback_append(&backlog, first, 1u));
    fail_on_call = allocation_calls + 2u;
    assert(!terminal_scrollback_append(&backlog, second, 2u));
    assert(backlog.row_count == 1u && backlog.cell_count == 1u);
    stored = terminal_scrollback_row(&backlog, 0u, &columns);
    assert(stored != NULL && columns == 1u && stored[0].codepoint == 'x');
    assert(terminal_scrollback_row(&backlog, 1u, &columns) == NULL);
    fail_on_call = 0u;
    assert(terminal_scrollback_append(&backlog, second, 2u));
    stored = terminal_scrollback_row(&backlog, 1u, &columns);
    assert(stored != NULL && columns == 2u && stored[1].codepoint == 'z');
    terminal_scrollback_destroy(&backlog);
}

static void test_scroll_coordinate_overflow_is_rejected(void)
{
    TerminalScrollback backlog;
    AstraTextCell cell = row('x');

    allocation_calls = 0u;
    fail_on_call = 0u;
    assert(terminal_scrollback_init(&backlog, 80u, 2u, 10u,
                                    test_reallocate));
    backlog.row_count = UINT32_MAX - 1u;
    assert(!terminal_scrollback_append(&backlog, &cell, 1u));
    assert(allocation_calls == 0u);
    backlog.row_count = 0u;
    terminal_scrollback_destroy(&backlog);
}

int main(void)
{
    test_rows_grow_and_wheel_clamps();
    test_allocation_failure_preserves_existing_rows();
    test_scroll_coordinate_overflow_is_rejected();
    puts("terminal scrollback: PASS");
    return 0;
}
