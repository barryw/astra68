#ifndef ASTRA_VFS_BACKEND_H
#define ASTRA_VFS_BACKEND_H

#include <stdint.h>

#include <astra/vfs_service.h>

/*
 * What a filesystem has to provide to sit behind the storage protocol.
 *
 * This is the seam that makes the filesystem replaceable. The service core
 * owns sessions, handles, generations, validation and the wire format; a
 * backend owns bytes on a medium and nothing else. lwext4 is one
 * implementation, the host test's in-memory tree is another, and neither
 * appears in the core.
 *
 * Every entry point returns an ASTRA_VFS_* status rather than an errno,
 * because errno sets differ between implementations and leaking one would
 * announce which backend is behind the protocol.
 *
 * A backend is called from one thread at a time. The core does not serialise
 * for it, because the core cannot know what the backend's own locking costs;
 * a backend that needs exclusion takes it itself.
 *
 * `node` is whatever the backend wants it to be -- an inode number, a pointer,
 * an index. The core stores it, checks nothing about it, and hands it back.
 * It never crosses the protocol boundary, so a client cannot forge one.
 */

typedef struct AstraVfsBackend AstraVfsBackend;

typedef struct AstraVfsNodeInfo {
    uint64_t size;
    uint16_t kind;
} AstraVfsNodeInfo;

typedef struct AstraVfsBackendOps {
    /*
     * Opens `path` and yields a backend-private token. `flags` carries the
     * ASTRA_VFS_OPEN_* set. A directory open is requested explicitly, so a
     * backend never has to guess what the caller meant by a path.
     */
    uint32_t (*open)(void *context, const char *path, uint32_t flags,
                     uintptr_t *node, AstraVfsNodeInfo *info);
    uint32_t (*close)(void *context, uintptr_t node);
    uint32_t (*read)(void *context, uintptr_t node, uint64_t offset,
                     void *buffer, uint32_t length, uint32_t *moved);
    uint32_t (*write)(void *context, uintptr_t node, uint64_t offset,
                      const void *buffer, uint32_t length, uint32_t *moved);
    uint32_t (*stat)(void *context, const char *path, AstraVfsNodeInfo *info);
    /*
     * Returns the entry at `index` in the directory at `path`. Index-addressed
     * rather than cursor-addressed because a cursor is per-session state the
     * service would have to reclaim when a client dies mid-scan, and the
     * protocol is stateless per request by design.
     */
    uint32_t (*readdir)(void *context, const char *path, uint32_t index,
                        char *name, uint32_t name_capacity,
                        AstraVfsNodeInfo *info);
    uint32_t (*mkdir)(void *context, const char *path);
    uint32_t (*unlink)(void *context, const char *path);
} AstraVfsBackendOps;

struct AstraVfsBackend {
    const AstraVfsBackendOps *ops;
    void *context;
};

#endif
