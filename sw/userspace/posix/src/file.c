/*
 * Files, directories and a current directory, over the VFS.
 *
 * This is the half of the POSIX layer that costs something: opening it brings
 * the filesystem library, a VFS client and a transfer area with it, which is
 * why nothing here is reachable from `write()` except through the vector the
 * descriptor table calls. A program that only prints links none of it.
 *
 * Astra authority still comes from assigns. POSIX programs see those assigns
 * as the first level below a synthetic slash root: WORK:src/main.c is
 * /WORK/src/main.c, and `/` lists only the names the process was granted.
 * Native ASSIGN:path spelling remains accepted at this boundary.
 */

#include <astra/posix.h>
#include <astra/posix_descriptor.h>

#include <astra/vfs_process.h>
#include <astra/runtime.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "path.h"

static AstraProcessFilesystem filesystem;
typedef struct PosixFileSlot {
    AstraFile file;
    uint8_t used;
} PosixFileSlot;

static PosixFileSlot *file_slots;
static uint32_t file_capacity;
static char cwd[ASTRA_VFS_PATH_MAX];
static int started;

typedef struct PosixPath {
    char normal[ASTRA_VFS_PATH_MAX];
    char native[ASTRA_VFS_PATH_MAX];
    int root;
} PosixPath;

/*
 * A protocol status as an errno.
 *
 * Not a lookup table: the mapping is the point, and a `switch` says which Unix
 * word each Astra one was judged to mean. Anything unmapped is EIO rather than
 * a plausible-looking guess -- a program that retries on the wrong errno does
 * something worse than one that gives up.
 */
static int
posix_errno(uint32_t status)
{
    switch (status) {
    case ASTRA_VFS_OK:            return 0;
    case ASTRA_VFS_ERR_NOT_FOUND: return ENOENT;
    case ASTRA_VFS_ERR_EXISTS:    return EEXIST;
    case ASTRA_VFS_ERR_NOT_DIR:   return ENOTDIR;
    case ASTRA_VFS_ERR_IS_DIR:    return EISDIR;
    case ASTRA_VFS_ERR_ACCESS:    return EACCES;
    case ASTRA_VFS_ERR_NO_SPACE:  return ENOSPC;
    case ASTRA_VFS_ERR_INVALID:   return EINVAL;
    case ASTRA_VFS_ERR_BAD_HANDLE: return EBADF;
    case ASTRA_VFS_ERR_LIMIT:     return EMFILE;
    case ASTRA_VFS_ERR_NOT_EMPTY: return ENOTEMPTY;
    case ASTRA_VFS_ERR_UNSUPPORTED: return ENOSYS;
    case ASTRA_VFS_ERR_BUSY:      return EBUSY;
    case ASTRA_VFS_ERR_BUFFER_TOO_SMALL: return ERANGE;
    case ASTRA_VFS_ERR_CROSS_DEVICE: return EXDEV;
    default:                      return EIO;
    }
}

static int
fail(uint32_t status)
{
    errno = posix_errno(status);
    return -1;
}

static ssize_t
file_read(uint32_t slot, void *bytes, size_t length);
static ssize_t
file_write(uint32_t slot, const void *bytes, size_t length);
static int
file_close(uint32_t slot);
static off_t
file_seek(uint32_t slot, off_t offset, int whence);

static const AstraPosixFileOps ops = {
    file_read, file_write, file_close, file_seek
};

/*
 * Opens the namespace once, on the first call that needs it.
 *
 * Not at startup: `astra_posix_start` runs in every program that prints, and
 * most of them never touch a file. The cost lands on the first one that does.
 */
static int
start(void)
{
    const AstraAssignTable *assigns;
    uint32_t status;

    if (started)
        return 1;
    status = astra_process_filesystem_open(&filesystem,
                                           astra_posix_startup());
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        return 0;
    }
    /*
     * CWD: if the launcher said where it was standing, and WORK: if it did
     * not. A program launched by something with no notion of a place still
     * has to resolve a bare name against something, and a person's own files
     * are the least surprising answer.
     */
    assigns = astra_process_vfs_assigns();
    (void)strcpy(cwd,
                 filesystem.library->assign_lookup(assigns, "CWD") != NULL ?
                     "/CWD" :
                 filesystem.library->assign_lookup(assigns, "WORK") != NULL ?
                     "/WORK" : "/");
    astra_posix_file_bind(&ops);
    started = 1;
    return 1;
}

/* One namespace conversion shared by every file and directory operation. */
static int
resolve(const char *path, PosixPath *out)
{
    int result;

    if (path == NULL) {
        errno = EFAULT;
        return 0;
    }
    if (!start())
        return 0;
    result = astra_posix_path_resolve(cwd, path, out->normal,
                                      sizeof(out->normal), out->native,
                                      sizeof(out->native));
    if (result < 0) {
        errno = ENAMETOOLONG;
        return 0;
    }
    out->root = result == 0;
    return 1;
}

