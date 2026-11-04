#define _GNU_SOURCE
#include "astra_storage_mount.h"
#include <astra/secret_memory.h>

#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libmount/libmount.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <linux/mount.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int mount_id(int fd, const char *path, uint64_t *id)
{
    struct statx info;
    if (statx(fd, path, AT_SYMLINK_NOFOLLOW | (path[0] == '\0' ? AT_EMPTY_PATH : 0),
               STATX_MNT_ID, &info) != 0)
        return errno;
    if ((info.stx_mask & STATX_MNT_ID) == 0u || info.stx_mnt_id == 0u)
        return ENOTSUP;
    *id = info.stx_mnt_id;
    return 0;
}

int astra_storage_domain_open(AstraStorageDomain *domain, uid_t uid, gid_t gid)
{
    char path[] = "/var/tmp/astra-storage.XXXXXX";
    int directory;

    if (domain == NULL || uid == 0u || gid == 0u || geteuid() != 0)
        return EPERM;
    if (domain->path[0] != '\0')
        return EBUSY;
    if (mkdtemp(path) == NULL)
        return errno;
    directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        int error = errno;
        (void)rmdir(path);
        return error;
    }
    *domain = (AstraStorageDomain){.directory = directory, .uid = uid, .gid = gid};
    memcpy(domain->path, path, sizeof(path));
    return 0;
}

/* Pathname removal never targets a replacement directory. The retained inode
 * remains the owner even after a normal child removed its last directory link. */
static int domain_identity(const AstraStorageDomain *domain, struct stat *held)
{
    struct stat named;
    if (domain == NULL || domain->directory < 0 || domain->path[0] == '\0' ||
        memchr(domain->path, '\0', sizeof(domain->path)) == NULL || geteuid() != 0)
        return EINVAL;
    if (fstat(domain->directory, held) != 0) return errno;
    if (!S_ISDIR(held->st_mode) || held->st_uid != 0u || (held->st_mode & 0777u) != 0700u)
        return EACCES;
    if (held->st_nlink == 0u) return 0;
    if (lstat(domain->path, &named) != 0) return errno;
    return S_ISDIR(named.st_mode) && named.st_dev == held->st_dev && named.st_ino == held->st_ino ? 0 : ESTALE;
}

int astra_storage_domain_enter(AstraStorageDomain *domain)
{
    struct stat held, entered;
    int error = domain_identity(domain, &held);
    if (error != 0) return error;
    if (held.st_nlink == 0u || domain->uid == 0u || domain->gid == 0u) return ESTALE;
    if (unshare(CLONE_NEWNS) != 0 || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
        return errno;
    /* A pre-unshare FD retains the old vfsmount. Reopen the same protected
     * inode so relative target lookup sees mounts in this private namespace. */
    int directory = open(domain->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) return errno;
    error = fstat(directory, &entered) != 0 ? errno :
        entered.st_dev != held.st_dev || entered.st_ino != held.st_ino ||
        !S_ISDIR(entered.st_mode) || entered.st_uid != 0u ||
        (entered.st_mode & 0777u) != 0700u ? ESTALE : 0;
    if (error != 0) { close(directory); return error; }
    close(domain->directory);
    domain->directory = directory;
    return 0;
}

int astra_storage_domain_close(AstraStorageDomain *domain)
{
    struct stat held;
    int error = domain_identity(domain, &held);
    if (error != 0) return error;
    if (held.st_nlink != 0u && rmdir(domain->path) != 0)
        return errno;
    close(domain->directory);
    *domain = (AstraStorageDomain){.directory = -1};
    return 0;
}

static int domain_mountpoint_name(const char *name)
{
    if (strlen(name) != 16u) return 0;
    int nonzero = 0;
    for (unsigned at = 0u; at < 16u; ++at) {
        if ((name[at] < '0' || name[at] > '9') && (name[at] < 'a' || name[at] > 'f')) return 0;
        if (name[at] != '0') nonzero = 1;
    }
    return nonzero;
}

int astra_storage_domain_retire(AstraStorageDomain *domain)
{
    struct stat held;
    int error = domain_identity(domain, &held);
    if (error != 0) return error;
    if (held.st_nlink == 0u) return astra_storage_domain_close(domain);
    int copy = openat(domain->directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (copy < 0) return errno;
    DIR *directory = fdopendir(copy);
    if (directory == NULL) { error = errno; close(copy); return error; }
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) { error = errno; break; }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        struct stat point;
        if (!domain_mountpoint_name(entry->d_name)) { error = EPROTO; break; }
        if (fstatat(domain->directory, entry->d_name, &point, AT_SYMLINK_NOFOLLOW) != 0) { error = errno; break; }
        if (!S_ISDIR(point.st_mode) || point.st_uid != 0u || (point.st_mode & 0777u) != 0700u) { error = EACCES; break; }
        if (unlinkat(domain->directory, entry->d_name, AT_REMOVEDIR) != 0) { error = errno; break; }
    }
    if (closedir(directory) != 0 && error == 0) error = errno;
    return error == 0 ? astra_storage_domain_close(domain) : error;
}

