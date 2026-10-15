/* One ordered resolver for provider paths. Original byte positions survive
 * link expansion so the native namespace can continue exactly where we left. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "astra68-resolve.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#define DIRECTORY_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#else
#define DIRECTORY_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#endif

#define TEXT_MAX 16384u
#define FROM_LINK UINT32_C(0x80000000)

typedef struct Walk {
    int *dirs;
    uint32_t count;
    uint32_t capacity;
} Walk;

static int reserve(Walk *walk, uint32_t needed)
{
    uint32_t capacity = walk->capacity ? walk->capacity : 16u;
    int *grown;

    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2u / sizeof(*grown))
            return -ENOMEM;
        capacity *= 2u;
    }
    if (capacity == walk->capacity)
        return 0;
    grown = realloc(walk->dirs, capacity * sizeof(*grown));
    if (grown == NULL)
        return -ENOMEM;
    walk->dirs = grown;
    walk->capacity = capacity;
    return 0;
}

static int push(Walk *walk, int fd)
{
    int status = reserve(walk, walk->count + 1u);

    if (status != 0)
        close(fd);
    else
        walk->dirs[walk->count++] = fd;
    return status;
}

static bool same_node(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int start_at(Walk *walk, int root, int base);

int astra_resolve_descriptor_path(int root, int fd, char *path,
                                  uint32_t capacity, uint32_t *length)
{
#ifdef __linux__
    char proc[64], root_path[TEXT_MAX], node_path[TEXT_MAX];
    struct stat root_st, node_st, check_st;
    struct open_how how = {
        .flags = O_PATH | O_CLOEXEC,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
                   RESOLVE_NO_MAGICLINKS,
    };
    ssize_t root_length, node_length;
    const char *relative;
    size_t prefix, size;
    int check, status = -ENOENT;

    if (path == NULL || length == NULL || capacity == 0u)
        return -EINVAL;
    *length = 0u;
    if (fstat(root, &root_st) != 0 || fstat(fd, &node_st) != 0)
        return -errno;
    if (!S_ISDIR(root_st.st_mode) || (!S_ISDIR(node_st.st_mode) && !S_ISREG(node_st.st_mode)))
        return -ENOTDIR;
    if (node_st.st_nlink == 0)
        return -ENOENT;
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", root);
    root_length = readlink(proc, root_path, sizeof(root_path) - 1u);
    if (root_length < 0)
        return -errno;
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    node_length = readlink(proc, node_path, sizeof(node_path) - 1u);
    if (node_length < 0)
        return -errno;
    if ((size_t)root_length == sizeof(root_path) - 1u ||
        (size_t)node_length == sizeof(node_path) - 1u)
        return -ENAMETOOLONG;
    root_path[root_length] = '\0';
    node_path[node_length] = '\0';
    prefix = root_length == 1 && root_path[0] == '/' ? 0u :
             (size_t)root_length;
    if ((size_t)node_length < prefix ||
        memcmp(root_path, node_path, prefix) != 0 ||
        (node_path[prefix] != '\0' && node_path[prefix] != '/'))
        return -ENOENT;
    relative = node_path[prefix] == '\0' ? "/" : node_path + prefix;
    check = syscall(SYS_openat2, root, relative[1] ? relative + 1 : ".",
                    &how, sizeof(how));
    if (check < 0)
        return -ENOENT;
    if (fstat(check, &check_st) == 0 && same_node(&node_st, &check_st)) {
        size = strlen(relative);
        if (size > capacity)
            status = -ERANGE;
        else {
            memcpy(path, relative, size);
            *length = (uint32_t)size;
            status = 0;
        }
    }
    close(check);
    return status;
#else
    (void)root;
    (void)fd;
    (void)path;
    (void)capacity;
    (void)length;
    return -ENOTSUP;
#endif
}

int astra_resolve_rooted(int root, int fd)
{
#ifdef __linux__
    char proc[64], root_path[TEXT_MAX], node_path[TEXT_MAX];
    struct stat root_st, node_st;
    ssize_t root_length, node_length;

    if (fstat(root, &root_st) != 0 || fstat(fd, &node_st) != 0)
        return -errno;
    if (!S_ISDIR(root_st.st_mode))
        return -ENOTDIR;
    if (same_node(&root_st, &node_st))
        return 0;
    if (S_ISDIR(node_st.st_mode)) {
        Walk walk = {0};
        int status = start_at(&walk, root, fd);

        while (walk.count != 0u)
            close(walk.dirs[--walk.count]);
        free(walk.dirs);
        return status;
    }
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", root);
    root_length = readlink(proc, root_path, sizeof(root_path));
    if (root_length < 0)
        return -errno;
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
    node_length = readlink(proc, node_path, sizeof(node_path));
    if (node_length < 0)
        return -errno;
    if ((size_t)root_length == sizeof(root_path) ||
        (size_t)node_length == sizeof(node_path))
        return -ENAMETOOLONG;
    if (root_length == 1 && root_path[0] == '/')
        return 0;
    return node_length > root_length &&
           memcmp(root_path, node_path, (size_t)root_length) == 0 &&
           node_path[root_length] == '/' ? 0 : -ESTALE;
#else
    (void)root;
    (void)fd;
    return -ENOTSUP;
#endif
}

/* Hold the base's actual ancestors so .. follows objects, not old spellings. */
static int start_at(Walk *walk, int root, int base)
{
    struct stat root_st, child_st;
    int fd = dup(base);
    int status;

    if (fd < 0)
        return -errno;
    status = push(walk, fd);
    if (status != 0)
        return status;
    if (fstat(root, &root_st) != 0)
        return -errno;
    for (;;) {
        struct stat parent_st;

        fd = walk->dirs[walk->count - 1u];
        if (fstat(fd, &child_st) != 0)
            return -errno;
        if (same_node(&root_st, &child_st))
            break;
        fd = openat(fd, "..", DIRECTORY_FLAGS);
        if (fd < 0)
            return -errno;
        if (fstat(fd, &parent_st) != 0) {
            status = -errno;
            close(fd);
            return status;
        }
        if (same_node(&parent_st, &child_st)) {
            close(fd);
            return -ESTALE;
        }
        status = push(walk, fd);
        if (status != 0)
            return status;
    }
    for (uint32_t index = 0u; index < walk->count / 2u; ++index) {
        int temp = walk->dirs[index];

        walk->dirs[index] = walk->dirs[walk->count - 1u - index];
        walk->dirs[walk->count - 1u - index] = temp;
    }
    return 0;
}

