#ifndef ASTRA_PROC_H
#define ASTRA_PROC_H

/**
 * @file proc.h
 * @brief PROC-service snapshot records: one process or one resident-library
 *        mapping per entry.
 *
 * Built on %library.h and %process.h: it adds nothing to the kernel ABI by
 * itself, only the two fixed-size records the supervisor's PROC introspection
 * surface (consumed by commands such as `ps`) copies out of the kernel in
 * bulk, one ::AstraProcSnapshot per live process and one
 * ::AstraProcLibrarySnapshot per resident library mapping.
 */

#include <astra/compiler.h>
#include <astra/library.h>
#include <astra/process.h>

/** Maximum length of a process name, including the terminating NUL. */
#define ASTRA_PROC_NAME_MAX 32u

/** One live process, as the PROC snapshot syscall reports it. */
typedef struct AstraProcSnapshot {
    /** The process's full accounting and lifecycle snapshot. */
    AstraProcessInfo process;
    /** The process's display name: its own argv[0], NUL-terminated and truncated to fit. */
    char name[ASTRA_PROC_NAME_MAX];
} AstraProcSnapshot;

/**
 * One resident library, paired with one of the processes mapping it.
 *
 * Libraries with no process mappings still produce one record with
 * process_id zero. A library mapped by several processes produces
 * consecutive records, so pagination is bounded only by the caller's
 * transfer buffer and live system resources.
 */
typedef struct AstraProcLibrarySnapshot {
    /** Identity of the mapped library. */
    AstraLibraryReference library;
    /** Virtual address at which the library is mapped. */
    uint32_t base;
    /** Total virtual bytes the library's image spans, including inter-segment holes. */
    uint32_t image_span;
    /** Bytes occupied by the library's shared page cache. */
    uint32_t cache_bytes;
    /**
     * Physically resident bytes attributed to the library: the shared cache
     * pages plus any private (copy-on-write) copies, each counted once.
     */
    uint32_t resident_bytes;
    /**
     * Total virtual bytes mapped by every process currently mapping the
     * library, counting one mapping's worth of bytes per mapping process.
     */
    uint32_t mapped_bytes;
    /** Count of live processes currently mapping the library. */
    uint32_t mapping_count;
    /** Total reference count on the library's cache entry: one per mapping process, plus the cache's own. */
    uint32_t reference_count;
    /** The process holding this particular mapping; zero if the library has no process mappings. */
    uint32_t process_id;
} AstraProcLibrarySnapshot;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraProcSnapshot) == 112u,
               "PROC snapshot record ABI changed");
_Static_assert(sizeof(AstraProcLibrarySnapshot) == 76u,
               "PROC library snapshot record ABI changed");
/** @endcond */
#endif
