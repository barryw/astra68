#include "astra_codec_provider.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct WavState {
    uint64_t data_offset;
    uint32_t data_bytes;
    uint32_t position;
    uint32_t frame_bytes;
    uint32_t sample_rate;
} WavState;

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int wav_probe(const uint8_t *prefix, size_t bytes)
{
    return bytes >= 12 && memcmp(prefix, "RIFF", 4) == 0 &&
           memcmp(prefix + 8, "WAVE", 4) == 0;
}

static int wav_open(const AstraCodecSource *source, AstraCodecSession *session,
                    AstraCodecFormat *format)
{
    uint8_t riff[12];
    uint8_t chunk[8];
    uint8_t fields[16];
    uint64_t end;
    uint64_t offset = 12;
    uint32_t rate = 0, frame_bytes = 0, encoding = 0, channels = 0;
    WavState *state;
    int have_format = 0;

    if (astra_codec_read_exact(source, 0, riff, sizeof(riff)) != ASTRA_CODEC_OK)
        return ASTRA_CODEC_IO;
    if (!wav_probe(riff, sizeof(riff)) || le32(riff + 4) < 4)
        return ASTRA_CODEC_INVALID;
    end = (uint64_t)le32(riff + 4) + 8;
    while (offset + sizeof(chunk) <= end) {
        uint32_t bytes;
        uint64_t next;

        if (astra_codec_read_exact(source, offset, chunk, sizeof(chunk)) !=
            ASTRA_CODEC_OK)
            return ASTRA_CODEC_IO;
        bytes = le32(chunk + 4);
        offset += sizeof(chunk);
        next = offset + bytes + (bytes & 1u);
        if (next > end)
            return ASTRA_CODEC_INVALID;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint32_t bits;

            if (have_format || bytes < sizeof(fields))
                return ASTRA_CODEC_INVALID;
            if (astra_codec_read_exact(source, offset, fields, sizeof(fields)) !=
                ASTRA_CODEC_OK)
                return ASTRA_CODEC_IO;
            if (le16(fields) != 1u)
                return ASTRA_CODEC_UNSUPPORTED;
            channels = le16(fields + 2);
            rate = le32(fields + 4);
            bits = le16(fields + 14);
            if (channels < 1 || channels > 2 || rate < 8000 ||
                rate > 192000 || (bits != 8 && bits != 16 &&
                                   bits != 24 && bits != 32))
                return ASTRA_CODEC_UNSUPPORTED;
            frame_bytes = channels * (bits / 8u);
            if (le16(fields + 12) != frame_bytes ||
                le32(fields + 8) != rate * frame_bytes)
                return ASTRA_CODEC_INVALID;
            encoding = bits == 8 ? ASTRA_CODEC_PCM_U8 :
                       bits == 16 ? ASTRA_CODEC_PCM_S16LE :
                       bits == 24 ? ASTRA_CODEC_PCM_S24LE :
                                    ASTRA_CODEC_PCM_S32LE;
            have_format = 1;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!have_format || bytes % frame_bytes != 0u)
                return ASTRA_CODEC_INVALID;
            state = calloc(1, sizeof(*state));
            if (state == NULL)
                return ASTRA_CODEC_IO;
            state->data_offset = offset;
            state->data_bytes = bytes;
            state->frame_bytes = frame_bytes;
            state->sample_rate = rate;
            session->state = state;
            *format = (AstraCodecFormat){
                .kind = ASTRA_CODEC_AUDIO,
                .encoding = encoding,
                .sample_rate = rate,
                .channels = channels
            };
            return ASTRA_CODEC_OK;
        }
        offset = next;
    }
    return ASTRA_CODEC_INVALID;
}

static int wav_read(AstraCodecSession *session, void *output, size_t capacity,
                    AstraCodecChunk *chunk)
{
    WavState *state = session->state;
    uint32_t remaining = state->data_bytes - state->position;
    size_t bytes = capacity - capacity % state->frame_bytes;
    uint64_t first = state->position / state->frame_bytes;

    if (bytes > remaining)
        bytes = remaining;
    if (bytes == 0) {
        chunk->end = remaining == 0;
        return remaining == 0 ? ASTRA_CODEC_OK : ASTRA_CODEC_INVALID;
    }
    if (astra_codec_read_exact(&session->source,
                               state->data_offset + state->position,
                               output, bytes) != ASTRA_CODEC_OK)
        return ASTRA_CODEC_IO;
    state->position += (uint32_t)bytes;
    chunk->bytes = bytes;
    chunk->frame_bytes = state->frame_bytes;
    chunk->timestamp_ns = first * UINT64_C(1000000000) /
                          state->sample_rate;
    chunk->duration_ns = ((uint64_t)(state->position / state->frame_bytes) *
                          UINT64_C(1000000000) / state->sample_rate) -
                         chunk->timestamp_ns;
    chunk->end = state->position == state->data_bytes;
    return ASTRA_CODEC_OK;
}

static int wav_seek(AstraCodecSession *session, uint64_t timestamp_ns)
{
    WavState *state = session->state;
    uint64_t seconds = timestamp_ns / UINT64_C(1000000000);
    uint64_t remainder = timestamp_ns % UINT64_C(1000000000);
    uint64_t frames;

    if (seconds > UINT32_MAX / state->sample_rate)
        return ASTRA_CODEC_INVALID;
    frames = seconds * state->sample_rate +
             remainder * state->sample_rate / UINT64_C(1000000000);
    if (frames > state->data_bytes / state->frame_bytes)
        return ASTRA_CODEC_INVALID;
    state->position = (uint32_t)(frames * state->frame_bytes);
    return ASTRA_CODEC_OK;
}

static void wav_close(AstraCodecSession *session)
{
    free(session->state);
}

const AstraCodecProvider astra_codec_wav_provider = {
    "WAV PCM", wav_probe, wav_open, wav_read, wav_seek, wav_close
};
