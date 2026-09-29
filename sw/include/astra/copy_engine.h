#ifndef ASTRA_COPY_ENGINE_H
#define ASTRA_COPY_ENGINE_H

#include <stdint.h>

/*
 * The machine's copy engine: a kernel-owned device that moves bytes between
 * physical extents of guest RAM on the MC68040's behalf. The kernel writes
 * the physical address of an AstraCopyList to COPY_LIST; the copy is done
 * when the write completes, and COPY_STATUS holds the result. Big-endian,
 * like every guest record. Extents may not overlap.
 */
#define ASTRA_COPY_ENGINE_ID      UINT32_C(0x434f5059) /* COPY */
#define ASTRA_COPY_LIST_MAGIC     UINT32_C(0x434c5354) /* CLST */
#define ASTRA_COPY_LIST_MAX       512u
#define ASTRA_COPY_STATUS_OK      0u
#define ASTRA_COPY_STATUS_INVALID 1u

typedef struct AstraCopyExtent {
    uint32_t source;
    uint32_t destination;
    uint32_t bytes;
} AstraCopyExtent;

typedef struct AstraCopyList {
    uint32_t magic;
    uint32_t count;
    AstraCopyExtent extents[ASTRA_COPY_LIST_MAX];
} AstraCopyList;

#endif
