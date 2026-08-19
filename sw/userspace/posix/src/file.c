/*
 * Files, directories and a current directory, over the VFS.
 *
 * This is the half of the POSIX layer that costs something: opening it brings
 * the filesystem library, a VFS client and a transfer area with it, which is
 * why nothing here is reachable from `write()` except through the vector the
 * descriptor table calls. A program that only prints links none of it.
 *
 * **A current directory is an assign plus a path.** The machine has no root:
 * every path is ASSIGN:path, and a bare name means nothing until something
 * says what it is relative to. The launcher says so -- the shell grants CWD:,
 * rooted at wherever the prompt is standing -- so that is what this starts
 * from, and `chdir` moves within it. `getcwd` answers in the machine's own
 * terms rather than inventing a slash-rooted string that names nothing.
 *
 * What a ported program gets out of that: `fopen("notes.txt", ...)` opens the
 * file beside it, the same as anywhere else, without this layer pretending the
 * filesystem has a shape it does not have.
 */

#include <astra/posix.h>
#include <astra/posix_descriptor.h>

#include <astra/vfs_process.h>
#include <astra/runtime.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    /*
     * Eight open files. The VFS client has its own ceiling and it is lower
     * than the descriptor table's sixteen, so this is where a program finds
     * out -- with EMFILE, which is the honest answer.
     */
    POSIX_FILE_MAX = 8,
    /* Enough for a program walking a tree; see the pool below for why. */
    POSIX_DIRECTORY_MAX = 4,
};

static AstraProcessFilesystem filesystem;
static AstraFile files[POSIX_FILE_MAX];
static uint8_t used[POSIX_FILE_MAX];
static char cwd_assign[ASTRA_CAPABILITY_NAME_MAX];
static char cwd_directory[ASTRA_VFS_PATH_MAX];
static int started;

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
    const AstraAssign *cwd;
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
    cwd = filesystem.library->assign_lookup(astra_process_vfs_assigns(),
                                            "CWD");
    (void)strcpy(cwd_assign, cwd != NULL ? "CWD" : "WORK");
    cwd_directory[0] = '\0';
    astra_posix_file_bind(&ops);
    started = 1;
    return 1;
}

/* A typed path against the current directory; an ASSIGN:path is left alone. */
static int
resolve(const char *path, char *out, uint32_t capacity)
{
    uint32_t status;

    if (path == NULL) {
        errno = EFAULT;
        return 0;
    }
    if (!start())
        return 0;
    status = filesystem.library->qualify(cwd_assign, cwd_directory, path, out,
                                         capacity);
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        return 0;
    }
    return 1;
}

static int
claim(void)
{
    for (int slot = 0; slot < POSIX_FILE_MAX; ++slot)
        if (used[slot] == 0u)
            return slot;
    errno = EMFILE;
    return -1;
}

static ssize_t
file_read(uint32_t slot, void *bytes, size_t length)
{
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= POSIX_FILE_MAX || used[slot] == 0u) {
        errno = EBADF;
        return -1;
    }
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    status = filesystem.library->read(&files[slot], bytes, (uint32_t)length,
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

    if (slot >= POSIX_FILE_MAX || used[slot] == 0u) {
        errno = EBADF;
        return -1;
    }
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    status = filesystem.library->write(&files[slot], bytes, (uint32_t)length,
                                       &moved);
    if (status != ASTRA_VFS_OK && moved == 0u)
        return fail(status);
    return (ssize_t)moved;
}

