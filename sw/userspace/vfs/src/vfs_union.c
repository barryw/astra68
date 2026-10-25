/*
 * The loop over a union's members. Everything it knows about a union is that
 * resolution stops answering at some index; everything it knows about I/O is
 * that a client opens a path.
 */

#include <astra/vfs_union.h>
#include <astra/vfs_path.h>
#include <astra/vfs_path_storage.h>
#include <astra/vfs_file_lifetime.h>
#include <astra/runtime.h>

#include <stddef.h>
#include <string.h>

typedef struct AstraVfsPathHistory {
    char *paths;
    uint32_t count;
    uint32_t capacity;
    uint32_t links;
} AstraVfsPathHistory;

static uint32_t path_visit(AstraVfsPathHistory *history, const char *path)
{
    char *grown;

    for (uint32_t index = 0u; index < history->count; ++index)
        if (strcmp(history->paths + index * ASTRA_VFS_PATH_MAX, path) == 0)
            return ASTRA_VFS_ERR_LOOP;
    if (history->count == history->capacity) {
        uint32_t capacity = history->capacity == 0u ? 8u :
                            history->capacity * 2u;

        if (capacity <= history->capacity ||
            capacity > UINT32_MAX / ASTRA_VFS_PATH_MAX)
            return ASTRA_VFS_ERR_LIMIT;
        grown = astra_runtime_reallocate(
            history->paths, (size_t)capacity * ASTRA_VFS_PATH_MAX);
        if (grown == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        history->paths = grown;
        history->capacity = capacity;
    }
    (void)strcpy(history->paths + history->count * ASTRA_VFS_PATH_MAX, path);
    ++history->count;
    return ASTRA_VFS_OK;
}

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
    for (uint32_t index = 0u; index < table->count; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        uint32_t resolved = astra_assign_resolve_raw(
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
                 const AstraAssign **found_assign, uint32_t *member,
                 uint32_t flags, uint32_t links_left, AstraVfsEscape *escape)
{
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;
    int missing_leaf = 0;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t index = 0u; index < table->count; ++index) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *serving;
        AstraVfsDirEntry found = {0};
        uint32_t resolved = astra_assign_resolve_raw(
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
        resolved = astra_vfs_lookup(serving, ASTRA_VFS_FILE_INVALID, wire, flags, links_left,
                                     &found, escape);
        if (resolved == ASTRA_VFS_ERR_UNSUPPORTED) {
            if (strlen(wire) > 1u && astra_path_requires_walk(wire))
                resolved = ASTRA_VFS_ERR_NOT_DIR;
            else
                resolved = astra_vfs_stat_meta(serving, wire, &found);
        }
        if (resolved == ASTRA_VFS_ERR_ESCAPED)
            return resolved;
        if (resolved != ASTRA_VFS_OK) {
            remember(&status, resolved);
            continue;
        }
        if ((flags & ASTRA_VFS_LOOKUP_MISSING) &&
            found.kind == ASTRA_VFS_KIND_UNKNOWN) {
            missing_leaf = 1;
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
    if (status == ASTRA_VFS_ERR_NOT_FOUND && missing_leaf) {
        if (entry != NULL)
            *entry = (AstraVfsDirEntry){0};
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
    char assign[ASTRA_ASSIGN_NAME_MAX];
    AstraVfsPathStorage rest_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *rest = rest_storage.bytes;
    if (rest == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    AstraVfsPathStorage collapsed_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *collapsed = collapsed_storage.bytes;
    if (collapsed == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status = astra_path_collapse(path, collapsed, ASTRA_VFS_PATH_MAX);

    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_path_split(collapsed, assign, sizeof(assign), rest,
                              ASTRA_VFS_PATH_MAX);

    if (status != ASTRA_VFS_OK)
        return status;
    return astra_path_qualify(assign, "", rest, out, capacity);
}

static void namespace_root_parents(char *path)
{
    const char *rest = path + 1u;

    for (;;) {
        while (*rest == '/')
            ++rest;
        if (rest[0] == '.' && (rest[1] == '/' || rest[1] == '\0'))
            rest += 1u;
        else if (rest[0] == '.' && rest[1] == '.' &&
                 (rest[2] == '/' || rest[2] == '\0'))
            rest += 2u;
        else
            break;
    }
    memmove(path + 1u, rest, strlen(rest) + 1u);
}

static uint32_t continue_escape(char *current, const AstraVfsEscape *escape,
                                const char *suffix, AstraVfsPathHistory *history)
{
    if (escape->length == 0u || escape->length > escape->capacity ||
        escape->links > ASTRA_VFS_LINK_MAX - history->links)
        return ASTRA_VFS_ERR_PROTOCOL;
    history->links += escape->links;
    memcpy(current, escape->path, escape->length);
    current[escape->length] = '\0';
    return append_path(current, ASTRA_VFS_PATH_MAX, suffix);
}

/*
 * Resolve links in Astra's logical namespace, never in a backend namespace.
 * Every namespace hop must name an assign held by this process.
 */
static uint32_t follow_path_inner(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    AstraVfsAssignClientFn client_for, void *context, uint32_t flags,
    int allow_missing_final, char *logical, uint32_t logical_capacity,
    AstraVfsDirEntry *entry, AstraVfsClient **client,
    const AstraAssign **found_assign, uint32_t *member, char *wire,
    uint32_t capacity, AstraVfsPathHistory *history)
{
    AstraVfsPathStorage current_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *current = current_storage.bytes;
    if (current == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status = astra_path_collapse(path, current, ASTRA_VFS_PATH_MAX);
    int follow_final = (flags & ASTRA_VFS_LOOKUP_FOLLOW) != 0u;
    int entry_final = (flags & ASTRA_VFS_LOOKUP_ENTRY) != 0u;

    if (status != ASTRA_VFS_OK || current[0] != '/' ||
        (flags & ~(ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY)) != 0u ||
        (follow_final && entry_final))
        return ASTRA_VFS_ERR_INVALID;
    for (;;) {
        char assign[ASTRA_ASSIGN_NAME_MAX];
        AstraVfsPathStorage rest_storage ASTRA_AUTO_VFS_PATH_STORAGE =
            astra_vfs_path_storage_open(1u);
        char *rest = rest_storage.bytes;
        if (rest == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        uint32_t rest_length = 0u;
        uint32_t start = 0u;
        int single_component = 1;
        int joined;

        namespace_root_parents(current);
        if (strcmp(current, "/") == 0) {
            if (!copy_path_to(logical, logical_capacity, current))
                return ASTRA_VFS_ERR_INVALID;
            return ASTRA_VFS_ERR_ESCAPED;
        }
        status = canonical_path(current, current, ASTRA_VFS_PATH_MAX);
        if (status != ASTRA_VFS_OK)
            return status;
        status = path_visit(history, current);
        if (status != ASTRA_VFS_OK)
            return status;
        status = astra_path_split(current, assign, sizeof(assign), rest,
                                  ASTRA_VFS_PATH_MAX);
        if (status != ASTRA_VFS_OK)
            return status;
        while (rest[rest_length] != '\0') {
            if (rest[rest_length] == '/')
                single_component = 0;
            ++rest_length;
        }
        joined = astra_assign_member(table, assign, 1u) != NULL;

        /*
         * Providers resolve the whole path. NOT_DIR from a backend that does
         * not implement ordered lookup sends us through the shared walk.
         */
        if (!joined || single_component || rest_length == 0u) {
            AstraVfsPathStorage direct_wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            char *direct_wire = direct_wire_storage.bytes;
            if (direct_wire == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsDirEntry direct = {0};
            AstraVfsClient *serving = NULL;
            const AstraAssign *binding = NULL;
            uint32_t found_member = 0u;
            AstraVfsPathStorage escape_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            if (escape_storage.bytes == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsEscape escape = {.path = (uint8_t *)escape_storage.bytes,
                .capacity = ASTRA_VFS_ESCAPE_PATH_MAX};
            uint32_t lookup_flags = flags |
                (allow_missing_final ? ASTRA_VFS_LOOKUP_MISSING : 0u);
            /* The first union entry owns a link, including a dangling one. */
            if (joined)
                lookup_flags &= ~ASTRA_VFS_LOOKUP_FOLLOW;

            status = assign_raw_lstat(
                table, current, rights, client_for, context, direct_wire,
                ASTRA_VFS_PATH_MAX, &direct, &serving, &binding,
                &found_member, lookup_flags,
                ASTRA_VFS_LINK_MAX - history->links, &escape);
            if (status == ASTRA_VFS_ERR_ESCAPED) {
                status = continue_escape(current, &escape, "", history);
                if (status != ASTRA_VFS_OK)
                    return status;
                continue;
            }
            if (status == ASTRA_VFS_OK) {
                history->links += escape.links;
                if (allow_missing_final && direct.kind == ASTRA_VFS_KIND_UNKNOWN) {
                    if (!copy_path_to(logical, logical_capacity, current))
                        return ASTRA_VFS_ERR_INVALID;
                    return ASTRA_VFS_OK;
                }
                if (rest_length != 0u && rest[rest_length - 1u] == '/' &&
                    direct.kind != ASTRA_VFS_KIND_DIRECTORY) {
                    if (direct.kind != ASTRA_VFS_KIND_SYMLINK || entry_final)
                        return ASTRA_VFS_ERR_NOT_DIR;
                    status = ASTRA_VFS_ERR_NOT_DIR;
                }
            }
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
            if (status == ASTRA_VFS_ERR_NOT_FOUND && allow_missing_final &&
                single_component) {
                if (!copy_path_to(logical, logical_capacity, current))
                    return ASTRA_VFS_ERR_INVALID;
                return ASTRA_VFS_OK;
            }
            if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_DIR &&
                !(status == ASTRA_VFS_ERR_NOT_FOUND && allow_missing_final))
                return status;
        }

        /* The empty rest names the assign root and has one node to inspect. */
        do {
            AstraVfsPathStorage prefix_rest_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            char *prefix_rest = prefix_rest_storage.bytes;
            if (prefix_rest == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsPathStorage prefix_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            char *prefix = prefix_storage.bytes;
            if (prefix == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsPathStorage prefix_wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            char *prefix_wire = prefix_wire_storage.bytes;
            if (prefix_wire == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsDirEntry found = {0};
            AstraVfsClient *serving = NULL;
            const AstraAssign *binding = NULL;
            uint32_t found_member = 0u;
            uint32_t end = start;
            int final;
            int trailing;

            while (end < rest_length && rest[end] != '/')
                ++end;
            trailing = end + 1u == rest_length && rest[end] == '/';
            final = end == rest_length || trailing;
            if ((end - start == 1u && rest[start] == '.') ||
                (end - start == 2u && rest[start] == '.' && rest[start + 1u] == '.')) {
                AstraVfsPathStorage remaining_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                    astra_vfs_path_storage_open(1u);
                char *remaining = remaining_storage.bytes;
                if (remaining == NULL)
                    return ASTRA_VFS_ERR_LIMIT;
                uint32_t parent = start == 0u ? 0u : start - 1u;
                int above = end - start == 2u && parent == 0u;

                if (end - start == 2u && parent != 0u) {
                    while (parent != 0u && rest[parent - 1u] != '/')
                        --parent;
                    if (parent != 0u)
                        --parent;
                }
                strcpy(remaining, rest + end);
                rest[parent] = '\0';
                if (above) {
                    strcpy(current, "/");
                    status = ASTRA_VFS_OK;
                } else {
                    status = astra_path_qualify(assign, "", rest, current,
                                                ASTRA_VFS_PATH_MAX);
                }
                if (status != ASTRA_VFS_OK ||
                    append_path(current, ASTRA_VFS_PATH_MAX, remaining) != ASTRA_VFS_OK)
                    return ASTRA_VFS_ERR_INVALID;
                break;
            }
            for (uint32_t index = 0u; index < end; ++index)
                prefix_rest[index] = rest[index];
            prefix_rest[end] = '\0';
            status = astra_path_qualify(assign, "", prefix_rest, prefix,
                                        ASTRA_VFS_PATH_MAX);
            if (status != ASTRA_VFS_OK)
                return status;
            AstraVfsPathStorage escape_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            if (escape_storage.bytes == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsEscape escape = {.path = (uint8_t *)escape_storage.bytes,
                .capacity = ASTRA_VFS_ESCAPE_PATH_MAX};
            status = assign_raw_lstat(
                table, prefix, final ? rights : ASTRA_RIGHT_READ, client_for,
                context, prefix_wire, ASTRA_VFS_PATH_MAX, &found, &serving,
                &binding, &found_member, 0u,
                ASTRA_VFS_LINK_MAX - history->links, &escape);
            if (status == ASTRA_VFS_ERR_ESCAPED) {
                status = continue_escape(current, &escape, rest + end, history);
                if (status != ASTRA_VFS_OK)
                    return status;
                break;
            }
            if (status == ASTRA_VFS_ERR_NOT_FOUND && final &&
                allow_missing_final) {
                if (!copy_path_to(logical, logical_capacity, current))
                    return ASTRA_VFS_ERR_INVALID;
                return ASTRA_VFS_OK;
            }
            if (status != ASTRA_VFS_OK)
                return status;
            if (found.kind == ASTRA_VFS_KIND_SYMLINK &&
                (!final || follow_final || (trailing && !entry_final))) {
                AstraVfsPathStorage target_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                    astra_vfs_path_storage_open(1u);
                char *target = target_storage.bytes;
                if (target == NULL)
                    return ASTRA_VFS_ERR_LIMIT;
                AstraVfsPathStorage parent_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                    astra_vfs_path_storage_open(1u);
                char *parent = parent_storage.bytes;
                if (parent == NULL)
                    return ASTRA_VFS_ERR_LIMIT;
                AstraVfsPathStorage replacement_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                    astra_vfs_path_storage_open(1u);
                char *replacement = replacement_storage.bytes;
                if (replacement == NULL)
                    return ASTRA_VFS_ERR_LIMIT;
                AstraVfsPathStorage combined_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                    astra_vfs_path_storage_open(1u);
                char *combined = combined_storage.bytes;
                if (combined == NULL)
                    return ASTRA_VFS_ERR_LIMIT;
                uint32_t length = 0u;
                uint32_t parent_length = start == 0u ? 0u : start - 1u;

                if (++history->links > ASTRA_VFS_LINK_MAX)
                    return ASTRA_VFS_ERR_LOOP;
                status = astra_vfs_readlink(serving, prefix_wire, target,
                                            ASTRA_VFS_PATH_MAX - 1u, &length);
                if (status != ASTRA_VFS_OK)
                    return status;
                if (length == 0u || length >= ASTRA_VFS_PATH_MAX)
                    return ASTRA_VFS_ERR_INVALID;
                target[length] = '\0';
                for (uint32_t index = 0u; index < parent_length; ++index)
                    parent[index] = rest[index];
                parent[parent_length] = '\0';
                status = astra_path_qualify(assign, parent, target,
                                            replacement,
                                            ASTRA_VFS_PATH_MAX);
                if (status != ASTRA_VFS_OK ||
                    !copy_path_to(combined, ASTRA_VFS_PATH_MAX, replacement))
                    return ASTRA_VFS_ERR_INVALID;
                status = append_path(combined, ASTRA_VFS_PATH_MAX, rest + end);
                if (status != ASTRA_VFS_OK)
                    return status;
                status = astra_path_collapse(combined, current, ASTRA_VFS_PATH_MAX);
                if (status != ASTRA_VFS_OK)
                    return status;
                break;                  /* restart after replacing this hop */
            }
            if (trailing && found.kind != ASTRA_VFS_KIND_DIRECTORY)
                return ASTRA_VFS_ERR_NOT_DIR;
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

static uint32_t follow_path(const AstraAssignTable *table, const char *path,
                            uint32_t rights,
                            AstraVfsAssignClientFn client_for, void *context,
                            uint32_t flags, int allow_missing_final,
                            char *logical, uint32_t logical_capacity,
                            AstraVfsDirEntry *entry, AstraVfsClient **client,
                            const AstraAssign **found_assign,
                            uint32_t *member, char *wire, uint32_t capacity)
{
    AstraVfsPathHistory history = {0};
    uint32_t status = follow_path_inner(
        table, path, rights, client_for, context, flags,
        allow_missing_final, logical, logical_capacity, entry, client,
        found_assign, member, wire, capacity, &history);

    astra_runtime_deallocate(history.paths);
    return status;
}

uint32_t
astra_vfs_assign_lstat(const AstraAssignTable *table, const char *path,
                       uint32_t rights,
                       AstraVfsAssignClientFn client_for, void *context,
                       char *wire, uint32_t capacity, AstraVfsDirEntry *entry,
                       AstraVfsClient **client,
                       const AstraAssign **found_assign, uint32_t *member)
{
    AstraVfsPathStorage logical_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *logical = logical_storage.bytes;
    if (logical == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return follow_path(table, path, rights, client_for, context, 0, 0,
                       logical, ASTRA_VFS_PATH_MAX, entry, client, found_assign,
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
    AstraVfsPathStorage logical_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *logical = logical_storage.bytes;
    if (logical == NULL)
        return ASTRA_VFS_ERR_LIMIT;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL)
        return ASTRA_VFS_ERR_INVALID;
    return follow_path(table, path, rights, client_for, context, 1, 0,
                       logical, ASTRA_VFS_PATH_MAX, entry, client, found_assign,
                       member, wire, capacity);
}

uint32_t
astra_vfs_assign_resolve_links(const AstraAssignTable *table,
                               const char *path, uint32_t rights,
    uint32_t flags, int allow_missing_final,
                               AstraVfsAssignClientFn client_for,
                               void *context, char *logical,
                               uint32_t capacity)
{
    AstraVfsPathStorage resolved_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *resolved = resolved_storage.bytes;
    if (resolved == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    AstraVfsPathStorage wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *wire = wire_storage.bytes;
    if (wire == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status;

    if (logical == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    status = follow_path(table, path, rights, client_for, context,
                         flags, allow_missing_final, resolved,
                         ASTRA_VFS_PATH_MAX, NULL, NULL, NULL, NULL, wire,
                         ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_ESCAPED)
        return status;
    return copy_path_to(logical, capacity, resolved) ? status :
                                                      ASTRA_VFS_ERR_INVALID;
}

uint32_t
astra_vfs_assign_destination(const AstraAssignTable *table, const char *path,
                             uint32_t rights, uint32_t flags,
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
                         flags & ~ASTRA_VFS_LOOKUP_MISSING,
                         (flags & ASTRA_VFS_LOOKUP_MISSING) != 0u,
                         logical, logical_capacity, NULL,
                         client, NULL, member, wire, wire_capacity);
    if (status != ASTRA_VFS_OK || *client != NULL)
        return status;
    {
        char name[ASTRA_ASSIGN_NAME_MAX];
        AstraVfsPathStorage rest_storage ASTRA_AUTO_VFS_PATH_STORAGE =
            astra_vfs_path_storage_open(1u);
        char *rest = rest_storage.bytes;
        if (rest == NULL)
            return ASTRA_VFS_ERR_LIMIT;

        if ((rights & ASTRA_RIGHT_WRITE) != 0u &&
            astra_path_split(logical, name, sizeof(name), rest, ASTRA_VFS_PATH_MAX) == ASTRA_VFS_OK &&
            rest[0] == '\0' && astra_assign_lookup(table, name) == NULL)
            return ASTRA_VFS_ERR_READ_ONLY;
    }
    return astra_vfs_assign_primary(table, logical, rights, client_for,
                                    context, wire, wire_capacity, client,
                                    member);
}

uint32_t astra_vfs_assign_destination_at(const AstraAssignTable *table,
    AstraVfsClient *directory_client, AstraVfsFile directory, const char *path,
    uint32_t rights, uint32_t flags, AstraVfsAssignClientFn client_for,
    void *context, char *wire, uint32_t capacity, AstraVfsClient **client,
    AstraVfsFile *base, const AstraAssign **assign, uint32_t *member,
    AstraVfsDirEntry *entry)
{
    AstraVfsDirEntry meta = {0};
    AstraVfsPathStorage escape_storage ASTRA_AUTO_VFS_PATH_STORAGE =
                astra_vfs_path_storage_open(1u);
            if (escape_storage.bytes == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            AstraVfsEscape escape = {.path = (uint8_t *)escape_storage.bytes,
                .capacity = ASTRA_VFS_ESCAPE_PATH_MAX};
    AstraVfsPathHistory history = {0};
    AstraVfsPathStorage logical_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *logical = logical_storage.bytes;
    if (logical == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    AstraVfsPathStorage continued_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *continued = continued_storage.bytes;
    if (continued == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status;

    if (table == NULL || directory_client == NULL || directory == ASTRA_VFS_FILE_INVALID ||
        path == NULL || path[0] == '\0' || path[0] == '/' || client_for == NULL ||
        wire == NULL || client == NULL || base == NULL || assign == NULL || member == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *client = NULL;
    *base = ASTRA_VFS_FILE_INVALID;
    *assign = NULL;
    *member = 0u;
    status = astra_vfs_lookup(directory_client, directory, path, flags,
                              ASTRA_VFS_LINK_MAX, &meta, &escape);
    if (status == ASTRA_VFS_OK) {
        if (!copy_path_to(wire, capacity, path))
            return ASTRA_VFS_ERR_INVALID;
        *client = directory_client;
        *base = directory;
        if (entry != NULL)
            *entry = meta;
        return ASTRA_VFS_OK;
    }
    if (status == ASTRA_VFS_ERR_ESCAPED) {
        status = continue_escape(continued, &escape, "", &history);
    } else if (status == ASTRA_VFS_ERR_UNSUPPORTED) {
        AstraVfsPathStorage provider_storage ASTRA_AUTO_VFS_PATH_STORAGE =
            astra_vfs_path_storage_open(1u);
        char *provider = provider_storage.bytes;
        if (provider == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        uint32_t length = 0u;

        status = astra_vfs_directory_path(directory_client, directory, provider,
                                          ASTRA_VFS_PATH_MAX - 1u, &length);
        if (status != ASTRA_VFS_OK)
            return status;
        if (length >= ASTRA_VFS_PATH_MAX)
            return ASTRA_VFS_ERR_PROTOCOL;
        provider[length] = '\0';
        status = ASTRA_VFS_ERR_NOT_FOUND;
        for (uint32_t index = 0u; index < table->count; ++index) {
            const AstraAssign *binding = &table->entries[index];

            if (client_for(binding, context) != directory_client)
                continue;
            status = astra_path_qualify_provider(binding->name, binding->root,
                provider, path, continued, ASTRA_VFS_PATH_MAX);
            if (status == ASTRA_VFS_OK)
                break;
            if (status != ASTRA_VFS_ERR_NOT_FOUND)
                return status;
        }
    }
    if (status != ASTRA_VFS_OK)
        return status;
    status = follow_path_inner(table, continued, rights, client_for, context,
        flags & ~ASTRA_VFS_LOOKUP_MISSING, (flags & ASTRA_VFS_LOOKUP_MISSING) != 0u,
        logical, ASTRA_VFS_PATH_MAX, entry, client, assign, member, wire, capacity, &history);
    astra_runtime_deallocate(history.paths);
    if (status == ASTRA_VFS_ERR_ESCAPED && !copy_path_to(wire, capacity, logical))
        return ASTRA_VFS_ERR_INVALID;
    if (status != ASTRA_VFS_OK || *client != NULL)
        return status;
    status = astra_vfs_assign_primary(table, logical, rights, client_for, context,
                                      wire, capacity, client, member);
    if (status == ASTRA_VFS_OK)
        status = astra_assign_resolve_raw(table, logical, rights, *member,
                                          wire, capacity, assign);
    return status;
}

uint32_t
astra_vfs_union_directory_open(const AstraAssignTable *table,
                               const char *path,
                               AstraVfsAssignClientFn client_for,
                               void *context,
                               AstraVfsUnionDirectory *directory)
{
    AstraVfsDirEntry entry = {0};
    AstraVfsPathStorage wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(2u);
    char *wire = astra_vfs_path_storage_at(&wire_storage, 1u);
    char *logical = wire_storage.bytes;
    if (wire == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status;

    if (table == NULL || client_for == NULL || directory == NULL)
        return ASTRA_VFS_ERR_INVALID;
    /* Open owns every field: read and close trust client and file. */
    *directory = (AstraVfsUnionDirectory)ASTRA_VFS_UNION_DIRECTORY_INIT;
    status = follow_path(table, path, ASTRA_RIGHT_READ, client_for, context,
                         1, 0, logical, ASTRA_VFS_PATH_MAX,
                         &entry, NULL, NULL, NULL, wire, ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK)
        return status;
    if (entry.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    directory->path = astra_runtime_allocate(strlen(logical) + 1u);
    if (directory->path == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    strcpy(directory->path, logical);
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
        (void)astra_vfs_file_owner_close(directory->client, directory->file,
            directory->ownership_epoch);
    directory->client = NULL;
    directory->file = ASTRA_VFS_FILE_INVALID;
    directory->ownership_epoch = 0u;
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
    if (directory->client != NULL && directory->file != ASTRA_VFS_FILE_INVALID &&
        astra_vfs_file_owner_status(directory->client, directory->ownership_epoch) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_LOST;
    if (directory->done != 0u)
        return ASTRA_VFS_OK;
    for (;;) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        AstraVfsPathStorage wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
            astra_vfs_path_storage_open(1u);
        char *wire = wire_storage.bytes;
        if (wire == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        uint64_t next = 0u;
        uint32_t found = 0u;
        uint32_t status = astra_assign_resolve_raw(
            directory->table, directory->path, ASTRA_RIGHT_READ,
            directory->member, wire, ASTRA_VFS_PATH_MAX, &assign);

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
            directory->ownership_epoch = client->ownership_epoch;
        }
        status = astra_vfs_readdir_file_batch(
            client, directory->file, wire,
            client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS ?
                ASTRA_VFS_CURSOR_CURRENT : directory->cursor, entries,
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
        astra_runtime_deallocate(directory->path);
        *directory = (AstraVfsUnionDirectory)
            ASTRA_VFS_UNION_DIRECTORY_INIT;
    }
}

uint32_t
astra_vfs_assign_open(const AstraAssignTable *table, const char *path,
                      uint32_t rights, uint32_t flags,
                      AstraVfsAssignClientFn client_for, void *context,
                      char *wire, uint32_t capacity, AstraVfsFile *file,
                      uint64_t *size, uint16_t *kind, AstraVfsClient **client,
                      uint32_t *member)
{
    AstraVfsPathStorage logical_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *logical = logical_storage.bytes;
    if (logical == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    AstraVfsPathStorage followed_wire_storage ASTRA_AUTO_VFS_PATH_STORAGE =
        astra_vfs_path_storage_open(1u);
    char *followed_wire = followed_wire_storage.bytes;
    if (followed_wire == NULL)
        return ASTRA_VFS_ERR_LIMIT;
    const AstraAssign *assign = NULL;
    AstraVfsClient *serving = NULL;
    uint32_t selected = 0u;
    uint32_t status;

    if (table == NULL || path == NULL || client_for == NULL || wire == NULL ||
        file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    /* A joined assign must select its namespace entry before opening bytes. */
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
                  ASTRA_VFS_OPEN_EXCLUSIVE)) == 0u &&
        !astra_path_requires_walk(path) &&
        astra_assign_resolve_raw(table, path, rights, 0u, wire, capacity,
                                &assign) == ASTRA_VFS_OK &&
        astra_assign_member(table, assign->name, 1u) == NULL) {
        serving = client_for(assign, context);
        if (serving != NULL) {
            status = astra_vfs_open(serving, wire, flags, file, size, kind);
            if (status == ASTRA_VFS_OK)
                goto opened;
        }
    }
    if ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
        (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        uint32_t exists = astra_vfs_assign_lstat(
            table, path, rights, client_for, context, followed_wire,
            ASTRA_VFS_PATH_MAX, NULL, NULL, NULL, NULL);

        if (exists == ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_EXISTS;
        if (exists != ASTRA_VFS_ERR_NOT_FOUND)
            return exists;
    }
    status = astra_vfs_assign_destination(
        table, path, rights, ((flags & ASTRA_VFS_OPEN_NOFOLLOW) ? 0u : ASTRA_VFS_LOOKUP_FOLLOW) |
        ((flags & ASTRA_VFS_OPEN_CREATE) ? ASTRA_VFS_LOOKUP_MISSING : 0u),
        client_for, context, logical, ASTRA_VFS_PATH_MAX, wire, capacity,
        &serving, &selected);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_vfs_open(serving, wire, flags, file, size, kind);
    if (status != ASTRA_VFS_OK)
        return status;
opened:
    if (client != NULL)
        *client = serving;
    if (member != NULL)
        *member = selected;
    return ASTRA_VFS_OK;
}
