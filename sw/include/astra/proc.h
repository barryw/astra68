#ifndef ASTRA_PROC_H
#define ASTRA_PROC_H

#include <astra/library.h>
#include <astra/process.h>

#define ASTRA_PROC_NAME_MAX 32u

typedef struct AstraProcSnapshot {
    AstraProcessInfo process;
    char name[ASTRA_PROC_NAME_MAX];
} AstraProcSnapshot;

/*
 * One resident library/process mapping. Libraries with no process mappings
 * still produce one record with process_id zero. A library mapped by several
 * processes produces consecutive records, so pagination is bounded only by
 * the caller's transfer buffer and live system resources.
 */
typedef struct AstraProcLibrarySnapshot {
    AstraLibraryReference library;
    uint32_t base;
    uint32_t image_span;
    uint32_t cache_bytes;
    uint32_t resident_bytes;
    uint32_t mapped_bytes;
    uint32_t mapping_count;
    uint32_t reference_count;
    uint32_t process_id;
} AstraProcLibrarySnapshot;

_Static_assert(sizeof(AstraProcSnapshot) == 112u,
               "PROC snapshot record ABI changed");
_Static_assert(sizeof(AstraProcLibrarySnapshot) == 76u,
               "PROC library snapshot record ABI changed");
#endif
