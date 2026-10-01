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

/* The SOUND volume's SoundFont directory, an open descriptor the module
 * keeps: fonts named "sound:NAME" are read from beneath it only, without
 * following links, since the guest writes there. */
void astra_audio_synth_set_sound_fonts(int directory);
/* Nonzero for a shared font's name: a plain .sf2 file name. */
int astra_audio_synth_sound_font_name(const char *name);
/* Opens a shared font read-only beneath the directory; -1 and errno if
 * it is not there, not a regular file, or not a valid name. */
int astra_audio_synth_open_sound_font(const char *name);

/* Adds a SoundFont on top of those already added: a preset in a later font
 * hides the same bank and program in an earlier one. @p path is
 * "sound:NAME" for a shared font, else a path the daemon owns. */
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

/* Plays @p count short MIDI messages (status, data1, data2, unused) in
 * order, after every earlier call: notes, controllers, program changes,
 * pressure and pitch bend on any of the 16 channels. */
uint32_t astra_audio_synth_events(AstraAudioSynth *synth,
                                  const uint8_t *events, uint32_t count);
/* Changes one ASTRA_HOST_MIDI_SET_* setting, in order like a command;
 * ASTRA_STATUS_INVALID for an unknown setting or a value out of range. */
uint32_t astra_audio_synth_set(AstraAudioSynth *synth, uint32_t setting,
                               uint32_t value);

typedef struct AstraAudioSynthStatus {
    uint32_t sounding;
    uint32_t position_ticks;
    uint32_t length_ticks;
    uint32_t ticks_per_quarter;
    uint32_t tempo_us_per_quarter;
    uint32_t fonts_loading;
} AstraAudioSynthStatus;

typedef struct AstraAudioSynthPreset {
    uint16_t bank;
    uint8_t program;
    /* Its font's place in the stack: 0 is the first added. */
    uint8_t font;
    char name[24];
} AstraAudioSynthPreset;

/* Where the song is and how much of the stack is still loading. */
void astra_audio_synth_report(AstraAudioSynth *synth,
                              AstraAudioSynthStatus *report);
/* Copies up to @p capacity presets from index @p first of the stack's
 * presets (the one heard for each bank and program, in that order), and
 * the total; ASTRA_STATUS_BUSY while a font is still loading. */
uint32_t astra_audio_synth_presets(AstraAudioSynth *synth, uint32_t first,
                                   AstraAudioSynthPreset *presets,
                                   uint32_t capacity, uint32_t *copied,
                                   uint32_t *total);

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
