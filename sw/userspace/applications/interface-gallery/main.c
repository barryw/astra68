#include <astra/control.h>
#include <astra/clipboard.h>
#include <astra/fonts.h>
#include <astra/clipboard_service.h>
#include <astra/graphics_kit.h>
#include <astra/graphics_library.h>
#include <astra/icon_tile.h>
#include <astra/interface_kit.h>
#include <astra/interface_library.h>
#include <astra/image_view.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/window.h>
#include <astra/string.h>
#include <astra/system_panels.h>
#include <string.h>

#define GALLERY_WIDTH 1120u
#define GALLERY_HEIGHT 800u
#define GALLERY_TEXT_VIEW_HEIGHT 420u
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
    GALLERY_PAGE_SCROLL_INPUT,
    GALLERY_PAGE_SCROLL_CHOICE,
    GALLERY_PAGE_SCROLL_VALUE,
    GALLERY_PAGE_SCROLL_PROGRESS,
    GALLERY_PAGE_SCROLL_TEXT,
    GALLERY_PAGE_SCROLL_LAYOUT,
    GALLERY_PAGE_SCROLL_COLLECTIONS,
    GALLERY_PAGE_SCROLL_FIELDS,
    GALLERY_PAGE_SCROLL_PANELS,
    GALLERY_PAGE_SCROLL_DRAG,
    GALLERY_PAGE_INPUT,
    GALLERY_PAGE_CHOICE,
    GALLERY_PAGE_VALUE,
    GALLERY_PAGE_PROGRESS,
    GALLERY_PAGE_TEXT,
    GALLERY_PAGE_LAYOUT,
    GALLERY_PAGE_COLLECTIONS,
    GALLERY_PAGE_FIELDS,
    GALLERY_PAGE_PANELS,
    GALLERY_PAGE_DRAG,
    GALLERY_TEXT_LABEL,
    GALLERY_LAYOUT_LABEL,
    GALLERY_COLLECTIONS_LABEL,
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
    GALLERY_LIST_LABEL,
    GALLERY_LIST,
    GALLERY_TEXT_VIEW_SCROLL,
    GALLERY_TEXT_VIEW,
    GALLERY_TEXT_VIEW_BAR,
    GALLERY_DISPLAY_LABEL,
    GALLERY_LEVEL_METER,
    GALLERY_SPARKLINE,
    GALLERY_STATUS_BAR,
    GALLERY_STATUS_LEFT,
    GALLERY_STATUS_RIGHT,
    GALLERY_FORM_ROW,
    GALLERY_FORM_LABEL,
    GALLERY_FORM_CONTENT,
    GALLERY_FORM_VALUE,
    GALLERY_FORM_HELP,
    GALLERY_FORM_OPTION,
    GALLERY_POPUP_LABEL,
    GALLERY_POPUP,
    GALLERY_COLUMNS,
    GALLERY_COLUMN_LEFT_SCROLL,
    GALLERY_COLUMN_LEFT_LIST,
    GALLERY_COLUMN_RIGHT_SCROLL,
    GALLERY_COLUMN_RIGHT_LIST,
    GALLERY_TABLE,
    GALLERY_FIELD_FAMILY_LABEL,
    GALLERY_SECURE_FIELD,
    GALLERY_SEARCH_FIELD,
    GALLERY_SEGMENTED_FIELD,
    GALLERY_SEGMENT_LEFT,
    GALLERY_SEGMENT_SEPARATOR,
    GALLERY_SEGMENT_RIGHT,
    GALLERY_TAG_INPUT,
    GALLERY_TAG_ONE,
    GALLERY_TAG_TWO,
    GALLERY_TAG_DRAFT,
    GALLERY_COLOUR_WELL,
    GALLERY_TOOLBAR_ROOT,
    GALLERY_TOOLBAR_NAV,
    GALLERY_TOOLBAR_BACK,
    GALLERY_TOOLBAR_FORWARD,
    GALLERY_TOOLBAR_FILE,
    GALLERY_TOOLBAR_OPEN,
    GALLERY_TOOLBAR_REMOVE,
    GALLERY_TOOLBAR_TRASH,
    GALLERY_TOOLBAR_MORE,
    GALLERY_COMBO_ROOT,
    GALLERY_COMBO_FIELD,
    GALLERY_COMBO_MENU,
    GALLERY_MULTI_POPUP,
    GALLERY_TREE,
    GALLERY_PAGINATION,
    GALLERY_PAGINATION_PREVIOUS,
    GALLERY_PAGINATION_LABEL,
    GALLERY_PAGINATION_NEXT,
    GALLERY_CUSTOM_VIEW,
    GALLERY_IMAGE_VIEW,
    GALLERY_DRAG_HEADING,
    GALLERY_DRAG_SOURCE,
    GALLERY_DRAG_WORK,
    GALLERY_DRAG_WORK_LABEL,
    GALLERY_DRAG_SYSTEM,
    GALLERY_DRAG_SYSTEM_LABEL,
    GALLERY_DRAG_RESULT,
    GALLERY_SPLIT_BUTTON_ROW,
    GALLERY_SPLIT_PRIMARY,
    GALLERY_SPLIT_MENU,
    GALLERY_ICON_GRID_LABEL,
    GALLERY_ICON_GRID,
    GALLERY_GRID_VIEW_LABEL,
    GALLERY_GRID_VIEW,
    GALLERY_PANEL_DEMOS_LABEL,
    GALLERY_PANEL_ALERT,
    GALLERY_PANEL_TOOLTIP,
    GALLERY_PANEL_NOTIFICATION,
    GALLERY_PANEL_MENU,
    GALLERY_PANEL_SHEET,
    GALLERY_PANEL_RESULT,
    GALLERY_PANEL_COLOUR_BUTTON,
    GALLERY_PANEL_OPEN_BUTTON,
    GALLERY_PANEL_SAVE_BUTTON,
    GALLERY_PANEL_FONT_BUTTON,
    GALLERY_COLOUR_PANEL_ROOT,
    GALLERY_COLOUR_PANEL_TITLE,
    GALLERY_COLOUR_PANEL_TABS,
    GALLERY_COLOUR_PANEL_SWATCHES,
    GALLERY_COLOUR_PANEL_WANTED,
    GALLERY_COLOUR_PANEL_SWATCH,
    GALLERY_COLOUR_PANEL_DIFFERENCE,
    GALLERY_COLOUR_PANEL_SLIDERS,
    GALLERY_COLOUR_PANEL_RED_LABEL,
    GALLERY_COLOUR_PANEL_RED,
    GALLERY_COLOUR_PANEL_GREEN_LABEL,
    GALLERY_COLOUR_PANEL_GREEN,
    GALLERY_COLOUR_PANEL_BLUE_LABEL,
    GALLERY_COLOUR_PANEL_BLUE,
    GALLERY_COLOUR_PANEL_FIELD_PAGE,
    GALLERY_COLOUR_PANEL_HEX_FIELD,
    GALLERY_COLOUR_PANEL_STORED_VALUE,
    GALLERY_COLOUR_PANEL_THEME_PAGE,
    GALLERY_COLOUR_PANEL_THEME_LIST,
    GALLERY_COLOUR_PANEL_RECENT_PAGE,
    GALLERY_COLOUR_PANEL_RECENT_LIST,
    GALLERY_COLOUR_PANEL_ACTIONS,
    GALLERY_COLOUR_PANEL_CANCEL,
    GALLERY_COLOUR_PANEL_USE,
    GALLERY_OPEN_PANEL_ROOT,
    GALLERY_SAVE_PANEL_ROOT = GALLERY_OPEN_PANEL_ROOT +
        ASTRA_FILE_PANEL_CONTROL_COUNT,
    GALLERY_FONT_PANEL_ROOT = GALLERY_SAVE_PANEL_ROOT +
        ASTRA_FILE_PANEL_CONTROL_COUNT,
    GALLERY_CONTROL_COUNT = GALLERY_FONT_PANEL_ROOT +
        ASTRA_FONT_PANEL_CONTROL_COUNT - 1u
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
static AstraSharedSurface gallery_sheet_surface;
static AstraWindow gallery_sheet_window = ASTRA_WINDOW_INIT;
static AstraControl gallery_sheet_controls[2];
static AstraUIContext gallery_sheet_ui = ASTRA_UI_CONTEXT_INIT;
static uint32_t gallery_sheet_open;
static AstraControl controls[GALLERY_CONTROL_COUNT];
static AstraControl benchmark_controls[GALLERY_BENCHMARK_CONTROL_MAX];
static _Alignas(4) uint8_t undo_benchmark_arena[GALLERY_UNDO_ARENA_BYTES];
static uint8_t text_benchmark_content[16384];
static _Alignas(4) uint8_t text_benchmark_metadata[131072];
static AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
static AstraPopup gallery_popup = ASTRA_POPUP_INIT;
static AstraComboBox gallery_combo = ASTRA_COMBO_BOX_INIT;
static AstraMultiPopup gallery_multi = ASTRA_MULTI_POPUP_INIT;
static AstraToolbarActions gallery_toolbar = ASTRA_TOOLBAR_ACTIONS_INIT;
static AstraColourPanel gallery_colour_panel = ASTRA_COLOUR_PANEL_INIT;
static AstraFilePanel gallery_open_panel = ASTRA_FILE_PANEL_INIT;
static AstraFilePanel gallery_save_panel = ASTRA_FILE_PANEL_INIT;
static AstraFontPanel gallery_font_panel = ASTRA_FONT_PANEL_INIT;
static AstraFontsCatalog gallery_font_catalog = ASTRA_FONTS_CATALOG_INIT;
static AstraDirectoryEntry gallery_file_entries[3] = {
    {.name = "Readme.txt", .kind = ASTRA_VFS_KIND_FILE},
    {.name = "Sketch.afnt", .kind = ASTRA_VFS_KIND_FILE},
    {.name = "Story.z5", .kind = ASTRA_VFS_KIND_FILE}
};
static AstraCollectionItem gallery_file_items[2][3];
static AstraCollectionItem gallery_file_spares[2][3];
static const AstraSurface *gallery_file_icons[2][3];
static AstraCollectionModel gallery_file_collections[2];
static AstraScrollModel gallery_file_scrolls[2];
static AstraTextModel gallery_save_name = ASTRA_TEXT_MODEL_INIT;
static uint8_t gallery_save_name_content[256];
static _Alignas(4) uint8_t gallery_save_name_metadata[512];
static AstraFontPanelFaceRow gallery_font_faces[128];
static AstraCollectionItem gallery_font_face_items[128];
static AstraFontPanelStrikeRow gallery_font_strikes[128];
static AstraFontPanelStrikeRow gallery_font_strikes_spare[128];
static AstraCollectionItem gallery_font_strike_items[128];
static AstraCollectionItem gallery_font_strike_items_spare[128];
static AstraCollectionModel gallery_font_face_collection;
static AstraCollectionModel gallery_font_strike_collection;
static AstraScrollModel gallery_font_face_scroll;
static AstraScrollModel gallery_font_strike_scroll;
static uint32_t gallery_visible_panel = GALLERY_COLOUR_PANEL_ROOT;
static AstraSurface gallery_image_source = ASTRA_SURFACE_INIT;
static AstraImageViewModel gallery_image_model;
static AstraCollectionModel gallery_icon_collection = ASTRA_COLLECTION_MODEL_INIT;
static AstraIconGridModel gallery_icon_model = ASTRA_ICON_GRID_MODEL_INIT;
static AstraSurface gallery_icon_sources[3] = {
    ASTRA_SURFACE_INIT, ASTRA_SURFACE_INIT, ASTRA_SURFACE_INIT
};
static const AstraSurface *gallery_icon_surfaces[3];
static AstraGridViewModel gallery_grid_model = ASTRA_GRID_VIEW_MODEL_INIT;
static AstraTextCell gallery_grid_cells[32];
static AstraHandle gallery_gui;
static uint32_t gallery_view_phase;
static AstraViewModel gallery_custom_model = ASTRA_VIEW_MODEL_INIT;
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static char slider_value_text[5] = "62%";
static char dial_value_text[22] = "0";
static uint8_t field_content[4096];
static _Alignas(4) uint8_t field_metadata[8192];
static AstraTextModel field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t combo_content[1024];
static _Alignas(4) uint8_t combo_metadata[2048];
static AstraTextModel combo_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t error_field_content[4096];
static _Alignas(4) uint8_t error_field_metadata[8192];
static AstraTextModel error_field_model = ASTRA_TEXT_MODEL_INIT;
static uint8_t read_only_field_content[4096];
static _Alignas(4) uint8_t read_only_field_metadata[8192];
static AstraTextModel read_only_field_model = ASTRA_TEXT_MODEL_INIT;
static AstraScrollModel gallery_scroll_model = ASTRA_SCROLL_MODEL_INIT;
static AstraScrollModel page_scroll_models[10];
static AstraScrollModel column_scrolls[2] = {
    ASTRA_SCROLL_MODEL_INIT, ASTRA_SCROLL_MODEL_INIT
};
static AstraCollectionModel column_models[2] = {
    ASTRA_COLLECTION_MODEL_INIT, ASTRA_COLLECTION_MODEL_INIT
};
static const AstraCollectionItem column_roots[] = {
    {1u, "Applications", 12u, {0u, 0u}},
    {2u, "Documents", 9u, {0u, 0u}},
    {3u, "Pictures", 8u, {0u, 0u}}
};
static const AstraCollectionItem column_apps[] = {
    {11u, "Doom", 4u, {0u, 0u}},
    {12u, "Quake", 5u, {0u, 0u}},
    {13u, "AstraZ", 6u, {0u, 0u}}
};
static AstraCollectionModel gallery_table_collection = ASTRA_COLLECTION_MODEL_INIT;
static AstraTableModel gallery_table_model = ASTRA_TABLE_MODEL_INIT;
static const AstraTableColumn gallery_table_columns[] = {
    {"Name", 4u, 220u, 0u},
    {"Kind", 4u, 120u, 0u}
};
static const AstraTableCell gallery_table_cells[] = {
    {"Game", 4u, 0u},
    {"Game", 4u, 0u},
    {"Story", 5u, 0u}
};
static const AstraCollectionItem gallery_tree_items[] = {
    {101u, "Applications", 12u, {0u, 0u}},
    {102u, "Games", 5u, {0u, 0u}},
    {103u, "Doom", 4u, {0u, 0u}},
    {104u, "Documents", 9u, {0u, 0u}}
};
static AstraTreeNode gallery_tree_nodes[] = {
    {0u, 1u, 0u}, {101u, 1u, 0u},
    {102u, 0u, 0u}, {0u, 0u, 0u}
};
static uint32_t gallery_tree_visible[4];
static AstraCollectionModel gallery_tree_collection = ASTRA_COLLECTION_MODEL_INIT;
static AstraTreeModel gallery_tree_model = ASTRA_TREE_MODEL_INIT;
static AstraCollectionModel gallery_pagination_collection =
    ASTRA_COLLECTION_MODEL_INIT;
