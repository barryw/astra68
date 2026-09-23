#include <astra/control.h>
#include <astra/clipboard_service.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#define GALLERY_WIDTH 1120u
#define GALLERY_HEIGHT 800u
#define GALLERY_BENCHMARK_CONTROL_MAX 256u
#define GALLERY_UNDO_BENCHMARK_OPERATIONS 10000u
#define GALLERY_UNDO_ARENA_BYTES (1024u * 1024u)
#define GALLERY_TEXT_BENCHMARK_OPERATIONS 4096u
#define GALLERY_SEGMENTED_BENCHMARK_OPERATIONS 10000u
#define GALLERY_TAB_BENCHMARK_OPERATIONS 10000u
#define GALLERY_STEPPER_BENCHMARK_OPERATIONS 10000u
#define GALLERY_DIAL_BENCHMARK_OPERATIONS 10000u
#define GALLERY_DISCLOSURE_BENCHMARK_OPERATIONS 10000u
#define GALLERY_SPLITTER_BENCHMARK_OPERATIONS 10000u

enum {
    GALLERY_TABS = 1u,
    GALLERY_PAGE_INPUT,
    GALLERY_PAGE_CHOICE,
    GALLERY_PAGE_VALUE,
    GALLERY_PAGE_PROGRESS,
    GALLERY_PAGE_TEXT,
    GALLERY_PAGE_LAYOUT,
    GALLERY_TEXT_LABEL,
    GALLERY_LAYOUT_LABEL,
    GALLERY_LABEL,
    GALLERY_APPLY,
    GALLERY_REVERT,
    GALLERY_HOVER,
    GALLERY_PRESSED,
    GALLERY_FOCUSED,
    GALLERY_DISABLED,
    GALLERY_SELECTED,
    GALLERY_ERROR,
    GALLERY_WARNING,
    GALLERY_DESTRUCTIVE,
    GALLERY_HELP,
    GALLERY_TOGGLE_LABEL,
    GALLERY_CHECKBOX,
    GALLERY_RADIO_565,
    GALLERY_RADIO_8888,
    GALLERY_SWITCH,
    GALLERY_TOGGLE_DISABLED,
    GALLERY_SLIDER_LABEL,
    GALLERY_SLIDER,
    GALLERY_SLIDER_VALUE,
    GALLERY_DIAL_LABEL,
    GALLERY_DIAL,
    GALLERY_DIAL_VALUE,
    GALLERY_PROGRESS_LABEL,
    GALLERY_PROGRESS,
    GALLERY_PROGRESS_VALUE,
    GALLERY_PROGRESS_UNKNOWN,
    GALLERY_PROGRESS_UNKNOWN_VALUE,
    GALLERY_FIELD_LABEL,
    GALLERY_FIELD,
    GALLERY_FIELD_ERROR,
    GALLERY_FIELD_ERROR_TEXT,
    GALLERY_FIELD_READ_ONLY,
    GALLERY_SEGMENTED_LABEL,
    GALLERY_SEGMENTED,
    GALLERY_STEPPER_LABEL,
    GALLERY_STEPPER,
    GALLERY_SCROLL_VIEW,
    GALLERY_SCROLL_CONTENT,
    GALLERY_SCROLL_LABEL,
    GALLERY_SCROLLBAR,
    GALLERY_SPLIT_ROW,
    GALLERY_SPLIT_LEFT,
    GALLERY_SPLIT_LEFT_LABEL,
    GALLERY_SPLITTER,
    GALLERY_SPLIT_RIGHT,
    GALLERY_SPLIT_RIGHT_LABEL,
    GALLERY_DISCLOSURE,
    GALLERY_DISCLOSURE_BODY,
    GALLERY_DISCLOSURE_OPTION_A,
    GALLERY_DISCLOSURE_OPTION_B,
    GALLERY_CONTROL_COUNT = GALLERY_DISCLOSURE_OPTION_B
};

enum {
    GALLERY_FAIL_LIBRARY = ASTRA_STATUS_PROGRAM_FIRST,
    GALLERY_FAIL_CONTROL,
    GALLERY_FAIL_SURFACE,
    GALLERY_FAIL_WINDOW,
    GALLERY_FAIL_RENDER
};

