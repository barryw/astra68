#ifndef ASTRA_PCM_H
#define ASTRA_PCM_H

/** @file pcm.h @brief Native PCM stream playback through the media service. */

#include <astra/attributes.h>
#include <astra/pcm_format.h>
#include <astra/resource.h>
#include <astra/result.h>
#include <astra/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned stream; initialize with ::ASTRA_PCM_STREAM_INIT. */
typedef struct AstraPcmStream {
    /** Private control endpoint. */
    AstraHandle control;
    /** Private reply endpoint. */
    AstraHandle reply;
    /** Private shared transfer area. */
    AstraHandle area;
    /** Private mapped transfer bytes. */
    void *mapped;
    /** Private request sequence. */
    uint32_t transaction;
    /** Private selected frame width. */
    uint32_t frame_bytes;
    /** Private selected format word. */
    uint32_t format;
} AstraPcmStream;

/** Empty stream initializer. */
#define ASTRA_PCM_STREAM_INIT {0u, 0u, 0u, NULL, 0u, 0u, 0u}

/** Queue and hardware state sampled at one instant. */
typedef struct AstraPcmStatus {
    /** Source frames still queued in this voice. */
    uint32_t queued_frames;
    /** Frames currently queued by the physical sink. */
    uint32_t hardware_frames;
    /** Hardware FIFO underrun count. */
    uint32_t underruns;
    /** Hardware FIFO overflow count. */
    uint32_t overflows;
    /** Software delivery gap count. */
    uint32_t software_gaps;
} AstraPcmStatus;

/** Open one playback stream on an AUDIO/PCM service capability.
 * Each stream has an isolated service session, revoked on client death.
 * @param service Startup `PCM` service capability.
 * @param format A format word from ASTRA_PCM_FORMAT(): any encoding in
 * %pcm_format.h, one or two channels, ASTRA_PCM_RATE_MIN to
 * ASTRA_PCM_RATE_MAX frames per second. The Linux audio host converts and
 * resamples it to the sink, so submit samples as they are; SDL2's device
 * takes each application's own format this way.
 * @param stream Empty caller-owned stream initialized with
 * ASTRA_PCM_STREAM_INIT.
 * @return ASTRA_OK on success, otherwise an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_open(AstraHandle service,
                                           uint32_t format,
                                           AstraPcmStream *stream);

/** Queue any number of complete frames, batched internally. The service
 * copies accepted frames before returning. A full queue returns
 * ASTRA_ERROR_BUSY and reports the prefix accepted in @p accepted; retry
 * only the remainder. Other failures may also follow a nonzero prefix.
 * @param stream Open PCM stream.
 * @param frames Interleaved source frames.
 * @param frame_count Number of complete frames.
 * @param accepted Receives the accepted prefix length in frames.
 * @return ASTRA_OK if all frames were accepted, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_write(AstraPcmStream *stream,
                                            const void *frames,
                                            uint32_t frame_count,
                                            uint32_t *accepted);

/** Wait until the stream's queue has room for @p frame_count frames.
 * Sleeps for as long as the host needs to play the frames in the way, at
 * the stream's own rate, rather than polling; pair it with
 * astra_pcm_write() for blocking playback. A paused stream does not drain,
 * so waiting on one returns only after it is resumed.
 * @param stream Open PCM stream.
 * @param frame_count 1 to ::ASTRA_PCM_QUEUE_FRAMES.
 * @return ASTRA_OK once there is room, otherwise the status error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_wait(AstraPcmStream *stream,
                                           uint32_t frame_count);

/** Set linear gain: 65536 is unity and zero is silent.
 * @param stream Open PCM stream.
 * @param gain_q16 Unsigned Q16 linear gain.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_gain(AstraPcmStream *stream,
                                           uint32_t gain_q16);
/** Pause or resume playback without discarding queued frames.
 * @param stream Open PCM stream.
 * @param paused Nonzero to pause, zero to resume.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_pause(AstraPcmStream *stream,
                                            int paused);
/** Discard queued frames; already-submitted hardware frames may still play.
 * @param stream Open PCM stream.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_clear(AstraPcmStream *stream);
/** Read queue and sink counters.
 * @param stream Open PCM stream.
 * @param status Receives a point-in-time status snapshot.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_status(AstraPcmStream *stream,
                                             AstraPcmStatus *status);
/** Declare that no further frames will be queued; queued frames play.
 * @param stream Open PCM stream.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_finish(AstraPcmStream *stream);
/** Release a stream immediately, discarding any unplayed frames.
 * @param stream Open PCM stream.
 * @return ASTRA_OK on success, otherwise an error. The local resources are
 * released even if the service has died.
 */
ASTRA_NODISCARD AstraResult astra_pcm_close(AstraPcmStream *stream);