static AstraPaginationModel gallery_pagination_model =
    ASTRA_PAGINATION_MODEL_INIT;
static uint32_t gallery_meter_values[2] = {62u, 71u};
static uint32_t gallery_meter_peaks[2] = {78u, 88u};
static AstraLevelMeterModel gallery_meter = ASTRA_LEVEL_METER_MODEL_INIT;
static uint32_t gallery_spark_samples[] = {
    30u, 34u, 31u, 60u, 72u, 36u, 30u,
    33u, 29u, 31u, 100u, 32u, 30u, 34u
};
static AstraSparklineModel gallery_spark = ASTRA_SPARKLINE_MODEL_INIT;
static AstraTextModel text_view_model = ASTRA_TEXT_MODEL_INIT;
static AstraUndoManager text_view_undo = ASTRA_UNDO_MANAGER_INIT;
static AstraTextUndoBinding text_view_undo_binding =
    ASTRA_TEXT_UNDO_BINDING_INIT;
static _Alignas(4) uint8_t text_view_undo_history[16384];
static uint8_t text_view_undo_scratch[4096];
static uint32_t text_view_undo_bound;
static AstraTextModel family_models[5] = {
    ASTRA_TEXT_MODEL_INIT, ASTRA_TEXT_MODEL_INIT, ASTRA_TEXT_MODEL_INIT,
    ASTRA_TEXT_MODEL_INIT, ASTRA_TEXT_MODEL_INIT
};
static uint8_t family_content[5][256];
static _Alignas(4) uint8_t family_metadata[5][512];
static const AstraChoiceItem family_tags[] = {
    {"vega", 4u, {0u, 0u}}, {"rtl", 3u, {0u, 0u}}
};
static AstraScrollModel text_view_scroll = ASTRA_SCROLL_MODEL_INIT;
static uint8_t text_view_content[2048];
static _Alignas(4) uint8_t text_view_metadata[2048];
static const char text_view_example[] =
    "Shared Text View — Flow Mode\n"
    "\n"
    "Click here to edit this document. Text remains selectable and copyable.\n"
    "TextModel owns UTF-8, lines and selection.\n"
    "TextSurface emits hardware glyph commands.\n"
    "ScrollView owns clipping and scrolling.\n"
    "\n"
    "Keyboard\tAction\r\n"
    "Arrows\tMove selection\n"
    "Shift\tExtend selection\n"
    "Home / End\tStart / end of line\n"
    "Ctrl+Home / End\tStart / end of document\n"
    "Page Up / Down\tMove through visible lines\n"
    "Meta+A / C\tSelect all / Copy\n"
    "Meta+Z\tUndo edit\n"
    "Meta+Shift+Z\tRedo edit\n"
    "\n"
    "Unicode: \xce\xa9 \xe4\xb8\x96\xe7\x95\x8c\n"
    "Long lines keep their original bytes; horizontal scrolling reveals text beyond the viewport.\n"
    "End of shared control example.";
static char clipboard_scratch[4096];
static uint32_t active_page = GALLERY_PAGE_INPUT;
static AstraDragGesture gallery_drag_gesture = ASTRA_DRAG_GESTURE_INIT;
static uint32_t gallery_drag_token;
static uint32_t gallery_drag_visual_target;
static uint32_t gallery_drag_visual_operation;
static uint32_t gallery_drag_last_target;
static const AstraChoiceItem gallery_tabs[] = {
    {"Input", sizeof("Input") - 1u, {0u, 0u}},
    {"Choice", sizeof("Choice") - 1u, {0u, 0u}},
    {"Value", sizeof("Value") - 1u, {0u, 0u}},
    {"Progress", sizeof("Progress") - 1u, {0u, 0u}},
    {"Text", sizeof("Text") - 1u, {0u, 0u}},
    {"Layout", sizeof("Layout") - 1u, {0u, 0u}},
    {"Collections", sizeof("Collections") - 1u, {0u, 0u}},
    {"Fields", sizeof("Fields") - 1u, {0u, 0u}},
    {"Panels", sizeof("Panels") - 1u, {0u, 0u}},
    {"Drag", sizeof("Drag") - 1u, {0u, 0u}}
};
static const AstraMenuItem gallery_menu_bar[] = {
    {"Gallery", 7u, 0u}, {"Help", 4u, 0u},
    {"Show notification", 17u, ASTRA_MENU_PARENT(1u)},
    {"Show alert", 10u, ASTRA_MENU_PARENT(1u)},
    {"About controls", 14u, ASTRA_MENU_PARENT(2u)}
};
static const AstraMenuItem gallery_context_items[] = {
    {"Open", 4u, 0u}, {"Duplicate", 9u, 0u},
    {NULL, 0u, ASTRA_MENU_SEPARATOR},
    {"Disabled", 8u, ASTRA_MENU_DISABLED},
    {"Show details", 12u, 0u}
};
static const AstraCollectionItem gallery_icon_items[] = {
    {701u, "Doom.app", 8u, {0u, 0u}},
    {702u, "Fonts.app", 9u, {0u, 0u}},
    {703u, "notes.txt", 9u, {0u, 0u}}
};
static const AstraChoiceItem gallery_choices[] = {
    {"Windows", sizeof("Windows") - 1u, {0u, 0u}},
    {"Scenes", sizeof("Scenes") - 1u, {0u, 0u}},
    {"Ports", sizeof("Ports") - 1u, {0u, 0u}}
};
static const AstraCollectionItem gallery_fonts[] = {
    {1u, "Astra Sans", sizeof("Astra Sans") - 1u, {0u, 0u}},
    {2u, "Astra Mono", sizeof("Astra Mono") - 1u, {0u, 0u}},
    {3u, "Rescue Mono", sizeof("Rescue Mono") - 1u, {0u, 0u}}
};
static const AstraToolbarCommand gallery_toolbar_actions[] = {
    {"go.back", 7u, "Back", 4u, ASTRA_TOOLBAR_ICON_BACK, {0u, 0u}},
    {"go.forward", 10u, "Forward", 7u, ASTRA_TOOLBAR_ICON_FORWARD,
        {0u, 0u}},
    {"file.open", 9u, "Open", 4u, ASTRA_TOOLBAR_ICON_OPEN, {0u, 0u}},
    {"file.trash", 10u, "Trash", 5u, ASTRA_TOOLBAR_ICON_TRASH,
        {0u, 0u}}
};
static const AstraToolbarGroup gallery_toolbar_groups[] = {
    {"Navigate", 8u, 0u, 2u, 0u, ASTRA_TOOLBAR_GROUP_PINNED, {0u, 0u}},
    {"File", 4u, 2u, 1u, 1u, 0u, {0u, 0u}},
    {"Remove", 6u, 3u, 1u, 2u, 0u, {0u, 0u}}
};

static AstraResult gallery_toolbar_query(void *context,
    AstraCommandState *state)
{
    const AstraToolbarCommand *action = context;
    *state = (AstraCommandState){
        action->label, action->label_length, 1u, 0u};
    return ASTRA_OK;
}

static AstraResult gallery_toolbar_invoke(void *context,
    const AstraCommandArguments *arguments)
{
    const AstraToolbarCommand *action = context;
    (void)arguments;
    (void)astra_log(action->label);
    return ASTRA_OK;
}

static const AstraCommand gallery_toolbar_commands[] = {
    {"go.back", 7u, 0u, 0u, gallery_toolbar_query,
        gallery_toolbar_invoke, (void *)&gallery_toolbar_actions[0]},
    {"go.forward", 10u, 0u, 0u, gallery_toolbar_query,
        gallery_toolbar_invoke, (void *)&gallery_toolbar_actions[1]},
    {"file.open", 9u, 0u, 0u, gallery_toolbar_query,
        gallery_toolbar_invoke, (void *)&gallery_toolbar_actions[2]},
    {"file.trash", 10u, 0u, 0u, gallery_toolbar_query,
        gallery_toolbar_invoke, (void *)&gallery_toolbar_actions[3]}
};
static const AstraCommandScope gallery_toolbar_scope = {
    NULL, gallery_toolbar_commands, 4u};
static uint8_t gallery_multi_checked[3] = {1u, 0u, 1u};
static const char *const gallery_multi_labels[3] = {
    "Show toolbar", "Show labels", "Show status"
};
static const uint32_t gallery_multi_label_lengths[3] = {12u, 11u, 11u};

static AstraResult gallery_multi_query(void *context, AstraCommandState *state)
{
    uint32_t at = (uint32_t)(uintptr_t)context;

    *state = (AstraCommandState){gallery_multi_labels[at],
        gallery_multi_label_lengths[at], 1u, gallery_multi_checked[at]};
    return ASTRA_OK;
}

static AstraResult gallery_multi_invoke(void *context,
    const AstraCommandArguments *arguments)
{
    uint32_t at = (uint32_t)(uintptr_t)context;
    (void)arguments;
    gallery_multi_checked[at] = !gallery_multi_checked[at];
    return ASTRA_OK;
}

static const AstraCommand gallery_multi_commands[3] = {
    {"view.toolbar", 12u, 0u, 0u, gallery_multi_query,
        gallery_multi_invoke, (void *)(uintptr_t)0u},
    {"view.labels", 11u, 0u, 0u, gallery_multi_query,
        gallery_multi_invoke, (void *)(uintptr_t)1u},
    {"view.status", 11u, 0u, 0u, gallery_multi_query,
        gallery_multi_invoke, (void *)(uintptr_t)2u}
};
static const AstraCommandScope gallery_multi_scope = {
    NULL, gallery_multi_commands, 3u};
static const AstraContextMenuItem gallery_multi_items[3] = {
    {"view.toolbar", 12u, NULL},
    {"view.labels", 11u, NULL},
    {"view.status", 11u, NULL}
};

#define GALLERY_CONTROL(id) (&controls[(id) - 1u])

static uint16_t rgb565(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static AstraResult gallery_waiting_image(void *context,
    const AstraControlFrame *frame, AstraSurfaceView *view,
    uint64_t *commands, uint64_t *payload)
{
    (void)context;
    (void)frame;
    (void)view;
    (void)commands;
    (void)payload;
    return ASTRA_OK;
}

static AstraResult gallery_custom_view_draw(void *context,
    const AstraControlFrame *frame, AstraSurfaceView *view,
    uint64_t *commands, uint64_t *payload)
{
    const uint32_t phase = *(const uint32_t *)context;
    uint32_t bar_width;

    if (view == NULL) {
        if (commands == NULL || payload == NULL ||
            *commands > UINT64_MAX - 4u)
            return ASTRA_ERROR_NO_RESOURCES;
        *commands += 4u;
        return ASTRA_OK;
    }
    if (frame->width == 0u || frame->height == 0u) return ASTRA_OK;
    bar_width = frame->width > 32u ? frame->width - 32u : frame->width;
    astra_surface_fill(view, frame->x, frame->y, frame->width,
                       frame->height, 0x1147u);
    astra_surface_fill(view, frame->x, frame->y, frame->width,
                       4u, 0x25b8u);
    astra_surface_fill(view, frame->x + 16, frame->y + 28,
                       bar_width * (phase == 0u ? 2u : 3u) / 4u,
                       12u, 0x25b8u);
    astra_surface_fill(view, frame->x + 16, frame->y + 50,
                       bar_width * (phase == 0u ? 3u : 2u) / 4u,
                       12u, 0xe46au);
    return ASTRA_OK;
}

static void report_layout(uint32_t count, uint32_t iterations,
                          uint64_t elapsed)
{
    char line_text[112];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "INTERFACE LAYOUT controls=");
    (void)astra_string_append_hex(&line, count, 8u);
    (void)astra_string_append(&line, " iterations=");
    (void)astra_string_append_hex(&line, iterations, 8u);
    (void)astra_string_append(&line, " elapsed-ns=");
    (void)astra_string_append_hex(&line, elapsed, 16u);
    (void)astra_log(line_text);
}

