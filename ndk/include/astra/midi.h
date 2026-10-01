#ifndef ASTRA_MIDI_H
#define ASTRA_MIDI_H

/**
 * @file midi.h
 * @brief The Linux audio host's SoundFont (wavetable) synthesizer.
 *
 * An AstraMidiSynth is one synthesizer instrument on the host, mixed like
 * any PCM voice. It plays Standard MIDI Files -- the MC68040 sends the file
 * once and nothing per note -- and takes live MIDI messages on its 16
 * channels.
 *
 * Every synth starts with the default SoundFont set that
 * SOUND:soundfonts/default lists. A program may stack more fonts on top: a
 * shared one from SOUND:soundfonts by file name, or its own bytes. A preset
 * in a later font hides the same bank and program in an earlier one.
 * Shared fonts live on the host and are read there in place; a program's
 * own font is sent, and the host keeps one copy of each.
 *
 * The functions live in pcm.library (the Audio Kit): 2.3 for songs and
 * fonts, 2.4 for the rest. They use the same PCM capability as
 * astra_pcm_open(). Calls on one AstraMidiSynth must be serialized by the
 * caller.
 */

#include <astra/attributes.h>
#include <astra/pcm.h>
#include <astra/result.h>
#include <astra/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One synthesizer instrument on the host. Fields are private. */
typedef struct AstraMidiSynth {
    /** Private transport to the media service. */
    AstraPcmStream session;
} AstraMidiSynth;

#define ASTRA_MIDI_SYNTH_INIT {ASTRA_PCM_STREAM_INIT}

/** astra_midi_play() count that repeats until stopped. */
#define ASTRA_MIDI_FOREVER (-1)

/** Longest SoundFont file name, with its terminator. */
#define ASTRA_MIDI_FONT_NAME_MAX 128u
/** Longest preset name, with its terminator. */
#define ASTRA_MIDI_PRESET_NAME_MAX 24u

/** AstraMidiFont flag: the font is in the default set every synth has. */
#define ASTRA_MIDI_FONT_DEFAULT (1u << 0)

/** A shared SoundFont in SOUND:soundfonts. */
typedef struct AstraMidiFont {
    /** File name, as astra_midi_add_system_font() takes it. */
    char name[ASTRA_MIDI_FONT_NAME_MAX];
    /** Size of the file in bytes. */
    uint64_t bytes;
    /** ASTRA_MIDI_FONT_* flags. */
    uint32_t flags;
} AstraMidiFont;

/** An instrument a synth's fonts provide. */
typedef struct AstraMidiPreset {
    /** MIDI bank, 0..16383 (bank select MSB * 128 + LSB). */
    uint16_t bank;
    /** MIDI program, 0..127. */
    uint8_t program;
    /** The font that provides it: 0 is the lowest in the stack. */
    uint8_t font;
    /** The preset's name in its font. */
    char name[ASTRA_MIDI_PRESET_NAME_MAX];
} AstraMidiPreset;

/** Where a synth's song is. Tick values are zero with no song. */
typedef struct AstraMidiStatus {
    /** Nonzero while the song plays or any note still sounds. */
    int sounding;
    /** The song's position and length in its own ticks. */
    uint32_t position_ticks;
    uint32_t length_ticks;
    /** The song's resolution, and its current tempo. */
    uint32_t ticks_per_quarter;
    uint32_t tempo_us_per_quarter;
    /** Fonts given to the synth that it is still loading. */
    uint32_t fonts_loading;
} AstraMidiStatus;

/** One short MIDI message: a channel voice message's bytes as on the
 * wire. System messages are ignored. */
typedef struct AstraMidiEvent {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    uint8_t reserved;
} AstraMidiEvent;

