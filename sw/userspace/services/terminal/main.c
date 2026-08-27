#include <console_shell.h>
#include <console_stream.h>

#include <astra/bytes.h>
#include <astra/area.h>
#include <astra/bundle.h>
#include <astra/event_control.h>
#include <astra/gui.h>
#include <astra/graphics_library.h>
#include <astra/graphics_kit.h>
#include <astra/input_service.h>
#include <astra/keymap.h>
#include <astra/font_library.h>
#include <astra/port.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/shared_library.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

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
    TERMINAL_FAIL_LIBRARY = ASTRA_STATUS_PROGRAM_FIRST + 0x30u,
    TERMINAL_FAIL_ICON = ASTRA_STATUS_PROGRAM_FIRST + 0x40u,
    TERMINAL_FAIL_STORAGE = ASTRA_STATUS_PROGRAM_FIRST + 0x50u
};

ASTRA_PROGRAM("terminal", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraLibraryHandle *font_handle;
static AstraLibraryHandle *graphics_handle;
static const AstraFontLibraryV1 *font_library;
static const AstraGraphicsLibraryV1 *graphics_library;
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;

typedef struct WindowTerminal {
    AstraSharedSurface surface;
    AstraArea model_area;
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
static char bundle_manifest_text[ASTRA_BUNDLE_MANIFEST_MAX + 1u];

static void draw_cursor(WindowTerminal *window, uint32_t row, uint32_t column,
                        uint16_t color);

static void close_area(AstraArea *area)
{
    AstraResult ignored = astra_area_close(area);

    (void)ignored;
}

static uint32_t load_title_icon(AstraArea *area, uint32_t *length)
{
    AstraBundleManifest manifest;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t manifest_length = 0u;
    uint32_t line = 0u;
    AstraResult result;

    if (astra_process_read_file(&process_filesystem, "APP:manifest",
                                bundle_manifest_text,
                                ASTRA_BUNDLE_MANIFEST_MAX,
                                &manifest_length) != ASTRA_VFS_OK)
        return TERMINAL_FAIL_ICON;
    bundle_manifest_text[manifest_length] = '\0';
    if (astra_bundle_manifest_parse(bundle_manifest_text, manifest_length,
                                    &manifest, &line) != ASTRA_BUNDLE_OK ||
        process_filesystem.library->qualify(
            "APP", "", manifest.icon, path, sizeof(path)) != ASTRA_VFS_OK)
        return TERMINAL_FAIL_ICON;
    result = astra_area_create(
        ASTRA_WINDOW_TITLE_ICON_BYTES_MAX,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
            ASTRA_RIGHT_TRANSFER,
        area);
    if (result == ASTRA_OK)
        result = astra_area_map(area,
                                ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result != ASTRA_OK ||
        astra_process_read_file(&process_filesystem, path, area->address,
                                area->size, length) != ASTRA_VFS_OK ||
        *length == 0u) {
        if (area->handle != ASTRA_INVALID_HANDLE)
            close_area(area);
        return TERMINAL_FAIL_ICON;
    }
    return ASTRA_STATUS_OK;
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
        graphics_library->abi_minor < ASTRA_GRAPHICS_LIBRARY_ABI_MINOR ||
        graphics_library->structure_size < sizeof(*graphics_library))
        return TERMINAL_FAIL_LIBRARY;
    return ASTRA_STATUS_OK;
}

static uint16_t rgb565(AstraColorRGBA8 color)
{
    return graphics_library->rgb565(color.red, color.green, color.blue);
}

static uint16_t terminal_color(uint32_t color, uint16_t fallback)
{
    static const uint8_t ansi[16][3] = {
        {0u, 0u, 0u},       {205u, 49u, 49u},   {13u, 188u, 121u},
        {229u, 229u, 16u},  {36u, 114u, 200u},  {188u, 63u, 188u},
        {17u, 168u, 205u},  {229u, 229u, 229u}, {102u, 102u, 102u},
        {241u, 76u, 76u},   {35u, 209u, 139u},  {245u, 245u, 67u},
        {59u, 142u, 234u},  {214u, 112u, 214u}, {41u, 184u, 219u},
        {255u, 255u, 255u},
    };
    uint32_t red;
    uint32_t green;
    uint32_t blue;

    if (color == ASTRA_TERMINAL_COLOR_DEFAULT)
        return fallback;
    if (ASTRA_TERMINAL_COLOR_IS_RGB(color)) {
        red = color >> 16u & 0xffu;
        green = color >> 8u & 0xffu;
        blue = color & 0xffu;
    } else if (color < 16u) {
        red = ansi[color][0];
        green = ansi[color][1];
        blue = ansi[color][2];
    } else if (color < 232u) {
        static const uint8_t level[6] = {0u, 95u, 135u, 175u, 215u, 255u};
        uint32_t cube = color - 16u;

        red = level[cube / 36u];
        green = level[cube / 6u % 6u];
        blue = level[cube % 6u];
    } else {
        red = green = blue = 8u + (color - 232u) * 10u;
    }
    return graphics_library->rgb565((uint8_t)red, (uint8_t)green,
                                    (uint8_t)blue);
}

static uint32_t terminal_columns(const WindowTerminal *window)
{
    uint32_t usable = window->width > TERMINAL_MARGIN_X * 2u ?
        window->width - TERMINAL_MARGIN_X * 2u : 1u;
    uint32_t columns = usable / window->cell_width;

    if (columns == 0u)
        columns = 1u;
    return columns;
}

static uint32_t terminal_rows(const WindowTerminal *window)
{
    uint32_t usable = window->height > TERMINAL_MARGIN_Y * 2u ?
        window->height - TERMINAL_MARGIN_Y * 2u : 1u;
    uint32_t rows = usable / TERMINAL_LINE_HEIGHT;

    if (rows == 0u)
        rows = 1u;
    return rows;
}

static void draw_cells(WindowTerminal *window, uint32_t x, uint32_t y,
                       const AstraTerminalCell *cells, uint32_t count,
                       uint16_t color)
{
    char utf8[count * 4u];
    uint32_t bytes = 0u;

    for (uint32_t index = 0u; index < count; ++index)
        bytes += astra_utf8_encode(cells[index].codepoint, utf8 + bytes);
    font_library->draw_list_mono_text(
        &window->surface.view, (int32_t)x, (int32_t)y, utf8, bytes,
        TERMINAL_FONT_HEIGHT, window->cell_width, color);
}

static void paint_cells(WindowTerminal *window, uint32_t row, uint32_t column,
                        const AstraTerminalCell *cells, uint32_t count)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint16_t default_foreground = rgb565(theme.text_primary);
    uint16_t default_background = rgb565(theme.system_bar);
    uint32_t first = 0u;

    while (first < count) {
        uint32_t last = first + 1u;
        uint32_t foreground_value = cells[first].foreground;
        uint32_t background_value = cells[first].background;
        uint16_t attributes = cells[first].attributes;
        uint16_t foreground;
        uint16_t background;
        uint32_t visible_first;
        uint32_t visible_last;
        uint32_t x = TERMINAL_MARGIN_X +
            (column + first) * window->cell_width;
        uint32_t y = TERMINAL_MARGIN_Y + row * TERMINAL_LINE_HEIGHT;

        while (last < count &&
               cells[last].foreground == foreground_value &&
               cells[last].background == background_value &&
               cells[last].attributes == attributes)
            ++last;
        foreground = terminal_color(foreground_value, default_foreground);
        background = terminal_color(background_value, default_background);
        if ((attributes & ASTRA_TERMINAL_INVERSE) != 0u) {
            uint16_t swap = foreground;
            foreground = background;
            background = swap;
        }
        if ((attributes & ASTRA_TERMINAL_HIDDEN) != 0u)
            foreground = background;
        graphics_library->fill(&window->surface.view, x, y,
                               (last - first) * window->cell_width,
                               TERMINAL_LINE_HEIGHT, background);
        visible_first = first;
        visible_last = last;
        while (visible_first < visible_last &&
               cells[visible_first].codepoint == ' ')
            ++visible_first;
        while (visible_last > visible_first &&
               cells[visible_last - 1u].codepoint == ' ')
            --visible_last;
        if (visible_first < visible_last)
            draw_cells(window,
                       TERMINAL_MARGIN_X +
                           (column + visible_first) * window->cell_width,
                       y, cells + visible_first, visible_last - visible_first,
                       foreground);
        if ((attributes & ASTRA_TERMINAL_UNDERLINE) != 0u)
            graphics_library->fill(
                &window->surface.view, x,
                y + TERMINAL_FONT_HEIGHT + 1u,
                (last - first) * window->cell_width, 1u, foreground);
        if ((attributes & ASTRA_TERMINAL_STRIKE) != 0u)
            graphics_library->fill(
                &window->surface.view, x,
                y + TERMINAL_FONT_HEIGHT / 2u,
                (last - first) * window->cell_width, 1u, foreground);
        first = last;
    }
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
                         const AstraTerminalCell *cells, uint32_t count)
{
    WindowTerminal *window = context;
    uint32_t x = TERMINAL_MARGIN_X + column * window->cell_width;
    uint32_t y = TERMINAL_MARGIN_Y + row * TERMINAL_LINE_HEIGHT;
    uint32_t width = count * window->cell_width;

    if (!window->dirty && !graphics_library->draw_list_view_init(
            &window->surface.view, window->surface.mapping,
            window->surface.view.byte_size, window->width, window->height))
        return 0;
    paint_cells(window, row, column, cells, count);
    window_damage(window, x, y, width, TERMINAL_LINE_HEIGHT);
    window->dirty = 1u;
    return 1;
}

