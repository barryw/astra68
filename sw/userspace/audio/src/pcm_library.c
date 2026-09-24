#include <astra/pcm.h>

#include <astra/area.h>
#include <astra/pcm_service.h>
#include <astra/runtime.h>

#include <string.h>

static int stream_open(const AstraPcmStream *stream)
{
    return stream != NULL && stream->control != 0u && stream->reply != 0u &&
           stream->area != 0u && stream->mapped != NULL &&
           stream->frame_bytes != 0u;
}

static void release(AstraPcmStream *stream)
{
    if (stream->mapped != NULL)
        (void)astra_rt_area_unmap(stream->mapped);
    if (stream->area != 0u)
        (void)astra_close(stream->area);
    if (stream->reply != 0u)
        (void)astra_close(stream->reply);
    if (stream->control != 0u)
        (void)astra_close(stream->control);
    (void)memset(stream, 0, sizeof(*stream));
}

static AstraResult receive_reply(AstraPcmStream *stream,
                                 AstraPcmReply *reply,
                                 uint32_t *received)
{
    uint32_t bytes = 0u, count = 0u;
    uint32_t status = astra_wait_one_restart(stream->reply,
                                              ASTRA_DEADLINE_FOREVER, NULL);

    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    status = astra_port_receive(stream->reply, reply, sizeof(*reply),
                                received, received == NULL ? 0u : 1u,
                                &bytes, &count);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    if (bytes != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u ||
        reply->header.protocol != ASTRA_PCM_PROTOCOL ||
        reply->header.protocol_version != ASTRA_PCM_PROTOCOL_VERSION ||
        reply->header.reserved != 0u ||
        reply->header.operation != ASTRA_PCM_REPLY ||
        reply->header.transaction_id != stream->transaction ||
        count != (received == NULL ? 0u :
                  reply->status == ASTRA_STATUS_OK ? 1u : 0u) ||
        (count == 1u && received != NULL && *received == 0u)) {
        if (count == 1u && received != NULL && *received != 0u)
            (void)astra_close(*received);
        return ASTRA_ERROR_IO;
    }
    return astra_result_from_service(reply->status);
}

static AstraResult exchange(AstraPcmStream *stream, uint32_t operation,
                            uint32_t frames, uint32_t value,
                            AstraPcmReply *reply)
{
    AstraPcmRequest request = {0};
    uint32_t status;

    if (!stream_open(stream))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    ++stream->transaction;
    if (stream->transaction == 0u)
        ++stream->transaction;
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             operation, stream->transaction);
    request.frames = frames;
    request.value = value;
    status = astra_port_send(stream->control, &request, sizeof(request),
                             NULL, 0u);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    return receive_reply(stream, reply, NULL);
}

AstraResult astra_pcm_open(AstraHandle service, uint32_t format,
                           AstraPcmStream *stream)
{
    AstraPcmRequest request = {0};
    AstraPcmReply reply = {0};
    uint32_t reply_send = 0u, area_send = 0u, mapped_size = 0u;
    uint32_t handles[2];
    uint32_t status;
    AstraResult result;

    uint32_t frame_bytes = astra_pcm_format_frame_bytes(format);

    if (service == 0u || frame_bytes == 0u || stream == NULL ||
        stream->control != 0u ||
        stream->reply != 0u || stream->area != 0u ||
        stream->mapped != NULL || stream->transaction != 0u ||
        stream->frame_bytes != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_rt_port_create(1u, sizeof(reply), &stream->reply,
                                  &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    status = astra_rt_area_create(ASTRA_PCM_TRANSFER_FRAMES *
                                  ASTRA_PCM_FRAME_BYTES,
                                  ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                  ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
                                  &stream->area);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto fail;
    }
    status = astra_rt_area_map(stream->area, ASTRA_AREA_MAP_READ |
                               ASTRA_AREA_MAP_WRITE, &stream->mapped,
                               &mapped_size);
    if (status != ASTRA_SYSCALL_OK ||
        mapped_size < ASTRA_PCM_TRANSFER_FRAMES * ASTRA_PCM_FRAME_BYTES) {
        result = ASTRA_ERROR_NO_RESOURCES;
        goto fail;
    }
    status = astra_rt_handle_duplicate(stream->area,
                                        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                                        ASTRA_RIGHT_TRANSFER, &area_send);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto fail;
    }
    stream->transaction = 1u;
    stream->frame_bytes = frame_bytes;
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             ASTRA_PCM_OPEN, stream->transaction);
    handles[0] = area_send;
    handles[1] = reply_send;
    request.value = format;
    status = astra_port_send(service, &request, sizeof(request), handles, 2u);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto fail;
    }
    area_send = 0u;
    reply_send = 0u;
    result = receive_reply(stream, &reply, &stream->control);
    if (result == ASTRA_OK)
        return ASTRA_OK;
