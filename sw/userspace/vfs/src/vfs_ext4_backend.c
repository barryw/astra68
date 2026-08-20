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

/*
 * lwext4's mode strings, chosen from the protocol's open flags.
 *
 * The protocol has four states and lwext4 has three modes, so one of ours has
 * no single mode: **create without truncate** -- what `>>` opens with, and what
 * anything that adds to a file opens with. `"wb"` creates and truncates, and
 * `"r+b"` keeps but refuses an absent name, so that state is two attempts
 * rather than one mode, which is why this returns a first choice and a
 * fallback rather than a string.
 *
 * It used to return `"wb"` for it and say so in a comment. Every append on the
 * machine silently truncated, and the file that came back was the right length
 * for the last write and the wrong length for the file -- which reads like a
 * short write and not like an open flag.
 */
static const char *
mode_of(uint32_t flags)
{
    if ((flags & ASTRA_VFS_OPEN_WRITE) == 0u) {
        return "rb";
    }
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) {
        return "wb";
    }
    /* Keeps what is there; the caller falls back to "wb" when it is absent. */
    return "r+b";
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
fill_metadata(const char *full, AstraVfsNodeInfo *info, int with_size)
{
    uint32_t mode = 0u;
    uint32_t uid = 0u;
    uint32_t gid = 0u;
    uint32_t mtime = 0u;
    uint32_t nlink = 0u;
    uint64_t size = 0u;

    if (ext4_meta_get(full, &mode, &uid, &gid, &mtime, &nlink,
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
ext4_backend_open(void *context, const char *path, uint32_t flags,
                  uintptr_t *node, AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    uint32_t index;
    int rc;

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        close_scan(backend);

    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u) {
        /*
         * A directory handle exists only so the core can answer "is this a
         * directory" and refuse reads against it. lwext4's directory cursor is
         * not this handle: readdir owns its own resumable scan below.
         */
        ext4_dir directory;

        rc = ext4_dir_open(&directory, full);
        if (rc != EOK) {
            return status_of(rc);
        }
        (void)ext4_dir_close(&directory);
        *node = 0u;
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        fill_metadata(full, info, 0);
        return ASTRA_VFS_OK;
    }

    for (index = 0u; index < ASTRA_VFS_EXT4_FILE_MAX; ++index) {
        if (!backend->open_files[index].used) {
            break;
        }
    }
    if (index == ASTRA_VFS_EXT4_FILE_MAX) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    rc = ext4_fopen(&backend->open_files[index].file, full, mode_of(flags));
    /*
     * Create without truncate, on a name that is not there yet. Only that
     * one refusal: a mode that failed for any other reason has said something
     * about the file, and retrying with the mode that truncates would answer
     * it by destroying what it was refusing to open.
     */
    if (rc == ENOENT && (flags & ASTRA_VFS_OPEN_CREATE) != 0u &&
        (flags & ASTRA_VFS_OPEN_TRUNCATE) == 0u &&
        (flags & ASTRA_VFS_OPEN_WRITE) != 0u) {
        rc = ext4_fopen(&backend->open_files[index].file, full, "wb");
    }
    if (rc != EOK) {
        return status_of(rc);
    }
    backend->open_files[index].used = 1;
    /*
     * The node is the slot index plus one, never a pointer. The core stores it
     * opaquely and hands it back, and a value that is only an index cannot be
     * turned into a wild pointer by a bug above this layer.
     */
    *node = (uintptr_t)(index + 1u);
    info->size = ext4_fsize(&backend->open_files[index].file);
    info->kind = ASTRA_VFS_KIND_FILE;
    fill_metadata(full, info, 0);
    return ASTRA_VFS_OK;
}

static ext4_file *
file_of(AstraVfsExt4Backend *backend, uintptr_t node)
{
    uint32_t index;

    if (node == 0u) {
        return NULL; /* a directory handle holds no lwext4 file */
    }
    index = (uint32_t)node - 1u;
    if (index >= ASTRA_VFS_EXT4_FILE_MAX || !backend->open_files[index].used) {
        return NULL;
    }
    return &backend->open_files[index].file;
}

static uint32_t
ext4_backend_close(void *context, uintptr_t node)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    ext4_file *file = file_of(backend, node);

    if (file == NULL) {
        return ASTRA_VFS_OK; /* directory handles have nothing to release */
    }
    (void)ext4_fclose(file);
    backend->open_files[(uint32_t)node - 1u].used = 0;
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
                   const void *buffer, uint32_t length, uint32_t *moved)
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
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    ext4_dir directory;
    ext4_file file;
    int rc;

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /* Directory first: opening one as a file succeeds on some backends. */
    rc = ext4_dir_open(&directory, full);
    if (rc == EOK) {
        (void)ext4_dir_close(&directory);
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        fill_metadata(full, info, 0);
        return ASTRA_VFS_OK;
    }
    rc = ext4_fopen(&file, full, "rb");
    if (rc != EOK) {
        return status_of(rc);
    }
    info->size = ext4_fsize(&file);
    info->kind = ASTRA_VFS_KIND_FILE;
    (void)ext4_fclose(&file);
    fill_metadata(full, info, 0);
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
ext4_backend_readdir(void *context, const char *path, uint64_t cookie,
                     char *name, uint32_t capacity, AstraVfsNodeInfo *info,
                     uint64_t *next)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];
    const ext4_direntry *entry;
    uint32_t copied;
    int rc;

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (!backend->scan_open || backend->scan_next != cookie ||
        !same_path(backend->scan_path, full)) {
        close_scan(backend);
        rc = ext4_dir_open(&backend->scan, full);
        if (rc != EOK)
            return status_of(rc);
        backend->scan_open = 1;
        remember_path(backend, full);
    }
    backend->scan.next_off = cookie;
    for (;;) {
        entry = ext4_dir_entry_next(&backend->scan);
        if (entry == NULL) {
            close_scan(backend);
            return ASTRA_VFS_ERR_NOT_FOUND; /* past the last entry */
        }
        if (entry->name_length != 0u) {
            break;
        }
    }
    if ((uint32_t)entry->name_length + 1u > capacity) {
        close_scan(backend);
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (copied = 0u; copied < entry->name_length; ++copied) {
        name[copied] = (char)entry->name[copied];
    }
    name[entry->name_length] = '\0';
    info->size = 0u;
    info->kind = entry->inode_type == EXT4_DE_DIR ?
        ASTRA_VFS_KIND_DIRECTORY : ASTRA_VFS_KIND_FILE;
    /*
     * The entry itself carries a name and a type and nothing else, so the
     * metadata comes from the inode behind it. One lookup per entry, in this
     * process -- the alternative is a stat per entry from the client, which is
     * a cross-process round trip each and three orders of magnitude worse.
     */
    {
        char child[ASTRA_VFS_EXT4_PATH_MAX];
        uint32_t at = 0u;

        while (at < sizeof(child) - 1u && full[at] != '\0') {
            child[at] = full[at];
            ++at;
        }
        if (at != 0u && child[at - 1u] != '/' && at < sizeof(child) - 1u)
            child[at++] = '/';
        for (uint32_t index = 0u;
             at < sizeof(child) - 1u && name[index] != '\0'; ++index)
            child[at++] = name[index];
        child[at] = '\0';
        fill_metadata(child, info, 1);
    }
    *next = backend->scan.next_off;
    backend->scan_next = *next;
    return ASTRA_VFS_OK;
}

