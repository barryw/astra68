#include <astraz/engine.h>
#include "screen_grid.h"
#include "transcript_flow.h"

#include <astra/control.h>
#include <astra/fonts.h>
#include <astra/graphics_kit.h>
#include <astra/interface_library.h>
#include <astra/menu.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/scroll.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <astra/window.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define WIDTH 800u
#define HEIGHT 520u
#define TRANSCRIPT_BYTES 65536u

enum { TOOLBAR = 1u, RESTART, UNDO, QUIT, BODY, STATUS, UPPER, SCROLL, CONTENT,
       TRANSCRIPT, PROMPT, PROMPT_MARK, INPUT,
       CONTROL_COUNT = INPUT };

static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraSharedSurface surface;
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraControl controls[CONTROL_COUNT];
static AstraScrollModel scroll = ASTRA_SCROLL_MODEL_INIT;
static AstraTextModel transcript_model = ASTRA_TEXT_MODEL_INIT;
static AstraTextModel status_model = ASTRA_TEXT_MODEL_INIT;
static AstraTextModel prompt_model = ASTRA_TEXT_MODEL_INIT;
static AstraTextModel input_model = ASTRA_TEXT_MODEL_INIT;
static char transcript[TRANSCRIPT_BYTES];
static char transcript_content[TRANSCRIPT_BYTES];
/* MC68040 statics need explicit four-byte alignment for TextModel metadata. */
static uint32_t transcript_metadata[TRANSCRIPT_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(4)));
static char input_content[4096];
static uint32_t input_metadata[1024] __attribute__((aligned(4)));
static uint32_t transcript_length;
static AstraZTranscriptFlow transcript_flow;
static uint32_t status_cells[80];
static uint32_t status_column;
static char status_text[321];
static char status_content[512];
static uint32_t status_metadata[256] __attribute__((aligned(4)));
static char prompt_content[32];
static uint32_t prompt_metadata[64] __attribute__((aligned(4)));
static uint8_t status_dirty;
static AstraZScreenGrid screen_grid;
static AstraGridViewModel upper_model = ASTRA_GRID_VIEW_MODEL_INIT;
static uint8_t upper_dirty;
static uint8_t prompt_visible;
static uint8_t transcript_dirty;
static uint8_t ui_ready;
static uint8_t running = 1u;
static uint8_t playing;
static uint8_t restart_requested;
static uint8_t story_ready;
static uint16_t pending_key;
static AstraHandle gui;
static const char *story_path;

#define CONTROL(id) (&controls[(id) - 1u])

static const AstraMenuItem menu_items[] = {
    {"Story", 5u, 0u}, {"Help", 4u, 0u},
    {"Restart", 7u, 1u << ASTRA_MENU_PARENT_SHIFT},
    {"Undo", 4u, 1u << ASTRA_MENU_PARENT_SHIFT},
    {"Quit", 4u, 1u << ASTRA_MENU_PARENT_SHIFT},
    {"About AstraZ", 12u, 2u << ASTRA_MENU_PARENT_SHIFT},
};

