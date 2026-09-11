#include <astra/text_model.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_text(const AstraTextModel *model, const char *expected,
                        uint32_t expected_bytes)
{
    char actual[256];
    uint32_t bytes = UINT32_MAX;

    assert(astra_text_model_copy(model, 0u, expected_bytes, actual,
                                 sizeof(actual), &bytes) == ASTRA_OK);
    assert(bytes == expected_bytes);
    assert(memcmp(actual, expected, bytes) == 0);
    assert(astra_text_model_validate(model) == ASTRA_OK);
}

static void test_piece_table_and_lines(void)
{
    static const char initial[] = "zero\none\n\xe4\xb8\x96\xe7\x95\x8c";
    uint8_t content[256];
    _Alignas(4) uint8_t metadata[256];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextLine line = ASTRA_TEXT_LINE_INIT;
    AstraTextSelection selection = ASTRA_TEXT_SELECTION_INIT;
    AstraTextModelRequirements requirements =
        ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;
    const char *span;
    uint32_t span_bytes;
    uint32_t copied;
    char output[64] = "unchanged";

    info.text = initial;
    info.text_bytes = sizeof(initial) - 1u;
    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    info.selection.anchor = 5u;
    info.selection.focus = 8u;
    assert(astra_text_model_init(&model, &info) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.generation == 1u && state.text_bytes == 15u);
    assert(state.line_count == 3u && state.piece_count == 1u);
    assert(state.selection.anchor == 5u && state.selection.focus == 8u);

    assert(astra_text_model_get_line(&model, 0u, &line) == ASTRA_OK);
    assert(line.start == 0u && line.content_end == 4u && line.end == 5u);
    assert(astra_text_model_get_line(&model, 1u, &line) == ASTRA_OK);
    assert(line.start == 5u && line.content_end == 8u && line.end == 9u);
    assert(astra_text_model_get_line(&model, 2u, &line) == ASTRA_OK);
    assert(line.start == 9u && line.content_end == 15u && line.end == 15u);
    assert(astra_text_model_get_line(&model, 3u, &line) ==
           ASTRA_ERROR_NOT_PRESENT);

    assert(astra_text_model_copy(&model, 5u, 8u, output, 2u, &copied) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(copied == 3u && memcmp(output, "unchanged", 9u) == 0);
    assert(astra_text_model_copy(&model, 5u, 8u, NULL, 0u, &copied) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(copied == 3u);
    assert(astra_text_model_copy(&model, 5u, 8u, output,
                                 sizeof(output), &copied) == ASTRA_OK);
    assert(copied == 3u && memcmp(output, "one", 3u) == 0);

    assert(astra_text_model_read(&model, 0u, &span, &span_bytes) == ASTRA_OK);
    assert(span_bytes == sizeof(initial) - 1u &&
           memcmp(span, initial, span_bytes) == 0);
    assert(astra_text_model_read(&model, 15u, &span, &span_bytes) == ASTRA_OK);
    assert(span == NULL && span_bytes == 0u);
    copied = 9u;
    assert(astra_text_model_scalar_advance(&model, &copied) == ASTRA_OK);
    assert(copied == 12u);
    assert(astra_text_model_scalar_advance(&model, &copied) == ASTRA_OK);
    assert(copied == 15u);
    assert(astra_text_model_scalar_advance(&model, &copied) ==
           ASTRA_ERROR_NOT_PRESENT && copied == 15u);
    assert(astra_text_model_scalar_retreat(&model, &copied) == ASTRA_OK);
    assert(copied == 12u);
    copied = 10u;
    assert(astra_text_model_scalar_retreat(&model, &copied) ==
           ASTRA_ERROR_INVALID_ARGUMENT && copied == 10u);

    selection.anchor = 10u;
    selection.focus = 15u;
    assert(astra_text_model_set_selection(&model, &selection) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    selection.anchor = 9u;
    assert(astra_text_model_set_selection(&model, &selection) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.generation == 2u);

    assert(astra_text_model_replace_requirements(
               &model, 5u, 8u, "one-two", 7u, &requirements) == ASTRA_OK);
    assert(requirements.text_bytes == 19u && requirements.line_count == 3u);
    assert(requirements.piece_count == 3u);
    assert(astra_text_model_replace(&model, 5u, 8u, "one-two", 7u) ==
           ASTRA_OK);
    assert_text(&model, "zero\none-two\n\xe4\xb8\x96\xe7\x95\x8c", 19u);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.selection.anchor == 12u && state.selection.focus == 12u);

    assert(astra_text_model_replace(&model, 4u, 13u, " / ", 3u) == ASTRA_OK);
    assert_text(&model, "zero / \xe4\xb8\x96\xe7\x95\x8c", 13u);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.line_count == 1u && state.selection.anchor == 7u &&
           state.selection.focus == 7u);
    assert(astra_text_model_get_line(&model, 0u, &line) == ASTRA_OK);
    assert(line.start == 0u && line.content_end == 13u && line.end == 13u);

    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    copied = state.content_bytes_used;
    astra_text_model_dispose(&model);
    for (uint32_t at = 0u; at < copied; ++at)
        assert(content[at] == 0u);
    assert(model._private_content == NULL && model._private_generation == 0u);
}

static void test_atomic_capacity_and_growth(void)
{
    uint8_t content[5] = {0};
    _Alignas(4) uint8_t metadata[20] = {0};
    uint8_t grown_content[64] = {0};
    _Alignas(4) uint8_t grown_metadata[64] = {0};
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState before = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextModelState after = ASTRA_TEXT_MODEL_STATE_INIT;
    AstraTextModelRequirements requirements =
        ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;

    info.text = "abc";
    info.text_bytes = 3u;
    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &info) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &before) == ASTRA_OK);
    assert(astra_text_model_replace_requirements(
               &model, 1u, 1u, "XYZ", 3u, &requirements) == ASTRA_OK);
    assert(requirements.content_arena_bytes == 6u);
    assert(requirements.metadata_arena_bytes == 28u);
    assert(astra_text_model_replace(&model, 1u, 1u, "XYZ", 3u) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(astra_text_model_get_state(&model, &after) == ASTRA_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert_text(&model, "abc", 3u);

    assert(astra_text_model_move_arenas(
               &model, grown_content, sizeof(grown_content),
               grown_metadata, sizeof(grown_metadata)) == ASTRA_OK);
    assert(astra_text_model_replace(&model, 1u, 1u, "XYZ", 3u) == ASTRA_OK);
    assert_text(&model, "aXYZbc", 6u);
    assert(astra_text_model_get_state(&model, &after) == ASTRA_OK);
    assert(after.piece_count == 3u && after.line_count == 1u);
    assert(after.content_bytes_used == 6u);
    assert(after.generation == before.generation + 2u);
    for (uint32_t at = 0u; at < sizeof(content); ++at)
        assert(content[at] == 0u);

    assert(astra_text_model_move_arenas(
               &model, grown_content, sizeof(grown_content),
               grown_metadata, sizeof(grown_metadata)) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_utf8_and_many_pieces(void)
{
    uint8_t content[4096] = {0};
    _Alignas(4) uint8_t metadata[16384] = {0};
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;

    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &info) == ASTRA_OK);
    assert(astra_text_model_replace(&model, 0u, 0u, "\xf0\x9f\x8c\x9f", 4u) ==
           ASTRA_OK);
    assert(astra_text_model_replace(&model, 1u, 1u, "x", 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_text_model_replace(&model, 0u, 0u, "\xc0\x80", 2u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    for (uint32_t at = 0u; at < 512u; ++at) {
        uint32_t position = (at & 1u) == 0u ? 0u : 4u + at;

        assert(astra_text_model_replace(&model, position, position,
                                        "x", 1u) == ASTRA_OK);
    }
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.text_bytes == 516u && state.piece_count > 256u);
    assert(state.line_count == 1u);
    assert(astra_text_model_validate(&model) == ASTRA_OK);
}

static void test_invalid_initial_state(void)
{
    uint8_t content[8];
    _Alignas(4) uint8_t metadata[16];
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;

    info.text = "\xed\xa0\x80";
    info.text_bytes = 3u;
    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info.text = NULL;
    info.text_bytes = 0u;
    info.metadata_arena_bytes = 3u;
    assert(astra_text_model_init(&model, &info) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
}

int main(void)
{
    test_piece_table_and_lines();
    test_atomic_capacity_and_growth();
    test_utf8_and_many_pieces();
    test_invalid_initial_state();
    puts("text model tests passed");
    return 0;
}
