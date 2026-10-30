#ifndef ASTRA_VFS_CLIPBOARD_H
#define ASTRA_VFS_CLIPBOARD_H

/** @file vfs_clipboard.h @brief Native file references for typed clipboard data. */

#include <astra/clipboard.h>
#include <astra/filesystem_library.h>
#include <astra/volume.h>

/** One file path, copied as one immutable clipboard representation. */
#define ASTRA_CLIPBOARD_TYPE_VFS_REFERENCE "application/x-astra-vfs-reference"
/** Version of AstraVfsClipboardReference payload. */
#define ASTRA_VFS_CLIPBOARD_REFERENCE_VERSION 1u

/** Exact mount identity prevents a stale removable path from selecting new
 * media. Fixed system assigns use an all-zero volume reference. Consumers
 * must verify size, version, terminated canonical path, and current mount
 * identity before reading the source. Authority still comes from VFS grants. */
typedef struct AstraVfsClipboardReference {
    /** Structure byte count. */
    uint32_t size;
    /** Payload version. */
    uint32_t version;
    /** Exact removable volume identity, or zero for fixed assigns. */
    AstraVolumeReference volume;
    /** Absolute, NUL-terminated native path. */
    char path[ASTRA_VFS_PATH_MAX];
} AstraVfsClipboardReference;

/** Initializer for a fixed-assign file reference. */
#define ASTRA_VFS_CLIPBOARD_REFERENCE_INIT \
    { sizeof(AstraVfsClipboardReference), ASTRA_VFS_CLIPBOARD_REFERENCE_VERSION, \
      { 0u, 0u, 0u }, { 0 } }

#endif
