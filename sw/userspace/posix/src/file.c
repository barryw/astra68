#define _POSIX_C_SOURCE 200809L

/*
 * Files, directories and a current directory, over the VFS.
 *
 * This is the half of the POSIX layer that costs something: opening it brings
 * the filesystem library, a VFS client and a transfer area with it, which is
 * why nothing here is reachable from `write()` except through the vector the
 * descriptor table calls. A program that only prints links none of it.
 *
 * Astra authority still comes from assigns. POSIX programs use the Filesystem
 * Kit's root directory, which lists only names the process was granted.
 */

#include <astra/posix.h>
#include <astra/posix_descriptor.h>

#include <astra/vfs_process.h>
#include <astra/vfs_reader.h>
#include <astra/runtime.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "path.h"
#include "resource_internal.h"

static AstraProcessFilesystem filesystem;
typedef struct PosixFileSlot {
    AstraFile file;
    uint8_t used;
} PosixFileSlot;

static PosixFileSlot *file_slots;
static uint32_t file_capacity;
static char cwd[ASTRA_VFS_PATH_MAX];
static int started;
static mode_t creation_mask = 0022u;

typedef struct PosixPath {
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
    case ASTRA_VFS_ERR_LOOP:        return ELOOP;
    case ASTRA_VFS_ERR_READ_ONLY:   return EROFS;
    default:                      return EIO;
    }
}

typedef struct PosixExecInterpreter {
    AstraVfsReadSource source;
    uint32_t failure;
} PosixExecInterpreter;

typedef struct PosixExecPrepare {
    void **handoff;
    uint32_t *handoff_size;
    int failure_errno;
} PosixExecPrepare;

static uint32_t
open_exec_interpreter(void *context, const char *identity,
                      AstraReadSource *source)
{
    PosixExecInterpreter *interpreter = context;
    AstraLibraryReference reference;
    uint32_t status;

    status = astra_process_library_source_open(
        identity, &interpreter->source, &reference);
    if (status != ASTRA_VFS_OK) {
        interpreter->failure = status;
        return ASTRA_SYSCALL_IO_ERROR;
    }
    *source = (AstraReadSource){
        .length = interpreter->source.length,
        .read_at = astra_vfs_read_source_read_at,
        .release = astra_vfs_read_source_close,
        .context = &interpreter->source,
    };
    return ASTRA_SYSCALL_OK;
}

static uint32_t
prepare_posix_exec(void *context, AstraExecRequest *request)
{
    PosixExecPrepare *prepare = context;

    if (astra_posix_exec_export(prepare->handoff,
                                prepare->handoff_size) < 0) {
        prepare->failure_errno = errno;
        return ASTRA_SYSCALL_IO_ERROR;
    }
    request->handoff_address =
        (uint32_t)(uintptr_t)*prepare->handoff;
    request->handoff_size = *prepare->handoff_size;
    return ASTRA_SYSCALL_OK;
}

static int
fail(uint32_t status)
{
    errno = posix_errno(status);
    if (errno == EIO)
        (void)astra_log_failure("POSIX VFS", status);
    return -1;
}

static ssize_t
file_read(uint32_t slot, void *bytes, size_t length);
static ssize_t
file_write(uint32_t slot, const void *bytes, size_t length);
static ssize_t
file_pread(uint32_t slot, void *bytes, size_t length, off_t offset);
static ssize_t
file_pwrite(uint32_t slot, const void *bytes, size_t length, off_t offset);
static int
file_close(uint32_t slot);
static off_t
file_seek(uint32_t slot, off_t offset, int whence);
static int claim(void);
static uint32_t file_exec_size(void);
static int file_exec_export(void *state, uint32_t capacity, uint32_t *used);
static int file_exec_import(const AstraStartupInfo *startup,
                            const void *state, uint32_t size);
static int file_state_export(uint32_t slot, void *state, uint32_t size);
static int file_state_import(const void *state, uint32_t size,
                             uint32_t *slot);
static int set_process_cwd(const char *normal);

static const AstraPosixFileOps ops = {
    .read = file_read,
    .write = file_write,
    .pread = file_pread,
    .pwrite = file_pwrite,
    .close = file_close,
    .seek = file_seek,
    .exec_size = file_exec_size,
    .exec_export = file_exec_export,
    .exec_import = file_exec_import,
    .file_export = file_state_export,
    .file_import = file_state_import,
};

#define POSIX_FILE_EXEC_MAGIC 0x50464558u
#define POSIX_FILE_EXEC_VERSION 1u

typedef struct PosixFileExecHeader {
    uint32_t magic;
    uint32_t total_size;
    uint32_t vfs_size;
    uint32_t version;
    uint32_t creation_mask;
    char cwd[ASTRA_VFS_PATH_MAX];
} PosixFileExecHeader;

void
astra_posix_file_prepare(void)
{
    astra_posix_file_bind(&ops);
}

