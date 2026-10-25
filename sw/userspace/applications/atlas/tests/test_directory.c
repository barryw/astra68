#include "../directory.h"
#include <astra/status.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t total, cursor, closes, read_error, open_error, fail_after;
static uint32_t allocations;

void *astra_runtime_allocate(size_t bytes)
{
    if (++allocations == fail_after)
        return NULL;
    return malloc(bytes);
}
void astra_runtime_deallocate(void *bytes) { free(bytes); }

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
    const char *path, AstraDirectory *directory)
{
    (void)filesystem; (void)path; (void)directory;
    cursor = 0u;
    return open_error;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count)
{
    (void)directory;
    if (read_error != 0u && cursor >= 8u)
        return read_error;
    *count = 0u;
    while (*count < capacity && cursor < total) {
        AstraDirectoryEntry *entry = &entries[(*count)++];
        memset(entry, 0, sizeof(*entry));
        if (cursor == 0u)
            strcpy(entry->name, ".hidden");
        else if (cursor == 1u)
            strcpy(entry->name, "caf\xc3\xa9");
        else if (cursor == 2u) {
            memset(entry->name, 'p', sizeof(entry->name) - 1u);
            entry->name[sizeof(entry->name) - 1u] = '\0';
        }
        else
            (void)snprintf(entry->name, sizeof(entry->name), "file-%u", cursor);
        entry->kind = cursor % 2u == 0u ? ASTRA_VFS_KIND_DIRECTORY : ASTRA_VFS_KIND_FILE;
        entry->byte_size = cursor;
        ++cursor;
    }
    return ASTRA_VFS_OK;
}
void astra_filesystem_directory_close(AstraDirectory *directory)
{
    (void)directory;
    ++closes;
}

static void reset(uint32_t count)
{
    total = count; cursor = closes = read_error = open_error = fail_after = allocations = 0u;
}

int main(void)
{
    AtlasDirectory model = ATLAS_DIRECTORY_INIT;
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    uint32_t cancelled = 0u;

    reset(257u);
    assert(atlas_directory_load(&model, &filesystem, "/dh0", &cancelled) == ASTRA_VFS_OK);
    assert(model.count == 257u && closes == 1u && allocations > 1u);
    assert(strcmp(model.entries[0].name, ".hidden") == 0);
    assert(strcmp(model.entries[1].name, "caf\xc3\xa9") == 0);
    assert(strlen(model.entries[2].name) == ASTRA_VFS_NAME_MAX - 1u);
    assert(strcmp(model.entries[256].name, "file-256") == 0);
    assert(model.entries[256].byte_size == 256u &&
           model.entries[256].kind == ASTRA_VFS_KIND_DIRECTORY);
    atlas_directory_dispose(&model);
    reset(0u);
    assert(atlas_directory_load(&model, &filesystem, "/dh0", NULL) == ASTRA_VFS_OK);
    assert(model.count == 0u && closes == 1u);
    reset(257u);
    fail_after = 1u;
    assert(atlas_directory_load(&model, &filesystem, "/dh0", NULL) == ASTRA_VFS_ERR_LIMIT);
    assert(model.count == 0u && model.entries == NULL && closes == 0u);
    reset(257u);
    fail_after = 2u;
    assert(atlas_directory_load(&model, &filesystem, "/dh0", NULL) == ASTRA_VFS_ERR_LIMIT);
    assert(model.count == 0u && model.entries == NULL && closes == 1u);
    reset(257u);
    read_error = ASTRA_VFS_ERR_IO;
    assert(atlas_directory_load(&model, &filesystem, "/dh0", NULL) == ASTRA_VFS_ERR_IO);
    assert(model.count == 0u && closes == 1u);
    reset(1u);
    open_error = ASTRA_VFS_ERR_ACCESS;
    assert(atlas_directory_load(&model, &filesystem, "/dh0", NULL) == ASTRA_VFS_ERR_ACCESS);
    assert(closes == 0u);
    reset(1u);
    cancelled = 1u;
    assert(atlas_directory_load(&model, &filesystem, "/dh0", &cancelled) == ATLAS_DIRECTORY_CANCELLED);
    assert(model.count == 0u && closes == 1u);
    atlas_directory_dispose(&model);
    puts("ATLAS DIRECTORY PASS");
    return 0;
}
