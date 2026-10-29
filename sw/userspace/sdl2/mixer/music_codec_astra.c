/* SDL_mixer music adapter: Codec Kit decodes ahead of the audio callback. */
#include "music_ogg.h"
#include "music_xmp.h"
#include "SDL.h"

#include <astra/codec.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#include <stdatomic.h>
#include <stdint.h>
#include <limits.h>

#define CODEC_AUDIO_BYTES 32768u
#define MUSIC_QUEUE_BYTES 524288
#define MUSIC_PREROLL_BYTES 131072
#define MUSIC_PREROLL_TIMEOUT_MS 3000u

typedef struct AstraCodecMusic {
    SDL_RWops *src;
    AstraCodecStream codec;
    AstraCodecFormat format;
    SDL_AudioStream *stream;
    SDL_Thread *thread;
    SDL_mutex *codec_lock;
    SDL_mutex *queue_lock;
    SDL_cond *changed;
    uint8_t *buffer;
    uint64_t position_ns;
    uint32_t generation;
    uint32_t underflows;
    uint32_t consecutive_underflows;
    uint32_t max_consecutive_underflows;
    uint32_t preroll_wait_ms;
    int max_buffered;
    uint64_t first_underflow_ms;
    uint64_t last_underflow_ms;
    uint64_t play_start_ms;
    uint64_t last_put_ms;
    uint64_t first_underflow_since_play_ms;
    uint64_t first_underflow_since_put_ms;
    int first_underflow_buffered;
    int freesrc;
    int play_count;
    _Atomic int playing;
    _Atomic int volume;
    int eof;
    int stopping;
} AstraCodecMusic;

static AstraResult read_at(void *context, uint64_t offset, void *output,
                           uint32_t capacity, uint32_t *received)
{
    SDL_RWops *source = context;

    if (offset > INT64_MAX ||
        SDL_RWseek(source, (Sint64)offset, RW_SEEK_SET) < 0)
        return ASTRA_ERROR_IO;
    *received = (uint32_t)SDL_RWread(source, output, 1u, capacity);
    return ASTRA_OK;
}

static void fail_music(AstraCodecMusic *music, uint32_t generation,
                       const char *reason)
{
    SDL_LockMutex(music->queue_lock);
    if (generation == music->generation && music->playing) {
        music->playing = 0;
        music->play_count = 0;
        SDL_CondSignal(music->changed);
        SDL_Log("Astra Codec music: %s", reason);
    }
    SDL_UnlockMutex(music->queue_lock);
}

static int pump_music(void *opaque)
{
    AstraCodecMusic *music = opaque;

    (void)SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    for (;;) {
        AstraCodecChunk chunk = {0};
        uint32_t generation;
        AstraResult result;
        int loop = 0;

        SDL_LockMutex(music->queue_lock);
        while (!music->stopping && (!music->playing || music->eof ||
               SDL_AudioStreamAvailable(music->stream) >= MUSIC_QUEUE_BYTES))
            SDL_CondWait(music->changed, music->queue_lock);
        if (music->stopping) {
            SDL_UnlockMutex(music->queue_lock);
            break;
        }
        SDL_UnlockMutex(music->queue_lock);

        SDL_LockMutex(music->codec_lock);
        SDL_LockMutex(music->queue_lock);
        generation = music->generation;
        if (!music->playing || music->eof || music->stopping) {
            SDL_UnlockMutex(music->queue_lock);
            SDL_UnlockMutex(music->codec_lock);
            continue;
        }
        SDL_UnlockMutex(music->queue_lock);
        result = astra_codec_read(&music->codec, music->buffer,
                                  CODEC_AUDIO_BYTES, &chunk);
        SDL_LockMutex(music->queue_lock);
        if (generation == music->generation && music->playing &&
            result == ASTRA_OK && chunk.bytes <= CODEC_AUDIO_BYTES &&
            chunk.bytes % (music->format.channels * 2u) == 0u) {
            if (chunk.bytes != 0u &&
                SDL_AudioStreamPut(music->stream, music->buffer,
                                   (int)chunk.bytes) < 0)
                result = ASTRA_ERROR_IO;
            else {
                int buffered = SDL_AudioStreamAvailable(music->stream);
                if (chunk.bytes != 0u) music->last_put_ms = SDL_GetTicks64();
                if (buffered > music->max_buffered)
                    music->max_buffered = buffered;
                music->position_ns = chunk.timestamp_ns + chunk.duration_ns;
                if (chunk.end) {
                    if (music->play_count == 1) {
                        music->play_count = 0;
                        music->eof = 1;
                        if (SDL_AudioStreamFlush(music->stream) < 0)
                            result = ASTRA_ERROR_IO;
                    } else {
                        if (music->play_count > 1) --music->play_count;
                        loop = 1;
                    }
                } else if (chunk.bytes == 0u) {
                    result = ASTRA_ERROR_IO;
                }
                SDL_CondSignal(music->changed);
            }
        } else if (generation == music->generation) {
            result = ASTRA_ERROR_IO;
        }
        SDL_UnlockMutex(music->queue_lock);
        if (loop && astra_codec_seek(&music->codec, 0u) != ASTRA_OK)
            result = ASTRA_ERROR_IO;
        SDL_UnlockMutex(music->codec_lock);
        if (result != ASTRA_OK)
            fail_music(music, generation, "decode or queue failed");
    }
    return 0;
}

