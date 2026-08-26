#ifndef ASTRA_TERMINAL_CONSOLE_SHELL_H
#define ASTRA_TERMINAL_CONSOLE_SHELL_H

#include <stdint.h>

#include <astra/terminal.h>
#include <astra/vfs_process.h>

typedef struct ConsoleShellBackend {
    uint32_t columns;
    uint32_t rows;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t terminal_capacity_columns;
    uint32_t terminal_capacity_rows;
    void *terminal_storage;
    uint32_t terminal_storage_size;
    AstraTerminalRender render;
    AstraTerminalScroll scroll;
    void *context;
    int (*present)(void *context, const AstraTerminal *terminal);
    int (*next_key)(void *context, uint32_t *key);
    uint32_t wait_handle;
    uint64_t idle_poll_ns;
    AstraProcessFilesystem *process_filesystem;
    uint32_t event_control;
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
void console_shell_run_backend(const ConsoleShellBackend *backend);

#endif
