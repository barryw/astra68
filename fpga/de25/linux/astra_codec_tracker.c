#include "astra_codec_provider.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <xmp.h>

#define TRACKER_RATE 48000u
#define TRACKER_MAX_SOURCE (32u * 1024u * 1024u)

typedef struct TrackerState {
    xmp_context decoder;
    uint64_t frame;
} TrackerState;

static int tracker_probe(const uint8_t *prefix, size_t bytes)
{
    /* Signatures are deliberately checked before asking libxmp to parse.
     * A ProTracker module stores its signature after its sample headers. */
    if (bytes >= 17 && memcmp(prefix, "Extended Module: ", 17) == 0)
        return 1;
    if (bytes >= 4 && memcmp(prefix, "IMPM", 4) == 0)
        return 1;
    if (bytes >= 48 && memcmp(prefix + 44, "SCRM", 4) == 0)
        return 1;
    if (bytes >= 1084) {
        const uint8_t *magic = prefix + 1080;

        return memcmp(magic, "M.K.", 4) == 0 ||
               memcmp(magic, "M!K!", 4) == 0 ||
               memcmp(magic, "4CHN", 4) == 0 ||
               memcmp(magic, "6CHN", 4) == 0 ||
               memcmp(magic, "8CHN", 4) == 0 ||
               memcmp(magic, "FLT4", 4) == 0 ||
               memcmp(magic, "FLT8", 4) == 0;
    }
    return 0;
}

static int tracker_open(const AstraCodecSource *source,
                        AstraCodecSession *session, AstraCodecFormat *format)
{
    TrackerState *state;
    uint8_t *module;
    int result;

    if (source->length < 48 || source->length > TRACKER_MAX_SOURCE ||
        source->length > LONG_MAX)
        return ASTRA_CODEC_UNSUPPORTED;
    module = malloc((size_t)source->length);
    state = calloc(1, sizeof(*state));
    if (module == NULL || state == NULL) {
        free(module);
        free(state);
        return ASTRA_CODEC_IO;
    }
    result = astra_codec_read_exact(source, 0, module, (size_t)source->length);
    if (result != ASTRA_CODEC_OK)
        goto failed;
    state->decoder = xmp_create_context();
    if (state->decoder == NULL) {
        result = ASTRA_CODEC_IO;
        goto failed;
    }
    result = xmp_load_module_from_memory(state->decoder, module,
                                         (long)source->length);
    free(module);
    module = NULL;
    if (result < 0) {
        result = ASTRA_CODEC_INVALID;
        goto failed;
    }
    if (xmp_start_player(state->decoder, TRACKER_RATE, 0) < 0) {
        xmp_release_module(state->decoder);
        result = ASTRA_CODEC_INVALID;
        goto failed;
    }
    session->state = state;
    *format = (AstraCodecFormat){
        .kind = ASTRA_CODEC_AUDIO,
        .encoding = ASTRA_CODEC_PCM_S16LE,
        .sample_rate = TRACKER_RATE,
        .channels = 2
    };
    return ASTRA_CODEC_OK;
failed:
    free(module);
    if (state->decoder != NULL)
        xmp_free_context(state->decoder);
    free(state);
    return result;
}

static int tracker_read(AstraCodecSession *session, void *output,
                        size_t capacity, AstraCodecChunk *chunk)
{
    TrackerState *state = session->state;
    uint64_t first = state->frame;
    int result;

    capacity &= ~(size_t)3u;
    if (capacity == 0 || capacity > INT_MAX)
        return ASTRA_CODEC_INVALID;
    result = xmp_play_buffer(state->decoder, output, (int)capacity, 1);
    if (result == -1) {
        chunk->end = 1;
        return ASTRA_CODEC_OK;
    }
    if (result < 0)
        return ASTRA_CODEC_INVALID;
    state->frame += capacity / 4u;
    chunk->bytes = capacity;
    chunk->frame_bytes = 4;
    chunk->timestamp_ns = first * UINT64_C(1000000000) / TRACKER_RATE;
    chunk->duration_ns = state->frame * UINT64_C(1000000000) /
                         TRACKER_RATE - chunk->timestamp_ns;
    return ASTRA_CODEC_OK;
}

static int tracker_seek(AstraCodecSession *session, uint64_t timestamp_ns)
{
    TrackerState *state = session->state;
    uint64_t milliseconds = timestamp_ns / UINT64_C(1000000);

    if (milliseconds > INT_MAX)
        return ASTRA_CODEC_INVALID;
    if (milliseconds == 0)
        xmp_restart_module(state->decoder);
    else if (xmp_seek_time(state->decoder, (int)milliseconds) < 0)
        return ASTRA_CODEC_INVALID;
    (void)xmp_play_buffer(state->decoder, NULL, 0, 1);
    state->frame = milliseconds * TRACKER_RATE / 1000u;
    return ASTRA_CODEC_OK;
}

static void tracker_close(AstraCodecSession *session)
{
    TrackerState *state = session->state;

    if (state == NULL)
        return;
    xmp_end_player(state->decoder);
    xmp_release_module(state->decoder);
    xmp_free_context(state->decoder);
    free(state);
}

const AstraCodecProvider astra_codec_tracker_provider = {
    "Tracker", tracker_probe, tracker_open, tracker_read, tracker_seek,
    tracker_close, 1084
};
