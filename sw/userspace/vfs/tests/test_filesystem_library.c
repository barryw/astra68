#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/filesystem_library.h>
#include <astra/vfs_port_transport.h>

extern const AstraFilesystemLibraryV1 astra_library_exports;

static AstraVfsClient client;
static AstraVfsClient secondary_client;
static uint8_t contents[512];
static uint64_t content_size;
static uint32_t read_calls;
static uint32_t write_calls;
static uint32_t bulk_write_calls;
static uint32_t made_directory;
static uint32_t removed_file;
static uint32_t renamed_file;
static uint16_t last_create_mode;
static uint16_t last_chmod_mode;
static AstraVfsClient *last_open_client;
static char last_open_path[ASTRA_VFS_PATH_MAX];
static char last_rename_from[ASTRA_VFS_PATH_MAX];
static char last_rename_to[ASTRA_VFS_PATH_MAX];

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
    (void)flags;
    last_create_mode = create_mode;
    last_open_client = value;
    strcpy(last_open_path, path);
    if (same(path, "/work/note")) {
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

uint32_t astra_vfs_close(AstraVfsClient *value, AstraVfsFile file)
{
    (void)value;
    return file == 1u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_BAD_HANDLE;
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
                                  uint64_t offset, const void *buffer,
                                  uint32_t length, uint32_t *moved,
                                  uint64_t *position)
{
    uint32_t status = astra_vfs_write(value, file, offset, buffer, length,
                                      moved);

    if (status == ASTRA_VFS_OK)
        *position = offset + *moved;
    return status;
}

static uint32_t bulk_write(AstraVfsClient *value, AstraVfsFile file,
                           uint64_t offset, const void *buffer,
                           uint32_t length, uint32_t *moved)
{
    ++bulk_write_calls;
    return astra_vfs_write(value, file, offset, buffer, length, moved);
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
    return removed_file != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_INVALID;
}

uint32_t astra_vfs_rename(AstraVfsClient *value, const char *from,
                          const char *to)
{
    (void)value;
    snprintf(last_rename_from, sizeof(last_rename_from), "%s", from);
    snprintf(last_rename_to, sizeof(last_rename_to), "%s", to);
    renamed_file = same(from, "/work/note") && same(to, "/work/renamed");
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

uint32_t astra_vfs_readlink(AstraVfsClient *value, const char *path,
                            void *buffer, uint32_t capacity,
                            uint32_t *length)
{
    static const char target[] = "note";

    if (value != &client || !same(path, "/work/link"))
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (capacity < sizeof(target) - 1u)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, target, sizeof(target) - 1u);
    *length = sizeof(target) - 1u;
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
    const AstraFilesystemLibraryV1 *library = &astra_library_exports;
    AstraAssignTable assigns;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
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

    assert(library->abi_major == ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR);
    astra_assign_table_init(&assigns);
    assert(astra_assign_bind(&assigns, "WORK", 7u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "work") == ASTRA_VFS_OK);
    assert(astra_assign_join(&assigns, "WORK", 8u,
                             ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                             "rom") == ASTRA_VFS_OK);
    client.transport = NULL;
    assert(library->attach_io(&filesystem, &assigns, client_for,
                              astra_vfs_port_read_bulk, bulk_write, NULL) ==
           ASTRA_VFS_OK);
    assert(library->qualify("WORK", "src", "main.c", path,
                            sizeof(path)) == ASTRA_VFS_OK);
    assert(same(path, "WORK:src/main.c"));
    assert(library->path_split(path, last_open_path, sizeof(last_open_path),
                               relative, sizeof(relative)) == ASTRA_VFS_OK);
    assert(same(last_open_path, "WORK") && same(relative, "src/main.c"));
    assert(library->path_normalise("src/../bin", path, sizeof(path)) ==
           ASTRA_VFS_OK && same(path, "bin"));
    assert(library->assign_lookup(&assigns, "WORK") != NULL);
    assert(library->assign_member(&assigns, "WORK", 1u)->handle == 8u);

    memcpy(contents, "hello", 5u);
    content_size = 5u;
    assert(library->open(&filesystem, "WORK:note",
                         ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
                         &file) == ASTRA_VFS_OK);
    for (uint32_t index = 0u; index < sizeof(written); ++index)
        written[index] = (uint8_t)index;
    assert(library->write(&file, written, sizeof(written), &moved) ==
           ASTRA_VFS_OK && moved == sizeof(written) &&
           bulk_write_calls == 1u && write_calls == 1u);
    assert(library->seek(&file, 0, ASTRA_FILE_SEEK_BEGIN, &offset) ==
           ASTRA_VFS_OK && offset == 0u);
    assert(library->read(&file, read, sizeof(read), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(read) && read_calls == 1u &&
           memcmp(read, written, sizeof(read)) == 0);
    assert(library->seek(&file, -1, ASTRA_FILE_SEEK_END, &offset) ==
           ASTRA_VFS_OK && offset == sizeof(written) - 1u);
    assert(library->file_info(&file, &info) == ASTRA_VFS_OK &&
           info.byte_size == sizeof(written) && info.offset == offset &&
           info.kind == ASTRA_VFS_KIND_FILE);
    assert(library->close(&file) == ASTRA_VFS_OK);

    assert(library->open(&filesystem, "WORK:tool",
                         ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                             ASTRA_VFS_OPEN_TRUNCATE,
                         &file) == ASTRA_VFS_OK);
    assert(last_open_client == &secondary_client &&
           same(last_open_path, "/rom/tool"));
    assert(library->close(&file) == ASTRA_VFS_OK);

    library->detach(&filesystem);
    assert(library->attach(&filesystem, &assigns, client_for, NULL, NULL) ==
           ASTRA_VFS_OK);
    assert(library->open(&filesystem, "WORK:note", ASTRA_VFS_OPEN_READ,
                         &file) == ASTRA_VFS_OK);
    assert(library->read(&file, read, sizeof(read), &moved) == ASTRA_VFS_OK &&
           moved == sizeof(read) && read_calls == 3u);
    assert(library->close(&file) == ASTRA_VFS_OK);

    info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    assert(library->stat(&filesystem, "WORK:note", &info) == ASTRA_VFS_OK &&
           info.byte_size == sizeof(written));
    assert(library->directory_open(&filesystem, "WORK:", &directory) ==
           ASTRA_VFS_OK);
    assert(library->directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 2u && same(entries[0].name, "note") &&
           entries[1].kind == ASTRA_VFS_KIND_DIRECTORY);
    assert(library->directory_read(&directory, entries, 2u, &count) ==
           ASTRA_VFS_OK && count == 0u);
    library->directory_close(&directory);
    assert(library->mkdir(&filesystem, "WORK:new") == ASTRA_VFS_OK &&
           made_directory != 0u);
    assert(last_create_mode == ASTRA_VFS_MODE_DEFAULT);
    assert(library->mkdir_mode(&filesystem, "WORK:new", 0700u) ==
           ASTRA_VFS_OK && last_create_mode == 0700u);
    assert(library->open_mode(&filesystem, "WORK:tool",
                              ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE,
                              0600u, &file) == ASTRA_VFS_OK);
    assert(last_create_mode == 0600u);
    assert(library->close(&file) == ASTRA_VFS_OK);
    assert(library->chmod(&filesystem, "WORK:note", 0640u) == ASTRA_VFS_OK);
    assert(last_chmod_mode == 0640u);
    assert(library->readlink(&filesystem, "WORK:link", read, sizeof(read),
                             &length) == ASTRA_VFS_OK);
    assert(length == 4u && memcmp(read, "note", length) == 0);
    assert(library->rename(&filesystem, "WORK:note", "WORK:renamed") ==
           ASTRA_VFS_OK);
    assert(renamed_file != 0u && same(last_rename_from, "/work/note") &&
           same(last_rename_to, "/work/renamed"));
    assert(library->rename(&filesystem, "WORK:note", "WORK:tool") ==
           ASTRA_VFS_ERR_CROSS_DEVICE);
    assert(library->unlink(&filesystem, "WORK:note") == ASTRA_VFS_OK &&
           removed_file != 0u);
    library->detach(&filesystem);
    puts("filesystem.library tests passed");
    return 0;
}
