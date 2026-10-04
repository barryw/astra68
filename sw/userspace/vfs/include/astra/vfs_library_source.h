#ifndef ASTRA_VFS_LIBRARY_SOURCE_H
#define ASTRA_VFS_LIBRARY_SOURCE_H

/* The provider resolver behind astra_process_library_source_open: the
   loaders and the filesystem library use it directly. Private to the VFS
   and its loaders. */

#include <stdint.h>

#include <astra/vfs_reader.h>

/* Resolve an installed provider without opening its binary. */
uint32_t astra_vfs_library_resolve(
    const AstraAssignTable *table, const char *assign, const char *identity,
    AstraVfsAssignClientFn client_for, void *context,
    char *path, uint32_t path_capacity, AstraLibraryReference *reference);

/*
 * Resolve and open one exact installed provider for a DT_NEEDED identity.
 *
 * This is the sole provider-resolution path. It reads the canonical provider
 * index through the supplied namespace, validates the exact provider record,
 * and opens the selected binary. Package discovery and version selection happen
 * when that index is built; runtime consumers do not scan Kits or select a
 * different version.
 */
uint32_t astra_vfs_library_source_open(
    AstraVfsReadSource *source, const AstraAssignTable *table,
    const char *assign, const char *identity,
    AstraVfsAssignClientFn client_for, void *context,
    AstraLibraryReference *reference);

#endif
