#include <astra/interface_kit.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

/* Build one editable UTF-8 field over caller-owned, replaceable arenas. */
AstraResult astra_example_build_text_field(
    AstraTextModel *model, AstraControl *field, AstraUIContext *ui, void *content,
    uint32_t content_bytes, void *metadata, uint32_t metadata_bytes,
    uint16_t width, uint16_t height)
{
    static const char initial[] = "/home//projects/\xe4\xb8\x96\xe7\x95\x8c";
    AstraTextModelInfo model_info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraFieldInfo field_info = ASTRA_FIELD_INFO_INIT;
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    AstraResult result;

    if (model == 0 || field == 0 || ui == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    model_info.text = initial;
    model_info.text_bytes = sizeof(initial) - 1u;
    model_info.content_arena = content;
    model_info.content_arena_bytes = content_bytes;
    model_info.metadata_arena = metadata;
    model_info.metadata_arena_bytes = metadata_bytes;
    model_info.selection.anchor = model_info.text_bytes;
    model_info.selection.focus = model_info.text_bytes;
    result = astra_text_model_init(model, &model_info);
    if (result != ASTRA_OK) return result;

    *field = (AstraControl)ASTRA_CONTROL_INIT;
    field_info.id = 1u;
    field_info.model = model;
    field_info.preferred_columns = 28u;
    result = astra_interface_field_init(field, &field_info);
    if (result != ASTRA_OK) return result;
    *ui = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    result = astra_interface_ui_init(ui, field, 1u, width, height);
    if (result != ASTRA_OK) return result;
    layout.padding_left = layout.padding_top = 24u;
    layout.padding_right = layout.padding_bottom = 24u;
    return astra_interface_ui_layout(ui, &layout);
}

ASTRA_PROGRAM("text_field", 1, 0, 0, "Your Name",
              "Copyright 2026 Your Name");


/* Deliver one key press (down, then up) and report the last action. */
static AstraResult key_press(AstraUIContext *ui, uint32_t usage,
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

enum { KEY_TAB = 0x2bu };

/* Type one character into a focused field: it lands at the caret. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    static uint32_t content[64];
    static uint32_t metadata[64];
    AstraTextModel model;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraControl field;
    AstraUIContext ui;
    AstraUIAction action;
    AstraWindowEvent typed = {0};
    char text[32];
    uint32_t bytes = 0u;

    if (output == 0 ||
        astra_example_build_text_field(&model, &field, &ui, content,
                                       sizeof(content), metadata,
                                       sizeof(metadata), 320u, 80u) !=
            ASTRA_OK ||
        key_press(&ui, KEY_TAB, &action) != ASTRA_OK)
        return 1;
    typed.size = sizeof(typed);
    typed.version = ASTRA_WINDOW_EVENT_VERSION;
    typed.type = ASTRA_WINDOW_EVENT_TEXT;
    typed.data.text.codepoint = '!';
    action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
    if (astra_interface_ui_handle_event(&ui, &typed, &action) != ASTRA_OK ||
        astra_text_model_get_state(&model, &state) != ASTRA_OK ||
        astra_text_model_copy(&model, 0u, state.text_bytes, text,
                              sizeof(text) - 1u, &bytes) != ASTRA_OK)
        return 2;
    text[bytes] = '\0';
    (void)astra_print(output->handle, "text_field: ");
    (void)astra_print(output->handle, text);
    (void)astra_print(output->handle, " (");
    (void)astra_print_u32(output->handle, bytes);
    (void)astra_print(output->handle, " bytes)\n");
    return 0;
}
