#ifndef ASTRA_INPUT_H
#define ASTRA_INPUT_H

/**
 * @file input.h
 * @brief The raw input device: capabilities, the Vesta input-queue registers'
 * flags, and the event batch `ASTRA_SYSCALL_INPUT_READ_TRY` copies across the
 * syscall boundary.
 *
 * This is the physical layer underneath the input *service*
 * (`sw/userspace/input`): one FIFO of ::AstraInputEvent records, drained a
 * handful at a time into a process holding a lease on
 * ::ASTRA_DEVICE_ID_INPUT0. The service turns these into the logical,
 * per-client event stream (focus, key repeat, pointer acceleration); this
 * header only describes what the device itself hands over.
 */

#include <astra/compiler.h>
#include <stdint.h>

/* For ASTRA_ABI_ALIGNMENT: an event batch is copied across the syscall. */
#include "astra/syscall.h"

/** @defgroup astra_input Raw input device ABI
 *  @brief The input device's identity, queue-status bits and the wire shape
 *  of one physical input event.
 *  @{
 */

/** Input device ABI version 1.1 (major.minor packed 16.16); the Vesta input device currently advertises 1.0 only. */
#define ASTRA_INPUT_VERSION_1_1 UINT32_C(0x00010001)

/** Device capability bit: a keyboard is attached. */
#define ASTRA_INPUT_CAP_KEYBOARD (UINT32_C(1) << 0)
/** Device capability bit: a pointer is attached. */
#define ASTRA_INPUT_CAP_POINTER  (UINT32_C(1) << 1)

/** Device class id ("INPT") identifying the input device to `ASTRA_SYSCALL_INPUT_READ_TRY` and the device registry. */
#define ASTRA_DEVICE_CLASS_INPUT UINT32_C(0x494e5054) /* INPT */
/** The one input device's id on this machine. */
#define ASTRA_DEVICE_ID_INPUT0   UINT32_C(0x494e0001)

/** Startup capability name for a lease on ::ASTRA_DEVICE_ID_INPUT0. */
#define ASTRA_CAPABILITY_INPUT_DEVICE "INPUT"
/** Startup capability name for the input device's interrupt endpoint. */
#define ASTRA_CAPABILITY_INPUT_IRQ    "INPUT_IRQ"

/** Mask isolating the queued-event count within the input status word. */
#define ASTRA_INPUT_STATUS_LEVEL_MASK UINT32_C(0x1f)
/** Largest number of events the device FIFO can hold, equal to ::ASTRA_INPUT_STATUS_LEVEL_MASK. */
#define ASTRA_INPUT_FIFO_CAPACITY ASTRA_INPUT_STATUS_LEVEL_MASK
/** Status bit: the head of the FIFO holds a valid event. */
#define ASTRA_INPUT_STATUS_VALID      (UINT32_C(1) << 8)
/** Status bit: an event was dropped because the FIFO was full; cleared by ::ASTRA_INPUT_ACK_OVERFLOW. */
#define ASTRA_INPUT_STATUS_OVERFLOW   (UINT32_C(1) << 9)

/** Input-pop register bit: discard the FIFO's head event. */
#define ASTRA_INPUT_POP_EVENT         (UINT32_C(1) << 0)
/** Input-pop register bit: acknowledge and clear ::ASTRA_INPUT_STATUS_OVERFLOW. */
#define ASTRA_INPUT_ACK_OVERFLOW      (UINT32_C(1) << 1)

/** `AstraInputEvent::header` event class: a keyboard event. */
#define ASTRA_INPUT_CLASS_KEYBOARD UINT32_C(1)
/** `AstraInputEvent::header` event class: a pointer event. */
#define ASTRA_INPUT_CLASS_POINTER  UINT32_C(2)

/** ::ASTRA_INPUT_CLASS_KEYBOARD kind: a physical key, identified by USB HID usage id in `AstraInputEvent::value`. */
#define ASTRA_INPUT_KEY_PHYSICAL UINT32_C(1)

/** ::ASTRA_INPUT_CLASS_POINTER kind: `AstraInputEvent::value` is a signed motion delta. */
#define ASTRA_INPUT_POINTER_RELATIVE UINT32_C(1)
/** ::ASTRA_INPUT_CLASS_POINTER kind: `AstraInputEvent::value` is an absolute coordinate. */
#define ASTRA_INPUT_POINTER_ABSOLUTE UINT32_C(2)
/** ::ASTRA_INPUT_CLASS_POINTER kind: `AstraInputEvent::value` is an `ASTRA_INPUT_BUTTON_*` code. */
#define ASTRA_INPUT_POINTER_BUTTON   UINT32_C(3)

/** `AstraInputEvent::header` flag: the key or button is pressed; clear means released. */
#define ASTRA_INPUT_FLAG_DOWN    (UINT32_C(1) << 0)
/** `AstraInputEvent::header` flag: a relative or absolute pointer value is the Y axis; clear means X. */
#define ASTRA_INPUT_FLAG_AXIS_Y  (UINT32_C(1) << 1)

