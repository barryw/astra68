#include "line_input.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    char content[4096];
    uint32_t metadata[1024];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &info) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, 'l', 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, 'o', 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, 'k', 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_LEFT, 0u, 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, 'o', 8u) == ASTRA_OK);
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    char output[32];
    uint32_t bytes = 0u;
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(astra_text_model_copy(&model, 0u, state.text_bytes, output,
        sizeof(output), &bytes) == ASTRA_OK);
    assert(bytes == 4u && memcmp(output, "look", 4u) == 0);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_HOME, 0u, 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, 0xe9u, 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_BACKSPACE, 0u, 8u) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.text_bytes == 4u && state.selection.focus == 0u);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_END, 0u, 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_DELETE, 0u, 8u) == ASTRA_OK);
    assert(astraz_line_input_edit(&model, ASTRAZ_EDIT_INSERT, '!', 5u) ==
        ASTRA_ERROR_BUFFER_TOO_SMALL);
    astra_text_model_dispose(&model);
    puts("AstraZ inline line input: PASS");
    return 0;
}
