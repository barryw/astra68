#ifndef ASTRA_PCM_FORMAT_H
#define ASTRA_PCM_FORMAT_H

#include <stdint.h>

/** @file pcm_format.h @brief Native PCM playback formats. */

/** Startup capability naming the native PCM media service. */
#define ASTRA_CAPABILITY_PCM "PCM"
/** Sink sample rate in frames per second. */
#define ASTRA_PCM_RATE 48000u
/** Number of interleaved channels. */
#define ASTRA_PCM_CHANNELS 2u
/** Signed 24-bit stereo bytes per frame. */
#define ASTRA_PCM_FRAME_BYTES 6u
/** Internal transport batch size; callers may write larger buffers. */
#define ASTRA_PCM_TRANSFER_FRAMES 1024u

/** Interleaved 48 kHz stereo signed-24-bit little-endian input. */
#define ASTRA_PCM_FORMAT_S24LE_STEREO UINT32_C(1)
/** Interleaved 48 kHz stereo signed-16-bit big-endian input (SDL2 S16SYS). */
#define ASTRA_PCM_FORMAT_S16BE_STEREO UINT32_C(2)
/** Maximum bytes occupied by one supported input frame. */
#define ASTRA_PCM_MAX_FRAME_BYTES ASTRA_PCM_FRAME_BYTES

/** Bytes in one interleaved input frame, or zero for an unsupported format. */
static inline uint32_t astra_pcm_format_frame_bytes(uint32_t format)
{
    if (format == ASTRA_PCM_FORMAT_S24LE_STEREO)
        return 6u;
    if (format == ASTRA_PCM_FORMAT_S16BE_STEREO)
        return 4u;
    return 0u;
}

#endif
