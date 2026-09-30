// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_graphics_hw.h"

#include <astra/audio_host.h>
#include <astra/pcm_format.h>
#include <astra/status.h>

#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum {
    AUDIO_BASE = ASTRA_CONTROL_BASE + 0x6000u,
    AUDIO_BYTES = 0x1000u,
    AUDIO_ID = 0x41554430u,
    AUDIO_VERSION = 0x00010000u,
    AUDIO_RATE = 48000u,
    AUDIO_FRAMES = 512u,
    REG_ID = 0x00u,
    REG_VERSION = 0x04u,
    REG_CONTROL = 0x0cu,
    REG_STATUS = 0x10u,
    REG_LEFT = 0x14u,
    REG_RIGHT = 0x18u,
    REG_UNDERRUNS = 0x1cu,
    REG_OVERFLOWS = 0x20u,
    REG_RATE = 0x24u,
    REG_FRAMES = 0x28u,
    CONTROL_ENABLE = 1u,
    CONTROL_DRAIN = 2u,
    STATUS_LEVEL_MASK = 0x3ffu,
    PREFILL_FRAMES = 384u,
    /* Resampling: a Kaiser-windowed sinc, ZERO_CROSSINGS wide on each side
     * at the cutoff, tabulated at FILTER_PHASES fractional positions and
     * interpolated between them. The cutoff sits a little below the lower
     * of the two Nyquist rates, so upsampling removes images and
     * downsampling removes what the sink cannot carry. */
    ZERO_CROSSINGS = 12u,
    FILTER_PHASES = 256u,
    /* Frames kept behind the read position for the filter's left wing:
     * the widest filter, at ASTRA_PCM_RATE_MAX, needs 51. */
    HISTORY_FRAMES = 64u,
};

#define FILTER_ROLLOFF 0.95
#define KAISER_BETA 8.6

typedef struct Voice {
    struct Voice *next;
    /* Decoded source frames, left and right, at 24-bit scale: the queue
     * plus HISTORY_FRAMES behind it. */
    float (*frames)[2];
    /* (FILTER_PHASES + 1) rows of taps; NULL when the rate is the sink's. */
    float *filter;
    uint32_t handle;
    uint32_t gain_q16;
    uint32_t format;
    uint32_t encoding;
    uint32_t channels;
    uint32_t rate;
    uint32_t frame_bytes;
    uint32_t taps;
    /* Source frames the filter needs after the read position. */
    uint32_t lookahead;
    /* Fractional read position, in 1/ASTRA_PCM_RATE of a source frame. */
    uint32_t phase;
    uint32_t read_at;
    uint32_t queued;
    int paused;
    int finished;
    int gap_reported;
    int ever_written;
} Voice;

typedef struct Client {
    struct Client *next;
    Voice *voices;
    int fd;
    int monitor;
} Client;

typedef struct AudioHost {
    volatile uint32_t *registers;
    Client *clients;
    int listener;
    int epoll_fd;
    int lock_fd;
    uint32_t next_handle;
    uint32_t underrun_start;
    uint32_t overflow_start;
    uint32_t software_gaps;
    uint64_t mixed_frames;
    uint32_t maximum_active_voices;
    uint32_t minimum_level;
    uint32_t tail_written;
    int playing;
    int tailing;
    int draining;
    AstraAudioHostMonitorPacket monitor_packet;
} AudioHost;

static volatile sig_atomic_t running = 1;