static void delete_music(void *opaque)
{
    AstraCodecMusic *music = opaque;

    if (music == NULL) return;
    if (music->thread != NULL) {
        SDL_LockMutex(music->queue_lock);
        music->stopping = 1;
        music->playing = 0;
        SDL_CondSignal(music->changed);
        SDL_UnlockMutex(music->queue_lock);
        SDL_WaitThread(music->thread, NULL);
    }
    SDL_Log("Astra Codec music: underflows=%u longest_run=%u "
            "first_ms=%llu last_ms=%llu highwater_bytes=%d preroll_ms=%u "
            "first_since_play_ms=%llu first_since_put_ms=%llu "
            "first_buffered_bytes=%d",
            music->underflows, music->max_consecutive_underflows,
            (unsigned long long)music->first_underflow_ms,
            (unsigned long long)music->last_underflow_ms,
            music->max_buffered, music->preroll_wait_ms,
            (unsigned long long)music->first_underflow_since_play_ms,
            (unsigned long long)music->first_underflow_since_put_ms,
            music->first_underflow_buffered);
    if (music->codec.control != 0u) {
        AstraResult ignored = astra_codec_close(&music->codec);
        (void)ignored;
    }
    if (music->changed != NULL) SDL_DestroyCond(music->changed);
    if (music->queue_lock != NULL) SDL_DestroyMutex(music->queue_lock);
    if (music->codec_lock != NULL) SDL_DestroyMutex(music->codec_lock);
    if (music->stream != NULL) SDL_FreeAudioStream(music->stream);
    SDL_free(music->buffer);
    if (music->freesrc && music->src != NULL) SDL_RWclose(music->src);
    SDL_free(music);
}

