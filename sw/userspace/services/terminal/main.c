#include <console_shell.h>
#include <loader.h>
#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/event.h>
#include <astra/event_control.h>
#include <astra/event_descriptor.h>
#include <astra/gui.h>
#include <astra/graphics_library.h>
#include <astra/graphics_kit.h>
#include <astra/input_service.h>
#include <astra/keymap.h>
#include <astra/font_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/shared_library.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#define TERMINAL_EVENT_PREFIX_RECORDS 6u
#define TERMINAL_MARGIN_X 10u
#define TERMINAL_MARGIN_Y 8u
#define TERMINAL_FONT_HEIGHT ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT
#define TERMINAL_LINE_HEIGHT (TERMINAL_FONT_HEIGHT + 4u)
#define TERMINAL_CURSOR_HEIGHT 2u
#define TERMINAL_CURSOR_BLINK_NS UINT64_C(500000000)

enum {
    TERMINAL_FAIL_SURFACE = ASTRA_STATUS_PROGRAM_FIRST,
    TERMINAL_FAIL_WINDOW = ASTRA_STATUS_PROGRAM_FIRST + 0x10u,
    TERMINAL_FAIL_FONT = ASTRA_STATUS_PROGRAM_FIRST + 0x20u,
    TERMINAL_FAIL_LIBRARY = ASTRA_STATUS_PROGRAM_FIRST + 0x30u
};

