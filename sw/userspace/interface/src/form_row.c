#include <astra/interface_library.h>
#include <astra/bytes.h>
#include <astra/utf8.h>
#include <stddef.h>

AstraResult astra_interface_form_row_init(AstraControl *controls,
                                          const AstraFormRowInfo *info)
{
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraResult result;

    if (controls == NULL || info == NULL || info->size < sizeof(*info) ||
        info->first_id == 0u ||
        info->first_id > UINT32_MAX - (ASTRA_FORM_ROW_CONTROL_COUNT - 1u) ||
        info->label_length == 0u ||
        !astra_utf8_validate(info->label, info->label_length, 0u) ||
        (info->message_length != 0u &&
         !astra_utf8_validate(info->message, info->message_length, 0u)) ||
        (info->message_role != ASTRA_TEXT_CLIENT_SECONDARY &&
         info->message_role != ASTRA_TEXT_CLIENT_FAULT) ||
        info->label_width == 0u ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    container.id = info->first_id + ASTRA_FORM_ROW_ROOT;
    container.layout.direction = ASTRA_FLEX_ROW;
    container.layout.align_items = ASTRA_FLEX_ALIGN_START;
    container.layout.main_gap = 14u;
    result = astra_interface_container_init(&controls[ASTRA_FORM_ROW_ROOT],
                                            &container);
    if (result != ASTRA_OK) return result;

    label.id = info->first_id + ASTRA_FORM_ROW_LABEL;
    label.text = info->label;
    label.text_length = info->label_length;
    label.text_role = ASTRA_TEXT_CLIENT_SECONDARY;
    result = astra_interface_label_init(&controls[ASTRA_FORM_ROW_LABEL], &label);
    if (result != ASTRA_OK) return result;
    item.parent_id = info->first_id + ASTRA_FORM_ROW_ROOT;
    item.basis = info->label_width;
    item.minimum_width = info->label_width;
    item.maximum_width = info->label_width;
    result = astra_interface_control_set_flex(&controls[ASTRA_FORM_ROW_LABEL],
                                              &item);
    if (result != ASTRA_OK) return result;

    container.id = info->first_id + ASTRA_FORM_ROW_CONTENT;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 4u;
    result = astra_interface_container_init(&controls[ASTRA_FORM_ROW_CONTENT],
                                            &container);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = info->first_id + ASTRA_FORM_ROW_ROOT;
    item.grow = 1u;
    item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
    result = astra_interface_control_set_flex(&controls[ASTRA_FORM_ROW_CONTENT],
                                              &item);
    if (result != ASTRA_OK) return result;

    container.id = info->first_id + ASTRA_FORM_ROW_VALUE;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    container.layout.main_gap = 0u;
    result = astra_interface_container_init(&controls[ASTRA_FORM_ROW_VALUE],
                                            &container);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = info->first_id + ASTRA_FORM_ROW_CONTENT;
    item.grow = 1u;
    item.minimum_width = 0u;
    item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
    result = astra_interface_control_set_flex(&controls[ASTRA_FORM_ROW_VALUE],
                                              &item);
    if (result != ASTRA_OK) return result;

    label = (AstraLabelInfo)ASTRA_LABEL_INFO_INIT;
    label.id = info->first_id + ASTRA_FORM_ROW_MESSAGE;
    label.text = info->message_length != 0u ? info->message : " ";
    label.text_length = info->message_length != 0u ? info->message_length : 1u;
    label.text_role = info->message_role;
    label.state = info->message_length != 0u ? 0u : ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_label_init(&controls[ASTRA_FORM_ROW_MESSAGE],
                                        &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = info->first_id + ASTRA_FORM_ROW_CONTENT;
    result = astra_interface_control_set_flex(&controls[ASTRA_FORM_ROW_MESSAGE],
                                              &item);
    return result;
}
