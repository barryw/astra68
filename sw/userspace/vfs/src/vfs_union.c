/*
 * The loop over a union's members. Everything it knows about a union is that
 * resolution stops answering at some index; everything it knows about I/O is
 * that a client opens a path.
 */

#include <astra/vfs_union.h>
#include <astra/vfs_path.h>

#include <stddef.h>

/* POSIX requires ELOOP; 40 matches the established Unix traversal ceiling. */
#define ASTRA_VFS_SYMLINK_FOLLOW_MAX 40u

static void remember(uint32_t *status, uint32_t candidate)
{
    if (*status == ASTRA_VFS_ERR_NOT_FOUND)
        *status = candidate;
}

uint32_t
astra_vfs_assign_primary(const AstraAssignTable *table, const char *path,
                         uint32_t rights,
                         AstraVfsAssignClientFn client_for, void *context,
                         char *wire, uint32_t capacity,
                         AstraVfsClient **client, uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL ||
        client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved = astra_assign_resolve(
            table, path, rights, index, wire, capacity, &assign);

        if (resolved == ASTRA_VFS_ERR_NOT_FOUND)
            break;
        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL)
            continue;
        *client = serving;
        if (member != NULL)
            *member = index;
        return ASTRA_VFS_OK;
    }
    return status;
}

static uint32_t
assign_raw_lstat(const AstraAssignTable *table, const char *path,
                 uint32_t rights, AstraVfsAssignClientFn client_for,
                 void *context, char *wire, uint32_t capacity,
                 AstraVfsDirEntry *entry, AstraVfsClient **client,
                 const AstraAssign **found_assign, uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        AstraVfsDirEntry found = {0};
        uint32_t resolved = astra_assign_resolve(
            table, path, ASTRA_RIGHT_READ, index, wire, capacity, &assign);

        if (resolved == ASTRA_VFS_ERR_NOT_FOUND)
            break;
        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL)
            continue;
        resolved = astra_vfs_stat_meta(serving, wire, &found);
        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            continue;
        }
        if ((assign->rights & rights) != rights) {
            remember(&status, ASTRA_VFS_ERR_ACCESS);
            continue;
        }
        if (entry != NULL)
            *entry = found;
        if (client != NULL)
            *client = serving;
        if (found_assign != NULL)
            *found_assign = assign;
        if (member != NULL)
            *member = index;
        return ASTRA_VFS_OK;
    }
    return status;
}

static int copy_path_to(char *out, uint32_t capacity, const char *path)
{
    uint32_t length = 0u;

    if (path == NULL || out == NULL || capacity == 0u)
        return 0;
    while (path[length] != '\0') {
        if (length + 1u >= capacity)
            return 0;
        out[length] = path[length];
        ++length;
    }
    out[length] = '\0';
    return 1;
}

static uint32_t append_path(char *out, uint32_t capacity, const char *suffix)
{
    uint32_t length = 0u;
    uint32_t index = 0u;

    while (out[length] != '\0')
        ++length;
    while (suffix[index] != '\0') {
        if (length + 1u >= capacity)
            return ASTRA_VFS_ERR_INVALID;
        out[length++] = suffix[index++];
    }
    out[length] = '\0';
    return ASTRA_VFS_OK;
}

static uint32_t canonical_path(const char *path, char *out,
                               uint32_t capacity)
{
    char assign[ASTRA_CAPABILITY_NAME_MAX];
    char rest[ASTRA_VFS_PATH_MAX];
    char normal[ASTRA_VFS_PATH_MAX];
    uint32_t status = astra_path_split(path, assign, sizeof(assign), rest,
                                       sizeof(rest));

    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_path_normalise(rest, normal, sizeof(normal));
    if (status != ASTRA_VFS_OK)
        return status;
    return astra_path_qualify(assign, "", normal, out, capacity);
}

