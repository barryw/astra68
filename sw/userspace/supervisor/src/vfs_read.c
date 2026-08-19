#include <vfs_host.h>

#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_port_transport.h>

uint32_t supervisor_vfs_read_borrow(const char *path, const uint8_t **bytes,
                                    uint32_t *length)
{
    if (path == NULL || bytes == NULL || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *length = 0u;
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        uint64_t size = 0u;
        uint32_t moved = 0u;
        uint32_t status;
        char wire[ASTRA_VFS_PATH_MAX];

        status = astra_assign_resolve(supervisor_assigns(), path,
                                      ASTRA_RIGHT_READ, member, wire,
                                      sizeof(wire), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return status;
        if (status != ASTRA_VFS_OK)
            continue;
        client = supervisor_vfs_client_for(assign);
        if (client == NULL)
            continue;
        status = astra_vfs_port_read_path(client, wire, bytes, &moved,
                                          &size);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            continue;
        if (status != ASTRA_VFS_OK)
            return status;
        if (moved == 0u || moved != (uint32_t)size) {
            *bytes = NULL;
            return ASTRA_VFS_ERR_IO;
        }
        *length = moved;
        return ASTRA_VFS_OK;
    }
}

uint32_t supervisor_vfs_read(const char *path, void *buffer,
                             uint32_t capacity, uint32_t *length)
{
    uint8_t *bytes = buffer;

    if (path == NULL || buffer == NULL || capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        AstraVfsFile file;
        uint64_t size = 0u;
        uint16_t kind = 0u;
        uint32_t status;
        char wire[ASTRA_VFS_PATH_MAX];

        status = astra_assign_resolve(supervisor_assigns(), path,
                                      ASTRA_RIGHT_READ, member, wire,
                                      sizeof(wire), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return status;
        if (status != ASTRA_VFS_OK)
            continue;
        client = supervisor_vfs_client_for(assign);
        if (client == NULL)
            continue;
        status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ, &file,
                                &size, &kind);
        if (status != ASTRA_VFS_OK)
            continue;
        if (kind == ASTRA_VFS_KIND_DIRECTORY || size > capacity) {
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
