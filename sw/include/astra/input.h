#ifndef ASTRA_INPUT_H
#define ASTRA_INPUT_H

#include <stdint.h>

/* For ASTRA_ABI_ALIGNMENT: an event batch is copied across the syscall. */
#include "astra/syscall.h"

#define ASTRA_INPUT_VERSION_1_1 UINT32_C(0x00010001)

#define ASTRA_INPUT_CAP_KEYBOARD (UINT32_C(1) << 0)
#define ASTRA_INPUT_CAP_POINTER  (UINT32_C(1) << 1)

#define ASTRA_DEVICE_CLASS_INPUT UINT32_C(0x494e5054) /* INPT */
#define ASTRA_DEVICE_ID_INPUT0   UINT32_C(0x494e0001)

#define ASTRA_CAPABILITY_INPUT_DEVICE "INPUT"
#define ASTRA_CAPABILITY_INPUT_IRQ    "INPUT_IRQ"

#define ASTRA_INPUT_STATUS_LEVEL_MASK UINT32_C(0x1f)
#define ASTRA_INPUT_STATUS_VALID      (UINT32_C(1) << 8)
#define ASTRA_INPUT_STATUS_OVERFLOW   (UINT32_C(1) << 9)

#define ASTRA_INPUT_POP_EVENT         (UINT32_C(1) << 0)
#define ASTRA_INPUT_ACK_OVERFLOW      (UINT32_C(1) << 1)

#define ASTRA_INPUT_CLASS_KEYBOARD UINT32_C(1)
#define ASTRA_INPUT_CLASS_POINTER  UINT32_C(2)

#define ASTRA_INPUT_KEY_PHYSICAL UINT32_C(1)

#define ASTRA_INPUT_POINTER_RELATIVE UINT32_C(1)
#define ASTRA_INPUT_POINTER_ABSOLUTE UINT32_C(2)
#define ASTRA_INPUT_POINTER_BUTTON   UINT32_C(3)

#define ASTRA_INPUT_FLAG_DOWN    (UINT32_C(1) << 0)
#define ASTRA_INPUT_FLAG_AXIS_Y  (UINT32_C(1) << 1)

#define ASTRA_INPUT_BUTTON_LEFT       UINT32_C(1)
#define ASTRA_INPUT_BUTTON_MIDDLE     UINT32_C(2)
#define ASTRA_INPUT_BUTTON_RIGHT      UINT32_C(3)
#define ASTRA_INPUT_BUTTON_WHEEL_UP   UINT32_C(4)
#define ASTRA_INPUT_BUTTON_WHEEL_DOWN UINT32_C(5)
#define ASTRA_INPUT_BUTTON_SIDE       UINT32_C(6)
#define ASTRA_INPUT_BUTTON_EXTRA      UINT32_C(7)
#define ASTRA_INPUT_BUTTON_WHEEL_LEFT UINT32_C(8)
#define ASTRA_INPUT_BUTTON_WHEEL_RIGHT UINT32_C(9)

#define ASTRA_INPUT_HEADER(event_class, kind, flags) \
    ((((uint32_t)(event_class) & UINT32_C(0xff)) << 24) | \
     (((uint32_t)(kind) & UINT32_C(0xff)) << 16) | \
     ((uint32_t)(flags) & UINT32_C(0xffff)))

#define ASTRA_INPUT_EVENT_CLASS(header) (((header) >> 24) & UINT32_C(0xff))
#define ASTRA_INPUT_EVENT_KIND(header)  (((header) >> 16) & UINT32_C(0xff))
#define ASTRA_INPUT_EVENT_FLAGS(header) ((header) & UINT32_C(0xffff))

#define ASTRA_INPUT_EVENT_DEVICE(device_sequence) \
    (((device_sequence) >> 16) & UINT32_C(0xffff))
#define ASTRA_INPUT_EVENT_SEQUENCE(device_sequence) \
    ((device_sequence) & UINT32_C(0xffff))

/* Physical keyboard values are USB HID Keyboard/Keypad Usage IDs. */
typedef uint16_t AstraPhysicalKey;

typedef struct AstraInputEvent {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
    uint32_t host_generation;
} AstraInputEvent;

_Static_assert(sizeof(AstraInputEvent) == 20u, "input event ABI size");
_Static_assert(_Alignof(AstraInputEvent) % ASTRA_ABI_ALIGNMENT == 0u,
               "input event must satisfy the syscall alignment rule");

#endif
