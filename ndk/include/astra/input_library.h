/** @file input_library.h @brief Input Kit shared-library ABI. */
#ifndef ASTRA_INPUT_LIBRARY_H
#define ASTRA_INPUT_LIBRARY_H

#include <astra/pointer.h>

/** Input Kit export-table ABI major version. */
#define ASTRA_INPUT_LIBRARY_ABI_MAJOR 1u
/** Input Kit export-table ABI minor version. */
#define ASTRA_INPUT_LIBRARY_ABI_MINOR 0u

/** Input Kit 1.x immutable export table. */
typedef struct AstraInputLibraryV1 {
    uint16_t abi_major; /**< ASTRA_INPUT_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_INPUT_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */
    /** Subscribe to pointer events. */
    AstraResult (*pointer_observer_open)(AstraHandle, uint32_t,
                                         AstraPointerObserver *);
    /** Receive a pointer event without waiting. */
    AstraResult (*pointer_event_try)(AstraPointerObserver *,
                                     AstraPointerEvent *);
    /** Receive a pointer event until an absolute deadline. */
    AstraResult (*pointer_event_wait)(AstraPointerObserver *,
                                      AstraPointerEvent *,
                                      AstraMonotonicDeadline);
    /** Close a pointer observer. */
    AstraResult (*pointer_observer_close)(AstraPointerObserver *);
} AstraInputLibraryV1;

#endif
