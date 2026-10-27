#define _GNU_SOURCE
#include "astra_codec_provider.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define STB_VORBIS_HEADER_ONLY 1
#include <stb_vorbis.h>

typedef struct VorbisState {
    AstraCodecSource source;
    FILE *file;
    stb_vorbis *decoder;
    uint64_t offset;
    uint32_t rate;
    uint32_t channels;
    uint32_t position;
} VorbisState;

static ssize_t source_read(void *context, char *buffer, size_t capacity)
{
    VorbisState *state = context;
    size_t received = 0;

    if (state->offset >= state->source.length)
        return 0;
    if (capacity > state->source.length - state->offset)
        capacity = (size_t)(state->source.length - state->offset);

    if (state->source.read_at(state->source.context, state->offset, buffer,
                              capacity, &received) != 0 ||
        received > capacity) {
        errno = EIO;
        return -1;
    }
    state->offset += received;
    return (ssize_t)received;
}

static int source_seek(void *context, off64_t *offset, int whence)
{
    VorbisState *state = context;
    uint64_t base;
    uint64_t absolute;

    if (state->source.length == 0) {
        errno = ESPIPE;
        return -1;
    }
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = state->offset;
    else if (whence == SEEK_END)
        base = state->source.length;
    else {
        errno = EINVAL;
        return -1;
    }
    if ((*offset < 0 && (uint64_t)(-(*offset + 1)) + 1 > base) ||
        (*offset >= 0 && (uint64_t)*offset > state->source.length - base)) {
        errno = EINVAL;
        return -1;
    }
    absolute = *offset < 0 ? base - ((uint64_t)(-(*offset + 1)) + 1) :
                             base + (uint64_t)*offset;
    if (absolute > state->source.length || absolute > INT64_MAX) {
        errno = EINVAL;
        return -1;
    }
    state->offset = absolute;
    *offset = (off64_t)absolute;
    return 0;
}

static int vorbis_probe(const uint8_t *prefix, size_t bytes)
{
    return bytes >= 4 && memcmp(prefix, "OggS", 4) == 0;
}

static int vorbis_open(const AstraCodecSource *source,
                       AstraCodecSession *session, AstraCodecFormat *format)
{
    cookie_io_functions_t io = {source_read, NULL, source_seek, NULL};
    VorbisState *state;
    stb_vorbis_info info;
    int error = 0;

    if (source->length == 0 || source->length > UINT32_MAX)
        return ASTRA_CODEC_UNSUPPORTED;
    state = calloc(1, sizeof(*state));
    if (state == NULL)
        return ASTRA_CODEC_IO;
    state->source = *source;
    state->file = fopencookie(state, "rb", io);
    if (state->file == NULL) {
        free(state);
        return ASTRA_CODEC_IO;
    }
    state->decoder = stb_vorbis_open_file(state->file, 0, &error, NULL);
    if (state->decoder == NULL) {
        fclose(state->file);
        free(state);
        return ASTRA_CODEC_INVALID;
    }
    info = stb_vorbis_get_info(state->decoder);
    if (info.channels < 1 || info.channels > 2 ||
        info.sample_rate < 8000 || info.sample_rate > 192000) {
        stb_vorbis_close(state->decoder);
        fclose(state->file);
        free(state);
        return ASTRA_CODEC_UNSUPPORTED;
    }
    state->rate = info.sample_rate;
    state->channels = (uint32_t)info.channels;
    session->state = state;
    *format = (AstraCodecFormat){
        .kind = ASTRA_CODEC_AUDIO,
        .encoding = ASTRA_CODEC_PCM_S16LE,
        .sample_rate = state->rate,
        .channels = state->channels
    };
    return ASTRA_CODEC_OK;
}

static int vorbis_read(AstraCodecSession *session, void *output,
                       size_t capacity, AstraCodecChunk *chunk)
{
    VorbisState *state = session->state;
    size_t samples = capacity / sizeof(int16_t);
    uint32_t first = state->position;
    int frames;

    if (samples < state->channels || samples > INT_MAX)
        return ASTRA_CODEC_INVALID;
    frames = stb_vorbis_get_samples_short_interleaved(
        state->decoder, (int)state->channels, output, (int)samples);
    if (frames < 0)
        return ASTRA_CODEC_IO;
    if (frames == 0) {
        chunk->end = 1;
        return stb_vorbis_get_error(state->decoder) == VORBIS__no_error ?
            ASTRA_CODEC_OK : ASTRA_CODEC_INVALID;
    }
    state->position += (uint32_t)frames;
    chunk->bytes = (size_t)frames * state->channels * sizeof(int16_t);
    chunk->frame_bytes = state->channels * sizeof(int16_t);
    chunk->timestamp_ns = (uint64_t)first * UINT64_C(1000000000) /
                          state->rate;
    chunk->duration_ns = (uint64_t)state->position *
                         UINT64_C(1000000000) / state->rate -
                         chunk->timestamp_ns;
    return ASTRA_CODEC_OK;
}

static int vorbis_seek(AstraCodecSession *session, uint64_t timestamp_ns)
{
    VorbisState *state = session->state;
    uint64_t seconds = timestamp_ns / UINT64_C(1000000000);
    uint64_t remainder = timestamp_ns % UINT64_C(1000000000);
    uint64_t frame;

    if (seconds > UINT32_MAX / state->rate)
        return ASTRA_CODEC_INVALID;
    frame = seconds * state->rate +
            remainder * state->rate / UINT64_C(1000000000);
    if (frame > UINT32_MAX ||
        !stb_vorbis_seek(state->decoder, (unsigned int)frame))
        return ASTRA_CODEC_INVALID;
    state->position = (uint32_t)frame;
    return ASTRA_CODEC_OK;
}

static void vorbis_close(AstraCodecSession *session)
{
    VorbisState *state = session->state;

    if (state == NULL)
        return;
    stb_vorbis_close(state->decoder);
    fclose(state->file);
    free(state);
}

const AstraCodecProvider astra_codec_vorbis_provider = {
    "Ogg Vorbis", vorbis_probe, vorbis_open, vorbis_read, vorbis_seek,
    vorbis_close
};
