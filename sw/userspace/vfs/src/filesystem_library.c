#include <astra/filesystem_library.h>
#include <astra/vfs_path_storage.h>
#include <astra/runtime.h>
#include <astra/ascii.h>
#include <astra/vfs_path.h>
#include <astra/vfs_path_payload.h>
#include <astra/vfs_file_lifetime.h>

#include <stddef.h>
#include <string.h>

static int filesystem_valid(const AstraFilesystem *filesystem)
{
    return filesystem != NULL && filesystem->_private_assigns != NULL &&
           filesystem->_private_client_for != NULL;
}

/* The first component is a protected namespace entry, not a backend node. */
static int namespace_entry_name(const char *path,
                                char name[ASTRA_ASSIGN_NAME_MAX])
{
    char root[ASTRA_ASSIGN_NAME_MAX + 2u] = "/";
    char rest[1];
    uint32_t at = 1u;

    if (path == NULL || path[0] != '/')
        return 0;
    while (path[at] != '\0' && path[at] != '/') {
        if (at + 1u >= sizeof(root))
            return 0;
        root[at] = path[at];
        ++at;
    }
    root[at] = '\0';
    while (path[at] == '/' && at + 1u < ASTRA_VFS_PATH_MAX)
        ++at;
    return path[at] == '\0' &&
        astra_path_split(root, name, ASTRA_ASSIGN_NAME_MAX, rest,
                          sizeof(rest)) == ASTRA_VFS_OK;
}

/* Disk-backed conveniences retain their own grants but display their mount. */
static int root_link_target_for_assign(const AstraAssign *assign,
                                       char target[ASTRA_VFS_PATH_MAX])
{
    static const char *const names[] = {
        "APPS", "COMMANDS", "CONFIG", "CWD", "FONTS", "HOME", "LIBS", "LOCAL",
        "SERVICES", "STARTUP", "STORE", "TMP", "TRASH", "WORK"
    };
    size_t length;

    if (assign == NULL)
        return 0;
    if (strcmp(assign->name, "SYSTEM") == 0) {
        memcpy(target, "/dh0", sizeof("/dh0"));
        return 1;
    }
    /* A convenience on the system volume names a directory there. One bound
       at a root is some volume's own root -- hostfs serving WORK -- and is a
       mount, not a link into /system. */
    if (assign->root[0] == '\0')
        return 0;
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(assign->name, names[i]) == 0) {
            length = strlen(assign->root);
            if (length + sizeof("/system/") > ASTRA_VFS_PATH_MAX)
                return 0;
            memcpy(target, "/system/", sizeof("/system/") - 1u);
            memcpy(target + sizeof("/system/") - 1u, assign->root,
                   length + 1u);
            return 1;
        }
    return 0;
}

/* Root links are namespace metadata; no writable backend owns their names. */
static int root_link_target(const AstraAssignTable *assigns,
                            const char *path,
                            char target[ASTRA_VFS_PATH_MAX])
{
    char name[ASTRA_ASSIGN_NAME_MAX];

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

static uint32_t file_owner_status(const AstraFile *file)
{
    return !file_valid(file) ? ASTRA_VFS_ERR_BAD_HANDLE :
        astra_vfs_file_owner_status(file->_private_client, file->_private_ownership_epoch);
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

static uint32_t directory_authority(const AstraFile *directory, uint32_t rights)
{
    if (file_owner_status(directory) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    return (file_rights(directory) & rights) == rights ? ASTRA_VFS_OK : ASTRA_VFS_ERR_ACCESS;
}

static uint32_t filesystem_open_rights(uint32_t flags)
{
    return ((flags & ASTRA_VFS_OPEN_READ) ? ASTRA_RIGHT_READ : 0u) |
        ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE)) ?
            ASTRA_RIGHT_WRITE : 0u);
}

static int filesystem_open_valid(uint32_t flags, uint16_t create_mode)
{
    return (flags & ~ASTRA_VFS_OPEN_MASK) == 0u &&
        ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) == 0u || (flags & ASTRA_VFS_OPEN_CREATE) != 0u) &&
        (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) != 0u &&
        (create_mode == ASTRA_VFS_MODE_DEFAULT ||
         ((create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) == 0u && (flags & ASTRA_VFS_OPEN_CREATE) != 0u));
}


static int relative_path(const char *path)
{
    return path != NULL && path[0] != '/';
}

