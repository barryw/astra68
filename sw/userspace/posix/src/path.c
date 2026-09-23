#include "path.h"

#include <astra/vfs_service.h>

#include <stddef.h>

static int append(char *out, uint32_t capacity, uint32_t *length, char value)
{
    if (*length + 1u >= capacity)
        return 0;
    out[(*length)++] = value;
    out[*length] = '\0';
    return 1;
}

int astra_posix_path_is_absolute(const char *path)
{
    return path != NULL && path[0] == '/';
}

static int input_path(const char *cwd, const char *path, char *out,
                      uint32_t capacity)
{
    uint32_t length = 0u;

    out[0] = '\0';
    if (*path != '/') {
        while (*cwd != '\0')
            if (!append(out, capacity, &length, *cwd++))
                return 0;
        if (length == 0u || out[length - 1u] != '/')
            if (!append(out, capacity, &length, '/'))
                return 0;
    }
    while (*path != '\0')
        if (!append(out, capacity, &length, *path++))
            return 0;
    return 1;
}

static int normalise(const char *input, char *out, uint32_t capacity)
{
    uint32_t length = 0u;
    uint32_t at = 0u;

    if (capacity < 2u || input[0] != '/')
        return 0;
    out[0] = '\0';
    if (!append(out, capacity, &length, '/'))
        return 0;
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
            while (length > 1u && out[length - 1u] != '/')
                --length;
            if (length > 1u)
                --length;
            out[length] = '\0';
            continue;
        }
        if (length > 1u && !append(out, capacity, &length, '/'))
            return 0;
        for (uint32_t index = 0u; index < count; ++index)
            if (!append(out, capacity, &length, input[start + index]))
                return 0;
    }
    return 1;
}

int astra_posix_path_resolve(const char *cwd, const char *path,
                             char *normal, uint32_t normal_capacity,
                             char *native, uint32_t native_capacity)
{
    char input[ASTRA_VFS_PATH_MAX];
    uint32_t length = 0u;

    if (cwd == NULL || path == NULL || path[0] == '\0' || normal == NULL ||
        native == NULL || cwd[0] != '/')
        return -1;
    if (!input_path(cwd, path, input, sizeof(input)) ||
        !normalise(input, normal, normal_capacity))
        return -1;
    while (normal[length] != '\0') {
        if (length + 1u >= native_capacity)
            return -1;
        native[length] = normal[length];
        ++length;
    }
    native[length] = '\0';
    return length == 1u ? 0 : 1;
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
    uint32_t length = 0u;

    if (target == NULL || native == NULL || capacity == 0u)
        return -1;
    native[0] = '\0';
    while (*target != '\0')
        if (!append(native, capacity, &length, *target++))
            return -1;
    return 0;
}

int astra_posix_link_target_to_posix(const char *target, char *posix,
                                     uint32_t capacity)
{
    uint32_t length = 0u;

    if (target == NULL || posix == NULL || capacity == 0u)
        return -1;
    posix[0] = '\0';
    while (*target != '\0')
        if (!append(posix, capacity, &length, *target++))
            return -1;
    return 0;
}