static void stop_running(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static uint32_t read_reg(const AudioHost *host, unsigned offset)
{
    return host->registers[offset / 4u];
}

static void write_reg(AudioHost *host, unsigned offset, uint32_t value)
{
    host->registers[offset / 4u] = value;
}

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

static float as_float(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* One sample of @p encoding at the sink's 24-bit scale. Integer encodings
 * come out exact; float ones may exceed full scale and are saturated when
 * mixed. */
static float decode_sample(uint32_t encoding, const uint8_t *data)
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

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;

    for (unsigned k = 1u; k < 64u && term > sum * 1e-12; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/* The polyphase table for converting @p rate to the sink rate. Row p holds
 * the taps for a read position p / FILTER_PHASES of a frame past the
 * current one; tap j weighs the frame j - (taps / 2 - 1) away. Each row is
 * normalised to unity gain at DC. */
static float *make_filter(uint32_t rate, uint32_t *taps_out)
{
    double cutoff = FILTER_ROLLOFF *
                    (rate > ASTRA_PCM_RATE ?
                     (double)ASTRA_PCM_RATE / rate : 1.0);
    uint32_t half = (uint32_t)ceil(ZERO_CROSSINGS / cutoff);
    uint32_t taps = 2u * half;
    float *filter = malloc((FILTER_PHASES + 1u) * taps * sizeof(*filter));
    double norm = bessel_i0(KAISER_BETA);

    if (filter == NULL || half > HISTORY_FRAMES)
        return free(filter), NULL;
    for (uint32_t phase = 0u; phase <= FILTER_PHASES; ++phase) {
        float *row = filter + phase * taps;
        double sum = 0.0;

        for (uint32_t tap = 0u; tap < taps; ++tap) {
            double t = (double)tap - (half - 1u) -
                       (double)phase / FILTER_PHASES;
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

static void free_voice(Voice *voice)
{
    free(voice->frames);
    free(voice->filter);
    free(voice);
}

static int32_t saturate24(int64_t sample)
{
    if (sample > INT32_C(0x7fffff))
        return INT32_C(0x7fffff);
    if (sample < -INT32_C(0x800000))
        return -INT32_C(0x800000);
    return (int32_t)sample;
}

static Voice *find_voice(Client *client, uint32_t handle)
{
    for (Voice *voice = client->voices; voice != NULL; voice = voice->next)
        if (voice->handle == handle)
            return voice;
    return NULL;
}

/* Frames in a voice's ring: the queue and the history behind it. */
#define VOICE_FRAMES (ASTRA_AUDIO_HOST_QUEUE_FRAMES + HISTORY_FRAMES)

/* A voice can give a sink frame when the filter's right wing is queued, or
 * when the stream is finished and the missing frames are its silent end. */
static int voice_ready(const Voice *voice)
{
    return !voice->paused && voice->queued != 0u &&
           (voice->finished || voice->queued > voice->lookahead);
}

static uint32_t queued_any(const AudioHost *host)
{
    for (const Client *client = host->clients; client != NULL;
         client = client->next)
        for (const Voice *voice = client->voices; voice != NULL;
             voice = voice->next)
            if (voice_ready(voice))
                return 1u;
    return 0u;
}

/* The source frame @p offset away from the read position: history behind
 * it, silence past a finished stream's end. */
static const float *voice_frame(const Voice *voice, int32_t offset)
{
    static const float silence[2];

    if (offset >= 0 && (uint32_t)offset >= voice->queued)
        return silence;
    return voice->frames[(voice->read_at + VOICE_FRAMES + (uint32_t)offset) %
                         VOICE_FRAMES];
}

/* One sink frame of @p voice, at 24-bit scale before gain, and the read
 * position moved on by one sink period. */
static void voice_next(Voice *voice, double out[2])
{
    uint32_t advance = 1u;

    if (voice->filter == NULL) {
        const float *frame = voice_frame(voice, 0);

        out[0] = frame[0];
        out[1] = frame[1];
    } else {
        double position = (double)voice->phase * FILTER_PHASES /
                          ASTRA_PCM_RATE;
        uint32_t row = (uint32_t)position;
        double blend = position - row;
        const float *first = voice->filter + row * voice->taps;
        const float *second = first + voice->taps;
        int32_t from = 1 - (int32_t)(voice->taps / 2u);

        out[0] = out[1] = 0.0;
        for (uint32_t tap = 0u; tap < voice->taps; ++tap) {
            const float *frame = voice_frame(voice, from + (int32_t)tap);
            double weight = first[tap] + blend * (second[tap] - first[tap]);

            out[0] += frame[0] * weight;
            out[1] += frame[1] * weight;
        }
        voice->phase += voice->rate;
        advance = voice->phase / ASTRA_PCM_RATE;
        voice->phase %= ASTRA_PCM_RATE;
    }
    if (advance > voice->queued)
        advance = voice->queued;
    voice->read_at = (voice->read_at + advance) % VOICE_FRAMES;
    voice->queued -= advance;
}

static void free_client(AudioHost *host, Client *client);

static void mix_frame(AudioHost *host, int32_t *left, int32_t *right)
{
    int64_t sum_left = 0;
    int64_t sum_right = 0;
    uint32_t active_voices = 0u;

    for (Client *client = host->clients; client != NULL;
         client = client->next)
        for (Voice *voice = client->voices; voice != NULL;
             voice = voice->next) {
            double sample[2];

            if (voice->paused)
                continue;
            if (!voice_ready(voice)) {
                if (voice->ever_written && !voice->finished &&
                    !voice->gap_reported) {
                    ++host->software_gaps;
                    voice->gap_reported = 1;
                }
                continue;
            }
            ++active_voices;
            voice_next(voice, sample);
            if (voice->filter == NULL) {
                /* The sink's own rate stays bit-exact. */
                sum_left += ((int64_t)sample[0] * voice->gain_q16) >> 16;
                sum_right += ((int64_t)sample[1] * voice->gain_q16) >> 16;
            } else {
                sum_left += llrint(sample[0] * voice->gain_q16 / 65536.0);
                sum_right += llrint(sample[1] * voice->gain_q16 / 65536.0);
            }
        }
    if (active_voices > host->maximum_active_voices)
        host->maximum_active_voices = active_voices;
    *left = saturate24(sum_left);
    *right = saturate24(sum_right);
}

static void monitor_flush(AudioHost *host)
{
    Client *client = host->clients;
    size_t length;

    if (host->monitor_packet.frames == 0u)
        return;
    length = 12u + host->monitor_packet.frames * 4u;
    while (client != NULL) {
        Client *next = client->next;

        if (client->monitor) {
            ssize_t written = send(client->fd, &host->monitor_packet, length,
                                   MSG_NOSIGNAL | MSG_DONTWAIT);

            if (written != (ssize_t)length &&
                !(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
                free_client(host, client);
        }
        client = next;
    }
    host->monitor_packet.frames = 0u;
}

static void monitor_frame(AudioHost *host, int32_t left, int32_t right)
{
    uint8_t *pcm;
    int16_t samples[2] = {(int16_t)(left / 256), (int16_t)(right / 256)};

    if (host->monitor_packet.frames == 0u) {
        bool listening = false;

        for (Client *client = host->clients; client != NULL;
             client = client->next)
            if (client->monitor)
                listening = true;
        if (!listening)
            return;
        host->monitor_packet.magic = ASTRA_AUDIO_HOST_MAGIC;
        host->monitor_packet.version = ASTRA_AUDIO_HOST_VERSION;
    }
    pcm = host->monitor_packet.pcm + host->monitor_packet.frames * 4u;
    for (unsigned channel = 0u; channel < 2u; ++channel) {
        pcm[channel * 2u] = (uint8_t)samples[channel];
        pcm[channel * 2u + 1u] = (uint8_t)((uint16_t)samples[channel] >> 8);
    }
    if (++host->monitor_packet.frames == ASTRA_AUDIO_HOST_MONITOR_FRAMES)
        monitor_flush(host);
}

static void feed(AudioHost *host)
{
    uint32_t level = read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK;
    uint32_t room = AUDIO_FRAMES - level;

    if (host->draining) {
        if (level != 0u)
            return;
        write_reg(host, REG_CONTROL, 0u);
        host->draining = 0;
    }
    if (host->playing && !host->tailing && queued_any(host) &&
        level < host->minimum_level)
        host->minimum_level = level;
    if (queued_any(host)) {
        host->tailing = 0;
        host->tail_written = 0u;
    }
    while (room != 0u && queued_any(host)) {
        int32_t left;
        int32_t right;

        mix_frame(host, &left, &right);
        write_reg(host, REG_LEFT, (uint32_t)left & UINT32_C(0xffffff));
        write_reg(host, REG_RIGHT, (uint32_t)right & UINT32_C(0xffffff));
        monitor_frame(host, left, right);
        --room;
        ++level;
        ++host->mixed_frames;
        if (!host->playing && level >= PREFILL_FRAMES) {
            write_reg(host, REG_CONTROL, CONTROL_ENABLE);
            host->playing = 1;
        }
    }
    if (queued_any(host))
        return;
    if (!host->playing && level == 0u)
        return;
    while (room != 0u && host->tail_written < AUDIO_FRAMES) {
        if (!host->tailing)
            for (Client *client = host->clients; client != NULL;
                 client = client->next)
                for (Voice *voice = client->voices; voice != NULL;
                     voice = voice->next)
                    if (!voice->paused && voice->ever_written &&
                        !voice->finished &&
                        !voice->gap_reported) {
                        ++host->software_gaps;
                        voice->gap_reported = 1;
                    }
        host->tailing = 1;
        write_reg(host, REG_LEFT, 0u);
        write_reg(host, REG_RIGHT, 0u);
        monitor_frame(host, 0, 0);
        --room;
        ++level;
        ++host->tail_written;
        if (!host->playing && level >= PREFILL_FRAMES) {
            write_reg(host, REG_CONTROL, CONTROL_ENABLE);
            host->playing = 1;
        }
    }
    if (host->tail_written == AUDIO_FRAMES) {
        write_reg(host, REG_CONTROL, CONTROL_DRAIN);
        host->playing = 0;
        host->tailing = 0;
        host->tail_written = 0u;
        host->draining = 1;
    }
}

static void free_client(AudioHost *host, Client *client)
{
    Client **at = &host->clients;

    while (*at != NULL && *at != client)
        at = &(*at)->next;
    if (*at == client)
        *at = client->next;
    while (client->voices != NULL) {
        Voice *voice = client->voices;

        client->voices = voice->next;
        free_voice(voice);
    }
    (void)epoll_ctl(host->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    (void)close(client->fd);
    free(client);
}

static uint32_t validate_request(const AstraAudioHostRequest *request,
                                 size_t packet_length)
{
    if (packet_length < sizeof(*request) ||
        request->magic != ASTRA_AUDIO_HOST_MAGIC ||
        request->version != ASTRA_AUDIO_HOST_VERSION ||
        request->data_length != packet_length - sizeof(*request))
        return ASTRA_STATUS_PROTOCOL;
    if (request->operation == ASTRA_HOST_AUDIO_WRITE)
        return request->handle != 0u && request->value == 0u &&
                       request->data_length != 0u &&
                       request->data_length <=
                           ASTRA_AUDIO_HOST_PACKET_FRAMES *
                               ASTRA_AUDIO_HOST_FRAME_BYTES ?
                   ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->data_length != 0u)
        return ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_AUDIO_HOST_MONITOR)
        return request->handle == 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_OPEN)
        return request->handle == 0u &&
               astra_pcm_format_frame_bytes(request->value) != 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_GAIN)
        return request->handle != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_STATUS)
        return request->value == 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_CLOSE)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_FINISH)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_PAUSE)
        return request->handle != 0u && request->value <= 1u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    if (request->operation == ASTRA_HOST_AUDIO_CLEAR)
        return request->handle != 0u && request->value == 0u ?
               ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
    return ASTRA_STATUS_UNSUPPORTED;
}

static void execute(AudioHost *host, Client *client,
                    const AstraAudioHostRequest *request,
                    const uint8_t *data, AstraAudioHostReply *reply)
{
    Voice *voice;

    switch (request->operation) {
    case ASTRA_AUDIO_HOST_MONITOR:
        if (client->voices != NULL)
            reply->status = ASTRA_STATUS_INVALID;
        else
            client->monitor = 1;
        break;
    case ASTRA_HOST_AUDIO_OPEN:
        voice = calloc(1u, sizeof(*voice));
        if (voice == NULL) {
            reply->status = ASTRA_STATUS_NO_SPACE;
            break;
        }
        voice->format = request->value;
        voice->encoding = astra_pcm_format_encoding(request->value);
        voice->channels = astra_pcm_format_channels(request->value);
        voice->rate = astra_pcm_format_rate(request->value);
        voice->frame_bytes = astra_pcm_format_frame_bytes(request->value);
        voice->frames = calloc(VOICE_FRAMES, sizeof(*voice->frames));
        if (voice->rate != ASTRA_PCM_RATE) {
            voice->filter = make_filter(voice->rate, &voice->taps);
            voice->lookahead = voice->taps / 2u;
        }
        if (voice->frames == NULL ||
            (voice->rate != ASTRA_PCM_RATE && voice->filter == NULL)) {
            free_voice(voice);
            reply->status = ASTRA_STATUS_NO_SPACE;
            break;
        }
        if (++host->next_handle == 0u)
            ++host->next_handle;
        voice->handle = host->next_handle;
        voice->gain_q16 = UINT32_C(65536);
        voice->next = client->voices;
        client->voices = voice;
        reply->handle = voice->handle;
        break;
    case ASTRA_HOST_AUDIO_WRITE:
        voice = find_voice(client, request->handle);
        if (voice == NULL) {
            reply->status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        if (voice->finished) {
            reply->status = ASTRA_STATUS_INVALID;
            break;
        }
        if (request->data_length % voice->frame_bytes != 0u) {
            reply->status = ASTRA_STATUS_INVALID;
            break;
        }
        if (request->data_length / voice->frame_bytes >
            ASTRA_AUDIO_HOST_QUEUE_FRAMES - voice->queued) {
            reply->status = ASTRA_STATUS_BUSY;
            break;
        }
        for (uint32_t at = 0u; at < request->data_length;
             at += voice->frame_bytes) {
            float *frame = voice->frames[(voice->read_at + voice->queued) %
                                         VOICE_FRAMES];
            uint32_t sample = voice->frame_bytes / voice->channels;

            frame[0] = decode_sample(voice->encoding, data + at);
            frame[1] = voice->channels == 1u ? frame[0] :
                       decode_sample(voice->encoding, data + at + sample);
            ++voice->queued;
        }
        voice->ever_written = 1;
        voice->gap_reported = 0;
        reply->queued_frames = voice->queued;
        break;
    case ASTRA_HOST_AUDIO_GAIN:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->gain_q16 = request->value;
        break;
    case ASTRA_HOST_AUDIO_STATUS:
        if (request->handle != 0u) {
            voice = find_voice(client, request->handle);
            if (voice == NULL)
                reply->status = ASTRA_STATUS_BAD_HANDLE;
            else
                reply->queued_frames = voice->queued;
        }
        break;
    case ASTRA_HOST_AUDIO_CLOSE: {
        Voice **at = &client->voices;

        while (*at != NULL && (*at)->handle != request->handle)
            at = &(*at)->next;
        if (*at == NULL) {
            reply->status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        voice = *at;
        *at = voice->next;
        free_voice(voice);
        break;
    }
    case ASTRA_HOST_AUDIO_FINISH:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->finished = 1;
        break;
    case ASTRA_HOST_AUDIO_PAUSE:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else
            voice->paused = request->value != 0u;
        break;
    case ASTRA_HOST_AUDIO_CLEAR:
        voice = find_voice(client, request->handle);
        if (voice == NULL)
            reply->status = ASTRA_STATUS_BAD_HANDLE;
        else {
            voice->queued = 0u;
            voice->read_at = 0u;
            voice->phase = 0u;
            memset(voice->frames, 0, VOICE_FRAMES * sizeof(*voice->frames));
            voice->ever_written = 0;
            voice->gap_reported = 0;
        }
        break;
    default:
        reply->status = ASTRA_STATUS_UNSUPPORTED;
        break;
    }
}

static void receive_client(AudioHost *host, Client *client)
{
    uint8_t packet[sizeof(AstraAudioHostRequest) +
                   ASTRA_AUDIO_HOST_PACKET_FRAMES *
                       ASTRA_AUDIO_HOST_FRAME_BYTES];
    AstraAudioHostRequest request;
    AstraAudioHostReply reply = {.magic = ASTRA_AUDIO_HOST_MAGIC,
                                 .status = ASTRA_STATUS_OK};
    ssize_t received = recv(client->fd, packet, sizeof(packet),
                            MSG_DONTWAIT | MSG_TRUNC);

    if (received <= 0) {
        if (received == 0 || (errno != EAGAIN && errno != EINTR))
            free_client(host, client);
        return;
    }
    if ((size_t)received > sizeof(packet)) {
        reply.status = ASTRA_STATUS_INVALID;
    } else {
        memset(&request, 0, sizeof(request));
        memcpy(&request, packet, received < (ssize_t)sizeof(request) ?
               (size_t)received : sizeof(request));
        reply.status = validate_request(&request, (size_t)received);
        if (reply.status == ASTRA_STATUS_OK && client->monitor &&
            request.operation != ASTRA_AUDIO_HOST_MONITOR)
            reply.status = ASTRA_STATUS_INVALID;
        if (reply.status == ASTRA_STATUS_OK)
            execute(host, client, &request, packet + sizeof(request), &reply);
    }
    reply.hardware_frames = read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK;
    reply.underruns = read_reg(host, REG_UNDERRUNS) - host->underrun_start;
    reply.overflows = read_reg(host, REG_OVERFLOWS) - host->overflow_start;
    reply.software_gaps = host->software_gaps;
    if (send(client->fd, &reply, sizeof(reply), MSG_NOSIGNAL | MSG_DONTWAIT) !=
        sizeof(reply))
        free_client(host, client);
}

static int setup_hardware(AudioHost *host)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (fd < 0)
        return -1;
    host->registers = mmap(NULL, AUDIO_BYTES, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, AUDIO_BASE);
    (void)close(fd);
    if (host->registers == MAP_FAILED) {
        host->registers = NULL;
        return -1;
    }
    if (read_reg(host, REG_ID) != AUDIO_ID ||
        read_reg(host, REG_VERSION) != AUDIO_VERSION ||
        read_reg(host, REG_RATE) != AUDIO_RATE ||
        read_reg(host, REG_FRAMES) != AUDIO_FRAMES) {
        errno = ENODEV;
        return -1;
    }
    write_reg(host, REG_CONTROL, CONTROL_DRAIN);
    for (unsigned attempt = 0u; attempt < 100u; ++attempt) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};

        if ((read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK) == 0u)
            break;
        (void)nanosleep(&delay, NULL);
    }
    if ((read_reg(host, REG_STATUS) & STATUS_LEVEL_MASK) != 0u) {
        errno = EBUSY;
        return -1;
    }
    write_reg(host, REG_CONTROL, 0u);
    host->underrun_start = read_reg(host, REG_UNDERRUNS);
    host->overflow_start = read_reg(host, REG_OVERFLOWS);
    host->minimum_level = AUDIO_FRAMES;
    return 0;
}

