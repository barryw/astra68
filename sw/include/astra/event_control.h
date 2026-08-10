#ifndef ASTRA_EVENT_CONTROL_H
#define ASTRA_EVENT_CONTROL_H

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_EVENT_CONTROL_PROTOCOL 0x45564354u /* EVCT */
#define ASTRA_EVENT_CONTROL_VERSION  1u
#define ASTRA_EVENT_CONTROL_SET      1u

#define ASTRA_CAPABILITY_EVENT_CONTROL "EVENT_CONTROL"
#define ASTRA_CAPABILITY_EVENT_TARGET  "EVENT_TARGET"

typedef struct AstraEventControlRequest {
    AstraMessageHeader header;
    uint32_t subsystem;
    uint32_t level;
} AstraEventControlRequest;

typedef struct AstraEventControlReply {
    AstraMessageHeader header;
    uint32_t status;
} AstraEventControlReply;

#define ASTRA_EVENT_CONTROL_REQUEST_SIZE 32u
#define ASTRA_EVENT_CONTROL_REPLY_SIZE   28u

_Static_assert(sizeof(AstraEventControlRequest) ==
                   ASTRA_EVENT_CONTROL_REQUEST_SIZE,
               "event-control request is an ABI");
_Static_assert(sizeof(AstraEventControlReply) == ASTRA_EVENT_CONTROL_REPLY_SIZE,
               "event-control reply is an ABI");

#endif
