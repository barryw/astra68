/* Private test seam: local core fixtures use real, rooted native clients.
 * Provider nodes and scopes must not be exposed merely to run these probes. */

static void probe_node_info(AstraVfsNodeInfo *info, const AstraVfsDirEntry *entry)
{
    *info = (AstraVfsNodeInfo){.size = entry->size, .mtime = entry->mtime,
        .uid = entry->uid, .gid = entry->gid, .kind = entry->kind,
        .mode = entry->mode, .nlink = entry->nlink, .meta = entry->meta};
}

static uint32_t probe_stat_file(void *client, uintptr_t node, AstraVfsNodeInfo *info)
{
    AstraVfsDirEntry entry = {0};
    uint32_t status = astra_vfs_stat_file_meta(client, (AstraVfsFile)node, &entry);
    if (status == ASTRA_VFS_OK) probe_node_info(info, &entry);
    return status;
}

static uint32_t probe_stat(void *client, const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsDirEntry entry = {0};
    uint32_t status = astra_vfs_stat_meta(client, path, &entry);
    if (status == ASTRA_VFS_OK) probe_node_info(info, &entry);
    return status;
}

static uint32_t probe_open(void *client, const char *path, uint32_t flags,
    uint16_t mode, uintptr_t *node, AstraVfsNodeInfo *info)
{
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint32_t status = astra_vfs_open_mode(client, path, flags, mode, &file, NULL, NULL);
    if (status != ASTRA_VFS_OK) return status;
    status = probe_stat_file(client, file, info);
    if (status != ASTRA_VFS_OK) (void)astra_vfs_close(client, file);
    else *node = file;
    return status;
}

static uint32_t probe_close(void *client, uintptr_t node)
{ return astra_vfs_close(client, (AstraVfsFile)node); }
static uint32_t probe_read(void *client, uintptr_t node, uint64_t offset,
    void *bytes, uint32_t length, uint32_t *moved)
{ return astra_vfs_read(client, (AstraVfsFile)node, offset, bytes, length, moved); }
static uint32_t probe_read_sequential(void *client, uintptr_t node,
    void *bytes, uint32_t length, uint32_t *moved, uint64_t *position)
{ return astra_vfs_read_position(client, (AstraVfsFile)node, 0u,
    ASTRA_VFS_IO_SEQUENTIAL, bytes, length, moved, position); }
static uint32_t probe_write(void *client, uintptr_t node, uint64_t offset,
    uint32_t flags, uint64_t limit, const void *bytes, uint32_t length,
    uint32_t *moved, uint64_t *position)
{ return astra_vfs_write_position(client, (AstraVfsFile)node, offset,
    flags, limit, bytes, length, moved, position); }
static uint32_t probe_sync(void *client, uintptr_t node)
{ return astra_vfs_sync(client, (AstraVfsFile)node); }
static uint32_t probe_set_file_times(void *client, uintptr_t node,
    const AstraVfsNodeInfo *times, uint32_t fields)
{
    AstraVfsDirEntry entry = {.mtime = times->mtime, .meta = times->meta};
    return astra_vfs_set_file_times(client, (AstraVfsFile)node, &entry, fields);
}
static uint32_t probe_truncate(void *client, uintptr_t node, uint64_t size)
{ return astra_vfs_truncate(client, (AstraVfsFile)node, size); }
static uint32_t probe_seek(void *client, uintptr_t node, int64_t delta,
    uint32_t whence, uint64_t *position)
{ return astra_vfs_seek(client, (AstraVfsFile)node, delta, whence, position); }
static uint32_t probe_flags(void *client, uintptr_t node, uint32_t mask,
    uint32_t flags, uint32_t *result)
{ return astra_vfs_file_flags(client, (AstraVfsFile)node, mask, flags, result); }
static uint32_t probe_rename_at(void *client, uintptr_t from, const char *left,
    uintptr_t to, const char *right)
{ return astra_vfs_rename_at(client, (AstraVfsFile)from, left, (AstraVfsFile)to, right); }
static uint32_t probe_share(void *client, uintptr_t node, uint64_t *token)
{ return astra_vfs_share(client, (AstraVfsFile)node, token); }
static uint32_t probe_adopt(void *client, uint64_t token, uint32_t flags, uintptr_t *node)
{
    if (flags == ASTRA_VFS_ADOPT_DISCARD) return astra_vfs_unshare(client, token);
    if (flags != 0u || node == NULL) return ASTRA_VFS_ERR_INVALID;
    AstraVfsFile file;
    uint32_t status = astra_vfs_adopt(client, token, &file);
    if (status == ASTRA_VFS_OK) *node = file;
    return status;
}

