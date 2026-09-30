#ifndef ASTRA_PCM_FORMAT_H
#define ASTRA_PCM_FORMAT_H

#include <stdint.h>

/** @file pcm_format.h @brief Native PCM playback formats.
 *
 * A format word names everything the host needs to play a stream: the
 * sample encoding, the channel count and the frame rate. The Linux audio
 * host converts and resamples every stream to its one 48 kHz stereo mix, so
 * a client submits samples exactly as it has them and the MC68040 does no
 * conversion. The word travels unchanged from pcm.library through the media
 * service and the AstraHost audio channel to the host.
 *
 * Bits 0-7 are the encoding, bits 8-11 the channel count, bits 12-31 the
 * rate in frames per second.
 */

/** Startup capability naming the native PCM media service. */
#define ASTRA_CAPABILITY_PCM "PCM"
/** Sink sample rate in frames per second; the host mixes at this rate. */
#define ASTRA_PCM_RATE 48000u
/** Lowest and highest accepted stream rates in frames per second. */
#define ASTRA_PCM_RATE_MIN 8000u
#define ASTRA_PCM_RATE_MAX 192000u
/** Highest accepted channel count; mono is played on both sink channels. */
#define ASTRA_PCM_CHANNELS_MAX 2u
/** Largest frame of any accepted format: 32-bit stereo. */
#define ASTRA_PCM_MAX_FRAME_BYTES 8u
/** Internal transport batch size; callers may write larger buffers. */
#define ASTRA_PCM_TRANSFER_FRAMES 1024u

/** Sample encodings: every SDL2 sample format, plus packed 24-bit. */
#define ASTRA_PCM_ENCODING_S24LE 1u /**< signed 24-bit little-endian, packed */
#define ASTRA_PCM_ENCODING_S16BE 2u /**< signed 16-bit big-endian */
#define ASTRA_PCM_ENCODING_U8 3u    /**< unsigned 8-bit */
#define ASTRA_PCM_ENCODING_S8 4u    /**< signed 8-bit */
#define ASTRA_PCM_ENCODING_S16LE 5u /**< signed 16-bit little-endian */
#define ASTRA_PCM_ENCODING_U16LE 6u /**< unsigned 16-bit little-endian */
#define ASTRA_PCM_ENCODING_U16BE 7u /**< unsigned 16-bit big-endian */
#define ASTRA_PCM_ENCODING_S32LE 8u /**< signed 32-bit little-endian */
#define ASTRA_PCM_ENCODING_S32BE 9u /**< signed 32-bit big-endian */
#define ASTRA_PCM_ENCODING_F32LE 10u /**< IEEE-754 single, little-endian */
#define ASTRA_PCM_ENCODING_F32BE 11u /**< IEEE-754 single, big-endian */

/** The format word for @p encoding, @p channels and @p rate. */
#define ASTRA_PCM_FORMAT(encoding, channels, rate) \
    ((uint32_t)(encoding) | ((uint32_t)(channels) << 8) | \
     ((uint32_t)(rate) << 12))

/** Interleaved 48 kHz stereo signed-24-bit little-endian input. */
#define ASTRA_PCM_FORMAT_S24LE_STEREO \
    ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S24LE, 2u, ASTRA_PCM_RATE)
/** Interleaved 48 kHz stereo signed-16-bit big-endian input. */
#define ASTRA_PCM_FORMAT_S16BE_STEREO \
    ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 2u, ASTRA_PCM_RATE)

static inline uint32_t astra_pcm_format_encoding(uint32_t format)
{
    return format & 0xffu;
}

static inline uint32_t astra_pcm_format_channels(uint32_t format)
{
    return (format >> 8) & 0xfu;
}

static inline uint32_t astra_pcm_format_rate(uint32_t format)
{
    return format >> 12;
}

/** Bytes in one sample of @p encoding, or zero for an unknown encoding. */
static inline uint32_t astra_pcm_encoding_bytes(uint32_t encoding)
{
    switch (encoding) {
    case ASTRA_PCM_ENCODING_U8:
    case ASTRA_PCM_ENCODING_S8:
        return 1u;
    case ASTRA_PCM_ENCODING_S16BE:
    case ASTRA_PCM_ENCODING_S16LE:
    case ASTRA_PCM_ENCODING_U16LE:
    case ASTRA_PCM_ENCODING_U16BE:
        return 2u;
    case ASTRA_PCM_ENCODING_S24LE:
        return 3u;
    case ASTRA_PCM_ENCODING_S32LE:
    case ASTRA_PCM_ENCODING_S32BE:
    case ASTRA_PCM_ENCODING_F32LE:
    case ASTRA_PCM_ENCODING_F32BE:
        return 4u;
    default:
        return 0u;
    }
}

/** Bytes in one interleaved frame, or zero for an unsupported format. */
static inline uint32_t astra_pcm_format_frame_bytes(uint32_t format)
{
    uint32_t channels = astra_pcm_format_channels(format);
    uint32_t rate = astra_pcm_format_rate(format);

    if (channels == 0u || channels > ASTRA_PCM_CHANNELS_MAX ||
        rate < ASTRA_PCM_RATE_MIN || rate > ASTRA_PCM_RATE_MAX)
        return 0u;
    return astra_pcm_encoding_bytes(astra_pcm_format_encoding(format)) *
           channels;
}

#endif