static int claim_hardware(AudioHost *host)
{
    host->lock_fd = open(ASTRA_AUDIO_HOST_LOCK,
                         O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (host->lock_fd < 0)
        return -1;
    if (flock(host->lock_fd, LOCK_EX | LOCK_NB) == 0)
        return 0;
    errno = EBUSY;
    return -1;
}

static int setup_socket(AudioHost *host, const char *path)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct stat st;
    struct epoll_event event = {.events = EPOLLIN, .data.ptr = host};

    if (strlen(path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode) || unlink(path) != 0)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    host->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK |
                           SOCK_CLOEXEC, 0);
    if (host->listener < 0)
        return -1;
    memcpy(address.sun_path, path, strlen(path) + 1u);
    if (bind(host->listener, (struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        chmod(path, 0600) != 0 || listen(host->listener, SOMAXCONN) != 0)
        return -1;
    host->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (host->epoll_fd < 0 ||
        epoll_ctl(host->epoll_fd, EPOLL_CTL_ADD, host->listener, &event) != 0)
        return -1;
    return 0;
}

static void accept_client(AudioHost *host)
{
    struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP};
    Client *client = calloc(1u, sizeof(*client));

    if (client == NULL)
        return;
    client->fd = accept4(host->listener, NULL, NULL,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client->fd < 0) {
        free(client);
        return;
    }
    event.data.ptr = client;
    if (epoll_ctl(host->epoll_fd, EPOLL_CTL_ADD, client->fd, &event) != 0) {
        (void)close(client->fd);
        free(client);
        return;
    }
    client->next = host->clients;
    host->clients = client;
}

