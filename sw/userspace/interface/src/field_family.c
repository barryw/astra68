#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/utf8.h>

#include <limits.h>
#include "control_internal.h"

AstraResult astra_interface_segmented_field_init(AstraControl *controls,
    uint32_t control_count, const AstraSegmentedFieldInfo *info)
{
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->segment_count < 2u ||
        info->segment_count > 16u ||
        control_count != info->segment_count * 2u ||
        info->first_id > UINT32_MAX - control_count + 1u ||
        info->models == NULL || info->preferred_columns == NULL ||
        info->separator_length == 0u ||
        !astra_utf8_validate(info->separator, info->separator_length, 0u) ||
        (info->state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    row.id = info->first_id;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    row.layout.main_gap = 2u;
    result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    for (uint32_t at = 0u; at < info->segment_count; ++at) {
        uint32_t field_at = at * 2u + 1u;
        AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        field.id = info->first_id + field_at;
        field.model = info->models[at];
        field.preferred_columns = info->preferred_columns[at];
        field.state = info->state;
        result = astra_interface_field_init(&controls[field_at], &field);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.shrink = 0u;
        result = astra_interface_control_set_flex(&controls[field_at], &item);
        if (result != ASTRA_OK) return result;
        if (at + 1u < info->segment_count) {
            AstraLabelInfo separator = ASTRA_LABEL_INFO_INIT;

            separator.id = info->first_id + field_at + 1u;
            separator.text = info->separator;
            separator.text_length = info->separator_length;
            separator.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
            result = astra_interface_label_init(
                &controls[field_at + 1u], &separator);
            if (result != ASTRA_OK) return result;
            result = astra_interface_control_set_flex(
                &controls[field_at + 1u], &item);
            if (result != ASTRA_OK) return result;
        }
    }
    return ASTRA_OK;
}

AstraResult astra_interface_tag_input_init(AstraControl *controls,
    uint32_t control_count, const AstraTagInputInfo *info)
{
    AstraContainerInfo row = ASTRA_CONTAINER_INFO_INIT;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u || info->tag_count > 32u ||
        control_count != info->tag_count + 2u ||
        info->first_id > UINT32_MAX - control_count + 1u ||
        (info->tag_count != 0u && info->tags == NULL) ||
        info->draft == NULL || info->preferred_columns == 0u ||
        (info->state & ~(ASTRA_CONTROL_DISABLED | ASTRA_CONTROL_ERROR)) != 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    row.id = info->first_id;
    row.layout.flags = ASTRA_FLEX_WRAP;
    row.layout.align_items = ASTRA_FLEX_ALIGN_CENTER;
    row.layout.main_gap = 6u;
    row.layout.cross_gap = 6u;
    row.layout.padding_left = row.layout.padding_right = 6u;
    row.layout.padding_top = row.layout.padding_bottom = 6u;
    result = astra_interface_container_init(&controls[0], &row);
    if (result != ASTRA_OK) return result;
    controls[0]._private_style = ASTRA_CONTAINER_STYLE_TAG_INPUT;
    for (uint32_t at = 0u; at < info->tag_count; ++at) {
        AstraButtonInfo tag = ASTRA_BUTTON_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        tag.id = info->first_id + at + 1u;
        tag.text = info->tags[at].text;
        tag.text_length = info->tags[at].text_length;
        tag.variant = ASTRA_BUTTON_TAG;
        tag.state = info->state & ASTRA_CONTROL_DISABLED;
        result = astra_interface_button_init(&controls[at + 1u], &tag);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.shrink = 0u;
        result = astra_interface_control_set_flex(&controls[at + 1u], &item);
        if (result != ASTRA_OK) return result;
    }
    {
        AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
        AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;

        field.id = info->first_id + info->tag_count + 1u;
        field.model = info->draft;
        field.preferred_columns = info->preferred_columns;
        field.state = info->state;
        field.flags = ASTRA_FIELD_INLINE;
        result = astra_interface_field_init(
            &controls[info->tag_count + 1u], &field);
        if (result != ASTRA_OK) return result;
        item.parent_id = info->first_id;
        item.grow = 1u;
        result = astra_interface_control_set_flex(
            &controls[info->tag_count + 1u], &item);
    }
    return result;
}
