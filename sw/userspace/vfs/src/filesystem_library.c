#include <astra/filesystem_library.h>
#include <astra/library.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <string.h>

ASTRA_LIBRARY("filesystem.library", 1, 1, 0,
              ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR,
              ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR,
              "Barry Walker", "Copyright 2026 Barry Walker");

static int filesystem_valid(const AstraFilesystem *filesystem)
{
    return filesystem != NULL && filesystem->_private_assigns != NULL &&
           filesystem->_private_client_for != NULL;
}

static int file_valid(const AstraFile *file)
{
    return file != NULL && file->_private_client != NULL &&
           file->_private_file != ASTRA_VFS_FILE_INVALID;
}

static AstraVfsClient *filesystem_client_for(const AstraAssign *assign,
                                             void *context)
{
    AstraFilesystem *filesystem = context;

    return filesystem->_private_client_for(assign,
                                           filesystem->_private_context);
}

static uint32_t filesystem_locate(AstraFilesystem *, const char *, uint32_t,
                                  AstraVfsClient **, char *, uint16_t *,
                                  uint64_t *, uint32_t *,
                                  const AstraAssign **, AstraVfsDirEntry *);
static uint32_t filesystem_primary(AstraFilesystem *, const char *, uint32_t,
                                   AstraVfsClient **, char *, uint32_t *);

static uint32_t filesystem_attach(AstraFilesystem *filesystem,
                                  const AstraAssignTable *assigns,
                                  AstraVfsAssignClientFn client_for,
                                  AstraFilesystemReadAtFn read_at,
                                  void *context)
{
    if (filesystem == NULL || assigns == NULL || client_for == NULL)
        return ASTRA_VFS_ERR_INVALID;
    filesystem->_private_assigns = assigns;
    filesystem->_private_client_for = client_for;
    filesystem->_private_read_at = read_at;
    filesystem->_private_context = context;
    return ASTRA_VFS_OK;
}

static void filesystem_detach(AstraFilesystem *filesystem)
{
    if (filesystem != NULL)
        *filesystem = (AstraFilesystem)ASTRA_FILESYSTEM_INIT;
}

static uint32_t filesystem_open(AstraFilesystem *filesystem, const char *path,
                                uint32_t flags, AstraFile *file)
{
    const uint32_t known = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE |
                           ASTRA_VFS_OPEN_DIRECTORY;
    AstraVfsClient *client = NULL;
    AstraVfsFile opened = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t member = 0u;
    uint32_t rights = 0u;
    uint32_t status;
    char wire[ASTRA_VFS_PATH_MAX];

    if (!filesystem_valid(filesystem) || path == NULL || file == NULL ||
        (flags & ~known) != 0u ||
        (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *file = (AstraFile)ASTRA_FILE_INIT;
    if ((flags & ASTRA_VFS_OPEN_READ) != 0u)
        rights |= ASTRA_RIGHT_READ;
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        rights |= ASTRA_RIGHT_WRITE;
    /*
     * Find an existing name before opening it. Some backends implement
     * truncate with a create-capable mode, so probing union members with an
     * open can manufacture a shadowing file on the first writable member.
     */
    status = filesystem_locate(filesystem, path, rights, &client, wire,
                               &kind, &size, &member, NULL, NULL);
    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        status = filesystem_primary(filesystem, path, rights, &client, wire,
                                    &member);
        kind = ASTRA_VFS_KIND_FILE;
        size = 0u;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_open(client, wire, flags, &opened, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return status;
    file->_private_client = client;
    file->_private_read_at = filesystem->_private_read_at;
    file->_private_file = opened;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = (uint16_t)member;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_close(AstraFile *file)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = astra_vfs_close(file->_private_client, file->_private_file);
    *file = (AstraFile)ASTRA_FILE_INIT;
    return status;
}

static uint32_t filesystem_read_at(AstraFile *file, uint64_t offset,
                                   void *buffer, uint32_t length,
                                   uint32_t *moved)
{
    uint8_t *bytes = buffer;
    uint32_t total = 0u;
    uint32_t status = ASTRA_VFS_OK;

    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_READ) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;

        if (chunk > (file->_private_read_at != NULL ?
                     ASTRA_VFS_BULK_MAX : ASTRA_VFS_IO_MAX))
            chunk = file->_private_read_at != NULL ?
                    ASTRA_VFS_BULK_MAX : ASTRA_VFS_IO_MAX;
        if (file->_private_read_at != NULL)
            status = file->_private_read_at(
                file->_private_client, file->_private_file, offset + total,
                bytes + total, chunk, &part);
        else
            status = astra_vfs_read(
                file->_private_client, file->_private_file, offset + total,
                bytes + total, chunk, &part);
        total += part;
        if (status != ASTRA_VFS_OK || part < chunk)
            break;
    }
    *moved = total;
    return status;
}

static uint32_t filesystem_write_at(AstraFile *file, uint64_t offset,
                                    const void *buffer, uint32_t length,
                                    uint32_t *moved)
{
    const uint8_t *bytes = buffer;
    uint32_t total = 0u;
    uint32_t status = ASTRA_VFS_OK;

    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_WRITE) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;

        if (chunk > ASTRA_VFS_IO_MAX)
            chunk = ASTRA_VFS_IO_MAX;
        status = astra_vfs_write(
            file->_private_client, file->_private_file, offset + total,
            bytes + total, chunk, &part);
        total += part;
        if (status != ASTRA_VFS_OK || part < chunk)
            break;
    }
    if (offset <= UINT64_MAX - total &&
        offset + total > file->_private_size)
        file->_private_size = offset + total;
    *moved = total;
    return status;
}

