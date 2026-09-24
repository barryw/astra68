#include <astra/audio_host.h>
#include <astra/host.h>
#include <astra/pcm_format.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <stdint.h>

ASTRA_PROGRAM("audio-certify", 0, 1, 0, "Astra68 contributors",
              "Astra native");

enum {
    RATE = 48000u,
    DURATION_SECONDS = 30u,
    FRAMES_PER_PACKET = ASTRA_AUDIO_HOST_PACKET_FRAMES,
    FRAME_BYTES = ASTRA_AUDIO_HOST_FRAME_BYTES,
    DATA_BYTES = FRAMES_PER_PACKET * FRAME_BYTES,
};

static uint32_t submit(AstraHostChannelClient *channel, uint16_t operation,
                       uint32_t handle, uint32_t value,
                       uint32_t data_length, uint32_t data_capacity,
                       AstraHostCommand **result)
{
    AstraHostCommand *command = astra_host_client_prepare(
        channel, ASTRA_HOST_SERVICE_AUDIO, operation);

    if (command == NULL)
        return ASTRA_STATUS_INVALID;
    command->handle = handle;
    command->value_lo = value;
    command->data_length = data_length;
    command->data_capacity = data_capacity;
    if (astra_host_client_submit(channel) != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    if (result != NULL)
        *result = command;
    return command->status;
}

static int32_t triangle(uint32_t phase)
{
    uint32_t quadrant = phase >> 30;
    uint32_t fraction = (phase >> 8) & UINT32_C(0x3fffff);

    switch (quadrant) {
    case 0u: return (int32_t)fraction;
    case 1u: return (int32_t)(UINT32_C(0x3fffff) - fraction);
    case 2u: return -(int32_t)fraction;
    default: return -(int32_t)(UINT32_C(0x3fffff) - fraction);
    }
}

static uint32_t prepare_pcm(AstraHostChannelClient *channel, uint32_t phase,
                            uint32_t frames)
{
    const uint32_t step = (uint32_t)((UINT64_C(440) << 32) / RATE);

    for (uint32_t frame = 0u; frame < frames; ++frame) {
        uint32_t bits = (uint32_t)triangle(phase) & UINT32_C(0xffffff);
        uint32_t offset = frame * FRAME_BYTES;

        channel->data[offset] = (uint8_t)bits;
        channel->data[offset + 1u] = (uint8_t)(bits >> 8);
        channel->data[offset + 2u] = (uint8_t)(bits >> 16);
        channel->data[offset + 3u] = (uint8_t)bits;
        channel->data[offset + 4u] = (uint8_t)(bits >> 8);
        channel->data[offset + 5u] = (uint8_t)(bits >> 16);
        phase += step;
    }
    return phase;
}

static uint32_t append_text(char *out, uint32_t at, const char *text)
{
    while (*text != '\0')
        out[at++] = *text++;
    return at;
}

static uint32_t append_hex32(char *out, uint32_t at, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4)
        out[at++] = digits[(value >> shift) & 0xfu];
    return at;
}

static void report(const char *prefix, uint32_t code, uint32_t detail)
{
    char line[100];
    uint32_t at = append_text(line, 0u, prefix);

    at = append_text(line, at, " stage=");
    at = append_hex32(line, at, code);
    at = append_text(line, at, " detail=");
    at = append_hex32(line, at, detail);
    line[at] = '\0';
    (void)astra_log(line);
}

