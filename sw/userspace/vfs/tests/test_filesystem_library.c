#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/filesystem_library.h>
#include <astra/vfs_port_transport.h>

static AstraVfsClient client;
static AstraVfsClient secondary_client;
static uint8_t contents[512];
static uint64_t content_size;
static uint32_t read_calls;
static uint32_t open_calls;
static uint32_t stat_calls;
static uint32_t readdir_file_calls;
static uint32_t readdir_file_mode;
static uint32_t write_calls;
static uint32_t bulk_write_calls;
static uint32_t last_write_flags;
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
                         uint64_t offset, const void *buffer, uint32_t length,
                         uint32_t *moved)
{
    (void)value;
    ++write_calls;
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
                                  uint64_t offset, uint32_t flags,
                                  const void *buffer, uint32_t length,
                                  uint32_t *moved, uint64_t *position)
{
    last_write_flags = flags;
    if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u)
        offset = content_size;
    uint32_t status = astra_vfs_write(value, file, offset, buffer, length,
                                      moved);

    if (status == ASTRA_VFS_OK)
        *position = offset + *moved;
    return status;
}

static uint32_t bulk_write(AstraVfsClient *value, AstraVfsFile file,
                           uint64_t offset, uint32_t flags,
                           const void *buffer, uint32_t length,
                           uint32_t *moved, uint64_t *position)
{
    uint32_t status;

    ++bulk_write_calls;
    last_write_flags = flags;
    if ((flags & ASTRA_VFS_WRITE_APPEND) != 0u)
        offset = content_size;
    status = astra_vfs_write(value, file, offset, buffer, length, moved);
    if (status == ASTRA_VFS_OK)
        *position = offset + *moved;
    return status;
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

uint32_t astra_vfs_stat(AstraVfsClient *value, const char *path,
                        uint64_t *size, uint16_t *kind)
{
    ++stat_calls;
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
    if (path[0] == '\0' && readdir_file_mode != 0u) {
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

uint32_t astra_vfs_unlink(AstraVfsClient *value, const char *path)
{
    (void)value;
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
        return ASTRA_VFS_ERR_NOT_DIR;
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

uint32_t astra_vfs_port_read_bulk(AstraVfsClient *value, AstraVfsFile file,
                                  uint64_t offset, void *buffer,
                                  uint32_t length, uint32_t *moved)
{
    uint32_t available;

    (void)value;
    ++read_calls;
    if (file != 1u || offset > content_size)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    available = (uint32_t)(content_size - offset);
    if (length > available)
        length = available;
    memcpy(buffer, contents + offset, length);
    *moved = length;
    return ASTRA_VFS_OK;
}

static AstraVfsClient *client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    if (assign->handle == 7u)
        return &client;
    return assign->handle == 8u ? &secondary_client : NULL;
}

int main(void)
{
    assert(ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR ==
           ASTRA_FILESYSTEM_LIBRARY_VERSION);
    AstraAssignTable assigns;
    AstraAssignTable single_assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFilesystem single_filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[2];
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
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "WORK:note",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK);
    assert(stat_calls == 0u && open_calls == 1u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "WORK:link",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK);
    assert(stat_calls != 0u && open_calls == 2u &&
           same(last_open_path, "/work/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "WORK:note",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                             ASTRA_VFS_OPEN_EXCLUSIVE,
                         &file) == ASTRA_VFS_ERR_EXISTS);
    assert(stat_calls == 0u && open_calls == 1u);
    stat_calls = 0u;
    open_calls = 0u;
    assert(astra_filesystem_open(&single_filesystem, "WORK:new-target",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                         &file) == ASTRA_VFS_OK);
    assert(stat_calls == 0u && open_calls == 1u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    stat_calls = 0u;
    renamed_file = 0u;
    assert(astra_filesystem_rename(&single_filesystem, "WORK:note", "WORK:renamed") ==
           ASTRA_VFS_OK);
    assert(stat_calls == 0u && renamed_file != 0u);
    stat_calls = 0u;
    renamed_file = 0u;
    assert(astra_filesystem_rename(&single_filesystem, "WORK:dir-link/note",
                           "WORK:dir-link/renamed") == ASTRA_VFS_OK);
    assert(stat_calls != 0u && renamed_file != 0u &&
           same(last_rename_from, "/work/real-dir/note") &&
           same(last_rename_to, "/work/real-dir/renamed"));
    assert(astra_filesystem_open(&single_filesystem, "WORK:",
                                 ASTRA_VFS_OPEN_READ |
                                     ASTRA_VFS_OPEN_DIRECTORY,
                                 &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_directory_from_file(&file, &directory) ==
           ASTRA_VFS_OK);
    readdir_file_mode = 1u;
    readdir_file_calls = 0u;
    assert(astra_filesystem_directory_read(&directory, entries, 2u, &count) ==
               ASTRA_VFS_OK &&
           count == 1u && same(entries[0].name, "terminal"));
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
    astra_filesystem_directory_close(&directory);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    readdir_file_mode = 0u;
    astra_filesystem_detach(&single_filesystem);

    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "WORK", 7u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "work") == ASTRA_VFS_OK);
    assert(astra_assign_join(&assigns, "WORK", 8u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "rom") == ASTRA_VFS_OK);
    client.transport = NULL;
    assert(astra_filesystem_attach_io(&filesystem, &assigns, client_for,
                              astra_vfs_port_read_bulk, bulk_write, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_path_qualify("WORK", "src", "main.c", path,
                            sizeof(path)) == ASTRA_VFS_OK);
    assert(same(path, "WORK:src/main.c"));
    assert(astra_path_split(path, last_open_path, sizeof(last_open_path),
                               relative, sizeof(relative)) == ASTRA_VFS_OK);
    assert(same(last_open_path, "WORK") && same(relative, "src/main.c"));
    assert(astra_path_normalise("src/../bin", path, sizeof(path)) ==
           ASTRA_VFS_OK && same(path, "bin"));
    assert(astra_assign_lookup(&assigns, "WORK") != NULL);
    assert(astra_assign_member(&assigns, "WORK", 1u)->handle == 8u);

    memcpy(contents, "hello", 5u);
    content_size = 5u;
    assert(astra_filesystem_open(&filesystem, "WORK:note",
                         ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
                         &file) == ASTRA_VFS_OK);
    for (uint32_t index = 0u; index < sizeof(written); ++index)
        written[index] = (uint8_t)index;
    assert(astra_filesystem_write(&file, written, sizeof(written), &moved) ==
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
    assert(astra_filesystem_write(&file, NULL, 1u, &moved) ==
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
           info.kind == ASTRA_VFS_KIND_FILE);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);

    memcpy(contents, "hello", 5u);
    content_size = 5u;
    assert(astra_filesystem_open(&filesystem, "WORK:note",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_APPEND,
                         &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_write_at(&file, 1u, "X", 1u, &moved) ==
           ASTRA_VFS_OK && moved == 1u && contents[1] == 'X' &&
           content_size == 5u && last_write_flags == 0u);
    assert(astra_filesystem_write(&file, "Y", 1u, &moved) == ASTRA_VFS_OK &&
           moved == 1u && contents[5] == 'Y' && content_size == 6u &&
           last_write_flags == ASTRA_VFS_WRITE_APPEND);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    memcpy(contents, written, sizeof(written));
    content_size = sizeof(written);

    assert(astra_filesystem_open(&filesystem, "WORK:tool",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                             ASTRA_VFS_OPEN_TRUNCATE,
                         &file) == ASTRA_VFS_OK);
    assert(last_open_client == &secondary_client &&
           same(last_open_path, "/rom/tool"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);

    astra_filesystem_detach(&filesystem);
    assert(astra_filesystem_attach(&filesystem, &assigns, client_for, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(astra_filesystem_open(&filesystem, "WORK:note", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_OK);
    assert(astra_filesystem_read(&file, read, sizeof(read), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(read) && read_calls == 3u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);

    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(astra_filesystem_stat(&filesystem, "WORK:note", &info) == ASTRA_VFS_OK &&
           info.byte_size == sizeof(written));
    assert(astra_filesystem_directory_open(&filesystem, "WORK:", &directory) ==
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
    assert(astra_filesystem_mkdir(&filesystem, "WORK:new") == ASTRA_VFS_OK &&
           made_directory != 0u);
    assert(last_create_mode == ASTRA_VFS_MODE_DEFAULT);
    assert(astra_filesystem_mkdir_mode(&filesystem, "WORK:new", 0700u) ==
           ASTRA_VFS_OK && last_create_mode == 0700u);
    assert(astra_filesystem_open_mode(&filesystem, "WORK:tool",
                              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                              0600u, &file) == ASTRA_VFS_OK);
    assert(last_create_mode == 0600u);
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_chmod(&filesystem, "WORK:note", 0640u) == ASTRA_VFS_OK);
    assert(last_chmod_mode == 0640u);
    assert(astra_filesystem_readlink(&filesystem, "WORK:link", read, sizeof(read),
                             &length) == ASTRA_VFS_OK);
    assert(length == 4u && memcmp(read, "note", length) == 0);
    assert(astra_filesystem_lstat(&filesystem, "WORK:link", &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_SYMLINK);
    assert(astra_filesystem_stat(&filesystem, "WORK:link", &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_FILE &&
           info.byte_size == sizeof(written));
    assert(astra_filesystem_open(&filesystem, "WORK:link", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(
               &filesystem, "WORK:link",
               ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                   ASTRA_VFS_OPEN_EXCLUSIVE,
               &file) == ASTRA_VFS_ERR_EXISTS);
    assert(astra_filesystem_open(&filesystem, "WORK:dangling",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                         &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/new-target"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_open(&filesystem, "WORK:dir-link/note",
                         ASTRA_VFS_OPEN_READ, &file) == ASTRA_VFS_OK &&
           same(last_open_path, "/work/real-dir/note"));
    assert(astra_filesystem_close(&file) == ASTRA_VFS_OK);
    assert(astra_filesystem_lstat(&filesystem, "WORK:dir-link/note", &info) ==
           ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_FILE);
    assert(astra_filesystem_open(&filesystem, "WORK:loop-a", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_ERR_LOOP);
    assert(astra_filesystem_symlink("note", &filesystem, "WORK:new-link") ==
           ASTRA_VFS_OK);
    assert(same(last_symlink_target, "note") &&
           same(last_symlink_path, "/work/new-link"));
    assert(astra_filesystem_link(&filesystem, "WORK:note", "WORK:hard-link") ==
           ASTRA_VFS_OK);
    assert(same(last_link_from, "/work/note") &&
           same(last_link_to, "/work/hard-link"));
    assert(astra_filesystem_link(&filesystem, "WORK:note", "WORK:tool") ==
           ASTRA_VFS_ERR_CROSS_DEVICE);
    assert(astra_filesystem_unlink(&filesystem, "WORK:link") == ASTRA_VFS_OK &&
           removed_link != 0u);
    stat_calls = 0u;
    assert(astra_filesystem_rename(&filesystem, "WORK:note", "WORK:renamed") ==
           ASTRA_VFS_OK);
    /* One source lookup and one destination union walk. WORK has two members,
     * so a missing destination must ask both exactly once, not twice. */
    assert(stat_calls == 3u);
    assert(renamed_file != 0u && same(last_rename_from, "/work/note") &&
           same(last_rename_to, "/work/renamed"));
    assert(astra_filesystem_rename(&filesystem, "WORK:note", "WORK:tool") ==
           ASTRA_VFS_ERR_CROSS_DEVICE);
    assert(astra_filesystem_unlink(&filesystem, "WORK:note") == ASTRA_VFS_OK &&
           removed_file != 0u);
    astra_filesystem_detach(&filesystem);
    puts("filesystem.library tests passed");
    return 0;
}
