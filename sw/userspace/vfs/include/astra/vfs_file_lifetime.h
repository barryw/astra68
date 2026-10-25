#ifndef ASTRA_VFS_FILE_LIFETIME_H
#define ASTRA_VFS_FILE_LIFETIME_H

#include <astra/vfs_client.h>

/* A fork copies local owners, not provider-token ownership. Their retained
 * cache pins protect this pointer until close or explicit receipt adoption. */
static inline uint32_t astra_vfs_file_owner_status(const AstraVfsClient *client,
    uint32_t epoch)
{
    if (client == NULL) return ASTRA_VFS_ERR_BAD_HANDLE;
    return epoch == client->ownership_epoch ? ASTRA_VFS_OK : ASTRA_VFS_ERR_LOST;
}

static inline uint32_t astra_vfs_file_owner_close(AstraVfsClient *client,
    AstraVfsFile file, uint32_t epoch)
{
    uint32_t status = astra_vfs_file_owner_status(client, epoch);
    if (status != ASTRA_VFS_OK) {
        if (client != NULL && file != ASTRA_VFS_FILE_INVALID &&
            client->description_release != NULL)
            client->description_release(client, epoch);
        return status;
    }
    return astra_vfs_close(client, file);
}

#endif
