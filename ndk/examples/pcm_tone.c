#include <astra/pcm.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("pcm_tone", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

/* Half a second of a 440 Hz square wave, 48 kHz mono, in 10 ms buffers. */
enum {
    RATE = 48000u,
    PERIOD = 480u,
    BUFFERS = 3u,
    PLAYED = 50u,
    HALF_CYCLE = RATE / 440u / 2u,
    LEVEL = 6000
};

/* Fill one buffer in place: the host reads it from here, so nothing is
   copied before it plays. */
static void fill(int16_t *samples, uint32_t *phase)
{
    for (uint32_t frame = 0u; frame < PERIOD; ++frame) {
        samples[frame] = (*phase / HALF_CYCLE) % 2u == 0u ? LEVEL : -LEVEL;
        ++*phase;
    }
}

/* Haiku's buffer loop: fill the free buffer, hand it over, sleep until the
   host gives one back. One system call per buffer and no service on the
   way. A host without audio streams answers ASTRA_ERROR_UNSUPPORTED; a
   program that must still play there uses astra_pcm_open() and
   astra_pcm_write() instead. */
static AstraResult play(AstraHandle service)
{
    AstraPcmBuffers buffers = ASTRA_PCM_BUFFERS_INIT;
    uint32_t phase = 0u;
    AstraResult result = astra_pcm_buffers_open(
        service, ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u, RATE), PERIOD,
        BUFFERS, &buffers);
    AstraResult closed;

    if (result != ASTRA_OK)
        return result;
    for (uint32_t played = 0u; played < PLAYED && result == ASTRA_OK;
         ++played) {
        fill(astra_pcm_buffers_get(&buffers), &phase);
        result = astra_pcm_buffers_queue(&buffers);
        if (result == ASTRA_OK)
            result = astra_pcm_buffers_wait(
                &buffers, astra_clock_monotonic() + UINT64_C(1000000000));
    }
    closed = astra_pcm_buffers_close(&buffers);
    return result == ASTRA_OK ? closed : result;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    const AstraStartupCapability *pcm =
        astra_startup_capability(startup, ASTRA_CAPABILITY_PCM);

    if (output == 0)
        return 1;
    if (pcm == 0) {
        (void)astra_print(output->handle, "pcm_tone: no PCM capability\n");
        return 2;
    }
    if (play(pcm->handle) != ASTRA_OK)
        return 3;
    (void)astra_print(output->handle, "pcm_tone: 50 buffers of 480 frames, "
                                      "440 Hz, played in place\n");
    return 0;
}