static void pause_for_queue(void)
{
    (void)astra_rt_thread_sleep(UINT64_C(1000000),
                                ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    AstraHostChannelClient channel = {0};
    AstraHostCommand *result = NULL;
    uint32_t handle = 0u;
    uint32_t phase = 0u;
    uint32_t sent = 0u;
    uint32_t busy = 0u;
    uint32_t stage = 1u;
    uint32_t status;
    uint64_t started;
    uint64_t deadline;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    if (device == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = astra_host_client_open(device->handle, ASTRA_HOST_CAP_AUDIO,
                                    DATA_BYTES, &channel);
    if (status != ASTRA_SYSCALL_OK) {
        report("ASTRA GUEST AUDIO FAIL", stage, status);
        return ASTRA_STATUS_IO;
    }
    started = astra_clock_monotonic();
    deadline = started + UINT64_C(45000000000);
    stage = 2u;
    status = submit(&channel, ASTRA_HOST_AUDIO_OPEN, 0u,
                    ASTRA_PCM_FORMAT_S24LE_STEREO,
                    0u, 0u, &result);
    if (status != ASTRA_STATUS_OK || result == NULL ||
        result->result_value == 0u)
        goto fail;
    handle = result->result_value;
    stage = 3u;
    if (submit(&channel, ASTRA_HOST_AUDIO_WRITE, handle, 0u, 5u, 5u,
               NULL) != ASTRA_STATUS_INVALID ||
        submit(&channel, ASTRA_HOST_AUDIO_WRITE, UINT32_MAX, 0u,
               FRAME_BYTES, FRAME_BYTES, NULL) != ASTRA_STATUS_BAD_HANDLE)
        goto fail;

    stage = 4u;
    while (sent < RATE * DURATION_SECONDS) {
        uint32_t frames = RATE * DURATION_SECONDS - sent;
        uint32_t next_phase;

        if (frames > FRAMES_PER_PACKET)
            frames = FRAMES_PER_PACKET;
        next_phase = prepare_pcm(&channel, phase, frames);
        for (;;) {
            status = submit(&channel, ASTRA_HOST_AUDIO_WRITE, handle, 0u,
                            frames * FRAME_BYTES, frames * FRAME_BYTES,
                            NULL);
            if (status == ASTRA_STATUS_OK)
                break;
            if (status != ASTRA_STATUS_BUSY ||
                astra_clock_monotonic() >= deadline)
                goto fail;
            ++busy;
            pause_for_queue();
        }
        phase = next_phase;
        sent += frames;
    }
    stage = 5u;
    status = submit(&channel, ASTRA_HOST_AUDIO_FINISH, handle, 0u,
                    0u, 0u, NULL);
    if (status != ASTRA_STATUS_OK ||
        submit(&channel, ASTRA_HOST_AUDIO_WRITE, handle, 0u,
               FRAME_BYTES, FRAME_BYTES, NULL) != ASTRA_STATUS_INVALID)
        goto fail;
    stage = 6u;
    do {
        volatile AstraHostAudioStatus *audio;

        status = submit(&channel, ASTRA_HOST_AUDIO_STATUS, handle, 0u,
                        0u, sizeof(AstraHostAudioStatus), &result);
        if (status != ASTRA_STATUS_OK ||
            result->result_length != sizeof(AstraHostAudioStatus))
            goto fail;
        audio = (volatile AstraHostAudioStatus *)(void *)channel.data;
        if (audio->underruns != 0u || audio->overflows != 0u ||
            audio->software_gaps != 0u) {
            status = audio->underruns | audio->overflows |
                     audio->software_gaps;
            goto fail;
        }
        if (audio->queued_frames == 0u && audio->hardware_frames == 0u)
            break;
        pause_for_queue();
    } while (astra_clock_monotonic() < deadline);
    if (astra_clock_monotonic() >= deadline) {
        status = ASTRA_STATUS_BUSY;
        goto fail;
    }
    stage = 7u;
    status = submit(&channel, ASTRA_HOST_AUDIO_CLOSE, handle, 0u,
                    0u, 0u, NULL);
    if (status != ASTRA_STATUS_OK ||
        submit(&channel, ASTRA_HOST_AUDIO_STATUS, handle, 0u,
               0u, sizeof(AstraHostAudioStatus), NULL) !=
            ASTRA_STATUS_BAD_HANDLE)
        goto fail;
    (void)astra_host_client_close(&channel);
    report("ASTRA GUEST AUDIO PASS", sent, busy);
    return ASTRA_STATUS_OK;

fail:
    report("ASTRA GUEST AUDIO FAIL", stage, status);
    if (handle != 0u)
        (void)submit(&channel, ASTRA_HOST_AUDIO_CLOSE, handle, 0u,
                     0u, 0u, NULL);
    (void)astra_host_client_close(&channel);
    return ASTRA_STATUS_IO;
}
