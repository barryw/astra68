#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

#include <stddef.h>

enum {
    EXAMPLE_TABS = 1u,
    EXAMPLE_FIRST_PAGE,
    EXAMPLE_SECOND_PAGE,
    EXAMPLE_FIRST_CONTENT,
    EXAMPLE_SECOND_CONTENT
};

/* Build two resize-responsive pages selected by one shared tab control. */
AstraResult astra_example_build_tabs(
    AstraControl controls[5], AstraUIContext *ui,
    uint16_t width, uint16_t height)
{
    static const AstraChoiceItem pages[] = {
        {"General", sizeof("General") - 1u, {0u, 0u}},
        {"Advanced", sizeof("Advanced") - 1u, {0u, 0u}}
    };
    AstraChoiceInfo tabs = ASTRA_CHOICE_INFO_INIT;
    AstraContainerInfo page = ASTRA_CONTAINER_INFO_INIT;
    AstraLabelInfo label = ASTRA_LABEL_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (controls == NULL || ui == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    tabs.id = EXAMPLE_TABS;
    tabs.items = pages;
    tabs.item_count = sizeof(pages) / sizeof(pages[0]);
    result = astra_interface_tab_init(&controls[0], &tabs);
    if (result != ASTRA_OK) return result;

    page.id = EXAMPLE_FIRST_PAGE;
    page.layout.direction = ASTRA_FLEX_COLUMN;
    result = astra_interface_container_init(&controls[1], &page);
    if (result != ASTRA_OK) return result;
    page.id = EXAMPLE_SECOND_PAGE;
    page.state = ASTRA_CONTROL_COLLAPSED;
    result = astra_interface_container_init(&controls[2], &page);
    if (result != ASTRA_OK) return result;

    label.id = EXAMPLE_FIRST_CONTENT;
    label.text = "General settings";
    label.text_length = sizeof("General settings") - 1u;
    result = astra_interface_label_init(&controls[3], &label);
    if (result != ASTRA_OK) return result;
    label.id = EXAMPLE_SECOND_CONTENT;
    label.text = "Advanced settings";
    label.text_length = sizeof("Advanced settings") - 1u;
    result = astra_interface_label_init(&controls[4], &label);
    if (result != ASTRA_OK) return result;

    item.flags = ASTRA_FLEX_BREAK_AFTER;
    result = astra_interface_control_set_flex(&controls[0], &item);
    if (result != ASTRA_OK) return result;
    item.grow = 1u;
    result = astra_interface_control_set_flex(&controls[1], &item);
    if (result != ASTRA_OK) return result;
    result = astra_interface_control_set_flex(&controls[2], &item);
    if (result != ASTRA_OK) return result;
    item = (AstraFlexItem)ASTRA_FLEX_ITEM_INIT;
    item.parent_id = EXAMPLE_FIRST_PAGE;
    result = astra_interface_control_set_flex(&controls[3], &item);
    if (result != ASTRA_OK) return result;
    item.parent_id = EXAMPLE_SECOND_PAGE;
    result = astra_interface_control_set_flex(&controls[4], &item);
    if (result != ASTRA_OK) return result;

    result = astra_interface_ui_init(ui, controls, 5u, width, height);
    if (result != ASTRA_OK) return result;
    root.direction = ASTRA_FLEX_COLUMN;
    root.align_items = ASTRA_FLEX_ALIGN_STRETCH;
    return astra_interface_ui_layout(ui, &root);
}

/* Apply one tab action; collapsed pages leave layout and focus atomically. */
AstraResult astra_example_select_tab(
    AstraUIContext *ui, AstraControl controls[5], uint32_t *active_page,
    const AstraUIAction *action)
{
    uint32_t next;
    AstraResult result;

    if (ui == NULL || controls == NULL ||
        active_page == NULL || action == NULL ||
        action->type != ASTRA_UI_ACTION_VALUE_CHANGED ||
        action->control_id != EXAMPLE_TABS || action->value < 0 ||
        action->value > 1 || *active_page > 1u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    next = (uint32_t)action->value;
    if (next == *active_page) return ASTRA_OK;
    result = astra_interface_ui_set_state(ui, &controls[1u + next], 0u);
    if (result != ASTRA_OK) return result;
    result = astra_interface_ui_set_state(
        ui, &controls[1u + *active_page], ASTRA_CONTROL_COLLAPSED);
    if (result != ASTRA_OK) {
        (void)astra_interface_ui_set_state(
            ui, &controls[1u + next], ASTRA_CONTROL_COLLAPSED);
        return result;
    }
    *active_page = next;
    return ASTRA_OK;
}

ASTRA_PROGRAM("interface_tabs", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");

enum { KEY_RIGHT = 0x4fu, KEY_TAB = 0x2bu };

/* Deliver one key press (down, then up) and report the last action. */
static AstraResult press(AstraUIContext *ui, uint32_t usage,
                         AstraUIAction *action)
{
    AstraWindowEvent event = {0};
    AstraResult result;

    event.size = sizeof(event);
    event.version = ASTRA_WINDOW_EVENT_VERSION;
    event.type = ASTRA_WINDOW_EVENT_KEY;
    event.flags = ASTRA_WINDOW_EVENT_DOWN;
    event.data.key.usage = usage;
    *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    result = astra_interface_ui_handle_event(ui, &event, action);
    if (result != ASTRA_OK || action->type != ASTRA_UI_ACTION_NONE)
        return result;
    event.flags = 0u;
    return astra_interface_ui_handle_event(ui, &event, action);
}

/* Tab to the tab strip and press Right: the second page replaces the first. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    AstraControl controls[5];
    AstraUIContext ui;
    AstraUIAction action;
    uint32_t active_page = 0u;

    if (output == 0 ||
        astra_example_build_tabs(controls, &ui, 480u, 320u) != ASTRA_OK)
        return 1;
    if (press(&ui, KEY_TAB, &action) != ASTRA_OK ||
        press(&ui, KEY_RIGHT, &action) != ASTRA_OK ||
        astra_example_select_tab(&ui, controls, &active_page, &action) !=
            ASTRA_OK || active_page != 1u)
        return 2;
    (void)astra_print(output->handle, "interface_tabs: Tab, Right showed "
                                      "Advanced and collapsed General\n");
    return 0;
}