/** Settings for astra_midi_set(). Levels are in thousandths. */
typedef enum AstraMidiSetting {
    ASTRA_MIDI_REVERB = 1,         /**< 0 off, 1 on (default on) */
    ASTRA_MIDI_REVERB_ROOM,        /**< room size, 0..1000 */
    ASTRA_MIDI_REVERB_DAMP,        /**< damping, 0..1000 */
    ASTRA_MIDI_REVERB_WIDTH,       /**< stereo width, 0..100000 */
    ASTRA_MIDI_REVERB_LEVEL,       /**< wet level, 0..1000 */
    ASTRA_MIDI_CHORUS,             /**< 0 off, 1 on (default on) */
    ASTRA_MIDI_CHORUS_VOICES,      /**< 0..99 */
    ASTRA_MIDI_CHORUS_LEVEL,       /**< 0..10000 */
    ASTRA_MIDI_CHORUS_SPEED,       /**< millihertz, 100..5000 */
    ASTRA_MIDI_CHORUS_DEPTH,       /**< microseconds, 0..256000 */
    ASTRA_MIDI_POLYPHONY,          /**< simultaneous voices, 1..65535 */
    ASTRA_MIDI_TEMPO,              /**< song tempo factor, 1..100000 */
    ASTRA_MIDI_POSITION            /**< seek the song to this tick */
} AstraMidiSetting;

/** Open a synthesizer with the default SoundFont set, silent.
 * @param service PCM service capability, as for astra_pcm_open().
 * @param synth Empty caller-owned synth initialized with
 * ASTRA_MIDI_SYNTH_INIT.
 */
ASTRA_NODISCARD AstraResult astra_midi_open(AstraHandle service,
                                            AstraMidiSynth *synth);

/** Release the synth; its sound stops. Local resources are released even
 * if the service has died. */
ASTRA_NODISCARD AstraResult astra_midi_close(AstraMidiSynth *synth);

/** List the shared SoundFonts, in name order.
 * @param first Index of the first font to copy.
 * @param fonts Receives up to @p capacity fonts.
 * @param count Receives how many were copied.
 * @param total Receives how many there are; NULL when not wanted.
 */
ASTRA_NODISCARD AstraResult astra_midi_fonts(AstraMidiSynth *synth,
                                             uint32_t first,
                                             AstraMidiFont *fonts,
                                             uint32_t capacity,
                                             uint32_t *count,
                                             uint32_t *total);

/** Stack a shared SoundFont on the synth by its file name in
 * SOUND:soundfonts (for example "TimGM6mb.sf2").
 * @return ASTRA_OK, or an error for a name that is not a plain .sf2 file
 * there.
 */
ASTRA_NODISCARD AstraResult astra_midi_add_system_font(AstraMidiSynth *synth,
                                                       const char *name);

/** Stack a SoundFont given as bytes (an .sf2 file the program has read,
 * such as one in its bundle). The host checks it and keeps one copy of
 * each distinct font however often it is sent.
 */
ASTRA_NODISCARD AstraResult astra_midi_add_font(AstraMidiSynth *synth,
                                                const void *font,
                                                uint32_t bytes);

/** List the instruments the synth's fonts provide: for each bank and
 * program, the preset that is heard, in bank then program order. Waits
 * until every font given to the synth has loaded.
 * @param first Index of the first preset to copy.
 * @param presets Receives up to @p capacity presets.
 * @param count Receives how many were copied.
 * @param total Receives how many there are; NULL when not wanted.
 */
ASTRA_NODISCARD AstraResult astra_midi_presets(AstraMidiSynth *synth,
                                               uint32_t first,
                                               AstraMidiPreset *presets,
                                               uint32_t capacity,
                                               uint32_t *count,
                                               uint32_t *total);

/** Replace the song with a Standard MIDI File; stops any song playing. */
ASTRA_NODISCARD AstraResult astra_midi_load(AstraMidiSynth *synth,
                                            const void *file,
                                            uint32_t bytes);

/** Play the song from its start @p plays times, or ::ASTRA_MIDI_FOREVER. */
ASTRA_NODISCARD AstraResult astra_midi_play(AstraMidiSynth *synth,
                                            int32_t plays);

/** Hold or resume the song where it is. */
ASTRA_NODISCARD AstraResult astra_midi_pause(AstraMidiSynth *synth,
                                             int paused);

/** End the song; every note stops at once. */
ASTRA_NODISCARD AstraResult astra_midi_stop(AstraMidiSynth *synth);

