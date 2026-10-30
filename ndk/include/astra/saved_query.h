/** @file saved_query.h @brief Durable, path-scoped file search presets. */
#ifndef ASTRA_SAVED_QUERY_H
#define ASTRA_SAVED_QUERY_H

#include <stddef.h>
#include <astra/filesystem_library.h>

/** One user-owned search preset. Search is a case-insensitive name substring
 * within directory; callers choose how to present matching entries. */
typedef struct AstraSavedQuery {
    uint32_t size; /**< Caller-supplied structure size. */
    char label[ASTRA_VFS_NAME_MAX]; /**< NUL-terminated display name. */
    char directory[ASTRA_VFS_PATH_MAX]; /**< Absolute native search directory. */
    char text[ASTRA_VFS_NAME_MAX]; /**< NUL-terminated name substring. */
    char filename[ASTRA_VFS_NAME_MAX]; /**< Set by load/snapshot. */
} AstraSavedQuery;

/** Empty saved query record. */
#define ASTRA_SAVED_QUERY_INIT { sizeof(AstraSavedQuery), {0}, {0}, {0}, {0} }

/** Owned snapshot of published saved query files. */
typedef struct AstraSavedQuerySnapshot {
    AstraSavedQuery *queries; /**< Owned records; release with close. */
    uint32_t count; /**< Number of valid records. */
} AstraSavedQuerySnapshot;

/** Empty saved query snapshot. */
#define ASTRA_SAVED_QUERY_SNAPSHOT_INIT { NULL, 0u }

/** Save preset as one complete, independent file in store_directory. A new
 * directory is created private to the user when absent. Existing presets are
 * never replaced. Temporary data is synced, then published through a hard
 * link so readers never observe a partial preset. @since Filesystem Kit 15.8.
 * @param filesystem Attached native filesystem with write access to store.
 * @param store_directory Absolute path to private preset directory.
 * @param query Valid label, directory, and search text.
 * @param filename Receives created file's leaf name.
 * @param filename_capacity Size of filename buffer.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_saved_query_save(AstraFilesystem *filesystem,
    const char *store_directory, const AstraSavedQuery *query,
    char *filename, uint32_t filename_capacity);

/** Replace one saved query's label through a synced temporary file and an
 * atomic rename. The original filename and search remain unchanged. The
 * current record must still match the caller's snapshot; stale edits fail.
 * @since Filesystem Kit 15.8.
 * @param filesystem Attached native filesystem with write access to store.
 * @param store_directory Absolute path to private preset directory.
 * @param query Valid record returned by load or snapshot.
 * @param label New nonempty UTF-8 label.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_saved_query_set_label(AstraFilesystem *filesystem,
    const char *store_directory, const AstraSavedQuery *query,
    const char *label);

/** Load and validate one saved query file. @since Filesystem Kit 15.8.
 * @param filesystem Attached native filesystem.
 * @param path Absolute path to published preset file.
 * @param query Receives complete validated record; supply size.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_saved_query_load(AstraFilesystem *filesystem,
    const char *path, AstraSavedQuery *query);

/** Enumerate published presets, skipping malformed or incomplete files.
 * A missing store directory yields an empty snapshot. @since 15.8.
 * @param filesystem Attached native filesystem.
 * @param store_directory Absolute path to private preset directory.
 * @param snapshot Receives owned records; close after use.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_saved_query_snapshot_open(AstraFilesystem *filesystem,
    const char *store_directory, AstraSavedQuerySnapshot *snapshot);

/** Release owned saved-query snapshot storage.
 * @param snapshot Snapshot to release; safe for an empty snapshot.
 */
void astra_saved_query_snapshot_close(AstraSavedQuerySnapshot *snapshot);

#endif
