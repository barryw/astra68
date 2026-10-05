#define _DEFAULT_SOURCE

#include <astra/audio_stream.h>
#include <astra/pcm.h>
#include <astra/pcm_service.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "../src/pcm_session.h"

/* The application's DMA buffer, where the kernel would map it: below 4 GiB,
 * since its address travels as a 32-bit virtual_base. A Linux host test;
 * macOS keeps the low 4 GiB unmapped. */
static uint8_t *memory;
static uint32_t grant_status;
static uint32_t dma_bytes;
static uint32_t closed[8];
static uint32_t closes;
static uint32_t waited_stream;
static uint64_t waited_deadline;
static AstraAudioStreamOpen opened;

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    assert(messages == 1u && bytes == sizeof(AstraPcmReply));
    *receive = 10u;
    *send = 11u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t handle, const void *message,
                         uint32_t size, const uint32_t *handles,
                         uint32_t count)
{
    const AstraPcmRequest *request = message;

    assert(handle == 99u && size == sizeof(*request));
    assert(request->header.operation == ASTRA_PCM_STREAM_GRANT &&
           request->frames == 0u && request->value == 0u);
    assert(count == 1u && handles[0] == 11u);
    return ASTRA_SYSCALL_OK;
}

AstraResult astra_pcm_reply_receive(uint32_t reply_port,
                                    uint32_t transaction,
                                    AstraPcmReply *reply,
                                    uint32_t *received)
{
    assert(reply_port == 10u && transaction == 1u && received != NULL);
    memset(reply, 0, sizeof(*reply));
    *received = grant_status == ASTRA_STATUS_OK ? 40u : 0u;
    return astra_result_from_service(grant_status);
}

uint32_t astra_close(uint32_t handle)
{
    assert(handle != 0u && closes < 8u);
    closed[closes++] = handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info)
{
    dma_bytes = byte_size;
    memset(info, 0, sizeof(*info));
    info->size = ASTRA_DMA_BUFFER_INFO_SIZE;
    info->handle = 60u;
    info->virtual_base = (uint32_t)(uintptr_t)memory;
    info->byte_size = 4096u * 4u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_audio_stream_open(uint32_t device,
                                 AstraAudioStreamOpen *request)
{
    assert(device == 40u && request->size == sizeof(*request) &&
           request->buffer == 60u && request->stream == 0u);
    opened = *request;
    request->stream = 50u;
    request->stream_generation = 7u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_audio_stream_wait(uint32_t stream, uint64_t deadline_ns)
{
    waited_stream = stream;
    waited_deadline = deadline_ns;
    return ASTRA_SYSCALL_TIMED_OUT;
}

int main(void)
{
    const uint32_t format = ASTRA_PCM_FORMAT_S16BE_STEREO;
    AstraPcmBuffers buffers = ASTRA_PCM_BUFFERS_INIT;
    volatile AstraAudioStreamHeader *header;
    uint8_t *first;

#ifndef MAP_32BIT
#define MAP_32BIT 0 /* aarch64 Linux honours the hint below */
#endif
    memory = mmap((void *)(uintptr_t)0x20000000u, 4096u * 4u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (memory == MAP_FAILED ||
        (uintptr_t)memory != (uint32_t)(uintptr_t)memory) {
        puts("test_pcm_buffers: SKIP (this host maps nothing below 4 GiB)");
        return 0;
    }

    /* Out of range: nothing is asked of the service. */
    assert(astra_pcm_buffers_open(99u, format, 32u, 3u, &buffers) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_pcm_buffers_open(99u, format, 480u, 1u, &buffers) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_pcm_buffers_open(99u, 0u, 480u, 3u, &buffers) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    /* A host without streams: the caller falls back. */
    grant_status = ASTRA_STATUS_UNSUPPORTED;
    assert(astra_pcm_buffers_open(99u, format, 480u, 3u, &buffers) ==
           ASTRA_ERROR_UNSUPPORTED);
    assert(dma_bytes == 0u && closes == 1u && closed[0] == 10u);
    assert(astra_pcm_buffers_get(&buffers) == NULL);

    grant_status = ASTRA_STATUS_OK;
    closes = 0u;
    assert(astra_pcm_buffers_open(99u, format, 480u, 3u, &buffers) ==
           ASTRA_OK);
    assert(dma_bytes == ASTRA_AUDIO_STREAM_HEADER_SIZE + 3u * 1920u);
    assert(opened.format == format && opened.period_frames == 480u &&
           opened.buffer_count == 3u);
    /* The reply port and the grant go; the stream keeps what it needs. */
    assert(closes == 2u && closed[0] == 10u && closed[1] == 40u);
    header = (volatile AstraAudioStreamHeader *)(void *)memory;
    first = memory + ASTRA_AUDIO_STREAM_HEADER_SIZE;

    /* Fill in place, hand over, in order, until the host has them all. */
    for (uint32_t index = 0u; index < 3u; ++index) {
        assert(astra_pcm_buffers_get(&buffers) == first + index * 1920u);
        assert(astra_pcm_buffers_queue(&buffers) == ASTRA_OK);
        assert(header->queued == index + 1u);
    }
    assert(astra_pcm_buffers_queue(&buffers) == ASTRA_ERROR_BUSY);
    assert(header->queued == 3u);
    header->consumed = 1u;
    assert(astra_pcm_buffers_get(&buffers) == first);
    assert(astra_pcm_buffers_queue(&buffers) == ASTRA_OK);
    assert(header->queued == 4u);

    assert(astra_pcm_buffers_wait(&buffers, 1234u) == ASTRA_ERROR_TIMEOUT);
    assert(waited_stream == 50u && waited_deadline == 1234u);

    /* The stream before the memory under it. */
    closes = 0u;
    assert(astra_pcm_buffers_close(&buffers) == ASTRA_OK);
    assert(closes == 2u && closed[0] == 50u && closed[1] == 60u);
    assert(astra_pcm_buffers_get(&buffers) == NULL);
    assert(astra_pcm_buffers_close(&buffers) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    return 0;
}
