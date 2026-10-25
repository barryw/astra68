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
