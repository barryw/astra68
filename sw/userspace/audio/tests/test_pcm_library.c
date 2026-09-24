#include <astra/pcm.h>
#include <astra/pcm_service.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t shared[ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_FRAME_BYTES];
static AstraPcmRequest last_request;
static uint32_t sends;
static uint32_t writes;
static uint32_t reply_status;
static int bad_transaction;
static int zero_control;

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
    assert(handle == 20u && (rights & ASTRA_RIGHT_WRITE) == 0u);
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
    assert((count == 0u) == (handles == NULL));
    last_request = *(const AstraPcmRequest *)message;
    ++sends;
    if (last_request.header.operation == ASTRA_PCM_WRITE) {
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
    reply->queued_frames = 123u;
    reply->hardware_frames = 45u;
    *size = sizeof(*reply);
    *handle_count = last_request.header.operation == ASTRA_PCM_OPEN &&
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
    uint8_t samples[1500u * ASTRA_PCM_FRAME_BYTES];
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
    assert(memcmp(shared, samples + accepted * ASTRA_PCM_FRAME_BYTES,
                  (1500u - accepted) * ASTRA_PCM_FRAME_BYTES) == 0);
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
    return 0;
}
