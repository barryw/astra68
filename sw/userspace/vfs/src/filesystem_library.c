#include <astra/filesystem_library.h>
#include <astra/library.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <string.h>

ASTRA_LIBRARY("filesystem.library", 2, 0, 0,
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

static int filesystem_resolve_single(AstraFilesystem *filesystem,
                                     const char *path, uint32_t rights,
                                     char *wire, AstraVfsClient **client)
{
    const AstraAssign *assign = NULL;

    if (astra_assign_resolve(filesystem->_private_assigns, path, rights, 0u,
                             wire, ASTRA_VFS_PATH_MAX, &assign) !=
            ASTRA_VFS_OK ||
        astra_assign_member(filesystem->_private_assigns, assign->name, 1u) !=
            NULL)
        return 0;
    *client = filesystem_client_for(assign, filesystem);
    return *client != NULL;
}

static uint32_t filesystem_locate(AstraFilesystem *, const char *, uint32_t,
                                  AstraVfsClient **, char *, uint16_t *,
                                  uint64_t *, uint32_t *,
                                  const AstraAssign **, AstraVfsDirEntry *);
static uint32_t filesystem_locate_literal(
    AstraFilesystem *, const char *, uint32_t, AstraVfsClient **, char *,
    uint16_t *, uint64_t *, uint32_t *, const AstraAssign **,
    AstraVfsDirEntry *);

static uint32_t filesystem_attach_io(AstraFilesystem *filesystem,
                                     const AstraAssignTable *assigns,
                                     AstraVfsAssignClientFn client_for,
                                     AstraFilesystemReadAtFn read_at,
                                     AstraFilesystemWriteAtFn write_at,
                                     void *context)
{
    if (filesystem == NULL || assigns == NULL || client_for == NULL)
        return ASTRA_VFS_ERR_INVALID;
    filesystem->_private_assigns = assigns;
    filesystem->_private_client_for = client_for;
    filesystem->_private_read_at = read_at;
    filesystem->_private_write_at = write_at;
    filesystem->_private_context = context;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_attach(AstraFilesystem *filesystem,
                                  const AstraAssignTable *assigns,
                                  AstraVfsAssignClientFn client_for,
                                  AstraFilesystemReadAtFn read_at,
                                  void *context)
{
    return filesystem_attach_io(filesystem, assigns, client_for, read_at,
                                NULL, context);
}

static void filesystem_detach(AstraFilesystem *filesystem)
{
    if (filesystem != NULL)
        *filesystem = (AstraFilesystem)ASTRA_FILESYSTEM_INIT;
}

static uint32_t filesystem_open_mode(AstraFilesystem *filesystem,
                                     const char *path, uint32_t flags,
                                     uint16_t create_mode, AstraFile *file)
{
    const uint32_t known = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE |
                           ASTRA_VFS_OPEN_DIRECTORY |
                           ASTRA_VFS_OPEN_EXCLUSIVE |
                           ASTRA_VFS_OPEN_APPEND;
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
        ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
         (flags & ASTRA_VFS_OPEN_CREATE) == 0u) ||
        (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u ||
        (create_mode != ASTRA_VFS_MODE_DEFAULT &&
         ((create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
          (flags & ASTRA_VFS_OPEN_CREATE) == 0u)))
        return ASTRA_VFS_ERR_INVALID;
    *file = (AstraFile)ASTRA_FILE_INIT;
    if ((flags & ASTRA_VFS_OPEN_READ) != 0u)
        rights |= ASTRA_RIGHT_READ;
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        rights |= ASTRA_RIGHT_WRITE;
    if (filesystem_resolve_single(filesystem, path, rights, wire, &client)) {
        status = astra_vfs_open_mode(client, wire, flags, create_mode,
                                     &opened, &size, &kind);
        if (status == ASTRA_VFS_OK)
            goto opened;
        if (status != ASTRA_VFS_ERR_LOOP && status != ASTRA_VFS_ERR_NOT_DIR &&
            status != ASTRA_VFS_ERR_NOT_FOUND)
            return status;
        client = NULL;
    }
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
        (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        status = filesystem_locate_literal(
            filesystem, path, rights, NULL, wire, NULL, NULL, NULL, NULL,
            NULL);
        if (status == ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_EXISTS;
        if (status != ASTRA_VFS_ERR_NOT_FOUND)
            return status;
    }
    /*
     * Find an existing name before opening it. Some backends implement
     * truncate with a create-capable mode, so probing union members with an
     * open can manufacture a shadowing file on the first writable member.
     */
    status = filesystem_locate(filesystem, path, rights, &client, wire,
                               &kind, &size, &member, NULL, NULL);
    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        char target[ASTRA_VFS_PATH_MAX];

        status = astra_vfs_assign_destination(
            filesystem->_private_assigns, path, rights, 1,
            filesystem_client_for, filesystem, target, sizeof(target), wire,
            sizeof(wire), &client, &member);
        kind = ASTRA_VFS_KIND_FILE;
        size = 0u;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_open_mode(client, wire, flags, create_mode,
                                     &opened, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return status;
opened:
    file->_private_client = client;
    file->_private_read_at = filesystem->_private_read_at;
    file->_private_write_at = filesystem->_private_write_at;
    file->_private_file = opened;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = (uint16_t)member;
    return ASTRA_VFS_OK;
}

static uint32_t filesystem_open(AstraFilesystem *filesystem, const char *path,
                                uint32_t flags, AstraFile *file)
{
    return filesystem_open_mode(filesystem, path, flags,
                                ASTRA_VFS_MODE_DEFAULT, file);
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

static uint32_t filesystem_sync(AstraFile *file)
{
    return !file_valid(file) ? ASTRA_VFS_ERR_BAD_HANDLE :
                               astra_vfs_sync(file->_private_client,
                                              file->_private_file);
}

static uint32_t filesystem_truncate(AstraFile *file, uint64_t size)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = astra_vfs_truncate(file->_private_client, file->_private_file,
                                size);
    if (status == ASTRA_VFS_OK)
        file->_private_size = size;
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

static uint32_t filesystem_write_from(AstraFile *file, uint64_t offset,
                                      const void *buffer, uint32_t length,
                                      uint32_t *moved, uint64_t *position)
{
    const uint8_t *bytes = buffer;
    uint32_t total = 0u;
    uint32_t status = ASTRA_VFS_OK;
    uint64_t last = offset;

    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        position == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_WRITE) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    *moved = 0u;
    *position = offset;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;

        if (file->_private_write_at != NULL) {
            if (chunk > ASTRA_VFS_BULK_MAX)
                chunk = ASTRA_VFS_BULK_MAX;
            status = file->_private_write_at(
                file->_private_client, file->_private_file, offset + total,
                file->_private_flags & ASTRA_VFS_OPEN_APPEND,
                bytes + total, chunk, &part, &last);
        } else {
            if (chunk > ASTRA_VFS_IO_MAX)
                chunk = ASTRA_VFS_IO_MAX;
            status = astra_vfs_write_position(
                file->_private_client, file->_private_file, offset + total,
                bytes + total, chunk, &part, &last);
        }
        total += part;
        if (status != ASTRA_VFS_OK || part < chunk ||
            (file->_private_flags & ASTRA_VFS_OPEN_APPEND) != 0u)
            break;
    }
    if (last > file->_private_size)
        file->_private_size = last;
    *moved = total;
    *position = last;
    return status;
}

static uint32_t filesystem_write_at(AstraFile *file, uint64_t offset,
                                    const void *buffer, uint32_t length,
                                    uint32_t *moved)
{
    uint64_t position;

    return filesystem_write_from(file, offset, buffer, length, moved,
                                 &position);
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
    uint64_t position;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = filesystem_write_from(file, file->_private_offset, buffer, length,
                                   moved, &position);
    if (moved != NULL)
        file->_private_offset = position;
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

static uint32_t filesystem_locate_literal(
    AstraFilesystem *filesystem, const char *path, uint32_t rights,
    AstraVfsClient **client, char *wire, uint16_t *kind, uint64_t *size,
    uint32_t *member, const AstraAssign **found_assign, AstraVfsDirEntry *meta)
{
    const AstraAssign *assign = NULL;
    AstraVfsClient *serving = NULL;
    AstraVfsDirEntry found = {0};
    uint32_t found_member = 0u;
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_lstat(
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

static uint32_t filesystem_stat_common(AstraFilesystem *filesystem,
                                       const char *path, AstraFileInfo *info,
                                       int literal)
{
    char wire[ASTRA_VFS_PATH_MAX];
    AstraVfsDirEntry meta = {0};
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t member = 0u;
    uint32_t status;

    if (info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    status = literal ?
        filesystem_locate_literal(
            filesystem, path, ASTRA_RIGHT_READ, NULL, wire, &kind, &size,
            &member, NULL, &meta) :
        filesystem_locate(filesystem, path, ASTRA_RIGHT_READ, NULL, wire,
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

static uint32_t filesystem_stat(AstraFilesystem *filesystem, const char *path,
                                AstraFileInfo *info)
{
    return filesystem_stat_common(filesystem, path, info, 0);
}

static uint32_t filesystem_lstat(AstraFilesystem *filesystem,
                                 const char *path, AstraFileInfo *info)
{
    return filesystem_stat_common(filesystem, path, info, 1);
}

static uint32_t filesystem_mkdir_mode(AstraFilesystem *filesystem,
                                      const char *path, uint16_t create_mode)
{
    AstraVfsClient *client = NULL;
    char logical[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical), wire,
        sizeof(wire), &client, NULL);

    return status == ASTRA_VFS_OK ?
        astra_vfs_mkdir_mode(client, wire, create_mode) : status;
}

static uint32_t filesystem_mkdir(AstraFilesystem *filesystem,
                                 const char *path)
{
    return filesystem_mkdir_mode(filesystem, path, ASTRA_VFS_MODE_DEFAULT);
}

static uint32_t filesystem_unlink(AstraFilesystem *filesystem,
                                  const char *path)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = filesystem_locate_literal(
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
    char to_logical[ASTRA_VFS_PATH_MAX];
    char to_wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (filesystem_valid(filesystem) &&
        filesystem_resolve_single(filesystem, from, ASTRA_RIGHT_WRITE,
                                  from_wire, &from_client) &&
        filesystem_resolve_single(filesystem, to, ASTRA_RIGHT_WRITE, to_wire,
                                  &to_client) &&
        from_client == to_client) {
        status = astra_vfs_rename(from_client, from_wire, to_wire);
        if (status != ASTRA_VFS_ERR_NOT_DIR)
            return status;
    }

    status = filesystem_locate_literal(
        filesystem, from, ASTRA_RIGHT_WRITE, &from_client, from_wire, NULL,
        NULL, NULL, NULL, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, to, ASTRA_RIGHT_WRITE, 0,
        filesystem_client_for, filesystem, to_logical, sizeof(to_logical),
        to_wire, sizeof(to_wire), &to_client, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    if (from_client != to_client)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    return astra_vfs_rename(from_client, from_wire, to_wire);
}

static uint32_t filesystem_chmod(AstraFilesystem *filesystem,
                                 const char *path, uint16_t mode)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status = filesystem_locate(
        filesystem, path, ASTRA_RIGHT_WRITE, &client, wire, NULL, NULL, NULL,
        NULL, NULL);

    return status == ASTRA_VFS_OK ? astra_vfs_chmod(client, wire, mode) :
                                    status;
}

static uint32_t filesystem_readlink(AstraFilesystem *filesystem,
                                    const char *path, void *buffer,
                                    uint32_t capacity, uint32_t *length)
{
    char logical[ASTRA_VFS_PATH_MAX];
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (!filesystem_valid(filesystem) || path == NULL || buffer == NULL ||
        capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_resolve_links(
        filesystem->_private_assigns, path, ASTRA_RIGHT_READ, 0, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical));
    if (status != ASTRA_VFS_OK)
        return status;
    status = ASTRA_VFS_ERR_NOT_FOUND;
    for (uint32_t member = 0u; member < ASTRA_ASSIGN_MAX; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        char wire[ASTRA_VFS_PATH_MAX];
        uint32_t resolved = astra_assign_resolve(
            filesystem->_private_assigns, logical, ASTRA_RIGHT_READ, member,
            wire, sizeof(wire), &assign);

        if (resolved == ASTRA_VFS_ERR_NOT_FOUND)
            break;
        if (resolved != ASTRA_VFS_OK)
            continue;
        client = filesystem_client_for(assign, filesystem);
        if (client == NULL)
            continue;
        resolved = astra_vfs_readlink(client, wire, buffer, capacity, length);
        if (resolved == ASTRA_VFS_OK)
            return resolved;
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            status = resolved;
    }
    return status;
}

static uint32_t filesystem_symlink(const char *target,
                                   AstraFilesystem *filesystem,
                                   const char *path)
{
    AstraVfsClient *client = NULL;
    char logical[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!filesystem_valid(filesystem) || target == NULL || target[0] == '\0' ||
        path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical), wire,
        sizeof(wire), &client, NULL);
    return status == ASTRA_VFS_OK ? astra_vfs_symlink(client, target, wire) :
                                   status;
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
    directory->_private_client = opened.client;
    directory->_private_file = opened.file;
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
    reading.client = directory->_private_client;
    reading.file = directory->_private_file;
    reading.member = directory->_private_member;
    reading.worst = directory->_private_worst;
    reading.active = directory->_private_active;
    reading.done = directory->_private_done;
    status = astra_vfs_union_directory_read(&reading, raw, capacity, count,
                                            &found_member);
    directory->_private_cursor = reading.cursor;
    directory->_private_client = reading.client;
    directory->_private_file = reading.file;
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
    if (directory != NULL) {
        if (directory->_private_client != NULL &&
            directory->_private_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_close(directory->_private_client,
                                  directory->_private_file);
        *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    }
}

const AstraFilesystemLibraryV2 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR,
    ASTRA_FILESYSTEM_LIBRARY_ABI_MINOR,
    sizeof(AstraFilesystemLibraryV2),
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
    filesystem_sync,
    filesystem_truncate,
    astra_vfs_sync,
    astra_vfs_truncate,
    filesystem_open_mode,
    filesystem_mkdir_mode,
    filesystem_chmod,
    filesystem_readlink,
    astra_vfs_open_mode,
    astra_vfs_mkdir_mode,
    astra_vfs_chmod,
    astra_vfs_readlink,
    filesystem_attach_io,
    filesystem_lstat,
    filesystem_symlink,
    astra_vfs_symlink,
};
