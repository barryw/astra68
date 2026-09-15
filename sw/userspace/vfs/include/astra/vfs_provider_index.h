#ifndef ASTRA_VFS_PROVIDER_INDEX_H
#define ASTRA_VFS_PROVIDER_INDEX_H

#include <stdint.h>

#include <astra/library.h>

/* Parses one versioned LIBS:.providers record into its exact identity/path. */
int astra_vfs_provider_index_parse(
    const uint8_t *bytes, uint32_t length, const char *name, uint16_t abi,
    char *path, uint32_t capacity, AstraLibraryReference *reference);

#endif
