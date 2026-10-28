#ifndef ASTRAZ_TRANSCRIPT_FLOW_H
#define ASTRAZ_TRANSCRIPT_FLOW_H

#include <astraz/engine.h>

typedef struct AstraZTranscriptFlow {
    int32_t row;
    uint8_t saw_text;
    uint8_t last_newline;
    uint8_t pending_break;
    uint64_t last_flush_ns;
} AstraZTranscriptFlow;

/* Return true when a line break belongs immediately before this character. */
int astraz_transcript_flow_event(AstraZTranscriptFlow *flow,
                                const AstraZEvent *event);
int astraz_transcript_flow_flush_due(AstraZTranscriptFlow *flow,
                                    uint64_t now_ns);
int astraz_input_copy(const char *text, uint32_t length,
                     uint16_t *buffer, uint32_t capacity);

#endif
