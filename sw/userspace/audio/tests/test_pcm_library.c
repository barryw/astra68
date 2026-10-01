#include <astra/midi.h>
#include <astra/pcm.h>
#include <astra/pcm_service.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t shared[ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_MAX_FRAME_BYTES];
static AstraPcmRequest last_request;
static uint32_t sends;
static uint32_t writes;
static uint32_t reply_status;
static int bad_transaction;
static int zero_control;
/* Queued frames each status reply reports, in turn. */
static uint32_t queued_script[4];
static uint32_t queued_next;
static uint64_t slept[4];
static uint32_t sleeps;
/* The fake converter doubles the rate of S16BE mono by repeating frames. */
static int converting;
static uint8_t pending[65536];
static uint32_t pending_bytes;
static uint32_t converted_out;
static int convert_ended;
static int midi_mode;
static uint32_t loads;

/* astra_midi_presets() waits on the monotonic clock. */
uint64_t astra_clock_monotonic(void)
{
    static uint64_t now;

    return now += UINT64_C(1000000);
}

uint32_t astra_rt_thread_sleep(uint64_t deadline_ns, uint32_t flags,
                                uint32_t reserved, uint32_t *remaining_ns)
{
    assert(flags == ASTRA_THREAD_SLEEP_RELATIVE && reserved == 0u &&
           remaining_ns == NULL && sleeps < 4u);
    slept[sleeps++] = deadline_ns;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    assert(messages == 1u && bytes == sizeof(AstraPcmReply));
    *receive = 10u;
    *send = 11u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_create(uint32_t bytes, uint32_t rights,
                              uint32_t *handle)
{
    assert(bytes == sizeof(shared));
    assert((rights & ASTRA_RIGHT_WRITE) != 0u);
    *handle = 20u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                           void **address, uint32_t *size)
{
    assert(handle == 20u && (permissions & ASTRA_AREA_MAP_WRITE) != 0u);
    *address = shared;
    *size = sizeof(shared);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_unmap(void *address)
{
    assert(address == shared);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                                   uint32_t *duplicate)
{
    assert(handle == 20u &&
           ((rights & ASTRA_RIGHT_WRITE) != 0u) ==
               (converting != 0 || midi_mode != 0));
    *duplicate = 21u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    assert(handle != 0u);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t handle, const void *message,
                         uint32_t size, const uint32_t *handles,
                         uint32_t count)
{
    assert(size == sizeof(last_request));
    assert(handle == 99u || handle == 30u);
    assert(count == (handle == 99u ? 2u : 0u));
    assert(handle != 99u ||
           ((const AstraPcmRequest *)message)->header.operation ==
               (converting ? ASTRA_PCM_CONVERT_OPEN :
                midi_mode ? ASTRA_PCM_MIDI_OPEN : ASTRA_PCM_OPEN));
    assert((count == 0u) == (handles == NULL));
    last_request = *(const AstraPcmRequest *)message;
    ++sends;
    if (last_request.header.operation == ASTRA_PCM_MIDI_LOAD)
        ++loads;
    if (last_request.header.operation == ASTRA_PCM_CONVERT) {
        assert(!convert_ended);
        for (uint32_t i = 0u; i < last_request.frames; ++i)
            for (uint32_t copy = 0u; copy < 2u; ++copy) {
                pending[pending_bytes++] = shared[2u * i];
                pending[pending_bytes++] = shared[2u * i + 1u];
            }
        convert_ended = last_request.value == ASTRA_PCM_CONVERT_END;
        converted_out = pending_bytes < sizeof(shared) ? pending_bytes :
                        sizeof(shared);
        memcpy(shared, pending, converted_out);
        memmove(pending, pending + converted_out,
                pending_bytes - converted_out);
        pending_bytes -= converted_out;
        reply_status = ASTRA_STATUS_OK;
    } else if (last_request.header.operation == ASTRA_PCM_WRITE) {
        ++writes;
        reply_status = writes == 2u ? ASTRA_STATUS_BUSY : ASTRA_STATUS_OK;
    } else {
        reply_status = ASTRA_STATUS_OK;
    }
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline,
                        uint32_t *detail)
{
    assert(handle == 10u && deadline == ASTRA_DEADLINE_FOREVER);
    assert(detail == NULL);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_receive(uint32_t handle, void *message,
                            uint32_t capacity, uint32_t *handles,
                            uint32_t handle_capacity, uint32_t *size,
                            uint32_t *handle_count)
{
    AstraPcmReply *reply = message;

    assert(handle == 10u && capacity == sizeof(*reply));
    memset(reply, 0, sizeof(*reply));
    astra_message_header_set(&reply->header, sizeof(*reply),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             ASTRA_PCM_REPLY,
                             last_request.header.transaction_id +
                             (uint32_t)bad_transaction);
    reply->status = reply_status;
    reply->queued_frames = queued_next != 0u ?
                           queued_script[--queued_next] : 123u;
    if (last_request.header.operation == ASTRA_PCM_CONVERT) {
        reply->frames_out = converted_out / 2u;
        reply->queued_frames = pending_bytes / 2u;
    }
    if (last_request.header.operation == ASTRA_PCM_MIDI_STATUS)
        reply->value = 1u;
    reply->hardware_frames = 45u;
    *size = sizeof(*reply);
    *handle_count = (last_request.header.operation == ASTRA_PCM_OPEN ||
                     last_request.header.operation ==
                         ASTRA_PCM_CONVERT_OPEN ||
                     last_request.header.operation == ASTRA_PCM_MIDI_OPEN) &&
                    reply_status == ASTRA_STATUS_OK ? 1u : 0u;
    if (*handle_count == 1u) {
        assert(handle_capacity == 1u && handles != NULL);
        *handles = zero_control ? 0u : 30u;
    }
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraPcmStream stream = ASTRA_PCM_STREAM_INIT;
    AstraPcmStatus status = {0};
    uint8_t samples[1500u * 6u]; /* S24LE stereo */
    uint32_t accepted = 99u;
    uint32_t before;

    for (uint32_t i = 0u; i < sizeof(samples); ++i)
        samples[i] = (uint8_t)i;
    assert(astra_pcm_open(99u, ASTRA_PCM_FORMAT_S24LE_STEREO, &stream) ==
           ASTRA_OK);
    assert(stream.control == 30u);
    assert(stream.frame_bytes == 6u);
    assert(astra_pcm_write(&stream, samples, 1500u, &accepted) ==
           ASTRA_ERROR_BUSY);
    assert(accepted == ASTRA_PCM_TRANSFER_FRAMES && writes == 2u);
    assert(memcmp(shared, samples + accepted * 6u,
                  (1500u - accepted) * 6u) == 0);
    assert(astra_pcm_gain(&stream, 32768u) == ASTRA_OK);
    assert(last_request.value == 32768u);
    assert(astra_pcm_pause(&stream, 1) == ASTRA_OK);
    assert(last_request.header.operation == ASTRA_PCM_PAUSE &&
           last_request.value == 1u);
    assert(astra_pcm_pause(&stream, 0) == ASTRA_OK);
    assert(last_request.value == 0u);
    assert(astra_pcm_clear(&stream) == ASTRA_OK);
    assert(last_request.header.operation == ASTRA_PCM_CLEAR);
    assert(astra_pcm_status(&stream, &status) == ASTRA_OK);
    assert(status.queued_frames == 123u && status.hardware_frames == 45u);
    /* A wait sleeps for the time the host needs to play what is in the
     * way, at the stream's rate (48 kHz here), and stops at room. */
    queued_script[1] = ASTRA_PCM_QUEUE_FRAMES;          /* first status */
    queued_script[0] = ASTRA_PCM_QUEUE_FRAMES - 1024u;  /* after a sleep */
    queued_next = 2u;
    assert(astra_pcm_wait(&stream, 1024u) == ASTRA_OK);
    assert(sleeps == 1u && queued_next == 0u);
    assert(slept[0] == UINT64_C(1024) * 1000000000u / 48000u + 1000000u);
    assert(astra_pcm_wait(&stream, 0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_pcm_wait(&stream, ASTRA_PCM_QUEUE_FRAMES + 1u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_pcm_finish(&stream) == ASTRA_OK);
    before = sends;
    assert(astra_pcm_write(&stream, NULL, 1u, &accepted) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(accepted == 0u && sends == before);
    assert(astra_pcm_write(&stream, samples, UINT32_MAX, &accepted) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(accepted == 0u && sends == before);
    assert(astra_pcm_close(&stream) == ASTRA_OK);
    assert(stream.control == 0u && stream.mapped == NULL &&
           stream.frame_bytes == 0u);

    before = sends;
    assert(astra_pcm_open(99u, 0u, &stream) == ASTRA_ERROR_INVALID_ARGUMENT);
    /* The old bare encoding numbers name no channels or rate. */
    assert(astra_pcm_open(99u, 2u, &stream) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(sends == before);
    assert(astra_pcm_open(99u, ASTRA_PCM_FORMAT_S16BE_STEREO, &stream) ==
           ASTRA_OK);
    assert(stream.frame_bytes == 4u);
    writes = 0u;
    accepted = 0u;
    assert(astra_pcm_write(&stream, samples, 2u, &accepted) == ASTRA_OK);
    assert(accepted == 2u && last_request.frames == 2u);
    assert(memcmp(shared, samples, 8u) == 0);
    assert(astra_pcm_close(&stream) == ASTRA_OK);

    bad_transaction = 1;
    assert(astra_pcm_open(99u, ASTRA_PCM_FORMAT_S24LE_STEREO, &stream) ==
           ASTRA_ERROR_IO);
    assert(stream.control == 0u && stream.area == 0u);
    bad_transaction = 0;
    zero_control = 1;
    assert(astra_pcm_open(99u, ASTRA_PCM_FORMAT_S24LE_STEREO, &stream) ==
           ASTRA_ERROR_IO);
    assert(stream.control == 0u && stream.area == 0u);
    zero_control = 0;

    /* Conversion: a source larger than one transfer, output four times
     * larger, drained before more source is sent, ending exactly. */
    {
        static uint8_t source[6000u * 2u];
        static uint8_t target[12000u * 2u];
        const uint32_t from = ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u,
                                               11025u);
        const uint32_t to = ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u,
                                             22050u);
        uint32_t produced = 99u;

        for (uint32_t i = 0u; i < sizeof(source); ++i)
            source[i] = (uint8_t)(i * 7u);
        converting = 1;
        assert(astra_pcm_convert(99u, from, source, 6000u, to, target,
                                 11999u, &produced) ==
               ASTRA_ERROR_BUFFER_TOO_SMALL);
        assert(produced == 0u);
        assert(astra_pcm_convert(99u, from, source, 6000u, to, target,
                                 12000u, &produced) == ASTRA_OK);
        assert(produced == 12000u && convert_ended && pending_bytes == 0u);
        assert(last_request.header.operation == ASTRA_PCM_CLOSE);
        for (uint32_t i = 0u; i < 6000u; ++i)
            assert(memcmp(target + 4u * i, source + 2u * i, 2u) == 0 &&
                   memcmp(target + 4u * i + 2u, source + 2u * i, 2u) == 0);
        convert_ended = 0;
        assert(astra_pcm_convert(99u, from, NULL, 0u, to, NULL, 0u,
                                 &produced) == ASTRA_OK);
        assert(produced == 0u && convert_ended);
        assert(astra_pcm_convert(99u, 0u, source, 1u, to, target, 2u,
                                 &produced) == ASTRA_ERROR_INVALID_ARGUMENT);
    }

    /* MIDI: a song larger than one transfer goes in offset-ordered pieces
     * naming the whole size; play, status and system fonts by name. */
    {
        static uint8_t file[20000];
        AstraMidiSynth song = ASTRA_MIDI_SYNTH_INIT;
        int active = 0;

        converting = 0;
        midi_mode = 1;
        assert(astra_midi_open(99u, &song) == ASTRA_OK);
        assert(astra_midi_load(&song, file, sizeof(file)) == ASTRA_OK);
        assert(loads == 3u && last_request.value == 16384u &&
               last_request.target == sizeof(file) &&
               last_request.frames == sizeof(file) - 16384u);
        assert(astra_midi_play(&song, ASTRA_MIDI_FOREVER) == ASTRA_OK &&
               last_request.value == ASTRA_PCM_MIDI_FOREVER);
        assert(astra_midi_play(&song, 0) == ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_active(&song, &active) == ASTRA_OK && active == 1);
        assert(astra_midi_add_system_font(&song, "TimGM6mb") == ASTRA_OK &&
               last_request.frames == 8u &&
               memcmp(shared, "TimGM6mb", 8u) == 0);
        assert(astra_midi_add_system_font(&song, "") ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        /* Arguments are judged before anything is sent. */
        assert(astra_midi_set(&song, (AstraMidiSetting)0, 1u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_set(&song, (AstraMidiSetting)(ASTRA_MIDI_POSITION +
                                                        1), 1u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_send(&song, NULL, 1u) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_fonts(&song, 0u, NULL, 1u, NULL, NULL) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_status(&song, NULL) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_midi_close(&song) == ASTRA_OK &&
               last_request.header.operation == ASTRA_PCM_CLOSE);
    }
    return 0;
}