static int mix_self_test(void)
{
    AudioHost host = {0};
    Client client = {0};
    uint32_t handles[16];
    const uint8_t frame[4] = {0u, 1u, 0u, 1u};
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = ASTRA_HOST_AUDIO_OPEN,
        .value = ASTRA_PCM_FORMAT_S16BE_STEREO,
    };
    AstraAudioHostReply reply;
    int32_t left = 0, right = 0;
    int passed = 0;
    unsigned stage = 0u;

    host.clients = &client;
    for (unsigned i = 0u; i < 16u; ++i) {
        stage = 100u + i;
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
            goto done;
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK || reply.handle == 0u)
            goto done;
        handles[i] = reply.handle;
        request.operation = ASTRA_HOST_AUDIO_WRITE;
        request.handle = reply.handle;
        request.value = 0u;
        request.data_length = sizeof(frame);
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, frame, &reply);
        if (reply.status != ASTRA_STATUS_OK || reply.queued_frames != 1u)
            goto done;
        request.operation = ASTRA_HOST_AUDIO_OPEN;
        request.handle = 0u;
        request.value = ASTRA_PCM_FORMAT_S16BE_STEREO;
        request.data_length = 0u;
    }
    mix_frame(&host, &left, &right);
    stage = 200u;
    if (left != 4096 || right != 4096 ||
        host.maximum_active_voices != 16u)
        goto done;
    for (unsigned i = 0u; i < 16u; ++i) {
        stage = 300u + i;
        request.operation = ASTRA_HOST_AUDIO_WRITE;
        request.handle = handles[i];
        request.value = 0u;
        request.data_length = sizeof(frame);
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, frame, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
        if (i < 8u) {
            request.operation = ASTRA_HOST_AUDIO_PAUSE;
            request.value = 1u;
            request.data_length = 0u;
            reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
            execute(&host, &client, &request, NULL, &reply);
            if (reply.status != ASTRA_STATUS_OK)
                goto done;
        }
    }
    mix_frame(&host, &left, &right);
    stage = 400u;
    if (left != 2048 || right != 2048)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_PAUSE;
    stage = 500u;
    request.handle = handles[0];
    request.value = 2u;
    request.data_length = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_WRITE;
    stage = 600u;
    request.value = 0u;
    request.data_length = 6u;
    if (validate_request(&request, sizeof(request) + 6u) != ASTRA_STATUS_OK)
        goto done;
    reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
    execute(&host, &client, &request, frame, &reply);
    if (reply.status != ASTRA_STATUS_INVALID)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_CLEAR;
    stage = 700u;
    request.handle = UINT32_MAX;
    request.data_length = 0u;
    reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
    execute(&host, &client, &request, NULL, &reply);
    if (reply.status != ASTRA_STATUS_BAD_HANDLE)
        goto done;
    for (unsigned i = 0u; i < 8u; ++i) {
        stage = 800u + i;
        request.handle = handles[i];
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
    }
    request.operation = ASTRA_HOST_AUDIO_PAUSE;
    request.value = 0u;
    for (unsigned i = 0u; i < 8u; ++i) {
        stage = 900u + i;
        request.handle = handles[i];
        reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
        execute(&host, &client, &request, NULL, &reply);
        if (reply.status != ASTRA_STATUS_OK)
            goto done;
    }
    mix_frame(&host, &left, &right);
    stage = 1000u;
    if (left != 0 || right != 0)
        goto done;
    passed = 1;