static void report_undo(uint64_t record_elapsed, uint64_t undo_elapsed,
                        uint64_t redo_elapsed, uint32_t history_bytes)
{
    char line_text[176];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "INTERFACE UNDO n=");
    (void)astra_string_append_hex(&line, GALLERY_UNDO_BENCHMARK_OPERATIONS, 8u);
    (void)astra_string_append(&line, " rec=");
    (void)astra_string_append_hex(&line, record_elapsed, 16u);
    (void)astra_string_append(&line, " undo=");
    (void)astra_string_append_hex(&line, undo_elapsed, 16u);
    (void)astra_string_append(&line, " redo=");
    (void)astra_string_append_hex(&line, redo_elapsed, 16u);
    (void)astra_string_append(&line, " bytes=");
    (void)astra_string_append_hex(&line, history_bytes, 8u);
    (void)astra_log(line_text);
}

static void report_text(uint64_t append_elapsed, uint64_t fragmented_elapsed,
                        uint32_t pieces)
{
    char line_text[144];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "INTERFACE TEXT n=");
    (void)astra_string_append_hex(&line, GALLERY_TEXT_BENCHMARK_OPERATIONS, 8u);
    (void)astra_string_append(&line, " append-ns=");
    (void)astra_string_append_hex(&line, append_elapsed, 16u);
    (void)astra_string_append(&line, " fragmented-ns=");
    (void)astra_string_append_hex(&line, fragmented_elapsed, 16u);
    (void)astra_string_append(&line, " pieces=");
    (void)astra_string_append_hex(&line, pieces, 8u);
    (void)astra_log(line_text);
}

static void report_control(const char *name, uint32_t count, uint64_t elapsed)
{
    char line_text[112];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "INTERFACE ");
    (void)astra_string_append(&line, name);
    (void)astra_string_append(&line, " n=");
    (void)astra_string_append_hex(&line, count, 8u);
    (void)astra_string_append(&line, " elapsed-ns=");
    (void)astra_string_append_hex(&line, elapsed, 16u);
    (void)astra_log(line_text);
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

