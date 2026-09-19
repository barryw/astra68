#include <terminal_cursor.h>

void terminal_cursor_set_focus(TerminalCursorState *cursor, int focused)
{
    cursor->active = focused != 0;
    cursor->visible = 1u;
    cursor->deadline = 0u;
}

int terminal_cursor_blink_due(const TerminalCursorState *cursor,
                              uint64_t now)
{
    return cursor->active != 0u && cursor->deadline != 0u &&
           now >= cursor->deadline;
}

void terminal_cursor_presented(TerminalCursorState *cursor, uint64_t now,
                               uint64_t interval, int reset)
{
    if (cursor->active == 0u) {
        cursor->visible = 1u;
        cursor->deadline = 0u;
        return;
    }
    if (reset)
        cursor->visible = 1u;
    else if (terminal_cursor_blink_due(cursor, now))
        cursor->visible ^= 1u;
    cursor->deadline = now + interval;
}
