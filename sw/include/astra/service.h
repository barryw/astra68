#ifndef ASTRA_SERVICE_H
#define ASTRA_SERVICE_H

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_SERVICE_PROTOCOL 0x53525643u /* SRVC */
#define ASTRA_SERVICE_VERSION  1u

#define ASTRA_SERVICE_READY 1u

typedef struct AstraServiceReady {
    AstraMessageHeader header;
    uint32_t status;
} AstraServiceReady;

#define ASTRA_SERVICE_READY_SIZE 28u
_Static_assert(sizeof(AstraServiceReady) == ASTRA_SERVICE_READY_SIZE,
               "service-ready message is an ABI");

#define ASTRA_CAPABILITY_SERVICE_READY "SERVICE_READY"

#endif
