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
    uint16_t size;
    uint16_t version;
    uint16_t type;
    uint16_t flags;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t generation;
    int32_t screen_x;
    int32_t screen_y;
    uint32_t button;
    int32_t wheel_x;
    int32_t wheel_y;
} AstraPointerEvent;

_Static_assert(sizeof(AstraPointerEvent) == 40u,
               "pointer event ABI changed");

/** Opaque bounded subscription owned by the calling process. */
typedef struct AstraPointerObserver {
    AstraHandle _private_events;
    uint32_t _private_client;
    uint32_t _private_generation;
} AstraPointerObserver;

#define ASTRA_POINTER_OBSERVER_INIT { ASTRA_INVALID_HANDLE, 0, 0 }

/**
 * Subscribe to screen-space pointer events through a delegated INPUT_SERVICE
 * capability. This API cannot request keyboard events or seat ownership.
 */
ASTRA_NODISCARD AstraResult astra_pointer_observer_open(
    AstraHandle input_service,
    uint32_t subscriptions,
    AstraPointerObserver *observer);

ASTRA_NODISCARD AstraResult astra_pointer_event_try(
    AstraPointerObserver *observer, AstraPointerEvent *event);
ASTRA_NODISCARD AstraResult astra_pointer_event_wait(
    AstraPointerObserver *observer, AstraPointerEvent *event,
    AstraMonotonicDeadline deadline_ns);
ASTRA_NODISCARD AstraResult astra_pointer_observer_close(
    AstraPointerObserver *observer);

ASTRA_EXTERN_C_END

#endif
