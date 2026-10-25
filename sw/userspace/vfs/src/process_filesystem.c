/** @file process_filesystem.c
 *  @brief High-level Filesystem Kit facade over process VFS state.
 *
 * This unit deliberately depends on filesystem.library's public operations.
 * The process namespace and library-source resolver remain in vfs_process.c,
 * allowing the eager ELF interpreter to embed that closed bootstrap subset
 * without acquiring a dependency on the library it is responsible for
 * loading.
 */

#include <astra/filesystem_library.h>
#include <astra/runtime.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_path.h>
#include <astra/vfs_path_storage.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup)
{
    uint32_t status;

    if (filesystem == NULL ||
        filesystem->filesystem._private_assigns != NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_filesystem_attach_io(
        &filesystem->filesystem, astra_process_vfs_assigns(),
        astra_process_vfs_assign_client, astra_vfs_port_read_bulk_position,
        astra_vfs_port_write_bulk_position, NULL);
    if (status == ASTRA_VFS_OK) {
        filesystem->filesystem._private_cwd_acquire = astra_process_vfs_current_acquire;
        filesystem->filesystem._private_cwd_release = astra_process_vfs_current_release;
        filesystem->filesystem._private_cwd_qualify = astra_process_vfs_current_qualify;
        filesystem->filesystem._private_namespace_acquire = astra_process_vfs_namespace_acquire;
        filesystem->filesystem._private_namespace_release = astra_process_vfs_namespace_release;
    } else
        astra_process_filesystem_close(filesystem);
    return status;
}

uint32_t astra_process_read_file(AstraProcessFilesystem *filesystem,
                                 const char *path, void *bytes,
                                 uint32_t capacity, uint32_t *length)
{
    AstraVfsPathStorage wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (wire_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    const AstraAssignTable *assigns;
    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status;

    if (filesystem == NULL ||
        filesystem->filesystem._private_assigns == NULL || path == NULL ||
        bytes == NULL || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    assigns = astra_process_vfs_assigns();
    view = filesystem->filesystem;
    if (view._private_namespace_acquire != NULL) {
        status = view._private_namespace_acquire(&assigns, &owner);
        if (status != ASTRA_VFS_OK) return status;
        view._private_assigns = assigns;
        view._private_namespace_acquire = NULL;
        view._private_namespace_release = NULL;
    }

    /* Use the service's one-request whole-file operation when available. */
    {
        const AstraAssign *assign = NULL;
        char *wire = wire_storage.bytes;

        if (!astra_path_requires_walk(path) &&
            astra_assign_resolve_raw(assigns, path, ASTRA_RIGHT_READ, 0u, wire,
                                 ASTRA_VFS_PATH_MAX, &assign) == ASTRA_VFS_OK) {
            AstraVfsClient *client = astra_process_vfs_client_for(assign);
            const uint8_t *whole = NULL;
            uint32_t moved = 0u;
            uint64_t node_size = 0u;

            if (client != NULL &&
                astra_vfs_port_read_path(client, wire, &whole, &moved,
                                         &node_size) == ASTRA_VFS_OK &&
                node_size <= UINT32_MAX &&
                moved == (uint32_t)node_size && moved <= capacity) {
                memcpy(bytes, whole, moved);
                *length = moved;
                if (owner != NULL) filesystem->filesystem._private_namespace_release(owner);
                return ASTRA_VFS_OK;
            }
        }
    }

    status = astra_filesystem_open(&view, path,
                                   ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_file_info(&file, &info);
    if (status == ASTRA_VFS_OK && info.byte_size > capacity)
        status = ASTRA_VFS_ERR_LIMIT;
    while (status == ASTRA_VFS_OK && *length < info.byte_size) {
        uint32_t moved = 0u;

        status = astra_filesystem_read(&file, (uint8_t *)bytes + *length,
                                       (uint32_t)info.byte_size - *length,
                                       &moved);
        if (status != ASTRA_VFS_OK || moved == 0u)
            break;
        *length += moved;
    }
    if (file._private_file != ASTRA_VFS_FILE_INVALID)
        (void)astra_filesystem_close(&file);
    if (owner != NULL) filesystem->filesystem._private_namespace_release(owner);
    return status == ASTRA_VFS_OK && *length == info.byte_size ?
        ASTRA_VFS_OK : (status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_IO : status);
}

uint32_t astra_process_read_file_alloc(AstraProcessFilesystem *filesystem,
                                       const char *path, void **bytes,
                                       uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint8_t *storage = NULL;
    uint32_t status;

    if (filesystem == NULL ||
        filesystem->filesystem._private_assigns == NULL || path == NULL ||
        bytes == NULL || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *length = 0u;
    status = astra_filesystem_open(&filesystem->filesystem, path,
                                   ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_file_info(&file, &info);
    if (status == ASTRA_VFS_OK && info.byte_size >= UINT32_MAX)
        status = ASTRA_VFS_ERR_LIMIT;
    if (status == ASTRA_VFS_OK) {
        storage = astra_runtime_allocate((size_t)info.byte_size + 1u);
        if (storage == NULL)
            status = ASTRA_VFS_ERR_LIMIT;
    }
    while (status == ASTRA_VFS_OK && *length < info.byte_size) {
        uint32_t moved = 0u;

        status = astra_filesystem_read(
            &file, storage + *length, (uint32_t)info.byte_size - *length,
            &moved);
        if (status != ASTRA_VFS_OK || moved == 0u)
            break;
        *length += moved;
    }
    if (file._private_file != ASTRA_VFS_FILE_INVALID &&
        astra_filesystem_close(&file) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK && *length != info.byte_size)
        status = ASTRA_VFS_ERR_IO;
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(storage);
        *length = 0u;
        return status;
    }
    storage[*length] = '\0';
    *bytes = storage;
    return ASTRA_VFS_OK;
}

void astra_process_filesystem_close(AstraProcessFilesystem *filesystem)
{
    if (filesystem == NULL)
        return;
    astra_filesystem_detach(&filesystem->filesystem);
    *filesystem = (AstraProcessFilesystem)ASTRA_PROCESS_FILESYSTEM_INIT;
}
