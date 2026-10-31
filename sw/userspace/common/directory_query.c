#include "directory_query.h"

#include <astra/ascii.h>
#include <astra/runtime.h>

#include <string.h>

typedef struct QueryContext {
    AstraDirectoryQuerySnapshot *snapshot;
    const AstraDirectoryQuerySpec *spec;
} QueryContext;

void astra_directory_query_dispose(AstraDirectoryQuerySnapshot *snapshot)
{
    if (snapshot == NULL) return;
    for (uint32_t at = 0u; at < snapshot->count; ++at)
        astra_runtime_deallocate(snapshot->items[at].path);
    astra_runtime_deallocate(snapshot->items);
    *snapshot = (AstraDirectoryQuerySnapshot)ASTRA_DIRECTORY_QUERY_SNAPSHOT_INIT;
}

static int contains_name(const char *name, const char *needle)
{
    if (needle == NULL || needle[0] == '\0') return 1;
    for (const char *start = name; *start != '\0'; ++start) {
        const char *left = start, *right = needle;
        while (*left != '\0' && *right != '\0' &&
            astra_ascii_lower((unsigned char)*left) ==
            astra_ascii_lower((unsigned char)*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') return 1;
    }
    return 0;
}

static uint32_t append(AstraDirectoryQuerySnapshot *snapshot,
    const char *path, const AstraDirectoryEntry *entry)
{
    if (snapshot->count == snapshot->capacity) {
        uint32_t capacity = snapshot->capacity == 0u ? 32u :
            snapshot->capacity * 2u;
        if (capacity <= snapshot->capacity ||
            capacity > UINT32_MAX / sizeof(*snapshot->items))
            return ASTRA_VFS_ERR_LIMIT;
        AstraDirectoryQueryItem *items = astra_runtime_allocate(
            (size_t)capacity * sizeof(*items));
        if (items == NULL) return ASTRA_VFS_ERR_LIMIT;
        if (snapshot->count != 0u)
            memcpy(items, snapshot->items,
                (size_t)snapshot->count * sizeof(*items));
        astra_runtime_deallocate(snapshot->items);
        snapshot->items = items;
        snapshot->capacity = capacity;
    }
    size_t length = strlen(path);
    char *copy = astra_runtime_allocate(length + 1u);
    if (copy == NULL) return ASTRA_VFS_ERR_LIMIT;
    memcpy(copy, path, length + 1u);
    snapshot->items[snapshot->count++] = (AstraDirectoryQueryItem){copy, *entry};
    return ASTRA_VFS_OK;
}

static uint32_t visit(void *opaque, const char *path,
    const AstraDirectoryEntry *entry, uint32_t *descend)
{
    QueryContext *context = opaque;
    const AstraDirectoryQuerySpec *spec = context->spec;
    if (!spec->recursive || (entry->name[0] == '.' &&
        !spec->include_hidden)) *descend = 0u;
    if (entry->name[0] == '.' && !spec->include_hidden) return ASTRA_VFS_OK;
    if (!contains_name(entry->name, spec->name_contains)) return ASTRA_VFS_OK;
    if (spec->use_modified_from || spec->use_modified_before) {
        if ((entry->meta.valid & ASTRA_VFS_META_MTIME) == 0u)
            return ASTRA_VFS_OK;
        if (spec->use_modified_from && entry->mtime < spec->modified_from)
            return ASTRA_VFS_OK;
        if (spec->use_modified_before && entry->mtime >= spec->modified_before)
            return ASTRA_VFS_OK;
    }
    return append(context->snapshot, path, entry);
}

uint32_t astra_directory_query_load(AstraDirectoryQuerySnapshot *snapshot,
    AstraFilesystem *filesystem, const char *directory,
    const AstraDirectoryQuerySpec *spec, const uint32_t *cancelled)
{
    if (snapshot == NULL || filesystem == NULL || directory == NULL ||
        spec == NULL || (spec->name_contains != NULL &&
        strlen(spec->name_contains) >= ASTRA_VFS_NAME_MAX) ||
        spec->recursive > 1u || spec->include_hidden > 1u ||
        spec->use_modified_from > 1u || spec->use_modified_before > 1u ||
        (spec->use_modified_from && spec->use_modified_before &&
         spec->modified_from >= spec->modified_before))
        return ASTRA_VFS_ERR_INVALID;
    astra_directory_query_dispose(snapshot);
    QueryContext context = {snapshot, spec};
    uint32_t status = astra_directory_tree_walk(filesystem, directory,
        visit, &context, cancelled);
    if (status != ASTRA_VFS_OK) astra_directory_query_dispose(snapshot);
    snapshot->status = status;
    return status;
}
