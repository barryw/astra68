#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <astra/filesystem_library.h>
#include <astra/vfs_port_transport.h>

static AstraVfsClient client;
static AstraVfsClient secondary_client;
static uint8_t contents[512];
static uint64_t content_size;
static uint64_t provider_position;
static uint32_t provider_directory_position;
static uint32_t provider_flags;
static uint32_t read_calls;
static uint32_t open_calls;
static uint32_t stat_calls;
static uint32_t readdir_file_calls;
static uint32_t readdir_file_mode;
static uint32_t last_readdir_capacity;
static uint32_t write_calls;
static uint32_t bulk_write_calls;
static uint32_t last_write_flags;
static uint32_t last_unlink_flags;
static uint32_t made_directory;
static uint32_t removed_file;
static uint32_t removed_link;
static uint32_t renamed_file;
static uint16_t last_create_mode;
static uint16_t last_chmod_mode;
static AstraVfsClient *last_open_client;
static char last_open_path[ASTRA_VFS_PATH_MAX];
static char last_rename_from[ASTRA_VFS_PATH_MAX];
static char last_rename_to[ASTRA_VFS_PATH_MAX];
static char last_symlink_target[ASTRA_VFS_PATH_MAX];
static char last_symlink_path[ASTRA_VFS_PATH_MAX];
static char last_link_from[ASTRA_VFS_PATH_MAX];
static char last_link_to[ASTRA_VFS_PATH_MAX];

static int same(const char *left, const char *right)
{
    return strcmp(left, right) == 0;
}

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

