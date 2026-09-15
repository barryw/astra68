#include <astra/interface_kit.h>

enum {
    EXAMPLE_LEFT_PANE = 1u,
    EXAMPLE_LEFT_LABEL,
    EXAMPLE_SPLITTER,
    EXAMPLE_RIGHT_PANE,
    EXAMPLE_RIGHT_LABEL
};

/* Compose two ordinary flex panes around the one shared drag primitive. */
AstraResult astra_example_build_split_view(
    const AstraInterfaceLibrary *interface, AstraControl controls[5],
    AstraUIContext *ui, uint16_t width, uint16_t height)
{
    AstraContainerInfo pane = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraSplitterInfo splitter = ASTRA_SPLITTER_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (controls == NULL || ui == NULL ||
        !astra_interface_library_supports(
            interface, 0u, ASTRA_INTERFACE_LIBRARY_5_0_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    pane.id = EXAMPLE_LEFT_PANE;
    pane.layout.direction = ASTRA_FLEX_COLUMN;
    pane.layout.padding_left = 8u;
    pane.layout.padding_top = 8u;
    result = interface->container_init(&controls[0], &pane);
    if (result != ASTRA_OK) return result;
    item.basis = width / 3u;
    item.minimum_width = 64u;
    item.grow = 1u;
    result = interface->control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;

    label.id = EXAMPLE_LEFT_LABEL;
    label.text = "Sidebar";
    label.text_length = sizeof("Sidebar") - 1u;
    result = interface->label_init(&controls[1], &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_LEFT_PANE;
    result = interface->control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;

    splitter.id = EXAMPLE_SPLITTER;
    result = interface->splitter_init(&controls[2], &splitter);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 8u;
    item.maximum_width = 8u;
    result = interface->control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;

    pane = (AstraContainerInfo)ASTRA_CONTAINER_INFO_INIT;
    pane.id = EXAMPLE_RIGHT_PANE;
    pane.layout.direction = ASTRA_FLEX_COLUMN;
    pane.layout.padding_left = 8u;
    pane.layout.padding_top = 8u;
    result = interface->container_init(&controls[3], &pane);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 64u;
    item.grow = 2u;
    result = interface->control_set_flex(&controls[3], &item);
    if (result != ASTRA_OK) return result;

    label.id = EXAMPLE_RIGHT_LABEL;
    label.text = "Content";
    label.text_length = sizeof("Content") - 1u;
    result = interface->label_init(&controls[4], &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_RIGHT_PANE;
    result = interface->control_set_flex(&controls[4], &item);
    if (result != ASTRA_OK) return result;

    result = interface->ui_init(ui, controls, 5u, width, height);
    if (result != ASTRA_OK) return result;
    root.direction = ASTRA_FLEX_ROW;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    return interface->ui_layout(ui, &root);
}