done:
    if (!passed)
        fprintf(stderr, "audio mix self-test failed at %u (%d,%d)\n",
                stage, left, right);
    while (client.voices != NULL) {
        Voice *voice = client.voices;

        client.voices = voice->next;
        free_voice(voice);
    }
    return passed;
}

static int decode_self_test(void)
{
    static const struct {
        uint32_t encoding;
        uint8_t bytes[4];
        float expected;
    } cases[] = {
        {ASTRA_PCM_ENCODING_U8, {0x00u}, -8388608.0f},
        {ASTRA_PCM_ENCODING_U8, {0x80u}, 0.0f},
        {ASTRA_PCM_ENCODING_S8, {0x7fu}, 127.0f * 65536.0f},
        {ASTRA_PCM_ENCODING_S16LE, {0x00u, 0x80u}, -8388608.0f},
        {ASTRA_PCM_ENCODING_S16BE, {0x00u, 0x01u}, 256.0f},
        {ASTRA_PCM_ENCODING_U16LE, {0xffu, 0xffu}, 32767.0f * 256.0f},
        {ASTRA_PCM_ENCODING_U16BE, {0x80u, 0x00u}, 0.0f},
        {ASTRA_PCM_ENCODING_S32BE, {0x80u, 0u, 0u, 0u}, -8388608.0f},
        {ASTRA_PCM_ENCODING_S32LE, {0u, 1u, 0u, 0u}, 1.0f},
        {ASTRA_PCM_ENCODING_F32BE, {0x3fu, 0x00u, 0u, 0u}, 4194304.0f},
        {ASTRA_PCM_ENCODING_F32LE, {0u, 0u, 0x80u, 0xbfu}, -8388608.0f},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (decode_sample(cases[i].encoding, cases[i].bytes) !=
            cases[i].expected) {
            fprintf(stderr, "audio decode self-test failed at %zu\n", i);
            return 0;
        }
    return astra_pcm_format_frame_bytes(
               ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 3u, 48000u)) ==
               0u &&
           astra_pcm_format_frame_bytes(
               ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u, 7999u)) ==
               0u &&
           astra_pcm_format_frame_bytes(
               ASTRA_PCM_FORMAT(12u, 2u, 48000u)) == 0u &&
           astra_pcm_format_frame_bytes(
               ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S24LE, 1u, 8000u)) == 3u;
}

