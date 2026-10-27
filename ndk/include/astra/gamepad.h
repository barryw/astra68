#ifndef ASTRA_GAMEPAD_H
#define ASTRA_GAMEPAD_H

/** @file gamepad.h Native gamepad hotplug and control events. */

#include <astra/compiler.h>
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>
#include <stdint.h>

ASTRA_EXTERN_C_BEGIN

/** Current gamepad event ABI version. */
#define ASTRA_GAMEPAD_EVENT_VERSION UINT16_C(1)
/** Device arrival or removal; DOWN marks arrival. */
#define ASTRA_GAMEPAD_EVENT_DEVICE UINT16_C(1)
/** Analog axis changed. */
#define ASTRA_GAMEPAD_EVENT_AXIS   UINT16_C(2)
/** Button changed; DOWN marks pressed state. */
#define ASTRA_GAMEPAD_EVENT_BUTTON UINT16_C(3)
/** Directional hat changed. */
#define ASTRA_GAMEPAD_EVENT_HAT    UINT16_C(4)
/** Observer lost events and must rebuild device state. */
#define ASTRA_GAMEPAD_EVENT_RESET  UINT16_C(5)
/** Flag marking device arrival or button press. */
#define ASTRA_GAMEPAD_EVENT_DOWN   UINT16_C(1)

/** DEVICE carries vendor/product, bus/version and control counts. */
typedef struct AstraGamepadEvent {
    uint16_t size; /**< Structure size in bytes. */
    uint16_t version; /**< ASTRA_GAMEPAD_EVENT_VERSION. */
    uint16_t type; /**< ASTRA_GAMEPAD_EVENT_* type. */
    uint16_t flags; /**< Event flags, including ASTRA_GAMEPAD_EVENT_DOWN. */
    uint32_t timestamp_ms; /**< Monotonic event time in milliseconds. */
    uint32_t sequence; /**< Per-observer event sequence. */
    uint16_t slot; /**< Device slot. */
    uint16_t index; /**< Axis, button, or hat index. */
    uint16_t vendor; /**< USB vendor ID on DEVICE events. */
    uint16_t product; /**< USB product ID on DEVICE events. */
    uint16_t bus; /**< Input bus on DEVICE events. */
    uint16_t product_version; /**< Device version on DEVICE events. */
    uint8_t axes; /**< Axis count on DEVICE events. */
    uint8_t buttons; /**< Button count on DEVICE events. */
    uint8_t hats; /**< Hat count on DEVICE events. */
    uint8_t reserved; /**< Reserved; ignore on input. */
    int32_t value; /**< Current control value. */
} AstraGamepadEvent;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraGamepadEvent) == 36u,
               "gamepad event ABI changed");
/** @endcond */

/** Caller-owned subscription to gamepad events. */
typedef struct AstraGamepadObserver {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_events;
    uint32_t _private_client;
    uint32_t _private_generation;
    /** @endcond */
} AstraGamepadObserver;

/** Empty gamepad observer initializer. */
#define ASTRA_GAMEPAD_OBSERVER_INIT { ASTRA_INVALID_HANDLE, 0, 0 }

/** Subscribe using delegated INPUT_SERVICE; existing devices arrive first.
 * @param input_service Delegated input service handle.
 * @param observer Receives subscription state.
 * @return ASTRA_OK or service/validation error.
 */
ASTRA_NODISCARD AstraResult astra_gamepad_observer_open(
    AstraHandle input_service, AstraGamepadObserver *observer);
/** Poll next event without blocking.
 * @param observer Open subscription.
 * @param event Receives next event when available.
 * @return ASTRA_OK or empty/service error.
 */
ASTRA_NODISCARD AstraResult astra_gamepad_event_try(
    AstraGamepadObserver *observer, AstraGamepadEvent *event);
/** Wait for next event until an absolute monotonic deadline.
 * @param observer Open subscription.
 * @param event Receives next event when available.
 * @param deadline_ns Absolute monotonic deadline in nanoseconds.
 * @return ASTRA_OK, timeout, or service error.
 */
ASTRA_NODISCARD AstraResult astra_gamepad_event_wait(
    AstraGamepadObserver *observer, AstraGamepadEvent *event,
    AstraMonotonicDeadline deadline_ns);
/** Close subscription.
 * @param observer Open subscription.
 * @return ASTRA_OK or service/validation error.
 */
ASTRA_NODISCARD AstraResult astra_gamepad_observer_close(
    AstraGamepadObserver *observer);

ASTRA_EXTERN_C_END

#endif