static uint32_t probe_advance(void *context, uintptr_t node, uint64_t expected,
    uint32_t length, uint64_t *position, uint32_t *advanced)
{
    AstraVfsClient *client = context;
    AstraVfsRequest request = {.size = ASTRA_VFS_REQUEST_SIZE, .version = client->version,
        .session = client->session, .file = (AstraVfsFile)node,
        .offset = expected, .length = length};
    AstraVfsReply reply = {0};
    uint32_t status = client->transport(client->context, ASTRA_VFS_OP_POSITION_ADVANCE,
        &request, NULL, &reply);
    if (status == ASTRA_VFS_OK) status = reply.status;
    if (status == ASTRA_VFS_OK) { *position = reply.position; *advanced = reply.count; }
    return status;
}

static uint32_t probe_readdir(void *client, uintptr_t directory, const char *path,
    uint64_t cookie, char *name, uint32_t capacity, AstraVfsNodeInfo *info, uint64_t *next)
{
    AstraVfsDirEntry entry = {0};
    uint32_t count = 0u;
    uint32_t status = directory == 0u ?
        astra_vfs_readdir_batch(client, path, cookie, &entry, 1u, &count, next) :
        astra_vfs_readdir_file_batch(client, (AstraVfsFile)directory, path, cookie,
            &entry, 1u, &count, next);
    if (status != ASTRA_VFS_OK) return status;
    if (count == 0u) return ASTRA_VFS_ERR_NOT_FOUND;
    size_t length = strlen(entry.name);
    if (length >= capacity) return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(name, entry.name, length + 1u);
    probe_node_info(info, &entry);
    return ASTRA_VFS_OK;
}

static uint32_t probe_lookup(void *client, uintptr_t directory, const char *path,
    uint32_t flags, uint32_t links_left, AstraVfsNodeInfo *info, AstraVfsEscape *escape)
{
    AstraVfsDirEntry entry = {0};
    uint32_t status = astra_vfs_lookup(client, (AstraVfsFile)directory, path,
        flags, links_left, &entry, escape);
    if (status == ASTRA_VFS_OK) probe_node_info(info, &entry);
    return status;
}

static const AstraVfsBackendOps probe_backend_ops = {
    .open = probe_open, .close = probe_close, .read = probe_read,
    .read_sequential = probe_read_sequential, .write = probe_write,
    .sync = probe_sync, .set_file_times = probe_set_file_times,
    .truncate = probe_truncate, .stat = probe_stat,
    .stat_node = probe_stat_file, .readdir = probe_readdir,
    .lookup = probe_lookup,
    .seek = probe_seek, .file_flags = probe_flags, .position_advance = probe_advance,
    .share = probe_share, .adopt = probe_adopt,
    .rename_at = probe_rename_at,
    .mkdir = astra_vfs_backend_deny_mkdir, .unlink = astra_vfs_backend_deny_unlink,
    .rename = astra_vfs_backend_deny_rename, .chmod = astra_vfs_backend_deny_chmod,
    .readlink = astra_vfs_backend_no_readlink, .symlink = astra_vfs_backend_deny_symlink,
    .link = astra_vfs_backend_deny_link, .open_at = astra_vfs_backend_no_open_at,
    .unlink_at = astra_vfs_backend_no_unlink_at, .chmod_node = astra_vfs_backend_no_chmod_node,
    .chmod_at = astra_vfs_backend_no_chmod_at, .filesystem_info = astra_vfs_backend_no_filesystem_info,
    .stat_at = astra_vfs_backend_no_stat_at,
};
