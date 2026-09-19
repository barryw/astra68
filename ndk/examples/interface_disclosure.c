#include <astra/interface_kit.h>

#include <stddef.h>

enum {
    EXAMPLE_DISCLOSURE = 1u,
    EXAMPLE_BODY,
    EXAMPLE_FIRST_OPTION,
    EXAMPLE_SECOND_OPTION
};

/* Build one disclosure whose target container owns the expanded state. */
AstraResult astra_example_build_disclosure(
    AstraControl controls[4], AstraUIContext *ui,
    uint16_t width, uint16_t height)
{
    AstraDisclosureInfo disclosure = ASTRA_DISCLOSURE_INFO_INIT;
    AstraContainerInfo body = ASTRA_CONTAINER_INFO_INIT;
    AstraToggleInfo option = ASTRA_TOGGLE_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (controls == NULL || ui == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    disclosure.id = EXAMPLE_DISCLOSURE;
    disclosure.text = "Advanced";
    disclosure.text_length = sizeof("Advanced") - 1u;
    disclosure.target_id = EXAMPLE_BODY;
    result = astra_interface_disclosure_init(&controls[0], &disclosure);
    if (result != ASTRA_OK) return result;

    body.id = EXAMPLE_BODY;
    body.layout.direction = ASTRA_FLEX_COLUMN;
    body.layout.main_gap = 8u;
    result = astra_interface_container_init(&controls[1], &body);
    if (result != ASTRA_OK) return result;

    option.id = EXAMPLE_FIRST_OPTION;
    option.text = "Reject late generations";
    option.text_length = sizeof("Reject late generations") - 1u;
    result = astra_interface_checkbox_init(&controls[2], &option);
    if (result != ASTRA_OK) return result;
    option.id = EXAMPLE_SECOND_OPTION;
    option.text = "Log active-surface hazards";
    option.text_length = sizeof("Log active-surface hazards") - 1u;
    option.state = ASTRA_CONTROL_SELECTED;
    result = astra_interface_checkbox_init(&controls[3], &option);
    if (result != ASTRA_OK) return result;

    item.flags = ASTRA_FLEX_BREAK_AFTER;
    result = astra_interface_control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_BODY;
    result = astra_interface_control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_flex(&controls[3], &item);
    if (result != ASTRA_OK) return result;

    result = astra_interface_ui_init(ui, controls, 4u, width, height);
    if (result != ASTRA_OK) return result;
    root.direction = ASTRA_FLEX_COLUMN;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.main_gap = 8u;
    return astra_interface_ui_layout(ui, &root);
}
