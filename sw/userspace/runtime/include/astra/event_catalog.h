#ifndef ASTRA_EVENT_CATALOG_H
#define ASTRA_EVENT_CATALOG_H

#include <stdint.h>

#include <astra/event_descriptor.h>

/*
 * The catalog, on the machine.
 *
 * The file is the `.astra_events` section's bytes verbatim -- what
 * `objcopy -O binary --only-section=.astra_events` produces -- and this is the
 * whole reader: a bounds check and a division. A message id is the descriptor's
 * address, descriptors are a fixed 128 bytes, so the record for an id is at
 * `(id - base) / 128`. An index, not a parse.
 *
 * That is the point of the shape. Rendering an event's text at read time means
 * resolving format strings on a 30 MHz machine, and it is affordable only
 * because nothing here parses anything: the bytes are this build's own struct,
 * in this machine's own byte order, so a lookup is a pointer and a render walks
 * one string once.
 *
 * A catalog from a foreign-endian build is not this reader's problem. Reading
 * one is what tools/event_catalog.py and tools/trace_decode.py are for, off the
 * machine, where a byte swap costs nothing anybody notices.
 */

typedef struct AstraEventCatalog {
    const AstraEventDescriptor *records;
    uint32_t count;
    uint32_t base;   /* the message id of records[0] */
} AstraEventCatalog;

/*
 * Adopts `bytes` in place; nothing is copied and the caller keeps it alive.
 * Refuses a length that is not a whole number of descriptors, a misaligned
 * buffer, or a first record whose magic is wrong -- each of which means the
 * file is not this build's catalog, and a catalog that is nearly right renders
 * every event wrongly rather than failing once.
 *
 * Returns 1 on success and 0 on refusal, leaving an empty catalog behind: a
 * reader with no catalog shows ids, which is honest, rather than text it made
 * up.
 */
int astra_event_catalog_init(AstraEventCatalog *catalog, const void *bytes,
                             uint32_t size, uint32_t base);

/* The descriptor for `message`, or NULL when the id is not this catalog's. */
const AstraEventDescriptor *
astra_event_catalog_lookup(const AstraEventCatalog *catalog, uint32_t message);

/*
 * Renders one occurrence into `out`, always NUL-terminated, and returns the
 * length written.
 *
 * The format's conversions are the argument types the macro can produce: `%u`,
 * `%d`, `%x` for a packed word and `%s` for the inline string, plus `%%`. A
 * conversion the format asks for and the record does not carry renders as
 * `<missing>` rather than reading past the payload -- a log that invents a
 * value is worse than one that admits it has none.
 *
 * An unknown catalog, or an id it does not hold, renders as the id itself.
 * That is the honest answer and it is still enough to grep for.
 */
uint32_t astra_event_catalog_render(const AstraEventCatalog *catalog,
                                    uint32_t message, uint16_t flags,
                                    const uint8_t *payload,
                                    uint32_t payload_length, char *out,
                                    uint32_t capacity);

#endif
