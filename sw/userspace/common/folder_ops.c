#include "folder_ops.h"

#include <astra/string.h>
#include <astra/utf8.h>

#include <string.h>

const char *astra_folder_leaf(const char *path)
{
    if (path == NULL) return NULL;
    const char *leaf = NULL;
    for (const char *at = path; *at != '\0'; ++at)
        if (*at == '/') leaf = at + 1u;
    return leaf != NULL && *leaf != '\0' ? leaf : NULL;
}

int astra_folder_name_valid(const char *name)
{
    if (name == NULL) return 0;
    size_t length = strlen(name);
    if (length == 0u || length >= ASTRA_VFS_NAME_MAX ||
        (length == 1u && name[0] == '.') ||
        (length == 2u && name[0] == '.' && name[1] == '.') ||
        !astra_utf8_validate(name, (uint32_t)length, 0u)) return 0;
    for (size_t at = 0u; at < length; ++at)
        if (name[at] == '/' || (unsigned char)name[at] < 0x20u ||
            (unsigned char)name[at] == 0x7fu) return 0;
    return 1;
}

uint32_t astra_folder_path(const char *parent, const char *name,
    char *path, uint32_t capacity)
{
    if (parent == NULL || parent[0] != '/' || !astra_folder_name_valid(name) ||
        path == NULL || capacity == 0u) return ASTRA_VFS_ERR_INVALID;
    AstraString out;
    astra_string_init(&out, path, capacity);
    if (!astra_string_append(&out, parent) ||
        (out.length != 1u && path[out.length - 1u] != '/' &&
         !astra_string_append_char(&out, '/')) ||
        !astra_string_append(&out, name)) return ASTRA_VFS_ERR_LIMIT;
    if (out.length >= ASTRA_VFS_PATH_MAX) return ASTRA_VFS_ERR_LIMIT;
    return ASTRA_VFS_OK;
}

uint32_t astra_folder_create_numbered(AstraFilesystem *filesystem,
    const char *parent, const char *prefix, char *path, uint32_t capacity)
{
    if (filesystem == NULL || !astra_folder_name_valid(prefix) ||
        path == NULL) return ASTRA_VFS_ERR_INVALID;
    char name[ASTRA_VFS_NAME_MAX];
    for (uint32_t number = 1u; number != 0u; ++number) {
        AstraString text;
        astra_string_init(&text, name, sizeof(name));
        if (!astra_string_append(&text, prefix) ||
            !astra_string_append_u64(&text, number))
            return ASTRA_VFS_ERR_LIMIT;
        uint32_t status = astra_folder_path(parent, name, path, capacity);
        if (status != ASTRA_VFS_OK) return status;
        status = astra_filesystem_mkdir(filesystem, path);
        if (status != ASTRA_VFS_ERR_EXISTS) return status;
    }
    return ASTRA_VFS_ERR_LIMIT;
}

/* Reserve matching node kind so rename cannot replace an existing entry. */
static uint32_t reserve_entry(AstraFilesystem *filesystem, const char *path,
    uint32_t kind)
{
    if (kind == ASTRA_VFS_KIND_DIRECTORY)
        return astra_filesystem_mkdir(filesystem, path);
    if (kind == ASTRA_VFS_KIND_FILE) {
        AstraFile file = ASTRA_FILE_INIT;
        uint32_t status = astra_filesystem_open_mode(filesystem, path,
            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_EXCLUSIVE, 0600u, &file);
        if (status != ASTRA_VFS_OK) return status;
        status = astra_filesystem_close(&file);
        if (status != ASTRA_VFS_OK)
            (void)astra_filesystem_unlink(filesystem, path, 0u);
        return status;
    }
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

static void release_entry(AstraFilesystem *filesystem, const char *path,
    uint32_t kind)
{
    (void)astra_filesystem_unlink(filesystem, path,
        kind == ASTRA_VFS_KIND_DIRECTORY ? ASTRA_VFS_UNLINK_DIRECTORY : 0u);
}

uint32_t astra_entry_rename_safe(AstraFilesystem *filesystem,
    const char *source, const char *parent, const char *name,
    char *path, uint32_t capacity)
{
    if (filesystem == NULL || source == NULL)
        return ASTRA_VFS_ERR_INVALID;
    uint32_t status = astra_folder_path(parent, name, path, capacity);
    if (status != ASTRA_VFS_OK || strcmp(source, path) == 0) return status;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    status = astra_filesystem_lstat(filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    status = reserve_entry(filesystem, path, info.kind);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_rename(filesystem, source, path);
    if (status != ASTRA_VFS_OK)
        release_entry(filesystem, path, info.kind);
    return status;
}

uint32_t astra_entry_move_to_trash(AstraFilesystem *filesystem,
    const char *source, const char *trash, char *path, uint32_t capacity)
{
    if (filesystem == NULL || source == NULL || trash == NULL || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status = astra_filesystem_lstat(filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir(filesystem, trash);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        return status;
    const char *leaf = astra_folder_leaf(source);
    if (!astra_folder_name_valid(leaf))
        return ASTRA_VFS_ERR_INVALID;
    char prefix[ASTRA_VFS_NAME_MAX];
    AstraString text;
    astra_string_init(&text, prefix, sizeof(prefix));
    uint32_t bytes = astra_string_utf8_prefix(leaf, (uint32_t)strlen(leaf), 180u);
    if (!astra_string_append_bytes(&text, leaf, bytes) ||
        !astra_string_append_char(&text, '-'))
        return ASTRA_VFS_ERR_LIMIT;
    for (uint32_t number = 1u; number != 0u; ++number) {
        char name[ASTRA_VFS_NAME_MAX];
        AstraString candidate;
        astra_string_init(&candidate, name, sizeof(name));
        if (!astra_string_append(&candidate, prefix) ||
            !astra_string_append_u64(&candidate, number))
            return ASTRA_VFS_ERR_LIMIT;
        status = astra_folder_path(trash, name, path, capacity);
        if (status != ASTRA_VFS_OK) return status;
        status = reserve_entry(filesystem, path, info.kind);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status != ASTRA_VFS_OK) return status;
        status = astra_filesystem_rename(filesystem, source, path);
        if (status != ASTRA_VFS_OK)
            release_entry(filesystem, path, info.kind);
        return status;
    }
    return ASTRA_VFS_ERR_LIMIT;
}
