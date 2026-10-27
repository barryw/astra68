#include <astra/codec.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("codec-certify", 0, 1, 0, "Astra68 contributors",
              "Astra native");

static const uint8_t wav[] = {
    'R','I','F','F',40,0,0,0,'W','A','V','E',
    'f','m','t',' ',16,0,0,0,1,0,1,0,0x40,0x1f,0,0,
    0x80,0x3e,0,0,2,0,16,0,
    'd','a','t','a',4,0,0,0,0,0,0,1
};
static const uint8_t video[] =
    "YUV4MPEG2 W2 H2 F25:1 Ip A1:1 C420\n"
    "FRAME\n"
    "\x10\x20\x30\x40\x80\x80";

typedef struct Source {
    const uint8_t *bytes;
    uint32_t length;
} Source;

static AstraResult read_at(void *context, uint64_t offset, void *output,
                           uint32_t capacity, uint32_t *received)
{
    Source *source = context;
    uint32_t count;

    if (offset >= source->length) {
        *received = 0u;
        return ASTRA_OK;
    }
    count = source->length - (uint32_t)offset;
    if (count > capacity) count = capacity;
    memcpy(output, source->bytes + (uint32_t)offset, count);
    *received = count;
    return ASTRA_OK;
}

static int run(AstraHandle service, Source *source, uint32_t kind,
               uint32_t encoding, const uint8_t *expected,
               uint32_t expected_bytes)
{
    AstraCodecStream stream = ASTRA_CODEC_STREAM_INIT;
    AstraCodecFormat format = {0};
    AstraCodecChunk chunk = {0};
    uint8_t output[64] = {0};
    AstraResult closed;
    int okay = 0;

    if (astra_codec_open(service, read_at, source, source->length,
                         &stream, &format) != ASTRA_OK)
        goto done;
    if (format.media_kind != kind || format.encoding != encoding)
        goto done;
    if (kind == ASTRA_CODEC_MEDIA_VIDEO &&
        (format.width != 2u || format.height != 2u ||
         format.stride != 2u || format.frame_rate_num != 25u ||
         format.frame_rate_den != 1u))
        goto done;
    if (kind == ASTRA_CODEC_MEDIA_AUDIO &&
        (format.sample_rate != 8000u || format.channels != 1u))
        goto done;
    if (astra_codec_read(&stream, output, sizeof(output), &chunk) != ASTRA_OK ||
        chunk.bytes != expected_bytes ||
        memcmp(output, expected, expected_bytes) != 0)
        goto done;
    if (kind == ASTRA_CODEC_MEDIA_VIDEO &&
        (chunk.frame_bytes != 6u || chunk.frame_offset != 0u ||
         chunk.timestamp_ns != 0u ||
         chunk.duration_ns != UINT64_C(40000000)))
        goto done;
    if (astra_codec_seek(&stream, 0u) != ASTRA_OK)
        goto done;
    memset(output, 0, sizeof(output));
    if (astra_codec_read(&stream, output, sizeof(output), &chunk) != ASTRA_OK ||
        chunk.bytes != expected_bytes ||
        memcmp(output, expected, expected_bytes) != 0)
        goto done;
    okay = 1;
done:
    if (stream.control != 0u) {
        closed = astra_codec_close(&stream);
        if (closed != ASTRA_OK) okay = 0;
    }
    return okay;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *service;
    Source audio = {wav, sizeof(wav)};
    Source moving = {video, sizeof(video) - 1u};
    const uint8_t audio_expected[] = {0,0,0,1};
    const uint8_t video_expected[] = {0x10,0x20,0x30,0x40,0x80,0x80};

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    service = astra_startup_capability(startup, "CODEC");
    if (service == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (!run(service->handle, &audio, ASTRA_CODEC_MEDIA_AUDIO,
             ASTRA_CODEC_AUDIO_S16LE, audio_expected,
             sizeof(audio_expected)) ||
        !run(service->handle, &moving, ASTRA_CODEC_MEDIA_VIDEO,
             ASTRA_CODEC_VIDEO_YUV420P, video_expected,
             sizeof(video_expected))) {
        (void)astra_log("ASTRA CODEC LIBRARY FAIL");
        return ASTRA_STATUS_IO;
    }
    (void)astra_log("ASTRA CODEC LIBRARY PASS");
    return ASTRA_STATUS_OK;
}
