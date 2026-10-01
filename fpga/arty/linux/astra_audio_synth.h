#ifndef ASTRA_AUDIO_SYNTH_H
#define ASTRA_AUDIO_SYNTH_H

/*
 * A SoundFont synthesizer playing one MIDI song for the Linux audio host
 * (docs/AUDIO_ARCHITECTURE.md). FluidSynth does the synthesis on a thread of
 * its own that renders ahead into a ring, so loading a SoundFont, pulling a
 * sample from disk or rendering a dense passage never stalls the HDMI
 * feeder, which only takes finished frames. Every control call queues a
 * command for that thread and returns at once; commands apply in order.
 * The QEMU gates' stand-in daemon loads this file as a shared object too.
 */

#include <stdint.h>

typedef struct AstraAudioSynth AstraAudioSynth;

/* A synthesizer rendering at @p rate Hz, idle and silent. NULL when memory
 * or the thread cannot be had. */
AstraAudioSynth *astra_audio_synth_open(uint32_t rate);
/* Stops the thread and frees everything. */
void astra_audio_synth_close(AstraAudioSynth *synth);

/* Adds the SoundFont at @p path on top of those already added: a preset in
 * a later font hides the same bank and program in an earlier one. */
uint32_t astra_audio_synth_add_font(AstraAudioSynth *synth,
                                    const char *path);
/* Replaces the song with a Standard MIDI File, copied. Stops playback. */
uint32_t astra_audio_synth_load(AstraAudioSynth *synth, const uint8_t *song,
                                uint32_t bytes);
/* Plays the song from the start @p plays times; -1 repeats forever. */
uint32_t astra_audio_synth_play(AstraAudioSynth *synth, int32_t plays);
/* Holds or resumes the song where it is; held notes are released. */
uint32_t astra_audio_synth_pause(AstraAudioSynth *synth, int paused);
/* Ends the song and silences every voice at once. */
uint32_t astra_audio_synth_stop(AstraAudioSynth *synth);

/* Nonzero while the song plays or its last notes still sound. */
int astra_audio_synth_active(const AstraAudioSynth *synth);
/* The first command that failed (an ASTRA_STATUS_*), or ASTRA_STATUS_OK. */
uint32_t astra_audio_synth_status(const AstraAudioSynth *synth);
/* Frames the ring holds now. */
uint32_t astra_audio_synth_ready(const AstraAudioSynth *synth);
/* Takes up to @p count rendered frames, left and right in -1..1, and
 * returns how many. Never waits; called by the feeder. */
uint32_t astra_audio_synth_read(AstraAudioSynth *synth, float (*frames)[2],
                                uint32_t count);

#endif
