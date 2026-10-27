#include "astra_codec_provider.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Y4mState {
    uint64_t first_frame;
    uint64_t frame_index;
    uint64_t offset;
    size_t frame_bytes;
    size_t frame_offset;
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
    int frame_open;
} Y4mState;

static int y4m_probe(const uint8_t *prefix, size_t bytes)
{
    return bytes >= 9 && memcmp(prefix, "YUV4MPEG2", 9) == 0;
}

static int number(const char *text, size_t length, uint32_t *value)
{
    uint32_t result = 0;

    if (length == 0)
        return 0;
    for (size_t at = 0; at < length; ++at) {
        uint32_t digit = (uint32_t)(uint8_t)text[at] - (uint32_t)'0';

        if (digit > 9 || result > (UINT32_MAX - digit) / 10u)
            return 0;
        result = result * 10u + digit;
    }
    *value = result;
    return result != 0;
}

static int y4m_open(const AstraCodecSource *source,
                    AstraCodecSession *session, AstraCodecFormat *format)
{
    char header[256];
    size_t length = 0;
    uint32_t width = 0, height = 0, numerator = 0, denominator = 0;
    int chroma = 0;
    Y4mState *state;

    while (length < sizeof(header) - 1) {
        size_t received = 0;
        int error = source->read_at(source->context, length,
                                    header + length,
                                    sizeof(header) - 1 - length, &received);
        if (error != 0 || received == 0 ||
            received > sizeof(header) - 1 - length)
            return ASTRA_CODEC_IO;
        size_t old_length = length;
        length += received;
        for (size_t at = old_length; at < length; ++at)
            if (header[at] == '\n') {
                length = at + 1;
                goto header_end;
            }
    }
    return ASTRA_CODEC_INVALID;
header_end:
    header[length - 1] = '\0';
    if (strncmp(header, "YUV4MPEG2 ", 10) != 0)
        return ASTRA_CODEC_INVALID;
    for (char *token = header + 10; *token != '\0';) {
        char *end = strchr(token, ' ');
        size_t size = end != NULL ? (size_t)(end - token) : strlen(token);

        if (size < 2)
            return ASTRA_CODEC_INVALID;
        if (token[0] == 'W') {
            if (width != 0 || !number(token + 1, size - 1, &width))
                return ASTRA_CODEC_INVALID;
        } else if (token[0] == 'H') {
            if (height != 0 || !number(token + 1, size - 1, &height))
                return ASTRA_CODEC_INVALID;
        } else if (token[0] == 'F') {
            const char *colon = memchr(token + 1, ':', size - 1);

            if (numerator != 0 || colon == NULL ||
                !number(token + 1, (size_t)(colon - token - 1),
                        &numerator) ||
                !number(colon + 1,
                        size - (size_t)(colon - token) - 1,
                        &denominator))
                return ASTRA_CODEC_INVALID;
        } else if (token[0] == 'C') {
            if (chroma || size < 4 || memcmp(token, "C420", 4) != 0)
                return ASTRA_CODEC_UNSUPPORTED;
            chroma = 1;
        } else if (token[0] == 'I') {
            if (size != 2 || token[1] != 'p')
                return ASTRA_CODEC_UNSUPPORTED;
        } else if (token[0] != 'A' && token[0] != 'X') {
            return ASTRA_CODEC_UNSUPPORTED;
        }
        token = end != NULL ? end + 1 : token + size;
    }
    if (width < 2 || height < 2 || width > 4096 || height > 4096 ||
        (width & 1u) != 0 || (height & 1u) != 0 ||
        numerator == 0 || denominator == 0 || !chroma)
        return ASTRA_CODEC_UNSUPPORTED;
    state = calloc(1, sizeof(*state));
    if (state == NULL)
        return ASTRA_CODEC_IO;
    state->first_frame = length;
    state->offset = length;
    state->frame_bytes = (size_t)width * height * 3u / 2u;
    state->frame_rate_num = numerator;
    state->frame_rate_den = denominator;
    session->state = state;
    *format = (AstraCodecFormat){
        .kind = ASTRA_CODEC_VIDEO,
        .encoding = ASTRA_CODEC_VIDEO_YUV420P,
        .width = width,
        .height = height,
        .stride = width,
        .frame_rate_num = numerator,
        .frame_rate_den = denominator
    };
    return ASTRA_CODEC_OK;
}

static uint64_t frame_time(const Y4mState *state, uint64_t frame)
{
    __uint128_t value = (__uint128_t)frame * state->frame_rate_den *
                        UINT64_C(1000000000) / state->frame_rate_num;

    return value > UINT64_MAX ? UINT64_MAX : (uint64_t)value;
}

static int y4m_read(AstraCodecSession *session, void *output,
                    size_t capacity, AstraCodecChunk *chunk)
{
    Y4mState *state = session->state;
    size_t bytes;

    if (!state->frame_open) {
        char marker[6];
        size_t received = 0;
        int error = session->source.read_at(session->source.context,
                                            state->offset, marker, 1,
                                            &received);

        if (error != 0 || received > 1)
            return ASTRA_CODEC_IO;
        if (received == 0) {
            chunk->end = 1;
            return ASTRA_CODEC_OK;
        }
        if (astra_codec_read_exact(&session->source, state->offset + 1,
                                   marker + 1, 5) != ASTRA_CODEC_OK)
            return ASTRA_CODEC_IO;
        if (memcmp(marker, "FRAME\n", 6) != 0)
            return ASTRA_CODEC_UNSUPPORTED;
        state->offset += 6;
        state->frame_offset = 0;
        state->frame_open = 1;
    }
    bytes = state->frame_bytes - state->frame_offset;
    if (bytes > capacity)
        bytes = capacity;
    if (astra_codec_read_exact(&session->source, state->offset,
                               output, bytes) != ASTRA_CODEC_OK)
        return ASTRA_CODEC_IO;
    chunk->bytes = bytes;
    chunk->frame_bytes = state->frame_bytes;
    chunk->frame_offset = state->frame_offset;
    chunk->timestamp_ns = frame_time(state, state->frame_index);
    chunk->duration_ns = frame_time(state, state->frame_index + 1) -
                         chunk->timestamp_ns;
    state->offset += bytes;
    state->frame_offset += bytes;
    if (state->frame_offset == state->frame_bytes) {
        ++state->frame_index;
        state->frame_open = 0;
        chunk->end = session->source.length != 0 &&
                     state->offset == session->source.length;
    }
    return ASTRA_CODEC_OK;
}

static int y4m_seek(AstraCodecSession *session, uint64_t timestamp_ns)
{
    Y4mState *state = session->state;
    __uint128_t frame = (__uint128_t)timestamp_ns * state->frame_rate_num /
                        ((__uint128_t)state->frame_rate_den *
                         UINT64_C(1000000000));
    __uint128_t offset = state->first_frame +
                         frame * (state->frame_bytes + 6u);

    if (frame > UINT64_MAX || offset > UINT64_MAX ||
        (session->source.length != 0 &&
         offset > session->source.length))
        return ASTRA_CODEC_INVALID;
    state->frame_index = (uint64_t)frame;
    state->offset = (uint64_t)offset;
    state->frame_offset = 0;
    state->frame_open = 0;
    return ASTRA_CODEC_OK;
}

static void y4m_close(AstraCodecSession *session)
{
    free(session->state);
}

const AstraCodecProvider astra_codec_y4m_provider = {
    "YUV4MPEG2 4:2:0", y4m_probe, y4m_open, y4m_read, y4m_seek, y4m_close
};
