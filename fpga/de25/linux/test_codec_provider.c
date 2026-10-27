#include "astra_codec_provider.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Source {
    const uint8_t *bytes;
    size_t length;
    size_t maximum_read;
} Source;

static int read_at(void *context, uint64_t offset, void *output,
                   size_t capacity, size_t *received)
{
    Source *source = context;

    *received = 0;
    if (offset >= source->length)
        return 0;
    if (capacity > source->maximum_read)
        capacity = source->maximum_read;
    if (capacity > source->length - (size_t)offset)
        capacity = source->length - (size_t)offset;
    memcpy(output, source->bytes + offset, capacity);
    *received = capacity;
    return 0;
}

static int file_read_at(void *context, uint64_t offset, void *output,
                        size_t capacity, size_t *received)
{
    FILE *file = context;

    if (offset > LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0)
        return -1;
    *received = fread(output, 1, capacity, file);
    return ferror(file) ? -1 : 0;
}

static void put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void wav(uint8_t *bytes)
{
    memset(bytes, 0, 60);
    memcpy(bytes, "RIFF", 4);
    put32(bytes + 4, 52);
    memcpy(bytes + 8, "WAVEJUNK", 8);
    put32(bytes + 16, 1);
    bytes[20] = 7;
    memcpy(bytes + 22, "fmt ", 4);
    put32(bytes + 26, 16);
    bytes[30] = 1;       /* PCM */
    bytes[32] = 2;       /* stereo */
    put32(bytes + 34, 48000);
    put32(bytes + 38, 192000);
    bytes[42] = 4;       /* frame bytes */
    bytes[44] = 16;      /* sample bits */
    memcpy(bytes + 46, "data", 4);
    put32(bytes + 50, 6); /* deliberately malformed until caller fixes */
    bytes[54] = 1;
    bytes[55] = 2;
    bytes[56] = 3;
    bytes[57] = 4;
    bytes[58] = 5;
    bytes[59] = 6;
}

int main(int argc, char **argv)
{
    uint8_t bytes[62];
    uint8_t output[16];
    AstraCodecSession session = {0};
    AstraCodecFormat format = {0};
    AstraCodecChunk chunk = {0};
    Source source = {bytes, sizeof(bytes), 3};
    AstraCodecSource input = {&source, sizeof(bytes), read_at};

    wav(bytes);
    assert(astra_codec_open(&input, &session, &format) ==
           ASTRA_CODEC_INVALID); /* partial frame */
    assert(session.provider == NULL && session.state == NULL);
    put32(bytes + 4, 54);
    put32(bytes + 50, 8);
    bytes[60] = 7;
    bytes[61] = 8;
    assert(astra_codec_open(&input, &session, &format) == ASTRA_CODEC_OK);
    assert(format.kind == ASTRA_CODEC_AUDIO &&
           format.encoding == ASTRA_CODEC_PCM_S16LE &&
           format.channels == 2 && format.sample_rate == 48000);
    assert(astra_codec_read(&session, output, 4, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 4 && !chunk.end && chunk.timestamp_ns == 0);
    assert(memcmp(output, bytes + 54, 4) == 0);
    assert(astra_codec_read(&session, output, 4, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 4 && chunk.end && chunk.timestamp_ns > 0);
    assert(memcmp(output, bytes + 58, 4) == 0);
    assert(astra_codec_read(&session, output, 4, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 0 && chunk.end);
    assert(astra_codec_seek(&session, 0) == ASTRA_CODEC_OK);
    assert(astra_codec_read(&session, output, 8, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 8 && chunk.end);
    assert(astra_codec_seek(&session, UINT64_MAX) == ASTRA_CODEC_INVALID);
    astra_codec_close(&session);
    assert(session.provider == NULL && session.state == NULL);

    source.length = 58;
    assert(astra_codec_open(&input, &session, &format) == ASTRA_CODEC_OK);
    assert(astra_codec_read(&session, output, 8, &chunk) == ASTRA_CODEC_IO);
    astra_codec_close(&session);

    source.length = sizeof(bytes);
    bytes[30] = 3; /* IEEE float: not PCM */
    assert(astra_codec_open(&input, &session, &format) ==
           ASTRA_CODEC_UNSUPPORTED);
    bytes[30] = 1;
    put32(bytes + 50, UINT32_MAX); /* size leaves RIFF */
    assert(astra_codec_open(&input, &session, &format) ==
           ASTRA_CODEC_INVALID);

    static const uint8_t video[] =
        "YUV4MPEG2 W2 H2 F30:1 Ip C420\n"
        "FRAME\n\x01\x02\x03\x04\x05\x06"
        "FRAME\n\x07\x08\x09\x0a\x0b\x0c";
    Source video_source = {video, sizeof(video) - 1, 3};
    AstraCodecSource y4m = {&video_source, sizeof(video) - 1, read_at};
    assert(astra_codec_open(&y4m, &session, &format) == ASTRA_CODEC_OK);
    assert(format.kind == ASTRA_CODEC_VIDEO &&
           format.encoding == ASTRA_CODEC_VIDEO_YUV420P &&
           format.width == 2 && format.height == 2 &&
           format.frame_rate_num == 30 && format.frame_rate_den == 1);
    assert(astra_codec_read(&session, output, 3, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 3 && chunk.frame_bytes == 6 &&
           chunk.frame_offset == 0 && chunk.timestamp_ns == 0 && !chunk.end);
    assert(memcmp(output, "\x01\x02\x03", 3) == 0);
    assert(astra_codec_read(&session, output, 3, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 3 && chunk.frame_offset == 3 && !chunk.end);
    assert(astra_codec_read(&session, output, 6, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 6 && chunk.frame_offset == 0 && chunk.end);
    assert(chunk.timestamp_ns > 30000000 && chunk.timestamp_ns < 40000000);
    assert(astra_codec_seek(&session, 0) == ASTRA_CODEC_OK);
    assert(astra_codec_read(&session, output, 6, &chunk) == ASTRA_CODEC_OK);
    assert(chunk.bytes == 6 && memcmp(output, "\x01\x02\x03\x04\x05\x06", 6) == 0);
    astra_codec_close(&session);

    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0);
    AstraCodecSource ogg = {file, (uint64_t)length, file_read_at};
    assert(astra_codec_open(&ogg, &session, &format) == ASTRA_CODEC_OK);
    assert(format.kind == ASTRA_CODEC_AUDIO &&
           format.encoding == ASTRA_CODEC_PCM_S16LE &&
           format.sample_rate == 48000 && format.channels == 2);
    size_t decoded = 0;
    do {
        assert(astra_codec_read(&session, output, sizeof(output), &chunk) ==
               ASTRA_CODEC_OK);
        decoded += chunk.bytes;
    } while (!chunk.end);
    assert(decoded > 30000 && decoded < 45000);
    assert(astra_codec_seek(&session, 0) == ASTRA_CODEC_OK);
    assert(astra_codec_read(&session, output, sizeof(output), &chunk) ==
           ASTRA_CODEC_OK && chunk.bytes != 0 && chunk.timestamp_ns == 0);
    astra_codec_close(&session);
    assert(fclose(file) == 0);
    return 0;
}
