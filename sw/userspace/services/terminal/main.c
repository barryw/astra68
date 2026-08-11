#include <console_shell.h>
#include <loader.h>
#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/event.h>
#include <astra/event_control.h>
#include <astra/event_descriptor.h>
#include <astra/gui.h>
#include <astra/input_service.h>
#include <astra/keymap.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_port_transport.h>
#include <astra/window.h>

#define TERMINAL_VFS_CLIENT_MAX 4u
#define TERMINAL_EVENT_PREFIX_RECORDS 5u
#define TERMINAL_MARGIN_X 10u
#define TERMINAL_MARGIN_Y 8u
#define TERMINAL_LINE_HEIGHT (ASTRA_UI_FONT_BODY_HEIGHT + 3u)

enum {
    TERMINAL_FAIL_SURFACE = ASTRA_STATUS_PROGRAM_FIRST,
    TERMINAL_FAIL_WINDOW = ASTRA_STATUS_PROGRAM_FIRST + 0x10u
};

ASTRA_PROGRAM("terminal", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/*
 * Message ids are descriptor addresses and the installed catalog is still the
 * supervisor catalog. Five supervisor descriptors precede console_shell.c in
 * that catalog, so reserve their addresses here. The Makefile compares the
 * remaining bytes and refuses a build if either layout drifts.
 *
 * ponytail: retain this prefix until catalogs become process-aware; that
 * larger event-format change is not needed to make the terminal a process.
 */
static const uint8_t terminal_event_prefix[
    TERMINAL_EVENT_PREFIX_RECORDS * ASTRA_EVENT_DESCRIPTOR_SIZE]
    __attribute__((section(".astra_events"), used, aligned(4))) = {0u};

static AstraAssignTable assigns;
static struct {
    AstraVfsClient client;
    uint32_t handle;
} clients[TERMINAL_VFS_CLIENT_MAX];
static uint32_t client_count;
static uint32_t event_control;

typedef struct WindowTerminal {
    AstraSharedSurface surface;
    AstraWindow window;
    AstraTerminal *terminal;
    uint16_t width;
    uint16_t height;
    uint16_t cell_width;
    uint8_t dirty;
    uint8_t force_present;
    uint8_t live;
    uint8_t reserved;
    uint32_t cursor_row;
    uint32_t cursor_column;
} WindowTerminal;

static WindowTerminal window_terminal;
static char terminal_line[ASTRA_TERMINAL_COLUMNS_MAX];

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup,
           const AstraStartupCapability *capabilities, const char *name)
{
    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(capabilities[index].name, name))
            return &capabilities[index];
    return NULL;
}

static uint32_t connect_namespaces(void)
{
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t handle = assigns.entries[index].handle;
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot].handle == handle)
                break;
        if (slot != client_count)
            continue;
        if (client_count == TERMINAL_VFS_CLIENT_MAX)
            return ASTRA_STATUS_LIMIT;
        clients[client_count].handle = handle;
        if (astra_vfs_port_connect(&clients[client_count].client,
                                   clients[client_count].handle) !=
                ASTRA_VFS_OK)
            return ASTRA_STATUS_PROTOCOL;
        ++client_count;
    }
    return client_count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}

AstraVfsClient *supervisor_vfs_client(void)
{
    return client_count != 0u ? &clients[0].client : NULL;
}

AstraAssignTable *supervisor_assigns(void)
{
    return &assigns;
}

AstraVfsClient *supervisor_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL)
        return supervisor_vfs_client();
    for (uint32_t index = 0u; index < client_count; ++index)
        if (clients[index].handle == assign->handle)
            return &clients[index].client;
    return NULL;
}

void supervisor_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < client_count; ++index)
        clients[index].client.activity = activity;
}

uint32_t supervisor_loader_event_control(void)
{
    return event_control;
}

void supervisor_loader_pump_event_control(void)
{
    /* The boot-global target remains in the resident supervisor. */
}

