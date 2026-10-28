#include "transcript_flow.h"
#include <astra/utf8.h>

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

int astraz_transcript_flow_flush_due(AstraZTranscriptFlow *flow,
                                    uint64_t now_ns)
{
    if (!flow) return 0;
    if (flow->last_flush_ns && now_ns >= flow->last_flush_ns &&
        now_ns - flow->last_flush_ns < UINT64_C(33000000)) return 0;
    flow->last_flush_ns = now_ns;
    return 1;
}

int astraz_transcript_flow_event(AstraZTranscriptFlow *flow,
                                const AstraZEvent *event)
{
    if (!flow || !event) return 0;
    switch (event->kind) {
    case ASTRAZ_EVENT_SCREEN_BEGIN:
        *flow = (AstraZTranscriptFlow){0};
        break;
    case ASTRAZ_EVENT_CURSOR:
        if (event->data.cursor.window != 0) break;
        if (flow->saw_text && flow->row != 0 &&
            flow->row != event->data.cursor.row && !flow->last_newline)
            flow->pending_break = 1u;
        flow->row = event->data.cursor.row;
        break;
    case ASTRAZ_EVENT_SCROLL:
        if (flow->saw_text && !flow->last_newline)
            flow->pending_break = 1u;
        break;
    case ASTRAZ_EVENT_CHARACTER: {
        if (event->data.text.window != 0) break;
        uint32_t character = event->data.text.character;
        if (character == '\n' || character == '\r') {
            flow->last_newline = 1u;
            flow->pending_break = 0u;
            flow->saw_text = 1u;
            break;
        }
        int insert = flow->pending_break && flow->saw_text &&
            !flow->last_newline;
        flow->pending_break = 0u;
        flow->last_newline = 0u;
        flow->saw_text = 1u;
        return insert;
    }
    default:
        break;
    }
    return 0;
}
