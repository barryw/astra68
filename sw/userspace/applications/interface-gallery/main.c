#include <astra/control.h>
#include <astra/clipboard_service.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/shared_library.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>

#define GALLERY_WIDTH 1120u
#define GALLERY_HEIGHT 640u
#define GALLERY_CONTROL_COUNT 31u
#define GALLERY_BENCHMARK_CONTROL_MAX 256u
#define GALLERY_UNDO_BENCHMARK_OPERATIONS 10000u
#define GALLERY_UNDO_ARENA_BYTES (1024u * 1024u)
#define GALLERY_TEXT_BENCHMARK_OPERATIONS 4096u

enum {
    GALLERY_LABEL = 1u,
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
    GALLERY_PROGRESS_LABEL,
    GALLERY_PROGRESS,
    GALLERY_PROGRESS_VALUE,
    GALLERY_PROGRESS_UNKNOWN,
    GALLERY_PROGRESS_UNKNOWN_VALUE,
    GALLERY_FIELD_LABEL,
    GALLERY_FIELD,
    GALLERY_FIELD_ERROR,
    GALLERY_FIELD_ERROR_TEXT,
    GALLERY_FIELD_READ_ONLY
};

enum {
    GALLERY_FAIL_LIBRARY = ASTRA_STATUS_PROGRAM_FIRST,
    GALLERY_FAIL_CONTROL,
    GALLERY_FAIL_SURFACE,
    GALLERY_FAIL_WINDOW,
    GALLERY_FAIL_RENDER
};

