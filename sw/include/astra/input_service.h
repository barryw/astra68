#ifndef ASTRA_INPUT_SERVICE_H
#define ASTRA_INPUT_SERVICE_H

#include <stdint.h>
#include <astra/syscall.h>

#define ASTRA_INPUT_SERVICE_PROTOCOL UINT32_C(0x494e5054) /* INPT */
#define ASTRA_INPUT_SERVICE_VERSION  UINT16_C(1)

#define ASTRA_CAPABILITY_INPUT_SERVICE "INPUT_SERVICE"

#define ASTRA_INPUT_CLIENT_MAX 8u
#define ASTRA_INPUT_EVENT_SIZE 32u

#define ASTRA_INPUT_EVENT_KEY            UINT16_C(1)
#define ASTRA_INPUT_EVENT_TEXT           UINT16_C(2)
#define ASTRA_INPUT_EVENT_POINTER_MOTION UINT16_C(3)
#define ASTRA_INPUT_EVENT_POINTER_BUTTON UINT16_C(4)
#define ASTRA_INPUT_EVENT_FOCUS          UINT16_C(5)
#define ASTRA_INPUT_EVENT_STATE_RESET    UINT16_C(6)

#define ASTRA_INPUT_OPERATION_CONNECT   UINT32_C(1)
#define ASTRA_INPUT_OPERATION_CONNECTED UINT32_C(2)
#define ASTRA_INPUT_OPERATION_EVENT     UINT32_C(3)

#define ASTRA_INPUT_LOGICAL_DOWN      (UINT16_C(1) << 0)
#define ASTRA_INPUT_LOGICAL_REPEAT    (UINT16_C(1) << 1)
#define ASTRA_INPUT_LOGICAL_SYNTHETIC (UINT16_C(1) << 2)
#define ASTRA_INPUT_LOGICAL_FOCUSED   (UINT16_C(1) << 3)
#define ASTRA_INPUT_LOGICAL_LOSS      (UINT16_C(1) << 4)

#define ASTRA_INPUT_SUBSCRIBE_POINTER_MOTION (UINT32_C(1) << 0)
#define ASTRA_INPUT_SUBSCRIBE_POINTER_BUTTON (UINT32_C(1) << 1)
#define ASTRA_INPUT_SUBSCRIBE_POINTER_WHEEL  (UINT32_C(1) << 2)
#define ASTRA_INPUT_SUBSCRIBE_KEY            (UINT32_C(1) << 3)
#define ASTRA_INPUT_SUBSCRIBE_TEXT           (UINT32_C(1) << 4)
#define ASTRA_INPUT_SUBSCRIBE_FOCUS          (UINT32_C(1) << 5)
#define ASTRA_INPUT_SUBSCRIBE_ALL \
    (ASTRA_INPUT_SUBSCRIBE_POINTER_MOTION | \
     ASTRA_INPUT_SUBSCRIBE_POINTER_BUTTON | \
     ASTRA_INPUT_SUBSCRIBE_POINTER_WHEEL | ASTRA_INPUT_SUBSCRIBE_KEY | \
     ASTRA_INPUT_SUBSCRIBE_TEXT | ASTRA_INPUT_SUBSCRIBE_FOCUS)

#define ASTRA_INPUT_CONNECT_SEAT_OWNER (UINT32_C(1) << 0)

#define ASTRA_INPUT_MOD_LEFT_CTRL   (UINT32_C(1) << 0)
#define ASTRA_INPUT_MOD_LEFT_SHIFT  (UINT32_C(1) << 1)
#define ASTRA_INPUT_MOD_LEFT_ALT    (UINT32_C(1) << 2)
#define ASTRA_INPUT_MOD_LEFT_GUI    (UINT32_C(1) << 3)
#define ASTRA_INPUT_MOD_RIGHT_CTRL  (UINT32_C(1) << 4)
#define ASTRA_INPUT_MOD_RIGHT_SHIFT (UINT32_C(1) << 5)
#define ASTRA_INPUT_MOD_RIGHT_ALT   (UINT32_C(1) << 6)
#define ASTRA_INPUT_MOD_RIGHT_GUI   (UINT32_C(1) << 7)
#define ASTRA_INPUT_MOD_CAPS_LOCK   (UINT32_C(1) << 8)

#define ASTRA_INPUT_MOD_CTRL \
    (ASTRA_INPUT_MOD_LEFT_CTRL | ASTRA_INPUT_MOD_RIGHT_CTRL)
#define ASTRA_INPUT_MOD_SHIFT \
    (ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_RIGHT_SHIFT)
#define ASTRA_INPUT_MOD_ALT \
    (ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_RIGHT_ALT)
#define ASTRA_INPUT_MOD_GUI \
    (ASTRA_INPUT_MOD_LEFT_GUI | ASTRA_INPUT_MOD_RIGHT_GUI)

typedef struct AstraLogicalInputEvent {
    uint16_t size;
    uint16_t version;
    uint16_t type;
    uint16_t flags;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t focus_generation;
    uint32_t code;
    int32_t value_x;
    int32_t value_y;
} AstraLogicalInputEvent;

_Static_assert(sizeof(AstraLogicalInputEvent) == ASTRA_INPUT_EVENT_SIZE,
               "logical input event ABI size changed");

typedef struct AstraInputEventMessage {
    AstraMessageHeader header;
    AstraLogicalInputEvent event;
} AstraInputEventMessage;

typedef struct AstraInputConnect {
    AstraMessageHeader header;
    uint32_t subscriptions;
    uint32_t flags;
} AstraInputConnect;

typedef struct AstraInputConnected {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t client;
    uint32_t generation;
} AstraInputConnected;

_Static_assert(sizeof(AstraInputEventMessage) ==
                   ASTRA_MESSAGE_HEADER_SIZE + ASTRA_INPUT_EVENT_SIZE,
               "input event message ABI size changed");
_Static_assert(sizeof(AstraInputConnect) == 32u,
               "input connect message ABI changed");
_Static_assert(sizeof(AstraInputConnected) == 36u,
               "input connected message ABI changed");

#endif
