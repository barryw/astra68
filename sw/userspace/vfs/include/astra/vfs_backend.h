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

/*
 * What a listing has to show and an editor has to preserve. `kind` and `size`
 * were enough while every caller was a program that read a whole file; `ls -l`
 * and any ported Unix tool need the rest, and a backend that has them and a
 * protocol that drops them is the same as not having them.
 *
 * A backend that cannot answer a field leaves it zero. Zero is a real answer
 * here -- mode 0 means "this filesystem has no permission bits" rather than
 * "no permissions" -- so a caller formats what it was given and does not
 * invent a default that looks authoritative.
 */
typedef struct AstraVfsNodeInfo {
    uint64_t size;
    int64_t mtime;          /* seconds since the epoch, 0 when unknown */
    uint32_t uid;
    uint32_t gid;
    uint16_t kind;
    uint16_t mode;          /* POSIX permission and type bits */
    uint16_t nlink;
    uint16_t reserved;
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
     * Returns one entry of the directory at `path`, resuming from `cookie`,
     * and yields in `next` the cookie that reaches the entry after it. A
     * `cookie` of zero starts a scan; running past the last entry is
     * ASTRA_VFS_ERR_NOT_FOUND, which is how a listing ends rather than a
     * failure.
     *
     * The cookie is the backend's own -- an offset, an index, whatever it can
     * resume from in constant time. The service stores nothing and hands it
     * back unread, so the protocol stays stateless per request and a client
     * that dies mid-scan strands nothing. That was the reason this was
     * index-addressed; a cookie keeps the property and drops the cost, because
     * an index makes a backend walk from the start for every entry and a
     * directory listing quadratic.
     */
    uint32_t (*readdir)(void *context, const char *path, uint64_t cookie,
                        char *name, uint32_t name_capacity,
                        AstraVfsNodeInfo *info, uint64_t *next);
    uint32_t (*mkdir)(void *context, const char *path);
    uint32_t (*unlink)(void *context, const char *path);
} AstraVfsBackendOps;

struct AstraVfsBackend {
    const AstraVfsBackendOps *ops;
    void *context;
};

#endif
