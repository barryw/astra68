#include "control_internal.h"
#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/theme.h>
#include <astra/utf8.h>
#include <stddef.h>

AstraResult astra_interface_settings_pane_init(AstraControl *controls,
                                               const AstraSettingsPaneInfo *info)
{
    const AstraFlexLayout standard = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u ||
        info->first_id > UINT32_MAX - (ASTRA_SETTINGS_PANE_CONTROL_COUNT - 1u) ||
        info->title_length == 0u || info->description_length == 0u ||
        !astra_utf8_validate(info->title, info->title_length, 0u) ||
        !astra_utf8_validate(info->description, info->description_length, 0u) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    for (uint32_t at = 0u; at < ASTRA_SETTINGS_PANE_CONTROL_COUNT; ++at) {
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
        AstraResult result;
        if (at == ASTRA_SETTINGS_PANE_TITLE ||
            at == ASTRA_SETTINGS_PANE_DESCRIPTION) {
            AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
            label.id = info->first_id + at;
            label.text = at == ASTRA_SETTINGS_PANE_TITLE ?
                info->title : info->description;
            label.text_length = at == ASTRA_SETTINGS_PANE_TITLE ?
                info->title_length : info->description_length;
            label.text_role = at == ASTRA_SETTINGS_PANE_TITLE ?
                ASTRA_TEXT_CLIENT_PRIMARY : ASTRA_TEXT_CLIENT_SECONDARY;
            result = astra_interface_label_init(&controls[at], &label);
        } else {
            AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
            container.id = info->first_id + at;
            container.layout.direction = ASTRA_FLEX_COLUMN;
            container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
            container.layout.main_gap = standard.main_gap;
            result = astra_interface_container_init(&controls[at], &container);
        }
        if (result != ASTRA_OK) return result;
        if (at == ASTRA_SETTINGS_PANE_BODY) {
            item.grow = 1u;
            item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
        }
        result = astra_interface_control_set_flex(&controls[at], &item);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_settings_sidebar_init(AstraControl *controls,
    const AstraSettingsSidebarInfo *info)
{
    AstraContainerInfo root = ASTRA_CONTAINER_INFO_INIT;
    AstraFieldInfo search = ASTRA_FIELD_INFO_INIT;
    AstraScrollViewInfo scroll = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraListInfo list = ASTRA_LIST_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size != sizeof(*info) ||
        info->first_id == 0u ||
        info->first_id > UINT32_MAX - ASTRA_SETTINGS_SIDEBAR_LIST ||
        info->search == NULL || info->scroll == NULL ||
        info->collection == NULL || info->width < 120u ||
        info->height == 0u || !astra_words_zero(info->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    root.id = info->first_id;
    root.layout.direction = ASTRA_FLEX_COLUMN;
    root.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.layout.padding_left = root.layout.padding_right = 10u;
    root.layout.padding_top = 12u;
    root.layout.main_gap = 12u;
    result = astra_interface_container_init(
        &controls[ASTRA_SETTINGS_SIDEBAR_ROOT], &root);
    if (result != ASTRA_OK) return result;
    item.minimum_width = item.maximum_width = info->width;
    item.minimum_height = info->height;
    item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
    result = astra_interface_control_set_flex(
        &controls[ASTRA_SETTINGS_SIDEBAR_ROOT], &item);
    if (result != ASTRA_OK) return result;

    search.id = info->first_id + ASTRA_SETTINGS_SIDEBAR_SEARCH;
    search.model = info->search;
    search.preferred_columns = 16u;
    result = astra_interface_search_field_init(
        &controls[ASTRA_SETTINGS_SIDEBAR_SEARCH], &search);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = root.id;
    item.minimum_width = info->width - 20u;
    result = astra_interface_control_set_flex(
        &controls[ASTRA_SETTINGS_SIDEBAR_SEARCH], &item);
    if (result != ASTRA_OK) return result;

    scroll.id = info->first_id + ASTRA_SETTINGS_SIDEBAR_SCROLL;
    scroll.model = info->scroll;
    scroll.preferred_width = info->width - 20u;
    scroll.preferred_height = info->height > 60u ?
        info->height - 60u : 1u;
    result = astra_interface_scroll_view_init(
        &controls[ASTRA_SETTINGS_SIDEBAR_SCROLL], &scroll);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = root.id;
    item.grow = 1u;
    item.minimum_width = info->width - 20u;
    result = astra_interface_control_set_flex(
        &controls[ASTRA_SETTINGS_SIDEBAR_SCROLL], &item);
    if (result != ASTRA_OK) return result;

    list.id = info->first_id + ASTRA_SETTINGS_SIDEBAR_LIST;
    list.model = info->collection;
    result = astra_interface_list_init(
        &controls[ASTRA_SETTINGS_SIDEBAR_LIST], &list);
    if (result != ASTRA_OK) return result;
    controls[ASTRA_SETTINGS_SIDEBAR_LIST]._private_style =
        ASTRA_LIST_STYLE_SETTINGS_SIDEBAR;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = scroll.id;
    item.minimum_width = info->width - 20u;
    return astra_interface_control_set_flex(
        &controls[ASTRA_SETTINGS_SIDEBAR_LIST], &item);
}