static uint32_t
ext4_backend_mkdir(void *context, const char *path)
{
    AstraVfsExt4Backend *backend = backend_of(context);
    char full[ASTRA_VFS_EXT4_PATH_MAX];

    if (!build_path(backend, path, full, sizeof(full))) {
        return ASTRA_VFS_ERR_INVALID;
    }
    close_scan(backend);
    return status_of(ext4_dir_mk(full));
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
    close_scan(backend);
    rc = ext4_fremove(full);
    if (rc == EOK) {
        return ASTRA_VFS_OK;
    }
    /* A directory is removed by a different call; try that before failing. */
    if (rc == EISDIR || rc == EPERM || rc == EACCES) {
        return status_of(ext4_dir_rm(full));
    }
    return status_of(rc);
}

static const AstraVfsBackendOps ext4_ops = {
    ext4_backend_open,
    ext4_backend_close,
    ext4_backend_read,
    ext4_backend_write,
    ext4_backend_stat,
    ext4_backend_readdir,
    ext4_backend_mkdir,
    ext4_backend_unlink
};

const AstraVfsBackendOps *
astra_vfs_ext4_ops(void)
{
    return &ext4_ops;
}

int
astra_vfs_ext4_init(AstraVfsExt4Backend *backend, const char *mount_point)
{
    uint32_t index = 0u;

    if (backend == NULL || mount_point == NULL) {
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
    backend->scan_open = 0;
    backend->scan_next = 0u;
    for (index = 0u; index < ASTRA_VFS_EXT4_FILE_MAX; ++index) {
        backend->open_files[index].used = 0;
    }
    return 1;
}