uint32_t supervisor_volume_device_status(void)
{
    return 0u;
}

uint32_t supervisor_volume_device_failure(void)
{
    return 0u;
}

static void ready(uint32_t handle, uint32_t status)
{
    AstraServiceReady message;

    (void)memset(&message, 0, sizeof(message));
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    (void)astra_port_send(handle, &message, sizeof(message), NULL, 0u);
}

static uint16_t rgb565(AstraColorRGBA8 color)
{
    return astra_surface_rgb565(color.red, color.green, color.blue);
}

static uint16_t maximum_cell_width(void)
{
    uint32_t maximum = 1u;

    for (uint32_t value = 0x20u; value <= 0x7eu; ++value) {
        char character = (char)value;
        uint32_t width = astra_surface_ui_text_width(
            &character, 1u, ASTRA_UI_FONT_BODY_HEIGHT);

        if (width > maximum)
            maximum = width;
    }
    return (uint16_t)maximum;
}

static uint32_t terminal_columns(const WindowTerminal *window)
{
    uint32_t usable = window->width > TERMINAL_MARGIN_X * 2u ?
        window->width - TERMINAL_MARGIN_X * 2u : 1u;
    uint32_t columns = usable / window->cell_width;

    if (columns == 0u)
        columns = 1u;
    if (columns > ASTRA_TERMINAL_COLUMNS_MAX)
        columns = ASTRA_TERMINAL_COLUMNS_MAX;
    return columns;
}

static uint32_t terminal_rows(const WindowTerminal *window)
{
    uint32_t usable = window->height > TERMINAL_MARGIN_Y * 2u ?
        window->height - TERMINAL_MARGIN_Y * 2u : 1u;
    uint32_t rows = usable / TERMINAL_LINE_HEIGHT;

    if (rows == 0u)
        rows = 1u;
    if (rows > ASTRA_TERMINAL_ROWS_MAX)
        rows = ASTRA_TERMINAL_ROWS_MAX;
    return rows;
}

static int window_render(void *context, uint32_t row, uint32_t column,
                         const uint8_t *cells, uint32_t count)
{
    WindowTerminal *window = context;

    (void)row;
    (void)column;
    (void)cells;
    (void)count;
    window->dirty = 1u;
    return 1;
}

static int window_present(void *context, const AstraTerminal *terminal)
{
    WindowTerminal *window = context;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint16_t background = rgb565(theme.system_bar);
    uint16_t foreground = rgb565(theme.text_primary);
    uint16_t cursor = rgb565(theme.accent);

    window->terminal = (AstraTerminal *)(uintptr_t)terminal;
    if (!window->dirty && !window->force_present &&
        window->cursor_row == terminal->cursor_row &&
        window->cursor_column == terminal->cursor_column)
        return 1;
    if (!astra_draw_list_view_init(&window->surface.view,
                                   window->surface.mapping,
                                   window->surface.view.byte_size,
                                   window->width, window->height))
        return 0;
    astra_surface_clear(&window->surface.view, background);
    for (uint32_t row = 0u; row < terminal->rows; ++row) {
        uint32_t length = terminal->columns;

        for (uint32_t column = 0u; column < terminal->columns; ++column)
            terminal_line[column] = (char)astra_terminal_cell(
                terminal, row, column);
        while (length != 0u && terminal_line[length - 1u] == ' ')
            --length;
        if (length != 0u)
            astra_surface_ui_text(
                &window->surface.view, TERMINAL_MARGIN_X,
                TERMINAL_MARGIN_Y + (int32_t)(row * TERMINAL_LINE_HEIGHT),
                terminal_line, length, ASTRA_UI_FONT_BODY_HEIGHT,
                foreground);
    }
    {
        uint32_t column = terminal->cursor_column;
        uint32_t cursor_x;
        uint32_t cursor_width;

        if (column > terminal->columns)
            column = terminal->columns;
        cursor_x = astra_surface_ui_text_width(
            terminal_line, 0u, ASTRA_UI_FONT_BODY_HEIGHT);
        for (uint32_t at = 0u; at < column; ++at) {
            char character = (char)astra_terminal_cell(
                terminal, terminal->cursor_row, at);

            cursor_x += astra_surface_ui_text_width(
                &character, 1u, ASTRA_UI_FONT_BODY_HEIGHT);
        }
        {
            char character = column < terminal->columns ?
                (char)astra_terminal_cell(terminal, terminal->cursor_row,
                                          column) : ' ';

            cursor_width = astra_surface_ui_text_width(
                &character, 1u, ASTRA_UI_FONT_BODY_HEIGHT);
        }
        if (cursor_width == 0u)
            cursor_width = 4u;
        astra_surface_fill(
            &window->surface.view,
            TERMINAL_MARGIN_X + (int32_t)cursor_x,
            TERMINAL_MARGIN_Y +
                (int32_t)(terminal->cursor_row * TERMINAL_LINE_HEIGHT) +
                ASTRA_UI_FONT_BODY_HEIGHT + 1,
            cursor_width, 2u, cursor);
    }
    if (astra_window_present(&window->window) != ASTRA_OK)
        return 0;
    window->dirty = 0u;
    window->force_present = 0u;
    window->cursor_row = terminal->cursor_row;
    window->cursor_column = terminal->cursor_column;
    return 1;
}

