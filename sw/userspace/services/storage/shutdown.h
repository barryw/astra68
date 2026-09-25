#ifndef ASTRA_STORAGE_SHUTDOWN_H
#define ASTRA_STORAGE_SHUTDOWN_H

#include <astra/block_device.h>
#include <stdint.h>

/* ext4's mount operations; VFS owns the filesystem-independent ordering. */
uint32_t storage_flush_volume(void *context);
uint32_t storage_unmount_volume(void *context);

#endif