/* Plays @p seconds of a @p tone Hz sine at @p rate through one voice of
 * @p format and returns the RMS and zero crossings of the middle half of
 * the sink output, relative to the input's full-scale RMS; 0 on failure. */
static int resample_tone(uint32_t format, double tone, double amplitude,
                         double *level, uint32_t *crossings,
                         uint32_t *window)
{
    AudioHost host = {0};
    Client client = {0};
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = ASTRA_HOST_AUDIO_OPEN,
        .value = format,
    };
    AstraAudioHostReply reply = {.status = ASTRA_STATUS_OK};
    uint32_t rate = astra_pcm_format_rate(format);
    uint32_t channels = astra_pcm_format_channels(format);
    uint32_t frame_bytes = astra_pcm_format_frame_bytes(format);
    uint32_t sample_bytes = frame_bytes / channels;
    uint32_t encoding = astra_pcm_format_encoding(format);
    uint32_t frames = rate / 4u; /* 250 ms */
    uint32_t written = 0u, produced = 0u, expected;
    uint8_t packet[ASTRA_AUDIO_HOST_PACKET_FRAMES *
                   ASTRA_AUDIO_HOST_FRAME_BYTES];
    double squares = 0.0;
    int32_t previous = 0;
    int passed = 0;

    host.clients = &client;
    *crossings = 0u;
    execute(&host, &client, &request, NULL, &reply);
    if (reply.status != ASTRA_STATUS_OK)
        goto done;
    request.operation = ASTRA_HOST_AUDIO_WRITE;
    request.handle = reply.handle;
    request.value = 0u;
    /* Short of the end by more than the filter's reach. */
    expected = (uint32_t)((uint64_t)(frames - HISTORY_FRAMES) *
                          ASTRA_PCM_RATE / rate);
    *window = expected / 2u;
    while (produced < expected) {
        int32_t left, right;

        while (written < frames && client.voices->queued <
               ASTRA_AUDIO_HOST_QUEUE_FRAMES - 256u) {
            uint32_t count = frames - written < 256u ?
                             frames - written : 256u;

            for (uint32_t i = 0u; i < count; ++i) {
                double value = amplitude *
                               sin(2.0 * M_PI * tone * (written + i) / rate);
                for (uint32_t c = 0u; c < channels; ++c) {
                    uint8_t *at = packet + i * frame_bytes + c * sample_bytes;
                    int32_t whole = (int32_t)lrint(value * 32767.0);

                    if (encoding == ASTRA_PCM_ENCODING_S16BE) {
                        at[0] = (uint8_t)((uint32_t)whole >> 8);
                        at[1] = (uint8_t)whole;
                    } else if (encoding == ASTRA_PCM_ENCODING_S16LE) {
                        at[0] = (uint8_t)whole;
                        at[1] = (uint8_t)((uint32_t)whole >> 8);
                    } else {
                        float single = (float)value;
                        uint32_t bits;

                        memcpy(&bits, &single, sizeof(bits));
                        for (uint32_t b = 0u; b < 4u; ++b)
                            at[b] = (uint8_t)(bits >> (24u - 8u * b));
                    }
                }
            }
            request.data_length = count * frame_bytes;
            reply = (AstraAudioHostReply){.status = ASTRA_STATUS_OK};
            execute(&host, &client, &request, packet, &reply);
            if (reply.status != ASTRA_STATUS_OK)
                goto done;
            written += count;
            if (written == frames) {
                request.operation = ASTRA_HOST_AUDIO_FINISH;
                request.data_length = 0u;
                execute(&host, &client, &request, NULL, &reply);
                request.operation = ASTRA_HOST_AUDIO_WRITE;
            }
        }
        if (!queued_any(&host))
            goto done;
        mix_frame(&host, &left, &right);
        if (left != right)
            goto done;
        if (produced >= expected / 4u && produced < expected * 3u / 4u) {
            squares += (double)left * left;
            if ((left < 0) != (previous < 0))
                ++*crossings;
        }
        previous = left;
        ++produced;
    }
    *level = sqrt(squares / (expected / 2u)) /
             (8388607.0 * sqrt(0.5));
    passed = 1;