fail:
    if (area_send != 0u)
        (void)astra_close(area_send);
    if (reply_send != 0u)
        (void)astra_close(reply_send);
    release(stream);
    return result;
}

AstraResult astra_pcm_write(AstraPcmStream *stream, const void *frames,
                            uint32_t frame_count, uint32_t *accepted)
{
    const uint8_t *source = frames;
    AstraPcmReply reply = {0};
    AstraResult result;

    if (accepted != NULL)
        *accepted = 0u;
    if (!stream_open(stream) || accepted == NULL ||
        (frames == NULL && frame_count != 0u) ||
        frame_count > UINT32_MAX / stream->frame_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t done = 0u; done < frame_count;) {
        uint32_t batch = frame_count - done;

        if (batch > ASTRA_PCM_TRANSFER_FRAMES)
            batch = ASTRA_PCM_TRANSFER_FRAMES;
        (void)memcpy(stream->mapped,
                     source + (size_t)done * stream->frame_bytes,
                     batch * stream->frame_bytes);
        result = exchange(stream, ASTRA_PCM_WRITE, batch, 0u, &reply);
        if (result != ASTRA_OK)
            return result;
        done += batch;
        *accepted = done;
    }
    return ASTRA_OK;
}

AstraResult astra_pcm_gain(AstraPcmStream *stream, uint32_t gain_q16)
{
    AstraPcmReply reply = {0};

    return exchange(stream, ASTRA_PCM_GAIN, 0u, gain_q16, &reply);
}

AstraResult astra_pcm_pause(AstraPcmStream *stream, int paused)
{
    AstraPcmReply reply = {0};

    return exchange(stream, ASTRA_PCM_PAUSE, 0u, paused != 0 ? 1u : 0u,
                    &reply);
}

AstraResult astra_pcm_clear(AstraPcmStream *stream)
{
    AstraPcmReply reply = {0};

    return exchange(stream, ASTRA_PCM_CLEAR, 0u, 0u, &reply);
}

AstraResult astra_pcm_status(AstraPcmStream *stream, AstraPcmStatus *status)
{
    AstraPcmReply reply = {0};
    AstraResult result;

    if (status == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(stream, ASTRA_PCM_STATUS, 0u, 0u, &reply);
    if (result == ASTRA_OK) {
        status->queued_frames = reply.queued_frames;
        status->hardware_frames = reply.hardware_frames;
        status->underruns = reply.underruns;
        status->overflows = reply.overflows;
        status->software_gaps = reply.software_gaps;
    }
    return result;
}

AstraResult astra_pcm_finish(AstraPcmStream *stream)
{
    AstraPcmReply reply = {0};

    return exchange(stream, ASTRA_PCM_FINISH, 0u, 0u, &reply);
}

AstraResult astra_pcm_close(AstraPcmStream *stream)
{
    AstraPcmReply reply = {0};
    AstraResult result;

    if (!stream_open(stream))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(stream, ASTRA_PCM_CLOSE, 0u, 0u, &reply);
    release(stream);
    return result;
}
