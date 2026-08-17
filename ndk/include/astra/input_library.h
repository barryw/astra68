#ifndef ASTRA_INPUT_LIBRARY_H
#define ASTRA_INPUT_LIBRARY_H

#include <astra/pointer.h>

#define ASTRA_INPUT_LIBRARY_ABI_MAJOR 1u
#define ASTRA_INPUT_LIBRARY_ABI_MINOR 0u

typedef struct AstraInputLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    AstraResult (*pointer_observer_open)(AstraHandle, uint32_t,
                                         AstraPointerObserver *);
    AstraResult (*pointer_event_try)(AstraPointerObserver *,
                                     AstraPointerEvent *);
    AstraResult (*pointer_event_wait)(AstraPointerObserver *,
                                      AstraPointerEvent *,
                                      AstraMonotonicDeadline);
    AstraResult (*pointer_observer_close)(AstraPointerObserver *);
} AstraInputLibraryV1;

#endif
