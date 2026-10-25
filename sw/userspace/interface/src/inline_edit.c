#include <astra/inline_edit.h>
#include <astra/interface_library.h>

#include <astra/draw_list.h>
#include <astra/input.h>
#include <astra/theme.h>

#include <string.h>

AstraResult astra_interface_inline_edit_start(AstraInlineEdit *edit, AstraDisplay *display,
    const char *initial)
{
    if (edit == NULL || display == NULL || initial == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    size_t length = strlen(initial);
    if (length >= sizeof(edit->content))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    edit->active = 0u;
    if (edit->surface._private_handle == ASTRA_INVALID_HANDLE) {
        AstraSurfaceCreateInfo info = ASTRA_SURFACE_CREATE_INFO_INIT;
        info.width = ASTRA_INLINE_EDIT_WIDTH;
        info.height = ASTRA_INLINE_EDIT_HEIGHT;
        info.format = ASTRA_PIXEL_FORMAT_RGB565;
        info.flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_DRAW_SOURCE;
        AstraResult result = astra_surface_create(display, &info, &edit->surface);
        if (result != ASTRA_OK) return result;
    }
    edit->model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo model = ASTRA_TEXT_MODEL_INFO_INIT;
    model.text = initial;
    model.text_bytes = (uint32_t)length;
    model.content_arena = edit->content;
    model.content_arena_bytes = sizeof(edit->content);
    model.metadata_arena = edit->metadata;
    model.metadata_arena_bytes = sizeof(edit->metadata);
    model.selection.anchor = 0u;
    model.selection.focus = (uint32_t)length;
    AstraResult result = astra_text_model_init(&edit->model, &model);
    if (result != ASTRA_OK) return result;
    edit->field = (AstraControl)ASTRA_CONTROL_INIT;
    AstraFieldInfo field = ASTRA_FIELD_INFO_INIT;
    field.id = 1u;
    field.model = &edit->model;
    field.preferred_columns = 12u;
    result = astra_interface_field_init(&edit->field, &field);
    if (result != ASTRA_OK) return result;
    edit->ui = (AstraUIContext)ASTRA_UI_CONTEXT_INIT;
    result = astra_interface_ui_init(&edit->ui, &edit->field, 1u,
        ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT);
    AstraFlexLayout layout = ASTRA_FLEX_LAYOUT_INIT;
    if (result == ASTRA_OK) result = astra_interface_ui_layout(&edit->ui, &layout);
    if (result != ASTRA_OK) return result;
    AstraWindowEvent focus = {.size = sizeof(focus),
        .version = ASTRA_WINDOW_EVENT_VERSION, .type = ASTRA_WINDOW_EVENT_KEY,
        .flags = ASTRA_WINDOW_EVENT_DOWN};
    focus.data.key.usage = 0x2bu;
    AstraUIAction action = ASTRA_UI_ACTION_INIT;
    result = astra_interface_ui_handle_event(&edit->ui, &focus, &action);
    if (result == ASTRA_OK) edit->active = 1u;
    return result;
}

AstraResult astra_interface_inline_edit_event(AstraInlineEdit *edit,
    const AstraWindowEvent *event, AstraUIAction *action)
{
    if (edit == NULL || event == NULL || action == NULL || !edit->active)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (event->type == ASTRA_WINDOW_EVENT_KEY &&
        (event->flags & ASTRA_WINDOW_EVENT_DOWN) != 0u &&
        event->data.key.usage == 0x29u) {
        *action = (AstraUIAction)ASTRA_UI_ACTION_INIT;
        action->type = ASTRA_UI_ACTION_DISMISS;
        return ASTRA_OK;
    }
    return astra_interface_ui_handle_event(&edit->ui, event, action);
}

AstraResult astra_interface_inline_edit_render(AstraInlineEdit *edit, uint32_t *commands,
    uint32_t command_bytes)
{
    if (edit == NULL || !edit->active || commands == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraSurfaceView view;
    if (!astra_draw_list_view_init(&view, commands, command_bytes,
        ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_interface_ui_render(&edit->ui, &view);
    if (result != ASTRA_OK) return result;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraRectI32 whole = {0, 0, ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT};
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    paint.foreground = ((AstraTheme)ASTRA_THEME_SYSTEM_INIT).canvas;
    result = astra_draw_list_create(&edit->surface, &whole, &list);
    if (result == ASTRA_OK) result = astra_draw_rectangle(&list, &whole, 1, &paint);
    if (result == ASTRA_OK) result = astra_draw_list_append_view(&list, &view);
    if (result == ASTRA_OK) result = astra_draw_post(&list, ASTRA_DRAW_POST_PRESENT);
    if (list._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_draw_list_close(&list);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

AstraResult astra_interface_inline_edit_text(const AstraInlineEdit *edit,
    char *text, uint32_t capacity)
{
    if (edit == NULL || text == NULL || capacity == 0u || !edit->active)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraResult result = astra_text_model_get_state(&edit->model, &state);
    if (result != ASTRA_OK) return result;
    if (state.text_bytes >= capacity) return ASTRA_ERROR_BUFFER_TOO_SMALL;
    uint32_t copied = 0u;
    result = astra_text_model_copy(&edit->model, 0u, state.text_bytes,
        text, capacity - 1u, &copied);
    if (result == ASTRA_OK) text[copied] = '\0';
    return result;
}

void astra_interface_inline_edit_stop(AstraInlineEdit *edit)
{
    if (edit != NULL) edit->active = 0u;
}
