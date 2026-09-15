#include <astra/interface_kit.h>

enum {
    EXAMPLE_SCROLL_VIEW = 1u,
    EXAMPLE_SCROLL_CONTENT,
    EXAMPLE_SCROLL_LABEL,
    EXAMPLE_SCROLLBAR
};

/* Build one resize-responsive viewport and an external model-bound bar. */
AstraResult astra_example_build_scroll(
    const AstraInterfaceLibrary *interface, AstraScrollModel *model,
    AstraControl controls[4], AstraUIContext *ui,
    uint16_t width, uint16_t height)
{
    AstraScrollModelInfo model_info = ASTRA_SCROLL_MODEL_INFO_INIT;
    AstraScrollViewInfo view = ASTRA_SCROLL_VIEW_INFO_INIT;
    AstraContainerInfo content = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraScrollbarInfo bar = ASTRA_SCROLLBAR_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (model == NULL || controls == NULL || ui == NULL ||
        !astra_interface_library_supports(
            interface, 0u, ASTRA_INTERFACE_LIBRARY_5_0_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model_info.line_width = 8u;
    model_info.line_height = 16u;
    result = interface->scroll_init(model, &model_info);
    if (result != ASTRA_OK) return result;

    view.id = EXAMPLE_SCROLL_VIEW;
    view.model = model;
    view.preferred_width = width > 10u ? width - 10u : 1u;
    view.preferred_height = height;
    result = interface->scroll_view_init(&controls[0], &view);
    if (result != ASTRA_OK) return result;

    content.id = EXAMPLE_SCROLL_CONTENT;
    content.layout.direction = ASTRA_FLEX_COLUMN;
    result = interface->container_init(&controls[1], &content);
    if (result != ASTRA_OK) return result;
    item.parent_id = EXAMPLE_SCROLL_VIEW;
    item.minimum_height = 600u;
    result = interface->control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;

    label.id = EXAMPLE_SCROLL_LABEL;
    label.text = "Scrollable UTF-8 content";
    label.text_length = sizeof("Scrollable UTF-8 content") - 1u;
    result = interface->label_init(&controls[2], &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_SCROLL_CONTENT;
    result = interface->control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;

    bar.id = EXAMPLE_SCROLLBAR;
    bar.model = model;
    bar.orientation = ASTRA_ORIENTATION_VERTICAL;
    result = interface->scrollbar_init(&controls[3], &bar);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 10u;
    item.maximum_width = 10u;
    result = interface->control_set_flex(&controls[3], &item);
    if (result != ASTRA_OK) return result;

    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.grow = 1u;
    result = interface->control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;
    result = interface->ui_init(ui, controls, 4u, width, height);
    if (result != ASTRA_OK) return result;
    root.direction = ASTRA_FLEX_ROW;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    return interface->ui_layout(ui, &root);
}
