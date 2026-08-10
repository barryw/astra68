#ifndef ASTRA_EVENT_EMIT_H
#define ASTRA_EVENT_EMIT_H

#include <stdint.h>

#include <astra/event.h>
#include <astra/event_descriptor.h>

/*
 * Emitting a typed event.
 *
 * ASTRA_EVENTn puts a descriptor in a section the loader never maps and
 * compiles a call carrying that descriptor's address as the message id. The
 * format string, the file and the line travel nowhere: they are properties of
 * the message, a reader resolves them from the catalog, and an occurrence
 * carries only the values that differ between occurrences.
 *
 * A disabled level costs one load, one compare and one branch. That is the
 * price of leaving `debug` call sites compiled into shipping code, and it is
 * low enough that they should be.
 *
 * Five macros with a number in the name rather than one variadic one: counting
 * __VA_ARGS__ needs either __VA_OPT__, which is C2x, or the ##__VA_ARGS__
 * extension, which -pedantic -Werror refuses. This is the boring option.
 */

/* One byte per subsystem: the lowest level that is emitted. */
extern uint8_t astra_event_levels[ASTRA_EVENT_SUBSYSTEM_MAX];

uint32_t astra_event_level_set(uint32_t subsystem, uint32_t level);

static inline int
astra_event_enabled(uint32_t subsystem, uint32_t level)
{
    return subsystem < ASTRA_EVENT_SUBSYSTEM_MAX &&
           level >= astra_event_levels[subsystem];
}

/* Big-endian, four bytes each. Returns the bytes written, or 0 on refusal. */
uint32_t astra_event_pack(uint8_t *out, uint32_t capacity,
                          const uint32_t *values, uint32_t count);

/*
 * Packs `count` four-byte values and emits. Separate from the macro so the
 * packing exists once rather than five times, and so a test can reach it.
 */
uint32_t astra_event_emit_packed(const AstraEventDescriptor *descriptor,
                                 uint32_t level, const uint32_t *values,
                                 uint32_t count);

/*
 * Signedness is what a reader needs to render a value, and it is the one thing
 * the compiler can tell us for free. A status or a handle is a uint32_t like
 * any other and cannot be told apart here; those types exist in the descriptor
 * for the day a reader renders them, and a call site that wants one says so in
 * its format string until then.
 */
#define ASTRA_EVENT_TYPE_OF(value) ((uint8_t)_Generic((value),                \
    signed char: ASTRA_EVENT_ARG_S32,                                         \
    short: ASTRA_EVENT_ARG_S32,                                               \
    int: ASTRA_EVENT_ARG_S32,                                                 \
    long: ASTRA_EVENT_ARG_S32,                                                \
    default: ASTRA_EVENT_ARG_U32))

/*
 * Wider than a word is refused at the call site rather than truncated in the
 * ring. Nothing on this machine needs a 64-bit argument yet, and a value that
 * silently lost its top half would be a wrong number in a log, which is worse
 * than a call that does not build.
 */
#define ASTRA_EVENT_CHECK(value)                                              \
    _Static_assert(sizeof(value) <= 4u,                                       \
                   "an event argument wider than 32 bits is not supported")

/*
 * Where a descriptor is placed. Mach-O spells a section as segment,section and
 * refuses this one outright, so a host test on the Mac collects descriptors
 * wherever the compiler likes: placement is a link-time property, and it is
 * the cross-build and readelf that check it rather than a host test that
 * cannot see a linker script anyway.
 */
#if defined(__ELF__)
#define ASTRA_EVENT_SECTION \
    __attribute__((section(".astra_events"), used, aligned(4)))
#else
#define ASTRA_EVENT_SECTION __attribute__((used, aligned(4)))
#endif

#define ASTRA_EVENT_DESCRIBE(subsystem, level, format, count, t0, t1, t2, t3) \
    static const AstraEventDescriptor astra_event_descriptor                  \
        ASTRA_EVENT_SECTION = {                                               \
        ASTRA_EVENT_DESCRIPTOR_MAGIC, (uint16_t)__LINE__,                     \
        (uint8_t)(subsystem), (uint8_t)(level), (uint8_t)(count),             \
        {(t0), (t1), (t2), (t3)}, {0u, 0u, 0u}, __FILE__, format              \
    }

