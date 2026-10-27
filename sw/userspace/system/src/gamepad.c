#include <astra/gamepad.h>

#include "internal/input_observer.h"

static int observer_live(const AstraGamepadObserver *observer)
{
    return observer != 0 &&
           observer->_private_events != ASTRA_INVALID_HANDLE &&
           observer->_private_client != 0u &&
           observer->_private_generation != 0u;
}

AstraResult astra_gamepad_observer_open(AstraHandle input_service,
                                        AstraGamepadObserver *observer)
{
    if (observer == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_internal_input_observer_open(
        input_service, ASTRA_INPUT_SUBSCRIBE_GAMEPAD, 256u,
        &observer->_private_events, &observer->_private_client,
        &observer->_private_generation);
}

static AstraResult receive_event(AstraGamepadObserver *observer,
                                 AstraGamepadEvent *event,
                                 AstraMonotonicDeadline deadline_ns)
{
    AstraLogicalInputEvent logical;
    AstraResult result;

    if (!observer_live(observer) || event == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_input_observer_receive(
        observer->_private_events, &logical, deadline_ns);
    if (result != ASTRA_OK)
        return result;
    if (logical.type != ASTRA_INPUT_EVENT_STATE_RESET &&
        (logical.type < ASTRA_INPUT_EVENT_GAMEPAD_DEVICE ||
         logical.type > ASTRA_INPUT_EVENT_GAMEPAD_HAT))
        return ASTRA_ERROR_IO;
    *event = (AstraGamepadEvent){
        .size = sizeof(*event),
        .version = ASTRA_GAMEPAD_EVENT_VERSION,
        .type = logical.type == ASTRA_INPUT_EVENT_STATE_RESET ?
                    ASTRA_GAMEPAD_EVENT_RESET :
                logical.type - ASTRA_INPUT_EVENT_GAMEPAD_DEVICE +
                    ASTRA_GAMEPAD_EVENT_DEVICE,
        .flags = (logical.flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u ?
                    ASTRA_GAMEPAD_EVENT_DOWN : 0u,
        .timestamp_ms = logical.timestamp_ms,
        .sequence = logical.sequence,
        .slot = logical.type == ASTRA_INPUT_EVENT_STATE_RESET ?
                    UINT16_MAX : (uint16_t)(logical.code >> 8),
        .index = (uint16_t)(logical.code & 0xffu),
        .vendor = (uint16_t)((uint32_t)logical.value_x >> 16),
        .product = (uint16_t)logical.value_x,
        .bus = (uint16_t)((uint32_t)logical.value_y >> 16),
        .product_version = (uint16_t)logical.value_y,
        .axes = (uint8_t)logical.total_x,
        .buttons = (uint8_t)((uint32_t)logical.total_x >> 8),
        .hats = (uint8_t)((uint32_t)logical.total_x >> 16),
        .value = logical.value_x,
    };
    if (logical.type == ASTRA_INPUT_EVENT_GAMEPAD_DEVICE)
        event->slot = (uint16_t)logical.code;
    else if (logical.type == ASTRA_INPUT_EVENT_GAMEPAD_HAT)
        event->value = logical.value_x;
    observer->_private_generation = logical.focus_generation != 0u ?
        logical.focus_generation : observer->_private_generation;
    return ASTRA_OK;
}

AstraResult astra_gamepad_event_try(AstraGamepadObserver *observer,
                                    AstraGamepadEvent *event)
{
    return receive_event(observer, event, ASTRA_DEADLINE_POLL);
}

AstraResult astra_gamepad_event_wait(AstraGamepadObserver *observer,
                                     AstraGamepadEvent *event,
                                     AstraMonotonicDeadline deadline_ns)
{
    return receive_event(observer, event, deadline_ns);
}

AstraResult astra_gamepad_observer_close(AstraGamepadObserver *observer)
{
    if (!observer_live(observer))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_internal_input_observer_close(
        &observer->_private_events, &observer->_private_client,
        &observer->_private_generation);
}
