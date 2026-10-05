#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SDL_AUDIO_DRIVER_ASTRA 1
#include "../../audio/SDL_astraaudio.c"

static AstraStartupCapability pcm_capability;
static int has_pcm;
static int open_result;
static int write_busy_once;
static int status_failure;
static int disconnected;
static uint32_t received_frames;
static uint32_t wait_count;
static uint32_t wait_frames;
static uint32_t opened_format;

void *SDL_calloc(size_t count, size_t size) { return calloc(count, size); }
void *SDL_malloc(size_t size) { return malloc(size); }
void *SDL_memcpy(void *to, const void *from, size_t size)
{
    return memcpy(to, from, size);
}
void *SDL_memset(void *to, int value, size_t size)
{
    return memset(to, value, size);
}
double SDL_ceil(double value)
{
    double whole = (double)(long long)value;

    return whole < value ? whole + 1.0 : whole;
}
Uint8 SDL_SilenceValueForFormat(const SDL_AudioFormat format)
{
    return format == AUDIO_U8 ? 0x80u : 0u;
}
void SDL_free(void *memory) { free(memory); }
int SDL_Error(SDL_errorcode code) { (void)code; return -1; }
int SDL_SetError(const char *format, ...) { (void)format; return -1; }
int SDL_AtomicGet(SDL_atomic_t *value) { return value->value; }
void SDL_OpenedAudioDeviceDisconnected(SDL_AudioDevice *device)
{
    (void)device;
    ++disconnected;
}
void SDL_CalculateAudioSpec(SDL_AudioSpec *spec)
{
    spec->size = (Uint32)spec->samples * spec->channels *
                 (SDL_AUDIO_BITSIZE(spec->format) / 8u);
    spec->silence = 0u;
}
const AstraStartupInfo *astra_posix_startup(void) { return NULL; }
const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *startup, const char *name)
{
    (void)startup;
    assert(strcmp(name, ASTRA_CAPABILITY_PCM) == 0);
    return has_pcm ? &pcm_capability : NULL;
}
AstraResult astra_pcm_open(AstraHandle service, uint32_t format,
                           AstraPcmStream *stream)
{
    assert(service == 42u);
    opened_format = format;
    if (open_result != 0)
        return ASTRA_ERROR_IO;
    stream->control = 1u;
    return ASTRA_OK;
}
AstraResult astra_pcm_status(AstraPcmStream *stream, AstraPcmStatus *status)
{
    assert(stream->control == 1u);
    if (status_failure)
        return ASTRA_ERROR_IO;
    memset(status, 0, sizeof(*status));
    return ASTRA_OK;
}
AstraResult astra_pcm_write(AstraPcmStream *stream, const void *frames,
                            uint32_t count, uint32_t *accepted)
{
    assert(stream->control == 1u);
    assert(frames != NULL);
    if (write_busy_once) {
        write_busy_once = 0;
        *accepted = 1u;
        received_frames += *accepted;
        return ASTRA_ERROR_BUSY;
    }
    *accepted = count;
    received_frames += count;
    return ASTRA_OK;
}
AstraResult astra_pcm_close(AstraPcmStream *stream)
{
    assert(stream->control == 1u);
    stream->control = 0u;
    return ASTRA_OK;
}
static int convert_failure;
static uint32_t converted_source, converted_target, converted_frames;
/* The fake host turns U8 mono into S16 mono at twice the rate. */
AstraResult astra_pcm_convert(AstraHandle service, uint32_t source_format,
                              const void *source, uint32_t source_frames,
                              uint32_t target_format, void *target,
                              uint32_t target_capacity,
                              uint32_t *target_frames)
{
    const uint8_t *from = source;
    uint8_t *to = target;

    assert(service == 42u);
    converted_source = source_format;
    converted_target = target_format;
    converted_frames = source_frames;
    if (convert_failure)
        return ASTRA_ERROR_PEER_DEAD;
    assert(target_capacity >= 2u * source_frames);
    for (uint32_t i = 0u; i < source_frames; ++i)
        for (uint32_t copy = 0u; copy < 2u; ++copy) {
            to[4u * i + 2u * copy] = (uint8_t)(from[i] - 0x80u);
            to[4u * i + 2u * copy + 1u] = 0u;
        }
    *target_frames = 2u * source_frames;
    return ASTRA_OK;
}
/* The buffer group: a host with audio streams when has_streams is set. */
static int has_streams;
static int stream_wait_failure;
static uint8_t group_memory[4u * 4096u];
static uint32_t group_period, group_count, group_format;
static uint32_t group_queued, group_waits, group_closes;
static uint64_t group_deadline;