ASTRA_PROGRAM("interface-gallery", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraLibraryHandle *graphics_handle;
static AstraLibraryHandle *interface_handle;
static const AstraGraphicsLibraryV2 *graphics_library;
static const AstraInterfaceLibraryV2 *interface_library;
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
static uint8_t field_content[4096];
static _Alignas(4) uint8_t field_metadata[8192];
static AstraTextModel field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t error_field_content[4096];
static _Alignas(4) uint8_t error_field_metadata[8192];
static AstraTextModel error_field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t read_only_field_content[4096];
static _Alignas(4) uint8_t read_only_field_metadata[8192];
static AstraTextModel read_only_field_model = ASTRA_TEXT_MODEL_INIT;
static char clipboard_scratch[4096];

static uint16_t rgb565(AstraColorRGBA8 value)
{
    return graphics_library->rgb565(value.red, value.green, value.blue);
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

static uint32_t add_label(uint32_t index, uint32_t id, const char *text,
                          uint32_t length, uint32_t role)
{
    AstraLabelInfo info = ASTRA_LABEL_INFO_INIT;

    info.id = id;
    info.text = text;
    info.text_length = length;
    info.text_role = role;
    return interface_library->label_init(&controls[index], &info) == ASTRA_OK ?
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
    return interface_library->button_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
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
        result = interface_library->checkbox_init(&controls[index], &info);
    else if (kind == ASTRA_CONTROL_RADIO)
        result = interface_library->radio_init(&controls[index], &info);
    else
        result = interface_library->switch_init(&controls[index], &info);
    return result == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_slider(uint32_t index, uint32_t id, int32_t value)
{
    AstraRangeInfo info = ASTRA_RANGE_INFO_INIT;

    info.id = id;
    info.value = value;
    return interface_library->slider_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_progress(uint32_t index, uint32_t id,
                             uint32_t value, uint32_t maximum)
{
    AstraProgressInfo info = ASTRA_PROGRESS_INFO_INIT;

    info.id = id;
    info.value = value;
    info.maximum = maximum;
    return interface_library->progress_init(&controls[index], &info) ==
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
    return interface_library->field_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
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
    return interface_library->text_model_init(model, &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t format_percent(char out[5], int32_t value)
{
    uint32_t at = 0u;

    if (value >= 100) out[at++] = '1';
    if (value >= 10) out[at++] = (char)('0' + value / 10 % 10);
    out[at++] = (char)('0' + value % 10);
    out[at++] = '%';
    out[at] = '\0';
    return at;
}

static AstraControl *field_control(uint32_t id)
{
    if (id == GALLERY_FIELD) return &controls[27];
    if (id == GALLERY_FIELD_ERROR) return &controls[28];
    if (id == GALLERY_FIELD_READ_ONLY) return &controls[30];
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

    result = interface_library->text_model_get_state(model, &state);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    if (start == end) return ASTRA_STATUS_OK;
    if (cut) {
        AstraTextModelRequirements requirements =
            ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;

        result = interface_library->text_model_replace_requirements(
            model, start, end, NULL, 0u, &requirements);
        if (result != ASTRA_OK ||
            requirements.content_arena_bytes > state.content_arena_bytes ||
            requirements.metadata_arena_bytes > state.metadata_arena_bytes)
            return GALLERY_FAIL_CONTROL;
    }
    result = interface_library->text_model_copy(
        model, start, end, clipboard_scratch, sizeof(clipboard_scratch),
        &bytes);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    representation.type = ASTRA_CLIPBOARD_TYPE_UTF8;
    representation.type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
    representation.data = clipboard_scratch;
    representation.data_length = bytes;
    result = interface_library->clipboard_write(
        clipboard, &representation, 1u, NULL);
    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    if (cut && interface_library->field_replace_selection(
                   &ui, control, NULL, 0u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    return ASTRA_STATUS_OK;
}

static uint32_t paste_field(AstraHandle clipboard, AstraControl *control)
{
    AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
    const void *text;
    uint32_t bytes;
    AstraResult result = interface_library->clipboard_read(clipboard, &item);
    AstraResult close_result;

    if (result != ASTRA_OK) return GALLERY_FAIL_CONTROL;
    result = interface_library->clipboard_item_find(
        &item, ASTRA_CLIPBOARD_TYPE_UTF8,
        sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u, &text, &bytes);
    if (result == ASTRA_OK)
        result = interface_library->field_replace_selection(
            &ui, control, text, bytes);
    close_result = interface_library->clipboard_item_close(&item);
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

static uint32_t set_break_after(uint32_t index)
{
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

    item.flags = ASTRA_FLEX_BREAK_AFTER;
    return interface_library->control_set_flex(&controls[index], &item) ==
            ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t layout_controls(void)
{
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;

    layout.flags = ASTRA_FLEX_WRAP;
    layout.padding_left = 24u;
    layout.padding_top = 24u;
    layout.padding_right = 24u;
    layout.padding_bottom = 24u;
    layout.main_gap = 12u;
    layout.cross_gap = 20u;
    return interface_library->ui_layout(&ui, &layout) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t build_controls(void)
{
    uint32_t status;

    status = add_label(0u, GALLERY_LABEL,
                       "BUTTON / STATES AND SEMANTIC VARIANTS",
                       sizeof("BUTTON / STATES AND SEMANTIC VARIANTS") - 1u,
                       ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_button(1u, GALLERY_APPLY, "Apply", 5u,
                            ASTRA_BUTTON_PRIMARY, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(2u, GALLERY_REVERT, "Revert", 6u,
                            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(3u, GALLERY_HOVER, "Hover", 5u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_HOVERED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(4u, GALLERY_PRESSED, "Pressed", 7u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_PRESSED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(5u, GALLERY_FOCUSED, "Focused", 7u,
                            ASTRA_BUTTON_STANDARD, 0u,
                            ASTRA_CONTROL_FOCUSED);
    if (status == ASTRA_STATUS_OK)
        status = add_button(6u, GALLERY_DISABLED, "Disabled", 8u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_DISABLED, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(7u, GALLERY_SELECTED, "Selected", 8u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_SELECTED, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(8u, GALLERY_ERROR, "Error", 5u,
                            ASTRA_BUTTON_STANDARD, ASTRA_CONTROL_ERROR, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(9u, GALLERY_WARNING, "Pending", 7u,
                            ASTRA_BUTTON_WARNING, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(10u, GALLERY_DESTRUCTIVE, "Erase volume", 12u,
                            ASTRA_BUTTON_DESTRUCTIVE, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(11u, GALLERY_HELP,
                           "Tab and Shift-Tab move focus. Enter or Space activates.",
                           sizeof("Tab and Shift-Tab move focus. Enter or Space activates.") - 1u,
                           ASTRA_TEXT_CLIENT_TERTIARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(12u, GALLERY_TOGGLE_LABEL,
                           "CHECK / RADIO / SWITCH",
                           sizeof("CHECK / RADIO / SWITCH") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(13u, GALLERY_CHECKBOX, "Snap to grid",
                            sizeof("Snap to grid") - 1u,
                            ASTRA_CONTROL_CHECKBOX, 0u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(14u, GALLERY_RADIO_565, "RGB565",
                            sizeof("RGB565") - 1u,
                            ASTRA_CONTROL_RADIO, 1u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(15u, GALLERY_RADIO_8888, "XRGB8888",
                            sizeof("XRGB8888") - 1u,
                            ASTRA_CONTROL_RADIO, 1u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(16u, GALLERY_SWITCH, "Tear-free",
                            sizeof("Tear-free") - 1u,
                            ASTRA_CONTROL_SWITCH, 0u,
                            ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(17u, GALLERY_TOGGLE_DISABLED, "Unavailable",
                            sizeof("Unavailable") - 1u,
                            ASTRA_CONTROL_CHECKBOX, 0u,
                            ASTRA_CONTROL_DISABLED);
    if (status == ASTRA_STATUS_OK)
        status = add_label(18u, GALLERY_SLIDER_LABEL, "SLIDER",
                           sizeof("SLIDER") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_slider(19u, GALLERY_SLIDER, 62);
    if (status == ASTRA_STATUS_OK)
        status = add_label(20u, GALLERY_SLIDER_VALUE, slider_value_text,
                           sizeof("62%") - 1u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(21u, GALLERY_PROGRESS_LABEL, "PROGRESS",
                           sizeof("PROGRESS") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_progress(22u, GALLERY_PROGRESS, 44u, 100u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(23u, GALLERY_PROGRESS_VALUE, "44%",
                           sizeof("44%") - 1u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = add_progress(24u, GALLERY_PROGRESS_UNKNOWN, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(25u, GALLERY_PROGRESS_UNKNOWN_VALUE, "Working",
                           sizeof("Working") - 1u,
                           ASTRA_TEXT_CLIENT_PRIMARY);
    if (status == ASTRA_STATUS_OK)
        status = init_field_model(
            &field_model, "HOME:/projects/astra68",
            sizeof("HOME:/projects/astra68") - 1u,
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
        status = add_label(26u, GALLERY_FIELD_LABEL,
                           "FIELD / UTF-8 / SELECTION",
                           sizeof("FIELD / UTF-8 / SELECTION") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_field(27u, GALLERY_FIELD, &field_model, 28u, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_field(28u, GALLERY_FIELD_ERROR, &error_field_model,
                           16u, 0u, ASTRA_CONTROL_ERROR);
    if (status == ASTRA_STATUS_OK)
        status = add_label(29u, GALLERY_FIELD_ERROR_TEXT,
                           "not a valid 32-bit address",
                           sizeof("not a valid 32-bit address") - 1u,
                           ASTRA_TEXT_CLIENT_FAULT);
    if (status == ASTRA_STATUS_OK)
        status = add_field(30u, GALLERY_FIELD_READ_ONLY,
                           &read_only_field_model, 18u,
                           ASTRA_FIELD_READ_ONLY, 0u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(0u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(6u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(10u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(11u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(12u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(17u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(18u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(20u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(21u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(23u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(25u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(26u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(27u);
    if (status == ASTRA_STATUS_OK) status = set_break_after(29u);
    if (status != ASTRA_STATUS_OK)
        return status;
    if (interface_library->ui_init(&ui, controls, GALLERY_CONTROL_COUNT,
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
        if (interface_library->container_init(&benchmark_controls[0], &info) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        item.grow = 1u;
        if (interface_library->control_set_flex(
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
        if (interface_library->button_init(&benchmark_controls[at], &info) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        item.parent_id = 1u;
        item.grow = at % 3u + 1u;
        if (interface_library->control_set_flex(
                &benchmark_controls[at], &item) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    if (interface_library->ui_init(&context, benchmark_controls, count,
                                   1120u, 800u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    if (interface_library->ui_layout(&context, &root) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
        AstraWindowEvent event = {0};

        event.size = sizeof(event);
        event.version = ASTRA_WINDOW_EVENT_VERSION;
        event.type = ASTRA_WINDOW_EVENT_FRAME;
        event.data.frame.frame.width =
            (iteration & 1u) != 0u ? 960u : 1120u;
        event.data.frame.frame.height = 800u;
        if (interface_library->ui_handle_event(
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
    if (interface_library->undo_init(&manager, &manager_info) != ASTRA_OK)
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
        if (interface_library->undo_perform_group(&manager, &group) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    record_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != GALLERY_UNDO_BENCHMARK_OPERATIONS)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u;
         index < GALLERY_UNDO_BENCHMARK_OPERATIONS; ++index)
        if (interface_library->undo_undo(&manager) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    undo_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != 0u)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u;
         index < GALLERY_UNDO_BENCHMARK_OPERATIONS; ++index)
        if (interface_library->undo_redo(&manager) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    redo_elapsed = astra_clock_monotonic() - started;
    if (undo_benchmark_value != GALLERY_UNDO_BENCHMARK_OPERATIONS ||
        interface_library->undo_get_state(&manager, &state) != ASTRA_OK ||
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
    if (interface_library->text_model_init(&model, &info) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_TEXT_BENCHMARK_OPERATIONS; ++at)
        if (interface_library->text_model_replace(
                &model, at, at, "x", 1u) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    append_elapsed = astra_clock_monotonic() - started;
    interface_library->text_model_dispose(&model);

    if (interface_library->text_model_init(&model, &info) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < GALLERY_TEXT_BENCHMARK_OPERATIONS; ++at) {
        uint32_t position = (at & 1u) == 0u ? 0u : at;

        if (interface_library->text_model_replace(
                &model, position, position, "x", 1u) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
    fragmented_elapsed = astra_clock_monotonic() - started;
    if (interface_library->text_model_get_state(&model, &state) != ASTRA_OK ||
        interface_library->text_model_validate(&model) != ASTRA_OK ||
        state.text_bytes != GALLERY_TEXT_BENCHMARK_OPERATIONS)
        return GALLERY_FAIL_CONTROL;
    report_text(append_elapsed, fragmented_elapsed, state.piece_count);
    interface_library->text_model_dispose(&model);
    return ASTRA_STATUS_OK;
}

static uint32_t paint(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    graphics_library->clear(&surface.view, rgb565(theme.client));
    return interface_library->ui_render(&ui, &surface.view) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_RENDER;
}

static uint32_t load_libraries(void)
{
    graphics_handle = OpenLibrary(ASTRA_GRAPHICS_LIBRARY_NAME,
                                  ASTRA_GRAPHICS_LIBRARY_VERSION);
    interface_handle = OpenLibrary(ASTRA_INTERFACE_LIBRARY_NAME,
                                   ASTRA_INTERFACE_LIBRARY_VERSION);
    if (graphics_handle == NULL || interface_handle == NULL)
        return GALLERY_FAIL_LIBRARY;
    graphics_library = graphics_handle->exports;
    interface_library = interface_handle->exports;
    if (graphics_library == NULL || interface_library == NULL ||
        graphics_library->abi_major != ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR ||
        graphics_library->structure_size < sizeof(*graphics_library) ||
        !astra_interface_library_supports(
            interface_library, 6u, ASTRA_INTERFACE_LIBRARY_2_6_SIZE))
        return GALLERY_FAIL_LIBRARY;
    return ASTRA_STATUS_OK;
}

static uint32_t create_window(AstraHandle gui)
{
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result;

    if (graphics_library->shared_draw_list_create(
            &surface, GALLERY_WIDTH, GALLERY_HEIGHT) != ASTRA_SYSCALL_OK)
        return GALLERY_FAIL_SURFACE;
    if (paint() != ASTRA_STATUS_OK)
        return GALLERY_FAIL_RENDER;
    info.flags = ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE;
    info.x = 400u;
    info.y = 220u;
    info.width = GALLERY_WIDTH;
    info.height = GALLERY_HEIGHT;
    info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    info.type = ASTRA_WINDOW_STANDARD;
    info.title = "Interface Gallery";
    info.title_length = 17u;
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                      ASTRA_WINDOW_SUBSCRIBE_KEY |
                      ASTRA_WINDOW_SUBSCRIBE_TEXT;
    result = interface_library->window_create(gui, surface.area, &info,
                                               &window);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery window create",
                                (uint32_t)(-result));
        return GALLERY_FAIL_WINDOW;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t run(AstraHandle clipboard)
{
    for (;;) {
        AstraWindowEvent event = {0};
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        AstraControlFrame damage;
        uint64_t now = astra_clock_monotonic();
        AstraMonotonicDeadline deadline =
            now <= (uint64_t)ASTRA_DEADLINE_INFINITE -
                       ASTRA_UI_ANIMATION_INTERVAL_NS ?
                (AstraMonotonicDeadline)(now +
                                         ASTRA_UI_ANIMATION_INTERVAL_NS) :
                ASTRA_DEADLINE_INFINITE;
        AstraResult result;

        if (interface_library->ui_tick(&ui, now) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        result = interface_library->window_event_wait(&window, &event,
                                                       deadline);
        if (result != ASTRA_OK && result != ASTRA_ERROR_TIMEOUT) {
            (void)astra_log_failure("gallery window event wait",
                                    (uint32_t)(-result));
            return GALLERY_FAIL_WINDOW;
        }
        if (interface_library->ui_tick(&ui, astra_clock_monotonic()) !=
            ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        if (result == ASTRA_OK &&
            event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST)
            return ASTRA_STATUS_OK;
        if (result == ASTRA_OK &&
            interface_library->ui_handle_event(&ui, &event, &action) !=
                ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        if (action.type == ASTRA_UI_ACTION_VALUE_CHANGED &&
            action.control_id == GALLERY_SLIDER) {
            uint32_t length = format_percent(slider_value_text, action.value);

            if (interface_library->control_set_text(
                    &ui, &controls[20], slider_value_text, length) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
        }
        if (handle_field_action(clipboard, &action) != ASTRA_STATUS_OK)
            (void)astra_log("interface gallery clipboard action failed");
        if (event.type == ASTRA_WINDOW_EVENT_FRAME &&
            event.data.frame.frame.width != 0u &&
            event.data.frame.frame.height != 0u &&
            (surface.view.width != event.data.frame.frame.width ||
             surface.view.height != event.data.frame.frame.height)) {
            if (!graphics_library->draw_list_view_init(
                    &surface.view, surface.view.pixels, surface.view.byte_size,
                    event.data.frame.frame.width,
                    event.data.frame.frame.height) ||
                paint() != ASTRA_STATUS_OK ||
                interface_library->window_present(&window) != ASTRA_OK)
                return GALLERY_FAIL_RENDER;
            interface_library->ui_damage_clear(&ui);
            continue;
        }
        if (interface_library->ui_damage(&ui, &damage) != ASTRA_OK)
            continue;
        if (!graphics_library->draw_list_view_init(
                &surface.view, surface.view.pixels, surface.view.byte_size,
                surface.view.width, surface.view.height) ||
            paint() != ASTRA_STATUS_OK)
            return GALLERY_FAIL_RENDER;
        {
            AstraWindowFrame region = {
                (uint16_t)damage.x, (uint16_t)damage.y,
                (uint16_t)damage.width, (uint16_t)damage.height};

            result = interface_library->window_present_region(&window,
                                                                &region);
            if (result != ASTRA_OK) {
                (void)astra_log_failure("gallery window present",
                                        (uint32_t)(-result));
                return GALLERY_FAIL_WINDOW;
            }
        }
        interface_library->ui_damage_clear(&ui);
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
        status = load_libraries();
    if (status == ASTRA_STATUS_OK)
        status = build_controls();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_layouts();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_undo();
    if (status == ASTRA_STATUS_OK)
        status = benchmark_text_model();
    if (status == ASTRA_STATUS_OK)
        status = create_window(gui->handle);
    if (status == ASTRA_STATUS_OK)
        interface_library->ui_damage_clear(&ui);
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        (void)astra_close(bootstrap->handle);
    }
    if (status == ASTRA_STATUS_OK)
        status = run(clipboard->handle);
    if (window._private_control != ASTRA_INVALID_HANDLE)
        (void)interface_library->window_close(&window);
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)graphics_library->shared_surface_close(&surface);
    if (interface_handle != NULL) CloseLibrary(interface_handle);
    if (graphics_handle != NULL) CloseLibrary(graphics_handle);
    astra_process_filesystem_close(&process_filesystem);
    return (int)status;
}
