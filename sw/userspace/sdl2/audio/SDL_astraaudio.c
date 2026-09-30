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
    uint32_t frame_bytes;
};

/* The Linux audio host converts and resamples every stream, so the device
 * takes the application's own sample format, channels and rate, and SDL
 * converts nothing on the MC68040. */
static uint32_t ASTRAAUDIO_Encoding(SDL_AudioFormat format)
{
    switch (format) {
    case AUDIO_U8: return ASTRA_PCM_ENCODING_U8;
    case AUDIO_S8: return ASTRA_PCM_ENCODING_S8;
    case AUDIO_U16LSB: return ASTRA_PCM_ENCODING_U16LE;
    case AUDIO_S16LSB: return ASTRA_PCM_ENCODING_S16LE;
    case AUDIO_U16MSB: return ASTRA_PCM_ENCODING_U16BE;
    case AUDIO_S16MSB: return ASTRA_PCM_ENCODING_S16BE;
    case AUDIO_S32LSB: return ASTRA_PCM_ENCODING_S32LE;
    case AUDIO_S32MSB: return ASTRA_PCM_ENCODING_S32BE;
    case AUDIO_F32LSB: return ASTRA_PCM_ENCODING_F32LE;
    case AUDIO_F32MSB: return ASTRA_PCM_ENCODING_F32BE;
    default: return 0u;
    }
}

static int ASTRAAUDIO_OpenDevice(_THIS, const char *devname)
{
    const AstraStartupCapability *capability;
    struct SDL_PrivateAudioData *hidden;
    uint32_t format;

    (void)devname;
    if (_this->iscapture)
        return SDL_SetError("Astra PCM capture is not available");
    capability = astra_startup_capability(astra_posix_startup(),
                                           ASTRA_CAPABILITY_PCM);
    if (capability == NULL)
        return SDL_SetError("PCM capability was not granted to this app");

    /* Anything the host cannot take SDL converts to the nearest thing it
     * can: more than two channels to stereo, an odd rate to the sink's. */
    if (ASTRAAUDIO_Encoding(_this->spec.format) == 0u)
        _this->spec.format = AUDIO_S16MSB;
    if (_this->spec.channels > ASTRA_PCM_CHANNELS_MAX)
        _this->spec.channels = ASTRA_PCM_CHANNELS_MAX;
    if (_this->spec.freq < (int)ASTRA_PCM_RATE_MIN ||
        _this->spec.freq > (int)ASTRA_PCM_RATE_MAX)
        _this->spec.freq = ASTRA_PCM_RATE;
    format = ASTRA_PCM_FORMAT(ASTRAAUDIO_Encoding(_this->spec.format),
                              _this->spec.channels,
                              (uint32_t)_this->spec.freq);
    SDL_CalculateAudioSpec(&_this->spec);
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL)
        return SDL_OutOfMemory();
    hidden->mixbuf = (Uint8 *)SDL_malloc(_this->spec.size);
    if (hidden->mixbuf == NULL) {
        SDL_free(hidden);
        return SDL_OutOfMemory();
    }
    hidden->frame_bytes = astra_pcm_format_frame_bytes(format);
    hidden->stream = (AstraPcmStream)ASTRA_PCM_STREAM_INIT;
    if (astra_pcm_open(capability->handle, format,
                       &hidden->stream) != ASTRA_OK) {
        SDL_free(hidden->mixbuf);
        SDL_free(hidden);
        return SDL_SetError("Astra PCM stream could not be opened");
    }
    _this->hidden = hidden;
    return 0;
}

/* Room worth waking for: a whole buffer, or half the host queue when the
 * buffer is larger, so the queue never runs down to empty before the next
 * write. */
static uint32_t ASTRAAUDIO_Room(_THIS, uint32_t frames)
{
    const uint32_t half = ASTRA_PCM_QUEUE_FRAMES / 2u;

    (void)_this;
    return frames < half ? frames : half;
}

static void ASTRAAUDIO_WaitDevice(_THIS)
{
    const uint32_t frames = _this->spec.size / _this->hidden->frame_bytes;

    if (!SDL_AtomicGet(&_this->shutdown) &&
        astra_pcm_wait(&_this->hidden->stream,
                       ASTRAAUDIO_Room(_this, frames)) != ASTRA_OK)
        SDL_OpenedAudioDeviceDisconnected(_this);
}

static Uint8 *ASTRAAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->mixbuf;
}

static void ASTRAAUDIO_PlayDevice(_THIS)
{
    const uint32_t frame_bytes = _this->hidden->frame_bytes;
    const uint32_t frames = _this->spec.size / frame_bytes;
    uint32_t sent = 0u;

    while (sent < frames && !SDL_AtomicGet(&_this->shutdown)) {
        uint32_t accepted = 0u;
        AstraResult result = astra_pcm_write(&_this->hidden->stream,
                                             _this->hidden->mixbuf +
                                                 sent * frame_bytes,
                                             frames - sent, &accepted);

        sent += accepted;
        if (result == ASTRA_OK)
            continue;
        if (result != ASTRA_ERROR_BUSY ||
            astra_pcm_wait(&_this->hidden->stream,
                           ASTRAAUDIO_Room(_this, frames - sent)) !=
                ASTRA_OK) {
            SDL_OpenedAudioDeviceDisconnected(_this);
            return;
        }
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
