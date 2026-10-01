// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_audio_synth.h"

#include <astra/host.h>
#include <astra/status.h>

#include <fluidsynth.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum {
    /* Rendered frames waiting for the feeder: about 170 ms at 48 kHz. The
     * thread keeps AHEAD_FRAMES of them, in blocks of BLOCK_FRAMES. */
    RING_FRAMES = 8192u,
    AHEAD_FRAMES = 4096u,
    BLOCK_FRAMES = 256u,
    /* How long a thread with a full ring sleeps before looking again: the
     * feeder takes frames without telling it. */
    POLL_NS = 5000000u
};

/* The shared SoundFont directory (the SOUND volume's soundfonts/), which
 * the guest writes through HostFS. A font named "sound:NAME" is opened
 * beneath it and nowhere else; every other name is a path the daemon chose
 * itself. -1 until astra_audio_synth_set_sound_fonts(). */
static atomic_int sound_fonts = -1;
#define SOUND_PREFIX "sound:"

typedef enum {
    COMMAND_FONT,
    COMMAND_LOAD,
    COMMAND_PLAY,
    COMMAND_PAUSE,
    COMMAND_STOP,
    COMMAND_EVENTS,
    COMMAND_SET
} CommandKind;

typedef struct Command {
    struct Command *next;
    CommandKind kind;
    int32_t value;
    uint32_t argument;
    uint8_t *bytes;
    uint32_t length;
    char path[];
} Command;

struct AstraAudioSynth {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    Command *head;
    Command **tail;
    int closing;
    /* Owned by the thread. */
    fluid_settings_t *settings;
    fluid_synth_t *fluid;
    fluid_player_t *player;
    uint8_t *song;
    uint32_t song_bytes;
    int sounding;
    /* The song's tempo factor, kept for every later play. */
    double tempo;
    /* Published by the thread under lock: where the song is, and the
     * presets the fonts provide, rebuilt after each font loads. */
    AstraAudioSynthStatus report;
    AstraAudioSynthPreset *presets;
    uint32_t preset_count;
    /* Fonts submitted and not yet loaded. */
    _Atomic uint32_t fonts_loading;
    float left[BLOCK_FRAMES];
    float right[BLOCK_FRAMES];
    /* Shared with the feeder. Indices count frames and wrap at 2^32;
     * active says the thread is (or is about to be) rendering. */
    _Atomic uint32_t written;
    _Atomic uint32_t taken;
    _Atomic int active;
    _Atomic uint32_t status;
    float ring[RING_FRAMES][2];
};

static void fail(AstraAudioSynth *synth, uint32_t status)
{
    uint32_t ok = ASTRA_STATUS_OK;

    (void)atomic_compare_exchange_strong(&synth->status, &ok, status);
}

static void drop_player(AstraAudioSynth *synth)
{
    /* No fluid_player_join: with the sample timing source the player only
     * moves while the synth renders, so a join here would never return. */
    if (synth->player != NULL) {
        (void)fluid_player_stop(synth->player);
        delete_fluid_player(synth->player);
        synth->player = NULL;
    }
}

/* Each play starts a fresh player on the stored song: a finished player
 * does not start over. */
static void play(AstraAudioSynth *synth, int32_t plays)
{
    drop_player(synth);
    (void)fluid_synth_all_sounds_off(synth->fluid, -1);
    (void)fluid_synth_system_reset(synth->fluid);
    if (synth->song == NULL) {
        fail(synth, ASTRA_STATUS_INVALID);
        return;
    }
    synth->player = new_fluid_player(synth->fluid);
    if (synth->player == NULL ||
        fluid_player_add_mem(synth->player, synth->song,
                             synth->song_bytes) != FLUID_OK ||
        fluid_player_set_loop(synth->player, plays) != FLUID_OK ||
        fluid_player_set_tempo(synth->player, FLUID_PLAYER_TEMPO_INTERNAL,
                               synth->tempo) != FLUID_OK ||
        fluid_player_play(synth->player) != FLUID_OK) {
        drop_player(synth);
        fail(synth, ASTRA_STATUS_INVALID);
        return;
    }
    synth->sounding = 1;
}

