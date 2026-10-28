#include "transcript_flow.h"

#include <assert.h>

int main(void)
{
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
    AstraZEvent event = {.kind = ASTRAZ_EVENT_SCREEN_BEGIN};
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.row = 1;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.character = 'Z';
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.window = 1;
    event.data.cursor.row = 9;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.window = 1;
    event.data.text.character = 'U';
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.data.text.window = 0;
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.window = 0;
    event.data.cursor.row = 24;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.character = 'C';
    assert(astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_SCROLL;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CURSOR;
    event.data.cursor.row = 24;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.character = 'W';
    assert(astraz_transcript_flow_event(&flow, &event));
    event.data.text.character = '\n';
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_SCROLL;
    assert(!astraz_transcript_flow_event(&flow, &event));
    event.kind = ASTRAZ_EVENT_CHARACTER;
    event.data.text.character = 'Y';
    assert(!astraz_transcript_flow_event(&flow, &event));
    return 0;
}
