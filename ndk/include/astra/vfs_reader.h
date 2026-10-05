#ifndef ASTRA_VFS_READER_H
#define ASTRA_VFS_READER_H

/**
 * @file vfs_reader.h
 * @brief Borrowed random-access reads of one resolved file, and executable
 *        providers opened through the process namespace.
 */

#include <stdint.h>

#include <astra/library.h>
#include <astra/types.h>
#include <astra/vfs_union.h>

ASTRA_EXTERN_C_BEGIN

/**
 * A resolved regular file kept open for borrowed random-access reads.
 * The returned bytes belong to the VFS transport and remain valid only until
 * the next operation on the same client.
 */
typedef struct AstraVfsReadSource {
    /** Client that serves the file. */
    AstraVfsClient *client;
    /** Open file on @ref client. */
    AstraVfsFile file;
    /** File length in bytes when it was opened. */
    uint32_t length;
} AstraVfsReadSource;

/** Initializer for a closed ::AstraVfsReadSource. */
#define ASTRA_VFS_READ_SOURCE_INIT { NULL, ASTRA_VFS_FILE_INVALID, 0u }

/**
 * Resolve @p path through an assign table and open it for reading.
 *
 * @param[out] source Closed source that receives the open file.
 * @param table Assign table that resolves the path's volume.
 * @param path Volume-qualified path of a regular file.
 * @param client_for Supplies the VFS client for a resolved assign.
 * @param context Passed to @p client_for.
 * @return ASTRA_VFS_OK or an ASTRA_VFS_ERR_* status.
 */
uint32_t astra_vfs_read_source_open(
    AstraVfsReadSource *source, const AstraAssignTable *table,
    const char *path, AstraVfsAssignClientFn client_for, void *context);

/**
 * Borrow up to @p length bytes at @p offset.
 *
 * @param context An open ::AstraVfsReadSource.
 * @param offset Byte offset within the file.
 * @param length Bytes wanted.
 * @param[out] bytes Receives a pointer valid until the next operation on the
 *                   source's client.
 * @param[out] moved Receives the bytes available at @p bytes.
 * @return ASTRA_VFS_OK or an ASTRA_VFS_ERR_* status.
 */
uint32_t astra_vfs_read_source_read_at(
    void *context, uint32_t offset, uint32_t length,
    const uint8_t **bytes, uint32_t *moved);

/**
 * Close a source opened by astra_vfs_read_source_open().
 *
 * @param context An open ::AstraVfsReadSource; it is left closed.
 * @return ASTRA_VFS_OK or an ASTRA_VFS_ERR_* status.
 */
uint32_t astra_vfs_read_source_close(void *context);

/**
 * Open the installed provider of one library identity through the current
 * process namespace, which must already be initialized.
 *
 * @param identity Exact library identity, such as `system.library.3`.
 * @param[out] source Closed source that receives the provider binary.
 * @param[out] reference Receives the provider's library reference.
 * @return ASTRA_VFS_OK or an ASTRA_VFS_ERR_* status.
 */
uint32_t astra_process_library_source_open(
    const char *identity, AstraVfsReadSource *source,
    AstraLibraryReference *reference);

ASTRA_EXTERN_C_END

#endif
