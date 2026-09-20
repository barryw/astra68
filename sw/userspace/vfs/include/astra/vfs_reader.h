#ifndef ASTRA_VFS_READER_H
#define ASTRA_VFS_READER_H

#include <stdint.h>

#include <astra/library.h>
#include <astra/vfs_union.h>

/*
 * A resolved regular file kept open for borrowed random-access reads.
 * The returned bytes belong to the VFS transport and remain valid only until
 * the next operation on the same client.
 */
typedef struct AstraVfsReadSource {
    AstraVfsClient *client;
    AstraVfsFile file;
    uint32_t length;
} AstraVfsReadSource;

#define ASTRA_VFS_READ_SOURCE_INIT { NULL, ASTRA_VFS_FILE_INVALID, 0u }

uint32_t astra_vfs_read_source_open(
    AstraVfsReadSource *source, const AstraAssignTable *table,
    const char *path, AstraVfsAssignClientFn client_for, void *context);

uint32_t astra_vfs_read_source_read_at(
    void *context, uint32_t offset, uint32_t length,
    const uint8_t **bytes, uint32_t *moved);

uint32_t astra_vfs_read_source_close(void *context);

/* Resolve an installed provider without opening its binary. */
uint32_t astra_vfs_library_resolve(
    const AstraAssignTable *table, const char *assign, const char *identity,
    AstraVfsAssignClientFn client_for, void *context,
    char *path, uint32_t path_capacity, AstraLibraryReference *reference);

/**
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

/**
 * Open a provider through the current process namespace.
 *
 * The process namespace must already be initialized. This is a convenience
 * wrapper over astra_vfs_library_source_open(), not a second resolver.
 */
uint32_t astra_process_library_source_open(
    const char *identity, AstraVfsReadSource *source,
    AstraLibraryReference *reference);

#endif