static void *create_music(SDL_RWops *src, int freesrc)
{
    const AstraStartupCapability *capability;
    AstraCodecMusic *music;
    Sint64 length;

    capability = astra_startup_capability(astra_posix_startup(), "CODEC");
    length = SDL_RWsize(src);
    if (capability == NULL || length <= 0) {
        Mix_SetError(capability == NULL ? "Codec capability missing" :
                     "Source length unavailable");
        return NULL;
    }
    music = SDL_calloc(1u, sizeof(*music));
    if (music == NULL) return NULL;
    atomic_init(&music->playing, 0);
    atomic_init(&music->volume, MIX_MAX_VOLUME);
    music->src = src;
    music->buffer = SDL_malloc(CODEC_AUDIO_BYTES);
    music->codec_lock = SDL_CreateMutex();
    music->queue_lock = SDL_CreateMutex();
    music->changed = SDL_CreateCond();
    if (music->buffer == NULL || music->codec_lock == NULL ||
        music->queue_lock == NULL || music->changed == NULL) {
        Mix_SetError("Codec playback resources unavailable");
        goto failed;
    }
    if (astra_codec_open(capability->handle, read_at, src,
                         (uint64_t)length, &music->codec,
                         &music->format) != ASTRA_OK) {
        Mix_SetError("Codec open failed");
        goto failed;
    }
    if (music->format.media_kind != ASTRA_CODEC_MEDIA_AUDIO ||
        music->format.encoding != ASTRA_CODEC_AUDIO_S16LE ||
        music->format.channels == 0u || music->format.channels > 2u ||
        music->format.sample_rate == 0u ||
        music->format.sample_rate > INT_MAX) {
        Mix_SetError("Codec audio format unsupported");
        goto failed;
    }
    music->stream = SDL_NewAudioStream(AUDIO_S16LSB,
        (Uint8)music->format.channels, (int)music->format.sample_rate,
        music_spec.format, music_spec.channels, music_spec.freq);
    if (music->stream == NULL) goto failed;
    music->thread = SDL_CreateThread(pump_music, "codec-audio", music);
    if (music->thread == NULL) {
        Mix_SetError("Codec playback thread unavailable");
        goto failed;
    }
    music->freesrc = freesrc;
    return music;
failed:
    delete_music(music);
    return NULL;
}

static void set_volume(void *opaque, int volume)
{
    AstraCodecMusic *music = opaque;

    if (volume < 0) volume = 0;
    if (volume > MIX_MAX_VOLUME) volume = MIX_MAX_VOLUME;
    atomic_store_explicit(&music->volume, volume, memory_order_relaxed);
}

static int get_volume(void *opaque)
{
    AstraCodecMusic *music = opaque;

    return atomic_load_explicit(&music->volume, memory_order_relaxed);
}

static int seek_music(void *opaque, double seconds)
{
    AstraCodecMusic *music = opaque;
    uint64_t position;
    uint64_t start_ms, deadline_ms;
    AstraResult result;
    int ready;

    if (seconds < 0.0 || seconds > 18446744073.0) {
        Mix_SetError("Codec music seek out of range");
        return -1;
    }
    position = (uint64_t)(seconds * 1000000000.0);
    SDL_LockMutex(music->codec_lock);
    result = astra_codec_seek(&music->codec, position);
    SDL_LockMutex(music->queue_lock);
    ++music->generation;
    SDL_AudioStreamClear(music->stream);
    music->position_ns = position;
    music->eof = 0;
    if (result != ASTRA_OK) music->playing = 0;
    SDL_CondSignal(music->changed);
    SDL_UnlockMutex(music->queue_lock);
    SDL_UnlockMutex(music->codec_lock);
    if (result != ASTRA_OK) {
        Mix_SetError("Astra Codec seek failed");
        return -1;
    }

    /* SDL_mixer seeks immediately after Play, while its audio lock is held. */
    start_ms = SDL_GetTicks64();
    deadline_ms = start_ms + MUSIC_PREROLL_TIMEOUT_MS;
    SDL_LockMutex(music->queue_lock);
    while (music->playing && !music->eof &&
           SDL_AudioStreamAvailable(music->stream) < MUSIC_PREROLL_BYTES) {
        uint64_t now_ms = SDL_GetTicks64();

        if (now_ms >= deadline_ms) {
            music->playing = 0;
            ++music->generation;
            break;
        }
        (void)SDL_CondWaitTimeout(music->changed, music->queue_lock,
                                  (Uint32)(deadline_ms - now_ms));
    }
    music->preroll_wait_ms = (uint32_t)(SDL_GetTicks64() - start_ms);
    ready = music->playing;
    SDL_UnlockMutex(music->queue_lock);
    if (!ready) Mix_SetError("Codec music preroll failed");
    return ready ? 0 : -1;
}

