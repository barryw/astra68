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
    return 0;
}
