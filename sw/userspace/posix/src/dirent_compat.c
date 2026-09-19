#define _GNU_SOURCE

/*
 * Directory traversal helpers layered on the canonical DIR stream API.
 *
 * Keeping collection and sorting here means every filesystem gets identical
 * scandir semantics; filesystem implementations only have to supply the
 * streaming operations used by opendir/readdir/closedir.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
alphasort(const struct dirent **left, const struct dirent **right)
{
    return strcoll((*left)->d_name, (*right)->d_name);
}

int
versionsort(const struct dirent **left, const struct dirent **right)
{
    return strverscmp((*left)->d_name, (*right)->d_name);
}

int
readdir_r(DIR *restrict directory, struct dirent *restrict entry,
          struct dirent **restrict result)
{
    struct dirent *current;
    int error;
    int saved_errno = errno;

    errno = 0;
    current = readdir(directory);
    error = errno;
    if (current == NULL) {
        *result = NULL;
        errno = saved_errno;
        return error;
    }
    *entry = *current;
    *result = entry;
    errno = saved_errno;
    return 0;
}

static void
free_entries(struct dirent **entries, size_t count)
{
    while (count != 0u)
        free(entries[--count]);
    free(entries);
}

static int
sort_entries(struct dirent **entries, size_t count,
             int (*compare)(const struct dirent **,
                            const struct dirent **))
{
    struct dirent **source = entries;
    struct dirent **destination;
    struct dirent **scratch;
    size_t width;

    if (compare == NULL || count < 2u)
        return 0;
    if (count > SIZE_MAX / sizeof(*scratch)) {
        errno = EOVERFLOW;
        return -1;
    }
    scratch = malloc(count * sizeof(*scratch));
    if (scratch == NULL) {
        errno = ENOMEM;
        return -1;
    }
    destination = scratch;
    for (width = 1u; width < count;) {
        size_t begin;

        for (begin = 0u; begin < count; begin += width * 2u) {
            size_t left = begin;
            size_t middle = begin + width < count ? begin + width : count;
            size_t right = middle;
            size_t end = middle + width < count ? middle + width : count;
            size_t output = begin;

            while (left < middle && right < end) {
                const struct dirent *left_entry = source[left];
                const struct dirent *right_entry = source[right];

                if (compare(&left_entry, &right_entry) <= 0)
                    destination[output++] = source[left++];
                else
                    destination[output++] = source[right++];
            }
            while (left < middle)
                destination[output++] = source[left++];
            while (right < end)
                destination[output++] = source[right++];
        }
        {
            struct dirent **swap = source;
            source = destination;
            destination = swap;
        }
        if (width > count / 2u)
            break;
        width *= 2u;
    }
    if (source != entries)
        (void)memcpy(entries, source, count * sizeof(*entries));
    free(scratch);
    return 0;
}

static int
scan_stream(DIR *directory, struct dirent ***namelist,
            int (*select_entry)(const struct dirent *),
            int (*compare)(const struct dirent **,
                           const struct dirent **))
{
    struct dirent **entries = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    int error = 0;

    for (;;) {
        struct dirent *current;
        struct dirent *copy;

        errno = 0;
        current = readdir(directory);
        if (current == NULL) {
            error = errno;
            break;
        }
        if (select_entry != NULL && select_entry(current) == 0)
            continue;
        if (count == capacity) {
            struct dirent **grown;
            size_t new_capacity = capacity == 0u ? 1u : capacity * 2u;

            if (new_capacity < capacity ||
                new_capacity > SIZE_MAX / sizeof(*grown)) {
                error = EOVERFLOW;
                break;
            }
            grown = realloc(entries, new_capacity * sizeof(*grown));
            if (grown == NULL) {
                error = ENOMEM;
                break;
            }
            entries = grown;
            capacity = new_capacity;
        }
        copy = malloc(sizeof(*copy));
        if (copy == NULL) {
            error = ENOMEM;
            break;
        }
        *copy = *current;
        entries[count++] = copy;
    }
    if (closedir(directory) != 0 && error == 0)
        error = errno;
    if (error == 0 && count > (size_t)INT_MAX)
        error = EOVERFLOW;
    if (error == 0 && sort_entries(entries, count, compare) != 0)
        error = errno;
    if (error != 0) {
        free_entries(entries, count);
        errno = error;
        return -1;
    }
    *namelist = entries;
    return (int)count;
}

int
scandir(const char *path, struct dirent ***namelist,
        int (*select_entry)(const struct dirent *),
        int (*compare)(const struct dirent **, const struct dirent **))
{
    DIR *directory;

    directory = opendir(path);
    if (directory == NULL)
        return -1;
    return scan_stream(directory, namelist, select_entry, compare);
}

int
scandirat(int directory_fd, const char *path, struct dirent ***namelist,
          int (*select_entry)(const struct dirent *),
          int (*compare)(const struct dirent **,
                         const struct dirent **))
{
    DIR *directory;
    int fd;

    fd = openat(directory_fd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    directory = fdopendir(fd);
    if (directory == NULL) {
        int error = errno;

        (void)close(fd);
        errno = error;
        return -1;
    }
    return scan_stream(directory, namelist, select_entry, compare);
}
