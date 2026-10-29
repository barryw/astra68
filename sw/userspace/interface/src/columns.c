#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <stddef.h>
#include <stdint.h>

AstraResult astra_interface_columns_init(AstraControl *controls,
    const AstraColumnsInfo *info)
{
    AstraContainerInfo root = ASTRA_CONTAINER_INFO_INIT;
    uint64_t total_width = 0u;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->columns == NULL ||
        info->column_count == 0u || info->column_count > 64u ||
        info->first_id > UINT32_MAX - info->column_count * 2u ||
        info->viewport_height == 0u ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->column_count; ++at) {
        const AstraColumnView *column = &info->columns[at];

        if (column->model == NULL || column->scroll == NULL ||
            column->width == 0u ||
            !astra_words_zero(column->reserved, 2u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        total_width += column->width;
        for (uint32_t previous = 0u; previous < at; ++previous)
            if (column->model == info->columns[previous].model ||
                column->scroll == info->columns[previous].scroll)
                return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    if (total_width + info->column_count - 1u > INT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;

    root.id = info->first_id;
    root.state = info->state;
    root.layout.direction = ASTRA_FLEX_ROW;
    root.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.layout.main_gap = 1u;
    result = astra_interface_container_init(&controls[0], &root);
    if (result != ASTRA_OK) return result;

    for (uint32_t at = 0u; at < info->column_count; ++at) {
        const AstraColumnView *column = &info->columns[at];
        uint32_t scroll_slot = 1u + at * 2u;
        uint32_t list_slot = scroll_slot + 1u;
        AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
        AstraListInfo list = ASTRA_LIST_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        view.id = info->first_id + scroll_slot;
        view.model = column->scroll;
        view.preferred_width = column->width;
        view.preferred_height = info->viewport_height;
        result = astra_interface_scroll_view_init(&controls[scroll_slot], &view);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.minimum_width = item.maximum_width = column->width;
        item.minimum_height = item.maximum_height = info->viewport_height;
        result = astra_interface_control_set_flex(&controls[scroll_slot], &item);
        if (result != ASTRA_OK) return result;

        list.id = info->first_id + list_slot;
        list.model = column->model;
        result = astra_interface_list_init(&controls[list_slot], &list);
        if (result != ASTRA_OK) return result;
        item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
        item.parent_id = view.id;
        item.minimum_width = column->width;
        result = astra_interface_control_set_flex(&controls[list_slot], &item);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

static int pagination_count(const AstraPaginationModel *model,
    uint32_t *pages)
{
    if (model == NULL || model->size != sizeof(*model) ||
        model->collection == NULL ||
        model->collection->size != sizeof(AstraCollectionModel) ||
        model->page_size == 0u ||
        model->active_label > 1u ||
        (model->collection->item_count != 0u &&
         model->collection->items == NULL) ||
        !astra_words_zero(model->reserved, 2u))
        return 0;
    *pages = model->collection->item_count / model->page_size +
        (model->collection->item_count % model->page_size != 0u);
    if (*pages == 0u) *pages = 1u;
    return 1;
}

static uint32_t decimal_append(char *buffer, uint32_t at, uint32_t value)
{
    char reversed[10];
    uint32_t count = 0u;

    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) buffer[at++] = reversed[--count];
    return at;
}

static uint32_t pagination_label(char label[24], uint32_t page,
    uint32_t pages)
{
    uint32_t at = decimal_append(label, 0u, page + 1u);
    label[at++] = ' ';
    label[at++] = '/';
    label[at++] = ' ';
    at = decimal_append(label, at, pages);
    label[at] = '\0';
    return at;
}

AstraResult astra_interface_pagination_range(
    const AstraPaginationModel *model, uint32_t *first, uint32_t *end,
    uint32_t *page_count)
{
    uint32_t pages;
    uint64_t start, after;

    if (first == NULL || end == NULL || page_count == NULL ||
        !pagination_count(model, &pages) || model->page_index >= pages)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    start = (uint64_t)model->page_index * model->page_size;
    after = start + model->page_size;
    *first = (uint32_t)start;
    *end = after > model->collection->item_count ?
        model->collection->item_count : (uint32_t)after;
    *page_count = pages;
    return ASTRA_OK;
}

AstraResult astra_interface_pagination_init(AstraControl *controls,
    const AstraPaginationInfo *info)
{
    AstraContainerInfo root = ASTRA_CONTAINER_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraPaginationModel *model;
    AstraResult result;
    uint32_t pages;

    if (controls == NULL || info == NULL || info->size != sizeof(*info) ||
        info->first_id == 0u || info->first_id > UINT32_MAX - 3u ||
        info->model == NULL ||
        (info->state & ~ASTRA_CONTROL_COLLAPSED) != 0u ||
        !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model = info->model;
    if (!pagination_count(model, &pages) || model->page_index >= pages)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model->label_length = pagination_label(model->label[model->active_label],
        model->page_index, pages);
    root.id = info->first_id;
    root.state = info->state;
    root.layout.direction = ASTRA_FLEX_ROW;
    root.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    root.layout.main_gap = 8u;
    result = astra_interface_container_init(&controls[0], &root);
    if (result != ASTRA_OK) return result;

    button.id = info->first_id + 1u;
    button.text = "Previous";
    button.text_length = 8u;
    button.state = model->page_index == 0u ? ASTRA_CONTROL_DISABLED : 0u;
    result = astra_interface_button_init(&controls[1], &button);
    if (result != ASTRA_OK) return result;
    item.parent_id = root.id;
    result = astra_interface_control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;

    label.id = info->first_id + 2u;
    label.text = model->label[model->active_label];
    label.text_length = model->label_length;
    result = astra_interface_label_init(&controls[2], &label);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;

    button.id = info->first_id + 3u;
    button.text = "Next";
    button.text_length = 4u;
    button.state = model->page_index + 1u == pages ?
        ASTRA_CONTROL_DISABLED : 0u;
    result = astra_interface_button_init(&controls[3], &button);
    if (result != ASTRA_OK) return result;
    return astra_interface_control_set_flex(&controls[3], &item);
}

AstraResult astra_interface_pagination_set_page(AstraUIContext *context,
    AstraControl *controls, AstraPaginationModel *model, uint32_t page_index)
{
    uint32_t pages;
    uint32_t next_label;
    uint32_t next_length;
    AstraResult result;

    if (context == NULL || controls == NULL ||
        !pagination_count(model, &pages) || page_index >= pages ||
        controls[0]._private_control_kind != ASTRA_CONTROL_CONTAINER ||
        controls[1]._private_control_kind != ASTRA_CONTROL_BUTTON ||
        controls[2]._private_control_kind != ASTRA_CONTROL_LABEL ||
        controls[3]._private_control_kind != ASTRA_CONTROL_BUTTON ||
        controls[2]._private_text != model->label[model->active_label] ||
        controls[1]._private_id != controls[0]._private_id + 1u ||
        controls[2]._private_id != controls[0]._private_id + 2u ||
        controls[3]._private_id != controls[0]._private_id + 3u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next_label = 1u - model->active_label;
    next_length = pagination_label(model->label[next_label],
        page_index, pages);
    result = astra_interface_control_set_text(context, &controls[2],
        model->label[next_label], next_length);
    if (result != ASTRA_OK) return result;
    model->page_index = page_index;
    model->label_length = next_length;
    model->active_label = next_label;
    (void)astra_interface_ui_set_state(context, &controls[1],
        page_index == 0u ? ASTRA_CONTROL_DISABLED : 0u);
    (void)astra_interface_ui_set_state(context, &controls[3],
        page_index + 1u == pages ? ASTRA_CONTROL_DISABLED : 0u);
    return ASTRA_OK;
}
