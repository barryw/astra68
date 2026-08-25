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

static int assign_character(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static char upper(char value)
{
    return value >= 'a' && value <= 'z' ?
        (char)(value - ('a' - 'A')) : value;
}

static int native_spelling(const char *path, uint32_t *colon)
{
    uint32_t at = 0u;

    while (path[at] != '\0' && path[at] != '/' && path[at] != ':') {
        if (!assign_character(path[at]))
            return 0;
        ++at;
    }
    if (at == 0u || path[at] != ':')
        return 0;
    *colon = at;
    return 1;
}

static int input_path(const char *cwd, const char *path, char *out,
                      uint32_t capacity)
{
    uint32_t colon = 0u;
    uint32_t length = 0u;

    out[0] = '\0';
    if (native_spelling(path, &colon)) {
        if (!append(out, capacity, &length, '/'))
            return 0;
        for (uint32_t at = 0u; at < colon; ++at)
            if (!append(out, capacity, &length, path[at]))
                return 0;
        path += colon + 1u;
        if (*path != '\0' && !append(out, capacity, &length, '/'))
            return 0;
    } else if (*path != '/') {
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
    uint32_t at = 1u;
    uint32_t out = 0u;

    if (cwd == NULL || path == NULL || normal == NULL || native == NULL ||
        cwd[0] != '/' || !input_path(cwd, path, input, sizeof(input)) ||
        !normalise(input, normal, normal_capacity))
        return -1;
    if (normal[1] == '\0') {
        native[0] = '\0';
        return 0;
    }
    while (normal[at] != '\0' && normal[at] != '/') {
        if (!assign_character(normal[at]) || out + 2u >= native_capacity)
            return -1;
        native[out++] = upper(normal[at++]);
    }
    if (out == 0u || out + 2u > native_capacity)
        return -1;
    native[out++] = ':';
    if (normal[at] == '/')
        ++at;
    while (normal[at] != '\0') {
        if (out + 1u >= native_capacity)
            return -1;
        native[out++] = normal[at++];
    }
    native[out] = '\0';
    return 1;
}
