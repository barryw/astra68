#ifndef ASTRA_PAGER_SERVICE_H
#define ASTRA_PAGER_SERVICE_H

#include <astra/message_abi.h>
#include <astra/status.h>

#define ASTRA_PAGER_PROTOCOL UINT32_C(0x50414752) /* PAGR */
#define ASTRA_PAGER_PROTOCOL_VERSION UINT16_C(1)
#define ASTRA_PAGER_OPERATION_PAGE 1u
#define ASTRA_PAGER_OPERATION_PAGE_REPLY 2u

/* Kernel requests one aligned page in a bound area. Reply capability is the
 * sole attached handle; cookie belongs to the service that bound the area. */
typedef struct AstraPagerPageRequest {
    AstraMessageHeader header;
    uint32_t cookie;
    uint32_t offset;
    uint32_t reserved[2];
} AstraPagerPageRequest;

/* data_area stays in the replier's table. Kernel reads valid_bytes from it
 * before completing the call and publishes a zero-tailed frame atomically. */
typedef struct AstraPagerPageReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t cookie;
    uint32_t offset;
    uint32_t data_area;
    uint32_t data_offset;
    uint32_t valid_bytes;
    uint32_t reserved;
} AstraPagerPageReply;

_Static_assert(sizeof(AstraPagerPageRequest) == 40u,
               "pager request ABI changed");
_Static_assert(sizeof(AstraPagerPageReply) == 52u,
               "pager reply ABI changed");

#endif
