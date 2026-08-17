#include <astra/input_library.h>

#include <astra/library.h>

ASTRA_LIBRARY("input.library", 1, 0, 0,
              ASTRA_INPUT_LIBRARY_ABI_MAJOR,
              ASTRA_INPUT_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

const AstraInputLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_INPUT_LIBRARY_ABI_MAJOR,
    ASTRA_INPUT_LIBRARY_ABI_MINOR,
    sizeof(AstraInputLibraryV1),
    astra_pointer_observer_open,
    astra_pointer_event_try,
    astra_pointer_event_wait,
    astra_pointer_observer_close,
};
