// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_audio_convert.h"

#include <astra/pcm_format.h>
#include <astra/status.h>

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_ROLLOFF 0.95
#define KAISER_BETA 8.6

static uint32_t load_le(const uint8_t *data, uint32_t bytes)
{
    uint32_t value = 0u;

    for (uint32_t at = bytes; at-- > 0u;)
        value = (value << 8) | data[at];
    return value;
}

static uint32_t load_be(const uint8_t *data, uint32_t bytes)
{
    uint32_t value = 0u;

    for (uint32_t at = 0u; at < bytes; ++at)
        value = (value << 8) | data[at];
    return value;
}

static void store_le(uint8_t *data, uint32_t value, uint32_t bytes)
{
    for (uint32_t at = 0u; at < bytes; ++at, value >>= 8)
        data[at] = (uint8_t)value;
}

static void store_be(uint8_t *data, uint32_t value, uint32_t bytes)
{
    for (uint32_t at = bytes; at-- > 0u; value >>= 8)
        data[at] = (uint8_t)value;
}

static float as_float(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float astra_audio_decode_sample(uint32_t encoding, const uint8_t *data)
{
    switch (encoding) {
    case ASTRA_PCM_ENCODING_U8:
        return (float)((int32_t)data[0] - 128) * 65536.0f;
    case ASTRA_PCM_ENCODING_S8:
        return (float)(int8_t)data[0] * 65536.0f;
    case ASTRA_PCM_ENCODING_S16BE:
        return (float)(int16_t)load_be(data, 2u) * 256.0f;
    case ASTRA_PCM_ENCODING_S16LE:
        return (float)(int16_t)load_le(data, 2u) * 256.0f;
    case ASTRA_PCM_ENCODING_U16BE:
        return (float)((int32_t)load_be(data, 2u) - 32768) * 256.0f;
    case ASTRA_PCM_ENCODING_U16LE:
        return (float)((int32_t)load_le(data, 2u) - 32768) * 256.0f;
    case ASTRA_PCM_ENCODING_S24LE:
        return (float)((int32_t)(load_le(data, 3u) ^ UINT32_C(0x800000)) -
                       INT32_C(0x800000));
    case ASTRA_PCM_ENCODING_S32BE:
        return (float)((double)(int32_t)load_be(data, 4u) / 256.0);
    case ASTRA_PCM_ENCODING_S32LE:
        return (float)((double)(int32_t)load_le(data, 4u) / 256.0);
    case ASTRA_PCM_ENCODING_F32BE:
        return as_float(load_be(data, 4u)) * 8388608.0f;
    case ASTRA_PCM_ENCODING_F32LE:
        return as_float(load_le(data, 4u)) * 8388608.0f;
    default:
        return 0.0f;
    }
}

/* @p value scaled down by @p divisor, rounded to nearest and clamped to the
 * signed range of @p bits. */
static int32_t quantise(double value, double divisor, uint32_t bits)
{
    double limit = ldexp(1.0, (int)bits - 1);
    double whole = nearbyint(value / divisor);

    if (whole > limit - 1.0)
        return (int32_t)(limit - 1.0);
    if (whole < -limit)
        return (int32_t)-limit;
    return (int32_t)whole;
}

void astra_audio_encode_sample(uint32_t encoding, double value, uint8_t *data)
{
    switch (encoding) {
    case ASTRA_PCM_ENCODING_U8:
        data[0] = (uint8_t)(quantise(value, 65536.0, 8u) + 128);
        break;
    case ASTRA_PCM_ENCODING_S8:
        data[0] = (uint8_t)quantise(value, 65536.0, 8u);
        break;
    case ASTRA_PCM_ENCODING_S16BE:
        store_be(data, (uint32_t)quantise(value, 256.0, 16u), 2u);
        break;
    case ASTRA_PCM_ENCODING_S16LE:
        store_le(data, (uint32_t)quantise(value, 256.0, 16u), 2u);
        break;
    case ASTRA_PCM_ENCODING_U16BE:
        store_be(data, (uint32_t)(quantise(value, 256.0, 16u) + 32768), 2u);
        break;
    case ASTRA_PCM_ENCODING_U16LE:
        store_le(data, (uint32_t)(quantise(value, 256.0, 16u) + 32768), 2u);
        break;
    case ASTRA_PCM_ENCODING_S24LE:
        store_le(data, (uint32_t)quantise(value, 1.0, 24u), 3u);
        break;
    case ASTRA_PCM_ENCODING_S32BE:
        store_be(data, (uint32_t)quantise(value, 1.0 / 256.0, 32u), 4u);
        break;
    case ASTRA_PCM_ENCODING_S32LE:
        store_le(data, (uint32_t)quantise(value, 1.0 / 256.0, 32u), 4u);
        break;
    case ASTRA_PCM_ENCODING_F32BE:
        store_be(data, float_bits((float)(value / 8388608.0)), 4u);
        break;
    case ASTRA_PCM_ENCODING_F32LE:
        store_le(data, float_bits((float)(value / 8388608.0)), 4u);
        break;
    default:
        break;
    }
}

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;

    for (unsigned k = 1u; k < 64u && term > sum * 1e-12; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

float *astra_audio_make_filter(uint32_t in_rate, uint32_t out_rate,
                               uint32_t *taps_out)
{
    double cutoff = FILTER_ROLLOFF *
                    (in_rate > out_rate ? (double)out_rate / in_rate : 1.0);
    uint32_t half = (uint32_t)ceil(ASTRA_AUDIO_ZERO_CROSSINGS / cutoff);
    uint32_t taps = 2u * half;
    float *filter = malloc((ASTRA_AUDIO_FILTER_PHASES + 1u) * taps *
                           sizeof(*filter));
    double norm = bessel_i0(KAISER_BETA);

    if (filter == NULL)
        return NULL;
    for (uint32_t phase = 0u; phase <= ASTRA_AUDIO_FILTER_PHASES; ++phase) {
        float *row = filter + phase * taps;
        double sum = 0.0;

        for (uint32_t tap = 0u; tap < taps; ++tap) {
            double t = (double)tap - (half - 1u) -
                       (double)phase / ASTRA_AUDIO_FILTER_PHASES;
            double x = M_PI * cutoff * t;
            double edge = t / half;
            double window = fabs(edge) >= 1.0 ? 0.0 :
                bessel_i0(KAISER_BETA * sqrt(1.0 - edge * edge)) / norm;
            double value = (x == 0.0 ? 1.0 : sin(x) / x) * window;

            row[tap] = (float)value;
            sum += value;
        }
        for (uint32_t tap = 0u; tap < taps; ++tap)
            row[tap] = (float)(row[tap] / sum);
    }
    *taps_out = taps;
    return filter;
}

/*
 * Building a table costs thousands of Bessel evaluations -- 61 ms on the
 * DE25's A76 for Doom's 56 effects, each a new converter at the same two
 * rates. Distinct rate pairs are few in practice; the bound keeps a guest
 * that invents rates from growing it without limit. Locked because the
 * gates' stand-in daemon converts on several threads.
 */
#define FILTER_CACHE_ENTRIES 32u

static struct {
    uint32_t in_rate;
    uint32_t out_rate;
    uint32_t taps;
    float *filter;
} filter_cache[FILTER_CACHE_ENTRIES];
static pthread_mutex_t filter_cache_lock = PTHREAD_MUTEX_INITIALIZER;

const float *astra_audio_filter(uint32_t in_rate, uint32_t out_rate,
                                uint32_t *taps_out)
{
    const float *found = NULL;

    (void)pthread_mutex_lock(&filter_cache_lock);
    for (uint32_t index = 0u; index < FILTER_CACHE_ENTRIES; ++index) {
        if (filter_cache[index].filter == NULL) {
            filter_cache[index].filter = astra_audio_make_filter(
                in_rate, out_rate, &filter_cache[index].taps);
            if (filter_cache[index].filter == NULL)
                break;
            filter_cache[index].in_rate = in_rate;
            filter_cache[index].out_rate = out_rate;
        }
        if (filter_cache[index].in_rate == in_rate &&
            filter_cache[index].out_rate == out_rate) {
            *taps_out = filter_cache[index].taps;
            found = filter_cache[index].filter;
            break;
        }
    }
    (void)pthread_mutex_unlock(&filter_cache_lock);
    return found;
}

struct AstraAudioConverter {
    uint32_t source_encoding;
    uint32_t source_channels;
    uint32_t source_rate;
    uint32_t source_frame_bytes;
    uint32_t target_encoding;
    uint32_t target_channels;
    uint32_t target_rate;
    uint32_t target_frame_bytes;
    /* NULL when the rates match. */
    const float *filter;
    /* Set only when the shared cache was full and this one is ours. */
    float *owned_filter;
    uint32_t taps;
    /*
     * The interpolated taps for each output phase, when the ratio repeats
     * soon enough to tabulate: output frame i reads row i % phases. A
     * reduced ratio of in/out = M/L has exactly L phases (Doom's 11025 ->
     * 44100 has 4), so the per-frame row choice, blend and multiply-add of
     * two rows become one row read. The doubles are the same values the
     * per-frame expression produces, so output is bit-identical. NULL when
     * L is too large to be worth it.
     */
    double *weights;
    uint32_t phases;
    /* Decoded source frames, left and right at 24-bit scale; frames[0] is
     * source frame @c base. */
    float (*frames)[2];
    uint32_t held;
    uint32_t capacity;
    uint64_t base;
    /* Source frames received, and target frames produced, since open. */
    uint64_t received;
    uint64_t produced;
    int ended;
};

static uint32_t greatest_divisor(uint32_t a, uint32_t b)
{
    while (b != 0u) {
        uint32_t rest = a % b;

        a = b;
        b = rest;
    }
    return a;
}

/* Most output phases tabulated: a 152-tap downsampling filter at this many
 * phases is 600 KiB, and the common ratios need a handful. */
#define CONVERTER_PHASES_MAX 512u

static void build_weights(AstraAudioConverter *converter)
{
    uint32_t phases = converter->target_rate /
                      greatest_divisor(converter->source_rate,
                                       converter->target_rate);

    if (phases > CONVERTER_PHASES_MAX)
        return;
    converter->weights = malloc((size_t)phases * converter->taps *
                                sizeof(*converter->weights));
    if (converter->weights == NULL)
        return;
    for (uint32_t phase = 0u; phase < phases; ++phase) {
        uint64_t scaled = (uint64_t)phase * converter->source_rate;
        double offset = (double)(scaled % converter->target_rate) *
                        ASTRA_AUDIO_FILTER_PHASES / converter->target_rate;
        uint32_t row = (uint32_t)offset;
        double blend = offset - row;
        const float *first = converter->filter + row * converter->taps;
        const float *second = first + converter->taps;
        double *weight = converter->weights + phase * converter->taps;

        for (uint32_t tap = 0u; tap < converter->taps; ++tap)
            weight[tap] = first[tap] + blend * (second[tap] - first[tap]);
    }
    converter->phases = phases;
}

AstraAudioConverter *astra_audio_converter_open(uint32_t source,
                                                uint32_t target)
{
    AstraAudioConverter *converter;

    if (astra_pcm_format_frame_bytes(source) == 0u ||
        astra_pcm_format_frame_bytes(target) == 0u)
        return NULL;
    converter = calloc(1u, sizeof(*converter));
    if (converter == NULL)
        return NULL;
    converter->source_encoding = astra_pcm_format_encoding(source);
    converter->source_channels = astra_pcm_format_channels(source);
    converter->source_rate = astra_pcm_format_rate(source);
    converter->source_frame_bytes = astra_pcm_format_frame_bytes(source);
    converter->target_encoding = astra_pcm_format_encoding(target);
    converter->target_channels = astra_pcm_format_channels(target);
    converter->target_rate = astra_pcm_format_rate(target);
    converter->target_frame_bytes = astra_pcm_format_frame_bytes(target);
    if (converter->source_rate != converter->target_rate) {
        converter->filter = astra_audio_filter(
            converter->source_rate, converter->target_rate,
            &converter->taps);
        if (converter->filter == NULL) {
            converter->owned_filter = astra_audio_make_filter(
                converter->source_rate, converter->target_rate,
                &converter->taps);
            converter->filter = converter->owned_filter;
        }
        if (converter->filter == NULL) {
            free(converter);
            return NULL;
        }
        build_weights(converter);
    }
    return converter;
}

void astra_audio_converter_close(AstraAudioConverter *converter)
{
    if (converter == NULL)
        return;
    free(converter->frames);
    free(converter->owned_filter);
    free(converter->weights);
    free(converter);
}

/* Source frames each side of an output position the filter reads. */
static uint32_t half_width(const AstraAudioConverter *converter)
{
    return converter->taps / 2u;
}

/* The first source frame target frame @p index still needs. */
static uint64_t first_needed(const AstraAudioConverter *converter,
                             uint64_t index)
{
    uint64_t position = index * converter->source_rate /
                        converter->target_rate;
    uint32_t half = half_width(converter);

    if (converter->filter == NULL)
        return position;
    return position + 1u > half ? position + 1u - half : 0u;
}

/* Drops source frames no later output reads, so a long stream holds only
 * the filter's width behind its read position. */
static void forget(AstraAudioConverter *converter)
{
    uint64_t keep = first_needed(converter, converter->produced);
    uint64_t drop;

    if (keep <= converter->base)
        return;
    drop = keep - converter->base;
    if (drop > converter->held)
        drop = converter->held;
    memmove(converter->frames, converter->frames + drop,
            (converter->held - drop) * sizeof(*converter->frames));
    converter->held -= (uint32_t)drop;
    converter->base += drop;
}

uint32_t astra_audio_converter_write(AstraAudioConverter *converter,
                                     const uint8_t *data, uint32_t bytes)
{
    uint32_t count;

    if (converter->ended || bytes % converter->source_frame_bytes != 0u)
        return ASTRA_STATUS_INVALID;
    count = bytes / converter->source_frame_bytes;
    forget(converter);
    if (count > ASTRA_AUDIO_CONVERTER_FRAMES_MAX - converter->held)
        return ASTRA_STATUS_BUSY;
    if (converter->held + count > converter->capacity) {
        uint32_t capacity = converter->capacity == 0u ? 4096u :
                            converter->capacity;
        float (*frames)[2];

        while (capacity < converter->held + count)
            capacity *= 2u;
        frames = realloc(converter->frames, capacity * sizeof(*frames));
        if (frames == NULL)
            return ASTRA_STATUS_NO_SPACE;
        converter->frames = frames;
        converter->capacity = capacity;
    }
    for (uint32_t at = 0u; at < bytes; at += converter->source_frame_bytes) {
        float *frame = converter->frames[converter->held++];
        uint32_t sample = converter->source_frame_bytes /
                          converter->source_channels;

        frame[0] = astra_audio_decode_sample(converter->source_encoding,
                                             data + at);
        frame[1] = converter->source_channels == 1u ? frame[0] :
                   astra_audio_decode_sample(converter->source_encoding,
                                             data + at + sample);
    }
    converter->received += count;
    return ASTRA_STATUS_OK;
}

void astra_audio_converter_end(AstraAudioConverter *converter)
{
    converter->ended = 1;
}

uint32_t astra_audio_converter_ready(const AstraAudioConverter *converter)
{
    uint64_t total;
    uint32_t half = half_width(converter);

    if (converter->ended)
        total = converter->received * converter->target_rate /
                converter->source_rate;
    else if (converter->received <= half)
        total = 0u;
    else
        /* Target frame i is complete once the source frame half a filter
         * past floor(i * in / out) has arrived. */
        total = ((converter->received - half) * converter->target_rate +
                 converter->source_rate - 1u) / converter->source_rate;
    if (total <= converter->produced)
        return 0u;
    total -= converter->produced;
    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

/* Source frame @p index, or silence before the start and past the end. */
static const float *source_frame(const AstraAudioConverter *converter,
                                 int64_t index)
{
    static const float silence[2];

    if (index < (int64_t)converter->base ||
        index >= (int64_t)(converter->base + converter->held))
        return silence;
    return converter->frames[index - (int64_t)converter->base];
}

uint32_t astra_audio_converter_read(AstraAudioConverter *converter,
                                    uint8_t *data, uint32_t capacity)
{
    uint32_t count = capacity / converter->target_frame_bytes;
    uint32_t ready = astra_audio_converter_ready(converter);
    uint32_t sample = converter->target_frame_bytes /
                      converter->target_channels;

    if (count > ready)
        count = ready;
    for (uint32_t out = 0u; out < count; ++out) {
        uint64_t index = converter->produced + out;
        double value[2];
        uint8_t *at = data + out * converter->target_frame_bytes;

        if (converter->filter == NULL) {
            const float *frame = source_frame(converter, (int64_t)index);

            value[0] = frame[0];
            value[1] = frame[1];
        } else if (converter->weights != NULL) {
            uint64_t scaled = index * converter->source_rate;
            int64_t position = (int64_t)(scaled / converter->target_rate);
            const double *weight = converter->weights +
                (uint32_t)(index % converter->phases) * converter->taps;
            int64_t from = position + 1 - (int64_t)half_width(converter);

            value[0] = value[1] = 0.0;
            if (from >= (int64_t)converter->base &&
                from + converter->taps <=
                    (int64_t)(converter->base + converter->held)) {
                /* The whole window is held: no per-tap bounds check. */
                const float (*frame)[2] =
                    converter->frames + (from - (int64_t)converter->base);

                for (uint32_t tap = 0u; tap < converter->taps; ++tap) {
                    value[0] += frame[tap][0] * weight[tap];
                    value[1] += frame[tap][1] * weight[tap];
                }
            } else {
                for (uint32_t tap = 0u; tap < converter->taps; ++tap) {
                    const float *frame = source_frame(converter,
                                                      from + (int64_t)tap);

                    value[0] += frame[0] * weight[tap];
                    value[1] += frame[1] * weight[tap];
                }
            }
        } else {
            uint64_t scaled = index * converter->source_rate;
            int64_t position = (int64_t)(scaled / converter->target_rate);
            double offset = (double)(scaled % converter->target_rate) *
                            ASTRA_AUDIO_FILTER_PHASES /
                            converter->target_rate;
            uint32_t row = (uint32_t)offset;
            double blend = offset - row;
            const float *first = converter->filter + row * converter->taps;
            const float *second = first + converter->taps;
            int64_t from = position + 1 - (int64_t)half_width(converter);

            value[0] = value[1] = 0.0;
            for (uint32_t tap = 0u; tap < converter->taps; ++tap) {
                const float *frame = source_frame(converter,
                                                  from + (int64_t)tap);
                double weight = first[tap] +
                                blend * (second[tap] - first[tap]);

                value[0] += frame[0] * weight;
                value[1] += frame[1] * weight;
            }
        }
        if (converter->target_channels == 1u) {
            astra_audio_encode_sample(converter->target_encoding,
                                      (value[0] + value[1]) / 2.0, at);
        } else {
            astra_audio_encode_sample(converter->target_encoding, value[0],
                                      at);
            astra_audio_encode_sample(converter->target_encoding, value[1],
                                      at + sample);
        }
    }
    converter->produced += count;
    return count * converter->target_frame_bytes;
}
