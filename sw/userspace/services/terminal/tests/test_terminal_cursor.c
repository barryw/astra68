#include <terminal_cursor.h>

#include <assert.h>
#include <stdio.h>

int main(void)
{
    TerminalCursorState cursor = {0u, 0u, 0u};

    terminal_cursor_set_focus(&cursor, 1);
    terminal_cursor_presented(&cursor, 100u, 50u, 1);
    assert(cursor.active == 1u && cursor.visible == 1u &&
           cursor.deadline == 150u);
    assert(!terminal_cursor_blink_due(&cursor, 149u));
    assert(terminal_cursor_blink_due(&cursor, 150u));
    terminal_cursor_presented(&cursor, 150u, 50u, 0);
    assert(cursor.visible == 0u && cursor.deadline == 200u);

    terminal_cursor_set_focus(&cursor, 0);
    cursor.visible = 0u;
    terminal_cursor_presented(&cursor, 1000u, 50u, 0);
    assert(cursor.active == 0u && cursor.visible == 1u &&
           cursor.deadline == 0u);
    assert(!terminal_cursor_blink_due(&cursor, UINT64_MAX));
    puts("terminal cursor tests passed");
    return 0;
}
