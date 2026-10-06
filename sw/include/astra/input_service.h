#ifndef ASTRA_INPUT_SERVICE_H
#define ASTRA_INPUT_SERVICE_H

#include <stdint.h>
#include <astra/input_modifiers.h>
#include <astra/limits.h>
#include <astra/syscall.h>

#define ASTRA_INPUT_SERVICE_PROTOCOL UINT32_C(0x494e5054) /* INPT */
#define ASTRA_INPUT_SERVICE_VERSION  UINT16_C(3)

#define ASTRA_CAPABILITY_INPUT_SERVICE "INPUT_SERVICE"

/*
 * The service keeps four permanent handles (its process, receive port, input
 * device and IRQ). Accepting a client temporarily receives two more handles;
 * after the reply handle closes, the new client's event handle remains.  This
 * is therefore the largest client set the process handle table can represent,
 * rather than a second, guessed client quota.
 */
#define ASTRA_INPUT_CLIENT_MAX (ASTRA_HANDLE_COUNT_MAX - 5u)
#define ASTRA_INPUT_EVENT_SIZE 44u

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
/*
 * Shared pointer position, Haiku's shared cursor (input_server's
 * fCursorBuffer). CONNECT carries a third handle, an area of at least
 * sizeof(AstraInputPointerState) with READ, WRITE and MAP rights. The service
 * publishes every pointer motion there instead of sending it, and the
 * client samples it when it is ready -- the display once a frame -- so a
 * mouse reporting at 1000 Hz wakes the client no more often than it looks.
 * Buttons and keys still arrive as events, after the position they happen
 * at is published.
 *
 * Since version 3 the seat owner also writes an AstraInputPointerControl
 * at ASTRA_INPUT_POINTER_CONTROL_OFFSET in the same area: where the pointer
 * may go and where to warp it. The service applies it before the next
 * device input and reports the control sequence it applied in the state,
 * so the owner can tell a position from before its warp from one after.
 */
#define ASTRA_INPUT_CONNECT_SHARED_POINTER (UINT32_C(1) << 1)

typedef struct AstraLogicalInputEvent {
    uint16_t size;
    uint16_t version;
    uint16_t type;
    uint16_t flags;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t focus_generation;
    uint32_t code;
    /** Normalized modifier state at the time of every input event. */
    uint32_t modifiers;
    int32_t value_x;
    int32_t value_y;
    /* POINTER_MOTION: the device's own motion, unaccelerated and never
       clamped, summed since the service started and wrapping. A reader
       subtracts two to get the motion between them, however many events
       were coalesced in between: what relative mouse modes read. */
    int32_t total_x;
    int32_t total_y;
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

/*
 * The newest pointer position, written only by the input service. sequence
 * is odd while a write is in progress and advances by two per position; a
 * reader that is preempted by the writer sees it change and reads again.
 */
typedef struct AstraInputPointerState {
    uint32_t sequence;
    int32_t x;
    int32_t y;
    uint32_t modifiers;
    uint32_t timestamp_ms;
    /* The motion totals of the newest position (AstraLogicalInputEvent). */
    int32_t total_x;
    int32_t total_y;
    /* The AstraInputPointerControl sequence applied before this position. */
    uint32_t control;
} AstraInputPointerState;

/*
 * Written only by the seat owner, read by the service; sequence is odd
 * while a write is in progress. The pointer stays inside [left, right) x
 * [top, bottom), which the service intersects with the screen; an empty
 * rectangle holds it where it is. When warp changes the pointer moves to
 * (warp_x, warp_y), kept inside the rectangle.
 */
#define ASTRA_INPUT_POINTER_CONTROL_OFFSET 32u
typedef struct AstraInputPointerControl {
    uint32_t sequence;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    uint32_t warp;
    int32_t warp_x;
    int32_t warp_y;
} AstraInputPointerControl;

_Static_assert(sizeof(AstraInputPointerState) <=
                   ASTRA_INPUT_POINTER_CONTROL_OFFSET,
               "pointer control must follow the pointer state");
#define ASTRA_INPUT_POINTER_AREA_BYTES \
    (ASTRA_INPUT_POINTER_CONTROL_OFFSET + sizeof(AstraInputPointerControl))

static inline void astra_input_pointer_publish(
    volatile AstraInputPointerState *state, int32_t x, int32_t y,
    uint32_t modifiers, uint32_t timestamp_ms, int32_t total_x,
    int32_t total_y, uint32_t control)
{
    uint32_t sequence = state->sequence;

    state->sequence = sequence + 1u;
    __asm__ __volatile__("" ::: "memory");
    state->x = x;
    state->y = y;
    state->modifiers = modifiers;
    state->timestamp_ms = timestamp_ms;
    state->total_x = total_x;
    state->total_y = total_y;
    state->control = control;
    __asm__ __volatile__("" ::: "memory");
    state->sequence = sequence + 2u;
}

static inline void astra_input_pointer_control_write(
    volatile AstraInputPointerControl *control,
    const AstraInputPointerControl *value)
{
    uint32_t sequence = control->sequence;

    control->sequence = sequence + 1u;
    __asm__ __volatile__("" ::: "memory");
    control->left = value->left;
    control->top = value->top;
    control->right = value->right;
    control->bottom = value->bottom;
    control->warp = value->warp;
    control->warp_x = value->warp_x;
    control->warp_y = value->warp_y;
    __asm__ __volatile__("" ::: "memory");
    control->sequence = sequence + 2u;
}

/* A consistent copy and its sequence; 0 means nothing was ever written. */
static inline uint32_t astra_input_pointer_control_read(
    const volatile AstraInputPointerControl *control,
    AstraInputPointerControl *copy)
{
    uint32_t sequence;

    do {
        sequence = control->sequence;
        __asm__ __volatile__("" ::: "memory");
        copy->left = control->left;
        copy->top = control->top;
        copy->right = control->right;
        copy->bottom = control->bottom;
        copy->warp = control->warp;
        copy->warp_x = control->warp_x;
        copy->warp_y = control->warp_y;
        __asm__ __volatile__("" ::: "memory");
    } while ((sequence & 1u) != 0u || sequence != control->sequence);
    copy->sequence = sequence;
    return sequence;
}

/* A consistent copy, and its sequence; 0 never names a position. */
static inline uint32_t astra_input_pointer_read(
    const volatile AstraInputPointerState *state,
    AstraInputPointerState *copy)
{
    uint32_t sequence;

    do {
        sequence = state->sequence;
        __asm__ __volatile__("" ::: "memory");
        copy->x = state->x;
        copy->y = state->y;
        copy->modifiers = state->modifiers;
        copy->timestamp_ms = state->timestamp_ms;
        copy->total_x = state->total_x;
        copy->total_y = state->total_y;
        copy->control = state->control;
        __asm__ __volatile__("" ::: "memory");
    } while ((sequence & 1u) != 0u || sequence != state->sequence);
    copy->sequence = sequence;
    return sequence;
}

_Static_assert(sizeof(AstraInputEventMessage) ==
                   ASTRA_MESSAGE_HEADER_SIZE + ASTRA_INPUT_EVENT_SIZE,
               "input event message ABI size changed");
_Static_assert(sizeof(AstraInputConnect) == 32u,
               "input connect message ABI changed");
_Static_assert(sizeof(AstraInputPointerState) == 32u,
               "shared pointer state ABI changed");
_Static_assert(sizeof(AstraInputConnected) == 36u,
               "input connected message ABI changed");

#endif
