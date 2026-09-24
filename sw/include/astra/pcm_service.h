#ifndef ASTRA_PCM_SERVICE_H
#define ASTRA_PCM_SERVICE_H

/* Private media-service wire format. Applications use <astra/pcm.h>. */

#include <astra/message_abi.h>
#include <astra/pcm_format.h>
#include <stdint.h>

#define ASTRA_PCM_PROTOCOL UINT32_C(0x50434d31) /* PCM1 */
#define ASTRA_PCM_PROTOCOL_VERSION UINT16_C(2)

enum {
    ASTRA_PCM_OPEN = 1u,
    ASTRA_PCM_WRITE,
    ASTRA_PCM_GAIN,
    ASTRA_PCM_STATUS,
    ASTRA_PCM_FINISH,
    ASTRA_PCM_CLOSE,
    ASTRA_PCM_REPLY,
    ASTRA_PCM_PAUSE,
    ASTRA_PCM_CLEAR
};

typedef struct AstraPcmRequest {
    AstraMessageHeader header;
    uint32_t frames;
    uint32_t value;
} AstraPcmRequest;

typedef struct AstraPcmReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t queued_frames;
    uint32_t hardware_frames;
    uint32_t underruns;
    uint32_t overflows;
    uint32_t software_gaps;
} AstraPcmReply;

_Static_assert(sizeof(AstraPcmRequest) == 32u, "PCM request ABI changed");
_Static_assert(sizeof(AstraPcmReply) == 48u, "PCM reply ABI changed");

#endif
