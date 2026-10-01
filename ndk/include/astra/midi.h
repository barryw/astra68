#ifndef ASTRA_MIDI_H
#define ASTRA_MIDI_H

/**
 * @file midi.h
 * @brief MIDI songs played by the Linux audio host's SoundFont synthesizer.
 *
 * A song is a Standard MIDI File the host synthesizes from SoundFonts and
 * mixes like any PCM voice: the MC68040 sends the file once and nothing
 * per note. Every song plays through Astra's default SoundFont set; a
 * program may stack more fonts on top -- one of the system's own by name,
 * or its own bytes -- and a preset in a later font hides the same bank and
 * program in an earlier one. The host keeps SoundFonts by content, so a
 * font it has seen before is not sent again.
 *
 * The functions live in pcm.library 2.3 (the Audio Kit) and use the same
 * PCM capability as astra_pcm_open().
 */

#include <astra/attributes.h>
#include <astra/pcm.h>
#include <astra/result.h>
#include <astra/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One synthesizer voice and its song. Fields are private. */
typedef struct AstraMidiSong {
    /** Private transport to the media service. */
    AstraPcmStream session;
} AstraMidiSong;

#define ASTRA_MIDI_SONG_INIT {ASTRA_PCM_STREAM_INIT}

/** astra_midi_play() count that repeats until stopped. */
#define ASTRA_MIDI_FOREVER (-1)

/** Open a synthesizer voice with the default SoundFont set, silent.
 * @param service PCM service capability, as for astra_pcm_open().
 * @param song Empty caller-owned song initialized with ASTRA_MIDI_SONG_INIT.
 * @return ASTRA_OK, or an error; the first open after boot may take a while
 * when the host has never held the default set.
 */
ASTRA_NODISCARD AstraResult astra_midi_open(AstraHandle service,
                                            AstraMidiSong *song);

/** Stack one of the system's SoundFonts on the voice, by its name in the
 * default set's index (for example "TimGM6mb").
 * @return ASTRA_OK, ASTRA_ERROR_INVALID_HANDLE for an unknown name, or an
 * error.
 */
ASTRA_NODISCARD AstraResult astra_midi_add_system_font(AstraMidiSong *song,
                                                       const char *name);

/** Stack a SoundFont given as bytes (an .sf2 file the program has read).
 * The host checks and keeps it; a later voice may send the same bytes
 * again, and the host recognizes them without keeping a second copy.
 */
ASTRA_NODISCARD AstraResult astra_midi_add_font(AstraMidiSong *song,
                                                const void *font,
                                                uint32_t bytes);

/** Replace the song with a Standard MIDI File; stops any song playing. */
ASTRA_NODISCARD AstraResult astra_midi_load(AstraMidiSong *song,
                                            const void *file,
                                            uint32_t bytes);

/** Play the song from its start @p plays times, or ::ASTRA_MIDI_FOREVER. */
ASTRA_NODISCARD AstraResult astra_midi_play(AstraMidiSong *song,
                                            int32_t plays);

/** Hold or resume the song where it is. */
ASTRA_NODISCARD AstraResult astra_midi_pause(AstraMidiSong *song,
                                             int paused);

/** End the song; every note stops at once. */
ASTRA_NODISCARD AstraResult astra_midi_stop(AstraMidiSong *song);

/** Set linear gain: 65536 is unity and zero is silent. */
ASTRA_NODISCARD AstraResult astra_midi_gain(AstraMidiSong *song,
                                            uint32_t gain_q16);

/** Ask whether the song is still sounding.
 * @param active Receives nonzero while it plays or its last notes ring.
 */
ASTRA_NODISCARD AstraResult astra_midi_active(AstraMidiSong *song,
                                              int *active);

/** Release the voice; the song stops. Local resources are released even if
 * the service has died. */
ASTRA_NODISCARD AstraResult astra_midi_close(AstraMidiSong *song);

#ifdef __cplusplus
}
#endif

#endif
