/* Astra SDL2 audio backend.  This file is deliberately outside upstream SDL. */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_ASTRA

#include "SDL_audio.h"
#include "SDL_audio_c.h"
#include "SDL_sysaudio.h"

#include <astra/pcm.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData {
    AstraPcmStream stream;
    Uint8 *mixbuf;
};

static int ASTRAAUDIO_OpenDevice(_THIS, const char *devname)
{
    const AstraStartupCapability *capability;
    struct SDL_PrivateAudioData *hidden;

    (void)devname;
    if (_this->iscapture)
        return SDL_SetError("Astra PCM capture is not available");
    capability = astra_startup_capability(astra_posix_startup(),
                                           ASTRA_CAPABILITY_PCM);
    if (capability == NULL)
        return SDL_SetError("PCM capability was not granted to this app");

    _this->spec.freq = ASTRA_PCM_RATE;
    _this->spec.channels = ASTRA_PCM_CHANNELS;
    _this->spec.format = AUDIO_S16MSB;
    SDL_CalculateAudioSpec(&_this->spec);
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL)
        return SDL_OutOfMemory();
    hidden->mixbuf = (Uint8 *)SDL_malloc(_this->spec.size);
    if (hidden->mixbuf == NULL) {
        SDL_free(hidden);
        return SDL_OutOfMemory();
    }
    hidden->stream = (AstraPcmStream)ASTRA_PCM_STREAM_INIT;
    if (astra_pcm_open(capability->handle, ASTRA_PCM_FORMAT_S16BE_STEREO,
                       &hidden->stream) != ASTRA_OK) {
        SDL_free(hidden->mixbuf);
        SDL_free(hidden);
        return SDL_SetError("Astra PCM stream could not be opened");
    }
    _this->hidden = hidden;
    return 0;
}

static void ASTRAAUDIO_WaitDevice(_THIS)
{
    AstraPcmStatus status;

    while (!SDL_AtomicGet(&_this->shutdown)) {
        if (astra_pcm_status(&_this->hidden->stream, &status) != ASTRA_OK) {
            SDL_OpenedAudioDeviceDisconnected(_this);
            return;
        }
        if (status.queued_frames <= _this->spec.samples)
            return;
        (void)astra_rt_thread_sleep(UINT64_C(2000000),
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
}

static Uint8 *ASTRAAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->mixbuf;
}

static void ASTRAAUDIO_PlayDevice(_THIS)
{
    const uint32_t frames = _this->spec.size / 4u;
    uint32_t sent = 0u;

    while (sent < frames && !SDL_AtomicGet(&_this->shutdown)) {
        uint32_t accepted = 0u;
        AstraResult result = astra_pcm_write(&_this->hidden->stream,
                                             _this->hidden->mixbuf + sent * 4u,
                                             frames - sent, &accepted);

        sent += accepted;
        if (result == ASTRA_OK)
            continue;
        if (result != ASTRA_ERROR_BUSY) {
            SDL_OpenedAudioDeviceDisconnected(_this);
            return;
        }
        (void)astra_rt_thread_sleep(UINT64_C(2000000),
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    }
}

static void ASTRAAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        AstraResult result = astra_pcm_close(&_this->hidden->stream);

        (void)result;
        SDL_free(_this->hidden->mixbuf);
        SDL_free(_this->hidden);
        _this->hidden = NULL;
    }
}

static SDL_bool ASTRAAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = ASTRAAUDIO_OpenDevice;
    impl->WaitDevice = ASTRAAUDIO_WaitDevice;
    impl->GetDeviceBuf = ASTRAAUDIO_GetDeviceBuf;
    impl->PlayDevice = ASTRAAUDIO_PlayDevice;
    impl->CloseDevice = ASTRAAUDIO_CloseDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->SupportsNonPow2Samples = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap ASTRAAUDIO_bootstrap = {
    "astra", "Astra PCM audio", ASTRAAUDIO_Init, SDL_FALSE
};

#endif
