#ifndef ASTRA_AUDIO_STREAM_H
#define ASTRA_AUDIO_STREAM_H

#include <astra/syscall.h>
#include <stdint.h>

/** @file audio_stream.h @brief An application's own PCM stream to the host.
 *
 * The audio data plane (docs/MEDIA_DATA_PLANE.md), modelled on Haiku's
 * BBufferGroup and BSoundPlayer: the application owns a group of equal
 * buffers in one of its DMA buffers, fills them in place and hands them to
 * the host in order. The host mixer takes each one as the output clock
 * needs it and gives it back. No server touches a buffer.
 *
 * Layout of the DMA buffer: an ::AstraAudioStreamHeader, then
 * `buffer_count` buffers of `buffer_bytes` each, back to back from
 * `buffer_offset`. Buffer `n` (counting from zero since open) lives at
 * index `n % buffer_count`.
 *
 * Two monotonic counters carry the whole exchange:
 *  - `queued`, written only by the application: buffers handed to the host.
 *  - `consumed`, written only by the host: buffers it has finished with.
 * `queued - consumed` is never more than `buffer_count`, and a buffer whose
 * number is below `consumed + buffer_count` and at or above `queued` is the
 * application's to fill. The host plays silence for this stream while
 * `queued == consumed`; it does not hold back the mix.
 *
 * ::ASTRA_SYSCALL_AUDIO_STREAM_WAIT tells the host about new buffers and
 * blocks until one is free: one call per buffer, and no reply from anyone.
 */

/** Native-big-endian `AAST` signature in ::AstraAudioStreamHeader::magic. */
#define ASTRA_AUDIO_STREAM_MAGIC UINT32_C(0x41415354) /* "AAST" */
/** Current stream header revision. */
#define ASTRA_AUDIO_STREAM_VERSION 1u
/** Byte size of ::AstraAudioStreamHeader, and so the first buffer's offset. */
#define ASTRA_AUDIO_STREAM_HEADER_SIZE 64u
/** Fewest buffers in a stream: one playing, one filling. */
#define ASTRA_AUDIO_STREAM_BUFFERS_MIN 2u
/** Most buffers in a stream. */
#define ASTRA_AUDIO_STREAM_BUFFERS_MAX 32u
/** Fewest frames in one buffer. */
#define ASTRA_AUDIO_STREAM_PERIOD_MIN 64u
/** Most frames in one buffer. */
#define ASTRA_AUDIO_STREAM_PERIOD_MAX 4096u

/** The shared header at the start of a stream's DMA buffer. The host
 * writes every field at open; afterwards the application writes only
 * `queued` and the host only `consumed` and `status`. Big-endian, as the
 * guest reads it.
 */
typedef struct AstraAudioStreamHeader {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t magic; /**< ::ASTRA_AUDIO_STREAM_MAGIC. */
    uint16_t version;          /**< ::ASTRA_AUDIO_STREAM_VERSION. */
    uint16_t header_size;      /**< ::ASTRA_AUDIO_STREAM_HEADER_SIZE. */
    uint32_t format;           /**< ASTRA_PCM_FORMAT() word (%pcm_format.h). */
    uint32_t period_frames;    /**< Frames in every buffer. */
    uint32_t buffer_count;     /**< Buffers in the group. */
    uint32_t buffer_bytes;     /**< Bytes from one buffer to the next. */
    uint32_t buffer_offset;    /**< Byte offset of buffer 0. */
    uint32_t total_size;       /**< Bytes of the DMA buffer the stream uses. */
    uint32_t stream_generation; /**< Nonzero; assigned at open. */
    volatile uint32_t queued;  /**< Application: buffers handed over. */
    uint32_t reserved0[2];     /**< Zero. */
    volatile uint32_t consumed; /**< Host: buffers finished with. */
    volatile uint32_t status;  /**< Host: ASTRA_SYSCALL_* stream status. */
    uint32_t reserved1[2];     /**< Zero. */
} AstraAudioStreamHeader;

/** Fixed byte size of ::AstraAudioStreamOpen. */
#define ASTRA_AUDIO_STREAM_OPEN_SIZE 32u
/** ::ASTRA_SYSCALL_AUDIO_STREAM_OPEN's in/out record. The caller fills
 * size, buffer, format, period_frames and buffer_count and zeroes the rest.
 */
typedef struct AstraAudioStreamOpen {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_AUDIO_STREAM_OPEN_SIZE. */
    uint32_t flags;            /**< Reserved; must be zero. */
    uint32_t buffer;           /**< The caller's DMA buffer handle. */
    uint32_t format;           /**< ASTRA_PCM_FORMAT() word. */
    uint32_t period_frames;    /**< Frames per buffer. */
    uint32_t buffer_count;     /**< Buffers in the group. */
    uint32_t stream;           /**< Zero on input; the stream handle on output. */
    uint32_t stream_generation; /**< Zero on input; the header's on output. */
} AstraAudioStreamOpen;

/** Bytes from one buffer to the next: the period rounded up to the ABI
 * alignment.
 * @param frame_bytes Bytes in one frame of the stream's format.
 * @param period_frames Frames per buffer.
 * @return The stride, or zero if it overflows.
 */
static inline uint32_t astra_audio_stream_buffer_bytes(uint32_t frame_bytes,
                                                       uint32_t period_frames)
{
    uint32_t bytes;

    if (frame_bytes == 0u || period_frames > UINT32_MAX / frame_bytes)
        return 0u;
    bytes = frame_bytes * period_frames;
    if (bytes > UINT32_MAX - (ASTRA_ABI_ALIGNMENT - 1u))
        return 0u;
    return (bytes + ASTRA_ABI_ALIGNMENT - 1u) &
           ~(uint32_t)(ASTRA_ABI_ALIGNMENT - 1u);
}

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraAudioStreamHeader) ==
                   ASTRA_AUDIO_STREAM_HEADER_SIZE,
               "audio stream header ABI changed");
_Static_assert(sizeof(AstraAudioStreamOpen) == ASTRA_AUDIO_STREAM_OPEN_SIZE,
               "audio stream open ABI changed");

/*
 * Kernel-to-platform stream lifecycle, built inside the kernel's platform
 * layer and written to HOST_ACCEL_STREAM_CONFIG; never built by user code.
 * The host validates it, writes the stream header, and from then on reads
 * buffers straight from guest memory until CLOSE, which it acknowledges
 * only once it has stopped -- so the kernel may then end the DMA pin.
 */
#define ASTRA_AUDIO_STREAM_CONFIG_VERSION 1u
#define ASTRA_AUDIO_STREAM_CONFIG_SIZE 64u
#define ASTRA_AUDIO_STREAM_CONFIG_OPEN  1u
#define ASTRA_AUDIO_STREAM_CONFIG_CLOSE 2u
typedef struct AstraAudioStreamConfig {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t operation;
    uint32_t slot;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t stream_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t format;
    uint32_t period_frames;
    uint32_t buffer_count;
    uint32_t reserved[5];
} AstraAudioStreamConfig;

_Static_assert(sizeof(AstraAudioStreamConfig) ==
                   ASTRA_AUDIO_STREAM_CONFIG_SIZE,
               "audio stream config ABI changed");
/** @endcond */

#endif
