#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

/* One control in this example; the application owns its command scope/items. */
AstraResult astra_example_menu_event(AstraContextMenu *menu, AstraUIContext *ui,
    AstraWindow *window, const AstraCommandScope *scope,
    const AstraContextMenuItem *items, uint32_t count,
    const AstraWindowEvent *event, AstraUIAction *action)
{
    int handled = 0;
    AstraResult result;
    if (action == NULL || action->size < sizeof(*action)) return ASTRA_ERROR_INVALID_ARGUMENT;
    *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    result = astra_interface_context_menu_handle_event(menu, event, &handled);
    if (handled || result != ASTRA_OK) return result;
    result = astra_interface_ui_handle_event(ui, event, action);
    if (result == ASTRA_OK && action->type == ASTRA_UI_ACTION_CONTEXT_MENU) {
        int32_t x, y;
        if (event->type == ASTRA_WINDOW_EVENT_POINTER_BUTTON) {
            x = event->data.pointer.x;
            y = event->data.pointer.y;
        } else {
            AstraControlFrame frame, clip;
            result = astra_interface_ui_geometry(ui, 0u, &frame, &clip);
            if (result != ASTRA_OK) return result;
            x = clip.x;
            y = clip.y;
        }
        return astra_interface_context_menu_open(menu, window, scope, items, count, x, y);
    }
    return result;
}

static AstraResult query(void *context, AstraCommandState *state)
{
    (void)context;
    *state = (AstraCommandState){"Open", 4u, 1u, 0u};
    return ASTRA_OK;
}
static AstraResult open(void *context, const AstraCommandArguments *args)
{
    (void)args;
    ++*(uint32_t *)context;
    return ASTRA_OK;
}
ASTRA_PROGRAM("interface_menu", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

/* The same control works without a GUI window, useful for behavioral checks. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output = astra_startup_capability(startup, "STDOUT");
    const AstraMenuItem rows[] = {{"Disabled", 8u, ASTRA_MENU_DISABLED},
        {"Open", 4u, 0u}};
    const AstraControlFrame work = {0, 34, 640, 400};
    AstraMenu menu = ASTRA_MENU_INIT;
    AstraWindowEvent event = {.size = sizeof(event), .version = ASTRA_WINDOW_EVENT_VERSION,
        .type = ASTRA_WINDOW_EVENT_KEY, .flags = ASTRA_WINDOW_EVENT_DOWN};
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    uint32_t calls = 0u;
    const AstraCommand command = {"open", 4u, 0u, 0u, query, open, &calls};
    const AstraCommandScope scope = {NULL, &command, 1u};

    if (output == NULL || astra_interface_menu_init(&menu, rows, 2u,
        &work, 20, 60, 160u, 0u) != ASTRA_OK) return 1;
    event.data.key.usage = 0x51u;
    if (astra_interface_menu_handle_event(&menu, &event, &action) != ASTRA_OK) return 2;
    event.data.key.usage = 0x28u;
    if (astra_interface_menu_handle_event(&menu, &event, &action) != ASTRA_OK ||
        action.type != ASTRA_UI_ACTION_ACTIVATE || action.control_id != 2u ||
        astra_interface_command_invoke(&scope, "open", 4u, NULL) != ASTRA_OK || calls != 1u)
        return 3;
    (void)astra_print(output->handle, "interface_menu: disabled rows skipped; Open invoked\n");
    return 0;
}
