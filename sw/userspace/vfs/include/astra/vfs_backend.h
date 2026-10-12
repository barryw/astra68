#ifndef ASTRA_VFS_BACKEND_H
#define ASTRA_VFS_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include <astra/vfs_service.h>

/* A transport's missing peer is a lost storage object for its caller. */
static inline uint32_t astra_vfs_storage_status(uint32_t status)
{
    return status == ASTRA_VFS_ERR_PEER ? ASTRA_VFS_ERR_LOST : status;
}

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
 * A backend marks fields it knows in meta.valid. Unmarked values are unknown;
 * zero permission bits can be a real answer.
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
    AstraVfsNodeMeta meta;
} AstraVfsNodeInfo;

/* Publish known permissions and links, including real zero values. */
static inline void astra_vfs_node_info_mode_links(AstraVfsNodeInfo *info,
                                                  uint16_t mode,
                                                  uint32_t nlink)
{
    info->mode = mode;
    info->nlink = nlink > UINT16_MAX ? UINT16_MAX : (uint16_t)nlink;
    info->meta.nlink = nlink;
    info->meta.valid |= ASTRA_VFS_META_MODE | ASTRA_VFS_META_NLINK;
}

/* Service and direct lane publish the same metadata contract. */
static inline void astra_vfs_node_info_publish(AstraVfsReply *reply,
                                               const AstraVfsNodeInfo *info)
{
    reply->node_size = info->size;
    reply->mtime = info->mtime;
    reply->uid = info->uid;
    reply->gid = info->gid;
    reply->kind = info->kind;
    reply->mode = info->mode;
    reply->nlink = info->nlink;
    reply->meta = info->meta;
}

typedef struct AstraVfsBackendOps {
    /*
     * Path metadata is no-follow: stat reports a final symbolic link and
     * readlink returns its stored target. Providers implementing lookup resolve
     * components in order and return ESCAPED at their root. Other backends
     * leave intermediate links unresolved (normally reporting NOT_DIR); the
     * shared assign layer follows those links and namespace continuations.
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
                      uint32_t flags, uint64_t max_end,
                      const void *buffer, uint32_t length,
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
    uint32_t (*unlink)(void *context, const char *path, uint32_t flags);
    uint32_t (*rename)(void *context, const char *from, const char *to);
    uint32_t (*chmod)(void *context, const char *path, uint16_t mode);
    uint32_t (*readlink)(void *context, const char *path, void *buffer,
                         uint32_t capacity, uint32_t *length);
    uint32_t (*symlink)(void *context, const char *target, const char *path);
    uint32_t (*link)(void *context, const char *from, const char *to);
    /** Open `path` beneath an already-open directory node. */
    uint32_t (*open_at)(void *context, uintptr_t directory, const char *path,
                        uint32_t flags, uint16_t create_mode,
                        uintptr_t *node, AstraVfsNodeInfo *info);
    /** Remove `path` beneath an open directory node. */
    uint32_t (*unlink_at)(void *context, uintptr_t directory,
                          const char *path, uint32_t flags);
    /** Change permissions on an already-open node. */
    uint32_t (*chmod_node)(void *context, uintptr_t node, uint16_t mode);
    /** Change permissions beneath an open directory node. */
    uint32_t (*chmod_at)(void *context, uintptr_t directory,
                         const char *path, uint16_t mode, uint32_t flags);
    /** Report capacity for either `node` or `path`; one must be supplied. */
    uint32_t (*filesystem_info)(void *context, uintptr_t node,
                                const char *path,
                                AstraVfsFilesystemInfo *info);
    /** Read no-follow metadata beneath an already-open directory node. */
    uint32_t (*stat_at)(void *context, uintptr_t directory,
                        const char *path, uint32_t flags, AstraVfsNodeInfo *info);
    /** Read current metadata from an open node, independent of its path. */
    uint32_t (*stat_node)(void *context, uintptr_t node,
                          AstraVfsNodeInfo *info);
    /** Ordered lookup, optional only for providers resolved by the native Kit. */
    uint32_t (*lookup)(void *context, uintptr_t directory, const char *path, uint32_t flags,
                       uint32_t links_left, AstraVfsNodeInfo *info,
                       AstraVfsEscape *escape);
    /** Optional provider-owned descriptions, advertised as one feature. */
    uint32_t (*read_sequential)(void *context, uintptr_t node, void *buffer,
                                uint32_t length, uint32_t *moved,
                                uint64_t *position);
    uint32_t (*seek)(void *context, uintptr_t node, int64_t delta,
                     uint32_t origin, uint64_t *position);
    uint32_t (*share)(void *context, uintptr_t node, uint64_t *token);
    uint32_t (*adopt)(void *context, uint64_t token, uint32_t flags,
                      uintptr_t *node);
    uint32_t (*file_flags)(void *context, uintptr_t node, uint32_t mask,
                           uint32_t flags, uint32_t *result);
    /** Optional atomic packed directory batch; CURSOR_CURRENT uses shared state. */
    uint32_t (*readdir_batch)(void *context, uintptr_t directory,
                              const char *path, uint64_t cursor,
                              uint32_t entry_limit, uint8_t *buffer,
                              uint32_t capacity, uint32_t *used,
                              uint64_t *next);
    /** Optional anchored path operations; zero bases mean the grant root. */
    uint32_t (*mkdir_at)(void *, uintptr_t, const char *, uint16_t);
    uint32_t (*rename_at)(void *, uintptr_t, const char *, uintptr_t, const char *);
    uint32_t (*link_at)(void *, uintptr_t, const char *, uintptr_t, const char *);
    uint32_t (*symlink_at)(void *, const char *, uintptr_t, const char *);
    uint32_t (*readlink_at)(void *, uintptr_t, const char *, void *, uint32_t, uint32_t *);
    uint32_t (*directory_path)(void *, uintptr_t, void *, uint32_t, uint32_t *);
} AstraVfsBackendOps;

