#include "transcript_flow.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(astraz_transcript_without_prompt("Room\n> \n", 8u) == 5u);
    assert(astraz_transcript_without_prompt("Room\n  >\n", 9u) == 5u);
    assert(astraz_transcript_without_prompt("A > B\n", 6u) == 6u);
    uint16_t input[4] = {0};
    assert(astraz_input_copy("abc", 3u, input, 4u));
    assert(input[0] == 'a' && input[1] == 'b' &&
           input[2] == 'c' && input[3] == 0u);
    assert(!astraz_input_copy("abcd", 4u, input, 4u));
    assert(astraz_input_copy("\xc3\xa9x", 3u, input, 3u));
    assert(input[0] == 0xe9u && input[1] == 'x' && input[2] == 0u);
    assert(!astraz_input_copy("\xc3\xa9xy", 4u, input, 3u));
    AstraZTranscriptFlow flow = {0};
    assert(astraz_transcript_flow_flush_due(&flow, UINT64_C(1000000000)));
    assert(!astraz_transcript_flow_flush_due(&flow, UINT64_C(1010000000)));
    assert(astraz_transcript_flow_flush_due(&flow, UINT64_C(1034000000)));
    astraz_transcript_flow_reset(&flow);
    char decoded[64];
    uint32_t decoded_bytes = 0u;
    const uint16_t first_word[] = {'A', ' ', 'l', 'o', 'n', 'g'};
    const uint16_t second_word[] = {' ', 'r', 'o', 'o', 'm'};
    assert(astraz_transcript_flow_text(&flow, first_word, 6u, decoded,
        sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 6u && memcmp(decoded, "A long", 6u) == 0);
    assert(astraz_transcript_flow_text(&flow, second_word, 5u, decoded,
        sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 5u && memcmp(decoded, " room", 5u) == 0);
    astraz_transcript_flow_new_line(&flow);
    const uint16_t paragraph[] = {9u, 'N', 'e', 'x', 't', 11u, 'x'};
    assert(astraz_transcript_flow_text(&flow, paragraph, 7u, decoded,
        sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 10u && memcmp(decoded, "   Next  x", 10u) == 0);
    const uint16_t style_start[] = {1u};
    const uint16_t style_end[] = {4u, '!', 2u, 5u, 0xe9u};
    assert(astraz_transcript_flow_text(&flow, style_start, 1u, decoded,
        sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 0u && flow.skip_marker_argument);
    assert(astraz_transcript_flow_text(&flow, style_end, 5u, decoded,
        sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 3u && memcmp(decoded, "!\xc3\xa9", 3u) == 0);
    const uint16_t zero_style_argument[] = {1u, 0u, 'B', 'a', 't', 'h'};
    assert(astraz_transcript_flow_text(&flow, zero_style_argument, 6u,
        decoded, sizeof(decoded), &decoded_bytes) == ASTRA_OK);
    assert(decoded_bytes == 4u && memcmp(decoded, "Bath", 4u) == 0);
    AstraZTranscriptFlow previous = flow;
    assert(astraz_transcript_flow_text(&flow, paragraph, 7u, decoded,
        2u, &decoded_bytes) == ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(memcmp(&flow, &previous, sizeof(flow)) == 0);

    char transcript[64] = "First\n";
    char content[64];
    uint32_t metadata[64];
    char copied[64];
    uint32_t copied_bytes = 0u;
    AstraTextModel model = ASTRA_TEXT_MODEL_INIT;
    AstraTextModelInfo info = ASTRA_TEXT_MODEL_INFO_INIT;
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    info.text = transcript;
    info.text_bytes = 6u;
    info.content_arena = content;
    info.content_arena_bytes = sizeof(content);
    info.metadata_arena = metadata;
    info.metadata_arena_bytes = sizeof(metadata);
    assert(astra_text_model_init(&model, &info) == ASTRA_OK);
    memcpy(transcript + 6u, "Second\n", 7u);
    info.text_bytes = 13u;
    assert(astraz_transcript_model_sync(&model, &info, 0u) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.generation == 2u && state.text_bytes == 13u &&
           state.content_bytes_used == 13u && state.piece_count == 1u);
    assert(astra_text_model_copy(&model, 0u, 13u, copied,
                                 sizeof(copied), &copied_bytes) == ASTRA_OK);
    assert(copied_bytes == 13u && memcmp(copied, "First\nSecond\n", 13u) == 0);

    memcpy(transcript, "Third\nFourth\n", 13u);
    info.text_bytes = 13u;
    assert(astraz_transcript_model_sync(&model, &info, 1u) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.generation == 1u && state.text_bytes == 13u);
    assert(astra_text_model_copy(&model, 0u, 13u, copied,
                                 sizeof(copied), &copied_bytes) == ASTRA_OK);
    assert(copied_bytes == 13u &&
           memcmp(copied, "Third\nFourth\n", 13u) == 0);
    memcpy(transcript, "Short\n", 6u);
    info.text_bytes = 6u;
    assert(astraz_transcript_model_sync(&model, &info, 0u) == ASTRA_OK);
    assert(astra_text_model_get_state(&model, &state) == ASTRA_OK);
    assert(state.generation == 1u && state.text_bytes == 6u);
    astra_text_model_dispose(&model);
    return 0;
}