static int play_music(void *opaque, int play_count)
{
    AstraCodecMusic *music = opaque;

    SDL_LockMutex(music->queue_lock);
    music->play_count = play_count;
    music->play_start_ms = SDL_GetTicks64();
    music->last_put_ms = music->play_start_ms;
    music->eof = 0;
    music->playing = 1;
    SDL_CondSignal(music->changed);
    SDL_UnlockMutex(music->queue_lock);
    return 0;
}

static SDL_bool is_playing(void *opaque)
{
    AstraCodecMusic *music = opaque;

    return atomic_load_explicit(&music->playing,
                                memory_order_relaxed) ? SDL_TRUE : SDL_FALSE;
}

static int get_some(void *opaque, void *data, int bytes, SDL_bool *done)
{
    AstraCodecMusic *music = opaque;
    int got;
    int buffered;

    SDL_LockMutex(music->queue_lock);
    buffered = SDL_AudioStreamAvailable(music->stream);
    got = SDL_AudioStreamGet(music->stream, data, bytes);
    if (got >= 0 && got < bytes) {
        if (music->eof || !music->playing) {
            music->playing = 0;
            *done = SDL_TRUE;
        } else {
            SDL_memset((uint8_t *)data + got, 0, (size_t)(bytes - got));
            ++music->underflows;
            ++music->consecutive_underflows;
            if (music->consecutive_underflows > music->max_consecutive_underflows)
                music->max_consecutive_underflows = music->consecutive_underflows;
            music->last_underflow_ms = SDL_GetTicks64();
            if (music->first_underflow_ms == 0u) {
                music->first_underflow_ms = music->last_underflow_ms;
                music->first_underflow_since_play_ms =
                    music->last_underflow_ms - music->play_start_ms;
                music->first_underflow_since_put_ms =
                    music->last_underflow_ms - music->last_put_ms;
                music->first_underflow_buffered = buffered;
            }
            got = bytes;
        }
    } else if (got == bytes) music->consecutive_underflows = 0u;
    SDL_CondSignal(music->changed);
    SDL_UnlockMutex(music->queue_lock);
    return got;
}

static int get_audio(void *opaque, void *data, int bytes)
{
    AstraCodecMusic *music = opaque;

    return music_pcm_getaudio(opaque, data, bytes, get_volume(music), get_some);
}

static void stop_music(void *opaque)
{
    AstraCodecMusic *music = opaque;

    SDL_LockMutex(music->queue_lock);
    ++music->generation;
    music->playing = 0;
    music->play_count = 0;
    SDL_AudioStreamClear(music->stream);
    SDL_CondSignal(music->changed);
    SDL_UnlockMutex(music->queue_lock);
}

static double tell_music(void *opaque)
{
    AstraCodecMusic *music = opaque;
    uint64_t position;

    SDL_LockMutex(music->queue_lock);
    position = music->position_ns;
    SDL_UnlockMutex(music->queue_lock);
    return (double)position / 1000000000.0;
}

static double unknown_duration(void *opaque)
{
    (void)opaque;
    return -1.0;
}

Mix_MusicInterface Mix_MusicInterface_OGG = {
    "OGG", MIX_MUSIC_OGG, MUS_OGG, SDL_FALSE, SDL_FALSE,
    NULL, NULL, create_music, NULL, set_volume, get_volume,
    play_music, is_playing, get_audio, NULL, seek_music, tell_music,
    unknown_duration, unknown_duration, unknown_duration, unknown_duration,
    NULL, NULL, NULL, NULL, NULL, stop_music,
    delete_music, NULL, NULL
};

/* Both formats use exactly the same Codec Kit stream and queue lifecycle.
 * The host provider selects the decoder from the source bytes. */
Mix_MusicInterface Mix_MusicInterface_XMP = {
    "XMP", MIX_MUSIC_LIBXMP, MUS_MOD, SDL_FALSE, SDL_FALSE,
    NULL, NULL, create_music, NULL, set_volume, get_volume,
    play_music, is_playing, get_audio, NULL, seek_music, tell_music,
    unknown_duration, unknown_duration, unknown_duration, unknown_duration,
    NULL, NULL, NULL, NULL, NULL, stop_music,
    delete_music, NULL, NULL
};
