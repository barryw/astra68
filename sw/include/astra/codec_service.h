#ifndef ASTRA_CODEC_SERVICE_H
#define ASTRA_CODEC_SERVICE_H

/* Private Codec service protocol. Caller owns source I/O and shared bytes. */
#include <astra/message_abi.h>
#include <astra/codec_worker.h>

#define ASTRA_CODEC_PROTOCOL UINT32_C(0x434f4431) /* COD1 */
#define ASTRA_CODEC_PROTOCOL_VERSION 1u
#define ASTRA_CODEC_TRANSFER_BYTES 32768u

enum {
    ASTRA_CODEC_SERVICE_OPEN = 1u,
    ASTRA_CODEC_SERVICE_SOURCE,
    ASTRA_CODEC_SERVICE_READ,
    ASTRA_CODEC_SERVICE_SEEK,
    ASTRA_CODEC_SERVICE_CLOSE,
    ASTRA_CODEC_SERVICE_REPLY
};

typedef struct AstraCodecServiceRequest {
    AstraMessageHeader header;
    uint32_t offset_hi;
    uint32_t offset_lo;
    uint32_t bytes;
} AstraCodecServiceRequest;

typedef struct AstraCodecServiceReply {
    AstraMessageHeader header;
    uint32_t status;
    AstraCodecWorkerMessage event;
} AstraCodecServiceReply;

_Static_assert(sizeof(AstraCodecServiceRequest) == 36u,
               "codec request ABI");
_Static_assert(sizeof(AstraCodecServiceReply) == 128u,
               "codec reply ABI");
#endif