/** Pointer button code: left button. */
#define ASTRA_INPUT_BUTTON_LEFT       UINT32_C(1)
/** Pointer button code: middle button. */
#define ASTRA_INPUT_BUTTON_MIDDLE     UINT32_C(2)
/** Pointer button code: right button. */
#define ASTRA_INPUT_BUTTON_RIGHT      UINT32_C(3)
/** Pointer button code: wheel scrolled up. */
#define ASTRA_INPUT_BUTTON_WHEEL_UP   UINT32_C(4)
/** Pointer button code: wheel scrolled down. */
#define ASTRA_INPUT_BUTTON_WHEEL_DOWN UINT32_C(5)
/** Pointer button code: a side button. */
#define ASTRA_INPUT_BUTTON_SIDE       UINT32_C(6)
/** Pointer button code: an extra button. */
#define ASTRA_INPUT_BUTTON_EXTRA      UINT32_C(7)
/** Pointer button code: wheel scrolled left. */
#define ASTRA_INPUT_BUTTON_WHEEL_LEFT UINT32_C(8)
/** Pointer button code: wheel scrolled right. */
#define ASTRA_INPUT_BUTTON_WHEEL_RIGHT UINT32_C(9)

/**
 * Pack an `AstraInputEvent::header` value.
 * @param event_class ::ASTRA_INPUT_CLASS_KEYBOARD or ::ASTRA_INPUT_CLASS_POINTER.
 * @param kind Class-specific kind, e.g. ::ASTRA_INPUT_KEY_PHYSICAL or an `ASTRA_INPUT_POINTER_*` value.
 * @param flags Class-specific flags, e.g. ::ASTRA_INPUT_FLAG_DOWN or ::ASTRA_INPUT_FLAG_AXIS_Y.
 * @return The packed header value.
 */
#define ASTRA_INPUT_HEADER(event_class, kind, flags) \
    ((((uint32_t)(event_class) & UINT32_C(0xff)) << 24) | \
     (((uint32_t)(kind) & UINT32_C(0xff)) << 16) | \
     ((uint32_t)(flags) & UINT32_C(0xffff)))

/**
 * Extract the event class from a packed header.
 * @param header A value built by ::ASTRA_INPUT_HEADER.
 * @return The ::ASTRA_INPUT_CLASS_KEYBOARD / ::ASTRA_INPUT_CLASS_POINTER class.
 */
#define ASTRA_INPUT_EVENT_CLASS(header) (((header) >> 24) & UINT32_C(0xff))
/**
 * Extract the class-specific kind from a packed header.
 * @param header A value built by ::ASTRA_INPUT_HEADER.
 * @return The kind value, meaningful within the header's event class.
 */
#define ASTRA_INPUT_EVENT_KIND(header)  (((header) >> 16) & UINT32_C(0xff))
/**
 * Extract the class-specific flags from a packed header.
 * @param header A value built by ::ASTRA_INPUT_HEADER.
 * @return The flags value, meaningful within the header's event class.
 */
#define ASTRA_INPUT_EVENT_FLAGS(header) ((header) & UINT32_C(0xffff))

/**
 * Extract the originating device id from `AstraInputEvent::device_sequence`.
 * @param device_sequence The event's `device_sequence` field.
 * @return The device id in bits 31:16.
 */
#define ASTRA_INPUT_EVENT_DEVICE(device_sequence) \
    (((device_sequence) >> 16) & UINT32_C(0xffff))
/**
 * Extract the per-device sequence number from `AstraInputEvent::device_sequence`.
 * @param device_sequence The event's `device_sequence` field.
 * @return The sequence number in bits 15:0.
 */
#define ASTRA_INPUT_EVENT_SEQUENCE(device_sequence) \
    ((device_sequence) & UINT32_C(0xffff))

/** A USB HID Keyboard/Keypad Usage ID, naming one physical key. */
typedef uint16_t AstraPhysicalKey;

/**
 * One physical input event, copied across `ASTRA_SYSCALL_INPUT_READ_TRY`.
 *
 * This is the device's native event: one key transition, one relative or
 * absolute pointer update, or one button transition. The input service
 * ingests a batch of these and turns them into the logical, per-client
 * event stream; nothing upstream of the service sees this type.
 */
typedef struct AstraInputEvent {
    /** Packed event class, kind and flags; decode with ::ASTRA_INPUT_EVENT_CLASS, ::ASTRA_INPUT_EVENT_KIND and ::ASTRA_INPUT_EVENT_FLAGS. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t header;
    /** The event's value: a HID usage id, a pointer delta or coordinate, or an `ASTRA_INPUT_BUTTON_*` code, depending on `header`. */
    uint32_t value;
    /** Event timestamp in milliseconds, from the input device's clock. */
    uint32_t timestamp_ms;
    /** Originating device id and per-device sequence number, packed; decode with ::ASTRA_INPUT_EVENT_DEVICE and ::ASTRA_INPUT_EVENT_SEQUENCE. */
    uint32_t device_sequence;
    /** Generation counter of the input device; a change from the previous event's value means device state (held keys, repeat) was reset and must be rebuilt. */
    uint32_t host_generation;
} AstraInputEvent;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraInputEvent) == 20u, "input event ABI size");
_Static_assert(_Alignof(AstraInputEvent) % ASTRA_ABI_ALIGNMENT == 0u,
               "input event must satisfy the syscall alignment rule");
/** @endcond */

/** @} */

#endif
