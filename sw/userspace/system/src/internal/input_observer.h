#ifndef ASTRA_SYSTEM_INPUT_OBSERVER_H
#define ASTRA_SYSTEM_INPUT_OBSERVER_H

#include <astra/input_service.h>
#include <astra/resource.h>
#include <astra/types.h>

AstraResult astra_internal_input_observer_open(
    AstraHandle service, uint32_t subscriptions, uint32_t queue_messages,
    AstraHandle *events, uint32_t *client, uint32_t *generation);
AstraResult astra_internal_input_observer_receive(
    AstraHandle events, AstraLogicalInputEvent *event,
    AstraMonotonicDeadline deadline_ns);
AstraResult astra_internal_input_observer_close(
    AstraHandle *events, uint32_t *client, uint32_t *generation);

#endif
