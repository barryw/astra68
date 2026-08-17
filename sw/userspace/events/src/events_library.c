#include <astra/events_library.h>

#include <astra/event_emit.h>
#include <astra/library.h>
#include <astra/runtime.h>

ASTRA_LIBRARY("events.library", 1, 0, 0,
              ASTRA_EVENTS_LIBRARY_ABI_MAJOR,
              ASTRA_EVENTS_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

const AstraEventsLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_EVENTS_LIBRARY_ABI_MAJOR,
    ASTRA_EVENTS_LIBRARY_ABI_MINOR,
    sizeof(AstraEventsLibraryV1),
    astra_event_emit,
    astra_event_emit_packed,
    astra_log_write,
    astra_log,
    astra_trace_read,
    astra_event_catalog_init,
    astra_event_catalog_lookup,
    astra_event_catalog_render,
};