ASTRA_PROGRAM("interface-gallery", 0, 2, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraSharedSurface surface;
static AstraWindow window = ASTRA_WINDOW_INIT;
static AstraControl controls[GALLERY_CONTROL_COUNT];
static AstraControl benchmark_controls[GALLERY_BENCHMARK_CONTROL_MAX];
static _Alignas(4) uint8_t undo_benchmark_arena[GALLERY_UNDO_ARENA_BYTES];
static uint8_t text_benchmark_content[16384];
static _Alignas(4) uint8_t text_benchmark_metadata[131072];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static char slider_value_text[5] = "62%";
static char dial_value_text[22] = "0";
static uint8_t field_content[4096];
static _Alignas(4) uint8_t field_metadata[8192];
static AstraTextModel field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t error_field_content[4096];
static _Alignas(4) uint8_t error_field_metadata[8192];
static AstraTextModel error_field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t read_only_field_content[4096];
static _Alignas(4) uint8_t read_only_field_metadata[8192];
static AstraTextModel read_only_field_model = ASTRA_TEXT_MODEL_INIT;
static AstraScrollModel gallery_scroll_model = ASTRA_SCROLL_MODEL_INIT;
static char clipboard_scratch[4096];
static uint32_t active_page = GALLERY_PAGE_INPUT;
static const AstraChoiceItem gallery_tabs[] = {
    {"Input", sizeof("Input") - 1u, {0u, 0u}},
    {"Choice", sizeof("Choice") - 1u, {0u, 0u}},
    {"Value", sizeof("Value") - 1u, {0u, 0u}},
    {"Progress", sizeof("Progress") - 1u, {0u, 0u}},
    {"Text", sizeof("Text") - 1u, {0u, 0u}},
    {"Layout", sizeof("Layout") - 1u, {0u, 0u}}
};
static const AstraChoiceItem gallery_choices[] = {
    {"Windows", sizeof("Windows") - 1u, {0u, 0u}},
    {"Scenes", sizeof("Scenes") - 1u, {0u, 0u}},
    {"Ports", sizeof("Ports") - 1u, {0u, 0u}}
};

#define GALLERY_CONTROL(id) (&controls[(id) - 1u])

static uint16_t rgb565(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static uint32_t append(char *out, uint32_t at, const char *text)
{
    while (*text != '\0') out[at++] = *text++;
    return at;
}

static uint32_t append_hex32(char *out, uint32_t at, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4)
        out[at++] = digits[(value >> shift) & 0xfu];
    return at;
}

static uint32_t append_hex64(char *out, uint32_t at, uint64_t value)
{
    at = append_hex32(out, at, (uint32_t)(value >> 32));
    return append_hex32(out, at, (uint32_t)value);
}

static void report_layout(uint32_t count, uint32_t iterations,
                          uint64_t elapsed)
{
    char line[112];
    uint32_t at = 0u;

    at = append(line, at, "INTERFACE LAYOUT controls=");
    at = append_hex32(line, at, count);
    at = append(line, at, " iterations=");
    at = append_hex32(line, at, iterations);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_undo(uint64_t record_elapsed, uint64_t undo_elapsed,
                        uint64_t redo_elapsed, uint32_t history_bytes)
{
    char line[176];
    uint32_t at = 0u;

    at = append(line, at, "INTERFACE UNDO n=");
    at = append_hex32(line, at, GALLERY_UNDO_BENCHMARK_OPERATIONS);
    at = append(line, at, " rec=");
    at = append_hex64(line, at, record_elapsed);
    at = append(line, at, " undo=");
    at = append_hex64(line, at, undo_elapsed);
    at = append(line, at, " redo=");
    at = append_hex64(line, at, redo_elapsed);
    at = append(line, at, " bytes=");
    at = append_hex32(line, at, history_bytes);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_text(uint64_t append_elapsed, uint64_t fragmented_elapsed,
                        uint32_t pieces)
{
    char line[144];
    uint32_t at = 0u;

    at = append(line, at, "INTERFACE TEXT n=");
    at = append_hex32(line, at, GALLERY_TEXT_BENCHMARK_OPERATIONS);
    at = append(line, at, " append-ns=");
    at = append_hex64(line, at, append_elapsed);
    at = append(line, at, " fragmented-ns=");
    at = append_hex64(line, at, fragmented_elapsed);
    at = append(line, at, " pieces=");
    at = append_hex32(line, at, pieces);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_control(const char *name, uint32_t count, uint64_t elapsed)
{
    char line[112];
    uint32_t at = 0u;

    at = append(line, at, "INTERFACE ");
    at = append(line, at, name);
    at = append(line, at, " n=");
    at = append_hex32(line, at, count);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
}

static uint32_t add_label(uint32_t index, uint32_t id, const char *text,
                          uint32_t length, uint32_t role)
{
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;

    info.id = id;
    info.text = text;
    info.text_length = length;
    info.text_role = role;
    return astra_interface_label_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_button(uint32_t index, uint32_t id, const char *text,
                           uint32_t length, uint32_t variant, uint32_t state,
                           uint32_t preview)
{
    AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;

    info.id = id;
    info.text = text;
    info.text_length = length;
    info.variant = variant;
    info.state = state;
    info.preview_state = preview;
    return astra_interface_button_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_disclosure(uint32_t index, uint32_t id,
                               uint32_t target_id, const char *text,
                               uint32_t length)
{
    AstraDisclosureInfo info = ASTRA_DISCLOSURE_INFO_INIT;

    info.id = id;
    info.target_id = target_id;
    info.text = text;
    info.text_length = length;
    return astra_interface_disclosure_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_toggle(uint32_t index, uint32_t id, const char *text,
                           uint32_t length, uint32_t kind, uint32_t group,
                           uint32_t state)
{
    AstraToggleInfo info = ASTRA_TOGGLE_INFO_INIT;
    AstraResult result;

    info.id = id;
    info.text = text;
    info.text_length = length;
    info.group_id = group;
    info.state = state;
    if (kind == ASTRA_CONTROL_CHECKBOX)
        result = astra_interface_checkbox_init(&controls[index], &info);
    else if (kind == ASTRA_CONTROL_RADIO)
        result = astra_interface_radio_init(&controls[index], &info);
    else
        result = astra_interface_switch_init(&controls[index], &info);
    return result == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_slider(uint32_t index, uint32_t id, int32_t value)
{
    AstraRangeInfo info = ASTRA_RANGE_INFO_INIT;

    info.id = id;
    info.value = value;
    return astra_interface_slider_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_dial(uint32_t index, uint32_t id, int32_t value)
{
    AstraDialInfo info = ASTRA_DIAL_INFO_INIT;

    info.id = id;
    info.value = value;
    info.minimum = -100;
    info.maximum = 100;
    info.step = 5;
    info.reset_value = 0;
    return astra_interface_dial_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_progress(uint32_t index, uint32_t id,
                             uint32_t value, uint32_t maximum)
{
    AstraProgressInfo info = ASTRA_PROGRESS_INFO_INIT;

    info.id = id;
    info.value = value;
    info.maximum = maximum;
    return astra_interface_progress_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_field(uint32_t index, uint32_t id,
                          AstraTextModel *model, uint32_t columns,
                          uint32_t flags, uint32_t state)
{
    AstraFieldInfo info = ASTRA_FIELD_INFO_INIT;

    info.id = id;
    info.model = model;
    info.preferred_columns = columns;
    info.flags = flags;
    info.state = state;
    return astra_interface_field_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_segmented(uint32_t index, uint32_t id,
                              uint32_t selected)
{
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;

    info.id = id;
    info.items = gallery_choices;
    info.item_count = sizeof(gallery_choices) / sizeof(gallery_choices[0]);
    info.selected = selected;
    return astra_interface_segmented_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_tabs(uint32_t index, uint32_t id)
{
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;

    info.id = id;
    info.items = gallery_tabs;
    info.item_count = sizeof(gallery_tabs) / sizeof(gallery_tabs[0]);
    return astra_interface_tab_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_page(uint32_t index, uint32_t id, uint32_t state)
{
    AstraContainerInfo info = ASTRA_CONTAINER_INFO_INIT;

    info.id = id;
    info.state = state;
    info.layout.flags = ASTRA_FLEX_WRAP;
    info.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    info.layout.padding_left = 8u;
    info.layout.padding_top = 8u;
    info.layout.padding_right = 8u;
    info.layout.padding_bottom = 8u;
    info.layout.main_gap = 12u;
    info.layout.cross_gap = 20u;
    return astra_interface_container_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_stepper(uint32_t index, uint32_t id, int32_t value,
                            int32_t minimum, int32_t maximum, int32_t step)
{
    AstraStepperInfo info = ASTRA_STEPPER_INFO_INIT;

    info.id = id;
    info.value = value;
    info.minimum = minimum;
    info.maximum = maximum;
    info.step = step;
    return astra_interface_stepper_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_scroll_view(uint32_t index, uint32_t id)
{
    AstraScrollViewInfo info = ASTRA_SCROLL_VIEW_INFO_INIT;

    info.id = id;
    info.model = &gallery_scroll_model;
    info.preferred_width = 260u;
    info.preferred_height = 120u;
    return astra_interface_scroll_view_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_scrollbar(uint32_t index, uint32_t id)
{
    AstraScrollbarInfo info = ASTRA_SCROLLBAR_INFO_INIT;

    info.id = id;
    info.model = &gallery_scroll_model;
    info.orientation = ASTRA_ORIENTATION_VERTICAL;
    return astra_interface_scrollbar_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_splitter(uint32_t index, uint32_t id)
{
    AstraSplitterInfo info = ASTRA_SPLITTER_INFO_INIT;

    info.id = id;
    return astra_interface_splitter_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_split_row(uint32_t index, uint32_t id)
{
    AstraContainerInfo info = ASTRA_CONTAINER_INFO_INIT;

    info.id = id;
    info.layout.direction = ASTRA_FLEX_ROW;
    info.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    return astra_interface_container_init(&controls[index], &info) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t init_field_model(AstraTextModel *model, const char *text,
                                 uint32_t text_bytes, void *content,
                                 uint32_t content_bytes, void *metadata,
                                 uint32_t metadata_bytes)
{
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;

    info.text = text;
    info.text_bytes = text_bytes;
    info.content_arena = content;
    info.content_arena_bytes = content_bytes;
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = metadata_bytes;
    info.selection.anchor = text_bytes;
    info.selection.focus = text_bytes;
    return astra_text_model_init(model, &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t format_percent(char out[5], int64_t value)
{
    uint32_t at = 0u;

    if (value >= 100) out[at++] = '1';
    if (value >= 10) out[at++] = (char)('0' + value / 10 % 10);
    out[at++] = (char)('0' + value % 10);
    out[at++] = '%';
    out[at] = '\0';
    return at;
}

static uint32_t format_integer(char out[22], int64_t value)
{
    char reverse[20];
    uint64_t magnitude = value < 0 ? 0u - (uint64_t)value :
                                     (uint64_t)value;
    uint32_t at = 0u;
    uint32_t digits = 0u;

    if (value < 0) out[at++] = '-';
    do {
        reverse[digits++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);
    while (digits != 0u) out[at++] = reverse[--digits];
    out[at] = '\0';
    return at;
}

static AstraControl *field_control(uint32_t id)
{
    if (id == GALLERY_FIELD) return GALLERY_CONTROL(GALLERY_FIELD);
    if (id == GALLERY_FIELD_ERROR)
        return GALLERY_CONTROL(GALLERY_FIELD_ERROR);
    if (id == GALLERY_FIELD_READ_ONLY)
        return GALLERY_CONTROL(GALLERY_FIELD_READ_ONLY);
    return NULL;
}

static AstraTextModel *field_model_for_id(uint32_t id)
{
    if (id == GALLERY_FIELD) return &field_model;
    if (id == GALLERY_FIELD_ERROR) return &error_field_model;
    if (id == GALLERY_FIELD_READ_ONLY) return &read_only_field_model;
    return NULL;
}

static uint32_t copy_field(AstraHandle clipboard, AstraControl *control,
                           AstraTextModel *model, int cut)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraClipboardRepresentation representation =
        ASTRA_CLIPBOARD_REPRESENTATION_INIT;
    uint32_t start;
    uint32_t end;
    uint32_t bytes;
    AstraResult result;

    result = astra_text_model_get_state(model, &state);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    if (start == end) return ASTRA_STATUS_OK;
    if (cut) {
        AstraTextModelRequirements requirements =
            ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;

        result = astra_text_model_replace_requirements(
            model, start, end, NULL, 0u, &requirements);
        if (result != ASTRA_OK ||
            requirements.content_arena_bytes > state.content_arena_bytes ||
            requirements.metadata_arena_bytes > state.metadata_arena_bytes)
            return GALLERY_FAIL_CONTROL;
    }
    result = astra_text_model_copy(
        model, start, end, clipboard_scratch, sizeof(clipboard_scratch),
        &bytes);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    representation.type = ASTRA_CLIPBOARD_TYPE_UTF8;
    representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
    representation.data = clipboard_scratch;
    representation.data_length = bytes;
    result = astra_clipboard_write(
        clipboard, &representation, 1u, NULL);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    if (cut && astra_interface_field_replace_selection(
                   &ui, control, NULL, 0u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    return ASTRA_STATUS_OK;
}

static uint32_t paste_field(AstraHandle clipboard, AstraControl *control)
{
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    const void *text;
    uint32_t bytes;
    AstraResult result = astra_clipboard_read(clipboard, &item);
    AstraResult close_result;

    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    result = astra_clipboard_item_find(
        &item, ASTRA_CLIPBOARD_TYPE_UTF8,
        sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u, &text, &bytes);
    if (result == ASTRA_OK)
        result = astra_interface_field_replace_selection(
            &ui, control, text, bytes);
    close_result = astra_clipboard_item_close(&item);
    if (result == ASTRA_OK) result = close_result;
    return result == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t handle_field_action(AstraHandle clipboard,
                                    const AstraUIAction *action)
{
    AstraControl *control = field_control(action->control_id);
    AstraTextModel *model = field_model_for_id(action->control_id);

    if (control == NULL || model == NULL) return ASTRA_STATUS_OK;
    if (action->type == ASTRA_UI_ACTION_COPY)
        return copy_field(clipboard, control, model, 0);
    if (action->type == ASTRA_UI_ACTION_CUT)
        return copy_field(clipboard, control, model, 1);
    if (action->type == ASTRA_UI_ACTION_PASTE)
        return paste_field(clipboard, control);
    return ASTRA_STATUS_OK;
}

static uint32_t set_item(uint32_t id, uint32_t parent, uint32_t flags,
                         uint32_t grow)
{
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    item.parent_id = parent;
    item.flags = flags;
    item.grow = grow;
    return astra_interface_control_set_flex(GALLERY_CONTROL(id), &item) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t layout_controls(void)
{
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    layout.padding_left = 16u;
    layout.padding_top = 16u;
    layout.padding_right = 16u;
    layout.padding_bottom = 16u;
    layout.main_gap = 12u;
    return astra_interface_ui_layout(&ui, &layout) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t select_page(int32_t selected)
{
    uint32_t page_count = GALLERY_PAGE_LAYOUT - GALLERY_PAGE_INPUT + 1u;
    uint32_t next;

    if (selected < 0 || (uint32_t)selected >= page_count)
        return GALLERY_FAIL_CONTROL;
    next = GALLERY_PAGE_INPUT + (uint32_t)selected;
    if (next == active_page) return ASTRA_STATUS_OK;
    if (astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(next), 0u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(active_page),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK) {
        (void)astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(next), ASTRA_CONTROL_COLLAPSED);
        return GALLERY_FAIL_CONTROL;
    }
    active_page = next;
    return ASTRA_STATUS_OK;
}

static uint32_t build_controls(void)
{
    uint32_t status = add_tabs(GALLERY_TABS - 1u, GALLERY_TABS);
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;

    scroll.line_width = 8u;
    scroll.line_height = 16u;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&gallery_scroll_model, &scroll) !=
            ASTRA_OK)
        status = GALLERY_FAIL_CONTROL;

    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_LAYOUT; ++page)
        status = add_page(page - 1u, page,
                          page == GALLERY_PAGE_INPUT ? 0u :
                          ASTRA_CONTROL_COLLAPSED);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_TEXT_LABEL - 1u, GALLERY_TEXT_LABEL,
                           "TEXT / UTF-8 / FALLBACK",
                           sizeof("TEXT / UTF-8 / FALLBACK") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_LAYOUT_LABEL - 1u, GALLERY_LAYOUT_LABEL,
                           "LAYOUT / STATES / RESIZE",
                           sizeof("LAYOUT / STATES / RESIZE") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_LABEL - 1u, GALLERY_LABEL,
                           "BUTTONS / ACTIONS",
                           sizeof("BUTTONS / ACTIONS") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_APPLY - 1u, GALLERY_APPLY, "Apply", 5u,
                            ASTRA_BUTTON_PRIMARY, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_REVERT - 1u, GALLERY_REVERT, "Revert", 6u,
                            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_HOVER - 1u, GALLERY_HOVER, "Hover", 5u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_HOVERED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PRESSED - 1u, GALLERY_PRESSED, "Pressed", 7u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_PRESSED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_FOCUSED - 1u, GALLERY_FOCUSED, "Focused", 7u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_FOCUSED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_DISABLED - 1u, GALLERY_DISABLED, "Disabled", 8u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_DISABLED, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_SELECTED - 1u, GALLERY_SELECTED, "Selected", 8u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_SELECTED, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_ERROR - 1u, GALLERY_ERROR, "Error", 5u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_ERROR, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_WARNING - 1u, GALLERY_WARNING, "Pending", 7u,
                            ASTRA_BUTTON_WARNING, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_DESTRUCTIVE - 1u, GALLERY_DESTRUCTIVE,
                            "Erase volume", 12u,
                            ASTRA_BUTTON_DESTRUCTIVE, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_HELP - 1u, GALLERY_HELP,
                           "Tab and Shift-Tab move focus. Enter or Space activates.",
                           sizeof("Tab and Shift-Tab move focus. Enter or Space activates.") - 1u,
                           ASTRA_TEXT_CLIENT_TERTIARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_TOGGLE_LABEL - 1u, GALLERY_TOGGLE_LABEL,
                           "CHECK / RADIO / SWITCH",
                           sizeof("CHECK / RADIO / SWITCH") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_CHECKBOX - 1u, GALLERY_CHECKBOX,
                            "Snap to grid", sizeof("Snap to grid") - 1u,
                            ASTRA_CONTROL_CHECKBOX, 0u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_RADIO_565 - 1u, GALLERY_RADIO_565,
                            "RGB565", sizeof("RGB565") - 1u,
                            ASTRA_CONTROL_RADIO, 1u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_RADIO_8888 - 1u, GALLERY_RADIO_8888,
                            "XRGB8888", sizeof("XRGB8888") - 1u,
                            ASTRA_CONTROL_RADIO, 1u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_SWITCH - 1u, GALLERY_SWITCH,
                            "Tear-free", sizeof("Tear-free") - 1u,
                            ASTRA_CONTROL_SWITCH, 0u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_TOGGLE_DISABLED - 1u,
                            GALLERY_TOGGLE_DISABLED, "Unavailable",
                            sizeof("Unavailable") - 1u,
                            ASTRA_CONTROL_CHECKBOX, 0u,
                            ASTRA_CONTROL_DISABLED);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SLIDER_LABEL - 1u, GALLERY_SLIDER_LABEL,
                           "SLIDER", sizeof("SLIDER") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_slider(GALLERY_SLIDER - 1u, GALLERY_SLIDER, 62);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SLIDER_VALUE - 1u, GALLERY_SLIDER_VALUE,
                           slider_value_text, sizeof("62%") - 1u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_DIAL_LABEL - 1u, GALLERY_DIAL_LABEL,
                           "DIAL · DRAG VERTICALLY · ALT FINE",
                           sizeof("DIAL · DRAG VERTICALLY · ALT FINE") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_dial(GALLERY_DIAL - 1u, GALLERY_DIAL, 0);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_DIAL_VALUE - 1u, GALLERY_DIAL_VALUE,
                           dial_value_text, 1u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_PROGRESS_LABEL - 1u,
                           GALLERY_PROGRESS_LABEL, "PROGRESS",
                           sizeof("PROGRESS") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_progress(GALLERY_PROGRESS - 1u, GALLERY_PROGRESS,
                              44u, 100u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_PROGRESS_VALUE - 1u,
                           GALLERY_PROGRESS_VALUE, "44%", 3u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = add_progress(GALLERY_PROGRESS_UNKNOWN - 1u,
                              GALLERY_PROGRESS_UNKNOWN, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_PROGRESS_UNKNOWN_VALUE - 1u,
                           GALLERY_PROGRESS_UNKNOWN_VALUE, "Working", 7u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = init_field_model(
            &field_model, "/home//projects/astra68",
            sizeof("/home//projects/astra68") - 1u,
            field_content, sizeof(field_content),
            field_metadata, sizeof(field_metadata));
    if (status == ASTRA_STATUS_OK)
        status = init_field_model(
            &error_field_model, "0x0000FFFG",
            sizeof("0x0000FFFG") - 1u,
            error_field_content, sizeof(error_field_content),
            error_field_metadata, sizeof(error_field_metadata));
    if (status == ASTRA_STATUS_OK)
        status = init_field_model(
            &read_only_field_model, "UTF-8: \xe4\xb8\x96\xe7\x95\x8c",
            sizeof("UTF-8: \xe4\xb8\x96\xe7\x95\x8c") - 1u,
            read_only_field_content, sizeof(read_only_field_content),
            read_only_field_metadata, sizeof(read_only_field_metadata));
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_FIELD_LABEL - 1u, GALLERY_FIELD_LABEL,
                           "FIELD / SELECTION / VALIDATION",
                           sizeof("FIELD / SELECTION / VALIDATION") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_field(GALLERY_FIELD - 1u, GALLERY_FIELD,
                           &field_model, 28u, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_field(GALLERY_FIELD_ERROR - 1u, GALLERY_FIELD_ERROR,
                           &error_field_model,
                           16u, 0u, ASTRA_CONTROL_ERROR);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_FIELD_ERROR_TEXT - 1u,
                           GALLERY_FIELD_ERROR_TEXT,
                           "not a valid 32-bit address",
                           sizeof("not a valid 32-bit address") - 1u,
                           ASTRA_TEXT_CLIENT_FAULT);
    if (status == ASTRA_STATUS_OK)
        status = add_field(GALLERY_FIELD_READ_ONLY - 1u,
                           GALLERY_FIELD_READ_ONLY,
                           &read_only_field_model, 18u,
                           ASTRA_FIELD_READ_ONLY, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SEGMENTED_LABEL - 1u,
                           GALLERY_SEGMENTED_LABEL,
                           "SEGMENTED SELECTOR",
                           sizeof("SEGMENTED SELECTOR") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_segmented(GALLERY_SEGMENTED - 1u,
                               GALLERY_SEGMENTED, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_STEPPER_LABEL - 1u,
                           GALLERY_STEPPER_LABEL, "STEPPER · KB",
                           sizeof("STEPPER · KB") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_stepper(GALLERY_STEPPER - 1u, GALLERY_STEPPER,
                             1024, 0, 8192, 64);
    if (status == ASTRA_STATUS_OK)
        status = add_scroll_view(GALLERY_SCROLL_VIEW - 1u,
                                 GALLERY_SCROLL_VIEW);
    if (status == ASTRA_STATUS_OK)
        status = add_page(GALLERY_SCROLL_CONTENT - 1u,
                          GALLERY_SCROLL_CONTENT, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SCROLL_LABEL - 1u, GALLERY_SCROLL_LABEL,
                           "SCROLLVIEW / HARDWARE RETENTION",
                           sizeof("SCROLLVIEW / HARDWARE RETENTION") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_scrollbar(GALLERY_SCROLLBAR - 1u, GALLERY_SCROLLBAR);
    if (status == ASTRA_STATUS_OK)
        status = add_split_row(GALLERY_SPLIT_ROW - 1u, GALLERY_SPLIT_ROW);
    if (status == ASTRA_STATUS_OK)
        status = add_page(GALLERY_SPLIT_LEFT - 1u, GALLERY_SPLIT_LEFT, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SPLIT_LEFT_LABEL - 1u,
                           GALLERY_SPLIT_LEFT_LABEL, "NAVIGATION PANE",
                           sizeof("NAVIGATION PANE") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_splitter(GALLERY_SPLITTER - 1u, GALLERY_SPLITTER);
    if (status == ASTRA_STATUS_OK)
        status = add_page(GALLERY_SPLIT_RIGHT - 1u, GALLERY_SPLIT_RIGHT, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SPLIT_RIGHT_LABEL - 1u,
                           GALLERY_SPLIT_RIGHT_LABEL, "CONTENT PANE",
                           sizeof("CONTENT PANE") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_disclosure(
            GALLERY_DISCLOSURE - 1u, GALLERY_DISCLOSURE,
            GALLERY_DISCLOSURE_BODY, "Advanced",
            sizeof("Advanced") - 1u);
    if (status == ASTRA_STATUS_OK)
        status = add_page(GALLERY_DISCLOSURE_BODY - 1u,
                          GALLERY_DISCLOSURE_BODY, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(
            GALLERY_DISCLOSURE_OPTION_A - 1u,
            GALLERY_DISCLOSURE_OPTION_A, "Reject late generations",
            sizeof("Reject late generations") - 1u,
            ASTRA_CONTROL_CHECKBOX, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(
            GALLERY_DISCLOSURE_OPTION_B - 1u,
            GALLERY_DISCLOSURE_OPTION_B, "Log active-surface hazards",
            sizeof("Log active-surface hazards") - 1u,
            ASTRA_CONTROL_CHECKBOX, 0u, ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = set_item(GALLERY_TABS, 0u, 0u, 0u);
    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_LAYOUT; ++page)
        status = set_item(page, 0u, 0u, 1u);

#define PAGE_ITEM(id, page, flags)                                      \
    do {                                                                \
        if (status == ASTRA_STATUS_OK)                                  \
            status = set_item((id), (page), (flags), 0u);               \
    } while (0)
    PAGE_ITEM(GALLERY_LABEL, GALLERY_PAGE_INPUT, ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_APPLY, GALLERY_PAGE_INPUT, 0u);
    PAGE_ITEM(GALLERY_REVERT, GALLERY_PAGE_INPUT, ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_LABEL, GALLERY_PAGE_INPUT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD, GALLERY_PAGE_INPUT, ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_ERROR, GALLERY_PAGE_INPUT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_ERROR_TEXT, GALLERY_PAGE_INPUT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_HELP, GALLERY_PAGE_INPUT, ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_TOGGLE_LABEL, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_CHECKBOX, GALLERY_PAGE_CHOICE, 0u);
    PAGE_ITEM(GALLERY_RADIO_565, GALLERY_PAGE_CHOICE, 0u);
    PAGE_ITEM(GALLERY_RADIO_8888, GALLERY_PAGE_CHOICE, 0u);
    PAGE_ITEM(GALLERY_SWITCH, GALLERY_PAGE_CHOICE, 0u);
    PAGE_ITEM(GALLERY_TOGGLE_DISABLED, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SEGMENTED_LABEL, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SEGMENTED, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    if (status == ASTRA_STATUS_OK)
        status = set_item(
            GALLERY_DISCLOSURE, GALLERY_PAGE_CHOICE,
            ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER, 1u);
    if (status == ASTRA_STATUS_OK)
        status = set_item(
            GALLERY_DISCLOSURE_BODY, GALLERY_PAGE_CHOICE,
            ASTRA_FLEX_BREAK_AFTER, 1u);
    PAGE_ITEM(GALLERY_DISCLOSURE_OPTION_A, GALLERY_DISCLOSURE_BODY, 0u);
    PAGE_ITEM(GALLERY_DISCLOSURE_OPTION_B, GALLERY_DISCLOSURE_BODY,
              ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_SLIDER_LABEL, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SLIDER, GALLERY_PAGE_VALUE, 0u);
    PAGE_ITEM(GALLERY_SLIDER_VALUE, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DIAL_LABEL, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DIAL, GALLERY_PAGE_VALUE, 0u);
    PAGE_ITEM(GALLERY_DIAL_VALUE, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_STEPPER_LABEL, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_STEPPER, GALLERY_PAGE_VALUE,
              ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_PROGRESS_LABEL, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PROGRESS, GALLERY_PAGE_PROGRESS, 0u);
    PAGE_ITEM(GALLERY_PROGRESS_VALUE, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PROGRESS_UNKNOWN, GALLERY_PAGE_PROGRESS, 0u);
    PAGE_ITEM(GALLERY_PROGRESS_UNKNOWN_VALUE, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_TEXT_LABEL, GALLERY_PAGE_TEXT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_READ_ONLY, GALLERY_PAGE_TEXT,
              ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_LAYOUT_LABEL, GALLERY_PAGE_LAYOUT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_HOVER, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_PRESSED, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_FOCUSED, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_DISABLED, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_SELECTED, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_ERROR, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_WARNING, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_DESTRUCTIVE, GALLERY_PAGE_LAYOUT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SCROLL_VIEW, GALLERY_PAGE_LAYOUT, 0u);
    PAGE_ITEM(GALLERY_SCROLL_LABEL, GALLERY_SCROLL_CONTENT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLIT_ROW, GALLERY_PAGE_LAYOUT,
              ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLIT_LEFT, GALLERY_SPLIT_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_LEFT_LABEL, GALLERY_SPLIT_LEFT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLITTER, GALLERY_SPLIT_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_RIGHT, GALLERY_SPLIT_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_RIGHT_LABEL, GALLERY_SPLIT_RIGHT,
              ASTRA_FLEX_BREAK_AFTER);
#undef PAGE_ITEM
    if (status == ASTRA_STATUS_OK) {
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        item.parent_id = GALLERY_SCROLL_VIEW;
        item.minimum_width = 260u;
        item.minimum_height = 320u;
        status = astra_interface_control_set_flex(
            GALLERY_CONTROL(GALLERY_SCROLL_CONTENT), &item) == ASTRA_OK ?
            ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_PAGE_LAYOUT;
        item.flags = ASTRA_FLEX_BREAK_AFTER;
        item.minimum_width = 10u;
        item.maximum_width = 10u;
        item.minimum_height = 120u;
        item.maximum_height = 120u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_SCROLLBAR), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_PAGE_LAYOUT;
        item.flags = ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER;
        item.minimum_width = 700u;
        item.minimum_height = 160u;
        item.grow = 1u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_SPLIT_ROW), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_SPLIT_ROW;
        item.basis = 280u;
        item.minimum_width = 160u;
        item.maximum_width = 700u;
        item.grow = 1u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_SPLIT_LEFT), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_SPLIT_ROW;
        item.minimum_width = 8u;
        item.maximum_width = 8u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_SPLITTER), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_SPLIT_ROW;
        item.minimum_width = 160u;
        item.grow = 2u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_SPLIT_RIGHT), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }
    if (status != ASTRA_STATUS_OK)
        return status;
    if (astra_interface_ui_init(&ui, controls, GALLERY_CONTROL_COUNT,
                                   GALLERY_WIDTH, GALLERY_HEIGHT) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    return layout_controls();
}

static uint32_t benchmark_layout(uint32_t count, uint32_t iterations)
{
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraFlexLayout content = ASTRA_FLEX_LAYOUT_INIT;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    content.flags = ASTRA_FLEX_WRAP;
    content.padding_left = 24u;
    content.padding_top = 24u;
    content.padding_right = 24u;
    content.padding_bottom = 24u;
    content.main_gap = 12u;
    content.cross_gap = 12u;
    {
        AstraContainerInfo info = ASTRA_CONTAINER_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        info.id = 1u;
        info.layout = content;
        benchmark_controls[0] = (AstraControl)ASTRA_CONTROL_INIT;
        if (astra_interface_container_init(&benchmark_controls[0], &info) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        item.grow = 1u;
        if (astra_interface_control_set_flex(
                &benchmark_controls[0], &item) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    for (uint32_t at = 1u; at < count; ++at) {
        AstraButtonInfo info = ASTRA_BUTTON_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        info.id = at + 1u;
        info.text = "Layout";
        info.text_length = 6u;
        benchmark_controls[at] = (AstraControl)ASTRA_CONTROL_INIT;
        if (astra_interface_button_init(&benchmark_controls[at], &info) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        item.parent_id = 1u;
        item.grow = at % 3u + 1u;
        if (astra_interface_control_set_flex(
                &benchmark_controls[at], &item) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    if (astra_interface_ui_init(&context, benchmark_controls, count,
                                   1120u, 800u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (astra_interface_ui_layout(&context, &root) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
        AstraWindowEvent event = {0};

        event.size = sizeof(event);
        event.version = ASTRA_WINDOW_EVENT_VERSION;
        event.type = ASTRA_WINDOW_EVENT_RESIZE;
        event.data.resize.width =
            (iteration & 1u) != 0u ? 960u : 1120u;
        event.data.resize.height = 800u;
        if (astra_interface_ui_handle_event(
                &context, &event, &action) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    report_layout(count, iterations, astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_layouts(void)
{
    uint32_t status = benchmark_layout(12u, 4096u);

    if (status == ASTRA_STATUS_OK) status = benchmark_layout(64u, 1024u);
    if (status == ASTRA_STATUS_OK) status = benchmark_layout(256u, 256u);
    return status;
}

static uint32_t benchmark_segmented(void)
{
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    info.id = 1u;
    info.items = gallery_choices;
    info.item_count = sizeof(gallery_choices) / sizeof(gallery_choices[0]);
    if (astra_interface_segmented_init(&control, &info) != ASTRA_OK ||
        astra_interface_ui_init(&context, &control, 1u, 240u, 40u) !=
            ASTRA_OK ||
        astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 1;
    event.data.pointer.y = 1;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u;
         at < GALLERY_SEGMENTED_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x4fu : 0x50u;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("SEGMENTED", GALLERY_SEGMENTED_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_tab(void)
{
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    info.id = 1u;
    info.items = gallery_tabs;
    info.item_count = sizeof(gallery_tabs) / sizeof(gallery_tabs[0]);
    if (astra_interface_tab_init(&control, &info) != ASTRA_OK ||
        astra_interface_ui_init(&context, &control, 1u, 480u, 40u) !=
            ASTRA_OK ||
        astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 1;
    event.data.pointer.y = 1;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_TAB_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x4fu : 0x50u;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("TAB", GALLERY_TAB_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_stepper(void)
{
    AstraStepperInfo info = ASTRA_STEPPER_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    info.id = 1u;
    info.value = 0;
    info.minimum = -1;
    info.maximum = 1;
    if (astra_interface_stepper_init(&control, &info) != ASTRA_OK ||
        astra_interface_ui_init(&context, &control, 1u, 100u, 40u) !=
            ASTRA_OK ||
        astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 1;
    event.data.pointer.y = 1;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(
            &context, &event, &action) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u;
         at < GALLERY_STEPPER_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x4fu : 0x50u;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("STEPPER", GALLERY_STEPPER_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_dial(void)
{
    AstraDialInfo info = ASTRA_DIAL_INFO_INIT;
    AstraControl control = ASTRA_CONTROL_INIT;
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    info.id = 1u;
    info.minimum = -1;
    info.maximum = 1;
    info.reset_value = 0;
    if (astra_interface_dial_init(&control, &info) != ASTRA_OK ||
        astra_interface_ui_init(&context, &control, 1u, 56u, 56u) !=
            ASTRA_OK ||
        astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 28;
    event.data.pointer.y = 28;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_DIAL_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x52u : 0x51u;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK || action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("DIAL", GALLERY_DIAL_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_disclosure(void)
{
    AstraDisclosureInfo disclosure = ASTRA_DISCLOSURE_INFO_INIT;
    AstraContainerInfo body = ASTRA_CONTAINER_INFO_INIT;
    AstraToggleInfo option = ASTRA_TOGGLE_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl benchmark[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    disclosure.id = 1u;
    disclosure.text = "Advanced";
    disclosure.text_length = 8u;
    disclosure.target_id = 2u;
    body.id = 2u;
    body.state = ASTRA_CONTROL_COLLAPSED;
    body.layout.direction = ASTRA_FLEX_COLUMN;
    option.id = 3u;
    option.text = "Option";
    option.text_length = 6u;
    item.parent_id = 2u;
    if (astra_interface_disclosure_init(&benchmark[0], &disclosure) !=
            ASTRA_OK ||
        astra_interface_container_init(&benchmark[1], &body) != ASTRA_OK ||
        astra_interface_checkbox_init(&benchmark[2], &option) != ASTRA_OK ||
        astra_interface_control_set_flex(&benchmark[2], &item) != ASTRA_OK ||
        astra_interface_ui_init(&context, benchmark, 3u, 240u, 100u) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    layout.direction = ASTRA_FLEX_COLUMN;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 1;
    event.data.pointer.y = 1;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u;
         at < GALLERY_DISCLOSURE_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x50u : 0x4fu;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("DISCLOSURE", GALLERY_DISCLOSURE_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_splitter(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraSplitterInfo splitter = ASTRA_SPLITTER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraControl benchmark[3] = {
        ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT, ASTRA_CONTROL_INIT};
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraWindowEvent event = {0};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint64_t started;

    label.id = 1u;
    label.text = "L";
    label.text_length = 1u;
    if (astra_interface_label_init(&benchmark[0], &label) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    item.basis = 96u;
    item.minimum_width = 32u;
    item.maximum_width = 160u;
    if (astra_interface_control_set_flex(&benchmark[0], &item) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    splitter.id = 2u;
    if (astra_interface_splitter_init(&benchmark[1], &splitter) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 8u;
    item.maximum_width = 8u;
    if (astra_interface_control_set_flex(&benchmark[1], &item) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    label.id = 3u;
    label.text = "R";
    if (astra_interface_label_init(&benchmark[2], &label) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.basis = 96u;
    item.minimum_width = 32u;
    item.maximum_width = 160u;
    if (astra_interface_control_set_flex(&benchmark[2], &item) != ASTRA_OK ||
        astra_interface_ui_init(&context, benchmark, 3u, 200u, 40u) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    layout.direction = ASTRA_FLEX_ROW;
    layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (astra_interface_ui_layout(&context, &layout) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_POINTER_BUTTON;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.pointer.x = 100;
    event.data.pointer.y = 20;
    event.data.pointer.button = ASTRA_INPUT_BUTTON_LEFT;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.flags = 0u;
    if (astra_interface_ui_handle_event(&context, &event, &action) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u;
         at < GALLERY_SPLITTER_BENCHMARK_OPERATIONS; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x4fu : 0x50u;
        if (astra_interface_ui_handle_event(&context, &event, &action) !=
                ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_VALUE_CHANGED)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("SPLITTER", GALLERY_SPLITTER_BENCHMARK_OPERATIONS,
                   astra_clock_monotonic() - started);
    return ASTRA_STATUS_OK;
}

typedef struct GalleryUndoChange {
    uint32_t before;
    uint32_t after;
} GalleryUndoChange;

static uint32_t undo_benchmark_value;

static AstraResult apply_undo_benchmark(void *context, uint32_t operation,
                                        uint32_t direction,
                                        const void *payload,
                                        uint32_t payload_bytes)
{
    const GalleryUndoChange *change = payload;
    uint32_t *value = context;

    if (value == NULL || operation != 1u || change == NULL ||
        payload_bytes != sizeof(*change) ||
        (direction != ASTRA_UNDO_DIRECTION_UNDO &&
         direction != ASTRA_UNDO_DIRECTION_REDO))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *value = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        change->before : change->after;
    return ASTRA_OK;
}

static uint32_t benchmark_undo(void)
{
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    AstraUndoManagerInfo manager_info = ASTRA_UNDO_MANAGER_INFO_INIT;
    AstraUndoState state = ASTRA_UNDO_STATE_INIT;
    uint64_t started;
    uint64_t record_elapsed;
    uint64_t undo_elapsed;
    uint64_t redo_elapsed;

    manager_info.arena = undo_benchmark_arena;
    manager_info.arena_bytes = sizeof(undo_benchmark_arena);
    manager_info.apply = apply_undo_benchmark;
    manager_info.context = &undo_benchmark_value;
    if (astra_undo_init(&manager, &manager_info) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u;
         index < GALLERY_UNDO_BENCHMARK_OPERATIONS; ++index) {
        GalleryUndoChange change = {index, index + 1u};
        AstraUndoAction action = ASTRA_UNDO_ACTION_INIT;
        AstraUndoGroupInfo group = ASTRA_UNDO_GROUP_INFO_INIT;

        action.operation = 1u;
        action.payload = &change;
        action.payload_bytes = sizeof(change);
        group.actions = &action;
        group.action_count = 1u;
        if (astra_undo_perform_group(&manager, &group) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    record_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != GALLERY_UNDO_BENCHMARK_OPERATIONS)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u;
         index < GALLERY_UNDO_BENCHMARK_OPERATIONS; ++index)
        if (astra_undo_undo(&manager) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    undo_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != 0u)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u;
         index < GALLERY_UNDO_BENCHMARK_OPERATIONS; ++index)
        if (astra_undo_redo(&manager) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    redo_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != GALLERY_UNDO_BENCHMARK_OPERATIONS ||
        astra_undo_get_state(&manager, &state) != ASTRA_OK ||
        state.group_count != GALLERY_UNDO_BENCHMARK_OPERATIONS)
        return GALLERY_FAIL_CONTROL;
    report_undo(record_elapsed, undo_elapsed, redo_elapsed,
                state.history_bytes);
    return ASTRA_STATUS_OK;
}

static uint32_t benchmark_text_model(void)
{
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint64_t started;
    uint64_t append_elapsed;
    uint64_t fragmented_elapsed;

    info.content_arena = text_benchmark_content;
    info.content_arena_bytes = sizeof(text_benchmark_content);
    info.metadata_arena = text_benchmark_metadata;
    info.metadata_arena_bytes = sizeof(text_benchmark_metadata);
    if (astra_text_model_init(&model, &info) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_TEXT_BENCHMARK_OPERATIONS; ++at)
        if (astra_text_model_replace(
                &model, at, at, "x", 1u) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    append_elapsed = astra_clock_monotonic() - started;
    astra_text_model_dispose(&model);

    if (astra_text_model_init(&model, &info) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_TEXT_BENCHMARK_OPERATIONS; ++at) {
        uint32_t position = (at & 1u) == 0u ? 0u : at;

        if (astra_text_model_replace(
                &model, position, position, "x", 1u) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    fragmented_elapsed = astra_clock_monotonic() - started;
    if (astra_text_model_get_state(&model, &state) != ASTRA_OK ||
        astra_text_model_validate(&model) != ASTRA_OK ||
        state.text_bytes != GALLERY_TEXT_BENCHMARK_OPERATIONS)
        return GALLERY_FAIL_CONTROL;
    report_text(append_elapsed, fragmented_elapsed, state.piece_count);
    astra_text_model_dispose(&model);
    return ASTRA_STATUS_OK;
}

static uint32_t paint(uint32_t clear)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    if (clear != 0u)
        astra_surface_clear(&surface.view, rgb565(theme.client));
    return astra_interface_ui_render(&ui, &surface.view) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_RENDER;
}

static uint32_t create_window(AstraHandle gui)
{
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result;

    if (astra_shared_draw_list_create(
            &surface, GALLERY_WIDTH, GALLERY_HEIGHT) != ASTRA_SYSCALL_OK)
        return GALLERY_FAIL_SURFACE;
    if (paint(1u) != ASTRA_STATUS_OK)
        return GALLERY_FAIL_RENDER;
    info.flags = ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE;
    info.x = 400u;
    info.y = 120u;
    info.width = GALLERY_WIDTH;
    info.height = GALLERY_HEIGHT;
    info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    info.type = ASTRA_WINDOW_STANDARD;
    info.title = "Interface Gallery";
    info.title_length = 17u;
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                      ASTRA_WINDOW_SUBSCRIBE_KEY |
                      ASTRA_WINDOW_SUBSCRIBE_TEXT |
                      ASTRA_WINDOW_SUBSCRIBE_VBLANK;
    result = astra_window_create(
        gui, surface.area, &info, &window);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery window create",
                                (uint32_t)(-result));
        return GALLERY_FAIL_WINDOW;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t handle_action(AstraHandle clipboard,
                              const AstraUIAction *action)
{
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        action->control_id == GALLERY_TABS &&
        select_page(action->value) != ASTRA_STATUS_OK)
        return GALLERY_FAIL_CONTROL;
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        action->control_id == GALLERY_SLIDER) {
        uint32_t length = format_percent(slider_value_text, action->value);

        if (astra_interface_control_set_text(
                &ui, GALLERY_CONTROL(GALLERY_SLIDER_VALUE),
                slider_value_text, length) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    if (action->type == ASTRA_UI_ACTION_VALUE_CHANGED &&
        action->control_id == GALLERY_DIAL) {
        uint32_t length = format_integer(dial_value_text, action->value);

        if (astra_interface_control_set_text(
                &ui, GALLERY_CONTROL(GALLERY_DIAL_VALUE),
                dial_value_text, length) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    if (handle_field_action(clipboard, action) != ASTRA_STATUS_OK)
        (void)astra_log("interface gallery clipboard action failed");
    return ASTRA_STATUS_OK;
}

static uint32_t run(AstraHandle clipboard)
{
    uint32_t vblank_subscribed = 1u;
    uint32_t applied_pointer_shape = UINT32_MAX;

    for (;;) {
        AstraWindowEvent event = {0};
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        AstraControlFrame damage;
        uint32_t animated =
            astra_interface_ui_animations_active(&ui) != 0;
        uint32_t waits[2];
        uint32_t selected = 0u;
        uint32_t scroll_only = 1u;
        AstraResult result;

        if (animated != vblank_subscribed) {
            uint32_t mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                            ASTRA_WINDOW_SUBSCRIBE_KEY |
                            ASTRA_WINDOW_SUBSCRIBE_TEXT;

            if (animated != 0u)
                mask |= ASTRA_WINDOW_SUBSCRIBE_VBLANK;
            if (astra_window_set_event_mask(&window, mask) !=
                ASTRA_OK)
                return GALLERY_FAIL_WINDOW;
            vblank_subscribed = animated;
        }
        if (animated != 0u) {
            waits[0] = astra_window_event_wait_handle(&window);
            waits[1] =
                astra_window_vblank_wait_handle(&window);
        } else {
            waits[0] = astra_window_event_wait_handle(&window);
        }
        if (astra_wait_multiple(waits, animated != 0u ? 2u : 1u,
                                ASTRA_DEADLINE_FOREVER, &selected, NULL) !=
            ASTRA_SYSCALL_OK)
            return GALLERY_FAIL_WINDOW;
        if (animated != 0u && selected == 1u) {
            if (astra_interface_ui_vblank(
                    &ui, astra_clock_monotonic(), &action) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (handle_action(clipboard, &action) != ASTRA_STATUS_OK)
                return GALLERY_FAIL_CONTROL;
            scroll_only = 0u;
            result = astra_window_event_try(&window, &event);
        } else {
            result = astra_window_event_try(&window, &event);
        }
        while (result == ASTRA_OK) {
            action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
            if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
                return ASTRA_STATUS_OK;
            if (astra_interface_ui_handle_event(
                    &ui, &event, &action) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (astra_interface_ui_update_pointer(
                    &ui, &window, ASTRA_POINTER_SHAPE_AUTOMATIC,
                    &applied_pointer_shape) != ASTRA_OK)
                return GALLERY_FAIL_WINDOW;
            if (handle_action(clipboard, &action) != ASTRA_STATUS_OK)
                return GALLERY_FAIL_CONTROL;
            if (action.type != ASTRA_UI_ACTION_SCROLL_CHANGED)
                scroll_only = 0u;
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE &&
                event.data.resize.width != 0u &&
                event.data.resize.height != 0u &&
                (surface.view.width != event.data.resize.width ||
                 surface.view.height != event.data.resize.height) &&
                !astra_draw_list_view_init(
                    &surface.view, surface.view.pixels, surface.view.byte_size,
                    event.data.resize.width,
                    event.data.resize.height))
                return GALLERY_FAIL_RENDER;
            result = astra_window_event_try(&window, &event);
        }
        if (result != ASTRA_ERROR_WOULD_BLOCK) {
            (void)astra_log_failure("gallery window event wait",
                                    (uint32_t)(-result));
            return GALLERY_FAIL_WINDOW;
        }
        if (astra_interface_ui_damage(&ui, &damage) != ASTRA_OK)
            continue;
        if (!astra_draw_list_view_init(
                &surface.view, surface.view.pixels, surface.view.byte_size,
                surface.view.width, surface.view.height) ||
            paint(scroll_only == 0u) != ASTRA_STATUS_OK)
            return GALLERY_FAIL_RENDER;
        {
            AstraWindowFrame region = {
                (uint16_t)damage.x, (uint16_t)damage.y,
                (uint16_t)damage.width, (uint16_t)damage.height};

            result = astra_window_present_region(&window,
                                                                &region);
            if (result != ASTRA_OK) {
                (void)astra_log_failure("gallery window present",
                                        (uint32_t)(-result));
                return GALLERY_FAIL_WINDOW;
            }
        }
        astra_interface_ui_damage_clear(&ui);
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *gui;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *clipboard;
    uint32_t status;

    if (!astra_startup_validate(startup) || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    clipboard = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_CLIPBOARD);
    if (gui == NULL || clipboard == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_STATUS_OK)
        status = GALLERY_FAIL_LIBRARY;
    if (status == ASTRA_STATUS_OK)
        status = build_controls();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_layouts();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_undo();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_text_model();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_segmented();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_tab();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_stepper();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_dial();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_disclosure();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_splitter();
    if (status == ASTRA_STATUS_OK)
        status = create_window(gui->handle);
    if (status == ASTRA_STATUS_OK)
        astra_interface_ui_damage_clear(&ui);
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        (void)astra_close(bootstrap->handle);
    }
    if (status == ASTRA_STATUS_OK)
        status = run(clipboard->handle);
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult close_result = astra_window_close(&window);
        (void)close_result;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_process_filesystem_close(&process_filesystem);
    return (int)status;
}