/*
 * Resolve links in Astra's logical namespace, never in a backend namespace.
 * Every absolute hop therefore has to name an assign held by this process,
 * and normalisation rejects a relative hop above that assign's root.
 */
static uint32_t follow_path(const AstraAssignTable *table, const char *path,
                            uint32_t rights,
                            AstraVfsAssignClientFn client_for, void *context,
                            int follow_final, int allow_missing_final,
                            char *logical, uint32_t logical_capacity,
                            AstraVfsDirEntry *entry, AstraVfsClient **client,
                            const AstraAssign **found_assign,
                            uint32_t *member, char *wire, uint32_t capacity)
{
    char current[ASTRA_VFS_PATH_MAX];
    uint32_t followed = 0u;
    uint32_t status = canonical_path(path, current, sizeof(current));

    if (status != ASTRA_VFS_OK)
        return status;
    for (;;) {
        char assign[ASTRA_CAPABILITY_NAME_MAX];
        char rest[ASTRA_VFS_PATH_MAX];
        uint32_t rest_length = 0u;
        uint32_t start = 0u;

        status = astra_path_split(current, assign, sizeof(assign), rest,
                                  sizeof(rest));
        if (status != ASTRA_VFS_OK)
            return status;
        while (rest[rest_length] != '\0')
            ++rest_length;

        /*
         * The common path is one backend lookup. A backend does not follow
         * links; an intermediate link therefore answers NOT_DIR, which is the
         * signal to walk components below. Paying one IPC round trip per
         * component on every ordinary path would make correct name lookup
         * needlessly slow.
         */
        {
            char direct_wire[ASTRA_VFS_PATH_MAX];
            AstraVfsDirEntry direct = {0};
            AstraVfsClient *serving = NULL;
            const AstraAssign *binding = NULL;
            uint32_t found_member = 0u;

            status = assign_raw_lstat(
                table, current, rights, client_for, context, direct_wire,
                sizeof(direct_wire), &direct, &serving, &binding,
                &found_member);
            if (status == ASTRA_VFS_OK &&
                (direct.kind != ASTRA_VFS_KIND_SYMLINK || !follow_final)) {
                if (!copy_path_to(logical, logical_capacity, current) ||
                    !copy_path_to(wire, capacity, direct_wire))
                    return ASTRA_VFS_ERR_INVALID;
                if (entry != NULL)
                    *entry = direct;
                if (client != NULL)
                    *client = serving;
                if (found_assign != NULL)
                    *found_assign = binding;
                if (member != NULL)
                    *member = found_member;
                return ASTRA_VFS_OK;
            }
            if (status == ASTRA_VFS_ERR_NOT_FOUND && allow_missing_final) {
                if (!copy_path_to(logical, logical_capacity, current))
                    return ASTRA_VFS_ERR_INVALID;
                return ASTRA_VFS_OK;
            }
            if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_DIR)
                return status;
        }

        /* The empty rest names the assign root and has one node to inspect. */
        do {
            char prefix_rest[ASTRA_VFS_PATH_MAX];
            char prefix[ASTRA_VFS_PATH_MAX];
            char prefix_wire[ASTRA_VFS_PATH_MAX];
            AstraVfsDirEntry found = {0};
            AstraVfsClient *serving = NULL;
            const AstraAssign *binding = NULL;
            uint32_t found_member = 0u;
            uint32_t end = start;
            int final;

            while (end < rest_length && rest[end] != '/')
                ++end;
            final = end == rest_length;
            for (uint32_t index = 0u; index < end; ++index)
                prefix_rest[index] = rest[index];
            prefix_rest[end] = '\0';
            status = astra_path_qualify(assign, "", prefix_rest, prefix,
                                        sizeof(prefix));
            if (status != ASTRA_VFS_OK)
                return status;
            status = assign_raw_lstat(
                table, prefix, final ? rights : ASTRA_RIGHT_READ, client_for,
                context, prefix_wire, sizeof(prefix_wire), &found, &serving,
                &binding, &found_member);
            if (status == ASTRA_VFS_ERR_NOT_FOUND && final &&
                allow_missing_final) {
                if (!copy_path_to(logical, logical_capacity, current))
                    return ASTRA_VFS_ERR_INVALID;
                return ASTRA_VFS_OK;
            }
            if (status != ASTRA_VFS_OK)
                return status;
            if (found.kind == ASTRA_VFS_KIND_SYMLINK &&
                (!final || follow_final)) {
                char target[ASTRA_VFS_PATH_MAX + 1u];
                char parent[ASTRA_VFS_PATH_MAX];
                char replacement[ASTRA_VFS_PATH_MAX];
                char combined[ASTRA_VFS_PATH_MAX];
                uint32_t length = 0u;
                uint32_t parent_length = start == 0u ? 0u : start - 1u;

                if (++followed > ASTRA_VFS_SYMLINK_FOLLOW_MAX)
                    return ASTRA_VFS_ERR_LOOP;
                status = astra_vfs_readlink(serving, prefix_wire, target,
                                            ASTRA_VFS_PATH_MAX, &length);
                if (status != ASTRA_VFS_OK)
                    return status;
                if (length == 0u || length > ASTRA_VFS_PATH_MAX)
                    return ASTRA_VFS_ERR_INVALID;
                target[length] = '\0';
                for (uint32_t index = 0u; index < parent_length; ++index)
                    parent[index] = rest[index];
                parent[parent_length] = '\0';
                status = astra_path_qualify(assign, parent, target,
                                            replacement,
                                            sizeof(replacement));
                if (status != ASTRA_VFS_OK ||
                    !copy_path_to(combined, sizeof(combined), replacement))
                    return ASTRA_VFS_ERR_INVALID;
                status = append_path(combined, sizeof(combined), rest + end);
                if (status != ASTRA_VFS_OK)
                    return status;
                status = canonical_path(combined, current, sizeof(current));
                if (status != ASTRA_VFS_OK)
                    return status;
                break;                  /* restart after replacing this hop */
            }
            if (!final && found.kind != ASTRA_VFS_KIND_DIRECTORY)
                return ASTRA_VFS_ERR_NOT_DIR;
            if (final) {
                if (!copy_path_to(logical, logical_capacity, current) ||
                    !copy_path_to(wire, capacity, prefix_wire))
                    return ASTRA_VFS_ERR_INVALID;
                if (entry != NULL)
                    *entry = found;
                if (client != NULL)
                    *client = serving;
                if (found_assign != NULL)
                    *found_assign = binding;
                if (member != NULL)
                    *member = found_member;
                return ASTRA_VFS_OK;
            }
            start = end + 1u;
        } while (start <= rest_length);
    }
}

