#include "transcript_flow.h"
#include <astra/utf8.h>

uint32_t astraz_transcript_without_prompt(const char *text, uint32_t length)
{
    if (text == NULL) return length;
    uint32_t end = length;
    while (end > 0u && (text[end - 1u] == ' ' ||
        text[end - 1u] == '\t' || text[end - 1u] == '\r' ||
        text[end - 1u] == '\n')) --end;
    if (end == 0u || text[end - 1u] != '>') return length;
    uint32_t start = end - 1u;
    while (start > 0u && (text[start - 1u] == ' ' ||
        text[start - 1u] == '\t')) --start;
    if (start > 0u && text[start - 1u] != '\n' &&
        text[start - 1u] != '\r') return length;
    return start;
}

int astraz_input_copy(const char *text, uint32_t length,
                     uint16_t *buffer, uint32_t capacity)
{
    if (!text || !buffer || !capacity) return 0;
    uint32_t count = 0u;
    for (uint32_t offset = 0u; offset < length;) {
        uint32_t consumed = 0u;
        (void)astra_utf8_decode(text + offset, length - offset, &consumed);
        if (!consumed || ++count >= capacity) return 0;
        offset += consumed;
    }
    count = 0u;
    for (uint32_t offset = 0u; offset < length;) {
        uint32_t consumed = 0u;
        uint32_t scalar = astra_utf8_decode(text + offset,
            length - offset, &consumed);
        buffer[count++] = (uint16_t)(scalar > 0xffffu ? '?' : scalar);
        offset += consumed;
    }
    buffer[count] = 0u;
    return 1;
}

AstraResult astraz_transcript_model_sync(AstraTextModel *model,
                                        const AstraTextModelInfo *info,
                                        uint32_t rebuild)
{
    AstraTextModelState state = ASTRA_TEXT_MODEL_STATE_INIT;
    if (info == NULL || astra_text_model_get_state(model, &state) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!rebuild && info->text_bytes >= state.text_bytes) {
        if (info->text_bytes == state.text_bytes) return ASTRA_OK;
        return astra_text_model_replace(model, state.text_bytes,
            state.text_bytes, info->text + state.text_bytes,
            info->text_bytes - state.text_bytes);
    }
    astra_text_model_dispose(model);
    *model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    return astra_text_model_init(model, info);
}

int astraz_transcript_flow_flush_due(AstraZTranscriptFlow *flow,
                                    uint64_t now_ns)
{
    if (!flow) return 0;
    if (flow->last_flush_ns && now_ns >= flow->last_flush_ns &&
        now_ns - flow->last_flush_ns < UINT64_C(33000000)) return 0;
    flow->last_flush_ns = now_ns;
    return 1;
}

void astraz_transcript_flow_reset(AstraZTranscriptFlow *flow)
{
    if (flow) *flow = (AstraZTranscriptFlow){.at_line_start = 1u};
}

void astraz_transcript_flow_new_line(AstraZTranscriptFlow *flow)
{
    if (!flow) return;
    flow->at_line_start = 1u;
    flow->marker = 0u;
}

AstraResult astraz_transcript_flow_text(AstraZTranscriptFlow *flow,
    const uint16_t *characters, uint32_t length, char *output,
    uint8_t *styles, uint32_t capacity, uint32_t *output_bytes)
{
    if (!flow || (!characters && length) || !output || !styles ||
        !output_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraZTranscriptFlow next = *flow;
    uint32_t used = 0u;
    for (uint32_t at = 0u; at < length; ++at) {
        uint32_t scalar = characters[at];
        if (next.marker) {
            if (next.marker == 1u) next.style = (uint8_t)scalar;
            next.marker = 0u;
            continue;
        }
        if (scalar == 1u || scalar == 2u) {
            next.marker = (uint8_t)scalar;
            continue;
        }
        if (scalar == 9u || scalar == 11u) {
            uint32_t spaces = scalar == 9u && next.at_line_start ? 3u :
                              scalar == 11u ? 2u : 1u;
            if (spaces > capacity - used) return ASTRA_ERROR_BUFFER_TOO_SMALL;
            for (uint32_t count = 0u; count < spaces; ++count) {
                styles[used] = next.style;
                output[used++] = ' ';
            }
            next.at_line_start = 0u;
            continue;
        }
        if (scalar == '\n' || scalar == '\r') {
            if (used == capacity) return ASTRA_ERROR_BUFFER_TOO_SMALL;
            styles[used] = next.style;
            output[used++] = '\n';
            next.at_line_start = 1u;
            continue;
        }
        if (scalar < 32u) continue;
        char bytes[4];
        uint32_t count = astra_utf8_encode(scalar, bytes);
        if (count == 0u) {
            bytes[0] = '?';
            count = 1u;
        }
        if (count > capacity - used) return ASTRA_ERROR_BUFFER_TOO_SMALL;
        for (uint32_t byte = 0u; byte < count; ++byte) {
            styles[used] = next.style;
            output[used++] = bytes[byte];
        }
        next.at_line_start = 0u;
    }
    *flow = next;
    *output_bytes = used;
    return ASTRA_OK;
}
