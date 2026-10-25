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

uint32_t astra_directory_snapshot_load(AstraDirectorySnapshot *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled);
void astra_directory_snapshot_dispose(AstraDirectorySnapshot *model);

#endif