/* A relative operation pins the process directory until its provider call ends. */
static uint32_t filesystem_relative(AstraFilesystem *filesystem, const char *path,
    uint32_t rights, char qualified[ASTRA_VFS_PATH_MAX], const AstraFile **directory)
{
    uint32_t status;

    *directory = NULL;
    if (!filesystem_valid(filesystem) || path == NULL || path[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    if (!relative_path(path))
        return ASTRA_VFS_OK;
    if (filesystem->_private_cwd_acquire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem->_private_cwd_acquire(directory);
    if (status != ASTRA_VFS_OK)
        return status;
    if (*directory != NULL) {
        if ((file_rights(*directory) & rights) == rights)
            return ASTRA_VFS_OK;
        status = ASTRA_VFS_ERR_ACCESS;
        *directory = NULL;
    } else
        status = filesystem->_private_cwd_qualify(path, qualified, ASTRA_VFS_PATH_MAX);
    filesystem->_private_cwd_release();
    return status;
}

static uint32_t filesystem_relative_done(AstraFilesystem *filesystem,
    const AstraFile *directory, const char *path, uint32_t status,
    char qualified[ASTRA_VFS_PATH_MAX])
{
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        uint32_t resolved = filesystem->_private_cwd_qualify(path, qualified, ASTRA_VFS_PATH_MAX);
        if (resolved != ASTRA_VFS_OK)
            status = resolved;
    }
    if (directory != NULL)
        filesystem->_private_cwd_release();
    return status;
}

static AstraVfsClient *filesystem_client_for(const AstraAssign *assign,
                                             void *context)
{
    AstraFilesystem *filesystem = context;

    return filesystem->_private_client_for(assign,
                                           filesystem->_private_context);
}

static uint32_t filesystem_at_destination(AstraFilesystem *filesystem,
    const AstraFile *directory, const char *path, uint32_t rights, uint32_t flags,
    AstraVfsClient **client, AstraVfsFile *base, char wire[ASTRA_VFS_PATH_MAX],
    const AstraAssign **assign, uint32_t *member, AstraVfsDirEntry *entry)
{
    uint32_t status = directory_authority(directory, rights);

    if (status != ASTRA_VFS_OK)
        return status;
    if (!filesystem_valid(filesystem))
        return ASTRA_VFS_ERR_INVALID;
    return astra_vfs_assign_destination_at(filesystem->_private_assigns,
        directory->_private_client, directory->_private_file, path, rights, flags,
        filesystem_client_for, filesystem, wire, ASTRA_VFS_PATH_MAX, client, base,
        assign, member, entry);
}

static int filesystem_resolve_single(AstraFilesystem *filesystem,
                                     const char *path, uint32_t rights,
                                     char *wire, AstraVfsClient **client,
                                     const AstraAssign **resolved_assign)
{
    const AstraAssign *assign = NULL;

    if (astra_path_requires_walk(path))
        return 0;
    if (astra_assign_resolve_raw(filesystem->_private_assigns, path, rights, 0u,
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
                                    AstraFilesystemReadFn read_at,
                                    AstraFilesystemWriteFn write_at,
                                    void *context)
{
    if (filesystem == NULL || assigns == NULL || client_for == NULL)
        return ASTRA_VFS_ERR_INVALID;
    filesystem->_private_assigns = assigns;
    filesystem->_private_client_for = client_for;
    filesystem->_private_read = read_at;
    filesystem->_private_write = write_at;
    filesystem->_private_context = context;
    filesystem->_private_cwd_acquire = NULL;
    filesystem->_private_cwd_release = NULL;
    filesystem->_private_cwd_qualify = NULL;
    filesystem->_private_namespace_acquire = NULL;
    filesystem->_private_namespace_release = NULL;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_attach(AstraFilesystem *filesystem,
                                 const AstraAssignTable *assigns,
                                 AstraVfsAssignClientFn client_for,
                                 AstraFilesystemReadFn read_at,
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

static uint32_t astra_filesystem_open_mode_bound(AstraFilesystem *filesystem,
                                    const char *path, uint32_t flags,
                                    uint16_t create_mode, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(3u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_rights = filesystem_open_rights(flags);
    uint32_t relative_status = filesystem_relative(filesystem, path, relative_rights, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_open_at_mode(filesystem, directory, path, flags, create_mode, file);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_open_mode(filesystem, qualified, flags, create_mode, file);
    }
    if (relative_path(path))
        path = qualified;

    AstraVfsClient *client = NULL;
    AstraVfsFile opened = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t member = 0u;
    uint32_t granted_rights = 0u;
    uint32_t rights = 0u;
    uint32_t status;
    char *wire = astra_vfs_path_storage_at(&path_storage, 1u);
    const AstraAssign *opened_assign = NULL;

    if (!filesystem_valid(filesystem) || path == NULL || file == NULL ||
        !filesystem_open_valid(flags, create_mode))
        return ASTRA_VFS_ERR_INVALID;
    if ((flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        char entry[ASTRA_ASSIGN_NAME_MAX];

        if (namespace_entry_name(path, entry) &&
            astra_assign_lookup(filesystem->_private_assigns, entry) == NULL)
            return ASTRA_VFS_ERR_READ_ONLY;
    }
    *file = (AstraFile)ASTRA_FILE_INIT;
    rights = relative_rights;
    if (filesystem_resolve_single(filesystem, path, rights, wire, &client,
                                  &opened_assign)) {
        status = astra_vfs_open_mode(client, wire, flags, create_mode,
                                     &opened, &size, &kind);
        if (status == ASTRA_VFS_OK) {
            granted_rights = opened_assign->rights;
            goto opened;
        }
        /* Continue links and provider escapes through the shared namespace. */
        if (status == ASTRA_VFS_ERR_LOOP && (flags & ASTRA_VFS_OPEN_NOFOLLOW))
            return status;
        if (status != ASTRA_VFS_ERR_LOOP && status != ASTRA_VFS_ERR_NOT_DIR &&
            status != ASTRA_VFS_ERR_ESCAPED &&
            (status != ASTRA_VFS_ERR_NOT_FOUND ||
             (flags & ASTRA_VFS_OPEN_CREATE) == 0u))
            return status;
        client = NULL;
    }
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
        (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        status = filesystem_locate_literal(
            filesystem, path, rights, NULL, wire, NULL, NULL, NULL, NULL,
            NULL);
        if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_ESCAPED)
            return ASTRA_VFS_ERR_EXISTS;
        if (status != ASTRA_VFS_ERR_NOT_FOUND)
            return status;
    }
    /*
     * Find an existing name before opening it. Some backends implement
     * truncate with a create-capable mode, so probing union members with an
     * open can manufacture a shadowing file on the first writable member.
     */
    status = (flags & ASTRA_VFS_OPEN_NOFOLLOW) ?
        filesystem_locate_literal(filesystem, path, rights, &client, wire,
                                   &kind, &size, &member, &opened_assign, NULL) :
        filesystem_locate(filesystem, path, rights, &client, wire,
                           &kind, &size, &member, &opened_assign, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_IS_DIR;
    if (status == ASTRA_VFS_OK && kind == ASTRA_VFS_KIND_SYMLINK &&
        (flags & ASTRA_VFS_OPEN_NOFOLLOW))
        return ASTRA_VFS_ERR_LOOP;
    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        char *target = astra_vfs_path_storage_at(&path_storage, 2u);

        status = astra_vfs_assign_destination(
            filesystem->_private_assigns, path, rights,
            ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_MISSING,
            filesystem_client_for, filesystem, target, ASTRA_VFS_PATH_MAX, wire,
            ASTRA_VFS_PATH_MAX, &client, &member);
        if (status == ASTRA_VFS_ERR_ESCAPED)
            return ASTRA_VFS_ERR_IS_DIR;
        if (status == ASTRA_VFS_OK) {
            const AstraAssign *assign = NULL;

            if (astra_assign_resolve_raw(filesystem->_private_assigns, target,
                                     rights, member, wire, ASTRA_VFS_PATH_MAX,
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
    file->_private_read = filesystem->_private_read;
    file->_private_write = filesystem->_private_write;
    file->_private_file = opened;
    file->_private_ownership_epoch = client->ownership_epoch;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = file_authority(member, granted_rights);
    return ASTRA_VFS_OK;
}

static uint32_t astra_filesystem_open_bound(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return astra_filesystem_open_mode(filesystem, path, flags,
                                      ASTRA_VFS_MODE_DEFAULT, file);
}

static uint32_t astra_filesystem_open_at_mode_bound(AstraFilesystem *filesystem, const AstraFile *directory,
                                       const char *path, uint32_t flags,
                                       uint16_t create_mode, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    uint32_t rights = 0u;
    uint32_t status;
    AstraVfsFile opened = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);

    if (file_owner_status(directory) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || file == NULL || !filesystem_open_valid(flags, create_mode))
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    rights = filesystem_open_rights(flags);
    if ((file_rights(directory) & rights) != rights)
        return ASTRA_VFS_ERR_ACCESS;
    *file = (AstraFile)ASTRA_FILE_INIT;
    status = filesystem_at_destination(filesystem, directory, path, rights,
        ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) ? ASTRA_VFS_LOOKUP_ENTRY :
         (flags & ASTRA_VFS_OPEN_NOFOLLOW) ? 0u : ASTRA_VFS_LOOKUP_FOLLOW) |
        ((flags & ASTRA_VFS_OPEN_CREATE) ? ASTRA_VFS_LOOKUP_MISSING : 0u),
        &client, &base, wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_IS_DIR;
    if (status != ASTRA_VFS_OK)
        return status;
    status = base != ASTRA_VFS_FILE_INVALID ?
        astra_vfs_open_at_mode(client, base, wire, flags, create_mode,
                               &opened, &size, &kind) :
        astra_vfs_open_mode(client, wire, flags, create_mode, &opened, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return status;
    file->_private_client = client;
    file->_private_read = filesystem->_private_read;
    file->_private_write = filesystem->_private_write;
    file->_private_file = opened;
    file->_private_ownership_epoch = client->ownership_epoch;
    file->_private_flags = flags;
    file->_private_size = size;
    file->_private_kind = kind;
    file->_private_member = assign != NULL ? file_authority(member, assign->rights) :
                                             directory->_private_member;
    return ASTRA_VFS_OK;
}

static uint32_t astra_filesystem_open_at_bound(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags,
                                  AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return astra_filesystem_open_at_mode(filesystem, directory, path, flags,
                                         ASTRA_VFS_MODE_DEFAULT, file);
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    uint32_t status;

    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = astra_vfs_file_owner_close(file->_private_client, file->_private_file,
        file->_private_ownership_epoch);
    *file = (AstraFile)ASTRA_FILE_INIT;
    return status;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{
    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    return !file_valid(file) ? ASTRA_VFS_ERR_BAD_HANDLE :
                               astra_vfs_sync(file->_private_client,
                                              file->_private_file);
}

uint32_t astra_filesystem_truncate(AstraFile *file, uint64_t size)
{
    uint32_t status;

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
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
    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file) || buffer == NULL || moved == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_READ) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;
        uint64_t position = offset + total;

        if (chunk > (file->_private_read != NULL ?
                     ASTRA_VFS_BULK_MAX : ASTRA_VFS_IO_MAX))
            chunk = file->_private_read != NULL ?
                    ASTRA_VFS_BULK_MAX : ASTRA_VFS_IO_MAX;
        if (file->_private_read != NULL)
            status = file->_private_read(
                file->_private_client, file->_private_file, offset + total,
                0u, bytes + total, chunk, &part, &position);
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
                                      uint32_t flags, uint64_t max_end, const void *buffer,
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
    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file) || (length != 0u && buffer == NULL) || moved == NULL ||
        position == NULL ||
        (file->_private_flags & ASTRA_VFS_OPEN_WRITE) == 0u ||
        (uint64_t)length > UINT64_MAX - offset)
        return ASTRA_VFS_ERR_INVALID;
    while (total < length) {
        uint32_t part = 0u;
        uint32_t chunk = length - total;

        if (file->_private_write != NULL) {
            if (chunk > ASTRA_VFS_BULK_MAX)
                chunk = ASTRA_VFS_BULK_MAX;
            status = file->_private_write(
                file->_private_client, file->_private_file, offset + total,
                flags, max_end,
                bytes + total, chunk, &part, &last);
        } else {
            if (chunk > ASTRA_VFS_IO_MAX)
                chunk = ASTRA_VFS_IO_MAX;
            status = astra_vfs_write_position(
                file->_private_client, file->_private_file, offset + total,
                flags, max_end, bytes + total, chunk, &part, &last);
        }
        total += part;
        if (status != ASTRA_VFS_OK || part < chunk ||
            (flags & (ASTRA_VFS_WRITE_APPEND | ASTRA_VFS_IO_SEQUENTIAL)) != 0u)
            break;
    }
    if (total != 0u && last > file->_private_size)
        file->_private_size = last;
    *moved = total;
    *position = last;
    return status;
}

uint32_t astra_filesystem_write_at(AstraFile *file, uint64_t offset, uint64_t max_end,
                                   const void *buffer, uint32_t length,
                                   uint32_t *moved)
{
    uint64_t position;

    return filesystem_write_from(file, offset, 0u, max_end, buffer, length, moved,
                                 &position);
}

uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                               uint32_t length, uint32_t *moved)
{
    uint32_t status;

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if ((file->_private_client->features &
         ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u) {
        uint64_t position = 0u;
        uint32_t chunk = file->_private_read != NULL ?
            ASTRA_VFS_BULK_MAX : ASTRA_VFS_IO_MAX;

        if (moved != NULL)
            *moved = 0u;
        if (buffer == NULL || moved == NULL ||
            (file->_private_flags & ASTRA_VFS_OPEN_READ) == 0u)
            return ASTRA_VFS_ERR_INVALID;
        if (length == 0u)
            return ASTRA_VFS_OK;
        if (chunk > length)
            chunk = length;
        status = file->_private_read != NULL ?
            file->_private_read(file->_private_client, file->_private_file,
                0u, ASTRA_VFS_IO_SEQUENTIAL, buffer, chunk, moved,
                &position) :
            astra_vfs_read_position(file->_private_client, file->_private_file,
                0u, ASTRA_VFS_IO_SEQUENTIAL, buffer, chunk, moved,
                &position);
        if (status == ASTRA_VFS_OK)
            file->_private_offset = position;
        return status;
    }
    status = astra_filesystem_read_at(file, file->_private_offset, buffer,
                                      length, moved);
    if (moved != NULL)
        file->_private_offset += *moved;
    return status;
}

uint32_t astra_filesystem_write(AstraFile *file, uint64_t max_end, const void *buffer,
                                uint32_t length, uint32_t *moved)
{
    uint32_t status;
    uint64_t position;

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    status = filesystem_write_from(
        file, file->_private_offset,
        (file->_private_client->features &
         ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u ? ASTRA_VFS_IO_SEQUENTIAL :
        (file->_private_flags & ASTRA_VFS_OPEN_APPEND) != 0u ?
            ASTRA_VFS_WRITE_APPEND : 0u, max_end,
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

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file) || offset == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if ((file->_private_client->features &
         ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u) {
        status = astra_vfs_seek(file->_private_client, file->_private_file,
                                delta, origin, offset);
        if (status == ASTRA_VFS_OK)
            file->_private_offset = *offset;
        return status;
    }
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

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
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
    if ((file->_private_client->features &
         ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u) {
        status = astra_vfs_file_flags(file->_private_client,
            file->_private_file, 0u, 0u, &info->open_flags);
        if (status == ASTRA_VFS_OK)
            status = astra_vfs_seek(file->_private_client,
                file->_private_file, 0, ASTRA_FILE_SEEK_CURRENT,
                &info->offset);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    info->kind = meta.kind;
    info->member = file_member(file);
    info->mtime = meta.mtime;
    info->uid = meta.uid;
    info->gid = meta.gid;
    info->mode = meta.mode;
    info->nlink = meta.nlink;
    info->meta = meta.meta;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_flags(AstraFile *file, uint32_t mask,
                                     uint32_t flags, uint32_t *result)
{
    uint32_t status;

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file) || result == NULL ||
        (mask & ~ASTRA_VFS_FILE_STATUS_MASK) != 0u || (flags & ~mask) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if ((file->_private_client->features &
         ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u) {
        status = astra_vfs_file_flags(file->_private_client,
            file->_private_file, mask, flags, result);
        if (status != ASTRA_VFS_OK)
            return status;
        file->_private_flags = *result;
    } else {
        file->_private_flags = (file->_private_flags & ~mask) | flags;
        *result = file->_private_flags;
    }
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
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(3u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_READ, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_stat_at(filesystem, directory, path, literal ? 0u : ASTRA_VFS_STAT_FOLLOW, info);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return filesystem_stat_common(filesystem, qualified, info, literal);
    }
    if (relative_path(path))
        path = qualified;

    char *wire = astra_vfs_path_storage_at(&path_storage, 1u);
    AstraVfsDirEntry meta = {0};
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t member = 0u;
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL || info == NULL ||
        info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
namespace_root:
    if (strcmp(path, "/") == 0) {
        *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        info->mode = 0555u;
        info->nlink = 1u;
        info->meta.valid = ASTRA_VFS_META_MODE | ASTRA_VFS_META_NLINK;
        info->meta.nlink = 1u;
        return ASTRA_VFS_OK;
    }
    if (literal) {
        char *target = astra_vfs_path_storage_at(&path_storage, 2u);

        if (root_link_target(filesystem->_private_assigns, path, target)) {
            *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
            info->byte_size = strlen(target);
            info->kind = ASTRA_VFS_KIND_SYMLINK;
            info->mode = 0777u;
            info->nlink = 1u;
            info->meta.valid = ASTRA_VFS_META_MODE | ASTRA_VFS_META_NLINK;
            info->meta.nlink = 1u;
            return ASTRA_VFS_OK;
        }
    }
    status = literal ?
        filesystem_locate_literal(
            filesystem, path, ASTRA_RIGHT_READ, NULL, wire, &kind, &size,
            &member, NULL, &meta) :
        filesystem_locate(filesystem, path, ASTRA_RIGHT_READ, NULL, wire,
                          &kind, &size, &member, NULL, &meta);
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        path = "/";
        goto namespace_root;
    }
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
    info->meta = meta.meta;
    return ASTRA_VFS_OK;
}

static uint32_t astra_filesystem_stat_bound(AstraFilesystem *filesystem, const char *path,
                               AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_stat_common(filesystem, path, info, 0);
}

static uint32_t astra_filesystem_lstat_bound(AstraFilesystem *filesystem,
                                const char *path, AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_stat_common(filesystem, path, info, 1);
}

static uint32_t astra_filesystem_stat_at_bound(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags, AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    AstraVfsDirEntry meta = {0};
    uint32_t status;
    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);

    if (file_owner_status(directory) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL || info == NULL || info->size < sizeof(*info))
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if (flags & ~ASTRA_VFS_STAT_FOLLOW)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_at_destination(filesystem, directory, path, ASTRA_RIGHT_READ,
        (flags & ASTRA_VFS_STAT_FOLLOW) ? ASTRA_VFS_LOOKUP_FOLLOW : 0u,
        &client, &base, wire, &assign, &member, &meta);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return astra_filesystem_stat(filesystem, "/", info);
    if (status != ASTRA_VFS_OK)
        return status;
    *info = (AstraFileInfo)ASTRA_FILE_INFO_INIT;
    info->byte_size = meta.size;
    info->kind = meta.kind;
    info->member = assign != NULL ? (uint16_t)member : file_member(directory);
    info->mtime = meta.mtime;
    info->uid = meta.uid;
    info->gid = meta.gid;
    info->mode = meta.mode;
    info->nlink = meta.nlink;
    info->meta = meta.meta;
    return ASTRA_VFS_OK;
}

static uint32_t astra_filesystem_mkdir_mode_bound(AstraFilesystem *filesystem,
                                     const char *path, uint16_t create_mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(3u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_WRITE, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_mkdir_at(filesystem, directory, path, create_mode);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_mkdir_mode(filesystem, qualified, create_mode);
    }
    if (relative_path(path))
        path = qualified;

    AstraVfsClient *client = NULL;
    char *logical = astra_vfs_path_storage_at(&path_storage, 1u);
    char *wire = astra_vfs_path_storage_at(&path_storage, 2u);
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0)
        return ASTRA_VFS_ERR_EXISTS;
    {
        char entry[ASTRA_ASSIGN_NAME_MAX];

        if (namespace_entry_name(path, entry))
            return astra_assign_lookup(filesystem->_private_assigns, entry) !=
                           NULL ? ASTRA_VFS_ERR_EXISTS :
                                  ASTRA_VFS_ERR_READ_ONLY;
    }
    status = astra_path_collapse(path, logical, ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK)
        return status;
    /* mkdir creates an entry; an existing final link means EXISTS. */
    for (uint32_t length = (uint32_t)strlen(logical);
         length > 1u && logical[length - 1u] == '/'; --length)
        logical[length - 1u] = '\0';
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, logical, ASTRA_RIGHT_WRITE,
        ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
        filesystem_client_for, filesystem, logical, ASTRA_VFS_PATH_MAX, wire,
        ASTRA_VFS_PATH_MAX, &client, NULL);

    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_EXISTS;
    return status == ASTRA_VFS_OK ?
        astra_vfs_mkdir_mode(client, wire, create_mode) : status;
}

static uint32_t astra_filesystem_mkdir_bound(AstraFilesystem *filesystem,
                                const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return astra_filesystem_mkdir_mode(filesystem, path,
                                       ASTRA_VFS_MODE_DEFAULT);
}

static uint32_t astra_filesystem_unlink_bound(AstraFilesystem *filesystem,
                                 const char *path, uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(3u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_WRITE, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_unlink_at(filesystem, directory, path, flags);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_unlink(filesystem, qualified, flags);
    }
    if (relative_path(path))
        path = qualified;

    AstraVfsClient *client = NULL;
    char *logical = astra_vfs_path_storage_at(&path_storage, 1u);
    char *wire = astra_vfs_path_storage_at(&path_storage, 2u);
    char entry[ASTRA_ASSIGN_NAME_MAX];
    uint32_t status;

    if (!filesystem_valid(filesystem) || path == NULL ||
        (flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0 || namespace_entry_name(path, entry))
        return ASTRA_VFS_ERR_READ_ONLY;
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY,
        filesystem_client_for, filesystem, logical, ASTRA_VFS_PATH_MAX, wire,
        ASTRA_VFS_PATH_MAX, &client, NULL);

    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    return status == ASTRA_VFS_OK ? astra_vfs_unlink(client, wire, flags) : status;
}

static uint32_t astra_filesystem_unlink_at_bound(AstraFilesystem *filesystem, const AstraFile *directory,
                                    const char *path, uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (file_owner_status(directory) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((file_rights(directory) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status = filesystem_at_destination(filesystem, directory, path,
        ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY, &client, &base, wire,
        &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    if (status != ASTRA_VFS_OK)
        return status;
    return base != ASTRA_VFS_FILE_INVALID ? astra_vfs_unlink_at(client, base, wire, flags) :
                                            astra_vfs_unlink(client, wire, flags);
}

typedef uint32_t (*FilesystemTwoPathOperation)(AstraVfsClient *, AstraVfsFile,
    const char *, AstraVfsFile, const char *);

static uint32_t filesystem_two_paths(AstraFilesystem *filesystem,
                                     const char *from, const char *to,
                                     FilesystemTwoPathOperation operation)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(9u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (relative_path(from) || relative_path(to)) {
        const AstraFile *directory;
        char *qualified_from = astra_vfs_path_storage_at(&path_storage, 0u);
        char *qualified_to = astra_vfs_path_storage_at(&path_storage, 1u);
        char *wire_from = astra_vfs_path_storage_at(&path_storage, 2u);
        char *wire_to = astra_vfs_path_storage_at(&path_storage, 3u);
        char *logical = astra_vfs_path_storage_at(&path_storage, 4u);
        AstraVfsClient *left = NULL;
        AstraVfsClient *right = NULL;
        AstraVfsFile left_base = 0u, right_base = 0u;
        uint32_t status = filesystem_relative(filesystem, relative_path(from) ? from : to,
            ASTRA_RIGHT_WRITE, qualified_from, &directory);
        if (status != ASTRA_VFS_OK)
            return status;
        if (directory != NULL) {
            if (relative_path(from)) {
                left = directory->_private_client;
                left_base = directory->_private_file;
            } else
                status = astra_vfs_assign_destination(filesystem->_private_assigns, from,
                    ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY, filesystem_client_for, filesystem,
                    logical, ASTRA_VFS_PATH_MAX, wire_from, ASTRA_VFS_PATH_MAX, &left, NULL);
            if (status == ASTRA_VFS_OK) {
                if (relative_path(to)) {
                    right = directory->_private_client;
                    right_base = directory->_private_file;
                } else
                    status = astra_vfs_assign_destination(filesystem->_private_assigns, to,
                        ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
                        filesystem_client_for, filesystem, logical, ASTRA_VFS_PATH_MAX,
                        wire_to, ASTRA_VFS_PATH_MAX, &right, NULL);
            }
            if (status == ASTRA_VFS_OK)
                status = left != right ? ASTRA_VFS_ERR_CROSS_DEVICE :
                    operation(left, left_base, left_base != 0u ? from : wire_from,
                              right_base, right_base != 0u ? to : wire_to);
            if (status != ASTRA_VFS_ERR_ESCAPED) {
                filesystem->_private_cwd_release();
                return status;
            }
        } else {
            status = filesystem->_private_cwd_acquire(&directory);
            if (status != ASTRA_VFS_OK)
                return status;
        }
        status = filesystem->_private_cwd_qualify(from, qualified_from, ASTRA_VFS_PATH_MAX);
        if (status == ASTRA_VFS_OK)
            status = filesystem->_private_cwd_qualify(to, qualified_to, ASTRA_VFS_PATH_MAX);
        filesystem->_private_cwd_release();
        return status == ASTRA_VFS_OK ?
            filesystem_two_paths(filesystem, qualified_from, qualified_to, operation) : status;
    }

    AstraVfsClient *from_client = NULL;
    AstraVfsClient *to_client = NULL;
    char *from_wire = astra_vfs_path_storage_at(&path_storage, 5u);
    char *from_logical = astra_vfs_path_storage_at(&path_storage, 6u);
    char *to_logical = astra_vfs_path_storage_at(&path_storage, 7u);
    char *to_wire = astra_vfs_path_storage_at(&path_storage, 8u);
    uint32_t status;
    char entry[ASTRA_ASSIGN_NAME_MAX];

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
        status = operation(from_client, 0u, from_wire, 0u, to_wire);
        if (status != ASTRA_VFS_ERR_NOT_DIR && status != ASTRA_VFS_ERR_ESCAPED)
            return status;
    }

    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, from, ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY,
        filesystem_client_for, filesystem, from_logical, ASTRA_VFS_PATH_MAX,
        from_wire, ASTRA_VFS_PATH_MAX, &from_client, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, to, ASTRA_RIGHT_WRITE,
        ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
        filesystem_client_for, filesystem, to_logical, ASTRA_VFS_PATH_MAX,
        to_wire, ASTRA_VFS_PATH_MAX, &to_client, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    if (status != ASTRA_VFS_OK)
        return status;
    if (from_client != to_client)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    return operation(from_client, 0u, from_wire, 0u, to_wire);
}

static uint32_t astra_filesystem_rename_bound(AstraFilesystem *filesystem,
    const char *from, const char *to,
    uint32_t (*operation)(AstraVfsClient *, AstraVfsFile, const char *,
        AstraVfsFile, const char *))
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_two_paths(filesystem, from, to, operation);
}

static uint32_t astra_filesystem_link_bound(AstraFilesystem *filesystem,
                               const char *from, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_two_paths(filesystem, from, to, astra_vfs_link_at);
}

static uint32_t astra_filesystem_chmod_bound(AstraFilesystem *filesystem,
                                const char *path, uint16_t mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_WRITE, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_chmod_at(filesystem, directory, path, mode, 0u);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_chmod(filesystem, qualified, mode);
    }
    if (relative_path(path))
        path = qualified;

    AstraVfsClient *client = NULL;
    char *wire = astra_vfs_path_storage_at(&path_storage, 1u);
    if (!filesystem_valid(filesystem) || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0)
        return ASTRA_VFS_ERR_READ_ONLY;
    uint32_t status = filesystem_locate(
        filesystem, path, ASTRA_RIGHT_WRITE, &client, wire, NULL, NULL, NULL,
        NULL, NULL);

    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    return status == ASTRA_VFS_OK ? astra_vfs_chmod(client, wire, mode) :
                                    status;
}

uint32_t astra_filesystem_chmod_file(AstraFile *file, uint16_t mode)
{
    uint32_t status;

    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if ((file_rights(file) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    status = astra_vfs_chmod_file(file->_private_client, file->_private_file,
                                  mode);
    return status;
}

static uint32_t astra_filesystem_chmod_at_bound(AstraFilesystem *filesystem, const AstraFile *directory,
                                   const char *path, uint16_t mode,
                                   uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (file_owner_status(directory) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(directory))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((file_rights(directory) & ASTRA_RIGHT_WRITE) == 0u)
        return ASTRA_VFS_ERR_ACCESS;
    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status;

    if ((flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_at_destination(filesystem, directory, path, ASTRA_RIGHT_WRITE,
        (flags & ASTRA_VFS_AT_SYMLINK_NOFOLLOW) ? 0u : ASTRA_VFS_LOOKUP_FOLLOW,
        &client, &base, wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    if (status != ASTRA_VFS_OK)
        return status;
    if (base != ASTRA_VFS_FILE_INVALID)
        return astra_vfs_chmod_at(client, base, wire, mode, flags);
    if ((flags & ASTRA_VFS_AT_SYMLINK_NOFOLLOW) == 0u)
        return astra_vfs_chmod(client, wire, mode);
    /* Preserve no-follow policy by operating on the retained parent entry. */
    char *leaf = NULL;
    for (char *at = wire; *at != '\0'; ++at)
        if (*at == '/')
            leaf = at;
    AstraVfsFile parent = ASTRA_VFS_FILE_INVALID;
    char *name = astra_vfs_path_storage_at(&path_storage, 1u);
    if (leaf == NULL || leaf[1] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    strcpy(name, leaf + 1);
    if (leaf == wire)
        wire[1] = '\0';
    else
        *leaf = '\0';
    status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
                            &parent, NULL, NULL);
    if (status == ASTRA_VFS_OK) {
        status = astra_vfs_chmod_at(client, parent, name, mode, flags);
        (void)astra_vfs_close(client, parent);
    }
    return status;
}

static uint32_t astra_filesystem_capacity_path_bound(AstraFilesystem *filesystem,
                                        const char *path,
                                        AstraFilesystemInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (relative_path(path)) {
        AstraFile file = ASTRA_FILE_INIT;
        uint32_t status = astra_filesystem_open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
        if (status == ASTRA_VFS_OK) {
            status = astra_filesystem_capacity_file(&file, info);
            (void)astra_filesystem_close(&file);
        }
        return status;
    }

    AstraVfsClient *client = NULL;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status;

    if (info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_locate(filesystem, path, ASTRA_RIGHT_READ, &client,
                               wire, NULL, NULL, NULL, NULL, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED ||
        (path != NULL && strcmp(path, "/") == 0))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    return status == ASTRA_VFS_OK ?
        astra_vfs_filesystem_info_path(client, wire, info) : status;
}

uint32_t astra_filesystem_capacity_file(const AstraFile *file,
                                        AstraFilesystemInfo *info)
{
    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return astra_vfs_filesystem_info_file(file->_private_client,
                                           file->_private_file, info);
}

static uint32_t astra_filesystem_readlink_bound(AstraFilesystem *filesystem,
                                   const char *path, void *buffer,
                                   uint32_t capacity, uint32_t *length)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(4u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_READ, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_readlink_at(filesystem, directory, path, buffer, capacity, length);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_readlink(filesystem, qualified, buffer, capacity, length);
    }
    if (relative_path(path))
        path = qualified;

    char *logical = astra_vfs_path_storage_at(&path_storage, 1u);
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (!filesystem_valid(filesystem) || path == NULL || buffer == NULL ||
        capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    {
        char *target = astra_vfs_path_storage_at(&path_storage, 2u);

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
        filesystem_client_for, filesystem, logical, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_INVALID;
    if (status != ASTRA_VFS_OK)
        return status;
    status = ASTRA_VFS_ERR_NOT_FOUND;
    for (uint32_t member = 0u;
         member < filesystem->_private_assigns->count; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        char *wire = astra_vfs_path_storage_at(&path_storage, 3u);
        uint32_t resolved = astra_assign_resolve_raw(
            filesystem->_private_assigns, logical, ASTRA_RIGHT_READ, member,
            wire, ASTRA_VFS_PATH_MAX, &assign);

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

static uint32_t astra_filesystem_symlink_bound(const char *target,
                                  AstraFilesystem *filesystem,
                                  const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(target, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(3u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    const AstraFile *directory;
    char *qualified = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t relative_status = filesystem_relative(filesystem, path, ASTRA_RIGHT_WRITE, qualified, &directory);
    if (relative_status != ASTRA_VFS_OK)
        return relative_status;
    if (directory != NULL) {
        relative_status = astra_filesystem_symlink_at(filesystem, target, directory, path);
        relative_status = filesystem_relative_done(filesystem, directory, path, relative_status, qualified);
        if (relative_status != ASTRA_VFS_ERR_ESCAPED)
            return relative_status;
        return astra_filesystem_symlink(target, filesystem, qualified);
    }
    if (relative_path(path))
        path = qualified;

    AstraVfsClient *client = NULL;
    char *logical = astra_vfs_path_storage_at(&path_storage, 1u);
    char *wire = astra_vfs_path_storage_at(&path_storage, 2u);
    uint32_t status;

    if (!filesystem_valid(filesystem) || target == NULL || target[0] == '\0' ||
        path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    {
        char entry[ASTRA_ASSIGN_NAME_MAX];

        if (namespace_entry_name(path, entry))
            return astra_assign_lookup(filesystem->_private_assigns, entry) !=
                           NULL ? ASTRA_VFS_ERR_EXISTS :
                                  ASTRA_VFS_ERR_READ_ONLY;
    }
    status = astra_vfs_assign_destination(
        filesystem->_private_assigns, path, ASTRA_RIGHT_WRITE,
        ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
        filesystem_client_for, filesystem, logical, ASTRA_VFS_PATH_MAX, wire,
        ASTRA_VFS_PATH_MAX, &client, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    return status == ASTRA_VFS_OK ? astra_vfs_symlink(client, target, wire) :
                                   status;
}

#define DIRECTORY_SINGLE_BACKEND UINT16_C(1)
#define DIRECTORY_VIRTUAL_ROOT UINT16_C(2)
#define DIRECTORY_OWNED_FILE UINT16_C(4)

static uint32_t astra_filesystem_directory_open_bound(AstraFilesystem *filesystem,
                                         const char *path,
                                         AstraDirectory *directory)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    if (relative_path(path)) {
        AstraFile file = ASTRA_FILE_INIT;
        uint32_t status = astra_filesystem_open(filesystem, path,
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY, &file);
        if (status == ASTRA_VFS_OK) {
            status = astra_filesystem_directory_from_file(&file, directory);
            if (status == ASTRA_VFS_OK)
                directory->_private_reserved |= DIRECTORY_OWNED_FILE;
            else
                (void)astra_filesystem_close(&file);
        }
        return status;
    }

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
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return astra_filesystem_directory_open(filesystem, "/", directory);
    if (status != ASTRA_VFS_OK)
        return status;
    directory->_private_filesystem = filesystem;
    directory->_private_path = opened.path; /* transfer owned pathname */
    opened.path = NULL;
    directory->_private_cursor = opened.cursor;
    directory->_private_client = opened.client;
    directory->_private_file = opened.file;
    directory->_private_ownership_epoch = opened.ownership_epoch;
    directory->_private_member = opened.member;
    directory->_private_worst = opened.worst;
    directory->_private_active = opened.active;
    directory->_private_done = opened.done;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_from_file(const AstraFile *file,
                                              AstraDirectory *directory)
{
    if (file_owner_status(file) == ASTRA_VFS_ERR_LOST)
        return ASTRA_VFS_ERR_LOST;
    if (!file_valid(file))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (directory == NULL || file->_private_kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    directory->_private_cursor = 0u;
    directory->_private_client = file->_private_client;
    directory->_private_file = file->_private_file;
    directory->_private_ownership_epoch = file->_private_ownership_epoch;
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
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    AstraVfsUnionDirectory reading = ASTRA_VFS_UNION_DIRECTORY_INIT;
    AstraFilesystem *filesystem;
    uint32_t found_member = 0u;
    uint32_t status;

    if (directory == NULL || entries == NULL || capacity == 0u ||
        count == NULL || directory->_private_active == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (directory->_private_client != NULL && directory->_private_file != ASTRA_VFS_FILE_INVALID &&
        astra_vfs_file_owner_status(directory->_private_client,
            directory->_private_ownership_epoch) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_LOST;
    if ((directory->_private_reserved & DIRECTORY_VIRTUAL_ROOT) != 0u) {
        const AstraAssignTable *assigns =
            directory->_private_filesystem->_private_assigns;
        uint32_t at = (uint32_t)directory->_private_cursor;

        *count = 0u;
        while (at < assigns->count && *count < capacity) {
            const AstraAssign *assign = &assigns->entries[at];
            char *target = astra_vfs_path_storage_at(&path_storage, 0u);
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
        uint32_t shared = directory->_private_client->features &
            ASTRA_VFS_FEATURE_DESCRIPTIONS;
        if (directory->_private_done != 0u && shared == 0u) {
            *count = 0u;
            return ASTRA_VFS_OK;
        }
        status = astra_vfs_readdir_file_batch(
            directory->_private_client, directory->_private_file, "",
            shared != 0u ? ASTRA_VFS_CURSOR_CURRENT :
                directory->_private_cursor, entries, capacity, count,
            &directory->_private_cursor);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            *count = 0u;
            directory->_private_done = 1u;
            return ASTRA_VFS_OK;
        }
        if (status != ASTRA_VFS_OK)
            return status;
        directory->_private_done = directory->_private_cursor == 0u;
        for (uint32_t index = 0u; index < *count; ++index)
            entries[index].member = (uint16_t)directory->_private_member;
        return ASTRA_VFS_OK;
    }
    filesystem = directory->_private_filesystem;
    reading.table = filesystem->_private_assigns;
    reading.client_for = filesystem_client_for;
    reading.context = filesystem;
    reading.path = directory->_private_path; /* borrowed during this read */
    reading.cursor = directory->_private_cursor;
    reading.client = directory->_private_client;
    reading.file = directory->_private_file;
    reading.ownership_epoch = directory->_private_ownership_epoch;
    reading.member = directory->_private_member;
    reading.worst = directory->_private_worst;
    reading.active = directory->_private_active;
    reading.done = directory->_private_done;
    status = astra_vfs_union_directory_read(&reading, entries, capacity, count,
                                            &found_member);
    directory->_private_cursor = reading.cursor;
    directory->_private_client = reading.client;
    directory->_private_file = reading.file;
    directory->_private_ownership_epoch = reading.ownership_epoch;
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
    if (directory->_private_client != NULL && directory->_private_file != ASTRA_VFS_FILE_INVALID &&
        astra_vfs_file_owner_status(directory->_private_client,
            directory->_private_ownership_epoch) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_LOST;
    if ((directory->_private_reserved & (DIRECTORY_SINGLE_BACKEND |
                                         DIRECTORY_VIRTUAL_ROOT)) != 0u) {
        if ((directory->_private_reserved & DIRECTORY_SINGLE_BACKEND) != 0u &&
            (directory->_private_client->features &
             ASTRA_VFS_FEATURE_DESCRIPTIONS) != 0u) {
            uint64_t position;
            uint32_t status = astra_vfs_seek(directory->_private_client,
                directory->_private_file, 0, ASTRA_FILE_SEEK_BEGIN, &position);
            if (status != ASTRA_VFS_OK)
                return status;
        }
        directory->_private_cursor = 0u;
        directory->_private_done = 0u;
        return ASTRA_VFS_OK;
    }
    if (directory->_private_client != NULL &&
        directory->_private_file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_file_owner_close(directory->_private_client,
            directory->_private_file, directory->_private_ownership_epoch);
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
        if (((directory->_private_reserved & DIRECTORY_SINGLE_BACKEND) == 0u ||
             (directory->_private_reserved & DIRECTORY_OWNED_FILE) != 0u) &&
            directory->_private_client != NULL &&
            directory->_private_file != ASTRA_VFS_FILE_INVALID)
            (void)astra_vfs_file_owner_close(directory->_private_client,
                directory->_private_file, directory->_private_ownership_epoch);
        astra_runtime_deallocate(directory->_private_path);
        AstraFilesystem *owned = directory->_private_owned_filesystem;
        void *owner = directory->_private_namespace;
        void (*release)(void *) = directory->_private_namespace_release;
        *directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
        astra_runtime_deallocate(owned);
        if (release != NULL) release(owner);
    }
}


static uint32_t astra_filesystem_mkdir_at_bound(AstraFilesystem *filesystem, const AstraFile *directory, const char *path, uint16_t mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status = filesystem_at_destination(filesystem, directory, path,
        ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
        &client, &base, wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    return status == ASTRA_VFS_OK ? astra_vfs_mkdir_at(client, base, wire, mode) : status;
}

static uint32_t filesystem_two_directories(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from,
    const AstraFile *to_directory, const char *to, FilesystemTwoPathOperation operation)
{
    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    uint32_t status = directory_authority(from_directory, ASTRA_RIGHT_WRITE);
    if (status == ASTRA_VFS_OK)
        status = directory_authority(to_directory, ASTRA_RIGHT_WRITE);
    if (status != ASTRA_VFS_OK)
        return status;
    AstraVfsClient *left = NULL, *right = NULL;
    AstraVfsFile left_base = ASTRA_VFS_FILE_INVALID, right_base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *left_wire = astra_vfs_path_storage_at(&path_storage, 0u);
    char *right_wire = astra_vfs_path_storage_at(&path_storage, 1u);
    status = filesystem_at_destination(filesystem, from_directory, from, ASTRA_RIGHT_WRITE,
        ASTRA_VFS_LOOKUP_ENTRY, &left, &left_base, left_wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_OK)
        status = filesystem_at_destination(filesystem, to_directory, to, ASTRA_RIGHT_WRITE,
            ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING, &right, &right_base,
            right_wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    if (status != ASTRA_VFS_OK)
        return status;
    if (left != right)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    return operation(left, left_base, left_wire, right_base, right_wire);
}

static uint32_t astra_filesystem_rename_at_bound(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from,
                                    const AstraFile *to_directory, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_two_directories(filesystem, from_directory, from, to_directory, to, astra_vfs_rename_at);
}

static uint32_t astra_filesystem_link_at_bound(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from,
                                  const AstraFile *to_directory, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    return filesystem_two_directories(filesystem, from_directory, from, to_directory, to, astra_vfs_link_at);
}

static uint32_t astra_filesystem_symlink_at_bound(AstraFilesystem *filesystem, const char *target, const AstraFile *directory, const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(target, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status = filesystem_at_destination(filesystem, directory, path,
        ASTRA_RIGHT_WRITE, ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_MISSING,
        &client, &base, wire, &assign, &member, NULL);
    if (status == ASTRA_VFS_ERR_ESCAPED)
        return ASTRA_VFS_ERR_READ_ONLY;
    return status == ASTRA_VFS_OK ? astra_vfs_symlink_at(client, target, base, wire) : status;
}

static uint32_t astra_filesystem_readlink_at_bound(AstraFilesystem *filesystem, const AstraFile *directory, const char *path,
                                      void *buffer, uint32_t capacity, uint32_t *length)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraVfsPathStorage path_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    if (path_storage.bytes == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    AstraVfsClient *client = NULL;
    AstraVfsFile base = ASTRA_VFS_FILE_INVALID;
    const AstraAssign *assign = NULL;
    uint32_t member = 0u;
    char *wire = astra_vfs_path_storage_at(&path_storage, 0u);
    uint32_t status = filesystem_at_destination(filesystem, directory, path,
        ASTRA_RIGHT_READ, 0u, &client, &base, wire, &assign, &member, NULL);
    return status == ASTRA_VFS_OK ? astra_vfs_readlink_at(client, base, wire,
        buffer, capacity, length) : status;
}

uint32_t astra_filesystem_directory_path(const AstraFile *directory, void *buffer,
                                         uint32_t capacity, uint32_t *length)
{
    uint32_t status = directory_authority(directory, ASTRA_RIGHT_READ);
    return status == ASTRA_VFS_OK ? astra_vfs_directory_path(directory->_private_client,
        directory->_private_file, buffer, capacity, length) : status;
}

/* A call owns one complete namespace view, including recursive continuations. */
static uint32_t namespace_view(AstraFilesystem *filesystem, AstraFilesystem *view,
    void **owner)
{
    if (filesystem == NULL) return ASTRA_VFS_ERR_INVALID;
    *view = *filesystem;
    *owner = NULL;
    if (filesystem->_private_namespace_acquire == NULL) return ASTRA_VFS_OK;
    uint32_t status = filesystem->_private_namespace_acquire(&view->_private_assigns, owner);
    view->_private_namespace_acquire = NULL;
    view->_private_namespace_release = NULL;
    return status;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *filesystem,
                                    const char *path, uint32_t flags,
                                    uint16_t create_mode, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_open_mode_bound(&view, path, flags, create_mode, file);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_open_bound(&view, path, flags, file);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_open_at_mode(AstraFilesystem *filesystem, const AstraFile *directory,
                                       const char *path, uint32_t flags,
                                       uint16_t create_mode, AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_open_at_mode_bound(&view, directory, path, flags, create_mode, file);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_open_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags,
                                  AstraFile *file)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_open_at_bound(&view, directory, path, flags, file);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_stat(AstraFilesystem *filesystem, const char *path,
                               AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_stat_bound(&view, path, info);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_lstat(AstraFilesystem *filesystem,
                                const char *path, AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_lstat_bound(&view, path, info);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_stat_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                  const char *path, uint32_t flags, AstraFileInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_stat_at_bound(&view, directory, path, flags, info);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *filesystem,
                                     const char *path, uint16_t create_mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir_mode_bound(&view, path, create_mode);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_mkdir(AstraFilesystem *filesystem,
                                const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir_bound(&view, path);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *filesystem,
                                 const char *path, uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_unlink_bound(&view, path, flags);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_unlink_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                    const char *path, uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_unlink_at_bound(&view, directory, path, flags);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

static uint32_t filesystem_rename_with(AstraFilesystem *filesystem,
    const char *from, const char *to,
    uint32_t (*operation)(AstraVfsClient *, AstraVfsFile, const char *,
        AstraVfsFile, const char *))
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_rename_bound(&view, from, to, operation);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_rename(AstraFilesystem *filesystem,
    const char *from, const char *to)
{
    return filesystem_rename_with(filesystem, from, to, astra_vfs_rename_at);
}

uint32_t astra_filesystem_rename_noreplace(AstraFilesystem *filesystem,
    const char *from, const char *to)
{
    return filesystem_rename_with(filesystem, from, to,
        astra_vfs_rename_noreplace_at);
}

uint32_t astra_filesystem_link(AstraFilesystem *filesystem,
                               const char *from, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_link_bound(&view, from, to);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_chmod(AstraFilesystem *filesystem,
                                const char *path, uint16_t mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_chmod_bound(&view, path, mode);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_chmod_at(AstraFilesystem *filesystem, const AstraFile *directory,
                                   const char *path, uint16_t mode,
                                   uint32_t flags)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_chmod_at_bound(&view, directory, path, mode, flags);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_capacity_path(AstraFilesystem *filesystem,
                                        const char *path,
                                        AstraFilesystemInfo *info)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_capacity_path_bound(&view, path, info);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_readlink(AstraFilesystem *filesystem,
                                   const char *path, void *buffer,
                                   uint32_t capacity, uint32_t *length)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_readlink_bound(&view, path, buffer, capacity, length);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_symlink(const char *target,
                                  AstraFilesystem *filesystem,
                                  const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(target, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_symlink_bound(target, &view, path);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
                                         const char *path,
                                         AstraDirectory *directory)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    if (filesystem == NULL || filesystem->_private_namespace_acquire == NULL)
        return astra_filesystem_directory_open_bound(filesystem, path, directory);
    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    AstraFilesystem *owned = astra_runtime_allocate(sizeof(*owned));
    if (owned == NULL) {
        filesystem->_private_namespace_release(owner);
        return ASTRA_VFS_ERR_LIMIT;
    }
    *owned = view;
    status = astra_filesystem_directory_open_bound(owned, path, directory);
    if (status == ASTRA_VFS_OK) {
        directory->_private_owned_filesystem = owned;
        directory->_private_namespace = owner;
        directory->_private_namespace_release = filesystem->_private_namespace_release;
    } else {
        astra_runtime_deallocate(owned);
        filesystem->_private_namespace_release(owner);
    }
    return status;
}

uint32_t astra_filesystem_mkdir_at(AstraFilesystem *filesystem, const AstraFile *directory, const char *path, uint16_t mode)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir_at_bound(&view, directory, path, mode);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_rename_at(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from,
                                    const AstraFile *to_directory, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_rename_at_bound(&view, from_directory, from, to_directory, to);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_link_at(AstraFilesystem *filesystem, const AstraFile *from_directory, const char *from,
                                  const AstraFile *to_directory, const char *to)
{
    {
        uint32_t input_status = astra_vfs_path_measure(from, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(to, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_link_at_bound(&view, from_directory, from, to_directory, to);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_symlink_at(AstraFilesystem *filesystem, const char *target, const AstraFile *directory, const char *path)
{
    {
        uint32_t input_status = astra_vfs_path_measure(target, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_symlink_at_bound(&view, target, directory, path);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}

uint32_t astra_filesystem_readlink_at(AstraFilesystem *filesystem, const AstraFile *directory, const char *path,
                                      void *buffer, uint32_t capacity, uint32_t *length)
{
    {
        uint32_t input_status = astra_vfs_path_measure(path, NULL);
        if (input_status != ASTRA_VFS_OK)
            return input_status;
    }

    AstraFilesystem view;
    void *owner = NULL;
    uint32_t status = namespace_view(filesystem, &view, &owner);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_readlink_at_bound(&view, directory, path, buffer, capacity, length);
    if (owner != NULL) filesystem->_private_namespace_release(owner);
    return status;
}
