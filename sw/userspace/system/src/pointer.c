#include <astra/pointer.h>

#include <astra/input.h>
#include <astra/input_service.h>

#include "internal/input_observer.h"

static int observer_live(const AstraPointerObserver *observer)
{
    return observer != 0 &&
           observer->_private_events != ASTRA_INVALID_HANDLE &&
           observer->_private_client != 0u &&
           observer->_private_generation != 0u;
}

AstraResult astra_pointer_observer_open(AstraHandle input_service,
                                        uint32_t subscriptions,
                                        AstraPointerObserver *observer)
{
    if (observer == 0 || subscriptions == 0u ||
        (subscriptions & ~ASTRA_POINTER_SUBSCRIBE_ALL) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_internal_input_observer_open(
        input_service, subscriptions, 8u,
        &observer->_private_events, &observer->_private_client,
        &observer->_private_generation);
}

static AstraResult receive_event(AstraPointerObserver *observer,
                                 AstraPointerEvent *event,
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
    if (logical.type < ASTRA_INPUT_EVENT_POINTER_MOTION ||
        logical.type > ASTRA_INPUT_EVENT_STATE_RESET ||
        logical.type == ASTRA_INPUT_EVENT_FOCUS)
        return ASTRA_ERROR_IO;
    *event = (AstraPointerEvent){
        .size = sizeof(*event),
        .version = ASTRA_POINTER_EVENT_VERSION,
        .type = logical.type == ASTRA_INPUT_EVENT_POINTER_MOTION ?
                    ASTRA_POINTER_EVENT_MOTION :
                logical.type == ASTRA_INPUT_EVENT_POINTER_BUTTON &&
                logical.code >= ASTRA_INPUT_BUTTON_WHEEL_UP &&
                logical.code <= ASTRA_INPUT_BUTTON_WHEEL_RIGHT ?
                    ASTRA_POINTER_EVENT_WHEEL :
                logical.type == ASTRA_INPUT_EVENT_POINTER_BUTTON ?
                    ASTRA_POINTER_EVENT_BUTTON :
                    ASTRA_POINTER_EVENT_STATE_RESET,
        .flags =
            ((logical.flags & ASTRA_INPUT_LOGICAL_DOWN) != 0u ?
                 ASTRA_POINTER_EVENT_DOWN : 0u) |
            ((logical.flags & ASTRA_INPUT_LOGICAL_SYNTHETIC) != 0u ?
                 ASTRA_POINTER_EVENT_SYNTHETIC : 0u) |
            ((logical.flags & ASTRA_INPUT_LOGICAL_LOSS) != 0u ?
                 ASTRA_POINTER_EVENT_LOSS : 0u),
        .timestamp_ms = logical.timestamp_ms,
        .sequence = logical.sequence,
        .generation = logical.focus_generation,
        .modifiers = logical.modifiers,
        .screen_x = logical.value_x,
        .screen_y = logical.value_y,
        .button = logical.code,
        .wheel_x = logical.code == ASTRA_INPUT_BUTTON_WHEEL_LEFT ? -1 :
                   logical.code == ASTRA_INPUT_BUTTON_WHEEL_RIGHT ? 1 : 0,
        .wheel_y = logical.code == ASTRA_INPUT_BUTTON_WHEEL_UP ? 1 :
                   logical.code == ASTRA_INPUT_BUTTON_WHEEL_DOWN ? -1 : 0,
    };
    observer->_private_generation = logical.focus_generation != 0u ?
        logical.focus_generation : observer->_private_generation;
    return ASTRA_OK;
}

AstraResult astra_pointer_event_try(AstraPointerObserver *observer,
                                    AstraPointerEvent *event)
{
    return receive_event(observer, event, ASTRA_DEADLINE_POLL);
}

AstraResult astra_pointer_event_wait(AstraPointerObserver *observer,
                                     AstraPointerEvent *event,
                                     AstraMonotonicDeadline deadline_ns)
{
    return receive_event(observer, event, deadline_ns);
}

AstraResult astra_pointer_observer_close(AstraPointerObserver *observer)
{
    if (!observer_live(observer))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_internal_input_observer_close(
        &observer->_private_events, &observer->_private_client,
        &observer->_private_generation);
}
