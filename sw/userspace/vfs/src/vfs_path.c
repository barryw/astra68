/*
 * Splitting NAME:rest, and normalising the rest. Neither function touches an
 * assign table or a backend: a path is refused here for what it says, and
 * refused later for what the process does not hold, and keeping the two apart
 * is what makes each one testable on its own.
 */

#include <astra/vfs_path.h>

#include <stddef.h>

static char
upper(char value)
{
    return (value >= 'a' && value <= 'z') ? (char)(value - ('a' - 'A')) : value;
}

uint32_t
astra_path_split(const char *path, char *name, uint32_t name_capacity,
                 char *rest, uint32_t rest_capacity)
{
    uint32_t index = 0u;
    uint32_t out = 0u;

    if (path == NULL || name == NULL || rest == NULL || name_capacity == 0u ||
        rest_capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    while (path[index] != ':') {
        if (path[index] == '\0' || index + 1u >= name_capacity) {
            return ASTRA_VFS_ERR_INVALID;
        }
        name[index] = upper(path[index]);
        ++index;
    }
    if (index == 0u) {
        return ASTRA_VFS_ERR_INVALID;   /* ":rest" names nothing */
    }
    name[index] = '\0';

    ++index;                            /* step over the colon */
    while (path[index] != '\0') {
        if (out + 1u >= rest_capacity) {
            /* Truncation would name a different file. */
            return ASTRA_VFS_ERR_INVALID;
        }
        rest[out++] = path[index++];
    }
    rest[out] = '\0';
    return ASTRA_VFS_OK;
}

/* True for the component between `start` and `end` being exactly ".." */
static int
is_parent(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 2u && rest[start] == '.' && rest[start + 1u] == '.';
}

static int
is_current(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 1u && rest[start] == '.';
}

uint32_t
astra_path_normalise(const char *rest, char *out, uint32_t capacity)
{
    uint32_t index = 0u;
    uint32_t length = 0u;

    if (rest == NULL || out == NULL || capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    out[0] = '\0';
    while (rest[index] != '\0') {
        uint32_t start;
        uint32_t end;

        while (rest[index] == '/') {
            ++index;                    /* empty components mean nothing */
        }
        start = index;
        while (rest[index] != '\0' && rest[index] != '/') {
            ++index;
        }
        end = index;
        if (end == start) {
            continue;
        }
        if (is_current(rest, start, end)) {
            continue;
        }
        if (is_parent(rest, start, end)) {
            if (length == 0u) {
                /* Above the assign's root there is nothing to name. */
                return ASTRA_VFS_ERR_NOT_FOUND;
            }
            while (length != 0u && out[length - 1u] != '/') {
                --length;
            }
            if (length != 0u) {
                --length;               /* drop the separator too */
            }
            out[length] = '\0';
            continue;
        }
        if (length != 0u) {
            if (length + 1u >= capacity) {
                return ASTRA_VFS_ERR_INVALID;
            }
            out[length++] = '/';
        }
        for (uint32_t at = start; at < end; ++at) {
            if (length + 1u >= capacity) {
                return ASTRA_VFS_ERR_INVALID;
            }
            out[length++] = rest[at];
        }
        out[length] = '\0';
    }
    return ASTRA_VFS_OK;
}
