/* SDL_mixer's native MIDI interface on Astra. This file is deliberately
 * outside upstream SDL_mixer.
 *
 * A song goes whole to the Linux audio host's SoundFont synthesizer
 * (<astra/midi.h>) and plays there: the MC68040 sends the file once and
 * nothing per note. Songs play through Astra's default SoundFont set, with
 * the application's own fonts stacked on top -- every .sf2 in its bundle's
 * resources/soundfonts/, in name order -- and then any it names through
 * Mix_SetSoundFonts() or SDL_SOUNDFONTS. */

#include "SDL.h"
#include "SDL_mixer.h"

#include "native_midi/native_midi.h"

#include <astra/midi.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

/* The bundle's own SoundFonts; a bundle with more is told so in the log. */
#define BUNDLE_FONTS_MAX 16

struct _NativeMidiSong {
    void *bytes;
    Uint32 length;
};

/* How long the host's answer to "still playing?" stands. SDL_mixer asks
 * every audio callback; each fresh answer is a round trip to the host. */
#define ASTRA_MIDI_ACTIVE_NS UINT64_C(250000000)

static AstraMidiSynth voice = ASTRA_MIDI_SYNTH_INIT;
static int voice_open;
static NativeMidiSong *current;
static uint32_t gain_q16 = UINT32_C(65536);
static int active;
static uint64_t active_until;
static const char *error_text = "";

/* SDL_mixer's interface has nowhere to report these; the next start or
 * status query says whether the service is still there. */
static void ignore(AstraResult result)
{
    (void)result;
}

static AstraHandle service(void)
{
    const AstraStartupCapability *capability = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_PCM);

    return capability != NULL ? capability->handle : 0u;
}

int native_midi_detect(void)
{
    return service() != 0u;
}

/* Mix_EachSoundFont callback: one font file, read and stacked. */
static int add_font(const char *path, void *data)
{
    SDL_RWops *file = SDL_RWFromFile(path, "rb");
    Sint64 size = file != NULL ? SDL_RWsize(file) : -1;
    void *bytes = size > 0 && size <= (Sint64)UINT32_MAX ?
                  SDL_malloc((size_t)size) : NULL;

    (void)data;
    if (bytes != NULL &&
        SDL_RWread(file, bytes, (size_t)size, 1) == 1 &&
        astra_midi_add_font(&voice, bytes, (uint32_t)size) == ASTRA_OK) {
        SDL_free(bytes);
        SDL_RWclose(file);
        return 1;
    }
    SDL_free(bytes);
    if (file != NULL)
        SDL_RWclose(file);
    /* A font that cannot be used leaves the defaults; keep going. */
    SDL_Log("Astra MIDI: SoundFont %s not used", path);
    return 1;
}

static int compare_names(const void *left, const void *right)
{
    return strcmp(*(char *const *)left, *(char *const *)right);
}

/* Stacks every .sf2 in the bundle's resources/soundfonts/, in name order. */
static void add_bundle_fonts(void)
{
    char *base = SDL_GetBasePath();
    char *names[BUNDLE_FONTS_MAX];
    size_t count = 0u;
    char *directory_path = NULL;
    DIR *directory = NULL;
    struct dirent *entry;

    if (base != NULL &&
        SDL_asprintf(&directory_path, "%ssoundfonts", base) >= 0)
        directory = opendir(directory_path);
    while (directory != NULL && (entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);

        if (length <= 4u || entry->d_name[0] == '.' ||
            SDL_strcasecmp(entry->d_name + length - 4u, ".sf2") != 0)
            continue;
        if (count == BUNDLE_FONTS_MAX) {
            SDL_Log("Astra MIDI: more than %d bundle SoundFonts; the rest "
                    "are not used", BUNDLE_FONTS_MAX);
            break;
        }
        if ((names[count] = SDL_strdup(entry->d_name)) != NULL)
            ++count;
    }
    if (directory != NULL)
        (void)closedir(directory);
    qsort(names, count, sizeof(names[0]), compare_names);
    for (size_t at = 0u; at < count; ++at) {
        char *path = NULL;

        if (SDL_asprintf(&path, "%s/%s", directory_path, names[at]) >= 0)
            (void)add_font(path, NULL);
        SDL_free(path);
        SDL_free(names[at]);
    }
    SDL_free(directory_path);
    SDL_free(base);
}