/** Play live MIDI messages at once, in order after every earlier call. */
ASTRA_NODISCARD AstraResult astra_midi_send(AstraMidiSynth *synth,
                                            const AstraMidiEvent *events,
                                            uint32_t count);

/** Change one setting; ASTRA_ERROR_INVALID_ARGUMENT for a value out of
 * its range. */
ASTRA_NODISCARD AstraResult astra_midi_set(AstraMidiSynth *synth,
                                           AstraMidiSetting setting,
                                           uint32_t value);

/** Set linear gain: 65536 is unity and zero is silent. */
ASTRA_NODISCARD AstraResult astra_midi_gain(AstraMidiSynth *synth,
                                            uint32_t gain_q16);

/** Ask where the song is and whether the synth still sounds. */
ASTRA_NODISCARD AstraResult astra_midi_status(AstraMidiSynth *synth,
                                              AstraMidiStatus *status);

/** Ask whether the synth is still sounding.
 * @param active Receives nonzero while a song plays or a note rings.
 */
ASTRA_NODISCARD AstraResult astra_midi_active(AstraMidiSynth *synth,
                                              int *active);

/** Start a note. */
static inline AstraResult astra_midi_note_on(AstraMidiSynth *synth,
                                             uint8_t channel, uint8_t key,
                                             uint8_t velocity)
{
    AstraMidiEvent event = {(uint8_t)(0x90u | (channel & 0x0fu)),
                            (uint8_t)(key & 0x7fu),
                            (uint8_t)(velocity & 0x7fu), 0u};

    return astra_midi_send(synth, &event, 1u);
}

/** Release a note. */
static inline AstraResult astra_midi_note_off(AstraMidiSynth *synth,
                                              uint8_t channel, uint8_t key)
{
    AstraMidiEvent event = {(uint8_t)(0x80u | (channel & 0x0fu)),
                            (uint8_t)(key & 0x7fu), 0x40u, 0u};

    return astra_midi_send(synth, &event, 1u);
}

/** Choose a channel's instrument: bank select, then program change. */
static inline AstraResult astra_midi_program(AstraMidiSynth *synth,
                                             uint8_t channel, uint16_t bank,
                                             uint8_t program)
{
    AstraMidiEvent events[3] = {
        {(uint8_t)(0xb0u | (channel & 0x0fu)), 0u,
         (uint8_t)((bank >> 7) & 0x7fu), 0u},
        {(uint8_t)(0xb0u | (channel & 0x0fu)), 32u,
         (uint8_t)(bank & 0x7fu), 0u},
        {(uint8_t)(0xc0u | (channel & 0x0fu)), (uint8_t)(program & 0x7fu),
         0u, 0u},
    };

    return astra_midi_send(synth, events, 3u);
}

/** Set a controller (CC 0..127) on a channel. */
static inline AstraResult astra_midi_control(AstraMidiSynth *synth,
                                             uint8_t channel,
                                             uint8_t controller,
                                             uint8_t value)
{
    AstraMidiEvent event = {(uint8_t)(0xb0u | (channel & 0x0fu)),
                            (uint8_t)(controller & 0x7fu),
                            (uint8_t)(value & 0x7fu), 0u};

    return astra_midi_send(synth, &event, 1u);
}

/** Bend a channel's pitch: 0..16383, 8192 is centred. */
static inline AstraResult astra_midi_pitch_bend(AstraMidiSynth *synth,
                                                uint8_t channel,
                                                uint16_t value)
{
    AstraMidiEvent event = {(uint8_t)(0xe0u | (channel & 0x0fu)),
                            (uint8_t)(value & 0x7fu),
                            (uint8_t)((value >> 7) & 0x7fu), 0u};

    return astra_midi_send(synth, &event, 1u);
}

/** Release every note on a channel (controller 123). */
static inline AstraResult astra_midi_all_notes_off(AstraMidiSynth *synth,
                                                   uint8_t channel)
{
    return astra_midi_control(synth, channel, 123u, 0u);
}

#ifdef __cplusplus
}
#endif

#endif
