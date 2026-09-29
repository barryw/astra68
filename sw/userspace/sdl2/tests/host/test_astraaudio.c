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
static uint32_t sleep_count;

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
    spec->size = (Uint32)spec->samples * spec->channels * 2u;
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
    assert(format == ASTRA_PCM_FORMAT_S16BE_STEREO);
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
uint32_t astra_rt_thread_sleep(uint64_t deadline_ns, uint32_t flags,
                                uint32_t reserved, uint32_t *remaining_ns)
{
    (void)deadline_ns;
    (void)flags;
    (void)reserved;
    (void)remaining_ns;
    ++sleep_count;
    return 0u;
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
    assert(driver.OpenDevice(&device, NULL) == 0);
    assert(device.spec.freq == ASTRA_PCM_RATE);
    assert(device.spec.channels == ASTRA_PCM_CHANNELS);
    assert(device.spec.format == AUDIO_S16MSB);
    assert(device.spec.size == 1024u);
    assert(driver.GetDeviceBuf(&device) != NULL);
    write_busy_once = 1;
    driver.PlayDevice(&device);
    assert(received_frames == 256u);
    assert(sleep_count == 1u);
    assert(disconnected == 0);
    status_failure = 1;
    driver.WaitDevice(&device);
    assert(disconnected == 1); /* failed provider does not masquerade as audio */
    driver.CloseDevice(&device);
    assert(device.hidden == NULL);
    return 0;
}
