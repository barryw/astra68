#include <astra/filesystem_library.h>
#include <astra/runtime.h>
#include <astra/string.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Entry {
    char path[ASTRA_VFS_PATH_MAX];
    char bytes[32];
    uint32_t length;
    uint16_t kind;
} Entry;

static Entry entries[128];
static uint32_t entry_count;
static int fail_write;
static int fail_unlink;
static uint32_t allocations;
static uint32_t fail_alloc_at;
static AstraFilesystem *expected_source;
static AstraFilesystem *expected_destination;

static void check_namespace(AstraFilesystem *filesystem, const char *path)
{
    if (expected_source == NULL) return;
    assert(filesystem == (strncmp(path, "/dh0/", 5u) == 0 ?
        expected_source : expected_destination));
}

static Entry *find(const char *path)
{
    for (uint32_t at = 0; at < entry_count; ++at)
        if (strcmp(entries[at].path, path) == 0) return &entries[at];
    return NULL;
}

static Entry *add(const char *path, uint16_t kind)
{
    assert(entry_count < 128u && find(path) == NULL);
    Entry *entry = &entries[entry_count++];
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->path, path);
    entry->kind = kind;
    return entry;
}

void *astra_runtime_allocate(size_t size)
{
    if (++allocations == fail_alloc_at) return NULL;
    return malloc(size);
}
void astra_runtime_deallocate(void *pointer) { free(pointer); }
uint64_t astra_clock_monotonic(void) { static uint64_t tick = 1u; return tick++; }

int astra_string_concat(char *target, uint32_t capacity, const char *suffix)
{
    size_t used = strlen(target), added = strlen(suffix);
    if (used + added >= capacity) return 0;
    memcpy(target + used, suffix, added + 1u);
    return 1;
}

void astra_string_init(AstraString *string, char *storage, uint32_t capacity)
{
    *string = (AstraString){.text = storage, .capacity = capacity};
    storage[0] = '\0';
}

int astra_string_append(AstraString *string, const char *text)
{
    if (!astra_string_concat(string->text, string->capacity, text)) return 0;
    string->length = (uint32_t)strlen(string->text);
    return 1;
}

int astra_string_append_hex(AstraString *string, uint64_t value,
                            uint32_t digits)
{
    char text[17];
    assert(digits == 16u);
    snprintf(text, sizeof(text), "%016llx", (unsigned long long)value);
    return astra_string_append(string, text);
}

