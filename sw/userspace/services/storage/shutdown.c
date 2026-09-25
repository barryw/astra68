#include "shutdown.h"

#include <astra/status.h>
#include <astra/syscall.h>
#include <ext4.h>
#include <stdbool.h>

#define MOUNT_POINT "/vol/"
#define DEVICE_NAME "astra"

uint32_t storage_flush_volume(void *context)
{
    (void)context;
    if (ext4_journal_commit(MOUNT_POINT) != EOK ||
        ext4_cache_write_back(MOUNT_POINT, false) != EOK)
        return ASTRA_STATUS_IO;
    return ASTRA_STATUS_OK;
}

uint32_t storage_unmount_volume(void *context)
{
    AstraBlockDevice *block = context;

    if (block == NULL || ext4_journal_stop(MOUNT_POINT) != EOK ||
        ext4_umount(MOUNT_POINT) != EOK ||
        ext4_device_unregister(DEVICE_NAME) != EOK ||
        astra_block_flush(block, ASTRA_DEADLINE_FOREVER) != ASTRA_BLOCK_OK)
        return ASTRA_STATUS_IO;
    return ASTRA_STATUS_OK;
}