uint64_t astra_clock_monotonic(void) { return 1000u; }
static uint32_t logged_failures;
uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    (void)status;
    ++logged_failures;
    return ASTRA_SYSCALL_OK;
}
AstraResult astra_pcm_buffers_open(AstraHandle service, uint32_t format,
                                   uint32_t period_frames, uint32_t count,
                                   AstraPcmBuffers *buffers)
{
    assert(service == 42u && buffers->stream == 0u);
    if (!has_streams)
        return ASTRA_ERROR_UNSUPPORTED;
    group_format = format;
    group_period = period_frames;
    group_count = count;
    buffers->stream = 7u;
    buffers->first = group_memory;
    buffers->count = count;
    return ASTRA_OK;
}
void *astra_pcm_buffers_get(AstraPcmBuffers *buffers)
{
    assert(buffers->stream == 7u);
    return group_memory + (group_queued % group_count) * group_period;
}
AstraResult astra_pcm_buffers_queue(AstraPcmBuffers *buffers)
{
    assert(buffers->stream == 7u);
    ++group_queued;
    return ASTRA_OK;
}
AstraResult astra_pcm_buffers_wait(AstraPcmBuffers *buffers,
                                   uint64_t deadline_ns)
{
    assert(buffers->stream == 7u);
    ++group_waits;
    group_deadline = deadline_ns;
    return stream_wait_failure ? ASTRA_ERROR_TIMEOUT : ASTRA_OK;
}
AstraResult astra_pcm_buffers_close(AstraPcmBuffers *buffers)
{
    assert(buffers->stream == 7u);
    buffers->stream = 0u;
    ++group_closes;
    return ASTRA_OK;
}

AstraResult astra_pcm_wait(AstraPcmStream *stream, uint32_t frame_count)
{
    assert(stream->control == 1u);
    ++wait_count;
    wait_frames = frame_count;
    return status_failure ? ASTRA_ERROR_IO : ASTRA_OK;
}

