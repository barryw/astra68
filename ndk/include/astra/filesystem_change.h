#ifndef ASTRA_FILESYSTEM_CHANGE_H
#define ASTRA_FILESYSTEM_CHANGE_H

/** @file filesystem_change.h @brief Volume-scoped VFS invalidation events. */

#include <stdint.h>
#include <astra/volume_abi.h>

/** HostFS messaging topic for exact node changes and volume invalidations. */
#define ASTRA_FILESYSTEM_CHANGE_TOPIC "filesystem"
/** Current filesystem change payload format. */
#define ASTRA_FILESYSTEM_CHANGE_VERSION 2u
/** Generation-only event: re-read watched directory or node. */
#define ASTRA_FILESYSTEM_CHANGE_INVALIDATE 0u
/** Exact changed inode is available; generations still govern recovery. */
#define ASTRA_FILESYSTEM_CHANGE_NODE 1u

/* Subscribe through MESSAGING, then enumerate before consuming events.
 * Generations remain authoritative after queue loss or unsupported mutations;
 * exact node records identify a mutation, not the node's current contents. */
/** Volume change event with optional exact inode identity. */
typedef struct AstraFilesystemChange {
    /** Payload format version. */
    uint32_t version;
    /** Total payload bytes. */
    uint32_t size;
    /** Exact mounted volume identity. */
    AstraVolumeReference volume;
    uint64_t nodes;   /**< Explicit file or directory mutation; reads do not count. */
    uint64_t entries; /**< Directory entries changed. */
    uint32_t kind;    /**< Exact node change, or generation-only invalidation. */
    uint32_t reserved; /**< Must be zero. */
    uint64_t inode;   /**< Exact changed inode for NODE; zero for INVALIDATE. */
} AstraFilesystemChange;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraFilesystemChange) == 64u,
    "filesystem change payload ABI changed");
/** @endcond */

#endif