done:
    while (client.voices != NULL) {
        Voice *voice = client.voices;

        client.voices = voice->next;
        free_voice(voice);
    }
    return passed;
}

static int resample_self_test(void)
{
    static const struct {
        uint32_t format;
        double tone, amplitude, level_low, level_high;
    } cases[] = {
        /* Upsampling keeps a tone's level and pitch. */
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 1u, 22050u),
         1000.0, 0.5, 0.49, 0.51},
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16LE, 2u, 44100u),
         440.0, 0.5, 0.49, 0.51},
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_S16BE, 2u, 8000u),
         1000.0, 0.5, 0.49, 0.51},
        /* Downsampling keeps what the sink can carry... */
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_F32BE, 1u, 96000u),
         1000.0, 0.5, 0.49, 0.51},
        /* ...and removes what it cannot, rather than folding it down. */
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_F32BE, 1u, 96000u),
         30000.0, 0.5, 0.0, 0.005},
        {ASTRA_PCM_FORMAT(ASTRA_PCM_ENCODING_F32BE, 2u, 192000u),
         60000.0, 0.5, 0.0, 0.005},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        double level = 0.0;
        uint32_t crossings = 0u, window = 0u;
        int played = resample_tone(cases[i].format, cases[i].tone,
                                   cases[i].amplitude, &level, &crossings,
                                   &window);
        double wanted = 2.0 * cases[i].tone * window / ASTRA_PCM_RATE;

        if (!played ||
            level < cases[i].level_low || level > cases[i].level_high ||
            (cases[i].level_low > 0.0 &&
             fabs(crossings - wanted) > 2.0)) {
            fprintf(stderr, "audio resample self-test failed at %zu "
                    "(level %.4f, crossings %u, wanted %.0f)\n",
                    i, level, crossings, wanted);
            return 0;
        }
    }
    return 1;
}