uint32_t astra_vfs_connect(AstraVfsClient *value, AstraVfsTransport transport,
                           void *context)
{
    value->transport = transport;
    value->context = context;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_client_connect_service(AstraVfsClient *value,
                                          uint32_t service)
{
    (void)value;
    return service == 7u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_disconnect(AstraVfsClient *value)
{
    (void)value;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_open(AstraVfsClient *value, const char *path,
                        uint32_t flags, AstraVfsFile *file, uint64_t *size,
                        uint16_t *kind)
{
    return astra_vfs_open_mode(value, path, flags, ASTRA_VFS_MODE_DEFAULT,
                               file, size, kind);
}

uint32_t astra_vfs_open_mode(AstraVfsClient *value, const char *path,
                             uint32_t flags, uint16_t create_mode,
                             AstraVfsFile *file, uint64_t *size,
                             uint16_t *kind)
{
    ++open_calls;
    last_create_mode = create_mode;
    last_open_client = value;
    strcpy(last_open_path, path);
    if (same(path, "/work/link") || same(path, "/work/dangling") ||
        same(path, "/work/dir-link/note"))
        return ASTRA_VFS_ERR_LOOP;
    if (same(path, "/work") &&
        (flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u) {
        *file = 2u;
        if (size != NULL)
            *size = 0u;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    if (same(path, "/work/note")) {
        if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
            (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE))
            return ASTRA_VFS_ERR_EXISTS;
        *file = 1u;
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    if (same(path, "/work/real-dir/note") ||
        (same(path, "/work/new-target") &&
         (flags & ASTRA_VFS_OPEN_CREATE) != 0u)) {
        *file = 1u;
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    if (value == &secondary_client && same(path, "/rom/tool")) {
        *file = 1u;
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_open_at(AstraVfsClient *value, AstraVfsFile directory,
                           const char *path, uint32_t flags,
                           AstraVfsFile *file, uint64_t *size,
                           uint16_t *kind)
{
    return astra_vfs_open_at_mode(value, directory, path, flags,
                                  ASTRA_VFS_MODE_DEFAULT, file, size, kind);
}

uint32_t astra_vfs_open_at_mode(AstraVfsClient *value,
                                AstraVfsFile directory, const char *path,
                                uint32_t flags, uint16_t create_mode,
                                AstraVfsFile *file, uint64_t *size,
                                uint16_t *kind)
{
    if (directory != 2u || path == NULL || path[0] == '\0')
        return ASTRA_VFS_ERR_NOT_DIR;
    if (same(path, "absolute-link") || same(path, "../work/note"))
        return ASTRA_VFS_ERR_ESCAPED;
    return astra_vfs_open_mode(value, "/work/note", flags, create_mode,
                               file, size, kind);
}

uint32_t astra_vfs_close(AstraVfsClient *value, AstraVfsFile file)
{
    (void)value;
    return file == 1u || file == 2u ? ASTRA_VFS_OK :
                                      ASTRA_VFS_ERR_BAD_HANDLE;
}

uint32_t astra_vfs_read(AstraVfsClient *value, AstraVfsFile file,
                        uint64_t offset, void *buffer, uint32_t length,
                        uint32_t *moved)
{
    uint32_t available;

    (void)value;
    ++read_calls;
    if (file != 1u || offset > content_size)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    available = (uint32_t)(content_size - offset);
    if (length > ASTRA_VFS_IO_MAX)
        length = ASTRA_VFS_IO_MAX;
    if (length > available)
        length = available;
    memcpy(buffer, contents + offset, length);
    *moved = length;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_write(AstraVfsClient *value, AstraVfsFile file,
                         uint64_t offset, uint64_t max_end, const void *buffer, uint32_t length,
                         uint32_t *moved)
{
    (void)value;
    ++write_calls;
    *moved = 0u;
    if (length != 0u && offset >= max_end)
        return ASTRA_VFS_ERR_FILE_TOO_LARGE;
    if (offset < max_end && length > max_end - offset)
        length = (uint32_t)(max_end - offset);
    if (file != 1u || offset > sizeof(contents) ||
        length > sizeof(contents) - offset)
        return ASTRA_VFS_ERR_NO_SPACE;
    memcpy(contents + offset, buffer, length);
    if (offset + length > content_size)
        content_size = offset + length;
    *moved = length;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_write_position(AstraVfsClient *value, AstraVfsFile file,
                                  uint64_t offset, uint32_t flags, uint64_t max_end,
                                  const void *buffer, uint32_t length,
                                  uint32_t *moved, uint64_t *position)
{
    last_write_flags = flags;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u)
        offset = provider_position;
    if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u ||
        ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
         (provider_flags & ASTRA_VFS_OPEN_APPEND) != 0u))
        offset = content_size;
    uint32_t status = astra_vfs_write(value, file, offset, max_end, buffer, length,
                                      moved);

    if (status == ASTRA_VFS_OK) {
        *position = offset + *moved;
        if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u)
            provider_position = *position;
    }
    return status;
}

static uint32_t bulk_write(AstraVfsClient *value, AstraVfsFile file,
                           uint64_t offset, uint32_t flags, uint64_t max_end,
                           const void *buffer, uint32_t length,
                           uint32_t *moved, uint64_t *position)
{
    ++bulk_write_calls;
    return astra_vfs_write_position(value, file, offset, flags, max_end, buffer,
                                     length, moved, position);
}

uint32_t astra_vfs_sync(AstraVfsClient *value, AstraVfsFile file)
{
    (void)value;
    return file == 1u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_BAD_HANDLE;
}

uint32_t astra_vfs_truncate(AstraVfsClient *value, AstraVfsFile file,
                            uint64_t size)
{
    (void)value;
    if (file != 1u || size > sizeof(contents))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    content_size = size;
    return ASTRA_VFS_OK;
}

/*
 * The metadata arm answers through the size/kind arm rather than repeating the
 * fixture, and stamps values a caller could not have guessed so a library that
 * dropped a field shows up as a zero rather than as a plausible default.
 */
uint32_t astra_vfs_lookup(AstraVfsClient *value, AstraVfsFile directory, const char *path,
                           uint32_t flags, uint32_t links_left,
                           AstraVfsDirEntry *meta, AstraVfsEscape *escape)
{
    (void)links_left;
    memset(escape, 0, sizeof(*escape));
    if (directory == 2u) {
        if (same(path, "absolute-link") || same(path, "../work/note")) {
            escape->kind = same(path, "absolute-link") ? ASTRA_VFS_ESCAPE_SYMLINK :
                                                       ASTRA_VFS_ESCAPE_PARENT;
            escape->links = same(path, "absolute-link") ? 1u : 0u;
            escape->consumed = (uint16_t)strlen(path);
            escape->length = sizeof("/work/note") - 1u;
            memcpy(escape->path, "/work/note", escape->length);
            return ASTRA_VFS_ERR_ESCAPED;
        }
        if (same(path, "a") || same(path, "b")) {
            *meta = (AstraVfsDirEntry){.kind = ASTRA_VFS_KIND_FILE};
            return ASTRA_VFS_OK;
        }
        uint32_t status = astra_vfs_stat_at_meta(value, directory, path,
            flags & ASTRA_VFS_LOOKUP_FOLLOW ? ASTRA_VFS_STAT_FOLLOW : 0u, meta);
        if (status == ASTRA_VFS_ERR_NOT_FOUND && (flags & ASTRA_VFS_LOOKUP_MISSING)) {
            *meta = (AstraVfsDirEntry){0};
            return ASTRA_VFS_OK;
        }
        return status;
    }
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_stat_meta(AstraVfsClient *value, const char *path,
                             AstraVfsDirEntry *meta)
{
    uint32_t status;

    if (meta == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *meta = (AstraVfsDirEntry){0};
    status = astra_vfs_stat(value, path, &meta->size, &meta->kind);
    if (status != ASTRA_VFS_OK)
        return status;
    meta->mode = 0644u;
    meta->uid = 501u;
    meta->gid = 20u;
    meta->nlink = 1u;
    meta->mtime = 1600000000;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_stat_file_meta(AstraVfsClient *value, AstraVfsFile file,
                                  AstraVfsDirEntry *meta)
{
    (void)value;
    if (meta == NULL || (file != 1u && file != 2u))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    *meta = (AstraVfsDirEntry){0};
    meta->size = file == 1u ? content_size : 0u;
    meta->kind = file == 1u ? ASTRA_VFS_KIND_FILE :
                             ASTRA_VFS_KIND_DIRECTORY;
    meta->mode = file == 1u ? 0600u : 0700u;
    meta->uid = 501u;
    meta->gid = 20u;
    meta->nlink = 1u;
    meta->mtime = 1600000000;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_stat_at_meta(AstraVfsClient *value, AstraVfsFile directory,
                                const char *path, uint32_t flags, AstraVfsDirEntry *meta)
{
    (void)flags;
    if (value != &client || directory != 2u || meta == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (same(path, "absolute-link") || same(path, "../work/note"))
        return ASTRA_VFS_ERR_ESCAPED;
    if (!same(path, "note"))
        return ASTRA_VFS_ERR_NOT_FOUND;
    *meta = (AstraVfsDirEntry){0};
    meta->size = content_size;
    meta->kind = ASTRA_VFS_KIND_FILE;
    meta->mode = 0755u;
    meta->uid = 501u;
    meta->gid = 20u;
    meta->nlink = 2u;
    meta->meta = (AstraVfsNodeMeta){ .valid = ASTRA_VFS_META_INODE |
        ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_MODE, .volume = 3u, .inode = 99u };
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_stat(AstraVfsClient *value, const char *path,
                        uint64_t *size, uint16_t *kind)
{
    static const char *const directories[] = {
        "/", "/apps", "/commands", "/config", "/home", "/libs",
        "/local", "/services", "/startup", "/services/events",
        "/tmp", "/trash"
    };

    ++stat_calls;
    for (size_t i = 0u;
         i < sizeof(directories) / sizeof(directories[0]); ++i)
        if (same(path, directories[i])) {
            if (size != NULL)
                *size = 0u;
            if (kind != NULL)
                *kind = ASTRA_VFS_KIND_DIRECTORY;
            return ASTRA_VFS_OK;
        }
    if (same(path, "/work/dir-link/note") ||
        same(path, "/work/dir-link/renamed"))
        return ASTRA_VFS_ERR_NOT_DIR;
    if (same(path, "/work")) {
        if (size != NULL)
            *size = 0u;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    if (value == &secondary_client && same(path, "/rom/tool")) {
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    if (same(path, "/work/note") && removed_file == 0u) {
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    if (value == &client &&
        (same(path, "/work/link") || same(path, "/work/loop-a") ||
         same(path, "/work/loop-b") || same(path, "/work/dangling") ||
         same(path, "/work/dir-link"))) {
        if (size != NULL)
            *size = 4u;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_SYMLINK;
        return ASTRA_VFS_OK;
    }
    if (same(path, "/work/real-dir")) {
        if (size != NULL)
            *size = 0u;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    if (same(path, "/work/real-dir/note")) {
        if (size != NULL)
            *size = content_size;
        if (kind != NULL)
            *kind = ASTRA_VFS_KIND_FILE;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_readdir_batch(AstraVfsClient *value, const char *path,
                                 uint64_t cursor, AstraVfsDirEntry *entries,
                                 uint32_t capacity, uint32_t *count,
                                 uint64_t *next)
{
    (void)value;
    if (!same(path, "/work") || cursor != 0u || capacity < 2u)
        return ASTRA_VFS_ERR_NOT_FOUND;
    strcpy(entries[0].name, "note");
    entries[0].kind = ASTRA_VFS_KIND_FILE;
    strcpy(entries[1].name, "tools");
    entries[1].kind = ASTRA_VFS_KIND_DIRECTORY;
    *count = 2u;
    *next = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_readdir_file_batch(
    AstraVfsClient *value, AstraVfsFile directory, const char *path,
    uint64_t cursor, AstraVfsDirEntry *entries, uint32_t capacity,
    uint32_t *count, uint64_t *next)
{
    if (directory != 2u)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    ++readdir_file_calls;
    last_readdir_capacity = capacity;
    if (path[0] == '\0' && readdir_file_mode != 0u) {
        if (cursor == ASTRA_VFS_CURSOR_CURRENT) {
            assert(value->features & ASTRA_VFS_FEATURE_DESCRIPTIONS);
            cursor = provider_directory_position;
            provider_directory_position = cursor == 0u ? 7u : 8u;
        }
        if (cursor == 0u) {
            strcpy(entries[0].name,
                   readdir_file_mode == 1u ? "terminal" : "first");
            entries[0].kind = ASTRA_VFS_KIND_FILE;
            *count = 1u;
            *next = readdir_file_mode == 1u ? 0u : 7u;
            return ASTRA_VFS_OK;
        }
        if (readdir_file_mode == 2u && cursor == 7u) {
            strcpy(entries[0].name, "last");
            entries[0].kind = ASTRA_VFS_KIND_FILE;
            *count = 1u;
            *next = 0u;
            return ASTRA_VFS_OK;
        }
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    return astra_vfs_readdir_batch(value, path, cursor, entries, capacity,
                                   count, next);
}

uint32_t astra_vfs_mkdir(AstraVfsClient *value, const char *path)
{
    return astra_vfs_mkdir_mode(value, path, ASTRA_VFS_MODE_DEFAULT);
}

uint32_t astra_vfs_mkdir_mode(AstraVfsClient *value, const char *path,
                              uint16_t create_mode)
{
    (void)value;
    last_create_mode = create_mode;
    made_directory = same(path, "/work/new");
    return made_directory != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_unlink(AstraVfsClient *value, const char *path, uint32_t flags)
{
    (void)value;
    assert((flags & ~ASTRA_VFS_UNLINK_DIRECTORY) == 0u);
    last_unlink_flags = flags;
    if (same(path, "/work/real-dir"))
        return flags == ASTRA_VFS_UNLINK_DIRECTORY ? ASTRA_VFS_OK :
                                                   ASTRA_VFS_ERR_IS_DIR;
    removed_file = same(path, "/work/note");
    removed_link = same(path, "/work/link");
    return removed_file != 0u || removed_link != 0u ? ASTRA_VFS_OK :
                                                      ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_unlink_at(AstraVfsClient *value, AstraVfsFile directory,
                             const char *path, uint32_t flags)
{
    (void)value;
    if (directory != 2u || !same(path, "note") || flags != 0u)
        return ASTRA_VFS_ERR_INVALID;
    ++removed_file;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_rename(AstraVfsClient *value, const char *from,
                          const char *to)
{
    (void)value;
    if (same(from, "/work/dir-link/note") ||
        same(to, "/work/dir-link/renamed"))
        return ASTRA_VFS_ERR_ESCAPED;
    snprintf(last_rename_from, sizeof(last_rename_from), "%s", from);
    snprintf(last_rename_to, sizeof(last_rename_to), "%s", to);
    renamed_file =
        (same(from, "/work/note") && same(to, "/work/renamed")) ||
        (same(from, "/work/real-dir/note") &&
         same(to, "/work/real-dir/renamed"));
    return renamed_file != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_chmod(AstraVfsClient *value, const char *path,
                         uint16_t mode)
{
    (void)value;
    last_chmod_mode = mode;
    return same(path, "/work/note") ? ASTRA_VFS_OK :
                                      ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_chmod_file(AstraVfsClient *value, AstraVfsFile file,
                              uint16_t mode)
{
    (void)value;
    if (file != 1u)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    last_chmod_mode = mode;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_chmod_at(AstraVfsClient *value, AstraVfsFile directory,
                            const char *path, uint16_t mode, uint32_t flags)
{
    (void)value;
    if (directory != 2u || !same(path, "note") || flags != 0u)
        return ASTRA_VFS_ERR_INVALID;
    last_chmod_mode = mode;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_info(AstraVfsFilesystemInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->block_size = 4096u;
    info->fragment_size = 4096u;
    info->blocks = 1024u;
    info->blocks_free = 512u;
    info->blocks_available = 500u;
    info->files = 100u;
    info->files_free = 80u;
    info->name_max = 63u;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_filesystem_info_path(AstraVfsClient *value,
                                        const char *path,
                                        AstraVfsFilesystemInfo *info)
{
    (void)value;
    return path == NULL ? ASTRA_VFS_ERR_INVALID : filesystem_info(info);
}

uint32_t astra_vfs_filesystem_info_file(AstraVfsClient *value,
                                        AstraVfsFile file,
                                        AstraVfsFilesystemInfo *info)
{
    (void)value;
    return file == ASTRA_VFS_FILE_INVALID ? ASTRA_VFS_ERR_BAD_HANDLE :
                                            filesystem_info(info);
}

uint32_t astra_vfs_readlink(AstraVfsClient *value, const char *path,
                            void *buffer, uint32_t capacity,
                            uint32_t *length)
{
    const char *target = same(path, "/work/loop-a") ? "loop-b" :
                         same(path, "/work/loop-b") ? "loop-a" :
                         same(path, "/work/dangling") ? "new-target" :
                         same(path, "/work/dir-link") ? "real-dir" : "note";

    if (value != &client ||
        (!same(path, "/work/link") && !same(path, "/work/loop-a") &&
         !same(path, "/work/loop-b") && !same(path, "/work/dangling") &&
         !same(path, "/work/dir-link")))
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (capacity < strlen(target))
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, target, strlen(target));
    *length = (uint32_t)strlen(target);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_symlink(AstraVfsClient *value, const char *target,
                           const char *path)
{
    if (value != &client)
        return ASTRA_VFS_ERR_INVALID;
    snprintf(last_symlink_target, sizeof(last_symlink_target), "%s", target);
    snprintf(last_symlink_path, sizeof(last_symlink_path), "%s", path);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_link(AstraVfsClient *value, const char *from,
                        const char *to)
{
    if (value != &client)
        return ASTRA_VFS_ERR_INVALID;
    snprintf(last_link_from, sizeof(last_link_from), "%s", from);
    snprintf(last_link_to, sizeof(last_link_to), "%s", to);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply)
{
    (void)context;
    (void)operation;
    (void)request;
    (void)reply;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_read_bulk_position(AstraVfsClient *value, AstraVfsFile file,
                                  uint64_t offset, uint32_t flags, void *buffer,
                                  uint32_t length, uint32_t *moved,
                                  uint64_t *position)
{
    uint32_t available;

    (void)value;
    assert((flags & ~ASTRA_VFS_IO_SEQUENTIAL) == 0u);
    if (flags & ASTRA_VFS_IO_SEQUENTIAL)
        offset = provider_position;
    ++read_calls;
    if (file != 1u || offset > content_size)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    available = (uint32_t)(content_size - offset);
    if (length > available)
        length = available;
    memcpy(buffer, contents + offset, length);
    *moved = length;
    *position = offset + length;
    if (flags & ASTRA_VFS_IO_SEQUENTIAL)
        provider_position = *position;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_read_position(AstraVfsClient *value, AstraVfsFile file,
                                  uint64_t offset, uint32_t flags,
                                  void *buffer, uint32_t length,
                                  uint32_t *moved, uint64_t *position)
{
    if (flags & ASTRA_VFS_IO_SEQUENTIAL)
        offset = provider_position;
    uint32_t status = astra_vfs_read(value, file, offset, buffer, length, moved);
    *position = offset + *moved;
    if (flags & ASTRA_VFS_IO_SEQUENTIAL)
        provider_position = *position;
    return status;
}

uint32_t astra_vfs_seek(AstraVfsClient *value, AstraVfsFile file,
                         int64_t delta, uint32_t origin, uint64_t *position)
{
    assert(value == &client && (file == 1u || file == 2u) &&
           value->features & ASTRA_VFS_FEATURE_DESCRIPTIONS);
    if (file == 2u) {
        assert(delta == 0 && origin == 0u);
        provider_directory_position = 0u;
        *position = 0u;
        return ASTRA_VFS_OK;
    }
    assert(delta >= 0 && origin <= 1u);
    provider_position = origin == 0u ? (uint64_t)delta :
        provider_position + (uint64_t)delta;
    *position = provider_position;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_file_flags(AstraVfsClient *value, AstraVfsFile file,
                               uint32_t mask, uint32_t flags,
                               uint32_t *result)
{
    assert(value == &client && file == 1u &&
           value->features & ASTRA_VFS_FEATURE_DESCRIPTIONS);
    assert((mask & ~ASTRA_VFS_FILE_STATUS_MASK) == 0u && (flags & ~mask) == 0u);
    provider_flags = (provider_flags & ~mask) | flags;
    *result = provider_flags;
    return ASTRA_VFS_OK;
}

static AstraVfsClient *client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    if (assign->handle == 7u)
        return &client;
    return assign->handle == 8u ? &secondary_client : NULL;
}

static void test_unlink_readonly_missing(void)
{
    AstraAssignTable assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;

    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "ROM", 8u, ASTRA_RIGHT_READ,
                             "rom") == ASTRA_VFS_OK);
    assert(astra_filesystem_attach(&filesystem, &assigns, client_for, NULL,
                                   NULL) == ASTRA_VFS_OK);
    assert(astra_filesystem_unlink(&filesystem, "/rom/missing", 0u) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_filesystem_unlink(&filesystem, "/rom/tool", 0u) ==
           ASTRA_VFS_ERR_ACCESS);
    assert(astra_filesystem_rename(&filesystem, "/rom/missing",
                                   "/rom/new") == ASTRA_VFS_ERR_NOT_FOUND);
    astra_filesystem_detach(&filesystem);
    astra_assign_table_destroy(&assigns);
}

static void test_protected_root_links(void)
{
    static const char *const names[] = {
        "SYSTEM", "APPS", "COMMANDS", "CONFIG", "CWD", "HOME", "LIBS",
        "LOCAL", "SERVICES", "STARTUP", "STORE", "TMP", "TRASH", "WORK"
    };
    static const char *const roots[] = {
        "", "apps", "commands", "config", "work", "home", "libs",
        "local", "services", "startup", "services/events", "tmp",
        "trash", "work"
    };
    static const char *const targets[] = {
        "/dh0", "/system/apps", "/system/commands", "/system/config",
        "/system/work", "/system/home", "/system/libs", "/system/local",
        "/system/services", "/system/startup", "/system/services/events",
        "/system/tmp", "/system/trash", "/system/work"
    };
    static const char *const volumes[] = {"EVENTS", "METRICS", "PROC"};
    AstraAssignTable assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[18];
    char path[ASTRA_VFS_PATH_MAX];
    char target[ASTRA_VFS_PATH_MAX];
    uint32_t count;
    uint32_t length;

    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "DH0", 7u, ASTRA_RIGHT_READ |
                             ASTRA_RIGHT_WRITE, "") == ASTRA_VFS_OK);
    for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        assert(astra_assign_bind(&assigns, names[i], 7u,
                                 ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                 roots[i]) == ASTRA_VFS_OK);
    for (uint32_t i = 0u; i < 3u; ++i)
        assert(astra_assign_bind(&assigns, volumes[i], 8u,
                                 ASTRA_RIGHT_READ, "") == ASTRA_VFS_OK);
    assert(astra_filesystem_attach(&filesystem, &assigns, client_for, NULL,
                                   NULL) == ASTRA_VFS_OK);
    assert(astra_filesystem_unlink(&filesystem, "/work/..", 0u) ==
           ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_unlink(&filesystem, "/work/.././",
                                   ASTRA_VFS_UNLINK_DIRECTORY) ==
           ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_directory_open(&filesystem, "/", &directory) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 18u, &count) ==
           ASTRA_VFS_OK && count == 18u);
    assert(same(entries[0].name, "dh0") &&
           entries[0].kind == ASTRA_VFS_KIND_DIRECTORY);
    for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        path[0] = '/';
        for (uint32_t j = 0u; j < strlen(names[i]); ++j)
            path[j + 1u] = (char)(names[i][j] + ('a' - 'A'));
        path[strlen(names[i]) + 1u] = '\0';
        assert(same(entries[i + 1u].name, path + 1) &&
               entries[i + 1u].kind == ASTRA_VFS_KIND_SYMLINK);
        info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
        assert(astra_filesystem_lstat(&filesystem, path, &info) ==
               ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_SYMLINK &&
               info.byte_size == strlen(targets[i]));
        info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
        assert(astra_filesystem_stat(&filesystem, path, &info) ==
               ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY);
        assert(astra_filesystem_readlink(&filesystem, path, target,
                                         sizeof(target), &length) ==
               ASTRA_VFS_OK && length == strlen(targets[i]) &&
               memcmp(target, targets[i], length) == 0);
        assert(astra_filesystem_readlink(&filesystem, path, target,
                                         length - 1u, &length) ==
               ASTRA_VFS_ERR_BUFFER_TOO_SMALL);
        assert(astra_filesystem_unlink(&filesystem, path, 0u) ==
               ASTRA_VFS_ERR_READ_ONLY);
        assert(astra_filesystem_rename(&filesystem, path,
                                       "/dh0/new") == ASTRA_VFS_ERR_READ_ONLY);
        assert(astra_filesystem_rename(&filesystem, "/dh0/old",
                                       path) == ASTRA_VFS_ERR_READ_ONLY);
        assert(astra_filesystem_symlink("/dh0", &filesystem, path) ==
               ASTRA_VFS_ERR_EXISTS);
    }
    for (uint32_t i = 0u; i < 3u; ++i)
        assert(entries[15u + i].kind == ASTRA_VFS_KIND_DIRECTORY);
    astra_filesystem_directory_close(&directory);
    astra_filesystem_detach(&filesystem);
    astra_assign_table_destroy(&assigns);
}

/* WORK served by another volume (hostfs) is that volume's root: a mount in
   `/`, not a link into /system. */
static void test_foreign_root_is_not_a_system_link(void)
{
    AstraAssignTable assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[4];
    char target[ASTRA_VFS_PATH_MAX];
    uint32_t count;
    uint32_t length;

    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "SYSTEM", 7u, ASTRA_RIGHT_READ, "") ==
           ASTRA_VFS_OK);
    assert(astra_assign_bind(&assigns, "HOME", 7u, ASTRA_RIGHT_READ,
                             "home") == ASTRA_VFS_OK);
    assert(astra_assign_bind(&assigns, "WORK", 8u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "") ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_attach(&filesystem, &assigns, client_for, NULL,
                                   NULL) == ASTRA_VFS_OK);
    assert(astra_filesystem_directory_open(&filesystem, "/", &directory) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 4u, &count) ==
           ASTRA_VFS_OK && count == 3u);
    assert(same(entries[1].name, "home") &&
           entries[1].kind == ASTRA_VFS_KIND_SYMLINK);
    assert(same(entries[2].name, "work") &&
           entries[2].kind == ASTRA_VFS_KIND_DIRECTORY);
    assert(astra_filesystem_lstat(&filesystem, "/work", &info) ==
               ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY);
    assert(astra_filesystem_readlink(&filesystem, "/work", target,
                                     sizeof(target), &length) !=
           ASTRA_VFS_OK);
    astra_filesystem_directory_close(&directory);
    astra_filesystem_detach(&filesystem);
    astra_assign_table_destroy(&assigns);
}

static void test_provider_position_is_the_authority(void)
{
    AstraFile first = ASTRA_FILE_INIT;
    AstraFile second;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint8_t bytes[512];
    uint32_t moved, flags;
    uint64_t position;

    client.features = ASTRA_VFS_FEATURE_DESCRIPTIONS;
    provider_position = 1u;
    provider_flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                     ASTRA_VFS_OPEN_APPEND;
    first._private_client = &client;
    first._private_file = 1u;
    first._private_flags = provider_flags;
    first._private_offset = 99u;
    first._private_size = content_size = 6u;
    memcpy(contents, "abcdef", 6u);
    second = first;
    assert(astra_filesystem_read(&first, bytes, 1u, &moved) == ASTRA_VFS_OK &&
           moved == 1u && bytes[0] == 'b');
    assert(astra_filesystem_seek(&second, 0, ASTRA_FILE_SEEK_CURRENT,
                                  &position) == ASTRA_VFS_OK && position == 2u);
    assert(astra_filesystem_read_at(&first, 0u, bytes, 1u, &moved) == ASTRA_VFS_OK &&
           bytes[0] == 'a' && provider_position == 2u);
    assert(astra_filesystem_write_at(&first, 0u, UINT64_MAX, "Z", 1u, &moved) == ASTRA_VFS_OK &&
           provider_position == 2u && contents[0] == 'Z');
    assert(astra_filesystem_write(&second, 7u, "XYZ", 3u, &moved) == ASTRA_VFS_OK &&
           moved == 1u && provider_position == 7u && content_size == 7u);
    assert(astra_filesystem_write(&first, 7u, "X", 1u, &moved) ==
               ASTRA_VFS_ERR_FILE_TOO_LARGE && moved == 0u && provider_position == 7u);
    assert(astra_filesystem_write(&first, 7u, NULL, 0u, &moved) == ASTRA_VFS_OK &&
           moved == 0u && provider_position == 7u && content_size == 7u);
    assert(astra_filesystem_write_at(&first, 7u, 7u, "X", 1u, &moved) ==
               ASTRA_VFS_ERR_FILE_TOO_LARGE && moved == 0u && provider_position == 7u);
    assert(astra_filesystem_file_flags(&second, ASTRA_VFS_FILE_STATUS_MASK,
           ASTRA_VFS_OPEN_NONBLOCK, &flags) == ASTRA_VFS_OK &&
           (flags & ASTRA_VFS_OPEN_APPEND) == 0u);
    assert(astra_filesystem_file_info(&first, &info) == ASTRA_VFS_OK &&
           info.offset == 7u && (info.open_flags & ASTRA_VFS_OPEN_NONBLOCK));
    assert(astra_filesystem_seek(&second, 1, ASTRA_FILE_SEEK_BEGIN,
                                  &position) == ASTRA_VFS_OK);
    assert(astra_filesystem_write(&first, UINT64_MAX, "Y", 1u, &moved) == ASTRA_VFS_OK &&
           provider_position == 2u && contents[1] == 'Y');
    memset(contents, 'R', sizeof(contents));
    content_size = sizeof(contents);
    provider_position = 0u;
    read_calls = 0u;
    assert(astra_filesystem_read(&first, bytes, sizeof(bytes), &moved) == ASTRA_VFS_OK &&
           moved == ASTRA_VFS_IO_MAX && read_calls == 1u &&
           provider_position == ASTRA_VFS_IO_MAX);
    first._private_read = astra_vfs_port_read_bulk_position;
    provider_position = 0u;
    read_calls = 0u;
    assert(astra_filesystem_read(&first, bytes, sizeof(bytes), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(bytes) && read_calls == 1u);
    client.features = 0u;
}

static void test_namespace_at_continuation(void)
{
    AstraAssignTable table = ASTRA_ASSIGN_TABLE_INIT;
    AstraFilesystem fs = ASTRA_FILESYSTEM_INIT;
    AstraFile directory = ASTRA_FILE_INIT;
    AstraFile opened = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;

    assert(astra_assign_bind(&table, "WORK", 7u,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "work") == ASTRA_VFS_OK);
    assert(astra_filesystem_attach(&fs, &table, client_for, NULL, NULL) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(&fs, "/work", ASTRA_VFS_OPEN_READ |
        ASTRA_VFS_OPEN_DIRECTORY, &directory) == ASTRA_VFS_OK);
    assert(astra_filesystem_open_at(&fs, &directory, "missing", UINT32_MAX,
        &opened) == ASTRA_VFS_ERR_INVALID);
    assert(astra_filesystem_open_at(&fs, &directory, "absolute-link", ASTRA_VFS_OPEN_READ,
        &opened) == ASTRA_VFS_OK);
    assert(astra_filesystem_close(&opened) == ASTRA_VFS_OK);
    assert(astra_filesystem_stat_at(&fs, &directory, "../work/note", ASTRA_VFS_STAT_FOLLOW,
        &info) == ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_FILE);
    assert(astra_filesystem_close(&directory) == ASTRA_VFS_OK);
    astra_filesystem_detach(&fs);
    astra_assign_table_destroy(&table);
}

int main(void)
{
    test_namespace_at_continuation();
    test_unlink_readonly_missing();
    test_protected_root_links();
    test_foreign_root_is_not_a_system_link();
    assert(ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR ==
           ASTRA_FILESYSTEM_LIBRARY_VERSION);
    AstraAssignTable assigns;
    AstraAssignTable single_assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFilesystem single_filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFile file = ASTRA_FILE_INIT;
    AstraFile child = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[33];
    uint8_t written[300];
    uint8_t read[300];
    char path[ASTRA_VFS_PATH_MAX];
    char relative[ASTRA_VFS_PATH_MAX];
    uint32_t moved;
    uint32_t count;
    uint64_t offset;
    uint32_t length;

    astra_assign_table_init(&single_assigns);
    assert(astra_assign_bind(&single_assigns, "WORK", 7u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "work") == ASTRA_VFS_OK);
    assert(astra_filesystem_attach(&single_filesystem, &single_assigns, client_for,
                           NULL, NULL) == ASTRA_VFS_OK);
    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_stat(&single_filesystem, "/", &info) ==
           ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY &&
           info.mode == 0555u);
    assert(astra_filesystem_directory_open(&single_filesystem, "/", &directory) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 1u, &count) ==
           ASTRA_VFS_OK && count == 1u && same(entries[0].name, "work") &&
           entries[0].kind == ASTRA_VFS_KIND_SYMLINK);
    assert(astra_filesystem_directory_read(&directory, entries, 1u, &count) ==
           ASTRA_VFS_OK && count == 0u);
    assert(astra_filesystem_directory_rewind(&directory) == ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 1u, &count) ==
           ASTRA_VFS_OK && count == 1u && same(entries[0].name, "work"));
    astra_filesystem_directory_close(&directory);
    assert(astra_filesystem_directory_read(&directory, entries, 1u, &count) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_filesystem_directory_open(&single_filesystem, "/missing",
                                           &directory) != ASTRA_VFS_OK);
    assert(astra_filesystem_mkdir(&single_filesystem, "/work") ==
           ASTRA_VFS_ERR_EXISTS);
    assert(astra_filesystem_mkdir(&single_filesystem, "/stuff") ==
           ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_open(&single_filesystem, "/stuff",
                                 ASTRA_VFS_OPEN_WRITE |
                                     ASTRA_VFS_OPEN_CREATE, &file) ==
           ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_unlink(&single_filesystem, "/work", 0u) ==
           ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_rename(&single_filesystem, "/work/note",
                                   "/stuff") == ASTRA_VFS_ERR_READ_ONLY);
    assert(astra_filesystem_symlink("/work", &single_filesystem, "/stuff") ==
           ASTRA_VFS_ERR_READ_ONLY);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "/work/note",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK);
    assert(stat_calls == 0u && open_calls == 1u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(&single_filesystem, "/work/note",
                         ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NONBLOCK,
                         &file) == ASTRA_VFS_OK);
    assert(file._private_flags & ASTRA_VFS_OPEN_NONBLOCK);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "/work/link",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK);
    assert(stat_calls != 0u && open_calls == 2u &&
           same(last_open_path, "/work/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "/work/note",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                             ASTRA_VFS_OPEN_EXCLUSIVE,
                         &file) == ASTRA_VFS_ERR_EXISTS);
    assert(stat_calls == 0u && open_calls == 1u);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "/work/new-target",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                         &file) == ASTRA_VFS_OK);
    assert(stat_calls == 0u && open_calls == 1u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    renamed_file = 0u;
    assert(astra_filesystem_rename(&single_filesystem, "/work/note", "/work/renamed") ==
           ASTRA_VFS_OK);
    assert(stat_calls == 0u && renamed_file != 0u);
    stat_calls = 0u;
    renamed_file = 0u;
    assert(astra_filesystem_rename(&single_filesystem, "/work/dir-link/note",
                           "/work/dir-link/renamed") == ASTRA_VFS_OK);
    assert(stat_calls != 0u && renamed_file != 0u &&
           same(last_rename_from, "/work/real-dir/note") &&
           same(last_rename_to, "/work/real-dir/renamed"));
    /* A name the backend does not have is one round trip: a backend
       follows no links, so only LOOP or NOT_DIR send the client walking. */
    open_calls = 0u;
    stat_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "/work/absent",
                                 ASTRA_VFS_OPEN_READ, &file) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(open_calls == 1u && stat_calls == 0u);
    assert(astra_filesystem_open(&single_filesystem, "/work",
                                 ASTRA_VFS_OPEN_READ |
                                     ASTRA_VFS_OPEN_DIRECTORY,
                                 &file) == ASTRA_VFS_OK);
    char directory_path[16];
    uint32_t directory_length = 0u;
    assert(astra_filesystem_directory_path(&file, directory_path, sizeof(directory_path), &directory_length) == ASTRA_VFS_OK &&
           directory_length == 1u && directory_path[0] == '/');
    AstraFile readonly_directory = file;
    readonly_directory._private_member = (uint16_t)(ASTRA_RIGHT_READ << 8);
    assert(astra_filesystem_mkdir_at(&single_filesystem, &readonly_directory, "new", 0700u) == ASTRA_VFS_ERR_ACCESS);
    assert(astra_filesystem_rename_at(&single_filesystem, &file, "a", &readonly_directory, "b") == ASTRA_VFS_ERR_ACCESS);
    AstraFile foreign_directory = file;
    AstraVfsClient foreign_client = {0};
    foreign_directory._private_client = &foreign_client;
    assert(astra_filesystem_link_at(&single_filesystem, &file, "a", &foreign_directory, "b") == ASTRA_VFS_ERR_CROSS_DEVICE);
    AstraFile closed_directory = ASTRA_FILE_INIT;
    assert(astra_filesystem_symlink_at(&single_filesystem, "target", &closed_directory, "new") == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_filesystem_readlink_at(&single_filesystem, &closed_directory, "new", directory_path, sizeof(directory_path), &directory_length) == ASTRA_VFS_ERR_BAD_HANDLE);
    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_stat_at(&single_filesystem, &file, "note", 0u, &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_FILE && info.mode == 0755u &&
           info.nlink == 2u && info.uid == 501u && info.gid == 20u);
    assert(info.meta.volume == 3u && info.meta.inode == 99u &&
           (info.meta.valid & ASTRA_VFS_META_MODE) != 0u);
    assert(astra_filesystem_stat_at(&single_filesystem, &file, "missing", 0u, &info) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_filesystem_stat_at(&single_filesystem, &file, "note", 0u, NULL) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_filesystem_open_at(&single_filesystem, &file, "note", ASTRA_VFS_OPEN_READ,
                                    &child) == ASTRA_VFS_OK);
    assert(astra_filesystem_close(&child) == ASTRA_VFS_OK);
    assert(astra_filesystem_open_at(&single_filesystem, &file, "absolute-link", ASTRA_VFS_OPEN_READ,
                                    &child) == ASTRA_VFS_OK);
    assert(astra_filesystem_close(&child) == ASTRA_VFS_OK);
    assert(astra_filesystem_stat_at(&single_filesystem, &file, "../work/note", ASTRA_VFS_STAT_FOLLOW,
                                    &info) == ASTRA_VFS_OK);
    assert(astra_filesystem_directory_from_file(&file, &directory) ==
           ASTRA_VFS_OK);
    readdir_file_mode = 1u;
    readdir_file_calls = 0u;
    assert(astra_filesystem_directory_read(&directory, entries, 0u, &count) ==
           ASTRA_VFS_ERR_INVALID);
    assert(readdir_file_calls == 0u);
    assert(astra_filesystem_directory_read(&directory, entries, 33u, &count) ==
               ASTRA_VFS_OK &&
           count == 1u && same(entries[0].name, "terminal") &&
           last_readdir_capacity == 33u);
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
               ASTRA_VFS_OK &&
           count == 0u && readdir_file_calls == 1u);
    assert(astra_filesystem_directory_rewind(&directory) == ASTRA_VFS_OK);
    readdir_file_mode = 2u;
    readdir_file_calls = 0u;
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
               ASTRA_VFS_OK &&
           count == 1u && same(entries[0].name, "first"));
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
               ASTRA_VFS_OK &&
           count == 1u && same(entries[0].name, "last"));
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
               ASTRA_VFS_OK &&
           count == 0u && readdir_file_calls == 2u);
    {
        AstraDirectory other;
        client.features = ASTRA_VFS_FEATURE_DESCRIPTIONS;
        assert(astra_filesystem_directory_rewind(&directory) == ASTRA_VFS_OK);
        other = directory;
        assert(astra_filesystem_directory_read(&directory, entries, 1u,
                                                &count) == ASTRA_VFS_OK &&
               count == 1u && same(entries[0].name, "first"));
        assert(astra_filesystem_directory_read(&other, entries, 1u, &count) ==
               ASTRA_VFS_OK && count == 1u && same(entries[0].name, "last"));
        assert(astra_filesystem_directory_read(&directory, entries, 1u,
                                                &count) == ASTRA_VFS_OK &&
               count == 0u);
        assert(astra_filesystem_directory_rewind(&other) == ASTRA_VFS_OK);
        assert(astra_filesystem_directory_read(&directory, entries, 1u,
                                                &count) == ASTRA_VFS_OK &&
               count == 1u && same(entries[0].name, "first"));
        astra_filesystem_directory_close(&other);
        client.features = 0u;
    }
    astra_filesystem_directory_close(&directory);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    readdir_file_mode = 0u;
    astra_filesystem_detach(&single_filesystem);
    astra_assign_table_destroy(&single_assigns);

    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "WORK", 7u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "work") == ASTRA_VFS_OK);
    assert(astra_assign_join(&assigns, "WORK", 8u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "rom") == ASTRA_VFS_OK);
    client.transport = NULL;
    assert(astra_filesystem_attach_io(&filesystem, &assigns, client_for,
                              astra_vfs_port_read_bulk_position, bulk_write, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_open(&filesystem, "/", &directory) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 1u && same(entries[0].name, "work"));
    astra_filesystem_directory_close(&directory);
    assert(astra_path_qualify("WORK", "src", "main.c", path,
                            sizeof(path)) == ASTRA_VFS_OK);
    assert(same(path, "/work/src/main.c"));
    assert(astra_path_split(path, last_open_path, sizeof(last_open_path),
                               relative, sizeof(relative)) == ASTRA_VFS_OK);
    assert(same(last_open_path, "WORK") && same(relative, "src/main.c"));
    assert(astra_path_normalise("src/../bin", path, sizeof(path)) ==
           ASTRA_VFS_OK && same(path, "bin"));
    assert(astra_assign_lookup(&assigns, "WORK") != NULL);
    assert(astra_assign_member(&assigns, "WORK", 1u)->handle == 8u);

    memcpy(contents, "hello", 5u);
    content_size = 5u;
    assert(astra_filesystem_open(&filesystem, "/work/note",
                         ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
                         &file) == ASTRA_VFS_OK);
    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_stat_at(&filesystem, &file, "note", 0u, &info) ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(astra_filesystem_open_at(&filesystem, &file, "note", ASTRA_VFS_OPEN_READ,
                                    &child) == ASTRA_VFS_ERR_NOT_DIR);
    assert(astra_filesystem_unlink_at(&filesystem, &file, "note", 0u) ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(astra_filesystem_chmod_at(&filesystem, &file, "note", 0640u, 0u) ==
           ASTRA_VFS_ERR_NOT_DIR);
    for (uint32_t index = 0u; index < sizeof(written); ++index)
        written[index] = (uint8_t)index;
    assert(astra_filesystem_write(&file, UINT64_MAX, written, sizeof(written), &moved) ==
           ASTRA_VFS_OK && moved == sizeof(written) &&
           bulk_write_calls == 1u && write_calls == 1u);
    assert(astra_filesystem_file_info(&file, &info) == ASTRA_VFS_OK);
    offset = info.offset;
    moved = UINT32_MAX;
    assert(astra_filesystem_read(&file, NULL, 1u, &moved) ==
           ASTRA_VFS_ERR_INVALID && moved == 0u);
    assert(astra_filesystem_file_info(&file, &info) == ASTRA_VFS_OK &&
           info.offset == offset);
    moved = UINT32_MAX;
    assert(astra_filesystem_write(&file, UINT64_MAX, NULL, 1u, &moved) ==
           ASTRA_VFS_ERR_INVALID && moved == 0u);
    assert(astra_filesystem_file_info(&file, &info) == ASTRA_VFS_OK &&
           info.offset == offset);
    assert(astra_filesystem_seek(&file, 0, ASTRA_FILE_SEEK_BEGIN, &offset) ==
           ASTRA_VFS_OK && offset == 0u);
    assert(astra_filesystem_read(&file, read, sizeof(read), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(read) && read_calls == 1u &&
           memcmp(read, written, sizeof(read)) == 0);
    assert(astra_filesystem_seek(&file, -1, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_OK && offset == sizeof(written) - 1u);
    assert(astra_filesystem_file_info(&file, &info) == ASTRA_VFS_OK &&
           info.byte_size == sizeof(written) && info.offset == offset &&
           info.kind == ASTRA_VFS_KIND_FILE && info.mode == 0600u &&
           info.uid == 501u && info.gid == 20u);
    content_size = 320u; /* another handle grew the same file */
    assert(astra_filesystem_seek(&file, -1, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_OK && offset == 319u);
    content_size = 4u; /* another handle truncated it */
    assert(astra_filesystem_seek(&file, 0, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_OK && offset == 4u);
    assert(astra_filesystem_seek(&file, -5, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_ERR_INVALID && offset == 4u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_seek(&file, 0, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_ERR_INVALID && offset == 4u);

    memcpy(contents, "hello", 5u);
    content_size = 5u;
    assert(astra_filesystem_open(&filesystem, "/work/note",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                         &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_write_at(&file, 1u, UINT64_MAX, "X", 1u, &moved) ==
           ASTRA_VFS_OK && moved == 1u && contents[1] == 'X' &&
           content_size == 5u && last_write_flags == 0u);
    assert(astra_filesystem_write(&file, UINT64_MAX, "Y", 1u, &moved) == ASTRA_VFS_OK &&
           moved == 1u && contents[5] == 'Y' && content_size == 6u &&
           last_write_flags == ASTRA_VFS_WRITE_APPEND);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_file_info(&file, &info) == ASTRA_VFS_ERR_INVALID);
    memcpy(contents, written, sizeof(written));
    content_size = sizeof(written);

    assert(astra_filesystem_open(&filesystem, "/work/tool",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                             ASTRA_VFS_OPEN_TRUNCATE,
                         &file) == ASTRA_VFS_OK);
    assert(last_open_client == &secondary_client &&
           same(last_open_path, "/rom/tool"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);

    astra_filesystem_detach(&filesystem);
    assert(astra_filesystem_attach(&filesystem, &assigns, client_for, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_open(&filesystem, "/work/note", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_read(&file, read, sizeof(read), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(read) && read_calls == 3u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);

    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_stat(&filesystem, "/work/note", &info) == ASTRA_VFS_OK &&
           info.byte_size == sizeof(written));
    assert(astra_filesystem_directory_open(&filesystem, "/work", &directory) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 2u && same(entries[0].name, "note") &&
           entries[1].kind == ASTRA_VFS_KIND_DIRECTORY);
    assert(astra_filesystem_directory_rewind(&directory) == ASTRA_VFS_OK);
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 2u && same(entries[0].name, "note") &&
           same(entries[1].name, "tools"));
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 0u);
    astra_filesystem_directory_close(&directory);
    assert(astra_filesystem_directory_rewind(&directory) == ASTRA_VFS_ERR_INVALID);
    assert(astra_filesystem_mkdir(&filesystem, "/work/new") == ASTRA_VFS_OK &&
           made_directory != 0u);
    assert(last_create_mode == ASTRA_VFS_MODE_DEFAULT);
    assert(astra_filesystem_mkdir_mode(&filesystem, "/work/new", 0700u) ==
           ASTRA_VFS_OK && last_create_mode == 0700u);
    assert(astra_filesystem_open_mode(&filesystem, "/work/tool",
                              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                              0600u, &file) == ASTRA_VFS_OK);
    assert(last_create_mode == 0600u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_chmod(&filesystem, "/work/note", 0640u) == ASTRA_VFS_OK);
    assert(last_chmod_mode == 0640u);
    assert(astra_filesystem_readlink(&filesystem, "/work/link", read, sizeof(read),
                             &length) == ASTRA_VFS_OK);
    assert(length == 4u && memcmp(read, "note", length) == 0);
    assert(astra_filesystem_lstat(&filesystem, "/work/link", &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_SYMLINK);
    assert(astra_filesystem_stat(&filesystem, "/work/link", &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_FILE &&
           info.byte_size == sizeof(written));
    assert(astra_filesystem_open(&filesystem, "/work/link", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(
               &filesystem, "/work/link",
               ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                   ASTRA_VFS_OPEN_EXCLUSIVE,
               &file) == ASTRA_VFS_ERR_EXISTS);
    assert(astra_filesystem_open(&filesystem, "/work/dangling",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                         &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/new-target"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(&filesystem, "/work/dir-link/note",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/real-dir/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_lstat(&filesystem, "/work/dir-link/note", &info) ==
           ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_FILE);
    assert(astra_filesystem_open(&filesystem, "/work/loop-a", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_ERR_LOOP);
    assert(astra_filesystem_symlink("note", &filesystem, "/work/new-link") ==
           ASTRA_VFS_OK);
    assert(same(last_symlink_target, "note") &&
           same(last_symlink_path, "/work/new-link"));
    assert(astra_filesystem_link(&filesystem, "/work/note", "/work/hard-link") ==
           ASTRA_VFS_OK);
    assert(same(last_link_from, "/work/note") &&
           same(last_link_to, "/work/hard-link"));
    assert(astra_filesystem_link(&filesystem, "/work/note", "/work/tool") ==
           ASTRA_VFS_ERR_CROSS_DEVICE);
    assert(astra_filesystem_unlink(&filesystem, "/work/link", 0u) == ASTRA_VFS_OK &&
           removed_link != 0u);
    assert(astra_filesystem_unlink(&filesystem, "/work/real-dir/",
                                   ASTRA_VFS_UNLINK_DIRECTORY) == ASTRA_VFS_OK &&
           last_unlink_flags == ASTRA_VFS_UNLINK_DIRECTORY);
    assert(astra_filesystem_unlink(&filesystem, "/work/real-dir/", 0u) ==
           ASTRA_VFS_ERR_IS_DIR);
    assert(astra_filesystem_unlink(&filesystem, "/work/note", UINT32_MAX) ==
           ASTRA_VFS_ERR_INVALID);
    stat_calls = 0u;
    assert(astra_filesystem_rename(&filesystem, "/work/note", "/work/renamed") ==
           ASTRA_VFS_OK);
    /* One source lookup and one destination union walk. WORK has two members,
     * so a missing destination must ask both exactly once, not twice. */
    assert(stat_calls == 3u);
    assert(renamed_file != 0u && same(last_rename_from, "/work/note") &&
           same(last_rename_to, "/work/renamed"));
    assert(astra_filesystem_rename(&filesystem, "/work/note", "/work/tool") ==
           ASTRA_VFS_ERR_CROSS_DEVICE);
    assert(astra_filesystem_unlink(&filesystem, "/work/note", 0u) == ASTRA_VFS_OK &&
           removed_file != 0u);
    assert(astra_filesystem_open(&filesystem, "/work",
                         ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                             ASTRA_VFS_OPEN_DIRECTORY,
                         &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_chmod_at(&filesystem, &file, "note", 0600u, 0u) ==
           ASTRA_VFS_OK && last_chmod_mode == 0600u);
    assert(astra_filesystem_unlink_at(&filesystem, &file, "note", 0u) == ASTRA_VFS_OK &&
           removed_file != 0u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    astra_filesystem_detach(&filesystem);
    astra_assign_table_destroy(&assigns);
    test_provider_position_is_the_authority();
    puts("filesystem.library tests passed");
    return 0;
}

uint32_t astra_vfs_mkdir_at(AstraVfsClient *value, AstraVfsFile dir, const char *path, uint16_t mode)
{ (void)dir; return astra_vfs_mkdir_mode(value, path, mode); }
uint32_t astra_vfs_rename_at(AstraVfsClient *value, AstraVfsFile fromdir, const char *from, AstraVfsFile todir, const char *to)
{ (void)fromdir; (void)todir; return astra_vfs_rename(value, from, to); }
uint32_t astra_vfs_link_at(AstraVfsClient *value, AstraVfsFile fromdir, const char *from, AstraVfsFile todir, const char *to)
{ (void)fromdir; (void)todir; return astra_vfs_link(value, from, to); }
uint32_t astra_vfs_symlink_at(AstraVfsClient *value, const char *target, AstraVfsFile dir, const char *path)
{ (void)dir; return astra_vfs_symlink(value, target, path); }
uint32_t astra_vfs_readlink_at(AstraVfsClient *value, AstraVfsFile dir, const char *path, void *buffer, uint32_t capacity, uint32_t *length)
{ (void)dir; return astra_vfs_readlink(value, path, buffer, capacity, length); }

uint32_t astra_vfs_directory_path(AstraVfsClient *value, AstraVfsFile file,
                                  void *buffer, uint32_t capacity, uint32_t *length)
{
    assert(value == &client && file == 2u);
    *length = 1u;
    if (capacity == 0u) return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, "/", 1u);
    return ASTRA_VFS_OK;
}
