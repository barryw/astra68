#ifndef ASTRA_CODEC_PROVIDER_H
#define ASTRA_CODEC_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

/* Private HPS provider interface. Codec Kit's guest ABI is separate. */
enum {
    ASTRA_CODEC_OK = 0,
    ASTRA_CODEC_INVALID = -1,
    ASTRA_CODEC_UNSUPPORTED = -2,
    ASTRA_CODEC_IO = -3
};

enum {
    ASTRA_CODEC_AUDIO = 1,
    ASTRA_CODEC_VIDEO = 2
};

enum {
    ASTRA_CODEC_PCM_U8 = 1,
    ASTRA_CODEC_PCM_S16LE = 2,
    ASTRA_CODEC_PCM_S24LE = 3,
    ASTRA_CODEC_PCM_S32LE = 4,
    ASTRA_CODEC_VIDEO_YUV420P = 16
};

typedef struct AstraCodecSource {
    void *context;
    uint64_t length; /* Known seekable source length; zero if unknown. */
    /* Read at absolute offset. Zero bytes means end of source. */
    int (*read_at)(void *context, uint64_t offset, void *data,
                   size_t capacity, size_t *received);
} AstraCodecSource;

typedef struct AstraCodecFormat {
    uint32_t kind;
    uint32_t encoding;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
} AstraCodecFormat;

typedef struct AstraCodecChunk {
    size_t bytes;
    size_t frame_bytes;
    size_t frame_offset;
    uint64_t timestamp_ns;
    uint64_t duration_ns;
    int end;
} AstraCodecChunk;

typedef struct AstraCodecSession AstraCodecSession;

typedef struct AstraCodecProvider {
    const char *name;
    int (*probe)(const uint8_t *prefix, size_t bytes);
    int (*open)(const AstraCodecSource *source, AstraCodecSession *session,
                AstraCodecFormat *format);
    int (*read)(AstraCodecSession *session, void *output, size_t capacity,
                AstraCodecChunk *chunk);
    int (*seek)(AstraCodecSession *session, uint64_t timestamp_ns);
    void (*close)(AstraCodecSession *session);
} AstraCodecProvider;

struct AstraCodecSession {
    const AstraCodecProvider *provider;
    AstraCodecSource source;
    void *state;
};

extern const AstraCodecProvider astra_codec_wav_provider;
extern const AstraCodecProvider astra_codec_vorbis_provider;
extern const AstraCodecProvider astra_codec_y4m_provider;

int astra_codec_read_exact(const AstraCodecSource *source, uint64_t offset,
                           void *output, size_t bytes);

int astra_codec_open(const AstraCodecSource *source,
                     AstraCodecSession *session, AstraCodecFormat *format);
int astra_codec_read(AstraCodecSession *session, void *output, size_t capacity,
                     AstraCodecChunk *chunk);
int astra_codec_seek(AstraCodecSession *session, uint64_t timestamp_ns);
void astra_codec_close(AstraCodecSession *session);

#endif
