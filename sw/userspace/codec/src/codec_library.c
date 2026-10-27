#include <astra/codec.h>
#include <astra/area.h>
#include <astra/codec_service.h>
#include <astra/runtime.h>

#include <string.h>

static void release(AstraCodecStream *stream)
{
    if (stream->mapped != NULL)
        (void)astra_rt_area_unmap(stream->mapped);
    if (stream->area != 0u)
        (void)astra_close(stream->area);
    if (stream->reply != 0u)
        (void)astra_close(stream->reply);
    if (stream->control != 0u)
        (void)astra_close(stream->control);
    memset(stream, 0, sizeof(*stream));
}

static AstraResult receive(AstraCodecStream *stream, uint32_t transaction,
                           int transfer, AstraCodecServiceReply *reply,
                           uint32_t *control)
{
    uint32_t bytes = 0u, count = 0u;
    uint32_t status = astra_wait_one_restart(stream->reply,
        ASTRA_DEADLINE_FOREVER, NULL);

    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    status = astra_port_receive(stream->reply, reply, sizeof(*reply),
                                transfer ? control : NULL,
                                transfer ? 1u : 0u, &bytes, &count);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    if (bytes != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u || reply->header.reserved != 0u ||
        reply->header.protocol != ASTRA_CODEC_PROTOCOL ||
        reply->header.protocol_version != ASTRA_CODEC_PROTOCOL_VERSION ||
        reply->header.operation != ASTRA_CODEC_SERVICE_REPLY ||
        reply->header.transaction_id != transaction ||
        count != (transfer && reply->status == ASTRA_STATUS_OK ? 1u : 0u)) {
        if (count != 0u && control != NULL && *control != 0u)
            (void)astra_close(*control);
        return ASTRA_ERROR_IO;
    }
    if (reply->status != ASTRA_STATUS_OK)
        return astra_result_from_service(reply->status);
    if (reply->event.magic != ASTRA_CODEC_WORKER_MAGIC ||
        reply->event.version != ASTRA_CODEC_WORKER_VERSION)
        return transfer ? ASTRA_ERROR_IO : ASTRA_OK;
    return ASTRA_OK;
}

static AstraResult send_request(AstraCodecStream *stream, uint32_t operation,
                                uint64_t offset, uint32_t bytes,
                                AstraCodecServiceReply *reply)
{
    AstraCodecServiceRequest request = {0};
    uint32_t status;

    ++stream->transaction;
    if (stream->transaction == 0u)
        ++stream->transaction;
    astra_message_header_set(&request.header, sizeof(request),
        ASTRA_CODEC_PROTOCOL, ASTRA_CODEC_PROTOCOL_VERSION, operation,
        stream->transaction);
    request.offset_hi = (uint32_t)(offset >> 32);
    request.offset_lo = (uint32_t)offset;
    request.bytes = bytes;
    status = astra_port_send(stream->control, &request, sizeof(request),
                             NULL, 0u);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    return receive(stream, stream->transaction, 0, reply, NULL);
}

static AstraResult provider_status(const AstraCodecWorkerMessage *event)
{
    switch (event->status) {
    case 0u: return ASTRA_OK;
    case 1u: return ASTRA_ERROR_INVALID_ARGUMENT;
    case 2u: return ASTRA_ERROR_UNSUPPORTED;
    default: return ASTRA_ERROR_IO;
    }
}

static AstraResult finish(AstraCodecStream *stream,
                          AstraCodecServiceReply *reply,
                          uint32_t expected)
{
    /* Bound a broken provider's source-request loop per client operation. */
    for (uint32_t requests = 0u; requests < 4096u; ++requests) {
        AstraCodecWorkerMessage *event = &reply->event;
        AstraResult result;
        uint64_t offset;
        uint32_t amount = 0u;

        if (event->magic != ASTRA_CODEC_WORKER_MAGIC ||
            event->version != ASTRA_CODEC_WORKER_VERSION)
            return ASTRA_ERROR_IO;
        result = provider_status(event);
        if (result != ASTRA_OK)
            return result;
        if (event->kind == expected)
            return ASTRA_OK;
        if (event->kind != ASTRA_CODEC_WORKER_NEED_SOURCE ||
            event->capacity == 0u ||
            event->capacity > ASTRA_CODEC_WORKER_LIMIT ||
            event->length != 0u)
            return ASTRA_ERROR_IO;
        offset = ((uint64_t)event->offset_hi << 32) | event->offset_lo;
        result = stream->read_at(stream->source_context, offset,
            stream->mapped,
            event->capacity < ASTRA_CODEC_TRANSFER_BYTES ?
                event->capacity : ASTRA_CODEC_TRANSFER_BYTES, &amount);
        if (result != ASTRA_OK)
            return result;
        if (amount > event->capacity || amount > ASTRA_CODEC_TRANSFER_BYTES)
            return ASTRA_ERROR_IO;
        result = send_request(stream, ASTRA_CODEC_SERVICE_SOURCE, offset,
                              amount, reply);
        if (result != ASTRA_OK)
            return result;
    }
    return ASTRA_ERROR_IO;
}

