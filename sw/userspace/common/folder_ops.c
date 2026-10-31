#include "folder_ops.h"

#include <astra/string.h>
#include <astra/utf8.h>
#include <astra/vfs_path.h>

#include <string.h>

const char *astra_folder_leaf(const char *path)
{
    if (path == NULL || path[0] != '/') return NULL;
    size_t length = strlen(path);
    if (length == 0u || path[length - 1u] == '/') return NULL;
    const char *leaf = astra_path_leaf(path);
    return leaf != path ? leaf : NULL;
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

uint32_t astra_entry_rename_safe(AstraFilesystem *filesystem,
    const char *source, const char *parent, const char *name,
    char *path, uint32_t capacity)
{
    if (filesystem == NULL || source == NULL)
        return ASTRA_VFS_ERR_INVALID;
    uint32_t status = astra_folder_path(parent, name, path, capacity);
    if (status != ASTRA_VFS_OK || strcmp(source, path) == 0) return status;
    return astra_filesystem_rename_noreplace(filesystem, source, path);
}
