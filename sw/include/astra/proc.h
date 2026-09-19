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
 * One resident library and every live process mapping it. The cache itself
 * owns one logical reference; each process in process_ids owns one more.
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
    uint16_t process_ids[ASTRA_PROCESS_COUNT_MAX];
} AstraProcLibrarySnapshot;

_Static_assert(sizeof(AstraProcSnapshot) == 112u,
               "PROC snapshot record ABI changed");
_Static_assert(sizeof(AstraProcLibrarySnapshot) == 136u,
               "PROC library snapshot record ABI changed");
#endif
