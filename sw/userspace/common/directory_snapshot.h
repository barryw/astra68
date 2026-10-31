#ifndef ASTRA_DIRECTORY_SNAPSHOT_H
#define ASTRA_DIRECTORY_SNAPSHOT_H

#include <stddef.h>
#include <astra/filesystem_library.h>
#include <astra/status.h>

typedef struct AstraDirectorySnapshot {
    AstraDirectoryEntry *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t status;
} AstraDirectorySnapshot;

#define ASTRA_DIRECTORY_SNAPSHOT_INIT {NULL, 0u, 0u, ASTRA_VFS_OK}
#define ASTRA_DIRECTORY_SNAPSHOT_CANCELLED ASTRA_STATUS_PROGRAM_FIRST

enum {
    ASTRA_DIRECTORY_SORT_NAME = 0u,
    ASTRA_DIRECTORY_SORT_SIZE = 1u,
    ASTRA_DIRECTORY_SORT_KIND = 2u,
    ASTRA_DIRECTORY_SORT_MODIFIED = 3u
};

uint32_t astra_directory_snapshot_load(AstraDirectorySnapshot *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled);
void astra_directory_snapshot_dispose(AstraDirectorySnapshot *model);
void astra_directory_snapshot_filter_hidden(AstraDirectorySnapshot *model);
void astra_directory_snapshot_filter_name(AstraDirectorySnapshot *model,
    const char *name);
uint32_t astra_directory_snapshot_sort(AstraDirectorySnapshot *model,
    uint32_t key, uint32_t reverse);

/** Visit descendants of one directory without following symlinks. The caller
 * owns search policy; setting descend to zero skips a directory's children.
 * Each path and entry is borrowed only for the duration of the callback.
 * Returning a nonzero VFS status stops the walk and closes all snapshots. */
typedef uint32_t (*AstraDirectoryTreeVisit)(void *context, const char *path,
    const AstraDirectoryEntry *entry, uint32_t *descend);

uint32_t astra_directory_tree_walk(AstraFilesystem *filesystem,
    const char *directory, AstraDirectoryTreeVisit visit, void *context,
    const uint32_t *cancelled);

#endif
