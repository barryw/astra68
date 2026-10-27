#ifndef ASTRA_CODEC_HOST_H
#define ASTRA_CODEC_HOST_H

#include <astra/codec_worker.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct AstraCodecHostSession {
    int socket;
    pid_t child;
    uint32_t pending;
    uint64_t requested_offset;
    uint32_t requested_capacity;
    int source_pending;
} AstraCodecHostSession;

/* One worker per stream; returned event can ask caller for source bytes. */
int astra_codec_host_open(const char *worker, uint64_t source_length,
                          AstraCodecHostSession *session,
                          AstraCodecWorkerMessage *event);
int astra_codec_host_read(AstraCodecHostSession *session, uint32_t capacity,
                          AstraCodecWorkerMessage *event, void *output);
int astra_codec_host_seek(AstraCodecHostSession *session, uint64_t timestamp_ns,
                          AstraCodecWorkerMessage *event);
int astra_codec_host_supply(AstraCodecHostSession *session, uint64_t offset,
                            const void *data, uint32_t bytes,
                            AstraCodecWorkerMessage *event, void *output,
                            uint32_t output_capacity);
void astra_codec_host_close(AstraCodecHostSession *session);

#endif
