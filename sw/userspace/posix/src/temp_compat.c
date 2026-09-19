#define _GNU_SOURCE 1

/* Atomic temporary-file and temporary-directory compatibility entry points. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TEMPORARY_DIGITS 6u
#define TEMPORARY_RADIX 62u
#define TEMPORARY_NAMESPACE UINT64_C(56800235584)

static void
temporary_name(char *digits, uint64_t value)
{
    static const char alphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (size_t index = TEMPORARY_DIGITS; index != 0u;) {
        --index;
        digits[index] = alphabet[value % TEMPORARY_RADIX];
        value /= TEMPORARY_RADIX;
    }
}

char *
mkdtemp(char *path)
{
    uint64_t candidate;
    char *digits;
    size_t length;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    length = strlen(path);
    if (length < TEMPORARY_DIGITS) {
        errno = EINVAL;
        return NULL;
    }
    digits = path + length - TEMPORARY_DIGITS;
    for (size_t index = 0u; index < TEMPORARY_DIGITS; ++index)
        if (digits[index] != 'X') {
            errno = EINVAL;
            return NULL;
        }

    arc4random_buf(&candidate, sizeof(candidate));
    candidate %= TEMPORARY_NAMESPACE;
    for (uint64_t tried = 0u; tried < TEMPORARY_NAMESPACE; ++tried) {
        temporary_name(digits, candidate);
        if (mkdir(path, 0700) == 0)
            return path;
        if (errno != EEXIST)
            return NULL;
        if (++candidate == TEMPORARY_NAMESPACE)
            candidate = 0u;
    }
    errno = EEXIST;
    return NULL;
}

int
mkostemp(char *path, int flags)
{
    return mkostemps(path, 0, flags);
}
