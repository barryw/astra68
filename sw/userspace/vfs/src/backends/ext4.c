/*
 * lwext4 behind the storage protocol.
 *
 * This is the only translation unit in the module that knows what ext4 is, and
 * that containment is the deliverable. Replacing it with AstraFS, a RAM
 * filesystem or a network handler is a matter of writing another file with
 * this shape: nothing above it names a filesystem, and no client is
 * recompiled.
 *
 * The mount itself is not opened here. Whoever owns the volume mounts it and
 * passes the mount point in, because mounting needs a device lease and a
 * bounded arena that belong to the service that holds them, not to a
 * translation layer.
 */

#include <astra/vfs_ext4_backend.h>

#include <ext4.h>

#include <stddef.h>

enum {
    EXT4_FILE_FREE = 0,
    EXT4_FILE_OPENING,
    EXT4_FILE_OPEN,
    EXT4_FILE_CLOSING
};

enum {
    EXT4_HANDLE_NONE = 0,
    EXT4_HANDLE_FILE,
    EXT4_HANDLE_DIRECTORY
};

static int
table_lock(AstraVfsExt4Backend *backend)
{
    return backend->table_lock == NULL ||
           backend->table_lock(backend->table_lock_context);
}

static void
table_unlock(AstraVfsExt4Backend *backend)
{
    if (backend->table_unlock != NULL)
        backend->table_unlock(backend->table_lock_context);
}

static int
scan_lock(AstraVfsExt4Backend *backend)
{
    return backend->scan_lock == NULL ||
           backend->scan_lock(backend->scan_lock_context);
}

static void
scan_unlock(AstraVfsExt4Backend *backend)
{
    if (backend->scan_unlock != NULL)
        backend->scan_unlock(backend->scan_lock_context);
}

/*
 * lwext4 speaks errno; the protocol does not. Mapping here rather than letting
 * an errno reach the wire is what stops a client learning which filesystem is
 * underneath -- and errno sets differ between implementations, so a client that
 * had learned one would break on the next backend.
 */
static uint32_t
status_of(int rc)
{
    switch (rc) {
    case EOK:
        return ASTRA_VFS_OK;
    case ENOENT:
        return ASTRA_VFS_ERR_NOT_FOUND;
    case EEXIST:
        return ASTRA_VFS_ERR_EXISTS;
    case ENOTDIR:
        return ASTRA_VFS_ERR_NOT_DIR;
    case EISDIR:
        return ASTRA_VFS_ERR_IS_DIR;
    case EACCES:
    case EPERM:
        return ASTRA_VFS_ERR_ACCESS;
    case ENOSPC:
        return ASTRA_VFS_ERR_NO_SPACE;
    case ENOTEMPTY:
        return ASTRA_VFS_ERR_NOT_EMPTY;
    case EINVAL:
        return ASTRA_VFS_ERR_INVALID;
    case ENOTSUP:
        return ASTRA_VFS_ERR_UNSUPPORTED;
    case EBUSY:
        return ASTRA_VFS_ERR_BUSY;
    case EROFS:
        return ASTRA_VFS_ERR_ACCESS;
    default:
        return ASTRA_VFS_ERR_IO;
    }
}

/*
 * Joins the mount point and a protocol path.
 *
 * The protocol's paths are absolute within the volume; lwext4 wants them
 * prefixed with the mount point it was given. Refuses rather than truncates:
 * a truncated path names a different file and the caller would be answered
 * about that one.
 */
static int
build_path(const AstraVfsExt4Backend *backend, const char *path, char *out,
           uint32_t capacity)
{
    uint32_t length = 0u;
    uint32_t index = 0u;

    while (backend->mount_point[length] != '\0') {
        if (length + 1u >= capacity) {
            return 0;
        }
        out[length] = backend->mount_point[length];
        ++length;
    }
    /* One separator exactly, whether or not either side supplied it. */
    if (length != 0u && out[length - 1u] == '/') {
        --length;
    }
    if (length + 1u >= capacity) {
        return 0;
    }
    out[length++] = '/';
    if (path[index] == '/') {
        ++index;
    }
    while (path[index] != '\0') {
        if (length + 1u >= capacity) {
            return 0;
        }
        out[length++] = path[index++];
    }
    out[length] = '\0';
    return 1;
}