static int
file_close(uint32_t slot)
{
    uint32_t status;

    if (slot >= POSIX_FILE_MAX || used[slot] == 0u) {
        errno = EBADF;
        return -1;
    }
    status = filesystem.library->close(&files[slot]);
    used[slot] = 0u;
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

static off_t
file_seek(uint32_t slot, off_t offset, int whence)
{
    uint64_t result = 0u;
    uint32_t origin;
    uint32_t status;

    if (slot >= POSIX_FILE_MAX || used[slot] == 0u) {
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
    status = filesystem.library->seek(&files[slot], (int64_t)offset, origin,
                                      &result);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    return (off_t)result;
}

int
open(const char *path, int flags, ...)
{
    char typed[ASTRA_VFS_PATH_MAX];
    uint32_t wanted = 0u;
    uint32_t status;
    int slot;
    int fd;

    if (!resolve(path, typed, sizeof(typed)))
        return -1;
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
    /*
     * O_EXCL asks a question the protocol has no flag for, so it is asked
     * separately: does this name already exist. Two round trips, on the one
     * path that wanted the guarantee, rather than a flag every open pays for.
     * It is not atomic against another process creating the same name between
     * the two -- and saying so here is better than a comment that implies it
     * is.
     */
    if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
        AstraFileInfo probe = ASTRA_FILE_INFO_INIT;

        if (filesystem.library->stat(&filesystem.filesystem, typed,
                                     &probe) == ASTRA_VFS_OK) {
            errno = EEXIST;
            return -1;
        }
    }
    slot = claim();
    if (slot < 0)
        return -1;
    files[slot] = (AstraFile)ASTRA_FILE_INIT;
    status = filesystem.library->open(&filesystem.filesystem, typed, wanted,
                                      &files[slot]);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    used[slot] = 1u;
    /* No append flag on the wire: it is a seek to the end, once, at open. */
    if ((flags & O_APPEND) != 0) {
        uint64_t end = 0u;

        (void)filesystem.library->seek(&files[slot], 0, ASTRA_FILE_SEEK_END,
                                       &end);
    }
    fd = astra_posix_descriptor_file((uint32_t)slot);
    if (fd < 0) {
        (void)file_close((uint32_t)slot);
        errno = EMFILE;
        return -1;
    }
    return fd;
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
    char typed[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!resolve(path, typed, sizeof(typed)))
        return -1;
    status = filesystem.library->stat(&filesystem.filesystem, typed, &info);
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
    if (slot < 0 || (uint32_t)slot >= POSIX_FILE_MAX ||
        used[slot] == 0u) {
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
    status = filesystem.library->file_info(&files[slot], &info);
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
    char typed[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    (void)mode;
    if (!resolve(path, typed, sizeof(typed)))
        return -1;
    status = filesystem.library->mkdir(&filesystem.filesystem, typed);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
unlink(const char *path)
{
    char typed[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!resolve(path, typed, sizeof(typed)))
        return -1;
    status = filesystem.library->unlink(&filesystem.filesystem, typed);
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
    char typed[ASTRA_VFS_PATH_MAX];
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char rest[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!resolve(path, typed, sizeof(typed)))
        return -1;
    status = filesystem.library->stat(&filesystem.filesystem, typed, &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }
    /*
     * Adopted only after it is known to be a directory, and taken apart by the
     * same parser that resolved it -- so `..` has already been folded away and
     * what is stored is a path the next `qualify` can build on.
     */
    if (filesystem.library->path_split(typed, name, sizeof(name), rest,
                                       sizeof(rest)) != ASTRA_VFS_OK ||
        filesystem.library->path_normalise(rest, cwd_directory,
                                           sizeof(cwd_directory)) !=
            ASTRA_VFS_OK) {
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)memcpy(cwd_assign, name, sizeof(cwd_assign));
    return 0;
}

/*
 * The current directory, in the machine's own terms: ASSIGN:path.
 *
 * Not a slash-rooted string. There is no root to root it at, and a program
 * that took `/work/notes` from here and handed it back to `open` would be
 * naming something that does not exist. What comes out of this goes back in.
 */
char *
getcwd(char *buffer, size_t size)
{
    size_t at = 0u;
    size_t index;

    if (buffer == NULL || size == 0u) {
        errno = EINVAL;
        return NULL;
    }
    if (!start())
        return NULL;
    for (index = 0u; cwd_assign[index] != '\0'; ++index) {
        if (at + 2u >= size) {
            errno = ERANGE;
            return NULL;
        }
        buffer[at++] = cwd_assign[index];
    }
    buffer[at++] = ':';
    for (index = 0u; cwd_directory[index] != '\0'; ++index) {
        if (at + 1u >= size) {
            errno = ERANGE;
            return NULL;
        }
        buffer[at++] = cwd_directory[index];
    }
    buffer[at] = '\0';
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
/*
 * Four at once, out of a static pool.
 *
 * `opendir` returns a pointer and the obvious source of one is `malloc` --
 * which this machine has no heap for yet. picolibc's allocator wants an `sbrk`
 * over something, and what that something should be on Astra is an area with a
 * ceiling somebody chose, not a decision made accidentally by the first
 * function that needed a pointer. So this does without, and the day a ported
 * program needs a real heap is the day that gets designed.
 */
typedef struct PosixDir {
    AstraDirectory directory;
    AstraDirectoryEntry batch[2];
    uint32_t count;
    uint32_t next;
} PosixDir;

_Static_assert(sizeof(PosixDir) <= sizeof(((DIR *)0)->buf),
               "the Astra directory handle and its batch must fit in DIR");

static DIR directories[POSIX_DIRECTORY_MAX];
static uint8_t directories_used[POSIX_DIRECTORY_MAX];

DIR *
opendir(const char *path)
{
    char typed[ASTRA_VFS_PATH_MAX];
    PosixDir *state;
    DIR *dir = NULL;
    uint32_t index;
    uint32_t status;

    if (!resolve(path, typed, sizeof(typed)))
        return NULL;
    for (index = 0u; index < POSIX_DIRECTORY_MAX; ++index) {
        if (directories_used[index] == 0u) {
            dir = &directories[index];
            break;
        }
    }
    if (dir == NULL) {
        errno = EMFILE;
        return NULL;
    }
    (void)memset(dir, 0, sizeof(*dir));
    dir->fd = -1;
    state = (PosixDir *)(void *)dir->buf;
    status = filesystem.library->directory_open(&filesystem.filesystem, typed,
                                                &state->directory);
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        return NULL;
    }
    directories_used[index] = 1u;
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
    filesystem.library->directory_close(&state->directory);
    for (uint32_t index = 0u; index < POSIX_DIRECTORY_MAX; ++index)
        if (&directories[index] == dir)
            directories_used[index] = 0u;
    return 0;
}
