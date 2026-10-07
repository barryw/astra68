#include <astra/vfs_cdfs_backend.h>
#include <astra/utf8.h>
#include <odfs/ancestry.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct AstraCdfsFile {
    AstraCdfsFile *next;
    odfs_node_t node;
};

static uint32_t status_of(odfs_err_t error)
{
    switch (error) {
    case ODFS_OK: return ASTRA_VFS_OK;
    case ODFS_ERR_NOMEM: return ASTRA_VFS_ERR_NO_SPACE;
    case ODFS_ERR_NOT_FOUND: return ASTRA_VFS_ERR_NOT_FOUND;
    case ODFS_ERR_NOT_DIR: return ASTRA_VFS_ERR_NOT_DIR;
    case ODFS_ERR_IS_DIR: return ASTRA_VFS_ERR_IS_DIR;
    case ODFS_ERR_IS_SYMLINK: return ASTRA_VFS_ERR_LOOP;
    case ODFS_ERR_READ_ONLY: return ASTRA_VFS_ERR_READ_ONLY;
    case ODFS_ERR_UNSUPPORTED: return ASTRA_VFS_ERR_UNSUPPORTED;
    case ODFS_ERR_MEDIA_CHANGED: case ODFS_ERR_NO_MEDIA:
        return ASTRA_VFS_ERR_PEER;
    case ODFS_ERR_LOOP: return ASTRA_VFS_ERR_LOOP;
    case ODFS_ERR_INVAL: case ODFS_ERR_RANGE: case ODFS_ERR_OVERFLOW:
        return ASTRA_VFS_ERR_INVALID;
    case ODFS_ERR_NAME_TOO_LONG: return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    default: return ASTRA_VFS_ERR_IO;
    }
}

static uint32_t enter(AstraVfsCdfsBackend *fs)
{
    uint32_t status;
    if (fs == NULL) return ASTRA_VFS_ERR_INVALID;
    if (fs->lock != NULL && !fs->lock(fs->lock_context))
        return ASTRA_VFS_ERR_IO;
    status = fs->mounted ? astra_cdfs_media_check(fs->source) :
                          ASTRA_VFS_ERR_PEER;
    if (status != ASTRA_VFS_OK && fs->unlock != NULL)
        fs->unlock(fs->lock_context);
    return status;
}

static void leave(AstraVfsCdfsBackend *fs)
{
    if (fs->unlock != NULL) fs->unlock(fs->lock_context);
}

static AstraCdfsFile *file_of(AstraVfsCdfsBackend *fs, uintptr_t handle)
{
    /* ponytail: linear open-handle lookup; index tokens if fan-out makes it hot. */
    for (AstraCdfsFile *file = fs->files; file != NULL; file = file->next)
        if ((uintptr_t)file == handle) return file;
    return NULL;
}

/* Astra's common calendar converts epoch to civil, and ODFS supplies no
 * inverse. Keep this Gregorian transform local; unknown dates remain zero. */
static int64_t timestamp(const odfs_timestamp_t *t)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int32_t year = t->year;
    int32_t month = t->month;
    int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    int32_t era;
    int32_t yoe;
    int64_t date;

    if (year < 1 || year > 9999 || month < 1 || month > 12 ||
        t->day < 1u || t->day > days[month - 1] + (month == 2 && leap) ||
        t->hour > 23u || t->minute > 59u || t->second > 60u)
        return 0;
    year -= month <= 2;
    era = year / 400;
    yoe = year - era * 400;
    date = (int64_t)era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 +
           (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + t->day - 1 -
           719468;
    return date * 86400 + t->hour * 3600 + t->minute * 60 + t->second -
           (t->tz_offset == INT16_MIN ? 0 : (int32_t)t->tz_offset * 60);
}

static void info_of(const odfs_node_t *node, AstraVfsNodeInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = node->size;
    info->mtime = timestamp(&node->mtime);
    info->mode = (uint16_t)node->mode;
    info->kind = node->kind == ODFS_NODE_DIR ? ASTRA_VFS_KIND_DIRECTORY :
        node->kind == ODFS_NODE_SYMLINK ? ASTRA_VFS_KIND_SYMLINK :
                                        ASTRA_VFS_KIND_FILE;
}

/* The upstream host resolver lacks dot/parent/trailing-slash semantics and
 * open_at. Use its component lookup and parent resolver, leaving all symlink
 * traversal to Astra's shared assign layer. */
