/*
 * The loop over a union's members. Everything it knows about a union is that
 * resolution stops answering at some index; everything it knows about I/O is
 * that a client opens a path.
 */

#include <astra/vfs_union.h>

#include <stddef.h>

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

uint32_t
astra_vfs_assign_stat(const AstraAssignTable *table, const char *path,
                      uint32_t rights,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsDirEntry *entry,
                      AstraVfsClient **client,
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

static int copy_path(char *out, const char *path)
{
    uint32_t length = 0u;

    if (path == NULL)
        return 0;
    while (path[length] != '\0') {
        if (length + 1u >= ASTRA_VFS_PATH_MAX)
            return 0;
        out[length] = path[length];
        ++length;
    }
    out[length] = '\0';
    return 1;
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

    if (table == NULL || client_for == NULL || directory == NULL ||
        !copy_path(directory->path, path))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_stat(table, path, ASTRA_RIGHT_READ, client_for,
                                   context, wire, sizeof(wire), &entry, NULL,
                                   NULL, NULL);
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
        status = astra_vfs_readdir_batch(client, wire, directory->cursor,
                                         entries, capacity, &found, &next);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        if (status != ASTRA_VFS_OK) {
            remember(&directory->worst, status);
            ++directory->member;
            directory->cursor = 0u;
            continue;
        }
        if (member != NULL)
            *member = directory->member;
        directory->cursor = next;
        if (next == 0u) {
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
    if (directory != NULL)
        *directory = (AstraVfsUnionDirectory)
            ASTRA_VFS_UNION_DIRECTORY_INIT;
}

uint32_t
astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                      uint32_t rights, uint32_t flags,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsFile *file,
                      uint64_t *size, uint16_t *kind, AstraVfsClient **client,
                      uint32_t *member)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    if (table == NULL || client_for == NULL || wire == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (uint32_t index = 0u; index < ASTRA_ASSIGN_MAX; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved;
        uint32_t opened;

        resolved = astra_assign_resolve(table, path, rights, index, wire,
                                        capacity, &assign);
        if (resolved != ASTRA_VFS_OK) {
            /*
             * NOT_FOUND is the ordinary case -- a member that simply is not
             * the answer -- so it never displaces a status that already says
             * something more specific. See the comment below for why that
             * matters.
             *
             * NOT_FOUND is also what ends the loop, and it means either of
             * two things: past the last member, or a `..` in `path` that
             * would climb out of whichever member this was. The two are not
             * distinguished, and cannot be from here -- astra_assign_resolve
             * returns the identical status for both (astra/vfs_assign.h) --
             * but they do not need to be. A `..` escape is a property of
             * `path` itself, not of any one member's root: astra_path_normalise
             * refuses it by refusing to backtrack past its own output
             * buffer's start, before it has even looked at what member is
             * being tried, so every member would refuse it identically. The
             * next member was never going to answer differently.
             */
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                status = resolved;
            }
            if (resolved == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            continue;
        }
        serving = client_for(assign, context);
        if (serving == NULL) {
            continue;
        }
        opened = astra_vfs_open(serving, wire, flags, file, size, kind);
        if (opened != ASTRA_VFS_OK) {
            /*
             * Keep trying every member regardless of what this one said: a
             * union's whole value is that one broken member costs only the
             * files on it, and a device that is failing must not stop a
             * working member later in the list from answering. But between
             * two failures, remember the worse one rather than the last one
             * -- ASTRA_VFS_ERR_NOT_FOUND just means "absent", the ordinary
             * case, while ASTRA_VFS_ERR_IO and friends mean a device could
             * not answer at all. A caller told "not found" when the truth is
             * "the device failed" has no reason to stop retrying, and will
             * hammer a machine that can never answer -- the single most
             * expensive wrong answer this project has already paid for once.
             */
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                status = opened;
            }
            continue;
        }
        if (client != NULL) {
            *client = serving;
        }
        if (member != NULL) {
            *member = index;
        }
        return ASTRA_VFS_OK;
    }
    return status;
}
