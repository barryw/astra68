#include <astra/interface_kit.h>

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
