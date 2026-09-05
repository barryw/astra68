/*
 * Resolving a message id to its text, on the machine.
 *
 * See astra/event_catalog.h for why this is an index rather than a parser. The
 * two things this file must never do are allocate and read past a bound: it
 * runs inside the reader that answers EVENTS:, and a renderer that can fault is
 * a machine that cannot be asked what went wrong.
 */

#include <astra/event_catalog.h>

#include <stddef.h>

#include <astra/event.h>

static uint32_t
append(char *out, uint32_t capacity, uint32_t at, const char *text)
{
    while (*text != '\0' && at + 1u < capacity) {
        out[at++] = *text++;
    }
    return at;
}

static uint32_t
append_unsigned(char *out, uint32_t capacity, uint32_t at, uint32_t value,
                uint32_t radix)
{
    static const char digits[] = "0123456789abcdef";
    char reversed[10];
    uint32_t count = 0u;

    if (value == 0u) {
        reversed[count++] = '0';
    }
    while (value != 0u && count < sizeof(reversed)) {
        reversed[count++] = digits[value % radix];
        value /= radix;
    }
    while (count != 0u && at + 1u < capacity) {
        out[at++] = reversed[--count];
    }
    return at;
}

static uint32_t
append_signed(char *out, uint32_t capacity, uint32_t at, uint32_t value)
{
    if ((value & 0x80000000u) != 0u) {
        if (at + 1u < capacity) {
            out[at++] = '-';
        }
        /* Negated as unsigned: -INT32_MIN has no signed value to hold it. */
        value = (uint32_t)(0u - value);
    }
    return append_unsigned(out, capacity, at, value, 10u);
}

/* The argument at `index`, big-endian as astra_event_pack wrote it. */
static int
argument(const uint8_t *payload, uint32_t payload_length, uint32_t index,
         uint32_t *value)
{
    uint32_t at = index * 4u;

    if (payload == NULL || at + 4u > payload_length) {
        return 0;
    }
    *value = ((uint32_t)payload[at] << 24) | ((uint32_t)payload[at + 1u] << 16) |
             ((uint32_t)payload[at + 2u] << 8) | (uint32_t)payload[at + 3u];
    return 1;
}

int
astra_event_catalog_init(AstraEventCatalog *catalog, const void *bytes,
                         uint32_t size, uint32_t base)
{
    const AstraEventDescriptor *records = bytes;

    if (catalog == NULL) {
        return 0;
    }
    catalog->records = NULL;
    catalog->count = 0u;
    catalog->base = 0u;
    /*
     * Natural alignment, not four. A descriptor's widest member is a uint32_t
     * and m68k aligns those to two bytes, so a static array of them can sit at
     * an odd multiple of two -- and a hardcoded four rejected the machine's own
     * catalog while every host test passed. Ask the compiler what the type
     * needs and the question cannot be got wrong per target.
     */
    if (bytes == NULL || size == 0u ||
        size % ASTRA_EVENT_DESCRIPTOR_SIZE != 0u ||
        ((uintptr_t)bytes % _Alignof(AstraEventDescriptor)) != 0u ||
        records[0].magic != ASTRA_EVENT_DESCRIPTOR_MAGIC) {
        return 0;
    }
    catalog->records = records;
    catalog->count = size / ASTRA_EVENT_DESCRIPTOR_SIZE;
    catalog->base = base;
    return 1;
}

const AstraEventDescriptor *
astra_event_catalog_lookup(const AstraEventCatalog *catalog, uint32_t message)
{
    const AstraEventDescriptor *record;
    uint32_t offset;

    if (catalog == NULL || catalog->records == NULL || message < catalog->base) {
        return NULL;
    }
    offset = message - catalog->base;
    /*
     * An id that does not land on a record boundary is not this catalog's, and
     * neither is one past its end. Both are the same fact -- somebody read an
     * event emitted by a different image -- and inventing a record for either
     * would render the wrong message under the right id.
     */
    if (offset % ASTRA_EVENT_DESCRIPTOR_SIZE != 0u) {
        return NULL;
    }
    offset /= ASTRA_EVENT_DESCRIPTOR_SIZE;
    if (offset >= catalog->count) {
        return NULL;
    }
    record = &catalog->records[offset];
    return record->magic == ASTRA_EVENT_DESCRIPTOR_MAGIC ? record : NULL;
}

uint32_t
astra_event_catalog_render(const AstraEventCatalog *catalog, uint32_t message,
                           uint16_t flags, const uint8_t *payload,
                           uint32_t payload_length, char *out,
                           uint32_t capacity)
{
    const AstraEventDescriptor *record;
    uint32_t taken = 0u;
    uint32_t at = 0u;
    uint32_t index;

    if (out == NULL || capacity == 0u) {
        return 0u;
    }
    /*
     * An inline string is the payload, whole. It is the one argument type that
     * costs what text logging costs, and it exists for the case where there is
     * no object to name -- a label off a disk that would not mount. It is
     * self-describing, so it must not depend on a catalog descriptor.
     */
    if ((flags & ASTRA_EVENT_FLAG_INLINE_STRING) != 0u) {
        for (index = 0u; index < payload_length && at + 1u < capacity; ++index) {
            out[at++] = (char)payload[index];
        }
        out[at] = '\0';
        return at;
    }
    record = astra_event_catalog_lookup(catalog, message);
    if (record == NULL) {
        /* No text, so the id itself. Honest, and still enough to grep for. */
        at = append(out, capacity, at, "message 0x");
        at = append_unsigned(out, capacity, at, message, 16u);
        out[at] = '\0';
        return at;
    }

    for (index = 0u;
         index < ASTRA_EVENT_FORMAT_MAX && record->format[index] != '\0' &&
             at + 1u < capacity;
         ++index) {
        uint32_t value = 0u;
        char conversion;

        if (record->format[index] != '%') {
            out[at++] = record->format[index];
            continue;
        }
        if (index + 1u >= ASTRA_EVENT_FORMAT_MAX) {
            break;
        }
        conversion = record->format[++index];
        if (conversion == '%') {
            out[at++] = '%';
            continue;
        }
        if (!argument(payload, payload_length, taken, &value)) {
            /*
             * The format wants a value this occurrence does not carry. Saying
             * so beats reading past the payload, and it beats printing a zero
             * that a reader would take for a measurement.
             */
            at = append(out, capacity, at, "<missing>");
            continue;
        }
        ++taken;
        switch (conversion) {
        case 'u':
            at = append_unsigned(out, capacity, at, value, 10u);
            break;
        case 'd':
            at = append_signed(out, capacity, at, value);
            break;
        case 'x':
            at = append_unsigned(out, capacity, at, value, 16u);
            break;
        default:
            /*
             * A conversion this build does not know. Rendering the value
             * anyway would guess at what it means, so the format's own
             * characters go through and the value is shown as a number.
             */
            out[at++] = '%';
            if (at + 1u < capacity) {
                out[at++] = conversion;
            }
            at = append(out, capacity, at, "=");
            at = append_unsigned(out, capacity, at, value, 10u);
            break;
        }
    }
    out[at] = '\0';
    return at;
}