/** Convert PCM between formats and rates on the host (pcm.library 2.2).
 * The Linux audio host decodes, resamples with the mixer's windowed sinc
 * and encodes; the MC68040 only moves bytes. The result is exactly
 * floor(@p source_frames * target rate / source rate) frames, with silence
 * assumed before the first source frame and after the last.
 * @param service PCM service capability, as for astra_pcm_open().
 * @param source_format Format word of @p source.
 * @param source Interleaved source frames.
 * @param source_frames Number of source frames; zero gives zero frames.
 * @param target_format Format word of @p target.
 * @param target Receives interleaved target frames.
 * @param target_capacity Room in @p target, in frames.
 * @param target_frames Receives the number of frames written.
 * @return ASTRA_OK on success; ASTRA_ERROR_BUFFER_TOO_SMALL before any
 * work when @p target cannot hold the result; otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_convert(AstraHandle service,
                                              uint32_t source_format,
                                              const void *source,
                                              uint32_t source_frames,
                                              uint32_t target_format,
                                              void *target,
                                              uint32_t target_capacity,
                                              uint32_t *target_frames);

/** An application's own group of playback buffers (pcm.library 2.5).
 *
 * The way to play a steady stream: Haiku's BBufferGroup and BSoundPlayer.
 * The buffers are shared with the host, so the application fills them in
 * place and the host mixer takes each as the output clock needs it; the
 * media service only grants the stream and is not involved again. One
 * system call per buffer (astra_pcm_buffers_wait()), no reply, no copy on
 * the MC68040. Initialize with ::ASTRA_PCM_BUFFERS_INIT; every field is
 * private.
 *
 * The loop: astra_pcm_buffers_get() names the buffer to fill,
 * astra_pcm_buffers_queue() hands it over, astra_pcm_buffers_wait()
 * sleeps until another is free.
 */
typedef struct AstraPcmBuffers {
    /** Private kernel audio stream. */
    AstraHandle stream;
    /** Private DMA buffer holding the header and the buffers. */
    AstraHandle dma;
    /** Private shared stream header. */
    void *header;
    /** Private first buffer. */
    uint8_t *first;
    /** Private bytes from one buffer to the next. */
    uint32_t stride;
    /** Private frames per buffer. */
    uint32_t period_frames;
    /** Private number of buffers. */
    uint32_t count;
    /** Private buffers handed over since open. */
    uint32_t queued;
    /** Private format word. */
    uint32_t format;
} AstraPcmBuffers;

/** Empty buffer group initializer. */
#define ASTRA_PCM_BUFFERS_INIT {0u, 0u, NULL, NULL, 0u, 0u, 0u, 0u, 0u}

/** Open a buffer group: @p count buffers of @p period_frames frames.
 * Haiku's sizing is max(3, latency / period + 2) buffers of about 10 ms.
 * @param service Startup `PCM` service capability.
 * @param format A format word, as for astra_pcm_open().
 * @param period_frames Frames per buffer, 64 to 4096.
 * @param count Buffers, 2 to 32.
 * @param buffers Empty group initialized with ::ASTRA_PCM_BUFFERS_INIT.
 * @return ASTRA_OK; ASTRA_ERROR_UNSUPPORTED when the host has no streams
 * (use astra_pcm_open()); otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_buffers_open(AstraHandle service,
                                                   uint32_t format,
                                                   uint32_t period_frames,
                                                   uint32_t count,
                                                   AstraPcmBuffers *buffers);
/** The next buffer to fill: period_frames frames of the group's format.
 * It is free once astra_pcm_buffers_wait() has returned ASTRA_OK since the
 * last astra_pcm_buffers_queue(), and at first.
 * @param buffers Open group.
 * @return The buffer, or NULL for a group that is not open.
 */
void *astra_pcm_buffers_get(AstraPcmBuffers *buffers);
/** Hand the buffer from astra_pcm_buffers_get() to the host. Returns at
 * once; the host learns of it at the next astra_pcm_buffers_wait().
 * @param buffers Open group.
 * @return ASTRA_OK, or ASTRA_ERROR_BUSY when no buffer was free.
 */
ASTRA_NODISCARD AstraResult astra_pcm_buffers_queue(AstraPcmBuffers *buffers);
/** Hand the host every queued buffer, then sleep until one is free.
 * @param buffers Open group.
 * @param deadline_ns Absolute monotonic deadline, or
 * ::ASTRA_DEADLINE_FOREVER.
 * @return ASTRA_OK when a buffer is free, ASTRA_ERROR_TIMEOUT, or
 * ASTRA_ERROR_PEER_DEAD once the host is gone.
 */
ASTRA_NODISCARD AstraResult astra_pcm_buffers_wait(AstraPcmBuffers *buffers,
                                                   uint64_t deadline_ns);
/** Stop playback at once and release the group.
 * @param buffers Open group.
 * @return ASTRA_OK, otherwise an error; the group is released regardless.
 */
ASTRA_NODISCARD AstraResult astra_pcm_buffers_close(AstraPcmBuffers *buffers);

#ifdef __cplusplus
}
#endif

#endif