uint32_t
astra_vfs_assign_lstat(const AstraAssignTable *table, const char *path,
                       uint32_t rights,
                       AstraVfsAssignClientFn client_for, void *context,
                       char *wire, uint32_t capacity, AstraVfsDirEntry *entry,
                       AstraVfsClient **client,
                       const AstraAssign **found_assign, uint32_t *member)
{
    char logical[ASTRA_VFS_PATH_MAX];

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return follow_path(table, path, rights, client_for, context, 0, 0,
                       logical, sizeof(logical), entry, client, found_assign,
                       member, wire, capacity);
}

uint32_t
astra_vfs_assign_stat(const AstraAssignTable *table, const char *path,
                      uint32_t rights,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsDirEntry *entry,
                      AstraVfsClient **client,
                      const AstraAssign **found_assign, uint32_t *member)
{
    char logical[ASTRA_VFS_PATH_MAX];

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return follow_path(table, path, rights, client_for, context, 1, 0,
                       logical, sizeof(logical), entry, client, found_assign,
                       member, wire, capacity);
}

uint32_t
astra_vfs_assign_resolve_links(const AstraAssignTable *table,
                               const char *path, uint32_t rights,
                               int follow_final, int allow_missing_final,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *logical,
                               uint32_t capacity)
{
    char resolved[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (logical == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = follow_path(table, path, rights, client_for, context,
                         follow_final, allow_missing_final, resolved,
                         sizeof(resolved), NULL, NULL, NULL, NULL, wire,
                         sizeof(wire));
    if (status != ASTRA_VFS_OK)
        return status;
    return copy_path_to(logical, capacity, resolved) ? ASTRA_VFS_OK :
                                                      ASTRA_VFS_ERR_INVALID;
}

uint32_t
astra_vfs_assign_destination(const AstraAssignTable *table, const char *path,
                             uint32_t rights, int follow_final,
                             AstraVfsAssignClientFn client_for, void *context,
                             char *logical, uint32_t logical_capacity,
                             char *wire, uint32_t wire_capacity,
                             AstraVfsClient **client, uint32_t *member)
{
    uint32_t status;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *client = NULL;
    status = follow_path(table, path, rights, client_for, context,
                         follow_final, 1, logical, logical_capacity, NULL,
                         client, NULL, member, wire, wire_capacity);
    if (status != ASTRA_VFS_OK || *client != NULL)
        return status;
    return astra_vfs_assign_primary(table, logical, rights, client_for,
                                    context, wire, wire_capacity, client,
                                    member);
}

uint32_t
astra_vfs_union_directory_open(const AstraAssignTable *table,
                               const char *path,
                               AstraVfsAssignClientFn client_for,
                               void *context,
                               AstraVfsUnionDirectory *directory)
{
    AstraVfsDirEntry entry = {0};
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (table == NULL || client_for == NULL || directory == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = follow_path(table, path, ASTRA_RIGHT_READ, client_for, context,
                         1, 0, directory->path, sizeof(directory->path),
                         &entry, NULL, NULL, NULL, wire, sizeof(wire));
    if (status != ASTRA_VFS_OK)
        return status;
    if (entry.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    directory->table = table;
    directory->client_for = client_for;
    directory->context = context;
    directory->cursor = 0u;
    directory->member = 0u;
    directory->worst = ASTRA_VFS_ERR_NOT_FOUND;
    directory->active = 1u;
    directory->done = 0u;
    return ASTRA_VFS_OK;
}

static void
union_directory_close_file(AstraVfsUnionDirectory *directory)
{
    if (directory->client != NULL &&
        directory->file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_close(directory->client, directory->file);
    directory->client = NULL;
    directory->file = ASTRA_VFS_FILE_INVALID;
}

uint32_t
astra_vfs_union_directory_read(AstraVfsUnionDirectory *directory,
                               AstraVfsDirEntry *entries, uint32_t capacity,
                               uint32_t *count, uint32_t *member)
{
    if (directory == NULL || entries == NULL || capacity == 0u ||
        count == NULL || directory->active == 0u)
        return ASTRA_VFS_ERR_INVALID;
    *count = 0u;
    if (directory->done != 0u)
        return ASTRA_VFS_OK;
    for (;;) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        char wire[ASTRA_VFS_PATH_MAX];
        uint64_t next = 0u;
        uint32_t found = 0u;
        uint32_t status = astra_assign_resolve(
            directory->table, directory->path, ASTRA_RIGHT_READ,
            directory->member, wire, sizeof(wire), &assign);

        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            directory->done = 1u;
            if (directory->worst != ASTRA_VFS_ERR_NOT_FOUND) {
                status = directory->worst;
                directory->worst = ASTRA_VFS_ERR_NOT_FOUND;
                return status;
            }
            return ASTRA_VFS_OK;
        }
        if (status != ASTRA_VFS_OK) {
            remember(&directory->worst, status);
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        client = directory->client_for(assign, directory->context);
        if (client == NULL) {
            remember(&directory->worst, ASTRA_VFS_ERR_PEER);
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        if (directory->file == ASTRA_VFS_FILE_INVALID) {
            uint64_t size = 0u;
            uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;

            status = astra_vfs_open(
                client, wire, ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
                &directory->file, &size, &kind);
            if (status != ASTRA_VFS_OK ||
                kind != ASTRA_VFS_KIND_DIRECTORY) {
                if (status == ASTRA_VFS_OK) {
                    (void)astra_vfs_close(client, directory->file);
                    status = ASTRA_VFS_ERR_NOT_DIR;
                }
                directory->file = ASTRA_VFS_FILE_INVALID;
                remember(&directory->worst, status);
                ++directory->member;
                directory->cursor = 0u;
                continue;
            }
            directory->client = client;
        }
        status = astra_vfs_readdir_file_batch(
            client, directory->file, wire, directory->cursor, entries,
            capacity, &found, &next);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            union_directory_close_file(directory);
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        if (status != ASTRA_VFS_OK) {
            union_directory_close_file(directory);
            remember(&directory->worst, status);
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        if (member != NULL)
            *member = directory->member;
        directory->cursor = next;
        if (next == 0u) {
            union_directory_close_file(directory);
            ++directory->member;
            directory->cursor = 0u;
        }
        if (found == 0u)
            continue;
        *count = found;
        return ASTRA_VFS_OK;
    }
}

void astra_vfs_union_directory_close(AstraVfsUnionDirectory *directory)
{
    if (directory != NULL) {
        union_directory_close_file(directory);
        *directory = (AstraVfsUnionDirectory)
            ASTRA_VFS_UNION_DIRECTORY_INIT;
    }
}

static uint32_t assign_raw_open(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    uint32_t flags, AstraVfsAssignClientFn client_for, void *context,
    char *wire, uint32_t capacity, AstraVfsFile *file, uint64_t *size,
    uint16_t *kind, AstraVfsClient **client, uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved = astra_assign_resolve(
            table, path, rights, index, wire, capacity, &assign);

        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            if (resolved == ASTRA_VFS_ERR_NOT_FOUND)
                break;
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL)
            continue;
        resolved = astra_vfs_open(serving, wire, flags, file, size, kind);
        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            continue;
        }
        if (client != NULL)
            *client = serving;
        if (member != NULL)
            *member = index;
        return ASTRA_VFS_OK;
    }
    return status;
}

uint32_t
astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                      uint32_t rights, uint32_t flags,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsFile *file,
                      uint64_t *size, uint16_t *kind, AstraVfsClient **client,
                      uint32_t *member)
{
    char logical[ASTRA_VFS_PATH_MAX];
    char followed_wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL ||
        file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /* A successful non-creating open proves the path contains no link. */
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
                  ASTRA_VFS_OPEN_EXCLUSIVE)) == 0u) {
        status = assign_raw_open(table, path, rights, flags, client_for,
                                 context, wire, capacity, file, size, kind,
                                 client, member);
        if (status == ASTRA_VFS_OK)
            return status;
    }
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
        (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        uint32_t exists = astra_vfs_assign_lstat(
            table, path, rights, client_for, context, followed_wire,
            sizeof(followed_wire), NULL, NULL, NULL, NULL);

        if (exists == ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_EXISTS;
        if (exists != ASTRA_VFS_ERR_NOT_FOUND)
            return exists;
    }
    status = follow_path(table, path, rights, client_for, context, 1,
                         (flags & ASTRA_VFS_OPEN_CREATE) != 0u,
                         logical, sizeof(logical), NULL, NULL, NULL, NULL,
                         followed_wire, sizeof(followed_wire));
    if (status != ASTRA_VFS_OK)
        return status;
    return assign_raw_open(table, logical, rights, flags, client_for, context,
                           wire, capacity, file, size, kind, client, member);
}