static uint32_t keymap_modifiers(uint32_t modifiers)
{
    uint32_t mapped = 0u;

    if ((modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_SHIFT;
    if ((modifiers & ASTRA_INPUT_MOD_CTRL) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_CONTROL;
    if ((modifiers & ASTRA_INPUT_MOD_CAPS_LOCK) != 0u)
        mapped |= ASTRA_KEYMAP_MOD_CAPS;
    return mapped;
}

static int window_next_key(void *context, uint32_t *key)
{
    WindowTerminal *window = context;

    for (;;) {
        AstraWindowEvent event = {0};
        AstraResult result = astra_window_event_try(&window->window, &event);

        if (result == ASTRA_ERROR_WOULD_BLOCK)
            return CONSOLE_SHELL_INPUT_NONE;
        if (result != ASTRA_OK)
            return CONSOLE_SHELL_INPUT_ERROR;
        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) {
            if (astra_window_close(&window->window) != ASTRA_OK)
                return CONSOLE_SHELL_INPUT_ERROR;
            window->live = 0u;
            return CONSOLE_SHELL_INPUT_STOP;
        }
        if (event.type == ASTRA_WINDOW_EVENT_FRAME) {
            uint16_t width = event.data.frame.frame.width;
            uint16_t height = event.data.frame.frame.height;

            if (width != 0u && height != 0u &&
                (width != window->width || height != window->height)) {
                window->width = width;
                window->height = height;
                if (window->terminal == NULL ||
                    astra_terminal_resize(window->terminal,
                                          terminal_columns(window),
                                          terminal_rows(window)) !=
                        ASTRA_TERMINAL_OK)
                    return CONSOLE_SHELL_INPUT_ERROR;
                window->force_present = 1u;
            }
            continue;
        }
        if (event.type == ASTRA_WINDOW_EVENT_KEY &&
            (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
            uint32_t modifiers = event.data.key.modifiers;
            uint32_t translated = astra_keymap_translate(
                event.data.key.usage, keymap_modifiers(modifiers));
            int chord = (modifiers & (ASTRA_INPUT_MOD_CTRL |
                                      ASTRA_INPUT_MOD_ALT |
                                      ASTRA_INPUT_MOD_GUI)) != 0u;

            if (translated == ASTRA_KEYMAP_BACKSPACE ||
                translated == ASTRA_KEYMAP_DELETE ||
                translated == ASTRA_KEYMAP_LEFT ||
                translated == ASTRA_KEYMAP_RIGHT ||
                translated == ASTRA_KEYMAP_UP ||
                translated == ASTRA_KEYMAP_DOWN ||
                translated == ASTRA_KEYMAP_HOME ||
                translated == ASTRA_KEYMAP_END ||
                (chord && translated != ASTRA_KEYMAP_NONE)) {
                *key = translated;
                return CONSOLE_SHELL_INPUT_KEY;
            }
            continue;
        }
        if (event.type == ASTRA_WINDOW_EVENT_TEXT) {
            uint32_t codepoint = event.data.text.codepoint;

            if (codepoint == '\n' || codepoint == '\r')
                *key = ASTRA_KEYMAP_ENTER;
            else if (codepoint >= 0x20u && codepoint <= 0x7eu)
                *key = codepoint;
            else
                continue;
            return CONSOLE_SHELL_INPUT_KEY;
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    const AstraStartupCapability *control;
    ConsoleShellBackend backend;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    bootstrap = capability(startup, capabilities,
                           ASTRA_CAPABILITY_SERVICE_READY);
    gui = capability(startup, capabilities, ASTRA_CAPABILITY_GUI);
    control = capability(startup, capabilities,
                         ASTRA_CAPABILITY_EVENT_CONTROL);
    if (bootstrap == NULL || gui == NULL || control == NULL)
        return ASTRA_STATUS_BAD_HANDLE;

    status = astra_assign_seed(&assigns, capabilities,
                               startup->capability_count) == ASTRA_VFS_OK ?
        connect_namespaces() : ASTRA_STATUS_INVALID;
    event_control = control->handle;

    (void)memset(&window_terminal, 0, sizeof(window_terminal));
    window_terminal.width = 840u;
    window_terminal.height = 460u;
    window_terminal.cell_width = maximum_cell_width();
    window_terminal.cursor_row = UINT32_MAX;
    window_terminal.cursor_column = UINT32_MAX;
    window_terminal.force_present = 1u;
    if (status == ASTRA_STATUS_OK &&
        astra_shared_draw_list_create(&window_terminal.surface,
                                      window_terminal.width,
                                      window_terminal.height) !=
            ASTRA_SYSCALL_OK)
        status = TERMINAL_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        AstraResult result;

        astra_surface_clear(&window_terminal.surface.view,
                            rgb565(theme.system_bar));
        info.flags = ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE;
        info.x = 180u;
        info.y = 90u;
        info.width = window_terminal.width;
        info.height = window_terminal.height;
        info.pitch = 0u;
        info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
        info.gadgets = ASTRA_WINDOW_GADGET_CLOSE |
                       ASTRA_WINDOW_GADGET_MINIMIZE |
                       ASTRA_WINDOW_GADGET_MAXIMIZE;
        info.type = ASTRA_WINDOW_STANDARD;
        info.title = "TERMINAL";
        info.title_length = 8u;
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_ALL;
        result = astra_window_create(gui->handle, window_terminal.surface.area,
                                     &info, &window_terminal.window);
        if (result != ASTRA_OK)
            status = TERMINAL_FAIL_WINDOW + (uint32_t)(-result);
        else
            window_terminal.live = 1u;
    }

    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    backend.columns = terminal_columns(&window_terminal);
    backend.rows = terminal_rows(&window_terminal);
    backend.render = window_render;
    backend.context = &window_terminal;
    backend.present = window_present;
    backend.next_key = window_next_key;
    backend.wait_handle = astra_window_event_wait_handle(
        &window_terminal.window);
    console_shell_run_backend(&backend, 1);
    if (window_terminal.live) {
        AstraResult close_result = astra_window_close(&window_terminal.window);

        (void)close_result;
    }
    (void)astra_shared_surface_close(&window_terminal.surface);
    return ASTRA_STATUS_OK;
}