static int window_scroll(void *context, uint32_t rows,
                         uint32_t preserved_rows)
{
    WindowTerminal *window = context;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t width = terminal_columns(window) * window->cell_width;
    uint32_t scroll_pixels = rows * TERMINAL_LINE_HEIGHT;
    uint32_t height = preserved_rows * TERMINAL_LINE_HEIGHT;
    AstraTextBox text_box = {
        &window->surface.view,
        TERMINAL_MARGIN_X,
        TERMINAL_MARGIN_Y,
        width,
        scroll_pixels + height,
    };

    if (preserved_rows == 0u ||
        (!window->dirty && !graphics_library->draw_list_view_init(
            &window->surface.view, window->surface.mapping,
            window->surface.view.byte_size, window->width, window->height)))
        return 0;
    if (window->cursor_row != UINT32_MAX)
        draw_cursor(window, window->cursor_row, window->cursor_column,
                    rgb565(theme.system_bar));
    if (!graphics_library->text_box_scroll(&text_box,
                                            (int32_t)scroll_pixels))
        return 0;
    window_damage(window, TERMINAL_MARGIN_X, TERMINAL_MARGIN_Y, width,
                  (rows + preserved_rows) * TERMINAL_LINE_HEIGHT);
    window->dirty = 1u;
    return 1;
}

