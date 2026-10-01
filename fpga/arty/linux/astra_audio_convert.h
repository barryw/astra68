#ifndef ASTRA_AUDIO_CONVERT_H
#define ASTRA_AUDIO_CONVERT_H

/*
 * Host-side PCM decoding, encoding and resampling: the one implementation
 * behind the audio daemon's mixer voices and its converters, and behind
 * the QEMU gates' stand-in daemon (loaded there as a shared object).
 */

#include <stdint.h>

enum {
    /* A Kaiser-windowed sinc, ZERO_CROSSINGS wide on each side at the
     * cutoff, tabulated at FILTER_PHASES fractional positions and
     * interpolated between them. The cutoff sits a little below the lower
     * of the two Nyquist rates, so upsampling removes images and
     * downsampling removes what the target cannot carry. */
    ASTRA_AUDIO_ZERO_CROSSINGS = 12u,
    ASTRA_AUDIO_FILTER_PHASES = 256u,
    /* Source frames one converter holds that it has not yet turned into
     * output: about 22 s at 48 kHz. A writer that never reads is refused. */
    ASTRA_AUDIO_CONVERTER_FRAMES_MAX = 1u << 20
};

/* One sample of @p encoding at 24-bit scale. Integer encodings come out
 * exact; float ones may exceed full scale. */
float astra_audio_decode_sample(uint32_t encoding, const uint8_t *data);

/* @p value at 24-bit scale, rounded and saturated into @p encoding. */
void astra_audio_encode_sample(uint32_t encoding, double value,
                               uint8_t *data);

/* The polyphase table for converting @p in_rate to @p out_rate:
 * (ASTRA_AUDIO_FILTER_PHASES + 1) rows of *@p taps_out taps. Row p holds the
 * taps for a read position p / FILTER_PHASES of a frame past the current
 * one; tap j weighs the frame j - (taps / 2 - 1) away. Each row has unity
 * gain at DC. NULL when out of memory. */
float *astra_audio_make_filter(uint32_t in_rate, uint32_t out_rate,
                               uint32_t *taps_out);

typedef struct AstraAudioConverter AstraAudioConverter;

/* A converter from one PCM format word to another (<astra/pcm_format.h>).
 * NULL when either format is invalid or memory is short. */
AstraAudioConverter *astra_audio_converter_open(uint32_t source,
                                                uint32_t target);
void astra_audio_converter_close(AstraAudioConverter *converter);

/* Queues whole source frames. ASTRA_STATUS_INVALID for a partial frame or a
 * write after the end, ASTRA_STATUS_BUSY when the converter would hold more
 * than ASTRA_AUDIO_CONVERTER_FRAMES_MAX unconverted frames. */
uint32_t astra_audio_converter_write(AstraAudioConverter *converter,
                                     const uint8_t *data, uint32_t bytes);

/* No more source: what follows the last frame is silence, and the output
 * is exactly floor(source frames * target rate / source rate) frames. */
void astra_audio_converter_end(AstraAudioConverter *converter);

/* Target frames that can be read now. */
uint32_t astra_audio_converter_ready(const AstraAudioConverter *converter);

/* Converts up to @p capacity bytes of whole target frames into @p data and
 * returns the bytes written. */
uint32_t astra_audio_converter_read(AstraAudioConverter *converter,
                                    uint8_t *data, uint32_t capacity);

#endif
