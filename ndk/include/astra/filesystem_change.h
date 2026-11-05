#ifndef ASTRA_FILESYSTEM_CHANGE_H
#define ASTRA_FILESYSTEM_CHANGE_H

/** @file filesystem_change.h @brief Volume-scoped VFS invalidation events. */

#include <stdint.h>
#include <astra/volume_abi.h>

/** HostFS messaging topic for coalesced volume change generations. */
#define ASTRA_FILESYSTEM_CHANGE_TOPIC "filesystem"
/** Current filesystem change payload format. */
#define ASTRA_FILESYSTEM_CHANGE_VERSION 1u

/* Subscribe through MESSAGING, then enumerate the directory before consuming
 * events. Changed generations invalidate cached views; they are not deltas. */
/** Coalesced notification that consumers must reconcile with fresh VFS reads. */
typedef struct AstraFilesystemChange {
    /** Payload format version. */
    uint32_t version;
    /** Total payload bytes. */
    uint32_t size;
    /** Exact mounted volume identity. */
    AstraVolumeReference volume;
    uint64_t nodes;   /**< Explicit file or directory mutation; reads do not count. */
    uint64_t entries; /**< Directory entries changed. */
} AstraFilesystemChange;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraFilesystemChange) == 48u,
    "filesystem change payload ABI changed");
/** @endcond */

#endif
