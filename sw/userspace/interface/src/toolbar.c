#include <astra/interface_library.h>
#include <astra/theme.h>
#include <astra/bytes.h>
#include <astra/utf8.h>
#include "control_internal.h"

AstraResult astra_interface_toolbar_init(AstraControl *controls,
                                         const AstraToolbarInfo *info)
{
    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->actions == NULL ||
        info->action_count == 0u ||
        info->first_id > UINT32_MAX - info->action_count ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < info->action_count; ++at)
        if (info->actions[at].label_length == 0u ||
            (info->actions[at].state & ~ASTRA_CONTROL_DISABLED) != 0u ||
            !astra_utf8_validate(info->actions[at].label,
                                 info->actions[at].label_length, 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;

    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    row.id = info->first_id;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    row.layout.padding_left = row.layout.padding_right =
        ASTRA_THEME_TOOLBAR_INSET;
    row.layout.padding_top = row.layout.padding_bottom =
        ASTRA_THEME_TOOLBAR_INSET;
    row.layout.main_gap = 2u;
    AstraResult result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    controls[0]._private_style = ASTRA_CONTAINER_STYLE_TOOLBAR;
    for (uint32_t at = 0u; at < info->action_count; ++at) {
        AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
        button.id = info->first_id + at + 1u;
        button.text = info->actions[at].label;
        button.text_length = info->actions[at].label_length;
        button.state = info->actions[at].state;
        button.variant = ASTRA_BUTTON_TOOLBAR;
        result = astra_interface_button_init(&controls[at + 1u], &button);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.shrink = 0u;
        item.maximum_height = ASTRA_THEME_TOOLBAR_BUTTON_HEIGHT;
        result = astra_interface_control_set_flex(&controls[at + 1u], &item);
        if (result != ASTRA_OK) return result;
    }
    return ASTRA_OK;
}

AstraResult astra_interface_toolbar_set_icons(AstraControl *controls,
    const uint32_t *icons, uint32_t count)
{
    if (controls == NULL || icons == NULL || count == 0u ||
        controls[0]._private_control_kind != ASTRA_CONTROL_CONTAINER ||
        controls[0]._private_style != ASTRA_CONTAINER_STYLE_TOOLBAR)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at)
        if (icons[at] > ASTRA_TOOLBAR_ICON_NEW_FOLDER ||
            controls[at + 1u]._private_control_kind != ASTRA_CONTROL_BUTTON ||
            (controls[at + 1u]._private_style & ASTRA_BUTTON_STYLE_MASK) !=
                ASTRA_BUTTON_TOOLBAR)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < count; ++at)
        controls[at + 1u]._private_style = ASTRA_BUTTON_TOOLBAR |
            (icons[at] != ASTRA_TOOLBAR_ICON_NONE ?
                controls[at + 1u]._private_style &
                    ASTRA_BUTTON_ICON_ONLY : 0u) |
            (icons[at] << ASTRA_BUTTON_ICON_SHIFT);
    return ASTRA_OK;
}