static uint32_t filesystem_read(AstraFile *file, void *buffer,
                                uint32_t length, uint32_t *moved)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = filesystem_read_at(file, file->_private_offset, buffer, length,
                                moved);
    if (moved != NULL)
        file->_private_offset += *moved;
    return status;
}

static uint32_t filesystem_write(AstraFile *file, const void *buffer,
                                 uint32_t length, uint32_t *moved)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = filesystem_write_at(file, file->_private_offset, buffer, length,
                                 moved);
    if (moved != NULL)
        file->_private_offset += *moved;
    return status;
}

static uint32_t filesystem_seek(AstraFile *file, int64_t delta,
                                uint32_t origin, uint64_t *offset)
{
    uint64_t base;
    uint64_t result;

    if (!file_valid(file) || offset == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (origin == ASTRA_FILE_SEEK_BEGIN)
        base = 0u;
    else if (origin == ASTRA_FILE_SEEK_CURRENT)
        base = file->_private_offset;
    else if (origin == ASTRA_FILE_SEEK_END)
        base = file->_private_size;
    else
        return ASTRA_VFS_ERR_INVALID;
    if (delta < 0) {
        uint64_t amount = (uint64_t)(-(delta + 1)) + 1u;

        if (amount > base)
            return ASTRA_VFS_ERR_INVALID;
        result = base - amount;
    } else {
        uint64_t amount = (uint64_t)delta;

        if (amount > UINT64_MAX - base)
            return ASTRA_VFS_ERR_INVALID;
        result = base + amount;
    }
    file->_private_offset = result;
    *offset = result;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_file_info(const AstraFile *file,
                                     AstraFileInfo *info)
{
    if (!file_valid(file) || info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    info->open_flags = file->_private_flags;
    info->byte_size = file->_private_size;
    info->offset = file->_private_offset;
    info->kind = file->_private_kind;
    info->member = file->_private_member;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_locate(AstraFilesystem *filesystem,
                                  const char *path, uint32_t rights,
                                  AstraVfsClient **client, char *wire,
                                  uint16_t *kind, uint64_t *size,
                                  uint32_t *member,
                                  const AstraAssign **found_assign,
                                  AstraVfsDirEntry *meta)
{
    const AstraAssign *assign = NULL;
    AstraVfsClient *serving = NULL;
    AstraVfsDirEntry found = {0};
    uint32_t found_member = 0u;
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_stat(
        filesystem->_private_assigns, path, rights, filesystem_client_for,
        filesystem, wire, ASTRA_VFS_PATH_MAX, &found, &serving, &assign,
        &found_member);
    if (status != ASTRA_VFS_OK)
        return status;
    if (client != NULL)
        *client = serving;
    if (kind != NULL)
        *kind = found.kind;
    if (size != NULL)
        *size = found.size;
    if (meta != NULL)
        *meta = found;
    if (member != NULL)
        *member = found_member;
    if (found_assign != NULL)
        *found_assign = assign;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_stat(AstraFilesystem *filesystem, const char *path,
                                AstraFileInfo *info)
{
    char wire[ASTRA_VFS_PATH_MAX];
    AstraVfsDirEntry meta = {0};
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t member = 0u;
    uint32_t status;

    if (info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_locate(filesystem, path, ASTRA_RIGHT_READ, NULL, wire,
                               &kind, &size, &member, NULL, &meta);
    if (status != ASTRA_VFS_OK)
        return status;
    *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    info->byte_size = size;
    info->kind = kind;
    info->member = (uint16_t)member;
    info->mtime = meta.mtime;
    info->uid = meta.uid;
    info->gid = meta.gid;
    info->mode = meta.mode;
    info->nlink = meta.nlink;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_primary(AstraFilesystem *filesystem,
                                   const char *path, uint32_t rights,
                                   AstraVfsClient **client, char *wire,
                                   uint32_t *found_member)
{
    if (!filesystem_valid(filesystem) || path == NULL || client == NULL ||
        wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return astra_vfs_assign_primary(
        filesystem->_private_assigns, path, rights, filesystem_client_for,
        filesystem, wire, ASTRA_VFS_PATH_MAX, client, found_member);
}

static uint32_t filesystem_mkdir(AstraFilesystem *filesystem,
                                 const char *path)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = filesystem_primary(filesystem, path, ASTRA_RIGHT_WRITE,
                                         &client, wire, NULL);

    return status == ASTRA_VFS_OK ? astra_vfs_mkdir(client, wire) : status;
}

static uint32_t filesystem_unlink(AstraFilesystem *filesystem,
                                  const char *path)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = filesystem_locate(
        filesystem, path, ASTRA_RIGHT_WRITE, &client, wire, NULL, NULL, NULL,
        NULL, NULL);

    return status == ASTRA_VFS_OK ? astra_vfs_unlink(client, wire) : status;
}

static uint32_t filesystem_rename(AstraFilesystem *filesystem,
                                  const char *from, const char *to)
{
    AstraVfsClient *from_client = NULL;
    AstraVfsClient *to_client = NULL;
    char from_wire[ASTRA_VFS_PATH_MAX];
    char to_wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    status = filesystem_locate(filesystem, from, ASTRA_RIGHT_WRITE,
                               &from_client, from_wire, NULL, NULL, NULL, NULL,
                               NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    status = filesystem_locate(filesystem, to, ASTRA_RIGHT_WRITE, &to_client,
                               to_wire, NULL, NULL, NULL, NULL, NULL);
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        status = filesystem_primary(filesystem, to, ASTRA_RIGHT_WRITE,
                                    &to_client, to_wire, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    if (from_client != to_client)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    return astra_vfs_rename(from_client, from_wire, to_wire);
}

static uint32_t filesystem_directory_open(AstraFilesystem *filesystem,
                                          const char *path,
                                          AstraDirectory *directory)
{
    AstraVfsUnionDirectory opened = ASTRA_VFS_UNION_DIRECTORY_INIT;
    uint32_t status;

    if (!filesystem_valid(filesystem) || directory == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    status = astra_vfs_union_directory_open(
        filesystem->_private_assigns, path, filesystem_client_for,
        filesystem, &opened);
    if (status != ASTRA_VFS_OK)
        return status;
    directory->_private_filesystem = filesystem;
    (void)memcpy(directory->_private_path, opened.path,
                 sizeof(directory->_private_path));
    directory->_private_cursor = opened.cursor;
    directory->_private_member = opened.member;
    directory->_private_worst = opened.worst;
    directory->_private_active = opened.active;
    directory->_private_done = opened.done;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_directory_read(AstraDirectory *directory,
                                          AstraDirectoryEntry *entries,
                                          uint32_t capacity,
                                          uint32_t *count)
{
    static AstraVfsDirEntry raw[ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX];
    AstraVfsUnionDirectory reading = ASTRA_VFS_UNION_DIRECTORY_INIT;
    AstraFilesystem *filesystem;
    uint32_t found_member = 0u;
    uint32_t status;

    if (directory == NULL || entries == NULL || capacity == 0u ||
        count == NULL || directory->_private_active == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (capacity > ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX)
        capacity = ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX;
    filesystem = directory->_private_filesystem;
    reading.table = filesystem->_private_assigns;
    reading.client_for = filesystem_client_for;
    reading.context = filesystem;
    (void)memcpy(reading.path, directory->_private_path,
                 sizeof(reading.path));
    reading.cursor = directory->_private_cursor;
    reading.member = directory->_private_member;
    reading.worst = directory->_private_worst;
    reading.active = directory->_private_active;
    reading.done = directory->_private_done;
    status = astra_vfs_union_directory_read(&reading, raw, capacity, count,
                                            &found_member);
    directory->_private_cursor = reading.cursor;
    directory->_private_member = reading.member;
    directory->_private_worst = reading.worst;
    directory->_private_active = reading.active;
    directory->_private_done = reading.done;
    if (status != ASTRA_VFS_OK)
        return status;
    for (uint32_t index = 0u; index < *count; ++index) {
        (void)memcpy(entries[index].name, raw[index].name,
                     sizeof(entries[index].name));
        entries[index].byte_size = raw[index].size;
        entries[index].mtime = raw[index].mtime;
        entries[index].uid = raw[index].uid;
        entries[index].gid = raw[index].gid;
        entries[index].kind = raw[index].kind;
        entries[index].mode = raw[index].mode;
        entries[index].nlink = raw[index].nlink;
        entries[index].member = (uint16_t)found_member;
    }
    return ASTRA_VFS_OK;
}

static void filesystem_directory_close(AstraDirectory *directory)
{
    if (directory != NULL)
        *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
}

const AstraFilesystemLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR,
    ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR,
    sizeof(AstraFilesystemLibraryV1),
    filesystem_attach,
    filesystem_detach,
    astra_path_qualify,
    astra_path_split,
    astra_path_normalise,
    filesystem_open,
    filesystem_close,
    filesystem_read,
    filesystem_write,
    filesystem_read_at,
    filesystem_write_at,
    filesystem_seek,
    filesystem_file_info,
    filesystem_stat,
    filesystem_mkdir,
    filesystem_unlink,
    filesystem_directory_open,
    filesystem_directory_read,
    filesystem_directory_close,
    astra_vfs_connect,
    astra_vfs_disconnect,
    astra_vfs_open,
    astra_vfs_close,
    astra_vfs_read,
    astra_vfs_write,
    astra_vfs_stat,
    astra_vfs_readdir_batch,
    astra_vfs_mkdir,
    astra_vfs_unlink,
    astra_assign_resolve,
    astra_assign_lookup,
    astra_assign_member,
    astra_vfs_assign_open,
    filesystem_rename,
    astra_vfs_rename,
};
