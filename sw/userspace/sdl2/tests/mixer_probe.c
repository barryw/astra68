#include <SDL.h>
#include <SDL_mixer.h>
#include <astra/runtime.h>

#include <string.h>

#define FRAMES 4800u
static unsigned char wave[44u + FRAMES * 4u];

static SDL_AssertState assertion_failed(const SDL_AssertData *data, void *context)
{
    (void)context;
    (void)astra_log(data->condition);
    (void)astra_log("SDL_MIXER_ASSERT_FAIL");
    return SDL_ASSERTION_ABORT;
}

static void le16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void le32(unsigned char *p, unsigned value)
{
    le16(p, value);
    le16(p + 2, value >> 16);
}

static void make_wave(void)
{
    memcpy(wave, "RIFF", 4);
    le32(wave + 4, sizeof(wave) - 8u);
    memcpy(wave + 8, "WAVEfmt ", 8);
    le32(wave + 16, 16u);
    le16(wave + 20, 1u);
    le16(wave + 22, 2u);
    le32(wave + 24, 48000u);
    le32(wave + 28, 192000u);
    le16(wave + 32, 4u);
    le16(wave + 34, 16u);
    memcpy(wave + 36, "data", 4);
    le32(wave + 40, sizeof(wave) - 44u);
}

static int check_music(const char *path, Mix_MusicType type)
{
    Mix_Music *music = Mix_LoadMUS(path);
    int valid;

    (void)astra_log("SDL_MIXER_LOADED");
    valid = music != NULL && Mix_GetMusicType(music) == type;
    if (valid)
        valid = Mix_PlayMusic(music, 0) == 0;

    (void)astra_log("SDL_MIXER_PLAYED");

    if (!valid)
        (void)astra_log(SDL_GetError());
    if (music != NULL) {
        Mix_HaltMusic();
        (void)astra_log("SDL_MIXER_HALTED");
        Mix_FreeMusic(music);
        (void)astra_log("SDL_MIXER_FREED");
    }
    return valid;
}

int main(void)
{
    static const char invalid[] = "not a wave file";
    Mix_Chunk *chunk = NULL;
    SDL_RWops *input;
    int result = 1;

    SDL_SetAssertionHandler(assertion_failed, NULL);
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0 ||
        SDL_Init(SDL_INIT_AUDIO) != 0) {
        (void)astra_log("SDL_MIXER_INIT_FAIL");
        return 1;
    }
    if ((Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC) &
         (MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC)) !=
        (MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC) ||
        Mix_OpenAudio(48000, AUDIO_S16MSB, 2, 1024) != 0) {
        (void)astra_log(SDL_GetError());
        (void)astra_log("SDL_MIXER_OPEN_FAIL");
        goto done;
    }
    input = SDL_RWFromConstMem(invalid, sizeof(invalid) - 1u);
    if (input == NULL || (chunk = Mix_LoadWAV_RW(input, 1)) != NULL) {
        (void)astra_log("SDL_MIXER_BAD_WAV_FAIL");
        goto done;
    }
    input = SDL_RWFromConstMem(invalid, sizeof(invalid) - 1u);
    if (input == NULL || Mix_LoadMUS_RW(input, 1) != NULL) {
        (void)astra_log("SDL_MIXER_BAD_MUSIC_FAIL");
        goto done;
    }
    make_wave();
    input = SDL_RWFromConstMem(wave, sizeof(wave));
    if (input == NULL || (chunk = Mix_LoadWAV_RW(input, 1)) == NULL) {
        (void)astra_log(SDL_GetError());
        (void)astra_log("SDL_MIXER_WAV_FAIL");
        goto done;
    }
    if (Mix_AllocateChannels(16) < 16) {
        (void)astra_log("SDL_MIXER_CHANNELS_FAIL");
        goto done;
    }
    for (int index = 0; index < 16; ++index) {
        if (Mix_PlayChannel(index, chunk, 0) != index) {
            (void)astra_log("SDL_MIXER_PLAY_FAIL");
            goto done;
        }
    }
    if (Mix_Playing(-1) != 16 || Mix_HaltChannel(-1) != 0 ||
        Mix_Playing(-1) != 0) {
        (void)astra_log("SDL_MIXER_STATE_FAIL");
        goto done;
    }
    (void)astra_log("SDL_MIXER_OGG_START");
    if (!check_music("/apps/SDLMixerProbe.app/resources/sample.ogg", MUS_OGG)) {
        (void)astra_log("SDL_MIXER_MUSIC_FAIL");
        goto done;
    }
    (void)astra_log("SDL_MIXER_MP3_START");
    if (!check_music("/apps/SDLMixerProbe.app/resources/sample.mp3", MUS_MP3)) {
        (void)astra_log("SDL_MIXER_MUSIC_FAIL");
        goto done;
    }
    (void)astra_log("SDL_MIXER_FLAC_START");
    if (!check_music("/apps/SDLMixerProbe.app/resources/sample.flac", MUS_FLAC)) {
        (void)astra_log("SDL_MIXER_MUSIC_FAIL");
        goto done;
    }
    (void)astra_log("SDL_MIXER_READY");
    result = 0;
done:
    if (chunk != NULL)
        Mix_FreeChunk(chunk);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    return result;
}
