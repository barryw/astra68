#define _GNU_SOURCE
#include "astra_storage_path.h"
#include "astra68-resolve.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

int astra_storage_path_seed(int root, int source, const char *directory)
{
    AstraResolution resolved = {.parent = -1};
    int error = astra_resolve(root, root, directory, ASTRA_RESOLVE_ENTRY,
                              ASTRA_RESOLVE_LINK_MAX, &resolved);
    if (error != 0) return -error;
    if (resolved.leaf[0] == '\0') error = EINVAL;
    else if (mkdirat(resolved.parent, resolved.leaf, 0755) != 0 && errno != EEXIST)
        error = errno;
    close(resolved.parent);
    if (error != 0) return error;
    int target = astra_resolve_open_path(root, root, directory,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC, 0u, 0u,
        ASTRA_RESOLVE_LINK_MAX, &resolved);
    if (target < 0) return -target;
    int scan = openat(source, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *entries = scan < 0 ? NULL : fdopendir(scan);
    if (entries == NULL) {
        error = errno;
        if (scan >= 0) close(scan);
        close(target);
        return error;
    }
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(entries);
        if (entry == NULL) { error = errno; break; }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        struct stat existing;
        if (fstatat(target, entry->d_name, &existing, AT_SYMLINK_NOFOLLOW) == 0) continue;
        if (errno != ENOENT) { error = errno; break; }
        int input = openat(source, entry->d_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (input < 0) { error = errno; break; }
        struct stat before, after;
        error = fstat(input, &before) != 0 ? errno :
            !S_ISREG(before.st_mode) || before.st_size < 0 ? EINVAL : 0;
        int output = error == 0 ? openat(target, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0644) : -1;
        if (error == 0 && output < 0) error = errno;
        off_t copied = 0;
        char bytes[65536];
        while (error == 0 && copied < before.st_size) {
            size_t wanted = (uint64_t)(before.st_size - copied) < sizeof(bytes) ?
                (size_t)(before.st_size - copied) : sizeof(bytes);
            ssize_t count = pread(input, bytes, wanted, copied);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { error = count < 0 ? errno : EIO; break; }
            ssize_t written = 0;
            while (written < count) {
                ssize_t amount = write(output, bytes + written, (size_t)(count - written));
                if (amount < 0 && errno == EINTR) continue;
                if (amount <= 0) { error = amount < 0 ? errno : EIO; break; }
                written += amount;
            }
            copied += count;
        }
        if (error == 0 && fstat(input, &after) != 0) error = errno;
        if (error == 0 && (after.st_size != before.st_size ||
            after.st_mtim.tv_sec != before.st_mtim.tv_sec || after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
            after.st_ctim.tv_sec != before.st_ctim.tv_sec || after.st_ctim.tv_nsec != before.st_ctim.tv_nsec))
            error = ESTALE;
        if (error == 0 && fsync(output) != 0) error = errno;
        if (error == 0) {
            int rooted = astra_resolve_rooted(root, target);
            char descriptor[64];
            (void)snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", output);
            if (rooted != 0) error = -rooted;
            else if (linkat(AT_FDCWD, descriptor, target, entry->d_name, AT_SYMLINK_FOLLOW) != 0 && errno != EEXIST)
                error = errno;
        }
        if (output >= 0) close(output);
        close(input);
        if (error != 0) break;
    }
    if (error == 0 && fsync(target) != 0) error = errno;
    closedir(entries);
    close(target);
    return error;
}

static void close_fds(int *fds, unsigned count)
{
    for (unsigned at = 0; at < count; ++at)
        if (fds[at] >= 0) {
            close(fds[at]);
            fds[at] = -1;
        }
}

static unsigned fd_count(uint32_t mask)
{
    return (mask & 1u) + ((mask >> 1) & 1u) + ((mask >> 2) & 1u);
}

int astra_storage_path_receive(int socket, pid_t peer, uid_t uid, gid_t gid,
                                AstraStoragePathRequest *request,
                                int fds[ASTRA_STORAGE_PROVIDER_FDS])
{
    struct ucred credentials = {0};
    int packed[ASTRA_STORAGE_PROVIDER_FDS];
    unsigned count = 0;
    unsigned at = 0;
    memset(request, 0, sizeof(*request));
    int error = astra_storage_receive_packet(socket, request, sizeof(*request), packed,
                                &count, &credentials);
    for (unsigned index = 0; index < ASTRA_STORAGE_PROVIDER_FDS; ++index)
        fds[index] = -1;
    if (error != 0)
        return error;
    if (credentials.pid != peer || credentials.uid != uid || credentials.gid != gid)
        error = EACCES;
    else if (request->version != ASTRA_STORAGE_PROVIDER_VERSION ||
             request->serial == 0u || request->epoch == 0u || request->mount == 0u ||
             request->generation == 0u || request->fd_mask & ~7u ||
             (request->fd_mask & 1u) == 0u ||
             count != fd_count(request->fd_mask) ||
             request->reserved[0] != 0u || request->reserved[1] != 0u ||
             memchr(request->path, '\0', sizeof(request->path)) == NULL ||
             memchr(request->path2, '\0', sizeof(request->path2)) == NULL)
        error = EPROTO;
    if (error != 0) {
        close_fds(packed, count);
        return error;
    }
    for (unsigned index = 0; index < ASTRA_STORAGE_PROVIDER_FDS; ++index)
        if (request->fd_mask & (1u << index))
            fds[index] = packed[at++];
    return 0;
}

int astra_storage_path_reply(int socket, const AstraStoragePathReply *reply,
                              int result)
{
    return astra_storage_send_packet(socket, reply, sizeof(*reply), &result, result < 0 ? 0u : 1u);
}

static bool mutates(const AstraStoragePathRequest *request)
{
    return (request->operation >= ASTRA_STORAGE_PATH_MKDIR &&
            request->operation != ASTRA_STORAGE_PATH_READLINK) ||
           (request->operation == ASTRA_STORAGE_PATH_OPEN &&
            (request->flags & (O_ACCMODE | O_CREAT | O_TRUNC)) != 0u);
}

static int change_mode(int fd, mode_t mode)
{
    struct stat info;
    char path[64];
    if (fstat(fd, &info) != 0)
        return errno;
    if (S_ISLNK(info.st_mode))
        return ENOTSUP;
    (void)snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    return chmod(path, mode) == 0 ? 0 : errno;
}

int astra_storage_path_roots(int allowed_root,
                              const int fds[ASTRA_STORAGE_PROVIDER_FDS])
{
    struct statx root;
    if (statx(allowed_root, "", AT_EMPTY_PATH, STATX_MNT_ID, &root) != 0)
        return -errno;
    if (!(root.stx_mask & STATX_MNT_ID))
        return -ENOTSUP;
    for (unsigned at = 0u; at < ASTRA_STORAGE_PROVIDER_FDS; ++at) {
        struct statx candidate;
        if (fds[at] < 0)
            continue;
        if (statx(fds[at], "", AT_EMPTY_PATH, STATX_MNT_ID, &candidate) != 0)
            return -errno;
        if (!(candidate.stx_mask & STATX_MNT_ID) || candidate.stx_mnt_id != root.stx_mnt_id)
            return -EXDEV;
    }
    return astra_resolve_rooted(allowed_root, fds[0]);
}

void astra_storage_path_execute(int allowed_root, int readonly,
                                 const AstraStoragePathRequest *request,
                                 const int fds[ASTRA_STORAGE_PROVIDER_FDS],
                                 AstraStoragePathReply *reply, int *result)
{
    AstraResolution left = {.parent = -1};
    AstraResolution right = {.parent = -1};
    AstraResolution *last = &left;
    int root = fds[0];
    int base = fds[1] < 0 ? root : fds[1];
    int second = fds[2] < 0 ? root : fds[2];
    int error;
    int fd = -1;
    const char *path = request->operation == ASTRA_STORAGE_PATH_SYMLINK ?
        request->path2 : request->path;
    char mkdir_path[ASTRA_STORAGE_PROVIDER_PATH_MAX];

    if (request->operation == ASTRA_STORAGE_PATH_MKDIR) {
        memcpy(mkdir_path, request->path, sizeof(mkdir_path));
        for (size_t length = strlen(mkdir_path);
             length > 1u && mkdir_path[length - 1u] == '/'; --length)
            mkdir_path[length - 1u] = '\0';
        path = mkdir_path;
    }

    *result = -1;
    *reply = (AstraStoragePathReply){.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .serial = request->serial, .epoch = request->epoch};
    error = astra_storage_path_roots(allowed_root, fds);
    if (error != 0)
        goto done;
    if (readonly && mutates(request)) {
        error = -EROFS;
        goto done;
    }
    if (request->mode & ~07777u || request->resolve &
        ~(ASTRA_RESOLVE_FOLLOW_FINAL | ASTRA_RESOLVE_ENTRY) ||
        request->links > ASTRA_RESOLVE_LINK_MAX ||
        request->capacity > sizeof(reply->data)) {
        error = -EINVAL;
        goto done;
    }
    switch (request->operation) {
    case ASTRA_STORAGE_PATH_OPEN:
        if (request->flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC |
                              O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_PATH |
                              O_CLOEXEC)) {
            error = -EINVAL;
            break;
        }
        *result = astra_resolve_open_path(root, base, path,
            request->flags | O_CLOEXEC, request->mode, request->resolve,
            request->links, &left);
        error = *result < 0 ? *result : 0;
        break;
    case ASTRA_STORAGE_PATH_ROOTED:
        error = fds[1] < 0 ? -EINVAL : astra_resolve_rooted(root, base);
        break;
    case ASTRA_STORAGE_PATH_NAME:
        error = fds[1] < 0 ? -EINVAL : astra_resolve_descriptor_path(root, base,
            reply->data, request->capacity, &reply->length);
        break;
    case ASTRA_STORAGE_PATH_CHMOD:
        if (fds[1] >= 0 && path[0] == '\0') {
            /* An unlinked retained file still carries its mount authority. */
            struct statx a, b;
            if (statx(root, "", AT_EMPTY_PATH, STATX_MNT_ID, &a) != 0 ||
                statx(base, "", AT_EMPTY_PATH, STATX_MNT_ID, &b) != 0)
                error = -errno;
            else if (!(a.stx_mask & b.stx_mask & STATX_MNT_ID) ||
                     a.stx_mnt_id != b.stx_mnt_id)
                error = -EXDEV;
            else
                error = -change_mode(base, request->mode);
        } else {
            fd = astra_resolve_open_path(root, base, path, O_PATH | O_CLOEXEC,
                0, request->resolve, request->links, &left);
            error = fd < 0 ? fd : -change_mode(fd, request->mode);
        }
        break;
    case ASTRA_STORAGE_PATH_RESOLVE:
    case ASTRA_STORAGE_PATH_MKDIR:
    case ASTRA_STORAGE_PATH_UNLINK:
    case ASTRA_STORAGE_PATH_RENAME:
    case ASTRA_STORAGE_PATH_LINK:
    case ASTRA_STORAGE_PATH_READLINK:
    case ASTRA_STORAGE_PATH_SYMLINK:
        error = astra_resolve(root, base, path, request->resolve,
                              request->links, &left);
        if (error != 0)
            break;
        if (request->operation == ASTRA_STORAGE_PATH_RESOLVE) {
            *result = left.parent;
            left.parent = -1;
            break;
        }
        if (request->operation == ASTRA_STORAGE_PATH_RENAME ||
            request->operation == ASTRA_STORAGE_PATH_LINK) {
            last = &right;
            error = astra_resolve(root, second, request->path2, request->resolve,
                                  request->links, &right);
            if (error != 0)
                break;
        }
        if (left.leaf[0] == '\0' ||
            (right.parent >= 0 && right.leaf[0] == '\0')) {
            error = request->operation == ASTRA_STORAGE_PATH_READLINK ? -EINVAL :
                    request->operation == ASTRA_STORAGE_PATH_MKDIR ||
                    request->operation == ASTRA_STORAGE_PATH_SYMLINK ? -EEXIST : -EACCES;
            break;
        }
        if (request->operation == ASTRA_STORAGE_PATH_MKDIR)
            error = mkdirat(left.parent, left.leaf, request->mode);
        else if (request->operation == ASTRA_STORAGE_PATH_UNLINK)
            error = request->flags & ~AT_REMOVEDIR ? -EINVAL :
                unlinkat(left.parent, left.leaf, request->flags);
        else if (request->operation == ASTRA_STORAGE_PATH_READLINK) {
            char bytes[ASTRA_STORAGE_PROVIDER_PATH_MAX + 1u];
            ssize_t length = readlinkat(left.parent, left.leaf, bytes,
                                        (size_t)request->capacity + 1u);
            error = length < 0 ? -errno : (uint32_t)length > request->capacity ?
                -ERANGE : 0;
            if (error == 0) {
                memcpy(reply->data, bytes, (size_t)length);
                reply->length = (uint32_t)length;
            }
            break;
        } else if (request->operation == ASTRA_STORAGE_PATH_SYMLINK)
            error = left.trailing ? -ENOENT :
                symlinkat(request->path, left.parent, left.leaf);
        else if (request->operation == ASTRA_STORAGE_PATH_LINK)
            error = right.trailing ? -ENOENT : left.trailing ? -EISDIR :
                linkat(left.parent, left.leaf, right.parent, right.leaf, 0);
        else {
            struct stat info;
            if (right.trailing && (fstatat(left.parent, left.leaf, &info,
                    AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(info.st_mode)))
                error = -ENOTDIR;
            else
                error = renameat(left.parent, left.leaf, right.parent, right.leaf);
        }
        if (error == -1)
            error = -errno;
        break;
    default:
        error = -ENOTSUP;
        break;
    }
done:
    reply->error = error < 0 ? -error : error;
    reply->trailing = last->trailing;
    reply->missing = last->missing;
    reply->escape = last->escape;
    reply->consumed = last->consumed;
    reply->links = last->links;
    memcpy(reply->leaf, last->leaf, sizeof(reply->leaf));
    memcpy(reply->continuation, last->continuation, sizeof(reply->continuation));
    if (left.parent >= 0)
        close(left.parent);
    if (right.parent >= 0)
        close(right.parent);
    if (fd >= 0)
        close(fd);
    if (reply->error != 0 && *result >= 0) {
        close(*result);
        *result = -1;
    }
}
