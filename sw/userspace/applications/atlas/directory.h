#ifndef ATLAS_DIRECTORY_H
#define ATLAS_DIRECTORY_H

#include <stddef.h>
#include <astra/filesystem_library.h>
#include <astra/status.h>

typedef struct AtlasDirectory {
    AstraDirectoryEntry *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t status;
} AtlasDirectory;

#define ATLAS_DIRECTORY_INIT {NULL, 0u, 0u, ASTRA_VFS_OK}
#define ATLAS_DIRECTORY_CANCELLED ASTRA_STATUS_PROGRAM_FIRST

uint32_t atlas_directory_load(AtlasDirectory *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled);
void atlas_directory_dispose(AtlasDirectory *model);

#endif
