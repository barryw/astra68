#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

#include <stddef.h>

enum {
    EXAMPLE_SCROLL_VIEW = 1u,
    EXAMPLE_SCROLL_CONTENT,
    EXAMPLE_SCROLL_LABEL,
    EXAMPLE_SCROLLBAR
};

/* Build one resize-responsive viewport and an external model-bound bar. */
AstraResult astra_example_build_scroll(
    AstraScrollModel *model, AstraControl controls[4], AstraUIContext *ui,
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

    if (model == NULL || controls == NULL || ui == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    model_info.line_width = 8u;
    model_info.line_height = 16u;
    result = astra_scroll_init(model, &model_info);
    if (result != ASTRA_OK) return result;

    view.id = EXAMPLE_SCROLL_VIEW;
    view.model = model;
    view.preferred_width = width > 10u ? width - 10u : 1u;
    view.preferred_height = height;
    result = astra_interface_scroll_view_init(&controls[0], &view);
    if (result != ASTRA_OK) return result;

    content.id = EXAMPLE_SCROLL_CONTENT;
    content.layout.direction = ASTRA_FLEX_COLUMN;
    result = astra_interface_container_init(&controls[1], &content);
    if (result != ASTRA_OK) return result;
    item.parent_id = EXAMPLE_SCROLL_VIEW;
    item.minimum_height = 600u;
    result = astra_interface_control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;

    label.id = EXAMPLE_SCROLL_LABEL;
    label.text = "Scrollable UTF-8 content";
    label.text_length = sizeof("Scrollable UTF-8 content") - 1u;
    result = astra_interface_label_init(&controls[2], &label);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_SCROLL_CONTENT;
    result = astra_interface_control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;

    bar.id = EXAMPLE_SCROLLBAR;
    bar.model = model;
    bar.orientation = ASTRA_ORIENTATION_VERTICAL;
    result = astra_interface_scrollbar_init(&controls[3], &bar);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.minimum_width = 10u;
    item.maximum_width = 10u;
    result = astra_interface_control_set_flex(&controls[3], &item);
    if (result != ASTRA_OK) return result;

    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.grow = 1u;
    result = astra_interface_control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;
    result = astra_interface_ui_init(ui, controls, 4u, width, height);
    if (result != ASTRA_OK) return result;
    root.direction = ASTRA_FLEX_ROW;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    return astra_interface_ui_layout(ui, &root);
}

ASTRA_PROGRAM("interface_scroll", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");

/* Turn the wheel one notch down over the viewport: the shared model scrolls
   one line, and the scrollbar bound to it follows. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    AstraScrollModel model = ASTRA_SCROLL_MODEL_INIT;
    AstraScrollState state = ASTRA_SCROLL_STATE_INIT;
    AstraControl controls[4];
    AstraUIContext ui;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    AstraWindowEvent wheel = {0};

    if (output == 0 ||
        astra_example_build_scroll(&model, controls, &ui, 320u, 200u) !=
            ASTRA_OK)
        return 1;
    wheel.size = sizeof(wheel);
    wheel.version = ASTRA_WINDOW_EVENT_VERSION;
    wheel.type = ASTRA_WINDOW_EVENT_POINTER_WHEEL;
    wheel.data.wheel.x = 20;
    wheel.data.wheel.y = 20;
    wheel.data.wheel.delta_y = -1;
    if (astra_interface_ui_handle_event(&ui, &wheel, &action) != ASTRA_OK ||
        action.type != ASTRA_UI_ACTION_SCROLL_CHANGED ||
        astra_scroll_get_state(&model, &state) != ASTRA_OK ||
        state.offset_y != state.line_height)
        return 2;
    (void)astra_print(output->handle, "interface_scroll: one wheel notch "
                                      "scrolled to ");
    (void)astra_print_u32(output->handle, state.offset_y);
    (void)astra_print(output->handle, " of ");
    (void)astra_print_u32(output->handle, state.maximum_y);
    (void)astra_print(output->handle, "\n");
    return 0;
}