struct AstraVfsBackend {
    const AstraVfsBackendOps *ops;
    void *context;
};

/* Both inline and area transfers obey the same description contract. */
static inline uint32_t
astra_vfs_backend_read_position(const AstraVfsBackend *backend,
                                uintptr_t node, uint64_t offset,
                                uint32_t flags, void *buffer,
                                uint32_t length, uint32_t *moved,
                                uint64_t *position)
{
    uint32_t status;

    *moved = 0u;
    *position = offset;
    if ((flags & ~ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u) {
        if (backend->ops->read_sequential == NULL)
            return ASTRA_VFS_ERR_UNSUPPORTED;
        status = backend->ops->read_sequential(backend->context, node,
            buffer, length, moved, position);
    } else {
        status = backend->ops->read(backend->context, node, offset,
                                     buffer, length, moved);
        if (status == ASTRA_VFS_OK) {
            if (offset > UINT64_MAX - *moved)
                return ASTRA_VFS_ERR_PROTOCOL;
            *position = offset + *moved;
        }
    }
    return status == ASTRA_VFS_OK &&
        (*moved > length ||
         ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
          (*position < *moved || *position > INT64_MAX))) ?
        ASTRA_VFS_ERR_PROTOCOL : status;
}

/* One validation and dispatch contract for root and anchored path operations. */
uint32_t astra_vfs_backend_path_operation(const AstraVfsBackend *backend,
    uint32_t operation, const AstraVfsRequest *request, uintptr_t base,
    uintptr_t second_base, AstraVfsReply *reply);

/* Shared wire encoders used by both the service and the direct host adapter. */
uint32_t astra_vfs_backend_readdir_into(
    const AstraVfsBackend *backend, uintptr_t directory, const char *path,
    uint64_t cursor, uint32_t entry_limit, uint8_t *buffer,
    uint32_t capacity, uint32_t *used, uint64_t *next);
uint32_t astra_vfs_backend_read_path(
    const AstraVfsBackend *backend, const char *path, void *buffer,
    uint32_t capacity, uint32_t *moved, uint64_t *node_size);
uint32_t astra_vfs_backend_filesystem_info_into(
    const AstraVfsBackend *backend, uintptr_t node, const char *path,
    void *buffer, uint32_t capacity);

/* Shared operation table entries for immutable synthetic filesystems. */
static inline uint32_t
astra_vfs_backend_deny_write(void *context, uintptr_t node, uint64_t offset,
                             uint32_t flags, uint64_t max_end, const void *buffer,
                             uint32_t length, uint32_t *moved,
                             uint64_t *position)
{
    (void)context;
    (void)node;
    (void)flags;
    (void)max_end;
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
astra_vfs_backend_deny_unlink(void *context, const char *path, uint32_t flags)
{
    (void)context;
    (void)path;
    (void)flags;
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

static inline uint32_t
astra_vfs_backend_deny_link(void *context, const char *from, const char *to)
{
    (void)context;
    (void)from;
    (void)to;
    return ASTRA_VFS_ERR_ACCESS;
}

static inline uint32_t
astra_vfs_backend_no_open_at(void *context, uintptr_t directory,
                             const char *path, uint32_t flags,
                             uint16_t create_mode, uintptr_t *node,
                             AstraVfsNodeInfo *info)
{
    (void)context;
    (void)directory;
    (void)path;
    (void)flags;
    (void)create_mode;
    (void)node;
    (void)info;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_unlink_at(void *context, uintptr_t directory,
                               const char *path, uint32_t flags)
{
    (void)context;
    (void)directory;
    (void)path;
    (void)flags;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_chmod_node(void *context, uintptr_t node, uint16_t mode)
{
    (void)context;
    (void)node;
    (void)mode;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_chmod_at(void *context, uintptr_t directory,
                              const char *path, uint16_t mode,
                              uint32_t flags)
{
    (void)context;
    (void)directory;
    (void)path;
    (void)mode;
    (void)flags;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_filesystem_info(void *context, uintptr_t node,
                                     const char *path,
                                     AstraVfsFilesystemInfo *info)
{
    (void)context;
    (void)node;
    (void)path;
    (void)info;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_stat_at(void *context, uintptr_t directory,
                             const char *path, uint32_t flags, AstraVfsNodeInfo *info)
{
    (void)flags;
    (void)context;
    (void)directory;
    (void)path;
    (void)info;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static inline uint32_t
astra_vfs_backend_no_stat_node(void *context, uintptr_t node,
                               AstraVfsNodeInfo *info)
{
    (void)context;
    (void)node;
    (void)info;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

#endif