static AstraVfsExt4Backend *
backend_of(void *context)
{
    return context;
}

static void
close_scan(AstraVfsExt4Backend *backend)
{
    if (backend->scan_open)
        (void)ext4_dir_close(&backend->scan);
    backend->scan_open = 0;
}

static int
same_path(const char *left, const char *right)
{
    uint32_t index = 0u;

    while (left[index] == right[index]) {
        if (left[index] == '\0')
            return 1;
        ++index;
    }
    return 0;
}

static void
remember_path(AstraVfsExt4Backend *backend, const char *path)
{
    uint32_t index = 0u;

    do {
        backend->scan_path[index] = path[index];
    } while (path[index++] != '\0');
}

/*
 * The metadata a listing shows, from one lookup. Failure is not an error: a
 * node whose metadata cannot be read is still a node with a name and a size,
 * and the fields stay zero, which is this protocol's word for "not known"
 * rather than for a real zero.
 */
static void
fill_metadata(const ext4_file *file, AstraVfsNodeInfo *info, int with_size)
{
    uint32_t mode = 0u;
    uint32_t uid = 0u;
    uint32_t gid = 0u;
    uint32_t mtime = 0u;
    uint32_t nlink = 0u;
    uint64_t size = 0u;

    if (ext4_meta_get_handle(file, &mode, &uid, &gid, &mtime, &nlink,
                             with_size ? &size : NULL) != EOK)
        return;
    info->mode = (uint16_t)mode;
    info->uid = uid;
    info->gid = gid;
    info->mtime = (int64_t)(uint64_t)mtime;
    info->nlink = (uint16_t)nlink;
    if (with_size)
        info->size = size;
}

static uint32_t
ext4_backend_open_from(void *context, const ext4_dir *parent,
                       const char *path, uint32_t flags,
                       uint16_t create_mode, uintptr_t *node,
                       AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    uint32_t index;
    int native_flags;
    int rc;

    if (parent == NULL && !build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u) {
        if (!scan_lock(backend))
            return ASTRA_VFS_ERR_IO;
        close_scan(backend);
        scan_unlock(backend);
    }

    if (!table_lock(backend))
        return ASTRA_VFS_ERR_IO;
    for (index = 0u; index < backend->file_high_water; ++index) {
        if (backend->open_files[index].state == EXT4_FILE_FREE) {
            break;
        }
    }
    if (index == backend->file_high_water) {
        if (backend->file_high_water == backend->file_capacity) {
            table_unlock(backend);
            return ASTRA_VFS_ERR_LIMIT;
        }
        ++backend->file_high_water;
        backend->open_files[index].state = EXT4_FILE_FREE;
    }
    backend->open_files[index].state = EXT4_FILE_OPENING;
    backend->open_files[index].kind = EXT4_HANDLE_NONE;
    table_unlock(backend);
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u) {
        rc = parent == NULL ?
            ext4_dir_open(&backend->open_files[index].node.directory, full) :
            ext4_dir_openat(&backend->open_files[index].node.directory,
                            parent, path);
        if (rc != EOK) {
            if (table_lock(backend)) {
                backend->open_files[index].state = EXT4_FILE_FREE;
                table_unlock(backend);
            }
            return status_of(rc);
        }
        if (!table_lock(backend)) {
            (void)ext4_dir_close(&backend->open_files[index].node.directory);
            return ASTRA_VFS_ERR_IO;
        }
        backend->open_files[index].kind = EXT4_HANDLE_DIRECTORY;
        backend->open_files[index].state = EXT4_FILE_OPEN;
        table_unlock(backend);
        *node = (uintptr_t)(index + 1u);
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        fill_metadata(&backend->open_files[index].node.directory.f, info, 0);
        return ASTRA_VFS_OK;
    }
    native_flags = (flags & ASTRA_VFS_OPEN_WRITE) == 0u ? O_RDONLY :
                   (flags & ASTRA_VFS_OPEN_READ) != 0u ? O_RDWR : O_WRONLY;
    if ((flags & ASTRA_VFS_OPEN_CREATE) != 0u)
        native_flags |= O_CREAT;
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u)
        native_flags |= O_TRUNC;
    if ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u)
        native_flags |= O_EXCL;
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u)
        native_flags |= O_APPEND;
    rc = parent == NULL ?
        ext4_fopen2_mode(&backend->open_files[index].node.file, full,
                         native_flags,
                         create_mode == ASTRA_VFS_MODE_DEFAULT ?
                             UINT32_MAX : create_mode) :
        ext4_fopenat2_mode(&backend->open_files[index].node.file, parent,
                           path, native_flags,
                           create_mode == ASTRA_VFS_MODE_DEFAULT ?
                               UINT32_MAX : create_mode);
    if (rc != EOK) {
        if (table_lock(backend)) {
            backend->open_files[index].state = EXT4_FILE_FREE;
            table_unlock(backend);
        }
        return status_of(rc);
    }
    if (!table_lock(backend)) {
        (void)ext4_fclose(&backend->open_files[index].node.file);
        return ASTRA_VFS_ERR_IO;
    }
    backend->open_files[index].kind = EXT4_HANDLE_FILE;
    backend->open_files[index].state = EXT4_FILE_OPEN;
    table_unlock(backend);
    /*
     * The node is the slot index plus one, never a pointer. The core stores it
     * opaquely and hands it back, and a value that is only an index cannot be
     * turned into a wild pointer by a bug above this layer.
     */
    *node = (uintptr_t)(index + 1u);
    info->size = ext4_fsize(&backend->open_files[index].node.file);
    info->kind = ASTRA_VFS_KIND_FILE;
    fill_metadata(&backend->open_files[index].node.file, info, 0);
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_open(void *context, const char *path, uint32_t flags,
                  uint16_t create_mode, uintptr_t *node,
                  AstraVfsNodeInfo *info)
{
    return ext4_backend_open_from(context, NULL, path, flags, create_mode,
                                   node, info);
}