static int compare_presets(const void *left, const void *right)
{
    const AstraAudioSynthPreset *a = left, *b = right;

    if (a->bank != b->bank)
        return a->bank < b->bank ? -1 : 1;
    if (a->program != b->program)
        return a->program < b->program ? -1 : 1;
    /* The higher font first: it is the one heard. */
    return a->font > b->font ? -1 : a->font < b->font;
}

/* Every preset the stack provides, by bank and program, the one in the
 * highest font for each pair. */
static void rebuild_presets(AstraAudioSynth *synth)
{
    int fonts = fluid_synth_sfcount(synth->fluid);
    AstraAudioSynthPreset *all = NULL;
    uint32_t count = 0u, capacity = 0u, kept = 0u;

    for (int index = 0; index < fonts; ++index) {
        /* fluid_synth_get_sfont(0) is the font added last. */
        fluid_sfont_t *font = fluid_synth_get_sfont(synth->fluid,
                                                    (unsigned)index);
        fluid_preset_t *preset;

        if (font == NULL)
            continue;
        fluid_sfont_iteration_start(font);
        while ((preset = fluid_sfont_iteration_next(font)) != NULL) {
            AstraAudioSynthPreset *entry;
            const char *name = fluid_preset_get_name(preset);

            if (count == capacity) {
                AstraAudioSynthPreset *grown = realloc(
                    all, (capacity = capacity ? capacity * 2u : 256u) *
                             sizeof(*all));

                if (grown == NULL) {
                    free(all);
                    return;
                }
                all = grown;
            }
            entry = &all[count++];
            memset(entry, 0, sizeof(*entry));
            entry->bank = (uint16_t)fluid_preset_get_banknum(preset);
            entry->program = (uint8_t)fluid_preset_get_num(preset);
            entry->font = (uint8_t)(fonts - 1 - index);
            if (name != NULL)
                (void)snprintf(entry->name, sizeof(entry->name), "%s", name);
        }
    }
    if (count != 0u)
        qsort(all, count, sizeof(*all), compare_presets);
    for (uint32_t at = 0u; at < count; ++at)
        if (kept == 0u || all[kept - 1u].bank != all[at].bank ||
            all[kept - 1u].program != all[at].program)
            all[kept++] = all[at];
    (void)pthread_mutex_lock(&synth->lock);
    free(synth->presets);
    synth->presets = all;
    synth->preset_count = kept;
    (void)pthread_mutex_unlock(&synth->lock);
}

/* One short MIDI message. System messages are not for a synthesizer
 * voice; they are ignored. */
static void event(AstraAudioSynth *synth, const uint8_t message[4])
{
    int channel = message[0] & 0x0f;
    int data1 = message[1] & 0x7f, data2 = message[2] & 0x7f;

    switch (message[0] & 0xf0) {
    case 0x80:
        (void)fluid_synth_noteoff(synth->fluid, channel, data1);
        break;
    case 0x90:
        if (data2 == 0)
            (void)fluid_synth_noteoff(synth->fluid, channel, data1);
        else
            (void)fluid_synth_noteon(synth->fluid, channel, data1, data2);
        break;
    case 0xa0:
        (void)fluid_synth_key_pressure(synth->fluid, channel, data1, data2);
        break;
    case 0xb0:
        (void)fluid_synth_cc(synth->fluid, channel, data1, data2);
        break;
    case 0xc0:
        (void)fluid_synth_program_change(synth->fluid, channel, data1);
        break;
    case 0xd0:
        (void)fluid_synth_channel_pressure(synth->fluid, channel, data1);
        break;
    case 0xe0:
        (void)fluid_synth_pitch_bend(synth->fluid, channel,
                                     data1 | data2 << 7);
        break;
    default:
        break;
    }
}

