#include <astra/filesystem_library.h>
#include <astra/ascii.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <string.h>

static int filesystem_valid(const AstraFilesystem *filesystem)
{
    return filesystem != NULL && filesystem->_private_assigns != NULL &&
           filesystem->_private_client_for != NULL;
}

/* The first component is a protected namespace entry, not a backend node. */
static int namespace_entry_name(const char *path,
                                char name[ASTRA_CAPABILITY_NAME_MAX])
{
    char rest[ASTRA_VFS_PATH_MAX];
    char normalized[ASTRA_VFS_PATH_MAX];

    return path != NULL &&
           astra_path_split(path, name, ASTRA_CAPABILITY_NAME_MAX, rest,
                            sizeof(rest)) == ASTRA_VFS_OK &&
           astra_path_normalise(rest, normalized, sizeof(normalized)) ==
           ASTRA_VFS_OK && normalized[0] == '\0';
}

/* Disk-backed conveniences retain their own grants but display their mount. */
static int root_link_target_for_assign(const AstraAssign *assign,
                                       char target[ASTRA_VFS_PATH_MAX])
{
    static const char *const names[] = {
        "APPS", "COMMANDS", "CONFIG", "CWD", "HOME", "LIBS", "LOCAL",
        "SERVICES", "STARTUP", "STORE", "TMP", "TRASH", "WORK"
    };
    size_t length;

    if (assign == NULL)
        return 0;
    if (strcmp(assign->name, "SYSTEM") == 0) {
        memcpy(target, "/dh0", sizeof("/dh0"));
        return 1;
    }
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(assign->name, names[i]) == 0) {
            length = strlen(assign->root);
            if (length + sizeof("/system/") > ASTRA_VFS_PATH_MAX)
                return 0;
            memcpy(target, "/system", sizeof("/system"));
            if (length != 0u) {
                target[sizeof("/system") - 1u] = '/';
                memcpy(target + sizeof("/system"), assign->root,
                       length + 1u);
            }
            return 1;
        }
    return 0;
}

/* Root links are namespace metadata; no writable backend owns their names. */
static int root_link_target(const AstraAssignTable *assigns,
                            const char *path,
                            char target[ASTRA_VFS_PATH_MAX])
{
    char name[ASTRA_CAPABILITY_NAME_MAX];

    if (path == NULL || path[0] != '/' ||
        !namespace_entry_name(path, name))
        return 0;
    for (const char *at = path + 1; *at != '\0'; ++at)
        if (*at == '/')
            return 0;
    return root_link_target_for_assign(astra_assign_lookup(assigns, name),
                                       target);
}

static int file_valid(const AstraFile *file)
{
    return file != NULL && file->_private_client != NULL &&
           file->_private_file != ASTRA_VFS_FILE_INVALID;
}

#define FILE_MEMBER_MASK UINT16_C(0x00ff)
#define FILE_RIGHTS_SHIFT 8u

static uint16_t file_authority(uint32_t member, uint32_t rights)
{
    return (uint16_t)(member & FILE_MEMBER_MASK) |
           (uint16_t)((rights & UINT32_C(0xff)) << FILE_RIGHTS_SHIFT);
}

static uint16_t file_member(const AstraFile *file)
{
    return file->_private_member & FILE_MEMBER_MASK;
}