int
astra_posix_file_fork_ready(void)
{
    for (uint32_t slot = 0u; slot < file_capacity; ++slot)
        if (file_slots[slot].used != 0u)
            return 0;
    return 1;
}

int
astra_posix_file_after_fork_child(void)
{
    uint32_t status = astra_process_vfs_after_fork_child(
        astra_posix_startup());

    if (status == ASTRA_VFS_OK)
        return 0;
    errno = posix_errno(status);
    return -1;
}

static uint32_t
file_exec_size(void)
{
    return (uint32_t)sizeof(PosixFileExecHeader) +
           astra_process_vfs_state_size();
}

static int
file_exec_export(void *state, uint32_t capacity, uint32_t *used)
{
    PosixFileExecHeader *header = state;
    uint32_t vfs_used = 0u;
    uint32_t required = file_exec_size();

    if (!started || used == NULL) {
        errno = EINVAL;
        return -1;
    }
    *used = required;
    if (state == NULL || capacity < required) {
        errno = ERANGE;
        return -1;
    }
    memset(state, 0, required);
    header->magic = POSIX_FILE_EXEC_MAGIC;
    header->total_size = required;
    header->vfs_size = required - (uint32_t)sizeof(*header);
    header->version = POSIX_FILE_EXEC_VERSION;
    header->creation_mask = creation_mask;
    (void)memcpy(header->cwd, cwd, sizeof(header->cwd));
    if (astra_process_vfs_export(header + 1, header->vfs_size, &vfs_used) !=
            ASTRA_VFS_OK ||
        vfs_used != header->vfs_size) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int
file_exec_import(const AstraStartupInfo *startup, const void *state,
                 uint32_t size)
{
    const PosixFileExecHeader *header = state;
    uint32_t status;

    if (state == NULL || size < sizeof(*header) ||
        header->magic != POSIX_FILE_EXEC_MAGIC ||
        header->version != POSIX_FILE_EXEC_VERSION ||
        header->total_size != size ||
        header->vfs_size != size - (uint32_t)sizeof(*header) ||
        header->cwd[sizeof(header->cwd) - 1u] != '\0' ||
        (header->creation_mask & ~(uint32_t)0777u) != 0u) {
        errno = EINVAL;
        return -1;
    }
    status = astra_process_vfs_import(startup, header + 1, header->vfs_size);
    if (status != ASTRA_VFS_OK) {
        (void)astra_log_failure("POSIX VFS restore", status);
        errno = EINVAL;
        return -1;
    }
    filesystem = (AstraProcessFilesystem)ASTRA_PROCESS_FILESYSTEM_INIT;
    if (astra_process_filesystem_open(&filesystem, startup) != ASTRA_VFS_OK) {
        errno = EIO;
        return -1;
    }
    if (set_process_cwd(header->cwd) != 0)
        return -1;
    (void)memcpy(cwd, header->cwd, sizeof(cwd));
    creation_mask = (mode_t)header->creation_mask;
    astra_posix_file_bind(&ops);
    started = 1;
    return 0;
}

static int
file_state_export(uint32_t slot, void *state, uint32_t size)
{
    AstraPosixFileExecState *output = state;
    uint32_t status;

    if (state == NULL || size != sizeof(*output) || slot >= file_capacity ||
        file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    status = astra_process_file_export(&file_slots[slot].file, output);
    if (status != ASTRA_VFS_OK) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int
file_state_import(const void *state, uint32_t size, uint32_t *slot_out)
{
    const AstraPosixFileExecState *input = state;
    int slot;

    if (!started || state == NULL || size != sizeof(*input) ||
        slot_out == NULL || input->service == 0u ||
        input->file == ASTRA_VFS_FILE_INVALID) {
        errno = EINVAL;
        return -1;
    }
    slot = claim();
    if (slot < 0)
        return -1;
    if (astra_process_file_import(input, &file_slots[slot].file) !=
        ASTRA_VFS_OK) {
        errno = EIO;
        return -1;
    }
    file_slots[slot].used = 1u;
    *slot_out = (uint32_t)slot;
    return 0;
}

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
                 astra_assign_lookup(assigns, "CWD") != NULL ?
                     "/cwd" :
                 astra_assign_lookup(assigns, "WORK") != NULL ?
                     "/work" : "/");
    astra_posix_file_bind(&ops);
    started = 1;
    return 1;
}

/* One namespace conversion shared by every file and directory operation. */
static int
resolve_path(const char *path, PosixPath *out, char *normal)
{
    int result;

    if (path == NULL) {
        errno = EFAULT;
        return 0;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return 0;
    }
    if (!start())
        return 0;
    result = normal != NULL ?
        astra_posix_path_resolve(cwd, path, normal, ASTRA_VFS_PATH_MAX,
                                 out->native, sizeof(out->native)) :
        astra_posix_path_resolve_native(cwd, path, out->native,
                                        sizeof(out->native));
    if (result < 0) {
        errno = ENAMETOOLONG;
        return 0;
    }
    out->root = result == 0;
    return 1;
}

static int
resolve(const char *path, PosixPath *out)
{
    return resolve_path(path, out, NULL);
}

static int
set_process_cwd(const char *normal)
{
    const char *slash;
    char assign[ASTRA_CAPABILITY_NAME_MAX];
    size_t length;
    uint32_t status;

    if (normal == NULL || normal[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    if (normal[1] == '\0') {
        status = astra_process_vfs_set_current_directory("", "");
    } else {
        slash = strchr(normal + 1, '/');
        length = slash != NULL ? (size_t)(slash - (normal + 1)) :
                                 strlen(normal + 1);
        if (length >= sizeof(assign)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        (void)memcpy(assign, normal + 1, length);
        assign[length] = '\0';
        status = astra_process_vfs_set_current_directory(
            assign, slash != NULL ? slash + 1 : "");
    }
    return status == ASTRA_VFS_OK ? 0 : fail(status);
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
    status = astra_filesystem_read(&file_slots[slot].file, bytes,
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
    rlim_t limit;
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    limit = astra_posix_resource_file_size();
    if (limit != RLIM_INFINITY) {
        off_t position = file_seek(slot, 0, SEEK_CUR);
        uint64_t remaining;

        if (position < 0)
            return -1;
        if ((uint64_t)position >= limit) {
            (void)kill(getpid(), SIGXFSZ);
            errno = EFBIG;
            return -1;
        }
        remaining = limit - (uint64_t)position;
        if ((uint64_t)length > remaining)
            length = (size_t)remaining;
    }
    status = astra_filesystem_write(&file_slots[slot].file, bytes,
                                       (uint32_t)length,
                                       &moved);
    if (status != ASTRA_VFS_OK && moved == 0u)
        return fail(status);
    return (ssize_t)moved;
}

static ssize_t
file_pread(uint32_t slot, void *bytes, size_t length, off_t offset)
{
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (offset < 0 || length > (size_t)SSIZE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (length == 0u)
        return 0;
    if (bytes == NULL) {
        errno = EFAULT;
        return -1;
    }
    status = astra_filesystem_read_at(
        &file_slots[slot].file, (uint64_t)offset, bytes, (uint32_t)length,
        &moved);
    if (status != ASTRA_VFS_OK && moved == 0u)
        return fail(status);
    return (ssize_t)moved;
}

static ssize_t
file_pwrite(uint32_t slot, const void *bytes, size_t length, off_t offset)
{
    rlim_t limit;
    uint32_t moved = 0u;
    uint32_t status;

    if (slot >= file_capacity || file_slots[slot].used == 0u) {
        errno = EBADF;
        return -1;
    }
    if (offset < 0 || length > (size_t)SSIZE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (length == 0u)
        return 0;
    if (bytes == NULL) {
        errno = EFAULT;
        return -1;
    }
    limit = astra_posix_resource_file_size();
    if (limit != RLIM_INFINITY) {
        uint64_t remaining;

        if ((uint64_t)offset >= limit) {
            (void)kill(getpid(), SIGXFSZ);
            errno = EFBIG;
            return -1;
        }
        remaining = limit - (uint64_t)offset;
        if ((uint64_t)length > remaining)
            length = (size_t)remaining;
    }
    status = astra_filesystem_write_at(
        &file_slots[slot].file, (uint64_t)offset, bytes, (uint32_t)length,
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
    status = astra_filesystem_close(&file_slots[slot].file);
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
    status = astra_filesystem_seek(&file_slots[slot].file,
                                      (int64_t)offset, origin,
                                      &result);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    return (off_t)result;
}

static int
translate_open_flags(int flags, uint32_t *wanted)
{
    *wanted = 0u;
    switch (flags & O_ACCMODE) {
    case O_RDONLY: *wanted = ASTRA_VFS_OPEN_READ; break;
    case O_WRONLY: *wanted = ASTRA_VFS_OPEN_WRITE; break;
    case O_RDWR:
        *wanted = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
        break;
    default:
        errno = EINVAL;
        return 0;
    }
    if ((flags & O_CREAT) != 0)
        *wanted |= ASTRA_VFS_OPEN_CREATE;
    if ((flags & O_TRUNC) != 0)
        *wanted |= ASTRA_VFS_OPEN_TRUNCATE;
    if ((flags & O_EXCL) != 0)
        *wanted |= ASTRA_VFS_OPEN_EXCLUSIVE;
    if ((flags & O_APPEND) != 0)
        *wanted |= ASTRA_VFS_OPEN_APPEND;
#ifdef O_DIRECTORY
    if ((flags & O_DIRECTORY) != 0)
        *wanted |= ASTRA_VFS_OPEN_DIRECTORY;
#endif
    return 1;
}

/* POSIX O_RDONLY opens a directory; native VFS requires an explicit kind. */
static int
may_open_directory(uint32_t wanted)
{
    return wanted == ASTRA_VFS_OPEN_READ;
}

static int
install_open_file(AstraFile *opened, int flags)
{
    int slot = claim();
    int fd;

    if (slot < 0)
        return -1;
    file_slots[slot].file = *opened;
    file_slots[slot].used = 1u;
    fd = astra_posix_descriptor_file((uint32_t)slot, flags);
    if (fd < 0) {
        (void)file_close((uint32_t)slot);
        errno = EMFILE;
        return -1;
    }
    return fd;
}

static AstraFile *
file_for_descriptor(int fd)
{
    int slot = astra_posix_descriptor_slot(fd);

    if (slot < 0 || (uint32_t)slot >= file_capacity ||
        file_slots[slot].used == 0u) {
        errno = EBADF;
        return NULL;
    }
    return &file_slots[slot].file;
}

static int
open_path(const char *path, int flags, mode_t create_mode)
{
    AstraFile opened = ASTRA_FILE_INIT;
    PosixPath resolved;
    uint32_t wanted;
    uint32_t status;

    if (path != NULL && strcmp(path, "/dev/tty") == 0) {
        if ((flags & (O_CREAT | O_TRUNC | O_EXCL | O_APPEND)) != 0) {
            errno = EINVAL;
            return -1;
        }
        return astra_posix_descriptor_controlling_terminal(flags);
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EISDIR;
        return -1;
    }
    if (!translate_open_flags(flags, &wanted))
        return -1;
    status = astra_filesystem_open_mode(
        &filesystem.filesystem, resolved.native, wanted,
        (uint16_t)create_mode, &opened);
    if ((status == ASTRA_VFS_ERR_NOT_FOUND ||
         status == ASTRA_VFS_ERR_IS_DIR) && may_open_directory(wanted)) {
        AstraFileInfo info = ASTRA_FILE_INFO_INIT;

        if (astra_filesystem_stat(&filesystem.filesystem, resolved.native,
                                  &info) == ASTRA_VFS_OK &&
            info.kind == ASTRA_VFS_KIND_DIRECTORY)
            status = astra_filesystem_open_mode(
                &filesystem.filesystem, resolved.native,
                wanted | ASTRA_VFS_OPEN_DIRECTORY,
                (uint16_t)create_mode, &opened);
    }
    if (status != ASTRA_VFS_OK)
        return fail(status);
    return install_open_file(&opened, flags);
}

int
open(const char *path, int flags, ...)
{
    mode_t create_mode = ASTRA_VFS_MODE_DEFAULT;

    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        create_mode = va_arg(arguments, mode_t);
        va_end(arguments);
        create_mode &= (mode_t)(ASTRA_VFS_MODE_MASK & ~creation_mask);
    }
    return open_path(path, flags, create_mode);
}

int
openat(int dirfd, const char *path, int flags, ...)
{
    AstraFile opened = ASTRA_FILE_INIT;
    AstraFile *directory;
    uint32_t wanted;
    uint32_t status;
    mode_t create_mode = ASTRA_VFS_MODE_DEFAULT;

    if ((flags & O_CREAT) != 0) {
        va_list arguments;

        va_start(arguments, flags);
        create_mode = va_arg(arguments, mode_t);
        va_end(arguments);
        create_mode &= (mode_t)(ASTRA_VFS_MODE_MASK & ~creation_mask);
    }
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (astra_posix_path_is_absolute(path) || dirfd == AT_FDCWD)
        return open_path(path, flags, create_mode);
    if (!start())
        return -1;
    if (!translate_open_flags(flags, &wanted))
        return -1;
    directory = file_for_descriptor(dirfd);
    if (directory == NULL)
        return -1;
    status = astra_filesystem_open_at_mode(
        directory, path, wanted, (uint16_t)create_mode, &opened);
    if ((status == ASTRA_VFS_ERR_NOT_FOUND ||
         status == ASTRA_VFS_ERR_IS_DIR) && may_open_directory(wanted)) {
        AstraFileInfo info = ASTRA_FILE_INFO_INIT;

        if (astra_filesystem_stat_at(directory, path, &info) ==
                ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY)
            status = astra_filesystem_open_at_mode(
                directory, path, wanted | ASTRA_VFS_OPEN_DIRECTORY,
                (uint16_t)create_mode, &opened);
    }
    if (status != ASTRA_VFS_OK)
        return fail(status);
    return install_open_file(&opened, flags);
}

int
execve(const char *path, char *const argv[], char *const envp[])
{
    AstraExecRequest request;
    AstraVfsReadSource program_source = ASTRA_VFS_READ_SOURCE_INIT;
    PosixExecInterpreter interpreter = {
        .source = ASTRA_VFS_READ_SOURCE_INIT,
    };
    AstraReadSource program;
    char *vectors = NULL;
    void *handoff = NULL;
    uint32_t handoff_size = 0u;
    PosixExecPrepare prepare = {
        .handoff = &handoff,
        .handoff_size = &handoff_size,
    };
    uint32_t status;
    uint32_t failure_detail = 0u;
    PosixPath resolved;
    const char *failure_operation = "execve open";
    int program_open = 0;

    if (path == NULL || argv == NULL || argv[0] == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (access(path, X_OK) != 0)
        goto failed;
    if (!resolve(path, &resolved))
        goto failed;
    if (resolved.root) {
        errno = EACCES;
        goto failed;
    }
    status = astra_vfs_read_source_open(
        &program_source, astra_process_vfs_assigns(), resolved.native,
        astra_process_vfs_assign_client, NULL);
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        goto failed;
    }
    program_open = 1;
    program = (AstraReadSource){
        .length = program_source.length,
        .read_at = astra_vfs_read_source_read_at,
        .release = astra_vfs_read_source_close,
        .context = &program_source,
    };
    vectors = malloc(ASTRA_STARTUP_BLOCK_SIZE);
    if (vectors == NULL) {
        errno = ENOMEM;
        goto failed;
    }
    failure_operation = "execve arguments";
    status = astra_exec_request_pack(
        &request, vectors, ASTRA_STARTUP_BLOCK_SIZE,
        astra_startup_launch_source(astra_posix_startup()), argv, envp);
    if (status != ASTRA_SYSCALL_OK) {
        errno = status == ASTRA_SYSCALL_RESOURCE_LIMIT ? E2BIG : EINVAL;
        goto failed;
    }
    failure_operation = "execve process replacement";
    status = astra_exec_executable_stream(
        &program, open_exec_interpreter, &interpreter,
        prepare_posix_exec, &prepare, &request);
    program_open = 0;
    failure_detail = status;
    if (prepare.failure_errno != 0)
        errno = prepare.failure_errno;
    else if (interpreter.failure != 0u)
        errno = posix_errno(interpreter.failure);
    else if (status == ASTRA_SYSCALL_INVALID_ARGUMENT)
        errno = ENOEXEC;
    else if (status == ASTRA_SYSCALL_RESOURCE_LIMIT)
        errno = E2BIG;
    else if (status == ASTRA_SYSCALL_OUT_OF_MEMORY)
        errno = ENOMEM;
    else if (status == ASTRA_SYSCALL_BAD_ADDRESS)
        errno = EFAULT;
    else
        errno = EIO;

failed:
    if (errno == EIO)
        (void)astra_log_failure(failure_operation,
                                failure_detail != 0u ? failure_detail :
                                                       (uint32_t)errno);
    if (program_open != 0)
        (void)astra_vfs_read_source_close(&program_source);
    free(handoff);
    free(vectors);
    return -1;
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
    status = astra_filesystem_sync(&file_slots[slot].file);
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
    if (astra_posix_resource_file_size() != RLIM_INFINITY &&
        (uint64_t)length > astra_posix_resource_file_size()) {
        (void)kill(getpid(), SIGXFSZ);
        errno = EFBIG;
        return -1;
    }
    status = astra_filesystem_truncate(&file_slots[slot].file,
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
    int symlink = info->kind == ASTRA_VFS_KIND_SYMLINK;

    (void)memset(out, 0, sizeof(*out));
    out->st_mode = (mode_t)((directory ? S_IFDIR : symlink ? S_IFLNK : S_IFREG) |
                            (info->mode != 0u ? info->mode :
                             (directory ? 0755u : symlink ? 0777u : 0644u)));
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

static int
stat_path(const char *path, struct stat *out, int literal)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    PosixPath resolved;
    const char *leaf;
    uint32_t status;

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (path != NULL && strcmp(path, "/dev/tty") == 0) {
        int fd = astra_posix_descriptor_controlling_terminal(O_RDONLY);

        if (fd < 0)
            return -1;
        (void)memset(out, 0, sizeof(*out));
        out->st_mode = S_IFCHR | 0620;
        out->st_nlink = 1;
        out->st_blksize = 512;
        (void)close(fd);
        return 0;
    }
    if (!resolve(path, &resolved))
        return -1;
    /* A final slash, . or .. names a directory, even after a root alias. */
    leaf = strrchr(path, '/');
    leaf = leaf != NULL ? leaf + 1u : path;
    if (literal && (leaf[0] == '\0' || strcmp(leaf, ".") == 0 ||
                    strcmp(leaf, "..") == 0))
        literal = 0;
    status = literal ?
        astra_filesystem_lstat(&filesystem.filesystem, resolved.native,
                                  &info) :
        astra_filesystem_stat(&filesystem.filesystem, resolved.native,
                                  &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill(out, &info);
    return 0;
}

int stat(const char *path, struct stat *out)
{
    return stat_path(path, out, 0);
}

int lstat(const char *path, struct stat *out)
{
    return stat_path(path, out, 1);
}

int
fstatat(int dirfd, const char *path, struct stat *out, int flags)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraFile *directory;
    uint32_t status;

    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (path == NULL || out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (astra_posix_path_is_absolute(path) || dirfd == AT_FDCWD)
        return stat_path(path, out, (flags & AT_SYMLINK_NOFOLLOW) != 0);
    if ((flags & AT_SYMLINK_NOFOLLOW) != 0) {
        errno = ENOTSUP;
        return -1;
    }
    if (!start())
        return -1;
    directory = file_for_descriptor(dirfd);
    if (directory == NULL)
        return -1;
    status = astra_filesystem_stat_at(directory, path, &info);
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
        if (astra_posix_descriptor_flags(fd) < 0)
            return -1;
        /*
         * A valid non-file descriptor. stdio calls this on its streams to
         * decide how to buffer, and a character device is what a stream is.
         */
        (void)memset(out, 0, sizeof(*out));
        out->st_mode = S_IFCHR | 0620;
        out->st_nlink = 1;
        out->st_blksize = 512;
        return 0;
    }
    status = astra_filesystem_file_info(&file_slots[slot].file, &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill(out, &info);
    return 0;
}

static void
fill_statvfs(struct statvfs *out, const AstraFilesystemInfo *info)
{
    (void)memset(out, 0, sizeof(*out));
    out->f_bsize = info->block_size;
    out->f_frsize = info->fragment_size;
    out->f_blocks = (fsblkcnt_t)info->blocks;
    out->f_bfree = (fsblkcnt_t)info->blocks_free;
    out->f_bavail = (fsblkcnt_t)info->blocks_available;
    out->f_files = (fsfilcnt_t)info->files;
    out->f_ffree = (fsfilcnt_t)info->files_free;
    out->f_favail = (fsfilcnt_t)info->files_free;
    out->f_flag = info->flags & (ST_RDONLY | ST_NOSUID);
    out->f_namemax = info->name_max;
}

int
statvfs(const char *path, struct statvfs *out)
{
    AstraFilesystemInfo info = { .size = sizeof(info) };
    PosixPath resolved;
    uint32_t status;

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EXDEV;
        return -1;
    }
    status = astra_filesystem_capacity_path(
        &filesystem.filesystem, resolved.native, &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill_statvfs(out, &info);
    return 0;
}

int
fstatvfs(int fd, struct statvfs *out)
{
    AstraFilesystemInfo info = { .size = sizeof(info) };
    AstraFile *file;
    uint32_t status;

    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }
    file = file_for_descriptor(fd);
    if (file == NULL)
        return -1;
    status = astra_filesystem_capacity_file(file, &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    fill_statvfs(out, &info);
    return 0;
}

int
access(const char *path, int mode)
{
    struct stat about;

    if ((mode & ~(R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    /*
     * Namespace rights still decide whether an open may read or write; inode
     * bits cannot answer that capability question. Execute is different:
     * command lookup must reject a regular file with no execute bit before it
     * attempts execve.
     */
    if (stat(path, &about) != 0)
        return -1;
    if ((mode & X_OK) != 0 &&
        (about.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int
faccessat(int dirfd, const char *path, int mode, int flags)
{
    struct stat about;

    if ((mode & ~(R_OK | W_OK | X_OK)) != 0 ||
        (flags & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (fstatat(dirfd, path, &about, flags & AT_SYMLINK_NOFOLLOW) != 0)
        return -1;
    if ((mode & X_OK) != 0 &&
        (about.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int
mkdir(const char *path, mode_t mode)
{
    PosixPath resolved;
    uint32_t status;

    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EEXIST;
        return -1;
    }
    status = astra_filesystem_mkdir_mode(
        &filesystem.filesystem, resolved.native,
        (uint16_t)(mode & (mode_t)(ASTRA_VFS_MODE_MASK & ~creation_mask)));
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

static int
remove_path(const char *path, uint32_t flags)
{
    AstraFile parent = ASTRA_FILE_INIT;
    PosixPath resolved;
    char parent_path[ASTRA_VFS_PATH_MAX];
    const char *leaf;
    const char *separator;
    size_t parent_length;
    uint32_t close_status;
    uint32_t status;

    if ((flags & ~ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = flags == 0u ? EISDIR : EBUSY;
        return -1;
    }
    separator = strrchr(resolved.native, '/');
    if (separator == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (separator == resolved.native) {
        errno = EROFS;
        return -1;
    }
    if (separator == NULL || separator[1] == '\0') {
        errno = flags == 0u ? EISDIR : EBUSY;
        return -1;
    }
    leaf = separator + 1u;
    parent_length = (size_t)(separator - resolved.native);
    if (parent_length + 1u > sizeof(parent_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)memcpy(parent_path, resolved.native, parent_length);
    parent_path[parent_length] = '\0';
    status = astra_filesystem_open_mode(
        &filesystem.filesystem, parent_path,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY,
        ASTRA_VFS_MODE_DEFAULT, &parent);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    status = astra_filesystem_unlink_at(&parent, leaf, flags);
    close_status = astra_filesystem_close(&parent);
    if (status == ASTRA_VFS_OK)
        status = close_status;
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
unlink(const char *path)
{
    return remove_path(path, 0u);
}

int
unlinkat(int dirfd, const char *path, int flags)
{
    AstraFile *directory;
    uint32_t astra_flags = 0u;
    uint32_t status;

    if ((flags & ~AT_REMOVEDIR) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & AT_REMOVEDIR) != 0)
        astra_flags = ASTRA_VFS_AT_REMOVE_DIRECTORY;
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (astra_posix_path_is_absolute(path) || dirfd == AT_FDCWD)
        return remove_path(path, astra_flags);
    if (!start())
        return -1;
    directory = file_for_descriptor(dirfd);
    if (directory == NULL)
        return -1;
    status = astra_filesystem_unlink_at(directory, path, astra_flags);
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
    status = astra_filesystem_rename(&filesystem.filesystem,
                                        from_resolved.native,
                                        to_resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
link(const char *from, const char *to)
{
    PosixPath from_resolved;
    PosixPath to_resolved;
    uint32_t status;

    if (!resolve(from, &from_resolved) || !resolve(to, &to_resolved))
        return -1;
    if (from_resolved.root || to_resolved.root) {
        errno = from_resolved.root ? EPERM : EEXIST;
        return -1;
    }
    status = astra_filesystem_link(&filesystem.filesystem,
                                      from_resolved.native,
                                      to_resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

mode_t
umask(mode_t mask)
{
    mode_t previous = creation_mask;

    creation_mask = mask & 0777u;
    return previous;
}

int
chmod(const char *path, mode_t mode)
{
    PosixPath resolved;
    uint32_t status;

    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EROFS;
        return -1;
    }
    status = astra_filesystem_chmod(
        &filesystem.filesystem, resolved.native,
        (uint16_t)(mode & ASTRA_VFS_MODE_MASK));
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
fchmod(int fd, mode_t mode)
{
    AstraFile *file = file_for_descriptor(fd);
    uint32_t status;

    if (file == NULL)
        return -1;
    status = astra_filesystem_chmod_file(
        file, (uint16_t)(mode & ASTRA_VFS_MODE_MASK));
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    AstraFile *directory;
    uint32_t astra_flags = 0u;
    uint32_t status;

    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & AT_SYMLINK_NOFOLLOW) != 0)
        astra_flags = ASTRA_VFS_AT_SYMLINK_NOFOLLOW;
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (path[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (astra_posix_path_is_absolute(path) || dirfd == AT_FDCWD) {
        if (astra_flags != 0u) {
            errno = ENOTSUP;
            return -1;
        }
        return chmod(path, mode);
    }
    if (!start())
        return -1;
    directory = file_for_descriptor(dirfd);
    if (directory == NULL)
        return -1;
    status = astra_filesystem_chmod_at(
        directory, path, (uint16_t)(mode & ASTRA_VFS_MODE_MASK), astra_flags);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

ssize_t
readlink(const char *path, char *buffer, size_t capacity)
{
    PosixPath resolved;
    char native_target[ASTRA_VFS_PATH_MAX + 1u];
    char posix_target[ASTRA_VFS_PATH_MAX + 2u];
    uint32_t length = 0u;
    uint32_t status;
    size_t moved;

    if (buffer == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (capacity == 0u) {
        errno = EINVAL;
        return -1;
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EINVAL;
        return -1;
    }
    status = astra_filesystem_readlink(
        &filesystem.filesystem, resolved.native, native_target,
        ASTRA_VFS_PATH_MAX, &length);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    if (length > ASTRA_VFS_PATH_MAX) {
        errno = EIO;
        return -1;
    }
    native_target[length] = '\0';
    if (astra_posix_link_target_to_posix(native_target, posix_target,
                                         sizeof(posix_target)) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    moved = strlen(posix_target);
    if (moved > capacity)
        moved = capacity;               /* POSIX readlink truncates. */
    (void)memcpy(buffer, posix_target, moved);
    return (ssize_t)moved;
}

int
symlink(const char *target, const char *path)
{
    PosixPath resolved;
    char native_target[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (target == NULL || path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (target[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (!resolve(path, &resolved))
        return -1;
    if (resolved.root) {
        errno = EEXIST;
        return -1;
    }
    if (astra_posix_link_target_to_native(target, native_target,
                                          sizeof(native_target)) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    status = astra_filesystem_symlink(
        native_target, &filesystem.filesystem, resolved.native);
    return status == ASTRA_VFS_OK ? 0 : fail(status);
}

int
rmdir(const char *path)
{
    return remove_path(path, ASTRA_VFS_AT_REMOVE_DIRECTORY);
}

int
chdir(const char *path)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    PosixPath resolved;
    char normal[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!resolve_path(path, &resolved, normal))
        return -1;
    if (resolved.root) {
        if (set_process_cwd("/") != 0)
            return -1;
        (void)strcpy(cwd, "/");
        return 0;
    }
    status = astra_filesystem_stat(&filesystem.filesystem, resolved.native,
                                      &info);
    if (status != ASTRA_VFS_OK)
        return fail(status);
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }
    if (set_process_cwd(normal) != 0)
        return -1;
    (void)strcpy(cwd, normal);
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
    uint64_t position;
    uint32_t count;
    uint32_t next;
} PosixDir;

_Static_assert(sizeof(PosixDir) <= sizeof(((DIR *)0)->buf),
               "the Astra directory handle and its batch must fit in DIR");

DIR *
opendir(const char *path)
{
    PosixPath resolved;
    DIR *dir;
    int fd;

    if (!resolve(path, &resolved))
        return NULL;
    if (resolved.root) {
        PosixDir *state;
        uint32_t status;

        dir = calloc(1u, sizeof(*dir));
        if (dir == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        dir->fd = -1;
        state = (PosixDir *)(void *)dir->buf;
        status = astra_filesystem_directory_open(
            &filesystem.filesystem, "/", &state->directory);
        if (status != ASTRA_VFS_OK) {
            free(dir);
            (void)fail(status);
            return NULL;
        }
        return dir;
    }
    fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return NULL;
    dir = fdopendir(fd);
    if (dir == NULL)
        (void)close(fd);
    return dir;
}

DIR *
fdopendir(int fd)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    AstraFile *file;
    PosixDir *state;
    DIR *dir;
    uint32_t status;
    int flags;

    file = file_for_descriptor(fd);
    if (file == NULL)
        return NULL;
    flags = astra_posix_descriptor_flags(fd);
    if (flags < 0)
        return NULL;
    if ((flags & O_ACCMODE) == O_WRONLY) {
        errno = EINVAL;
        return NULL;
    }
    status = astra_filesystem_file_info(file, &info);
    if (status != ASTRA_VFS_OK) {
        (void)fail(status);
        return NULL;
    }
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        errno = ENOTDIR;
        return NULL;
    }
    dir = calloc(1u, sizeof(*dir));
    if (dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    dir->fd = fd;
    state = (PosixDir *)(void *)dir->buf;
    status = astra_filesystem_directory_from_file(file, &state->directory);
    if (status != ASTRA_VFS_OK) {
        free(dir);
        (void)fail(status);
        return NULL;
    }
    return dir;
}

int
dirfd(DIR *dir)
{
    if (dir == NULL || dir->fd < 0) {
        errno = EBADF;
        return -1;
    }
    return dir->fd;
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
        uint32_t status = astra_filesystem_directory_read(
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
                         entry->kind == ASTRA_VFS_KIND_SYMLINK ? DT_LNK :
                                                                DT_REG;
    for (index = 0u; index + 1u < sizeof(dir->dirent.d_name) &&
                     entry->name[index] != '\0'; ++index)
        dir->dirent.d_name[index] = entry->name[index];
    dir->dirent.d_name[index] = '\0';
    ++state->position;
    return &dir->dirent;
}

static int
directory_rewind(DIR *dir)
{
    PosixDir *state;
    uint32_t status;

    if (dir == NULL) {
        errno = EBADF;
        return -1;
    }
    state = (PosixDir *)(void *)dir->buf;
    status = astra_filesystem_directory_rewind(&state->directory);
    if (status != ASTRA_VFS_OK) {
        errno = posix_errno(status);
        return -1;
    }
    state->position = 0u;
    state->count = 0u;
    state->next = 0u;
    return 0;
}

void
rewinddir(DIR *dir)
{
    (void)directory_rewind(dir);
}

long
telldir(DIR *dir)
{
    const PosixDir *state;

    if (dir == NULL) {
        errno = EBADF;
        return -1L;
    }
    state = (const PosixDir *)(const void *)dir->buf;
    if (state->position > (uint64_t)LONG_MAX) {
        errno = EOVERFLOW;
        return -1L;
    }
    return (long)state->position;
}

void
seekdir(DIR *dir, long location)
{
    PosixDir *state;

    if (location < 0L || directory_rewind(dir) != 0) {
        if (location < 0L)
            errno = EINVAL;
        return;
    }
    state = (PosixDir *)(void *)dir->buf;
    while (state->position < (uint64_t)location) {
        errno = 0;
        if (readdir(dir) == NULL) {
            if (errno == 0)
                errno = EINVAL;
            return;
        }
    }
}

int
fdclosedir(DIR *dir)
{
    PosixDir *state;
    int fd;

    if (dir == NULL || dir->fd < 0) {
        errno = EBADF;
        return -1;
    }
    fd = dir->fd;
    state = (PosixDir *)(void *)dir->buf;
    astra_filesystem_directory_close(&state->directory);
    free(dir);
    return fd;
}

int
closedir(DIR *dir)
{
    PosixDir *state;
    int result = 0;

    if (dir == NULL) {
        errno = EBADF;
        return -1;
    }
    state = (PosixDir *)(void *)dir->buf;
    astra_filesystem_directory_close(&state->directory);
    if (dir->fd >= 0)
        result = close(dir->fd);
    free(dir);
    return result;
}