uint32_t astra_filesystem_lstat(AstraFilesystem *filesystem, const char *path,
                               AstraFileInfo *info)
{
    check_namespace(filesystem, path);
    Entry *entry = find(path);
    if (entry == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    info->kind = entry->kind;
    info->mode = entry->kind == ASTRA_VFS_KIND_DIRECTORY ? 0755u : 0644u;
    info->byte_size = entry->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    check_namespace(filesystem, path); (void)flags;
    Entry *entry = find(path);
    if (entry == NULL || entry->kind != ASTRA_VFS_KIND_FILE)
        return ASTRA_VFS_ERR_NOT_FOUND;
    file->_private_file = (AstraVfsFile)(entry - entries + 1u);
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *filesystem,
    const char *path, uint32_t flags, uint16_t mode, AstraFile *file)
{
    check_namespace(filesystem, path); (void)flags; (void)mode;
    if (find(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    Entry *entry = add(path, ASTRA_VFS_KIND_FILE);
    file->_private_file = (AstraVfsFile)(entry - entries + 1u);
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_readlink(AstraFilesystem *filesystem,
    const char *path, void *buffer, uint32_t capacity, uint32_t *length)
{
    check_namespace(filesystem, path);
    Entry *entry = find(path);
    if (entry == NULL || entry->kind != ASTRA_VFS_KIND_SYMLINK)
        return ASTRA_VFS_ERR_NOT_FOUND;
    if (capacity < entry->length) return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memcpy(buffer, entry->bytes, entry->length);
    *length = entry->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_symlink(const char *target,
    AstraFilesystem *filesystem, const char *path)
{
    check_namespace(filesystem, path);
    if (find(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    Entry *entry = add(path, ASTRA_VFS_KIND_SYMLINK);
    entry->length = (uint32_t)strlen(target);
    assert(entry->length <= sizeof(entry->bytes));
    memcpy(entry->bytes, target, entry->length);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    file->_private_file = ASTRA_VFS_FILE_INVALID;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{
    (void)file;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                               uint32_t capacity, uint32_t *moved)
{
    Entry *entry = &entries[file->_private_file - 1u];
    uint32_t remaining = entry->length - (uint32_t)file->_private_offset;
    *moved = remaining < capacity ? remaining : capacity;
    memcpy(buffer, entry->bytes + file->_private_offset, *moved);
    file->_private_offset += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_write(AstraFile *file, uint64_t max_end,
    const void *buffer, uint32_t length, uint32_t *moved)
{
    (void)max_end;
    *moved = 0u;
    if (fail_write) {
        fail_write = 0;
        return ASTRA_VFS_ERR_NO_SPACE;
    }
    Entry *entry = &entries[file->_private_file - 1u];
    assert(file->_private_offset + length <= sizeof(entry->bytes));
    memcpy(entry->bytes + file->_private_offset, buffer, length);
    file->_private_offset += length;
    if (entry->length < file->_private_offset)
        entry->length = (uint32_t)file->_private_offset;
    *moved = length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *filesystem,
                                     const char *path, uint16_t mode)
{
    check_namespace(filesystem, path); (void)mode;
    if (find(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    add(path, ASTRA_VFS_KIND_DIRECTORY);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *filesystem,
                                 const char *path, uint32_t flags)
{
    check_namespace(filesystem, path);
    Entry *entry = find(path);
    if (entry == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    if (fail_unlink) {
        fail_unlink = 0;
        return ASTRA_VFS_ERR_LOST;
    }
    assert((flags == ASTRA_VFS_UNLINK_DIRECTORY) ==
           (entry->kind == ASTRA_VFS_KIND_DIRECTORY));
    size_t length = strlen(path);
    for (uint32_t at = 0u; at < entry_count; ++at)
        if (strncmp(entries[at].path, path, length) == 0 &&
            entries[at].path[length] == '/')
            return ASTRA_VFS_ERR_NOT_EMPTY;
    memmove(entry, entry + 1u,
            (entry_count - (uint32_t)(entry - entries) - 1u) * sizeof(*entry));
    --entry_count;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
                                         const char *path, AstraDirectory *directory)
{
    check_namespace(filesystem, path);
    Entry *entry = find(path);
    if (entry == NULL || entry->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_FOUND;
    directory->_private_path = malloc(strlen(path) + 1u);
    assert(directory->_private_path != NULL);
    strcpy(directory->_private_path, path);
    directory->_private_cursor = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *batch, uint32_t capacity, uint32_t *count)
{
    const char *parent = directory->_private_path;
    size_t length = strlen(parent);
    *count = 0u;
    if (directory->_private_cursor < 2u) {
        strcpy(batch[0].name,
            directory->_private_cursor == 0u ? "." : "..");
        ++directory->_private_cursor;
        *count = 1u;
        return ASTRA_VFS_OK;
    }
    for (uint32_t at = (uint32_t)directory->_private_cursor - 2u;
         at < entry_count && *count < capacity; ++at) {
        directory->_private_cursor = at + 3u;
        const char *path = entries[at].path;
        if (strncmp(path, parent, length) != 0 || path[length] != '/')
            continue;
        const char *name = path + length + 1u;
        if (strchr(name, '/') != NULL) continue;
        memset(&batch[*count], 0, sizeof(batch[*count]));
        strcpy(batch[*count].name, name);
        ++*count;
    }
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{
    free(directory->_private_path);
    directory->_private_path = NULL;
}

uint32_t astra_filesystem_rename(AstraFilesystem *filesystem,
                                  const char *from, const char *to)
{
    if (expected_source != NULL && filesystem == expected_source &&
        strncmp(from, "/dh0/", 5u) == 0 &&
        strncmp(to, "/usb0/", 6u) == 0)
        return ASTRA_VFS_ERR_CROSS_DEVICE;
    check_namespace(filesystem, to);
    if (find(to) != NULL) return ASTRA_VFS_ERR_EXISTS;
    size_t length = strlen(from);
    for (uint32_t at = 0u; at < entry_count; ++at) {
        Entry *entry = &entries[at];
        if (strncmp(entry->path, from, length) != 0 ||
            (entry->path[length] != '\0' && entry->path[length] != '/'))
            continue;
        char path[ASTRA_VFS_PATH_MAX];
        strcpy(path, to);
        strcat(path, entry->path + length);
        strcpy(entry->path, path);
    }
    return ASTRA_VFS_OK;
}

int main(void)
{
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    add("/dh0/Doom.app", ASTRA_VFS_KIND_DIRECTORY);
    Entry *source = add("/dh0/Doom.app/Doom", ASTRA_VFS_KIND_FILE);
    memcpy(source->bytes, "DOOM", 4u);
    source->length = 4u;
    Entry *source_link = add("/dh0/Doom.app/Data", ASTRA_VFS_KIND_SYMLINK);
    memcpy(source_link->bytes, "Doom", 4u);
    source_link->length = 4u;
    fail_write = 1;
    assert(astra_filesystem_copy_tree(&filesystem, "/dh0/Doom.app",
                                     "/usb0/Doom.app") == ASTRA_VFS_ERR_NO_SPACE);
    for (uint32_t at = 0u; at < entry_count; ++at)
        assert(strstr(entries[at].path, ".astra-copying") == NULL);
    assert(astra_filesystem_copy_tree(&filesystem, "/dh0/Doom.app",
                                     "/usb0/Doom.app") == ASTRA_VFS_OK);
    Entry *copied_link = find("/usb0/Doom.app/Data");
    assert(copied_link != NULL && copied_link->kind == ASTRA_VFS_KIND_SYMLINK &&
        copied_link->length == 4u &&
        memcmp(copied_link->bytes, "Doom", 4u) == 0);
    Entry *copied = find("/usb0/Doom.app/Doom");
    assert(copied != NULL && copied->length == 4u &&
           memcmp(copied->bytes, "DOOM", 4u) == 0);

    entry_count = 0u;
    add("/dh0/Doom.app", ASTRA_VFS_KIND_DIRECTORY);
    source = add("/dh0/Doom.app/Doom", ASTRA_VFS_KIND_FILE);
    memcpy(source->bytes, "DOOM", 4u);
    source->length = 4u;
    fail_write = 1;
    fail_unlink = 1;
    assert(astra_filesystem_copy_tree(&filesystem, "/dh0/Doom.app",
                                     "/usb0/Doom.app") == ASTRA_VFS_ERR_NO_SPACE);
    assert(astra_filesystem_copy_tree(&filesystem, "/dh0/Doom.app",
                                     "/usb0/Doom.app") == ASTRA_VFS_OK);
    copied = find("/usb0/Doom.app/Doom");
    assert(copied != NULL && copied->length == 4u &&
           memcmp(copied->bytes, "DOOM", 4u) == 0);
    entry_count = 0u;
    add("/dh0/Doom.app", ASTRA_VFS_KIND_DIRECTORY);
    source = add("/dh0/Doom.app/Doom", ASTRA_VFS_KIND_FILE);
    memcpy(source->bytes, "DOOM", 4u);
    source->length = 4u;
    source_link = add("/dh0/Doom.app/Data", ASTRA_VFS_KIND_SYMLINK);
    memcpy(source_link->bytes, "Doom", 4u);
    source_link->length = 4u;
    AstraFilesystem removable = ASTRA_FILESYSTEM_INIT;
    expected_source = &filesystem;
    expected_destination = &removable;
    assert(astra_filesystem_copy_tree_between(&filesystem, "/dh0/Doom.app",
        &removable, "/usb0/Doom.app") == ASTRA_VFS_OK);
    copied = find("/usb0/Doom.app/Doom");
    assert(copied != NULL && copied->length == 4u &&
        memcmp(copied->bytes, "DOOM", 4u) == 0);
    copied_link = find("/usb0/Doom.app/Data");
    assert(copied_link != NULL && copied_link->kind == ASTRA_VFS_KIND_SYMLINK &&
        copied_link->length == 4u &&
        memcmp(copied_link->bytes, "Doom", 4u) == 0);

    entry_count = 0u;
    expected_source = NULL;
    expected_destination = NULL;
    add("/dh0/Old", ASTRA_VFS_KIND_DIRECTORY);
    add("/dh0/Old/Child", ASTRA_VFS_KIND_FILE);
    uint32_t published = 99u;
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Old",
        &filesystem, "/dh0/New", &published) == ASTRA_VFS_OK);
    assert(published == 1u && find("/dh0/Old") == NULL &&
        find("/dh0/New") != NULL && find("/dh0/New/Child") != NULL);
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/New",
        &filesystem, "/dh0/New/Inside", &published) == ASTRA_VFS_ERR_INVALID);
    assert(published == 0u && find("/dh0/New") != NULL);
    add("/dh0/Occupied", ASTRA_VFS_KIND_FILE);
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/New",
        &filesystem, "/dh0/Occupied", &published) == ASTRA_VFS_ERR_EXISTS);
    assert(published == 0u && find("/dh0/New") != NULL);

    entry_count = 0u;
    add("/dh0/Move.app", ASTRA_VFS_KIND_DIRECTORY);
    Entry *move_file = add("/dh0/Move.app/Run", ASTRA_VFS_KIND_FILE);
    memcpy(move_file->bytes, "MOVE", 4u);
    move_file->length = 4u;
    Entry *move_link = add("/dh0/Move.app/Link", ASTRA_VFS_KIND_SYMLINK);
    memcpy(move_link->bytes, "Run", 3u);
    move_link->length = 3u;
    expected_source = &filesystem;
    expected_destination = &removable;
    fail_write = 1;
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Move.app",
        &removable, "/usb0/Move.app", &published) == ASTRA_VFS_ERR_NO_SPACE);
    assert(published == 0u && find("/dh0/Move.app/Run") != NULL &&
        find("/usb0/Move.app") == NULL);
    fail_unlink = 1;
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Move.app",
        &removable, "/usb0/Move.app", &published) == ASTRA_VFS_ERR_LOST);
    assert(published == 1u && find("/dh0/Move.app/Run") != NULL &&
        find("/usb0/Move.app/Run") != NULL);
    fail_unlink = 0;
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Move.app",
        &removable, "/usb0/Another.app", &published) == ASTRA_VFS_OK);
    assert(published == 1u && find("/dh0/Move.app") == NULL &&
        find("/dh0/Move.app/Run") == NULL &&
        find("/dh0/Move.app/Link") == NULL &&
        find("/usb0/Another.app/Run") != NULL &&
        find("/usb0/Another.app/Link") != NULL);

    entry_count = 0u;
    expected_source = &filesystem;
    expected_destination = &removable;
    char deep[ASTRA_VFS_PATH_MAX];
    strcpy(deep, "/dh0/Deep");
    add(deep, ASTRA_VFS_KIND_DIRECTORY);
    for (uint32_t at = 0u; at < 29u; ++at) {
        strcat(deep, "/d");
        add(deep, ASTRA_VFS_KIND_DIRECTORY);
    }
    strcat(deep, "/Data");
    Entry *deep_file = add(deep, ASTRA_VFS_KIND_FILE);
    memcpy(deep_file->bytes, "NESTED", 6u);
    deep_file->length = 6u;
    for (uint32_t at = 0u; at < 10u; ++at) {
        char sibling[ASTRA_VFS_PATH_MAX];
        snprintf(sibling, sizeof(sibling), "/dh0/Deep/Side%02u", at);
        add(sibling, ASTRA_VFS_KIND_FILE);
    }
    fail_alloc_at = allocations + 5u;
    assert(astra_filesystem_copy_tree_between(&filesystem, "/dh0/Deep",
        &removable, "/usb0/Deep") == ASTRA_VFS_ERR_LIMIT);
    fail_alloc_at = 0u;
    assert(find("/usb0/Deep") == NULL);
    for (uint32_t at = 0u; at < entry_count; ++at)
        assert(strstr(entries[at].path, ".astra-copying") == NULL);
    fail_write = 1;
    assert(astra_filesystem_copy_tree_between(&filesystem, "/dh0/Deep",
        &removable, "/usb0/Deep") == ASTRA_VFS_ERR_NO_SPACE);
    assert(find(deep) != NULL && find("/usb0/Deep") == NULL);
    for (uint32_t at = 0u; at < entry_count; ++at)
        assert(strstr(entries[at].path, ".astra-copying") == NULL);
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Deep",
        &removable, "/usb0/Deep", &published) == ASTRA_VFS_OK);
    assert(published == 1u && find("/dh0/Deep") == NULL &&
        find(deep) == NULL && find("/usb0/Deep") != NULL);
    char moved_deep[ASTRA_VFS_PATH_MAX];
    strcpy(moved_deep, "/usb0");
    strcat(moved_deep, deep + strlen("/dh0"));
    deep_file = find(moved_deep);
    assert(deep_file != NULL && deep_file->length == 6u &&
        memcmp(deep_file->bytes, "NESTED", 6u) == 0);
    assert(find("/usb0/Deep/Side00") != NULL &&
        find("/usb0/Deep/Side09") != NULL);

    entry_count = 0u;
    strcpy(deep, "/dh0/Deep48");
    add(deep, ASTRA_VFS_KIND_DIRECTORY);
    for (uint32_t at = 0u; at < 47u; ++at) {
        strcat(deep, "/d");
        add(deep, ASTRA_VFS_KIND_DIRECTORY);
    }
    strcat(deep, "/Data");
    deep_file = add(deep, ASTRA_VFS_KIND_FILE);
    memcpy(deep_file->bytes, "DEEPER", 6u);
    deep_file->length = 6u;
    fail_write = 1;
    assert(astra_filesystem_copy_tree_between(&filesystem, "/dh0/Deep48",
        &removable, "/usb0/Deep48") == ASTRA_VFS_ERR_NO_SPACE);
    assert(find("/usb0/Deep48") == NULL);
    for (uint32_t at = 0u; at < entry_count; ++at)
        assert(strstr(entries[at].path, ".astra-copying") == NULL);
    assert(astra_filesystem_move_tree_between(&filesystem, "/dh0/Deep48",
        &removable, "/usb0/Deep48", &published) == ASTRA_VFS_OK);
    assert(published == 1u && find("/dh0/Deep48") == NULL &&
        find(deep) == NULL && find("/usb0/Deep48") != NULL);
    strcpy(moved_deep, "/usb0");
    strcat(moved_deep, deep + strlen("/dh0"));
    deep_file = find(moved_deep);
    assert(deep_file != NULL && deep_file->length == 6u &&
        memcmp(deep_file->bytes, "DEEPER", 6u) == 0);
    return 0;
}