static uint32_t file_rights(const AstraFile *file)
{
    return file->_private_member >> FILE_RIGHTS_SHIFT;
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
                                     char *wire, AstraVfsClient **client,
                                     const AstraAssign **resolved_assign)
{
    const AstraAssign *assign = NULL;

    if (astra_assign_resolve(filesystem->_private_assigns, path, rights, 0u,
                             wire, ASTRA_VFS_PATH_MAX, &assign) !=
            ASTRA_VFS_OK ||
        astra_assign_member(filesystem->_private_assigns, assign->name, 1u) !=
            NULL)
        return 0;
    *client = filesystem_client_for(assign, filesystem);
    if (resolved_assign != NULL)
        *resolved_assign = assign;
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

uint32_t astra_filesystem_attach_io(AstraFilesystem *filesystem,
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

uint32_t astra_filesystem_attach(AstraFilesystem *filesystem,
                                 const AstraAssignTable *assigns,
                                 AstraVfsAssignClientFn client_for,
                                 AstraFilesystemReadAtFn read_at,
                                 void *context)
{
    return astra_filesystem_attach_io(filesystem, assigns, client_for,
                                      read_at, NULL, context);
}

void astra_filesystem_detach(AstraFilesystem *filesystem)
{
    if (filesystem != NULL)
        *filesystem = (AstraFilesystem)ASTRA_FILESYSTEM_INIT;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *filesystem,
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
    uint32_t granted_rights = 0u;
    uint32_t rights = 0u;
    uint32_t status;
    char wire[ASTRA_VFS_PATH_MAX];
    const AstraAssign *opened_assign = NULL;

    if (!filesystem_valid(filesystem) || path == NULL || file == NULL ||
        (flags & ~known) != 0u ||
        ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
         (flags & ASTRA_VFS_OPEN_CREATE) == 0u) ||
        (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u ||
        (create_mode != ASTRA_VFS_MODE_DEFAULT &&
         ((create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
          (flags & ASTRA_VFS_OPEN_CREATE) == 0u)))
        return ASTRA_VFS_ERR_INVALID;
    if ((flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        char entry[ASTRA_CAPABILITY_NAME_MAX];

        if (namespace_entry_name(path, entry) &&
            astra_assign_lookup(filesystem->_private_assigns, entry) == NULL)
            return ASTRA_VFS_ERR_READ_ONLY;
    }
    *file = (AstraFile)ASTRA_FILE_INIT;
    if ((flags & ASTRA_VFS_OPEN_READ) != 0u)
        rights |= ASTRA_RIGHT_READ;
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        rights |= ASTRA_RIGHT_WRITE;
    if (filesystem_resolve_single(filesystem, path, rights, wire, &client,
                                  &opened_assign)) {
        status = astra_vfs_open_mode(client, wire, flags, create_mode,
                                     &opened, &size, &kind);
        if (status == ASTRA_VFS_OK) {
            granted_rights = opened_assign->rights;
            goto opened;
        }
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
                               &kind, &size, &member, &opened_assign, NULL);
    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        char target[ASTRA_VFS_PATH_MAX];

        status = astra_vfs_assign_destination(
            filesystem->_private_assigns, path, rights, 1,
            filesystem_client_for, filesystem, target, sizeof(target), wire,
            sizeof(wire), &client, &member);
        if (status == ASTRA_VFS_OK) {
            const AstraAssign *assign = NULL;

            if (astra_assign_resolve(filesystem->_private_assigns, target,
                                     rights, member, wire, sizeof(wire),
                                     &assign) != ASTRA_VFS_OK ||
                assign == NULL)
                return ASTRA_VFS_ERR_ACCESS;
            opened_assign = assign;
        }
        kind = ASTRA_VFS_KIND_FILE;
        size = 0u;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_open_mode(client, wire, flags, create_mode,
                                     &opened, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return status;
    if (opened_assign != NULL)
        granted_rights = opened_assign->rights;
opened:
    file->_private_client = client;
    file->_private_read_at = filesystem->_private_read_at;
    file->_private_write_at = filesystem->_private_write_at;
    file->_private_file = opened;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = file_authority(member, granted_rights);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    return astra_filesystem_open_mode(filesystem, path, flags,
                                      ASTRA_VFS_MODE_DEFAULT, file);
}

uint32_t astra_filesystem_open_at_mode(const AstraFile *directory,
                                       const char *path, uint32_t flags,
                                       uint16_t create_mode, AstraFile *file)
{
    uint32_t rights = 0u;
    uint32_t status;
    AstraVfsFile opened = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;

    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || file == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((flags & ASTRA_VFS_OPEN_READ) != 0u)
        rights |= ASTRA_RIGHT_READ;
    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        rights |= ASTRA_RIGHT_WRITE;
    if ((file_rights(directory) & rights) != rights)
        return ASTRA_VFS_ERR_ACCESS;
    *file = (AstraFile)ASTRA_FILE_INIT;
    status = astra_vfs_open_at_mode(
        directory->_private_client, directory->_private_file, path, flags,
        create_mode, &opened, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return status;
    file->_private_client = directory->_private_client;
    file->_private_read_at = directory->_private_read_at;
    file->_private_write_at = directory->_private_write_at;
    file->_private_file = opened;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = directory->_private_member;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open_at(const AstraFile *directory,
                                  const char *path, uint32_t flags,
                                  AstraFile *file)
{
    return astra_filesystem_open_at_mode(directory, path, flags,
                                         ASTRA_VFS_MODE_DEFAULT, file);
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = astra_vfs_close(file->_private_client, file->_private_file);
    *file = (AstraFile)ASTRA_FILE_INIT;
    return status;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{
    return !file_valid(file) ? ASTRA_VFS_ERR_BAD_HANDLE :
                               astra_vfs_sync(file->_private_client,
                                              file->_private_file);
}

uint32_t astra_filesystem_truncate(AstraFile *file, uint64_t size)
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

uint32_t astra_filesystem_read_at(AstraFile *file, uint64_t offset,
                                  void *buffer, uint32_t length,
                                  uint32_t *moved)
{
    uint8_t *bytes = buffer;
    uint32_t total = 0u;
    uint32_t status = ASTRA_VFS_OK;

    if (moved != NULL)
        *moved = 0u;
    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_READ) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
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
                                      uint32_t flags, const void *buffer,
                                      uint32_t length, uint32_t *moved,
                                      uint64_t *position)
{
    const uint8_t *bytes = buffer;
    uint32_t total = 0u;
    uint32_t status = ASTRA_VFS_OK;
    uint64_t last = offset;

    if (moved != NULL)
        *moved = 0u;
    if (position != NULL)
        *position = offset;
    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        position == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_WRITE) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;

        if (file->_private_write_at != NULL) {
            if (chunk > ASTRA_VFS_BULK_MAX)
                chunk = ASTRA_VFS_BULK_MAX;
            status = file->_private_write_at(
                file->_private_client, file->_private_file, offset + total,
                flags,
                bytes + total, chunk, &part, &last);
        } else {
            if (chunk > ASTRA_VFS_IO_MAX)
                chunk = ASTRA_VFS_IO_MAX;
            status = astra_vfs_write_position(
                file->_private_client, file->_private_file, offset + total,
                flags, bytes + total, chunk, &part, &last);
        }
        total += part;
        if (status != ASTRA_VFS_OK || part < chunk ||
            (flags & ASTRA_VFS_WRITE_APPEND) != 0u)
            break;
    }
    if (last > file->_private_size)
        file->_private_size = last;
    *moved = total;
    *position = last;
    return status;
}

uint32_t astra_filesystem_write_at(AstraFile *file, uint64_t offset,
                                   const void *buffer, uint32_t length,
                                   uint32_t *moved)
{
    uint64_t position;

    return filesystem_write_from(file, offset, 0u, buffer, length, moved,
                                 &position);
}

uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                               uint32_t length, uint32_t *moved)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = astra_filesystem_read_at(file, file->_private_offset, buffer,
                                      length, moved);
    if (moved != NULL)
        file->_private_offset += *moved;
    return status;
}

uint32_t astra_filesystem_write(AstraFile *file, const void *buffer,
                                uint32_t length, uint32_t *moved)
{
    uint32_t status;
    uint64_t position;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = filesystem_write_from(
        file, file->_private_offset,
        (file->_private_flags & ASTRA_VFS_OPEN_APPEND) != 0u ?
            ASTRA_VFS_WRITE_APPEND : 0u,
        buffer, length, moved, &position);
    if (moved != NULL)
        file->_private_offset = position;
    return status;
}

uint32_t astra_filesystem_seek(AstraFile *file, int64_t delta,
                               uint32_t origin, uint64_t *offset)
{
    AstraVfsDirEntry meta = {0};
    uint64_t base;
    uint64_t result;
    uint32_t status;

    if (!file_valid(file) || offset == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (origin == ASTRA_FILE_SEEK_BEGIN)
        base = 0u;
    else if (origin == ASTRA_FILE_SEEK_CURRENT)
        base = file->_private_offset;
    else if (origin == ASTRA_FILE_SEEK_END) {
        status = astra_vfs_stat_file_meta(file->_private_client,
                                          file->_private_file, &meta);
        if (status != ASTRA_VFS_OK)
            return status;
        base = meta.size;
        file->_private_size = base;
    }
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

uint32_t astra_filesystem_file_info(const AstraFile *file,
                                    AstraFileInfo *info)
{
    AstraVfsDirEntry meta = {0};
    uint32_t status;

    if (!file_valid(file) || info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_stat_file_meta(file->_private_client,
                                      file->_private_file, &meta);
    if (status != ASTRA_VFS_OK)
        return status;
    *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    info->open_flags = file->_private_flags;
    info->byte_size = meta.size;
    info->offset = file->_private_offset;
    info->kind = meta.kind;
    info->member = file_member(file);
    info->mtime = meta.mtime;
    info->uid = meta.uid;
    info->gid = meta.gid;
    info->mode = meta.mode;
    info->nlink = meta.nlink;
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

    if (!filesystem_valid(filesystem) || path == NULL || info == NULL ||
        info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0) {
        *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0555u;
        info->nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (literal) {
        char target[ASTRA_VFS_PATH_MAX];

        if (root_link_target(filesystem->_private_assigns, path, target)) {
            *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
            info->byte_size = strlen(target);
            info->kind = ASTRA_VFS_KIND_SYMLINK;
            info->mode = 0777u;
            info->nlink = 1u;
            return ASTRA_VFS_OK;
        }
    }
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

uint32_t astra_filesystem_stat(AstraFilesystem *filesystem, const char *path,
                               AstraFileInfo *info)
{
    return filesystem_stat_common(filesystem, path, info, 0);
}

uint32_t astra_filesystem_lstat(AstraFilesystem *filesystem,
                                const char *path, AstraFileInfo *info)
{
    return filesystem_stat_common(filesystem, path, info, 1);
}

uint32_t astra_filesystem_stat_at(const AstraFile *directory,
                                  const char *path, AstraFileInfo *info)
{
    AstraVfsDirEntry meta = {0};
    uint32_t status;

    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    status = astra_vfs_stat_at_meta(directory->_private_client,
                                    directory->_private_file, path, &meta);
    if (status != ASTRA_VFS_OK)
        return status;
    *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    info->byte_size = meta.size;
    info->kind = meta.kind;
    info->member = file_member(directory);
    info->mtime = meta.mtime;
    info->uid = meta.uid;
    info->gid = meta.gid;
    info->mode = meta.mode;
    info->nlink = meta.nlink;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *filesystem,
                                     const char *path, uint16_t create_mode)
{
    AstraVfsClient *client = NULL;
    char logical[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0)
        return ASTRA_VFS_ERR_EXISTS;
    {
        char entry[ASTRA_CAPABILITY_NAME_MAX];

        if (namespace_entry_name(path, entry))
            return astra_assign_lookup(filesystem->_private_assigns, entry) !=
                           NULL ? ASTRA_VFS_ERR_EXISTS :
                                  ASTRA_VFS_ERR_READ_ONLY;
    }
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical), wire,
        sizeof(wire), &client, NULL);

    return status == ASTRA_VFS_OK ?
        astra_vfs_mkdir_mode(client, wire, create_mode) : status;
}

uint32_t astra_filesystem_mkdir(AstraFilesystem *filesystem,
                                const char *path)
{
    return astra_filesystem_mkdir_mode(filesystem, path,
                                       ASTRA_VFS_MODE_DEFAULT);
}

uint32_t astra_filesystem_unlink(AstraFilesystem *filesystem,
                                 const char *path)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    char entry[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0 || namespace_entry_name(path, entry))
        return ASTRA_VFS_ERR_READ_ONLY;
    status = filesystem_locate_literal(
        filesystem, path, ASTRA_RIGHT_WRITE, &client, wire, NULL, NULL, NULL,
        NULL, NULL);

    return status == ASTRA_VFS_OK ? astra_vfs_unlink(client, wire) : status;
}

uint32_t astra_filesystem_unlink_at(const AstraFile *directory,
                                    const char *path, uint32_t flags)
{
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((file_rights(directory) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    return astra_vfs_unlink_at(directory->_private_client,
                               directory->_private_file, path, flags);
}

typedef uint32_t (*FilesystemTwoPathOperation)(AstraVfsClient *, const char *,
                                               const char *);

static uint32_t filesystem_two_paths(AstraFilesystem *filesystem,
                                     const char *from, const char *to,
                                     FilesystemTwoPathOperation operation)
{
    AstraVfsClient *from_client = NULL;
    AstraVfsClient *to_client = NULL;
    char from_wire[ASTRA_VFS_PATH_MAX];
    char to_logical[ASTRA_VFS_PATH_MAX];
    char to_wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;
    char entry[ASTRA_CAPABILITY_NAME_MAX];

    if (!filesystem_valid(filesystem) || from == NULL || to == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(from, "/") == 0 || strcmp(to, "/") == 0 ||
        namespace_entry_name(from, entry) || namespace_entry_name(to, entry))
        return ASTRA_VFS_ERR_READ_ONLY;

    if (filesystem_valid(filesystem) &&
        filesystem_resolve_single(filesystem, from, ASTRA_RIGHT_WRITE,
                                  from_wire, &from_client, NULL) &&
        filesystem_resolve_single(filesystem, to, ASTRA_RIGHT_WRITE, to_wire,
                                  &to_client, NULL) &&
        from_client == to_client) {
        status = operation(from_client, from_wire, to_wire);
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
    return operation(from_client, from_wire, to_wire);
}

uint32_t astra_filesystem_rename(AstraFilesystem *filesystem,
                                 const char *from, const char *to)
{
    return filesystem_two_paths(filesystem, from, to, astra_vfs_rename);
}

uint32_t astra_filesystem_link(AstraFilesystem *filesystem,
                               const char *from, const char *to)
{
    return filesystem_two_paths(filesystem, from, to, astra_vfs_link);
}

uint32_t astra_filesystem_chmod(AstraFilesystem *filesystem,
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

uint32_t astra_filesystem_chmod_file(AstraFile *file, uint16_t mode)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if ((file_rights(file) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    status = astra_vfs_chmod_file(file->_private_client, file->_private_file,
                                  mode);
    return status;
}

uint32_t astra_filesystem_chmod_at(const AstraFile *directory,
                                   const char *path, uint16_t mode,
                                   uint32_t flags)
{
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((file_rights(directory) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    return astra_vfs_chmod_at(directory->_private_client,
                              directory->_private_file, path, mode, flags);
}

uint32_t astra_filesystem_capacity_path(AstraFilesystem *filesystem,
                                        const char *path,
                                        AstraFilesystemInfo *info)
{
    AstraVfsClient *client = NULL;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_locate(filesystem, path, ASTRA_RIGHT_READ, &client,
                               wire, NULL, NULL, NULL, NULL, NULL);
    return status == ASTRA_VFS_OK ?
        astra_vfs_filesystem_info_path(client, wire, info) : status;
}

uint32_t astra_filesystem_capacity_file(const AstraFile *file,
                                        AstraFilesystemInfo *info)
{
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return astra_vfs_filesystem_info_file(file->_private_client,
                                           file->_private_file, info);
}

uint32_t astra_filesystem_readlink(AstraFilesystem *filesystem,
                                   const char *path, void *buffer,
                                   uint32_t capacity, uint32_t *length)
{
    char logical[ASTRA_VFS_PATH_MAX];
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (!filesystem_valid(filesystem) || path == NULL || buffer == NULL ||
        capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    {
        char target[ASTRA_VFS_PATH_MAX];

        if (root_link_target(filesystem->_private_assigns, path, target)) {
            *length = (uint32_t)strlen(target);
            if (*length > capacity)
                return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
            memcpy(buffer, target, *length);
            return ASTRA_VFS_OK;
        }
    }
    status = astra_vfs_assign_resolve_links(
        filesystem->_private_assigns, path, ASTRA_RIGHT_READ, 0, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical));
    if (status != ASTRA_VFS_OK)
        return status;
    status = ASTRA_VFS_ERR_NOT_FOUND;
    for (uint32_t member = 0u;
         member < filesystem->_private_assigns->count; ++member) {
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

uint32_t astra_filesystem_symlink(const char *target,
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
    {
        char entry[ASTRA_CAPABILITY_NAME_MAX];

        if (namespace_entry_name(path, entry))
            return astra_assign_lookup(filesystem->_private_assigns, entry) !=
                           NULL ? ASTRA_VFS_ERR_EXISTS :
                                  ASTRA_VFS_ERR_READ_ONLY;
    }
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE, 0,
        filesystem_client_for, filesystem, logical, sizeof(logical), wire,
        sizeof(wire), &client, NULL);
    return status == ASTRA_VFS_OK ? astra_vfs_symlink(client, target, wire) :
                                   status;
}

#define DIRECTORY_SINGLE_BACKEND UINT16_C(1)
#define DIRECTORY_VIRTUAL_ROOT UINT16_C(2)

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
                                         const char *path,
                                         AstraDirectory *directory)
{
    AstraVfsUnionDirectory opened = ASTRA_VFS_UNION_DIRECTORY_INIT;
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL || directory == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    if (strcmp(path, "/") == 0) {
        directory->_private_filesystem = filesystem;
        directory->_private_active = 1u;
        directory->_private_reserved = DIRECTORY_VIRTUAL_ROOT;
        return ASTRA_VFS_OK;
    }
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

uint32_t astra_filesystem_directory_from_file(const AstraFile *file,
                                              AstraDirectory *directory)
{
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (directory == NULL || file->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    directory->_private_cursor = 0u;
    directory->_private_client = file->_private_client;
    directory->_private_file = file->_private_file;
    directory->_private_member = file_member(file);
    directory->_private_active = 1u;
    directory->_private_reserved = DIRECTORY_SINGLE_BACKEND;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
                                         AstraDirectoryEntry *entries,
                                         uint32_t capacity,
                                         uint32_t *count)
{
    AstraVfsUnionDirectory reading = ASTRA_VFS_UNION_DIRECTORY_INIT;
    AstraFilesystem *filesystem;
    uint32_t found_member = 0u;
    uint32_t status;

    if (directory == NULL || entries == NULL || capacity == 0u ||
        count == NULL || directory->_private_active == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((directory->_private_reserved & DIRECTORY_VIRTUAL_ROOT) != 0u) {
        const AstraAssignTable *assigns =
            directory->_private_filesystem->_private_assigns;
        uint32_t at = (uint32_t)directory->_private_cursor;

        *count = 0u;
        while (at < assigns->count && *count < capacity) {
            const AstraAssign *assign = &assigns->entries[at];
            char target[ASTRA_VFS_PATH_MAX];
            uint32_t earlier;

            for (earlier = 0u; earlier < at; ++earlier)
                if (strcmp(assigns->entries[earlier].name, assign->name) == 0)
                    break;
            ++at;
            if (earlier + 1u != at)
                continue;
            entries[*count] = (AstraDirectoryEntry){0};
            for (uint32_t i = 0u; i + 1u < sizeof(entries[*count].name) &&
                                   assign->name[i] != '\0'; ++i)
                entries[*count].name[i] = astra_ascii_lower(assign->name[i]);
            if (root_link_target_for_assign(assign, target)) {
                entries[*count].kind = ASTRA_VFS_KIND_SYMLINK;
                entries[*count].mode = 0777u;
            } else {
                entries[*count].kind = ASTRA_VFS_KIND_DIRECTORY;
                entries[*count].mode = 0555u;
            }
            entries[*count].nlink = 1u;
            ++*count;
        }
        directory->_private_cursor = at;
        return ASTRA_VFS_OK;
    }
    if ((directory->_private_reserved & DIRECTORY_SINGLE_BACKEND) != 0u) {
        if (directory->_private_done != 0u) {
            *count = 0u;
            return ASTRA_VFS_OK;
        }
        status = astra_vfs_readdir_file_batch(
            directory->_private_client, directory->_private_file, "",
            directory->_private_cursor, entries, capacity, count,
            &directory->_private_cursor);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            *count = 0u;
            directory->_private_done = 1u;
            return ASTRA_VFS_OK;
        }
        if (status != ASTRA_VFS_OK)
            return status;
        if (directory->_private_cursor == 0u)
            directory->_private_done = 1u;
        for (uint32_t index = 0u; index < *count; ++index)
            entries[index].member = (uint16_t)directory->_private_member;
        return ASTRA_VFS_OK;
    }
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
    status = astra_vfs_union_directory_read(&reading, entries, capacity, count,
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
    for (uint32_t index = 0u; index < *count; ++index)
        entries[index].member = (uint16_t)found_member;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_rewind(AstraDirectory *directory)
{
    if (directory == NULL || directory->_private_active == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((directory->_private_reserved & (DIRECTORY_SINGLE_BACKEND |
                                         DIRECTORY_VIRTUAL_ROOT)) != 0u) {
        directory->_private_cursor = 0u;
        directory->_private_done = 0u;
        return ASTRA_VFS_OK;
    }
    if (directory->_private_client != NULL &&
        directory->_private_file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_close(directory->_private_client,
                              directory->_private_file);
    directory->_private_cursor = 0u;
    directory->_private_client = NULL;
    directory->_private_file = ASTRA_VFS_FILE_INVALID;
    directory->_private_member = 0u;
    directory->_private_worst = ASTRA_VFS_ERR_NOT_FOUND;
    directory->_private_done = 0u;
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{
    if (directory != NULL) {
        if ((directory->_private_reserved & DIRECTORY_SINGLE_BACKEND) == 0u &&
            directory->_private_client != NULL &&
            directory->_private_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_close(directory->_private_client,
                                  directory->_private_file);
        *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    }
}
