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

/** A stream accepts one explicitly selected 48 kHz stereo input format. */
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
} AstraPcmStream;

/** Empty stream initializer. */
#define ASTRA_PCM_STREAM_INIT {0u, 0u, 0u, NULL, 0u, 0u}

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
 * @param format ASTRA_PCM_FORMAT_S24LE_STEREO or
 * ASTRA_PCM_FORMAT_S16BE_STEREO. SDL2 can advertise the latter as its
 * 48 kHz AUDIO_S16MSB playback device; SDL2 handles other app formats.
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

/** Set linear gain: 65536 is unity and zero is silent.
 * @param stream Open PCM stream.
 * @param gain_q16 Unsigned Q16 linear gain.
 * @return ASTRA_OK on success, otherwise an error.
 */
ASTRA_NODISCARD AstraResult astra_pcm_gain(AstraPcmStream *stream,
                                           uint32_t gain_q16);
/** Pause or resume playback without discarding queued frames. */
ASTRA_NODISCARD AstraResult astra_pcm_pause(AstraPcmStream *stream,
                                            int paused);
/** Discard queued frames; already-submitted hardware frames may still play. */
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

#ifdef __cplusplus
}
#endif

#endif
