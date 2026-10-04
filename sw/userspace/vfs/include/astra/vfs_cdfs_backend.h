#ifndef ASTRA_VFS_CDFS_BACKEND_H
#define ASTRA_VFS_CDFS_BACKEND_H

#include <astra/block_device.h>
#include <astra/filesystem_library.h>
#include <astra/vfs_backend.h>
#include <odfs/api.h>

#define ASTRA_CDFS_SECTOR_BYTES 2048u

/* Sources are borrowed and must not be repurposed until unmount.
 * Image contents must remain immutable. */
typedef struct AstraCdfsMedia {
    odfs_media_t media;
    AstraBlockDevice *block;
    AstraFile *file;
    AstraBlockGeometry geometry;
    int64_t mtime;
    uint32_t sectors;
    uint32_t status;
} AstraCdfsMedia;

uint32_t astra_cdfs_media_block(AstraCdfsMedia *media, AstraBlockDevice *block);
uint32_t astra_cdfs_media_file(AstraCdfsMedia *media, AstraFile *file);
uint32_t astra_cdfs_media_check(AstraCdfsMedia *media);

typedef int (*AstraCdfsLock)(void *context);
typedef void (*AstraCdfsUnlock)(void *context);
typedef struct AstraCdfsFile AstraCdfsFile;

typedef struct AstraVfsCdfsBackend {
    odfs_mount_t mount;
    AstraCdfsMedia *source;
    AstraCdfsFile *files;
    AstraCdfsLock lock;
    AstraCdfsUnlock unlock;
    void *lock_context;
    uint8_t mounted;
} AstraVfsCdfsBackend;

/* ODFS caches and parsers share one lock per mount. NULL locks are for
 * single-threaded callers. No source or open handle is owned by ODFS. */
uint32_t astra_vfs_cdfs_mount(AstraVfsCdfsBackend *backend,
    AstraCdfsMedia *source, const odfs_mount_opts_t *options,
    AstraCdfsLock lock, AstraCdfsUnlock unlock, void *lock_context);
/* Refuses while files remain open; service shutdown closes sessions first. */
uint32_t astra_vfs_cdfs_unmount(void *context);
const AstraVfsBackendOps *astra_vfs_cdfs_ops(void);

#endif
