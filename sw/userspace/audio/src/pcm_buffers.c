#include <astra/pcm.h>

#include <astra/audio_stream.h>
#include <astra/compiler.h>
#include <astra/pcm_service.h>
#include <astra/runtime.h>

#include <string.h>

#include "pcm_session.h"

/*
 * An application's own buffer group on a kernel audio stream
 * (audio_stream.h): Haiku's BBufferGroup, filled in place and handed to the
 * host mixer by the application's own audio thread (BSoundPlayer). The
 * media service grants the right to open streams and is not involved again.
 */

static volatile AstraAudioStreamHeader *header_of(
    const AstraPcmBuffers *buffers)
{
    return (volatile AstraAudioStreamHeader *)buffers->header;
}

static int buffers_open(const AstraPcmBuffers *buffers)
{
    return buffers != NULL && buffers->stream != 0u &&
           buffers->header != NULL && buffers->count != 0u;
}

/* ASTRA_PCM_STREAM_GRANT: a host device handle that opens streams. */
static AstraResult stream_grant(AstraHandle service, uint32_t *device)
{
    AstraPcmRequest request = {0};
    AstraPcmReply reply = {0};
    uint32_t reply_receive = 0u, reply_send = 0u;
    uint32_t status;
    AstraResult result;

    *device = 0u;
    status = astra_rt_port_create(1u, sizeof(reply), &reply_receive,
                                  &reply_send);
    if (status != ASTRA_SYSCALL_OK)
        return astra_result_from_syscall(status);
    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             ASTRA_PCM_STREAM_GRANT, 1u);
    status = astra_port_send(service, &request, sizeof(request), &reply_send,
                             1u);
    if (status != ASTRA_SYSCALL_OK) {
        (void)astra_close(reply_send);
        (void)astra_close(reply_receive);
        return astra_result_from_syscall(status);
    }
    result = astra_pcm_reply_receive(reply_receive, 1u, &reply, device);
    (void)astra_close(reply_receive);
    return result;
}

AstraResult astra_pcm_buffers_open(AstraHandle service, uint32_t format,
                                   uint32_t period_frames, uint32_t count,
                                   AstraPcmBuffers *buffers)
{
    AstraAudioStreamOpen request = {0};
    AstraDmaBufferInfo dma = {0};
    uint32_t stride = astra_audio_stream_buffer_bytes(
        astra_pcm_format_frame_bytes(format), period_frames);
    uint32_t device = 0u;
    uint32_t status;
    AstraResult result;

    if (service == 0u || buffers == NULL || buffers->stream != 0u ||
        buffers->dma != 0u || stride == 0u ||
        period_frames < ASTRA_AUDIO_STREAM_PERIOD_MIN ||
        period_frames > ASTRA_AUDIO_STREAM_PERIOD_MAX ||
        count < ASTRA_AUDIO_STREAM_BUFFERS_MIN ||
        count > ASTRA_AUDIO_STREAM_BUFFERS_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = stream_grant(service, &device);
    if (result != ASTRA_OK)
        return result;
    status = astra_dma_create(ASTRA_AUDIO_STREAM_HEADER_SIZE + count * stride,
                              &dma);
    if (status == ASTRA_SYSCALL_OK) {
        request.size = sizeof(request);
        request.buffer = dma.handle;
        request.format = format;
        request.period_frames = period_frames;
        request.buffer_count = count;
        status = astra_audio_stream_open(device, &request);
    }
    /* The stream holds what it needs; the right to open more goes. */
    (void)astra_close(device);
    if (status != ASTRA_SYSCALL_OK) {
        if (dma.handle != 0u)
            (void)astra_close(dma.handle);
        return astra_result_from_syscall(status);
    }
    buffers->stream = request.stream;
    buffers->dma = dma.handle;
    buffers->header = (void *)(uintptr_t)dma.virtual_base;
    buffers->first = (uint8_t *)(uintptr_t)dma.virtual_base +
                     ASTRA_AUDIO_STREAM_HEADER_SIZE;
    buffers->stride = stride;
    buffers->period_frames = period_frames;
    buffers->count = count;
    buffers->queued = 0u;
    buffers->format = format;
    return ASTRA_OK;
}

void *astra_pcm_buffers_get(AstraPcmBuffers *buffers)
{
    if (!buffers_open(buffers))
        return NULL;
    return buffers->first + (buffers->queued % buffers->count) *
                                buffers->stride;
}

AstraResult astra_pcm_buffers_queue(AstraPcmBuffers *buffers)
{
    volatile AstraAudioStreamHeader *header;

    if (!buffers_open(buffers))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    header = header_of(buffers);
    if (buffers->queued - header->consumed >= buffers->count)
        return ASTRA_ERROR_BUSY;
    /* The samples the caller wrote before the count that publishes them. */
    astra_memory_release_fence();
    header->queued = ++buffers->queued;
    return ASTRA_OK;
}

AstraResult astra_pcm_buffers_wait(AstraPcmBuffers *buffers,
                                   uint64_t deadline_ns)
{
    if (!buffers_open(buffers))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_result_from_syscall(
        astra_audio_stream_wait(buffers->stream, deadline_ns));

    /* The host is done reading the buffer it gave back before we refill
     * it. */
    astra_memory_acquire_fence();
    return result;
}

AstraResult astra_pcm_buffers_close(AstraPcmBuffers *buffers)
{
    uint32_t status;

    if (!buffers_open(buffers))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* The stream first: the host stops before the memory goes. */
    status = astra_close(buffers->stream);
    if (astra_close(buffers->dma) != ASTRA_SYSCALL_OK &&
        status == ASTRA_SYSCALL_OK)
        status = ASTRA_SYSCALL_IO_ERROR;
    (void)memset(buffers, 0, sizeof(*buffers));
    return astra_result_from_syscall(status);
}
