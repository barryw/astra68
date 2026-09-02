#include "path.h"

#include <astra/ascii.h>
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

static int simple_native(const char *path, uint32_t colon, char *normal,
                         uint32_t normal_capacity, char *native,
                         uint32_t native_capacity)
{
    const char *rest = path + colon + 1u;
    uint32_t rest_length = 0u;
    uint32_t normal_length;

    while (rest[rest_length] != '\0') {
        uint32_t start = rest_length;

        if (rest[rest_length] == '/')
            return 0;
        while (rest[rest_length] != '\0' && rest[rest_length] != '/')
            ++rest_length;
        if (rest_length - start == 1u && rest[start] == '.')
            return 0;
        if (rest_length - start == 2u && rest[start] == '.' &&
            rest[start + 1u] == '.')
            return 0;
        if (rest[rest_length] == '/') {
            ++rest_length;
            if (rest[rest_length] == '\0')
                return 0;
        }
    }
    normal_length = 1u + colon + (rest_length != 0u ? 1u + rest_length : 0u);
    if ((normal != NULL && normal_length + 1u > normal_capacity) ||
        colon + 2u + rest_length > native_capacity)
        return 0;
    if (normal != NULL)
        normal[0] = '/';
    for (uint32_t index = 0u; index < colon; ++index) {
        char value = astra_ascii_upper(path[index]);

        if (normal != NULL)
            normal[index + 1u] = path[index];
        native[index] = value;
    }
    native[colon] = ':';
    for (uint32_t index = 0u; index < rest_length; ++index) {
        if (normal != NULL)
            normal[1u + colon + 1u + index] = rest[index];
        native[colon + 1u + index] = rest[index];
    }
    if (normal != NULL && rest_length != 0u)
        normal[1u + colon] = '/';
    if (normal != NULL)
        normal[normal_length] = '\0';
    native[colon + 1u + rest_length] = '\0';
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
        cwd[0] != '/')
        return -1;
    if (native_spelling(path, &at) &&
        simple_native(path, at, normal, normal_capacity, native,
                      native_capacity))
        return 1;
    at = 1u;
    if (!input_path(cwd, path, input, sizeof(input)) ||
        !normalise(input, normal, normal_capacity))
        return -1;
    if (normal[1] == '\0') {
        native[0] = '\0';
        return 0;
    }
    while (normal[at] != '\0' && normal[at] != '/') {
        if (!assign_character(normal[at]) || out + 2u >= native_capacity)
            return -1;
        native[out++] = astra_ascii_upper(normal[at++]);
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

int astra_posix_path_resolve_native(const char *cwd, const char *path,
                                    char *native, uint32_t native_capacity)
{
    char normal[ASTRA_VFS_PATH_MAX];
    uint32_t colon = 0u;

    if (cwd == NULL || path == NULL || native == NULL || cwd[0] != '/')
        return -1;
    if (native_spelling(path, &colon) &&
        simple_native(path, colon, NULL, 0u, native, native_capacity))
        return 1;
    return astra_posix_path_resolve(cwd, path, normal, sizeof(normal), native,
                                    native_capacity);
}

int astra_posix_link_target_to_native(const char *target, char *native,
                                      uint32_t capacity)
{
    uint32_t colon = 0u;
    uint32_t length = 0u;

    if (target == NULL || native == NULL || capacity == 0u)
        return -1;
    native[0] = '\0';
    if (target[0] == '/') {
        char normal[ASTRA_VFS_PATH_MAX + 2u];

        return astra_posix_path_resolve("/", target, normal, sizeof(normal),
                                        native, capacity) == 1 ? 0 : -1;
    }
    if (native_spelling(target, &colon)) {
        for (uint32_t index = 0u; index < colon; ++index)
            if (!append(native, capacity, &length,
                        astra_ascii_upper(target[index])))
                return -1;
        target += colon;
    }
    while (*target != '\0')
        if (!append(native, capacity, &length, *target++))
            return -1;
    return 0;
}

int astra_posix_link_target_to_posix(const char *target, char *posix,
                                     uint32_t capacity)
{
    uint32_t colon = 0u;
    uint32_t length = 0u;

    if (target == NULL || posix == NULL || capacity == 0u)
        return -1;
    posix[0] = '\0';
    if (native_spelling(target, &colon)) {
        if (!append(posix, capacity, &length, '/'))
            return -1;
        for (uint32_t index = 0u; index < colon; ++index)
            if (!append(posix, capacity, &length, target[index]))
                return -1;
        target += colon + 1u;
        if (*target != '\0' && !append(posix, capacity, &length, '/'))
            return -1;
    }
    while (*target != '\0')
        if (!append(posix, capacity, &length, *target++))
            return -1;
    return 0;
}