static int
claim(void)
{
    PosixFileSlot *grown;
    uint32_t capacity;
    uint32_t slot;

    for (uint32_t slot = 0u; slot < file_capacity; ++slot)
        if (file_slots[slot].used == 0u)
            return (int)slot;
    if (file_capacity > (uint32_t)INT_MAX / 2u) {
        errno = EMFILE;
        return -1;
    }
    slot = file_capacity;
    capacity = file_capacity == 0u ? 4u : file_capacity * 2u;
    if ((size_t)capacity > SIZE_MAX / sizeof(*grown)) {
        errno = ENOMEM;
        return -1;
    }
    grown = realloc(file_slots, (size_t)capacity * sizeof(*grown));
    if (grown == NULL) {
        errno = ENOMEM;
        return -1;
    }
    (void)memset(grown + file_capacity, 0,
                 (size_t)(capacity - file_capacity) * sizeof(*grown));
    file_slots = grown;
    file_capacity = capacity;
    return (int)slot;
}

static ssize_t
file_read(uint32_t slot, void *bytes, size_t length)
{
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    status = filesystem.library->read(&file_slots[slot].file, bytes,
                                      (uint32_t)length,
                                      &moved);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    /* A short read is normal -- one message carries a bounded payload -- and
     * zero is the end of the file, which is what a POSIX reader wants. */
    return (ssize_t)moved;
}

static ssize_t
file_write(uint32_t slot, const void *bytes, size_t length)
{
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    status = filesystem.library->write(&file_slots[slot].file, bytes,
                                       (uint32_t)length,
                                       &moved);
    if (status != ASTRA_VFS_OK && moved == 0u)
        return fail(status);
    return (ssize_t)moved;
}

static int
file_close(uint32_t slot)
{
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    status = filesystem.library->close(&file_slots[slot].file);
    file_slots[slot].used = 0u;
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

static off_t
file_seek(uint32_t slot, off_t offset, int whence)
{
    uint64_t result = 0u;
    uint32_t origin;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    switch (whence) {
    case SEEK_SET: origin = ASTRA_FILE_SEEK_BEGIN; break;
    case SEEK_CUR: origin = ASTRA_FILE_SEEK_CURRENT; break;
    case SEEK_END: origin = ASTRA_FILE_SEEK_END; break;
    default:
        errno = EINVAL;
        return -1;
    }
    status = filesystem.library->seek(&file_slots[slot].file,
                                      (int64_t)offset, origin,
                                      &result);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    return (off_t)result;
}

int
open(const char *path, int flags, ...)
{
    PosixPath resolved;
    uint32_t wanted = 0u;
    uint32_t status;
    int slot;
    int fd;

    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EISDIR;
        return -1;
    }
    switch (flags & O_ACCMODE) {
    case O_RDONLY: wanted = ASTRA_VFS_OPEN_READ; break;
    case O_WRONLY: wanted = ASTRA_VFS_OPEN_WRITE; break;
    case O_RDWR:   wanted = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE; break;
    default:
        errno = EINVAL;
        return -1;
    }
    if ((flags & O_CREAT) != 0)
        wanted |= ASTRA_VFS_OPEN_CREATE;
    if ((flags & O_TRUNC) != 0)
        wanted |= ASTRA_VFS_OPEN_TRUNCATE;
    if ((flags & O_EXCL) != 0)
        wanted |= ASTRA_VFS_OPEN_EXCLUSIVE;
    if ((flags & O_APPEND) != 0)
        wanted |= ASTRA_VFS_OPEN_APPEND;
    slot = claim();
    if (slot < 0)
        return -1;
    file_slots[slot].file = (AstraFile)ASTRA_FILE_INIT;
    status = filesystem.library->open(&filesystem.filesystem, resolved.native,
                                      wanted, &file_slots[slot].file);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    file_slots[slot].used = 1u;
    fd = astra_posix_descriptor_file((uint32_t)slot);
    if (fd < 0) {
        (void)file_close((uint32_t)slot);
        errno = EMFILE;
        return -1;
    }
    return fd;
}