int main(void)
{
    SDL_AudioDevice device;
    SDL_AudioDriverImpl driver = {0};

    memset(&device, 0, sizeof(device));
    pcm_capability.handle = 42u;
    device.spec.freq = 22050;
    device.spec.channels = 1u;
    device.spec.format = AUDIO_U8;
    device.spec.samples = 256u;
    assert(ASTRAAUDIO_Init(&driver) == SDL_TRUE);
    assert(driver.OnlyHasDefaultOutputDevice == SDL_TRUE);
    assert(driver.HasCaptureSupport == SDL_FALSE);
    device.iscapture = SDL_TRUE;
    assert(driver.OpenDevice(&device, NULL) == -1); /* no capture backend */
    device.iscapture = SDL_FALSE;
    assert(driver.OpenDevice(&device, NULL) == -1); /* no grant */
    assert(device.hidden == NULL);
    has_pcm = 1;
    open_result = 1;
    assert(driver.OpenDevice(&device, NULL) == -1); /* service failure */
    assert(device.hidden == NULL);
    open_result = 0;
    /* The host converts: the device keeps the application's spec. */
    assert(driver.OpenDevice(&device, NULL) == 0);
    assert(device.spec.freq == 22050);
    assert(device.spec.channels == 1u);
    assert(device.spec.format == AUDIO_U8);
    assert(device.spec.size == 256u);
    assert(opened_format ==
           ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_U8, 1u, 22050u));
    assert(driver.GetDeviceBuf(&device) != NULL);
    write_busy_once = 1;
    driver.PlayDevice(&device);
    assert(received_frames == 256u);
    /* A full queue waits for room for the rest, rather than polling. */
    assert(wait_count == 1u && wait_frames == 255u);
    assert(disconnected == 0);
    driver.WaitDevice(&device);
    assert(wait_count == 2u && wait_frames == 256u && disconnected == 0);
    status_failure = 1;
    driver.WaitDevice(&device);
    assert(disconnected == 1); /* failed provider does not masquerade as audio */
    driver.CloseDevice(&device);
    assert(device.hidden == NULL);

    /* What the host cannot take, SDL converts: 5.1 to stereo, and a rate
     * outside the host's range to the sink's. */
    status_failure = 0;
    device.spec.freq = 4000;
    device.spec.channels = 6u;
    device.spec.format = AUDIO_F32MSB;
    device.spec.samples = 256u;
    assert(driver.OpenDevice(&device, NULL) == 0);
    assert(device.spec.freq == (int)ASTRA_PCM_RATE);
    assert(device.spec.channels == 2u);
    assert(device.spec.format == AUDIO_F32MSB);
    assert(device.spec.size == 2048u);
    assert(opened_format == ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_F32BE, 2u,
                                             ASTRA_PCM_RATE));
    received_frames = 0u;
    driver.PlayDevice(&device);
    assert(received_frames == 256u);
    driver.CloseDevice(&device);

    /* With audio streams SDL mixes into the host's buffers in place: no
     * copy, no media service, one wait per buffer. */
    has_streams = 1;
    disconnected = 0;
    received_frames = 0u;
    device.spec.freq = 22050;
    device.spec.channels = 1u;
    device.spec.format = AUDIO_U8;
    device.spec.samples = 16u; /* below the stream minimum */
    assert(driver.OpenDevice(&device, NULL) == 0);
    assert(device.spec.samples == ASTRA_AUDIO_STREAM_PERIOD_MIN);
    assert(group_period == ASTRA_AUDIO_STREAM_PERIOD_MIN &&
           group_count == ASTRAAUDIO_BUFFERS &&
           group_format == ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_U8, 1u,
                                            22050u));
    assert(device.hidden->mixbuf == NULL);
    assert(driver.GetDeviceBuf(&device) == group_memory);
    driver.PlayDevice(&device);
    assert(group_queued == 1u && received_frames == 0u);
    assert(driver.GetDeviceBuf(&device) ==
           group_memory + ASTRA_AUDIO_STREAM_PERIOD_MIN);
    driver.WaitDevice(&device);
    assert(group_waits == 1u &&
           group_deadline == 1000u + ASTRAAUDIO_WAIT_NS && disconnected == 0);
    stream_wait_failure = 1;
    driver.WaitDevice(&device);
    assert(disconnected == 1); /* a host that takes nothing is gone */
    driver.CloseDevice(&device);
    assert(group_closes == 1u && device.hidden == NULL);
    has_streams = 0;

    /* SDL_BuildAudioCVT asks the host first: one filter, the format words
     * where SDL keeps its resampler's rates, the length ratio exact. */
    {
        SDL_AudioCVT cvt;
        uint8_t buffer[64];

        memset(&cvt, 0, sizeof(cvt));
        has_pcm = 0;
        assert(ASTRAAUDIO_BuildHostCVT(&cvt, AUDIO_U8, 1u, 11025, AUDIO_S16MSB,
                                       1u, 22050) == 0);
        has_pcm = 1;
        assert(ASTRAAUDIO_BuildHostCVT(&cvt, AUDIO_U8, 6u, 11025,
                                       AUDIO_S16MSB, 1u, 22050) == 0);
        assert(ASTRAAUDIO_BuildHostCVT(&cvt, AUDIO_U8, 1u, 4000,
                                       AUDIO_S16MSB, 1u, 22050) == 0);
        assert(cvt.filters[0] == NULL);
        assert(ASTRAAUDIO_BuildHostCVT(&cvt, AUDIO_U8, 1u, 11025,
                                       AUDIO_S16MSB, 1u, 22050) == 1);
        assert(cvt.needed == 1 && cvt.filters[0] == ASTRAAUDIO_HostConvert &&
               cvt.filters[1] == NULL && cvt.len_mult == 4 &&
               cvt.len_ratio == 4.0);
        cvt.src_format = AUDIO_U8;
        cvt.dst_format = AUDIO_S16MSB;
        cvt.buf = buffer;
        cvt.len = 8;
        cvt.len_cvt = 8;
        for (uint32_t i = 0u; i < 8u; ++i)
            buffer[i] = (uint8_t)(0x80u + i);
        cvt.filter_index = 0;
        cvt.filters[0](&cvt, cvt.src_format);
        assert(converted_source ==
               ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_U8, 1u, 11025u));
        assert(converted_target ==
               ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u, 22050u));
        assert(converted_frames == 8u && cvt.len_cvt == 32);
        assert(buffer[0] == 0u && buffer[28] == 7u && buffer[30] == 7u);
        /* A dead service leaves the promised length, silent. */
        convert_failure = 1;
        cvt.len_cvt = 8;
        cvt.filter_index = 0;
        cvt.filters[0](&cvt, cvt.src_format);
        assert(cvt.len_cvt == 32 && buffer[0] == 0u && buffer[31] == 0u);
    }
    return 0;
}
