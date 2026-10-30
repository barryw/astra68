#include <astra/saved_query.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
static uint8_t staged[8192], published[8192];
static uint32_t staged_bytes, published_bytes, store_exists, has_stage, has_published;
static uint32_t write_limit = 3u;
static char staged_path[ASTRA_VFS_PATH_MAX], published_path[ASTRA_VFS_PATH_MAX];

void *astra_runtime_allocate(size_t bytes) { return malloc(bytes); }
void *astra_runtime_reallocate(void *pointer, size_t bytes)
{ return realloc(pointer, bytes); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
uint64_t astra_clock_monotonic(void) { return UINT64_C(0x1234); }
int astra_path_requires_walk(const char *path)
{ return strstr(path, "/../") != NULL || strstr(path, "/./") != NULL; }

uint32_t astra_filesystem_lstat(AstraFilesystem *fs, const char *path,
    AstraFileInfo *info)
{
    assert(fs == &filesystem && info != NULL);
    if (strcmp(path, "/home/Saved Searches") == 0 && store_exists) {
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *fs, const char *path,
    uint16_t mode)
{
    assert(fs == &filesystem && strcmp(path, "/home/Saved Searches") == 0 &&
        mode == 0700u);
    store_exists = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *fs, const char *path,
    uint32_t flags, uint16_t mode, AstraFile *file)
{
    assert(fs == &filesystem && file != NULL && mode == 0600u &&
        (flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
            (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE));
    if (has_stage && strcmp(path, staged_path) == 0)
        return ASTRA_VFS_ERR_EXISTS;
    assert(strstr(path, ".tmp") != NULL);
    strcpy(staged_path, path);
    staged_bytes = 0u;
    has_stage = 1u;
    file->_private_file = 1u;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_write(AstraFile *file, uint64_t max_end,
    const void *data, uint32_t length, uint32_t *moved)
{
    assert(file->_private_file == 1u && max_end == UINT64_MAX &&
        staged_bytes + length <= sizeof(staged));
    *moved = length < write_limit ? length : write_limit;
    memcpy(staged + staged_bytes, data, *moved);
    staged_bytes += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{ assert(file->_private_file == 1u); return ASTRA_VFS_OK; }

uint32_t astra_filesystem_close(AstraFile *file)
{ assert(file->_private_file == 1u || file->_private_file == 2u);
  file->_private_file = ASTRA_VFS_FILE_INVALID; return ASTRA_VFS_OK; }

uint32_t astra_filesystem_link(AstraFilesystem *fs, const char *from,
    const char *to)
{
    assert(fs == &filesystem && has_stage && strcmp(from, staged_path) == 0);
    if (has_published && strcmp(to, published_path) == 0)
        return ASTRA_VFS_ERR_EXISTS;
    strcpy(published_path, to);
    memcpy(published, staged, staged_bytes);
    published_bytes = staged_bytes;
    has_published = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *fs, const char *path,
    uint32_t flags)
{
    assert(fs == &filesystem && flags == 0u && has_stage &&
        strcmp(path, staged_path) == 0);
    has_stage = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *fs, const char *path,
    uint32_t flags, AstraFile *file)
{
    assert(fs == &filesystem && flags == ASTRA_VFS_OPEN_READ);
    if (!has_published || strcmp(path, published_path) != 0)
        return ASTRA_VFS_ERR_NOT_FOUND;
    file->_private_file = 2u;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_info(const AstraFile *file,
    AstraFileInfo *info)
{
    assert(file->_private_file == 2u && info != NULL);
    info->kind = ASTRA_VFS_KIND_FILE;
    info->byte_size = published_bytes;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *data,
    uint32_t length, uint32_t *moved)
{
    assert(file->_private_file == 2u);
    uint32_t remaining = published_bytes - (uint32_t)file->_private_offset;
    *moved = length < remaining ? length : remaining;
    if (*moved > 4u) *moved = 4u;
    memcpy(data, published + file->_private_offset, *moved);
    file->_private_offset += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *fs,
    const char *path, AstraDirectory *directory)
{
    assert(fs == &filesystem && strcmp(path, "/home/Saved Searches") == 0);
    if (!store_exists) return ASTRA_VFS_ERR_NOT_FOUND;
    directory->_private_cursor = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count)
{
    assert(capacity >= 1u && entries != NULL && count != NULL);
    *count = 0u;
    if (!has_published || directory->_private_cursor != 0u)
        return ASTRA_VFS_OK;
    const char *name = published_path + strlen("/home/Saved Searches/");
    strcpy(entries[0].name, name);
    *count = 1u;
    directory->_private_cursor = 1u;
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{ (void)directory; }

int main(void)
{
    AstraSavedQuery query = ASTRA_SAVED_QUERY_INIT;
    AstraSavedQuery loaded = ASTRA_SAVED_QUERY_INIT;
    AstraSavedQuerySnapshot snapshot = ASTRA_SAVED_QUERY_SNAPSHOT_INIT;
    char filename[ASTRA_VFS_NAME_MAX];
    strcpy(query.label, "Doom files");
    strcpy(query.directory, "/dh0");
    strcpy(query.text, "doom");
    assert(astra_saved_query_save(&filesystem, "/home/Saved Searches",
        &query, filename, sizeof(filename)) == ASTRA_VFS_OK);
    assert(store_exists && has_published && !has_stage);
    assert(strcmp(filename, published_path +
        strlen("/home/Saved Searches/")) == 0);
    assert(astra_saved_query_load(&filesystem, published_path, &loaded) ==
        ASTRA_VFS_OK);
    assert(strcmp(loaded.label, query.label) == 0 &&
        strcmp(loaded.directory, query.directory) == 0 &&
        strcmp(loaded.text, query.text) == 0);
    assert(astra_saved_query_snapshot_open(&filesystem,
        "/home/Saved Searches", &snapshot) == ASTRA_VFS_OK &&
        snapshot.count == 1u);
    astra_saved_query_snapshot_close(&snapshot);
    published[0] = '?';
    assert(astra_saved_query_load(&filesystem, published_path, &loaded) ==
        ASTRA_VFS_ERR_INVALID);
    assert(astra_saved_query_snapshot_open(&filesystem,
        "/home/Saved Searches", &snapshot) == ASTRA_VFS_OK &&
        snapshot.count == 0u);
    astra_saved_query_snapshot_close(&snapshot);
    published[0] = 'A';
    --published_bytes;
    assert(astra_saved_query_load(&filesystem, published_path, &loaded) ==
        ASTRA_VFS_ERR_INVALID);
    puts("saved query tests passed");
    return 0;
}