ASTRA_PROGRAM("terminal", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/*
 * Message ids are descriptor addresses and the installed catalog is still the
 * supervisor catalog. Six supervisor descriptors precede console_shell.c in
 * that catalog, so reserve their addresses here. The Makefile compares the
 * remaining bytes and refuses a build if either layout drifts.
 *
 * ponytail: retain this prefix until catalogs become process-aware; that
 * larger event-format change is not needed to make the terminal a process.
 */
static const uint8_t terminal_event_prefix[
    TERMINAL_EVENT_PREFIX_RECORDS * ASTRA_EVENT_DESCRIPTOR_SIZE]
    __attribute__((section(".astra_events"), used, aligned(4))) = {0u};

static uint32_t event_control;
static AstraLibraryHandle *font_handle;
static AstraLibraryHandle *graphics_handle;
static const AstraFontLibraryV1 *font_library;
static const AstraGraphicsLibraryV1 *graphics_library;
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;

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
    uint8_t cursor_visible;
    uint32_t cursor_row;
    uint32_t cursor_column;
    uint64_t cursor_deadline;
    AstraWindowFrame damage;
    uint8_t damage_valid;
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

AstraVfsClient *supervisor_vfs_client(void)
{
    return astra_process_vfs_client();
}

AstraAssignTable *supervisor_assigns(void)
{
    return astra_process_vfs_assigns();
}

AstraVfsClient *supervisor_vfs_client_for(const AstraAssign *assign)
{
    return astra_process_vfs_client_for(assign);
}

void supervisor_vfs_set_activity(uint32_t activity)
{
    AstraAssignTable *table = astra_process_vfs_assigns();

    for (uint32_t index = 0u; index < table->count; ++index) {
        AstraVfsClient *client = astra_process_vfs_client_for(
            &table->entries[index]);

        if (client != NULL)
            client->activity = activity;
    }
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

static uint32_t load_graphics_kit(void)
{
    font_handle = OpenLibrary(ASTRA_FONT_LIBRARY_NAME,
                              ASTRA_FONT_LIBRARY_VERSION);
    if (font_handle == NULL)
        return TERMINAL_FAIL_LIBRARY;
    font_library = font_handle->exports;
    if (font_library->abi_major != ASTRA_FONT_LIBRARY_ABI_MAJOR ||
        font_library->structure_size < sizeof(*font_library))
        return TERMINAL_FAIL_LIBRARY;

    graphics_handle = OpenLibrary(ASTRA_GRAPHICS_LIBRARY_NAME,
                                  ASTRA_GRAPHICS_LIBRARY_VERSION);
    if (graphics_handle == NULL)
        return TERMINAL_FAIL_LIBRARY;
    graphics_library = graphics_handle->exports;
    if (graphics_library->abi_major != ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR ||
        graphics_library->structure_size < sizeof(*graphics_library))
        return TERMINAL_FAIL_LIBRARY;
    return ASTRA_STATUS_OK;
}

static uint16_t rgb565(AstraColorRGBA8 color)
{
    return graphics_library->rgb565(color.red, color.green, color.blue);
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

static void window_damage(WindowTerminal *window, uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height)
{
    uint32_t old_right = (uint32_t)window->damage.x + window->damage.width;
    uint32_t old_bottom = (uint32_t)window->damage.y + window->damage.height;
    uint32_t right = x + width;
    uint32_t bottom = y + height;

    if (!window->damage_valid) {
        window->damage = (AstraWindowFrame){x, y, width, height};
        window->damage_valid = 1u;
        return;
    }
    if (x < window->damage.x)
        window->damage.x = (uint16_t)x;
    if (y < window->damage.y)
        window->damage.y = (uint16_t)y;
    if (right < old_right)
        right = old_right;
    if (bottom < old_bottom)
        bottom = old_bottom;
    window->damage.width = (uint16_t)(right - window->damage.x);
    window->damage.height = (uint16_t)(bottom - window->damage.y);
}

static int window_render(void *context, uint32_t row, uint32_t column,
                         const uint8_t *cells, uint32_t count)
{
    WindowTerminal *window = context;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t first = 0u;
    uint32_t last = count;
    uint32_t x = TERMINAL_MARGIN_X + column * window->cell_width;
    uint32_t y = TERMINAL_MARGIN_Y + row * TERMINAL_LINE_HEIGHT;
    uint32_t width = count * window->cell_width;

    if (!window->dirty && !graphics_library->draw_list_view_init(
            &window->surface.view, window->surface.mapping,
            window->surface.view.byte_size, window->width, window->height))
        return 0;
    graphics_library->fill(&window->surface.view, x, y, width,
                           TERMINAL_LINE_HEIGHT, rgb565(theme.system_bar));
    while (first < last && cells[first] == ' ')
        ++first;
    while (last > first && cells[last - 1u] == ' ')
        --last;
    if (first < last)
        font_library->draw_list_mono_text(
            &window->surface.view, x + first * window->cell_width, y,
            (const char *)cells + first, last - first,
            TERMINAL_FONT_HEIGHT, window->cell_width,
            rgb565(theme.text_primary));
    window_damage(window, x, y, width, TERMINAL_LINE_HEIGHT);
    window->dirty = 1u;
    return 1;
}

static void draw_cursor(WindowTerminal *window, uint32_t row, uint32_t column,
                        uint16_t color)
{
    uint32_t x = TERMINAL_MARGIN_X + column * window->cell_width;
    uint32_t y = TERMINAL_MARGIN_Y + row * TERMINAL_LINE_HEIGHT +
                 TERMINAL_FONT_HEIGHT + 1u;

    graphics_library->fill(&window->surface.view, x, y, window->cell_width,
                           TERMINAL_CURSOR_HEIGHT, color);
    window_damage(window, x, y, window->cell_width, TERMINAL_CURSOR_HEIGHT);
}

static int window_present(void *context, const AstraTerminal *terminal)
{
    WindowTerminal *window = context;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint16_t background = rgb565(theme.system_bar);
    uint16_t foreground = rgb565(theme.text_primary);
    uint16_t cursor = rgb565(theme.accent);
    uint64_t now = astra_clock_monotonic();
    int cursor_moved = window->cursor_row != terminal->cursor_row ||
                       window->cursor_column != terminal->cursor_column;
    int blink = window->cursor_deadline != 0u &&
                now >= window->cursor_deadline;

    window->terminal = (AstraTerminal *)(uintptr_t)terminal;
    if (!window->dirty && !window->force_present && !cursor_moved && !blink)
        return 1;
    if (window->force_present) {
        if (!graphics_library->draw_list_view_init(&window->surface.view,
                                       window->surface.mapping,
                                       window->surface.view.byte_size,
                                       window->width, window->height))
            return 0;
        graphics_library->clear(&window->surface.view, background);
        window->damage = (AstraWindowFrame){0u, 0u, window->width,
                                             window->height};
        window->damage_valid = 1u;
        for (uint32_t row = 0u; row < terminal->rows; ++row) {
            uint32_t first = 0u;
            uint32_t last = terminal->columns;

            for (uint32_t column = 0u; column < terminal->columns; ++column)
                terminal_line[column] = (char)astra_terminal_cell(
                    terminal, row, column);
            while (first < last && terminal_line[first] == ' ')
                ++first;
            while (last > first && terminal_line[last - 1u] == ' ')
                --last;
            if (first < last)
                font_library->draw_list_mono_text(
                    &window->surface.view,
                    TERMINAL_MARGIN_X + first * window->cell_width,
                    TERMINAL_MARGIN_Y + row * TERMINAL_LINE_HEIGHT,
                    terminal_line + first, last - first,
                    TERMINAL_FONT_HEIGHT, window->cell_width, foreground);
        }
    } else {
        if (!window->dirty && !graphics_library->draw_list_view_init(
                &window->surface.view, window->surface.mapping,
                window->surface.view.byte_size, window->width,
                window->height))
            return 0;
        if (window->cursor_row != UINT32_MAX)
            draw_cursor(window, window->cursor_row, window->cursor_column,
                        background);
    }
    if (window->dirty || window->force_present || cursor_moved)
        window->cursor_visible = 1u;
    else if (blink)
        window->cursor_visible ^= 1u;
    if (window->cursor_visible)
        draw_cursor(window, terminal->cursor_row, terminal->cursor_column,
                    cursor);
    window->cursor_deadline = now + TERMINAL_CURSOR_BLINK_NS;
    if (!window->damage_valid ||
        astra_window_present_region(&window->window, &window->damage) !=
            ASTRA_OK)
        return 0;
    window->dirty = 0u;
    window->force_present = 0u;
    window->damage_valid = 0u;
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

    status = astra_process_filesystem_open(&process_filesystem, startup);
    event_control = control->handle;
    if (status == ASTRA_STATUS_OK)
        status = load_graphics_kit();

    (void)memset(&window_terminal, 0, sizeof(window_terminal));
    window_terminal.width = 840u;
    window_terminal.height = 460u;
    window_terminal.cell_width = status == ASTRA_STATUS_OK ?
        font_library->mono_cell_width(TERMINAL_FONT_HEIGHT) : 0u;
    window_terminal.cursor_row = UINT32_MAX;
    window_terminal.cursor_column = UINT32_MAX;
    window_terminal.cursor_visible = 1u;
    window_terminal.force_present = 1u;
    if (status == ASTRA_STATUS_OK && window_terminal.cell_width == 0u)
        status = TERMINAL_FAIL_FONT;
    if (status == ASTRA_STATUS_OK &&
        graphics_library->shared_draw_list_create(
            &window_terminal.surface, window_terminal.width,
            window_terminal.height) !=
            ASTRA_SYSCALL_OK)
        status = TERMINAL_FAIL_SURFACE;
    if (status == ASTRA_STATUS_OK) {
        AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        AstraResult result;

        graphics_library->clear(&window_terminal.surface.view,
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
        info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                          ASTRA_WINDOW_SUBSCRIBE_KEY |
                          ASTRA_WINDOW_SUBSCRIBE_TEXT;
        result = astra_window_create(gui->handle, window_terminal.surface.area,
                                     &info, &window_terminal.window);
        if (result != ASTRA_OK)
            status = TERMINAL_FAIL_WINDOW + (uint32_t)(-result);
        else
            window_terminal.live = 1u;
    }

    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) {
        CloseLibrary(graphics_handle);
        CloseLibrary(font_handle);
        astra_process_filesystem_close(&process_filesystem);
        return (int)status;
    }
    backend.columns = terminal_columns(&window_terminal);
    backend.rows = terminal_rows(&window_terminal);
    backend.render = window_render;
    backend.context = &window_terminal;
    backend.present = window_present;
    backend.next_key = window_next_key;
    backend.wait_handle = astra_window_event_wait_handle(
        &window_terminal.window);
    backend.idle_poll_ns = TERMINAL_CURSOR_BLINK_NS;
    backend.filesystem = &process_filesystem.filesystem;
    backend.filesystem_library = process_filesystem.library;
    console_shell_run_backend(&backend, 1);
    if (window_terminal.live) {
        AstraResult close_result = astra_window_close(&window_terminal.window);

        (void)close_result;
    }
    (void)graphics_library->shared_surface_close(&window_terminal.surface);
    CloseLibrary(graphics_handle);
    CloseLibrary(font_handle);
    astra_process_filesystem_close(&process_filesystem);
    return ASTRA_STATUS_OK;
}