static int open_voice(void)
{
    if (voice_open)
        return 1;
    if (astra_midi_open(service(), &voice) != ASTRA_OK) {
        error_text = "Astra MIDI synthesizer unavailable";
        return 0;
    }
    voice_open = 1;
    add_bundle_fonts();
    if (Mix_GetSoundFonts() != NULL)
        (void)Mix_EachSoundFont(add_font, NULL);
    if (astra_midi_gain(&voice, gain_q16) != ASTRA_OK) {
        error_text = "Astra MIDI gain failed";
        return 0;
    }
    return 1;
}

NativeMidiSong *native_midi_loadsong_RW(SDL_RWops *src, int freesrc)
{
    NativeMidiSong *song = SDL_calloc(1, sizeof(*song));
    Sint64 size = SDL_RWsize(src);

    if (song == NULL) {
        error_text = "out of memory";
        return NULL;
    }
    if (size <= 0 || size > (Sint64)UINT32_MAX ||
        (song->bytes = SDL_malloc((size_t)size)) == NULL ||
        SDL_RWread(src, song->bytes, (size_t)size, 1) != 1) {
        error_text = "MIDI file could not be read";
        SDL_free(song->bytes);
        SDL_free(song);
        return NULL;
    }
    song->length = (Uint32)size;
    /* SDL_mixer owns src until a decoder accepts it: a refusal leaves it
     * open, and Mix_LoadMUSType_RW rewinds it for the next decoder. */
    if (freesrc)
        SDL_RWclose(src);
    return song;
}

void native_midi_freesong(NativeMidiSong *song)
{
    if (song == NULL)
        return;
    if (song == current)
        native_midi_stop();
    SDL_free(song->bytes);
    SDL_free(song);
}

void native_midi_start(NativeMidiSong *song, int loops)
{
    if (song == NULL || !open_voice())
        return;
    if (astra_midi_load(&voice, song->bytes, song->length) != ASTRA_OK ||
        astra_midi_play(&voice, loops < 0 ? ASTRA_MIDI_FOREVER :
                                loops == 0 ? 1 : loops) != ASTRA_OK) {
        error_text = "Astra MIDI song refused";
        current = NULL;
        return;
    }
    current = song;
    active = 1;
    active_until = astra_clock_monotonic() + ASTRA_MIDI_ACTIVE_NS;
}

void native_midi_pause(void)
{
    if (current != NULL)
        ignore(astra_midi_pause(&voice, 1));
}

void native_midi_resume(void)
{
    if (current != NULL)
        ignore(astra_midi_pause(&voice, 0));
}

void native_midi_stop(void)
{
    if (current != NULL)
        ignore(astra_midi_stop(&voice));
    current = NULL;
    active = 0;
}

int native_midi_active(void)
{
    uint64_t now;

    if (current == NULL)
        return 0;
    now = astra_clock_monotonic();
    if (now >= active_until) {
        int sounding = 0;

        active = astra_midi_active(&voice, &sounding) == ASTRA_OK &&
                 sounding != 0;
        active_until = now + ASTRA_MIDI_ACTIVE_NS;
    }
    return active;
}

void native_midi_setvolume(int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > MIX_MAX_VOLUME)
        volume = MIX_MAX_VOLUME;
    gain_q16 = (uint32_t)volume * UINT32_C(65536) / MIX_MAX_VOLUME;
    if (voice_open)
        ignore(astra_midi_gain(&voice, gain_q16));
}

const char *native_midi_error(void)
{
    return error_text;
}
