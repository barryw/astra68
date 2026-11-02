#define _GNU_SOURCE
#include "astra_storage_config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#define CONFIG_MARKER ".astra-config-migrated"

static int copy_xattrs(int source, int target)
{
    ssize_t length = flistxattr(source, NULL, 0);
    if (length < 0) return errno;
    if (length == 0) return 0;
    if (length > 1 << 20) return E2BIG;
    char *names = malloc((size_t)length);
    if (names == NULL) return ENOMEM;
    int error = 0;
    if (flistxattr(source, names, (size_t)length) != length) error = ESTALE;
    for (ssize_t at = 0; error == 0 && at < length;) {
        size_t remaining = (size_t)(length - at);
        size_t name_length = strnlen(names + at, remaining);
        if (name_length == remaining) { error = EINVAL; break; }
        ssize_t size = fgetxattr(source, names + at, NULL, 0);
        if (size < 0) { error = errno; break; }
        if (size > 1 << 20) { error = E2BIG; break; }
        void *value = malloc(size == 0 ? 1u : (size_t)size);
        if (value == NULL) { error = ENOMEM; break; }
        if (fgetxattr(source, names + at, value, (size_t)size) != size)
            error = ESTALE;
        else if (fsetxattr(target, names + at, value, (size_t)size, 0) != 0)
            error = errno;
        free(value);
        at += (ssize_t)name_length + 1;
    }
    free(names);
    return error;
}

static int same_file(int source, int target, off_t size)
{
    char left[16384], right[16384];
    for (off_t offset = 0; offset < size;) {
        size_t wanted = (uint64_t)(size - offset) < sizeof(left) ?
            (size_t)(size - offset) : sizeof(left);
        ssize_t a = pread(source, left, wanted, offset);
        ssize_t b = pread(target, right, wanted, offset);
        if (a <= 0 || b != a || memcmp(left, right, (size_t)a) != 0)
            return EEXIST;
        offset += a;
    }
    return 0;
}

