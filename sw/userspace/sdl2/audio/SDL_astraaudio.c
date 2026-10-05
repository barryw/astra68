/* Astra SDL2 audio backend.  This file is deliberately outside upstream SDL. */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_ASTRA

#include "SDL_audio.h"
#include "SDL_audio_c.h"
#include "SDL_sysaudio.h"

#include <astra/audio_stream.h>
#include <astra/pcm.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#define _THIS SDL_AudioDevice *_this

/* Buffers SDL fills in place (Haiku's sizing: max(3, latency/period+2)). */
#define ASTRAAUDIO_BUFFERS 3u
/* A host that takes nothing for this long is gone. */
#define ASTRAAUDIO_WAIT_NS UINT64_C(1000000000)

/* With an audio stream (pcm.library 2.5) SDL mixes straight into buffers
 * the host reads, and each costs one wait; without one, the media service's
 * voice and a copy. */
struct SDL_PrivateAudioData {
    AstraPcmBuffers buffers;
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

/* The PCM format word for an SDL spec, or zero when the host cannot take
 * it as is. */
static uint32_t ASTRAAUDIO_Format(SDL_AudioFormat format, Uint8 channels,
                                  int rate)
{
    uint32_t encoding = ASTRAAUDIO_Encoding(format);

    if (encoding == 0u || channels == 0u ||
        channels > ASTRA_PCM_CHANNELS_MAX ||
        rate < (int)ASTRA_PCM_RATE_MIN || rate > (int)ASTRA_PCM_RATE_MAX)
        return 0u;
    return ASTRA_PCM_FORMAT(encoding, channels, (uint32_t)rate);
}

/* SDL_ConvertAudio's one filter when the host converts: the source and
 * target format words ride in the two slots SDL's own resampler uses for
 * its rates. */
static void SDLCALL ASTRAAUDIO_HostConvert(SDL_AudioCVT *cvt,
                                          SDL_AudioFormat format)
{
    const AstraStartupCapability *capability = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_PCM);
    uint32_t source = (uint32_t)(uintptr_t)
        cvt->filters[SDL_AUDIOCVT_MAX_FILTERS - 1];
    uint32_t target = (uint32_t)(uintptr_t)
        cvt->filters[SDL_AUDIOCVT_MAX_FILTERS];
    uint32_t source_bytes = astra_pcm_format_frame_bytes(source);
    uint32_t target_bytes = astra_pcm_format_frame_bytes(target);
    uint32_t frames = (uint32_t)cvt->len_cvt / source_bytes;
    uint32_t capacity = (uint32_t)(cvt->len * cvt->len_mult) / target_bytes;
    uint32_t produced = 0u;
    void *copy = SDL_malloc(frames == 0u ? 1u : frames * source_bytes);

    (void)format;
    if (copy == NULL || capability == NULL) {
        SDL_OutOfMemory();
    } else {
        SDL_memcpy(copy, cvt->buf, frames * source_bytes);
        if (astra_pcm_convert(capability->handle, source, copy, frames,
                              target, cvt->buf, capacity, &produced) !=
            ASTRA_OK) {
            /* Audio is gone with the media service; keep the length the
             * caller was promised, silent. */
            produced = (uint32_t)((uint64_t)frames *
                                  astra_pcm_format_rate(target) /
                                  astra_pcm_format_rate(source));
            SDL_memset(cvt->buf, SDL_SilenceValueForFormat(cvt->dst_format),
                       produced * target_bytes);
            SDL_SetError("Astra host audio conversion failed");
        }
    }
    SDL_free(copy);
    cvt->len_cvt = (int)(produced * target_bytes);
    if (cvt->filters[++cvt->filter_index] != NULL)
        cvt->filters[cvt->filter_index](cvt, cvt->dst_format);
}

/* Called by SDL_BuildAudioCVT before it builds its own float chain: when
 * the host can take both specs, the whole conversion is one host request
 * (decode, resample, encode) and the MC68040 only moves bytes. Returns 1
 * with @p cvt set up, or 0 to let SDL convert. */
