// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_audio_synth.h"

#include <astra/status.h>

#include <fluidsynth.h>

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

typedef enum {
    COMMAND_FONT,
    COMMAND_LOAD,
    COMMAND_PLAY,
    COMMAND_PAUSE,
    COMMAND_STOP
} CommandKind;

typedef struct Command {
    struct Command *next;
    CommandKind kind;
    int32_t value;
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
        fluid_player_play(synth->player) != FLUID_OK) {
        drop_player(synth);
        fail(synth, ASTRA_STATUS_INVALID);
        return;
    }
    synth->sounding = 1;
}

static void apply(AstraAudioSynth *synth, Command *command)
{
    switch (command->kind) {
    case COMMAND_FONT:
        if (fluid_synth_sfload(synth->fluid, command->path, 1) ==
            FLUID_FAILED)
            fail(synth, ASTRA_STATUS_INVALID);
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
    }
}

static uint32_t submit(AstraAudioSynth *synth, CommandKind kind,
                       int32_t value, const void *bytes, uint32_t length,
                       const char *path)
{
    size_t path_bytes = path != NULL ? strlen(path) + 1u : 0u;
    Command *command = calloc(1u, sizeof(*command) + path_bytes);

    if (command == NULL)
        return ASTRA_STATUS_NO_SPACE;
    command->kind = kind;
    command->value = value;
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
    if (kind == COMMAND_PLAY ||
        (kind == COMMAND_PAUSE && value == 0))
        atomic_store(&synth->active, 1);
    (void)pthread_cond_signal(&synth->wake);
    (void)pthread_mutex_unlock(&synth->lock);
    return ASTRA_STATUS_OK;
}

AstraAudioSynth *astra_audio_synth_open(uint32_t rate)
{
    AstraAudioSynth *synth = calloc(1u, sizeof(*synth));

    if (synth == NULL)
        return NULL;
    synth->tail = &synth->head;
    atomic_init(&synth->written, 0u);
    atomic_init(&synth->taken, 0u);
    atomic_init(&synth->active, 0);
    atomic_init(&synth->status, ASTRA_STATUS_OK);
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
    free(synth);
}

uint32_t astra_audio_synth_add_font(AstraAudioSynth *synth, const char *path)
{
    return submit(synth, COMMAND_FONT, 0, NULL, 0u, path);
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