static int copy_regular(int source_dir, int target_dir, const char *name,
                        const struct stat *source_info, int initialized)
{
    int source = openat(source_dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (source < 0) return errno;
    struct stat current;
    int error = fstat(source, &current) != 0 ? errno :
        !S_ISREG(current.st_mode) || current.st_size != source_info->st_size ? ESTALE : 0;
    int existing = error == 0 ? openat(target_dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (existing >= 0) {
        struct stat target_info;
        error = fstat(existing, &target_info) != 0 ? errno :
            !S_ISREG(target_info.st_mode) ? EEXIST :
            initialized ? 0 :
            target_info.st_size != current.st_size ? EEXIST :
            same_file(source, existing, current.st_size);
        close(existing);
        close(source);
        return error;
    }
    if (error == 0 && errno != ENOENT) error = errno;
    int output = error == 0 ? openat(target_dir, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600) : -1;
    if (error == 0 && output < 0) error = errno;
    char bytes[16384];
    for (off_t offset = 0; error == 0 && offset < current.st_size;) {
        size_t wanted = (uint64_t)(current.st_size - offset) < sizeof(bytes) ?
            (size_t)(current.st_size - offset) : sizeof(bytes);
        ssize_t moved = pread(source, bytes, wanted, offset);
        if (moved < 0 && errno == EINTR) continue;
        if (moved <= 0) { error = moved < 0 ? errno : EIO; break; }
        for (ssize_t written = 0; written < moved;) {
            ssize_t n = write(output, bytes + written, (size_t)(moved - written));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { error = n < 0 ? errno : EIO; break; }
            written += n;
        }
        offset += moved;
    }
    struct stat after;
    if (error == 0 && fstat(source, &after) != 0) error = errno;
    if (error == 0 && (after.st_size != current.st_size ||
        after.st_mtim.tv_sec != current.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != current.st_mtim.tv_nsec ||
        after.st_ctim.tv_sec != current.st_ctim.tv_sec ||
        after.st_ctim.tv_nsec != current.st_ctim.tv_nsec)) error = ESTALE;
    if (error == 0 && fchown(output, current.st_uid, current.st_gid) != 0) error = errno;
    if (error == 0 && fchmod(output, current.st_mode & 07777) != 0) error = errno;
    if (error == 0) error = copy_xattrs(source, output);
    const struct timespec times[2] = {current.st_atim, current.st_mtim};
    if (error == 0 && futimens(output, times) != 0) error = errno;
    if (error == 0 && fsync(output) != 0) error = errno;
    if (error == 0) {
        char descriptor[64];
        (void)snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", output);
        if (linkat(AT_FDCWD, descriptor, target_dir, name, AT_SYMLINK_FOLLOW) != 0)
            error = errno;
    }
    if (output >= 0) close(output);
    close(source);
    return error;
}

static int copy_link(int source_dir, int target_dir, const char *name,
                     int initialized)
{
    char link[4096], existing[4096];
    char source_path[512];
    struct stat info;
    if (fstatat(source_dir, name, &info, AT_SYMLINK_NOFOLLOW) != 0)
        return errno;
    if (snprintf(source_path, sizeof(source_path), "/proc/self/fd/%d/%s",
                 source_dir, name) >= (int)sizeof(source_path)) return ENAMETOOLONG;
    ssize_t attributes = llistxattr(source_path, NULL, 0);
    if (attributes < 0) return errno;
    /* No symlink attributes may disappear during migration. */
    if (attributes != 0) return ENOTSUP;
    ssize_t length = readlinkat(source_dir, name, link, sizeof(link));
    if (length < 0) return errno;
    if ((size_t)length == sizeof(link)) return ENAMETOOLONG;
    link[length] = '\0';
    ssize_t present = readlinkat(target_dir, name, existing, sizeof(existing));
    if (present >= 0) {
        if (initialized) return 0;
        if (present != length || memcmp(link, existing, (size_t)length) != 0)
            return EEXIST;
    } else {
        if (errno != ENOENT) return errno;
        if (symlinkat(link, target_dir, name) != 0) return errno;
    }
    if (fchownat(target_dir, name, info.st_uid, info.st_gid,
                 AT_SYMLINK_NOFOLLOW) != 0) return errno;
    const struct timespec times[2] = {info.st_atim, info.st_mtim};
    return utimensat(target_dir, name, times, AT_SYMLINK_NOFOLLOW) == 0 ? 0 : errno;
}

static int copy_tree(int source, int target, int initialized)
{
    int scan = openat(source, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *entries = scan < 0 ? NULL : fdopendir(scan);
    if (entries == NULL) { int error = errno; if (scan >= 0) close(scan); return error; }
    int error = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(entries);
        if (entry == NULL) { if (error == 0) error = errno; break; }
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        struct stat info;
        if (fstatat(source, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            error = errno; break;
        }
        if (initialized) {
            struct stat prior;
            if (fstatat(target, name, &prior, AT_SYMLINK_NOFOLLOW) == 0) {
                if (!S_ISDIR(info.st_mode) || !S_ISDIR(prior.st_mode))
                    continue;
            } else if (errno != ENOENT) {
                error = errno; break;
            }
        }
        if (S_ISDIR(info.st_mode)) {
            if (mkdirat(target, name, 0700) != 0 && errno != EEXIST) {
                error = errno; break;
            }
            int child_source = openat(source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            int child_target = openat(target, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child_source < 0 || child_target < 0)
                error = errno;
            else {
                error = copy_tree(child_source, child_target, initialized);
                if (error == 0 && !initialized) {
                    if (fchown(child_target, info.st_uid, info.st_gid) != 0 ||
                        fchmod(child_target, info.st_mode & 07777) != 0)
                        error = errno;
                    if (error == 0) error = copy_xattrs(child_source, child_target);
                    const struct timespec times[2] = {info.st_atim, info.st_mtim};
                    if (error == 0 && futimens(child_target, times) != 0)
                        error = errno;
                    if (error == 0 && fsync(child_target) != 0)
                        error = errno;
                }
            }
            if (child_source >= 0) close(child_source);
            if (child_target >= 0) close(child_target);
        } else if (S_ISREG(info.st_mode)) {
            error = copy_regular(source, target, name, &info, initialized);
        } else if (S_ISLNK(info.st_mode)) {
            error = copy_link(source, target, name, initialized);
        } else {
            error = ENOTSUP;
        }
        if (error != 0) break;
    }
    if (error == 0 && fsync(target) != 0) error = errno;
    closedir(entries);
    return error;
}

static int clear_tree(int source)
{
    int scan = openat(source, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *entries = scan < 0 ? NULL : fdopendir(scan);
    if (entries == NULL) { int error = errno; if (scan >= 0) close(scan); return error; }
    int error = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(entries);
        if (entry == NULL) { if (error == 0) error = errno; break; }
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        struct stat info;
        if (fstatat(source, name, &info, AT_SYMLINK_NOFOLLOW) != 0) { error = errno; break; }
        if (S_ISDIR(info.st_mode)) {
            int child = openat(source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) { error = errno; break; }
            error = clear_tree(child);
            close(child);
            if (error == 0 && unlinkat(source, name, AT_REMOVEDIR) != 0) error = errno;
        } else if (unlinkat(source, name, 0) != 0) {
            error = errno;
        }
        if (error != 0) break;
        rewinddir(entries);
    }
    if (error == 0 && fsync(source) != 0) error = errno;
    closedir(entries);
    return error;
}

static int mark_initialized(int private_root)
{
    int marker = openat(private_root, CONFIG_MARKER,
                        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (marker < 0) return errno;
    int error = fsync(marker) == 0 ? 0 : errno;
    close(marker);
    if (error == 0 && fsync(private_root) != 0) error = errno;
    return error;
}

int astra_storage_config_migrate(int system_root, int private_root)
{
    int source = openat(system_root, "config", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (source < 0 && errno != ENOENT) return errno;
    if (source < 0) {
        if (mkdirat(private_root, "config", 0700) != 0 && errno != EEXIST)
            return errno;
        int empty = openat(private_root, "config", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (empty < 0) return errno;
        close(empty);
        int marker = openat(private_root, CONFIG_MARKER, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (marker >= 0) { close(marker); return 0; }
        return errno == ENOENT ? mark_initialized(private_root) : errno;
    }
    int marker = openat(private_root, CONFIG_MARKER, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    int initialized = marker >= 0;
    if (marker >= 0) close(marker);
    else if (errno != ENOENT) { int error = errno; close(source); return error; }
    if (mkdirat(private_root, "config", 0700) != 0 && errno != EEXIST) {
        int error = errno; close(source); return error;
    }
    int target = openat(private_root, "config", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (target < 0) { int error = errno; close(source); return error; }
    int error = fchmod(target, 0700) == 0 ? copy_tree(source, target, initialized) : errno;
    if (error == 0 && !initialized) error = mark_initialized(private_root);
    if (error == 0) error = clear_tree(source);
    close(target);
    close(source);
    return error;
}
