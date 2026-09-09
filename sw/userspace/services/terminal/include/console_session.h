#ifndef ASTRA_TERMINAL_CONSOLE_SESSION_H
#define ASTRA_TERMINAL_CONSOLE_SESSION_H

#include <stdint.h>

#include <astra/terminal.h>
#include <astra/vfs_process.h>

typedef struct ConsoleSessionBackend {
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
    const AstraStartupInfo *startup;
} ConsoleSessionBackend;

enum {
    CONSOLE_SESSION_INPUT_STOP = -2,
    CONSOLE_SESSION_INPUT_ERROR = -1,
    CONSOLE_SESSION_INPUT_NONE = 0,
    CONSOLE_SESSION_INPUT_KEY = 1
};

/* Runs one interactive zsh session and returns its exit status. */
uint32_t console_session_run_backend(const ConsoleSessionBackend *backend);

#endif