int
fsync(int fd)
{
    int slot = astra_posix_descriptor_slot(fd);
    uint32_t status;

    if (slot < 0 || (uint32_t)slot >= file_capacity ||
        file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (filesystem.library->abi_minor < 2u ||
        filesystem.library->structure_size < sizeof(*filesystem.library)) {
        errno = ENOSYS;
        return -1;
    }
    status = filesystem.library->sync(&file_slots[slot].file);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
ftruncate(int fd, off_t length)
{
    int slot = astra_posix_descriptor_slot(fd);
    uint32_t status;

    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    if (slot < 0 || (uint32_t)slot >= file_capacity ||
        file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (filesystem.library->abi_minor < 2u ||
        filesystem.library->structure_size < sizeof(*filesystem.library)) {
        errno = ENOSYS;
        return -1;
    }
    status = filesystem.library->truncate(&file_slots[slot].file,
                                          (uint64_t)length);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

/*
 * A stat, from what the protocol carries.
 *
 * `mode` of zero means the filesystem does not carry permission bits, not that
 * a file has none -- `ls` prints `?????????` for exactly that reason. POSIX has
 * no way to say it: `st_mode` is a number and every program will read it as
 * one. So a filesystem that does not answer gets the conventional default
 * here, and the honest version stays where it can be told: `ls -l`.
 */
static void
fill(struct stat *out, const AstraFileInfo *info)
{
    int directory = info->kind == ASTRA_VFS_KIND_DIRECTORY;

    (void)memset(out, 0, sizeof(*out));
    out->st_mode = (mode_t)((directory ? S_IFDIR : S_IFREG) |
                            (info->mode != 0u ? info->mode :
                             (directory ? 0755u : 0644u)));
    out->st_size = (off_t)info->byte_size;
    out->st_nlink = (nlink_t)(info->nlink != 0u ? info->nlink : 1u);
    out->st_uid = (uid_t)info->uid;
    out->st_gid = (gid_t)info->gid;
    out->st_mtim.tv_sec = (time_t)info->mtime;
    out->st_atim = out->st_mtim;
    out->st_ctim = out->st_mtim;
    out->st_blksize = 512;
    out->st_blocks = (blkcnt_t)((info->byte_size + 511u) / 512u);
}

int
stat(const char *path, struct stat *out)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    PosixPath resolved;
    uint32_t status;

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        (void)memset(out, 0, sizeof(*out));
        out->st_mode = S_IFDIR | 0555;
        out->st_nlink = 1;
        out->st_blksize = 512;
        return 0;
    }
    status = filesystem.library->stat(&filesystem.filesystem, resolved.native,
                                      &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill(out, &info);
    return 0;
}

int
fstat(int fd, struct stat *out)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;
    int slot = astra_posix_descriptor_slot(fd);

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (slot < 0 || (uint32_t)slot >= file_capacity ||
        file_slots[slot].used == 0u) {
        /*
         * Not a file. stdio calls this on its own three descriptors to decide
         * how to buffer, and a character device is what a stream is.
         */
        (void)memset(out, 0, sizeof(*out));
        out->st_mode = S_IFCHR | 0620;
        out->st_nlink = 1;
        out->st_blksize = 512;
        return 0;
    }
    status = filesystem.library->file_info(&file_slots[slot].file, &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill(out, &info);
    return 0;
}

int
access(const char *path, int mode)
{
    struct stat about;

    (void)mode;
    /*
     * Existence, and nothing more. The rights a process holds are on the
     * assign it holds them through, not on the file, so "may I write this"
     * cannot be answered by looking at the node -- and answering it wrongly is
     * worse than making the caller try the open and find out.
     */
    return stat(path, &about);
}

int
mkdir(const char *path, mode_t mode)
{
    PosixPath resolved;
    uint32_t status;

    (void)mode;
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EEXIST;
        return -1;
    }
    status = filesystem.library->mkdir(&filesystem.filesystem,
                                       resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
unlink(const char *path)
{
    PosixPath resolved;
    uint32_t status;

    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EISDIR;
        return -1;
    }
    status = filesystem.library->unlink(&filesystem.filesystem,
                                        resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
rename(const char *from, const char *to)
{
    PosixPath from_resolved;
    PosixPath to_resolved;
    uint32_t status;

    if (!resolve(from, &from_resolved) || !resolve(to, &to_resolved))
        return -1;
    if (from_resolved.root || to_resolved.root) {
        errno = EBUSY;
        return -1;
    }
    if (filesystem.library->abi_minor < 1u ||
        filesystem.library->structure_size < sizeof(*filesystem.library)) {
        errno = ENOSYS;
        return -1;
    }
    status = filesystem.library->rename(&filesystem.filesystem,
                                        from_resolved.native,
                                        to_resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

/*
 * One removal, and the filesystem decides whether the name was the right kind
 * for it. Refusing a directory here on POSIX's behalf would be this layer
 * inventing a rule and getting it wrong for a filesystem that allows it.
 */
int
rmdir(const char *path)
{
    return unlink(path);
}

int
chdir(const char *path)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    PosixPath resolved;
    uint32_t status;

    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        (void)strcpy(cwd, "/");
        return 0;
    }
    status = filesystem.library->stat(&filesystem.filesystem, resolved.native,
                                      &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }
    (void)strcpy(cwd, resolved.normal);
    return 0;
}

/*
 * The current directory in the slash namespace accepted above. Its first
 * component is still an assign, so the path remains capability-scoped.
 */
char *
getcwd(char *buffer, size_t size)
{
    size_t length;

    if (buffer == NULL || size == 0u) {
        errno = EINVAL;
        return NULL;
    }
    if (!start())
        return NULL;
    length = strlen(cwd) + 1u;
    if (length > size) {
        errno = ERANGE;
        return NULL;
    }
    (void)memcpy(buffer, cwd, length);
    return buffer;
}

/*
 * Directories.
 *
 * picolibc declares the dirent API and implements none of it, so `DIR` is a
 * layout to fill rather than a contract to meet. The Astra directory handle
 * and a small batch of entries live in its buffer: a listing is a round trip
 * per batch, about 7.5 ms here, and reading one name at a time would make a
 * forty-name directory cost a third of a second in address-space switches.
 * That is the same arithmetic that put the metadata on the directory entry in
 * protocol version 6.
 */
typedef struct PosixDir {
    AstraDirectory directory;
    AstraDirectoryEntry batch[2];
    uint32_t count;
    uint32_t next;
    uint32_t root_index;
    uint8_t root;
} PosixDir;

_Static_assert(sizeof(PosixDir) <= sizeof(((DIR *)0)->buf),
               "the Astra directory handle and its batch must fit in DIR");

DIR *
opendir(const char *path)
{
    PosixPath resolved;
    PosixDir *state;
    DIR *dir;
    uint32_t status;

    if (!resolve(path, &resolved))
        return NULL;
    dir = calloc(1u, sizeof(*dir));
    if (dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    dir->fd = -1;
    state = (PosixDir *)(void *)dir->buf;
    if (resolved.root) {
        state->root = 1u;
        return dir;
    }
    status = filesystem.library->directory_open(&filesystem.filesystem,
                                                 resolved.native,
                                                 &state->directory);
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        free(dir);
        return NULL;
    }
    return dir;
}

struct dirent *
readdir(DIR *dir)
{
    PosixDir *state;
    const AstraDirectoryEntry *entry;
    uint32_t index;

    if (dir == NULL) {
        errno = EBADF;
        return NULL;
    }
    state = (PosixDir *)(void *)dir->buf;
    if (state->root != 0u) {
        const AstraAssignTable *assigns = astra_process_vfs_assigns();

        while (state->root_index < assigns->count) {
            const AstraAssign *assign = &assigns->entries[state->root_index];
            int duplicate = 0;

            for (index = 0u; index < state->root_index; ++index)
                if (strcmp(assigns->entries[index].name, assign->name) == 0) {
                    duplicate = 1;
                    break;
                }
            ++state->root_index;
            if (duplicate)
                continue;
            (void)memset(&dir->dirent, 0, sizeof(dir->dirent));
            dir->dirent.d_type = DT_DIR;
            (void)strncpy(dir->dirent.d_name, assign->name,
                          sizeof(dir->dirent.d_name) - 1u);
            return &dir->dirent;
        }
        return NULL;
    }
    if (state->next >= state->count) {
        uint32_t status = filesystem.library->directory_read(
            &state->directory, state->batch,
            (uint32_t)(sizeof(state->batch) / sizeof(state->batch[0])),
            &state->count);

        state->next = 0u;
        if (status != ASTRA_VFS_OK) {
            errno = posix_errno(status);
            return NULL;
        }
        /* No error and nothing left is the end, which readdir says as NULL. */
        if (state->count == 0u)
            return NULL;
    }
    entry = &state->batch[state->next++];
    (void)memset(&dir->dirent, 0, sizeof(dir->dirent));
    dir->dirent.d_type = entry->kind == ASTRA_VFS_KIND_DIRECTORY ? DT_DIR :
                                                                   DT_REG;
    for (index = 0u; index + 1u < sizeof(dir->dirent.d_name) &&
                     entry->name[index] != '\0'; ++index)
        dir->dirent.d_name[index] = entry->name[index];
    dir->dirent.d_name[index] = '\0';
    return &dir->dirent;
}

int
closedir(DIR *dir)
{
    PosixDir *state;

    if (dir == NULL) {
        errno = EBADF;
        return -1;
    }
    state = (PosixDir *)(void *)dir->buf;
    if (state->root == 0u)
        filesystem.library->directory_close(&state->directory);
    free(dir);
    return 0;
}
