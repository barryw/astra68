#include "astra_codec_provider.h"
#include "astra_codec_wire.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

typedef struct WorkerSource {
    int socket;
    uint64_t length;
} WorkerSource;

static int source_read_at(void *context, uint64_t offset, void *output,
                          size_t capacity, size_t *received)
{
    WorkerSource *source = context;
    AstraCodecWorkerMessage request = {0};
    AstraCodecWorkerMessage reply = {0};
    int error;

    if (capacity > ASTRA_CODEC_WORKER_LIMIT)
        capacity = ASTRA_CODEC_WORKER_LIMIT;
    if (offset >= source->length) {
        *received = 0;
        return ASTRA_CODEC_OK;
    }
    if (capacity > source->length - offset)
        capacity = (size_t)(source->length - offset);
    request.kind = ASTRA_CODEC_WORKER_NEED_SOURCE;
    request.offset_hi = (uint32_t)(offset >> 32);
    request.offset_lo = (uint32_t)offset;
    request.capacity = (uint32_t)capacity;
    error = astra_codec_wire_message(source->socket, &request, 1, -1);
    if (error == 0)
        error = astra_codec_wire_message(source->socket, &reply, 0, -1);
    if (error != 0 || reply.kind != ASTRA_CODEC_WORKER_SOURCE ||
        reply.status != 0 || reply.length > capacity ||
        reply.offset_hi != request.offset_hi ||
        reply.offset_lo != request.offset_lo)
        return ASTRA_CODEC_IO;
    if (reply.length != 0) {
        error = astra_codec_wire_transfer(source->socket, output,
                                           reply.length, 0, -1);
        if (error != 0)
            return ASTRA_CODEC_IO;
    }
    *received = reply.length;
    return ASTRA_CODEC_OK;
}

static int reply_status(int socket, uint32_t kind, int status)
{
    AstraCodecWorkerMessage reply = {0};

    reply.kind = kind;
    reply.status = status == ASTRA_CODEC_OK ? 0u : (uint32_t)-status;
    return astra_codec_wire_message(socket, &reply, 1, -1);
}

int astra_codec_worker_run(int socket)
{
    AstraCodecWorkerMessage request = {0};
    AstraCodecWorkerMessage reply = {0};
    AstraCodecSession session = {0};
    AstraCodecFormat format = {0};
    WorkerSource source = {socket, 0};
    AstraCodecSource input = {&source, 0, source_read_at};
    uint8_t output[ASTRA_CODEC_WORKER_LIMIT];
    int error;

    error = astra_codec_wire_message(socket, &request, 0, -1);
    if (error != 0 || request.kind != ASTRA_CODEC_WORKER_OPEN ||
        request.length != 0 || request.capacity != 0)
        return 1;
    source.length = ((uint64_t)request.offset_hi << 32) |
                    request.offset_lo;
    input.length = source.length;
    error = astra_codec_open(&input, &session, &format);
    reply.kind = ASTRA_CODEC_WORKER_FORMAT;
    reply.status = error == ASTRA_CODEC_OK ? 0u : (uint32_t)-error;
    if (error == ASTRA_CODEC_OK) {
        reply.media_kind = format.kind;
        reply.encoding = format.encoding;
        reply.sample_rate = format.sample_rate;
        reply.channels = format.channels;
        reply.width = format.width;
        reply.height = format.height;
        reply.stride = format.stride;
        reply.frame_rate_num = format.frame_rate_num;
        reply.frame_rate_den = format.frame_rate_den;
    }
    if (astra_codec_wire_message(socket, &reply, 1, -1) != 0 ||
        error != ASTRA_CODEC_OK) {
        astra_codec_close(&session);
        return 1;
    }
    for (;;) {
        AstraCodecChunk chunk = {0};

        error = astra_codec_wire_message(socket, &request, 0, -1);
        if (error != 0)
            break;
        if (request.kind == ASTRA_CODEC_WORKER_READ) {
            if (request.capacity == 0 ||
                request.capacity > ASTRA_CODEC_WORKER_LIMIT ||
                request.length != 0)
                break;
            error = astra_codec_read(&session, output, request.capacity,
                                     &chunk);
            reply = (AstraCodecWorkerMessage){0};
            reply.kind = ASTRA_CODEC_WORKER_CHUNK;
            reply.status = error == ASTRA_CODEC_OK ? 0u : (uint32_t)-error;
            if (error == ASTRA_CODEC_OK) {
                reply.length = (uint32_t)chunk.bytes;
                reply.frame_bytes = (uint32_t)chunk.frame_bytes;
                reply.frame_offset = (uint32_t)chunk.frame_offset;
                reply.timestamp_hi = (uint32_t)(chunk.timestamp_ns >> 32);
                reply.timestamp_lo = (uint32_t)chunk.timestamp_ns;
                reply.duration_hi = (uint32_t)(chunk.duration_ns >> 32);
                reply.duration_lo = (uint32_t)chunk.duration_ns;
                reply.end = (uint32_t)chunk.end;
            }
            if (astra_codec_wire_message(socket, &reply, 1, -1) != 0 ||
                (reply.length != 0 &&
                 astra_codec_wire_transfer(socket, output, reply.length,
                                           1, -1) != 0))
                break;
        } else if (request.kind == ASTRA_CODEC_WORKER_SEEK) {
            uint64_t timestamp = ((uint64_t)request.offset_hi << 32) |
                                 request.offset_lo;

            error = astra_codec_seek(&session, timestamp);
            if (reply_status(socket, ASTRA_CODEC_WORKER_SEEKED, error) != 0)
                break;
        } else if (request.kind == ASTRA_CODEC_WORKER_CLOSE) {
            astra_codec_close(&session);
            return reply_status(socket, ASTRA_CODEC_WORKER_CLOSED,
                                ASTRA_CODEC_OK) == 0 ? 0 : 1;
        } else {
            break;
        }
    }
    astra_codec_close(&session);
    return 1;
}
