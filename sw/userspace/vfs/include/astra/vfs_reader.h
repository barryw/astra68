#ifndef ASTRA_VFS_READER_H
#define ASTRA_VFS_READER_H

#include <stdint.h>

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

#endif
