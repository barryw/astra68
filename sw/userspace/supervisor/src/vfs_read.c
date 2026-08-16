#include <vfs_host.h>

#include <astra/syscall.h>
#include <astra/vfs_path.h>

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
        while (*length < (uint32_t)size) {
            uint32_t moved = 0u;

            status = astra_vfs_read(client, file, *length, bytes + *length,
                                    (uint32_t)size - *length, &moved);
            if (status != ASTRA_VFS_OK || moved == 0u)
                break;
            *length += moved;
        }
        (void)astra_vfs_close(client, file);
        return status == ASTRA_VFS_OK && *length == (uint32_t)size ?
            ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
    }
}
