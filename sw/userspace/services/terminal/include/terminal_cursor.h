#ifndef ASTRA_TERMINAL_CURSOR_H
#define ASTRA_TERMINAL_CURSOR_H

#include <stdint.h>

typedef struct TerminalCursorState {
    uint64_t deadline;
    uint8_t active;
    uint8_t visible;
} TerminalCursorState;

void terminal_cursor_set_focus(TerminalCursorState *cursor, int focused);
int terminal_cursor_blink_due(const TerminalCursorState *cursor,
                              uint64_t now);
void terminal_cursor_presented(TerminalCursorState *cursor, uint64_t now,
                               uint64_t interval, int reset);

#endif
