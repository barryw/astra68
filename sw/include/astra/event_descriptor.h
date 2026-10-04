#ifndef ASTRA_EVENT_DESCRIPTOR_H
#define ASTRA_EVENT_DESCRIPTOR_H

/**
 * @file event_descriptor.h
 * @brief What a message *is*, as opposed to what one occurrence of it carried.
 *
 * Subsystem, level, file, line, the format string and the argument types are
 * properties of the call site, not of the event. They are emitted once, into a
 * section the loader never maps, and cost zero bytes per occurrence -- which is
 * what lets every event on this machine know which file and line emitted it
 * without anybody typing that anywhere.
 *
 * The strings are arrays rather than pointers on purpose. A pointer would put
 * the format in `.rodata`, which is loaded, and the whole claim is that static
 * context is free at runtime. A build step reads these records out of the ELF
 * into a catalog, and the ROM image strips the section the way it already
 * strips DWARF.
 */

#include <astra/compiler.h>
#include <stdint.h>

/** @defgroup astra_event_descriptor Event descriptor catalog
 *  @brief The static, per-call-site half of a structured event: what
 *  `ASTRA_EVENTn()` (astra/event_emit.h) writes and a reader resolves a
 *  drained event's message id against.
 *  @{
 */

/** `AstraEventDescriptor::magic` signature ("AEVD"), checked before a record is trusted. */
#define ASTRA_EVENT_DESCRIPTOR_MAGIC 0x41455644u /* "AEVD" */
/** Byte size of ::AstraEventDescriptor, and the catalog's per-record stride. */
#define ASTRA_EVENT_DESCRIPTOR_SIZE  128u
/** Capacity, including the terminating NUL, of `AstraEventDescriptor::file`. */
#define ASTRA_EVENT_FILE_MAX          48u
/** Capacity, including the terminating NUL, of `AstraEventDescriptor::format`. */
#define ASTRA_EVENT_FORMAT_MAX        64u

/**
 * Base address a catalog's message ids are offsets from.
 *
 * Where the descriptors are linked. Nothing is mapped here and nothing is read
 * through it: a message id is a number that happens to be an address, and
 * basing it far from every real one means an id can never be mistaken for a
 * pointer, nor collide with the reserved ids in astra/%event.h.
 */
#define ASTRA_EVENT_CATALOG_BASE 0xE0000000u

/** Argument slot not used by this message. */
#define ASTRA_EVENT_ARG_NONE   0u
/** Argument slot holds an unsigned 32-bit value. */
#define ASTRA_EVENT_ARG_U32    1u
/** Argument slot holds a signed 32-bit value. */
#define ASTRA_EVENT_ARG_S32    2u
/** Argument slot holds an `ASTRA_SYSCALL_*`/`ASTRA_STATUS_*` status code. */
#define ASTRA_EVENT_ARG_STATUS 3u
/** Argument slot holds a kernel handle value. */
#define ASTRA_EVENT_ARG_HANDLE 4u
/** Argument slot holds a 64-bit value; declared, but no emit macro produces one yet. */
#define ASTRA_EVENT_ARG_U64    5u
/** Argument slot holds inline text rather than a packed word; `astra_log()`'s path. */
#define ASTRA_EVENT_ARG_STRING 6u

/** Highest number of typed arguments one descriptor can describe. */
#define ASTRA_EVENT_ARGUMENT_COUNT_MAX 4u

/**
 * The static description of one `ASTRA_EVENTn()` call site.
 *
 * One of these is emitted per call site, into a section the loader never
 * maps; a build-time tool collects them from the ELF into the catalog a
 * reader resolves a drained ::AstraEventDrained.message against.
 */
typedef struct AstraEventDescriptor {
    /** ::ASTRA_EVENT_DESCRIPTOR_MAGIC, validating the record before any field of it is trusted. */
    uint32_t magic;
    /** Source line of the `ASTRA_EVENTn()` call. */
    uint16_t line;
    /** Owning `ASTRA_EVENT_SUBSYSTEM_*` identifier (astra/%event.h). */
    uint8_t  subsystem;
    /** `ASTRA_EVENT_LEVEL_*` severity (astra/%event.h). */
    uint8_t  level;
    /** Number of leading entries of `argument_type` this message uses, 0..::ASTRA_EVENT_ARGUMENT_COUNT_MAX. */
    uint8_t  argument_count;
    /** `ASTRA_EVENT_ARG_*` type of each argument, in order; entries at or past `argument_count` are ::ASTRA_EVENT_ARG_NONE. */
    uint8_t  argument_type[ASTRA_EVENT_ARGUMENT_COUNT_MAX];
    /** Reserved padding, written zero by `ASTRA_EVENT_DESCRIBE()`. */
    uint8_t  reserved[3];
    /** Source file of the `ASTRA_EVENTn()` call, NUL-terminated. */
    char     file[ASTRA_EVENT_FILE_MAX];
    /** Static message format string, NUL-terminated. */
    char     format[ASTRA_EVENT_FORMAT_MAX];
} AstraEventDescriptor;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraEventDescriptor) == ASTRA_EVENT_DESCRIPTOR_SIZE,
               "the catalog extractor walks this in fixed steps");
/** @endcond */

/** @} */

#endif
