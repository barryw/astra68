#include "astra_codec_host.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int advance_source(AstraCodecHostSession *session,
                          AstraCodecWorkerMessage *event, FILE *file,
                          uint8_t *output, uint32_t capacity)
{
    uint8_t input[ASTRA_CODEC_WORKER_LIMIT];
    int steps = 0;

    while (event->kind == ASTRA_CODEC_WORKER_NEED_SOURCE) {
        uint64_t offset = ((uint64_t)event->offset_hi << 32) |
                          event->offset_lo;
        size_t bytes;

        assert(++steps < 1000);
        assert(offset <= LONG_MAX && fseek(file, (long)offset, SEEK_SET) == 0);
        bytes = fread(input, 1, event->capacity, file);
        assert(!ferror(file));
        assert(astra_codec_host_supply(session, offset, input,
                                        (uint32_t)bytes, event,
                                        output, capacity) == 0);
    }
    return steps;
}

int main(int argc, char **argv)
{
    AstraCodecHostSession session = {0};
    AstraCodecWorkerMessage event = {0};
    uint8_t output[256];
    size_t decoded = 0;
    FILE *file;
    long length;

    assert(argc == 4);
    file = fopen(argv[2], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0);
    assert(astra_codec_host_open(argv[1], (uint64_t)length,
                                  &session, &event) == 0);
    assert(advance_source(&session, &event, file, output,
                          sizeof(output)) > 0);
    assert(event.kind == ASTRA_CODEC_WORKER_FORMAT && event.status == 0);
    assert(event.media_kind == 1 && event.sample_rate == 48000 &&
           event.channels == 2);
    for (;;) {
        assert(astra_codec_host_read(&session, sizeof(output),
                                      &event, output) == 0);
        (void)advance_source(&session, &event, file, output,
                             sizeof(output));
        assert(event.kind == ASTRA_CODEC_WORKER_CHUNK &&
               event.status == 0 && event.length <= sizeof(output));
        decoded += event.length;
        if (event.end)
            break;
    }
    assert(decoded > 30000 && decoded < 45000);
    assert(astra_codec_host_seek(&session, 0, &event) == 0);
    (void)advance_source(&session, &event, file, output, sizeof(output));
    assert(event.kind == ASTRA_CODEC_WORKER_SEEKED && event.status == 0);
    assert(astra_codec_host_read(&session, sizeof(output), &event, output) == 0);
    (void)advance_source(&session, &event, file, output, sizeof(output));
    assert(event.kind == ASTRA_CODEC_WORKER_CHUNK && event.length != 0 &&
           event.timestamp_hi == 0 && event.timestamp_lo == 0);
    astra_codec_host_close(&session);
    assert(session.child == 0 && fclose(file) == 0);

    file = fopen(argv[3], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 1084);
    assert(astra_codec_host_open(argv[1], (uint64_t)length,
                                  &session, &event) == 0);
    assert(advance_source(&session, &event, file, output,
                          sizeof(output)) > 0);
    assert(event.kind == ASTRA_CODEC_WORKER_FORMAT && event.status == 0);
    assert(event.media_kind == 1 && event.sample_rate == 48000 &&
           event.channels == 2);
    assert(astra_codec_host_read(&session, sizeof(output),
                                  &event, output) == 0);
    (void)advance_source(&session, &event, file, output, sizeof(output));
    assert(event.kind == ASTRA_CODEC_WORKER_CHUNK && event.status == 0 &&
           event.length == sizeof(output) && event.timestamp_lo == 0);
    uint8_t first_tracker[sizeof(output)];
    memcpy(first_tracker, output, sizeof(output));
    assert(astra_codec_host_seek(&session, 0, &event) == 0);
    (void)advance_source(&session, &event, file, output, sizeof(output));
    assert(event.kind == ASTRA_CODEC_WORKER_SEEKED && event.status == 0);
    assert(astra_codec_host_read(&session, sizeof(output),
                                  &event, output) == 0);
    (void)advance_source(&session, &event, file, output, sizeof(output));
    assert(event.kind == ASTRA_CODEC_WORKER_CHUNK && event.status == 0 &&
           event.length == sizeof(output) && event.timestamp_lo == 0 &&
           memcmp(first_tracker, output, sizeof(output)) == 0);
    astra_codec_host_close(&session);
    assert(session.child == 0 && fclose(file) == 0);

    /* A provider cannot read past the source length even when backing
       storage still contains more bytes. */
    static const uint8_t video[] =
        "YUV4MPEG2 W2 H2 F25:1 Ip C420\n"
        "FRAME\n\x01\x02\x03\x04\x05\x06";
    file = tmpfile();
    assert(file != NULL);
    assert(fwrite(video, 1, sizeof(video) - 1, file) == sizeof(video) - 1);
    assert(fflush(file) == 0);
    assert(astra_codec_host_open(argv[1], sizeof(video) - 2,
                                  &session, &event) == 0);
    assert(advance_source(&session, &event, file, output,
                          sizeof(output)) > 0);
    assert(event.kind == ASTRA_CODEC_WORKER_FORMAT && event.status == 0);
    assert(astra_codec_host_read(&session, sizeof(output),
                                  &event, output) == 0);
    assert(advance_source(&session, &event, file, output,
                          sizeof(output)) > 0);
    assert(event.kind == ASTRA_CODEC_WORKER_CHUNK && event.status != 0);
    astra_codec_host_close(&session);
    assert(session.child == 0 && fclose(file) == 0);
    return 0;
}