static int self_test(void)
{
    uint8_t sample[] = {0xffu, 0xffu, 0x7fu,
                        0x00u, 0x00u, 0x80u};
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = ASTRA_HOST_AUDIO_WRITE,
        .handle = 1u,
        .data_length = sizeof(sample),
    };
    AudioHost monitor_host = {0};
    Client monitor_client = {.fd = -1, .monitor = 1};
    AstraAudioHostMonitorPacket packet;
    int sockets[2];
    ssize_t received;

    if (!mix_self_test() || !decode_self_test() || !resample_self_test() ||
        decode_sample(ASTRA_PCM_ENCODING_S24LE, sample) != 8388607.0f ||
        decode_sample(ASTRA_PCM_ENCODING_S16BE,
                      (const uint8_t[]){0x80u, 0u}) != -8388608.0f ||
        decode_sample(ASTRA_PCM_ENCODING_S24LE, sample + 3u) !=
            -8388608.0f ||
        saturate24(INT64_C(9000000)) != INT32_C(0x7fffff) ||
        saturate24(-INT64_C(9000000)) != -INT32_C(0x800000) ||
        validate_request(&request, sizeof(request) + sizeof(sample)) !=
            ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    /* Any length can be whole frames of some format (8-bit mono); the
     * voice checks its own. Only the transport's packet limit is fixed. */
    request.data_length = 5u;
    if (validate_request(&request, sizeof(request) + 5u) != ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.data_length = ASTRA_AUDIO_HOST_PACKET_FRAMES *
                          ASTRA_AUDIO_HOST_FRAME_BYTES + 1u;
    if (validate_request(&request, sizeof(request) + request.data_length) !=
        ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    request.data_length = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    request.operation = ASTRA_HOST_AUDIO_OPEN;
    request.handle = 0u;
    request.value = ASTRA_PCM_FORMAT_S24LE_STEREO;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.operation = ASTRA_AUDIO_HOST_MONITOR;
    request.value = 0u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_OK)
        return EXIT_FAILURE;
    request.handle = 1u;
    if (validate_request(&request, sizeof(request)) != ASTRA_STATUS_INVALID)
        return EXIT_FAILURE;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0)
        return EXIT_FAILURE;
    monitor_frame(&monitor_host, INT32_C(0x400000), -INT32_C(0x400000));
    if (monitor_host.monitor_packet.frames != 0u)
        goto monitor_failed;
    monitor_client.fd = sockets[0];
    monitor_host.clients = &monitor_client;
    for (unsigned frame = 0u; frame < ASTRA_AUDIO_HOST_MONITOR_FRAMES;
         ++frame)
        monitor_frame(&monitor_host, INT32_C(0x400000), -INT32_C(0x400000));
    received = recv(sockets[1], &packet, sizeof(packet), MSG_DONTWAIT);
    if (received != (ssize_t)sizeof(packet) ||
        packet.magic != ASTRA_AUDIO_HOST_MAGIC ||
        packet.version != ASTRA_AUDIO_HOST_VERSION ||
        packet.frames != ASTRA_AUDIO_HOST_MONITOR_FRAMES ||
        memcmp(packet.pcm, (const uint8_t[]){0x00, 0x40, 0x00, 0xc0},
               4u) != 0 ||
        monitor_host.monitor_packet.frames != 0u)
        goto monitor_failed;
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    request.magic = 0u;
    return validate_request(&request, sizeof(request)) ==
           ASTRA_STATUS_PROTOCOL ? EXIT_SUCCESS : EXIT_FAILURE;

monitor_failed:
    (void)close(sockets[0]);
    (void)close(sockets[1]);
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    const char *path = ASTRA_AUDIO_HOST_SOCKET;
    AudioHost host = {.listener = -1, .epoll_fd = -1, .lock_fd = -1};
    struct epoll_event events[32];
    int result = EXIT_FAILURE;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc == 3 && strcmp(argv[1], "--socket") == 0)
        path = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test | --socket PATH]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (claim_hardware(&host) != 0 || setup_hardware(&host) != 0 ||
        setup_socket(&host, path) != 0) {
        perror("Astra audio host setup");
        goto done;
    }
    fprintf(stderr, "ASTRA AUDIO HOST ready socket=%s rate=%u channels=2\n",
            path, AUDIO_RATE);
    (void)signal(SIGTERM, stop_running);
    (void)signal(SIGINT, stop_running);
    while (running) {
        int count = epoll_wait(host.epoll_fd, events,
                               sizeof(events) / sizeof(events[0]),
                               host.playing || queued_any(&host) ? 1 : 1000);

        if (count < 0 && errno != EINTR) {
            perror("Astra audio host poll");
            goto done;
        }
        for (int index = 0; index < count; ++index) {
            if (events[index].data.ptr == &host)
                accept_client(&host);
            else
                receive_client(&host, events[index].data.ptr);
        }
        feed(&host);
    }
    result = EXIT_SUCCESS;

done:
    if (host.registers != NULL) {
        write_reg(&host, REG_CONTROL, 0u);
        fprintf(stderr, "ASTRA AUDIO HOST frames=%llu voices_peak=%u "
                "min_fifo=%u underruns=%u overflows=%u gaps=%u\n",
                (unsigned long long)host.mixed_frames,
                host.maximum_active_voices,
                host.minimum_level,
                read_reg(&host, REG_UNDERRUNS) - host.underrun_start,
                read_reg(&host, REG_OVERFLOWS) - host.overflow_start,
                host.software_gaps);
    }
    while (host.clients != NULL)
        free_client(&host, host.clients);
    if (host.epoll_fd >= 0)
        (void)close(host.epoll_fd);
    if (host.listener >= 0)
        (void)close(host.listener);
    if (host.lock_fd >= 0)
        (void)close(host.lock_fd);
    if (host.registers != NULL)
        (void)munmap((void *)host.registers, AUDIO_BYTES);
    if (result == EXIT_SUCCESS)
        (void)unlink(path);
    return result;
}
