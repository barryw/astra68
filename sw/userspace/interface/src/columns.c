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