static uint32_t add_list(uint32_t index, uint32_t id)
{
    static AstraCollectionModel model = ASTRA_COLLECTION_MODEL_INIT;
    AstraListInfo info = ASTRA_LIST_INFO_INIT;

    if (astra_interface_collection_init(&model, gallery_fonts,
        sizeof(gallery_fonts) / sizeof(gallery_fonts[0]), 1u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    info.id = id;
    info.model = &model;
    return astra_interface_list_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t add_popup(uint32_t index, uint32_t id)
{
    AstraChoiceInfo info = ASTRA_CHOICE_INFO_INIT;

    info.id = id;
    info.items = gallery_choices;
    info.item_count = sizeof(gallery_choices) / sizeof(gallery_choices[0]);
    info.selected = 0u;
    return astra_interface_popup_init(&controls[index], &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
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

static uint32_t add_page_scroll(uint32_t index, uint32_t id,
                                AstraScrollModel *model, uint32_t state)
{
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;

    scroll.line_width = 8u;
    scroll.line_height = 24u;
    if (astra_scroll_init(model, &scroll) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    view.id = id;
    view.state = state;
    view.model = model;
    view.preferred_width = GALLERY_WIDTH - 32u;
    view.preferred_height = GALLERY_HEIGHT - 88u;
    return astra_interface_scroll_view_init(&controls[index], &view) ==
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

/* 0 through 100, then '%'. */
static uint32_t format_percent(char out[5], int64_t value)
{
    AstraString text;

    astra_string_init(&text, out, 5u);
    (void)astra_string_append_i64(&text, value);
    (void)astra_string_append_char(&text, '%');
    return text.length;
}

static uint32_t format_integer(char out[22], int64_t value)
{
    AstraString text;

    astra_string_init(&text, out, 22u);
    (void)astra_string_append_i64(&text, value);
    return text.length;
}

static AstraControl *field_control(uint32_t id)
{
    if (id == GALLERY_FIELD) return GALLERY_CONTROL(GALLERY_FIELD);
    if (id == GALLERY_FIELD_ERROR)
        return GALLERY_CONTROL(GALLERY_FIELD_ERROR);
    if (id == GALLERY_FIELD_READ_ONLY)
        return GALLERY_CONTROL(GALLERY_FIELD_READ_ONLY);
    if (id == GALLERY_TEXT_VIEW) return GALLERY_CONTROL(GALLERY_TEXT_VIEW);
    return NULL;
}

static AstraTextModel *field_model_for_id(uint32_t id)
{
    if (id == GALLERY_FIELD) return &field_model;
    if (id == GALLERY_FIELD_ERROR) return &error_field_model;
    if (id == GALLERY_FIELD_READ_ONLY) return &read_only_field_model;
    if (id == GALLERY_TEXT_VIEW) return &text_view_model;
    return NULL;
}

static AstraResult replace_gallery_text(AstraControl *control,
                                        const char *text, uint32_t bytes)
{
    return control == GALLERY_CONTROL(GALLERY_TEXT_VIEW) ?
        astra_interface_text_view_replace_selection(&ui, control,
                                                     text, bytes) :
        astra_interface_field_replace_selection(&ui, control,
                                                text, bytes);
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
    if (cut && replace_gallery_text(control, NULL, 0u) != ASTRA_OK)
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
        result = replace_gallery_text(control, text, bytes);
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
    if (action->type == ASTRA_UI_ACTION_COPY) {
        uint32_t status = copy_field(clipboard, control, model, 0);
        if (status == ASTRA_STATUS_OK)
            (void)astra_log("gallery clipboard copy complete");
        return status;
    }
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
    uint32_t page_count = GALLERY_PAGE_DRAG - GALLERY_PAGE_INPUT + 1u;
    uint32_t next;

    if (selected < 0 || (uint32_t)selected >= page_count)
        return GALLERY_FAIL_CONTROL;
    next = GALLERY_PAGE_INPUT + (uint32_t)selected;
    if (next == active_page) return ASTRA_STATUS_OK;
    if (astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(GALLERY_PAGE_SCROLL_INPUT +
                active_page - GALLERY_PAGE_INPUT),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(GALLERY_PAGE_SCROLL_INPUT +
                next - GALLERY_PAGE_INPUT), 0u) != ASTRA_OK) {
        (void)astra_interface_ui_set_state(
            &ui, GALLERY_CONTROL(GALLERY_PAGE_SCROLL_INPUT +
                active_page - GALLERY_PAGE_INPUT), 0u);
        return GALLERY_FAIL_CONTROL;
    }
    active_page = next;
    return ASTRA_STATUS_OK;
}

static uint32_t build_file_panel(AstraFilePanel *panel, uint32_t first_id,
                                 uint32_t slot, uint32_t mode)
{
    AstraFilePanelInfo info = ASTRA_FILE_PANEL_INFO_INIT;
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;

    scroll.line_width = 8u;
    scroll.line_height = 16u;
    if (astra_scroll_init(&gallery_file_scrolls[slot], &scroll) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    info.first_id = first_id;
    info.mode = mode;
    info.directory = "/Gallery";
    info.entries = gallery_file_entries;
    info.entry_count = 3u;
    info.item_storage = gallery_file_items[slot];
    info.item_capacity = 3u;
    info.spare_item_storage = gallery_file_spares[slot];
    info.spare_item_capacity = 3u;
    info.grid_icons = gallery_file_icons[slot];
    info.grid_icon_capacity = 3u;
    info.collection = &gallery_file_collections[slot];
    info.scroll = &gallery_file_scrolls[slot];
    info.name_model = mode == ASTRA_FILE_PANEL_SAVE ?
        &gallery_save_name : NULL;
    return astra_interface_file_panel_init(panel,
        GALLERY_CONTROL(first_id), &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t build_font_panel(void)
{
    AstraFontPanelInfo info = ASTRA_FONT_PANEL_INFO_INIT;
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;

    scroll.line_width = 8u;
    scroll.line_height = 16u;
    if (gallery_font_catalog.face_count > 128u ||
        astra_scroll_init(&gallery_font_face_scroll, &scroll) != ASTRA_OK ||
        astra_scroll_init(&gallery_font_strike_scroll, &scroll) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    info.first_id = GALLERY_FONT_PANEL_ROOT;
    info.gui = gallery_gui;
    info.catalog = &gallery_font_catalog;
    info.face_rows = gallery_font_faces;
    info.face_items = gallery_font_face_items;
    info.face_capacity = 128u;
    info.strike_rows = gallery_font_strikes;
    info.strike_items = gallery_font_strike_items;
    info.strike_rows_spare = gallery_font_strikes_spare;
    info.strike_items_spare = gallery_font_strike_items_spare;
    info.strike_capacity = 128u;
    info.face_collection = &gallery_font_face_collection;
    info.strike_collection = &gallery_font_strike_collection;
    info.face_scroll = &gallery_font_face_scroll;
    info.strike_scroll = &gallery_font_strike_scroll;
    info.preview_text = "Astra interface preview 0123456789";
    info.preview_length = sizeof("Astra interface preview 0123456789") - 1u;
    return astra_interface_font_panel_init(&gallery_font_panel,
        GALLERY_CONTROL(GALLERY_FONT_PANEL_ROOT), &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t build_controls(void)
{
    uint32_t status = add_tabs(GALLERY_TABS - 1u, GALLERY_TABS);
    AstraScrollModelInfo scroll = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    scroll.line_width = 8u;
    scroll.line_height = 16u;
    if (status == ASTRA_STATUS_OK &&
        astra_scroll_init(&gallery_scroll_model, &scroll) !=
            ASTRA_OK)
        status = GALLERY_FAIL_CONTROL;

    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_DRAG; ++page)
        status = add_page_scroll(
            GALLERY_PAGE_SCROLL_INPUT + page - GALLERY_PAGE_INPUT - 1u,
            GALLERY_PAGE_SCROLL_INPUT + page - GALLERY_PAGE_INPUT,
            &page_scroll_models[page - GALLERY_PAGE_INPUT],
            page == GALLERY_PAGE_INPUT ? 0u : ASTRA_CONTROL_COLLAPSED);
    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_DRAG; ++page)
        status = add_page(page - 1u, page, 0u);
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
        status = add_label(GALLERY_COLLECTIONS_LABEL - 1u,
                           GALLERY_COLLECTIONS_LABEL,
                           "COLLECTIONS / DATA / NAVIGATION",
                           sizeof("COLLECTIONS / DATA / NAVIGATION") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_FIELD_FAMILY_LABEL - 1u,
                           GALLERY_FIELD_FAMILY_LABEL,
                           "SECURE / SEARCH / SEGMENTS / TAGS / COLOUR",
                           sizeof("SECURE / SEARCH / SEGMENTS / TAGS / COLOUR") - 1u,
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
        status = add_label(GALLERY_DISPLAY_LABEL - 1u,
                           GALLERY_DISPLAY_LABEL,
                           "LEVEL METER / SPARKLINE / STATUS BAR",
                           sizeof("LEVEL METER / SPARKLINE / STATUS BAR") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK) {
        AstraDisplayInfo display = ASTRA_DISPLAY_INFO_INIT;

        gallery_meter.values = gallery_meter_values;
        gallery_meter.peaks = gallery_meter_peaks;
        gallery_meter.channels = 2u;
        gallery_meter.maximum = 100u;
        display.id = GALLERY_LEVEL_METER;
        display.model = &gallery_meter;
        if (astra_interface_level_meter_init(
                GALLERY_CONTROL(GALLERY_LEVEL_METER), &display) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        gallery_spark.samples = gallery_spark_samples;
        gallery_spark.sample_count = sizeof(gallery_spark_samples) /
            sizeof(gallery_spark_samples[0]);
        gallery_spark.maximum = 100u;
        gallery_spark.warning_threshold = 90u;
        display.id = GALLERY_SPARKLINE;
        display.model = &gallery_spark;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_sparkline_init(
                GALLERY_CONTROL(GALLERY_SPARKLINE), &display) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraStatusBarInfo bar = ASTRA_STATUS_BAR_INFO_INIT;

        bar.first_id = GALLERY_STATUS_BAR;
        bar.left = "14 samples";
        bar.left_length = sizeof("14 samples") - 1u;
        bar.right = "audio.mix L R";
        bar.right_length = sizeof("audio.mix L R") - 1u;
        if (astra_interface_status_bar_init(
                GALLERY_CONTROL(GALLERY_STATUS_BAR), &bar) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
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
        status = init_field_model(&text_view_model, text_view_example,
            sizeof(text_view_example) - 1u, text_view_content,
            sizeof(text_view_content), text_view_metadata, sizeof(text_view_metadata));
    if (status == ASTRA_STATUS_OK) {
        AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
        AstraTextViewInfo text = ASTRA_TEXT_VIEW_INFO_INIT;
        AstraScrollbarInfo bar = ASTRA_SCROLLBAR_INFO_INIT;
        scroll.line_height = theme.control_height;
        view.id = GALLERY_TEXT_VIEW_SCROLL; view.model = &text_view_scroll;
        view.preferred_width = 640u; view.preferred_height = GALLERY_TEXT_VIEW_HEIGHT;
        text.id = GALLERY_TEXT_VIEW; text.model = &text_view_model;
        bar.id = GALLERY_TEXT_VIEW_BAR; bar.model = &text_view_scroll;
        bar.orientation = ASTRA_ORIENTATION_VERTICAL;
        if (astra_scroll_init(&text_view_scroll, &scroll) != ASTRA_OK ||
            astra_interface_scroll_view_init(GALLERY_CONTROL(view.id), &view) != ASTRA_OK ||
            astra_interface_text_view_init(GALLERY_CONTROL(text.id), &text) != ASTRA_OK ||
            astra_interface_scrollbar_init(GALLERY_CONTROL(bar.id), &bar) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
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
        status = add_label(GALLERY_LIST_LABEL - 1u, GALLERY_LIST_LABEL,
                           "SINGLE-SELECTION LIST",
                           sizeof("SINGLE-SELECTION LIST") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_list(GALLERY_LIST - 1u, GALLERY_LIST);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_POPUP_LABEL - 1u, GALLERY_POPUP_LABEL,
                           "POPUP", sizeof("POPUP") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_popup(GALLERY_POPUP - 1u, GALLERY_POPUP);
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
                           GALLERY_SPLIT_LEFT_LABEL, "CUSTOM VIEW",
                           sizeof("CUSTOM VIEW") - 1u,
                           ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_splitter(GALLERY_SPLITTER - 1u, GALLERY_SPLITTER);
    if (status == ASTRA_STATUS_OK)
        status = add_page(GALLERY_SPLIT_RIGHT - 1u, GALLERY_SPLIT_RIGHT, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_SPLIT_RIGHT_LABEL - 1u,
                           GALLERY_SPLIT_RIGHT_LABEL, "IMAGE VIEW / FIT",
                           sizeof("IMAGE VIEW / FIT") - 1u,
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
    if (status == ASTRA_STATUS_OK) {
        AstraFormRowInfo row = ASTRA_FORM_ROW_INFO_INIT;
        row.first_id = GALLERY_FORM_ROW;
        row.label = "Notifications";
        row.label_length = sizeof("Notifications") - 1u;
        row.message = "Shown in the shared system style";
        row.message_length = sizeof("Shown in the shared system style") - 1u;
        status = astra_interface_form_row_init(
            GALLERY_CONTROL(GALLERY_FORM_ROW), &row) == ASTRA_OK ?
            ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK)
        status = add_toggle(GALLERY_FORM_OPTION - 1u, GALLERY_FORM_OPTION,
            "Allow", sizeof("Allow") - 1u,
            ASTRA_CONTROL_CHECKBOX, 0u, ASTRA_CONTROL_SELECTED);
    if (status == ASTRA_STATUS_OK)
        status = set_item(GALLERY_FORM_OPTION, GALLERY_FORM_VALUE, 0u, 0u);
    if (status == ASTRA_STATUS_OK) {
        AstraScrollModelInfo scroll_info = ASTRA_SCROLL_MODEL_INFO_INIT;
        AstraColumnView columns[2] = {
            {&column_models[0], &column_scrolls[0], 220u, {0u, 0u}},
            {&column_models[1], &column_scrolls[1], 220u, {0u, 0u}}
        };
        AstraColumnsInfo browser = ASTRA_COLUMNS_INFO_INIT;

        if (astra_scroll_init(&column_scrolls[0], &scroll_info) != ASTRA_OK ||
            astra_scroll_init(&column_scrolls[1], &scroll_info) != ASTRA_OK ||
            astra_interface_collection_init(&column_models[0], column_roots,
                3u, 1u) != ASTRA_OK ||
            astra_interface_collection_init(&column_models[1], column_apps,
                3u, 11u) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        browser.first_id = GALLERY_COLUMNS;
        browser.columns = columns;
        browser.column_count = 2u;
        browser.viewport_height = 160u;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_columns_init(GALLERY_CONTROL(GALLERY_COLUMNS),
                &browser) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraTableInfo table = ASTRA_TABLE_INFO_INIT;

        if (astra_interface_collection_init(&gallery_table_collection,
            column_apps, 3u, 12u) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        gallery_table_model.collection = &gallery_table_collection;
        gallery_table_model.columns = gallery_table_columns;
        gallery_table_model.column_count = 2u;
        gallery_table_model.cells = gallery_table_cells;
        gallery_table_model.cell_count = 3u;
        table.id = GALLERY_TABLE;
        table.model = &gallery_table_model;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_table_init(GALLERY_CONTROL(GALLERY_TABLE),
                &table) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraIconGridInfo info = ASTRA_ICON_GRID_INFO_INIT;
        AstraGridViewInfo grid = {0};
        static const char sample[33] =
            "GRIDVIEW"
            "ROW TWO "
            "ROW  3  "
            "ROW FOUR";

        if (astra_interface_collection_init(&gallery_icon_collection,
                gallery_icon_items, 3u, 701u) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        gallery_icon_model.collection = &gallery_icon_collection;
        gallery_icon_model.icons = gallery_icon_surfaces;
        gallery_icon_model.icon_count = 3u;
        gallery_icon_model.columns = 3u;
        info.id = GALLERY_ICON_GRID;
        info.model = &gallery_icon_model;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_icon_grid_init(GALLERY_CONTROL(GALLERY_ICON_GRID),
                &info) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        for (uint32_t at = 0u; at < 32u; ++at) {
            gallery_grid_cells[at].codepoint = (uint8_t)sample[at];
            gallery_grid_cells[at].foreground = ASTRA_TEXT_COLOR_DEFAULT;
            gallery_grid_cells[at].background = ASTRA_TEXT_COLOR_DEFAULT;
            gallery_grid_cells[at].width = 1u;
        }
        gallery_grid_model.cells = gallery_grid_cells;
        gallery_grid_model.columns = 8u;
        gallery_grid_model.rows = 4u;
        gallery_grid_model.capacity_cells = 32u;
        grid.size = sizeof(grid);
        grid.id = GALLERY_GRID_VIEW;
        grid.model = &gallery_grid_model;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_grid_view_init(
                GALLERY_CONTROL(GALLERY_GRID_VIEW), &grid) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        static const char *const examples[5] = {
            "open-sesame", "projects", "192", "168", "new tag"
        };
        static const uint32_t lengths[5] = {11u, 8u, 3u, 3u, 7u};

        for (uint32_t at = 0u; at < 5u; ++at) {
            status = init_field_model(&family_models[at], examples[at],
                lengths[at], family_content[at],
                sizeof(family_content[at]), family_metadata[at],
                sizeof(family_metadata[at]));
            if (status != ASTRA_STATUS_OK) break;
        }
    }
    if (status == ASTRA_STATUS_OK) {
        AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;

        field.id = GALLERY_SECURE_FIELD;
        field.model = &family_models[0];
        field.preferred_columns = 20u;
        if (astra_interface_secure_field_init(
                GALLERY_CONTROL(GALLERY_SECURE_FIELD), &field) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        field.id = GALLERY_SEARCH_FIELD;
        field.model = &family_models[1];
        if (status == ASTRA_STATUS_OK &&
            astra_interface_search_field_init(
                GALLERY_CONTROL(GALLERY_SEARCH_FIELD), &field) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraTextModel *segments[2] = {&family_models[2], &family_models[3]};
        uint32_t columns[2] = {3u, 3u};
        AstraSegmentedFieldInfo info = ASTRA_SEGMENTED_FIELD_INFO_INIT;

        info.first_id = GALLERY_SEGMENTED_FIELD;
        info.models = segments;
        info.preferred_columns = columns;
        info.segment_count = 2u;
        info.separator = ".";
        info.separator_length = 1u;
        if (astra_interface_segmented_field_init(
                GALLERY_CONTROL(GALLERY_SEGMENTED_FIELD), 4u,
                &info) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraTagInputInfo info = ASTRA_TAG_INPUT_INFO_INIT;

        info.first_id = GALLERY_TAG_INPUT;
        info.tags = family_tags;
        info.tag_count = 2u;
        info.draft = &family_models[4];
        if (astra_interface_tag_input_init(
                GALLERY_CONTROL(GALLERY_TAG_INPUT), 4u, &info) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraColourWellInfo well = ASTRA_COLOUR_WELL_INFO_INIT;

        well.id = GALLERY_COLOUR_WELL;
        well.label = "accent";
        well.label_length = 6u;
        well.binding = ASTRA_COLOUR_THEME_ACCENT;
        if (astra_interface_colour_well_init(
                GALLERY_CONTROL(GALLERY_COLOUR_WELL), &well) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK &&
        astra_interface_colour_panel_init(&gallery_colour_panel,
            GALLERY_CONTROL(GALLERY_COLOUR_PANEL_ROOT),
            GALLERY_COLOUR_PANEL_ROOT, ASTRA_COLOUR_PANEL_RGB565,
            0x1a98u, NULL, 0u) != ASTRA_OK)
        status = GALLERY_FAIL_CONTROL;
    if (status == ASTRA_STATUS_OK)
        status = init_field_model(&gallery_save_name, "Gallery sample.txt",
            sizeof("Gallery sample.txt") - 1u,
            gallery_save_name_content, sizeof(gallery_save_name_content),
            gallery_save_name_metadata, sizeof(gallery_save_name_metadata));
    if (status == ASTRA_STATUS_OK)
        status = build_file_panel(&gallery_open_panel,
            GALLERY_OPEN_PANEL_ROOT, 0u, ASTRA_FILE_PANEL_OPEN);
    if (status == ASTRA_STATUS_OK)
        status = build_file_panel(&gallery_save_panel,
            GALLERY_SAVE_PANEL_ROOT, 1u, ASTRA_FILE_PANEL_SAVE);
    if (status == ASTRA_STATUS_OK)
        status = build_font_panel();
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_ICON_GRID_LABEL - 1u,
            GALLERY_ICON_GRID_LABEL, "ICON GRID / STABLE SELECTION",
            sizeof("ICON GRID / STABLE SELECTION") - 1u,
            ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_GRID_VIEW_LABEL - 1u,
            GALLERY_GRID_VIEW_LABEL, "FIXED-CELL TEXT GRID",
            sizeof("FIXED-CELL TEXT GRID") - 1u,
            ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_PANEL_DEMOS_LABEL - 1u,
            GALLERY_PANEL_DEMOS_LABEL,
            "SYSTEM MENUS / ALERTS / SHEETS / OVERLAYS",
            sizeof("SYSTEM MENUS / ALERTS / SHEETS / OVERLAYS") - 1u,
            ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_ALERT - 1u, GALLERY_PANEL_ALERT,
            "Show alert", 10u, ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_TOOLTIP - 1u,
            GALLERY_PANEL_TOOLTIP, "Show tooltip", 12u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_NOTIFICATION - 1u,
            GALLERY_PANEL_NOTIFICATION, "Show notification", 17u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_MENU - 1u, GALLERY_PANEL_MENU,
            "Open context menu", 17u, ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_SHEET - 1u, GALLERY_PANEL_SHEET,
            "Show sheet", 10u, ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_PANEL_RESULT - 1u,
            GALLERY_PANEL_RESULT,
            "Gallery fixtures preview controls; no files are opened or saved.",
            sizeof("Gallery fixtures preview controls; no files are opened or saved.") - 1u,
            ASTRA_TEXT_CLIENT_TERTIARY);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_COLOUR_BUTTON - 1u,
            GALLERY_PANEL_COLOUR_BUTTON, "Colour", 6u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_OPEN_BUTTON - 1u,
            GALLERY_PANEL_OPEN_BUTTON, "Open", 4u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_SAVE_BUTTON - 1u,
            GALLERY_PANEL_SAVE_BUTTON, "Save", 4u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_PANEL_FONT_BUTTON - 1u,
            GALLERY_PANEL_FONT_BUTTON, "Font", 4u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK) {
        AstraToolbarActionsInfo info = ASTRA_TOOLBAR_ACTIONS_INFO_INIT;

        info.first_id = GALLERY_TOOLBAR_ROOT;
        info.groups = gallery_toolbar_groups;
        info.group_count = sizeof(gallery_toolbar_groups) /
            sizeof(gallery_toolbar_groups[0]);
        info.actions = gallery_toolbar_actions;
        info.action_count = sizeof(gallery_toolbar_actions) /
            sizeof(gallery_toolbar_actions[0]);
        info.scope = &gallery_toolbar_scope;
        status = astra_interface_toolbar_actions_init(&gallery_toolbar,
            GALLERY_CONTROL(GALLERY_TOOLBAR_ROOT), &info) == ASTRA_OK ?
            ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
        AstraButtonInfo primary = ASTRA_BUTTON_INFO_INIT;

        row.id = GALLERY_SPLIT_BUTTON_ROW;
        row.layout.direction = ASTRA_FLEX_ROW;
        row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
        row.layout.main_gap = 0u;
        primary.id = GALLERY_SPLIT_PRIMARY;
        primary.text = "Open";
        primary.text_length = 4u;
        primary.variant = ASTRA_BUTTON_TOOLBAR;
        if (astra_interface_container_init(
                GALLERY_CONTROL(GALLERY_SPLIT_BUTTON_ROW), &row) != ASTRA_OK ||
            astra_interface_split_button_init(
                GALLERY_CONTROL(GALLERY_SPLIT_PRIMARY),
                GALLERY_CONTROL(GALLERY_SPLIT_MENU), &primary,
                GALLERY_SPLIT_MENU) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        static const AstraChoiceItem suggestions[3] = {
            {"Windows", 7u, {0u, 0u}},
            {"Scenes", 6u, {0u, 0u}},
            {"Ports", 5u, {0u, 0u}}
        };
        AstraComboBoxInfo combo = ASTRA_COMBO_BOX_INFO_INIT;
        AstraMultiPopupInfo multi = ASTRA_MULTI_POPUP_INFO_INIT;

        status = init_field_model(&combo_model, "", 0u, combo_content,
            sizeof(combo_content), combo_metadata, sizeof(combo_metadata));
        combo.first_id = GALLERY_COMBO_ROOT;
        combo.model = &combo_model;
        combo.items = suggestions;
        combo.item_count = 3u;
        combo.preferred_columns = 16u;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_combo_box_init(&gallery_combo,
                GALLERY_CONTROL(GALLERY_COMBO_ROOT), &combo) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        multi.id = GALLERY_MULTI_POPUP;
        multi.label = "View options";
        multi.label_length = 12u;
        multi.items = gallery_multi_items;
        multi.item_count = 3u;
        multi.scope = &gallery_multi_scope;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_multi_popup_init(&gallery_multi,
                GALLERY_CONTROL(GALLERY_MULTI_POPUP), &multi) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraTreeInfo tree = ASTRA_TREE_INFO_INIT;
        AstraPaginationInfo pagination = ASTRA_PAGINATION_INFO_INIT;

        if (astra_interface_collection_init(&gallery_tree_collection,
            gallery_tree_items, 4u, 103u) != ASTRA_OK ||
            astra_interface_collection_init(&gallery_pagination_collection,
            column_apps, 3u, 0u) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        gallery_tree_model.collection = &gallery_tree_collection;
        gallery_tree_model.nodes = gallery_tree_nodes;
        gallery_tree_model.node_count = 4u;
        gallery_tree_model.visible_rows = gallery_tree_visible;
        gallery_tree_model.visible_capacity = 4u;
        tree.id = GALLERY_TREE;
        tree.model = &gallery_tree_model;
        gallery_pagination_model.collection = &gallery_pagination_collection;
        gallery_pagination_model.page_size = 1u;
        pagination.first_id = GALLERY_PAGINATION;
        pagination.model = &gallery_pagination_model;
        if (status == ASTRA_STATUS_OK &&
            (astra_interface_tree_init(GALLERY_CONTROL(GALLERY_TREE),
                &tree) != ASTRA_OK ||
             astra_interface_pagination_init(
                GALLERY_CONTROL(GALLERY_PAGINATION),
                &pagination) != ASTRA_OK))
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK) {
        AstraViewInfo info = ASTRA_VIEW_INFO_INIT;

        gallery_custom_model.intrinsic_width = 180u;
        gallery_custom_model.intrinsic_height = 90u;
        gallery_custom_model.draw = gallery_custom_view_draw;
        gallery_custom_model.context = &gallery_view_phase;
        info.id = GALLERY_CUSTOM_VIEW;
        info.model = &gallery_custom_model;
        if (astra_interface_view_init(
                GALLERY_CONTROL(GALLERY_CUSTOM_VIEW), &info) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        gallery_image_model.view = (AstraViewModel)ASTRA_VIEW_MODEL_INIT;
        gallery_image_model.view.intrinsic_width = 160u;
        gallery_image_model.view.intrinsic_height = 90u;
        gallery_image_model.view.draw = gallery_waiting_image;
        gallery_image_model.view.context = &gallery_image_model;
        info.id = GALLERY_IMAGE_VIEW;
        info.model = &gallery_image_model.view;
        if (status == ASTRA_STATUS_OK && astra_interface_view_init(
                GALLERY_CONTROL(GALLERY_IMAGE_VIEW), &info) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_DRAG_HEADING - 1u,
            GALLERY_DRAG_HEADING, "DRAG / DROP / CROSS-WINDOW TRANSFER",
            sizeof("DRAG / DROP / CROSS-WINDOW TRANSFER") - 1u,
            ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = add_button(GALLERY_DRAG_SOURCE - 1u,
            GALLERY_DRAG_SOURCE, "Drag notes.txt + 2",
            sizeof("Drag notes.txt + 2") - 1u,
            ASTRA_BUTTON_STANDARD, 0u, 0u);
    if (status == ASTRA_STATUS_OK) {
        AstraDropTargetInfo target = ASTRA_DROP_TARGET_INFO_INIT;

        target.first_id = GALLERY_DRAG_WORK;
        target.message = "WORK:/projects";
        target.message_length = sizeof("WORK:/projects") - 1u;
        target.minimum_width = 330u;
        target.minimum_height = 140u;
        if (astra_interface_drop_target_init(
            GALLERY_CONTROL(GALLERY_DRAG_WORK), &target) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
        target.first_id = GALLERY_DRAG_SYSTEM;
        target.message = "SYSTEM: read-only";
        target.message_length = sizeof("SYSTEM: read-only") - 1u;
        if (status == ASTRA_STATUS_OK &&
            astra_interface_drop_target_init(
                GALLERY_CONTROL(GALLERY_DRAG_SYSTEM), &target) != ASTRA_OK)
            status = GALLERY_FAIL_CONTROL;
    }
    if (status == ASTRA_STATUS_OK)
        status = add_label(GALLERY_DRAG_RESULT - 1u, GALLERY_DRAG_RESULT,
            "Drag three elements onto either volume; Option requests Move.",
            sizeof("Drag three elements onto either volume; Option requests Move.") - 1u,
            ASTRA_TEXT_CLIENT_SECONDARY);
    if (status == ASTRA_STATUS_OK)
        status = set_item(GALLERY_TABS, 0u, 0u, 0u);
    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_DRAG; ++page)
        status = set_item(GALLERY_PAGE_SCROLL_INPUT +
            page - GALLERY_PAGE_INPUT, 0u, ASTRA_FLEX_BREAK_AFTER, 1u);
    for (uint32_t page = GALLERY_PAGE_INPUT;
         status == ASTRA_STATUS_OK && page <= GALLERY_PAGE_DRAG; ++page) {
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        item.parent_id = GALLERY_PAGE_SCROLL_INPUT +
            page - GALLERY_PAGE_INPUT;
        item.minimum_width = GALLERY_WIDTH - 32u;
        item.maximum_width = item.minimum_width;
        status = astra_interface_control_set_flex(GALLERY_CONTROL(page),
            &item) == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }

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
    PAGE_ITEM(GALLERY_LIST_LABEL, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_LIST, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_POPUP_LABEL, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_POPUP, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_TOOLBAR_ROOT, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLIT_BUTTON_ROW, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLIT_PRIMARY, GALLERY_SPLIT_BUTTON_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_MENU, GALLERY_SPLIT_BUTTON_ROW, 0u);
    PAGE_ITEM(GALLERY_COMBO_ROOT, GALLERY_PAGE_CHOICE,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_MULTI_POPUP, GALLERY_PAGE_CHOICE,
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
    PAGE_ITEM(GALLERY_DISPLAY_LABEL, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_LEVEL_METER, GALLERY_PAGE_PROGRESS, 0u);
    PAGE_ITEM(GALLERY_SPARKLINE, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_STATUS_BAR, GALLERY_PAGE_PROGRESS,
              ASTRA_FLEX_BREAK_AFTER);

    PAGE_ITEM(GALLERY_TEXT_LABEL, GALLERY_PAGE_TEXT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_READ_ONLY, GALLERY_PAGE_TEXT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_TEXT_VIEW_SCROLL, GALLERY_PAGE_TEXT, 0u);
    PAGE_ITEM(GALLERY_TEXT_VIEW, GALLERY_TEXT_VIEW_SCROLL, 0u);
    PAGE_ITEM(GALLERY_TEXT_VIEW_BAR, GALLERY_PAGE_TEXT, ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_GRID_VIEW_LABEL, GALLERY_PAGE_TEXT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_GRID_VIEW, GALLERY_PAGE_TEXT,
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
    PAGE_ITEM(GALLERY_CUSTOM_VIEW, GALLERY_SPLIT_LEFT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SPLITTER, GALLERY_SPLIT_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_RIGHT, GALLERY_SPLIT_ROW, 0u);
    PAGE_ITEM(GALLERY_SPLIT_RIGHT_LABEL, GALLERY_SPLIT_RIGHT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_IMAGE_VIEW, GALLERY_SPLIT_RIGHT,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FORM_ROW, GALLERY_PAGE_LAYOUT,
              ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_COLLECTIONS_LABEL, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_COLUMNS, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_BEFORE);
    PAGE_ITEM(GALLERY_TABLE, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_TREE, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_BEFORE);
    PAGE_ITEM(GALLERY_PAGINATION, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_ICON_GRID_LABEL, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_ICON_GRID, GALLERY_PAGE_COLLECTIONS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FIELD_FAMILY_LABEL, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SECURE_FIELD, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SEARCH_FIELD, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SEGMENTED_FIELD, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_TAG_INPUT, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_COLOUR_WELL, GALLERY_PAGE_FIELDS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PANEL_DEMOS_LABEL, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PANEL_ALERT, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_TOOLTIP, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_NOTIFICATION, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_MENU, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_SHEET, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PANEL_RESULT, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_PANEL_COLOUR_BUTTON, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_OPEN_BUTTON, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_SAVE_BUTTON, GALLERY_PAGE_PANELS, 0u);
    PAGE_ITEM(GALLERY_PANEL_FONT_BUTTON, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_COLOUR_PANEL_ROOT, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_OPEN_PANEL_ROOT, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_SAVE_PANEL_ROOT, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_FONT_PANEL_ROOT, GALLERY_PAGE_PANELS,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DRAG_HEADING, GALLERY_PAGE_DRAG,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DRAG_SOURCE, GALLERY_PAGE_DRAG,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DRAG_WORK, GALLERY_PAGE_DRAG, 0u);
    PAGE_ITEM(GALLERY_DRAG_SYSTEM, GALLERY_PAGE_DRAG,
              ASTRA_FLEX_BREAK_AFTER);
    PAGE_ITEM(GALLERY_DRAG_RESULT, GALLERY_PAGE_DRAG,
              ASTRA_FLEX_BREAK_AFTER);
#undef PAGE_ITEM
    if (status == ASTRA_STATUS_OK) {
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        item.parent_id = GALLERY_PAGE_TEXT;
        item.minimum_width = 10u; item.maximum_width = 10u;
        item.minimum_height = GALLERY_TEXT_VIEW_HEIGHT;
        item.maximum_height = GALLERY_TEXT_VIEW_HEIGHT;
        status = astra_interface_control_set_flex(
            GALLERY_CONTROL(GALLERY_TEXT_VIEW_BAR), &item) == ASTRA_OK ?
            ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
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
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = GALLERY_SPLIT_LEFT;
        item.minimum_width = 160u;
        item.minimum_height = 90u;
        item.grow = 1u;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_CUSTOM_VIEW), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
        item.parent_id = GALLERY_SPLIT_RIGHT;
        if (status == ASTRA_STATUS_OK)
            status = astra_interface_control_set_flex(
                GALLERY_CONTROL(GALLERY_IMAGE_VIEW), &item) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }
    if (status != ASTRA_STATUS_OK)
        return status;
    if (astra_interface_ui_init(&ui, controls, GALLERY_CONTROL_COUNT,
                                   GALLERY_WIDTH, GALLERY_HEIGHT) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(GALLERY_OPEN_PANEL_ROOT),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK ||
        astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(GALLERY_SAVE_PANEL_ROOT),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK ||
        astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(GALLERY_FONT_PANEL_ROOT),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_text_view_set_mode(&ui,
            GALLERY_CONTROL(GALLERY_TEXT_VIEW),
            ASTRA_TEXT_SURFACE_FLOW) != ASTRA_OK ||
        astra_interface_text_view_set_wrap_width(&ui,
            GALLERY_CONTROL(GALLERY_TEXT_VIEW), 600u) != ASTRA_OK ||
        astra_interface_text_view_set_editable(&ui,
            GALLERY_CONTROL(GALLERY_TEXT_VIEW), 1u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_text_undo_bind(&text_view_undo_binding, &ui,
            GALLERY_CONTROL(GALLERY_TEXT_VIEW), &text_view_undo,
            text_view_undo_history, sizeof(text_view_undo_history),
            text_view_undo_scratch, sizeof(text_view_undo_scratch)) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    text_view_undo_bound = 1u;
    if (layout_controls() != ASTRA_STATUS_OK ||
        astra_interface_toolbar_actions_refresh(&gallery_toolbar, &ui) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_multi_popup_refresh(&gallery_multi, &ui, NULL) !=
        ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    {
        AstraToolbarFit fit = ASTRA_TOOLBAR_FIT_INIT;

        return astra_interface_toolbar_actions_fit(&gallery_toolbar, &ui,
            GALLERY_WIDTH - 32u, ASTRA_TOOLBAR_LABELS, &fit) == ASTRA_OK ?
            ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
    }
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

static uint32_t benchmark_menu(void)
{
    const AstraMenuItem rows[] = {{"Open", 4u, 0u}, {NULL, 0u, ASTRA_MENU_SEPARATOR},
        {"Disabled", 8u, ASTRA_MENU_DISABLED}, {"Checked", 7u, ASTRA_MENU_CHECKED}};
    const AstraControlFrame work = {0, 34, 1920, 1004};
    AstraMenu menu = ASTRA_MENU_INIT;
    AstraWindowEvent event = {.size = sizeof(event), .version = ASTRA_WINDOW_EVENT_VERSION,
        .type = ASTRA_WINDOW_EVENT_KEY, .flags = ASTRA_WINDOW_EVENT_DOWN};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraMenuState state;
    uint64_t started;

    if (astra_interface_menu_init(&menu, rows, 4u, &work, 24, 60, 160u, 0u) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    started = astra_clock_monotonic();
    for (uint32_t at = 0u; at < 10000u; ++at) {
        event.data.key.usage = (at & 1u) == 0u ? 0x51u : 0x52u;
        if (astra_interface_menu_handle_event(&menu, &event, &action) != ASTRA_OK ||
            action.type != ASTRA_UI_ACTION_NONE)
            return GALLERY_FAIL_CONTROL;
    }
    report_control("MENU", 10000u, astra_clock_monotonic() - started);
    if (astra_interface_menu_get_state(&menu, &state) != ASTRA_OK || state.selection != 4u)
        return GALLERY_FAIL_CONTROL;
    return ASTRA_STATUS_OK;
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
    AstraResult result;

    if (clear != 0u)
        astra_surface_clear(&surface.view, rgb565(theme.client));
    result = astra_interface_ui_render(&ui, &surface.view);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery UI render", (uint32_t)(-result));
        return GALLERY_FAIL_RENDER;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t create_image_fixture(void)
{
    static const struct {
        AstraRectI32 rectangle;
        AstraColorRGBA8 color;
    } shapes[] = {
        {{0, 0, 160u, 90u}, {21u, 35u, 45u, 255u}},
        {{0, 0, 160u, 12u}, {45u, 174u, 184u, 255u}},
        {{14, 24, 132u, 50u}, {38u, 57u, 68u, 255u}},
        {{26, 34, 48u, 30u}, {236u, 163u, 83u, 255u}},
        {{86, 37, 48u, 6u}, {220u, 231u, 234u, 255u}},
        {{86, 51, 36u, 6u}, {45u, 174u, 184u, 255u}}
    };
    const AstraRectI32 whole = {0, 0, 160u, 90u};
    AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraDrawList draw = ASTRA_DRAW_LIST_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraResult result = astra_window_display(&window, &display);

    info.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_DRAW_TARGET;
    info.width = 160u;
    info.height = 90u;
    info.format = ASTRA_PIXEL_FORMAT_RGB565;
    if (result == ASTRA_OK)
        result = astra_surface_create(&display, &info,
                                      &gallery_image_source);
    if (result == ASTRA_OK)
        result = astra_draw_list_create(&gallery_image_source, &whole,
                                        &draw);
    for (uint32_t at = 0u;
         result == ASTRA_OK && at < sizeof(shapes) / sizeof(shapes[0]); ++at) {
        paint.foreground = shapes[at].color;
        result = astra_draw_rectangle(&draw, &shapes[at].rectangle, 1,
                                      &paint);
    }
    if (result == ASTRA_OK) result = astra_draw_post(&draw, 0u);
    if (draw._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_draw_list_close(&draw);
        if (result == ASTRA_OK) result = closed;
    }
    if (result == ASTRA_OK)
        result = astra_interface_image_view_model_init(
            &gallery_image_model, &gallery_image_source, &whole,
            ASTRA_IMAGE_VIEW_FIT);
    if (display._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_display_close(&display);
        if (result == ASTRA_OK) result = closed;
    }
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery image fixture",
                                (uint32_t)(-result));
        if (gallery_image_source._private_handle != ASTRA_INVALID_HANDLE) {
            AstraResult closed = astra_surface_close(&gallery_image_source);
            (void)closed;
        }
        return GALLERY_FAIL_RENDER;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t create_icon_fixtures(void)
{
    static const struct {
        AstraRectI32 rectangle;
        AstraColorRGBA8 color;
    } shapes[3][4] = {
        {
            {{0, 0, 64u, 64u}, {21u, 35u, 45u, 255u}},
            {{8, 12, 48u, 34u}, {45u, 174u, 184u, 255u}},
            {{12, 16, 40u, 26u}, {38u, 57u, 68u, 255u}},
            {{25, 24, 14u, 10u}, {236u, 163u, 83u, 255u}}
        },
        {
            {{0, 0, 64u, 64u}, {38u, 57u, 68u, 255u}},
            {{9, 10, 46u, 44u}, {235u, 242u, 240u, 255u}},
            {{15, 17, 34u, 8u}, {45u, 174u, 184u, 255u}},
            {{15, 31, 25u, 14u}, {21u, 35u, 45u, 255u}}
        },
        {
            {{0, 0, 64u, 64u}, {45u, 174u, 184u, 255u}},
            {{12, 7, 40u, 50u}, {235u, 242u, 240u, 255u}},
            {{18, 20, 28u, 4u}, {38u, 57u, 68u, 255u}},
            {{18, 32, 22u, 4u}, {38u, 57u, 68u, 255u}}
        }
    };
    const AstraRectI32 whole = {0, 0, ASTRA_ICON_TILE_SOURCE_SIZE,
                                ASTRA_ICON_TILE_SOURCE_SIZE};
    AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraResult result = astra_window_display(&window, &display);

    info.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_DRAW_TARGET;
    info.width = ASTRA_ICON_TILE_SOURCE_SIZE;
    info.height = ASTRA_ICON_TILE_SOURCE_SIZE;
    info.format = ASTRA_PIXEL_FORMAT_RGB565;
    for (uint32_t icon = 0u; result == ASTRA_OK && icon < 3u; ++icon) {
        AstraDrawList draw = ASTRA_DRAW_LIST_INIT;
        AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;

        result = astra_surface_create(&display, &info,
                                      &gallery_icon_sources[icon]);
        if (result == ASTRA_OK)
            result = astra_draw_list_create(&gallery_icon_sources[icon],
                                            &whole, &draw);
        for (uint32_t shape = 0u; result == ASTRA_OK && shape < 4u; ++shape) {
            paint.foreground = shapes[icon][shape].color;
            result = astra_draw_rectangle(&draw,
                &shapes[icon][shape].rectangle, 1, &paint);
        }
        if (result == ASTRA_OK) result = astra_draw_post(&draw, 0u);
        if (draw._private_handle != ASTRA_INVALID_HANDLE) {
            AstraResult closed = astra_draw_list_close(&draw);
            if (result == ASTRA_OK) result = closed;
        }
        if (result == ASTRA_OK)
            gallery_icon_surfaces[icon] = &gallery_icon_sources[icon];
    }
    if (display._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_display_close(&display);
        if (result == ASTRA_OK) result = closed;
    }
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery icon fixtures", (uint32_t)(-result));
        return GALLERY_FAIL_RENDER;
    }
    return ASTRA_STATUS_OK;
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
    info.flags = ASTRA_WINDOW_RESIZABLE;
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
                      ASTRA_WINDOW_SUBSCRIBE_DRAG |
                      ASTRA_WINDOW_SUBSCRIBE_KEY |
                      ASTRA_WINDOW_SUBSCRIBE_TEXT |
                      ASTRA_WINDOW_SUBSCRIBE_VBLANK;
    result = astra_window_create(
        gui, surface.area, &info, &window);
    if (result == ASTRA_OK)
        result = astra_window_set_application_name(
            &window, "Interface Gallery", 17u);
    if (result == ASTRA_OK)
        result = astra_window_menu_bar_set(&window, gallery_menu_bar,
            sizeof(gallery_menu_bar) / sizeof(gallery_menu_bar[0]));
    if (result == ASTRA_OK && create_image_fixture() != ASTRA_STATUS_OK)
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK && create_icon_fixtures() != ASTRA_STATUS_OK)
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK)
        result = astra_window_activate(&window);
    if (result != ASTRA_OK) {
        (void)astra_log_failure("gallery window create",
                                (uint32_t)(-result));
        return GALLERY_FAIL_WINDOW;
    }
    gallery_gui = gui;
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_panel_message(const char *message)
{
    return astra_interface_control_set_text(&ui,
        GALLERY_CONTROL(GALLERY_PANEL_RESULT), message,
        (uint32_t)strlen(message)) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_CONTROL;
}

static uint32_t gallery_select_panel(uint32_t next)
{
    if (next == gallery_visible_panel) return ASTRA_STATUS_OK;
    if (astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(gallery_visible_panel),
            ASTRA_CONTROL_COLLAPSED) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(next), 0u) != ASTRA_OK) {
        (void)astra_interface_ui_set_state(&ui,
            GALLERY_CONTROL(gallery_visible_panel), 0u);
        return GALLERY_FAIL_CONTROL;
    }
    gallery_visible_panel = next;
    return gallery_panel_message("Panel selection changes preview only; no disk I/O.");
}

static uint32_t gallery_show_alert(void)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;

    info.title = "Interface Kit alert";
    info.title_length = sizeof("Interface Kit alert") - 1u;
    info.message = "Shared alert from Interface Gallery.";
    info.message_length = sizeof("Shared alert from Interface Gallery.") - 1u;
    info.button = "Close";
    info.button_length = 5u;
    return astra_interface_show_alert(gallery_gui, &info) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
}

static uint32_t gallery_sheet_close(void)
{
    AstraResult result = ASTRA_OK;

    if (gallery_sheet_window._private_control != ASTRA_INVALID_HANDLE)
        result = astra_window_close(&gallery_sheet_window);
    if (gallery_sheet_surface.area != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_shared_surface_close(&gallery_sheet_surface);
        if (result == ASTRA_OK) result = closed;
    }
    gallery_sheet_open = 0u;
    return result == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
}

static uint32_t gallery_sheet_paint(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    if (!astra_draw_list_view_init(&gallery_sheet_surface.view,
            gallery_sheet_surface.view.pixels,
            gallery_sheet_surface.view.byte_size, 420u, 180u))
        return GALLERY_FAIL_RENDER;
    astra_surface_clear(&gallery_sheet_surface.view, rgb565(theme.client));
    if (astra_interface_ui_render(&gallery_sheet_ui,
            &gallery_sheet_surface.view) != ASTRA_OK ||
        astra_window_present(&gallery_sheet_window) != ASTRA_OK)
        return GALLERY_FAIL_RENDER;
    astra_interface_ui_damage_clear(&gallery_sheet_ui);
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_show_sheet(void)
{
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraResult result;

    if (gallery_sheet_open != 0u) return ASTRA_STATUS_OK;
    if (astra_shared_draw_list_create(&gallery_sheet_surface, 420u, 180u) !=
        ASTRA_SYSCALL_OK)
        return GALLERY_FAIL_SURFACE;
    label.id = 1u;
    label.text = "Sheet stays attached while Gallery window moves.";
    label.text_length = sizeof("Sheet stays attached while Gallery window moves.") - 1u;
    button.id = 2u;
    button.text = "Close sheet";
    button.text_length = 11u;
    button.variant = ASTRA_BUTTON_PRIMARY;
    layout.direction = ASTRA_FLEX_ROW;
    layout.flags = ASTRA_FLEX_WRAP;
    layout.padding_left = layout.padding_right = 24u;
    layout.padding_top = layout.padding_bottom = 24u;
    layout.main_gap = 24u;
    item.flags = ASTRA_FLEX_BREAK_AFTER;
    result = astra_interface_label_init(&gallery_sheet_controls[0], &label);
    if (result == ASTRA_OK)
        result = astra_interface_button_init(&gallery_sheet_controls[1],
            &button);
    if (result == ASTRA_OK)
        result = astra_interface_control_set_flex(
            &gallery_sheet_controls[0], &item);
    if (result == ASTRA_OK)
        result = astra_interface_ui_init(&gallery_sheet_ui,
            gallery_sheet_controls, 2u, 420u, 180u);
    if (result == ASTRA_OK)
        result = astra_interface_ui_layout(&gallery_sheet_ui, &layout);
    info.type = ASTRA_WINDOW_SHEET;
    info.flags = 0u;
    info.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    info.x = 340u;
    info.y = 32u;
    info.width = 420u;
    info.height = 180u;
    info.title = "Gallery sheet";
    info.title_length = 13u;
    info.event_mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                      ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                      ASTRA_WINDOW_SUBSCRIBE_KEY;
    if (result == ASTRA_OK)
        result = astra_window_sheet_create(gallery_gui,
            gallery_sheet_surface.area, &info, &window,
            &gallery_sheet_window);
    if (result == ASTRA_OK)
        result = astra_window_activate(&gallery_sheet_window);
    if (result != ASTRA_OK ||
        gallery_sheet_paint() != ASTRA_STATUS_OK) {
        (void)gallery_sheet_close();
        return GALLERY_FAIL_WINDOW;
    }
    gallery_sheet_open = 1u;
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_sheet_events(void)
{
    AstraWindowEvent event = {0};
    AstraResult result = astra_window_event_try(&gallery_sheet_window,
                                                &event);

    while (result == ASTRA_OK) {
        AstraUIAction action = ASTRA_UI_ACTION_INIT;

        if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST ||
            (event.type == ASTRA_WINDOW_EVENT_KEY &&
             (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
             event.data.key.usage == 0x29u))
            return gallery_sheet_close();
        if (astra_interface_ui_handle_event(&gallery_sheet_ui,
                &event, &action) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        if (action.type == ASTRA_UI_ACTION_ACTIVATE &&
            action.control_id == 2u)
            return gallery_sheet_close();
        result = astra_window_event_try(&gallery_sheet_window, &event);
    }
    if (result != ASTRA_ERROR_WOULD_BLOCK)
        return GALLERY_FAIL_WINDOW;
    {
        AstraControlFrame damage;
        if (astra_interface_ui_damage(&gallery_sheet_ui, &damage) == ASTRA_OK)
            return gallery_sheet_paint();
    }
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_open_context(uint32_t anchor_id,
                                     int32_t x, int32_t y)
{
    if (anchor_id != 0u) {
        AstraControlFrame frame = {0};
        AstraControlFrame clip = {0};

        if (astra_interface_ui_geometry(&ui, anchor_id - 1u,
                &frame, &clip) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        x = frame.x;
        y = frame.y + (int32_t)frame.height;
    }
    return astra_window_menu_open(&window, gallery_context_items,
        sizeof(gallery_context_items) / sizeof(gallery_context_items[0]),
        x, y, UINT32_C(0x47414c4c)) == ASTRA_OK ?
        ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
}

static uint32_t handle_action(AstraHandle clipboard,
                              const AstraUIAction *action)
{
    int toolbar_handled = 0;
    uint32_t panel_outcome = ASTRA_COLOUR_PANEL_NONE;

    if (gallery_visible_panel == GALLERY_COLOUR_PANEL_ROOT &&
        astra_interface_colour_panel_action(&gallery_colour_panel, &ui,
            GALLERY_CONTROL(GALLERY_COLOUR_PANEL_ROOT), action,
            &panel_outcome) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (panel_outcome == ASTRA_COLOUR_PANEL_ACCEPTED) {
        AstraColorRGBA8 selected;

        if (astra_interface_colour_panel_color(&gallery_colour_panel,
                &selected) != ASTRA_OK ||
            astra_interface_colour_well_set(&ui,
                GALLERY_CONTROL(GALLERY_COLOUR_WELL),
                ASTRA_COLOUR_LITERAL, selected, 0u) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        return select_page(GALLERY_PAGE_FIELDS - GALLERY_PAGE_INPUT);
    }
    if (panel_outcome == ASTRA_COLOUR_PANEL_CANCELLED)
        return select_page(GALLERY_PAGE_FIELDS - GALLERY_PAGE_INPUT);
    if (gallery_visible_panel == GALLERY_OPEN_PANEL_ROOT ||
        gallery_visible_panel == GALLERY_SAVE_PANEL_ROOT) {
        AstraFilePanel *panel = gallery_visible_panel ==
            GALLERY_OPEN_PANEL_ROOT ? &gallery_open_panel :
            &gallery_save_panel;
        uint32_t outcome = ASTRA_FILE_PANEL_NONE;

        if (astra_interface_file_panel_action(panel, &ui,
                GALLERY_CONTROL(gallery_visible_panel), action,
                &outcome) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        if (outcome == ASTRA_FILE_PANEL_PICKED ||
            outcome == ASTRA_FILE_PANEL_OVERWRITE)
            return gallery_panel_message(panel->target);
        if (outcome == ASTRA_FILE_PANEL_CANCELLED)
            return gallery_select_panel(GALLERY_COLOUR_PANEL_ROOT);
        if (outcome == ASTRA_FILE_PANEL_NAVIGATE)
            return gallery_panel_message("Gallery fixture has no subfolders.");
    }
    if (gallery_visible_panel == GALLERY_FONT_PANEL_ROOT) {
        uint32_t outcome = ASTRA_FONT_PANEL_NONE;

        if (astra_interface_font_panel_action(&gallery_font_panel, &ui,
                GALLERY_CONTROL(GALLERY_FONT_PANEL_ROOT), action,
                &outcome) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
        if (outcome == ASTRA_FONT_PANEL_CANCELLED)
            return gallery_select_panel(GALLERY_COLOUR_PANEL_ROOT);
        if (outcome == ASTRA_FONT_PANEL_ACCEPTED)
            return gallery_panel_message("Font selected for preview only.");
    }
    if (action->type == ASTRA_UI_ACTION_ACTIVATE) {
        if (action->control_id == GALLERY_PANEL_COLOUR_BUTTON)
            return gallery_select_panel(GALLERY_COLOUR_PANEL_ROOT);
        if (action->control_id == GALLERY_PANEL_OPEN_BUTTON)
            return gallery_select_panel(GALLERY_OPEN_PANEL_ROOT);
        if (action->control_id == GALLERY_PANEL_SAVE_BUTTON)
            return gallery_select_panel(GALLERY_SAVE_PANEL_ROOT);
        if (action->control_id == GALLERY_PANEL_FONT_BUTTON)
            return gallery_select_panel(GALLERY_FONT_PANEL_ROOT);
        if (action->control_id == GALLERY_PANEL_ALERT)
            return gallery_show_alert();
        if (action->control_id == GALLERY_PANEL_SHEET)
            return gallery_show_sheet();
        if (action->control_id == GALLERY_PANEL_TOOLTIP) {
            AstraControlFrame frame = {0};
            AstraControlFrame clip = {0};
            static const char message[] = "Shared tooltip follows pointer and focus";

            if (astra_interface_ui_geometry(&ui,
                    GALLERY_PANEL_TOOLTIP - 1u, &frame, &clip) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            return astra_window_tooltip_show(&window, message,
                sizeof(message) - 1u,
                (uint16_t)frame.x,
                (uint16_t)(frame.y + frame.height), 5000u) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
        }
        if (action->control_id == GALLERY_PANEL_NOTIFICATION) {
            static const char message[] = "Shared notification from Interface Gallery";

            return astra_window_notification_show(&window, message,
                sizeof(message) - 1u, 7000u) == ASTRA_OK ?
                ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
        }
        if (action->control_id == GALLERY_PANEL_MENU ||
            action->control_id == GALLERY_SPLIT_MENU)
            return gallery_open_context(action->control_id, 0, 0);
        if (action->control_id == GALLERY_SPLIT_PRIMARY)
            return gallery_panel_message("Split Button primary action: Open");
    }
    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        action->control_id == GALLERY_COLOUR_WELL)
        return select_page(GALLERY_PAGE_PANELS - GALLERY_PAGE_INPUT);

    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        action->control_id == GALLERY_COMBO_MENU) {
        AstraResult opened = astra_interface_combo_box_open(
            &gallery_combo, &ui, &window);
        if (opened != ASTRA_OK && opened != ASTRA_ERROR_NOT_PRESENT)
            return GALLERY_FAIL_CONTROL;
        return ASTRA_STATUS_OK;
    }
    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        action->control_id == GALLERY_MULTI_POPUP &&
        astra_interface_multi_popup_open(&gallery_multi, &ui, &window) !=
            ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        action->control_id == GALLERY_MULTI_POPUP)
        return ASTRA_STATUS_OK;
    if (astra_interface_toolbar_actions_handle_action(&gallery_toolbar,
            &window, action, &toolbar_handled) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (toolbar_handled) return ASTRA_STATUS_OK;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        action->control_id == GALLERY_POPUP &&
        astra_interface_popup_open(&gallery_popup, &ui,
            GALLERY_CONTROL(GALLERY_POPUP), &window) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (action->type == ASTRA_UI_ACTION_ACTIVATE &&
        (action->control_id == GALLERY_PAGINATION_PREVIOUS ||
         action->control_id == GALLERY_PAGINATION_NEXT)) {
        uint32_t next = gallery_pagination_model.page_index;

        if (action->control_id == GALLERY_PAGINATION_PREVIOUS && next != 0u)
            --next;
        else if (action->control_id == GALLERY_PAGINATION_NEXT)
            ++next;
        if (astra_interface_pagination_set_page(&ui,
            GALLERY_CONTROL(GALLERY_PAGINATION),
            &gallery_pagination_model, next) != ASTRA_OK)
            return GALLERY_FAIL_CONTROL;
    }
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

static int gallery_frame_contains(uint32_t id, int32_t x, int32_t y)
{
    AstraControlFrame frame = {0};
    AstraControlFrame clip = {0};

    return astra_interface_ui_geometry(&ui, id - 1u, &frame, &clip) ==
               ASTRA_OK && x >= frame.x && y >= frame.y &&
           x < frame.x + (int32_t)frame.width &&
           y < frame.y + (int32_t)frame.height;
}

static uint32_t gallery_drag_target_at(int32_t x, int32_t y)
{
    if (active_page != GALLERY_PAGE_DRAG) return 0u;
    if (gallery_frame_contains(GALLERY_DRAG_WORK, x, y))
        return GALLERY_DRAG_WORK;
    if (gallery_frame_contains(GALLERY_DRAG_SYSTEM, x, y))
        return GALLERY_DRAG_SYSTEM;
    return 0u;
}

static uint32_t gallery_drag_visual(uint32_t target, uint32_t operation)
{
    const char *work = "WORK:/projects";
    const char *system = "SYSTEM: read-only";
    uint32_t work_state = ASTRA_DROP_TARGET_IDLE;
    uint32_t system_state = ASTRA_DROP_TARGET_IDLE;

    if (target == gallery_drag_visual_target &&
        operation == gallery_drag_visual_operation)
        return ASTRA_STATUS_OK;
    if (target == GALLERY_DRAG_WORK) {
        work_state = ASTRA_DROP_TARGET_ACCEPT;
        work = operation == ASTRA_DRAG_MOVE ?
            "Move 3 elements to WORK:/projects" :
            "Copy 3 elements to WORK:/projects";
    } else if (target == GALLERY_DRAG_SYSTEM) {
        system_state = ASTRA_DROP_TARGET_REFUSE;
        system = "SYSTEM: is read-only \xe2\x80\x94 can't drop here";
    }
    if (astra_interface_drop_target_set(&ui,
            GALLERY_CONTROL(GALLERY_DRAG_WORK), work_state,
            work, (uint32_t)strlen(work)) != ASTRA_OK ||
        astra_interface_drop_target_set(&ui,
            GALLERY_CONTROL(GALLERY_DRAG_SYSTEM), system_state,
            system, (uint32_t)strlen(system)) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    gallery_drag_visual_target = target;
    gallery_drag_visual_operation = operation;
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_drag_pointer(const AstraWindowEvent *event)
{
    uint32_t triggered = 0u;
    uint32_t source_hit = active_page == GALLERY_PAGE_DRAG &&
        event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
        gallery_frame_contains(GALLERY_DRAG_SOURCE,
            event->data.pointer.x, event->data.pointer.y);
    if (astra_interface_drag_gesture_event(&gallery_drag_gesture,
            event, source_hit, &triggered) != ASTRA_OK)
        return GALLERY_FAIL_CONTROL;
    if (active_page != GALLERY_PAGE_DRAG || triggered == 0u)
        return ASTRA_STATUS_OK;
    {
        static const char files[] = "notes.txt\nsketch.afnt\nDoom.app";
        AstraClipboardRepresentation representation =
            ASTRA_CLIPBOARD_REPRESENTATION_INIT;

        representation.type = "text/plain";
        representation.type_length = sizeof("text/plain") - 1u;
        representation.data = files;
        representation.data_length = sizeof(files) - 1u;
        if (astra_window_drag_begin(&window, &representation, 1u, 3u,
                ASTRA_DRAG_OPERATIONS, "notes.txt +2",
                sizeof("notes.txt +2") - 1u) != ASTRA_OK)
            return GALLERY_FAIL_WINDOW;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t gallery_drag_event(AstraWindowEvent *event)
{
    uint32_t target = 0u;
    uint32_t operation = 0u;
    uint32_t status = ASTRA_STATUS_OK;
    uint32_t completed = 0u;
    const char *feedback = "Choose a writable target";
    AstraResult result = ASTRA_OK;

    if (event->type == ASTRA_WINDOW_EVENT_DRAG_ENTER ||
        event->type == ASTRA_WINDOW_EVENT_DRAG_MOTION) {
        gallery_drag_token = event->data.drag.token;
        target = gallery_drag_target_at(event->data.drag.x,
                                         event->data.drag.y);
        gallery_drag_last_target = target;
        if (target == GALLERY_DRAG_WORK) {
            operation = (event->data.drag.modifiers & ASTRA_INPUT_MOD_ALT) !=
                0u ? ASTRA_DRAG_MOVE : ASTRA_DRAG_COPY;
            feedback = operation == ASTRA_DRAG_MOVE ?
                "Move 3 elements to WORK:/projects" :
                "Copy 3 elements to WORK:/projects";
        } else if (target == GALLERY_DRAG_SYSTEM) {
            feedback = "SYSTEM: is read-only \xe2\x80\x94 can't drop here";
        }
        if (target != gallery_drag_visual_target ||
            operation != gallery_drag_visual_operation ||
            event->type == ASTRA_WINDOW_EVENT_DRAG_ENTER) {
            if (gallery_drag_visual(target, operation) != ASTRA_STATUS_OK)
                { status = GALLERY_FAIL_CONTROL; goto close_document; }
            result = astra_window_drag_feedback(&window,
                gallery_drag_token, operation, feedback,
                (uint16_t)strlen(feedback));
        }
    } else if (event->type == ASTRA_WINDOW_EVENT_DRAG_LEAVE ||
               event->type == ASTRA_WINDOW_EVENT_DRAG_FINISHED) {
        if (event->type == ASTRA_WINDOW_EVENT_DRAG_FINISHED &&
            gallery_drag_last_target == GALLERY_DRAG_SYSTEM) {
            const char *message = "SYSTEM: is read-only; drop refused";

            if (astra_interface_control_set_text(&ui,
                    GALLERY_CONTROL(GALLERY_DRAG_RESULT), message,
                    (uint32_t)strlen(message)) != ASTRA_OK)
                { status = GALLERY_FAIL_CONTROL; goto close_document; }
        }
        gallery_drag_token = 0u;
        gallery_drag_gesture.armed = 0u;
        if (event->type == ASTRA_WINDOW_EVENT_DRAG_FINISHED)
            gallery_drag_last_target = 0u;
        if (gallery_drag_visual(0u, 0u) != ASTRA_STATUS_OK)
            { status = GALLERY_FAIL_CONTROL; goto close_document; }
    } else if (event->type == ASTRA_WINDOW_EVENT_DRAG_DROP) {
        uint32_t drop_target = gallery_drag_target_at(event->data.drag.x,
                                                      event->data.drag.y);
        const char *message = event->data.drag.operation == ASTRA_DRAG_MOVE ?
            "Moved 3 elements to WORK:/projects" :
            "Copied 3 elements to WORK:/projects";

        if (drop_target == GALLERY_DRAG_SYSTEM)
            message = "SYSTEM: is read-only; drop refused";
        if (drop_target != 0u &&
            astra_interface_control_set_text(&ui,
                GALLERY_CONTROL(GALLERY_DRAG_RESULT), message,
                (uint32_t)strlen(message)) != ASTRA_OK)
            { status = GALLERY_FAIL_CONTROL; goto close_document; }
        if (drop_target == GALLERY_DRAG_WORK)
            completed = event->data.drag.operation;
        if (gallery_drag_visual(0u, 0u) != ASTRA_STATUS_OK)
            { status = GALLERY_FAIL_CONTROL; goto close_document; }
    }
close_document:
    if (event->type == ASTRA_WINDOW_EVENT_DRAG_ENTER ||
        event->type == ASTRA_WINDOW_EVENT_DRAG_DROP) {
        AstraResult closed = astra_window_drag_document_close(event);

        if (result == ASTRA_OK) result = closed;
    }
    if (event->type == ASTRA_WINDOW_EVENT_DRAG_DROP) {
        AstraResult acknowledged = astra_window_drag_complete(&window,
            event->data.drag.token,
            status == ASTRA_STATUS_OK && result == ASTRA_OK ?
                completed : 0u);

        if (result == ASTRA_OK) result = acknowledged;
    }
    return status != ASTRA_STATUS_OK ? status :
           result == ASTRA_OK ? ASTRA_STATUS_OK : GALLERY_FAIL_WINDOW;
}

static uint32_t run(AstraHandle clipboard, AstraHandle gui)
{
    uint32_t vblank_subscribed = 1u;
    uint32_t applied_pointer_shape = UINT32_MAX;

    for (;;) {
        AstraWindowEvent event = {0};
        AstraUIAction action = ASTRA_UI_ACTION_INIT;
        AstraControlFrame damage;
        uint32_t animated =
            astra_interface_ui_animations_active(&ui) != 0;
        uint32_t waits[3];
        uint32_t wait_count = animated != 0u ? 2u : 1u;
        uint32_t sheet_index = UINT32_MAX;
        uint32_t selected = 0u;
        uint32_t scroll_only = 1u;
        AstraResult result;

        if (animated != vblank_subscribed) {
            uint32_t mask = ASTRA_WINDOW_SUBSCRIBE_DEFAULT |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON |
                            ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL |
                            ASTRA_WINDOW_SUBSCRIBE_DRAG |
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
        if (gallery_sheet_open != 0u) {
            sheet_index = wait_count;
            waits[wait_count++] =
                astra_window_event_wait_handle(&gallery_sheet_window);
        }
        if (astra_wait_multiple(waits, wait_count,
                                ASTRA_DEADLINE_FOREVER, &selected, NULL) !=
            ASTRA_SYSCALL_OK)
            return GALLERY_FAIL_WINDOW;
        if (selected == sheet_index) {
            if (gallery_sheet_events() != ASTRA_STATUS_OK)
                return GALLERY_FAIL_WINDOW;
            continue;
        }
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
            int popup_handled = 0;
            int toolbar_handled = 0;
            int combo_handled = 0;
            int multi_handled = 0;

            action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
            if (event.type == ASTRA_WINDOW_EVENT_CLOSE_REQUEST) {
                (void)gallery_sheet_close();
                (void)astra_interface_popup_close(&gallery_popup);
                (void)astra_interface_combo_box_close(&gallery_combo);
                (void)astra_interface_multi_popup_close(&gallery_multi);
                (void)astra_interface_toolbar_actions_close(&gallery_toolbar);
                return ASTRA_STATUS_OK;
            }
            if (event.type >= ASTRA_WINDOW_EVENT_DRAG_ENTER &&
                event.type <= ASTRA_WINDOW_EVENT_DRAG_FINISHED) {
                if (gallery_drag_event(&event) != ASTRA_STATUS_OK)
                    return GALLERY_FAIL_CONTROL;
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (gallery_drag_pointer(&event) != ASTRA_STATUS_OK)
                return GALLERY_FAIL_WINDOW;
            if (astra_interface_combo_box_handle_event(&gallery_combo,
                    &event, &action, &combo_handled) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (combo_handled) {
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (astra_interface_multi_popup_handle_event(&gallery_multi,
                    &event, &action, &multi_handled) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (multi_handled) {
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (astra_interface_toolbar_actions_handle_event(&gallery_toolbar,
                    &event, &toolbar_handled) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (toolbar_handled) {
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_MENU_RESULT &&
                event.data.menu_result.request ==
                    ASTRA_WINDOW_MENU_BAR_REQUEST) {
                uint32_t selection = event.data.menu_result.selection;

                if (selection == 3u &&
                    astra_window_notification_show(&window,
                        "Gallery menu notification", 25u, 7000u) !=
                        ASTRA_OK)
                    return GALLERY_FAIL_WINDOW;
                if ((selection == 4u || selection == 5u) &&
                    gallery_show_alert() != ASTRA_STATUS_OK)
                    return GALLERY_FAIL_WINDOW;
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_MENU_RESULT &&
                event.data.menu_result.request == UINT32_C(0x47414c4c)) {
                static const char *const results[] = {
                    "Context menu dismissed", "Context: Open",
                    "Context: Duplicate", "Context: separator",
                    "Context: Disabled", "Context: Show details"
                };
                uint32_t selection = event.data.menu_result.selection;

                if (selection < sizeof(results) / sizeof(results[0]) &&
                    gallery_panel_message(results[selection]) !=
                        ASTRA_STATUS_OK)
                    return GALLERY_FAIL_CONTROL;
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (active_page == GALLERY_PAGE_PANELS &&
                event.type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
                (event.flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
                event.data.pointer.button == ASTRA_INPUT_BUTTON_RIGHT) {
                if (gallery_open_context(0u, event.data.pointer.x,
                        event.data.pointer.y) != ASTRA_STATUS_OK)
                    return GALLERY_FAIL_WINDOW;
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            if (event.type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
                AstraResult refreshed = astra_fonts_use_system(gui, &event);
                if (refreshed != ASTRA_OK)
                    (void)astra_log_failure("gallery font refresh", (uint32_t)refreshed);
            }
            if (astra_interface_popup_handle_event(&gallery_popup, &event,
                    &action, &popup_handled) != ASTRA_OK)
                return GALLERY_FAIL_CONTROL;
            if (popup_handled) {
                if (handle_action(clipboard, &action) != ASTRA_STATUS_OK)
                    return GALLERY_FAIL_CONTROL;
                scroll_only = 0u;
                result = astra_window_event_try(&window, &event);
                continue;
            }
            {
                AstraResult handled = astra_interface_ui_handle_event(
                    &ui, &event, &action);
                if (handled != ASTRA_OK) {
                    (void)astra_log_failure("gallery UI event",
                                            (uint32_t)(-handled));
                    return GALLERY_FAIL_CONTROL;
                }
            }
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE) {
                gallery_view_phase ^= 1u;
                if (astra_interface_view_invalidate(&ui,
                        GALLERY_CONTROL(GALLERY_CUSTOM_VIEW)) != ASTRA_OK)
                    return GALLERY_FAIL_CONTROL;
            }
            if (event.type == ASTRA_WINDOW_EVENT_RESIZE &&
                event.data.resize.width > 32u) {
                AstraToolbarFit fit = ASTRA_TOOLBAR_FIT_INIT;
                AstraResult fitted = astra_interface_toolbar_actions_fit(
                    &gallery_toolbar, &ui,
                    event.data.resize.width - 32u,
                    ASTRA_TOOLBAR_LABELS, &fit);

                if (fitted != ASTRA_OK &&
                    fitted != ASTRA_ERROR_NO_RESOURCES)
                    return GALLERY_FAIL_CONTROL;
            }
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
    const AstraStartupCapability *fonts;
    const char *argument;
    uint32_t benchmarks;
    uint32_t status;

    if (!astra_startup_validate(startup) || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    gui = astra_startup_capability(startup, ASTRA_CAPABILITY_GUI);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    clipboard = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_CLIPBOARD);
    fonts = astra_startup_capability(startup,
                                     ASTRA_CAPABILITY_FONT_CATALOG);
    if (gui == NULL || clipboard == NULL || fonts == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_fonts_use_system(gui->handle, NULL) != ASTRA_OK)
        return ASTRA_STATUS_IO;
    argument = astra_startup_argument(startup, 1u);
    benchmarks = argument != NULL &&
        astra_string_compare_ascii_nocase(argument, "--benchmark") == 0;
    status = startup->argc <= 1u || (startup->argc == 2u && benchmarks) ?
        ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (status == ASTRA_STATUS_OK) {
        status = astra_process_filesystem_open(&process_filesystem, startup);
        if (status != ASTRA_STATUS_OK)
            status = GALLERY_FAIL_LIBRARY;
    }
    if (status == ASTRA_STATUS_OK &&
        astra_fonts_catalog_open(fonts->handle,
            &gallery_font_catalog) != ASTRA_OK)
        status = GALLERY_FAIL_LIBRARY;
    gallery_gui = gui->handle;
    if (status == ASTRA_STATUS_OK)
        status = build_controls();
    if (status == ASTRA_STATUS_OK && benchmarks) {
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
            status = benchmark_menu();
    }
    if (status == ASTRA_STATUS_OK)
        status = create_window(gui->handle);
    if (status == ASTRA_STATUS_OK)
        astra_interface_ui_damage_clear(&ui);
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
        (void)astra_close(bootstrap->handle);
    }
    if (status == ASTRA_STATUS_OK)
        status = run(clipboard->handle, gui->handle);
    if (text_view_undo_bound != 0u) {
        (void)astra_interface_text_undo_unbind(&text_view_undo_binding);
        text_view_undo_bound = 0u;
    }
    if (gallery_image_source._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_surface_close(&gallery_image_source);
        if (closed != ASTRA_OK)
            (void)astra_log_failure("gallery image close",
                                    (uint32_t)(-closed));
    }
    for (uint32_t icon = 0u; icon < 3u; ++icon)
        if (gallery_icon_sources[icon]._private_handle != ASTRA_INVALID_HANDLE) {
            AstraResult closed = astra_surface_close(&gallery_icon_sources[icon]);
            if (closed != ASTRA_OK)
                (void)astra_log_failure("gallery icon close",
                                        (uint32_t)(-closed));
        }
    if (gallery_sheet_open != 0u)
        (void)gallery_sheet_close();
    if (window._private_control != ASTRA_INVALID_HANDLE) {
        AstraResult close_result = astra_window_close(&window);
        (void)close_result;
    }
    if (surface.area != ASTRA_INVALID_HANDLE)
        (void)astra_shared_surface_close(&surface);
    astra_interface_file_panel_dispose(&gallery_open_panel);
    astra_interface_file_panel_dispose(&gallery_save_panel);
    if (gallery_font_catalog._private_area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_fonts_catalog_close(&gallery_font_catalog);
        if (closed != ASTRA_OK)
            (void)astra_log_failure("gallery font catalog close",
                                    (uint32_t)(-closed));
    }
    astra_process_filesystem_close(&process_filesystem);
    return (int)status;
}
