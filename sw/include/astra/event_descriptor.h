#ifndef ASTRA_EVENT_DESCRIPTOR_H
#define ASTRA_EVENT_DESCRIPTOR_H

#include <stdint.h>

/*
 * What a message *is*, as opposed to what one occurrence of it carried.
 *
 * Subsystem, level, file, line, the format string and the argument types are
 * properties of the call site, not of the event. They are emitted once, into a
 * section the loader never maps, and cost zero bytes per occurrence -- which is
 * what lets every event on this machine know which file and line emitted it
 * without anybody typing that anywhere.
 *
 * The strings are arrays rather than pointers on purpose. A pointer would put
 * the format in .rodata, which is loaded, and the whole claim is that static
 * context is free at runtime. A build step reads these records out of the ELF
 * into a catalog, and the ROM image strips the section the way it already
 * strips DWARF.
 */

#define ASTRA_EVENT_DESCRIPTOR_MAGIC 0x41455644u /* "AEVD" */
#define ASTRA_EVENT_DESCRIPTOR_SIZE  128u
#define ASTRA_EVENT_FILE_MAX          48u
#define ASTRA_EVENT_FORMAT_MAX        64u

/*
 * Where the descriptors are linked. Nothing is mapped here and nothing is read
 * through it: a message id is a number that happens to be an address, and
 * basing it far from every real one means an id can never be mistaken for a
 * pointer, nor collide with the reserved ids in astra/event.h.
 */
#define ASTRA_EVENT_CATALOG_BASE 0xE0000000u

#define ASTRA_EVENT_ARG_NONE   0u
#define ASTRA_EVENT_ARG_U32    1u
#define ASTRA_EVENT_ARG_S32    2u
#define ASTRA_EVENT_ARG_STATUS 3u
#define ASTRA_EVENT_ARG_HANDLE 4u
#define ASTRA_EVENT_ARG_U64    5u /* declared; the macro refuses one for now */
#define ASTRA_EVENT_ARG_STRING 6u /* the inline string; astra_log's path */

#define ASTRA_EVENT_ARGUMENT_COUNT_MAX 4u

typedef struct AstraEventDescriptor {
    uint32_t magic;
    uint16_t line;
    uint8_t  subsystem;
    uint8_t  level;
    uint8_t  argument_count;
    uint8_t  argument_type[ASTRA_EVENT_ARGUMENT_COUNT_MAX];
    uint8_t  reserved[3];
    char     file[ASTRA_EVENT_FILE_MAX];
    char     format[ASTRA_EVENT_FORMAT_MAX];
} AstraEventDescriptor;

_Static_assert(sizeof(AstraEventDescriptor) == ASTRA_EVENT_DESCRIPTOR_SIZE,
               "the catalog extractor walks this in fixed steps");

#endif
