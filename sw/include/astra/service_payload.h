#ifndef ASTRA_SERVICE_PAYLOAD_H
#define ASTRA_SERVICE_PAYLOAD_H

/* Internal native control payload ownership. Wire-specific bounds and
 * validation remain with the protocol; mapped areas are never parsed live. */
#include <astra/area.h>

AstraResult astra_service_payload_copy(AstraHandle *payload, void *bytes,
    uint32_t length, uint32_t capacity);
AstraResult astra_service_payload_finish(AstraArea *area, AstraHandle *payload);
AstraResult astra_service_payload_create(const void *bytes, uint32_t length,
    AstraHandle *payload);

#endif
