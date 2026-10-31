/** @file trash.h @brief Per-volume recoverable Trash operations. */
#ifndef ASTRA_TRASH_H
#define ASTRA_TRASH_H

#include <astra/filesystem_library.h>

/** Move one file, folder, or link to a writable volume's Trash without
 * replacing an existing item. Store its original volume-relative path before
 * the move so a later mount slot change does not break Restore.
 * @param filesystem Attached writable filesystem view.
 * @param volume_root Current assign root for the item.
 * @param source Existing path strictly beneath volume_root, outside Trash.
 * @param trashed Receives the full path of the moved item.
 * @param capacity Size of trashed.
 * @return ASTRA_VFS_* status.
 * @since Filesystem Kit 15.11.
 */
uint32_t astra_trash_move(AstraFilesystem *filesystem,
    const char *volume_root, const char *source,
    char *trashed, uint32_t capacity);

/** Read a trashed item's original path under the current volume root.
 * Untracked legacy entries return ASTRA_VFS_ERR_NOT_FOUND. A malformed record
 * returns ASTRA_VFS_ERR_INVALID. No file is moved.
 * @param filesystem Attached filesystem view.
 * @param volume_root Current assign root for the item.
 * @param trashed Direct child of volume_root/.Trash.
 * @param origin Receives the full original path under volume_root.
 * @param capacity Size of origin.
 * @return ASTRA_VFS_* status.
 * @since Filesystem Kit 15.11.
 */
uint32_t astra_trash_origin(AstraFilesystem *filesystem,
    const char *volume_root, const char *trashed,
    char *origin, uint32_t capacity);

/** Restore a tracked item to its original path without replacing an entry.
 * The output path is set before rename; moved becomes one after rename even
 * if metadata cleanup then fails. A nonzero moved value means the item is
 * already at origin and must not be retried as a Trash payload.
 * @param filesystem Attached writable filesystem view.
 * @param volume_root Current assign root for the item.
 * @param trashed Direct child of volume_root/.Trash.
 * @param origin Receives original path even if rename fails.
 * @param capacity Size of origin.
 * @param moved Receives one after successful rename, zero otherwise.
 * @return ASTRA_VFS_* status.
 * @since Filesystem Kit 15.11.
 */
uint32_t astra_trash_restore(AstraFilesystem *filesystem,
    const char *volume_root, const char *trashed,
    char *origin, uint32_t capacity, uint32_t *moved);

/** Empty payloads on one volume. Directories are removed recursively without
 * following links. On failure, failed receives the exact path; removed counts
 * payloads fully removed before that failure. Legacy untracked payloads are
 * removed too. Orphan origin records contain no payload and are ignored.
 * @param filesystem Attached writable filesystem view.
 * @param volume_root Current assign root for the Trash.
 * @param failed Receives exact payload or metadata path on failure.
 * @param capacity Size of failed.
 * @param removed Receives count of payloads removed during this call.
 * @return ASTRA_VFS_* status.
 * @since Filesystem Kit 15.11.
 */
uint32_t astra_trash_empty(AstraFilesystem *filesystem,
    const char *volume_root, char *failed, uint32_t capacity,
    uint32_t *removed);

#endif
