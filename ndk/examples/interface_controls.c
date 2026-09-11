#include <astra/interface_kit.h>

enum {
    SETTINGS_ROOT = 1u,
    SETTINGS_TITLE,
    SETTINGS_SNAP,
    SETTINGS_APPLY
};

/* Build one resize-responsive control tree in caller-owned storage. */
AstraResult astra_example_build_settings(
    const AstraInterfaceLibraryV2 *interface, AstraControl controls[4],
    AstraUIContext *ui, uint16_t width, uint16_t height)
{
    AstraContainerInfo container = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraToggleInfo toggle = ASTRA_TOGGLE_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (controls == 0 || ui == 0 ||
        !astra_interface_library_supports(
            interface, 0u, ASTRA_INTERFACE_LIBRARY_2_0_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    container.id = SETTINGS_ROOT;
    container.layout.direction = ASTRA_FLEX_COLUMN;
    container.layout.main_gap = 12u;
    controls[0] = (AstraControl)ASTRA_CONTROL_INIT;
    result = interface->container_init(&controls[0], &container);
    if (result != ASTRA_OK) return result;
    item.grow = 1u;
    result = interface->control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;

    label.id = SETTINGS_TITLE;
    label.text = "Desktop settings";
    label.text_length = sizeof("Desktop settings") - 1u;
    controls[1] = (AstraControl)ASTRA_CONTROL_INIT;
    result = interface->label_init(&controls[1], &label);
    if (result != ASTRA_OK) return result;

    toggle.id = SETTINGS_SNAP;
    toggle.text = "Snap to grid";
    toggle.text_length = sizeof("Snap to grid") - 1u;
    controls[2] = (AstraControl)ASTRA_CONTROL_INIT;
    result = interface->checkbox_init(&controls[2], &toggle);
    if (result != ASTRA_OK) return result;

    button.id = SETTINGS_APPLY;
    button.text = "Apply";
    button.text_length = sizeof("Apply") - 1u;
    button.variant = ASTRA_BUTTON_PRIMARY;
    controls[3] = (AstraControl)ASTRA_CONTROL_INIT;
    result = interface->button_init(&controls[3], &button);
    if (result != ASTRA_OK) return result;

    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = SETTINGS_ROOT;
    for (uint32_t index = 1u; index < 4u; ++index) {
        result = interface->control_set_flex(&controls[index], &item);
        if (result != ASTRA_OK) return result;
    }
    result = interface->ui_init(ui, controls, 4u, width, height);
    if (result != ASTRA_OK) return result;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    root.padding_left = root.padding_top = 24u;
    root.padding_right = root.padding_bottom = 24u;
    return interface->ui_layout(ui, &root);
}
