#ifndef ASTRA_CODEC_WORKER_H
#define ASTRA_CODEC_WORKER_H

/* Private socket protocol between QEMU's CODEC host service and one isolated
 * HPS decoder process. Both endpoints run on the same host architecture. */

#include <stdint.h>

#define ASTRA_CODEC_WORKER_MAGIC UINT32_C(0x43445731) /* CDW1 */
#define ASTRA_CODEC_WORKER_VERSION 1u
#define ASTRA_CODEC_WORKER_LIMIT (64u * 1024u)

enum {
    ASTRA_CODEC_WORKER_OPEN = 1,
    ASTRA_CODEC_WORKER_READ,
    ASTRA_CODEC_WORKER_SEEK,
    ASTRA_CODEC_WORKER_SOURCE,
    ASTRA_CODEC_WORKER_CLOSE,
    ASTRA_CODEC_WORKER_NEED_SOURCE = 0x100,
    ASTRA_CODEC_WORKER_FORMAT,
    ASTRA_CODEC_WORKER_CHUNK,
    ASTRA_CODEC_WORKER_CLOSED,
    ASTRA_CODEC_WORKER_SEEKED
};

typedef struct AstraCodecWorkerMessage {
    uint32_t magic;
    uint32_t version;
    uint32_t kind;
    uint32_t status;
    uint32_t offset_hi;
    uint32_t offset_lo;
    uint32_t length;
    uint32_t capacity;
    uint32_t bytes;
    uint32_t frame_bytes;
    uint32_t frame_offset;
    uint32_t timestamp_hi;
    uint32_t timestamp_lo;
    uint32_t duration_hi;
    uint32_t duration_lo;
    uint32_t media_kind;
    uint32_t encoding;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
    uint32_t end;
} AstraCodecWorkerMessage;

_Static_assert(sizeof(AstraCodecWorkerMessage) == 100u,
               "codec worker message layout");

#endif