static AstraResult refresh_status(void)
{
    if (!status_dirty || !ui_ready) return ASTRA_OK;
    uint32_t last = 80u;
    while (last != 0u && (status_cells[last - 1u] == 0u ||
                          status_cells[last - 1u] == ' ')) --last;
    uint32_t length = 0u;
    for (uint32_t at = 0u; at < last; ++at) {
        uint32_t scalar = status_cells[at] ? status_cells[at] : ' ';
        uint32_t bytes = astra_utf8_encode(scalar, status_text + length);
        if (bytes == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
        length += bytes;
    }
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    info.text = status_text;
    info.text_bytes = length;
    info.content_arena = status_content;
    info.content_arena_bytes = sizeof(status_content);
    info.metadata_arena = status_metadata;
    info.metadata_arena_bytes = sizeof(status_metadata);
    astra_text_model_dispose(&status_model);
    status_model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    if (astra_text_model_init(&status_model, &info) != ASTRA_OK ||
        astra_interface_text_view_refresh(&ui, CONTROL(STATUS)) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    status_dirty = 0u;
    return ASTRA_OK;
}

static void transcript_append(const char *bytes, uint32_t length)
{
    if (length == 0u || length >= TRANSCRIPT_BYTES) return;
    if (transcript_length + length >= TRANSCRIPT_BYTES) {
        uint32_t cut = transcript_length / 2u;
        while (cut < transcript_length && transcript[cut] != '\n') ++cut;
        if (cut < transcript_length) ++cut;
        else {
            cut = transcript_length / 2u;
            while (cut < transcript_length &&
                   ((uint8_t)transcript[cut] & 0xc0u) == 0x80u) ++cut;
        }
        memmove(transcript, transcript + cut, transcript_length - cut);
        transcript_length -= cut;
    }
    memcpy(transcript + transcript_length, bytes, length);
    transcript_length += length;
    transcript_dirty = 1u;
}

static AstraResult present(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraWindowFrame full = {0u, 0u, WIDTH, HEIGHT};
    if (upper_dirty && ui_ready) {
        upper_model.rows = screen_grid.rows;
        if (astra_interface_ui_set_state(&ui, CONTROL(UPPER),
                screen_grid.rows ? 0u : ASTRA_CONTROL_COLLAPSED) != ASTRA_OK ||
            astra_interface_grid_view_refresh(&ui, CONTROL(UPPER)) != ASTRA_OK)
            return ASTRA_ERROR_IO;
        upper_dirty = 0u;
    }
    if (refresh_status() != ASTRA_OK ||
        !astra_draw_list_view_init(&surface.view, surface.view.pixels,
            surface.view.byte_size, WIDTH, HEIGHT)) return ASTRA_ERROR_IO;
    astra_surface_clear(&surface.view, astra_surface_rgb565(
        theme.client.red, theme.client.green, theme.client.blue));
    if (astra_interface_ui_render(&ui, &surface.view) != ASTRA_OK ||
        astra_window_present_region(&window, &full) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    astra_interface_ui_damage_clear(&ui);
    return ASTRA_OK;
}

static AstraResult flush_transcript(void)
{
    if (!ui_ready) return ASTRA_OK;
    if (!transcript_dirty)
        return upper_dirty || status_dirty ? present() : ASTRA_OK;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    astra_text_model_dispose(&transcript_model);
    transcript_model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    info.text = transcript;
    info.text_bytes = transcript_length -
        (prompt_visible && transcript_length != 0u &&
         transcript[transcript_length - 1u] == '\n' ? 1u : 0u);
    info.content_arena = transcript_content;
    info.content_arena_bytes = sizeof(transcript_content);
    info.metadata_arena = transcript_metadata;
    info.metadata_arena_bytes = sizeof(transcript_metadata);
    if (astra_text_model_init(&transcript_model, &info) != ASTRA_OK ||
        astra_interface_text_view_refresh(&ui, CONTROL(TRANSCRIPT)) != ASTRA_OK ||
        astra_scroll_get_state(&scroll, &state) != ASTRA_OK ||
        astra_scroll_set_offset(&scroll, 0u, state.maximum_y) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    transcript_dirty = 0u;
    return present();
}

static AstraResult build_ui(void)
{
    static const AstraToolbarAction actions[] = {
        {"Restart", 7u, ASTRA_CONTROL_DISABLED},
        {"Undo", 4u, ASTRA_CONTROL_DISABLED},
        {"Quit", 4u, 0u}
    };
    static const uint32_t icons[] = {
        ASTRA_TOOLBAR_ICON_RESTART,
        ASTRA_TOOLBAR_ICON_UNDO,
        ASTRA_TOOLBAR_ICON_CLOSE
    };
    AstraToolbarInfo toolbar = ASTRA_TOOLBAR_INFO_INIT;
    AstraContainerInfo body = ASTRA_CONTAINER_INFO_INIT;
    AstraContainerInfo content = ASTRA_CONTAINER_INFO_INIT;
    AstraContainerInfo prompt = ASTRA_CONTAINER_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraTextViewInfo text = ASTRA_TEXT_VIEW_INFO_INIT;
    AstraGridViewInfo upper = ASTRA_GRID_VIEW_INFO_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraTextModelInfo transcript_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelInfo status_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelInfo prompt_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelInfo input_info = ASTRA_TEXT_MODEL_INFO_INIT;
    for (uint32_t at = 0u; at < CONTROL_COUNT; ++at)
        controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
    scroll_info.line_height = 18u;
    transcript_info.content_arena = transcript_content;
    transcript_info.content_arena_bytes = sizeof(transcript_content);
    transcript_info.metadata_arena = transcript_metadata;
    transcript_info.metadata_arena_bytes = sizeof(transcript_metadata);
    status_info.content_arena = status_content;
    status_info.content_arena_bytes = sizeof(status_content);
    status_info.metadata_arena = status_metadata;
    status_info.metadata_arena_bytes = sizeof(status_metadata);
    prompt_info.text = "> ";
    prompt_info.text_bytes = 2u;
    prompt_info.content_arena = prompt_content;
    prompt_info.content_arena_bytes = sizeof(prompt_content);
    prompt_info.metadata_arena = prompt_metadata;
    prompt_info.metadata_arena_bytes = sizeof(prompt_metadata);
    input_info.content_arena = input_content;
    input_info.content_arena_bytes = sizeof(input_content);
    input_info.metadata_arena = input_metadata;
    input_info.metadata_arena_bytes = sizeof(input_metadata);
    if (astra_scroll_init(&scroll, &scroll_info) != ASTRA_OK ||
        astra_text_model_init(&transcript_model, &transcript_info) != ASTRA_OK ||
        astra_text_model_init(&status_model, &status_info) != ASTRA_OK ||
        astra_text_model_init(&prompt_model, &prompt_info) != ASTRA_OK ||
        astra_text_model_init(&input_model, &input_info) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    toolbar.first_id = TOOLBAR;
    toolbar.actions = actions;
    toolbar.action_count = 3u;
    if (astra_interface_toolbar_init(controls, &toolbar) != ASTRA_OK ||
        astra_interface_toolbar_set_icons(controls, icons, 3u) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    body.id = BODY;
    body.layout.direction = ASTRA_FLEX_COLUMN;
    body.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    body.layout.padding_left = body.layout.padding_right = 12u;
    body.layout.padding_top = body.layout.padding_bottom = 12u;
    body.layout.main_gap = 8u;
    if (astra_interface_container_init(CONTROL(BODY), &body) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item.grow = 1u;
    if (astra_interface_control_set_flex(CONTROL(BODY), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    text.id = STATUS;
    text.model = &status_model;
    text.state = ASTRA_CONTROL_COLLAPSED;
    if (astra_interface_text_view_init(CONTROL(STATUS), &text) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = BODY;
    if (astra_interface_control_set_flex(CONTROL(STATUS), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    upper_model.cells = screen_grid.cells;
    upper_model.columns = ASTRAZ_GRID_COLUMNS;
    upper_model.capacity_cells = ASTRAZ_GRID_ROWS * ASTRAZ_GRID_COLUMNS;
    upper.id = UPPER;
    upper.model = &upper_model;
    upper.state = ASTRA_CONTROL_COLLAPSED;
    if (astra_interface_grid_view_init(CONTROL(UPPER), &upper) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = BODY;
    if (astra_interface_control_set_flex(CONTROL(UPPER), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    view.id = SCROLL;
    view.model = &scroll;
    view.preferred_width = WIDTH - 24u;
    view.preferred_height = HEIGHT - 110u;
    if (astra_interface_scroll_view_init(CONTROL(SCROLL), &view) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item.grow = 1u;
    item.minimum_height = 200u;
    item.parent_id = BODY;
    if (astra_interface_control_set_flex(CONTROL(SCROLL), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    content.id = CONTENT;
    content.layout.direction = ASTRA_FLEX_COLUMN;
    content.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (astra_interface_container_init(CONTROL(CONTENT), &content) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SCROLL;
    item.minimum_width = WIDTH - 24u;
    if (astra_interface_control_set_flex(CONTROL(CONTENT), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    text.id = TRANSCRIPT;
    text.model = &transcript_model;
    text.state = 0u;
    if (astra_interface_text_view_init(CONTROL(TRANSCRIPT), &text) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = CONTENT;
    item.minimum_width = WIDTH - 24u;
    if (astra_interface_control_set_flex(CONTROL(TRANSCRIPT), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    prompt.id = PROMPT;
    prompt.state = ASTRA_CONTROL_COLLAPSED;
    prompt.layout.direction = ASTRA_FLEX_ROW;
    prompt.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    if (astra_interface_container_init(CONTROL(PROMPT), &prompt) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = CONTENT;
    if (astra_interface_control_set_flex(CONTROL(PROMPT), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    text.id = PROMPT_MARK;
    text.model = &prompt_model;
    if (astra_interface_text_view_init(CONTROL(PROMPT_MARK), &text) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = PROMPT;
    item.shrink = 0u;
    if (astra_interface_control_set_flex(CONTROL(PROMPT_MARK), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    field.id = INPUT;
    field.model = &input_model;
    field.preferred_columns = 70u;
    field.flags = ASTRA_FIELD_INLINE;
    if (astra_interface_field_init(CONTROL(INPUT), &field) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = PROMPT;
    item.grow = 1u;
    if (astra_interface_control_set_flex(CONTROL(INPUT), &item) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    AstraResult result = astra_interface_ui_init(&ui, controls,
        CONTROL_COUNT, WIDTH, HEIGHT);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("AstraZ UI init", (uint32_t)result);
        return result;
    }
    result = astra_interface_ui_layout(&ui, &layout);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("AstraZ UI layout", (uint32_t)result);
        return result;
    }
    result = astra_interface_text_view_set_inverted(
        &ui, CONTROL(STATUS), 1u);
    if (result != ASTRA_OK) return result;
    ui_ready = 1u;
    return ASTRA_OK;
}

static void action(uint32_t id)
{
    if (id == RESTART) {
        if (playing) pending_key = ASTRAZ_HOTKEY_RESTART;
        else if (story_ready) restart_requested = 1u;
    }
    else if (id == UNDO && playing) pending_key = ASTRAZ_HOTKEY_UNDO;
    else if (id == QUIT) running = 0u;
    else if (id == 6u) {
        AstraAlertInfo alert = ASTRA_ALERT_INFO_INIT;
        alert.kind = ASTRA_ALERT_INFORMATION;
        alert.title = "About AstraZ";
        alert.title_length = 12u;
        alert.message = "AstraZ plays Z-machine stories with the Frotz engine.";
        alert.message_length = (uint16_t)strlen(alert.message);
        alert.button = "Close";
        alert.button_length = 5u;
        (void)astra_interface_show_alert(gui, &alert);
    }
}

static int next_event(AstraWindowEvent *event, AstraUIAction *ui_action,
                      uint64_t deadline)
{
    uint32_t handle = astra_window_event_wait_handle(&window);
    uint32_t selected = 0u;
    for (;;) {
        uint32_t status = astra_wait_multiple(&handle, 1u, deadline,
                                               &selected, NULL);
        if (status == ASTRA_SYSCALL_TIMED_OUT) return 0;
        if (status != ASTRA_SYSCALL_OK) return -1;
        AstraResult result = astra_window_event_try(&window, event);
        if (result == ASTRA_ERROR_WOULD_BLOCK) continue;
        if (result != ASTRA_OK) return -1;
        break;
    }
    if (event->type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) running = 0u;
    if (event->type == ASTRA_WINDOW_EVENT_FONT_CHANGED &&
        astra_fonts_use_system(gui, event) != ASTRA_OK) return -1;
    if (event->type == ASTRA_WINDOW_EVENT_MENU_RESULT &&
        event->data.menu_result.request == ASTRA_WINDOW_MENU_BAR_REQUEST &&
        event->data.menu_result.selection >= 3u &&
        event->data.menu_result.selection <= 6u)
        action(event->data.menu_result.selection == 6u ? 6u :
               event->data.menu_result.selection - 1u);
    *ui_action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    if (astra_interface_ui_handle_event(&ui, event, ui_action) != ASTRA_OK)
        return -1;
    if (prompt_visible &&
        ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u ||
         event->type == ASTRA_WINDOW_EVENT_STATE_RESET ||
         (event->type == ASTRA_WINDOW_EVENT_STATE &&
          (event->data.state.flags & ASTRA_WINDOW_ACTIVE) != 0u)) &&
        astra_interface_ui_focus_control(&ui, INPUT) != ASTRA_OK)
        return -1;
    if (ui_action->type == ASTRA_UI_ACTION_ACTIVATE &&
        ui_action->control_id >= RESTART && ui_action->control_id <= QUIT)
        action(ui_action->control_id);
    AstraControlFrame damage;
    if (astra_interface_ui_damage(&ui, &damage) == ASTRA_OK &&
        present() != ASTRA_OK) return -1;
    return 1;
}

static int host_event(void *unused, const AstraZEvent *event)
{
    (void)unused;
    if (astraz_screen_grid_event(&screen_grid, event)) {
        upper_dirty = 1u;
        if (event->kind == ASTRAZ_EVENT_CHARACTER &&
            event->data.text.window >= 1 && event->data.text.window <= 6)
            return 0;
        if (event->kind == ASTRAZ_EVENT_SCROLL) return 0;
    }
    if (event->kind == ASTRAZ_EVENT_SCREEN_BEGIN) {
        for (uint32_t at = 0u; at < 80u; ++at) status_cells[at] = ' ';
        status_column = 1u;
        status_dirty = 1u;
        if (astra_interface_ui_set_state(&ui, CONTROL(STATUS),
            event->data.screen.version <= 3u ? 0u :
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK) return -1;
    }
    if (event->kind == ASTRAZ_EVENT_CURSOR &&
        event->data.cursor.window == 7) {
        status_column = event->data.cursor.column > 0 ?
            (uint32_t)event->data.cursor.column : 1u;
        if (status_column == 1u) {
            for (uint32_t at = 0u; at < 80u; ++at) status_cells[at] = ' ';
            status_dirty = 1u;
        }
        return 0;
    }
    if (event->kind == ASTRAZ_EVENT_CHARACTER &&
        event->data.text.window == 7) {
        uint32_t scalar = event->data.text.character;
        if (status_column >= 1u && status_column <= 80u && scalar >= 32u) {
            status_cells[status_column - 1u] = scalar;
            status_dirty = 1u;
        }
        ++status_column;
        return 0;
    }
    if (event->kind == ASTRAZ_EVENT_ERASE &&
        event->data.erase.window == 7) {
        uint32_t first = event->data.erase.left > 1 ?
            (uint32_t)event->data.erase.left - 1u : 0u;
        uint32_t last = event->data.erase.right <= 0 ? 0u :
            event->data.erase.right < 80 ?
                (uint32_t)event->data.erase.right : 80u;
        for (uint32_t at = first; at < last; ++at) status_cells[at] = ' ';
        status_dirty = 1u;
        return 0;
    }
    if (event->kind == ASTRAZ_EVENT_ERASE &&
        event->data.erase.window == -2) {
        transcript_length = 0u;
        transcript_dirty = 1u;
    }
    int line_break = astraz_transcript_flow_event(&transcript_flow, event);
    if (event->kind == ASTRAZ_EVENT_CHARACTER) {
        char bytes[4];
        uint32_t scalar = event->data.text.character;
        if (scalar == 0u || (scalar < 32u && scalar != '\n' &&
            scalar != '\r' && scalar != '\t')) return 0;
        uint32_t length = astra_utf8_encode(
            scalar == '\r' ? '\n' : scalar, bytes);
        if (line_break) transcript_append("\n", 1u);
        if (length != 0u) transcript_append(bytes, length);
        if ((scalar == '\n' || scalar == '\r') &&
            astraz_transcript_flow_flush_due(&transcript_flow,
                astra_clock_monotonic()))
            return flush_transcript() == ASTRA_OK ? 0 : -1;
    } else if (event->kind == ASTRAZ_EVENT_MORE) {
        transcript_append("\n[More — press a key]\n", 24u);
        return flush_transcript() == ASTRA_OK ? 0 : -1;
    } else if (event->kind == ASTRAZ_EVENT_WARNING) {
        transcript_append("\n[Warning] ", 11u);
        transcript_append(event->data.warning.text,
                          (uint32_t)strlen(event->data.warning.text));
        transcript_append("\n", 1u);
    } else if (event->kind == ASTRAZ_EVENT_SCREEN_END)
        return flush_transcript() == ASTRA_OK ? 0 : -1;
    else if (event->kind == ASTRAZ_EVENT_REPAINT)
        return 1;
    return 0;
}

static int host_tick(void *unused)
{
    (void)unused;
    return running != 0u;
}

static uint16_t host_read_key(void *unused, uint32_t timeout, int cursor)
{
    (void)unused; (void)cursor;
    uint64_t deadline = timeout == 0u ? ASTRA_DEADLINE_FOREVER :
        astra_clock_monotonic() + (uint64_t)timeout * UINT64_C(100000000);
    if (flush_transcript() != ASTRA_OK) { running = 0u; return 0u; }
    while (running) {
        AstraWindowEvent event = {0};
        AstraUIAction ui_action = ASTRA_UI_ACTION_INIT;
        int received = next_event(&event, &ui_action, deadline);
        if (received == 0) return ASTRAZ_KEY_TIMEOUT;
        if (received < 0) { running = 0u; break; }
        if (pending_key != 0u) {
            uint16_t key = pending_key;
            pending_key = 0u;
            return key;
        }
        if (event.type == ASTRA_WINDOW_EVENT_TEXT &&
            event.data.text.codepoint >= 32u &&
            event.data.text.codepoint <= 0xffffu)
            return (uint16_t)event.data.text.codepoint;
        if (event.type == ASTRA_WINDOW_EVENT_KEY &&
            (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
            switch (event.data.key.usage) {
            case 0x28u: return ASTRAZ_KEY_RETURN;
            case 0x29u: return ASTRAZ_KEY_ESCAPE;
            case 0x2au: return ASTRAZ_KEY_BACKSPACE;
            case 0x4fu: return ASTRAZ_KEY_RIGHT;
            case 0x50u: return ASTRAZ_KEY_LEFT;
            case 0x51u: return ASTRAZ_KEY_DOWN;
            case 0x52u: return ASTRAZ_KEY_UP;
            default: break;
            }
        }
    }
    return 0u;
}

static uint16_t host_read_line(void *unused, uint16_t *buffer, uint32_t max,
                               uint32_t timeout, uint32_t width, int continued)
{
    (void)unused; (void)width; (void)continued;
    uint64_t deadline = timeout == 0u ? ASTRA_DEADLINE_FOREVER :
        astra_clock_monotonic() + (uint64_t)timeout * UINT64_C(100000000);
    if (!prompt_visible) {
        if (transcript_length != 0u &&
            transcript[transcript_length - 1u] == '>') {
            --transcript_length;
            transcript_dirty = 1u;
        }
        if (transcript_length != 0u &&
            transcript[transcript_length - 1u] != '\n')
            transcript_append("\n", 1u);
        prompt_visible = 1u;
        transcript_dirty = 1u;
    }
    if (astra_interface_ui_set_state(&ui, CONTROL(PROMPT), 0u) != ASTRA_OK ||
        flush_transcript() != ASTRA_OK ||
        astra_interface_ui_focus_control(&ui, INPUT) != ASTRA_OK ||
        present() != ASTRA_OK) {
        running = 0u;
        return 0u;
    }
    while (running) {
        AstraWindowEvent event = {0};
        AstraUIAction ui_action = ASTRA_UI_ACTION_INIT;
        int received = next_event(&event, &ui_action, deadline);
        if (received == 0) return ASTRAZ_KEY_TIMEOUT;
        if (received < 0) { running = 0u; break; }
        if (pending_key != 0u) {
            uint16_t key = pending_key;
            pending_key = 0u;
            return key;
        }
        if (ui_action.type == ASTRA_UI_ACTION_ACTIVATE &&
            ui_action.control_id == INPUT) {
            AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
            char text[1024];
            uint32_t length = 0u;
            if (astra_text_model_get_state(&input_model, &state) != ASTRA_OK ||
                state.text_bytes >= sizeof(text) ||
                astra_text_model_copy(&input_model, 0u, state.text_bytes,
                    text, sizeof(text), &length) != ASTRA_OK) {
                running = 0u;
                break;
            }
            if (!astraz_input_copy(text, length, buffer, max)) {
                AstraAlertInfo alert = ASTRA_ALERT_INFO_INIT;
                alert.kind = ASTRA_ALERT_WARNING;
                alert.title = "Input too long";
                alert.title_length = 14u;
                alert.message = "This story accepts a shorter command.";
                alert.message_length = (uint16_t)strlen(alert.message);
                alert.button = "Edit command";
                alert.button_length = 12u;
                (void)astra_interface_show_alert(gui, &alert);
                (void)astra_interface_ui_focus_control(&ui, INPUT);
                continue;
            }
            prompt_visible = 0u;
            if (astra_interface_ui_set_state(&ui, CONTROL(PROMPT),
                ASTRA_CONTROL_COLLAPSED) != ASTRA_OK) {
                running = 0u;
                break;
            }
            transcript_append("> ", 2u);
            transcript_append(text, length);
            transcript_append("\n", 1u);
            astra_text_model_dispose(&input_model);
            input_model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
            AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
            info.content_arena = input_content;
            info.content_arena_bytes = sizeof(input_content);
            info.metadata_arena = input_metadata;
            info.metadata_arena_bytes = sizeof(input_metadata);
            if (astra_text_model_init(&input_model, &info) != ASTRA_OK ||
                astra_interface_field_refresh(&ui, CONTROL(INPUT)) != ASTRA_OK ||
                flush_transcript() != ASTRA_OK)
                running = 0u;
            return ASTRAZ_KEY_RETURN;
        }
    }
    return 0u;
}

static int host_choose_file(void *unused, int purpose, const char *suggested,
                            char *path, size_t capacity)
{
    (void)unused; (void)suggested;
    const char *base = strrchr(story_path, '/');
    char name[64];
    uint32_t hash = UINT32_C(2166136261);
    size_t length = 0u;
    const char *suffix = purpose == ASTRAZ_FILE_SAVE ||
        purpose == ASTRAZ_FILE_RESTORE ? ".qzl" :
        purpose == ASTRAZ_FILE_TRANSCRIPT ? ".txt" : ".dat";
    base = base != NULL ? base + 1u : story_path;
    for (const char *at = story_path; *at; ++at)
        hash = (hash ^ (uint8_t)*at) * UINT32_C(16777619);
    for (const char *at = base; *at && length + 1u < sizeof(name); ++at)
        if ((*at >= 'A' && *at <= 'Z') || (*at >= 'a' && *at <= 'z') ||
            (*at >= '0' && *at <= '9') || *at == '-') name[length++] = *at;
    if (length == 0u) name[length++] = 'z';
    name[length] = '\0';
    if (mkdir("/home/AstraZ", 0700) != 0 && errno != EEXIST) return 0;
    int needed = snprintf(path, capacity, "/home/AstraZ/%s-%08x%s",
                          name, (unsigned)hash, suffix);
    return needed > 0 && (size_t)needed < capacity;
}

static int play_story(const AstraZStory *story)
{
    AstraZHost host = {.width = story->version == 6u ? 800u : 80u,
        .height = story->version == 6u ? 480u : 24u,
        .cell_width = story->version == 6u ? 10u : 1u,
        .cell_height = story->version == 6u ? 20u : 1u,
        .event = host_event, .read_key = host_read_key,
        .read_line = host_read_line, .choose_file = host_choose_file,
        .tick = host_tick};
    char error[256] = {0};
    AstraZRun run = {.story_path = story_path, .host = &host,
        .error = error, .error_capacity = sizeof(error)};
    playing = 1u;
    if (astra_interface_ui_set_state(&ui, CONTROL(RESTART), 0u) != ASTRA_OK ||
        astra_interface_ui_set_state(&ui, CONTROL(UNDO), 0u) != ASTRA_OK ||
        present() != ASTRA_OK) {
        playing = 0u;
        return ASTRAZ_IO;
    }
    int result = astraz_engine_run(&run);
    playing = 0u;
    (void)astra_interface_ui_set_state(&ui, CONTROL(UNDO), ASTRA_CONTROL_DISABLED);
    prompt_visible = 0u;
    (void)astra_interface_ui_set_state(&ui, CONTROL(PROMPT), ASTRA_CONTROL_COLLAPSED);
    if (running) {
        const char *message = result == ASTRAZ_OK ? "\n[Story ended]\n" :
            error[0] ? error : "Story could not run.";
        transcript_append(message, (uint32_t)strlen(message));
        if (result != ASTRAZ_OK) transcript_append("\n", 1u);
    }
    return result;
}

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *gui_cap;
    const AstraStartupCapability *ready;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    uint32_t status = ASTRA_STATUS_OK;
    if (!astra_startup_validate(startup) || argc > 2) return 2;
    gui_cap = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    ready = astra_startup_capability(startup, "SERVICE_READY");
    if (gui_cap == NULL) return 2;
    gui = gui_cap->handle;
    story_path = argc == 2 ? argv[1] : NULL;
    AstraResult setup = astra_fonts_use_system(gui, NULL);
    if (setup != ASTRA_OK) {
        (void)astra_log_failure("AstraZ system font", (uint32_t)setup);
        return 3;
    }
    setup = build_ui();
    if (setup != ASTRA_OK) {
        (void)astra_log_failure("AstraZ build UI", (uint32_t)setup);
        return 3;
    }
    if (astra_shared_draw_list_create(&surface, WIDTH, HEIGHT) !=
        ASTRA_SYSCALL_OK) {
        (void)astra_log("AstraZ draw list allocation failed");
        return 3;
    }
    info.x = 240u; info.y = 100u;
    info.width = WIDTH; info.height = HEIGHT;
    info.flags = 0u;
    info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    info.type = ASTRA_WINDOW_STANDARD;
    info.title = "AstraZ"; info.title_length = 6u;
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
        ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
        ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT;
    if (astra_window_create(gui, surface.area, &info, &window) != ASTRA_OK ||
        astra_window_set_application_name(&window, "AstraZ", 6u) != ASTRA_OK ||
        astra_window_menu_bar_set(&window, menu_items,
            sizeof(menu_items) / sizeof(menu_items[0])) != ASTRA_OK ||
        astra_window_activate(&window) != ASTRA_OK || present() != ASTRA_OK)
        status = ASTRA_STATUS_IO;
    if (ready != NULL) {
        (void)astra_service_ready(ready->handle, status, NULL, 0u);
        (void)astra_close(ready->handle);
    }
    AstraZStory story = {0};
    int playable = status == ASTRA_STATUS_OK && story_path != NULL &&
        story_path[0] == '/' &&
        astraz_engine_probe(story_path, &story) == ASTRAZ_OK &&
        story.vm == ASTRAZ_VM_Z;
    story_ready = playable != 0;
    if (story_ready)
        (void)astra_interface_ui_set_state(&ui, CONTROL(RESTART), 0u);
    if (status == ASTRA_STATUS_OK && !playable) {
        const char *message = story.vm == ASTRAZ_VM_GLULX ?
            "Glulx execution is not installed yet.\n" :
            story_path == NULL ? "Open a Z-machine story in Atlas to play it.\n" :
            "This file is not a supported Z-machine story.\n";
        transcript_append(message, (uint32_t)strlen(message));
    }
    do {
        if (status == ASTRA_STATUS_OK && playable) {
            (void)play_story(&story);
            if (running && flush_transcript() != ASTRA_OK)
                status = ASTRA_STATUS_IO;
        } else if (status == ASTRA_STATUS_OK && flush_transcript() != ASTRA_OK)
            status = ASTRA_STATUS_IO;
        while (status == ASTRA_STATUS_OK && running && !restart_requested) {
            AstraWindowEvent event = {0};
            AstraUIAction ui_action = ASTRA_UI_ACTION_INIT;
            if (next_event(&event, &ui_action, ASTRA_DEADLINE_FOREVER) <= 0)
                status = ASTRA_STATUS_IO;
        }
        restart_requested = 0u;
    } while (status == ASTRA_STATUS_OK && running && playable);
    if (window._private_control != ASTRA_INVALID_HANDLE &&
        astra_window_close(&window) != ASTRA_OK && status == ASTRA_STATUS_OK)
        status = ASTRA_STATUS_IO;
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_text_model_dispose(&input_model);
    astra_text_model_dispose(&prompt_model);
    astra_text_model_dispose(&status_model);
    astra_text_model_dispose(&transcript_model);
    return status == ASTRA_STATUS_OK ? 0 : 1;
}
