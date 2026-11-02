#define _GNU_SOURCE
#include "astra_storage_secret.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int trusted_open(const char *path, int flags)
{
    struct open_how how = {.flags = (uint64_t)(flags | O_CLOEXEC),
        .resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS};
    return (int)syscall(SYS_openat2, AT_FDCWD, path, &how, sizeof(how));
}

static int complete_random(uint8_t *out, size_t size)
{
    while (size != 0u) {
        ssize_t count = syscall(SYS_getrandom, out, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        out += count;
        size -= (size_t)count;
    }
    return 0;
}

static int valid_key(int fd)
{
    struct stat info = {0};
    uint8_t key[32];
    if (fstat(fd, &info) != 0) return -1;
    if (!S_ISREG(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 0777u) != 0600u || info.st_size != sizeof(key)) {
        errno = EACCES;
        return -1;
    }
    ssize_t count = pread(fd, key, sizeof(key), 0);
    memset(key, 0, sizeof(key));
    if (count != sizeof(key)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int astra_storage_secret_open(const char *path)
{
    uint8_t key[32];
    int fd = -1, parent = -1, saved;
    char *directory = NULL, temporary[48];
    const char *name;
    size_t length;
    struct stat info = {0};
    uint8_t suffix[8];

    if (path == NULL || path[0] != '/' ||
        (length = strlen(path)) > 4096u || length < 2u) {
        errno = EINVAL;
        return -1;
    }
    name = strrchr(path, '/');
    if (name == NULL || name[1] == '\0') {
        errno = EINVAL;
        return -1;
    }
    directory = strndup(path, name == path ? 1u : (size_t)(name - path));
    if (directory == NULL) return -1;
    parent = trusted_open(directory, O_RDONLY | O_DIRECTORY);
    if (parent < 0 || fstat(parent, &info) != 0 || info.st_uid != 0 ||
        !S_ISDIR(info.st_mode) || (info.st_mode & 0022u) != 0u) {
        if (parent >= 0) close(parent);
        free(directory);
        errno = EACCES;
        return -1;
    }
    free(directory);
    name++;
    fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) {
        saved = valid_key(fd) == 0 ? 0 : errno;
        close(parent);
        if (saved == 0) return fd;
        close(fd); errno = saved; return -1;
    }
    if (errno != ENOENT) goto failed;
    if (complete_random(suffix, sizeof(suffix)) != 0) goto failed;
    (void)memcpy(temporary, ".astra-key-", 11u);
    static const char digits[] = "0123456789abcdef";
    for (unsigned at = 0u; at < sizeof(suffix); ++at) {
        temporary[11u + at * 2u] = digits[suffix[at] >> 4];
        temporary[12u + at * 2u] = digits[suffix[at] & 15u];
    }
    temporary[27] = '\0';
    fd = openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL |
                O_CLOEXEC | O_NOFOLLOW, 0600u);
    if (fd < 0) goto failed;
    if (complete_random(key, sizeof(key)) != 0 ||
        write(fd, key, sizeof(key)) != sizeof(key) || fsync(fd) != 0)
        goto failed_file;
    memset(key, 0, sizeof(key));
    if (syscall(SYS_renameat2, parent, temporary, parent, name,
                RENAME_NOREPLACE) != 0 && errno != EEXIST)
        goto failed_file;
    close(fd);
    fd = -1;
    fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || valid_key(fd) != 0) goto failed_file;
    /* Existing key wins races. Never overwrite a key protecting old secrets. */
    (void)unlinkat(parent, temporary, 0);
    if (fsync(parent) != 0) goto failed_file;
    close(parent);
    return fd;

failed_file:
    saved = errno;
    memset(key, 0, sizeof(key));
    if (fd >= 0) close(fd);
    (void)unlinkat(parent, temporary, 0);
    errno = saved;
failed:
    if (parent >= 0) close(parent);
    return -1;
}
