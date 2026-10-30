#ifndef ASTRA_VFS_CLIPBOARD_H
#define ASTRA_VFS_CLIPBOARD_H

/** @file vfs_clipboard.h @brief Native file selections for typed clipboard data. */

#include <astra/clipboard.h>
#include <astra/filesystem_library.h>
#include <astra/volume.h>

ASTRA_EXTERN_C_BEGIN

/** One or more file references in one immutable clipboard representation. */
#define ASTRA_CLIPBOARD_TYPE_VFS_SELECTION "application/x-astra-vfs-selection"
/** Current serialized file-selection payload version. */
#define ASTRA_VFS_CLIPBOARD_SELECTION_VERSION 2u

/** Exact mount identity prevents a stale removable path from selecting new
 * media. Fixed system assigns use an all-zero volume reference. Consumers
 * must verify current mount identity before reading the source. Authority
 * still comes from VFS grants; this value is never a capability. */
typedef struct AstraVfsClipboardEntry {
    /** Exact removable volume identity, or zero for fixed assigns. */
    AstraVolumeReference volume;
    /** Absolute NUL-terminated native path. */
    char path[ASTRA_VFS_PATH_MAX];
} AstraVfsClipboardEntry;

/** Followed by count entries in selection order. */
typedef struct AstraVfsClipboardSelection {
    uint32_t size;       /**< Complete payload byte count. */
    uint32_t version;    /**< ASTRA_VFS_CLIPBOARD_SELECTION_VERSION. */
    uint32_t count;      /**< Nonzero number of entries. */
    uint32_t reserved;   /**< Must be zero. */
    AstraVfsClipboardEntry entries[]; /**< count serialized references. */
} AstraVfsClipboardSelection;

/** Compute payload size.
 * @param count Number of selected files.
 * @return Byte count, or zero for empty/overflowing count.
 */
uint32_t astra_vfs_clipboard_selection_size(uint32_t count);

/** Validate complete payload before using any entry. Does not grant access or
 * validate whether volume identities remain mounted.
 * @param data Serialized payload.
 * @param bytes Payload byte count.
 * @return ASTRA_OK only when header, count and every path are valid.
 */
AstraResult astra_vfs_clipboard_selection_validate(const void *data,
    uint32_t bytes);

ASTRA_EXTERN_C_END

#endif
