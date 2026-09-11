/** @file events_library.h @brief Structured-event shared-library ABI. */
#ifndef ASTRA_EVENTS_LIBRARY_H
#define ASTRA_EVENTS_LIBRARY_H

#include <stdint.h>

#include <astra/event.h>
#include <astra/event_catalog.h>
#include <astra/event_descriptor.h>

/** Events Kit export-table ABI major version. */
#define ASTRA_EVENTS_LIBRARY_ABI_MAJOR 1u
/** Events Kit export-table ABI minor version. */
#define ASTRA_EVENTS_LIBRARY_ABI_MINOR 0u

/** Events Kit 1.x immutable export table. */
typedef struct AstraEventsLibraryV1 {
    uint16_t abi_major; /**< ASTRA_EVENTS_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_EVENTS_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */
    /** Emit an event with an explicit payload. */
    uint32_t (*emit)(uint32_t, uint32_t, const void *, uint32_t);
    /** Emit an event whose payload is packed from words. */
    uint32_t (*emit_packed)(const AstraEventDescriptor *, uint32_t,
                            const uint32_t *, uint32_t);
    /** Append raw bytes to the system log. */
    uint32_t (*log_write)(const void *, uint32_t);
    /** Append one NUL-terminated line to the system log. */
    uint32_t (*log)(const char *);
    /** Drain structured trace records. */
    uint32_t (*trace_read)(uint32_t, uint32_t *, AstraEventDrained *,
                           uint32_t, uint32_t *, uint32_t *);
    /** Adopt an event catalog view. */
    int (*catalog_init)(AstraEventCatalog *, const void *, uint32_t,
                        uint32_t);
    /** Find a descriptor by message identifier. */
    const AstraEventDescriptor *(*catalog_lookup)(const AstraEventCatalog *,
                                                   uint32_t);
    /** Render one structured event with its catalog. */
    uint32_t (*catalog_render)(const AstraEventCatalog *, uint32_t, uint16_t,
                               const uint8_t *, uint32_t, char *, uint32_t);
} AstraEventsLibraryV1;

#endif
