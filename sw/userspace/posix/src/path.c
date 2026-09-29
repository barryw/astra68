#include "path.h"

#include <astra/string.h>
#include <astra/vfs_service.h>

#include <stddef.h>

int astra_posix_path_is_absolute(const char *path)
{
    return path != NULL && path[0] == '/';
}

static int input_path(const char *cwd, const char *path, char *out,
                      uint32_t capacity)
{
    AstraString text;

    astra_string_init(&text, out, capacity);
    if (*path != '/') {
        (void)astra_string_append(&text, cwd);
        if (text.length == 0u || out[text.length - 1u] != '/')
            (void)astra_string_append_char(&text, '/');
    }
    (void)astra_string_append(&text, path);
    return !text.truncated;
}

static int normalise(const char *input, char *out, uint32_t capacity)
{
    AstraString text;
    uint32_t at = 0u;

    if (capacity < 2u || input[0] != '/')
        return 0;
    astra_string_init(&text, out, capacity);
    (void)astra_string_append_char(&text, '/');
    while (input[at] != '\0') {
        uint32_t start;
        uint32_t count;

        while (input[at] == '/')
            ++at;
        start = at;
        while (input[at] != '\0' && input[at] != '/')
            ++at;
        count = at - start;
        if (count == 0u)
            break;
        if (count == 1u && input[start] == '.')
            continue;
        if (count == 2u && input[start] == '.' && input[start + 1u] == '.') {
            /* Drop the last component; the root stays. */
            while (text.length > 1u && out[text.length - 1u] != '/')
                --text.length;
            if (text.length > 1u)
                --text.length;
            out[text.length] = '\0';
            continue;
        }
        if (text.length > 1u)
            (void)astra_string_append_char(&text, '/');
        if (!astra_string_append_bytes(&text, input + start, count))
            return 0;
    }
    return !text.truncated;
}

int astra_posix_path_resolve(const char *cwd, const char *path,
                             char *normal, uint32_t normal_capacity,
                             char *native, uint32_t native_capacity)
{
    char input[ASTRA_VFS_PATH_MAX];

    if (cwd == NULL || path == NULL || path[0] == '\0' || normal == NULL ||
        native == NULL || cwd[0] != '/')
        return -1;
    if (!input_path(cwd, path, input, sizeof(input)) ||
        !normalise(input, normal, normal_capacity))
        return -1;
    if (!astra_string_copy(native, native_capacity, normal))
        return -1;
    return normal[1] == '\0' ? 0 : 1;
}

int astra_posix_path_resolve_native(const char *cwd, const char *path,
                                    char *native, uint32_t native_capacity)
{
    char normal[ASTRA_VFS_PATH_MAX];
    if (cwd == NULL || path == NULL || path[0] == '\0' || native == NULL ||
        cwd[0] != '/')
        return -1;
    return astra_posix_path_resolve(cwd, path, normal, sizeof(normal), native,
                                    native_capacity);
}

int astra_posix_link_target_to_native(const char *target, char *native,
                                      uint32_t capacity)
{
    if (target == NULL || native == NULL || capacity == 0u)
        return -1;
    return astra_string_copy(native, capacity, target) ? 0 : -1;
}

int astra_posix_link_target_to_posix(const char *target, char *posix,
                                     uint32_t capacity)
{
    if (target == NULL || posix == NULL || capacity == 0u)
        return -1;
    return astra_string_copy(posix, capacity, target) ? 0 : -1;
}