static const char *fstype(uint32_t format)
{
    switch (format) {
    case ASTRA_VOLUME_EXT4: return "ext4";
    case ASTRA_VOLUME_FAT32: return "vfat";
    case ASTRA_VOLUME_FAT12: return "vfat";
    case ASTRA_VOLUME_FAT16: return "vfat";
    case ASTRA_VOLUME_NTFS: return "ntfs3";
    case ASTRA_VOLUME_EXFAT: return "exfat";
    case ASTRA_VOLUME_ISO9660: return "iso9660";
    case ASTRA_VOLUME_UDF: return "udf";
    case ASTRA_VOLUME_TMPFS: return "tmpfs";
    case ASTRA_VOLUME_NFS: return "nfs4";
    case ASTRA_VOLUME_SMB: return "cifs";
    default: return NULL;
    }
}

static int context(struct libmnt_context **out)
{
    struct libmnt_context *ctx = mnt_new_context();
    if (ctx == NULL)
        return ENOMEM;
    if (mnt_context_set_optsmode(ctx, MNT_OMODE_NOTAB) != 0 ||
        mnt_context_disable_canonicalize(ctx, 1) != 0 ||
        mnt_context_disable_helpers(ctx, 1) != 0 ||
        mnt_context_disable_mtab(ctx, 1) != 0 ||
        mnt_context_enable_rwonly_mount(ctx, 1) != 0) {
        mnt_free_context(ctx);
        return EINVAL;
    }
    *out = ctx;
    return 0;
}

