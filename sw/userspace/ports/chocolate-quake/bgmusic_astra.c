/* Chocolate Quake music adapter for Astra's bundled SDL_mixer codecs. */
#include "bgmusic.h"
#include "client.h"
#include "cmd.h"
#include "console.h"
#include "sound.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>

static const struct {
    const char *extension;
    Mix_MusicType type;
} formats[] = {
    {"ogg", MUS_OGG}, {"mp3", MUS_MP3},
    {"flac", MUS_FLAC}, {"wav", MUS_WAV}
};

static Mix_Music *music;
static byte remap[100];
static byte current_track;
static qboolean initialized;
static qboolean enabled = true;
static qboolean playing;
static qboolean paused;
static qboolean looping;
static float cdvolume = -1;

void BGMusic_Stop(void) {
    if (music) {
        Mix_HaltMusic();
        Mix_FreeMusic(music);
        music = NULL;
    }
    playing = false;
    paused = false;
}

void BGMusic_Pause(void) {
    if (playing && music) {
        Mix_PauseMusic();
        playing = false;
        paused = true;
    }
}

void BGMusic_Resume(void) {
    if (paused && music && enabled) {
        Mix_ResumeMusic();
        paused = false;
        playing = true;
    }
}

void BGMusic_Play(byte track, qboolean loop) {
    if (!initialized || !enabled || track >= sizeof(remap)) {
        return;
    }
    track = remap[track];
    if (track == 0 || track >= sizeof(remap)) {
        return;
    }
    if (music) {
        if (current_track == track && (playing || paused)) {
            return;
        }
        BGMusic_Stop();
    }

    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        char name[MAX_QPATH];
        FILE *file = NULL;
        snprintf(name, sizeof(name), "music/track%02u.%s", track,
                 formats[i].extension);
        if (COM_FindMusicTrack(name, &file) < 0 || !file) {
            continue;
        }
        SDL_RWops *rw = SDL_RWFromFP(file, SDL_TRUE);
        if (!rw) {
            fclose(file);
            Con_Printf("Music stream error: %s\n", SDL_GetError());
            return;
        }
        music = Mix_LoadMUSType_RW(rw, formats[i].type, 1);
        if (!music) {
            Con_Printf("Cannot decode %s: %s\n", name, Mix_GetError());
            continue;
        }
        if (Mix_PlayMusic(music, loop ? -1 : 0) < 0) {
            Con_Printf("Cannot play %s: %s\n", name, Mix_GetError());
            Mix_FreeMusic(music);
            music = NULL;
            continue;
        }
        current_track = track;
        looping = loop;
        playing = true;
        if (cdvolume == 0) {
            BGMusic_Pause();
        }
        return;
    }
    Con_Printf("Couldn't find a playable music track %u\n", track);
}

static void CD_f(void) {
    if (Cmd_Argc() < 2) {
        return;
    }
    const char *command = Cmd_Argv(1);
    if (!Q_strcasecmp(command, "on")) {
        enabled = true;
    } else if (!Q_strcasecmp(command, "off")) {
        BGMusic_Stop();
        enabled = false;
    } else if (!Q_strcasecmp(command, "reset")) {
        BGMusic_Stop();
        enabled = true;
        for (size_t i = 0; i < sizeof(remap); ++i) {
            remap[i] = (byte)i;
        }
    } else if (!Q_strcasecmp(command, "remap")) {
        if (Cmd_Argc() == 2) {
            for (size_t i = 1; i < sizeof(remap); ++i) {
                if (remap[i] != i) {
                    Con_Printf("  %u -> %u\n", (unsigned)i, remap[i]);
                }
            }
        } else {
            for (int i = 2; i < Cmd_Argc() && i < (int)sizeof(remap) + 1;
                 ++i) {
                int mapped = Q_atoi(Cmd_Argv(i));
                if (mapped >= 0 && mapped < (int)sizeof(remap)) {
                    remap[i - 1] = (byte)mapped;
                }
            }
        }
    } else if (!Q_strcasecmp(command, "play")) {
        BGMusic_Play((byte)Q_atoi(Cmd_Argv(2)), false);
    } else if (!Q_strcasecmp(command, "loop")) {
        BGMusic_Play((byte)Q_atoi(Cmd_Argv(2)), true);
    } else if (!Q_strcasecmp(command, "stop")) {
        BGMusic_Stop();
    } else if (!Q_strcasecmp(command, "pause")) {
        BGMusic_Pause();
    } else if (!Q_strcasecmp(command, "resume")) {
        BGMusic_Resume();
    } else if (!Q_strcasecmp(command, "info")) {
        if (music) {
            Con_Printf("%s track %u%s\n", paused ? "Paused" : "Playing",
                       current_track, looping ? " looping" : "");
        }
        Con_Printf("Volume is %f\n", cdvolume);
    }
}

i32 BGMusic_Init(void) {
    if (cls.state == ca_dedicated || COM_CheckParm("-nocdaudio")) {
        return -1;
    }
    const int codecs = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC;
    if ((Mix_Init(codecs) & codecs) != codecs) {
        Con_Printf("Music codecs unavailable: %s\n", Mix_GetError());
        Mix_Quit();
        return -1;
    }
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) < 0) {
        Con_Printf("Music audio unavailable: %s\n", Mix_GetError());
        Mix_Quit();
        return -1;
    }
    Mix_AllocateChannels(0);
    for (size_t i = 0; i < sizeof(remap); ++i) {
        remap[i] = (byte)i;
    }
    initialized = true;
    Cmd_AddCommand("cd", CD_f);
    Con_Printf("CD Audio Initialized (MP3, Ogg Vorbis, FLAC, WAV)\n");
    return 0;
}

void BGMusic_Shutdown(void) {
    if (initialized) {
        BGMusic_Stop();
        Mix_CloseAudio();
        Mix_Quit();
        initialized = false;
    }
}

void BGMusic_Update(void) {
    if (!initialized || !enabled) {
        return;
    }
    float volume = SDL_clamp(bgmvolume.value, 0, 1);
    if (volume != cdvolume) {
        int mix_volume = (int)(volume * MIX_MAX_VOLUME + 0.5f);
        if (volume != bgmvolume.value) {
            Cvar_SetValue("bgmvolume", volume);
        }
        cdvolume = volume;
        Mix_VolumeMusic(mix_volume);
        if (mix_volume == 0) {
            BGMusic_Pause();
        } else {
            BGMusic_Resume();
        }
    }
    if (playing && music && !Mix_PlayingMusic()) {
        Con_Printf("Music stopped: track %u: %s\n", current_track,
                   Mix_GetError());
        BGMusic_Stop();
    }
}
