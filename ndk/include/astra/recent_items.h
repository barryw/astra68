/** @file recent_items.h @brief Durable per-user recently opened file records. */
#ifndef ASTRA_RECENT_ITEMS_H
#define ASTRA_RECENT_ITEMS_H

#include <astra/filesystem_library.h>
#include <astra/volume.h>

/** Maximum distinct paths returned by one recent-item snapshot. */
#define ASTRA_RECENT_ITEMS_LIMIT 64u

/** One opened file or folder. A zero volume reference denotes a fixed path.
 * Other references must still match a mounted volume when used. */
typedef struct AstraRecentItem {
    uint32_t size; /**< Structure size. */
    char path[ASTRA_VFS_PATH_MAX]; /**< Absolute native path. */
    char filename[ASTRA_VFS_NAME_MAX]; /**< Store filename, for maintenance. */
    AstraVolumeReference volume; /**< Exact removable incarnation or zero. */
    uint64_t opened_ns; /**< Durable newest-first ordering value. */
    uint32_t kind; /**< ASTRA_VFS_KIND_FILE or DIRECTORY. */
    uint32_t reserved; /**< Must be zero. */
} AstraRecentItem;

/** Empty recent-item record. */
#define ASTRA_RECENT_ITEM_INIT { sizeof(AstraRecentItem), {0}, {0}, {0}, \
    0u, 0u, 0u }

/** Owned, newest-first recent-item collection. Close after use. */
typedef struct AstraRecentItemSnapshot {
    AstraRecentItem *items; /**< Owned records, newest first. */
    uint32_t count; /**< Number of records in items. */
} AstraRecentItemSnapshot;

/** Empty owned recent-item snapshot. */
#define ASTRA_RECENT_ITEM_SNAPSHOT_INIT { NULL, 0u }

/** Record an opened item in a private user directory. Complete records are
 * synced and atomically published; concurrent writers create distinct files.
 * @since Filesystem Kit 15.9.
 * @param filesystem Attached native filesystem client.
 * @param store_directory Absolute private directory for this user's records.
 * @param path Absolute path successfully opened by the application.
 * @param kind ASTRA_VFS_KIND_FILE or ASTRA_VFS_KIND_DIRECTORY.
 * @param volume Exact removable-volume reference, or NULL for a fixed path.
 * @return ASTRA_VFS_OK or a validation, storage, or resource error.
 */
uint32_t astra_recent_item_record(AstraFilesystem *filesystem,
    const char *store_directory, const char *path, uint32_t kind,
    const AstraVolumeReference *volume);

/** Read at most 64 unique records, newest first. Invalid files are ignored.
 * A missing store is an empty snapshot. @since Filesystem Kit 15.9.
 * @param filesystem Attached native filesystem client.
 * @param store_directory Absolute private record directory.
 * @param snapshot Receives owned records; initialize before calling.
 * @return ASTRA_VFS_OK or a storage or resource error.
 */
uint32_t astra_recent_item_snapshot_open(AstraFilesystem *filesystem,
    const char *store_directory, AstraRecentItemSnapshot *snapshot);

/** Release records and reset a recent-item snapshot.
 * @param snapshot Owned snapshot to close.
 */
void astra_recent_item_snapshot_close(AstraRecentItemSnapshot *snapshot);

#endif
