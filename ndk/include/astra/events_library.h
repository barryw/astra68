#ifndef ASTRA_EVENTS_LIBRARY_H
#define ASTRA_EVENTS_LIBRARY_H

#include <stdint.h>

#include <astra/event.h>
#include <astra/event_catalog.h>
#include <astra/event_descriptor.h>

#define ASTRA_EVENTS_LIBRARY_ABI_MAJOR 1u
#define ASTRA_EVENTS_LIBRARY_ABI_MINOR 0u

typedef struct AstraEventsLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    uint32_t (*emit)(uint32_t, uint32_t, const void *, uint32_t);
    uint32_t (*emit_packed)(const AstraEventDescriptor *, uint32_t,
                            const uint32_t *, uint32_t);
    uint32_t (*log_write)(const void *, uint32_t);
    uint32_t (*log)(const char *);
    uint32_t (*trace_read)(uint32_t, uint32_t *, AstraEventDrained *,
                           uint32_t, uint32_t *, uint32_t *);
    int (*catalog_init)(AstraEventCatalog *, const void *, uint32_t,
                        uint32_t);
    const AstraEventDescriptor *(*catalog_lookup)(const AstraEventCatalog *,
                                                   uint32_t);
    uint32_t (*catalog_render)(const AstraEventCatalog *, uint32_t, uint16_t,
                               const uint8_t *, uint32_t, char *, uint32_t);
} AstraEventsLibraryV1;

#endif
