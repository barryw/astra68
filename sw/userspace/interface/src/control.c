#include "control_internal.h"
#include "text_surface_internal.h"
#include "display_widgets_internal.h"

#include <astra/bytes.h>
#include <astra/field_family.h>
#include <astra/draw_list.h>
#include <astra/fonts.h>
#include <astra/input.h>
#include <astra/input_modifiers.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/text_surface.h>
#include <astra/utf8.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define CONTROL_NONE UINT32_MAX
#define CONTROL_SEMANTIC_STATES \
    (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_SELECTED | ASTRA_CONTROL_ERROR | \
     ASTRA_CONTROL_COLLAPSED)
#define CONTROL_VALUE_STATES \
    (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR | ASTRA_CONTROL_COLLAPSED)
#define CONTROL_PREVIEW_STATES \
    (ASTRA_CONTROL_HOVERED | ASTRA_CONTROL_PRESSED | ASTRA_CONTROL_FOCUSED)
#define CONTROL_FOCUS_VISIBLE (UINT32_C(1) << 31)
#define TEXT_VIEW_SOFT_END (UINT32_C(1) << 30)
#define CONTROL_DYNAMIC_STATES \
    (CONTROL_PREVIEW_STATES | CONTROL_FOCUS_VISIBLE | TEXT_VIEW_SOFT_END)
#define FIELD_FLAGS (ASTRA_FIELD_READ_ONLY | ASTRA_FIELD_INLINE | \
    ASTRA_FIELD_SECURE | ASTRA_FIELD_SEARCH)
#define FIELD_PADDING_X 10u
#define FIELD_SEARCH_PADDING_X 28u

/* One hardware LINES command paints the entire dashed target outline. */
static uint32_t drop_target_border(const AstraControlFrame *frame,
                                    AstraSurfaceLine *lines)
{
    uint32_t count = 0u;
    uint32_t step = 16u;
    uint32_t dash;

    if (frame->width < 6u || frame->height < 6u) return 0u;
    while (4u * ((frame->width + step - 1u) / step +
                 (frame->height + step - 1u) / step) > 192u)
        step *= 2u;
    dash = step * 9u / 16u;
    for (uint32_t x = 2u; x + 2u < frame->width; x += step) {
        uint32_t end = x + dash < frame->width - 2u ?
            x + dash : frame->width - 3u;
        for (uint32_t edge = 0u; edge < 2u; ++edge)
            for (uint32_t thick = 0u; thick < 2u; ++thick) {
                if (lines != NULL)
                    lines[count] = (AstraSurfaceLine){
                        frame->x + (int32_t)x,
                        frame->y + (int32_t)(edge == 0u ? 1u + thick :
                                               frame->height - 2u - thick),
                        frame->x + (int32_t)end,
                        frame->y + (int32_t)(edge == 0u ? 1u + thick :
                                               frame->height - 2u - thick)};
                ++count;
            }
    }
    for (uint32_t y = 2u; y + 2u < frame->height; y += step) {
        uint32_t end = y + dash < frame->height - 2u ?
            y + dash : frame->height - 3u;
        for (uint32_t edge = 0u; edge < 2u; ++edge)
            for (uint32_t thick = 0u; thick < 2u; ++thick) {
                if (lines != NULL)
                    lines[count] = (AstraSurfaceLine){
                        frame->x + (int32_t)(edge == 0u ? 1u + thick :
                                               frame->width - 2u - thick),
                        frame->y + (int32_t)y,
                        frame->x + (int32_t)(edge == 0u ? 1u + thick :
                                               frame->width - 2u - thick),
                        frame->y + (int32_t)end};
                ++count;
            }
    }
    return count;
}
#define FIELD_MASK_BATCH 128u
#define TEXT_VIEW_INVERTED UINT32_C(4)
#define TEXT_VIEW_WRAP UINT32_C(8)
#define TEXT_VIEW_FLOW UINT32_C(16)
#define TEXT_VIEW_LINE_NUMBERS UINT32_C(32)
#define TEXT_MODEL_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_FIELD || (kind) == ASTRA_CONTROL_TEXT_VIEW)
#define FLEX_ITEM_FLAGS (ASTRA_FLEX_BREAK_BEFORE | ASTRA_FLEX_BREAK_AFTER)
#define VALUE_INPUT_EDITING UINT32_C(1)
#define VALUE_INPUT_NEGATIVE UINT32_C(2)
#define VALUE_INPUT_FLAGS (VALUE_INPUT_EDITING | VALUE_INPUT_NEGATIVE)
#define STEPPER_PART_VALUE 0u
#define STEPPER_PART_UP 1u
#define STEPPER_PART_DOWN 2u
#define SCROLLBAR_PART_NONE 0u
#define SCROLLBAR_PART_THUMB 1u
#define SCROLLBAR_PART_BEFORE 2u
#define SCROLLBAR_PART_AFTER 3u
#define TABLE_SORT_COLUMN_MASK UINT32_C(0x3f)
#define TABLE_SORT_REVERSE UINT32_C(0x40)
#define TABLE_SORT_ACTIVE UINT32_C(0x80)
#define TABLE_SORT_MASK (TABLE_SORT_COLUMN_MASK | TABLE_SORT_REVERSE | \
    TABLE_SORT_ACTIVE)
#define STEPPER_BUTTON_WIDTH 20u
#define STEPPER_REPEAT_DELAY 24u
#define STEPPER_REPEAT_INTERVAL 5u
#define TOGGLE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_CHECKBOX || (kind) == ASTRA_CONTROL_RADIO || \
     (kind) == ASTRA_CONTROL_SWITCH)
#define COLLECTION_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_LIST || (kind) == ASTRA_CONTROL_TABLE || \
     (kind) == ASTRA_CONTROL_OUTLINE_TABLE || \
     (kind) == ASTRA_CONTROL_TREE || (kind) == ASTRA_CONTROL_ICON_GRID)
#define TABLE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_TABLE || \
     (kind) == ASTRA_CONTROL_OUTLINE_TABLE)
#define CHOICE_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_SEGMENTED || (kind) == ASTRA_CONTROL_TAB || \
     COLLECTION_KINDS(kind) || (kind) == ASTRA_CONTROL_POPUP)
#define CONTAINER_KINDS(kind) \
    ((kind) == ASTRA_CONTROL_CONTAINER || \
     (kind) == ASTRA_CONTROL_SCROLL_VIEW)
#define SCROLLBAR_STYLE_ORIENTATION UINT32_C(1)
#define SCROLLBAR_STYLE_OVERLAY UINT32_C(2)
#define SCROLLBAR_STYLE_MASK \
    (SCROLLBAR_STYLE_ORIENTATION | SCROLLBAR_STYLE_OVERLAY)
#define SCROLLBAR_FADE_FRAMES 48u
#define SCROLL_DAMAGE_RECORDING UINT32_C(0x80000000)
#define SCROLL_DAMAGE_INDEX_MASK UINT32_C(0x7fffffff)
#define SCROLL_DAMAGE_INVALID UINT32_MAX
#define TEXT_VIEW_CELL_BATCH 256u

static uint16_t color(AstraColorRGBA8 value)
{
    return astra_surface_rgb565(value.red, value.green, value.blue);
}

static AstraColorRGBA8 blend(AstraColorRGBA8 background,
                            AstraColorRGBA8 foreground, uint32_t alpha);

static uint32_t field_inset(uint32_t style)
{
    if ((style & ASTRA_FIELD_INLINE) != 0u) return 0u;
    return (style & ASTRA_FIELD_SEARCH) != 0u ?
        FIELD_SEARCH_PADDING_X : FIELD_PADDING_X;
}

static int layout_valid(const AstraFlexLayout *layout);
static void control_damage(AstraUIContext *context, uint32_t index);
static uint32_t control_index(const AstraUIContext *context,
                              const AstraControl *control);
static AstraResult measure_control(const AstraControl *control,
                                   AstraControlSize *size);
static AstraResult measure_container(const AstraUIContext *context,
                                     uint32_t index,
                                     AstraControlSize *size);
static int range_value_representable(int64_t value, int64_t minimum,
                                     int64_t maximum, int64_t step);
static AstraResult activate_control(AstraUIContext *context, uint32_t index,
                                    AstraUIAction *action);
static void animation_sync(AstraUIContext *context, uint32_t index);
static AstraResult splitter_set_position(AstraUIContext *context,
                                         uint32_t index, int64_t position,
                                         AstraUIAction *action);
static int splitter_neighbors(const AstraUIContext *context, uint32_t index,
                              const AstraFlexLayout *root_layout,
                              uint32_t *before, uint32_t *after, int *row);
static AstraResult text_view_measure(const AstraControl *control,
                                     AstraControlSize *size);
static int field_caret_visible_at(uint32_t phase);
static AstraResult grid_view_measure(const AstraControl *control,
                                     AstraControlSize *size);
static AstraResult grid_view_render(const AstraControl *control,
    AstraSurfaceView *surface, uint64_t *commands, uint64_t *payload);
static AstraResult replace_measured_control(AstraUIContext *context,
    AstraControl *control, const AstraControl *replacement);

static AstraTextModel *control_text_model(AstraControl *control)
{
    return (AstraTextModel *)(void *)control->_private_text;
}

static const AstraTextModel *const_control_text_model(const AstraControl *control)
{
    return (const AstraTextModel *)(const void *)control->_private_text;
}

static const AstraGridViewModel *grid_view_model(const AstraControl *control)
{
    return (const AstraGridViewModel *)(const void *)control->_private_text;
}

static int grid_view_model_valid(const AstraGridViewModel *model)
{
    return model != NULL && model->size == sizeof(*model) &&
           model->cells != NULL && model->columns != 0u &&
           (uint64_t)model->columns * model->rows <= model->capacity_cells &&
           astra_words_zero(model->reserved, 2u);
}

static const AstraChoiceItem *choice_items(const AstraControl *control)
{
    return (const AstraChoiceItem *)(const void *)control->_private_text;
}

static AstraCollectionModel *collection_model(AstraControl *control)
{
    return (AstraCollectionModel *)(void *)control->_private_text;
}

static const AstraCollectionModel *const_collection_model(
    const AstraControl *control)
{
    return (const AstraCollectionModel *)(const void *)control->_private_text;
}

static const AstraTableModel *table_model(const AstraControl *control)
{
    if (control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE)
        return ((const AstraOutlineTableModel *)
            control->_private_display_model)->table;
    return (const AstraTableModel *)control->_private_display_model;
}

static AstraOutlineTableModel *outline_table_model(AstraControl *control)
{
    return (AstraOutlineTableModel *)(void *)control->_private_display_model;
}

static const AstraOutlineTableModel *const_outline_table_model(
    const AstraControl *control)
{
    return (const AstraOutlineTableModel *)control->_private_display_model;
}

static int32_t table_header_at(const AstraControl *control, int32_t x,
    int32_t y)
{
    const AstraTableModel *table = table_model(control);
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    int64_t relative_x = (int64_t)x - control->_private_frame.x;
    int64_t relative_y = (int64_t)y - control->_private_frame.y;
    uint64_t left = 0u;

    if (relative_x < 0 || relative_y < 0 ||
        relative_y >= theme.control_height)
        return -1;
    for (uint32_t at = 0u; at < table->column_count; ++at) {
        left += table->columns[at].width;
        if ((uint64_t)relative_x < left) return (int32_t)at;
    }
    return -1;
}

static AstraTreeModel *tree_model(AstraControl *control)
{
    return (AstraTreeModel *)(void *)control->_private_display_model;
}

static const AstraTreeModel *const_tree_model(const AstraControl *control)
{
    return (const AstraTreeModel *)control->_private_display_model;
}

static AstraIconGridModel *icon_grid_model(AstraControl *control)
{
    return (AstraIconGridModel *)(void *)control->_private_display_model;
}

static const AstraIconGridModel *const_icon_grid_model(
    const AstraControl *control)
{
    return (const AstraIconGridModel *)control->_private_display_model;
}

static const AstraViewModel *view_model(const AstraControl *control)
{
    return (const AstraViewModel *)control->_private_display_model;
}

static int view_model_valid(const AstraViewModel *model)
{
    return model != NULL && model->size == sizeof(*model) &&
           model->draw != NULL &&
           model->intrinsic_width <= INT32_MAX &&
           model->intrinsic_height <= INT32_MAX &&
           astra_words_zero(model->reserved, 2u);
}

static int table_cells_valid(const AstraTableCell *cells, uint32_t count)
{
    if (count != 0u && cells == NULL) return 0;
    for (uint32_t at = 0u; at < count; ++at)
        if (cells[at].reserved != 0u ||
            (cells[at].text_length != 0u &&
             !astra_utf8_validate(cells[at].text, cells[at].text_length, 0u)))
            return 0;
    return 1;
}

static int selection_valid(const AstraSelectionSet *selection, uint32_t count);

static int table_model_valid(const AstraTableModel *table)
{
    uint64_t width = 0u;

    if (table == NULL || table->size != sizeof(*table) ||
        table->collection == NULL ||
        table->collection->size != sizeof(AstraCollectionModel) ||
        !selection_valid(table->collection->selection,
            table->collection->item_count) ||
        table->columns == NULL || table->column_count == 0u ||
        table->column_count > 64u ||
        table->cell_count !=
            (uint64_t)table->collection->item_count *
                (table->column_count - 1u) ||
        !astra_words_zero(table->reserved, 2u) ||
        !table_cells_valid(table->cells, table->cell_count))
        return 0;
    for (uint32_t at = 0u; at < table->column_count; ++at) {
        const AstraTableColumn *column = &table->columns[at];

        if (column->heading_length == 0u || column->width == 0u ||
            column->reserved != 0u ||
            !astra_utf8_validate(column->heading,
                                 column->heading_length, 0u))
            return 0;
        width += column->width;
    }
    return width <= INT32_MAX;
}

static int outline_rows_valid(const AstraOutlineTableRow *rows,
    uint32_t count)
{
    if (count != 0u && rows == NULL) return 0;
    for (uint32_t at = 0u; at < count; ++at) {
        if (rows[at].depth >= ASTRA_TREE_MAX_DEPTH ||
            rows[at].expandable > 1u || rows[at].expanded > 1u ||
            rows[at].reserved != 0u ||
            (rows[at].expanded != 0u && rows[at].expandable == 0u) ||
            (at == 0u && rows[at].depth != 0u) ||
            (at != 0u && rows[at].depth > rows[at - 1u].depth + 1u) ||
            (at != 0u && rows[at].depth > rows[at - 1u].depth &&
             rows[at - 1u].expanded == 0u))
            return 0;
    }
    return 1;
}

static int outline_model_valid(const AstraOutlineTableModel *outline)
{
    return outline != NULL && outline->size == sizeof(*outline) &&
        table_model_valid(outline->table) &&
        outline->row_count == outline->table->collection->item_count &&
        outline_rows_valid(outline->rows, outline->row_count) &&
        astra_words_zero(outline->reserved, 2u);
}

static int collection_items_valid(const AstraCollectionItem *items,
                                  uint32_t count)
{
    if (count > INT32_MAX || (count != 0u && items == NULL)) return 0;
    for (uint32_t at = 0u; at < count; ++at) {
        if (items[at].id == 0u || items[at].text_length == 0u ||
            !astra_utf8_validate(items[at].text, items[at].text_length, 0u) ||
            (items[at].presentation.flags & ~ASTRA_COLLECTION_HEADING) != 0u ||
            items[at].presentation.icon > ASTRA_TOOLBAR_ICON_RAM ||
            ((items[at].presentation.flags & ASTRA_COLLECTION_HEADING) != 0u &&
             items[at].presentation.icon != ASTRA_TOOLBAR_ICON_NONE))
            return 0;
        for (uint32_t other = 0u; other < at; ++other)
            if (items[other].id == items[at].id) return 0;
    }
    return 1;
}

static int32_t collection_index(const AstraCollectionItem *items,
                                uint32_t count, uint64_t id)
{
    if (id == 0u) return -1;
    for (uint32_t at = 0u; at < count; ++at)
        if (items[at].id == id) return (int32_t)at;
    return -1;
}

static int32_t collection_selectable_index(const AstraCollectionItem *items,
    uint32_t count, uint64_t id)
{
    int32_t index = collection_index(items, count, id);
    return index >= 0 &&
        (items[index].presentation.flags & ASTRA_COLLECTION_HEADING) == 0u ?
        index : -1;
}

static uint32_t selection_slot(const AstraSelectionSet *selection,
    uint64_t id)
{
    uint32_t lo = 0u, hi = selection->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (selection->ids[mid] < id) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

uint32_t astra_interface_selection_contains(const AstraSelectionSet *selection,
    uint64_t id)
{
    if (selection == NULL || selection->size != sizeof(*selection) ||
        id == 0u || selection->count > selection->capacity ||
        (selection->capacity != 0u && selection->ids == NULL)) return 0u;
    uint32_t slot = selection_slot(selection, id);
    return slot < selection->count && selection->ids[slot] == id;
}

static int selection_valid(const AstraSelectionSet *selection, uint32_t count)
{
    if (selection == NULL) return 1;
    if (selection->size != sizeof(*selection) ||
        selection->capacity < count ||
        selection->count > selection->capacity ||
        (selection->capacity != 0u && selection->ids == NULL) ||
        !astra_words_zero(selection->reserved, 2u)) return 0;
    for (uint32_t at = 0u; at < selection->count; ++at)
        if (selection->ids[at] == 0u ||
            (at != 0u && selection->ids[at] <= selection->ids[at - 1u]))
            return 0;
    return 1;
}

static void selection_single(AstraSelectionSet *selection, uint64_t id)
{
    if (selection == NULL) return;
    selection->count = id == 0u ? 0u : 1u;
    if (id != 0u) selection->ids[0] = id;
    selection->anchor_id = id;
}

static void selection_prune(AstraSelectionSet *selection,
    const AstraCollectionItem *items, uint32_t count)
{
    if (selection == NULL) return;
    uint32_t kept = 0u;
    for (uint32_t at = 0u; at < selection->count; ++at)
        if (collection_selectable_index(items, count,
                selection->ids[at]) >= 0)
            selection->ids[kept++] = selection->ids[at];
    selection->count = kept;
    if (collection_selectable_index(items, count,
            selection->anchor_id) < 0)
        selection->anchor_id = 0u;
}

AstraResult astra_interface_selection_init(AstraSelectionSet *selection,
    uint64_t *ids, uint32_t capacity)
{
    if (selection == NULL || (capacity != 0u && ids == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *selection = (AstraSelectionSet)ASTRA_SELECTION_SET_INIT;
    selection->ids = ids;
    selection->capacity = capacity;
    return ASTRA_OK;
}

AstraResult astra_interface_collection_multi_bind(AstraCollectionModel *model,
    AstraSelectionSet *selection)
{
    if (model == NULL || model->size != sizeof(*model) ||
        model->selection != NULL || !selection_valid(selection,
            model->item_count) || selection == NULL ||
        (model->selected_id != 0u && selection->capacity == 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (selection->count != 0u && model->selected_id != 0u &&
        !astra_interface_selection_contains(selection, model->selected_id))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (selection->count == 0u && selection->anchor_id != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model->selection = selection;
    if (selection->count == 0u)
        selection_single(selection, model->selected_id);
    return ASTRA_OK;
}

static void selection_insert(AstraSelectionSet *selection, uint64_t id)
{
    uint32_t at = selection_slot(selection, id);
    if (at < selection->count && selection->ids[at] == id) return;
    memmove(selection->ids + at + 1u, selection->ids + at,
        (selection->count - at) * sizeof(*selection->ids));
    selection->ids[at] = id;
    ++selection->count;
}

static void selection_remove(AstraSelectionSet *selection, uint64_t id)
{
    uint32_t at = selection_slot(selection, id);
    if (at == selection->count || selection->ids[at] != id) return;
    memmove(selection->ids + at, selection->ids + at + 1u,
        (selection->count - at - 1u) * sizeof(*selection->ids));
    --selection->count;
}

static uint32_t selection_item_at(const AstraControl *control, uint32_t row)
{
    return control->_private_control_kind == ASTRA_CONTROL_TREE ?
        const_tree_model(control)->visible_rows[row] : row;
}

static int collection_row_selectable(const AstraControl *control,
    uint32_t row)
{
    const AstraCollectionModel *model = const_collection_model(control);
    return row < control->_private_text_length &&
        (model->items[selection_item_at(control, row)].presentation.flags &
         ASTRA_COLLECTION_HEADING) == 0u;
}

static int32_t selection_visible_row(const AstraControl *control, uint64_t id)
{
    const AstraCollectionModel *model = const_collection_model(control);
    for (uint32_t row = 0u; row < control->_private_text_length; ++row)
        if (model->items[selection_item_at(control, row)].id == id)
            return (int32_t)row;
    return -1;
}

static int selection_apply(AstraControl *control, uint32_t row,
    uint32_t modifiers)
{
    if (!collection_row_selectable(control, row)) return 0;
    AstraCollectionModel *model = collection_model(control);
    AstraSelectionSet *selection = model->selection;
    uint64_t id = model->items[selection_item_at(control, row)].id;
    uint64_t before = model->selected_id;
    uint32_t extend = (modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u;
    uint32_t toggle = (modifiers & (ASTRA_INPUT_MOD_CTRL |
        ASTRA_INPUT_MOD_GUI)) != 0u;

    if (selection == NULL) {
        model->selected_id = id;
        return before != id;
    }
    if (extend) {
        int32_t anchor = selection_visible_row(control, selection->anchor_id);
        if (anchor < 0) {
            anchor = selection_visible_row(control, before);
            if (anchor < 0) anchor = (int32_t)row;
            selection->anchor_id = model->items[
                selection_item_at(control, (uint32_t)anchor)].id;
        }
        if (!toggle) selection->count = 0u;
        uint32_t first = (uint32_t)anchor < row ? (uint32_t)anchor : row;
        uint32_t last = (uint32_t)anchor > row ? (uint32_t)anchor : row;
        for (uint32_t at = first; at <= last; ++at)
            if (collection_row_selectable(control, at))
                selection_insert(selection, model->items[
                    selection_item_at(control, at)].id);
        model->selected_id = id;
        return 1;
    }
    if (toggle) {
        if (astra_interface_selection_contains(selection, id)) {
            selection_remove(selection, id);
            model->selected_id = selection->count != 0u ?
                selection->ids[0] : 0u;
        } else {
            selection_insert(selection, id);
            model->selected_id = id;
        }
        selection->anchor_id = id;
        return 1;
    }
    if (selection->count == 1u && selection->ids[0] == id && before == id)
        return 0;
    selection_single(selection, id);
    model->selected_id = id;
    return 1;
}

static AstraResult collection_context_select(AstraUIContext *context,
    uint32_t index, int32_t row)
{
    AstraControl *control = &context->_private_controls[index];
    AstraCollectionModel *model = collection_model(control);
    if (row < 0) return ASTRA_OK;
    if ((uint32_t)row >= control->_private_text_length || model == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!collection_row_selectable(control, (uint32_t)row)) return ASTRA_OK;
    uint64_t id = model->items[selection_item_at(control, (uint32_t)row)].id;
    if (model->selection == NULL ||
        !astra_interface_selection_contains(model->selection, id))
        return astra_interface_control_set_value(context, control, row);
    if (model->selected_id == id && control->_private_value == row)
        return ASTRA_OK;
    control_damage(context, index);
    model->selected_id = id;
    control->_private_value = row;
    return ASTRA_OK;
}

AstraResult astra_interface_collection_mirror_primary(AstraUIContext *context,
    AstraControl *target, const AstraCollectionModel *source)
{
    uint32_t index = control_index(context, target);
    AstraCollectionModel *model;
    int32_t row;

    if (index == CONTROL_NONE || source == NULL ||
        !COLLECTION_KINDS(target->_private_control_kind))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model = collection_model(target);
    if (model == NULL || model->size != sizeof(*model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row = selection_visible_row(target, source->selected_id);
    if (source->selected_id != 0u && row < 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (model->selection != NULL && model->selection == source->selection) {
        if (model->selected_id != source->selected_id) {
            control_damage(context, index);
            model->selected_id = source->selected_id;
            target->_private_value = row;
        }
        return ASTRA_OK;
    }
    return astra_interface_control_set_value(context, target, row);
}

static int icon_grid_model_valid(const AstraIconGridModel *grid,
                                 int validate_items)
{
    if (grid == NULL || grid->size != sizeof(*grid) ||
        grid->collection == NULL ||
        grid->collection->size != sizeof(AstraCollectionModel) ||
        !selection_valid(grid->collection->selection,
            grid->collection->item_count) ||
        !astra_words_zero(grid->collection->reserved, 2u) ||
        (validate_items ?
            !collection_items_valid(grid->collection->items,
                                    grid->collection->item_count) :
            (grid->collection->item_count != 0u &&
             grid->collection->items == NULL)) ||
        grid->icon_count != grid->collection->item_count ||
        (grid->icon_count != 0u && grid->icons == NULL) ||
        grid->columns == 0u || grid->columns > 256u ||
        (grid->tile_style != ASTRA_ICON_TILE_CLIENT &&
         grid->tile_style != ASTRA_ICON_TILE_DESKTOP) ||
        !astra_words_zero(grid->reserved, 2u))
        return 0;
    for (uint32_t at = 0u; at < grid->icon_count; ++at) {
        const AstraSurface *icon = grid->icons[at];

        if (icon != NULL &&
            (icon->_private_handle == ASTRA_INVALID_HANDLE ||
             icon->_private_id == 0u || icon->_private_window == 0u ||
             icon->_private_width < ASTRA_ICON_TILE_SOURCE_SIZE ||
             icon->_private_height < ASTRA_ICON_TILE_SOURCE_SIZE))
            return 0;
    }
    return 1;
}

static int32_t tree_visible_index(const AstraTreeModel *tree, uint64_t id)
{
    if (id == 0u) return -1;
    for (uint32_t row = 0u; row < tree->visible_count; ++row)
        if (tree->collection->items[tree->visible_rows[row]].id == id)
            return (int32_t)row;
    return -1;
}

static void tree_normalize_selection(AstraTreeModel *tree)
{
    uint64_t id = tree->collection->selected_id;

    while (id != 0u && tree_visible_index(tree, id) < 0) {
        int32_t index = collection_index(tree->collection->items,
                                          tree->node_count, id);
        id = index < 0 ? 0u : tree->nodes[index].parent_id;
    }
    tree->collection->selected_id = id;
}

static int tree_has_children(const AstraTreeModel *tree, uint32_t item)
{
    return item + 1u < tree->node_count &&
        tree->nodes[item + 1u].depth > tree->nodes[item].depth;
}

static int tree_rebuild(AstraTreeModel *tree, int validate_items)
{
    uint64_t ancestors[ASTRA_TREE_MAX_DEPTH];
    uint32_t indices[ASTRA_TREE_MAX_DEPTH];
    uint8_t visible[ASTRA_TREE_MAX_DEPTH];
    uint32_t previous_depth = 0u;

    if (tree == NULL || tree->size != sizeof(*tree) ||
        tree->collection == NULL ||
        tree->collection->size != sizeof(AstraCollectionModel) ||
        !selection_valid(tree->collection->selection,
            tree->collection->item_count) ||
        tree->node_count != tree->collection->item_count ||
        tree->visible_capacity < tree->node_count ||
        (tree->node_count != 0u &&
         (tree->nodes == NULL || tree->visible_rows == NULL)) ||
        !astra_words_zero(tree->reserved, 2u) ||
        (validate_items &&
         !collection_items_valid(tree->collection->items, tree->node_count)))
        return 0;

    /* Validate preorder and depth without modifying caller-owned state. */
    for (uint32_t item = 0u; item < tree->node_count; ++item) {
        uint32_t depth = 0u;

        if (tree->nodes[item].expanded > 1u ||
            (item == 0u && tree->nodes[item].parent_id != 0u))
            return 0;
        if (tree->nodes[item].parent_id != 0u) {
            while (depth <= previous_depth &&
                   ancestors[depth] != tree->nodes[item].parent_id)
                ++depth;
            if (depth > previous_depth ||
                depth + 1u >= ASTRA_TREE_MAX_DEPTH)
                return 0;
            ++depth;
        }
        ancestors[depth] = tree->collection->items[item].id;
        previous_depth = depth;
    }

    tree->visible_count = 0u;
    previous_depth = 0u;
    for (uint32_t item = 0u; item < tree->node_count; ++item) {
        uint32_t depth = 0u;

        if (tree->nodes[item].parent_id != 0u) {
            while (depth <= previous_depth &&
                   ancestors[depth] != tree->nodes[item].parent_id)
                ++depth;
            if (depth > previous_depth) return 0;
            ++depth;
        }
        tree->nodes[item].depth = depth;
        visible[depth] = depth == 0u ||
            (visible[depth - 1u] && tree->nodes[indices[depth - 1u]].expanded);
        if (visible[depth])
            tree->visible_rows[tree->visible_count++] = item;
        ancestors[depth] = tree->collection->items[item].id;
        indices[depth] = item;
        previous_depth = depth;
    }
    return 1;
}

static AstraScrollModel *scroll_model(AstraControl *control)
{
    return (AstraScrollModel *)(void *)control->_private_text;
}

static const AstraScrollModel *const_scroll_model(const AstraControl *control)
{
    return (const AstraScrollModel *)(const void *)control->_private_text;
}

static uint32_t private_u32(const int64_t *value)
{
    return (uint32_t)*value;
}

static void private_u32_set(int64_t *value, uint32_t bits)
{
    *value = bits;
}

int astra_interface_single_line_text_valid(const char *text, uint32_t bytes)
{
    uint32_t at = 0u;

    if (!astra_utf8_validate(text, bytes, 0u))
        return 0;
    while (at < bytes) {
        uint32_t consumed = 0u;
        uint32_t scalar = astra_utf8_decode(text + at, bytes - at,
                                            &consumed);

        if (scalar < 0x20u || (scalar >= 0x7fu && scalar <= 0x9fu) ||
            scalar == 0x2028u || scalar == 0x2029u)
            return 0;
        at += consumed;
    }
    return 1;
}

static int field_content_valid(const AstraTextModel *model)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t offset = 0u;

    if (astra_text_model_get_state(model, &state) != ASTRA_OK ||
        state.line_count != 1u)
        return 0;
    while (offset < state.text_bytes) {
        const char *text;
        uint32_t bytes;
        if (astra_text_model_read(model, offset, &text, &bytes) != ASTRA_OK ||
            bytes == 0u || !astra_interface_single_line_text_valid(text, bytes))
            return 0;
        offset += bytes;
    }
    return 1;
}

static int control_valid(const AstraControl *control)
{
    uint32_t state;

    if (control == NULL ||
        control->_private_structure_size != sizeof(*control) ||
        control->_private_id == 0u)
        return 0;
    state = control->_private_state;
    if ((state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (control->_private_preview_state & ~CONTROL_PREVIEW_STATES) != 0u ||
        (control->_private_dynamic_state & ~CONTROL_DYNAMIC_STATES) != 0u ||
        control->_private_minimum_width > control->_private_maximum_width ||
        control->_private_minimum_height > control->_private_maximum_height ||
        control->_private_align_self > ASTRA_FLEX_ALIGN_AUTO ||
        (control->_private_flex_flags & ~FLEX_ITEM_FLAGS) != 0u ||
        (control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         (control->_private_reset_value != 0 ||
          control->_private_decimal_places != 0u)))
        return 0;
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               (control->_private_state &
                ~ASTRA_CONTROL_COLLAPSED) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_style <= ASTRA_CONTAINER_STYLE_INSPECTOR &&
               (control->_private_style == ASTRA_CONTAINER_STYLE_DROP_TARGET ?
                    (control->_private_value >= ASTRA_DROP_TARGET_IDLE &&
                     control->_private_value <= ASTRA_DROP_TARGET_REFUSE) :
                    control->_private_value == 0) &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0 &&
               layout_valid(&control->_private_child_layout);
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
        AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style != 0u &&
               (control->_private_state & ~ASTRA_CONTROL_COLLAPSED) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value_step >= 0 &&
               control->_private_value_step <= 1 &&
               astra_scroll_get_state(
                   const_scroll_model(control), &scroll) == ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
        AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

        return control->_private_text != NULL &&
               control->_private_text_length == 0u &&
               (control->_private_style & ~SCROLLBAR_STYLE_MASK) == 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0 &&
               astra_scroll_get_state(
                   const_scroll_model(control), &scroll) == ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style <= ASTRA_ORIENTATION_VERTICAL &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u;
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style >= ASTRA_TEXT_CLIENT_PRIMARY &&
               control->_private_style <= ASTRA_TEXT_CLIENT_FAULT &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_dynamic_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style <= ASTRA_COLOUR_PALETTE_SLOT &&
               (state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value >= 0 &&
               control->_private_value <= UINT16_MAX &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value >= 0 &&
               control->_private_maximum_value <= 255 &&
               (control->_private_style == ASTRA_COLOUR_PALETTE_SLOT ||
                control->_private_maximum_value == 0) &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               (control->_private_style & ASTRA_BUTTON_STYLE_MASK) <=
                   ASTRA_BUTTON_TAG &&
               (control->_private_style &
                ~(ASTRA_BUTTON_STYLE_MASK | ASTRA_BUTTON_ICON_MASK |
                  ASTRA_BUTTON_ICON_ONLY)) == 0u &&
               ASTRA_BUTTON_ICON(control->_private_style) <=
                   ASTRA_TOOLBAR_ICON_RAM &&
               (ASTRA_BUTTON_ICON(control->_private_style) == 0u ||
                (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
                    ASTRA_BUTTON_TOOLBAR) &&
               ((control->_private_style & ASTRA_BUTTON_ICON_ONLY) == 0u ||
                ((control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
                    ASTRA_BUTTON_TOOLBAR &&
                 ASTRA_BUTTON_ICON(control->_private_style) != 0u)) &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style != 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (TOGGLE_KINDS(control->_private_control_kind))
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               (control->_private_control_kind == ASTRA_CONTROL_RADIO ?
                    control->_private_style != 0u :
                    control->_private_style == 0u) &&
               control->_private_preview_state == 0u &&
               control->_private_value == 0 &&
               control->_private_minimum_value == 0 &&
               control->_private_maximum_value == 0 &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style <= ASTRA_ORIENTATION_VERTICAL &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    if (control->_private_control_kind == ASTRA_CONTROL_DIAL) {
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               control->_private_reset_value >=
                   control->_private_minimum_value &&
               control->_private_reset_value <=
                   control->_private_maximum_value &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step) &&
               range_value_representable(
                   control->_private_reset_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    }
    if (control->_private_control_kind == ASTRA_CONTROL_STEPPER)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value <
                   control->_private_maximum_value &&
               control->_private_value >= control->_private_minimum_value &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step > 0 &&
               range_value_representable(
                   control->_private_value,
                   control->_private_minimum_value,
                   control->_private_maximum_value,
                   control->_private_value_step);
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        return control->_private_text == NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_value >= 0 &&
               control->_private_maximum_value >= 0 &&
               control->_private_value <= control->_private_maximum_value &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER ||
        control->_private_control_kind == ASTRA_CONTROL_SPARKLINE)
        return astra_interface_display_model_valid(control);
    if (control->_private_control_kind == ASTRA_CONTROL_TREE) {
        const AstraTreeModel *tree = const_tree_model(control);
        const AstraCollectionModel *model = const_collection_model(control);

        return tree != NULL && model != NULL &&
               tree->size == sizeof(*tree) && tree->collection == model &&
               selection_valid(model->selection, model->item_count) &&
               tree->node_count == model->item_count &&
               tree->visible_count == control->_private_text_length &&
               tree->visible_capacity >= tree->node_count &&
               (tree->visible_count == 0u || tree->visible_rows != NULL) &&
               (tree->node_count == 0u || tree->nodes != NULL) &&
               (control->_private_value < 0 ? model->selected_id == 0u :
                control->_private_value < (int64_t)tree->visible_count &&
                model->items[tree->visible_rows[control->_private_value]].id ==
                    model->selected_id) &&
               control->_private_minimum_value >= -1 &&
               control->_private_maximum_value ==
                   (int64_t)tree->visible_count - 1 &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
        const AstraIconGridModel *grid = const_icon_grid_model(control);
        const AstraCollectionModel *model = const_collection_model(control);

        return grid != NULL && model != NULL &&
               grid->collection == model && icon_grid_model_valid(grid, 0) &&
               grid->icon_count == control->_private_text_length &&
               (control->_private_value < 0 ? model->selected_id == 0u :
                control->_private_value < (int64_t)grid->icon_count &&
                model->items[control->_private_value].id ==
                    model->selected_id) &&
               control->_private_minimum_value >= -1 &&
               control->_private_maximum_value ==
                   (int64_t)grid->icon_count - 1 &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_VIEW)
        return view_model_valid(view_model(control)) &&
               control->_private_style == 0u &&
               (state & ~(ASTRA_CONTROL_DISABLED |
                          ASTRA_CONTROL_COLLAPSED)) == 0u &&
               control->_private_preview_state == 0u;
    if (control->_private_control_kind == ASTRA_CONTROL_LIST ||
        TABLE_KINDS(control->_private_control_kind))
        return control->_private_text != NULL &&
               (control->_private_control_kind == ASTRA_CONTROL_LIST ?
                    control->_private_display_model == NULL :
                    (control->_private_control_kind !=
                         ASTRA_CONTROL_OUTLINE_TABLE ||
                     (const_outline_table_model(control) != NULL &&
                      const_outline_table_model(control)->size ==
                          sizeof(AstraOutlineTableModel) &&
                      const_outline_table_model(control)->row_count ==
                          control->_private_text_length &&
                      (control->_private_text_length == 0u ||
                       const_outline_table_model(control)->rows != NULL))) &&
                    table_model(control) != NULL &&
                    table_model(control)->collection ==
                        const_collection_model(control) &&
                    table_model(control)->column_count != 0u &&
                    table_model(control)->column_count <= 64u &&
                    table_model(control)->cell_count ==
                        (uint64_t)control->_private_text_length *
                        (table_model(control)->column_count - 1u)) &&
               const_collection_model(control)->size ==
                   sizeof(AstraCollectionModel) &&
               selection_valid(const_collection_model(control)->selection,
                   const_collection_model(control)->item_count) &&
               const_collection_model(control)->item_count ==
                   control->_private_text_length &&
               (control->_private_text_length == 0u ||
                const_collection_model(control)->items != NULL) &&
               control->_private_value >= -1 &&
               control->_private_value <
                   (int64_t)control->_private_text_length &&
               (control->_private_value < 0 ?
                const_collection_model(control)->selected_id == 0u :
                const_collection_model(control)->items[
                    control->_private_value].id ==
                    const_collection_model(control)->selected_id) &&
               (control->_private_control_kind == ASTRA_CONTROL_LIST ?
                    control->_private_style == 0u :
                    (control->_private_style == 0u ||
                     ((control->_private_style & ~TABLE_SORT_MASK) == 0u &&
                      (control->_private_style & TABLE_SORT_ACTIVE) != 0u &&
                      (control->_private_style & TABLE_SORT_COLUMN_MASK) <
                          table_model(control)->column_count))) &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value >= -1 &&
               control->_private_value <=
                   (int64_t)control->_private_text_length - 1 &&
               control->_private_minimum_value >= -1 &&
               control->_private_minimum_value <=
                   (int64_t)control->_private_text_length - 1 &&
               control->_private_maximum_value ==
                   (int64_t)control->_private_text_length - 1 &&
               control->_private_value_step == 0;
    if (CHOICE_KINDS(control->_private_control_kind))
        return control->_private_text != NULL &&
               control->_private_text_length != 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_value >= 0 &&
               (uint32_t)control->_private_value <
                   control->_private_text_length &&
               control->_private_minimum_value >= -1 &&
               (uint32_t)(control->_private_minimum_value + 1) <=
                   control->_private_text_length &&
               control->_private_maximum_value ==
                   (int32_t)(control->_private_text_length - 1u) &&
               control->_private_value_step == 0;
    if (control->_private_control_kind == ASTRA_CONTROL_GRID_VIEW)
        return control->_private_text != NULL &&
               control->_private_text_length == 0u &&
               control->_private_style == 0u &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               grid_view_model_valid(grid_view_model(control));
    if (TEXT_MODEL_KINDS(control->_private_control_kind)) {
        AstraTextModelState model_state = ASTRA_TEXT_MODEL_STATE_INIT;

        return control->_private_text != NULL &&
               (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW ?
                    control->_private_text_length == 0u :
                    control->_private_text_length != 0u) &&
               (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW ?
                    (control->_private_style &
                     ~(ASTRA_FIELD_READ_ONLY | TEXT_VIEW_INVERTED |
                       TEXT_VIEW_WRAP | TEXT_VIEW_FLOW |
                       TEXT_VIEW_LINE_NUMBERS)) == 0u :
                    (control->_private_style & ~FIELD_FLAGS) == 0u) &&
               (state & ~CONTROL_VALUE_STATES) == 0u &&
               control->_private_preview_state == 0u &&
               control->_private_minimum_value == 0 &&
               control->_private_value_step == 0 &&
               astra_text_model_get_state(
                   const_control_text_model(control), &model_state) == ASTRA_OK &&
               (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW ?
                    1 :
                    model_state.line_count == 1u);
    }
    return 0;
}

static int context_valid(const AstraUIContext *context)
{
    return context != NULL &&
           context->_private_structure_size == sizeof(*context) &&
           context->_private_width != 0u && context->_private_height != 0u &&
           (context->_private_control_count == 0u ||
            context->_private_controls != NULL) &&
           context->_private_has_layout <= 1u &&
           (context->_private_has_layout == 0u ||
            layout_valid(&context->_private_layout)) &&
           (context->_private_first_child == CONTROL_NONE ||
            context->_private_first_child < context->_private_control_count) &&
           (context->_private_last_child == CONTROL_NONE ||
            context->_private_last_child < context->_private_control_count) &&
           (context->_private_animation_first == CONTROL_NONE ||
            context->_private_animation_first <
                context->_private_control_count) &&
           (context->_private_reserved == 0u ||
            context->_private_reserved == SCROLL_DAMAGE_INVALID ||
            (((context->_private_reserved & SCROLL_DAMAGE_INDEX_MASK) != 0u) &&
             (context->_private_reserved & SCROLL_DAMAGE_INDEX_MASK) - 1u <
                 context->_private_control_count)) &&
           (context->_private_value_input_digits <= 19u ||
            (context->_private_capture < context->_private_control_count &&
             context->_private_controls[context->_private_capture]
                     ._private_control_kind == ASTRA_CONTROL_DIAL)) &&
           (context->_private_value_input_flags & ~VALUE_INPUT_FLAGS) == 0u &&
           context->_private_hover_part <= SCROLLBAR_PART_AFTER &&
           (context->_private_capture_part <= SCROLLBAR_PART_AFTER ||
            (context->_private_capture < context->_private_control_count &&
             TABLE_KINDS(context->_private_controls[
                 context->_private_capture]._private_control_kind) &&
             table_model(&context->_private_controls[
                 context->_private_capture]) != NULL &&
             context->_private_capture_part <= table_model(
                 &context->_private_controls[
                     context->_private_capture])->column_count));
}

static uint32_t control_index(const AstraUIContext *context,
                              const AstraControl *control)
{
    if (!context_valid(context) || control == NULL)
        return CONTROL_NONE;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (&context->_private_controls[at] == control)
            return at;
    return CONTROL_NONE;
}

AstraResult astra_interface_control_invalidate(AstraUIContext *context,
    const AstraControl *control)
{
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE || !control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control_damage(context, index);
    return ASTRA_OK;
}

static uint32_t control_id_index(const AstraUIContext *context, uint32_t id)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_id == id)
            return at;
    return CONTROL_NONE;
}

static uint32_t effective_state(const AstraControl *control)
{
    uint32_t state = control->_private_state | control->_private_preview_state |
                     control->_private_dynamic_state;

    if ((control->_private_preview_state & ASTRA_CONTROL_FOCUSED) != 0u)
        state |= CONTROL_FOCUS_VISIBLE;
    return state;
}

static void damage_add(AstraUIContext *context,
                       const AstraControlFrame *frame, uint32_t state,
                       const AstraControlFrame *clip)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t old_right;
    int64_t old_bottom;

    if (frame->width == 0u || frame->height == 0u)
        return;
    left = frame->x;
    top = frame->y;
    right = left + frame->width;
    bottom = top + frame->height;
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) {
        left -= theme.focus_width;
        top -= theme.focus_width;
        right += theme.focus_width;
        bottom += theme.focus_width;
    }
    if (left < clip->x) left = clip->x;
    if (top < clip->y) top = clip->y;
    if (right > (int64_t)clip->x + clip->width)
        right = (int64_t)clip->x + clip->width;
    if (bottom > (int64_t)clip->y + clip->height)
        bottom = (int64_t)clip->y + clip->height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > context->_private_width) right = context->_private_width;
    if (bottom > context->_private_height) bottom = context->_private_height;
    if (left >= right || top >= bottom)
        return;
    if (context->_private_has_damage == 0u) {
        context->_private_damage = (AstraControlFrame){
            (int32_t)left, (int32_t)top,
            (uint32_t)(right - left), (uint32_t)(bottom - top)};
        context->_private_has_damage = 1u;
        return;
    }
    old_right = (int64_t)context->_private_damage.x +
                context->_private_damage.width;
    old_bottom = (int64_t)context->_private_damage.y +
                 context->_private_damage.height;
    if (left > context->_private_damage.x)
        left = context->_private_damage.x;
    if (top > context->_private_damage.y)
        top = context->_private_damage.y;
    if (right < old_right) right = old_right;
    if (bottom < old_bottom) bottom = old_bottom;
    context->_private_damage = (AstraControlFrame){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)};
}

static void control_damage(AstraUIContext *context, uint32_t index)
{
    AstraControl *control;

    if (context->_private_reserved != 0u &&
        (context->_private_reserved & SCROLL_DAMAGE_RECORDING) == 0u)
        context->_private_reserved = SCROLL_DAMAGE_INVALID;

    if (context->_private_has_damage != 0u &&
        context->_private_damage.x == 0 &&
        context->_private_damage.y == 0 &&
        context->_private_damage.width == context->_private_width &&
        context->_private_damage.height == context->_private_height)
        return;
    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    control = &context->_private_controls[index];
    if (control->_private_laid_out != 0u)
        damage_add(context, &control->_private_frame,
                   effective_state(control), &control->_private_clip);
}

static void disclosure_target_damage(AstraUIContext *context,
                                     uint32_t target_id)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_control_kind ==
                ASTRA_CONTROL_DISCLOSURE &&
            context->_private_controls[at]._private_style == target_id)
            control_damage(context, at);
}

static void dynamic_state(AstraUIContext *context, uint32_t index,
                          uint32_t mask, int enabled)
{
    AstraControl *control;
    uint32_t before;

    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    control = &context->_private_controls[index];
    before = effective_state(control);
    if (enabled)
        control->_private_dynamic_state |= mask;
    else
        control->_private_dynamic_state &= ~mask;
    if (before != effective_state(control)) {
        damage_add(context, &control->_private_frame, before,
                   &control->_private_clip);
        control_damage(context, index);
    }
}

static void set_hover(AstraUIContext *context, uint32_t index)
{
    uint32_t previous = context->_private_hover;

    if (context->_private_hover == index)
        return;
    dynamic_state(context, previous, ASTRA_CONTROL_HOVERED, 0);
    if (previous != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[previous]
                         ._private_control_kind)) {
        context->_private_controls[previous]._private_minimum_value = -1;
        control_damage(context, previous);
    }
    context->_private_hover = index;
    dynamic_state(context, index, ASTRA_CONTROL_HOVERED, 1);
    if (previous != CONTROL_NONE &&
        context->_private_controls[previous]._private_control_kind ==
            ASTRA_CONTROL_SCROLLBAR)
        animation_sync(context, previous);
    if (index != CONTROL_NONE &&
        context->_private_controls[index]._private_control_kind ==
            ASTRA_CONTROL_SCROLLBAR) {
        context->_private_controls[index]._private_animation_phase = 0u;
        animation_sync(context, index);
    }
}

static int control_animates(const AstraUIContext *context, uint32_t index)
{
    const AstraControl *control;

    if (index >= context->_private_control_count)
        return 0;
    control = &context->_private_controls[index];
    if ((control->_private_state & ASTRA_CONTROL_DISABLED) != 0u)
        return 0;
    if (context->_private_has_layout != 0u &&
        control->_private_laid_out == 0u)
        return 0;
    return (control->_private_control_kind == ASTRA_CONTROL_PROGRESS &&
            control->_private_maximum_value == 0) ||
           (control->_private_control_kind == ASTRA_CONTROL_FIELD &&
            context->_private_focus == index) ||
           (control->_private_control_kind == ASTRA_CONTROL_STEPPER &&
            context->_private_capture == index &&
            (control->_private_dynamic_state & ASTRA_CONTROL_PRESSED) != 0u &&
            context->_private_capture_part != STEPPER_PART_VALUE) ||
           (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR &&
            (control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
            (((effective_state(control) &
               (ASTRA_CONTROL_HOVERED | ASTRA_CONTROL_PRESSED)) != 0u) ||
             control->_private_animation_phase < SCROLLBAR_FADE_FRAMES));
}

static void animation_sync(AstraUIContext *context, uint32_t index)
{
    uint32_t previous = CONTROL_NONE;
    uint32_t at = context->_private_animation_first;

    if (index == CONTROL_NONE || index >= context->_private_control_count)
        return;
    while (at != CONTROL_NONE && at != index) {
        if (at >= context->_private_control_count)
            return;
        previous = at;
        at = context->_private_controls[at]._private_animation_next;
    }
    if (!control_animates(context, index)) {
        if (at == index) {
            uint32_t next =
                context->_private_controls[index]._private_animation_next;

            if (previous == CONTROL_NONE)
                context->_private_animation_first = next;
            else
                context->_private_controls[previous]
                    ._private_animation_next = next;
            context->_private_controls[index]._private_animation_next =
                CONTROL_NONE;
        }
        return;
    }
    if (at == index)
        return;
    if (context->_private_animation_first == CONTROL_NONE) {
        context->_private_animation_time_low = 0u;
        context->_private_animation_time_high = 0u;
        context->_private_animation_phase = 0u;
    }
    context->_private_controls[index]._private_animation_phase = 0u;
    context->_private_controls[index]._private_animation_next =
        context->_private_animation_first;
    context->_private_animation_first = index;
}

static void set_focus(AstraUIContext *context, uint32_t index, int visible)
{
    uint32_t before = context->_private_focus;

    if (context->_private_focus == index) {
        dynamic_state(context, index, CONTROL_FOCUS_VISIBLE, visible);
        return;
    }
    context->_private_value_input = 0u;
    context->_private_value_input_digits = 0u;
    context->_private_value_input_flags = 0u;
    dynamic_state(context, before,
                  ASTRA_CONTROL_FOCUSED | CONTROL_FOCUS_VISIBLE, 0);
    context->_private_focus = index;
    animation_sync(context, before);
    animation_sync(context, index);
    dynamic_state(context, index, ASTRA_CONTROL_FOCUSED, 1);
    dynamic_state(context, index, CONTROL_FOCUS_VISIBLE, visible);
}

static void clear_capture(AstraUIContext *context)
{
    uint32_t captured = context->_private_capture;

    dynamic_state(context, captured,
                  ASTRA_CONTROL_PRESSED, 0);
    if (captured < context->_private_control_count &&
        context->_private_controls[captured]._private_control_kind ==
            ASTRA_CONTROL_DIAL) {
        context->_private_value_input = 0u;
        context->_private_value_input_digits = 0u;
        context->_private_value_input_flags = 0u;
    }
    context->_private_capture = CONTROL_NONE;
    context->_private_capture_part = STEPPER_PART_VALUE;
    animation_sync(context, captured);
}

static void clear_keyboard(AstraUIContext *context)
{
    dynamic_state(context, context->_private_keyboard,
                  ASTRA_CONTROL_PRESSED, 0);
    context->_private_keyboard = CONTROL_NONE;
}

static void reset_interaction(AstraUIContext *context, int clear_focus)
{
    set_hover(context, CONTROL_NONE);
    clear_capture(context);
    clear_keyboard(context);
    if (clear_focus)
        set_focus(context, CONTROL_NONE, 0);
}

static int focusable(const AstraControl *control)
{
    return control->_private_control_kind == ASTRA_CONTROL_BUTTON ||
           control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL ||
           control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE ||
           TOGGLE_KINDS(control->_private_control_kind) ||
           control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
           control->_private_control_kind == ASTRA_CONTROL_DIAL ||
           control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
           CHOICE_KINDS(control->_private_control_kind) ||
           control->_private_control_kind == ASTRA_CONTROL_FIELD ||
           control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW ||
           control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW ||
           control->_private_control_kind == ASTRA_CONTROL_SPLITTER;
}

static int interactive(const AstraControl *control)
{
    return focusable(control) ||
           control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR;
}

static uint32_t hit_test(const AstraUIContext *context, int32_t x, int32_t y,
                          int context_request)
{
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        const AstraControl *control =
            &context->_private_controls[count - 1u];
        const AstraControlFrame *frame = &control->_private_frame;
        const AstraControlFrame *clip = &control->_private_clip;
        int64_t right = (int64_t)frame->x + frame->width;
        int64_t bottom = (int64_t)frame->y + frame->height;
        int64_t clip_right = (int64_t)clip->x + clip->width;
        int64_t clip_bottom = (int64_t)clip->y + clip->height;

        if ((context_request || interactive(control)) &&
            control->_private_laid_out != 0u &&
            (context_request || (control->_private_state & ASTRA_CONTROL_DISABLED) == 0u) &&
            x >= frame->x && (int64_t)x < right &&
            y >= frame->y && (int64_t)y < bottom &&
            x >= clip->x && (int64_t)x < clip_right &&
            y >= clip->y && (int64_t)y < clip_bottom)
            return count - 1u;
    }
    return CONTROL_NONE;
}

static uint32_t next_focus(const AstraUIContext *context, int backwards)
{
    uint32_t count = context->_private_control_count;
    uint32_t start;

    if (count == 0u)
        return CONTROL_NONE;
    start = context->_private_focus == CONTROL_NONE ?
        (backwards ? 0u : count - 1u) : context->_private_focus;
    for (uint32_t step = 1u; step <= count; ++step) {
        uint32_t index = backwards ?
            (start + count - (step % count)) % count :
            (start + step) % count;
        const AstraControl *control = &context->_private_controls[index];

        if (focusable(control) &&
            control->_private_laid_out != 0u &&
            (control->_private_state & ASTRA_CONTROL_DISABLED) == 0u)
            return index;
    }
    return CONTROL_NONE;
}

static int label_font_valid(const AstraLabelFont *font)
{
    if (font == NULL || font->size < sizeof(*font) || font->reserved16 != 0u ||
        !astra_words_zero(font->reserved, 2u)) return 0;
    if (font->gui == ASTRA_INVALID_HANDLE)
        return font->id == NULL && font->id_length == 0u && font->pixel_height == 0u;
    return font->id != NULL && font->id_length != 0u &&
        font->id_length < ASTRA_FONT_ID_MAX && font->pixel_height != 0u &&
        memchr(font->id, '\0', font->id_length) == NULL;
}

AstraResult astra_interface_label_init(AstraControl *control,
                                       const AstraLabelInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        info->text_role < ASTRA_TEXT_CLIENT_PRIMARY ||
        info->text_role > ASTRA_TEXT_CLIENT_FAULT ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 3u) || !label_font_valid(&info->font))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_LABEL;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->text_role;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    control->_private_label_font = info->font;
    return ASTRA_OK;
}

AstraResult astra_interface_button_init(AstraControl *control,
                                        const AstraButtonInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        info->variant > ASTRA_BUTTON_TAG ||
        (info->state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (info->preview_state & ~CONTROL_PREVIEW_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_BUTTON;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_preview_state = info->preview_state;
    control->_private_style = info->variant;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_split_button_init(AstraControl *primary,
    AstraControl *menu, const AstraButtonInfo *primary_info, uint32_t menu_id)
{
    static const char marker[] = "v";
    AstraButtonInfo menu_info = ASTRA_BUTTON_INFO_INIT;
    AstraControl first = ASTRA_CONTROL_INIT;
    AstraControl second = ASTRA_CONTROL_INIT;
    AstraResult result;

    if (primary == NULL || menu == NULL || primary == menu ||
        primary_info == NULL || primary_info->variant != ASTRA_BUTTON_TOOLBAR ||
        menu_id == 0u || menu_id == primary_info->id)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_interface_button_init(&first, primary_info);
    if (result != ASTRA_OK) return result;
    menu_info.id = menu_id;
    menu_info.text = marker;
    menu_info.text_length = 1u;
    menu_info.variant = ASTRA_BUTTON_SPLIT_MENU;
    menu_info.state = primary_info->state;
    result = astra_interface_button_init(&second, &menu_info);
    if (result != ASTRA_OK) return result;
    *primary = first;
    *menu = second;
    return ASTRA_OK;
}

AstraResult astra_interface_disclosure_init(
    AstraControl *control, const AstraDisclosureInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->target_id == 0u ||
        info->id == info->target_id || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_DISCLOSURE;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->target_id;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_container_init(AstraControl *control,
                                           const AstraContainerInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || !layout_valid(&info->layout) ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_CONTAINER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_child_layout = info->layout;
    return ASTRA_OK;
}

AstraResult astra_interface_scroll_view_init(
    AstraControl *control, const AstraScrollViewInfo *info)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->preferred_width == 0u || info->preferred_height == 0u ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_scroll_get_state(info->model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SCROLL_VIEW;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_text_length = info->preferred_width;
    control->_private_style = info->preferred_height;
    return ASTRA_OK;
}

AstraResult astra_interface_scrollbar_init(
    AstraControl *control, const AstraScrollbarInfo *info)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->flags & ~ASTRA_SCROLLBAR_OVERLAY) != 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_scroll_get_state(info->model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SCROLLBAR;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_style = info->orientation |
        ((info->flags & ASTRA_SCROLLBAR_OVERLAY) != 0u ?
             SCROLLBAR_STYLE_OVERLAY : 0u);
    return ASTRA_OK;
}

AstraResult astra_interface_splitter_init(
    AstraControl *control, const AstraSplitterInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SPLITTER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->orientation;
    return ASTRA_OK;
}

static AstraResult toggle_init(AstraControl *control,
                               const AstraToggleInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->text_length == 0u ||
        !astra_utf8_validate(info->text, info->text_length, 0u) ||
        (info->state & ~CONTROL_SEMANTIC_STATES) != 0u ||
        (kind == ASTRA_CONTROL_RADIO ? info->group_id == 0u :
                                      info->group_id != 0u) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = kind;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->group_id;
    control->_private_text = info->text;
    control->_private_text_length = info->text_length;
    return ASTRA_OK;
}

AstraResult astra_interface_checkbox_init(AstraControl *control,
                                          const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_CHECKBOX);
}

AstraResult astra_interface_radio_init(AstraControl *control,
                                       const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_RADIO);
}

AstraResult astra_interface_switch_init(AstraControl *control,
                                        const AstraToggleInfo *info)
{
    return toggle_init(control, info, ASTRA_CONTROL_SWITCH);
}

AstraResult astra_interface_slider_init(AstraControl *control,
                                        const AstraRangeInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        info->orientation > ASTRA_ORIENTATION_VERTICAL ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_SLIDER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->orientation;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    return ASTRA_OK;
}

static uint64_t range_offset(int64_t value, int64_t minimum)
{
    return (uint64_t)value - (uint64_t)minimum;
}

static int64_t range_value_at(int64_t minimum, uint64_t offset)
{
    uint64_t bits = (uint64_t)minimum + offset;
    int64_t value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_value_representable(int64_t value, int64_t minimum,
                                     int64_t maximum, int64_t step)
{
    return step > 0 && value >= minimum && value <= maximum &&
           (value == maximum || range_offset(value, minimum) %
            (uint64_t)step == 0u);
}

AstraResult astra_interface_dial_init(AstraControl *control,
                                      const AstraDialInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        info->reset_value < info->minimum ||
        info->reset_value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        !range_value_representable(info->reset_value, info->minimum,
                                   info->maximum, info->step) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_DIAL;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    control->_private_reset_value = info->reset_value;
    control->_private_decimal_places = info->decimal_places;
    return ASTRA_OK;
}

AstraResult astra_interface_stepper_init(AstraControl *control,
                                         const AstraStepperInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->minimum >= info->maximum || info->step <= 0 ||
        info->value < info->minimum || info->value > info->maximum ||
        !range_value_representable(info->value, info->minimum,
                                   info->maximum, info->step) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_STEPPER;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = info->value;
    control->_private_minimum_value = info->minimum;
    control->_private_maximum_value = info->maximum;
    control->_private_value_step = info->step;
    return ASTRA_OK;
}

AstraResult astra_interface_progress_init(AstraControl *control,
                                          const AstraProgressInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->value > info->maximum ||
        info->maximum > INT32_MAX ||
        (info->maximum == 0u && info->value != 0u) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_PROGRESS;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_value = (int32_t)info->value;
    control->_private_maximum_value = (int32_t)info->maximum;
    return ASTRA_OK;
}

static AstraResult choice_init(AstraControl *control,
                               const AstraChoiceInfo *info, uint32_t kind)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u ||
        (info->item_count != 0u && info->items == NULL) ||
        info->item_count == 0u ||
        info->item_count > (uint32_t)INT32_MAX ||
        (info->selected != UINT32_MAX &&
         info->selected >= info->item_count) ||
        info->selected == UINT32_MAX ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->item_count; ++at) {
        const AstraChoiceItem *item = &info->items[at];

        if (item->text_length == 0u ||
            !astra_utf8_validate(item->text, item->text_length, 0u) ||
            !astra_words_zero(item->reserved, 2u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = kind;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)info->items;
    control->_private_text_length = info->item_count;
    control->_private_value = (int32_t)info->selected;
    control->_private_minimum_value = 0;
    control->_private_maximum_value = (int32_t)info->item_count - 1;
    return ASTRA_OK;
}

AstraResult astra_interface_segmented_init(AstraControl *control,
                                           const AstraChoiceInfo *info)
{
    return choice_init(control, info, ASTRA_CONTROL_SEGMENTED);
}

AstraResult astra_interface_tab_init(AstraControl *control,
                                     const AstraChoiceInfo *info)
{
    return choice_init(control, info, ASTRA_CONTROL_TAB);
}

/** Initialize stable collection identity and single selection. */
AstraResult astra_interface_collection_init(AstraCollectionModel *model,
    const AstraCollectionItem *items, uint32_t count, uint64_t selected_id)
{
    if (model == NULL || !collection_items_valid(items, count) ||
        (selected_id != 0u &&
         collection_selectable_index(items, count, selected_id) < 0))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *model = (AstraCollectionModel)ASTRA_COLLECTION_MODEL_INIT;
    model->items = items;
    model->item_count = count;
    model->selected_id = selected_id;
    return ASTRA_OK;
}

AstraResult astra_interface_list_init(AstraControl *control,
                                      const AstraListInfo *info)
{
    int32_t selected;

    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->model->size != sizeof(AstraCollectionModel) ||
        !selection_valid(info->model->selection, info->model->item_count) ||
        !astra_words_zero(info->model->reserved, 2u) ||
        !collection_items_valid(info->model->items, info->model->item_count) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    selected = collection_selectable_index(info->model->items, info->model->item_count,
                                info->model->selected_id);
    if (info->model->selected_id != 0u && selected < 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_LIST;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(void *)info->model;
    control->_private_text_length = info->model->item_count;
    control->_private_value = selected;
    control->_private_minimum_value = -1;
    control->_private_maximum_value = (int64_t)info->model->item_count - 1;
    return ASTRA_OK;
}

static AstraResult collection_set_items(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraTableCell *cells, uint32_t cell_count,
    const AstraOutlineTableRow *rows)
{
    AstraCollectionModel before;
    AstraCollectionModel *model;
    AstraTableModel *table = NULL;
    const AstraTableCell *old_cells = NULL;
    uint32_t old_cell_count = 0u;
    AstraOutlineTableModel *outline = NULL;
    const AstraOutlineTableRow *old_rows = NULL;
    uint32_t old_row_count = 0u;
    AstraControl next;
    AstraResult result;
    int32_t selected;
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        (control->_private_control_kind != ASTRA_CONTROL_LIST &&
         !TABLE_KINDS(control->_private_control_kind)) ||
        !collection_items_valid(items, count) ||
        !selection_valid(collection_model(control)->selection, count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (TABLE_KINDS(control->_private_control_kind)) {
        table = (AstraTableModel *)(void *)table_model(control);
        if (table == NULL || table->column_count == 0u ||
            cell_count != (uint64_t)count * (table->column_count - 1u) ||
            !table_cells_valid(cells, cell_count))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE) {
            outline = outline_table_model(control);
            if (!outline_rows_valid(rows, count))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            old_rows = outline->rows;
            old_row_count = outline->row_count;
            outline->rows = rows;
            outline->row_count = count;
        }
        old_cells = table->cells;
        old_cell_count = table->cell_count;
        table->cells = cells;
        table->cell_count = cell_count;
    }
    model = collection_model(control);
    before = *model;
    selected = collection_selectable_index(items, count, model->selected_id);
    if (selected < 0 && model->selection != NULL)
        for (uint32_t at = 0u; at < model->selection->count; ++at) {
            selected = collection_selectable_index(items, count,
                model->selection->ids[at]);
            if (selected >= 0) break;
        }
    next = *control;
    next._private_text_length = count;
    next._private_value = selected;
    next._private_minimum_value = -1;
    next._private_maximum_value = (int64_t)count - 1;
    model->items = items;
    model->item_count = count;
    model->selected_id = selected < 0 ? 0u : items[selected].id;
    result = replace_measured_control(context, control, &next);
    if (result != ASTRA_OK) {
        *model = before;
        if (table != NULL) {
            table->cells = old_cells;
            table->cell_count = old_cell_count;
        }
        if (outline != NULL) {
            outline->rows = old_rows;
            outline->row_count = old_row_count;
        }
        if (context->_private_has_layout != 0u)
            (void)astra_interface_ui_layout(context, &context->_private_layout);
    } else {
        selection_prune(model->selection, items, count);
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
    }
    return result;
}

AstraResult astra_interface_list_set_items(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count)
{
    if (control == NULL ||
        control->_private_control_kind != ASTRA_CONTROL_LIST)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return collection_set_items(context, control, items, count, NULL, 0u,
        NULL);
}

AstraResult astra_interface_table_init(AstraControl *control,
    const AstraTableInfo *info)
{
    int32_t selected;
    const AstraCollectionModel *collection;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || !table_model_valid(info->model) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    collection = info->model->collection;
    if (!astra_words_zero(collection->reserved, 2u) ||
        !collection_items_valid(collection->items, collection->item_count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    selected = collection_selectable_index(collection->items, collection->item_count,
                                collection->selected_id);
    if (collection->selected_id != 0u && selected < 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_TABLE;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)collection;
    control->_private_text_length = collection->item_count;
    control->_private_display_model = info->model;
    control->_private_value = selected;
    control->_private_minimum_value = -1;
    control->_private_maximum_value = (int64_t)collection->item_count - 1;
    return ASTRA_OK;
}

AstraResult astra_interface_table_set_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraTableCell *cells, uint32_t cell_count)
{
    if (control == NULL ||
        control->_private_control_kind != ASTRA_CONTROL_TABLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return collection_set_items(context, control, items, count,
                                cells, cell_count, NULL);
}

AstraResult astra_interface_outline_table_init(AstraControl *control,
    const AstraOutlineTableInfo *info)
{
    AstraTableInfo table = ASTRA_TABLE_INFO_INIT;
    AstraResult result;
    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || !outline_model_valid(info->model) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    table.id = info->id;
    table.model = info->model->table;
    table.state = info->state;
    result = astra_interface_table_init(control, &table);
    if (result != ASTRA_OK) return result;
    control->_private_control_kind = ASTRA_CONTROL_OUTLINE_TABLE;
    control->_private_display_model = info->model;
    return ASTRA_OK;
}

AstraResult astra_interface_outline_table_set_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraTableCell *cells, uint32_t cell_count,
    const AstraOutlineTableRow *rows)
{
    if (control == NULL || control->_private_control_kind !=
            ASTRA_CONTROL_OUTLINE_TABLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return collection_set_items(context, control, items, count, cells,
        cell_count, rows);
}

AstraResult astra_interface_table_set_sort(AstraUIContext *context,
    AstraControl *control, uint32_t column, uint32_t reverse)
{
    uint32_t index = control_index(context, control);
    uint32_t style;

    if (index == CONTROL_NONE ||
        !TABLE_KINDS(control->_private_control_kind) ||
        reverse > 1u ||
        (column != UINT32_MAX && column >= table_model(control)->column_count) ||
        (column == UINT32_MAX && reverse != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    style = column == UINT32_MAX ? 0u :
        TABLE_SORT_ACTIVE | column |
        (reverse != 0u ? TABLE_SORT_REVERSE : 0u);
    if (style == control->_private_style) return ASTRA_OK;
    control_damage(context, index);
    control->_private_style = style;
    return ASTRA_OK;
}

AstraResult astra_interface_popup_init(AstraControl *control,
                                       const AstraChoiceInfo *info)
{
    return choice_init(control, info, ASTRA_CONTROL_POPUP);
}

AstraResult astra_interface_tree_init(AstraControl *control,
    const AstraTreeInfo *info)
{
    AstraTreeModel *tree;

    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tree = info->model;
    if (!tree_rebuild(tree, 1) ||
        !astra_words_zero(tree->collection->reserved, 2u) ||
        (tree->collection->selected_id != 0u &&
         collection_index(tree->collection->items, tree->node_count,
                          tree->collection->selected_id) < 0))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tree_normalize_selection(tree);
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_TREE;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(void *)tree->collection;
    control->_private_text_length = tree->visible_count;
    control->_private_display_model = tree;
    control->_private_value = tree_visible_index(tree,
        tree->collection->selected_id);
    control->_private_minimum_value = -1;
    control->_private_maximum_value = (int64_t)tree->visible_count - 1;
    return ASTRA_OK;
}

AstraResult astra_interface_tree_set_expanded(AstraUIContext *context,
    AstraControl *control, uint64_t item_id, uint32_t expanded)
{
    AstraTreeModel *tree;
    AstraControl next;
    AstraResult result;
    uint64_t selected_before;
    int32_t item;

    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TREE ||
        expanded > 1u || item_id == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tree = tree_model(control);
    item = collection_index(tree->collection->items, tree->node_count, item_id);
    if (item < 0 || !tree_has_children(tree, (uint32_t)item))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (tree->nodes[item].expanded == expanded) return ASTRA_OK;
    selected_before = tree->collection->selected_id;
    tree->nodes[item].expanded = expanded;
    (void)tree_rebuild(tree, 0);
    tree_normalize_selection(tree);
    next = *control;
    next._private_text_length = tree->visible_count;
    next._private_value = tree_visible_index(tree, tree->collection->selected_id);
    next._private_maximum_value = (int64_t)tree->visible_count - 1;
    result = replace_measured_control(context, control, &next);
    if (result != ASTRA_OK) {
        tree->nodes[item].expanded = !expanded;
        (void)tree_rebuild(tree, 0);
        tree->collection->selected_id = selected_before;
    }
    return result;
}

AstraResult astra_interface_tree_set_nodes(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    AstraTreeNode *nodes, uint32_t *visible_rows, uint32_t visible_capacity)
{
    AstraTreeModel *tree;
    AstraTreeModel before;
    AstraCollectionModel collection_before;
    AstraControl next;
    AstraResult result;
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TREE ||
        !collection_items_valid(items, count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tree = tree_model(control);
    before = *tree;
    collection_before = *tree->collection;
    tree->collection->items = items;
    tree->collection->item_count = count;
    tree->nodes = nodes;
    tree->node_count = count;
    tree->visible_rows = visible_rows;
    tree->visible_capacity = visible_capacity;
    if (!tree_rebuild(tree, 0)) {
        *tree = before;
        *tree->collection = collection_before;
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    tree_normalize_selection(tree);
    next = *control;
    next._private_text_length = tree->visible_count;
    next._private_value = tree_visible_index(tree, tree->collection->selected_id);
    next._private_maximum_value = (int64_t)tree->visible_count - 1;
    result = replace_measured_control(context, control, &next);
    if (result != ASTRA_OK) {
        *tree = before;
        *tree->collection = collection_before;
        (void)tree_rebuild(tree, 0);
        if (context->_private_has_layout != 0u)
            (void)astra_interface_ui_layout(context, &context->_private_layout);
    } else {
        selection_prune(tree->collection->selection, items, count);
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
    }
    return result;
}

AstraResult astra_interface_view_init(AstraControl *control,
    const AstraViewInfo *info)
{
    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || !view_model_valid(info->model) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 2u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_VIEW;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_display_model = info->model;
    return ASTRA_OK;
}

AstraResult astra_interface_view_invalidate(AstraUIContext *context,
    AstraControl *control)
{
    uint32_t index = control_index(context, control);

    if (!context_valid(context) || index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_VIEW ||
        !view_model_valid(view_model(control)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_icon_grid_init(AstraControl *control,
    const AstraIconGridInfo *info)
{
    const AstraIconGridModel *grid;
    int32_t selected;

    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || !icon_grid_model_valid(info->model, 1) ||
        (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    grid = info->model;
    selected = collection_selectable_index(grid->collection->items, grid->icon_count,
                                grid->collection->selected_id);
    if (grid->collection->selected_id != 0u && selected < 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_ICON_GRID;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_text = (const char *)(const void *)grid->collection;
    control->_private_text_length = grid->icon_count;
    control->_private_display_model = grid;
    control->_private_value = selected;
    control->_private_minimum_value = -1;
    control->_private_maximum_value = (int64_t)grid->icon_count - 1;
    return ASTRA_OK;
}

AstraResult astra_interface_icon_grid_set_items(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *items, uint32_t count,
    const AstraSurface *const *icons)
{
    AstraIconGridModel *grid;
    AstraIconGridModel before;
    AstraCollectionModel collection_before;
    AstraControl next;
    AstraResult result;
    int32_t selected;
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_ICON_GRID ||
        !collection_items_valid(items, count) ||
        !selection_valid(icon_grid_model(control)->collection->selection,
            count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    grid = icon_grid_model(control);
    before = *grid;
    collection_before = *grid->collection;
    grid->collection->items = items;
    grid->collection->item_count = count;
    grid->icons = icons;
    grid->icon_count = count;
    if (!icon_grid_model_valid(grid, 0)) {
        *grid = before;
        *grid->collection = collection_before;
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    selected = collection_selectable_index(items, count,
        collection_before.selected_id);
    if (selected < 0 && grid->collection->selection != NULL)
        for (uint32_t at = 0u;
             at < grid->collection->selection->count; ++at) {
            selected = collection_selectable_index(items, count,
                grid->collection->selection->ids[at]);
            if (selected >= 0) break;
        }
    grid->collection->selected_id = selected < 0 ? 0u : items[selected].id;
    next = *control;
    next._private_text_length = count;
    next._private_value = selected;
    next._private_maximum_value = (int64_t)count - 1;
    result = replace_measured_control(context, control, &next);
    if (result != ASTRA_OK) {
        *grid = before;
        *grid->collection = collection_before;
        if (context->_private_has_layout != 0u)
            (void)astra_interface_ui_layout(context, &context->_private_layout);
    } else {
        selection_prune(grid->collection->selection, items, count);
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
    }
    return result;
}

AstraResult astra_interface_field_init(AstraControl *control,
                                       const AstraFieldInfo *info)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint64_t width;

    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->model == NULL ||
        info->preferred_columns == 0u ||
        (info->flags & ~FIELD_FLAGS) != 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED |
                         ASTRA_CONTROL_ERROR)) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        astra_text_model_get_state(info->model, &state) != ASTRA_OK ||
        state.line_count != 1u || !field_content_valid(info->model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    width = (uint64_t)info->preferred_columns * theme.mono_cell_width +
            field_inset(info->flags) * 2u;
    if (width > UINT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_FIELD;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->flags;
    control->_private_text = (const char *)(const void *)info->model;
    control->_private_text_length = info->preferred_columns;
    private_u32_set(&control->_private_maximum_value, state.generation);
    return ASTRA_OK;
}

AstraResult astra_interface_secure_field_init(AstraControl *control,
                                              const AstraFieldInfo *info)
{
    AstraFieldInfo secure;

    if (info == NULL || info->size < sizeof(*info) ||
        (info->flags & ASTRA_FIELD_INLINE) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    secure = *info;
    secure.flags |= ASTRA_FIELD_SECURE;
    return astra_interface_field_init(control, &secure);
}

AstraResult astra_interface_search_field_init(AstraControl *control,
                                              const AstraFieldInfo *info)
{
    AstraFieldInfo search;

    if (info == NULL || info->size < sizeof(*info) ||
        (info->flags & (ASTRA_FIELD_INLINE | ASTRA_FIELD_SECURE)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    search = *info;
    search.flags |= ASTRA_FIELD_SEARCH;
    return astra_interface_field_init(control, &search);
}

static int colour_binding_valid(uint32_t binding,
    AstraColorRGBA8 rgb, uint32_t slot)
{
    return binding <= ASTRA_COLOUR_PALETTE_SLOT && rgb.alpha == 255u &&
        (binding == ASTRA_COLOUR_PALETTE_SLOT ? slot <= 255u : slot == 0u);
}

AstraResult astra_interface_colour_well_init(AstraControl *control,
    const AstraColourWellInfo *info)
{
    if (control == NULL || info == NULL || info->size < sizeof(*info) ||
        info->id == 0u || info->label_length == 0u ||
        !astra_utf8_validate(info->label, info->label_length, 0u) ||
        !colour_binding_valid(info->binding, info->color,
                              info->palette_slot) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR |
                         ASTRA_CONTROL_COLLAPSED)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control = (AstraControl)ASTRA_CONTROL_INIT;
    control->_private_control_kind = ASTRA_CONTROL_COLOUR_WELL;
    control->_private_id = info->id;
    control->_private_state = info->state;
    control->_private_style = info->binding;
    control->_private_text = info->label;
    control->_private_text_length = info->label_length;
    control->_private_value = astra_surface_rgb565(
        info->color.red, info->color.green, info->color.blue);
    control->_private_maximum_value = info->palette_slot;
    return ASTRA_OK;
}

AstraResult astra_interface_colour_well_set(AstraUIContext *context,
    AstraControl *control, uint32_t binding, AstraColorRGBA8 rgb,
    uint32_t palette_slot)
{
    uint32_t index = control_index(context, control);
    uint16_t packed;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_COLOUR_WELL ||
        !colour_binding_valid(binding, rgb, palette_slot))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    packed = astra_surface_rgb565(rgb.red, rgb.green, rgb.blue);
    if (control->_private_style == binding &&
        control->_private_value == packed &&
        control->_private_maximum_value == palette_slot)
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_style = binding;
    control->_private_value = packed;
    control->_private_maximum_value = palette_slot;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_field_refresh(AstraUIContext *context,
                                          AstraControl *control)
{
    uint32_t index = control_index(context, control);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_FIELD ||
        astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK ||
        state.line_count != 1u || !field_content_valid(control_text_model(control)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (private_u32(&control->_private_maximum_value) != state.generation) {
        control_damage(context, index);
        private_u32_set(&control->_private_maximum_value, state.generation);
        control_damage(context, index);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_text_view_init(AstraControl *control,
                                          const AstraTextViewInfo *info)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraControl next = ASTRA_CONTROL_INIT;
    AstraControlSize size;
    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        (info->style_at == NULL && info->style_context != NULL) ||
        astra_text_model_get_state(info->model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next._private_control_kind = ASTRA_CONTROL_TEXT_VIEW;
    next._private_id = info->id;
    next._private_state = info->state;
    next._private_style = ASTRA_FIELD_READ_ONLY;
    next._private_text = (const char *)(const void *)info->model;
    next._private_text_style_at = info->style_at;
    next._private_text_style_context = info->style_context;
    private_u32_set(&next._private_maximum_value, state.generation);
    AstraResult result = text_view_measure(&next, &size);
    if (result == ASTRA_OK) *control = next;
    return result;
}

AstraResult astra_interface_text_view_refresh(AstraUIContext *context,
                                             AstraControl *control)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraControl next;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = *control;
    private_u32_set(&next._private_maximum_value, state.generation);
    next._private_dynamic_state &= ~TEXT_VIEW_SOFT_END;
    return replace_measured_control(context, control, &next);
}

AstraResult astra_interface_text_view_set_inverted(AstraUIContext *context,
    AstraControl *control, uint32_t inverted)
{
    uint32_t index = control_index(context, control);
    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        inverted > 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control_damage(context, index);
    if (inverted != 0u)
        control->_private_style |= TEXT_VIEW_INVERTED;
    else
        control->_private_style &= ~TEXT_VIEW_INVERTED;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_text_view_set_wrap_width(AstraUIContext *context,
    AstraControl *control, uint32_t width)
{
    AstraControl next;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        width > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (((control->_private_style & TEXT_VIEW_WRAP) != 0u ?
         control->_private_measured_width : 0u) == width)
        return ASTRA_OK;
    next = *control;
    next._private_dynamic_state &= ~TEXT_VIEW_SOFT_END;
    if (width != 0u) {
        next._private_style |= TEXT_VIEW_WRAP;
        next._private_measured_width = width;
    } else {
        next._private_style &= ~TEXT_VIEW_WRAP;
    }
    return replace_measured_control(context, control, &next);
}

AstraResult astra_interface_text_view_set_mode(AstraUIContext *context,
    AstraControl *control, uint32_t mode)
{
    AstraControl next;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        (mode != ASTRA_TEXT_SURFACE_CODE &&
         mode != ASTRA_TEXT_SURFACE_FLOW))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (mode == ASTRA_TEXT_SURFACE_FLOW) {
        uint32_t height = astra_surface_ui_text_height(theme.body_font_height);
        if (height == 0u || height > UINT16_MAX)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    if (((control->_private_style & TEXT_VIEW_FLOW) != 0u) ==
        (mode == ASTRA_TEXT_SURFACE_FLOW))
        return ASTRA_OK;
    next = *control;
    if (mode == ASTRA_TEXT_SURFACE_FLOW)
        next._private_style |= TEXT_VIEW_FLOW;
    else
        next._private_style &= ~TEXT_VIEW_FLOW;
    next._private_dynamic_state &= ~TEXT_VIEW_SOFT_END;
    return replace_measured_control(context, control, &next);
}

AstraResult astra_interface_text_view_set_editable(AstraUIContext *context,
    AstraControl *control, uint32_t editable)
{
    uint32_t index = control_index(context, control);
    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        editable > 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (((control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u) ==
        (editable != 0u))
        return ASTRA_OK;
    control_damage(context, index);
    if (editable != 0u)
        control->_private_style &= ~ASTRA_FIELD_READ_ONLY;
    else
        control->_private_style |= ASTRA_FIELD_READ_ONLY;
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_text_view_set_line_numbers(AstraUIContext *context,
    AstraControl *control, uint32_t visible)
{
    AstraControl next;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        visible > 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (((control->_private_style & TEXT_VIEW_LINE_NUMBERS) != 0u) ==
        (visible != 0u))
        return ASTRA_OK;
    next = *control;
    if (visible != 0u)
        next._private_style |= TEXT_VIEW_LINE_NUMBERS;
    else
        next._private_style &= ~TEXT_VIEW_LINE_NUMBERS;
    return replace_measured_control(context, control, &next);
}

AstraResult astra_interface_grid_view_init(AstraControl *control,
    const AstraGridViewInfo *info)
{
    AstraControl next = ASTRA_CONTROL_INIT;
    AstraControlSize size;
    if (control == NULL || info == NULL || info->size != sizeof(*info) ||
        info->id == 0u || (info->state & ~CONTROL_VALUE_STATES) != 0u ||
        !astra_words_zero(info->reserved, 4u) ||
        !grid_view_model_valid(info->model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next._private_control_kind = ASTRA_CONTROL_GRID_VIEW;
    next._private_id = info->id;
    next._private_state = info->state;
    next._private_text = (const char *)(const void *)info->model;
    AstraResult result = grid_view_measure(&next, &size);
    if (result == ASTRA_OK) *control = next;
    return result;
}

AstraResult astra_interface_grid_view_refresh(AstraUIContext *context,
    AstraControl *control)
{
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_GRID_VIEW ||
        !grid_view_model_valid(grid_view_model(control)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return replace_measured_control(context, control, control);
}

AstraResult astra_interface_control_set_flex(AstraControl *control,
                                             const AstraFlexItem *item)
{
    if (!control_valid(control) || item == NULL || item->size < sizeof(*item) ||
        item->minimum_width > item->maximum_width ||
        item->minimum_height > item->maximum_height ||
        item->align_self > ASTRA_FLEX_ALIGN_AUTO ||
        (item->flags & ~FLEX_ITEM_FLAGS) != 0u ||
        !astra_words_zero(item->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control->_private_flex_grow = item->grow;
    control->_private_flex_shrink = item->shrink;
    control->_private_flex_basis = item->basis;
    control->_private_minimum_width = item->minimum_width;
    control->_private_minimum_height = item->minimum_height;
    control->_private_maximum_width = item->maximum_width;
    control->_private_maximum_height = item->maximum_height;
    control->_private_align_self = item->align_self;
    control->_private_flex_flags = item->flags;
    control->_private_parent_id = item->parent_id;
    return ASTRA_OK;
}

static AstraResult replace_measured_control(AstraUIContext *context,
    AstraControl *control, const AstraControl *replacement)
{
    uint32_t index = control_index(context, control);
    AstraControl before;
    AstraControlSize size;
    AstraResult result;

    if (index == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    before = *control;
    result = measure_control(replacement, &size);
    if (result != ASTRA_OK)
        return result;

    control_damage(context, index);
    *control = *replacement;
    control->_private_measured_width = size.width;
    control->_private_measured_height = size.height;
    if ((size.width != before._private_measured_width ||
         size.height != before._private_measured_height) &&
        context->_private_has_layout != 0u) {
        result = astra_interface_ui_layout(context,
                                           &context->_private_layout);
        if (result != ASTRA_OK) {
            *control = before;
            (void)astra_interface_ui_layout(context,
                                            &context->_private_layout);
            return result;
        }
    } else {
        control_damage(context, index);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_control_set_text(AstraUIContext *context,
    AstraControl *control, const char *text, uint32_t text_length)
{
    AstraControl next;
    if (control_index(context, control) == CONTROL_NONE || text_length == 0u ||
        !astra_utf8_validate(text, text_length, 0u) ||
        (control->_private_control_kind != ASTRA_CONTROL_LABEL &&
         control->_private_control_kind != ASTRA_CONTROL_BUTTON &&
         control->_private_control_kind != ASTRA_CONTROL_DISCLOSURE &&
         control->_private_control_kind != ASTRA_CONTROL_COLOUR_WELL &&
         !TOGGLE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = *control;
    next._private_text = text;
    next._private_text_length = text_length;
    return replace_measured_control(context, control, &next);
}

AstraResult astra_interface_label_set_font(AstraUIContext *context,
    AstraControl *control, const AstraLabelFont *font)
{
    AstraControl next;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_LABEL ||
        !label_font_valid(font)) return ASTRA_ERROR_INVALID_ARGUMENT;
    next = *control;
    next._private_label_font = *font;
    return replace_measured_control(context, control, &next);
}

static AstraResult measure_controls(AstraUIContext *context)
{
    for (uint32_t count = context->_private_control_count; count != 0u; --count) {
        uint32_t at = count - 1u;
        AstraControl *control = &context->_private_controls[at];
        AstraControlSize measured;
        AstraResult result = control->_private_control_kind == ASTRA_CONTROL_CONTAINER ?
            measure_container(context, at, &measured) : measure_control(control, &measured);

        if (result != ASTRA_OK) return result;
        control->_private_measured_width = measured.width;
        control->_private_measured_height = measured.height;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_ui_init(AstraUIContext *context,
                                    AstraControl *controls,
                                    uint32_t control_count,
                                    uint16_t width, uint16_t height)
{
    if (context == NULL || width == 0u || height == 0u ||
        (control_count != 0u && controls == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < control_count; ++at) {
        if (!control_valid(&controls[at]))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t before = 0u; before < at; ++before)
            if (controls[before]._private_id == controls[at]._private_id)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            else if (controls[at]._private_control_kind ==
                         ASTRA_CONTROL_RADIO &&
                     (controls[at]._private_state &
                      ASTRA_CONTROL_SELECTED) != 0u &&
                     controls[before]._private_control_kind ==
                         ASTRA_CONTROL_RADIO &&
                     controls[before]._private_style ==
                         controls[at]._private_style &&
                     (controls[before]._private_state &
                      ASTRA_CONTROL_SELECTED) != 0u)
                return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    *context = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    context->_private_controls = controls;
    context->_private_control_count = control_count;
    context->_private_width = width;
    context->_private_height = height;
    for (uint32_t at = 0u; at < control_count; ++at) {
        AstraControl *control = &controls[at];
        uint32_t parent = CONTROL_NONE;

        control->_private_dynamic_state = 0u;
        control->_private_laid_out = 0u;
        control->_private_parent = CONTROL_NONE;
        control->_private_first_child = CONTROL_NONE;
        control->_private_last_child = CONTROL_NONE;
        control->_private_next_sibling = CONTROL_NONE;
        control->_private_animation_next = CONTROL_NONE;
        control->_private_animation_phase = 0u;
        if (control->_private_parent_id != 0u) {
            for (uint32_t before = 0u; before < at; ++before)
                if (controls[before]._private_id ==
                    control->_private_parent_id) {
                    parent = before;
                    break;
                }
            if (parent == CONTROL_NONE ||
                !CONTAINER_KINDS(controls[parent]._private_control_kind))
                return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        control->_private_parent = parent;
        if (parent == CONTROL_NONE) {
            if (context->_private_last_child == CONTROL_NONE)
                context->_private_first_child = at;
            else
                controls[context->_private_last_child]
                    ._private_next_sibling = at;
            context->_private_last_child = at;
        } else {
            AstraControl *owner = &controls[parent];

            if (owner->_private_last_child == CONTROL_NONE)
                owner->_private_first_child = at;
            else
                controls[owner->_private_last_child]
                    ._private_next_sibling = at;
            owner->_private_last_child = at;
        }
    }
    for (uint32_t at = 0u; at < control_count; ++at)
        if (controls[at]._private_control_kind == ASTRA_CONTROL_SCROLL_VIEW &&
            (controls[at]._private_first_child == CONTROL_NONE ||
             controls[at]._private_first_child !=
                 controls[at]._private_last_child))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        else if (controls[at]._private_control_kind ==
                     ASTRA_CONTROL_DISCLOSURE) {
            uint32_t target = control_id_index(
                context, controls[at]._private_style);

            if (target == CONTROL_NONE ||
                controls[target]._private_control_kind !=
                    ASTRA_CONTROL_CONTAINER ||
                controls[target]._private_parent !=
                    controls[at]._private_parent)
                return ASTRA_ERROR_INVALID_ARGUMENT;
        }
    if (measure_controls(context) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < control_count; ++at)
        animation_sync(context, at);
    return ASTRA_OK;
}

/** Set keyboard focus after a UI layout, for document-first applications. */
AstraResult astra_interface_ui_focus_control(AstraUIContext *context,
                                             uint32_t control_id)
{
    uint32_t index;
    if (context == NULL || context->_private_controls == NULL ||
        control_id == 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    index = control_id_index(context, control_id);
    if (index == CONTROL_NONE || !focusable(&context->_private_controls[index]) ||
        (context->_private_controls[index]._private_state &
         (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    set_focus(context, index, 1);
    return ASTRA_OK;
}

AstraResult astra_interface_ui_focused_control(const AstraUIContext *context,
                                              uint32_t *control_id)
{
    if (context == NULL || control_id == NULL ||
        context->_private_controls == NULL ||
        context->_private_structure_size != sizeof(*context) ||
        (context->_private_focus != CONTROL_NONE &&
         context->_private_focus >= context->_private_control_count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control_id = context->_private_focus == CONTROL_NONE ? 0u :
        context->_private_controls[context->_private_focus]._private_id;
    return ASTRA_OK;
}

static AstraResult measure_control(const AstraControl *control,
                                   AstraControlSize *size)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t width;

    if (size == NULL || !control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW)
        return text_view_measure(control, size);
    if (control->_private_control_kind == ASTRA_CONTROL_GRID_VIEW)
        return grid_view_measure(control, size);
    if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER ||
        control->_private_control_kind == ASTRA_CONTROL_SPARKLINE)
        return astra_interface_display_measure(control, size);
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL) {
        const AstraLabelFont *font = &control->_private_label_font;
        if (font->gui != ASTRA_INVALID_HANDLE) {
            AstraFontTextMetrics metrics = ASTRA_FONT_TEXT_METRICS_INIT;
            AstraResult result = astra_fonts_text_measure(font->gui, font->id,
                font->id_length, font->pixel_height, control->_private_text,
                control->_private_text_length, UINT32_MAX, &metrics);
            size->width = metrics.width;
            size->height = metrics.height;
            return result;
        }
        size->width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.body_font_height);
        size->height = astra_surface_ui_text_height(theme.body_font_height);
        return size->height != 0u ? ASTRA_OK : ASTRA_ERROR_UNSUPPORTED;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
        size->width = control->_private_text_length;
        size->height = control->_private_style;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
        if ((control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
            ASTRA_ORIENTATION_HORIZONTAL) {
            size->width = theme.spacing_unit * 32u;
            size->height = 10u;
        } else {
            size->width = 10u;
            size->height = theme.spacing_unit * 32u;
        }
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER) {
        if (control->_private_style == ASTRA_ORIENTATION_VERTICAL) {
            size->width = theme.spacing_unit * 2u;
            size->height = theme.spacing_unit * 8u;
        } else {
            size->width = theme.spacing_unit * 8u;
            size->height = theme.spacing_unit * 2u;
        }
        return ASTRA_OK;
    }
    if (TOGGLE_KINDS(control->_private_control_kind)) {
        uint32_t indicator = control->_private_control_kind ==
            ASTRA_CONTROL_SWITCH ? 34u : 16u;

        width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.control_font_height);
        if (width > UINT32_MAX - indicator - theme.spacing_unit * 2u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = indicator + theme.spacing_unit * 2u + width;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE) {
        width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.control_font_height);
        if (width > UINT32_MAX - 10u - theme.spacing_unit * 2u -
                        (uint32_t)theme.control_padding_x * 2u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = width + 10u + theme.spacing_unit * 2u +
                      (uint32_t)theme.control_padding_x * 2u;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER) {
        if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
            size->width = theme.spacing_unit * 32u;
            size->height = theme.control_height;
        } else {
            size->width = theme.control_height;
            size->height = theme.spacing_unit * 32u;
        }
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_DIAL) {
        size->width = 56u;
        size->height = 56u;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
        uint64_t minimum_magnitude =
            control->_private_minimum_value < 0 ?
                0u - (uint64_t)control->_private_minimum_value :
                (uint64_t)control->_private_minimum_value;
        uint64_t maximum_magnitude =
            control->_private_maximum_value < 0 ?
                0u - (uint64_t)control->_private_maximum_value :
                (uint64_t)control->_private_maximum_value;
        uint32_t minimum_length = control->_private_minimum_value < 0 ? 1u : 0u;
        uint32_t maximum_length = control->_private_maximum_value < 0 ? 1u : 0u;
        uint32_t value_width;

        do {
            ++minimum_length;
            minimum_magnitude /= 10u;
        } while (minimum_magnitude != 0u);
        do {
            ++maximum_length;
            maximum_magnitude /= 10u;
        } while (maximum_magnitude != 0u);
        width = minimum_length > maximum_length ? minimum_length :
                                                   maximum_length;
        value_width = width * theme.mono_cell_width +
                      theme.spacing_unit * 2u;
        if (value_width < 56u) value_width = 56u;
        if (value_width > UINT32_MAX - STEPPER_BUTTON_WIDTH)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = value_width + STEPPER_BUTTON_WIDTH;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS) {
        size->width = theme.spacing_unit * 32u;
        size->height = theme.spacing_unit * 2u;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_VIEW) {
        size->width = view_model(control)->intrinsic_width;
        size->height = view_model(control)->intrinsic_height;
        return ASTRA_OK;
    }
    if (TABLE_KINDS(control->_private_control_kind)) {
        const AstraTableModel *table = table_model(control);
        uint64_t width = 0u;
        uint64_t height = ((uint64_t)control->_private_text_length + 1u) *
                          theme.control_height;

        if (height > INT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
        for (uint32_t at = 0u; at < table->column_count; ++at)
            width += table->columns[at].width;
        if (width > INT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
        size->width = (uint32_t)width;
        size->height = (uint32_t)height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
        const AstraIconGridModel *grid = const_icon_grid_model(control);
        uint32_t tile_width = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_WIDTH : ASTRA_ICON_TILE_DESKTOP_WIDTH;
        uint32_t tile_height = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_HEIGHT : ASTRA_ICON_TILE_DESKTOP_HEIGHT;
        uint32_t rows = grid->icon_count == 0u ? 1u :
            1u + (grid->icon_count - 1u) / grid->columns;
        uint64_t width = (uint64_t)grid->columns * tile_width;
        uint64_t height = (uint64_t)rows * tile_height;

        if (width > INT32_MAX || height > INT32_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = (uint32_t)width;
        size->height = (uint32_t)height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_LIST ||
        control->_private_control_kind == ASTRA_CONTROL_TREE) {
        const AstraCollectionItem *items =
            const_collection_model(control)->items;
        const AstraTreeModel *tree = control->_private_control_kind ==
            ASTRA_CONTROL_TREE ? const_tree_model(control) : NULL;
        uint32_t widest = 0u;
        uint64_t height = (uint64_t)(control->_private_text_length != 0u ?
            control->_private_text_length : 1u) * theme.control_height;

        if (height > INT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
        for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
            uint32_t item = tree == NULL ? at : tree->visible_rows[at];
            uint32_t item_width = astra_surface_ui_text_width(
                items[item].text, items[item].text_length,
                theme.control_font_height);

            if (tree != NULL) {
                uint64_t indented = (uint64_t)item_width +
                    (uint64_t)(tree->nodes[item].depth + 1u) *
                        theme.control_height;
                if (indented > UINT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
                item_width = (uint32_t)indented;
            }

            if (item_width > widest) widest = item_width;
        }
        if (widest > UINT32_MAX - (uint32_t)theme.control_padding_x * 2u)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = widest + (uint32_t)theme.control_padding_x * 2u;
        if (size->width < theme.spacing_unit * 32u)
            size->width = theme.spacing_unit * 32u;
        size->height = (uint32_t)height;
        return ASTRA_OK;
    }
    if (CHOICE_KINDS(control->_private_control_kind)) {
        const AstraChoiceItem *items = choice_items(control);
        uint32_t widest = 0u;
        uint64_t total_width = 0u;

        for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
            uint32_t item_width = astra_surface_ui_text_width(
                items[at].text, items[at].text_length,
                theme.control_font_height);

            if (item_width > widest) widest = item_width;
            if (control->_private_control_kind == ASTRA_CONTROL_TAB)
                total_width += item_width + theme.spacing_unit * 6u;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED)
            total_width = ((uint64_t)widest + theme.spacing_unit * 4u) *
                          control->_private_text_length;
        if (total_width > UINT32_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
        if (control->_private_control_kind == ASTRA_CONTROL_POPUP &&
            widest > UINT32_MAX -
                (uint32_t)theme.control_padding_x * 2u - 24u)
            return ASTRA_ERROR_NO_RESOURCES;
        size->width = control->_private_control_kind == ASTRA_CONTROL_POPUP ?
            widest + (uint32_t)theme.control_padding_x * 2u + 24u :
            (uint32_t)total_width;
        if (control->_private_control_kind == ASTRA_CONTROL_POPUP &&
            size->width < 104u) size->width = 104u;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
        uint64_t field_width =
            (uint64_t)control->_private_text_length * theme.mono_cell_width +
            field_inset(control->_private_style) * 2u;

        if (field_width > UINT32_MAX)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = (uint32_t)field_width;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL) {
        uint32_t text_width = astra_surface_ui_text_width(
            control->_private_text, control->_private_text_length,
            theme.control_font_height);

        if (text_width > UINT32_MAX - 60u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = text_width + 60u;
        size->height = theme.control_height;
        return ASTRA_OK;
    }
    width = astra_surface_ui_text_width(
        control->_private_text, control->_private_text_length,
        theme.control_font_height);
    if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
        ((control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_TOOLBAR ||
         (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_SPLIT_MENU)) {
        uint32_t extra = 16u +
            (ASTRA_BUTTON_ICON(control->_private_style) != 0u ? 22u : 0u);
        if (width > UINT32_MAX - extra)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_SPLIT_MENU ? ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT :
            (control->_private_style & ASTRA_BUTTON_ICON_ONLY) != 0u ?
                ASTRA_THEME_TOOLBAR_ICON_BUTTON_WIDTH : width + extra;
        size->height = ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
        control->_private_style == ASTRA_BUTTON_TAG) {
        if (width > UINT32_MAX - 30u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        size->width = width + 30u;
        size->height = 24u;
        return ASTRA_OK;
    }
    if (width > UINT32_MAX - (uint32_t)theme.control_padding_x * 2u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    size->width = width + (uint32_t)theme.control_padding_x * 2u;
    size->height = theme.control_height;
    return ASTRA_OK;
}

AstraResult astra_interface_toolbar_button_measure(
    const AstraControl *control, uint32_t icon_only, AstraControlSize *size)
{
    AstraControl candidate;

    if (!control_valid(control) || size == NULL || icon_only > 1u ||
        control->_private_control_kind != ASTRA_CONTROL_BUTTON ||
        (control->_private_style & ASTRA_BUTTON_STYLE_MASK) !=
            ASTRA_BUTTON_TOOLBAR ||
        ASTRA_BUTTON_ICON(control->_private_style) == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    candidate = *control;
    if (icon_only != 0u)
        candidate._private_style |= ASTRA_BUTTON_ICON_ONLY;
    else
        candidate._private_style &= ~ASTRA_BUTTON_ICON_ONLY;
    return measure_control(&candidate, size);
}

AstraResult astra_interface_ui_measure(const AstraUIContext *context,
                                       const AstraControl *control,
                                       AstraControlSize *size)
{
    if (!context_valid(context) ||
        control_index(context, control) == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (size == NULL || !control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    size->width = control->_private_measured_width;
    size->height = control->_private_measured_height;
    return ASTRA_OK;
}

static uint32_t axis_measured(const AstraControl *control, int row)
{
    return row ? control->_private_measured_width :
                 control->_private_measured_height;
}

static uint32_t axis_minimum(const AstraControl *control, int row)
{
    uint32_t configured = row ? control->_private_minimum_width :
                                control->_private_minimum_height;
    uint32_t measured = axis_measured(control, row);

    return configured > measured ? configured : measured;
}

static uint32_t axis_maximum(const AstraControl *control, int row)
{
    return row ? control->_private_maximum_width :
                 control->_private_maximum_height;
}

static uint32_t axis_base(const AstraControl *control, int row)
{
    uint32_t value = control->_private_flex_basis == ASTRA_FLEX_AUTO ?
        axis_measured(control, row) : control->_private_flex_basis;
    uint32_t minimum = axis_minimum(control, row);
    uint32_t maximum = axis_maximum(control, row);

    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static uint32_t cross_base(const AstraControl *control, int row)
{
    uint32_t value = axis_measured(control, !row);
    uint32_t minimum = axis_minimum(control, !row);
    uint32_t maximum = axis_maximum(control, !row);

    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static uint32_t next_layout_child(const AstraUIContext *context,
                                  uint32_t index)
{
    while (index != CONTROL_NONE &&
           (context->_private_controls[index]._private_state &
            ASTRA_CONTROL_COLLAPSED) != 0u)
        index = context->_private_controls[index]._private_next_sibling;
    return index;
}

static AstraResult measure_container(const AstraUIContext *context,
                                     uint32_t index,
                                     AstraControlSize *size)
{
    const AstraControl *container = &context->_private_controls[index];
    const AstraFlexLayout *layout = &container->_private_child_layout;

    if (size == NULL || !layout_valid(layout))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (layout->direction == ASTRA_FLEX_ROW &&
        (layout->flags & ASTRA_FLEX_WRAP) != 0u &&
        container->_private_minimum_width != 0u &&
        container->_private_minimum_width ==
            container->_private_maximum_width) {
        uint32_t width = container->_private_maximum_width;
        uint64_t padding = (uint64_t)layout->padding_left +
                           layout->padding_right;
        uint64_t available = width > padding ? width - padding : 0u;
        uint64_t height = (uint64_t)layout->padding_top +
                          layout->padding_bottom;
        uint64_t line_width = 0u;
        uint32_t line_height = 0u;
        uint32_t line_count = 0u;

        for (uint32_t at = next_layout_child(context,
                 container->_private_first_child);
             at != CONTROL_NONE;
             at = next_layout_child(context,
                 context->_private_controls[at]._private_next_sibling)) {
            const AstraControl *child = &context->_private_controls[at];
            uint32_t child_width = axis_base(child, 1);
            uint32_t child_height = cross_base(child, 1);
            uint64_t addition = child_width +
                (line_count != 0u ? layout->main_gap : 0u);

            if (line_count != 0u &&
                ((child->_private_flex_flags & ASTRA_FLEX_BREAK_BEFORE) != 0u ||
                 line_width + addition > available)) {
                height += line_height + layout->cross_gap;
                line_width = 0u;
                line_height = 0u;
                line_count = 0u;
                addition = child_width;
            }
            line_width += addition;
            if (child_height > line_height) line_height = child_height;
            ++line_count;
            if ((child->_private_flex_flags & ASTRA_FLEX_BREAK_AFTER) != 0u) {
                height += line_height + layout->cross_gap;
                line_width = 0u;
                line_height = 0u;
                line_count = 0u;
            }
        }
        if (line_count != 0u) height += line_height;
        else if (height > (uint64_t)layout->padding_top +
                           layout->padding_bottom)
            height -= layout->cross_gap;
        if (height > UINT32_MAX) return ASTRA_ERROR_NO_RESOURCES;
        size->width = width;
        size->height = (uint32_t)height;
        return ASTRA_OK;
    }
    uint64_t main = layout->direction == ASTRA_FLEX_ROW ?
        (uint64_t)layout->padding_left + layout->padding_right :
        (uint64_t)layout->padding_top + layout->padding_bottom;
    uint64_t cross_padding = layout->direction == ASTRA_FLEX_ROW ?
        (uint64_t)layout->padding_top + layout->padding_bottom :
        (uint64_t)layout->padding_left + layout->padding_right;
    uint32_t cross = 0u;
    uint32_t count = 0u;
    int row = layout->direction == ASTRA_FLEX_ROW;

    for (uint32_t at = next_layout_child(
             context, container->_private_first_child);
         at != CONTROL_NONE;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling)) {
        const AstraControl *child = &context->_private_controls[at];
        uint32_t child_cross = cross_base(child, row);

        main += axis_base(child, row);
        if (child_cross > cross) cross = child_cross;
        ++count;
    }
    if (count > 1u)
        main += (uint64_t)(count - 1u) * layout->main_gap;
    if (main > UINT32_MAX || cross_padding + cross > UINT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    if (row) {
        size->width = (uint32_t)main;
        size->height = (uint32_t)(cross_padding + cross);
    } else {
        size->width = (uint32_t)(cross_padding + cross);
        size->height = (uint32_t)main;
    }
    return ASTRA_OK;
}

static uint64_t line_used(const AstraUIContext *context, uint32_t first,
                          uint32_t after, uint32_t count, uint32_t gap)
{
    uint64_t used = count > 1u ? (uint64_t)(count - 1u) * gap : 0u;

    for (uint32_t at = first; at != after;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling))
        used += context->_private_controls[at]._private_flex_target;
    return used;
}

static uint64_t item_weight(const AstraControl *control, int row, int grow)
{
    if (grow)
        return control->_private_flex_target < axis_maximum(control, row) ?
            control->_private_flex_grow : 0u;
    if (control->_private_flex_target <= axis_minimum(control, row))
        return 0u;
    return (uint64_t)control->_private_flex_shrink *
           control->_private_flex_target;
}

static int flex_line(AstraUIContext *context, uint32_t first, uint32_t after,
                     uint32_t count, uint32_t inner_main, uint32_t gap,
                     int row)
{
    uint64_t used = line_used(context, first, after, count, gap);
    int grow = used < inner_main;
    uint64_t remaining = grow ? inner_main - used : used - inner_main;

    /* ponytail: constraint redistribution can scan a line repeatedly; replace
       it with a scratch-backed active set only if measured UI sizes warrant it. */
    while (remaining != 0u) {
        uint64_t total = 0u;
        uint64_t seen = 0u;
        uint64_t apportioned = 0u;
        uint64_t applied = 0u;

        for (uint32_t at = first; at != after;
             at = next_layout_child(
                 context,
                 context->_private_controls[at]._private_next_sibling)) {
            uint64_t weight = item_weight(
                &context->_private_controls[at], row, grow);

            if (UINT64_MAX - total < weight)
                return 0;
            total += weight;
        }
        if (total == 0u || remaining > UINT64_MAX / total)
            return total == 0u;
        for (uint32_t at = first; at != after;
             at = next_layout_child(
                 context,
                 context->_private_controls[at]._private_next_sibling)) {
            AstraControl *control = &context->_private_controls[at];
            uint64_t weight = item_weight(control, row, grow);
            uint64_t entitled;
            uint64_t share;
            uint32_t room;

            if (weight == 0u) continue;
            seen += weight;
            entitled = remaining * seen / total;
            share = entitled - apportioned;
            apportioned = entitled;
            room = grow ? axis_maximum(control, row) -
                              control->_private_flex_target :
                          control->_private_flex_target -
                              axis_minimum(control, row);
            if (share > room) share = room;
            if (grow)
                control->_private_flex_target += (uint32_t)share;
            else
                control->_private_flex_target -= (uint32_t)share;
            applied += share;
        }
        if (applied == 0u)
            return 0;
        remaining -= applied;
    }
    return 1;
}

static int layout_valid(const AstraFlexLayout *layout)
{
    return layout != NULL && layout->size >= sizeof(*layout) &&
           layout->direction <= ASTRA_FLEX_COLUMN &&
           (layout->flags & ~ASTRA_FLEX_WRAP) == 0u &&
           layout->justify <= ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN &&
           layout->align_items <= ASTRA_FLEX_ALIGN_STRETCH &&
           astra_words_zero(layout->reserved, 4u);
}

static AstraControlFrame frame_intersection(const AstraControlFrame *first,
                                            const AstraControlFrame *second)
{
    int64_t left = first->x > second->x ? first->x : second->x;
    int64_t top = first->y > second->y ? first->y : second->y;
    int64_t first_right = (int64_t)first->x + first->width;
    int64_t second_right = (int64_t)second->x + second->width;
    int64_t first_bottom = (int64_t)first->y + first->height;
    int64_t second_bottom = (int64_t)second->y + second->height;
    int64_t right = first_right < second_right ? first_right : second_right;
    int64_t bottom = first_bottom < second_bottom ? first_bottom : second_bottom;

    if (right < left) right = left;
    if (bottom < top) bottom = top;
    return (AstraControlFrame){
        (int32_t)left, (int32_t)top,
        (uint32_t)(right - left), (uint32_t)(bottom - top)};
}

static uint32_t scroll_content_extent(const AstraControl *control, int width)
{
    uint32_t measured = width ? control->_private_measured_width :
                                control->_private_measured_height;
    uint32_t minimum = width ? control->_private_minimum_width :
                               control->_private_minimum_height;
    uint32_t maximum = width ? control->_private_maximum_width :
                               control->_private_maximum_height;

    uint32_t extent = measured > minimum ? measured : minimum;
    return extent < maximum ? extent : maximum;
}

static AstraResult layout_scroll_view(AstraUIContext *context,
                                      uint32_t index)
{
    AstraControl *view = &context->_private_controls[index];
    AstraControl *child;
    AstraControlFrame clip;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    int64_t x;
    int64_t y;
    AstraResult result;

    if (view->_private_first_child == CONTROL_NONE ||
        view->_private_first_child != view->_private_last_child)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    child = &context->_private_controls[view->_private_first_child];
    result = astra_scroll_set_extents(
        scroll_model(view), scroll_content_extent(child, 1),
        scroll_content_extent(child, 0), view->_private_frame.width,
        view->_private_frame.height);
    if (result != ASTRA_OK ||
        astra_scroll_get_state(scroll_model(view), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x = (int64_t)view->_private_frame.x - state.offset_x;
    y = (int64_t)view->_private_frame.y - state.offset_y;
    if (x < INT32_MIN || x > INT32_MAX ||
        y < INT32_MIN || y > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    clip = frame_intersection(&view->_private_frame, &view->_private_clip);
    child->_private_frame = (AstraControlFrame){
        (int32_t)x, (int32_t)y,
        state.content_width > view->_private_frame.width ?
            state.content_width : view->_private_frame.width,
        state.content_height > view->_private_frame.height ?
            state.content_height : view->_private_frame.height};
    child->_private_clip = clip;
    child->_private_laid_out = 1u;
    control_damage(context, index);
    control_damage(context, view->_private_first_child);
    return ASTRA_OK;
}

static AstraResult layout_children(AstraUIContext *context, uint32_t child,
                                   const AstraFlexLayout *layout,
                                   const AstraControlFrame *bounds,
                                   const AstraControlFrame *clip)
{
    uint32_t inner_main;
    uint32_t inner_cross;
    int64_t cross_position;
    int row;

    child = next_layout_child(context, child);
    row = layout->direction == ASTRA_FLEX_ROW;
    inner_main = row ? bounds->width : bounds->height;
    inner_cross = row ? bounds->height : bounds->width;
    {
        uint64_t main_padding = row ?
            (uint64_t)layout->padding_left + layout->padding_right :
            (uint64_t)layout->padding_top + layout->padding_bottom;
        uint64_t cross_padding = row ?
            (uint64_t)layout->padding_top + layout->padding_bottom :
            (uint64_t)layout->padding_left + layout->padding_right;

        inner_main = main_padding < inner_main ?
            inner_main - (uint32_t)main_padding : 0u;
        inner_cross = cross_padding < inner_cross ?
            inner_cross - (uint32_t)cross_padding : 0u;
    }
    for (uint32_t at = child; at != CONTROL_NONE;
         at = next_layout_child(
             context,
             context->_private_controls[at]._private_next_sibling))
        context->_private_controls[at]._private_flex_target =
            axis_base(&context->_private_controls[at], row);
    cross_position = (int64_t)(row ? bounds->y : bounds->x) +
                     (row ? layout->padding_top : layout->padding_left);
    for (uint32_t first = child; first != CONTROL_NONE;) {
        uint32_t after = first;
        uint32_t line_count = 0u;
        uint64_t natural = 0u;
        uint32_t line_cross = 0u;
        uint32_t main_gap = layout->main_gap;
        uint32_t justify_offset = 0u;
        uint32_t gap_extra = 0u;
        uint32_t gap_remainder = 0u;
        uint32_t previous = CONTROL_NONE;
        uint64_t used;
        int64_t main_position;

        while (after != CONTROL_NONE) {
            AstraControl *control = &context->_private_controls[after];
            uint64_t addition = control->_private_flex_target;

            if (line_count != 0u) addition += layout->main_gap;
            if (line_count != 0u &&
                ((control->_private_flex_flags & ASTRA_FLEX_BREAK_BEFORE) != 0u ||
                 ((layout->flags & ASTRA_FLEX_WRAP) != 0u &&
                  natural + addition > inner_main)))
                break;
            natural += addition;
            ++line_count;
            after = next_layout_child(context,
                                      control->_private_next_sibling);
            if ((control->_private_flex_flags & ASTRA_FLEX_BREAK_AFTER) != 0u)
                break;
        }
        if (!flex_line(context, first, after, line_count, inner_main,
                       layout->main_gap, row))
            return ASTRA_ERROR_NO_RESOURCES;
        used = line_used(context, first, after, line_count, layout->main_gap);
        if ((layout->flags & ASTRA_FLEX_WRAP) == 0u) {
            line_cross = inner_cross;
        } else {
            for (uint32_t at = first; at != after;
                 at = next_layout_child(
                     context,
                     context->_private_controls[at]._private_next_sibling)) {
                uint32_t value = cross_base(
                    &context->_private_controls[at], row);
                if (value > line_cross) line_cross = value;
            }
        }
        if (used < inner_main) {
            uint32_t spare = inner_main - (uint32_t)used;

            if (layout->justify == ASTRA_FLEX_JUSTIFY_CENTER)
                justify_offset = spare / 2u;
            else if (layout->justify == ASTRA_FLEX_JUSTIFY_END)
                justify_offset = spare;
            else if (layout->justify == ASTRA_FLEX_JUSTIFY_SPACE_BETWEEN &&
                     line_count > 1u) {
                uint32_t spaces = line_count - 1u;
                gap_extra = spare / spaces;
                gap_remainder = spare % spaces;
            }
        }
        main_position = (int64_t)(row ? bounds->x : bounds->y) +
                        (row ? layout->padding_left : layout->padding_top) +
                        justify_offset;
        for (uint32_t at = first; at != after;) {
            AstraControl *control = &context->_private_controls[at];
            uint32_t next = next_layout_child(
                context, control->_private_next_sibling);
            uint32_t align = control->_private_align_self ==
                    ASTRA_FLEX_ALIGN_AUTO ? layout->align_items :
                                           control->_private_align_self;
            uint32_t cross = cross_base(control, row);
            uint32_t cross_offset = 0u;
            AstraControlFrame frame;

            if (align == ASTRA_FLEX_ALIGN_STRETCH) {
                uint32_t maximum = axis_maximum(control, !row);
                cross = line_cross < maximum ? line_cross : maximum;
            } else if (line_cross > cross && align == ASTRA_FLEX_ALIGN_CENTER) {
                cross_offset = (line_cross - cross) / 2u;
            } else if (line_cross > cross && align == ASTRA_FLEX_ALIGN_END) {
                cross_offset = line_cross - cross;
            }
            if (main_position < INT32_MIN || main_position > INT32_MAX ||
                cross_position + cross_offset < INT32_MIN ||
                cross_position + cross_offset > INT32_MAX)
                return ASTRA_ERROR_NO_RESOURCES;
            frame = row ? (AstraControlFrame){
                (int32_t)main_position,
                (int32_t)(cross_position + cross_offset),
                control->_private_flex_target, cross} :
                (AstraControlFrame){
                (int32_t)(cross_position + cross_offset),
                (int32_t)main_position,
                cross, control->_private_flex_target};
            control->_private_frame = frame;
            control->_private_clip = *clip;
            control->_private_laid_out = 1u;
            control_damage(context, at);
            if (previous != CONTROL_NONE &&
                context->_private_controls[previous]._private_control_kind ==
                    ASTRA_CONTROL_SPLITTER) {
                AstraControl *splitter =
                    &context->_private_controls[previous];
                uint32_t before;
                uint32_t after_splitter;
                int splitter_row;

                if (!splitter_neighbors(context, previous, layout, &before,
                                        &after_splitter, &splitter_row) ||
                    after_splitter != at)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                splitter->_private_value = (int32_t)(splitter_row ?
                    context->_private_controls[before]._private_frame.width :
                    context->_private_controls[before]._private_frame.height);
            }
            previous = at;
            main_position += control->_private_flex_target;
            if (next != after) {
                main_position += main_gap + gap_extra;
                if (gap_remainder != 0u) {
                    ++main_position;
                    --gap_remainder;
                }
            }
            at = next;
        }
        if (previous != CONTROL_NONE &&
            context->_private_controls[previous]._private_control_kind ==
                ASTRA_CONTROL_SPLITTER)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        cross_position += line_cross;
        if (after != CONTROL_NONE) cross_position += layout->cross_gap;
        first = after;
    }
    return ASTRA_OK;
}

static int splitter_neighbors(const AstraUIContext *context, uint32_t index,
                              const AstraFlexLayout *root_layout,
                              uint32_t *before, uint32_t *after, int *row)
{
    const AstraControl *splitter;
    const AstraFlexLayout *layout;
    uint32_t first;
    uint32_t previous = CONTROL_NONE;
    uint32_t at;

    if (index >= context->_private_control_count || before == NULL ||
        after == NULL || row == NULL)
        return 0;
    splitter = &context->_private_controls[index];
    if (splitter->_private_control_kind != ASTRA_CONTROL_SPLITTER ||
        splitter->_private_laid_out == 0u)
        return 0;
    if (splitter->_private_parent == CONTROL_NONE) {
        layout = root_layout;
        first = context->_private_first_child;
    } else {
        const AstraControl *parent =
            &context->_private_controls[splitter->_private_parent];

        if (parent->_private_control_kind != ASTRA_CONTROL_CONTAINER)
            return 0;
        layout = &parent->_private_child_layout;
        first = parent->_private_first_child;
    }
    if (!layout_valid(layout) || (layout->flags & ASTRA_FLEX_WRAP) != 0u)
        return 0;
    *row = layout->direction == ASTRA_FLEX_ROW;
    if (splitter->_private_style != (*row ? ASTRA_ORIENTATION_VERTICAL :
                                            ASTRA_ORIENTATION_HORIZONTAL))
        return 0;
    for (at = first; at != CONTROL_NONE && at != index;
         at = context->_private_controls[at]._private_next_sibling)
        previous = at;
    if (at != index || previous == CONTROL_NONE)
        return 0;
    at = splitter->_private_next_sibling;
    if (at == CONTROL_NONE ||
        context->_private_controls[previous]._private_laid_out == 0u ||
        context->_private_controls[at]._private_laid_out == 0u ||
        context->_private_controls[previous]._private_control_kind ==
            ASTRA_CONTROL_SPLITTER ||
        context->_private_controls[at]._private_control_kind ==
            ASTRA_CONTROL_SPLITTER)
        return 0;
    *before = previous;
    *after = at;
    return 1;
}

AstraResult astra_interface_ui_layout(AstraUIContext *context,
                                      const AstraFlexLayout *layout)
{
    AstraControlFrame root;
    AstraResult result;

    if (!context_valid(context) || !layout_valid(layout))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        uint32_t at = count - 1u;
        AstraControl *control = &context->_private_controls[at];

        if (!control_valid(control))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER) {
            AstraControlSize measured;

            result = measure_container(context, at, &measured);
            if (result != ASTRA_OK) return result;
            control->_private_measured_width = measured.width;
            control->_private_measured_height = measured.height;
        } else if (control->_private_measured_width == 0u ||
                   control->_private_measured_height == 0u) {
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        control_damage(context, at);
        context->_private_controls[at]._private_laid_out = 0u;
        context->_private_controls[at]._private_clip = (AstraControlFrame){0};
    }
    root = (AstraControlFrame){
        0, 0, context->_private_width, context->_private_height};
    result = layout_children(context, context->_private_first_child, layout,
                             &root, &root);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *container = &context->_private_controls[at];
        AstraControlFrame clip;

        if (!CONTAINER_KINDS(container->_private_control_kind) ||
            container->_private_laid_out == 0u)
            continue;
        if (container->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
            result = layout_scroll_view(context, at);
            if (result != ASTRA_OK) return result;
            continue;
        }
        clip = frame_intersection(&container->_private_frame,
                                  &container->_private_clip);
        result = layout_children(context, container->_private_first_child,
                                 &container->_private_child_layout,
                                 &container->_private_frame, &clip);
        if (result != ASTRA_OK) return result;
    }
    context->_private_layout = *layout;
    context->_private_has_layout = 1u;
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        animation_sync(context, at);
    return ASTRA_OK;
}

static AstraResult splitter_set_position(AstraUIContext *context,
                                         uint32_t index, int64_t position,
                                         AstraUIAction *action)
{
    AstraControl *splitter;
    AstraControl *before_control;
    AstraControl *after_control;
    uint32_t before;
    uint32_t after;
    uint32_t before_size;
    uint32_t after_size;
    uint32_t old_before_basis;
    uint32_t old_after_basis;
    int64_t delta;
    int64_t minimum_delta;
    int64_t maximum_delta;
    int64_t bound;
    int row;
    AstraResult result;

    if (!context_valid(context) || context->_private_has_layout == 0u ||
        !splitter_neighbors(context, index, &context->_private_layout,
                            &before, &after, &row))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    splitter = &context->_private_controls[index];
    before_control = &context->_private_controls[before];
    after_control = &context->_private_controls[after];
    before_size = row ? before_control->_private_frame.width :
                        before_control->_private_frame.height;
    after_size = row ? after_control->_private_frame.width :
                       after_control->_private_frame.height;
    minimum_delta = (int64_t)axis_minimum(before_control, row) - before_size;
    bound = (int64_t)after_size - axis_maximum(after_control, row);
    if (bound > minimum_delta) minimum_delta = bound;
    maximum_delta = (int64_t)axis_maximum(before_control, row) - before_size;
    bound = (int64_t)after_size - axis_minimum(after_control, row);
    if (bound < maximum_delta) maximum_delta = bound;
    if (minimum_delta > maximum_delta)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (position <= (int64_t)before_size + minimum_delta)
        delta = minimum_delta;
    else if (position >= (int64_t)before_size + maximum_delta)
        delta = maximum_delta;
    else
        delta = position - before_size;
    if (delta == 0)
        return ASTRA_OK;

    old_before_basis = before_control->_private_flex_basis;
    old_after_basis = after_control->_private_flex_basis;
    before_control->_private_flex_basis =
        (uint32_t)((int64_t)before_size + delta);
    after_control->_private_flex_basis =
        (uint32_t)((int64_t)after_size - delta);
    result = astra_interface_ui_layout(context, &context->_private_layout);
    if (result != ASTRA_OK) {
        before_control->_private_flex_basis = old_before_basis;
        after_control->_private_flex_basis = old_after_basis;
        (void)astra_interface_ui_layout(context, &context->_private_layout);
        return result;
    }
    if (action != NULL) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = splitter->_private_id;
        action->value = splitter->_private_value;
        action->decimal_places = 0u;
    }
    return ASTRA_OK;
}

static int control_descends_from(const AstraUIContext *context,
                                 uint32_t index, uint32_t ancestor)
{
    uint32_t at = context->_private_controls[index]._private_parent;

    while (at != CONTROL_NONE) {
        if (at == ancestor) return 1;
        at = context->_private_controls[at]._private_parent;
    }
    return 0;
}

static AstraResult relayout_scroll_model(AstraUIContext *context,
                                         const AstraScrollModel *model)
{
    int found = 0;

    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *view = &context->_private_controls[at];
        AstraResult result;

        if (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
            const_scroll_model(view) != model ||
            view->_private_laid_out == 0u)
            continue;
        found = 1;
        control_damage(context, at);
        result = layout_scroll_view(context, at);
        if (result != ASTRA_OK) return result;
        for (uint32_t child = at + 1u;
             child < context->_private_control_count; ++child) {
            AstraControl *container = &context->_private_controls[child];
            AstraControlFrame clip;

            if (!control_descends_from(context, child, at) ||
                !CONTAINER_KINDS(container->_private_control_kind) ||
                container->_private_laid_out == 0u)
                continue;
            if (container->_private_control_kind ==
                ASTRA_CONTROL_SCROLL_VIEW) {
                result = layout_scroll_view(context, child);
            } else {
                clip = frame_intersection(&container->_private_frame,
                                          &container->_private_clip);
                result = layout_children(
                    context, container->_private_first_child,
                    &container->_private_child_layout,
                    &container->_private_frame, &clip);
            }
            if (result != ASTRA_OK) return result;
        }
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at)
        if (context->_private_controls[at]._private_control_kind ==
                ASTRA_CONTROL_SCROLLBAR &&
            const_scroll_model(&context->_private_controls[at]) == model)
            control_damage(context, at);
    return found ? ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
}

AstraResult astra_interface_ui_set_state(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t state)
{
    uint32_t index = control_index(context, control);
    uint32_t allowed;
    uint32_t before;
    AstraResult result;

    if (index == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (CONTAINER_KINDS(control->_private_control_kind))
        allowed = ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR ||
             control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        allowed = ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_COLLAPSED;
    else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
             control->_private_control_kind == ASTRA_CONTROL_DIAL ||
             control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
             control->_private_control_kind == ASTRA_CONTROL_PROGRESS ||
             control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER ||
             control->_private_control_kind == ASTRA_CONTROL_SPARKLINE ||
             control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL ||
             CHOICE_KINDS(control->_private_control_kind) ||
             TEXT_MODEL_KINDS(control->_private_control_kind))
        allowed = CONTROL_VALUE_STATES;
    else
        allowed = CONTROL_SEMANTIC_STATES;
    if ((state & ~allowed) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_RADIO &&
        (state & ASTRA_CONTROL_SELECTED) != 0u) {
        for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
            AstraControl *peer = &context->_private_controls[at];

            if (at != index &&
                peer->_private_control_kind == ASTRA_CONTROL_RADIO &&
                peer->_private_style == control->_private_style &&
                (peer->_private_state & ASTRA_CONTROL_SELECTED) != 0u) {
                control_damage(context, at);
                peer->_private_state &= ~ASTRA_CONTROL_SELECTED;
                control_damage(context, at);
            }
        }
    }
    if (control->_private_state == state)
        return ASTRA_OK;
    before = control->_private_state;
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        disclosure_target_damage(context, control->_private_id);
    control_damage(context, index);
    control->_private_state = state;
    if ((state & ASTRA_CONTROL_DISABLED) != 0u) {
        if (context->_private_hover == index) set_hover(context, CONTROL_NONE);
        if (context->_private_capture == index) clear_capture(context);
        if (context->_private_keyboard == index) clear_keyboard(context);
        if (context->_private_focus == index)
            set_focus(context, CONTROL_NONE, 0);
    }
    if (((before ^ state) & ASTRA_CONTROL_COLLAPSED) != 0u &&
        context->_private_has_layout != 0u) {
        result = astra_interface_ui_layout(context,
                                           &context->_private_layout);
        if (result != ASTRA_OK) {
            control->_private_state = before;
            (void)astra_interface_ui_layout(context,
                                            &context->_private_layout);
            if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
                disclosure_target_damage(context, control->_private_id);
            return result;
        }
        if (context->_private_hover != CONTROL_NONE &&
            context->_private_controls[context->_private_hover]
                    ._private_laid_out == 0u)
            set_hover(context, CONTROL_NONE);
        if (context->_private_capture != CONTROL_NONE &&
            context->_private_controls[context->_private_capture]
                    ._private_laid_out == 0u)
            clear_capture(context);
        if (context->_private_keyboard != CONTROL_NONE &&
            context->_private_controls[context->_private_keyboard]
                    ._private_laid_out == 0u)
            clear_keyboard(context);
        if (context->_private_focus != CONTROL_NONE &&
            context->_private_controls[context->_private_focus]
                    ._private_laid_out == 0u)
            set_focus(context, CONTROL_NONE, 0);
        if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
            disclosure_target_damage(context, control->_private_id);
        return ASTRA_OK;
    }
    animation_sync(context, index);
    control_damage(context, index);
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER)
        disclosure_target_damage(context, control->_private_id);
    return ASTRA_OK;
}

static uint64_t range_offset_at(uint64_t span, uint32_t position,
                                uint32_t extent)
{
    uint64_t quotient;
    uint64_t remainder;

    if (extent == 0u) return 0u;
    quotient = span / extent;
    remainder = span % extent;
    return quotient * position + remainder * position / extent;
}

static uint32_t range_position(uint64_t offset, uint64_t span,
                               uint32_t extent)
{
    uint32_t low = 0u;
    uint32_t high = extent;

    while (low < high) {
        uint32_t middle = low + (high - low + 1u) / 2u;

        if (range_offset_at(span, middle, extent) <= offset)
            low = middle;
        else
            high = middle - 1u;
    }
    return low;
}

static int64_t range_snap(const AstraControl *control, int64_t value)
{
    int64_t minimum = control->_private_minimum_value;
    int64_t maximum = control->_private_maximum_value;
    uint64_t step = (uint64_t)control->_private_value_step;
    uint64_t span;
    uint64_t offset;
    uint64_t lower;
    uint64_t upper;

    if (value <= minimum) return minimum;
    if (value >= maximum) return maximum;
    span = range_offset(maximum, minimum);
    offset = range_offset(value, minimum);
    lower = offset / step * step;
    upper = step <= span - lower ? lower + step : span;
    return range_value_at(
        minimum,
        offset - lower < upper - offset ? lower : upper);
}

static int64_t range_move_steps(const AstraControl *control, int64_t value,
                                int64_t steps)
{
    uint64_t offset = range_offset(value, control->_private_minimum_value);
    uint64_t span = range_offset(control->_private_maximum_value,
                                 control->_private_minimum_value);
    uint64_t step = (uint64_t)control->_private_value_step;
    uint64_t count;

    if (steps >= 0) {
        count = (uint64_t)steps;
        if (count > (span - offset) / step)
            return control->_private_maximum_value;
        offset += count * step;
    } else {
        count = 0u - (uint64_t)steps;
        if (count > offset / step)
            return control->_private_minimum_value;
        offset -= count * step;
    }
    return range_value_at(control->_private_minimum_value, offset);
}

static int stepper_draft_value(const AstraUIContext *context,
                               int64_t *value)
{
    uint64_t magnitude = context->_private_value_input;

    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u ||
        context->_private_value_input_digits == 0u)
        return 0;
    if ((context->_private_value_input_flags & VALUE_INPUT_NEGATIVE) != 0u) {
        if (magnitude > UINT64_C(9223372036854775808)) return 0;
        *value = magnitude == UINT64_C(9223372036854775808) ? INT64_MIN :
                 -(int64_t)magnitude;
    } else {
        if (magnitude > INT64_MAX) return 0;
        *value = (int64_t)magnitude;
    }
    return 1;
}

static uint32_t stepper_format(const AstraUIContext *context,
                               uint32_t index, char text[22])
{
    const AstraControl *control = &context->_private_controls[index];
    char reversed[20];
    uint64_t magnitude;
    uint32_t length = 0u;
    uint32_t digits = 0u;
    int negative;

    if (context->_private_focus == index &&
        (context->_private_value_input_flags & VALUE_INPUT_EDITING) != 0u) {
        magnitude = context->_private_value_input;
        negative = (context->_private_value_input_flags &
                    VALUE_INPUT_NEGATIVE) != 0u;
        if (negative) text[length++] = '-';
        if (context->_private_value_input_digits == 0u)
            return length;
    } else {
        int64_t value = control->_private_value;

        negative = value < 0;
        magnitude = negative ? 0u - (uint64_t)value : (uint64_t)value;
        if (negative) text[length++] = '-';
    }
    do {
        reversed[digits++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);
    while (digits != 0u) text[length++] = reversed[--digits];
    return length;
}

AstraResult astra_interface_control_set_value(AstraUIContext *context,
                                              AstraControl *control,
                                              int64_t value)
{
    uint32_t index = control_index(context, control);
    int64_t snapped;

    if (index == CONTROL_NONE ||
        (control->_private_control_kind != ASTRA_CONTROL_SLIDER &&
         control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         control->_private_control_kind != ASTRA_CONTROL_STEPPER &&
         control->_private_control_kind != ASTRA_CONTROL_SPLITTER &&
         !CHOICE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        return splitter_set_position(context, index, value, NULL);
    if (CHOICE_KINDS(control->_private_control_kind)) {
        if (value < (COLLECTION_KINDS(control->_private_control_kind) ?
                     -1 : 0) || value > control->_private_maximum_value)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        snapped = value;
        if (COLLECTION_KINDS(control->_private_control_kind) &&
            snapped >= 0 &&
            !collection_row_selectable(control, (uint32_t)snapped))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    } else {
        snapped = range_snap(control, value);
    }
    if (snapped == control->_private_value &&
        (!COLLECTION_KINDS(control->_private_control_kind) ||
         collection_model(control)->selection == NULL ||
         (snapped < 0 ?
            collection_model(control)->selection->count == 0u :
            collection_model(control)->selection->count == 1u &&
            collection_model(control)->selection->ids[0] ==
                collection_model(control)->selected_id)))
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_value = snapped;
    if (COLLECTION_KINDS(control->_private_control_kind)) {
        AstraCollectionModel *model = collection_model(control);
        uint32_t item = control->_private_control_kind == ASTRA_CONTROL_TREE &&
                        snapped >= 0 ?
            tree_model(control)->visible_rows[snapped] : (uint32_t)snapped;
        model->selected_id = snapped < 0 ? 0u : model->items[item].id;
        selection_single(model->selection, model->selected_id);
    }
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_control_get_value(const AstraControl *control,
                                              int64_t *value,
                                              uint32_t *decimal_places)
{
    if (!control_valid(control) || value == NULL ||
        (control->_private_control_kind != ASTRA_CONTROL_SLIDER &&
         control->_private_control_kind != ASTRA_CONTROL_DIAL &&
         control->_private_control_kind != ASTRA_CONTROL_STEPPER &&
         control->_private_control_kind != ASTRA_CONTROL_PROGRESS &&
         control->_private_control_kind != ASTRA_CONTROL_SPLITTER &&
         !CHOICE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *value = control->_private_value;
    if (decimal_places != NULL)
        *decimal_places = control->_private_decimal_places;
    return ASTRA_OK;
}

AstraResult astra_interface_progress_set(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t value, uint32_t maximum)
{
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_PROGRESS ||
        value > maximum || maximum > INT32_MAX ||
        (maximum == 0u && value != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_value == (int32_t)value &&
        control->_private_maximum_value == (int32_t)maximum)
        return ASTRA_OK;
    control_damage(context, index);
    control->_private_value = (int32_t)value;
    control->_private_maximum_value = (int32_t)maximum;
    animation_sync(context, index);
    control_damage(context, index);
    return ASTRA_OK;
}

AstraResult astra_interface_display_refresh(AstraUIContext *context,
                                             AstraControl *control)
{
    uint32_t index = control_index(context, control);

    if (index == CONTROL_NONE ||
        (control->_private_control_kind != ASTRA_CONTROL_LEVEL_METER &&
         control->_private_control_kind != ASTRA_CONTROL_SPARKLINE) ||
        !astra_interface_display_model_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control_damage(context, index);
    return ASTRA_OK;
}

static AstraColorRGBA8 label_color(const AstraTheme *theme,
                                   const AstraControl *control)
{
    if ((effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->text_muted;
    switch (control->_private_style) {
    case ASTRA_TEXT_CLIENT_SECONDARY: return theme->client_text_secondary;
    case ASTRA_TEXT_CLIENT_TERTIARY: return theme->text_tertiary;
    case ASTRA_TEXT_CLIENT_MUTED: return theme->text_muted;
    case ASTRA_TEXT_CLIENT_FAULT: return theme->fault;
    default: return theme->client_text;
    }
}

static AstraColorRGBA8 button_fill(const AstraTheme *theme,
                                   const AstraControl *control)
{
    uint32_t state = effective_state(control);

    if ((control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_TOOLBAR ||
        (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_SPLIT_MENU) {
        if ((state & ASTRA_CONTROL_SELECTED) != 0u)
            return theme->control_selected;
        if ((state & ASTRA_CONTROL_PRESSED) != 0u)
            return theme->control_hover;
        if ((state & ASTRA_CONTROL_HOVERED) != 0u)
            return theme->surface_raised;
        return theme->surface_inset;
    }

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->control_disabled;
    if ((state & ASTRA_CONTROL_PRESSED) != 0u)
        return theme->control_pressed;
    if ((state & ASTRA_CONTROL_SELECTED) != 0u)
        return theme->control_selected;
    if (control->_private_style == ASTRA_BUTTON_PRIMARY)
        return theme->accent;
    if (control->_private_style == ASTRA_BUTTON_WARNING)
        return theme->warning;
    if (control->_private_style == ASTRA_BUTTON_DESTRUCTIVE)
        return theme->fault;
    if ((state & ASTRA_CONTROL_HOVERED) != 0u)
        return theme->control_hover;
    return theme->control;
}

static AstraColorRGBA8 button_text(const AstraTheme *theme,
                                   const AstraControl *control)
{
    uint32_t state = effective_state(control);

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        return theme->text_tertiary;
    if ((control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_TOOLBAR ||
        (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
            ASTRA_BUTTON_SPLIT_MENU)
        return theme->client_text;
    if ((state & ASTRA_CONTROL_PRESSED) != 0u)
        return theme->text_secondary;
    if ((state & ASTRA_CONTROL_SELECTED) != 0u ||
        control->_private_style == ASTRA_BUTTON_PRIMARY ||
        control->_private_style == ASTRA_BUTTON_WARNING)
        return theme->accent_text;
    return theme->text_primary;
}

static AstraColorRGBA8 toggle_text(const AstraTheme *theme,
                                   const AstraControl *control)
{
    return (effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->client_text;
}

typedef struct FieldView {
    AstraTextModelState state;
    uint32_t start;
    uint32_t end;
    uint32_t columns;
    uint32_t caret_column;
    uint32_t selection_first;
    uint32_t selection_last;
} FieldView;

static int model_scalar_count(const AstraTextModel *model, uint32_t start,
                              uint32_t end, uint32_t *count)
{
    uint32_t total = 0u;

    while (start < end) {
        const char *text;
        uint32_t bytes;
        uint32_t limit;
        uint32_t at = 0u;

        if (astra_text_model_read(model, start, &text, &bytes) != ASTRA_OK ||
            bytes == 0u)
            return 0;
        limit = bytes < end - start ? bytes : end - start;
        while (at < limit) {
            uint32_t before = at;

            if (!astra_utf8_scalar_advance(text, limit, &at) || at <= before)
                return 0;
            ++total;
        }
        start += limit;
    }
    *count = total;
    return 1;
}

static int model_advance_scalars(const AstraTextModel *model,
                                 uint32_t start, uint32_t maximum,
                                 uint32_t *end)
{
    uint32_t moved = 0u;

    while (moved < maximum) {
        const char *text;
        uint32_t bytes;
        uint32_t at = 0u;

        if (astra_text_model_read(model, start, &text, &bytes) != ASTRA_OK)
            return 0;
        if (bytes == 0u)
            break;
        while (at < bytes && moved < maximum) {
            uint32_t before = at;

            if (!astra_utf8_scalar_advance(text, bytes, &at) || at <= before)
                return 0;
            ++moved;
        }
        start += at;
    }
    *end = start;
    return 1;
}

static uint32_t text_view_unit_width(const AstraControl *control)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    return (control->_private_style & TEXT_VIEW_FLOW) != 0u ?
        1u : theme.mono_cell_width;
}

static uint32_t text_view_line_height(const AstraControl *control)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t height = (control->_private_style & TEXT_VIEW_FLOW) != 0u ?
        astra_surface_ui_text_height(theme.body_font_height) : 0u;
    return height > theme.control_height ? height : theme.control_height;
}

static uint32_t text_view_gutter_width(const AstraControl *control)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint32_t digits = 1u;
    uint32_t number;
    if ((control->_private_style &
         (TEXT_VIEW_LINE_NUMBERS | TEXT_VIEW_FLOW)) !=
        TEXT_VIEW_LINE_NUMBERS ||
        astra_text_model_get_state(const_control_text_model(control),
                                   &state) != ASTRA_OK)
        return 0u;
    number = state.line_count;
    while (number >= 10u) { number /= 10u; ++digits; }
    return (digits + 2u) * theme.mono_cell_width;
}

static uint32_t text_view_line_number(uint32_t number, char output[10])
{
    char reversed[10];
    uint32_t count = 0u;
    do {
        reversed[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0u);
    for (uint32_t at = 0u; at < count; ++at)
        output[at] = reversed[count - at - 1u];
    return count;
}

static int text_view_step(const AstraControl *control,
                          const AstraTextModel *model, uint32_t end,
                          uint32_t *offset, uint32_t column,
                          uint32_t *scalar, uint32_t *width)
{
    const char *text;
    uint32_t bytes, consumed;
    uint32_t before = *offset;
    if (astra_text_model_read(model, *offset, &text, &bytes) != ASTRA_OK ||
        bytes == 0u) return 0;
    *scalar = astra_utf8_decode((const uint8_t *)text, bytes, &consumed);
    if (consumed == 0u || consumed > end - *offset) return 0;
    *offset += consumed;
    *width = *scalar == '\t' ? 8u - column % 8u :
             *scalar == '\r' && *offset == end ? 0u : 1u;
    if ((control->_private_style & TEXT_VIEW_FLOW) != 0u && *width != 0u) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        AstraTextViewStyle style = ASTRA_TEXT_VIEW_STYLE_INIT;
        if (control->_private_text_style_at != NULL &&
            (control->_private_text_style_at(
                control->_private_text_style_context, before, &style) != ASTRA_OK ||
             style.reserved != 0u ||
             (style.attributes & ~ASTRA_TEXT_STYLE_ALLOWED_MASK) != 0u))
            return 0;
        if (*scalar == '\t') {
            uint32_t space = astra_surface_ui_text_width_styled(" ", 1u,
                theme.body_font_height,
                style.attributes & ASTRA_TEXT_RENDER_STYLE_MASK);
            uint32_t tab = space * 8u;
            *width = tab != 0u ? tab - column % tab : 0u;
        } else {
            *width = astra_surface_ui_text_width_styled(text, consumed,
                theme.body_font_height,
                style.attributes & ASTRA_TEXT_RENDER_STYLE_MASK);
        }
    }
    if (*scalar < 0x20u || *scalar == 0x7fu)
        *scalar = *scalar == '\t' ? ' ' : ASTRA_UTF8_REPLACEMENT;
    return column <= UINT32_MAX - *width;
}

static int text_view_column(const AstraControl *control,
                            const AstraTextModel *model, const AstraTextLine *line,
                            uint32_t limit, uint32_t *offset, uint32_t *column)
{
    *offset = line->start;
    *column = 0u;
    while (*offset < line->content_end) {
        uint32_t next = *offset, scalar, width;
        if (!text_view_step(control, model, line->content_end, &next, *column,
                            &scalar, &width)) return 0;
        if (width != 0u && limit - *column < (width + 1u) / 2u) break;
        *offset = next;
        *column += width;
        if (*column >= limit) break;
    }
    return 1;
}

typedef struct TextViewRows {
    const AstraControl *control;
    const AstraTextModel *model;
    uint32_t logical_count;
    uint32_t logical_row;
    uint32_t next_offset;
    uint32_t columns;
    uint8_t continuing;
} TextViewRows;

typedef struct TextViewRow {
    uint32_t logical_row;
    uint32_t start;
    uint32_t end;
    uint32_t hard_end;
    uint32_t columns;
} TextViewRow;

static uint32_t text_view_wrap_columns(const AstraControl *control)
{
    uint32_t gutter = text_view_gutter_width(control);
    uint32_t available = control->_private_measured_width > gutter ?
        control->_private_measured_width - gutter : 1u;
    uint32_t cells = available /
        text_view_unit_width(control);
    return cells != 0u ? cells : 1u;
}

/* Returns one for a visual row, zero at end, or minus one for invalid text. */
static int text_view_next_row(TextViewRows *rows, TextViewRow *visual)
{
    AstraTextLine line = ASTRA_TEXT_LINE_INIT;
    uint32_t offset, column = 0u, last_break = 0u, break_column = 0u;
    for (;;) {
        if (rows->logical_row >= rows->logical_count) return 0;
        if (astra_text_model_get_line(rows->model, rows->logical_row,
                                      &line) != ASTRA_OK) return -1;
        visual->logical_row = rows->logical_row;
        visual->start = rows->continuing ? rows->next_offset : line.start;
        if (!rows->continuing) break;
        while (visual->start < line.content_end) {
            uint32_t next = visual->start, scalar, width;
            if (!text_view_step(rows->control, rows->model,
                                line.content_end, &next, 0u,
                                &scalar, &width)) return -1;
            if (scalar != ' ' && scalar != '\t') break;
            visual->start = next;
        }
        if (visual->start < line.content_end) break;
        visual->end = visual->hard_end = line.content_end;
        visual->columns = 0u;
        ++rows->logical_row;
        rows->continuing = 0u;
        return 1;
    }
    visual->hard_end = line.content_end;
    if (visual->start > line.content_end) return -1;
    offset = visual->start;
    while (offset < line.content_end) {
        uint32_t next = offset, scalar, width;
        if (!text_view_step(rows->control, rows->model,
                            line.content_end, &next, column,
                            &scalar, &width)) return -1;
        if (width > rows->columns - column && offset != visual->start) {
            if (last_break > visual->start) {
                offset = last_break;
                column = break_column;
            }
            break;
        }
        offset = next;
        column += width;
        if (scalar == ' ' || scalar == '\t') {
            last_break = offset;
            break_column = column;
        }
        if (column >= rows->columns && offset < line.content_end) {
            if (rows->columns == UINT32_MAX) return -1;
            if (last_break > visual->start && last_break < offset) {
                offset = last_break;
                column = break_column;
            }
            break;
        }
    }
    visual->end = offset;
    visual->columns = column;
    if (offset == line.content_end) {
        ++rows->logical_row;
        rows->continuing = 0u;
    } else {
        rows->next_offset = offset;
        rows->continuing = 1u;
    }
    return 1;
}

static int text_view_row_at(const AstraControl *control,
    const AstraTextModel *model, uint32_t logical_count,
    uint32_t columns, uint32_t index, TextViewRow *visual)
{
    TextViewRows rows = {control, model, logical_count, 0u, 0u, columns, 0u};
    int result;
    do {
        result = text_view_next_row(&rows, visual);
        if (result <= 0) return result;
    } while (index-- != 0u);
    return 1;
}

static int text_view_visual_location(const AstraControl *control,
    const AstraTextModel *model,
    uint32_t logical_count, uint32_t columns, uint32_t position,
    uint32_t soft_end,
    uint32_t *row, uint32_t *column, TextViewRow *visual)
{
    TextViewRows rows = {control, model, logical_count, 0u, 0u, columns, 0u};
    *row = 0u;
    for (;;) {
        int result = text_view_next_row(&rows, visual);
        if (result <= 0) return 0;
        int at_soft_end = position == visual->end &&
            visual->end != visual->hard_end;
        if (at_soft_end && !soft_end) {
            TextViewRows following = rows;
            TextViewRow next;
            int found = text_view_next_row(&following, &next);
            if (found < 0) return 0;
            if (found == 0 || next.start > position) soft_end = 1u;
        }
        if (position < visual->end ||
            (position == visual->end &&
             (visual->end == visual->hard_end || soft_end))) {
            uint32_t offset = visual->start;
            *column = 0u;
            while (offset < position) {
                uint32_t scalar, width;
                if (!text_view_step(control, model, visual->end, &offset, *column,
                                    &scalar, &width)) return 0;
                *column += width;
            }
            return 1;
        }
        ++*row;
    }
}

static int text_view_segment_column(const AstraControl *control,
    const AstraTextModel *model,
    const TextViewRow *visual, uint32_t limit, uint32_t *position,
    uint32_t *soft_end)
{
    uint32_t offset = visual->start, column = 0u;
    while (offset < visual->end) {
        uint32_t next = offset, scalar, width;
        if (!text_view_step(control, model, visual->end, &next, column,
                            &scalar, &width)) return 0;
        if (width != 0u && limit - column < (width + 1u) / 2u) break;
        offset = next;
        column += width;
        if (column >= limit) break;
    }
    *position = offset;
    *soft_end = offset == visual->end && visual->end != visual->hard_end;
    return 1;
}

static AstraResult text_view_measure(const AstraControl *control,
                                     AstraControlSize *size)
{
    const AstraTextModel *model = const_control_text_model(control);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t widest = 1u;
    if (astra_text_model_get_state(model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (text_view_line_height(control) > UINT16_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    uint32_t columns = (control->_private_style & TEXT_VIEW_WRAP) != 0u ?
        text_view_wrap_columns(control) : UINT32_MAX;
    TextViewRows rows = {control, model, state.line_count, 0u, 0u, columns, 0u};
    TextViewRow visual;
    uint64_t row_count = 0u;
    int result;
    while ((result = text_view_next_row(&rows, &visual)) > 0) {
        if (visual.columns > widest) widest = visual.columns;
        ++row_count;
    }
    if (result < 0) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint64_t width = (control->_private_style & TEXT_VIEW_WRAP) != 0u ?
        control->_private_measured_width :
        (uint64_t)widest * text_view_unit_width(control) +
        text_view_gutter_width(control);
    uint64_t height = row_count * text_view_line_height(control);
    if (width > INT32_MAX || height > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    *size = (AstraControlSize){(uint32_t)width, (uint32_t)height};
    return ASTRA_OK;
}

static AstraResult grid_view_measure(const AstraControl *control,
                                     AstraControlSize *size)
{
    const AstraGridViewModel *model = grid_view_model(control);
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    if (!grid_view_model_valid(model) || size == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint64_t width = (uint64_t)model->columns * theme.mono_cell_width;
    /* Empty grids still need a positive measured size for flex layout. */
    uint64_t height = model->rows ?
        (uint64_t)model->rows * theme.control_height : 1u;
    if (width > INT32_MAX || height > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    *size = (AstraControlSize){(uint32_t)width, (uint32_t)height};
    return ASTRA_OK;
}

typedef struct TextViewFlowRun {
    uint32_t bytes;
    uint32_t advance;
    uint32_t column;
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
} TextViewFlowRun;

static AstraResult text_view_flow_flush(const AstraTextSurface *text,
    AstraSurfaceView *surface, int32_t x, int32_t y,
    const char *buffer, TextViewFlowRun *run,
    uint64_t *commands, uint64_t *payload)
{
    AstraResult result;
    if (run->bytes == 0u) return ASTRA_OK;
    result = surface != NULL ?
        astra_text_surface_render_flow_run(text, surface,
            x + (int32_t)run->column, y, buffer, run->bytes, run->advance,
            run->foreground, run->background, run->attributes) :
        astra_text_surface_flow_run_requirements(text, buffer, run->bytes,
            run->advance, run->foreground, run->background,
            run->attributes, commands, payload);
    if (result == ASTRA_OK) {
        run->bytes = 0u;
        run->advance = 0u;
    }
    return result;
}

static AstraResult text_view_render(const AstraControl *control,
    AstraSurfaceView *surface, uint64_t *commands, uint64_t *payload)
{
    const AstraTextModel *model = const_control_text_model(control);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;
    AstraTextSurface text = ASTRA_TEXT_SURFACE_INIT;
    AstraTextSurfaceInfo info = ASTRA_TEXT_SURFACE_INFO_INIT;
    char scratch[TEXT_VIEW_CELL_BATCH * 4u];
    AstraTextCell cells[TEXT_VIEW_CELL_BATCH];
    int inverted = (control->_private_style & TEXT_VIEW_INVERTED) != 0u;
    uint32_t gutter = text_view_gutter_width(control);
    int64_t content_x = (int64_t)frame->x + gutter;
    uint32_t first_row, last_row, first_column, last_column;
    uint32_t selection_start, selection_end;
    uint32_t line_height = text_view_line_height(control);
    int64_t left = clip->x > frame->x ? clip->x : frame->x;
    int64_t top = clip->y > frame->y ? clip->y : frame->y;
    int64_t right = (int64_t)clip->x + clip->width;
    int64_t bottom = (int64_t)clip->y + clip->height;
    if (right > (int64_t)frame->x + frame->width)
        right = (int64_t)frame->x + frame->width;
    if (bottom > (int64_t)frame->y + frame->height)
        bottom = (int64_t)frame->y + frame->height;
    if (right <= left || bottom <= top) return ASTRA_OK;
    if (astra_text_model_get_state(model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (line_height > UINT16_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    first_row = (uint32_t)((top - frame->y) / line_height);
    last_row = (uint32_t)((bottom - frame->y + line_height - 1u) /
                          line_height);
    uint32_t unit_width = text_view_unit_width(control);
    first_column = left <= content_x ? 0u :
        (uint32_t)((left - content_x) / unit_width);
    last_column = right <= content_x ? 0u :
        (uint32_t)((right - content_x + unit_width - 1u) /
                   unit_width);
    selection_start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    selection_end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    info.mode = (control->_private_style & TEXT_VIEW_FLOW) != 0u ?
        ASTRA_TEXT_SURFACE_FLOW : ASTRA_TEXT_SURFACE_CODE;
    info.font_height = info.mode == ASTRA_TEXT_SURFACE_FLOW ?
        theme.body_font_height : theme.mono_font_height;
    info.cell_width = theme.mono_cell_width;
    info.line_height = (uint16_t)line_height;
    info.default_foreground = color((effective_state(control) &
        ASTRA_CONTROL_DISABLED) != 0u ? theme.text_tertiary :
        inverted ? theme.client : theme.client_text);
    info.default_background = color(inverted ? theme.client_text : theme.client);
    info.scratch = scratch; info.scratch_bytes = sizeof(scratch);
    if (astra_text_surface_init(&text, &info) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (surface != NULL)
        astra_surface_fill(surface, frame->x, frame->y, frame->width,
                            frame->height, info.default_background);
    else ++*commands;
    if (gutter != 0u) {
        if (surface != NULL) {
            astra_surface_fill(surface, frame->x, frame->y, gutter,
                frame->height, color(theme.surface_inset));
            astra_surface_fill(surface, (int32_t)content_x - 1,
                frame->y, 1u, frame->height, color(theme.client_border));
        } else *commands += 2u;
    }
    int wrapped = (control->_private_style & TEXT_VIEW_WRAP) != 0u;
    uint32_t wrap_columns = wrapped ?
        text_view_wrap_columns(control) : UINT32_MAX;
    TextViewRows rows = {control, model, state.line_count,
        wrapped ? 0u : first_row, 0u, wrap_columns, 0u};
    TextViewRow visual;
    for (uint32_t row = wrapped ? 0u : first_row;
         row < last_row; ++row) {
        uint32_t offset, column = 0u, count = 0u, run_column = first_column;
        TextViewFlowRun flow = {0};
        int next = text_view_next_row(&rows, &visual);
        if (next < 0) return ASTRA_ERROR_INVALID_ARGUMENT;
        if (next == 0) break;
        if (row < first_row) continue;
        if (gutter != 0u) {
            AstraTextLine logical = ASTRA_TEXT_LINE_INIT;
            if (astra_text_model_get_line(model, visual.logical_row,
                                          &logical) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (visual.start == logical.start) {
                char number[10];
                uint32_t digits = text_view_line_number(
                    visual.logical_row + 1u, number);
                if (surface != NULL) {
                    if (!astra_surface_mono_text_styled(surface,
                        (int32_t)content_x -
                            (int32_t)((digits + 1u) * theme.mono_cell_width),
                        frame->y + (int32_t)(row * line_height) +
                            (int32_t)(line_height -
                                      theme.mono_font_height) / 2,
                        number, digits, theme.mono_font_height,
                        theme.mono_cell_width,
                        color(theme.text_tertiary), 0u))
                        return ASTRA_ERROR_NO_RESOURCES;
                } else {
                    ++*commands;
                    *payload += digits;
                }
            }
        }
        offset = visual.start;
        while (offset < visual.end && column < last_column) {
            uint32_t before = offset, scalar, width;
            AstraTextViewStyle style = ASTRA_TEXT_VIEW_STYLE_INIT;
            if (!text_view_step(control, model, visual.end, &offset, column,
                                &scalar, &width)) return ASTRA_ERROR_INVALID_ARGUMENT;
            if (control->_private_text_style_at != NULL) {
                AstraResult result = control->_private_text_style_at(
                    control->_private_text_style_context, before, &style);
                if (result != ASTRA_OK || style.reserved != 0u ||
                    (style.attributes & ~ASTRA_TEXT_STYLE_ALLOWED_MASK) != 0u)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
            }
            uint32_t end = column + width;
            int selected = before >= selection_start && before < selection_end;
            if (info.mode == ASTRA_TEXT_SURFACE_FLOW) {
                if (width != 0u && end > first_column &&
                    column < last_column) {
                    char encoded[4];
                    uint32_t encoded_bytes = astra_utf8_encode(scalar, encoded);
                    uint32_t foreground = selected && inverted ?
                        color(theme.accent_text) : style.foreground;
                    uint32_t background = selected ?
                        color(inverted ? theme.accent : theme.control_selected) :
                        style.background;
                    const char *source;
                    uint32_t source_bytes;
                    int tab = scalar == ' ' &&
                        astra_text_model_read(model, before, &source,
                                              &source_bytes) == ASTRA_OK &&
                        source_bytes != 0u && source[0] == '\t';
                    if (flow.bytes != 0u &&
                        (flow.foreground != foreground ||
                         flow.background != background ||
                         flow.attributes != style.attributes || tab ||
                         flow.bytes + encoded_bytes > sizeof(scratch))) {
                        AstraResult result = text_view_flow_flush(&text,
                            surface, (int32_t)content_x,
                            frame->y + (int32_t)(row * line_height),
                            scratch, &flow, commands, payload);
                        if (result != ASTRA_OK) return result;
                    }
                    if (flow.bytes == 0u) {
                        flow.column = column;
                        flow.foreground = foreground;
                        flow.background = background;
                        flow.attributes = style.attributes;
                    }
                    memcpy(scratch + flow.bytes, encoded, encoded_bytes);
                    flow.bytes += encoded_bytes;
                    flow.advance += width;
                    if (tab) {
                        AstraResult result = text_view_flow_flush(&text,
                            surface, (int32_t)content_x,
                            frame->y + (int32_t)(row * line_height),
                            scratch, &flow, commands, payload);
                        if (result != ASTRA_OK) return result;
                    }
                }
                column = end;
                continue;
            }
            while (column < end && column < last_column) {
                if (column++ < first_column) continue;
                cells[count++] = (AstraTextCell){scalar,
                    selected && inverted ? color(theme.accent_text) :
                        style.foreground,
                    selected ? color(inverted ? theme.accent :
                                               theme.control_selected) :
                        style.background, style.attributes, 1u, 0u};
                if (count == TEXT_VIEW_CELL_BATCH || column == last_column ||
                    (column == end && offset == visual.end)) {
                    if (surface != NULL) {
                        AstraResult result = astra_text_surface_render_cells(
                            &text, surface, (int32_t)content_x, frame->y,
                            row, run_column,
                            cells, count);
                        if (result != ASTRA_OK) return result;
                    } else {
                        AstraResult result = astra_text_surface_cells_requirements(
                            &text, cells, count, commands, payload);
                        if (result != ASTRA_OK) return result;
                    }
                    run_column += count; count = 0u;
                }
            }
        }
        if (info.mode == ASTRA_TEXT_SURFACE_FLOW) {
            AstraResult result = text_view_flow_flush(&text, surface,
                (int32_t)content_x,
                frame->y + (int32_t)(row * line_height),
                scratch, &flow, commands, payload);
            if (result != ASTRA_OK) return result;
        }
        if (count != 0u) {
            if (surface != NULL) {
                AstraResult result = astra_text_surface_render_cells(&text,
                    surface, (int32_t)content_x, frame->y,
                    row, run_column, cells, count);
                if (result != ASTRA_OK) return result;
            } else {
                AstraResult result = astra_text_surface_cells_requirements(
                    &text, cells, count, commands, payload);
                if (result != ASTRA_OK) return result;
            }
        }
    }
    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u &&
        (effective_state(control) &
            (ASTRA_CONTROL_FOCUSED | ASTRA_CONTROL_DISABLED)) ==
            ASTRA_CONTROL_FOCUSED &&
        state.selection.anchor == state.selection.focus &&
        field_caret_visible_at(control->_private_animation_phase)) {
        uint32_t caret_row, caret_column;
        TextViewRow caret_visual;
        if (!text_view_visual_location(control, model, state.line_count,
                wrap_columns, state.selection.focus,
                (control->_private_dynamic_state & TEXT_VIEW_SOFT_END) != 0u,
                &caret_row, &caret_column, &caret_visual))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        int64_t caret_x = content_x +
            (int64_t)caret_column * unit_width;
        int64_t caret_y = (int64_t)frame->y +
            (int64_t)caret_row * line_height;
        if (caret_x < INT32_MIN || caret_x > INT32_MAX ||
            caret_y < INT32_MIN || caret_y > INT32_MAX)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (surface != NULL) {
            AstraResult result = astra_text_surface_draw_caret_at(&text,
                surface, (int32_t)caret_x, (int32_t)caret_y,
                theme.mono_cell_width, ASTRA_TEXT_CARET_STANDARD,
                color(theme.accent));
            if (result != ASTRA_OK) return result;
        } else ++*commands;
    }
    if ((effective_state(control) & CONTROL_FOCUS_VISIBLE) != 0u) {
        if (surface != NULL) {
            uint16_t focus = color(theme.control_focus);
            astra_surface_fill(surface, (int32_t)left, (int32_t)top,
                                (uint32_t)(right - left), 1u, focus);
            astra_surface_fill(surface, (int32_t)left, (int32_t)bottom - 1,
                                (uint32_t)(right - left), 1u, focus);
            astra_surface_fill(surface, (int32_t)left, (int32_t)top,
                                1u, (uint32_t)(bottom - top), focus);
            astra_surface_fill(surface, (int32_t)right - 1, (int32_t)top,
                                1u, (uint32_t)(bottom - top), focus);
        } else *commands += 4u;
    }
    return ASTRA_OK;
}

static AstraResult grid_view_render(const AstraControl *control,
    AstraSurfaceView *surface, uint64_t *commands, uint64_t *payload)
{
    const AstraGridViewModel *model = grid_view_model(control);
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraTextSurface text = ASTRA_TEXT_SURFACE_INIT;
    AstraTextSurfaceInfo info = ASTRA_TEXT_SURFACE_INFO_INIT;
    char scratch[TEXT_VIEW_CELL_BATCH * 4u];
    int64_t left = clip->x > frame->x ? clip->x : frame->x;
    int64_t top = clip->y > frame->y ? clip->y : frame->y;
    int64_t right = (int64_t)clip->x + clip->width;
    int64_t bottom = (int64_t)clip->y + clip->height;
    if (right > (int64_t)frame->x + frame->width)
        right = (int64_t)frame->x + frame->width;
    if (bottom > (int64_t)frame->y + frame->height)
        bottom = (int64_t)frame->y + frame->height;
    if (right <= left || bottom <= top) return ASTRA_OK;
    if (!grid_view_model_valid(model)) return ASTRA_ERROR_INVALID_ARGUMENT;
    info.font_height = theme.mono_font_height;
    info.cell_width = theme.mono_cell_width;
    info.line_height = theme.control_height;
    info.default_foreground = color(theme.client_text);
    info.default_background = color(theme.client);
    info.scratch = scratch;
    info.scratch_bytes = sizeof(scratch);
    if (astra_text_surface_init(&text, &info) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (surface != NULL)
        astra_surface_fill(surface, frame->x, frame->y, frame->width,
                           frame->height, info.default_background);
    else ++*commands;
    uint32_t first_row = (uint32_t)((top - frame->y) / theme.control_height);
    uint32_t last_row = (uint32_t)((bottom - frame->y +
        theme.control_height - 1u) / theme.control_height);
    uint32_t first_column = (uint32_t)((left - frame->x) /
        theme.mono_cell_width);
    uint32_t last_column = (uint32_t)((right - frame->x +
        theme.mono_cell_width - 1u) / theme.mono_cell_width);
    if (last_row > model->rows) last_row = model->rows;
    if (last_column > model->columns) last_column = model->columns;
    for (uint32_t row = first_row; row < last_row; ++row)
        for (uint32_t column = first_column; column < last_column;) {
            uint32_t count = last_column - column;
            if (count > TEXT_VIEW_CELL_BATCH) count = TEXT_VIEW_CELL_BATCH;
            const AstraTextCell *cells = model->cells +
                row * model->columns + column;
            AstraResult result = surface != NULL ?
                astra_text_surface_render_cells(&text, surface,
                    frame->x, frame->y, row, column, cells, count) :
                astra_text_surface_cells_requirements(&text, cells, count,
                    commands, payload);
            if (result != ASTRA_OK) return result;
            column += count;
        }
    return ASTRA_OK;
}

static int field_view(const AstraControl *control, FieldView *view)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraTextModel *model = const_control_text_model(control);
    uint32_t inner_width;
    uint32_t scroll = private_u32(&control->_private_value);
    uint32_t distance = 0u;
    uint32_t selection_start;
    uint32_t selection_end;
    const char *ignored_text;
    uint32_t ignored_bytes;

    *view = (FieldView){.state = ASTRA_TEXT_MODEL_STATE_INIT};
    if (astra_text_model_get_state(model, &view->state) != ASTRA_OK)
        return 0;
    uint32_t inset = field_inset(control->_private_style);
    inner_width = control->_private_frame.width > inset * 2u ?
        control->_private_frame.width - inset * 2u : 0u;
    view->columns = inner_width / theme.mono_cell_width;
    if (scroll > view->state.text_bytes ||
        astra_text_model_read(model, scroll, &ignored_text,
                              &ignored_bytes) != ASTRA_OK)
        scroll = 0u;
    if (view->state.selection.focus < scroll ||
        !model_scalar_count(model, scroll, view->state.selection.focus,
                            &distance) ||
        (view->columns != 0u && distance >= view->columns)) {
        uint32_t remaining = view->columns == 0u ? 0u : view->columns - 1u;

        scroll = view->state.selection.focus;
        while (remaining-- != 0u &&
               astra_text_model_scalar_retreat(model, &scroll) == ASTRA_OK)
            ;
    }
    view->start = scroll;
    if (!model_advance_scalars(model, view->start, view->columns, &view->end) ||
        !model_scalar_count(model, view->start,
                            view->state.selection.focus,
                            &view->caret_column))
        return 0;
    selection_start = view->state.selection.anchor <
                              view->state.selection.focus ?
        view->state.selection.anchor : view->state.selection.focus;
    selection_end = view->state.selection.anchor >
                            view->state.selection.focus ?
        view->state.selection.anchor : view->state.selection.focus;
    if (selection_start < view->start) selection_start = view->start;
    if (selection_end > view->end) selection_end = view->end;
    if (selection_start < selection_end &&
        (!model_scalar_count(model, view->start, selection_start,
                             &view->selection_first) ||
         !model_scalar_count(model, view->start, selection_end,
                             &view->selection_last)))
        return 0;
    return 1;
}

static int field_caret_visible_at(uint32_t phase)
{
    return phase < 30u || ((phase / 30u) & 1u) == 0u;
}

static int field_caret_visible(const AstraUIContext *context,
                               const AstraControl *control)
{
    (void)context;
    return field_caret_visible_at(control->_private_animation_phase);
}

static int field_render_requirements(const AstraUIContext *context,
                                     const AstraControl *control,
                                     uint64_t *commands, uint64_t *payload)
{
    FieldView view;
    uint32_t offset;
    uint32_t state = effective_state(control);

    if (!field_view(control, &view))
        return 0;
    *commands += 2u;
    if ((control->_private_style & ASTRA_FIELD_SEARCH) != 0u)
        *commands += view.state.text_bytes == 0u ? 5u : 7u;
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) ++*commands;
    if (view.selection_first < view.selection_last) ++*commands;
    if ((state & (ASTRA_CONTROL_FOCUSED | ASTRA_CONTROL_DISABLED)) ==
            ASTRA_CONTROL_FOCUSED && field_caret_visible(context, control))
        ++*commands;
    offset = view.start;
    while (offset < view.end) {
        const char *text;
        uint32_t bytes;
        uint32_t take;

        if (astra_text_model_read(const_control_text_model(control), offset,
                                  &text, &bytes) != ASTRA_OK || bytes == 0u)
            return 0;
        take = bytes < view.end - offset ? bytes : view.end - offset;
        if ((control->_private_style & ASTRA_FIELD_SECURE) != 0u) {
            uint32_t scalars;

            if (!model_scalar_count(const_control_text_model(control),
                                    offset, offset + take, &scalars))
                return 0;
            *commands += (scalars + FIELD_MASK_BATCH - 1u) /
                         FIELD_MASK_BATCH;
            *payload += scalars;
        } else {
            ++*commands;
            *payload += take;
        }
        offset += take;
    }
    return 1;
}

static void list_visible_range(const AstraControl *control,
                               uint32_t row_height, uint32_t header_height,
                               uint32_t *first,
                               uint32_t *end)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;
    int64_t origin = (int64_t)frame->y + header_height;
    int64_t top = origin > clip->y ? origin : clip->y;
    int64_t bottom = (int64_t)frame->y + frame->height;
    int64_t clip_bottom = (int64_t)clip->y + clip->height;
    uint64_t after;

    *first = *end = 0u;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (bottom <= top || row_height == 0u) return;
    *first = (uint32_t)((top - origin) / row_height);
    after = (uint64_t)(bottom - origin + row_height - 1u) / row_height;
    if (*first > control->_private_text_length)
        *first = control->_private_text_length;
    *end = after > control->_private_text_length ?
        control->_private_text_length : (uint32_t)after;
}

static void icon_grid_visible_rows(const AstraControl *control,
    uint32_t tile_height, uint32_t columns, uint32_t *first,
    uint32_t *end)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;
    int64_t top = frame->y > clip->y ? frame->y : clip->y;
    int64_t bottom = (int64_t)frame->y + frame->height;
    int64_t clip_bottom = (int64_t)clip->y + clip->height;
    uint32_t rows = control->_private_text_length == 0u ? 0u :
        1u + (control->_private_text_length - 1u) / columns;

    *first = *end = 0u;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (bottom <= top) return;
    *first = (uint32_t)((top - frame->y) / tile_height);
    *end = (uint32_t)((bottom - frame->y + tile_height - 1u) /
                      tile_height);
    if (*first > rows) *first = rows;
    if (*end > rows) *end = rows;
}

static int render_requirements(const AstraUIContext *context,
                               uint32_t first, uint32_t end,
                               uint64_t *commands, uint64_t *payload)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    char font_id[ASTRA_FONT_ID_MAX];
    uint32_t id_length;

    *commands = 0u;
    *payload = 0u;
    if (astra_fonts_context_id(font_id) != ASTRA_OK) return 0;
    id_length = (uint32_t)strlen(font_id);
    for (uint32_t at = first; at < end; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        uint32_t state;

        if (!control_valid(control))
            return 0;
        if (control->_private_laid_out == 0u)
            continue;
        if (CONTAINER_KINDS(control->_private_control_kind)) {
            if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
                control->_private_style == ASTRA_CONTAINER_STYLE_TOOLBAR)
                *commands += 2u;
            else if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
                     control->_private_style == ASTRA_CONTAINER_STYLE_STATUS_BAR)
                *commands += 2u;
            else if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
                     control->_private_style == ASTRA_CONTAINER_STYLE_INSPECTOR)
                *commands += 2u;
            else if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
                     control->_private_style == ASTRA_CONTAINER_STYLE_TAG_INPUT)
                *commands += 2u;
            else if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
                     control->_private_style == ASTRA_CONTAINER_STYLE_DROP_TARGET) {
                *commands += 1u;
                uint32_t lines = drop_target_border(&control->_private_frame,
                                                     NULL);
                if (lines != 0u) {
                    ++*commands;
                    *payload += 3u + (uint64_t)lines *
                        sizeof(AstraDrawListSegment);
                }
            }
            continue;
        }
        if (control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        state = effective_state(control);
        if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW) {
            if (text_view_render(control, NULL, commands, payload) != ASTRA_OK)
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_GRID_VIEW) {
            if (grid_view_render(control, NULL, commands, payload) != ASTRA_OK)
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_VIEW) {
            if (view_model(control)->draw(view_model(control)->context,
                &control->_private_frame, NULL, commands, payload) != ASTRA_OK)
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER ||
            control->_private_control_kind == ASTRA_CONTROL_SPARKLINE) {
            if (astra_interface_display_render(control, &theme, NULL,
                                               commands) != ASTRA_OK)
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
            if (!field_render_requirements(context, control,
                                           commands, payload))
                return 0;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_LIST ||
            control->_private_control_kind == ASTRA_CONTROL_TREE) {
            const AstraCollectionItem *items =
                const_collection_model(control)->items;
            const AstraTreeModel *tree = control->_private_control_kind ==
                ASTRA_CONTROL_TREE ? const_tree_model(control) : NULL;
            uint32_t first;
            uint32_t end;

            list_visible_range(control, theme.control_height, 0u, &first, &end);
            *commands += 5u + (uint64_t)(end - first) *
                (tree == NULL ? 2u : 5u);
            for (uint32_t row = first; row < end; ++row) {
                uint32_t item = tree == NULL ? row : tree->visible_rows[row];
                *payload += (uint64_t)items[item].text_length + id_length;
                if (tree == NULL &&
                    items[item].presentation.icon != ASTRA_TOOLBAR_ICON_NONE) {
                    ++*commands;
                    *payload += 16u * sizeof(AstraDrawListSegment);
                }
            }
            continue;
        }
        if (TABLE_KINDS(control->_private_control_kind)) {
            const AstraTableModel *table = table_model(control);
            const AstraOutlineTableModel *outline =
                control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE ?
                    const_outline_table_model(control) : NULL;
            const AstraCollectionItem *items =
                const_collection_model(control)->items;
            uint32_t first, end;

            list_visible_range(control, theme.control_height,
                               theme.control_height, &first, &end);
            *commands += 7u + table->column_count +
                (uint64_t)(end - first) * (1u + table->column_count);
            if (outline != NULL)
                for (uint32_t row = first; row < end; ++row)
                    if (outline->rows[row].expandable != 0u)
                        *commands += 3u;
            for (uint32_t col = 0u; col < table->column_count; ++col)
                *payload += table->columns[col].heading_length + id_length;
            for (uint32_t item = first; item < end; ++item) {
                *payload += (uint64_t)items[item].text_length + id_length;
                for (uint32_t col = 1u; col < table->column_count; ++col) {
                    const AstraTableCell *cell = &table->cells[
                        item * (table->column_count - 1u) + col - 1u];
                    if (cell->text_length != 0u)
                        *payload += (uint64_t)cell->text_length + id_length;
                }
            }
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
            const AstraIconGridModel *grid = const_icon_grid_model(control);
            const AstraCollectionItem *items = grid->collection->items;
            uint32_t tile_width = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
                ASTRA_ICON_TILE_CLIENT_WIDTH : ASTRA_ICON_TILE_DESKTOP_WIDTH;
            uint32_t tile_height = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
                ASTRA_ICON_TILE_CLIENT_HEIGHT : ASTRA_ICON_TILE_DESKTOP_HEIGHT;
            uint32_t first, end;

            icon_grid_visible_rows(control, tile_height, grid->columns,
                                   &first, &end);
            *commands += 5u;
            for (uint32_t row = first; row < end; ++row)
                for (uint32_t col = 0u; col < grid->columns; ++col) {
                    uint64_t item = (uint64_t)row * grid->columns + col;
                    int64_t x = (int64_t)control->_private_frame.x +
                                (uint64_t)col * tile_width;

                    if (item >= grid->icon_count ||
                        x >= (int64_t)control->_private_clip.x +
                                 control->_private_clip.width ||
                        x + tile_width <= control->_private_clip.x)
                        continue;
                    *commands += 3u;
                    *payload += (uint64_t)items[item].text_length + id_length;
                }
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR) {
            AstraScrollState scroll = ASTRA_SCROLL_STATE_INIT;

            if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
                (state & (ASTRA_CONTROL_HOVERED |
                          ASTRA_CONTROL_PRESSED)) == 0u &&
                control->_private_animation_phase >= SCROLLBAR_FADE_FRAMES)
                continue;

            if (astra_scroll_get_state(
                    const_scroll_model(control), &scroll) != ASTRA_OK)
                return 0;
            *commands += 2u + scroll.mark_count;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER) {
            ++*commands;
            continue;
        }
        if (CHOICE_KINDS(control->_private_control_kind)) {
            const AstraChoiceItem *items = choice_items(control);

            if (control->_private_control_kind == ASTRA_CONTROL_POPUP) {
                *commands += 7u;
                *payload += items[control->_private_value].text_length +
                            id_length;
                continue;
            }

            if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED) {
                *commands += 2u +
                    (uint64_t)control->_private_text_length * 2u;
                if (control->_private_minimum_value >= 0 &&
                    control->_private_minimum_value != control->_private_value)
                    ++*commands;
                if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
                    ++*commands;
            } else {
                *commands += 2u + control->_private_text_length;
                if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
                    *commands += 2u;
                if (control->_private_control_kind == ASTRA_CONTROL_TAB &&
                    (state & (ASTRA_CONTROL_DISABLED |
                              ASTRA_CONTROL_HOVERED)) ==
                        ASTRA_CONTROL_HOVERED &&
                    control->_private_minimum_value >= 0 &&
                    control->_private_minimum_value != control->_private_value)
                    ++*commands;
            }
            for (uint32_t item = 0u;
                 item < control->_private_text_length; ++item)
                *payload += (uint64_t)items[item].text_length + id_length;
            continue;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
            *commands += 1u;
        else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
            *commands += (control->_private_style == ASTRA_BUTTON_TAG ? 5u :
                (control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
                    ASTRA_BUTTON_SPLIT_MENU ? 4u : 2u) +
                (ASTRA_BUTTON_ICON(control->_private_style) != 0u ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL)
            *commands += 6u +
                ((state & CONTROL_FOCUS_VISIBLE) != 0u ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
            *commands += 7u;
        else if (control->_private_control_kind == ASTRA_CONTROL_CHECKBOX)
            *commands += 3u +
                (((state & ASTRA_CONTROL_SELECTED) != 0u) ? 5u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_RADIO)
            *commands += 3u +
                (((state & ASTRA_CONTROL_SELECTED) != 0u) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
            *commands += 3u +
                ((control->_private_value >
                  control->_private_minimum_value) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_DIAL)
            *commands += 3u +
                (((state & CONTROL_FOCUS_VISIBLE) != 0u) ? 1u : 0u);
        else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
            char text[22];

            *commands += 15u;
            if ((state & CONTROL_FOCUS_VISIBLE) != 0u) ++*commands;
            if (context->_private_hover == at &&
                context->_private_hover_part != STEPPER_PART_VALUE)
                ++*commands;
            *payload += stepper_format(context, at, text);
        }
        else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
            *commands += 2u;
        else
            *commands += 3u +
                ((control->_private_control_kind == ASTRA_CONTROL_SWITCH &&
                  ((state & ASTRA_CONTROL_SELECTED) == 0u ||
                   (state & ASTRA_CONTROL_DISABLED) != 0u)) ? 1u : 0u);
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            ASTRA_BUTTON_ICON(control->_private_style) != 0u)
            *payload += 3u + 16u * sizeof(AstraDrawListSegment);
        if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_BUTTON &&
            (state & ASTRA_CONTROL_ERROR) != 0u)
            ++*commands;
        if (TOGGLE_KINDS(control->_private_control_kind) &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind == ASTRA_CONTROL_SLIDER &&
            (state & CONTROL_FOCUS_VISIBLE) != 0u)
            ++*commands;
        if (control->_private_control_kind != ASTRA_CONTROL_STEPPER) {
            *payload += control->_private_text_length;
            if (control->_private_text_length != 0u)
                *payload += control->_private_control_kind == ASTRA_CONTROL_LABEL &&
                    control->_private_label_font.gui != ASTRA_INVALID_HANDLE ?
                    control->_private_label_font.id_length : id_length;
        }
    }
    return 1;
}

int astra_interface_draw_list_has_room(const AstraSurfaceView *surface,
                              uint64_t commands, uint64_t payload)
{
    AstraSurfaceView adopted;
    const AstraDrawListHeader *header =
        (const AstraDrawListHeader *)(const void *)surface->pixels;

    return astra_draw_list_view_adopt(
               &adopted, surface->pixels, surface->byte_size,
               surface->width, surface->height) &&
           commands <= ASTRA_DRAW_LIST_COMMAND_MAX - header->command_count &&
           payload <= ASTRA_DRAW_LIST_PAYLOAD_BYTES - header->payload_bytes;
}

static AstraResult render_label(const AstraTheme *theme,
                         const AstraControl *control,
                         AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraLabelFont *font = &control->_private_label_font;
    if (font->gui != ASTRA_INVALID_HANDLE) {
        AstraFontTextMetrics metrics = ASTRA_FONT_TEXT_METRICS_INIT;
        AstraResult result = astra_fonts_text_measure(font->gui, font->id,
            font->id_length, font->pixel_height, control->_private_text,
            control->_private_text_length, frame->width, &metrics);
        int32_t y = frame->y +
            ((int32_t)frame->height - (int32_t)metrics.height) / 2;
        if (result != ASTRA_OK) return result;
        return astra_surface_font_text(surface, font->id, font->id_length,
            frame->x, y, control->_private_text, metrics.fitted_bytes, font->pixel_height,
            color(label_color(theme, control))) ? ASTRA_OK : ASTRA_ERROR_NO_RESOURCES;
    }
    uint32_t length = astra_surface_ui_text_fit(
        control->_private_text, control->_private_text_length,
        theme->body_font_height, frame->width);
    int32_t y = frame->y + ((int32_t)frame->height -
        (int32_t)astra_surface_ui_text_height(theme->body_font_height)) / 2;

    astra_surface_ui_text(surface, frame->x, y, control->_private_text,
                          length, theme->body_font_height,
                          color(label_color(theme, control)));
    return ASTRA_OK;
}

typedef struct ToolbarLine { int8_t x0, y0, x1, y1; } ToolbarLine;

static void render_toolbar_icon(AstraSurfaceView *surface, uint32_t icon,
                                int32_t x, int32_t y, uint16_t ink)
{
    static const ToolbarLine back[] = {
        {7, 2, 2, 8}, {2, 8, 7, 14}, {2, 8, 14, 8}
    };
    static const ToolbarLine forward[] = {
        {8, 2, 13, 8}, {13, 8, 8, 14}, {2, 8, 13, 8}
    };
    static const ToolbarLine restart[] = {
        {4, 4, 7, 2}, {7, 2, 12, 3}, {12, 3, 14, 7},
        {14, 7, 13, 11}, {13, 11, 10, 14}, {10, 14, 5, 13},
        {5, 13, 2, 10}, {2, 10, 2, 6}, {2, 6, 4, 4},
        {2, 6, 2, 2}, {2, 6, 6, 6}
    };
    static const ToolbarLine undo[] = {
        {5, 3, 2, 6}, {2, 6, 5, 9}, {2, 6, 9, 6},
        {9, 6, 13, 8}, {13, 8, 14, 11}, {14, 11, 11, 14}
    };
    static const ToolbarLine close_icon[] = {
        {3, 3, 13, 13}, {13, 3, 3, 13}
    };
    static const ToolbarLine open[] = {
        {1, 5, 1, 13}, {1, 13, 14, 13}, {14, 13, 14, 5},
        {14, 5, 7, 5}, {7, 5, 5, 3}, {5, 3, 1, 3}
    };
    static const ToolbarLine new_folder[] = {
        {1, 5, 1, 13}, {1, 13, 14, 13}, {14, 13, 14, 5},
        {14, 5, 7, 5}, {7, 5, 5, 3}, {5, 3, 1, 3},
        {8, 7, 8, 11}, {6, 9, 10, 9}
    };
    static const ToolbarLine home[] = {
        {8, 2, 3, 7}, {8, 2, 13, 7}, {3, 7, 3, 14},
        {3, 14, 13, 14}, {13, 14, 13, 7}, {6, 14, 6, 9},
        {6, 9, 10, 9}, {10, 9, 10, 14}
    };
    static const ToolbarLine recent[] = {
        {2, 3, 14, 3}, {14, 3, 14, 13}, {14, 13, 2, 13},
        {2, 13, 2, 3}, {5, 6, 11, 6}, {5, 8, 11, 8},
        {5, 10, 9, 10}
    };
    static const ToolbarLine volume[] = {
        {2, 4, 14, 4}, {14, 4, 14, 12}, {14, 12, 2, 12},
        {2, 12, 2, 4}, {4, 7, 12, 7}, {4, 9, 12, 9}
    };
    static const ToolbarLine ram[] = {
        {3, 4, 13, 4}, {13, 4, 13, 11}, {13, 11, 3, 11},
        {3, 11, 3, 4}, {5, 11, 5, 14}, {11, 11, 11, 14}
    };
    static const ToolbarLine trash[] = {
        {2, 4, 14, 4}, {5, 2, 11, 2}, {4, 5, 5, 14},
        {5, 14, 11, 14}, {11, 14, 12, 5}, {7, 7, 7, 12},
        {9, 7, 9, 12}
    };
    static const ToolbarLine grid[] = {
        {2, 2, 6, 2}, {6, 2, 6, 6}, {6, 6, 2, 6}, {2, 6, 2, 2},
        {10, 2, 14, 2}, {14, 2, 14, 6}, {14, 6, 10, 6}, {10, 6, 10, 2},
        {2, 10, 6, 10}, {6, 10, 6, 14}, {6, 14, 2, 14}, {2, 14, 2, 10},
        {10, 10, 14, 10}, {14, 10, 14, 14}, {14, 14, 10, 14},
        {10, 14, 10, 10}
    };
    static const ToolbarLine list[] = {
        {2, 3, 4, 3}, {6, 3, 14, 3}, {2, 8, 4, 8},
        {6, 8, 14, 8}, {2, 13, 4, 13}, {6, 13, 14, 13}
    };
    static const ToolbarLine columns[] = {
        {2, 2, 2, 14}, {2, 2, 14, 2}, {14, 2, 14, 14},
        {2, 14, 14, 14}, {6, 2, 6, 14}, {10, 2, 10, 14}
    };
    static const ToolbarLine search[] = {
        {6, 2, 10, 2}, {10, 2, 13, 5}, {13, 5, 13, 9},
        {13, 9, 10, 12}, {10, 12, 6, 12}, {6, 12, 3, 9},
        {3, 9, 3, 5}, {3, 5, 6, 2}, {11, 11, 15, 15}
    };
    static const ToolbarLine more[] = {
        {2, 4, 6, 8}, {6, 8, 2, 12},
        {8, 4, 12, 8}, {12, 8, 8, 12}
    };
    const ToolbarLine *lines = NULL;
    AstraSurfaceLine segments[16];
    uint32_t count = 0u;
#define PICK(name) do { \
    _Static_assert(sizeof(name) / sizeof(name[0]) <= 16u, \
                   "toolbar icon segment capacity"); \
    lines = name; count = sizeof(name) / sizeof(name[0]); \
} while (0)
    switch (icon) {
    case ASTRA_TOOLBAR_ICON_BACK: PICK(back); break;
    case ASTRA_TOOLBAR_ICON_FORWARD: PICK(forward); break;
    case ASTRA_TOOLBAR_ICON_RESTART: PICK(restart); break;
    case ASTRA_TOOLBAR_ICON_UNDO: PICK(undo); break;
    case ASTRA_TOOLBAR_ICON_CLOSE: PICK(close_icon); break;
    case ASTRA_TOOLBAR_ICON_OPEN: PICK(open); break;
    case ASTRA_TOOLBAR_ICON_TRASH: PICK(trash); break;
    case ASTRA_TOOLBAR_ICON_GRID: PICK(grid); break;
    case ASTRA_TOOLBAR_ICON_LIST: PICK(list); break;
    case ASTRA_TOOLBAR_ICON_COLUMNS: PICK(columns); break;
    case ASTRA_TOOLBAR_ICON_SEARCH: PICK(search); break;
    case ASTRA_TOOLBAR_ICON_NEW_FOLDER: PICK(new_folder); break;
    case ASTRA_TOOLBAR_ICON_HOME: PICK(home); break;
    case ASTRA_TOOLBAR_ICON_RECENT: PICK(recent); break;
    case ASTRA_TOOLBAR_ICON_VOLUME: PICK(volume); break;
    case ASTRA_TOOLBAR_ICON_RAM: PICK(ram); break;
    case ASTRA_TOOLBAR_ICON_MORE: PICK(more); break;
    default: break;
    }
#undef PICK
    for (uint32_t at = 0u; at < count; ++at)
        segments[at] = (AstraSurfaceLine){
            x + lines[at].x0, y + lines[at].y0,
            x + lines[at].x1, y + lines[at].y1};
    if (count != 0u)
        (void)astra_surface_lines(surface, segments, count, ink);
}

static void render_button(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    int32_t x = frame->x;
    int32_t y = frame->y;
    uint32_t width = frame->width;
    uint32_t height = frame->height;
    uint32_t inset = 0u;
    uint32_t text_width;
    uint32_t icon = ASTRA_BUTTON_ICON(control->_private_style);
    int32_t text_x;
    int32_t text_y;

    if (control->_private_style == ASTRA_BUTTON_TAG) {
        AstraColorRGBA8 ink = (state & ASTRA_CONTROL_DISABLED) != 0u ?
            theme->text_tertiary : theme->client_text;
        AstraColorRGBA8 pill = blend(theme->surface_raised,
            theme->accent, 40u);
        int32_t cross_x = x + (int32_t)width - 12;
        int32_t cross_y = y + (int32_t)height / 2;

        if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
            astra_surface_fill_round(surface,
                x - theme->focus_width, y - theme->focus_width,
                width + (uint32_t)theme->focus_width * 2u,
                height + (uint32_t)theme->focus_width * 2u,
                height / 2u + theme->focus_width,
                color(theme->control_focus));
        astra_surface_fill_round(surface, x, y, width, height,
            height / 2u, color(theme->accent));
        astra_surface_fill_round(surface, x + 1, y + 1,
            width - 2u, height - 2u, height / 2u - 1u, color(pill));
        astra_surface_ui_text(surface, x + 8,
            y + (int32_t)(height - theme->control_font_height) / 2,
            control->_private_text, control->_private_text_length,
            theme->control_font_height, color(ink));
        (void)astra_surface_line(surface, cross_x - 2, cross_y - 2,
            cross_x + 2, cross_y + 2, color(ink));
        (void)astra_surface_line(surface, cross_x + 2, cross_y - 2,
            cross_x - 2, cross_y + 2, color(ink));
        return;
    }

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, x - theme->focus_width, y - theme->focus_width,
            width + (uint32_t)theme->focus_width * 2u,
            height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    if ((state & ASTRA_CONTROL_ERROR) != 0u) {
        astra_surface_fill_round(surface, x, y, width, height,
                                 theme->control_radius,
                                 color(theme->control_error));
        inset = theme->control_border_width;
    }
    astra_surface_fill_round(
        surface, x + (int32_t)inset, y + (int32_t)inset,
        width - inset * 2u, height - inset * 2u,
        theme->control_radius > inset ?
            (uint16_t)(theme->control_radius - inset) : 0u,
        color(button_fill(theme, control)));
    text_width = astra_surface_ui_text_width(
        control->_private_text, control->_private_text_length,
        theme->control_font_height);
    if (icon != 0u) {
        render_toolbar_icon(surface, icon,
                            x + ((control->_private_style &
                                  ASTRA_BUTTON_ICON_ONLY) != 0u ?
                                ((int32_t)width - 16) / 2 : 8),
                            y + ((int32_t)height - 16) / 2,
                            color(button_text(theme, control)));
        text_x = x + 30;
    } else
        text_x = x + (int32_t)(width - text_width) / 2;
    text_y = y + (int32_t)(height - theme->control_font_height) / 2;
    if ((control->_private_style & ASTRA_BUTTON_STYLE_MASK) ==
        ASTRA_BUTTON_SPLIT_MENU) {
        int32_t arrow_x = x + (int32_t)width / 2 - 4;
        int32_t arrow_y = y + (int32_t)height / 2 - 2;
        uint16_t ink = color(button_text(theme, control));

        astra_surface_fill(surface, arrow_x, arrow_y, 9u, 1u, ink);
        astra_surface_fill(surface, arrow_x + 1, arrow_y + 1, 7u, 1u, ink);
        astra_surface_fill(surface, arrow_x + 3, arrow_y + 2, 3u, 1u, ink);
        return;
    }
    if ((control->_private_style & ASTRA_BUTTON_ICON_ONLY) != 0u) return;
    astra_surface_ui_text(surface, text_x, text_y, control->_private_text,
                          control->_private_text_length,
                          theme->control_font_height,
                          color(button_text(theme, control)));
}

static void render_colour_well(const AstraTheme *theme,
    const AstraControl *control, AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint16_t swatch = (uint16_t)control->_private_value;
    uint16_t ink = color((state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->client_text);
    int32_t cy = frame->y + (int32_t)frame->height / 2;
    int32_t right = frame->x + (int32_t)frame->width;

    if (control->_private_style == ASTRA_COLOUR_THEME_ACCENT)
        swatch = color(theme->accent);
    else if (control->_private_style == ASTRA_COLOUR_THEME_WARNING)
        swatch = color(theme->warning);
    else if (control->_private_style == ASTRA_COLOUR_THEME_FAULT)
        swatch = color(theme->fault);
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(surface,
            frame->x - theme->focus_width, frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            theme->control_radius + theme->focus_width,
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
        frame->width, frame->height, theme->control_radius,
        color((state & ASTRA_CONTROL_ERROR) != 0u ?
            theme->fault : theme->client_border));
    if (frame->width > 2u && frame->height > 2u)
        astra_surface_fill_round(surface, frame->x + 1, frame->y + 1,
            frame->width - 2u, frame->height - 2u,
            theme->control_radius - 1u,
            color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->control_disabled : theme->surface_raised));
    if (frame->height > 8u)
        astra_surface_fill_round(surface, frame->x + 4, frame->y + 4,
            22u, frame->height - 8u, 5u, swatch);
    astra_surface_ui_text(surface, frame->x + 34,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        control->_private_text, control->_private_text_length,
        theme->control_font_height, ink);
    (void)astra_surface_line(surface, right - 14, cy - 2,
        right - 10, cy + 2, ink);
    (void)astra_surface_line(surface, right - 10, cy + 2,
        right - 6, cy - 2, ink);
}

static void render_toggle(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t indicator_width = control->_private_control_kind ==
        ASTRA_CONTROL_SWITCH ? 34u : 16u;
    uint32_t indicator_height = control->_private_control_kind ==
        ASTRA_CONTROL_SWITCH ? 18u : 16u;
    int32_t x = frame->x;
    int32_t y = frame->y + (int32_t)(frame->height - indicator_height) / 2;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    AstraColorRGBA8 fill = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->client;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, x - theme->focus_width, y - theme->focus_width,
            indicator_width + (uint32_t)theme->focus_width * 2u,
            indicator_height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(indicator_height / 2u + theme->focus_width),
            color(theme->control_focus));
    if (control->_private_control_kind == ASTRA_CONTROL_SWITCH) {
        int disabled = (state & ASTRA_CONTROL_DISABLED) != 0u;
        int selected = (state & ASTRA_CONTROL_SELECTED) != 0u;
        AstraColorRGBA8 track = disabled ? theme->control_disabled :
            (selected ? theme->accent : theme->surface_inset);
        int32_t thumb_x = x + ((state & ASTRA_CONTROL_SELECTED) != 0u ?
                               18 : 2);

        astra_surface_fill_round(surface, x, y, indicator_width,
                                 indicator_height, indicator_height / 2u,
                                 color(track));
        if (!selected || disabled) {
            astra_surface_fill_round(
                surface, thumb_x, y + 2, 14u, 14u, 7u,
                color(disabled ? theme->text_tertiary :
                                 theme->client_border));
            astra_surface_fill_round(surface, thumb_x + 2, y + 4,
                                     10u, 10u, 5u, color(track));
        } else {
            astra_surface_fill_round(surface, thumb_x, y + 2,
                                     14u, 14u, 7u, color(theme->client));
        }
    } else {
        uint16_t radius = control->_private_control_kind ==
            ASTRA_CONTROL_RADIO ? 8u : 4u;
        int selected = (state & ASTRA_CONTROL_SELECTED) != 0u;
        int disabled = (state & ASTRA_CONTROL_DISABLED) != 0u;

        if (selected)
            border = disabled ?
                theme->text_tertiary : theme->accent;
        astra_surface_fill_round(surface, x, y, 16u, 16u, radius,
                                 color(border));
        if (control->_private_control_kind == ASTRA_CONTROL_RADIO) {
            astra_surface_fill_round(surface, x + 1, y + 1, 14u, 14u, 7u,
                                     color(fill));
            if (selected)
                astra_surface_fill_round(surface, x + 4, y + 4, 8u, 8u, 4u,
                                         color(border));
        } else if (selected) {
            uint16_t mark = color(disabled ? theme->client :
                                            theme->accent_text);
            (void)astra_surface_line(surface, x + 3, y + 8,
                                     x + 6, y + 11, mark);
            (void)astra_surface_line(surface, x + 6, y + 11,
                                     x + 12, y + 4, mark);
        } else {
            astra_surface_fill_round(surface, x + 1, y + 1, 14u, 14u, 3u,
                                     color(fill));
        }
    }
    astra_surface_ui_text(
        surface, x + (int32_t)indicator_width + theme->spacing_unit * 2,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        control->_private_text, control->_private_text_length,
        theme->control_font_height, color(toggle_text(theme, control)));
}

static AstraColorRGBA8 blend(AstraColorRGBA8 background,
                             AstraColorRGBA8 foreground, uint32_t alpha)
{
    uint32_t inverse = 255u - alpha;

    return (AstraColorRGBA8){
        (uint8_t)(((uint32_t)background.red * inverse +
                   (uint32_t)foreground.red * alpha + 127u) / 255u),
        (uint8_t)(((uint32_t)background.green * inverse +
                   (uint32_t)foreground.green * alpha + 127u) / 255u),
        (uint8_t)(((uint32_t)background.blue * inverse +
                   (uint32_t)foreground.blue * alpha + 127u) / 255u),
        255u};
}

static AstraResult render_field(const AstraUIContext *context,
                                const AstraTheme *theme,
                                const AstraControl *control,
                                AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    int inline_field = (control->_private_style & ASTRA_FIELD_INLINE) != 0u;
    uint32_t inset = field_inset(control->_private_style);
    uint32_t border_width = inline_field ? 0u : theme->control_border_width;
    AstraColorRGBA8 background =
        inline_field ? theme->client :
        (state & ASTRA_CONTROL_DISABLED) != 0u ?
            theme->control_disabled :
            theme->surface_raised;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error :
        ((state & ASTRA_CONTROL_FOCUSED) != 0u ?
             theme->control_focus : theme->client_border);
    FieldView view;
    AstraSurfaceView clipped = *surface;
    int32_t text_x = frame->x + (int32_t)inset;
    int32_t text_y = frame->y +
        (int32_t)(frame->height - theme->mono_font_height) / 2;
    uint32_t offset;
    uint32_t column = 0u;

    if (!field_view(control, &view))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!inline_field && (state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    if (!inline_field) astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(border));
    if (!inline_field) astra_surface_fill_round(
        surface, frame->x + theme->control_border_width,
        frame->y + theme->control_border_width,
        frame->width - (uint32_t)theme->control_border_width * 2u,
        frame->height - (uint32_t)theme->control_border_width * 2u,
        theme->control_radius > theme->control_border_width ?
            (uint16_t)(theme->control_radius -
                       theme->control_border_width) : 0u,
        color(background));
    if ((control->_private_style & ASTRA_FIELD_SEARCH) != 0u) {
        int32_t cx = frame->x + 14;
        int32_t cy = frame->y + (int32_t)frame->height / 2;
        uint16_t ink = color(theme->text_tertiary);

        (void)astra_surface_line(surface, cx - 3, cy - 4, cx + 1, cy - 4, ink);
        (void)astra_surface_line(surface, cx - 4, cy - 3, cx - 4, cy + 1, ink);
        (void)astra_surface_line(surface, cx + 2, cy - 3, cx + 2, cy + 1, ink);
        (void)astra_surface_line(surface, cx - 3, cy + 2, cx + 1, cy + 2, ink);
        (void)astra_surface_line(surface, cx + 2, cy + 2, cx + 5, cy + 5, ink);
        if (view.state.text_bytes != 0u) {
            int32_t clear_x = frame->x + (int32_t)frame->width - 16;

            (void)astra_surface_line(surface, clear_x - 3, cy - 3,
                                     clear_x + 3, cy + 3, ink);
            (void)astra_surface_line(surface, clear_x + 3, cy - 3,
                                     clear_x - 3, cy + 3, ink);
        }
    }
    if (!astra_surface_clip(
            &clipped, text_x, frame->y + (int32_t)border_width,
            frame->width > inset * 2u ?
                frame->width - inset * 2u : 0u,
            frame->height - border_width * 2u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (view.selection_first < view.selection_last) {
        AstraColorRGBA8 selection = blend(background, theme->accent, 71u);

        astra_surface_fill(
            &clipped,
            text_x + (int32_t)(view.selection_first *
                               theme->mono_cell_width),
            text_y,
            (view.selection_last - view.selection_first) *
                theme->mono_cell_width,
            theme->mono_font_height, color(selection));
    }
    offset = view.start;
    while (offset < view.end) {
        const char *text;
        uint32_t bytes;
        uint32_t take;
        uint32_t scalars;

        if (astra_text_model_read(const_control_text_model(control), offset,
                                  &text, &bytes) != ASTRA_OK || bytes == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        take = bytes < view.end - offset ? bytes : view.end - offset;
        if (!model_scalar_count(const_control_text_model(control), offset,
                                offset + take, &scalars))
            return ASTRA_ERROR_IO;
        if ((control->_private_style & ASTRA_FIELD_SECURE) != 0u) {
            char mask[FIELD_MASK_BATCH];
            uint32_t remaining = scalars;
            uint32_t shown = 0u;

            memset(mask, '*', sizeof(mask));
            while (remaining != 0u) {
                uint32_t count = remaining > FIELD_MASK_BATCH ?
                    FIELD_MASK_BATCH : remaining;

                if (!astra_surface_mono_text_styled(&clipped,
                    text_x + (int32_t)((column + shown) *
                                       theme->mono_cell_width),
                    text_y, mask, count, theme->mono_font_height,
                    theme->mono_cell_width,
                    color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                          theme->text_tertiary : theme->client_text), 0u))
                    return ASTRA_ERROR_IO;
                shown += count;
                remaining -= count;
            }
        } else if (!astra_surface_mono_text_styled(&clipped,
                    text_x + (int32_t)(column * theme->mono_cell_width),
                    text_y, text, take, theme->mono_font_height,
                    theme->mono_cell_width,
                    color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                          theme->text_tertiary : theme->client_text), 0u))
            return ASTRA_ERROR_IO;
        column += scalars;
        offset += take;
    }
    if ((state & (ASTRA_CONTROL_FOCUSED | ASTRA_CONTROL_DISABLED)) ==
            ASTRA_CONTROL_FOCUSED && field_caret_visible(context, control)) {
        AstraTextSurface text = ASTRA_TEXT_SURFACE_INIT;
        AstraTextSurfaceInfo info = ASTRA_TEXT_SURFACE_INFO_INIT;
        char scratch[4];

        info.font_height = theme->mono_font_height;
        info.cell_width = theme->mono_cell_width;
        info.line_height = theme->mono_font_height;
        info.scratch = scratch;
        info.scratch_bytes = sizeof(scratch);
        if (astra_text_surface_init(&text, &info) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        return astra_text_surface_draw_caret(&text, &clipped,
            text_x, text_y, 0u, view.caret_column,
            ASTRA_TEXT_CARET_STANDARD, color(theme->accent));
    }
    return ASTRA_OK;
}

static void render_slider(const AstraTheme *theme,
                          const AstraControl *control,
                          AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    uint64_t offset = range_offset(control->_private_value,
                                   control->_private_minimum_value);
    AstraColorRGBA8 track = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    AstraColorRGBA8 active = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->accent;
    AstraColorRGBA8 thumb = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->text_tertiary;
    int32_t thumb_x;
    int32_t thumb_y;

    if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
        uint32_t travel = frame->width - 16u;
        uint32_t position = range_position(offset, range, travel);
        int32_t center_y = frame->y + (int32_t)frame->height / 2;

        astra_surface_fill_round(surface, frame->x + 8, center_y - 2,
                                 travel, 4u, 2u, color(track));
        if (offset != 0u)
            astra_surface_fill_round(surface, frame->x + 8, center_y - 2,
                                     position, 4u, 2u, color(active));
        thumb_x = frame->x + (int32_t)position;
        thumb_y = center_y - 8;
    } else {
        uint32_t travel = frame->height - 16u;
        uint32_t position = range_position(offset, range, travel);
        int32_t center_x = frame->x + (int32_t)frame->width / 2;

        astra_surface_fill_round(surface, center_x - 2, frame->y + 8,
                                 4u, travel, 2u, color(track));
        if (offset != 0u)
            astra_surface_fill_round(
                surface, center_x - 2,
                frame->y + 8 + (int32_t)(travel - position),
                4u, position, 2u, color(active));
        thumb_x = center_x - 8;
        thumb_y = frame->y + (int32_t)(travel - position);
    }
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, thumb_x - theme->focus_width,
            thumb_y - theme->focus_width,
            16u + (uint32_t)theme->focus_width * 2u,
            16u + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(8u + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, thumb_x, thumb_y, 16u, 16u, 8u,
                             color(thumb));
    astra_surface_fill_round(surface, thumb_x + 1, thumb_y + 1,
                             14u, 14u, 7u, color(theme->client));
}

static void render_dial(const AstraTheme *theme,
                        const AstraControl *control,
                        AstraSurfaceView *surface)
{
    static const int8_t hand[33][2] = {
        {-13, 13}, {-14, 11}, {-16, 8}, {-17, 6}, {-18, 4}, {-18, 1},
        {-18, -2}, {-17, -4}, {-17, -7}, {-15, -9}, {-14, -11},
        {-12, -13}, {-10, -15}, {-8, -16}, {-5, -17}, {-3, -18},
        {0, -18}, {3, -18}, {5, -17}, {8, -16}, {10, -15},
        {12, -13}, {14, -11}, {15, -9}, {17, -7}, {17, -4},
        {18, -2}, {18, 1}, {18, 4}, {17, 6}, {16, 8}, {14, 11},
        {13, 13}
    };
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    uint64_t offset = range_offset(control->_private_value,
                                   control->_private_minimum_value);
    uint32_t index = range_position(offset, range, 32u);
    int32_t center_x = frame->x + (int32_t)frame->width / 2;
    int32_t center_y = frame->y + (int32_t)frame->height / 2;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    AstraColorRGBA8 face = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled : theme->surface_inset;
    AstraColorRGBA8 ink = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->accent;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(surface, center_x - 27, center_y - 27,
                                 54u, 54u, 27u,
                                 color(theme->control_focus));
    astra_surface_fill_round(surface, center_x - 24, center_y - 24,
                             48u, 48u, 24u, color(border));
    astra_surface_fill_round(surface, center_x - 22, center_y - 22,
                             44u, 44u, 22u, color(face));
    (void)astra_surface_line(surface, center_x, center_y,
                             center_x + hand[index][0],
                             center_y + hand[index][1], color(ink));
}

typedef struct ScrollbarGeometry {
    uint32_t content;
    uint32_t viewport;
    uint32_t offset;
    uint32_t maximum;
    uint32_t track_length;
    uint32_t thumb_position;
    uint32_t thumb_length;
    int vertical;
} ScrollbarGeometry;

static int scrollbar_geometry(const AstraControl *control,
                              ScrollbarGeometry *geometry)
{
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    uint32_t travel;

    if (astra_scroll_get_state(
            const_scroll_model(control), &state) != ASTRA_OK)
        return 0;
    geometry->vertical =
        (control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
        ASTRA_ORIENTATION_VERTICAL;
    geometry->content = geometry->vertical ? state.content_height :
                                             state.content_width;
    geometry->viewport = geometry->vertical ? state.viewport_height :
                                              state.viewport_width;
    geometry->offset = geometry->vertical ? state.offset_y : state.offset_x;
    geometry->maximum = geometry->vertical ? state.maximum_y : state.maximum_x;
    geometry->track_length = geometry->vertical ?
        control->_private_frame.height : control->_private_frame.width;
    geometry->thumb_length = geometry->track_length;
    geometry->thumb_position = 0u;
    if (geometry->maximum == 0u || geometry->track_length == 0u)
        return 1;
    geometry->thumb_length = (uint32_t)(
        (uint64_t)geometry->viewport * geometry->track_length /
        geometry->content);
    if (geometry->thumb_length < 24u) geometry->thumb_length = 24u;
    if (geometry->thumb_length > geometry->track_length)
        geometry->thumb_length = geometry->track_length;
    travel = geometry->track_length - geometry->thumb_length;
    geometry->thumb_position = (uint32_t)(
        (uint64_t)geometry->offset * travel / geometry->maximum);
    return 1;
}

static uint32_t scrollbar_part(const AstraControl *control,
                               int32_t x, int32_t y)
{
    ScrollbarGeometry geometry;
    int64_t coordinate;
    int64_t first;
    int64_t last;

    if (!scrollbar_geometry(control, &geometry) || geometry.maximum == 0u)
        return SCROLLBAR_PART_NONE;
    coordinate = geometry.vertical ? y : x;
    first = (geometry.vertical ? control->_private_frame.y :
                                 control->_private_frame.x) +
            geometry.thumb_position;
    last = first + geometry.thumb_length;
    if (coordinate < first) return SCROLLBAR_PART_BEFORE;
    if (coordinate >= last) return SCROLLBAR_PART_AFTER;
    return SCROLLBAR_PART_THUMB;
}

static void scrollbar_drag_save(AstraUIContext *context, int32_t pointer,
                                 uint32_t offset)
{
    context->_private_value_input =
        ((uint64_t)(uint32_t)pointer << 32) | offset;
}

static int32_t scrollbar_drag_pointer(const AstraUIContext *context)
{
    return (int32_t)(uint32_t)(context->_private_value_input >> 32);
}

static uint32_t scrollbar_drag_offset(const AstraUIContext *context)
{
    return (uint32_t)context->_private_value_input;
}

static uint32_t scrollbar_offset(const ScrollbarGeometry *geometry,
                                 int64_t value)
{
    if (value < 0) return 0u;
    if ((uint64_t)value > geometry->maximum) return geometry->maximum;
    return (uint32_t)value;
}

static void scroll_activity(AstraUIContext *context,
                            const AstraScrollModel *model)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *control = &context->_private_controls[at];

        if (control->_private_control_kind != ASTRA_CONTROL_SCROLLBAR ||
            const_scroll_model(control) != model ||
            (control->_private_style & SCROLLBAR_STYLE_OVERLAY) == 0u)
            continue;
        control_damage(context, at);
        control->_private_animation_phase = 0u;
        animation_sync(context, at);
        control_damage(context, at);
    }
}

static AstraResult scroll_apply(AstraUIContext *context,
                                AstraScrollModel *model,
                                uint32_t x, uint32_t y,
                                uint32_t control_id,
                                AstraUIAction *action)
{
    AstraScrollState before = ASTRA_SCROLL_STATE_INIT;
    uint32_t previous_scroll_damage = context->_private_reserved;
    uint32_t scroll_view = CONTROL_NONE;
    uint32_t scroll_view_count = 0u;
    AstraResult result;

    if (astra_scroll_get_state(model, &before) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_scroll_set_offset(model, x, y);
    if (result != ASTRA_OK)
        return result;
    {
        AstraScrollState after = ASTRA_SCROLL_STATE_INIT;

        if (astra_scroll_get_state(model, &after) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (after.generation == before.generation)
            return ASTRA_OK;
        for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
            const AstraControl *control = &context->_private_controls[at];

            if (control->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW &&
                control->_private_laid_out != 0u &&
                const_scroll_model(control) == model) {
                scroll_view = at;
                ++scroll_view_count;
            }
        }
        if (scroll_view_count == 1u &&
            ((context->_private_has_damage == 0u &&
              previous_scroll_damage == 0u) ||
             (previous_scroll_damage != SCROLL_DAMAGE_INVALID &&
              (previous_scroll_damage & SCROLL_DAMAGE_INDEX_MASK) ==
                  scroll_view + 1u))) {
            context->_private_reserved =
                SCROLL_DAMAGE_RECORDING | (scroll_view + 1u);
        } else {
            context->_private_reserved = SCROLL_DAMAGE_INVALID;
        }
        result = relayout_scroll_model(context, model);
        if (result != ASTRA_OK) {
            (void)astra_scroll_set_offset(model, before.offset_x,
                                          before.offset_y);
            (void)relayout_scroll_model(context, model);
            context->_private_reserved = previous_scroll_damage;
            return result;
        }
        action->type = ASTRA_UI_ACTION_SCROLL_CHANGED;
        action->control_id = control_id;
        action->value = 0;
        scroll_activity(context, model);
        if (context->_private_reserved != SCROLL_DAMAGE_INVALID)
            context->_private_reserved &= ~SCROLL_DAMAGE_RECORDING;
    }
    return ASTRA_OK;
}

static AstraResult scrollbar_set(AstraUIContext *context, uint32_t index,
                                 uint32_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

    if (astra_scroll_get_state(scroll_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return (control->_private_style & SCROLLBAR_STYLE_ORIENTATION) ==
            ASTRA_ORIENTATION_VERTICAL ?
        scroll_apply(context, scroll_model(control), state.offset_x, value,
                     control->_private_id, action) :
        scroll_apply(context, scroll_model(control), value, state.offset_y,
                     control->_private_id, action);
}

static AstraResult scrollbar_pointer(AstraUIContext *context,
                                     uint32_t index,
                                     const AstraWindowEvent *event,
                                     AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    ScrollbarGeometry geometry;
    int32_t pointer;
    int64_t value;

    if (!scrollbar_geometry(control, &geometry) || geometry.maximum == 0u)
        return ASTRA_OK;
    pointer = geometry.vertical ? event->data.pointer.y :
                                  event->data.pointer.x;
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) {
        int64_t delta = (int64_t)pointer -
                        scrollbar_drag_pointer(context);

        if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u) {
            value = (int64_t)scrollbar_drag_offset(context) + delta;
        } else {
            uint32_t travel = geometry.track_length - geometry.thumb_length;

            value = scrollbar_drag_offset(context);
            if (travel != 0u)
                value += delta * geometry.maximum / travel;
        }
        return scrollbar_set(context, index,
                             scrollbar_offset(&geometry, value), action);
    }
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u)
        return ASTRA_OK;
    if (context->_private_capture_part == SCROLLBAR_PART_THUMB) {
        scrollbar_drag_save(context, pointer, geometry.offset);
        return ASTRA_OK;
    }
    if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_ALT) != 0u) {
        int64_t relative = (int64_t)pointer -
            (geometry.vertical ? control->_private_frame.y :
                                 control->_private_frame.x) -
            geometry.thumb_length / 2u;
        uint32_t travel = geometry.track_length - geometry.thumb_length;

        value = travel == 0u ? 0 : relative * geometry.maximum / travel;
    } else if (context->_private_capture_part == SCROLLBAR_PART_BEFORE) {
        value = (int64_t)geometry.offset - geometry.viewport;
    } else {
        value = (int64_t)geometry.offset + geometry.viewport;
    }
    return scrollbar_set(context, index,
                         scrollbar_offset(&geometry, value), action);
}

static AstraColorRGBA8 scroll_mark_color(const AstraTheme *theme,
                                         uint32_t kind)
{
    if (kind == ASTRA_SCROLL_MARK_WARNING) return theme->warning;
    if (kind == ASTRA_SCROLL_MARK_FAULT) return theme->fault;
    return theme->accent;
}

static void render_scrollbar(const AstraTheme *theme,
                             const AstraControl *control,
                             AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    ScrollbarGeometry geometry;
    uint32_t state_flags = effective_state(control);
    AstraColorRGBA8 thumb;
    AstraColorRGBA8 track;
    uint32_t alpha = 255u;
    int32_t thumb_x;
    int32_t thumb_y;
    uint32_t thumb_width;
    uint32_t thumb_height;

    if (!scrollbar_geometry(control, &geometry) ||
        astra_scroll_get_state(
            const_scroll_model(control), &state) != ASTRA_OK)
        return;
    if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u &&
        (state_flags & (ASTRA_CONTROL_HOVERED |
                        ASTRA_CONTROL_PRESSED)) == 0u) {
        if (control->_private_animation_phase >= SCROLLBAR_FADE_FRAMES)
            return;
        alpha = (SCROLLBAR_FADE_FRAMES -
                 control->_private_animation_phase) * 255u /
                SCROLLBAR_FADE_FRAMES;
    }
    track = (control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u ?
        blend(theme->frame, theme->surface_inset, alpha / 3u) :
        theme->client_border;
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height, 5u,
                             color(track));
    thumb = geometry.maximum == 0u ||
            (state_flags & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->control_disabled :
        ((state_flags & ASTRA_CONTROL_PRESSED) != 0u ? theme->accent :
         ((state_flags & ASTRA_CONTROL_HOVERED) != 0u ?
              theme->control_hover : theme->text_tertiary));
    if ((control->_private_style & SCROLLBAR_STYLE_OVERLAY) != 0u)
        thumb = blend(theme->frame, thumb, alpha);
    thumb_x = frame->x + (geometry.vertical ?
        (int32_t)(frame->width > 6u ? (frame->width - 6u) / 2u : 0u) :
        (int32_t)geometry.thumb_position);
    thumb_y = frame->y + (geometry.vertical ?
        (int32_t)geometry.thumb_position :
        (int32_t)(frame->height > 6u ? (frame->height - 6u) / 2u : 0u));
    thumb_width = geometry.vertical ?
        (frame->width < 6u ? frame->width : 6u) : geometry.thumb_length;
    thumb_height = geometry.vertical ? geometry.thumb_length :
        (frame->height < 6u ? frame->height : 6u);
    astra_surface_fill_round(surface, thumb_x, thumb_y,
                             thumb_width, thumb_height, 3u, color(thumb));
    if (geometry.maximum == 0u)
        return;
    for (uint32_t at = 0u; at < state.mark_count; ++at) {
        uint32_t coordinate = geometry.vertical ? state.marks[at].y :
                                                  state.marks[at].x;
        uint32_t position = geometry.content == 0u ? 0u : (uint32_t)(
            (uint64_t)coordinate * geometry.track_length /
            geometry.content);

        if (position >= geometry.track_length && geometry.track_length != 0u)
            position = geometry.track_length - 1u;
        if (geometry.vertical)
            astra_surface_fill(surface, frame->x, frame->y + position,
                               frame->width, 2u,
                               color(scroll_mark_color(
                                   theme, state.marks[at].kind)));
        else
            astra_surface_fill(surface, frame->x + position, frame->y,
                               2u, frame->height,
                               color(scroll_mark_color(
                                   theme, state.marks[at].kind)));
    }
}

static void render_stepper_arrow(AstraSurfaceView *surface, int32_t center_x,
                                 int32_t center_y, int up, uint16_t ink)
{
    int32_t direction = up ? 1 : -1;

    astra_surface_fill(surface, center_x - 4, center_y + direction * 2,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x - 2, center_y,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x, center_y - direction * 2,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x + 2, center_y,
                       2u, 2u, ink);
    astra_surface_fill(surface, center_x + 4, center_y + direction * 2,
                       2u, 2u, ink);
}

static void render_disclosure(const AstraUIContext *context,
                              const AstraTheme *theme,
                              const AstraControl *control,
                              AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t target = control_id_index(context, control->_private_style);
    uint32_t state = effective_state(control);
    int expanded = target != CONTROL_NONE &&
        (context->_private_controls[target]._private_state &
         ASTRA_CONTROL_COLLAPSED) == 0u;
    AstraColorRGBA8 background = theme->surface_inset;
    AstraColorRGBA8 ink = theme->client_text;
    int32_t chevron_x = frame->x + theme->control_padding_x + 5;
    int32_t center_y = frame->y + (int32_t)frame->height / 2;
    int32_t text_x = chevron_x + 5 + theme->spacing_unit * 2;
    uint32_t available = text_x < frame->x + (int32_t)frame->width ?
        (uint32_t)((int64_t)frame->x + frame->width - text_x) : 0u;
    uint32_t length;

    if ((state & ASTRA_CONTROL_DISABLED) != 0u) {
        background = theme->control_disabled;
        ink = theme->text_tertiary;
    } else if ((state & ASTRA_CONTROL_PRESSED) != 0u) {
        background = theme->control_pressed;
    } else if ((state & ASTRA_CONTROL_HOVERED) != 0u) {
        background = theme->control_hover;
    }
    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(background));
    if (expanded) {
        render_stepper_arrow(surface, chevron_x, center_y, 0, color(ink));
    } else {
        astra_surface_fill(surface, chevron_x - 2, center_y - 4,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x, center_y - 2,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x + 2, center_y,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x, center_y + 2,
                           2u, 2u, color(ink));
        astra_surface_fill(surface, chevron_x - 2, center_y + 4,
                           2u, 2u, color(ink));
    }
    length = astra_surface_ui_text_fit(
        control->_private_text, control->_private_text_length,
        theme->control_font_height, available);
    astra_surface_ui_text(
        surface, text_x,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        control->_private_text, length, theme->control_font_height,
        color(ink));
}

static AstraResult render_stepper(const AstraUIContext *context,
                                  uint32_t index, const AstraTheme *theme,
                                  const AstraControl *control,
                                  AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t half = frame->height / 2u;
    int32_t button_x = frame->x + (int32_t)frame->width -
                       STEPPER_BUTTON_WIDTH;
    int32_t button_center = button_x + STEPPER_BUTTON_WIDTH / 2;
    AstraColorRGBA8 background =
        (state & ASTRA_CONTROL_DISABLED) != 0u ?
            theme->control_disabled : theme->client;
    AstraColorRGBA8 border = (state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->control_error : theme->client_border;
    char text[22];
    uint32_t text_length = stepper_format(context, index, text);
    uint32_t text_width = text_length * theme->mono_cell_width;
    uint16_t up_ink = color(
        (state & ASTRA_CONTROL_DISABLED) != 0u ||
        control->_private_value == control->_private_maximum_value ?
            theme->text_muted : theme->text_secondary);
    uint16_t down_ink = color(
        (state & ASTRA_CONTROL_DISABLED) != 0u ||
        control->_private_value == control->_private_minimum_value ?
            theme->text_muted : theme->text_secondary);

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
                             frame->width, frame->height,
                             theme->control_radius, color(border));
    astra_surface_fill_round(
        surface, frame->x + theme->control_border_width,
        frame->y + theme->control_border_width,
        frame->width - (uint32_t)theme->control_border_width * 2u,
        frame->height - (uint32_t)theme->control_border_width * 2u,
        theme->control_radius > theme->control_border_width ?
            (uint16_t)(theme->control_radius -
                       theme->control_border_width) : 0u,
        color(background));
    if (context->_private_hover == index &&
        context->_private_hover_part != STEPPER_PART_VALUE) {
        uint32_t part = context->_private_hover_part;
        int pressed = context->_private_capture == index &&
                      context->_private_capture_part == part &&
                      (state & ASTRA_CONTROL_PRESSED) != 0u;

        astra_surface_fill(
            surface, button_x + theme->control_border_width,
            frame->y + (part == STEPPER_PART_DOWN ? (int32_t)half :
                                                       theme->control_border_width),
            STEPPER_BUTTON_WIDTH - theme->control_border_width * 2u,
            half - theme->control_border_width,
            color(pressed ? theme->control_pressed : theme->control_hover));
    }
    astra_surface_fill(surface, button_x, frame->y, 1u, frame->height,
                       color(theme->border_soft));
    astra_surface_fill(surface, button_x, frame->y + (int32_t)half,
                       STEPPER_BUTTON_WIDTH, 1u,
                       color(theme->border_soft));
    render_stepper_arrow(surface, button_center - 1,
                         frame->y + (int32_t)half / 2, 1, up_ink);
    render_stepper_arrow(surface, button_center - 1,
                         frame->y + (int32_t)half +
                             (int32_t)(frame->height - half) / 2,
                         0, down_ink);
    if (!astra_surface_mono_text_styled(
            surface,
            button_x - (int32_t)theme->spacing_unit * 2 -
                (int32_t)text_width,
            frame->y +
                (int32_t)(frame->height - theme->mono_font_height) / 2,
            text, text_length, theme->mono_font_height,
            theme->mono_cell_width,
            color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                  theme->text_tertiary : theme->client_text), 0u))
        return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

static void render_progress(const AstraTheme *theme,
                            const AstraControl *control,
                            AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t width;
    uint32_t offset;
    AstraColorRGBA8 fill;

    astra_surface_fill_round(surface, frame->x, frame->y, frame->width,
                             frame->height, frame->height / 2u,
                             color(theme->client_border));
    if (control->_private_maximum_value == 0) {
        uint32_t phase = control->_private_animation_phase & 63u;
        uint32_t triangle = phase <= 32u ? phase : 64u - phase;

        width = frame->width / 3u;
        offset = triangle * (frame->width - width) / 32u;
    } else {
        width = (uint32_t)((uint64_t)frame->width *
                           (uint32_t)control->_private_value /
                           (uint32_t)control->_private_maximum_value);
        offset = 0u;
    }
    if ((state & ASTRA_CONTROL_ERROR) != 0u)
        fill = theme->fault;
    else if ((state & ASTRA_CONTROL_DISABLED) != 0u ||
             (control->_private_maximum_value != 0 &&
              control->_private_value == control->_private_maximum_value))
        fill = theme->text_tertiary;
    else
        fill = theme->accent;
    astra_surface_fill_round(surface, frame->x + (int32_t)offset, frame->y,
                             width, frame->height, frame->height / 2u,
                             color(fill));
}

static void render_collection_frame(const AstraTheme *theme,
    const AstraControl *control, AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint16_t border = color((state & ASTRA_CONTROL_ERROR) != 0u ?
        theme->fault : (state & CONTROL_FOCUS_VISIBLE) != 0u ?
        theme->control_focus : theme->client_border);

    astra_surface_fill(surface, frame->x, frame->y, frame->width,
                       frame->height, color(theme->surface_raised));
    astra_surface_fill(surface, frame->x, frame->y, frame->width, 1u, border);
    astra_surface_fill(surface, frame->x,
                       frame->y + (int32_t)frame->height - 1,
                       frame->width, 1u, border);
    astra_surface_fill(surface, frame->x, frame->y, 1u,
                       frame->height, border);
    astra_surface_fill(surface,
                       frame->x + (int32_t)frame->width - 1,
                       frame->y, 1u, frame->height, border);
}

static AstraColorRGBA8 render_collection_row(const AstraTheme *theme,
    const AstraControl *control, AstraSurfaceView *surface,
    uint32_t row, int32_t y)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    const AstraCollectionModel *model = const_collection_model(control);
    int selected = model->selection != NULL ?
        astra_interface_selection_contains(model->selection,
            model->items[selection_item_at(control, row)].id) :
        control->_private_value == (int64_t)row;
    int hot = control->_private_minimum_value == (int64_t)row &&
              (state & ASTRA_CONTROL_HOVERED) != 0u;
    AstraColorRGBA8 text_color =
        (state & ASTRA_CONTROL_DISABLED) != 0u ? theme->text_tertiary :
        selected ? theme->accent_text : theme->client_text;

    if (selected || hot) {
        AstraColorRGBA8 fill = selected ? theme->accent : theme->client;

        if ((state & ASTRA_CONTROL_DISABLED) != 0u)
            fill = theme->control_disabled;
        if (frame->width > 2u)
            astra_surface_fill(surface, frame->x + 1, y,
                               frame->width - 2u,
                               theme->control_height, color(fill));
    }
    return text_color;
}

static void render_list(const AstraTheme *theme,
                        const AstraControl *control,
                        AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraCollectionItem *items = const_collection_model(control)->items;
    uint32_t first, end;

    list_visible_range(control, theme->control_height, 0u, &first, &end);
    render_collection_frame(theme, control, surface);
    for (uint32_t at = first; at < end; ++at) {
        int32_t y = (int32_t)((int64_t)frame->y +
                              (uint64_t)at * theme->control_height);
        uint32_t heading = (items[at].presentation.flags &
            ASTRA_COLLECTION_HEADING) != 0u;
        uint32_t icon = items[at].presentation.icon;
        AstraColorRGBA8 text_color = render_collection_row(
            theme, control, surface, at, y);
        uint32_t inset = (uint32_t)theme->control_padding_x +
            (icon != ASTRA_TOOLBAR_ICON_NONE ? 22u : 0u);
        uint32_t text_width = frame->width >
            inset + (uint32_t)theme->control_padding_x ?
            frame->width - inset - (uint32_t)theme->control_padding_x : 0u;
        if (heading) text_color = theme->client_text_secondary;
        if (icon != ASTRA_TOOLBAR_ICON_NONE)
            render_toolbar_icon(surface, icon,
                frame->x + theme->control_padding_x,
                y + ((int32_t)theme->control_height - 16) / 2,
                color(text_color));
        uint32_t fitted = astra_surface_ui_text_fit(
            items[at].text, items[at].text_length,
            theme->control_font_height, text_width);

        astra_surface_ui_text(
            surface, frame->x + (int32_t)inset,
            y + (int32_t)(theme->control_height -
                          theme->control_font_height) / 2,
            items[at].text, fitted, theme->control_font_height,
            color(text_color));
    }
}

static void render_popup(const AstraTheme *theme,
                         const AstraControl *control,
                         AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    if (frame->width < 32u || frame->height < 2u) return;
    const AstraChoiceItem *item =
        &choice_items(control)[control->_private_value];
    uint32_t state = effective_state(control);
    uint32_t text_space = frame->width > 32u ? frame->width - 32u : 0u;
    uint32_t fitted = astra_surface_ui_text_fit(
        item->text, item->text_length, theme->control_font_height,
        text_space);
    int32_t arrow_x = frame->x + (int32_t)frame->width - 18;
    int32_t arrow_y = frame->y + (int32_t)frame->height / 2;
    AstraColorRGBA8 ink = (state & ASTRA_CONTROL_DISABLED) != 0u ?
        theme->text_tertiary : theme->client_text;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(surface,
            frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(surface, frame->x, frame->y,
        frame->width, frame->height, theme->control_radius,
        color((state & ASTRA_CONTROL_ERROR) != 0u ?
              theme->control_error : theme->client_border));
    astra_surface_fill_round(surface, frame->x + 1, frame->y + 1,
        frame->width - 2u, frame->height - 2u,
        theme->control_radius > 1u ? theme->control_radius - 1u : 0u,
        color((state & ASTRA_CONTROL_DISABLED) != 0u ?
              theme->control_disabled :
              (state & ASTRA_CONTROL_PRESSED) != 0u ?
                  theme->control_pressed : theme->surface_raised));
    astra_surface_ui_text(surface,
        frame->x + theme->control_padding_x,
        frame->y + (int32_t)(frame->height - theme->control_font_height) / 2,
        item->text, fitted, theme->control_font_height, color(ink));
    astra_surface_fill(surface, arrow_x, arrow_y - 2, 9u, 1u, color(ink));
    astra_surface_fill(surface, arrow_x + 1, arrow_y - 1, 7u, 1u,
                       color(ink));
    astra_surface_fill(surface, arrow_x + 3, arrow_y, 3u, 1u, color(ink));
}

static void render_disclosure_mark(AstraSurfaceView *surface,
    int32_t x, int32_t y, uint32_t expanded, AstraColorRGBA8 ink)
{
    if (expanded != 0u) {
        astra_surface_fill(surface, x - 3, y - 1, 7u, 1u, color(ink));
        astra_surface_fill(surface, x - 2, y, 5u, 1u, color(ink));
        astra_surface_fill(surface, x - 1, y + 1, 3u, 1u, color(ink));
    } else {
        astra_surface_fill(surface, x - 1, y - 3, 1u, 7u, color(ink));
        astra_surface_fill(surface, x, y - 2, 1u, 5u, color(ink));
        astra_surface_fill(surface, x + 1, y - 1, 1u, 3u, color(ink));
    }
}

static void render_tree(const AstraTheme *theme,
                        const AstraControl *control,
                        AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraTreeModel *tree = const_tree_model(control);
    const AstraCollectionItem *items = tree->collection->items;
    uint32_t first, end;

    list_visible_range(control, theme->control_height, 0u, &first, &end);
    render_collection_frame(theme, control, surface);
    for (uint32_t row = first; row < end; ++row) {
        uint32_t item = tree->visible_rows[row];
        uint32_t indent = (tree->nodes[item].depth + 1u) *
                          theme->control_height;
        int32_t y = (int32_t)((int64_t)frame->y +
                              (uint64_t)row * theme->control_height);
        AstraColorRGBA8 text_color = render_collection_row(
            theme, control, surface, row, y);
        int32_t glyph_x = frame->x + (int32_t)indent -
                          (int32_t)theme->control_height / 2;
        int32_t glyph_y = y + (int32_t)theme->control_height / 2;
        uint32_t available = frame->width > indent +
            (uint32_t)theme->control_padding_x ?
            frame->width - indent - theme->control_padding_x : 0u;
        uint32_t fitted = astra_surface_ui_text_fit(
            items[item].text, items[item].text_length,
            theme->control_font_height, available);

        if (tree_has_children(tree, item)) {
            AstraColorRGBA8 mark =
                (effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->text_tertiary : text_color;
            render_disclosure_mark(surface, glyph_x, glyph_y,
                tree->nodes[item].expanded, mark);
        }
        astra_surface_ui_text(surface, frame->x + (int32_t)indent,
            y + (int32_t)(theme->control_height -
                          theme->control_font_height) / 2,
            items[item].text, fitted, theme->control_font_height,
            color(text_color));
    }
}

static AstraResult render_icon_grid(const AstraTheme *theme,
    const AstraControl *control, AstraSurfaceView *surface)
{
    const AstraIconGridModel *grid = const_icon_grid_model(control);
    const AstraCollectionItem *items = grid->collection->items;
    uint32_t tile_width = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
        ASTRA_ICON_TILE_CLIENT_WIDTH : ASTRA_ICON_TILE_DESKTOP_WIDTH;
    uint32_t tile_height = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
        ASTRA_ICON_TILE_CLIENT_HEIGHT : ASTRA_ICON_TILE_DESKTOP_HEIGHT;
    uint32_t first, end;

    icon_grid_visible_rows(control, tile_height, grid->columns, &first, &end);
    render_collection_frame(theme, control, surface);
    for (uint32_t row = first; row < end; ++row)
        for (uint32_t col = 0u; col < grid->columns; ++col) {
            uint64_t item = (uint64_t)row * grid->columns + col;
            int64_t x = (int64_t)control->_private_frame.x +
                        (uint64_t)col * tile_width;
            int64_t y = (int64_t)control->_private_frame.y +
                        (uint64_t)row * tile_height;
            AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;
            AstraResult result;

            if (item >= grid->icon_count ||
                x >= (int64_t)control->_private_clip.x +
                         control->_private_clip.width ||
                x + tile_width <= control->_private_clip.x)
                continue;
            int selected = grid->collection->selection != NULL ?
                astra_interface_selection_contains(grid->collection->selection,
                    items[item].id) :
                (int64_t)item == control->_private_value;
            if (selected ||
                ((int64_t)item == control->_private_minimum_value &&
                 (effective_state(control) & ASTRA_CONTROL_HOVERED) != 0u))
                astra_surface_fill_round(surface, (int32_t)x + 2,
                    (int32_t)y + 2, tile_width - 4u, tile_height - 4u,
                    theme->control_radius,
                    color(selected ?
                        theme->control_selected : theme->client));
            tile.style = grid->tile_style;
            tile.frame = (AstraRectI32){(int32_t)x, (int32_t)y,
                                        tile_width, tile_height};
            tile.clip = (AstraRectI32){control->_private_clip.x,
                control->_private_clip.y, control->_private_clip.width,
                control->_private_clip.height};
            tile.icon = grid->icons[item];
            tile.label = items[item].text;
            tile.label_bytes = items[item].text_length;
            result = astra_interface_icon_tile_render_view(surface, &tile);
            if (result != ASTRA_OK) return result;
        }
    return ASTRA_OK;
}

static void render_table(const AstraTheme *theme,
                         const AstraControl *control,
                         AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraTableModel *table = table_model(control);
    const AstraOutlineTableModel *outline =
        control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE ?
            const_outline_table_model(control) : NULL;
    const AstraCollectionItem *items = const_collection_model(control)->items;
    uint32_t first, end;
    int32_t column_x;

    list_visible_range(control, theme->control_height,
                       theme->control_height, &first, &end);
    render_collection_frame(theme, control, surface);
    astra_surface_fill(surface, frame->x + 1, frame->y + 1,
        frame->width > 2u ? frame->width - 2u : 0u,
        theme->control_height > 1u ? theme->control_height - 1u : 0u,
        color(theme->client));
    column_x = frame->x;
    for (uint32_t col = 0u; col < table->column_count; ++col) {
        const AstraTableColumn *column = &table->columns[col];
        uint32_t sorted = (control->_private_style & TABLE_SORT_ACTIVE) != 0u &&
            (control->_private_style & TABLE_SORT_COLUMN_MASK) == col;
        uint32_t available = column->width >
            (uint32_t)theme->control_padding_x * 2u ?
            column->width - (uint32_t)theme->control_padding_x * 2u : 0u;
        if (sorted && available > 14u) available -= 14u;
        uint32_t fitted = astra_surface_ui_text_fit(
            column->heading, column->heading_length,
            theme->control_font_height, available);

        astra_surface_ui_text(surface,
            column_x + theme->control_padding_x,
            frame->y + (int32_t)(theme->control_height -
                theme->control_font_height) / 2,
            column->heading, fitted, theme->control_font_height,
            color(theme->client_text_secondary));
        if (sorted && column->width >= 20u)
            render_stepper_arrow(surface,
                column_x + (int32_t)column->width - 11,
                frame->y + (int32_t)theme->control_height / 2,
                (control->_private_style & TABLE_SORT_REVERSE) == 0u,
                color(theme->client_text_secondary));
        column_x += (int32_t)column->width;
    }
    for (uint32_t row = first; row < end; ++row) {
        int32_t y = (int32_t)((int64_t)frame->y +
            (uint64_t)(row + 1u) * theme->control_height);
        AstraColorRGBA8 text_color = render_collection_row(
            theme, control, surface, row, y);

        column_x = frame->x;
        for (uint32_t col = 0u; col < table->column_count; ++col) {
            const char *text = col == 0u ? items[row].text :
                table->cells[row * (table->column_count - 1u) + col - 1u].text;
            uint32_t length = col == 0u ? items[row].text_length :
                table->cells[row * (table->column_count - 1u) + col - 1u].text_length;
            uint32_t width = table->columns[col].width;
            uint32_t available = width >
                (uint32_t)theme->control_padding_x * 2u ?
                width - (uint32_t)theme->control_padding_x * 2u : 0u;
            uint32_t indent = col == 0u && outline != NULL ?
                outline->rows[row].depth * 16u + 20u : 0u;

            if (indent > available) available = 0u;
            else available -= indent;
            if (col == 0u && outline != NULL &&
                outline->rows[row].expandable != 0u &&
                indent <= width) {
                AstraColorRGBA8 mark =
                    (effective_state(control) & ASTRA_CONTROL_DISABLED) != 0u ?
                        theme->text_tertiary : text_color;
                render_disclosure_mark(surface,
                    column_x + theme->control_padding_x +
                        (int32_t)indent - 12,
                    y + (int32_t)theme->control_height / 2,
                    outline->rows[row].expanded, mark);
            }

            if (length != 0u && available != 0u) {
                uint32_t fitted = astra_surface_ui_text_fit(
                    text, length, theme->control_font_height, available);
                astra_surface_ui_text(surface,
                    column_x + theme->control_padding_x + (int32_t)indent,
                    y + (int32_t)(theme->control_height -
                        theme->control_font_height) / 2,
                    text, fitted, theme->control_font_height,
                    color(text_color));
            }
            column_x += (int32_t)width;
        }
    }
}

static void render_segmented(const AstraTheme *theme,
                             const AstraControl *control,
                             AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraChoiceItem *items = choice_items(control);
    uint32_t state = effective_state(control);
    uint32_t inset = 2u;
    uint32_t inner_width = frame->width - inset * 2u;
    uint32_t inner_height = frame->height - inset * 2u;
    uint32_t count = control->_private_text_length;

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u)
        astra_surface_fill_round(
            surface, frame->x - theme->focus_width,
            frame->y - theme->focus_width,
            frame->width + (uint32_t)theme->focus_width * 2u,
            frame->height + (uint32_t)theme->focus_width * 2u,
            (uint16_t)(theme->control_radius + theme->focus_width),
            color(theme->control_focus));
    astra_surface_fill_round(
        surface, frame->x, frame->y, frame->width, frame->height,
        theme->control_radius,
        color((state & ASTRA_CONTROL_ERROR) != 0u ?
              theme->control_error : theme->client_border));
    for (uint32_t at = 0u; at < count; ++at) {
        uint32_t left = (uint32_t)((uint64_t)inner_width * at / count);
        uint32_t right = (uint32_t)((uint64_t)inner_width * (at + 1u) /
                                    count);
        int selected = control->_private_value == (int32_t)at;
        int hot = control->_private_minimum_value == (int32_t)at;
        AstraColorRGBA8 text_color =
            (state & ASTRA_CONTROL_DISABLED) != 0u ?
                theme->text_tertiary : theme->client_text_secondary;
        uint32_t fitted;
        uint32_t text_width;
        int32_t text_x;
        int32_t text_y;

        if ((state & ASTRA_CONTROL_PRESSED) != 0u && hot) {
            astra_surface_fill_round(
                surface, frame->x + (int32_t)inset + (int32_t)left,
                frame->y + (int32_t)inset,
                right - left > 2u ? right - left - 2u : right - left,
                inner_height,
                theme->control_radius - inset,
                color(theme->control_pressed));
            text_color = theme->text_primary;
        } else if (selected ||
                   ((state & ASTRA_CONTROL_HOVERED) != 0u && hot)) {
            AstraColorRGBA8 fill = selected ? theme->surface_raised :
                                              theme->client;

            if ((state & ASTRA_CONTROL_DISABLED) != 0u)
                fill = theme->control_disabled;
            astra_surface_fill_round(
                surface, frame->x + (int32_t)inset + (int32_t)left,
                frame->y + (int32_t)inset,
                right - left > 2u ? right - left - 2u : right - left,
                inner_height,
                theme->control_radius - inset,
                color(fill));
            if ((state & ASTRA_CONTROL_DISABLED) == 0u)
                text_color = theme->client_text;
        }
        fitted = astra_surface_ui_text_fit(
            items[at].text, items[at].text_length,
            theme->control_font_height, right - left);
        text_width = astra_surface_ui_text_width(
            items[at].text, fitted, theme->control_font_height);
        text_x = frame->x + (int32_t)inset + (int32_t)left +
                 (int32_t)(right - left - text_width) / 2;
        text_y = frame->y +
                 (int32_t)(frame->height - theme->control_font_height) / 2;
        astra_surface_ui_text(surface, text_x, text_y, items[at].text,
                              fitted, theme->control_font_height,
                              color(text_color));
    }
}

static uint32_t tab_item_width(const AstraTheme *theme,
                               const AstraChoiceItem *item)
{
    return astra_surface_ui_text_width(item->text, item->text_length,
                                       theme->control_font_height) +
           theme->spacing_unit * 6u;
}

static uint64_t tab_total_width(const AstraTheme *theme,
                                const AstraControl *control)
{
    const AstraChoiceItem *items = choice_items(control);
    uint64_t total = 0u;

    for (uint32_t at = 0u; at < control->_private_text_length; ++at)
        total += tab_item_width(theme, &items[at]);
    return total;
}

static void tab_item_bounds(uint32_t frame_width, uint64_t total,
                            uint64_t before, uint32_t width,
                            uint32_t *left, uint32_t *right)
{
    if (total > frame_width) {
        *left = (uint32_t)(before * frame_width / total);
        *right = (uint32_t)((before + width) * frame_width / total);
    } else {
        *left = (uint32_t)before;
        *right = *left + width;
    }
}

static void render_tab(const AstraTheme *theme,
                       const AstraControl *control,
                       AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraChoiceItem *items = choice_items(control);
    uint32_t state = effective_state(control);
    uint64_t total = tab_total_width(theme, control);
    uint64_t before = 0u;
    uint16_t rail = color((state & ASTRA_CONTROL_ERROR) != 0u ?
                          theme->control_error : theme->client_border);

    if ((state & CONTROL_FOCUS_VISIBLE) != 0u) {
        astra_surface_fill_round(surface, frame->x, frame->y,
                                 frame->width, frame->height,
                                 theme->control_radius,
                                 color(theme->control_focus));
        astra_surface_fill_round(
            surface, frame->x + theme->control_border_width,
            frame->y + theme->control_border_width,
            frame->width - (uint32_t)theme->control_border_width * 2u,
            frame->height - (uint32_t)theme->control_border_width * 2u,
            theme->control_radius > theme->control_border_width ?
                (uint16_t)(theme->control_radius -
                           theme->control_border_width) : 0u,
            color(theme->client));
    }
    astra_surface_fill(surface, frame->x,
                       frame->y + (int32_t)frame->height - 1,
                       frame->width, 1u, rail);
    for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
        uint32_t left;
        uint32_t right;
        uint32_t fitted;
        uint32_t text_width;
        uint32_t item_width = tab_item_width(theme, &items[at]);
        AstraColorRGBA8 text_color = theme->client_text_secondary;

        tab_item_bounds(frame->width, total, before, item_width,
                        &left, &right);
        before += item_width;
        if ((state & (ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_HOVERED)) ==
                ASTRA_CONTROL_HOVERED &&
            control->_private_minimum_value == (int32_t)at &&
            control->_private_value != (int32_t)at &&
            frame->height > 2u)
            astra_surface_fill_round(surface, frame->x + (int32_t)left,
                frame->y, right - left, frame->height - 2u,
                theme->control_radius - 2u, color(theme->surface_inset));
        if ((state & ASTRA_CONTROL_DISABLED) != 0u)
            text_color = theme->text_tertiary;
        else if (control->_private_value == (int32_t)at ||
                 control->_private_minimum_value == (int32_t)at)
            text_color = theme->client_text;
        fitted = astra_surface_ui_text_fit(
            items[at].text, items[at].text_length,
            theme->control_font_height, right - left);
        text_width = astra_surface_ui_text_width(
            items[at].text, fitted, theme->control_font_height);
        astra_surface_ui_text(
            surface,
            frame->x + (int32_t)left +
                (int32_t)(right - left - text_width) / 2,
            frame->y +
                (int32_t)(frame->height - theme->control_font_height) / 2,
            items[at].text, fitted, theme->control_font_height,
            color(text_color));
        if (control->_private_value == (int32_t)at)
            astra_surface_fill(surface, frame->x + (int32_t)left,
                               frame->y + (int32_t)frame->height - 2,
                               right - left, 2u,
                               color((state & ASTRA_CONTROL_DISABLED) != 0u ?
                                     theme->text_tertiary : theme->accent));
    }
}

static int32_t collection_row_at(const AstraControl *control, int32_t y)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    int64_t relative = (int64_t)y - control->_private_frame.y -
        (TABLE_KINDS(control->_private_control_kind) ?
            theme.control_height : 0u);
    if (relative < 0) return -1;
    uint32_t index = (uint32_t)relative / theme.control_height;
    return index < control->_private_text_length ? (int32_t)index : -1;
}

static int32_t choice_index_at(const AstraControl *control, int32_t x,
                               int32_t y)
{
    if (control->_private_control_kind == ASTRA_CONTROL_POPUP)
        return control->_private_value;
    if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
        const AstraIconGridModel *grid = const_icon_grid_model(control);
        uint32_t tile_width = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_WIDTH : ASTRA_ICON_TILE_DESKTOP_WIDTH;
        uint32_t tile_height = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_HEIGHT : ASTRA_ICON_TILE_DESKTOP_HEIGHT;
        int64_t relative_x = (int64_t)x - control->_private_frame.x;
        int64_t relative_y = (int64_t)y - control->_private_frame.y;
        uint64_t col, row, item;

        if (relative_x < 0 || relative_y < 0) return -1;
        col = (uint64_t)relative_x / tile_width;
        row = (uint64_t)relative_y / tile_height;
        if (col >= grid->columns) return -1;
        item = row * grid->columns + col;
        return item < grid->icon_count ? (int32_t)item : -1;
    }
    if (COLLECTION_KINDS(control->_private_control_kind)) {
        int32_t row = collection_row_at(control, y);
        return row >= 0 && collection_row_selectable(control, (uint32_t)row) ?
            row : -1;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_TAB) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        const AstraChoiceItem *items = choice_items(control);
        uint64_t total = tab_total_width(&theme, control);
        uint64_t before = 0u;
        uint32_t relative = (uint32_t)((int64_t)x -
                                       control->_private_frame.x);

        for (uint32_t at = 0u; at < control->_private_text_length; ++at) {
            uint32_t left;
            uint32_t right;
            uint32_t item_width = tab_item_width(&theme, &items[at]);

            tab_item_bounds(control->_private_frame.width, total, before,
                            item_width, &left, &right);
            before += item_width;
            if (relative >= left && relative < right)
                return (int32_t)at;
        }
        return -1;
    }
    uint64_t relative = (uint64_t)((int64_t)x -
                                   control->_private_frame.x);
    uint32_t count = control->_private_text_length;
    uint32_t index = (uint32_t)(relative * count /
                                control->_private_frame.width);

    return (int32_t)(index < count ? index : count - 1u);
}

static void choice_hot(AstraUIContext *context, uint32_t index, int32_t x,
                       int32_t y)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t hot = choice_index_at(control, x, y);

    if (hot == control->_private_minimum_value)
        return;
    control_damage(context, index);
    control->_private_minimum_value = hot;
    control_damage(context, index);
}

static void choice_action(AstraUIContext *context, uint32_t index,
                          int32_t value, uint32_t modifiers,
                          AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t before = control->_private_value;

    if (COLLECTION_KINDS(control->_private_control_kind) &&
        collection_model(control)->selection != NULL) {
        if (value < 0 || value >= (int32_t)control->_private_text_length)
            return;
        if (!selection_apply(control, (uint32_t)value, modifiers)) return;
        control_damage(context, index);
        control->_private_value = selection_visible_row(control,
            collection_model(control)->selected_id);
        control_damage(context, index);
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
        action->decimal_places = 0u;
        return;
    }

    if (astra_interface_control_set_value(context, control, value) !=
            ASTRA_OK || control->_private_value == before)
        return;
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = control->_private_id;
    action->value = control->_private_value;
    action->decimal_places = 0u;
}

static AstraResult control_reveal(AstraUIContext *context, uint32_t index,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t top = (int64_t)control->_private_frame.y + y;
    int64_t bottom = top + height;
    int64_t left = (int64_t)control->_private_frame.x + x;
    int64_t right = left + width;
    int64_t clip_top = control->_private_clip.y;
    int64_t clip_bottom = clip_top + control->_private_clip.height;
    int64_t clip_left = control->_private_clip.x;
    int64_t clip_right = clip_left + control->_private_clip.width;
    int64_t delta = top < clip_top ? top - clip_top :
                    bottom > clip_bottom ? bottom - clip_bottom : 0;
    int64_t dx = left < clip_left ? left - clip_left :
                 right > clip_right ? right - clip_right : 0;
    uint32_t parent = control->_private_parent;

    if (delta == 0 && dx == 0) return ASTRA_OK;
    while (parent != CONTROL_NONE) {
        AstraControl *view = &context->_private_controls[parent];

        if (view->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
            AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
            AstraUIAction ignored = ASTRA_UI_ACTION_INIT;
            int64_t offset, offset_x;

            if (astra_scroll_get_state(scroll_model(view), &state) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            offset = (int64_t)state.offset_y + delta;
            if (offset < 0) offset = 0;
            if (offset > state.maximum_y) offset = state.maximum_y;
            offset_x = (int64_t)state.offset_x + dx;
            if (offset_x < 0) offset_x = 0;
            if (offset_x > state.maximum_x) offset_x = state.maximum_x;
            return scroll_apply(context, scroll_model(view), (uint32_t)offset_x,
                                (uint32_t)offset, view->_private_id, &ignored);
        }
        parent = view->_private_parent;
    }
    return ASTRA_OK;
}

static AstraResult list_reveal_row(AstraUIContext *context, uint32_t index,
                                   uint32_t row)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    const AstraControl *control = &context->_private_controls[index];
    if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
        const AstraIconGridModel *grid = const_icon_grid_model(control);
        uint32_t tile_width = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_WIDTH : ASTRA_ICON_TILE_DESKTOP_WIDTH;
        uint32_t tile_height = grid->tile_style == ASTRA_ICON_TILE_CLIENT ?
            ASTRA_ICON_TILE_CLIENT_HEIGHT : ASTRA_ICON_TILE_DESKTOP_HEIGHT;
        return control_reveal(context, index,
            (row % grid->columns) * tile_width,
            (row / grid->columns) * tile_height,
            tile_width, tile_height);
    }
    uint32_t header = context->_private_controls[index]._private_control_kind ==
        ASTRA_CONTROL_TABLE ||
        context->_private_controls[index]._private_control_kind ==
            ASTRA_CONTROL_OUTLINE_TABLE ? theme.control_height : 0u;
    return control_reveal(context, index, 0u,
                           header + row * theme.control_height,
                           1u, theme.control_height);
}

static void range_action(AstraUIContext *context, uint32_t index,
                         int64_t value, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t before = control->_private_value;

    (void)astra_interface_control_set_value(context, control, value);
    if (control->_private_value != before) {
        action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
        action->decimal_places = control->_private_decimal_places;
    }
}

static AstraResult splitter_pointer(AstraUIContext *context, uint32_t index,
                                    int32_t x, int32_t y,
                                    AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t pointer = control->_private_style == ASTRA_ORIENTATION_VERTICAL ?
        x : y;
    int64_t position = (int64_t)control->_private_maximum_value + pointer -
                       control->_private_minimum_value;

    return splitter_set_position(context, index, position, action);
}

static void slider_pointer(AstraUIContext *context, uint32_t index,
                           int32_t x, int32_t y, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t travel;
    int64_t position;
    uint64_t range = range_offset(control->_private_maximum_value,
                                  control->_private_minimum_value);
    int64_t value;

    if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL) {
        travel = frame->width - 16u;
        position = (int64_t)x - frame->x - 8;
    } else {
        travel = frame->height - 16u;
        position = (int64_t)frame->y + 8 + travel - y;
    }
    if (position < 0) position = 0;
    if ((uint64_t)position > travel) position = travel;
    value = range_value_at(
        control->_private_minimum_value,
        range_offset_at(range, (uint32_t)position, travel));
    range_action(context, index, value, action);
}

static void dial_drag_save(AstraUIContext *context, int32_t pointer,
                           int64_t value)
{
    memcpy(&context->_private_value_input, &value, sizeof(value));
    context->_private_value_input_digits = (uint32_t)pointer;
}

static void dial_pointer(AstraUIContext *context, uint32_t index,
                         const AstraWindowEvent *event,
                         AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int32_t anchor_y = (int32_t)context->_private_value_input_digits;
    int64_t anchor_value;
    int64_t pixels = (int64_t)anchor_y - event->data.pointer.y;
    int64_t value;
    int64_t snapped;

    memcpy(&anchor_value, &context->_private_value_input,
           sizeof(anchor_value));
    if ((event->data.pointer.modifiers & ASTRA_INPUT_MOD_ALT) != 0u)
        pixels /= 4;
    value = range_move_steps(control, anchor_value, pixels);
    snapped = range_snap(control, value);
    if (control->_private_minimum_value < 0 &&
        control->_private_maximum_value > 0 &&
        range_value_representable(
            0, control->_private_minimum_value,
            control->_private_maximum_value,
            control->_private_value_step) &&
        snapped >= -control->_private_value_step &&
        snapped <= control->_private_value_step)
        value = 0;
    range_action(context, index, value, action);
}

static uint32_t stepper_part(const AstraControl *control, int32_t x,
                             int32_t y)
{
    const AstraControlFrame *frame = &control->_private_frame;
    int64_t button_left = (int64_t)frame->x + frame->width -
                          STEPPER_BUTTON_WIDTH;

    if (x < button_left || x >= (int64_t)frame->x + frame->width ||
        y < frame->y || y >= (int64_t)frame->y + frame->height)
        return STEPPER_PART_VALUE;
    return y < frame->y + (int32_t)frame->height / 2 ?
        STEPPER_PART_UP : STEPPER_PART_DOWN;
}

static void stepper_step(AstraUIContext *context, uint32_t index,
                         uint32_t part, uint32_t count,
                         AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    int64_t steps;

    if (part == STEPPER_PART_UP)
        steps = count;
    else if (part == STEPPER_PART_DOWN)
        steps = -(int64_t)count;
    else
        return;
    range_action(context, index,
                 range_move_steps(control, control->_private_value, steps),
                 action);
}

static void stepper_finish_input(AstraUIContext *context, uint32_t index,
                                 int commit, AstraUIAction *action)
{
    int64_t value;

    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u)
        return;
    if (commit && stepper_draft_value(context, &value))
        range_action(context, index, value, action);
    control_damage(context, index);
    context->_private_value_input = 0u;
    context->_private_value_input_digits = 0u;
    context->_private_value_input_flags = 0u;
    control_damage(context, index);
}

static void stepper_text(AstraUIContext *context, uint32_t index,
                         uint32_t scalar, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    uint32_t digit;
    int64_t value;

    if (scalar == '-' && context->_private_value_input_digits == 0u &&
        control->_private_minimum_value < 0) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_flags =
            VALUE_INPUT_EDITING | VALUE_INPUT_NEGATIVE;
        control_damage(context, index);
        return;
    }
    if (scalar == '+' && context->_private_value_input_digits == 0u) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_flags = VALUE_INPUT_EDITING;
        control_damage(context, index);
        return;
    }
    if (scalar < '0' || scalar > '9') return;
    digit = scalar - '0';
    if ((context->_private_value_input_flags & VALUE_INPUT_EDITING) == 0u) {
        control_damage(context, index);
        context->_private_value_input = 0u;
        context->_private_value_input_digits = 0u;
        context->_private_value_input_flags = VALUE_INPUT_EDITING;
    }
    if (context->_private_value_input == 0u &&
        context->_private_value_input_digits != 0u) {
        context->_private_value_input_digits = 0u;
    }
    {
        uint64_t limit =
            (context->_private_value_input_flags & VALUE_INPUT_NEGATIVE) != 0u ?
                UINT64_C(9223372036854775808) : (uint64_t)INT64_MAX;

        if (context->_private_value_input_digits >= 19u ||
            context->_private_value_input > (limit - digit) / 10u)
            return;
    }
    context->_private_value_input =
        context->_private_value_input * 10u + digit;
    ++context->_private_value_input_digits;
    control_damage(context, index);
    if (stepper_draft_value(context, &value) &&
        value >= control->_private_minimum_value &&
        value <= control->_private_maximum_value &&
        range_snap(control, value) == value)
        range_action(context, index, value, action);
}

static AstraResult text_position(const AstraControl *control, int32_t x, int32_t y,
                                  uint32_t *position, uint32_t *soft_end)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    FieldView view;
    int64_t frame_right = (int64_t)control->_private_frame.x +
                          control->_private_frame.width;
    int64_t relative;
    uint32_t columns;

    if (position == NULL || soft_end == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *soft_end = 0u;
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW) {
        AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
        AstraTextLine line = ASTRA_TEXT_LINE_INIT;
        int64_t row = ((int64_t)y - control->_private_frame.y);
        uint32_t column;
        if (position == NULL || astra_text_model_get_state(
                const_control_text_model(control), &state) != ASTRA_OK)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (row < 0) row = 0;
        row /= text_view_line_height(control);
        relative = (int64_t)x - control->_private_frame.x -
            text_view_gutter_width(control);
        columns = relative <= 0 ? 0u :
            (control->_private_style & TEXT_VIEW_FLOW) != 0u ?
                (uint32_t)relative : (uint32_t)(
                    (relative + theme.mono_cell_width / 2u) /
                    theme.mono_cell_width);
        if ((control->_private_style & TEXT_VIEW_WRAP) != 0u) {
            TextViewRow visual;
            int found = row > UINT32_MAX ? 0 : text_view_row_at(
                control, const_control_text_model(control), state.line_count,
                text_view_wrap_columns(control), (uint32_t)row, &visual);
            if (found < 0) return ASTRA_ERROR_INVALID_ARGUMENT;
            if (found == 0) {
                *position = state.text_bytes;
                return ASTRA_OK;
            }
            return text_view_segment_column(control, const_control_text_model(control),
                &visual, columns, position, soft_end) ? ASTRA_OK :
                ASTRA_ERROR_INVALID_ARGUMENT;
        }
        if ((uint64_t)row >= state.line_count) {
            *position = state.text_bytes; return ASTRA_OK;
        }
        return astra_text_model_get_line(const_control_text_model(control),
                    (uint32_t)row, &line) == ASTRA_OK &&
               text_view_column(control, const_control_text_model(control), &line, columns,
                                  position, &column) ?
            ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
    }
    if (position == NULL || !field_view(control, &view))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (x < control->_private_frame.x) {
        *position = 0u;
        return ASTRA_OK;
    }
    if ((int64_t)x >= frame_right) {
        *position = view.state.text_bytes;
        return ASTRA_OK;
    }
    relative = (int64_t)x - control->_private_frame.x -
               field_inset(control->_private_style);
    columns = relative <= 0 ? 0u :
        (uint32_t)((relative + theme.mono_cell_width / 2u) /
                   theme.mono_cell_width);
    if (columns > view.columns) columns = view.columns;
    if (!model_advance_scalars(const_control_text_model(control), view.start,
                               columns, position))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}

static int text_view_location(const AstraControl *control,
    const AstraTextModel *model, uint32_t offset,
    uint32_t lines, uint32_t *row, uint32_t *column, AstraTextLine *line)
{
    uint32_t low = 0u, high = lines;
    while (low + 1u < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (astra_text_model_get_line(model, middle, line) != ASTRA_OK) return 0;
        if (line->start <= offset) low = middle; else high = middle;
    }
    if (astra_text_model_get_line(model, low, line) != ASTRA_OK) return 0;
    *row = low; *column = 0u;
    uint32_t at = line->start;
    while (at < offset && at < line->content_end) {
        uint32_t scalar, width;
        if (!text_view_step(control, model, line->content_end, &at, *column,
                            &scalar, &width)) return 0;
        *column += width;
    }
    return 1;
}

static AstraResult text_selection(AstraUIContext *context, uint32_t index,
                                   uint32_t anchor, uint32_t focus,
                                   uint32_t soft_end,
                                   AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    AstraResult result;

    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        (control->_private_style & TEXT_VIEW_WRAP) == 0u)
        soft_end = 0u;
    if (state.selection.anchor == anchor && state.selection.focus == focus &&
        ((control->_private_dynamic_state & TEXT_VIEW_SOFT_END) != 0u) ==
            (soft_end != 0u))
        return ASTRA_OK;
    if (state.selection.anchor != anchor || state.selection.focus != focus) {
        selection.anchor = anchor;
        selection.focus = focus;
        result = astra_text_model_set_selection(control_text_model(control), &selection);
        if (result != ASTRA_OK) return result;
        if (control->_private_text_undo != NULL)
            ++control->_private_text_undo->_private_sequence;
    }
    if (soft_end != 0u)
        control->_private_dynamic_state |= TEXT_VIEW_SOFT_END;
    else
        control->_private_dynamic_state &= ~TEXT_VIEW_SOFT_END;
    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    private_u32_set(&control->_private_maximum_value, state.generation);
    control->_private_animation_phase = 0u;
    control_damage(context, index);
    action->type = ASTRA_UI_ACTION_SELECTION_CHANGED;
    action->control_id = control->_private_id;
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW) {
        AstraTextLine line = ASTRA_TEXT_LINE_INIT;
        uint32_t row, column;
        TextViewRow visual;
        int located = (control->_private_style & TEXT_VIEW_WRAP) != 0u ?
            text_view_visual_location(control, control_text_model(control),
                state.line_count, text_view_wrap_columns(control), focus,
                soft_end,
                &row, &column, &visual) :
            text_view_location(control, control_text_model(control), focus,
                state.line_count, &row, &column, &line);
        if (!located)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        return control_reveal(context, index,
            text_view_gutter_width(control) +
            column * text_view_unit_width(control),
            row * text_view_line_height(control), 1u,
            text_view_line_height(control));
    }
    return ASTRA_OK;
}

AstraResult astra_interface_text_view_reveal(AstraUIContext *context,
    AstraControl *control, uint32_t byte_offset)
{
    uint32_t index = control_index(context, control);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextLine line = ASTRA_TEXT_LINE_INIT;
    TextViewRow visual;
    uint32_t row, column, bytes;
    int located;
    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK ||
        astra_text_model_copy(control_text_model(control), byte_offset,
            byte_offset, NULL, 0u, &bytes) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    located = (control->_private_style & TEXT_VIEW_WRAP) != 0u ?
        text_view_visual_location(control, control_text_model(control),
            state.line_count, text_view_wrap_columns(control), byte_offset,
            0u, &row, &column, &visual) :
        text_view_location(control, control_text_model(control), byte_offset,
            state.line_count, &row, &column, &line);
    if (!located || column > UINT32_MAX / text_view_unit_width(control) ||
        row > UINT32_MAX / text_view_line_height(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return control_reveal(context, index,
        text_view_gutter_width(control) +
        column * text_view_unit_width(control),
        row * text_view_line_height(control), 1u,
        text_view_line_height(control));
}

AstraResult astra_interface_text_view_select(AstraUIContext *context,
    AstraControl *control, uint32_t anchor, uint32_t focus)
{
    uint32_t index = control_index(context, control);
    AstraUIAction ignored = ASTRA_UI_ACTION_INIT;
    AstraResult result;
    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = text_selection(context, index, anchor, focus, 0u, &ignored);
    if (result != ASTRA_OK) return result;
    return astra_interface_text_view_reveal(context, control, focus);
}

AstraResult astra_interface_text_view_find(AstraUIContext *context,
    AstraControl *control, const char *needle, uint32_t needle_bytes,
    uint32_t from, uint32_t wrap, AstraTextSelection *match)
{
    AstraTextSelection found = ASTRA_TEXT_SELECTION_INIT;
    AstraResult result;
    if (control_index(context, control) == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW ||
        match == NULL || match->size != sizeof(*match) ||
        !astra_words_zero(match->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_text_model_find(control_text_model(control), needle,
        needle_bytes, from, wrap, &found);
    if (result != ASTRA_OK) return result;
    result = astra_interface_text_view_select(context, control,
        found.anchor, found.focus);
    if (result == ASTRA_OK) *match = found;
    return result;
}

typedef struct TextUndoEdit {
    uint32_t start;
    uint32_t removed_bytes;
    uint32_t inserted_bytes;
    uint32_t before_anchor;
    uint32_t before_focus;
    uint32_t after_anchor;
    uint32_t after_focus;
} TextUndoEdit;

#define TEXT_UNDO_REPLACE 1u
#define TEXT_UNDO_COALESCE_TYPE 1u
#define TEXT_UNDO_COALESCE_BACKSPACE 2u
#define TEXT_UNDO_COALESCE_DELETE 3u

static int text_undo_ranges_overlap(const void *left, uint32_t left_bytes,
                                    const void *right, uint32_t right_bytes)
{
    uintptr_t a = (uintptr_t)left;
    uintptr_t b = (uintptr_t)right;

    if (left_bytes == 0u || right_bytes == 0u) return 0;
    if (a > UINTPTR_MAX - left_bytes || b > UINTPTR_MAX - right_bytes)
        return 1;
    return a < b + right_bytes && b < a + left_bytes;
}

static int text_undo_binding_valid(const AstraTextUndoBinding *binding)
{
    AstraUndoState state = ASTRA_UNDO_STATE_INIT;
    const AstraTextModel *model;

    if (binding == NULL ||
        binding->_private_structure_size != sizeof(*binding) ||
        binding->_private_context == NULL ||
        binding->_private_control == NULL ||
        binding->_private_manager == NULL ||
        binding->_private_scratch == NULL ||
        binding->_private_scratch_bytes < sizeof(TextUndoEdit) ||
        !astra_words_zero(binding->_private_reserved, 3u) ||
        binding->_private_control->_private_text_undo != binding ||
        control_index(binding->_private_context,
                      binding->_private_control) == CONTROL_NONE ||
        astra_undo_get_state(binding->_private_manager, &state) != ASTRA_OK)
        return 0;
    model = const_control_text_model(binding->_private_control);
    return !text_undo_ranges_overlap(binding->_private_scratch,
            binding->_private_scratch_bytes, model->_private_content,
            model->_private_content_capacity) &&
        !text_undo_ranges_overlap(binding->_private_scratch,
            binding->_private_scratch_bytes, model->_private_metadata,
            model->_private_metadata_capacity) &&
        !text_undo_ranges_overlap(binding->_private_manager->_private_arena,
            binding->_private_manager->_private_capacity,
            model->_private_content, model->_private_content_capacity) &&
        !text_undo_ranges_overlap(binding->_private_manager->_private_arena,
            binding->_private_manager->_private_capacity,
            model->_private_metadata, model->_private_metadata_capacity);
}

static int text_undo_span_equal(const AstraTextModel *model, uint32_t start,
                                const char *expected, uint32_t bytes)
{
    uint32_t offset = start;
    uint32_t compared = 0u;

    while (compared != bytes) {
        const char *text = NULL;
        uint32_t available = 0u;
        uint32_t count;

        if (astra_text_model_read(model, offset, &text, &available) !=
                ASTRA_OK || available == 0u || text == NULL)
            return 0;
        count = available < bytes - compared ? available : bytes - compared;
        if (memcmp(text, expected + compared, count) != 0) return 0;
        offset += count;
        compared += count;
    }
    return 1;
}

static AstraResult text_undo_apply(void *opaque, uint32_t operation,
                                   uint32_t direction, const void *payload,
                                   uint32_t payload_bytes)
{
    AstraTextUndoBinding *binding = opaque;
    TextUndoEdit edit;
    AstraTextModel *model;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    const char *old_text;
    const char *new_text;
    const char *expected;
    const char *replacement;
    uint32_t expected_bytes;
    uint32_t replacement_bytes;

    if (!text_undo_binding_valid(binding) ||
        operation != TEXT_UNDO_REPLACE ||
        (direction != ASTRA_UNDO_DIRECTION_UNDO &&
         direction != ASTRA_UNDO_DIRECTION_REDO) ||
        payload == NULL || payload_bytes < sizeof(edit))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((binding->_private_control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_ERROR_PERMISSION;
    memcpy(&edit, payload, sizeof(edit));
    if ((uint64_t)sizeof(edit) + edit.removed_bytes + edit.inserted_bytes !=
        payload_bytes) return ASTRA_ERROR_INVALID_ARGUMENT;
    old_text = (const char *)payload + sizeof(edit);
    new_text = old_text + edit.removed_bytes;
    expected = direction == ASTRA_UNDO_DIRECTION_UNDO ? new_text : old_text;
    expected_bytes = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        edit.inserted_bytes : edit.removed_bytes;
    replacement = direction == ASTRA_UNDO_DIRECTION_UNDO ? old_text : new_text;
    replacement_bytes = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        edit.removed_bytes : edit.inserted_bytes;
    model = control_text_model(binding->_private_control);
    if (astra_text_model_get_state(model, &state) != ASTRA_OK ||
        edit.start > state.text_bytes ||
        expected_bytes > state.text_bytes - edit.start ||
        !text_undo_span_equal(model, edit.start, expected, expected_bytes))
        return ASTRA_ERROR_BUSY;
    selection.anchor = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        edit.before_anchor : edit.after_anchor;
    selection.focus = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        edit.before_focus : edit.after_focus;
    if ((uint64_t)state.text_bytes - expected_bytes + replacement_bytes >
            UINT32_MAX ||
        selection.anchor > (uint64_t)state.text_bytes - expected_bytes +
                               replacement_bytes ||
        selection.focus > (uint64_t)state.text_bytes - expected_bytes +
                              replacement_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_text_model_replace(model, edit.start,
        edit.start + expected_bytes, replacement, replacement_bytes);
    if (result != ASTRA_OK) return result;
    return astra_text_model_set_selection(model, &selection);
}

AstraResult astra_interface_text_undo_bind(AstraTextUndoBinding *binding,
    AstraUIContext *context, AstraControl *control, AstraUndoManager *manager,
    void *history_arena, uint32_t history_bytes,
    void *scratch, uint32_t scratch_bytes)
{
    AstraUndoManagerInfo info = ASTRA_UNDO_MANAGER_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraResult result;

    if (binding == NULL || binding->_private_structure_size !=
            sizeof(*binding) || binding->_private_context != NULL ||
        !astra_words_zero(binding->_private_reserved, 3u) ||
        control_index(context, control) == CONTROL_NONE ||
        (control->_private_control_kind != ASTRA_CONTROL_FIELD &&
         control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW) ||
        (control->_private_style & (ASTRA_FIELD_READ_ONLY |
                                    ASTRA_FIELD_SECURE)) != 0u ||
        control->_private_text_undo != NULL || manager == NULL ||
        history_arena == NULL || history_bytes == 0u ||
        scratch == NULL || scratch_bytes < sizeof(TextUndoEdit) ||
        text_undo_ranges_overlap(history_arena, history_bytes,
                                 binding, sizeof(*binding)) ||
        text_undo_ranges_overlap(history_arena, history_bytes,
                                 manager, sizeof(*manager)) ||
        text_undo_ranges_overlap(history_arena, history_bytes,
                                 control, sizeof(*control)) ||
        text_undo_ranges_overlap(scratch, scratch_bytes,
                                 binding, sizeof(*binding)) ||
        text_undo_ranges_overlap(scratch, scratch_bytes,
                                 manager, sizeof(*manager)) ||
        text_undo_ranges_overlap(scratch, scratch_bytes,
                                 control, sizeof(*control)) ||
        text_undo_ranges_overlap(history_arena, history_bytes,
                                 scratch, scratch_bytes) ||
        astra_text_model_get_state(control_text_model(control), &state) !=
            ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (text_undo_ranges_overlap(history_arena, history_bytes,
            control_text_model(control)->_private_content,
            control_text_model(control)->_private_content_capacity) ||
        text_undo_ranges_overlap(history_arena, history_bytes,
            control_text_model(control)->_private_metadata,
            control_text_model(control)->_private_metadata_capacity) ||
        text_undo_ranges_overlap(scratch, scratch_bytes,
            control_text_model(control)->_private_content,
            control_text_model(control)->_private_content_capacity) ||
        text_undo_ranges_overlap(scratch, scratch_bytes,
            control_text_model(control)->_private_metadata,
            control_text_model(control)->_private_metadata_capacity))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    binding->_private_context = context;
    binding->_private_control = control;
    binding->_private_manager = manager;
    binding->_private_scratch = scratch;
    binding->_private_scratch_bytes = scratch_bytes;
    binding->_private_sequence = 1u;
    info.arena = history_arena;
    info.arena_bytes = history_bytes;
    info.coalesce_interval_ns = UINT64_C(750000000);
    info.apply = text_undo_apply;
    info.context = binding;
    result = astra_undo_init(manager, &info);
    if (result != ASTRA_OK) {
        *binding = (AstraTextUndoBinding)ASTRA_TEXT_UNDO_BINDING_INIT;
        return result;
    }
    control->_private_text_undo = binding;
    return ASTRA_OK;
}

AstraResult astra_interface_text_undo_unbind(AstraTextUndoBinding *binding)
{
    AstraResult result;

    if (!text_undo_binding_valid(binding))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_undo_clear(binding->_private_manager);
    if (result != ASTRA_OK) return result;
    memset(binding->_private_scratch, 0, binding->_private_scratch_bytes);
    binding->_private_control->_private_text_undo = NULL;
    *binding->_private_manager = (AstraUndoManager)ASTRA_UNDO_MANAGER_INIT;
    *binding = (AstraTextUndoBinding)ASTRA_TEXT_UNDO_BINDING_INIT;
    return ASTRA_OK;
}

static AstraResult text_undo_refresh(AstraTextUndoBinding *binding)
{
    AstraControl *control = binding->_private_control;
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW)
        return astra_interface_text_view_refresh(binding->_private_context,
                                                 control);
    return astra_interface_field_refresh(binding->_private_context, control);
}

AstraResult astra_interface_text_undo_undo(AstraTextUndoBinding *binding)
{
    AstraResult result;
    if (!text_undo_binding_valid(binding))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_undo_undo(binding->_private_manager);
    return result == ASTRA_OK ? text_undo_refresh(binding) : result;
}

AstraResult astra_interface_text_undo_redo(AstraTextUndoBinding *binding)
{
    AstraResult result;
    if (!text_undo_binding_valid(binding))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_undo_redo(binding->_private_manager);
    return result == ASTRA_OK ? text_undo_refresh(binding) : result;
}

static AstraResult text_undo_perform_edit(AstraTextUndoBinding *binding,
    uint32_t start, uint32_t end, AstraTextSelection before,
    const char *replacement, uint32_t replacement_bytes,
    uint32_t timestamp_ms, uint32_t coalesce_kind)
{
    static const char typing_name[] = "Typing";
    static const char delete_name[] = "Delete";
    static const char replace_name[] = "Replace";
    AstraUndoAction action = ASTRA_UNDO_ACTION_INIT;
    AstraUndoGroupInfo group = ASTRA_UNDO_GROUP_INFO_INIT;
    TextUndoEdit edit = {0};
    uint32_t copied = 0u;
    uint64_t payload_bytes;
    uint8_t *scratch;

    if (!text_undo_binding_valid(binding) || start > end ||
        start > UINT32_MAX - replacement_bytes ||
        (replacement_bytes != 0u && replacement == NULL))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    payload_bytes = (uint64_t)sizeof(edit) + (end - start) +
                    replacement_bytes;
    if (payload_bytes > binding->_private_scratch_bytes ||
        payload_bytes > UINT32_MAX ||
        text_undo_ranges_overlap(binding->_private_scratch,
            binding->_private_scratch_bytes, replacement, replacement_bytes))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    edit.start = start;
    edit.removed_bytes = end - start;
    edit.inserted_bytes = replacement_bytes;
    edit.before_anchor = before.anchor;
    edit.before_focus = before.focus;
    edit.after_anchor = start + replacement_bytes;
    edit.after_focus = edit.after_anchor;
    scratch = binding->_private_scratch;
    memcpy(scratch, &edit, sizeof(edit));
    AstraResult result = astra_text_model_copy(
        control_text_model(binding->_private_control), start, end,
        (char *)scratch + sizeof(edit), end - start, &copied);
    if (result != ASTRA_OK) return result;
    if (copied != end - start) return ASTRA_ERROR_IO;
    if (replacement_bytes != 0u)
        memcpy(scratch + sizeof(edit) + copied, replacement,
               replacement_bytes);
    action.operation = TEXT_UNDO_REPLACE;
    action.payload = scratch;
    action.payload_bytes = (uint32_t)payload_bytes;
    group.actions = &action;
    group.action_count = 1u;
    group.name = coalesce_kind == TEXT_UNDO_COALESCE_TYPE ? typing_name :
        (coalesce_kind != 0u ? delete_name : replace_name);
    group.name_length = (uint32_t)strlen(group.name);
    group.timestamp_ns = (uint64_t)timestamp_ms * UINT64_C(1000000);
    if (coalesce_kind != 0u && before.anchor == before.focus) {
        group.coalesce_id = (binding->_private_sequence << 2) |
                            coalesce_kind;
        if (group.coalesce_id == 0u) group.coalesce_id = coalesce_kind;
    }
    return astra_undo_perform_group(binding->_private_manager, &group);
}

static AstraResult field_replace_range_internal(
    AstraUIContext *context, uint32_t index, uint32_t start, uint32_t end,
    AstraTextSelection before, const char *replacement,
    uint32_t replacement_bytes, uint32_t timestamp_ms,
    uint32_t coalesce_kind, AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraResult result;

    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (start > end || end > state.text_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_text_undo != NULL)
        result = text_undo_perform_edit(control->_private_text_undo,
            start, end, before, replacement, replacement_bytes,
            timestamp_ms, coalesce_kind);
    else
        result = astra_text_model_replace(control_text_model(control),
            start, end, replacement, replacement_bytes);
    if (result != ASTRA_OK)
        return result;
    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    private_u32_set(&control->_private_maximum_value, state.generation);
    control->_private_animation_phase = 0u;
    control_damage(context, index);
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW) {
        result = astra_interface_text_view_refresh(context, control);
        if (result != ASTRA_OK) return result;
        result = astra_interface_text_view_reveal(context, control,
            state.selection.focus);
        if (result != ASTRA_OK) return result;
    }
    action->type = ASTRA_UI_ACTION_TEXT_CHANGED;
    action->control_id = control->_private_id;
    return ASTRA_OK;
}

static AstraResult field_replace_selection_internal(
    AstraUIContext *context, uint32_t index, const char *replacement,
    uint32_t replacement_bytes, uint32_t timestamp_ms,
    uint32_t coalesce_kind, AstraUIAction *action)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t start;
    uint32_t end;

    if (astra_text_model_get_state(control_text_model(
            &context->_private_controls[index]), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    return field_replace_range_internal(context, index, start, end,
        state.selection, replacement, replacement_bytes, timestamp_ms,
        coalesce_kind, action);
}

AstraResult astra_interface_field_replace_selection(
    AstraUIContext *context, AstraControl *control,
    const char *replacement, uint32_t replacement_bytes)
{
    uint32_t index = control_index(context, control);
    AstraUIAction ignored = ASTRA_UI_ACTION_INIT;

    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_FIELD ||
        !astra_interface_single_line_text_valid(replacement, replacement_bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_ERROR_PERMISSION;
    return field_replace_selection_internal(
        context, index, replacement, replacement_bytes, 0u, 0u, &ignored);
}

AstraResult astra_interface_text_view_replace_selection(
    AstraUIContext *context, AstraControl *control,
    const char *replacement, uint32_t replacement_bytes)
{
    uint32_t index = control_index(context, control);
    AstraUIAction ignored = ASTRA_UI_ACTION_INIT;
    if (index == CONTROL_NONE ||
        control->_private_control_kind != ASTRA_CONTROL_TEXT_VIEW)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_ERROR_PERMISSION;
    return field_replace_selection_internal(context, index,
        replacement, replacement_bytes, 0u, 0u, &ignored);
}

static AstraResult field_delete(AstraUIContext *context, uint32_t index,
                                int backwards, uint32_t timestamp_ms,
                                AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t start;
    uint32_t end;
    AstraResult result;

    if ((control->_private_style & ASTRA_FIELD_READ_ONLY) != 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    start = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    end = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    if (start == end) {
        if (backwards) {
            result = astra_text_model_scalar_retreat(control_text_model(control),
                                                     &start);
        } else {
            result = astra_text_model_scalar_advance(control_text_model(control),
                                                     &end);
        }
        if (result == ASTRA_ERROR_NOT_PRESENT)
            return ASTRA_OK;
        if (result != ASTRA_OK)
            return result;
    }
    return field_replace_range_internal(context, index, start, end,
        state.selection, NULL, 0u, timestamp_ms,
        backwards ? TEXT_UNDO_COALESCE_BACKSPACE :
                    TEXT_UNDO_COALESCE_DELETE, action);
}

static AstraResult text_key(AstraUIContext *context, uint32_t index,
                             const AstraWindowEvent *event,
                             AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    uint32_t usage = event->data.key.usage;
    uint32_t modifiers = event->data.key.modifiers;
    uint32_t anchor;
    uint32_t focus;
    int shift = (modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u;
    AstraResult result;

    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u)
        return ASTRA_OK;
    if (astra_text_model_get_state(control_text_model(control), &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD &&
        usage == 0x29u &&
        (control->_private_style & ASTRA_FIELD_SEARCH) != 0u &&
        state.text_bytes != 0u) {
        result = text_selection(context, index, 0u, state.text_bytes,
                                0u, action);
        if (result != ASTRA_OK) return result;
        return field_replace_selection_internal(context, index, NULL, 0u,
                                                event->timestamp_ms, 0u,
                                                action);
    }
    if ((modifiers & ASTRA_INPUT_MOD_META) != 0u &&
        (modifiers & ASTRA_INPUT_MOD_CTRL) == 0u) {
        if (usage == 0x1du && control->_private_text_undo != NULL &&
            (control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u) {
            result = shift ? astra_interface_text_undo_redo(
                control->_private_text_undo) : astra_interface_text_undo_undo(
                control->_private_text_undo);
            if (result == ASTRA_ERROR_NOT_PRESENT) return ASTRA_OK;
            if (result != ASTRA_OK) return result;
            action->type = ASTRA_UI_ACTION_TEXT_CHANGED;
            action->control_id = control->_private_id;
            return ASTRA_OK;
        }
        if (usage == 0x04u)
            return text_selection(context, index, 0u, state.text_bytes,
                                   0u, action);
        if (usage == 0x06u &&
            state.selection.anchor != state.selection.focus &&
            (control->_private_control_kind != ASTRA_CONTROL_FIELD ||
             (control->_private_style & ASTRA_FIELD_SECURE) == 0u)) {
            action->type = ASTRA_UI_ACTION_COPY;
            action->control_id = control->_private_id;
        } else if (usage == 0x1bu &&
                   state.selection.anchor != state.selection.focus &&
                   (control->_private_style & (ASTRA_FIELD_READ_ONLY |
                                              ASTRA_FIELD_SECURE)) == 0u) {
            action->type = ASTRA_UI_ACTION_CUT;
            action->control_id = control->_private_id;
        } else if (usage == 0x19u &&
                   (control->_private_style & ASTRA_FIELD_READ_ONLY) == 0u) {
            action->type = ASTRA_UI_ACTION_PASTE;
            action->control_id = control->_private_id;
        }
        return ASTRA_OK;
    }
    if (usage == 0x28u || usage == 0x58u) {
        if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW)
            return field_replace_selection_internal(context, index,
                "\n", 1u, event->timestamp_ms, 0u, action);
        if ((event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u) {
            action->type = ASTRA_UI_ACTION_ACTIVATE;
            action->control_id = control->_private_id;
        }
        return ASTRA_OK;
    }
    if (usage == 0x2au)
        return field_delete(context, index, 1, event->timestamp_ms, action);
    if (usage == 0x4cu)
        return field_delete(context, index, 0, event->timestamp_ms, action);
    anchor = state.selection.anchor;
    focus = state.selection.focus;
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW &&
        (usage == 0x4au || usage == 0x4du || usage == 0x52u || usage == 0x51u ||
         usage == 0x4bu || usage == 0x4eu)) {
        AstraTextLine line = ASTRA_TEXT_LINE_INIT;
        uint32_t row, column, ignored;
        if ((control->_private_style & TEXT_VIEW_WRAP) != 0u) {
            TextViewRow visual;
            AstraControlSize size;
            uint32_t wrap = text_view_wrap_columns(control);
            uint32_t soft_end = 0u;
            if (!text_view_visual_location(control, control_text_model(control),
                    state.line_count, wrap, focus,
                    (control->_private_dynamic_state & TEXT_VIEW_SOFT_END) != 0u,
                    &row, &column, &visual))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            if (usage == 0x4au || usage == 0x4du) {
                focus = (modifiers & ASTRA_INPUT_MOD_CTRL) != 0u ?
                    (usage == 0x4au ? 0u : state.text_bytes) :
                    (usage == 0x4au ? visual.start : visual.end);
                soft_end = (modifiers & ASTRA_INPUT_MOD_CTRL) == 0u &&
                    usage == 0x4du && visual.end != visual.hard_end;
            } else {
                uint32_t steps = usage == 0x4bu || usage == 0x4eu ?
                    control->_private_clip.height /
                        text_view_line_height(control) : 1u;
                if (steps == 0u) steps = 1u;
                if (text_view_measure(control, &size) != ASTRA_OK)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                uint32_t count = size.height / text_view_line_height(control);
                if (usage == 0x52u || usage == 0x4bu)
                    row = row < steps ? 0u : row - steps;
                else row += steps < count - row ? steps : count - row - 1u;
                if (text_view_row_at(control, control_text_model(control),
                        state.line_count, wrap, row, &visual) != 1 ||
                    !text_view_segment_column(control, control_text_model(control),
                        &visual, column, &focus, &soft_end))
                    return ASTRA_ERROR_INVALID_ARGUMENT;
            }
            return text_selection(context, index, shift ? anchor : focus,
                                  focus, soft_end, action);
        }
        if (!text_view_location(control, control_text_model(control), focus, state.line_count,
                                 &row, &column, &line))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (usage == 0x4au || usage == 0x4du) {
            if ((modifiers & ASTRA_INPUT_MOD_CTRL) != 0u)
                focus = usage == 0x4au ? 0u : state.text_bytes;
            else if (!text_view_column(control, control_text_model(control), &line,
                usage == 0x4au ? 0u : UINT32_MAX, &focus, &ignored))
                return ASTRA_ERROR_INVALID_ARGUMENT;
        } else {
            uint32_t rows = usage == 0x4bu || usage == 0x4eu ?
                control->_private_clip.height /
                    text_view_line_height(control) : 1u;
            if (rows == 0u) rows = 1u;
            if (usage == 0x52u || usage == 0x4bu)
                row = row < rows ? 0u : row - rows;
            else row += rows < state.line_count - row ? rows :
                         state.line_count - row - 1u;
            if (astra_text_model_get_line(control_text_model(control), row, &line) !=
                    ASTRA_OK || !text_view_column(control, control_text_model(control), &line,
                        column, &focus, &ignored))
                return ASTRA_ERROR_INVALID_ARGUMENT;
        }
    } else if (usage == 0x4au)
        focus = 0u;
    else if (usage == 0x4du)
        focus = state.text_bytes;
    else if (usage == 0x50u) {
        if (!shift && anchor != focus)
            focus = anchor < focus ? anchor : focus;
        else {
            result = astra_text_model_scalar_retreat(control_text_model(control),
                                                     &focus);
            if (result != ASTRA_OK && result != ASTRA_ERROR_NOT_PRESENT)
                return result;
        }
    } else if (usage == 0x4fu) {
        if (!shift && anchor != focus)
            focus = anchor > focus ? anchor : focus;
        else {
            result = astra_text_model_scalar_advance(control_text_model(control),
                                                     &focus);
            if (result != ASTRA_OK && result != ASTRA_ERROR_NOT_PRESENT)
                return result;
        }
    } else {
        return ASTRA_OK;
    }
    return text_selection(context, index, shift ? anchor : focus,
                           focus, 0u, action);
}

static AstraResult field_text(AstraUIContext *context, uint32_t index,
                              const AstraWindowEvent *event,
                              AstraUIAction *action)
{
    uint32_t scalar = event->data.text.codepoint;
    char encoded[4];
    uint32_t bytes;

    if (!astra_unicode_scalar_valid(scalar))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((scalar < 0x20u &&
         !(scalar == '\n' && context->_private_controls[index]
            ._private_control_kind == ASTRA_CONTROL_TEXT_VIEW)) ||
        (scalar >= 0x7fu && scalar <= 0x9fu) ||
        scalar == 0x2028u || scalar == 0x2029u)
        return ASTRA_OK;
    bytes = astra_utf8_encode(scalar, encoded);
    return field_replace_selection_internal(
        context, index, encoded, bytes, event->timestamp_ms,
        TEXT_UNDO_COALESCE_TYPE, action);
}

typedef struct ScrollCopy {
    uint32_t view;
    AstraControlFrame viewport;
    AstraControlFrame exposed[2];
    uint32_t exposed_count;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
} ScrollCopy;

static int scroll_copy_prepare(const AstraUIContext *context,
                               const AstraSurfaceView *surface,
                               ScrollCopy *copy)
{
    const AstraDrawListHeader *header;
    const AstraControl *view;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    uint32_t encoded = context->_private_reserved &
                       SCROLL_DAMAGE_INDEX_MASK;
    uint32_t old_x;
    uint32_t old_y;
    int64_t dx;
    int64_t dy;
    uint32_t shift_x;
    uint32_t shift_y;

    *copy = (ScrollCopy){0};
    if (surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST || encoded == 0u ||
        context->_private_reserved == SCROLL_DAMAGE_INVALID ||
        (context->_private_reserved & SCROLL_DAMAGE_RECORDING) != 0u)
        return 0;
    header = (const AstraDrawListHeader *)(const void *)surface->pixels;
    if (header->command_count != 0u)
        return 0;
    copy->view = encoded - 1u;
    if (copy->view >= context->_private_control_count)
        return 0;
    view = &context->_private_controls[copy->view];
    if (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
        view->_private_laid_out == 0u || view->_private_value_step == 0 ||
        astra_scroll_get_state(const_scroll_model(view), &state) != ASTRA_OK)
        return 0;
    old_x = private_u32(&view->_private_minimum_value);
    old_y = private_u32(&view->_private_maximum_value);
    dx = (int64_t)state.offset_x - old_x;
    dy = (int64_t)state.offset_y - old_y;
    if (dx == 0 && dy == 0)
        return 0;
    copy->viewport = frame_intersection(&view->_private_frame,
                                        &view->_private_clip);
    shift_x = dx < 0 ? (uint32_t)-dx : (uint32_t)dx;
    shift_y = dy < 0 ? (uint32_t)-dy : (uint32_t)dy;
    if (shift_x >= copy->viewport.width ||
        shift_y >= copy->viewport.height)
        return 0;
    copy->source_x = (uint32_t)copy->viewport.x + (dx > 0 ? shift_x : 0u);
    copy->source_y = (uint32_t)copy->viewport.y + (dy > 0 ? shift_y : 0u);
    copy->destination_x = (uint32_t)copy->viewport.x +
                          (dx < 0 ? shift_x : 0u);
    copy->destination_y = (uint32_t)copy->viewport.y +
                          (dy < 0 ? shift_y : 0u);
    copy->width = copy->viewport.width - shift_x;
    copy->height = copy->viewport.height - shift_y;
    if (shift_x != 0u) {
        copy->exposed[copy->exposed_count++] = (AstraControlFrame){
            copy->viewport.x + (dx > 0 ?
                (int32_t)(copy->viewport.width - shift_x) : 0),
            copy->viewport.y, shift_x, copy->viewport.height};
    }
    if (shift_y != 0u) {
        copy->exposed[copy->exposed_count++] = (AstraControlFrame){
            copy->viewport.x,
            copy->viewport.y + (dy > 0 ?
                (int32_t)(copy->viewport.height - shift_y) : 0),
            copy->viewport.width, shift_y};
    }
    return 1;
}

static void render_splitter(const AstraTheme *theme,
                            const AstraControl *control,
                            AstraSurfaceView *surface)
{
    const AstraControlFrame *frame = &control->_private_frame;
    uint32_t state = effective_state(control);
    uint32_t thickness =
        (state & (ASTRA_CONTROL_PRESSED | CONTROL_FOCUS_VISIBLE)) != 0u ?
            2u : 1u;
    AstraColorRGBA8 line = theme->client_border;

    if ((state & ASTRA_CONTROL_DISABLED) != 0u)
        line = theme->control_disabled;
    else if ((state & (ASTRA_CONTROL_PRESSED |
                       CONTROL_FOCUS_VISIBLE)) != 0u)
        line = theme->accent;
    else if ((state & ASTRA_CONTROL_HOVERED) != 0u)
        line = theme->border_emphasis;
    if (control->_private_style == ASTRA_ORIENTATION_VERTICAL)
        astra_surface_fill(surface,
                           frame->x + (int32_t)(frame->width - thickness) / 2,
                           frame->y, thickness, frame->height, color(line));
    else
        astra_surface_fill(surface, frame->x,
                           frame->y +
                               (int32_t)(frame->height - thickness) / 2,
                           frame->width, thickness, color(line));
}

static AstraResult render_one_control(const AstraUIContext *context,
                                      uint32_t index,
                                      const AstraTheme *theme,
                                      const AstraControl *control,
                                      AstraSurfaceView *surface)
{
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
        control->_private_style == ASTRA_CONTAINER_STYLE_TOOLBAR) {
        const AstraControlFrame *frame = &control->_private_frame;
        astra_surface_fill(surface, frame->x, frame->y,
                           frame->width, frame->height,
                           color(theme->surface_inset));
        astra_surface_fill(surface, frame->x,
                           frame->y + (int32_t)frame->height - 1,
                           frame->width, 1u, color(theme->client_border));
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
        control->_private_style == ASTRA_CONTAINER_STYLE_STATUS_BAR) {
        const AstraControlFrame *frame = &control->_private_frame;

        astra_surface_fill(surface, frame->x, frame->y,
                           frame->width, frame->height,
                           color(theme->surface_inset));
        astra_surface_fill(surface, frame->x, frame->y,
                           frame->width, 1u,
                           color(theme->client_border));
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
        control->_private_style == ASTRA_CONTAINER_STYLE_INSPECTOR) {
        const AstraControlFrame *frame = &control->_private_frame;
        astra_surface_fill(surface, frame->x, frame->y,
                           frame->width, frame->height,
                           color(theme->surface_raised));
        astra_surface_fill(surface, frame->x, frame->y,
                           1u, frame->height, color(theme->client_border));
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
        control->_private_style == ASTRA_CONTAINER_STYLE_TAG_INPUT) {
        const AstraControlFrame *frame = &control->_private_frame;

        astra_surface_fill_round(surface, frame->x, frame->y,
            frame->width, frame->height, theme->control_radius,
            color(theme->client_border));
        if (frame->width > 2u && frame->height > 2u)
            astra_surface_fill_round(surface, frame->x + 1, frame->y + 1,
                frame->width - 2u, frame->height - 2u,
                theme->control_radius - 1u, color(theme->surface_raised));
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
        control->_private_style == ASTRA_CONTAINER_STYLE_DROP_TARGET) {
        const AstraControlFrame *frame = &control->_private_frame;
        uint32_t visual = (uint32_t)control->_private_value;
        AstraColorRGBA8 border = visual == ASTRA_DROP_TARGET_ACCEPT ?
            theme->accent : theme->client_border;
        AstraColorRGBA8 background = visual == ASTRA_DROP_TARGET_ACCEPT ?
            theme->surface_inset : theme->surface_raised;
        AstraSurfaceLine lines[192];
        uint32_t count = drop_target_border(frame, lines);

        astra_surface_fill_round(surface, frame->x, frame->y,
            frame->width, frame->height, theme->control_radius,
            color(background));
        if (count != 0u && !astra_surface_lines(surface, lines, count,
                                                color(border)))
            return ASTRA_ERROR_NO_RESOURCES;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_TEXT_VIEW)
        return text_view_render(control, surface, NULL, NULL);
    if (control->_private_control_kind == ASTRA_CONTROL_GRID_VIEW)
        return grid_view_render(control, surface, NULL, NULL);
    if (control->_private_control_kind == ASTRA_CONTROL_VIEW) {
        const AstraViewModel *model = view_model(control);
        AstraDrawListHeader *list = (AstraDrawListHeader *)surface->pixels;
        uint32_t old_commands = list->command_count;
        uint32_t old_payload = list->payload_bytes;
        uint64_t commands = 0u, payload = 0u;
        AstraResult result = model->draw(model->context,
            &control->_private_frame, NULL, &commands, &payload);

        if (result != ASTRA_OK) return result;
        result = model->draw(model->context, &control->_private_frame,
                             surface, NULL, NULL);
        if (result != ASTRA_OK) return result;
        return list->command_count < old_commands ||
               list->payload_bytes < old_payload ||
               list->command_count - old_commands > commands ||
               list->payload_bytes - old_payload > payload ?
            ASTRA_ERROR_INVALID_ARGUMENT : ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_LEVEL_METER ||
        control->_private_control_kind == ASTRA_CONTROL_SPARKLINE)
        return astra_interface_display_render(control, theme, surface, NULL);
    if (control->_private_control_kind == ASTRA_CONTROL_FIELD)
        return render_field(context, theme, control, surface);
    if (control->_private_control_kind == ASTRA_CONTROL_LABEL)
        return render_label(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_BUTTON)
        render_button(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_COLOUR_WELL)
        render_colour_well(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE)
        render_disclosure(context, theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SLIDER)
        render_slider(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_DIAL)
        render_dial(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR)
        render_scrollbar(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        render_splitter(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER)
        return render_stepper(context, index, theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS)
        render_progress(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_SEGMENTED)
        render_segmented(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_TAB)
        render_tab(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_LIST)
        render_list(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_POPUP)
        render_popup(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_TREE)
        render_tree(theme, control, surface);
    else if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID)
        return render_icon_grid(theme, control, surface);
    else if (TABLE_KINDS(control->_private_control_kind))
        render_table(theme, control, surface);
    else
        render_toggle(theme, control, surface);
    return ASTRA_OK;
}

static void render_finish(AstraUIContext *context)
{
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        AstraControl *control = &context->_private_controls[at];
        AstraScrollState state = ASTRA_SCROLL_STATE_INIT;

        if (control->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW ||
            astra_scroll_get_state(scroll_model(control), &state) != ASTRA_OK)
            continue;
        private_u32_set(&control->_private_minimum_value, state.offset_x);
        private_u32_set(&control->_private_maximum_value, state.offset_y);
        control->_private_value_step = 1;
    }
    context->_private_reserved = 0u;
}

AstraResult astra_interface_ui_render(AstraUIContext *context,
                                      AstraSurfaceView *surface)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraSurfaceView validated;
    uint64_t commands;
    uint64_t payload;
    ScrollCopy copy;
    int accelerated;

    if (!context_valid(context) || surface == NULL || surface->pixels == NULL ||
        surface->width != context->_private_width ||
        surface->height != context->_private_height ||
        surface->clip_left > surface->clip_right ||
        surface->clip_right > surface->width ||
        surface->clip_top > surface->clip_bottom ||
        surface->clip_bottom > surface->height ||
        surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        !render_requirements(context, 0u, context->_private_control_count,
                             &commands, &payload))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_draw_list_view_adopt(&validated, surface->pixels,
                                    surface->byte_size, surface->width,
                                    surface->height))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    accelerated = scroll_copy_prepare(context, surface, &copy);
    if (!astra_interface_draw_list_has_room(surface,
                            commands + (accelerated ?
                                1u + copy.exposed_count : 0u), payload))
        return ASTRA_ERROR_NO_RESOURCES;
    validated = *surface;
    if (accelerated) {
        AstraTheme background = ASTRA_THEME_SYSTEM_INIT;

        if (!astra_draw_list_copy(&validated, copy.source_x, copy.source_y,
                                  copy.destination_x, copy.destination_y,
                                  copy.width, copy.height))
            return ASTRA_ERROR_NO_RESOURCES;
        for (uint32_t at = 0u; at < copy.exposed_count; ++at)
            astra_surface_fill(&validated, copy.exposed[at].x,
                               copy.exposed[at].y,
                               copy.exposed[at].width,
                               copy.exposed[at].height,
                               color(background.client));
    }
    for (uint32_t at = 0u; at < context->_private_control_count; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        AstraSurfaceView clipped;
        AstraResult result;

        if (control->_private_laid_out == 0u ||
            (CONTAINER_KINDS(control->_private_control_kind) &&
             !(control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
               (control->_private_style == ASTRA_CONTAINER_STYLE_TOOLBAR ||
                control->_private_style == ASTRA_CONTAINER_STYLE_STATUS_BAR ||
                control->_private_style == ASTRA_CONTAINER_STYLE_TAG_INPUT ||
                control->_private_style == ASTRA_CONTAINER_STYLE_DROP_TARGET ||
                control->_private_style == ASTRA_CONTAINER_STYLE_INSPECTOR))) ||
            control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        if (accelerated &&
            control->_private_control_kind == ASTRA_CONTROL_SCROLLBAR &&
            const_scroll_model(control) ==
                const_scroll_model(&context->_private_controls[copy.view])) {
            clipped = validated;
            if (!astra_surface_clip(&clipped, control->_private_clip.x,
                                    control->_private_clip.y,
                                    control->_private_clip.width,
                                    control->_private_clip.height))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            result = render_one_control(context, at, &theme, control,
                                        &clipped);
            if (result != ASTRA_OK) return result;
            continue;
        }
        if (accelerated) {
            if (!control_descends_from(context, at, copy.view))
                continue;
            for (uint32_t strip = 0u; strip < copy.exposed_count; ++strip) {
                AstraControlFrame visible = frame_intersection(
                    &control->_private_clip, &copy.exposed[strip]);

                if (visible.width == 0u || visible.height == 0u)
                    continue;
                clipped = validated;
                if (!astra_surface_clip(&clipped, visible.x, visible.y,
                                        visible.width, visible.height))
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                result = render_one_control(context, at, &theme, control,
                                            &clipped);
                if (result != ASTRA_OK) return result;
            }
            continue;
        }
        clipped = validated;
        if (!astra_surface_clip(&clipped, control->_private_clip.x,
                                control->_private_clip.y,
                                control->_private_clip.width,
                                control->_private_clip.height))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = render_one_control(context, at, &theme, control, &clipped);
        if (result != ASTRA_OK) return result;
    }
    render_finish(context);
    return ASTRA_OK;
}

AstraResult astra_interface_ui_render_range(AstraUIContext *context,
    AstraSurfaceView *surface, uint32_t first, uint32_t count)
{
    AstraSurfaceView validated;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    uint64_t commands, payload;
    uint32_t end;

    if (!context_valid(context) || surface == NULL ||
        surface->pixels == NULL || surface->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        surface->width != context->_private_width ||
        surface->height != context->_private_height ||
        surface->clip_left > surface->clip_right ||
        surface->clip_right > surface->width ||
        surface->clip_top > surface->clip_bottom ||
        surface->clip_bottom > surface->height || count == 0u ||
        first >= context->_private_control_count ||
        count > context->_private_control_count - first)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    end = first + count;
    if (!render_requirements(context, first, end, &commands, &payload) ||
        !astra_draw_list_view_adopt(&validated, surface->pixels,
            surface->byte_size, surface->width, surface->height))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_interface_draw_list_has_room(surface, commands, payload))
        return ASTRA_ERROR_NO_RESOURCES;
    for (uint32_t at = first; at < end; ++at) {
        const AstraControl *control = &context->_private_controls[at];
        AstraSurfaceView clipped;
        AstraResult result;

        if (control->_private_laid_out == 0u ||
            (CONTAINER_KINDS(control->_private_control_kind) &&
             !(control->_private_control_kind == ASTRA_CONTROL_CONTAINER &&
               (control->_private_style == ASTRA_CONTAINER_STYLE_TOOLBAR ||
                control->_private_style == ASTRA_CONTAINER_STYLE_STATUS_BAR ||
                control->_private_style == ASTRA_CONTAINER_STYLE_TAG_INPUT ||
                control->_private_style == ASTRA_CONTAINER_STYLE_DROP_TARGET ||
                control->_private_style == ASTRA_CONTAINER_STYLE_INSPECTOR))) ||
            control->_private_clip.width == 0u ||
            control->_private_clip.height == 0u)
            continue;
        clipped = validated;
        if (!astra_surface_clip(&clipped, control->_private_clip.x,
                control->_private_clip.y, control->_private_clip.width,
                control->_private_clip.height))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        result = render_one_control(context, at, &theme, control, &clipped);
        if (result != ASTRA_OK) return result;
    }
    if (end == context->_private_control_count) render_finish(context);
    return ASTRA_OK;
}

static AstraResult pointer_event(AstraUIContext *context,
                                 const AstraWindowEvent *event,
                                 AstraUIAction *action)
{
    uint32_t hit;

    if (event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
        event->data.pointer.button == ASTRA_INPUT_BUTTON_RIGHT &&
        (event->flags & (ASTRA_WINDOW_EVENT_DOWN | ASTRA_WINDOW_EVENT_LOSS)) == 0u &&
        event->data.pointer.x >= 0 && event->data.pointer.y >= 0 &&
        (uint32_t)event->data.pointer.x < context->_private_width &&
        (uint32_t)event->data.pointer.y < context->_private_height) {
        uint32_t target = hit_test(context, event->data.pointer.x,
                                   event->data.pointer.y, 1);
        clear_capture(context);
        clear_keyboard(context);
        action->value = -1;
        if (target != CONTROL_NONE) {
            const AstraControl *control = &context->_private_controls[target];
            if (focusable(control) && (control->_private_state & ASTRA_CONTROL_DISABLED) == 0u)
                set_focus(context, target, 0);
            action->control_id = control->_private_id;
            if (COLLECTION_KINDS(control->_private_control_kind)) {
                action->value = choice_index_at(control,
                    event->data.pointer.x, event->data.pointer.y);
                if ((control->_private_state & ASTRA_CONTROL_DISABLED) == 0u) {
                    AstraResult result = collection_context_select(context,
                        target, (int32_t)action->value);
                    if (result != ASTRA_OK) return result;
                }
            } else action->value = -1;
        }
        action->type = ASTRA_UI_ACTION_CONTEXT_MENU;
        return ASTRA_OK;
    }

    hit = hit_test(context, event->data.pointer.x, event->data.pointer.y, 0);
    set_hover(context, hit);
    {
        uint32_t part = STEPPER_PART_VALUE;

        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_STEPPER)
            part = stepper_part(&context->_private_controls[hit],
                                event->data.pointer.x,
                                event->data.pointer.y);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR)
            part = scrollbar_part(&context->_private_controls[hit],
                                  event->data.pointer.x,
                                  event->data.pointer.y);
        if (context->_private_hover_part != part) {
            control_damage(context, hit);
            context->_private_hover_part = part;
            control_damage(context, hit);
        }
    }
    if (hit != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[hit]
                         ._private_control_kind))
        choice_hot(context, hit, event->data.pointer.x,
                   event->data.pointer.y);
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION) {
        if (context->_private_capture != CONTROL_NONE) {
            uint32_t captured = context->_private_capture;
            AstraControl *captured_control =
                &context->_private_controls[captured];
            int stepper = captured_control->_private_control_kind ==
                          ASTRA_CONTROL_STEPPER;
            int scrollbar = captured_control->_private_control_kind ==
                            ASTRA_CONTROL_SCROLLBAR;
            int splitter = captured_control->_private_control_kind ==
                           ASTRA_CONTROL_SPLITTER;
            int dial = captured_control->_private_control_kind ==
                       ASTRA_CONTROL_DIAL;
            int pressed = scrollbar || splitter || dial || hit == captured;

            if (stepper)
                pressed = pressed &&
                          context->_private_hover_part ==
                              context->_private_capture_part &&
                          context->_private_capture_part !=
                              STEPPER_PART_VALUE;
            dynamic_state(context, captured, ASTRA_CONTROL_PRESSED, pressed);
            if (stepper) animation_sync(context, captured);
            if (captured_control->_private_control_kind ==
                    ASTRA_CONTROL_SLIDER)
                slider_pointer(context, context->_private_capture,
                               event->data.pointer.x,
                               event->data.pointer.y, action);
            else if (dial)
                dial_pointer(context, context->_private_capture,
                             event, action);
            else if (scrollbar) {
                AstraResult result = scrollbar_pointer(
                    context, context->_private_capture, event, action);

                if (result != ASTRA_OK) return result;
            }
            else if (splitter) {
                AstraResult result = splitter_pointer(
                    context, context->_private_capture,
                    event->data.pointer.x, event->data.pointer.y, action);

                if (result != ASTRA_OK) return result;
            }
            else if (TEXT_MODEL_KINDS(captured_control->_private_control_kind)) {
                AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
                uint32_t position, soft_end;
                AstraResult result = text_position(
                    &context->_private_controls[context->_private_capture],
                    event->data.pointer.x, event->data.pointer.y, &position,
                    &soft_end);

                if (result != ASTRA_OK)
                    return result;
                if (astra_text_model_get_state(
                        control_text_model(&context->_private_controls[
                            context->_private_capture]), &state) != ASTRA_OK)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                result = text_selection(
                    context, context->_private_capture,
                    state.selection.anchor, position, soft_end, action);
                if (result != ASTRA_OK)
                    return result;
            }
        }
        return ASTRA_OK;
    }
    if (event->data.pointer.button != ASTRA_INPUT_BUTTON_LEFT)
        return ASTRA_OK;
    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u) {
        clear_keyboard(context);
        clear_capture(context);
        if (hit == CONTROL_NONE || focusable(&context->_private_controls[hit]))
            set_focus(context, hit, 0);
        context->_private_capture = hit;
        context->_private_capture_part = STEPPER_PART_VALUE;
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_STEPPER)
            context->_private_capture_part = stepper_part(
                &context->_private_controls[hit], event->data.pointer.x,
                event->data.pointer.y);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR)
            context->_private_capture_part = scrollbar_part(
                &context->_private_controls[hit], event->data.pointer.x,
                event->data.pointer.y);
        else if (hit != CONTROL_NONE &&
                 TABLE_KINDS(context->_private_controls[hit]
                     ._private_control_kind)) {
            int32_t column = table_header_at(
                &context->_private_controls[hit], event->data.pointer.x,
                event->data.pointer.y);
            context->_private_capture_part = column < 0 ? 0u :
                (uint32_t)column + 1u;
        }
        dynamic_state(context, hit, ASTRA_CONTROL_PRESSED,
                      hit == CONTROL_NONE ||
                      context->_private_controls[hit]._private_control_kind !=
                          ASTRA_CONTROL_STEPPER ||
                      context->_private_capture_part != STEPPER_PART_VALUE);
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_SPLITTER) {
            AstraControl *splitter = &context->_private_controls[hit];

            splitter->_private_minimum_value =
                splitter->_private_style == ASTRA_ORIENTATION_VERTICAL ?
                    event->data.pointer.x : event->data.pointer.y;
            splitter->_private_maximum_value = splitter->_private_value;
        }
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_DIAL) {
            AstraControl *dial = &context->_private_controls[hit];

            if (event->data.pointer.click_count >= 2u)
                range_action(context, hit, dial->_private_reset_value,
                             action);
            dial_drag_save(context, event->data.pointer.y,
                           dial->_private_value);
        }
        if (hit != CONTROL_NONE &&
            context->_private_controls[hit]._private_control_kind ==
                ASTRA_CONTROL_SLIDER)
            slider_pointer(context, hit, event->data.pointer.x,
                           event->data.pointer.y, action);
        else if (hit != CONTROL_NONE &&
                 context->_private_controls[hit]._private_control_kind ==
                     ASTRA_CONTROL_SCROLLBAR) {
            AstraResult result = scrollbar_pointer(
                context, hit, event, action);

            if (result != ASTRA_OK) return result;
        }
        else if (hit != CONTROL_NONE &&
                 TEXT_MODEL_KINDS(context->_private_controls[hit]._private_control_kind)) {
            uint32_t position, soft_end;
            AstraControl *text_control = &context->_private_controls[hit];
            AstraResult result = text_position(
                text_control, event->data.pointer.x, event->data.pointer.y,
                &position, &soft_end);

            if (result != ASTRA_OK)
                return result;
            if (text_control->_private_control_kind == ASTRA_CONTROL_FIELD &&
                (text_control->_private_style & ASTRA_FIELD_SEARCH) != 0u &&
                event->data.pointer.x >= text_control->_private_frame.x +
                    (int32_t)text_control->_private_frame.width -
                    (int32_t)FIELD_SEARCH_PADDING_X) {
                AstraTextModelState field_state = ASTRA_TEXT_MODEL_STATE_INIT;

                if (astra_text_model_get_state(control_text_model(text_control),
                                               &field_state) != ASTRA_OK)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                if (field_state.text_bytes != 0u) {
                    result = text_selection(context, hit, 0u,
                        field_state.text_bytes, 0u, action);
                    if (result != ASTRA_OK) return result;
                    return field_replace_selection_internal(
                        context, hit, NULL, 0u, event->timestamp_ms, 0u,
                        action);
                }
            }
            result = text_selection(context, hit, position, position,
                                     soft_end, action);
            if (result != ASTRA_OK)
                return result;
        } else if (hit != CONTROL_NONE &&
                   context->_private_controls[hit]._private_control_kind ==
                       ASTRA_CONTROL_STEPPER) {
            stepper_finish_input(context, hit, 0, action);
            stepper_step(context, hit, context->_private_capture_part, 1u,
                         action);
            animation_sync(context, hit);
        }
        return ASTRA_OK;
    }
    if (context->_private_capture != CONTROL_NONE) {
        uint32_t captured = context->_private_capture;
        int slider = context->_private_controls[captured]
                         ._private_control_kind == ASTRA_CONTROL_SLIDER;
        int choice = CHOICE_KINDS(context->_private_controls[captured]
                                     ._private_control_kind);
        int stepper = context->_private_controls[captured]
                          ._private_control_kind == ASTRA_CONTROL_STEPPER;
        int scrollbar = context->_private_controls[captured]
                            ._private_control_kind == ASTRA_CONTROL_SCROLLBAR;
        int splitter = context->_private_controls[captured]
                           ._private_control_kind == ASTRA_CONTROL_SPLITTER;
        int dial = context->_private_controls[captured]
                       ._private_control_kind == ASTRA_CONTROL_DIAL;

        if (slider)
            slider_pointer(context, captured, event->data.pointer.x,
                           event->data.pointer.y, action);
        else if (choice && hit == captured) {
            AstraControl *control = &context->_private_controls[captured];
            if (COLLECTION_KINDS(control->_private_control_kind)) {
                int32_t row = collection_row_at(control,
                    event->data.pointer.y);
                if (row >= 0 &&
                    !collection_row_selectable(control, (uint32_t)row)) {
                    clear_capture(context);
                    return ASTRA_OK;
                }
            }
            if (TABLE_KINDS(control->_private_control_kind) &&
                context->_private_capture_part != 0u &&
                table_header_at(control, event->data.pointer.x,
                    event->data.pointer.y) ==
                    (int32_t)context->_private_capture_part - 1) {
                action->type = ASTRA_UI_ACTION_SORT_REQUESTED;
                action->control_id = control->_private_id;
                action->value = (int64_t)context->_private_capture_part - 1;
                clear_capture(context);
                return ASTRA_OK;
            }
            if (control->_private_control_kind == ASTRA_CONTROL_POPUP) {
                clear_capture(context);
                return activate_control(context, captured, action);
            }
            if (control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE &&
                control->_private_minimum_value >= 0) {
                uint32_t row = (uint32_t)control->_private_minimum_value;
                const AstraOutlineTableModel *outline =
                    const_outline_table_model(control);
                AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
                int64_t relative_x = (int64_t)event->data.pointer.x -
                    control->_private_frame.x;
                uint32_t left = (uint32_t)theme.control_padding_x +
                    outline->rows[row].depth * 16u;
                if (outline->rows[row].expandable != 0u &&
                    relative_x >= left && relative_x < (int64_t)left + 20) {
                    action->type = ASTRA_UI_ACTION_EXPANSION_REQUESTED;
                    action->control_id = control->_private_id;
                    action->value = row;
                    clear_capture(context);
                    return ASTRA_OK;
                }
            }
            if (control->_private_control_kind == ASTRA_CONTROL_TREE &&
                control->_private_minimum_value >= 0) {
                AstraTreeModel *tree = tree_model(control);
                uint32_t row = (uint32_t)control->_private_minimum_value;
                uint32_t item = tree->visible_rows[row];
                AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
                uint32_t indent = (tree->nodes[item].depth + 1u) *
                                  theme.control_height;
                int64_t relative_x = (int64_t)event->data.pointer.x -
                                     control->_private_frame.x;

                if (tree_has_children(tree, item) && relative_x >= 0 &&
                    relative_x < indent) {
                    uint32_t expanded = !tree->nodes[item].expanded;
                    AstraResult result = astra_interface_tree_set_expanded(
                        context, control, tree->collection->items[item].id,
                        expanded);
                    if (result != ASTRA_OK) return result;
                    action->type = ASTRA_UI_ACTION_EXPANSION_CHANGED;
                    action->control_id = control->_private_id;
                    action->value = item;
                    clear_capture(context);
                    return ASTRA_OK;
                }
            }
            int invoke = COLLECTION_KINDS(control->_private_control_kind) &&
                         event->data.pointer.click_count >= 2u &&
                         control->_private_value >= 0 &&
                         control->_private_value == control->_private_minimum_value;

            choice_action(context, captured, control->_private_minimum_value,
                          event->data.pointer.modifiers, action);
            if (invoke) {
                action->type = ASTRA_UI_ACTION_ACTIVATE;
                action->control_id = control->_private_id;
                action->value = control->_private_value;
            }
        }
        else if (splitter) {
            AstraResult result = splitter_pointer(
                context, captured, event->data.pointer.x,
                event->data.pointer.y, action);

            if (result != ASTRA_OK) return result;
        }

        clear_capture(context);
        if (!slider && !dial && !choice && !stepper && !scrollbar && !splitter &&
            hit == captured &&
            !TEXT_MODEL_KINDS(context->_private_controls[captured]._private_control_kind) &&
            (context->_private_controls[captured]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            return activate_control(context, captured, action);
    }
    return ASTRA_OK;
}

static int point_in_control(const AstraControl *control, int32_t x, int32_t y)
{
    const AstraControlFrame *frame = &control->_private_frame;
    const AstraControlFrame *clip = &control->_private_clip;

    return control->_private_laid_out != 0u &&
           x >= frame->x && (int64_t)x < (int64_t)frame->x + frame->width &&
           y >= frame->y && (int64_t)y < (int64_t)frame->y + frame->height &&
           x >= clip->x && (int64_t)x < (int64_t)clip->x + clip->width &&
           y >= clip->y && (int64_t)y < (int64_t)clip->y + clip->height;
}

static uint32_t scroll_delta(uint32_t offset, uint32_t maximum,
                             int32_t notches, uint32_t step)
{
    int64_t value = (int64_t)offset - (int64_t)notches * step;

    if (value < 0) return 0u;
    if ((uint64_t)value > maximum) return maximum;
    return (uint32_t)value;
}

static AstraResult wheel_event(AstraUIContext *context,
                               const AstraWindowEvent *event,
                               AstraUIAction *action)
{
    for (uint32_t count = context->_private_control_count; count != 0u;
         --count) {
        uint32_t at = count - 1u;
        AstraControl *view = &context->_private_controls[at];
        AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
        uint32_t x;
        uint32_t y;
        AstraResult result;

        if (!point_in_control(view, event->data.wheel.x,
                              event->data.wheel.y) ||
            (view->_private_control_kind != ASTRA_CONTROL_SCROLL_VIEW &&
             view->_private_control_kind != ASTRA_CONTROL_SCROLLBAR) ||
            astra_scroll_get_state(
                scroll_model(view), &state) != ASTRA_OK)
            continue;
        x = scroll_delta(state.offset_x, state.maximum_x,
                         event->data.wheel.delta_x, state.line_width);
        y = scroll_delta(state.offset_y, state.maximum_y,
                         event->data.wheel.delta_y, state.line_height);
        if (x == state.offset_x && y == state.offset_y)
            continue;
        result = scroll_apply(context, scroll_model(view), x, y,
                              view->_private_id, action);
        return result;
    }
    return ASTRA_OK;
}

static AstraResult scroll_key(AstraUIContext *context,
                              const AstraWindowEvent *event,
                              AstraUIAction *action)
{
    uint32_t at;

    if ((event->flags & ASTRA_WINDOW_EVENT_DOWN) == 0u ||
        context->_private_focus == CONTROL_NONE)
        return ASTRA_OK;
    at = context->_private_focus;
    while (at != CONTROL_NONE) {
        AstraControl *view = &context->_private_controls[at];

        if (view->_private_control_kind == ASTRA_CONTROL_SCROLL_VIEW) {
            AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
            uint32_t x;
            uint32_t y;
            int handled = 1;

            if (astra_scroll_get_state(
                    scroll_model(view), &state) != ASTRA_OK)
                return ASTRA_ERROR_INVALID_ARGUMENT;
            x = state.offset_x;
            y = state.offset_y;
            if (event->data.key.usage == 0x50u)
                x = scroll_delta(x, state.maximum_x, 1,
                                 state.line_width);
            else if (event->data.key.usage == 0x4fu)
                x = scroll_delta(x, state.maximum_x, -1,
                                 state.line_width);
            else if (event->data.key.usage == 0x52u)
                y = scroll_delta(y, state.maximum_y, 1,
                                 state.line_height);
            else if (event->data.key.usage == 0x51u)
                y = scroll_delta(y, state.maximum_y, -1,
                                 state.line_height);
            else if (event->data.key.usage == 0x4bu)
                y = state.offset_y > state.viewport_height ?
                    state.offset_y - state.viewport_height : 0u;
            else if (event->data.key.usage == 0x4eu)
                y = state.maximum_y - state.offset_y < state.viewport_height ?
                    state.maximum_y : state.offset_y + state.viewport_height;
            else if (event->data.key.usage == 0x4au) {
                if (state.maximum_y != 0u) y = 0u;
                else x = 0u;
            } else if (event->data.key.usage == 0x4du) {
                if (state.maximum_y != 0u) y = state.maximum_y;
                else x = state.maximum_x;
            } else {
                handled = 0;
            }
            if (handled && (x != state.offset_x || y != state.offset_y))
                return scroll_apply(context, scroll_model(view), x, y,
                                    view->_private_id, action);
            if (handled)
                return ASTRA_OK;
        }
        at = view->_private_parent;
    }
    return ASTRA_OK;
}

static int activation_key(uint32_t usage)
{
    return usage == 0x28u || usage == 0x2cu || usage == 0x58u;
}

static AstraResult disclosure_set(AstraUIContext *context, uint32_t index,
                                  int expanded, AstraUIAction *action)
{
    AstraControl *disclosure = &context->_private_controls[index];
    uint32_t target_index = control_id_index(
        context, disclosure->_private_style);
    AstraControl *target;
    uint32_t state;
    AstraResult result;

    if (target_index == CONTROL_NONE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    target = &context->_private_controls[target_index];
    state = target->_private_state;
    if (((state & ASTRA_CONTROL_COLLAPSED) == 0u) == (expanded != 0))
        return ASTRA_OK;
    if (expanded)
        state &= ~ASTRA_CONTROL_COLLAPSED;
    else
        state |= ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_ui_set_state(context, target, state);
    if (result != ASTRA_OK)
        return result;
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->control_id = disclosure->_private_id;
    action->value = expanded != 0;
    action->decimal_places = 0u;
    return ASTRA_OK;
}

static AstraResult activate_control(AstraUIContext *context, uint32_t index,
                                    AstraUIAction *action)
{
    AstraControl *control = &context->_private_controls[index];

    if (control->_private_control_kind == ASTRA_CONTROL_POPUP) {
        action->type = ASTRA_UI_ACTION_ACTIVATE;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
        return ASTRA_OK;
    }

    if (COLLECTION_KINDS(control->_private_control_kind)) {
        if (control->_private_value < 0) return ASTRA_OK;
        action->type = ASTRA_UI_ACTION_ACTIVATE;
        action->control_id = control->_private_id;
        action->value = control->_private_value;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_SLIDER ||
        control->_private_control_kind == ASTRA_CONTROL_DIAL ||
        control->_private_control_kind == ASTRA_CONTROL_STEPPER ||
        control->_private_control_kind == ASTRA_CONTROL_SPLITTER ||
        CHOICE_KINDS(control->_private_control_kind))
        return ASTRA_OK;
    if (control->_private_control_kind == ASTRA_CONTROL_DISCLOSURE) {
        uint32_t target = control_id_index(context, control->_private_style);

        if (target == CONTROL_NONE)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        return disclosure_set(
            context, index,
            (context->_private_controls[target]._private_state &
             ASTRA_CONTROL_COLLAPSED) != 0u,
            action);
    }
    action->control_id = control->_private_id;
    if (!TOGGLE_KINDS(control->_private_control_kind)) {
        action->type = ASTRA_UI_ACTION_ACTIVATE;
        return ASTRA_OK;
    }
    if (control->_private_control_kind == ASTRA_CONTROL_RADIO) {
        AstraResult result;

        if ((control->_private_state & ASTRA_CONTROL_SELECTED) != 0u)
            return ASTRA_OK;
        result = astra_interface_ui_set_state(
            context, control,
            control->_private_state | ASTRA_CONTROL_SELECTED);
        if (result != ASTRA_OK)
            return result;
    } else {
        control_damage(context, index);
        control->_private_state ^= ASTRA_CONTROL_SELECTED;
        control_damage(context, index);
    }
    action->type = ASTRA_UI_ACTION_VALUE_CHANGED;
    action->value = (control->_private_state & ASTRA_CONTROL_SELECTED) != 0u;
    action->decimal_places = 0u;
    return ASTRA_OK;
}

static AstraResult key_event(AstraUIContext *context,
                             const AstraWindowEvent *event,
                             AstraUIAction *action)
{
    int down = (event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u;

    if (event->data.key.usage == 0x2bu) {
        if (down && (event->flags & ASTRA_WINDOW_EVENT_REPEAT) == 0u)
            set_focus(context, next_focus(
                context,
                (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u),
                1);
        return ASTRA_OK;
    }
    if (down && context->_private_focus != CONTROL_NONE)
        dynamic_state(context, context->_private_focus,
                      CONTROL_FOCUS_VISIBLE, 1);
    if (context->_private_focus != CONTROL_NONE &&
        TEXT_MODEL_KINDS(context->_private_controls[context->_private_focus]
                ._private_control_kind))
        return text_key(context, context->_private_focus, event, action);
    if (down && context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_DISCLOSURE &&
        (event->data.key.usage == 0x50u ||
         event->data.key.usage == 0x4fu))
        return disclosure_set(context, context->_private_focus,
                              event->data.key.usage == 0x4fu, action);
    if (down && context->_private_focus != CONTROL_NONE &&
        COLLECTION_KINDS(context->_private_controls[context->_private_focus]
                ._private_control_kind)) {
        AstraControl *control = &context->_private_controls[context->_private_focus];
        int32_t value = (int32_t)control->_private_value;
        uint32_t usage = event->data.key.usage;

        if (control->_private_control_kind == ASTRA_CONTROL_TREE &&
            value >= 0 && (usage == 0x50u || usage == 0x4fu)) {
            AstraTreeModel *tree = tree_model(control);
            uint32_t item = tree->visible_rows[value];
            uint32_t expand = usage == 0x4fu;

            if (tree_has_children(tree, item) &&
                tree->nodes[item].expanded != expand) {
                AstraResult result = astra_interface_tree_set_expanded(
                    context, control, tree->collection->items[item].id, expand);
                if (result != ASTRA_OK) return result;
                action->type = ASTRA_UI_ACTION_EXPANSION_CHANGED;
                action->control_id = control->_private_id;
                action->value = item;
            } else if (!expand && tree->nodes[item].parent_id != 0u) {
                int32_t parent = tree_visible_index(tree,
                    tree->nodes[item].parent_id);
                if (parent >= 0) choice_action(context,
                    context->_private_focus, parent,
                    event->data.key.modifiers, action);
            } else if (expand && value + 1 < (int32_t)tree->visible_count &&
                       tree->nodes[tree->visible_rows[value + 1]].depth >
                           tree->nodes[item].depth) {
                choice_action(context, context->_private_focus, value + 1,
                              event->data.key.modifiers, action);
            }
            return ASTRA_OK;
        }
        if (control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE &&
            value >= 0 && (usage == 0x50u || usage == 0x4fu)) {
            const AstraOutlineTableModel *outline =
                const_outline_table_model(control);
            const AstraOutlineTableRow *row = &outline->rows[value];
            uint32_t expand = usage == 0x4fu;
            if (row->expandable != 0u && row->expanded != expand) {
                action->type = ASTRA_UI_ACTION_EXPANSION_REQUESTED;
                action->control_id = control->_private_id;
                action->value = value;
            } else if (!expand && row->depth != 0u) {
                int32_t parent = value - 1;
                while (parent >= 0 &&
                       outline->rows[parent].depth >= row->depth)
                    --parent;
                if (parent >= 0) choice_action(context,
                    context->_private_focus, parent,
                    event->data.key.modifiers, action);
            } else if (expand && value + 1 < (int32_t)outline->row_count &&
                       outline->rows[value + 1].depth > row->depth) {
                choice_action(context, context->_private_focus, value + 1,
                    event->data.key.modifiers, action);
            }
            return ASTRA_OK;
        }

        if (control->_private_text_length == 0u)
            return ASTRA_OK;
        if (control->_private_control_kind == ASTRA_CONTROL_ICON_GRID) {
            const AstraIconGridModel *grid = const_icon_grid_model(control);
            if (usage == 0x52u && value >= 0) {
                value = value >= (int32_t)grid->columns ?
                    value - (int32_t)grid->columns : value;
            } else if (usage == 0x51u && value >= 0) {
                int64_t below = (int64_t)value + grid->columns;
                if (below <= control->_private_maximum_value)
                    value = (int32_t)below;
            } else if (usage == 0x50u && value > 0) {
                --value;
            } else if (usage == 0x4fu &&
                       value < control->_private_maximum_value) {
                ++value;
            } else if (usage == 0x4au) {
                value = 0;
            } else if (usage == 0x4du) {
                value = (int32_t)control->_private_maximum_value;
            } else if (value < 0 &&
                       (usage == 0x50u || usage == 0x4fu ||
                        usage == 0x52u || usage == 0x51u)) {
                value = 0;
            } else {
                value = -1;
            }
            if (value >= 0) {
                AstraResult result = list_reveal_row(context,
                    context->_private_focus, (uint32_t)value);
                if (result != ASTRA_OK) return result;
                choice_action(context, context->_private_focus, value,
                    event->data.key.modifiers, action);
            }
            return ASTRA_OK;
        }
        if (usage == 0x52u || usage == 0x51u) {
            if (value < 0)
                value = 0;
            else if (usage == 0x52u && value > 0)
                --value;
            else if (usage == 0x51u && value < control->_private_maximum_value)
                ++value;
        } else if (usage == 0x4au) {
            value = 0;
        } else if (usage == 0x4du) {
            value = (int32_t)control->_private_maximum_value;
        } else {
            value = -1;
        }
        if (value >= 0 &&
            !collection_row_selectable(control, (uint32_t)value)) {
            int32_t step = usage == 0x52u || usage == 0x4du ? -1 : 1;
            while (value >= 0 &&
                   value <= control->_private_maximum_value &&
                   !collection_row_selectable(control, (uint32_t)value))
                value += step;
        }
        if (value >= 0 && value <= control->_private_maximum_value) {
            AstraResult result = list_reveal_row(
                context, context->_private_focus, (uint32_t)value);

            if (result != ASTRA_OK) return result;
            choice_action(context, context->_private_focus, value,
                event->data.key.modifiers, action);
            return ASTRA_OK;
        }
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        CHOICE_KINDS(context->_private_controls[context->_private_focus]
                         ._private_control_kind) &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind != ASTRA_CONTROL_POPUP &&
        !COLLECTION_KINDS(context->_private_controls[context->_private_focus]
                ._private_control_kind)) {
        AstraControl *control =
            &context->_private_controls[context->_private_focus];
        int32_t value = control->_private_value;
        int handled = 1;

        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x52u) {
            if (value != 0) --value;
        } else if (event->data.key.usage == 0x4fu ||
                   event->data.key.usage == 0x51u) {
            if (value != control->_private_maximum_value) ++value;
        } else if (event->data.key.usage == 0x4au) {
            value = 0;
        } else if (event->data.key.usage == 0x4du) {
            value = control->_private_maximum_value;
        } else {
            handled = 0;
        }
        if (handled) {
            choice_action(context, context->_private_focus, value,
                event->data.key.modifiers, action);
            return ASTRA_OK;
        }
    }
    if (context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_STEPPER) {
        uint32_t index = context->_private_focus;
        AstraControl *control = &context->_private_controls[index];

        if (!down) return ASTRA_OK;
        if (event->data.key.usage == 0x29u) {
            stepper_finish_input(context, index, 0, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x28u ||
            event->data.key.usage == 0x58u) {
            stepper_finish_input(context, index, 1, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x2au &&
            (context->_private_value_input_flags &
             VALUE_INPUT_EDITING) != 0u) {
            int64_t value;

            control_damage(context, index);
            if (context->_private_value_input_digits != 0u) {
                context->_private_value_input /= 10u;
                --context->_private_value_input_digits;
            } else {
                context->_private_value_input_flags = 0u;
            }
            control_damage(context, index);
            if (stepper_draft_value(context, &value) &&
                value >= control->_private_minimum_value &&
                value <= control->_private_maximum_value &&
                range_snap(control, value) == value)
                range_action(context, index, value, action);
            return ASTRA_OK;
        }
        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x51u ||
            event->data.key.usage == 0x4fu ||
            event->data.key.usage == 0x52u ||
            event->data.key.usage == 0x4au ||
            event->data.key.usage == 0x4du) {
            uint32_t part;
            uint32_t count = 1u;

            stepper_finish_input(context, index, 0, action);
            if (event->data.key.usage == 0x4au) {
                range_action(context, index,
                             control->_private_minimum_value, action);
                return ASTRA_OK;
            }
            if (event->data.key.usage == 0x4du) {
                range_action(context, index,
                             control->_private_maximum_value, action);
                return ASTRA_OK;
            }
            part = event->data.key.usage == 0x4fu ||
                   event->data.key.usage == 0x52u ?
                STEPPER_PART_UP : STEPPER_PART_DOWN;
            stepper_step(context, index, part, count, action);
            return ASTRA_OK;
        }
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        context->_private_controls[context->_private_focus]
                ._private_control_kind == ASTRA_CONTROL_SPLITTER) {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
        uint32_t index = context->_private_focus;
        AstraControl *control = &context->_private_controls[index];
        int64_t position = control->_private_value;
        int32_t step =
            (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u ?
                1 : (int32_t)theme.spacing_unit;
        int handled = 1;

        if (event->data.key.usage == 0x4au)
            position = INT64_MIN;
        else if (event->data.key.usage == 0x4du)
            position = INT64_MAX;
        else if (control->_private_style == ASTRA_ORIENTATION_VERTICAL &&
                 event->data.key.usage == 0x50u)
            position -= step;
        else if (control->_private_style == ASTRA_ORIENTATION_VERTICAL &&
                 event->data.key.usage == 0x4fu)
            position += step;
        else if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL &&
                 event->data.key.usage == 0x52u)
            position -= step;
        else if (control->_private_style == ASTRA_ORIENTATION_HORIZONTAL &&
                 event->data.key.usage == 0x51u)
            position += step;
        else
            handled = 0;
        if (handled)
            return splitter_set_position(context, index, position, action);
    }
    if (down && context->_private_focus != CONTROL_NONE &&
        (context->_private_controls[context->_private_focus]
                 ._private_control_kind == ASTRA_CONTROL_SLIDER ||
         context->_private_controls[context->_private_focus]
                 ._private_control_kind == ASTRA_CONTROL_DIAL)) {
        AstraControl *control =
            &context->_private_controls[context->_private_focus];
        int64_t value = control->_private_value;
        int handled = 1;

        if (event->data.key.usage == 0x50u ||
            event->data.key.usage == 0x51u)
            value = range_move_steps(control, value, -1);
        else if (event->data.key.usage == 0x4fu ||
                 event->data.key.usage == 0x52u)
            value = range_move_steps(control, value, 1);
        else if (event->data.key.usage == 0x4au)
            value = control->_private_minimum_value;
        else if (event->data.key.usage == 0x4du)
            value = control->_private_maximum_value;
        else
            handled = 0;
        if (handled) {
            range_action(context, context->_private_focus, value, action);
            return ASTRA_OK;
        }
    }
    {
        AstraResult result = scroll_key(context, event, action);

        if (result != ASTRA_OK || action->type != ASTRA_UI_ACTION_NONE)
            return result;
    }
    if (!activation_key(event->data.key.usage))
        return ASTRA_OK;
    if (down) {
        if ((event->flags & ASTRA_WINDOW_EVENT_REPEAT) != 0u ||
            context->_private_focus == CONTROL_NONE)
            return ASTRA_OK;
        clear_capture(context);
        clear_keyboard(context);
        context->_private_keyboard = context->_private_focus;
        dynamic_state(context, context->_private_keyboard,
                      ASTRA_CONTROL_PRESSED, 1);
        return ASTRA_OK;
    }
    if (context->_private_keyboard != CONTROL_NONE) {
        uint32_t pressed = context->_private_keyboard;

        clear_keyboard(context);
        if (pressed == context->_private_focus &&
            (context->_private_controls[pressed]._private_state &
             ASTRA_CONTROL_DISABLED) == 0u)
            return activate_control(context, pressed, action);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_ui_handle_event(AstraUIContext *context,
                                            const AstraWindowEvent *event,
                                            AstraUIAction *action)
{
    if (!context_valid(context) || event == NULL || action == NULL ||
        event->size < sizeof(*event) ||
        event->version != ASTRA_WINDOW_EVENT_VERSION ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action->type = ASTRA_UI_ACTION_NONE;
    action->control_id = 0u;
    action->value = 0;
    action->decimal_places = 0u;
    if ((event->flags & ASTRA_WINDOW_EVENT_LOSS) != 0u ||
        event->type == ASTRA_WINDOW_EVENT_STATE_RESET) {
        reset_interaction(context, 1);
        if (event->type != ASTRA_WINDOW_EVENT_FONT_CHANGED) return ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_FONT_CHANGED) {
        AstraResult result = measure_controls(context);
        if (result != ASTRA_OK) return result;
        context->_private_damage = (AstraControlFrame){
            0, 0, context->_private_width, context->_private_height};
        context->_private_has_damage = 1u;
        return context->_private_has_layout != 0u ?
            astra_interface_ui_layout(context, &context->_private_layout) : ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_KEY &&
        (event->flags & (ASTRA_WINDOW_EVENT_DOWN | ASTRA_WINDOW_EVENT_REPEAT |
                         ASTRA_WINDOW_EVENT_LOSS)) == ASTRA_WINDOW_EVENT_DOWN &&
        (event->data.key.modifiers & (ASTRA_INPUT_MOD_CTRL | ASTRA_INPUT_MOD_ALT |
                                     ASTRA_INPUT_MOD_GUI | ASTRA_INPUT_MOD_META)) == 0u &&
        (event->data.key.usage == 0x65u ||
         (event->data.key.usage == 0x43u &&
          (event->data.key.modifiers & ASTRA_INPUT_MOD_SHIFT) != 0u))) {
        clear_capture(context);
        clear_keyboard(context);
        action->type = ASTRA_UI_ACTION_CONTEXT_MENU;
        if (context->_private_focus != CONTROL_NONE)
            action->control_id = context->_private_controls[context->_private_focus]._private_id;
        return ASTRA_OK;
    }
    if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION ||
        event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON)
        return pointer_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_POINTER_WHEEL)
        return wheel_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_KEY)
        return key_event(context, event, action);
    else if (event->type == ASTRA_WINDOW_EVENT_TEXT &&
             context->_private_focus != CONTROL_NONE) {
        uint32_t index = context->_private_focus;
        uint32_t kind = context->_private_controls[index]
                            ._private_control_kind;

        if (kind == ASTRA_CONTROL_FIELD || kind == ASTRA_CONTROL_TEXT_VIEW)
            return field_text(context, index, event, action);
        if (kind == ASTRA_CONTROL_STEPPER) {
            if (!astra_unicode_scalar_valid(event->data.text.codepoint))
                return ASTRA_ERROR_INVALID_ARGUMENT;
            stepper_text(context, index, event->data.text.codepoint, action);
        }
    }
    else if (event->type == ASTRA_WINDOW_EVENT_RESIZE &&
             event->data.resize.width != 0u &&
             event->data.resize.height != 0u) {
        if (context->_private_width == event->data.resize.width &&
            context->_private_height == event->data.resize.height)
            return ASTRA_OK;
        context->_private_width = event->data.resize.width;
        context->_private_height = event->data.resize.height;
        context->_private_damage = (AstraControlFrame){
            0, 0, context->_private_width, context->_private_height};
        context->_private_has_damage = 1u;
        if (context->_private_has_layout != 0u)
            return astra_interface_ui_layout(
                context, &context->_private_layout);
    }
    else if (event->type == ASTRA_WINDOW_EVENT_STATE &&
             (event->data.state.flags & ASTRA_WINDOW_ACTIVE) == 0u)
        reset_interaction(context, 1);
    return ASTRA_OK;
}

AstraResult astra_interface_ui_pointer_shape(const AstraUIContext *context,
                                             uint32_t *shape)
{
    uint32_t index;
    const AstraControl *control;

    if (!context_valid(context) || shape == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    index = context->_private_capture != CONTROL_NONE ?
        context->_private_capture : context->_private_hover;
    if (index == CONTROL_NONE) {
        *shape = ASTRA_POINTER_SHAPE_DEFAULT;
        return ASTRA_OK;
    }
    control = &context->_private_controls[index];
    if (TEXT_MODEL_KINDS(control->_private_control_kind))
        *shape = ASTRA_POINTER_SHAPE_TEXT;
    else if (control->_private_control_kind == ASTRA_CONTROL_SPLITTER)
        *shape = control->_private_style == ASTRA_ORIENTATION_VERTICAL ?
            ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL :
            ASTRA_POINTER_SHAPE_RESIZE_VERTICAL;
    else
        *shape = ASTRA_POINTER_SHAPE_DEFAULT;
    return ASTRA_OK;
}

AstraResult astra_interface_ui_update_pointer(const AstraUIContext *context,
                                              AstraWindow *window,
                                              uint32_t requested_shape,
                                              uint32_t *applied_shape)
{
    uint32_t shape;
    AstraResult result;

    if ((requested_shape != ASTRA_POINTER_SHAPE_AUTOMATIC &&
         requested_shape >= ASTRA_POINTER_SHAPE_COUNT) ||
        applied_shape == NULL ||
        (*applied_shape != UINT32_MAX &&
         *applied_shape >= ASTRA_POINTER_SHAPE_COUNT))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (requested_shape == ASTRA_POINTER_SHAPE_AUTOMATIC) {
        result = astra_interface_ui_pointer_shape(context, &shape);
        if (result != ASTRA_OK)
            return result;
    } else {
        if (!context_valid(context))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        shape = requested_shape;
    }
    if (shape == *applied_shape)
        return ASTRA_OK;
    result = astra_window_set_pointer_shape(window, (AstraPointerShape)shape);
    if (result == ASTRA_OK)
        *applied_shape = shape;
    return result;
}

AstraResult astra_interface_ui_geometry(const AstraUIContext *context,
    uint32_t index, AstraControlFrame *frame, AstraControlFrame *clip)
{
    const AstraControl *control;

    if (!context_valid(context) || frame == NULL || clip == NULL ||
        index >= context->_private_control_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    control = &context->_private_controls[index];
    if (context->_private_has_layout == 0u ||
        control->_private_laid_out == 0u)
        return ASTRA_ERROR_WOULD_BLOCK;
    *frame = control->_private_frame;
    *clip = control->_private_clip;
    return ASTRA_OK;
}

AstraResult astra_interface_collection_text_rect(
    const AstraUIContext *context, const AstraControl *control,
    uint32_t row, AstraControlFrame *text, AstraControlFrame *clip)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraControlFrame frame;
    uint32_t index = control_index(context, control);
    const AstraCollectionModel *collection;
    uint32_t indent = 0u;
    uint64_t top;
    AstraResult result;
    if (index == CONTROL_NONE || text == NULL || clip == NULL ||
        (control->_private_control_kind != ASTRA_CONTROL_LIST &&
         !TABLE_KINDS(control->_private_control_kind)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    collection = const_collection_model(control);
    if (collection == NULL || row >= collection->item_count)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_interface_ui_geometry(context, index, &frame, clip);
    if (result != ASTRA_OK) return result;
    if (control->_private_control_kind == ASTRA_CONTROL_OUTLINE_TABLE) {
        const AstraOutlineTableModel *outline =
            const_outline_table_model(control);
        indent = outline->rows[row].depth * 16u + 20u;
    }
    top = (uint64_t)row * theme.control_height +
        (TABLE_KINDS(control->_private_control_kind) ?
            theme.control_height : 0u);
    if (top > INT32_MAX || frame.y > INT32_MAX - (int32_t)top ||
        frame.x > INT32_MAX - theme.control_padding_x ||
        frame.x + theme.control_padding_x > INT32_MAX - (int32_t)indent)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t inset = (uint32_t)theme.control_padding_x + indent;
    *text = (AstraControlFrame){frame.x + (int32_t)inset,
        frame.y + (int32_t)top,
        frame.width > inset + (uint32_t)theme.control_padding_x ?
            frame.width - inset - (uint32_t)theme.control_padding_x : 0u,
        theme.control_height};
    return ASTRA_OK;
}

AstraResult astra_interface_ui_pointer_hit(const AstraUIContext *context,
    int32_t x, int32_t y, uint32_t *control_id, int32_t *row)
{
    if (!context_valid(context) || control_id == NULL || row == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *control_id = 0u;
    *row = -1;
    if (x < 0 || y < 0 || (uint32_t)x >= context->_private_width ||
        (uint32_t)y >= context->_private_height)
        return ASTRA_OK;
    uint32_t hit = hit_test(context, x, y, 1);
    if (hit != CONTROL_NONE) {
        const AstraControl *control = &context->_private_controls[hit];
        *control_id = control->_private_id;
        if (COLLECTION_KINDS(control->_private_control_kind))
            *row = choice_index_at(control, x, y);
    }
    return ASTRA_OK;
}

AstraResult astra_interface_drag_gesture_event(AstraDragGesture *gesture,
    const AstraWindowEvent *event, uint32_t source_hit, uint32_t *triggered)
{
    if (gesture == NULL || event == NULL || triggered == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *triggered = 0u;
    if (event->type == ASTRA_WINDOW_EVENT_STATE_RESET) {
        gesture->armed = 0u;
    } else if (event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON &&
        event->data.pointer.button == ASTRA_INPUT_BUTTON_LEFT) {
        gesture->armed = (event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
            (event->flags & ASTRA_WINDOW_EVENT_LOSS) == 0u && source_hit != 0u;
        gesture->start_x = event->data.pointer.x;
        gesture->start_y = event->data.pointer.y;
    } else if (event->type == ASTRA_WINDOW_EVENT_POINTER_MOTION &&
        gesture->armed != 0u) {
        int64_t dx = (int64_t)event->data.motion.x - gesture->start_x;
        int64_t dy = (int64_t)event->data.motion.y - gesture->start_y;
        if (dx < -5 || dx > 5 || dy < -5 || dy > 5) {
            gesture->armed = 0u;
            *triggered = 1u;
        }
    }
    return ASTRA_OK;
}

AstraResult astra_interface_ui_damage(const AstraUIContext *context,
                                      AstraControlFrame *damage)
{
    if (!context_valid(context) || damage == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (context->_private_has_damage == 0u)
        return ASTRA_ERROR_WOULD_BLOCK;
    *damage = context->_private_damage;
    return ASTRA_OK;
}

void astra_interface_ui_damage_clear(AstraUIContext *context)
{
    if (!context_valid(context))
        return;
    context->_private_damage = (AstraControlFrame){0};
    context->_private_has_damage = 0u;
    context->_private_reserved = 0u;
}

AstraResult astra_interface_ui_vblank(AstraUIContext *context,
                                      uint64_t now_ns,
                                      AstraUIAction *action)
{
    uint64_t before;
    uint64_t phase64;
    uint32_t phase_delta;
    uint32_t at;
    uint32_t visited = 0u;

    if (!context_valid(context) || action == NULL ||
        action->size < sizeof(*action))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action->type = ASTRA_UI_ACTION_NONE;
    action->control_id = 0u;
    action->value = 0;
    action->decimal_places = 0u;
    before = ((uint64_t)context->_private_animation_time_high << 32) |
             context->_private_animation_time_low;
    if (before != 0u && now_ns < before)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    phase64 = (now_ns / UINT64_C(1000000000)) * ASTRA_UI_VBLANK_HZ +
              ((now_ns % UINT64_C(1000000000)) * ASTRA_UI_VBLANK_HZ) /
                  UINT64_C(1000000000);
    phase_delta = before == 0u ? 1u :
        (uint32_t)phase64 - context->_private_animation_phase;
    context->_private_animation_phase = (uint32_t)phase64;
    at = context->_private_animation_first;
    while (at != CONTROL_NONE) {
        AstraControl *control;
        uint32_t next;

        if (at >= context->_private_control_count ||
            ++visited > context->_private_control_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        control = &context->_private_controls[at];
        next = control->_private_animation_next;
        if (control->_private_control_kind == ASTRA_CONTROL_PROGRESS) {
            control->_private_animation_phase += phase_delta;
            if (phase_delta != 0u)
                control_damage(context, at);
        } else if (control->_private_control_kind == ASTRA_CONTROL_FIELD) {
            int before_visible = field_caret_visible_at(
                control->_private_animation_phase);

            control->_private_animation_phase += phase_delta;
            if (before_visible != field_caret_visible_at(
                    control->_private_animation_phase))
                control_damage(context, at);
        } else if (control->_private_control_kind == ASTRA_CONTROL_STEPPER) {
            uint32_t before_phase = control->_private_animation_phase;
            uint32_t after_phase = before_phase + phase_delta;
            uint32_t before_repeats = before_phase < STEPPER_REPEAT_DELAY ?
                0u : 1u + (before_phase - STEPPER_REPEAT_DELAY) /
                                STEPPER_REPEAT_INTERVAL;
            uint32_t after_repeats = after_phase < STEPPER_REPEAT_DELAY ?
                0u : 1u + (after_phase - STEPPER_REPEAT_DELAY) /
                                STEPPER_REPEAT_INTERVAL;

            control->_private_animation_phase = after_phase;
            if (after_repeats > before_repeats)
                stepper_step(context, at, context->_private_capture_part,
                             after_repeats - before_repeats, action);
        } else if (control->_private_control_kind ==
                   ASTRA_CONTROL_SCROLLBAR) {
            uint32_t before_phase = control->_private_animation_phase;

            if (before_phase < SCROLLBAR_FADE_FRAMES) {
                uint32_t remaining = SCROLLBAR_FADE_FRAMES - before_phase;

                control->_private_animation_phase +=
                    phase_delta < remaining ? phase_delta : remaining;
                if (control->_private_animation_phase != before_phase)
                    control_damage(context, at);
            }
            animation_sync(context, at);
        }
        at = next;
    }
    context->_private_animation_time_low = (uint32_t)now_ns;
    context->_private_animation_time_high = (uint32_t)(now_ns >> 32);
    return ASTRA_OK;
}

int astra_interface_ui_animations_active(const AstraUIContext *context)
{
    return context_valid(context) &&
           context->_private_animation_first != CONTROL_NONE;
}