int astra_resolve(int root, int base, const char *path, unsigned flags,
                  uint32_t links_left, AstraResolution *out)
{
    Walk walk = {0};
    char *text = NULL;
    uint32_t *origin = NULL;
    size_t at = 0u, length;
    int status;

    if (out == NULL)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    out->parent = -1;
    if (links_left > ASTRA_RESOLVE_LINK_MAX || path == NULL || (flags & ~(ASTRA_RESOLVE_FOLLOW_FINAL |
                                   ASTRA_RESOLVE_ENTRY)) != 0u ||
        flags == (ASTRA_RESOLVE_FOLLOW_FINAL | ASTRA_RESOLVE_ENTRY))
        return -EINVAL;
    length = strnlen(path, TEXT_MAX);
    if (length == TEXT_MAX)
        return -ENAMETOOLONG;
    if (path[0] == '/')
        base = root;
    status = astra_resolve_rooted(root, base);
    if (status != 0)
        return status;
    if (length == 0u) {
        out->parent = dup(base);
        return out->parent < 0 ? -errno : 0;
    }
    status = start_at(&walk, root, base);
    if (status != 0)
        goto done;
    text = strdup(path);
    origin = malloc((length + 1u) * sizeof(*origin));
    if (text == NULL || origin == NULL) {
        status = -ENOMEM;
        goto done;
    }
    for (size_t index = 0u; index <= length; ++index)
        origin[index] = (uint32_t)index + 1u;
    for (;;) {
        char name[256];
        size_t start, end, next;
        bool last, trailing;
        int top = walk.dirs[walk.count - 1u];
        struct stat st;
        int fd;

        while (text[at] == '/')
            ++at;
        if (text[at] == '\0')
            break;
        start = at;
        while (text[at] != '\0' && text[at] != '/')
            ++at;
        end = at;
        next = end;
        while (text[next] == '/')
            ++next;
        last = text[next] == '\0';
        trailing = last && next != end;
        if (end - start >= sizeof(name)) {
            status = -ENAMETOOLONG;
            goto done;
        }
        memcpy(name, text + start, end - start);
        name[end - start] = '\0';
        if (strcmp(name, ".") == 0) {
            at = next;
            continue;
        }
        if (strcmp(name, "..") == 0) {
            if (walk.count == 1u) {
                out->escape = (origin[end - 1u] & FROM_LINK) ?
                    ASTRA_RESOLVE_ESCAPE_SYMLINK : ASTRA_RESOLVE_ESCAPE_PARENT;
                out->consumed = origin[end - 1u] & ~FROM_LINK;
                if (snprintf(out->continuation, sizeof(out->continuation),
                             "/%s", text + next) >= (int)sizeof(out->continuation)) {
                    status = -ENAMETOOLONG;
                    goto done;
                }
                status = -EXDEV;
                goto done;
            }
            close(walk.dirs[--walk.count]);
            at = next;
            continue;
        }
        if (last && !trailing && !(flags & ASTRA_RESOLVE_FOLLOW_FINAL)) {
            memcpy(out->leaf, name, end - start + 1u);
            break;
        }
#ifdef __linux__
        fd = openat(top, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
#else
        fd = openat(top, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#endif
        if (fd < 0) {
            if (errno == ENOENT && last) {
                memcpy(out->leaf, name, end - start + 1u);
                out->trailing = trailing;
                out->missing = 1u;
                break;
            }
            status = -errno;
            goto done;
        }
        if (fstat(fd, &st) != 0) {
            status = -errno;
            close(fd);
            goto done;
        }
        if (S_ISLNK(st.st_mode)) {
            char target[4096];
            char *spliced;
            uint32_t *positions;
            ssize_t bytes;
            size_t rest;
            uint32_t source = origin[end - 1u] | FROM_LINK;

            if (last && (flags & ASTRA_RESOLVE_ENTRY)) {
                close(fd);
                status = -ENOTDIR;
                goto done;
            }
            if (++out->links > links_left) {
                close(fd);
                status = -ELOOP;
                goto done;
            }
            /* The held symlink cannot be swapped between stat and readlink. */
            bytes = readlinkat(fd, "", target, sizeof(target));
            status = bytes < 0 ? -errno : bytes == 0 ? -ENOENT :
                     (size_t)bytes == sizeof(target) ? -ENAMETOOLONG : 0;
            close(fd);
            if (status != 0)
                goto done;
            if (target[0] == '/') {
                out->escape = ASTRA_RESOLVE_ESCAPE_SYMLINK;
                out->consumed = source & ~FROM_LINK;
                if ((size_t)bytes + strlen(text + end) >= sizeof(out->continuation)) {
                    status = -ENAMETOOLONG;
                    goto done;
                }
                memcpy(out->continuation, target, (size_t)bytes);
                strcpy(out->continuation + bytes, text + end);
                status = -EXDEV;
                goto done;
            }
            rest = strlen(text + end);
            if ((size_t)bytes + rest + 1u > TEXT_MAX) {
                status = -ENAMETOOLONG;
                goto done;
            }
            spliced = malloc((size_t)bytes + rest + 1u);
            positions = malloc(((size_t)bytes + rest + 1u) * sizeof(*positions));
            if (spliced == NULL || positions == NULL) {
                free(spliced);
                free(positions);
                status = -ENOMEM;
                goto done;
            }
            memcpy(spliced, target, (size_t)bytes);
            memcpy(spliced + bytes, text + end, rest + 1u);
            for (size_t index = 0u; index < (size_t)bytes; ++index)
                positions[index] = source;
            memcpy(positions + bytes, origin + end, (rest + 1u) * sizeof(*positions));
            free(text);
            free(origin);
            text = spliced;
            origin = positions;
            at = 0u;
            continue;
        }
        if (last) {
            close(fd);
            if (trailing && !S_ISDIR(st.st_mode)) {
                status = -ENOTDIR;
                goto done;
            }
            memcpy(out->leaf, name, end - start + 1u);
            out->trailing = trailing;
            break;
        }
        if (!S_ISDIR(st.st_mode)) {
            close(fd);
            status = -ENOTDIR;
            goto done;
        }
        status = push(&walk, fd);
        if (status != 0)
            goto done;
        at = next;
    }
    status = astra_resolve_rooted(root, walk.dirs[walk.count - 1u]);
    if (status == 0) {
        out->parent = dup(walk.dirs[walk.count - 1u]);
        status = out->parent < 0 ? -errno : 0;
    }
done:
    while (walk.count != 0u)
        close(walk.dirs[--walk.count]);
    free(walk.dirs);
    free(text);
    free(origin);
    return status;
}

int astra_resolve_open(int root, const AstraResolution *out, int flags,
                       mode_t mode)
{
#ifdef __linux__
    struct open_how how = {0};
    struct stat opened, entry;
    int fd, status;

    status = astra_resolve_rooted(root, out->parent);
    if (status != 0)
        return status;
    if (out->trailing) {
        if (flags & O_CREAT)
            return -EISDIR;
        flags |= O_DIRECTORY;
    }
    how.flags = (uint64_t)((flags & ~O_TRUNC) | O_NOFOLLOW | O_CLOEXEC);
    how.mode = (flags & O_CREAT) ? mode : 0u;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS;
    fd = (int)syscall(SYS_openat2, out->parent, out->leaf[0] ? out->leaf : ".",
                      &how, sizeof(how));
    if (fd < 0)
        return -errno;
    status = astra_resolve_rooted(root, out->parent);
    if (status == 0) {
        if (fstat(fd, &opened) != 0 ||
            fstatat(out->parent, out->leaf[0] ? out->leaf : ".", &entry,
                    AT_SYMLINK_NOFOLLOW) != 0)
            status = -errno;
        else if (!same_node(&opened, &entry))
            status = -ESTALE;
    }
    if (status == 0 && (flags & O_TRUNC) && ftruncate(fd, 0) != 0)
        status = -errno;
    if (status != 0) {
        close(fd);
        return status;
    }
    return fd;
#else
    (void)root;
    (void)out;
    (void)flags;
    (void)mode;
    return -ENOTSUP;
#endif
}

int astra_resolve_open_path(int root, int base, const char *path, int flags,
                            mode_t mode, unsigned resolve, uint32_t links_left,
                            AstraResolution *out)
{
    int fd, status;

#ifdef __linux__
    if (out != NULL && path != NULL && path[0] != '\0' &&
        strnlen(path, TEXT_MAX) < TEXT_MAX &&
        links_left <= ASTRA_RESOLVE_LINK_MAX &&
        (resolve & ~ASTRA_RESOLVE_FOLLOW_FINAL) == 0u &&
        (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC)) &&
        (flags & O_TMPFILE) != O_TMPFILE) {
        const char *part = path;
        bool plain = true;
        struct open_how how = {
            .flags = (uint64_t)((flags & ~O_NOFOLLOW) | O_CLOEXEC |
                (resolve & ASTRA_RESOLVE_FOLLOW_FINAL ? 0 : O_NOFOLLOW)),
            .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS |
                       RESOLVE_NO_MAGICLINKS,
        };

        /* Dots can cross the Astra namespace; Linux must not consume them. */
        while (*part != '\0') {
            size_t size;

            while (*part == '/')
                ++part;
            size = strcspn(part, "/");
            if ((size == 1u && part[0] == '.') ||
                (size == 2u && part[0] == '.' && part[1] == '.'))
                plain = false;
            part += size;
        }
        part = path;
        if (*part == '/') {
            base = root;
            while (*part == '/')
                ++part;
        }
        if (plain && (base == root || astra_resolve_rooted(root, base) == 0)) {
            fd = (int)syscall(SYS_openat2, base, *part ? part : ".",
                              &how, sizeof(how));
            if (fd >= 0) {
                if (base == root || astra_resolve_rooted(root, base) == 0) {
                    memset(out, 0, sizeof(*out));
                    out->parent = -1;
                    return fd;
                }
                close(fd);
            }
        }
    }
#endif
    status = astra_resolve(root, base, path, resolve, links_left, out);
    if (status != 0)
        return status;
    fd = astra_resolve_open(root, out, flags, mode);
    if (fd == -ENOENT)
        out->missing = 1u;
    close(out->parent);
    out->parent = -1;
    return fd;
}
