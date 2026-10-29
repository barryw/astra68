#include "audio.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FORM_TAG UINT32_C(0x464f524d)
#define OGGV_TAG UINT32_C(0x4f474756)
#define MOD_TAG UINT32_C(0x4d4f4420)

struct AstraZAudio {
    Mix_Chunk *effect;
    Mix_Music *music;
    uint8_t *music_bytes;
    Mix_Chunk *beep[2];
    uint8_t *beep_bytes[2];
    int number;
    int playing;
    int music_playing;
};

#define BEEP_FRAMES 2646u

static int make_beep(AstraZAudio *audio, int index)
{
    const uint32_t sample_rate = 22050u;
    const uint32_t frequency = index == 0 ? 440u : 880u;
    uint32_t bytes = BEEP_FRAMES * 4u;
    uint8_t *data = malloc(bytes);
    if (!data) return 0;
    uint32_t phase = 0u;
    for (uint32_t at = 0u; at < BEEP_FRAMES; ++at) {
        phase += frequency;
        if (phase >= sample_rate) phase -= sample_rate;
        int32_t amplitude = phase < sample_rate / 2u ? 4800 : -4800;
        uint32_t fade = at < 128u ? at :
            BEEP_FRAMES - at < 128u ? BEEP_FRAMES - at : 128u;
        int16_t sample = (int16_t)(amplitude * (int32_t)fade / 128);
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            data[at * 4u + channel * 2u] = (uint8_t)(sample >> 8);
            data[at * 4u + channel * 2u + 1u] = (uint8_t)sample;
        }
    }
    audio->beep[index] = Mix_QuickLoad_RAW(data, bytes);
    if (!audio->beep[index]) { free(data); return 0; }
    audio->beep_bytes[index] = data;
    return 1;
}

static void stop(AstraZAudio *audio)
{
    if (audio->playing) {
        if (audio->music_playing) Mix_HaltMusic();
        else Mix_HaltChannel(0);
    }
    audio->playing = 0;
}

static void release(AstraZAudio *audio)
{
    stop(audio);
    if (audio->effect) Mix_FreeChunk(audio->effect);
    if (audio->music) Mix_FreeMusic(audio->music);
    free(audio->music_bytes);
    audio->effect = NULL;
    audio->music = NULL;
    audio->music_bytes = NULL;
    audio->number = 0;
}

AstraZAudio *astraz_audio_open(void)
{
    AstraZAudio *audio = calloc(1u, sizeof(*audio));
    if (!audio) return NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 ||
        (Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG) == 0 ||
        Mix_OpenAudio(22050, AUDIO_S16MSB, 2, 512) != 0 ||
        Mix_AllocateChannels(2) < 2) {
        Mix_CloseAudio();
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        free(audio);
        return NULL;
    }
    if (!make_beep(audio, 0) || !make_beep(audio, 1)) {
        astraz_audio_close(audio);
        return NULL;
    }
    return audio;
}

static int prepare(AstraZAudio *audio, const AstraZEvent *event)
{
    const AstraZResource *resource = &event->data.sound.resource;
    if (audio->number == event->data.sound.number &&
        (audio->effect || audio->music)) return 1;
    release(audio);
    if (!resource->bytes || !resource->length || resource->length > INT32_MAX - 8) {
        SDL_SetError("Invalid Blorb sound resource");
        return 0;
    }
    if (resource->format == FORM_TAG) {
        uint32_t length = resource->length;
        uint8_t *form = malloc(length + 8u);
        if (!form) return 0;
        memcpy(form, "FORM", 4u);
        form[4] = (uint8_t)(length >> 24);
        form[5] = (uint8_t)(length >> 16);
        form[6] = (uint8_t)(length >> 8);
        form[7] = (uint8_t)length;
        memcpy(form + 8u, resource->bytes, length);
        SDL_RWops *stream = SDL_RWFromConstMem(form, (int)(length + 8u));
        if (stream) audio->effect = Mix_LoadWAV_RW(stream, 1);
        free(form);
    } else if (resource->format == OGGV_TAG ||
               resource->format == MOD_TAG) {
        if (resource->format == MOD_TAG &&
            (Mix_Init(MIX_INIT_MOD) & MIX_INIT_MOD) == 0) {
            SDL_SetError("MOD playback is unavailable in SDL Kit");
            return 0;
        }
        audio->music_bytes = malloc(resource->length);
        if (!audio->music_bytes) return 0;
        memcpy(audio->music_bytes, resource->bytes, resource->length);
        SDL_RWops *stream = SDL_RWFromConstMem(audio->music_bytes,
                                                (int)resource->length);
        if (stream) audio->music = Mix_LoadMUS_RW(stream, 1);
    } else {
        SDL_SetError("Unsupported Blorb sound format");
        return 0;
    }
    if (!audio->effect && !audio->music) {
        release(audio);
        return 0;
    }
    audio->number = event->data.sound.number;
    return 1;
}

int astraz_audio_event(AstraZAudio *audio, const AstraZEvent *event)
{
    if (!audio || !event) return 0;
    switch (event->kind) {
    case ASTRAZ_EVENT_SOUND_PREPARE:
        return prepare(audio, event);
    case ASTRAZ_EVENT_SOUND_PLAY: {
        if (!prepare(audio, event)) return 0;
        stop(audio);
        int volume = event->data.sound.volume;
        int repeats = event->data.sound.repeats;
        int loops = repeats == 255 ? -1 : repeats > 1 ? repeats - 1 : 0;
        if (volume == 255) volume = 8;
        if (volume < 0) volume = 0;
        if (volume > 8) volume = 8;
        if (audio->music) {
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 8);
            if (Mix_PlayMusic(audio->music, loops) != 0) return 0;
            audio->music_playing = 1;
        } else {
            Mix_Volume(0, volume * MIX_MAX_VOLUME / 8);
            if (Mix_PlayChannel(0, audio->effect, loops) != 0) return 0;
            audio->music_playing = 0;
        }
        audio->playing = 1;
        return 1;
    }
    case ASTRAZ_EVENT_SOUND_STOP:
        if (event->data.sound.number == 0 ||
            event->data.sound.number == audio->number) stop(audio);
        return 1;
    case ASTRAZ_EVENT_SOUND_RELEASE:
        if (event->data.sound.number == 0 ||
            event->data.sound.number == audio->number) release(audio);
        return 1;
    case ASTRAZ_EVENT_BEEP:
        Mix_Volume(1, MIX_MAX_VOLUME / 3);
        return Mix_PlayChannel(1,
            audio->beep[event->data.beep.pitch == 2 ? 1 : 0], 0) == 1;
    default:
        return 0;
    }
}

int astraz_audio_finished(AstraZAudio *audio)
{
    if (!audio || !audio->playing ||
        (audio->music_playing ? Mix_PlayingMusic() : Mix_Playing(0)))
        return 0;
    audio->playing = 0;
    return 1;
}

void astraz_audio_close(AstraZAudio *audio)
{
    if (!audio) return;
    release(audio);
    Mix_HaltChannel(1);
    for (uint32_t at = 0u; at < 2u; ++at) {
        if (audio->beep[at]) Mix_FreeChunk(audio->beep[at]);
        free(audio->beep_bytes[at]);
    }
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(audio);
}
