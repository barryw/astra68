#include "../../../common/directory_snapshot.h"
#include "../../../common/directory_query.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t allocations, frees, opens, closes, fail_open;
static uint32_t cancel_flag, seen;
static char paths[8][ASTRA_VFS_PATH_MAX];

void *astra_runtime_allocate(size_t bytes)
{
    void *memory = malloc(bytes);
    if (memory != NULL) ++allocations;
    return memory;
}

void astra_runtime_deallocate(void *memory)
{
    if (memory != NULL) ++frees;
    free(memory);
}

int astra_path_requires_walk(const char *path)
{
    return strstr(path, "/../") != NULL ||
        strstr(path, "/./") != NULL;
}

uint32_t astra_folder_path(const char *parent, const char *name,
    char *path, uint32_t capacity)
{
    int count = snprintf(path, capacity, "%s%s%s", parent,
        strcmp(parent, "/") == 0 ? "" : "/", name);
    return count >= 0 && (uint32_t)count < capacity ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_LIMIT;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
    const char *path, AstraDirectory *directory)
{
    (void)filesystem;
    if (fail_open && strcmp(path, "/dh0/sub") == 0)
        return ASTRA_VFS_ERR_IO;
    directory->_private_path = (char *)path;
    directory->_private_cursor = 0u;
    ++opens;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count)
{
    const char *names[5] = {NULL};
    uint16_t kinds[5] = {0u};
    uint32_t length = 0u;
    if (strcmp(directory->_private_path, "/dh0") == 0) {
        names[0] = "."; names[1] = "sub"; names[2] = "link";
        names[3] = ".Trash"; names[4] = "root.txt";
        kinds[1] = kinds[3] = ASTRA_VFS_KIND_DIRECTORY;
        kinds[2] = ASTRA_VFS_KIND_SYMLINK;
        kinds[4] = ASTRA_VFS_KIND_FILE;
        length = 5u;
    } else if (strcmp(directory->_private_path, "/dh0/sub") == 0) {
        names[0] = "child.txt"; names[1] = "nested";
        kinds[0] = ASTRA_VFS_KIND_FILE;
        kinds[1] = ASTRA_VFS_KIND_DIRECTORY;
        length = 2u;
    } else if (strcmp(directory->_private_path, "/dh0/sub/nested") == 0) {
        names[0] = "end.txt";
        kinds[0] = ASTRA_VFS_KIND_FILE;
        length = 1u;
    } else if (strcmp(directory->_private_path, "/dh0/.Trash") != 0) {
        assert(0 && "unexpected directory descent");
    }
    *count = 0u;
    while (*count < capacity && directory->_private_cursor < length) {
        uint32_t index = (uint32_t)directory->_private_cursor++;
        AstraDirectoryEntry *entry = &entries[(*count)++];
        memset(entry, 0, sizeof(*entry));
        strcpy(entry->name, names[index]);
        entry->kind = kinds[index];
        entry->mtime = 100 + index;
        entry->meta.valid = ASTRA_VFS_META_MTIME;
    }
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{
    (void)directory;
    ++closes;
}

static uint32_t visit(void *context, const char *path,
    const AstraDirectoryEntry *entry, uint32_t *descend)
{
    (void)context;
    assert(seen < 8u);
    strcpy(paths[seen++], path);
    if (strcmp(entry->name, ".Trash") == 0) *descend = 0u;
    return ASTRA_VFS_OK;
}

static uint32_t cancel_visit(void *context, const char *path,
    const AstraDirectoryEntry *entry, uint32_t *descend)
{
    uint32_t status = visit(context, path, entry, descend);
    cancel_flag = 1u;
    return status;
}

static void reset(void)
{
    allocations = frees = opens = closes = fail_open = cancel_flag = seen = 0u;
}

int main(void)
{
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraDirectoryQuerySnapshot matches = ASTRA_DIRECTORY_QUERY_SNAPSHOT_INIT;
    reset();
    assert(astra_directory_tree_walk(&filesystem, "/dh0", visit, NULL,
        NULL) == ASTRA_VFS_OK);
    assert(seen == 7u);
    assert(strcmp(paths[0], "/dh0/sub") == 0);
    assert(strcmp(paths[1], "/dh0/sub/child.txt") == 0);
    assert(strcmp(paths[2], "/dh0/sub/nested") == 0);
    assert(strcmp(paths[3], "/dh0/sub/nested/end.txt") == 0);
    assert(strcmp(paths[4], "/dh0/link") == 0);
    assert(strcmp(paths[5], "/dh0/.Trash") == 0);
    assert(strcmp(paths[6], "/dh0/root.txt") == 0);
    assert(opens == 3u && closes == opens && allocations == frees);

    reset();
    fail_open = 1u;
    assert(astra_directory_tree_walk(&filesystem, "/dh0", visit, NULL,
        NULL) == ASTRA_VFS_ERR_IO);
    assert(opens == closes && allocations == frees);

    reset();
    assert(astra_directory_tree_walk(&filesystem, "/dh0", cancel_visit,
        NULL, &cancel_flag) == ASTRA_DIRECTORY_SNAPSHOT_CANCELLED);
    assert(seen == 1u && opens == closes && allocations == frees);
    assert(astra_directory_tree_walk(&filesystem, "/dh0/../dh0", visit,
        NULL, NULL) == ASTRA_VFS_ERR_INVALID);

    reset();
    AstraDirectoryQuerySpec query = {
        .name_contains = ".TXT", .recursive = 1u
    };
    assert(astra_directory_query_load(&matches, &filesystem, "/dh0",
        &query, NULL) == ASTRA_VFS_OK);
    assert(matches.count == 3u);
    assert(strcmp(matches.items[0].path, "/dh0/sub/child.txt") == 0);
    assert(strcmp(matches.items[1].path, "/dh0/sub/nested/end.txt") == 0);
    assert(strcmp(matches.items[2].path, "/dh0/root.txt") == 0);
    astra_directory_query_dispose(&matches);
    assert(opens == closes && allocations == frees);

    reset();
    query = (AstraDirectoryQuerySpec){
        .use_modified_from = 1u, .modified_from = 104,
        .recursive = 0u
    };
    assert(astra_directory_query_load(&matches, &filesystem, "/dh0",
        &query, NULL) == ASTRA_VFS_OK);
    assert(matches.count == 1u);
    assert(strcmp(matches.items[0].path, "/dh0/root.txt") == 0);
    astra_directory_query_dispose(&matches);
    assert(opens == closes && allocations == frees);

    reset();
    fail_open = 1u;
    query = (AstraDirectoryQuerySpec){.recursive = 1u};
    assert(astra_directory_query_load(&matches, &filesystem, "/dh0",
        &query, NULL) == ASTRA_VFS_ERR_IO);
    assert(matches.count == 0u && matches.items == NULL);
    assert(opens == closes && allocations == frees);
    puts("ATLAS DIRECTORY TREE PASS");
    return 0;
}
