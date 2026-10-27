#ifndef ASTRA_CODEC_H
#define ASTRA_CODEC_H

/** @file codec.h @brief Native decoded audio and video streams. */
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/result.h>
#include <astra/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASTRA_CODEC_MEDIA_AUDIO = 1u,
    ASTRA_CODEC_MEDIA_VIDEO = 2u,
    ASTRA_CODEC_AUDIO_U8 = 1u,
    ASTRA_CODEC_AUDIO_S16LE = 2u,
    ASTRA_CODEC_AUDIO_S24LE = 3u,
    ASTRA_CODEC_AUDIO_S32LE = 4u,
    ASTRA_CODEC_VIDEO_YUV420P = 16u
};

/** Read up to capacity source bytes at absolute offset. Return zero at EOF. */
typedef AstraResult (*AstraCodecReadAt)(void *context, uint64_t offset,
                                       void *output, uint32_t capacity,
                                       uint32_t *received);

/** Negotiated decoded sample or frame format. Unused fields are zero. */
typedef struct AstraCodecFormat {
    uint32_t media_kind;     /**< ASTRA_CODEC_MEDIA_* value. */
    uint32_t encoding;       /**< ASTRA_CODEC_AUDIO_* or ASTRA_CODEC_VIDEO_* value. */
    uint32_t sample_rate;    /**< Audio samples per second. */
    uint32_t channels;       /**< Interleaved audio channel count. */
    uint32_t width;          /**< Video luma width in pixels. */
    uint32_t height;         /**< Video luma height in pixels. */
    uint32_t stride;         /**< Video luma row bytes. */
    uint32_t frame_rate_num; /**< Video frame rate numerator. */
    uint32_t frame_rate_den; /**< Video frame rate denominator. */
} AstraCodecFormat;

/** One bounded decoded output chunk. Video frames may span several chunks. */
typedef struct AstraCodecChunk {
    uint32_t bytes;          /**< Valid output bytes copied by this read. */
    uint32_t frame_bytes;    /**< Total bytes in this frame, or audio sample bytes. */
    uint32_t frame_offset;   /**< Byte offset of this chunk within video frame. */
    uint64_t timestamp_ns;   /**< Presentation timestamp in nanoseconds. */
    uint64_t duration_ns;    /**< Chunk or frame duration in nanoseconds. */
    int end;                 /**< Nonzero when stream ended after this chunk. */
} AstraCodecChunk;

/** One isolated provider session. Initialize with ASTRA_CODEC_STREAM_INIT. */
typedef struct AstraCodecStream {
    AstraHandle control;         /**< Private service request port. */
    AstraHandle reply;           /**< Private service reply port. */
    AstraHandle area;            /**< Private transfer area. */
    void *mapped;                /**< Private mapped transfer bytes. */
    AstraCodecReadAt read_at;    /**< Caller source callback. */
    void *source_context;        /**< Caller source context. */
    uint32_t transaction;        /**< Private request sequence. */
} AstraCodecStream;

/** Zero initializer for a stream before astra_codec_open(). */
#define ASTRA_CODEC_STREAM_INIT {0u, 0u, 0u, NULL, NULL, NULL, 0u}

/** Probe and open source by content; source length must be known.
 * @param service CODEC startup capability handle.
 * @param read_at Callback for absolute source reads.
 * @param source_context Context passed to read_at.
 * @param source_length Exact encoded source length in bytes.
 * @param stream Zero-initialized stream receiving session state.
 * @param format Receives negotiated decoded format.
 * @return ASTRA_OK on success, otherwise source, format, or service error.
 */
ASTRA_NODISCARD AstraResult astra_codec_open(AstraHandle service,
    AstraCodecReadAt read_at, void *source_context, uint64_t source_length,
    AstraCodecStream *stream, AstraCodecFormat *format);

/** Decode one bounded chunk.
 * @param stream Open stream.
 * @param output Buffer receiving decoded bytes.
 * @param capacity Output buffer length in bytes.
 * @param chunk Receives byte count, timing, and frame progress.
 * @return ASTRA_OK on success, otherwise stream or provider error.
 */
ASTRA_NODISCARD AstraResult astra_codec_read(AstraCodecStream *stream,
    void *output, uint32_t capacity, AstraCodecChunk *chunk);

/** Seek by presentation time in nanoseconds.
 * @param stream Open stream.
 * @param timestamp_ns Requested presentation time.
 * @return ASTRA_OK on success, otherwise seek or provider error.
 */
ASTRA_NODISCARD AstraResult astra_codec_seek(AstraCodecStream *stream,
    uint64_t timestamp_ns);

/** Close stream and release resources, including on service failure.
 * @param stream Open stream to close and clear.
 * @return ASTRA_OK on orderly close, otherwise service error after cleanup.
 */
ASTRA_NODISCARD AstraResult astra_codec_close(AstraCodecStream *stream);

#ifdef __cplusplus
}
#endif
#endif
