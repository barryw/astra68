#ifndef ASTRA_VFS_EXT4_BACKEND_H
#define ASTRA_VFS_EXT4_BACKEND_H

#include <stdint.h>

#include <astra/vfs_backend.h>

#include <ext4.h>

/*
 * lwext4 bound to the storage protocol's backend interface.
 *
 * This header is the boundary marker: it is the only one in the module that
 * includes <ext4.h>, and only a service that has already mounted a volume
 * includes it. The core, the client Kit and every caller above them are
 * compiled without lwext4 on the include path at all, which is what makes the
 * containment checkable rather than merely intended.
 */

#define ASTRA_VFS_EXT4_MOUNT_MAX 32u
/* The protocol's path plus the mount point prefix, with room for a separator. */
#define ASTRA_VFS_EXT4_PATH_MAX (ASTRA_VFS_PATH_MAX + ASTRA_VFS_EXT4_MOUNT_MAX)

typedef struct AstraVfsExt4File {
    ext4_file file;
    uint8_t state;
} AstraVfsExt4File;

typedef int (*AstraVfsExt4Lock)(void *context);
typedef void (*AstraVfsExt4Unlock)(void *context);

typedef struct AstraVfsExt4Backend {
    char mount_point[ASTRA_VFS_EXT4_MOUNT_MAX];
    AstraVfsExt4File *open_files;
    uint32_t file_capacity;
    uint32_t file_high_water;
    ext4_dir scan;
    char scan_path[ASTRA_VFS_EXT4_PATH_MAX];
    uint64_t scan_next;
    int scan_open;
    AstraVfsExt4Lock table_lock;
    AstraVfsExt4Unlock table_unlock;
    void *table_lock_context;
    AstraVfsExt4Lock scan_lock;
    AstraVfsExt4Unlock scan_unlock;
    void *scan_lock_context;
} AstraVfsExt4Backend;

/* Returns 0 when the mount point does not fit. The volume must already be
 * mounted at it; this layer never mounts, because mounting needs a device
 * lease and an arena that belong to the service holding them. */
int astra_vfs_ext4_init(AstraVfsExt4Backend *backend, const char *mount_point,
                        AstraVfsExt4File *files, uint32_t file_capacity);
int astra_vfs_ext4_set_locks(AstraVfsExt4Backend *backend,
                             AstraVfsExt4Lock table_lock,
                             AstraVfsExt4Unlock table_unlock,
                             void *table_context,
                             AstraVfsExt4Lock scan_lock,
                             AstraVfsExt4Unlock scan_unlock,
                             void *scan_context);

const AstraVfsBackendOps *astra_vfs_ext4_ops(void);

#endif
