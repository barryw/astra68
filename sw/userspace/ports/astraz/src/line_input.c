#include "line_input.h"

#include <astra/utf8.h>

#include <string.h>

static int count_scalars(const char *text, uint32_t bytes,
    uint32_t start, uint32_t end, uint32_t *count)
{
    uint32_t offset = start;
    *count = 0u;
    while (offset < end) {
        if (!astra_utf8_scalar_advance(text, bytes, &offset)) return 0;
        ++*count;
    }
    return offset == end;
}

AstraResult astraz_line_input_edit(AstraTextModel *model, uint32_t operation,
    uint32_t scalar, uint32_t maximum_scalars)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    char text[4096], replacement[4];
    uint32_t bytes = 0u;
    if (model == NULL || maximum_scalars == 0u ||
        astra_text_model_get_state(model, &state) != ASTRA_OK ||
        state.text_bytes >= sizeof(text) ||
        astra_text_model_copy(model, 0u, state.text_bytes, text,
            sizeof(text), &bytes) != ASTRA_OK) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t first = state.selection.anchor < state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    uint32_t last = state.selection.anchor > state.selection.focus ?
        state.selection.anchor : state.selection.focus;
    uint32_t focus = state.selection.focus;
    if (operation == ASTRAZ_EDIT_LEFT || operation == ASTRAZ_EDIT_RIGHT ||
        operation == ASTRAZ_EDIT_HOME || operation == ASTRAZ_EDIT_END) {
        if (operation == ASTRAZ_EDIT_HOME) focus = 0u;
        else if (operation == ASTRAZ_EDIT_END) focus = bytes;
        else if (first != last)
            focus = operation == ASTRAZ_EDIT_LEFT ? first : last;
        else if (operation == ASTRAZ_EDIT_LEFT)
            (void)astra_utf8_scalar_retreat(text, bytes, &focus);
        else
            (void)astra_utf8_scalar_advance(text, bytes, &focus);
        AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
        selection.anchor = selection.focus = focus;
        return astra_text_model_set_selection(model, &selection);
    }
    if (operation == ASTRAZ_EDIT_BACKSPACE && first == last) {
        (void)astra_utf8_scalar_retreat(text, bytes, &first);
    } else if (operation == ASTRAZ_EDIT_DELETE && first == last) {
        (void)astra_utf8_scalar_advance(text, bytes, &last);
    }
    uint32_t replacement_bytes = 0u;
    if (operation == ASTRAZ_EDIT_INSERT) {
        if (scalar < 32u || !astra_unicode_scalar_valid(scalar))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        replacement_bytes = astra_utf8_encode(scalar, replacement);
    } else if (operation != ASTRAZ_EDIT_BACKSPACE &&
               operation != ASTRAZ_EDIT_DELETE) {
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    uint32_t total = 0u, removed = 0u;
    if (!count_scalars(text, bytes, 0u, bytes, &total) ||
        !count_scalars(text, bytes, first, last, &removed))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (total - removed + (replacement_bytes != 0u) >= maximum_scalars)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    return astra_text_model_replace(model, first, last,
        replacement_bytes ? replacement : NULL, replacement_bytes);
}
