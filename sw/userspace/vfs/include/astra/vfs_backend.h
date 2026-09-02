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
 * Backend operations may be called concurrently. A backend owns the locking
 * required by its filesystem, must not rely on the VFS service state lock,
 * and must not retain pointers into a request after returning.
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
     * Path metadata is no-follow: stat reports a final symbolic link and
     * readlink returns its stored target. Backends also leave intermediate
     * links unresolved (normally reporting NOT_DIR). The shared assign layer
     * alone follows links in Astra's logical namespace, rechecks rights after
     * traversal, and permits cross-filesystem targets.
     */
    /*
     * Opens `path` and yields a backend-private token. `flags` carries the
     * ASTRA_VFS_OPEN_* set. A directory open is requested explicitly, so a
     * backend never has to guess what the caller meant by a path.
     */
    uint32_t (*open)(void *context, const char *path, uint32_t flags,
                     uint16_t create_mode, uintptr_t *node,
                     AstraVfsNodeInfo *info);
    uint32_t (*close)(void *context, uintptr_t node);
    uint32_t (*read)(void *context, uintptr_t node, uint64_t offset,
                     void *buffer, uint32_t length, uint32_t *moved);
    uint32_t (*write)(void *context, uintptr_t node, uint64_t offset,
                      uint32_t flags, const void *buffer, uint32_t length,
                      uint32_t *moved, uint64_t *position);
    uint32_t (*sync)(void *context, uintptr_t node);
    uint32_t (*truncate)(void *context, uintptr_t node, uint64_t size);
    uint32_t (*stat)(void *context, const char *path, AstraVfsNodeInfo *info);
    /*
     * Returns one entry from an open `directory`, resuming from the backend's
     * opaque `cookie`. Zero starts a scan; running past the last entry returns
     * ASTRA_VFS_ERR_NOT_FOUND. Legacy path-only requests pass directory zero.
     * Open-handle lifecycle belongs to open/close, so stream-bound host
     * cookies are never replayed against a different directory stream.
     */
    uint32_t (*readdir)(void *context, uintptr_t directory,
                        const char *path, uint64_t cookie, char *name,
                        uint32_t name_capacity, AstraVfsNodeInfo *info,
                        uint64_t *next);
    uint32_t (*mkdir)(void *context, const char *path, uint16_t create_mode);
    uint32_t (*unlink)(void *context, const char *path);
    uint32_t (*rename)(void *context, const char *from, const char *to);
    uint32_t (*chmod)(void *context, const char *path, uint16_t mode);
    uint32_t (*readlink)(void *context, const char *path, void *buffer,
                         uint32_t capacity, uint32_t *length);
    uint32_t (*symlink)(void *context, const char *target, const char *path);
} AstraVfsBackendOps;

struct AstraVfsBackend {
    const AstraVfsBackendOps *ops;
    void *context;
};

/* Shared wire encoders used by both the service and the direct host adapter. */
uint32_t astra_vfs_backend_readdir_into(
    const AstraVfsBackend *backend, uintptr_t directory, const char *path,
    uint64_t cursor, uint32_t entry_limit, uint8_t *buffer,
    uint32_t capacity, uint32_t *used, uint64_t *next);
uint32_t astra_vfs_backend_read_path(
    const AstraVfsBackend *backend, const char *path, void *buffer,
    uint32_t capacity, uint32_t *moved, uint64_t *node_size);

/* Shared operation table entries for immutable synthetic filesystems. */
static inline uint32_t
astra_vfs_backend_deny_write(void *context, uintptr_t node, uint64_t offset,
                             uint32_t flags, const void *buffer,
                             uint32_t length, uint32_t *moved,
                             uint64_t *position)
{
    (void)context;
    (void)node;
    (void)flags;
    (void)buffer;
    (void)length;
    *moved = 0u;
    *position = offset;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_sync(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_truncate(void *context, uintptr_t node, uint64_t size)
{
    (void)context;
    (void)node;
    (void)size;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_mkdir(void *context, const char *path,
                             uint16_t create_mode)
{
    (void)context;
    (void)path;
    (void)create_mode;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_unlink(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_rename(void *context, const char *from, const char *to)
{
    (void)context;
    (void)from;
    (void)to;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_deny_chmod(void *context, const char *path, uint16_t mode)
{
    (void)context;
    (void)path;
    (void)mode;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_no_readlink(void *context, const char *path, void *buffer,
                              uint32_t capacity, uint32_t *length)
{
    (void)context;
    (void)path;
    (void)buffer;
    (void)capacity;
    (void)length;
    return ASTRA_VFS_ERR_NOT_FOUND;
}

static inline uint32_t
astra_vfs_backend_deny_symlink(void *context, const char *target,
                               const char *path)
{
    (void)context;
    (void)target;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

#endif
