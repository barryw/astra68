#ifndef ASTRAZ_AUDIO_H
#define ASTRAZ_AUDIO_H

#include <astraz/engine.h>

typedef struct AstraZAudio AstraZAudio;

AstraZAudio *astraz_audio_open(void);
int astraz_audio_event(AstraZAudio *audio, const AstraZEvent *event);
int astraz_audio_finished(AstraZAudio *audio);
void astraz_audio_close(AstraZAudio *audio);

#endif