static ext4_file *
file_of(AstraVfsExt4Backend *backend, uintptr_t node)
{
    uint32_t index;

    if (node == 0u) {
        return NULL; /* a directory handle holds no lwext4 file */
    }
    index = (uint32_t)node - 1u;
    if (index >= backend->file_capacity || !table_lock(backend))
        return NULL;
    if (index >= backend->file_high_water ||
        backend->open_files[index].state != EXT4_FILE_OPEN ||
        backend->open_files[index].kind != EXT4_HANDLE_FILE) {
        table_unlock(backend);
        return NULL;
    }
    table_unlock(backend);
    return &backend->open_files[index].node.file;
}

static ext4_dir *
directory_of(AstraVfsExt4Backend *backend, uintptr_t node)
{
    uint32_t index;

    if (node == 0u)
        return NULL;
    index = (uint32_t)node - 1u;
    if (index >= backend->file_capacity || !table_lock(backend))
        return NULL;
    if (index >= backend->file_high_water ||
        backend->open_files[index].state != EXT4_FILE_OPEN ||
        backend->open_files[index].kind != EXT4_HANDLE_DIRECTORY) {
        table_unlock(backend);
        return NULL;
    }
    table_unlock(backend);
    return &backend->open_files[index].node.directory;
}

static uint32_t
ext4_backend_open_at(void *context, uintptr_t directory, const char *path,
                     uint32_t flags, uint16_t create_mode, uintptr_t *node,
                     AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_dir *parent = directory_of(backend, directory);

    if (parent == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return ext4_backend_open_from(context, parent, path, flags, create_mode,
                                   node, info);
}

static uint32_t
ext4_backend_close(void *context, uintptr_t node)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    AstraVfsExt4File *slot;
    uint32_t index;
    uint8_t kind;

    if (node == 0u)
        return ASTRA_VFS_OK;
    index = (uint32_t)node - 1u;
    if (index >= backend->file_capacity || !table_lock(backend))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    slot = &backend->open_files[index];
    if (index >= backend->file_high_water || slot->state != EXT4_FILE_OPEN) {
        table_unlock(backend);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    kind = slot->kind;
    slot->state = EXT4_FILE_CLOSING;
    table_unlock(backend);
    if (kind == EXT4_HANDLE_DIRECTORY)
        (void)ext4_dir_close(&slot->node.directory);
    else if (kind == EXT4_HANDLE_FILE)
        (void)ext4_fclose(&slot->node.file);
    else
        return ASTRA_VFS_ERR_IO;
    if (!table_lock(backend))
        return ASTRA_VFS_ERR_IO;
    slot->kind = EXT4_HANDLE_NONE;
    slot->state = EXT4_FILE_FREE;
    table_unlock(backend);
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_read(void *context, uintptr_t node, uint64_t offset,
                  void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);
    size_t count = 0u;
    int rc;

    *moved = 0u;
    if (file == NULL) {
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    rc = ext4_fseek(file, (int64_t)offset, SEEK_SET);
    if (rc != EOK) {
        return status_of(rc);
    }
    rc = ext4_fread(file, buffer, length, &count);
    if (rc != EOK) {
        return status_of(rc);
    }
    *moved = (uint32_t)count;
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_write(void *context, uintptr_t node, uint64_t offset,
                   uint32_t flags, const void *buffer, uint32_t length,
                   uint32_t *moved, uint64_t *position)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);
    size_t count = 0u;
    int rc;

    *moved = 0u;
    *position = offset;
    if (file == NULL) {
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    if ((flags & ASTRA_VFS_WRITE_APPEND) == 0u) {
        rc = ext4_fseek(file, (int64_t)offset, SEEK_SET);
        if (rc != EOK)
            return status_of(rc);
    }
    rc = ext4_fwrite(file, buffer, length, &count);
    if (rc != EOK) {
        return status_of(rc);
    }
    /*
     * A short write with EOK used to be how lwext4 reported a failure it had
     * discarded; see third_party/lwext4 patch 0004. The status is honest now,
     * and reporting the true count keeps it honest for the caller too.
    */
    *moved = (uint32_t)count;
    *position = ext4_ftell(file);
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_sync(void *context, uintptr_t node)
{
    AstraVfsExt4Backend *backend = backend_of(context);

    if (file_of(backend, node) == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    /* Like ext4/JBD2 fsync, durability requires the running journal
     * transaction to commit; checkpointing its metadata back to home blocks
     * is independent background work. */
    return status_of(ext4_journal_commit(backend->mount_point));
}

static uint32_t
ext4_backend_truncate(void *context, uintptr_t node, uint64_t size)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);

    if (file == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return status_of(ext4_ftruncate(file, size));
}

static uint32_t
ext4_backend_stat_from(AstraVfsExt4Backend *backend,
                        const ext4_dir *parent, const char *path,
                        AstraVfsNodeInfo *info)
{
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    uint32_t mode = 0u;
    uint32_t mtime = 0u;
    uint32_t nlink = 0u;
    int rc;

    if (parent == NULL && !build_path(backend, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    rc = parent == NULL ?
        ext4_meta_get(full, &mode, &info->uid, &info->gid, &mtime,
                       &nlink, &info->size) :
        ext4_meta_getat(parent, path, &mode, &info->uid, &info->gid,
                         &mtime, &nlink, &info->size);
    if (rc != EOK)
        return status_of(rc);
    info->mode = (uint16_t)mode;
    info->mtime = (int64_t)(uint64_t)mtime;
    info->nlink = (uint16_t)nlink;
    switch (mode & EXT4_INODE_MODE_TYPE_MASK) {
    case EXT4_INODE_MODE_DIRECTORY:
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        break;
    case EXT4_INODE_MODE_SOFTLINK:
        info->kind = ASTRA_VFS_KIND_SYMLINK;
        break;
    default:
        info->kind = ASTRA_VFS_KIND_FILE;
        break;
    }
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    return ext4_backend_stat_from(backend_of(context), NULL, path, info);
}

static uint32_t
ext4_backend_stat_at(void *context, uintptr_t directory,
                      const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_dir *parent = directory_of(backend, directory);

    if (parent == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return ext4_backend_stat_from(backend, parent, path, info);
}

static uint32_t
ext4_backend_stat_node(void *context, uintptr_t node,
                        AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);
    uint32_t mode = 0u;
    uint32_t mtime = 0u;
    uint32_t nlink = 0u;
    int rc;

    if (file == NULL) {
        ext4_dir *directory = directory_of(backend, node);

        if (directory == NULL)
            return ASTRA_VFS_ERR_BAD_HANDLE;
        file = &directory->f;
    }
    rc = ext4_meta_get_handle(file, &mode, &info->uid, &info->gid,
                               &mtime, &nlink, &info->size);
    if (rc != EOK)
        return status_of(rc);
    info->mode = (uint16_t)mode;
    info->mtime = (int64_t)(uint64_t)mtime;
    info->nlink = (uint16_t)nlink;
    info->kind = (mode & EXT4_INODE_MODE_TYPE_MASK) ==
                         EXT4_INODE_MODE_DIRECTORY ?
                     ASTRA_VFS_KIND_DIRECTORY :
                 (mode & EXT4_INODE_MODE_TYPE_MASK) ==
                         EXT4_INODE_MODE_SOFTLINK ?
                     ASTRA_VFS_KIND_SYMLINK : ASTRA_VFS_KIND_FILE;
    return ASTRA_VFS_OK;
}

/*
 * The cookie is lwext4's own iterator offset. `ext4_dir.next_off` is where the
 * next entry begins and `ext4_dir_entry_next` resumes from it. The ordinary
 * sequential caller keeps that directory open until EOF; an interleaved path
 * or cursor closes it and resumes from the supplied cookie, preserving the
 * stateless protocol while avoiding one ext4 open/close per listed entry.
 */
static uint32_t
ext4_backend_readdir(void *context, uintptr_t directory, const char *path,
                     uint64_t cookie, char *name, uint32_t capacity,
                     AstraVfsNodeInfo *info, uint64_t *next)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_dir *scan = NULL;
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    const ext4_direntry *entry;
    uint32_t copied;
    int rc;

    (void)directory;
    if (directory != 0u)
        scan = directory_of(backend, directory);
    else if (!build_path(backend, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    if (directory != 0u && scan == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    if (scan == NULL) {
        if (!backend->scan_open || backend->scan_next != cookie ||
            !same_path(backend->scan_path, full)) {
            close_scan(backend);
            rc = ext4_dir_open(&backend->scan, full);
            if (rc != EOK) {
                scan_unlock(backend);
                return status_of(rc);
            }
            backend->scan_open = 1;
            remember_path(backend, full);
        }
        scan = &backend->scan;
    }
    scan->next_off = cookie;
    for (;;) {
        entry = ext4_dir_entry_next(scan);
        if (entry == NULL) {
            if (directory == 0u)
                close_scan(backend);
            scan_unlock(backend);
            return ASTRA_VFS_ERR_NOT_FOUND; /* past the last entry */
        }
        if (entry->name_length != 0u) {
            break;
        }
    }
    if ((uint32_t)entry->name_length + 1u > capacity) {
        close_scan(backend);
        scan_unlock(backend);
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (copied = 0u; copied < entry->name_length; ++copied) {
        name[copied] = (char)entry->name[copied];
    }
    name[entry->name_length] = '\0';
    info->size = 0u;
    info->kind = entry->inode_type == EXT4_DE_DIR ?
        ASTRA_VFS_KIND_DIRECTORY :
        entry->inode_type == EXT4_DE_SYMLINK ? ASTRA_VFS_KIND_SYMLINK :
                                               ASTRA_VFS_KIND_FILE;
    /*
     * The entry itself carries a name and a type and nothing else, so the
     * metadata comes from the inode behind it. One lookup per entry, in this
     * process -- the alternative is a stat per entry from the client, which is
     * a cross-process round trip each and three orders of magnitude worse.
     */
    {
        uint32_t mode = 0u;
        uint32_t uid = 0u;
        uint32_t gid = 0u;
        uint32_t mtime = 0u;
        uint32_t nlink = 0u;
        uint64_t size = 0u;

        if (ext4_dir_entry_meta(scan, entry, &mode, &uid, &gid,
                                &mtime, &nlink, &size) == EOK) {
            info->mode = (uint16_t)mode;
            info->uid = uid;
            info->gid = gid;
            info->mtime = (int64_t)(uint64_t)mtime;
            info->nlink = (uint16_t)nlink;
            info->size = size;
        }
    }
    *next = scan->next_off == UINT64_MAX ? 0u : scan->next_off;
    if (directory == 0u)
        backend->scan_next = *next;
    scan_unlock(backend);
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_mkdir(void *context, const char *path, uint16_t create_mode)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_dir_mk_mode(
        full, create_mode == ASTRA_VFS_MODE_DEFAULT ? UINT32_MAX :
                                                        create_mode));
}

static uint32_t
ext4_backend_unlink(void *context, const char *path)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    int rc;

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    rc = ext4_fremove(full);
    if (rc == EOK) {
        return ASTRA_VFS_OK;
    }
    /* The legacy VFS remove operation may remove an empty directory, never a
     * nonempty one. lwext4's ext4_dir_rm is recursive. */
    if (rc == EISDIR || rc == EPERM || rc == EACCES) {
        return status_of(ext4_dir_rmdir(full));
    }
    return status_of(rc);
}

static uint32_t
ext4_backend_unlink_at(void *context, uintptr_t directory,
                        const char *path, uint32_t flags)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_dir *parent = directory_of(backend, directory);

    if (parent == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || path[0] == '/' || path[0] == '\0' ||
        (flags & ~ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_unlinkat(
        parent, path, (flags & ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u));
}

static uint32_t
ext4_backend_rename(void *context, const char *from, const char *to)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char old_path[ASTRA_VFS_EXT4_PATH_MAX];
    char new_path[ASTRA_VFS_EXT4_PATH_MAX];

    if (!build_path(backend, from, old_path, sizeof(old_path)) ||
        !build_path(backend, to, new_path, sizeof(new_path)))
        return ASTRA_VFS_ERR_INVALID;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_frename(old_path, new_path));
}

static uint32_t
ext4_backend_chmod(void *context, const char *path, uint16_t mode)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];

    if (!build_path(backend, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_mode_set(full, mode));
}

static uint32_t
ext4_backend_chmod_node(void *context, uintptr_t node, uint16_t mode)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);

    if (file == NULL) {
        ext4_dir *directory = directory_of(backend, node);

        if (directory == NULL)
            return ASTRA_VFS_ERR_BAD_HANDLE;
        file = &directory->f;
    }
    return status_of(ext4_mode_set_handle(file, mode));
}

static uint32_t
ext4_backend_chmod_at(void *context, uintptr_t directory,
                       const char *path, uint16_t mode, uint32_t flags)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_dir *parent = directory_of(backend, directory);

    if (parent == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || path[0] == '/' || path[0] == '\0' ||
        (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    /* lwext4 does not follow final symlinks. The inode adapter rejects them
     * rather than silently chmodding a link when the caller wanted its target. */
    return status_of(ext4_mode_setat(parent, path, mode));
}

static uint32_t
ext4_backend_readlink(void *context, const char *path, void *buffer,
                      uint32_t capacity, uint32_t *length)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    size_t moved = 0u;
    int rc;

    if (buffer == NULL || length == NULL || capacity == 0u ||
        !build_path(backend, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    rc = ext4_readlink(full, buffer, capacity, &moved);
    if (rc == EOK)
        *length = (uint32_t)moved;
    return status_of(rc);
}

static uint32_t
ext4_backend_symlink(void *context, const char *target, const char *path)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];

    if (target == NULL || target[0] == '\0' ||
        !build_path(backend, path, full, sizeof(full)))
        return ASTRA_VFS_ERR_INVALID;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_fsymlink(target, full));
}

static uint32_t
ext4_backend_link(void *context, const char *from, const char *to)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char from_full[ASTRA_VFS_EXT4_PATH_MAX];
    char to_full[ASTRA_VFS_EXT4_PATH_MAX];

    if (!build_path(backend, from, from_full, sizeof(from_full)) ||
        !build_path(backend, to, to_full, sizeof(to_full)))
        return ASTRA_VFS_ERR_INVALID;
    if (!scan_lock(backend))
        return ASTRA_VFS_ERR_IO;
    close_scan(backend);
    scan_unlock(backend);
    return status_of(ext4_flink(from_full, to_full));
}

