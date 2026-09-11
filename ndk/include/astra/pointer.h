#ifndef ASTRA_POINTER_H
#define ASTRA_POINTER_H

/**
 * @file pointer.h
 * @brief Screen-space pointer observation for applications without windows.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Pointer event wire-format version. */
#define ASTRA_POINTER_EVENT_VERSION UINT16_C(1)

enum {
    ASTRA_POINTER_SUBSCRIBE_MOTION = 1u << 0,
    ASTRA_POINTER_SUBSCRIBE_BUTTON = 1u << 1,
    ASTRA_POINTER_SUBSCRIBE_WHEEL = 1u << 2,
    ASTRA_POINTER_SUBSCRIBE_ALL = ASTRA_POINTER_SUBSCRIBE_MOTION |
                                  ASTRA_POINTER_SUBSCRIBE_BUTTON |
                                  ASTRA_POINTER_SUBSCRIBE_WHEEL
};

enum {
    ASTRA_POINTER_EVENT_MOTION = 1,
    ASTRA_POINTER_EVENT_BUTTON = 2,
    ASTRA_POINTER_EVENT_WHEEL = 3,
    ASTRA_POINTER_EVENT_STATE_RESET = 4
};

enum {
    ASTRA_POINTER_EVENT_DOWN = 1u << 0,
    ASTRA_POINTER_EVENT_SYNTHETIC = 1u << 1,
    ASTRA_POINTER_EVENT_LOSS = 1u << 2
};

/** One logical pointer event. Coordinates are always screen-relative. */
typedef struct AstraPointerEvent {
    uint16_t size; /**< Structure bytes. */
    uint16_t version; /**< ASTRA_POINTER_EVENT_VERSION. */
    uint16_t type; /**< ASTRA_POINTER_EVENT_* type. */
    uint16_t flags; /**< ASTRA_POINTER_EVENT_* flags. */
    uint32_t timestamp_ms; /**< Monotonic event time in milliseconds. */
    uint32_t sequence; /**< Per-seat event sequence. */
    uint32_t generation; /**< Pointer state generation. */
    int32_t screen_x; /**< Screen-relative x coordinate. */
    int32_t screen_y; /**< Screen-relative y coordinate. */
    uint32_t button; /**< Changed button identifier, or zero. */
    int32_t wheel_x; /**< Horizontal wheel delta. */
    int32_t wheel_y; /**< Vertical wheel delta. */
} AstraPointerEvent;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraPointerEvent) == 40u,
               "pointer event ABI changed");
/** @endcond */

/** Opaque bounded subscription owned by the calling process. */
typedef struct AstraPointerObserver {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_events;
    uint32_t _private_client;
    uint32_t _private_generation;
    /** @endcond */
} AstraPointerObserver;

/** Empty pointer-observer initializer. */
#define ASTRA_POINTER_OBSERVER_INIT { ASTRA_INVALID_HANDLE, 0, 0 }

/**
 * Subscribe to screen-space pointer events through a delegated INPUT_SERVICE
 * capability. This API cannot request keyboard events or seat ownership.
 * @param input_service Delegated input-service capability.
 * @param subscriptions ASTRA_POINTER_SUBSCRIBE_* mask.
 * @param observer Receives the observer.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_pointer_observer_open(
    AstraHandle input_service,
    uint32_t subscriptions,
    AstraPointerObserver *observer);

/** Poll one pointer event. @param observer Open observer. @param event Receives the event. @return ASTRA_OK, ASTRA_ERR_WOULD_BLOCK, or an error. */
ASTRA_NODISCARD AstraResult astra_pointer_event_try(
    AstraPointerObserver *observer, AstraPointerEvent *event);
/** Wait for a pointer event. @param observer Open observer. @param event Receives the event. @param deadline_ns Absolute monotonic deadline. @return ASTRA_OK, ASTRA_ERR_TIMED_OUT, or an error. */
ASTRA_NODISCARD AstraResult astra_pointer_event_wait(
    AstraPointerObserver *observer, AstraPointerEvent *event,
    AstraMonotonicDeadline deadline_ns);
/** Close an observer. @param observer Open observer. @return ASTRA_OK or an error. */
ASTRA_NODISCARD AstraResult astra_pointer_observer_close(
    AstraPointerObserver *observer);

ASTRA_EXTERN_C_END

#endif