#define ASTRA_EVENT0(subsystem, level, format)                                \
    do {                                                                      \
        ASTRA_EVENT_DESCRIBE(subsystem, level, format, 0u, 0u, 0u, 0u, 0u);   \
        if (astra_event_enabled((subsystem), (level)))                        \
            (void)astra_event_emit_packed(&astra_event_descriptor, (level),   \
                                          NULL, 0u);                          \
    } while (0)

#define ASTRA_EVENT1(subsystem, level, format, a0)                            \
    do {                                                                      \
        ASTRA_EVENT_CHECK(a0);                                                \
        ASTRA_EVENT_DESCRIBE(subsystem, level, format, 1u,                    \
                             ASTRA_EVENT_TYPE_OF(a0), 0u, 0u, 0u);            \
        if (astra_event_enabled((subsystem), (level))) {                      \
            const uint32_t astra_event_values[1] = {(uint32_t)(a0)};          \
            (void)astra_event_emit_packed(&astra_event_descriptor, (level),   \
                                          astra_event_values, 1u);            \
        }                                                                     \
    } while (0)

#define ASTRA_EVENT2(subsystem, level, format, a0, a1)                        \
    do {                                                                      \
        ASTRA_EVENT_CHECK(a0);                                                \
        ASTRA_EVENT_CHECK(a1);                                                \
        ASTRA_EVENT_DESCRIBE(subsystem, level, format, 2u,                    \
                             ASTRA_EVENT_TYPE_OF(a0),                         \
                             ASTRA_EVENT_TYPE_OF(a1), 0u, 0u);                \
        if (astra_event_enabled((subsystem), (level))) {                      \
            const uint32_t astra_event_values[2] = {(uint32_t)(a0),           \
                                                    (uint32_t)(a1)};          \
            (void)astra_event_emit_packed(&astra_event_descriptor, (level),   \
                                          astra_event_values, 2u);            \
        }                                                                     \
    } while (0)

#define ASTRA_EVENT3(subsystem, level, format, a0, a1, a2)                    \
    do {                                                                      \
        ASTRA_EVENT_CHECK(a0);                                                \
        ASTRA_EVENT_CHECK(a1);                                                \
        ASTRA_EVENT_CHECK(a2);                                                \
        ASTRA_EVENT_DESCRIBE(subsystem, level, format, 3u,                    \
                             ASTRA_EVENT_TYPE_OF(a0),                         \
                             ASTRA_EVENT_TYPE_OF(a1),                         \
                             ASTRA_EVENT_TYPE_OF(a2), 0u);                    \
        if (astra_event_enabled((subsystem), (level))) {                      \
            const uint32_t astra_event_values[3] = {(uint32_t)(a0),           \
                                                    (uint32_t)(a1),           \
                                                    (uint32_t)(a2)};          \
            (void)astra_event_emit_packed(&astra_event_descriptor, (level),   \
                                          astra_event_values, 3u);            \
        }                                                                     \
    } while (0)

#define ASTRA_EVENT4(subsystem, level, format, a0, a1, a2, a3)                \
    do {                                                                      \
        ASTRA_EVENT_CHECK(a0);                                                \
        ASTRA_EVENT_CHECK(a1);                                                \
        ASTRA_EVENT_CHECK(a2);                                                \
        ASTRA_EVENT_CHECK(a3);                                                \
        ASTRA_EVENT_DESCRIBE(subsystem, level, format, 4u,                    \
                             ASTRA_EVENT_TYPE_OF(a0),                         \
                             ASTRA_EVENT_TYPE_OF(a1),                         \
                             ASTRA_EVENT_TYPE_OF(a2),                         \
                             ASTRA_EVENT_TYPE_OF(a3));                        \
        if (astra_event_enabled((subsystem), (level))) {                      \
            const uint32_t astra_event_values[4] = {(uint32_t)(a0),           \
                                                    (uint32_t)(a1),           \
                                                    (uint32_t)(a2),           \
                                                    (uint32_t)(a3)};          \
            (void)astra_event_emit_packed(&astra_event_descriptor, (level),   \
                                          astra_event_values, 4u);            \
        }                                                                     \
    } while (0)

#endif
