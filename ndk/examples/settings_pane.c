#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("settings_pane", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");

/* The shell supplies layout only. The owning service still validates/saves
   system policy; an application uses its own Config Kit grant. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output = astra_startup_capability(startup, "STDOUT");
    AstraControl controls[6];
    AstraSettingsPaneInfo pane = ASTRA_SETTINGS_PANE_INFO_INIT;
    AstraToggleInfo option = ASTRA_TOGGLE_INFO_INIT;
    AstraButtonInfo button = ASTRA_BUTTON_INFO_INIT;
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    AstraFlexLayout root = ASTRA_SETTINGS_PANE_LAYOUT_INIT;
    AstraUIContext ui = ASTRA_UI_CONTEXT_INIT;
    AstraControlFrame frame, clip;

    pane.first_id = 1u;
    pane.title = "Preferences";
    pane.title_length = sizeof("Preferences") - 1u;
    pane.description = "Shared layout and controls";
    pane.description_length = sizeof("Shared layout and controls") - 1u;
    if (output == 0 || astra_interface_settings_pane_init(controls, &pane) != ASTRA_OK)
        return 1;
    option.id = 5u;
    option.text = "Show details";
    option.text_length = sizeof("Show details") - 1u;
    item.parent_id = pane.first_id + ASTRA_SETTINGS_PANE_BODY;
    if (astra_interface_checkbox_init(&controls[4], &option) != ASTRA_OK ||
        astra_interface_control_set_flex(&controls[4], &item) != ASTRA_OK)
        return 2;
    button.id = 6u;
    button.text = "Apply";
    button.text_length = sizeof("Apply") - 1u;
    item.parent_id = pane.first_id + ASTRA_SETTINGS_PANE_ACTIONS;
    if (astra_interface_button_init(&controls[5], &button) != ASTRA_OK ||
        astra_interface_control_set_flex(&controls[5], &item) != ASTRA_OK ||
        astra_interface_ui_init(&ui, controls, 6u, 640u, 480u) != ASTRA_OK ||
        astra_interface_ui_layout(&ui, &root) != ASTRA_OK ||
        astra_interface_ui_geometry(&ui, 5u, &frame, &clip) != ASTRA_OK)
        return 3;
    (void)astra_print(output->handle, "settings_pane: shared Labels, Containers and controls\n");
    return 0;
}