static uint32_t walk(AstraVfsCdfsBackend *fs, uintptr_t directory,
                     const char *path, odfs_node_t *node,
                     odfs_node_t *parent, char *name)
{
    AstraCdfsFile *base;
    size_t length;
    uint32_t status;

    if (path == NULL || (length = strlen(path)) >= ASTRA_VFS_PATH_MAX ||
        !astra_utf8_validate(path, (uint32_t)length, 0u))
        return ASTRA_VFS_ERR_INVALID;
    if (directory != 0u) {
        base = file_of(fs, directory);
        if (base == NULL) return ASTRA_VFS_ERR_BAD_HANDLE;
        if (base->node.kind != ODFS_NODE_DIR) return ASTRA_VFS_ERR_NOT_DIR;
        if (*path == '/') return ASTRA_VFS_ERR_INVALID;
        *node = base->node;
    } else {
        *node = fs->mount.root;
    }
    if (name != NULL) *name = '\0';
    if (parent != NULL) *parent = *node;
    while (*path == '/') ++path;
    while (*path != '\0') {
        char component[ASTRA_VFS_NAME_MAX];
        const char *end = path;
        int separator;

        if (node->kind != ODFS_NODE_DIR) return ASTRA_VFS_ERR_NOT_DIR;
        while (*end != '\0' && *end != '/') ++end;
        length = (size_t)(end - path);
        if (length >= sizeof(component)) return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        memcpy(component, path, length);
        component[length] = '\0';
        if (parent != NULL) *parent = *node;
        if (name != NULL) memcpy(name, component, length + 1u);
        if (strcmp(component, "..") == 0) {
            odfs_node_t up;
            odfs_err_t error = odfs_resolve_parent_node(&fs->mount, node, &up, NULL);
            if (error == ODFS_OK) *node = up;
            else if (error != ODFS_ERR_NOT_FOUND ||
                     !odfs_node_matches_identity(node, &fs->mount.root))
                return status_of(error);
        } else if (strcmp(component, ".") != 0) {
            odfs_node_t child;
            status = status_of(odfs_lookup(&fs->mount, node, component, &child));
            if (status != ASTRA_VFS_OK) return status;
            *node = child;
        }
        separator = *end == '/';
        while (*end == '/') ++end;
        if (separator && node->kind != ODFS_NODE_DIR)
            return ASTRA_VFS_ERR_NOT_DIR;
        path = end;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_cdfs_mount(AstraVfsCdfsBackend *fs,
    AstraCdfsMedia *source, const odfs_mount_opts_t *options,
    AstraCdfsLock lock, AstraCdfsUnlock unlock, void *lock_context)
{
    uint32_t status;
    if (fs == NULL || source == NULL || source->media.ops == NULL ||
        (lock == NULL) != (unlock == NULL)) return ASTRA_VFS_ERR_INVALID;
    memset(fs, 0, sizeof(*fs));
    fs->source = source;
    fs->lock = lock;
    fs->unlock = unlock;
    fs->lock_context = lock_context;
    status = astra_cdfs_media_check(source);
    if (status != ASTRA_VFS_OK) return status;
    status = status_of(odfs_mount(&source->media, options, NULL, &fs->mount));
    if (status != ASTRA_VFS_OK) {
        odfs_unmount(&fs->mount);
        return status;
    }
    fs->mounted = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_cdfs_unmount(void *context)
{
    AstraVfsCdfsBackend *fs = context;
    uint32_t status;
    if (fs == NULL) return ASTRA_VFS_ERR_INVALID;
    if (fs->lock != NULL && !fs->lock(fs->lock_context)) return ASTRA_VFS_ERR_IO;
    status = fs->files != NULL ? ASTRA_VFS_ERR_BUSY : ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK && fs->mounted) {
        odfs_unmount(&fs->mount);
        fs->mounted = 0u;
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_open_at(void *context, uintptr_t directory,
    const char *path, uint32_t flags, uint16_t mode,
    uintptr_t *handle, AstraVfsNodeInfo *info)
{
    AstraVfsCdfsBackend *fs = context;
    AstraCdfsFile *file;
    odfs_node_t node;
    uint32_t status;
    (void)mode;
    if (handle == NULL || info == NULL) return ASTRA_VFS_ERR_INVALID;
    *handle = 0u;
    if (flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                 ASTRA_VFS_OPEN_TRUNCATE | ASTRA_VFS_OPEN_APPEND))
        return ASTRA_VFS_ERR_READ_ONLY;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    status = walk(fs, directory, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK && node.kind == ODFS_NODE_SYMLINK)
        status = ASTRA_VFS_ERR_LOOP;
    if (status == ASTRA_VFS_OK &&
        ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u) != (node.kind == ODFS_NODE_DIR))
        status = node.kind == ODFS_NODE_DIR ? ASTRA_VFS_ERR_IS_DIR : ASTRA_VFS_ERR_NOT_DIR;
    if (status == ASTRA_VFS_OK) {
        file = malloc(sizeof(*file));
        if (file == NULL) status = ASTRA_VFS_ERR_NO_SPACE;
        else {
            file->node = node;
            file->next = fs->files;
            fs->files = file;
            *handle = (uintptr_t)file;
            info_of(&node, info);
        }
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_open(void *context, const char *path, uint32_t flags,
    uint16_t mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    return cdfs_open_at(context, 0u, path, flags, mode, node, info);
}

static uint32_t cdfs_close(void *context, uintptr_t node)
{
    AstraVfsCdfsBackend *fs = context;
    AstraCdfsFile **at;
    uint32_t status = ASTRA_VFS_ERR_BAD_HANDLE;
    /* Closing must work even after media removal. */
    if (fs == NULL) return ASTRA_VFS_ERR_INVALID;
    if (fs->lock != NULL && !fs->lock(fs->lock_context)) return ASTRA_VFS_ERR_IO;
    for (at = &fs->files; *at != NULL; at = &(*at)->next) {
        if ((uintptr_t)*at == node) {
            AstraCdfsFile *file = *at;
            *at = file->next;
            free(file);
            status = ASTRA_VFS_OK;
            break;
        }
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_read(void *context, uintptr_t handle, uint64_t offset,
    void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsCdfsBackend *fs = context;
    AstraCdfsFile *file;
    size_t count = length;
    uint32_t status;
    if (moved == NULL || (length != 0u && buffer == NULL)) return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    file = file_of(fs, handle);
    if (file == NULL) status = ASTRA_VFS_ERR_BAD_HANDLE;
    else if (file->node.kind == ODFS_NODE_DIR) status = ASTRA_VFS_ERR_IS_DIR;
    else if (length != 0u && offset < file->node.size) {
        status = status_of(odfs_read(&fs->mount, &file->node, offset, buffer, &count));
        if (status == ASTRA_VFS_OK && count <= length) *moved = (uint32_t)count;
        else if (status == ASTRA_VFS_OK) status = ASTRA_VFS_ERR_IO;
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_stat_at(void *context, uintptr_t directory,
    const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsCdfsBackend *fs = context;
    odfs_node_t node;
    uint32_t status;
    if (info == NULL) return ASTRA_VFS_ERR_INVALID;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    status = walk(fs, directory, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK) info_of(&node, info);
    leave(fs);
    return status;
}

static uint32_t cdfs_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    return cdfs_stat_at(context, 0u, path, info);
}

static uint32_t cdfs_stat_node(void *context, uintptr_t node, AstraVfsNodeInfo *info)
{
    AstraVfsCdfsBackend *fs = context;
    AstraCdfsFile *file;
    uint32_t status;
    if (info == NULL) return ASTRA_VFS_ERR_INVALID;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    file = file_of(fs, node);
    if (file == NULL) status = ASTRA_VFS_ERR_BAD_HANDLE;
    else info_of(&file->node, info);
    leave(fs);
    return status;
}

typedef struct CdfsEntry {
    char *name;
    uint32_t capacity;
    AstraVfsNodeInfo *info;
    uint32_t status;
    int found;
} CdfsEntry;

static odfs_err_t entry_of(const odfs_node_t *node, void *context)
{
    CdfsEntry *entry = context;
    size_t length = strlen(node->name);
    if (strcmp(node->name, ".") == 0 || strcmp(node->name, "..") == 0)
        return ODFS_OK;
    if (length == 0u || !astra_utf8_validate(node->name, (uint32_t)length, 0u))
        entry->status = ASTRA_VFS_ERR_IO;
    else if (length >= entry->capacity || length >= ASTRA_VFS_NAME_MAX)
        entry->status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    else {
        memcpy(entry->name, node->name, length + 1u);
        info_of(node, entry->info);
        entry->found = 1;
    }
    return ODFS_ERR_EOF;
}

static uint32_t cdfs_readdir(void *context, uintptr_t directory,
    const char *path, uint64_t cookie, char *name, uint32_t capacity,
    AstraVfsNodeInfo *info, uint64_t *next)
{
    AstraVfsCdfsBackend *fs = context;
    odfs_node_t node;
    uint32_t cursor = (uint32_t)cookie;
    uint32_t status;
    CdfsEntry entry = {name, capacity, info, ASTRA_VFS_OK, 0};
    if (name == NULL || info == NULL || next == NULL || cookie > UINT32_MAX)
        return ASTRA_VFS_ERR_INVALID;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    if (directory != 0u) {
        AstraCdfsFile *file = file_of(fs, directory);
        if (file == NULL) status = ASTRA_VFS_ERR_BAD_HANDLE;
        else node = file->node;
    } else status = walk(fs, 0u, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK) {
        odfs_err_t error = odfs_readdir(&fs->mount, &node, entry_of, &entry, &cursor);
        if (error != ODFS_OK && error != ODFS_ERR_EOF) status = status_of(error);
        else if (entry.status != ASTRA_VFS_OK) status = entry.status;
        else if (!entry.found) status = ASTRA_VFS_ERR_NOT_FOUND;
        else if (cursor == cookie) status = ASTRA_VFS_ERR_IO;
        else *next = cursor;
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_readlink(void *context, const char *path, void *buffer,
    uint32_t capacity, uint32_t *length)
{
    AstraVfsCdfsBackend *fs = context;
    odfs_node_t node;
    odfs_node_t parent;
    char name[ASTRA_VFS_NAME_MAX];
    char target[ASTRA_VFS_PATH_MAX];
    uint32_t status;
    if (length == NULL || (capacity != 0u && buffer == NULL)) return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    status = walk(fs, 0u, path, &node, &parent, name);
    if (status == ASTRA_VFS_OK && node.kind != ODFS_NODE_SYMLINK)
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK)
        status = status_of(odfs_readlink(&fs->mount, &parent, name, target, sizeof(target)));
    if (status == ASTRA_VFS_OK) {
        *length = (uint32_t)strlen(target);
        if (!astra_utf8_validate(target, *length, 0u)) status = ASTRA_VFS_ERR_IO;
        else if (*length > capacity) status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        else if (*length != 0u) memcpy(buffer, target, *length);
    }
    leave(fs);
    return status;
}

static uint32_t cdfs_filesystem_info(void *context, uintptr_t handle,
    const char *path, AstraVfsFilesystemInfo *info)
{
    AstraVfsCdfsBackend *fs = context;
    odfs_node_t node;
    uint32_t status;
    if (info == NULL) return ASTRA_VFS_ERR_INVALID;
    status = enter(fs);
    if (status != ASTRA_VFS_OK) return status;
    if (handle != 0u) status = file_of(fs, handle) != NULL ? ASTRA_VFS_OK : ASTRA_VFS_ERR_BAD_HANDLE;
    else status = walk(fs, 0u, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK) {
        memset(info, 0, sizeof(*info));
        info->size = sizeof(*info);
        info->flags = ASTRA_VFS_FILESYSTEM_READ_ONLY;
        info->block_size = ASTRA_CDFS_SECTOR_BYTES;
        info->fragment_size = ASTRA_CDFS_SECTOR_BYTES;
        info->blocks = fs->mount.total_blocks;
        info->name_max = ASTRA_VFS_NAME_MAX - 1u;
    }
    leave(fs);
    return status;
}

static uint32_t ro_write(void *c, uintptr_t n, uint64_t o, uint32_t f,
    const void *b, uint32_t l, uint32_t *m, uint64_t *p)
{ (void)astra_vfs_backend_deny_write(c,n,o,f,b,l,m,p); return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_sync(void *c, uintptr_t n)
{ AstraVfsNodeInfo i; return cdfs_stat_node(c,n,&i); }
static uint32_t ro_truncate(void *c, uintptr_t n, uint64_t s)
{ (void)c; (void)n; (void)s; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_mkdir(void *c, const char *p, uint16_t m)
{ (void)c; (void)p; (void)m; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_unlink(void *c, const char *p)
{ (void)c; (void)p; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_pair(void *c, const char *a, const char *b)
{ (void)c; (void)a; (void)b; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_unlink_at(void *c, uintptr_t n, const char *p, uint32_t f)
{ (void)c; (void)n; (void)p; (void)f; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_chmod_node(void *c, uintptr_t n, uint16_t m)
{ (void)c; (void)n; (void)m; return ASTRA_VFS_ERR_READ_ONLY; }
static uint32_t ro_chmod_at(void *c, uintptr_t n, const char *p, uint16_t m, uint32_t f)
{ (void)c; (void)n; (void)p; (void)m; (void)f; return ASTRA_VFS_ERR_READ_ONLY; }

static const AstraVfsBackendOps cdfs_ops = {
    .open = cdfs_open, .close = cdfs_close, .read = cdfs_read,
    .write = ro_write, .sync = ro_sync, .truncate = ro_truncate,
    .stat = cdfs_stat, .readdir = cdfs_readdir,
    .mkdir = ro_mkdir, .unlink = ro_unlink, .rename = ro_pair,
    .chmod = ro_mkdir, .readlink = cdfs_readlink, .symlink = ro_pair,
    .link = ro_pair, .open_at = cdfs_open_at, .unlink_at = ro_unlink_at,
    .chmod_node = ro_chmod_node, .chmod_at = ro_chmod_at,
    .filesystem_info = cdfs_filesystem_info,
    .stat_at = cdfs_stat_at, .stat_node = cdfs_stat_node
};

const AstraVfsBackendOps *astra_vfs_cdfs_ops(void) { return &cdfs_ops; }
