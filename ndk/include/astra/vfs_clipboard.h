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
#define ASTRA_VFS_CLIPBOARD_SELECTION_VERSION 3u
/** Copy source entries when pasted. */
#define ASTRA_VFS_CLIPBOARD_COPY 0u
/** Move source entries when pasted. Source must remain writable. */
#define ASTRA_VFS_CLIPBOARD_MOVE 1u

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
    uint32_t operation;  /**< ASTRA_VFS_CLIPBOARD_COPY or MOVE. */
#ifndef __cplusplus
    AstraVfsClipboardEntry entries[]; /**< count serialized references. */
#endif
} AstraVfsClipboardSelection;

/** Access serialized entries in C and ISO C++ after validating the complete
 * payload. ISO C++ has no flexible array member.
 * @param selection Complete validated selection payload.
 * @return First serialized entry after the fixed header.
 */
static inline const AstraVfsClipboardEntry *
astra_vfs_clipboard_selection_entries(const AstraVfsClipboardSelection *selection)
{
    return (const AstraVfsClipboardEntry *)
        ((const unsigned char *)selection + sizeof(*selection));
}

/** Access writable serialized entries in C and ISO C++ after allocating the
 * complete payload size.
 * @param selection Complete writable selection payload.
 * @return First serialized entry after the fixed header.
 */
static inline AstraVfsClipboardEntry *
astra_vfs_clipboard_selection_entries_mutable(AstraVfsClipboardSelection *selection)
{
    return (AstraVfsClipboardEntry *)
        ((unsigned char *)selection + sizeof(*selection));
}

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
