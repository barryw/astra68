#ifndef ASTRA_DIRECTORY_QUERY_H
#define ASTRA_DIRECTORY_QUERY_H

#include "directory_snapshot.h"

/** Generic, path-scoped query over directory entry metadata. Time bounds are
 * Unix seconds; lower bound is inclusive and upper bound is exclusive. */
typedef struct AstraDirectoryQuerySpec {
    const char *name_contains;
    int64_t modified_from;
    int64_t modified_before;
    uint32_t use_modified_from;
    uint32_t use_modified_before;
    uint32_t recursive;
    uint32_t include_hidden;
} AstraDirectoryQuerySpec;

typedef struct AstraDirectoryQueryItem {
    char *path;
    AstraDirectoryEntry entry;
} AstraDirectoryQueryItem;

typedef struct AstraDirectoryQuerySnapshot {
    AstraDirectoryQueryItem *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t status;
} AstraDirectoryQuerySnapshot;

#define ASTRA_DIRECTORY_QUERY_SNAPSHOT_INIT {NULL, 0u, 0u, ASTRA_VFS_OK}

uint32_t astra_directory_query_load(AstraDirectoryQuerySnapshot *snapshot,
    AstraFilesystem *filesystem, const char *directory,
    const AstraDirectoryQuerySpec *spec, const uint32_t *cancelled);
void astra_directory_query_dispose(AstraDirectoryQuerySnapshot *snapshot);

#endif
