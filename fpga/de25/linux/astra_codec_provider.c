#include "astra_codec_provider.h"

#include <string.h>

/* Add providers here only after each uses this same lifetime and tests. */
static const AstraCodecProvider *const providers[] = {
    &astra_codec_wav_provider,
    &astra_codec_vorbis_provider,
    &astra_codec_y4m_provider
};

int astra_codec_read_exact(const AstraCodecSource *source, uint64_t offset,
                           void *output, size_t bytes)
{
    uint8_t *target = output;

    while (bytes != 0) {
        size_t received = 0;
        int error = source->read_at(source->context, offset, target, bytes,
                                     &received);

        if (error != 0 || received == 0 || received > bytes)
            return ASTRA_CODEC_IO;
        target += received;
        offset += received;
        bytes -= received;
    }
    return ASTRA_CODEC_OK;
}

int astra_codec_open(const AstraCodecSource *source,
                     AstraCodecSession *session, AstraCodecFormat *format)
{
    uint8_t prefix[32];
    size_t received = 0;
    int error;

    if (source == NULL || source->read_at == NULL || session == NULL ||
        format == NULL || session->provider != NULL || session->state != NULL)
        return ASTRA_CODEC_INVALID;
    while (received < sizeof(prefix)) {
        size_t moved = 0;

        error = source->read_at(source->context, received,
                                prefix + received, sizeof(prefix) - received,
                                &moved);
        if (error != 0 || moved > sizeof(prefix) - received)
            return ASTRA_CODEC_IO;
        if (moved == 0)
            break;
        received += moved;
    }
    for (size_t at = 0; at < sizeof(providers) / sizeof(providers[0]); ++at) {
        const AstraCodecProvider *provider = providers[at];

        if (!provider->probe(prefix, received))
            continue;
        session->source = *source;
        session->provider = provider;
        error = provider->open(source, session, format);
        if (error == ASTRA_CODEC_OK)
            return error;
        astra_codec_close(session);
        return error;
    }
    return ASTRA_CODEC_UNSUPPORTED;
}

int astra_codec_read(AstraCodecSession *session, void *output, size_t capacity,
                     AstraCodecChunk *chunk)
{
    if (session == NULL || session->provider == NULL || output == NULL ||
        capacity == 0 || chunk == NULL)
        return ASTRA_CODEC_INVALID;
    *chunk = (AstraCodecChunk){0};
    return session->provider->read(session, output, capacity, chunk);
}

int astra_codec_seek(AstraCodecSession *session, uint64_t timestamp_ns)
{
    if (session == NULL || session->provider == NULL)
        return ASTRA_CODEC_INVALID;
    return session->provider->seek(session, timestamp_ns);
}

void astra_codec_close(AstraCodecSession *session)
{
    if (session == NULL)
        return;
    if (session->provider != NULL)
        session->provider->close(session);
    (void)memset(session, 0, sizeof(*session));
}