int astra_storage_linux_loop(int source, uint64_t offset, uint64_t length, int readonly)
{
    int control;
    int number;
    int loop;
    char name[64];
    struct stat info;
    struct loop_config config = {
        .fd = (uint32_t)source,
        .info = {.lo_offset = offset, .lo_sizelimit = length,
            .lo_flags = LO_FLAGS_AUTOCLEAR | (readonly ? LO_FLAGS_READ_ONLY : 0u)}
    };

    if (fstat(source, &info) != 0)
        return -errno;
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 || length == 0u ||
        offset > (uint64_t)info.st_size || length > (uint64_t)info.st_size - offset ||
        (readonly != 0 && readonly != 1))
        return -EINVAL;

    control = open("/dev/loop-control", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (control < 0)
        return -errno;
    number = ioctl(control, LOOP_CTL_GET_FREE);
    if (number < 0) {
        int error = errno;
        close(control);
        return -error;
    }
    close(control);
    (void)snprintf(name, sizeof(name), "/dev/loop%d", number);
    loop = open(name, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (loop < 0)
        return -errno;
    if (fstat(loop, &info) != 0 || !S_ISBLK(info.st_mode) ||
        major(info.st_rdev) != 7u || minor(info.st_rdev) != (unsigned)number) {
        close(loop);
        return -ENODEV;
    }
    if (ioctl(loop, LOOP_CONFIGURE, &config) != 0) {
        int error = errno;
        close(loop);
        return -error;
    }
    return loop;
}

static int same_root(AstraStorageMounted *mounted)
{
    uint64_t id = 0u;
    int root = open(mounted->target, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int error;

    if (root < 0)
        return errno;
    error = mount_id(root, "", &id);
    if (error != 0 || id != mounted->linux_mount_id) {
        close(root);
        return error == 0 ? ESTALE : error;
    }
    mounted->root = root;
    return 0;
}

int astra_storage_linux_bind(AstraStorageDomain *domain,
                             const AstraStorageRecord *record, int root,
                             AstraStorageMounted *mounted)
{
    char source[64], name[32];
    struct stat info;
    int error;
    if (domain == NULL || domain->directory < 0 || record == NULL ||
        record->kind != ASTRA_VOLUME_BINDING || record->state != ASTRA_VOLUME_MOUNTING ||
        geteuid() != 0)
        return EINVAL;
    if (fstat(root, &info) != 0)
        return errno;
    if (!S_ISDIR(info.st_mode))
        return ENOTDIR;
    *mounted = (AstraStorageMounted){.id = record->id, .root = -1, .loop = -1, .backing = -1};
    (void)snprintf(name, sizeof(name), "%016" PRIx64, record->id);
    (void)snprintf(mounted->target, sizeof(mounted->target), "%s/%s", domain->path, name);
    (void)snprintf(source, sizeof(source), "/proc/self/fd/%d", root);
    if (mkdirat(domain->directory, name, 0700) != 0)
        return errno;
    mounted->directory = 1;
    if (mount(source, mounted->target, NULL, MS_BIND, NULL) != 0) {
        error = errno;
        if (unlinkat(domain->directory, name, AT_REMOVEDIR) == 0)
            mounted->directory = 0;
        return error;
    }
    mounted->attached = 1;
    error = mount_id(AT_FDCWD, mounted->target, &mounted->linux_mount_id);
    if (error != 0)
        return error;
    /* A distinct private mount ID makes received file descriptors provably
     * belong to this export, including retained unlinked objects. */
    if (mount(NULL, mounted->target, NULL, MS_BIND | MS_REMOUNT | MS_NODEV | MS_NOSUID |
              MS_NOEXEC | ((record->flags & ASTRA_VOLUME_READ_ONLY) ? MS_RDONLY : 0u), NULL) != 0)
        return errno;
    return same_root(mounted);
}

int astra_storage_linux_loop_release(int *loop)
{
    if (loop == NULL)
        return EINVAL;
    if (*loop >= 0) {
        struct loop_info64 info;
        if (ioctl(*loop, LOOP_CLR_FD) != 0 && errno != ENXIO)
            return errno;
        if (ioctl(*loop, LOOP_GET_STATUS64, &info) == 0)
            return EAGAIN;
        if (errno != ENXIO)
            return errno;
        close(*loop);
        *loop = -1;
    }
    return 0;
}

int astra_storage_linux_cleanup(AstraStorageMounted *mounted)
{
    if (mounted == NULL || mounted->id == 0u)
        return EINVAL;
    if (mounted->attached)
        return EBUSY;
    int detached = astra_storage_linux_loop_release(&mounted->loop);
    if (detached != 0)
        return detached;
    if (mounted->backing >= 0) {
        close(mounted->backing);
        mounted->backing = -1;
    }
    if (mounted->directory) {
        if (rmdir(mounted->target) != 0)
            return errno;
        mounted->directory = 0;
    }
    return 0;
}

int astra_storage_linux_detach(AstraStorageMounted *mounted, int writable, int lost)
{
    struct libmnt_context *ctx = NULL;
    int error;
    int status;

    if (mounted == NULL || mounted->id == 0u)
        return EINVAL;
    if (!mounted->attached)
        return EINVAL;
    if (writable) {
        int sync_fd = open(mounted->target, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        uint64_t id = 0u;
        if (sync_fd < 0)
            return errno;
        error = mount_id(sync_fd, "", &id);
        if (error == 0 && id != mounted->linux_mount_id)
            error = ESTALE;
        if (error == 0 && syncfs(sync_fd) != 0)
            error = errno;
        close(sync_fd);
        if (error != 0)
            return error;
    }
    if (!lost) {
        error = context(&ctx);
        if (error != 0)
            return error;
        if (mnt_context_set_target(ctx, mounted->target) != 0) {
            mnt_free_context(ctx);
            return EINVAL;
        }
    }
    if (mounted->root >= 0) {
        close(mounted->root);
        mounted->root = -1;
    }
    /* Once the catalog declares source loss, a network root may return
     * ESTALE even to libmount's lookup; detach the owned private mount. */
    status = lost ? umount2(mounted->target, MNT_DETACH) : mnt_context_umount(ctx);
    error = status == 0 ? 0 : lost ? errno : mnt_context_get_syscall_errno(ctx);
    if (status != 0 && error == 0)
        error = EIO;
    if (ctx != NULL)
        mnt_free_context(ctx);
    if (error != 0) {
        int restored = same_root(mounted);
        return restored == 0 ? error : restored;
    }
    mounted->linux_mount_id = 0u;
    mounted->attached = 0;
    return 0;
}

int astra_storage_linux_unmount(AstraStorageMounted *mounted, int writable)
{
    if (mounted == NULL || mounted->id == 0u)
        return EINVAL;
    int error = mounted->attached ? astra_storage_linux_detach(mounted, writable, 0) : 0;
    return error == 0 ? astra_storage_linux_cleanup(mounted) : error;
}

int astra_storage_linux_reconcile(AstraStorageDomain *domain,
                                  AstraStorageMounted *mounted)
{
    uint64_t parent = 0u, actual = 0u;
    if (domain == NULL || mounted == NULL || !mounted->directory)
        return EINVAL;
    int error = mount_id(domain->directory, "", &parent);
    if (error != 0)
        return error;
    error = mount_id(AT_FDCWD, mounted->target, &actual);
    if (error != 0)
        return error;
    if (mounted->root >= 0) {
        close(mounted->root);
        mounted->root = -1;
    }
    mounted->attached = actual != parent;
    mounted->linux_mount_id = mounted->attached ? actual : 0u;
    return mounted->attached ? same_root(mounted) : 0;
}

int astra_storage_linux_prepare(AstraStorageDomain *domain,
                              const AstraStorageRecord *record, int source,
                              uint64_t offset, uint64_t length,
                              AstraStorageMounted *mounted)
{
    struct stat info;
    char target_name[32];
    const char *type;
    bool readonly;
    int error;

    if (mounted != NULL)
        *mounted = (AstraStorageMounted){.id = record == NULL ? 0u : record->id,
            .root = -1, .loop = -1, .backing = -1};

    if (domain == NULL || record == NULL || mounted == NULL || geteuid() != 0 ||
        domain->directory < 0 || record->id == 0u ||
        record->kind != ASTRA_VOLUME_MOUNT || record->state != ASTRA_VOLUME_MOUNTING ||
        (type = fstype(record->format)) == NULL)
        return EINVAL;
    readonly = (record->flags & ASTRA_VOLUME_READ_ONLY) != 0u;
    if (astra_storage_format_read_only(record->format))
        if (!readonly)
            return EROFS;
    if (record->origin == ASTRA_VOLUME_ORIGIN_NETWORK) {
        if (source != -1 || offset != 0u || length != 0u ||
            (record->format != ASTRA_VOLUME_NFS && record->format != ASTRA_VOLUME_SMB))
            return EINVAL;
    } else if (record->format == ASTRA_VOLUME_TMPFS) {
        if (source != -1 || offset != 0u || length != 0u ||
            record->byte_limit == 0u || record->inode_limit == 0u)
            return EINVAL;
    } else {
        if (fstat(source, &info) != 0)
            return errno;
        if (S_ISREG(info.st_mode)) {
            if (info.st_size <= 0 || length == 0u || offset > (uint64_t)info.st_size ||
                length > (uint64_t)info.st_size - offset ||
                (record->origin == ASTRA_VOLUME_ORIGIN_IMAGE &&
                 record->backing_object == 0u) ||
                (record->origin != ASTRA_VOLUME_ORIGIN_IMAGE &&
                 (record->flags & ASTRA_VOLUME_SYSTEM) == 0u))
                return EINVAL;
            mounted->backing = fcntl(source, F_DUPFD_CLOEXEC, 0);
            if (mounted->backing < 0)
                return errno;
            mounted->loop = astra_storage_linux_loop(mounted->backing, offset, length, readonly);
            if (mounted->loop < 0) {
                error = -mounted->loop;
                mounted->loop = -1;
                close(mounted->backing);
                mounted->backing = -1;
                return error;
            }
        } else if (!S_ISBLK(info.st_mode) || offset != 0u || length != 0u) {
            return EINVAL;
        } else if (readonly && record->format == ASTRA_VOLUME_EXT4) {
            int block_readonly = 0;
            /* A read-only mount must not authorize ext4 journal replay writes. */
            if (ioctl(source, BLKROGET, &block_readonly) != 0)
                return errno;
            if (!block_readonly)
                return EROFS;
        }
    }
    (void)snprintf(target_name, sizeof(target_name), "%016" PRIx64, record->id);
    (void)snprintf(mounted->target, sizeof(mounted->target), "%s/%s", domain->path, target_name);
    if (mkdirat(domain->directory, target_name, 0700) != 0) {
        error = errno;
        goto failed;
    }
    mounted->directory = 1;
    return 0;
failed:
    if (mounted->loop >= 0 || mounted->backing >= 0 || mounted->directory)
        (void)astra_storage_linux_cleanup(mounted);
    return error;
}

static int authenticated_smb_mount(const char *source, const char *target,
    const char *address, uid_t uid, gid_t gid, int readonly,
    int secret, const AstraStorageMountAuth *auth)
{
    char password[256] = {0};
    char uid_text[24], gid_text[24];
    int fs = -1, tree = -1, error = 0;
    ssize_t length = pread(secret, password, sizeof(password), 0);
    if (length < 1 || password[length - 1] != '\0' ||
        memchr(password, '\0', (size_t)length - 1u) != NULL) {
        error = EINVAL;
        goto done;
    }
    if (snprintf(uid_text, sizeof(uid_text), "%u", (unsigned)uid) < 0 ||
        snprintf(gid_text, sizeof(gid_text), "%u", (unsigned)gid) < 0) {
        error = EINVAL;
        goto done;
    }
    fs = (int)syscall(SYS_fsopen, "cifs", FSOPEN_CLOEXEC);
    if (fs < 0) { error = errno; goto done; }
#define SET_STRING(key, value) do { \
    if (syscall(SYS_fsconfig, fs, FSCONFIG_SET_STRING, key, value, 0) != 0) { \
        error = errno; goto done; \
    } \
} while (0)
    SET_STRING("source", source);
    SET_STRING("vers", "3");
    SET_STRING("sec", "ntlmssp");
    SET_STRING("iocharset", "utf8");
    SET_STRING("cache", "none");
    SET_STRING("ip", address);
    SET_STRING("uid", uid_text);
    SET_STRING("gid", gid_text);
    SET_STRING("username", auth->account);
    if (auth->domain[0] != '\0') SET_STRING("domain", auth->domain);
    SET_STRING("password", password);
#undef SET_STRING
    astra_secret_wipe(password, sizeof(password));
    /* Remote failure must return an error, not strand QEMU in CIFS writeback. */
    if (syscall(SYS_fsconfig, fs, FSCONFIG_SET_FLAG, "soft", NULL, 0) != 0 ||
        syscall(SYS_fsconfig, fs, FSCONFIG_CMD_CREATE, NULL, NULL, 0) != 0) {
        error = errno;
        goto done;
    }
    tree = (int)syscall(SYS_fsmount, fs, FSMOUNT_CLOEXEC,
        MOUNT_ATTR_NODEV | MOUNT_ATTR_NOSUID | MOUNT_ATTR_NOEXEC |
        (readonly ? MOUNT_ATTR_RDONLY : 0u));
    if (tree < 0 || syscall(SYS_move_mount, tree, "", AT_FDCWD, target,
                            MOVE_MOUNT_F_EMPTY_PATH) != 0)
        error = errno;
done:
    astra_secret_wipe(password, sizeof(password));
    if (fs >= 0) close(fs);
    if (tree >= 0) close(tree);
    return error;
}

int astra_storage_linux_attach(AstraStorageDomain *domain,
                               const AstraStorageRecord *record, int source,
                               AstraStorageMounted *mounted, int secret,
                               const AstraStorageMountAuth *auth)
{
    struct libmnt_context *ctx = NULL;
    char source_name[sizeof(mounted->network.server) +
                     sizeof(mounted->network.share) + 4u] = "tmpfs";
    char options[256] = "";
    char address[INET6_ADDRSTRLEN] = {0};
    const char *type;
    uint64_t domain_mount = 0u;
    bool readonly;
    int error;
    int status;
    if (domain == NULL || record == NULL || mounted == NULL || geteuid() != 0 ||
        (type = fstype(record->format)) == NULL || mounted->id != record->id ||
        !mounted->directory || mounted->attached ||
        (secret >= 0) != (auth != NULL) ||
        (auth != NULL && (record->origin != ASTRA_VOLUME_ORIGIN_NETWORK ||
                          record->format != ASTRA_VOLUME_SMB)))
        return EINVAL;
    readonly = (record->flags & ASTRA_VOLUME_READ_ONLY) != 0u;
    if (record->origin == ASTRA_VOLUME_ORIGIN_NETWORK) {
        if ((record->format != ASTRA_VOLUME_NFS && record->format != ASTRA_VOLUME_SMB) ||
            mounted->network.format != record->format)
            return ENOTSUP;
        struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
        struct addrinfo *addresses = NULL;
        int resolved = getaddrinfo(mounted->network.server, NULL, &hints, &addresses);
        if (resolved != 0) return resolved == EAI_MEMORY ? ENOMEM : EHOSTUNREACH;
        for (const struct addrinfo *at = addresses; at != NULL; at = at->ai_next) {
            const void *bytes = at->ai_family == AF_INET ?
                (const void *)&((const struct sockaddr_in *)at->ai_addr)->sin_addr :
                at->ai_family == AF_INET6 ?
                (const void *)&((const struct sockaddr_in6 *)at->ai_addr)->sin6_addr : NULL;
            if (bytes != NULL && inet_ntop(at->ai_family, bytes, address,
                                           sizeof(address)) != NULL) break;
        }
        freeaddrinfo(addresses);
        if (address[0] == '\0') return EHOSTUNREACH;
        int printed;
        if (record->format == ASTRA_VOLUME_NFS)
            printed = snprintf(source_name, sizeof(source_name),
                strchr(mounted->network.server, ':') != NULL ? "[%s]:%s" : "%s:%s",
                mounted->network.server, mounted->network.share);
        else
            printed = snprintf(source_name, sizeof(source_name), "//%s%s",
                mounted->network.server, mounted->network.share);
        if (printed < 0 || (size_t)printed >= sizeof(source_name)) return ENAMETOOLONG;
        if (record->format == ASTRA_VOLUME_NFS)
            printed = snprintf(options, sizeof(options),
                "vers=4,proto=tcp,sec=sys,addr=%s", address);
        else if (auth == NULL)
            /* Anonymous shares have no credential material to retain or leak. */
            printed = snprintf(options, sizeof(options),
                "vers=3,sec=none,soft,cache=none,iocharset=utf8,ip=%s,uid=%u,gid=%u", address,
                (unsigned)domain->uid, (unsigned)domain->gid);
        if (printed < 0 || (size_t)printed >= sizeof(options)) return ENAMETOOLONG;
    } else if (record->format == ASTRA_VOLUME_TMPFS)
        (void)snprintf(options, sizeof(options),
            "size=%" PRIu64 ",nr_inodes=%" PRIu64 ",noswap,uid=%u,gid=%u,mode=0770",
            record->byte_limit, record->inode_limit, (unsigned)domain->uid,
            (unsigned)domain->gid);
    else {
        (void)snprintf(source_name, sizeof(source_name), "/proc/self/fd/%d",
            mounted->loop >= 0 ? mounted->loop : source);
        if (record->format == ASTRA_VOLUME_ISO9660)
            (void)snprintf(options, sizeof(options), "utf8");
        if (record->format == ASTRA_VOLUME_UDF)
            (void)snprintf(options, sizeof(options), "iocharset=utf8");
        if (record->format == ASTRA_VOLUME_FAT12 ||
            record->format == ASTRA_VOLUME_FAT16 ||
            record->format == ASTRA_VOLUME_FAT32 || record->format == ASTRA_VOLUME_EXFAT)
            (void)snprintf(options, sizeof(options), "uid=%u,gid=%u,%s",
                (unsigned)domain->uid, (unsigned)domain->gid,
                record->format == ASTRA_VOLUME_EXFAT ? "iocharset=utf8" : "utf8=1");
    }
    error = mount_id(domain->directory, "", &domain_mount);
    if (error != 0)
        goto failed;
    if (auth != NULL) {
        error = authenticated_smb_mount(source_name, mounted->target, address,
            domain->uid, domain->gid, readonly, secret, auth);
        mounted->attached = error == 0;
        if (error != 0) goto failed;
        goto verify;
    }
    error = context(&ctx);
    if (error != 0)
        goto failed;
    if (mnt_context_set_source(ctx, source_name) != 0 ||
        mnt_context_set_target(ctx, mounted->target) != 0 ||
        mnt_context_set_fstype(ctx, type) != 0 ||
        mnt_context_set_options(ctx, options) != 0 ||
        mnt_context_set_mflags(ctx, MS_NODEV | MS_NOSUID | MS_NOEXEC |
                                 (readonly ? MS_RDONLY : 0u)) != 0) {
        error = EINVAL;
        goto failed;
    }
    status = mnt_context_mount(ctx);
    mounted->attached = mnt_context_syscall_called(ctx) &&
        mnt_context_get_syscall_errno(ctx) == 0;
    error = status == 0 ? 0 : mnt_context_get_syscall_errno(ctx);
    if (status != 0 && error == 0)
        error = EIO;
    mnt_free_context(ctx);
    ctx = NULL;
    if (!mounted->attached)
        goto failed;
verify:
    /* Query by the protected target without allocating an fd first. Root-open
     * exhaustion must not erase evidence of a successful Linux mount. */
    status = mount_id(AT_FDCWD, mounted->target, &mounted->linux_mount_id);
    if (status != 0 || mounted->linux_mount_id == domain_mount)
        return status == 0 ? EPROTO : status;
    if (error != 0)
        return error;
    mounted->root = open(mounted->target, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (mounted->root < 0)
        return errno; /* Retain backing/target for rollback; never publish. */
    return 0;

failed:
    if (ctx != NULL)
        mnt_free_context(ctx);
    return error;
}

int astra_storage_linux_mount(AstraStorageDomain *domain,
                              const AstraStorageRecord *record, int source,
                              uint64_t offset, uint64_t length,
                              AstraStorageMounted *mounted)
{
    int error = astra_storage_linux_prepare(domain, record, source, offset, length, mounted);
    if (error == 0)
        error = astra_storage_linux_attach(domain, record, source, mounted, -1, NULL);
    if (error != 0 && mounted != NULL && !mounted->attached &&
        (mounted->loop >= 0 || mounted->backing >= 0 || mounted->directory))
        (void)astra_storage_linux_cleanup(mounted);
    return error;
}
