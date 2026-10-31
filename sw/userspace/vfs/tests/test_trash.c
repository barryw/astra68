#include <astra/trash.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    char path[ASTRA_VFS_PATH_MAX];
    uint8_t bytes[ASTRA_VFS_PATH_MAX + 16u];
    uint32_t length;
    uint16_t kind;
} Node;

static AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
static Node nodes[48];
static uint32_t node_count;
static uint32_t fail_rename;
static uint32_t fail_sync;
static const char *fail_remove;

static Node *find(const char *path)
{
    for (uint32_t at = 0u; at < node_count; ++at)
        if (strcmp(nodes[at].path, path) == 0) return &nodes[at];
    return NULL;
}

static Node *add(const char *path, uint16_t kind)
{
    assert(node_count < 48u && find(path) == NULL);
    Node *node = &nodes[node_count++];
    memset(node, 0, sizeof(*node));
    strcpy(node->path, path);
    node->kind = kind;
    return node;
}

void *astra_runtime_allocate(size_t size) { return malloc(size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }

int astra_path_requires_walk(const char *path)
{
    if (path == NULL) return 1;
    size_t length = strlen(path);
    if (length != 0u && path[length - 1u] == '/') return 1;
    for (const char *at = path; *at != '\0';) {
        while (*at == '/') ++at;
        const char *start = at;
        while (*at != '\0' && *at != '/') ++at;
        if ((at - start == 1 && start[0] == '.') ||
            (at - start == 2 && start[0] == '.' && start[1] == '.'))
            return 1;
    }
    return 0;
}

uint32_t astra_filesystem_lstat(AstraFilesystem *fs, const char *path,
    AstraFileInfo *info)
{
    assert(fs == &filesystem && path != NULL && info != NULL);
    Node *node = find(path);
    if (node == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    info->kind = node->kind;
    info->byte_size = node->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_mkdir_mode(AstraFilesystem *fs, const char *path,
    uint16_t mode)
{
    assert(fs == &filesystem && mode == 0700u);
    if (find(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    add(path, ASTRA_VFS_KIND_DIRECTORY);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *fs, const char *path,
    uint32_t flags, uint16_t mode, AstraFile *file)
{
    assert(fs == &filesystem && file != NULL && mode == 0600u);
    assert(flags == (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE));
    if (find(path) != NULL) return ASTRA_VFS_ERR_EXISTS;
    add(path, ASTRA_VFS_KIND_FILE);
    file->_private_file = node_count;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *fs, const char *path,
    uint32_t flags, AstraFile *file)
{
    assert(fs == &filesystem && flags == ASTRA_VFS_OPEN_READ &&
        file != NULL);
    Node *node = find(path);
    if (node == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    if (node->kind != ASTRA_VFS_KIND_FILE) return ASTRA_VFS_ERR_INVALID;
    file->_private_file = (uint32_t)(node - nodes) + 1u;
    file->_private_offset = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_write(AstraFile *file, uint64_t max_end,
    const void *bytes, uint32_t length, uint32_t *moved)
{
    assert(file != NULL && file->_private_file > 0u &&
        file->_private_file <= node_count && max_end == UINT64_MAX &&
        moved != NULL);
    Node *node = &nodes[file->_private_file - 1u];
    assert(node->length + length <= sizeof(node->bytes));
    memcpy(node->bytes + node->length, bytes, length);
    node->length += length;
    *moved = length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *bytes,
    uint32_t length, uint32_t *moved)
{
    assert(file != NULL && file->_private_file > 0u &&
        file->_private_file <= node_count && moved != NULL);
    Node *node = &nodes[file->_private_file - 1u];
    uint32_t remaining = node->length - (uint32_t)file->_private_offset;
    *moved = length < remaining ? length : remaining;
    memcpy(bytes, node->bytes + file->_private_offset, *moved);
    file->_private_offset += *moved;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_info(const AstraFile *file,
    AstraFileInfo *info)
{
    assert(file != NULL && file->_private_file > 0u &&
        file->_private_file <= node_count && info != NULL);
    Node *node = &nodes[file->_private_file - 1u];
    info->kind = node->kind;
    info->byte_size = node->length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_sync(AstraFile *file)
{
    assert(file != NULL && file->_private_file > 0u);
    return fail_sync ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    assert(file != NULL && file->_private_file > 0u);
    file->_private_file = ASTRA_VFS_FILE_INVALID;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *fs, const char *path,
    uint32_t flags)
{
    assert(fs == &filesystem && flags == 0u);
    Node *node = find(path);
    if (node == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    uint32_t index = (uint32_t)(node - nodes);
    memmove(nodes + index, nodes + index + 1u,
        (node_count - index - 1u) * sizeof(nodes[0]));
    --node_count;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_rename_noreplace(AstraFilesystem *fs,
    const char *from, const char *to)
{
    assert(fs == &filesystem);
    Node *node = find(from);
    if (node == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    if (find(to) != NULL) return ASTRA_VFS_ERR_EXISTS;
    if (fail_rename) return ASTRA_VFS_ERR_IO;
    strcpy(node->path, to);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *fs,
    const char *path, AstraDirectory *directory)
{
    assert(fs == &filesystem && directory != NULL);
    Node *node = find(path);
    if (node == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    if (node->kind != ASTRA_VFS_KIND_DIRECTORY) return ASTRA_VFS_ERR_NOT_DIR;
    directory->_private_path = (char *)path;
    directory->_private_cursor = 0u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
    AstraDirectoryEntry *entries, uint32_t capacity, uint32_t *count)
{
    assert(directory != NULL && entries != NULL && capacity == 1u &&
        count != NULL);
    const char *parent = directory->_private_path;
    size_t parent_length = strlen(parent);
    for (uint32_t at = (uint32_t)directory->_private_cursor;
         at < node_count; ++at) {
        const char *path = nodes[at].path;
        if (strncmp(path, parent, parent_length) != 0 ||
            path[parent_length] != '/') continue;
        const char *name = path + parent_length + 1u;
        if (strchr(name, '/') != NULL) continue;
        memset(entries, 0, sizeof(*entries));
        strcpy(entries->name, name);
        directory->_private_cursor = at + 1u;
        *count = 1u;
        return ASTRA_VFS_OK;
    }
    *count = 0u;
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{
    assert(directory != NULL);
    directory->_private_path = NULL;
}

uint32_t astra_filesystem_remove_tree_internal(AstraFilesystem *fs,
    const char *path)
{
    assert(fs == &filesystem && path != NULL);
    if (fail_remove != NULL && strcmp(path, fail_remove) == 0)
        return ASTRA_VFS_ERR_IO;
    if (find(path) == NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    size_t length = strlen(path);
    for (uint32_t at = 0u; at < node_count;) {
        if (strcmp(nodes[at].path, path) == 0 ||
            (strncmp(nodes[at].path, path, length) == 0 &&
             nodes[at].path[length] == '/')) {
            memmove(nodes + at, nodes + at + 1u,
                (node_count - at - 1u) * sizeof(nodes[0]));
            --node_count;
        } else {
            ++at;
        }
    }
    return ASTRA_VFS_OK;
}

int main(void)
{
    char moved[ASTRA_VFS_PATH_MAX];
    char origin[ASTRA_VFS_PATH_MAX];
    add("/usb0/Test", ASTRA_VFS_KIND_FILE);
    assert(astra_trash_move(&filesystem, "/usb0", "/usb0/Test",
        moved, sizeof(moved)) == ASTRA_VFS_OK);
    assert(strcmp(moved, "/usb0/.Trash/Test-1") == 0 &&
        find("/usb0/Test") == NULL && find(moved) != NULL &&
        find("/usb0/.Trash/.origins/Test-1") != NULL);
    assert(astra_trash_origin(&filesystem, "/usb0", moved,
        origin, sizeof(origin)) == ASTRA_VFS_OK &&
        strcmp(origin, "/usb0/Test") == 0);

    for (uint32_t at = 0u; at < node_count; ++at)
        if (strncmp(nodes[at].path, "/usb0", 5u) == 0) {
            char current[ASTRA_VFS_PATH_MAX];
            strcpy(current, nodes[at].path + 5u);
            strcpy(nodes[at].path, "/usb3");
            strcat(nodes[at].path, current);
        }
    assert(astra_trash_origin(&filesystem, "/usb3",
        "/usb3/.Trash/Test-1", origin, sizeof(origin)) == ASTRA_VFS_OK &&
        strcmp(origin, "/usb3/Test") == 0);

    add("/usb3/Test", ASTRA_VFS_KIND_FILE);
    add("/usb3/.Trash/Test-2", ASTRA_VFS_KIND_FILE);
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/Test",
        moved, sizeof(moved)) == ASTRA_VFS_OK);
    assert(strcmp(moved, "/usb3/.Trash/Test-3") == 0 &&
        find("/usb3/.Trash/.origins/Test-2") == NULL);
    assert(astra_trash_origin(&filesystem, "/usb3", moved,
        origin, sizeof(origin)) == ASTRA_VFS_OK &&
        strcmp(origin, "/usb3/Test") == 0);

    add("/usb3/Fail", ASTRA_VFS_KIND_SYMLINK);
    fail_rename = 1u;
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/Fail",
        moved, sizeof(moved)) == ASTRA_VFS_ERR_IO);
    assert(find("/usb3/Fail") != NULL &&
        find("/usb3/.Trash/.origins/Fail-1") == NULL);
    fail_rename = 0u;
    fail_sync = 1u;
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/Fail",
        moved, sizeof(moved)) == ASTRA_VFS_ERR_IO);
    assert(find("/usb3/Fail") != NULL &&
        find("/usb3/.Trash/.origins/Fail-1") == NULL);
    fail_sync = 0u;
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/Fail",
        moved, sizeof(moved)) == ASTRA_VFS_OK);

    add("/usb3/.hidden", ASTRA_VFS_KIND_FILE);
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/.hidden",
        moved, sizeof(moved)) == ASTRA_VFS_OK);
    assert(strcmp(moved, "/usb3/.Trash/.hidden-1") == 0 &&
        astra_trash_origin(&filesystem, "/usb3", moved,
            origin, sizeof(origin)) == ASTRA_VFS_OK &&
        strcmp(origin, "/usb3/.hidden") == 0);
    assert(astra_filesystem_unlink(&filesystem, moved, 0u) == ASTRA_VFS_OK);
    assert(astra_trash_origin(&filesystem, "/usb3", moved,
        origin, sizeof(origin)) == ASTRA_VFS_ERR_NOT_FOUND);

    uint32_t restored = 99u;
    add("/usb3/Fail", ASTRA_VFS_KIND_FILE);
    assert(astra_trash_restore(&filesystem, "/usb3",
        "/usb3/.Trash/Fail-1", origin, sizeof(origin), &restored) ==
        ASTRA_VFS_ERR_EXISTS && restored == 0u);
    assert(find("/usb3/.Trash/Fail-1") != NULL &&
        find("/usb3/.Trash/.origins/Fail-1") != NULL);
    assert(astra_filesystem_unlink(&filesystem, "/usb3/Fail", 0u) ==
        ASTRA_VFS_OK);
    assert(astra_trash_restore(&filesystem, "/usb3",
        "/usb3/.Trash/Fail-1", origin, sizeof(origin), &restored) ==
        ASTRA_VFS_OK && restored == 1u &&
        strcmp(origin, "/usb3/Fail") == 0);
    assert(find("/usb3/Fail") != NULL &&
        find("/usb3/.Trash/Fail-1") == NULL &&
        find("/usb3/.Trash/.origins/Fail-1") == NULL);

    assert(astra_trash_restore(&filesystem, "/usb3",
        "/usb3/.Trash/Test-1", origin, sizeof(origin), &restored) ==
        ASTRA_VFS_OK && restored == 1u &&
        strcmp(origin, "/usb3/Test") == 0);
    assert(find("/usb3/.Trash/.origins/Test-1") == NULL);

    Node *record = find("/usb3/.Trash/.origins/Test-3");
    assert(record != NULL);
    record->bytes[0] = '?';
    assert(astra_trash_origin(&filesystem, "/usb3",
        "/usb3/.Trash/Test-3", origin, sizeof(origin)) ==
        ASTRA_VFS_ERR_INVALID && origin[0] == '\0');
    record->bytes[0] = 'A';
    record->bytes[8] = 0u;
    assert(astra_trash_origin(&filesystem, "/usb3",
        "/usb3/.Trash/Test-3", origin, sizeof(origin)) ==
        ASTRA_VFS_ERR_INVALID && origin[0] == '\0');
    assert(astra_trash_move(&filesystem, "/usb3",
        "/usb3/.Trash/Test-3", moved, sizeof(moved)) ==
        ASTRA_VFS_ERR_INVALID);
    assert(astra_trash_move(&filesystem, "/usb3", "/usb3/../Test",
        moved, sizeof(moved)) == ASTRA_VFS_ERR_INVALID);
    assert(astra_trash_origin(&filesystem, "/usb3",
        "/usb3/.Trash/.origins/Test-1", origin, sizeof(origin)) ==
        ASTRA_VFS_ERR_INVALID);
    add("/usb3/.Trash/Directory-1", ASTRA_VFS_KIND_DIRECTORY);
    add("/usb3/.Trash/Directory-1/child", ASTRA_VFS_KIND_FILE);
    uint32_t removed = 99u;
    char failed[ASTRA_VFS_PATH_MAX];
    fail_remove = "/usb3/.Trash/Test-3";
    uint32_t empty_status = astra_trash_empty(&filesystem, "/usb3", failed,
        sizeof(failed), &removed);
    assert(empty_status == ASTRA_VFS_ERR_IO);
    assert(strcmp(failed, fail_remove) == 0 && removed <= 1u &&
        find("/usb3/.Trash/Test-3") != NULL);
    uint32_t first_removed = removed;
    fail_remove = NULL;
    assert(astra_trash_empty(&filesystem, "/usb3", failed,
        sizeof(failed), &removed) == ASTRA_VFS_OK);
    assert(removed + first_removed == 3u && failed[0] == '\0' &&
        find("/usb3/.Trash/Directory-1") == NULL &&
        find("/usb3/.Trash/Directory-1/child") == NULL &&
        find("/usb3/.Trash/Test-3") == NULL &&
        find("/usb3/.Trash/.origins/Test-3") == NULL &&
        find("/usb3/.Trash/.origins") != NULL);
    puts("trash tests passed");
    return 0;
}
