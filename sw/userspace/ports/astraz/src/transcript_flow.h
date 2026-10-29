#ifndef ASTRAZ_TRANSCRIPT_FLOW_H
#define ASTRAZ_TRANSCRIPT_FLOW_H

#include <astraz/engine.h>
#include <astra/text_model.h>

typedef struct AstraZTranscriptFlow {
    uint8_t at_line_start;
    uint8_t marker;
    uint8_t style;
    uint64_t last_flush_ns;
} AstraZTranscriptFlow;

void astraz_transcript_flow_reset(AstraZTranscriptFlow *flow);
void astraz_transcript_flow_new_line(AstraZTranscriptFlow *flow);
/* Decode Frotz stream text before physical 80-column screen wrapping. */
AstraResult astraz_transcript_flow_text(AstraZTranscriptFlow *flow,
    const uint16_t *characters, uint32_t length, char *output,
    uint8_t *styles, uint32_t capacity, uint32_t *output_bytes);
int astraz_transcript_flow_flush_due(AstraZTranscriptFlow *flow,
                                    uint64_t now_ns);
int astraz_input_copy(const char *text, uint32_t length,
                     uint16_t *buffer, uint32_t capacity);
uint32_t astraz_transcript_without_prompt(const char *text, uint32_t length);
/* Keep visible transcript text in TextModel; rebuild after prefix changes. */
AstraResult astraz_transcript_model_sync(AstraTextModel *model,
                                        const AstraTextModelInfo *info,
                                        uint32_t rebuild);

#endif
