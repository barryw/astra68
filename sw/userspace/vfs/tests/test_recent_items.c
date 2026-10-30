#include <astra/recent_items.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_CAPACITY 120u
#define DATA_CAPACITY (ASTRA_VFS_PATH_MAX + 64u)

typedef struct TestFile {
    char path[ASTRA_VFS_PATH_MAX];
    uint8_t bytes[DATA_CAPACITY];
    uint32_t length;
    uint32_t exists;
} TestFile;

static AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
static TestFile files[FILE_CAPACITY];
static uint32_t store_exists, syncs;
static uint32_t realtime_available = 1u;
static uint64_t time_ns = UINT64_C(1800000000000000000);
static uint64_t monotonic_ns = 1u;

void *astra_runtime_allocate(size_t bytes) { return malloc(bytes); }
void *astra_runtime_reallocate(void *pointer, size_t bytes)
{ return realloc(pointer, bytes); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
uint64_t astra_clock_monotonic(void) { return monotonic_ns++; }
uint32_t astra_clock_realtime(uint64_t *value)
{ if (!realtime_available) return 1u;
  *value = ++time_ns; return ASTRA_SYSCALL_OK; }
int astra_path_requires_walk(const char *path)
{ return strstr(path, "/../") != NULL || strstr(path, "/./") != NULL; }

static TestFile *find_file(const char *path)
{
    for (uint32_t at = 0u; at < FILE_CAPACITY; ++at)
        if (files[at].exists && strcmp(files[at].path, path) == 0)
            return &files[at];
    return NULL;
}

static TestFile *new_file(const char *path)
{
    for (uint32_t at = 0u; at < FILE_CAPACITY; ++at)
        if (!files[at].exists) {
            TestFile *file = &files[at];
            assert(strlen(path) < sizeof(file->path));
            strcpy(file->path, path);
            file->length = 0u;
            file->exists = 1u;
            return file;
        }
    assert(!"test file capacity");
    return NULL;
}

static TestFile *open_file(const AstraFile *file)
{
    assert(file->_private_file > 0u &&
        file->_private_file <= FILE_CAPACITY);
    TestFile *slot = &files[file->_private_file - 1u];
    assert(slot->exists);
    return slot;
}

uint32_t astra_filesystem_lstat(AstraFilesystem *fs, const char *path,
    AstraFileInfo *info)
{
    assert(fs == &filesystem);
    if (strcmp(path, "/home/Recent Items") == 0 && store_exists) {
        info->kind = ASTRA_VFS_KIND_DIRECTORY;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *fs, const char *path,
    uint16_t mode)
{
    assert(fs == &filesystem && strcmp(path, "/home/Recent Items") == 0 &&
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
    if (find_file(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    TestFile *slot = new_file(path);
    file->_private_file = (uint32_t)(slot - files) + 1u;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *fs, const char *path,
    uint32_t flags, AstraFile *file)
{
    assert(fs == &filesystem && flags == ASTRA_VFS_OPEN_READ);
    TestFile *slot = find_file(path);
    if (slot == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    file->_private_file = (uint32_t)(slot - files) + 1u;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_write(AstraFile *file, uint64_t maximum,
    const void *bytes, uint32_t length, uint32_t *moved)
{
    TestFile *slot = open_file(file);
    assert(maximum == UINT64_MAX && slot->length + length <= DATA_CAPACITY);
    *moved = length < 7u ? length : 7u;
    memcpy(slot->bytes + slot->length, bytes, *moved);
    slot->length += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{ (void)open_file(file); ++syncs; return ASTRA_VFS_OK; }

uint32_t astra_filesystem_close(AstraFile *file)
{ (void)open_file(file); file->_private_file = ASTRA_VFS_FILE_INVALID;
  return ASTRA_VFS_OK; }

uint32_t astra_filesystem_link(AstraFilesystem *fs, const char *from,
    const char *to)
{
    assert(fs == &filesystem);
    TestFile *source = find_file(from);
    assert(source != NULL);
    if (find_file(to) != NULL) return ASTRA_VFS_ERR_EXISTS;
    TestFile *target = new_file(to);
    memcpy(target->bytes, source->bytes, source->length);
    target->length = source->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *fs, const char *path,
    uint32_t flags)
{
    assert(fs == &filesystem && flags == 0u);
    TestFile *slot = find_file(path);
    if (slot == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    slot->exists = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_info(const AstraFile *file, AstraFileInfo *info)
{
    TestFile *slot = open_file(file);
    info->kind = ASTRA_VFS_KIND_FILE;
    info->byte_size = slot->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *bytes,
    uint32_t capacity, uint32_t *moved)
{
    TestFile *slot = open_file(file);
    uint32_t remain = slot->length - (uint32_t)file->_private_offset;
    *moved = capacity < remain ? capacity : remain;
    if (*moved > 7u) *moved = 7u;
    memcpy(bytes, slot->bytes + file->_private_offset, *moved);
    file->_private_offset += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *fs,
    const char *path, AstraDirectory *directory)
{
    assert(fs == &filesystem && strcmp(path, "/home/Recent Items") == 0);
    if (!store_exists) return ASTRA_VFS_ERR_NOT_FOUND;
    directory->_private_cursor = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count)
{
    *count = 0u;
    while (directory->_private_cursor < FILE_CAPACITY && *count < capacity) {
        TestFile *slot = &files[directory->_private_cursor++];
        if (!slot->exists) continue;
        const char *name = strrchr(slot->path, '/');
        assert(name != NULL);
        strcpy(entries[*count].name, name + 1u);
        ++*count;
    }
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{ directory->_private_cursor = 0u; }

static uint32_t published_count(void)
{
    uint32_t count = 0u;
    for (uint32_t at = 0u; at < FILE_CAPACITY; ++at)
        if (files[at].exists && strstr(files[at].path, ".ari") != NULL)
            ++count;
    return count;
}

int main(void)
{
    AstraRecentItemSnapshot snapshot = ASTRA_RECENT_ITEM_SNAPSHOT_INIT;
    AstraVolumeReference volume = {7u, 8u, 9u};
    assert(astra_recent_item_snapshot_open(&filesystem,
        "/home/Recent Items", &snapshot) == ASTRA_VFS_OK);
    assert(snapshot.count == 0u);
    assert(astra_recent_item_record(&filesystem, "/home/Recent Items",
        "/usb0/report.txt", ASTRA_VFS_KIND_FILE, &volume) == ASTRA_VFS_OK);
    assert(syncs == 1u && published_count() == 1u);
    assert(astra_recent_item_record(&filesystem, "/home/Recent Items",
        "/usb0/report.txt", ASTRA_VFS_KIND_FILE, &volume) == ASTRA_VFS_OK);
    assert(astra_recent_item_snapshot_open(&filesystem,
        "/home/Recent Items", &snapshot) == ASTRA_VFS_OK);
    assert(snapshot.count == 1u &&
        strcmp(snapshot.items[0].path, "/usb0/report.txt") == 0 &&
        snapshot.items[0].volume.generation == 9u);
    astra_recent_item_snapshot_close(&snapshot);
    realtime_available = 0u;
    assert(astra_recent_item_record(&filesystem, "/home/Recent Items",
        "/home/offline", ASTRA_VFS_KIND_DIRECTORY, NULL) == ASTRA_VFS_OK);
    assert(astra_recent_item_snapshot_open(&filesystem,
        "/home/Recent Items", &snapshot) == ASTRA_VFS_OK);
    assert(snapshot.count == 2u &&
        strcmp(snapshot.items[0].path, "/home/offline") == 0);
    astra_recent_item_snapshot_close(&snapshot);
    realtime_available = 1u;
    time_ns = 1u;
    assert(astra_recent_item_record(&filesystem, "/home/Recent Items",
        "/home/../bad", ASTRA_VFS_KIND_FILE, NULL) ==
        ASTRA_VFS_ERR_INVALID);
    for (uint32_t at = 0u; at < 70u; ++at) {
        char path[40];
        snprintf(path, sizeof(path), "/home/file%02u", at);
        assert(astra_recent_item_record(&filesystem,
            "/home/Recent Items", path, ASTRA_VFS_KIND_FILE,
            NULL) == ASTRA_VFS_OK);
    }
    assert(published_count() == ASTRA_RECENT_ITEMS_LIMIT);
    assert(astra_recent_item_snapshot_open(&filesystem,
        "/home/Recent Items", &snapshot) == ASTRA_VFS_OK);
    assert(snapshot.count == ASTRA_RECENT_ITEMS_LIMIT);
    assert(strcmp(snapshot.items[0].path, "/home/file69") == 0);
    assert(strcmp(snapshot.items[ASTRA_RECENT_ITEMS_LIMIT - 1u].path,
        "/home/file06") == 0);
    astra_recent_item_snapshot_close(&snapshot);
    for (uint32_t at = 0u; at < FILE_CAPACITY; ++at)
        if (files[at].exists && strstr(files[at].path, ".ari") != NULL) {
            files[at].bytes[0] = 'X';
            break;
        }
    assert(astra_recent_item_snapshot_open(&filesystem,
        "/home/Recent Items", &snapshot) == ASTRA_VFS_OK);
    assert(snapshot.count == ASTRA_RECENT_ITEMS_LIMIT - 1u);
    astra_recent_item_snapshot_close(&snapshot);
    puts("recent item tests passed");
    return 0;
}