AstraResult astra_codec_open(AstraHandle service, AstraCodecReadAt read_at,
    void *source_context, uint64_t source_length, AstraCodecStream *stream,
    AstraCodecFormat *format)
{
    AstraCodecServiceRequest request = {0};
    AstraCodecServiceReply reply = {0};
    uint32_t send_area = 0u, send_reply = 0u, mapped = 0u;
    uint32_t handles[2];
    uint32_t status;
    AstraResult result;

    if (service == 0u || read_at == NULL || stream == NULL || format == NULL ||
        stream->control != 0u || stream->reply != 0u ||
        stream->area != 0u || stream->mapped != NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_rt_port_create(1u, sizeof(reply), &stream->reply,
                                  &send_reply);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    status = astra_rt_area_create(ASTRA_CODEC_TRANSFER_BYTES,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER, &stream->area);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto failed;
    }
    status = astra_rt_area_map(stream->area, ASTRA_AREA_MAP_READ |
        ASTRA_AREA_MAP_WRITE, &stream->mapped, &mapped);
    if (status != ASTRA_SYSCALL_OK || mapped < ASTRA_CODEC_TRANSFER_BYTES) {
        result = ASTRA_ERROR_NO_RESOURCES;
        goto failed;
    }
    status = astra_rt_handle_duplicate(stream->area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER, &send_area);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto failed;
    }
    stream->read_at = read_at;
    stream->source_context = source_context;
    stream->transaction = 1u;
    astra_message_header_set(&request.header, sizeof(request),
        ASTRA_CODEC_PROTOCOL, ASTRA_CODEC_PROTOCOL_VERSION,
        ASTRA_CODEC_SERVICE_OPEN, stream->transaction);
    request.offset_hi = (uint32_t)(source_length >> 32);
    request.offset_lo = (uint32_t)source_length;
    handles[0] = send_area;
    handles[1] = send_reply;
    status = astra_port_send(service, &request, sizeof(request), handles, 2u);
    if (status != ASTRA_SYSCALL_OK) {
        result = astra_result_from_syscall(status);
        goto failed;
    }
    send_area = send_reply = 0u;
    result = receive(stream, stream->transaction, 1, &reply, &stream->control);
    if (result == ASTRA_OK)
        result = finish(stream, &reply, ASTRA_CODEC_WORKER_FORMAT);
    if (result == ASTRA_OK) {
        const AstraCodecWorkerMessage *event = &reply.event;

        *format = (AstraCodecFormat){event->media_kind, event->encoding,
            event->sample_rate, event->channels, event->width, event->height,
            event->stride, event->frame_rate_num, event->frame_rate_den};
        return ASTRA_OK;
    }
failed:
    if (send_area != 0u) (void)astra_close(send_area);
    if (send_reply != 0u) (void)astra_close(send_reply);
    if (stream->control != 0u) {
        AstraCodecServiceReply ignored;
        (void)send_request(stream, ASTRA_CODEC_SERVICE_CLOSE, 0u, 0u,
                           &ignored);
    }
    release(stream);
    return result;
}

AstraResult astra_codec_read(AstraCodecStream *stream, void *output,
    uint32_t capacity, AstraCodecChunk *chunk)
{
    AstraCodecServiceReply reply = {0};
    AstraCodecWorkerMessage *event = &reply.event;
    AstraResult result;

    if (stream == NULL || stream->control == 0u || output == NULL ||
        chunk == NULL || capacity == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (capacity > ASTRA_CODEC_TRANSFER_BYTES)
        capacity = ASTRA_CODEC_TRANSFER_BYTES;
    result = send_request(stream, ASTRA_CODEC_SERVICE_READ, 0u,
                          capacity, &reply);
    if (result == ASTRA_OK)
        result = finish(stream, &reply, ASTRA_CODEC_WORKER_CHUNK);
    if (result != ASTRA_OK)
        return result;
    if (event->length > capacity || event->frame_offset > event->frame_bytes)
        return ASTRA_ERROR_IO;
    memcpy(output, stream->mapped, event->length);
    *chunk = (AstraCodecChunk){event->length, event->frame_bytes,
        event->frame_offset,
        ((uint64_t)event->timestamp_hi << 32) | event->timestamp_lo,
        ((uint64_t)event->duration_hi << 32) | event->duration_lo,
        event->end != 0u};
    return ASTRA_OK;
}

AstraResult astra_codec_seek(AstraCodecStream *stream, uint64_t timestamp_ns)
{
    AstraCodecServiceReply reply = {0};
    AstraResult result;

    if (stream == NULL || stream->control == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = send_request(stream, ASTRA_CODEC_SERVICE_SEEK,
                          timestamp_ns, 0u, &reply);
    return result == ASTRA_OK ?
        finish(stream, &reply, ASTRA_CODEC_WORKER_SEEKED) : result;
}

AstraResult astra_codec_close(AstraCodecStream *stream)
{
    AstraCodecServiceReply reply = {0};
    AstraResult result;

    if (stream == NULL || stream->control == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = send_request(stream, ASTRA_CODEC_SERVICE_CLOSE, 0u, 0u,
                          &reply);
    release(stream);
    return result;
}
