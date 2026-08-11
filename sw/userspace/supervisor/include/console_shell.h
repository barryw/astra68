#ifndef ASTRA_SUPERVISOR_CONSOLE_SHELL_H
#define ASTRA_SUPERVISOR_CONSOLE_SHELL_H

#include <stdint.h>

#include <astra/terminal.h>

typedef struct ConsoleShellBackend {
    uint32_t columns;
    uint32_t rows;
    AstraTerminalRender render;
    void *context;
    int (*present)(void *context, const AstraTerminal *terminal);
    int (*next_key)(void *context, uint32_t *key);
    uint32_t wait_handle;
} ConsoleShellBackend;

enum {
    CONSOLE_SHELL_INPUT_STOP = -2,
    CONSOLE_SHELL_INPUT_ERROR = -1,
    CONSOLE_SHELL_INPUT_NONE = 0,
    CONSOLE_SHELL_INPUT_KEY = 1
};

/*
 * Runs the terminal until it is asked to stop. Never returns a failure code:
 * the status halfword has no bits left, so how far it got is reported through
 * the progress counter and the caller parks either way.
 */
void console_shell_run(uint32_t display, uint32_t input, uint32_t input_irq,
                       int volume_ready);
void console_shell_run_backend(const ConsoleShellBackend *backend,
                               int volume_ready);

#endif