static void set(AstraAudioSynth *synth, uint32_t setting, uint32_t value)
{
    fluid_synth_t *fluid = synth->fluid;
    double thousandths = (double)value / 1000.0;

    switch (setting) {
    case ASTRA_HOST_MIDI_SET_REVERB:
        (void)fluid_synth_reverb_on(fluid, -1, (int)value);
        break;
    case ASTRA_HOST_MIDI_SET_REVERB_ROOM:
        (void)fluid_synth_set_reverb_group_roomsize(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_REVERB_DAMP:
        (void)fluid_synth_set_reverb_group_damp(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_REVERB_WIDTH:
        (void)fluid_synth_set_reverb_group_width(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_REVERB_LEVEL:
        (void)fluid_synth_set_reverb_group_level(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_CHORUS:
        (void)fluid_synth_chorus_on(fluid, -1, (int)value);
        break;
    case ASTRA_HOST_MIDI_SET_CHORUS_VOICES:
        (void)fluid_synth_set_chorus_group_nr(fluid, -1, (int)value);
        break;
    case ASTRA_HOST_MIDI_SET_CHORUS_LEVEL:
        (void)fluid_synth_set_chorus_group_level(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_CHORUS_SPEED:
        (void)fluid_synth_set_chorus_group_speed(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_CHORUS_DEPTH:
        (void)fluid_synth_set_chorus_group_depth(fluid, -1, thousandths);
        break;
    case ASTRA_HOST_MIDI_SET_POLYPHONY:
        if (fluid_synth_set_polyphony(fluid, (int)value) != FLUID_OK)
            fail(synth, ASTRA_STATUS_NO_SPACE);
        break;
    case ASTRA_HOST_MIDI_SET_TEMPO:
        synth->tempo = thousandths;
        if (synth->player != NULL)
            (void)fluid_player_set_tempo(synth->player,
                                         FLUID_PLAYER_TEMPO_INTERNAL,
                                         synth->tempo);
        break;
    case ASTRA_HOST_MIDI_SET_POSITION:
        if (synth->player != NULL &&
            fluid_player_seek(synth->player, (int)value) != FLUID_OK)
            fail(synth, ASTRA_STATUS_INVALID);
        break;
    default:
        break;
    }
}

/* Where the song is, for astra_audio_synth_report(). */
static void publish(AstraAudioSynth *synth)
{
    AstraAudioSynthStatus report = {0};

    if (synth->player != NULL) {
        int tick = fluid_player_get_current_tick(synth->player);
        int total = fluid_player_get_total_ticks(synth->player);
        int division = fluid_player_get_division(synth->player);
        int tempo = fluid_player_get_midi_tempo(synth->player);

        report.position_ticks = tick > 0 ? (uint32_t)tick : 0u;
        report.length_ticks = total > 0 ? (uint32_t)total : 0u;
        report.ticks_per_quarter = division > 0 ? (uint32_t)division : 0u;
        report.tempo_us_per_quarter = tempo > 0 ? (uint32_t)tempo : 0u;
    }
    (void)pthread_mutex_lock(&synth->lock);
    synth->report.position_ticks = report.position_ticks;
    synth->report.length_ticks = report.length_ticks;
    synth->report.ticks_per_quarter = report.ticks_per_quarter;
    synth->report.tempo_us_per_quarter = report.tempo_us_per_quarter;
    (void)pthread_mutex_unlock(&synth->lock);
}

static void apply(AstraAudioSynth *synth, Command *command)
{
    switch (command->kind) {
    case COMMAND_FONT:
        if (fluid_synth_sfload(synth->fluid, command->path, 1) ==
            FLUID_FAILED)
            fail(synth, ASTRA_STATUS_INVALID);
        rebuild_presets(synth);
        (void)atomic_fetch_sub(&synth->fonts_loading, 1u);
        break;
    case COMMAND_LOAD:
        drop_player(synth);
        (void)fluid_synth_all_notes_off(synth->fluid, -1);
        free(synth->song);
        synth->song = command->bytes;
        synth->song_bytes = command->length;
        command->bytes = NULL;
        break;
    case COMMAND_PLAY:
        play(synth, command->value);
        break;
    case COMMAND_PAUSE:
        if (synth->player == NULL)
            break;
        if (command->value != 0) {
            (void)fluid_player_stop(synth->player);
            (void)fluid_synth_all_notes_off(synth->fluid, -1);
        } else if (fluid_player_get_status(synth->player) !=
                   FLUID_PLAYER_DONE) {
            (void)fluid_player_play(synth->player);
            synth->sounding = 1;
        }
        break;
    case COMMAND_STOP:
        drop_player(synth);
        (void)fluid_synth_all_sounds_off(synth->fluid, -1);
        break;
    case COMMAND_EVENTS:
        for (uint32_t at = 0u; at + 4u <= command->length; at += 4u)
            event(synth, command->bytes + at);
        /* A note started here rings until render() hears it end. */
        synth->sounding = 1;
        break;
    case COMMAND_SET:
        set(synth, (uint32_t)command->value, command->argument);
        break;
    }
}

/* Renders while the song plays or its voices still sound, then goes
 * quiet: an idle synth costs the host nothing. */
static void render(AstraAudioSynth *synth)
{
    for (;;) {
        uint32_t written = atomic_load_explicit(&synth->written,
                                                memory_order_relaxed);
        uint32_t taken = atomic_load_explicit(&synth->taken,
                                              memory_order_acquire);
        int playing = synth->player != NULL &&
            fluid_player_get_status(synth->player) == FLUID_PLAYER_PLAYING;

        if (!playing && fluid_synth_get_active_voice_count(synth->fluid) ==
                            0) {
            synth->sounding = 0;
            /* Quiet only with nothing queued: a PLAY submitted meanwhile
             * set active under this lock and must not be undone. */
            (void)pthread_mutex_lock(&synth->lock);
            if (synth->head == NULL)
                atomic_store(&synth->active, 0);
            (void)pthread_mutex_unlock(&synth->lock);
            return;
        }
        atomic_store(&synth->active, 1);
        if (written - taken + BLOCK_FRAMES > AHEAD_FRAMES)
            return;
        if (fluid_synth_write_float(synth->fluid, BLOCK_FRAMES, synth->left,
                                    0, 1, synth->right, 0, 1) != FLUID_OK) {
            fail(synth, ASTRA_STATUS_IO);
            drop_player(synth);
            (void)fluid_synth_all_sounds_off(synth->fluid, -1);
            continue;
        }
        for (uint32_t frame = 0u; frame < BLOCK_FRAMES; ++frame) {
            float *slot = synth->ring[(written + frame) % RING_FRAMES];

            slot[0] = synth->left[frame];
            slot[1] = synth->right[frame];
        }
        atomic_store_explicit(&synth->written, written + BLOCK_FRAMES,
                              memory_order_release);
    }
}

static void *run(void *argument)
{
    AstraAudioSynth *synth = argument;

    for (;;) {
        Command *command;

        (void)pthread_mutex_lock(&synth->lock);
        while (!synth->closing && synth->head == NULL) {
            struct timespec until;

            if (!synth->sounding) {
                (void)pthread_cond_wait(&synth->wake, &synth->lock);
                continue;
            }
            if (atomic_load(&synth->written) - atomic_load(&synth->taken) +
                    BLOCK_FRAMES <= AHEAD_FRAMES)
                break;
            (void)clock_gettime(CLOCK_MONOTONIC, &until);
            until.tv_nsec += POLL_NS;
            if (until.tv_nsec >= 1000000000L) {
                until.tv_nsec -= 1000000000L;
                ++until.tv_sec;
            }
            (void)pthread_cond_timedwait(&synth->wake, &synth->lock, &until);
        }
        if (synth->closing) {
            (void)pthread_mutex_unlock(&synth->lock);
            return NULL;
        }
        command = synth->head;
        if (command != NULL) {
            synth->head = command->next;
            if (synth->head == NULL)
                synth->tail = &synth->head;
        }
        (void)pthread_mutex_unlock(&synth->lock);
        if (command != NULL) {
            apply(synth, command);
            free(command->bytes);
            free(command);
        }
        render(synth);
        publish(synth);
    }
}

static uint32_t submit_with(AstraAudioSynth *synth, CommandKind kind,
                            int32_t value, uint32_t argument,
                            const void *bytes, uint32_t length,
                            const char *path)
{
    size_t path_bytes = path != NULL ? strlen(path) + 1u : 0u;
    Command *command = calloc(1u, sizeof(*command) + path_bytes);

    if (command == NULL)
        return ASTRA_STATUS_NO_SPACE;
    command->kind = kind;
    command->value = value;
    command->argument = argument;
    if (path != NULL)
        memcpy(command->path, path, path_bytes);
    if (length != 0u) {
        command->bytes = malloc(length);
        if (command->bytes == NULL) {
            free(command);
            return ASTRA_STATUS_NO_SPACE;
        }
        memcpy(command->bytes, bytes, length);
        command->length = length;
    }
    (void)pthread_mutex_lock(&synth->lock);
    *synth->tail = command;
    synth->tail = &command->next;
    /* A command that makes sound counts as sound from now: the feeder must
     * not see the synth idle between this call and the first block. */
    if (kind == COMMAND_PLAY || kind == COMMAND_EVENTS ||
        (kind == COMMAND_PAUSE && value == 0))
        atomic_store(&synth->active, 1);
    if (kind == COMMAND_FONT)
        (void)atomic_fetch_add(&synth->fonts_loading, 1u);
    (void)pthread_cond_signal(&synth->wake);
    (void)pthread_mutex_unlock(&synth->lock);
    return ASTRA_STATUS_OK;
}

static uint32_t submit(AstraAudioSynth *synth, CommandKind kind,
                       int32_t value, const void *bytes, uint32_t length,
                       const char *path)
{
    return submit_with(synth, kind, value, 0u, bytes, length, path);
}

void astra_audio_synth_set_sound_fonts(int directory)
{
    atomic_store(&sound_fonts, directory);
}

int astra_audio_synth_sound_font_name(const char *name)
{
    size_t length = name != NULL ? strlen(name) : 0u;

    if (length < 5u || length > 128u || name[0] == '.' ||
        strcmp(name + length - 4u, ".sf2") != 0)
        return 0;
    for (size_t at = 0u; at < length; ++at) {
        char c = name[at];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ||
              c == ' '))
            return 0;
    }
    return 1;
}

int astra_audio_synth_open_sound_font(const char *name)
{
    struct open_how how = {
        .flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
                   RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV,
    };
    int directory = atomic_load(&sound_fonts);
    struct stat st;
    int fd;

    if (directory < 0 || !astra_audio_synth_sound_font_name(name)) {
        errno = EINVAL;
        return -1;
    }
    fd = (int)syscall(SYS_openat2, directory, name, &how, sizeof(how));
    if (fd >= 0 && (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))) {
        (void)close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

/* FluidSynth's file access for every font, so a shared font is read from
 * beneath the SOUND directory however it is named afterwards. */
static void *font_open(const char *name)
{
    int fd;
    FILE *file;

    if (strncmp(name, SOUND_PREFIX, sizeof(SOUND_PREFIX) - 1u) == 0)
        fd = astra_audio_synth_open_sound_font(
            name + sizeof(SOUND_PREFIX) - 1u);
    else
        fd = open(name, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    file = fdopen(fd, "rb");
    if (file == NULL)
        (void)close(fd);
    return file;
}

static int font_read(void *buffer, fluid_long_long_t count, void *handle)
{
    return count >= 0 && fread(buffer, 1u, (size_t)count, handle) ==
                             (size_t)count ? FLUID_OK : FLUID_FAILED;
}

static int font_seek(void *handle, fluid_long_long_t offset, int origin)
{
    return fseeko(handle, (off_t)offset, origin) == 0 ? FLUID_OK :
                                                        FLUID_FAILED;
}

static fluid_long_long_t font_tell(void *handle)
{
    return (fluid_long_long_t)ftello(handle);
}

static int font_close(void *handle)
{
    return fclose(handle) == 0 ? FLUID_OK : FLUID_FAILED;
}

AstraAudioSynth *astra_audio_synth_open(uint32_t rate)
{
    fluid_sfloader_t *loader;

    AstraAudioSynth *synth = calloc(1u, sizeof(*synth));

    if (synth == NULL)
        return NULL;
    synth->tail = &synth->head;
    atomic_init(&synth->written, 0u);
    atomic_init(&synth->taken, 0u);
    atomic_init(&synth->active, 0);
    atomic_init(&synth->status, ASTRA_STATUS_OK);
    atomic_init(&synth->fonts_loading, 0u);
    synth->tempo = 1.0;
    synth->settings = new_fluid_settings();
    if (synth->settings == NULL)
        goto failed;
    /* Samples come off disk as presets are chosen, not all at load: a
     * General MIDI bank is mostly instruments a song never plays. */
    if (fluid_settings_setnum(synth->settings, "synth.sample-rate",
                              (double)rate) != FLUID_OK ||
        fluid_settings_setint(synth->settings,
                              "synth.dynamic-sample-loading", 1) !=
            FLUID_OK ||
        fluid_settings_setstr(synth->settings, "player.timing-source",
                              "sample") != FLUID_OK)
        goto failed;
    synth->fluid = new_fluid_synth(synth->settings);
    if (synth->fluid == NULL)
        goto failed;
    /* Ahead of FluidSynth's own loader, which opens names as plain paths.
     * A "sound:" name is not a path that loader can open, so a shared font
     * this one refuses stays refused. */
    loader = new_fluid_defsfloader(synth->settings);
    if (loader == NULL ||
        fluid_sfloader_set_callbacks(loader, font_open, font_read, font_seek,
                                     font_tell, font_close) != FLUID_OK) {
        if (loader != NULL)
            delete_fluid_sfloader(loader);
        goto failed;
    }
    fluid_synth_add_sfloader(synth->fluid, loader);
    if (pthread_mutex_init(&synth->lock, NULL) != 0)
        goto failed;
    if (pthread_cond_init(&synth->wake, NULL) != 0) {
        (void)pthread_mutex_destroy(&synth->lock);
        goto failed;
    }
    if (pthread_create(&synth->thread, NULL, run, synth) != 0) {
        (void)pthread_cond_destroy(&synth->wake);
        (void)pthread_mutex_destroy(&synth->lock);
        goto failed;
    }
    return synth;
failed:
    if (synth->fluid != NULL)
        delete_fluid_synth(synth->fluid);
    if (synth->settings != NULL)
        delete_fluid_settings(synth->settings);
    free(synth);
    return NULL;
}

void astra_audio_synth_close(AstraAudioSynth *synth)
{
    if (synth == NULL)
        return;
    (void)pthread_mutex_lock(&synth->lock);
    synth->closing = 1;
    (void)pthread_cond_signal(&synth->wake);
    (void)pthread_mutex_unlock(&synth->lock);
    (void)pthread_join(synth->thread, NULL);
    while (synth->head != NULL) {
        Command *command = synth->head;

        synth->head = command->next;
        free(command->bytes);
        free(command);
    }
    drop_player(synth);
    delete_fluid_synth(synth->fluid);
    delete_fluid_settings(synth->settings);
    (void)pthread_cond_destroy(&synth->wake);
    (void)pthread_mutex_destroy(&synth->lock);
    free(synth->song);
    free(synth->presets);
    free(synth);
}

uint32_t astra_audio_synth_add_font(AstraAudioSynth *synth, const char *path)
{
    return submit(synth, COMMAND_FONT, 0, NULL, 0u, path);
}

uint32_t astra_audio_synth_events(AstraAudioSynth *synth,
                                  const uint8_t *events, uint32_t count)
{
    if (count == 0u || count > UINT32_MAX / 4u)
        return ASTRA_STATUS_INVALID;
    return submit(synth, COMMAND_EVENTS, 0, events, count * 4u, NULL);
}

uint32_t astra_audio_synth_set(AstraAudioSynth *synth, uint32_t setting,
                               uint32_t value)
{
    static const struct {
        uint32_t low, high;
    } ranges[ASTRA_HOST_MIDI_SET_MAX + 1u] = {
        [ASTRA_HOST_MIDI_SET_REVERB] = {0u, 1u},
        [ASTRA_HOST_MIDI_SET_REVERB_ROOM] = {0u, 1000u},
        [ASTRA_HOST_MIDI_SET_REVERB_DAMP] = {0u, 1000u},
        [ASTRA_HOST_MIDI_SET_REVERB_WIDTH] = {0u, 100000u},
        [ASTRA_HOST_MIDI_SET_REVERB_LEVEL] = {0u, 1000u},
        [ASTRA_HOST_MIDI_SET_CHORUS] = {0u, 1u},
        [ASTRA_HOST_MIDI_SET_CHORUS_VOICES] = {0u, 99u},
        [ASTRA_HOST_MIDI_SET_CHORUS_LEVEL] = {0u, 10000u},
        [ASTRA_HOST_MIDI_SET_CHORUS_SPEED] = {100u, 5000u},
        [ASTRA_HOST_MIDI_SET_CHORUS_DEPTH] = {0u, 256000u},
        [ASTRA_HOST_MIDI_SET_POLYPHONY] = {1u, 65535u},
        [ASTRA_HOST_MIDI_SET_TEMPO] = {1u, 100000u},
        [ASTRA_HOST_MIDI_SET_POSITION] = {0u, INT32_MAX},
    };

    if (setting == 0u || setting > ASTRA_HOST_MIDI_SET_MAX ||
        value < ranges[setting].low || value > ranges[setting].high)
        return ASTRA_STATUS_INVALID;
    return submit_with(synth, COMMAND_SET, (int32_t)setting, value, NULL, 0u,
                       NULL);
}

void astra_audio_synth_report(AstraAudioSynth *synth,
                              AstraAudioSynthStatus *report)
{
    (void)pthread_mutex_lock(&synth->lock);
    *report = synth->report;
    (void)pthread_mutex_unlock(&synth->lock);
    report->sounding = astra_audio_synth_active(synth) ? 1u : 0u;
    report->fonts_loading = atomic_load(&synth->fonts_loading);
}

uint32_t astra_audio_synth_presets(AstraAudioSynth *synth, uint32_t first,
                                   AstraAudioSynthPreset *presets,
                                   uint32_t capacity, uint32_t *copied,
                                   uint32_t *total)
{
    uint32_t count = 0u;

    if (atomic_load(&synth->fonts_loading) != 0u)
        return ASTRA_STATUS_BUSY;
    (void)pthread_mutex_lock(&synth->lock);
    *total = synth->preset_count;
    for (uint32_t at = first; at < synth->preset_count && count < capacity;
         ++at)
        presets[count++] = synth->presets[at];
    (void)pthread_mutex_unlock(&synth->lock);
    *copied = count;
    return ASTRA_STATUS_OK;
}

uint32_t astra_audio_synth_load(AstraAudioSynth *synth, const uint8_t *song,
                                uint32_t bytes)
{
    if (bytes == 0u)
        return ASTRA_STATUS_INVALID;
    return submit(synth, COMMAND_LOAD, 0, song, bytes, NULL);
}

uint32_t astra_audio_synth_play(AstraAudioSynth *synth, int32_t plays)
{
    if (plays == 0 || plays < -1)
        return ASTRA_STATUS_INVALID;
    return submit(synth, COMMAND_PLAY, plays, NULL, 0u, NULL);
}

uint32_t astra_audio_synth_pause(AstraAudioSynth *synth, int paused)
{
    return submit(synth, COMMAND_PAUSE, paused != 0, NULL, 0u, NULL);
}

uint32_t astra_audio_synth_stop(AstraAudioSynth *synth)
{
    return submit(synth, COMMAND_STOP, 0, NULL, 0u, NULL);
}

int astra_audio_synth_active(const AstraAudioSynth *synth)
{
    return atomic_load(&synth->active) ||
           astra_audio_synth_ready(synth) != 0u;
}

uint32_t astra_audio_synth_status(const AstraAudioSynth *synth)
{
    return atomic_load(&synth->status);
}

uint32_t astra_audio_synth_ready(const AstraAudioSynth *synth)
{
    return atomic_load_explicit(&synth->written, memory_order_acquire) -
           atomic_load_explicit(&synth->taken, memory_order_relaxed);
}

uint32_t astra_audio_synth_read(AstraAudioSynth *synth, float (*frames)[2],
                                uint32_t count)
{
    uint32_t taken = atomic_load_explicit(&synth->taken,
                                          memory_order_relaxed);
    uint32_t ready = astra_audio_synth_ready(synth);

    if (count > ready)
        count = ready;
    for (uint32_t frame = 0u; frame < count; ++frame) {
        const float *slot = synth->ring[(taken + frame) % RING_FRAMES];

        frames[frame][0] = slot[0];
        frames[frame][1] = slot[1];
    }
    atomic_store_explicit(&synth->taken, taken + count,
                          memory_order_release);
    return count;
}
