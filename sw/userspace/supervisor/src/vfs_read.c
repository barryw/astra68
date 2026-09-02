#include <vfs_host.h>

#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_union.h>

uint32_t supervisor_vfs_read(const char *path, void *buffer,
                             uint32_t capacity, uint32_t *length)
{
    uint8_t *bytes = buffer;

    if (path == NULL || buffer == NULL || capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    {
        AstraVfsClient *client = NULL;
        AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
        uint64_t size = 0u;
        uint16_t kind = 0u;
        uint32_t status;
        char wire[ASTRA_VFS_PATH_MAX];

        status = astra_vfs_assign_open(
            supervisor_assigns(), path, ASTRA_RIGHT_READ,
            ASTRA_VFS_OPEN_READ, supervisor_vfs_assign_client, NULL, wire,
            sizeof(wire), &file, &size, &kind, &client, NULL);
        if (status != ASTRA_VFS_OK)
            return status;
        if (kind != ASTRA_VFS_KIND_FILE || size > capacity) {
            (void)astra_vfs_close(client, file);
            return ASTRA_VFS_ERR_LIMIT;
        }
        /*
         * Bulk, not inline. astra_vfs_read is capped at ASTRA_VFS_IO_MAX --
         * 192 bytes -- because that is what fits beside the header in one
         * message, so reading an application image through it costs one
         * synchronous round trip to the storage service per 192 bytes: 216 of
         * them for a 41 KiB program, and a round trip is milliseconds. The
         * shared-area path moves ASTRA_VFS_BULK_MAX per trip and is what every
         * other image reader in the system already uses; it falls back to the
         * inline path by itself against a service too old to offer it.
         */
        while (*length < (uint32_t)size) {
            uint32_t moved = 0u;

            status = astra_vfs_port_read_bulk(client, file, *length,
                                              bytes + *length,
                                              (uint32_t)size - *length,
                                              &moved);
            if (status != ASTRA_VFS_OK || moved == 0u)
                break;
            *length += moved;
        }
        (void)astra_vfs_close(client, file);
        return status == ASTRA_VFS_OK && *length == (uint32_t)size ?
            ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
    }
}