static int window_resize_model(WindowTerminal *window, uint32_t columns,
                               uint32_t rows)
{
    AstraTerminalStatus terminal_status = astra_terminal_resize(
        window->terminal, columns, rows, NULL, 0u);
    AstraArea grown = ASTRA_AREA_INIT;
    size_t bytes;

    if (terminal_status == ASTRA_TERMINAL_OK) {
        console_stream_resize(columns, rows,
                              columns * window->cell_width,
                              rows * TERMINAL_LINE_HEIGHT);
        return 1;
    }
    if (terminal_status != ASTRA_TERMINAL_STORAGE_TOO_SMALL ||
        astra_terminal_storage_size(columns, rows, &bytes) !=
            ASTRA_TERMINAL_OK || bytes > ASTRA_AREA_SIZE_MAX ||
        astra_area_create((uint32_t)bytes,
                          ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                              ASTRA_RIGHT_MAP,
                          &grown) != ASTRA_OK ||
        astra_area_map(&grown,
                       ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE) !=
            ASTRA_OK) {
        if (grown.handle != ASTRA_INVALID_HANDLE)
            close_area(&grown);
        return 0;
    }
    terminal_status = astra_terminal_resize(
        window->terminal, columns, rows, grown.address, grown.size);
    if (terminal_status != ASTRA_TERMINAL_OK) {
        close_area(&grown);
        return 0;
    }
    close_area(&window->model_area);
    window->model_area = grown;
    console_stream_resize(columns, rows, columns * window->cell_width,
                          rows * TERMINAL_LINE_HEIGHT);
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
            const AstraTerminalCell *cells = terminal->cells +
                (size_t)row * terminal->capacity_columns;

            paint_cells(window, row, 0u, cells, terminal->columns);
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
    if (window->cursor_visible && terminal->cursor_visible)
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
                    !window_resize_model(window, terminal_columns(window),
                                         terminal_rows(window)))
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
    AstraArea title_icon = ASTRA_AREA_INIT;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *gui;
    const AstraStartupCapability *control;
    ConsoleShellBackend backend;
    uint32_t status;
    uint32_t title_icon_length = 0u;
    uint32_t terminal_capacity_columns = 0u;
    uint32_t terminal_capacity_rows = 0u;
    size_t terminal_storage_bytes = 0u;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    control = astra_startup_capability(startup,
                                       ASTRA_CAPABILITY_EVENT_CONTROL);
    if (bootstrap == NULL || gui == NULL || control == NULL)
        return ASTRA_STATUS_BAD_HANDLE;

    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status == ASTRA_STATUS_OK)
        status = load_graphics_kit();
    if (status == ASTRA_STATUS_OK)
        status = load_title_icon(&title_icon, &title_icon_length);

    (void)memset(&window_terminal, 0, sizeof(window_terminal));
    window_terminal.model_area = (AstraArea)ASTRA_AREA_INIT;
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
    if (status == ASTRA_STATUS_OK) {
        terminal_capacity_columns =
            (ASTRA_DISPLAY_WIDTH - TERMINAL_MARGIN_X * 2u) /
            window_terminal.cell_width;
        terminal_capacity_rows =
            (ASTRA_DISPLAY_HEIGHT - TERMINAL_MARGIN_Y * 2u) /
            TERMINAL_LINE_HEIGHT;
    }
    if (status == ASTRA_STATUS_OK &&
        (astra_terminal_storage_size(terminal_capacity_columns,
                                     terminal_capacity_rows,
                                     &terminal_storage_bytes) !=
             ASTRA_TERMINAL_OK ||
         terminal_storage_bytes > ASTRA_AREA_SIZE_MAX))
        status = TERMINAL_FAIL_STORAGE;
    if (status == ASTRA_STATUS_OK &&
        (astra_area_create((uint32_t)terminal_storage_bytes,
                           ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                               ASTRA_RIGHT_MAP,
                           &window_terminal.model_area) != ASTRA_OK ||
         astra_area_map(&window_terminal.model_area,
                        ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE) !=
             ASTRA_OK))
        status = TERMINAL_FAIL_STORAGE;
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
        info.title_icon_area = title_icon.handle;
        info.title_icon_length = title_icon_length;
        result = astra_window_create(gui->handle,
                                     window_terminal.surface.area,
                                     &info, &window_terminal.window);
        close_area(&title_icon);
        if (result != ASTRA_OK)
            status = TERMINAL_FAIL_WINDOW + (uint32_t)(-result);
        else
            window_terminal.live = 1u;
    }
    if (title_icon.handle != ASTRA_INVALID_HANDLE)
        close_area(&title_icon);

    (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) {
        if (window_terminal.model_area.handle != ASTRA_INVALID_HANDLE)
            close_area(&window_terminal.model_area);
        CloseLibrary(graphics_handle);
        CloseLibrary(font_handle);
        astra_process_filesystem_close(&process_filesystem);
        return (int)status;
    }
    backend.columns = terminal_columns(&window_terminal);
    backend.rows = terminal_rows(&window_terminal);
    backend.pixel_width = backend.columns * window_terminal.cell_width;
    backend.pixel_height = backend.rows * TERMINAL_LINE_HEIGHT;
    backend.terminal_capacity_columns = terminal_capacity_columns;
    backend.terminal_capacity_rows = terminal_capacity_rows;
    backend.terminal_storage = window_terminal.model_area.address;
    backend.terminal_storage_size = window_terminal.model_area.size;
    backend.render = window_render;
    backend.scroll = window_scroll;
    backend.context = &window_terminal;
    backend.present = window_present;
    backend.next_key = window_next_key;
    backend.wait_handle = astra_window_event_wait_handle(
        &window_terminal.window);
    backend.idle_poll_ns = TERMINAL_CURSOR_BLINK_NS;
    backend.process_filesystem = &process_filesystem;
    backend.startup = startup;
    console_shell_run_backend(&backend);
    if (window_terminal.live) {
        AstraResult close_result = astra_window_close(&window_terminal.window);

        (void)close_result;
    }
    (void)graphics_library->shared_surface_close(&window_terminal.surface);
    close_area(&window_terminal.model_area);
    CloseLibrary(graphics_handle);
    CloseLibrary(font_handle);
    astra_process_filesystem_close(&process_filesystem);
    return ASTRA_STATUS_OK;
}