static uint32_t
ext4_backend_filesystem_info(void *context, uintptr_t node,
                             const char *path, AstraVfsFilesystemInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    struct ext4_mount_stats stats;
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    int rc;

    if (info == NULL || ((node == 0u) == (path == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    if (node != 0u) {
        if (file_of(backend, node) == NULL &&
            directory_of(backend, node) == NULL)
            return ASTRA_VFS_ERR_BAD_HANDLE;
    } else {
        if (!build_path(backend, path, full, sizeof(full)))
            return ASTRA_VFS_ERR_INVALID;
        rc = ext4_inode_exist(full, EXT4_DE_UNKNOWN);
        if (rc != EOK)
            return status_of(rc);
    }
    rc = ext4_mount_point_stats(backend->mount_point, &stats);
    if (rc != EOK)
        return status_of(rc);
    *info = (AstraVfsFilesystemInfo){0};
    info->size = sizeof(*info);
    info->block_size = stats.block_size;
    info->fragment_size = stats.block_size;
    info->blocks = stats.blocks_count;
    info->blocks_free = stats.free_blocks_count;
    info->blocks_available = stats.free_blocks_count;
    info->files = stats.inodes_count;
    info->files_free = stats.free_inodes_count;
    info->name_max = EXT4_DIRECTORY_FILENAME_LEN;
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps ext4_ops = {
    .open = ext4_backend_open,
    .close = ext4_backend_close,
    .read = ext4_backend_read,
    .write = ext4_backend_write,
    .sync = ext4_backend_sync,
    .truncate = ext4_backend_truncate,
    .stat = ext4_backend_stat,
    .readdir = ext4_backend_readdir,
    .mkdir = ext4_backend_mkdir,
    .unlink = ext4_backend_unlink,
    .rename = ext4_backend_rename,
    .chmod = ext4_backend_chmod,
    .readlink = ext4_backend_readlink,
    .symlink = ext4_backend_symlink,
    .link = ext4_backend_link,
    .open_at = ext4_backend_open_at,
    .unlink_at = ext4_backend_unlink_at,
    .chmod_node = ext4_backend_chmod_node,
    .chmod_at = ext4_backend_chmod_at,
    .filesystem_info = ext4_backend_filesystem_info,
    .stat_at = ext4_backend_stat_at,
    .stat_node = ext4_backend_stat_node,
};

const AstraVfsBackendOps *
astra_vfs_ext4_ops(void)
{
    return &ext4_ops;
}

int
astra_vfs_ext4_init(AstraVfsExt4Backend *backend, const char *mount_point,
                    AstraVfsExt4File *files, uint32_t file_capacity)
{
    uint32_t index = 0u;

    if (backend == NULL || mount_point == NULL || files == NULL ||
        file_capacity == 0u || file_capacity > ASTRA_VFS_FILE_HANDLE_MAX) {
        return 0;
    }
    while (mount_point[index] != '\0') {
        if (index + 1u >= (uint32_t)sizeof(backend->mount_point)) {
            return 0;
        }
        backend->mount_point[index] = mount_point[index];
        ++index;
    }
    backend->mount_point[index] = '\0';
    backend->open_files = files;
    backend->file_capacity = file_capacity;
    backend->file_high_water = 0u;
    backend->scan_open = 0;
    backend->scan_next = 0u;
    backend->table_lock = NULL;
    backend->table_unlock = NULL;
    backend->table_lock_context = NULL;
    backend->scan_lock = NULL;
    backend->scan_unlock = NULL;
    backend->scan_lock_context = NULL;
    return 1;
}

int
astra_vfs_ext4_set_locks(AstraVfsExt4Backend *backend,
                         AstraVfsExt4Lock table_lock_fn,
                         AstraVfsExt4Unlock table_unlock_fn,
                         void *table_context,
                         AstraVfsExt4Lock scan_lock_fn,
                         AstraVfsExt4Unlock scan_unlock_fn,
                         void *scan_context)
{
    if (backend == NULL || table_lock_fn == NULL || table_unlock_fn == NULL ||
        scan_lock_fn == NULL || scan_unlock_fn == NULL ||
        backend->table_lock != NULL || backend->scan_lock != NULL)
        return 0;
    backend->table_lock = table_lock_fn;
    backend->table_unlock = table_unlock_fn;
    backend->table_lock_context = table_context;
    backend->scan_lock = scan_lock_fn;
    backend->scan_unlock = scan_unlock_fn;
    backend->scan_lock_context = scan_context;
    return 1;
}
