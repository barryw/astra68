#include <astra/pcm.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <stdint.h>

ASTRA_PROGRAM("pcm-certify", 0, 1, 0, "Astra68 contributors",
              "Astra native");

static uint8_t samples[ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_FRAME_BYTES];

static void fill_tone(uint32_t phase, uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t position = (phase + i) % 109u;
        int32_t sample = position < 54u ?
                         (int32_t)(position * 50000u) :
                         (int32_t)((108u - position) * 50000u);
        uint32_t bits = (uint32_t)sample;
        uint32_t at = i * ASTRA_PCM_FRAME_BYTES;

        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            samples[at++] = (uint8_t)bits;
            samples[at++] = (uint8_t)(bits >> 8);
            samples[at++] = (uint8_t)(bits >> 16);
        }
    }
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *service;
    AstraPcmStream stream = ASTRA_PCM_STREAM_INIT;
    AstraPcmStatus state = {0};
    uint32_t sent = 0u;
    uint64_t deadline;
    AstraResult result;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    service = astra_startup_capability(startup, ASTRA_CAPABILITY_PCM);
    if (service == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_pcm_open(service->handle, ASTRA_PCM_FORMAT_S24LE_STEREO,
                       &stream) != ASTRA_OK)
        return ASTRA_STATUS_IO;
    if (astra_pcm_write(&stream, NULL, 1u, &sent) !=
        ASTRA_ERROR_INVALID_ARGUMENT || sent != 0u)
        goto fail;
    deadline = astra_clock_monotonic() + UINT64_C(10000000000);
    while (sent < ASTRA_PCM_RATE) {
        uint32_t count = ASTRA_PCM_RATE - sent;
        uint32_t accepted = 0u;

        if (count > ASTRA_PCM_TRANSFER_FRAMES)
            count = ASTRA_PCM_TRANSFER_FRAMES;
        fill_tone(sent, count);
        result = astra_pcm_write(&stream, samples, count, &accepted);
        sent += accepted;
        if (result == ASTRA_ERROR_BUSY) {
            if (astra_clock_monotonic() >= deadline)
                goto fail;
            (void)astra_rt_thread_sleep(UINT64_C(1000000),
                                        ASTRA_THREAD_SLEEP_RELATIVE,
                                        0u, NULL);
        } else if (result != ASTRA_OK) {
            goto fail;
        }
    }
    if (astra_pcm_finish(&stream) != ASTRA_OK)
        goto fail;
    do {
        if (astra_pcm_status(&stream, &state) != ASTRA_OK ||
            state.underruns != 0u || state.overflows != 0u ||
            state.software_gaps != 0u)
            goto fail;
        if (state.queued_frames == 0u && state.hardware_frames == 0u)
            break;
        (void)astra_rt_thread_sleep(UINT64_C(1000000),
                                    ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
    } while (astra_clock_monotonic() < deadline);
    if (astra_clock_monotonic() >= deadline ||
        astra_pcm_close(&stream) != ASTRA_OK)
        goto fail;
    (void)astra_log("ASTRA PCM LIBRARY PASS");
    return ASTRA_STATUS_OK;

fail:
    if (stream.control != 0u) {
        AstraResult ignored = astra_pcm_close(&stream);
        (void)ignored;
    }
    (void)astra_log("ASTRA PCM LIBRARY FAIL");
    return ASTRA_STATUS_IO;
}