int ASTRAAUDIO_BuildHostCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format,
                            Uint8 src_channels, int src_rate,
                            SDL_AudioFormat dst_format, Uint8 dst_channels,
                            int dst_rate)
{
    uint32_t source = ASTRAAUDIO_Format(src_format, src_channels, src_rate);
    uint32_t target = ASTRAAUDIO_Format(dst_format, dst_channels, dst_rate);
    double ratio;

    if (source == 0u || target == 0u ||
        astra_startup_capability(astra_posix_startup(),
                                 ASTRA_CAPABILITY_PCM) == NULL)
        return 0;
    cvt->filters[0] = ASTRAAUDIO_HostConvert;
    cvt->filter_index = 1;
    cvt->filters[SDL_AUDIOCVT_MAX_FILTERS - 1] =
        (SDL_AudioFilter)(uintptr_t)source;
    cvt->filters[SDL_AUDIOCVT_MAX_FILTERS] =
        (SDL_AudioFilter)(uintptr_t)target;
    ratio = (double)dst_rate / src_rate *
            astra_pcm_format_frame_bytes(target) /
            astra_pcm_format_frame_bytes(source);
    cvt->len_ratio = ratio;
    cvt->len_mult = ratio > 1.0 ? (int)SDL_ceil(ratio) : 1;
    cvt->needed = 1;
    return 1;
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
    if (_this->spec.samples < ASTRA_AUDIO_STREAM_PERIOD_MIN)
        _this->spec.samples = ASTRA_AUDIO_STREAM_PERIOD_MIN;
    if (_this->spec.samples > ASTRA_AUDIO_STREAM_PERIOD_MAX)
        _this->spec.samples = ASTRA_AUDIO_STREAM_PERIOD_MAX;
    SDL_CalculateAudioSpec(&_this->spec);
    hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*hidden));
    if (hidden == NULL)
        return SDL_OutOfMemory();
    hidden->frame_bytes = astra_pcm_format_frame_bytes(format);
    hidden->buffers = (AstraPcmBuffers)ASTRA_PCM_BUFFERS_INIT;
    {
        AstraResult opened = astra_pcm_buffers_open(
            capability->handle, format, _this->spec.samples,
            ASTRAAUDIO_BUFFERS, &hidden->buffers);

        if (opened == ASTRA_OK) {
            _this->hidden = hidden;
            return 0;
        }
        /* A host with streams that refuses one is a fault, not a choice:
         * say so before falling back to the media service's voice. */
        if (opened != ASTRA_ERROR_UNSUPPORTED)
            (void)astra_log_failure("SDL audio stream", (uint32_t)-opened);
    }
    hidden->mixbuf = (Uint8 *)SDL_malloc(_this->spec.size);
    if (hidden->mixbuf == NULL) {
        SDL_free(hidden);
        return SDL_OutOfMemory();
    }
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

    if (_this->hidden->buffers.stream != 0u) {
        if (!SDL_AtomicGet(&_this->shutdown) &&
            astra_pcm_buffers_wait(&_this->hidden->buffers,
                                   astra_clock_monotonic() +
                                       ASTRAAUDIO_WAIT_NS) != ASTRA_OK)
            SDL_OpenedAudioDeviceDisconnected(_this);
        return;
    }
    if (!SDL_AtomicGet(&_this->shutdown) &&
        astra_pcm_wait(&_this->hidden->stream,
                       ASTRAAUDIO_Room(_this, frames)) != ASTRA_OK)
        SDL_OpenedAudioDeviceDisconnected(_this);
}

static Uint8 *ASTRAAUDIO_GetDeviceBuf(_THIS)
{
    if (_this->hidden->buffers.stream != 0u)
        return (Uint8 *)astra_pcm_buffers_get(&_this->hidden->buffers);
    return _this->hidden->mixbuf;
}

static void ASTRAAUDIO_PlayDevice(_THIS)
{
    const uint32_t frame_bytes = _this->hidden->frame_bytes;
    const uint32_t frames = _this->spec.size / frame_bytes;
    uint32_t sent = 0u;

    if (_this->hidden->buffers.stream != 0u) {
        if (astra_pcm_buffers_queue(&_this->hidden->buffers) != ASTRA_OK)
            SDL_OpenedAudioDeviceDisconnected(_this);
        return;
    }

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
        AstraResult result = _this->hidden->buffers.stream != 0u ?
            astra_pcm_buffers_close(&_this->hidden->buffers) :
            astra_pcm_close(&_this->hidden->stream);

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
