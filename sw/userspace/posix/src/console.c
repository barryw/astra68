/*
 * The descriptor table.
 *
 * 0, 1 and 2 arrive as stream capabilities the program was granted; anything
 * above them is whatever `open()` put there. picolibc's stdio calls read(),
 * write(), close() and lseek() on a number and nothing else, so this file is
 * the whole of what stdio needs and the only place a number becomes authority.
 *
 * One table rather than two, because a descriptor has to be able to change
 * what it is: a shell redirecting output makes fd 1 a file while stdio goes on
 * writing to fd 1. Files are reached through a registered vector rather than a
 * direct call, so a command that only prints does not drag the VFS client in
 * behind printf -- see `posix_descriptor.h` for why that seam is where it is.
 */

#include <astra/posix.h>
#include <astra/posix_descriptor.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    POSIX_STDIN = 0,
    POSIX_STDOUT = 1,
    POSIX_STDERR = 2,
    /*
     * Sixteen. A Unix program expects to open a few files at once; this is not
     * a resource the kernel is spending, only the table, and an entry is eight
     * bytes. The real ceiling is further down in the VFS client, and this
     * being generous means a program meets the honest limit rather than this
     * one.
     */
    POSIX_DESCRIPTOR_MAX = 16,
};

typedef enum PosixDescriptorKind {
    POSIX_DESCRIPTOR_FREE = 0,
    POSIX_DESCRIPTOR_STREAM,
    POSIX_DESCRIPTOR_FILE
} PosixDescriptorKind;

typedef struct PosixDescriptor {
    uint8_t kind;
    /* A stream capability, or the file half's slot. Never both. */
    uint32_t value;
} PosixDescriptor;

/*
 * A free entry means the program was not granted that stream, or has not
 * opened anything there. It is not an error to lack one -- `status` is granted
 * none -- so the failure surfaces at the write rather than at startup.
 */
static PosixDescriptor descriptors[POSIX_DESCRIPTOR_MAX];
static const AstraPosixFileOps *file_ops;
static const AstraStartupInfo *startup_block;

/*
 * Keeps this library's `sbrk` in the link, ahead of picolibc's.
 *
 * picolibc ships a *weak* `sbrk` over `__heap_start` and `__heap_end`, in its
 * own archive member. Inside `--start-group` the linker pulls that member the
 * moment `malloc` names `sbrk`, the weak definition satisfies the reference,
 * and the group never comes back for the strong one in `heap.c` -- which links
 * as "undefined reference to __heap_start" and reads like a missing linker
 * script rather than the wrong allocator winning. So something already in the
 * link refers to it: `astra_posix_start` is in this file and every program
 * that uses this library calls it.
 */
extern void *sbrk(intptr_t increment);
typedef void *(*PosixBreakFn)(intptr_t);
static const PosixBreakFn posix_keep_heap __attribute__((used)) = sbrk;

static PosixDescriptor *
entry(int fd)
{
    if (fd < 0 || fd >= POSIX_DESCRIPTOR_MAX ||
        descriptors[fd].kind == POSIX_DESCRIPTOR_FREE)
        return NULL;
    return &descriptors[fd];
}

void
astra_posix_file_bind(const AstraPosixFileOps *ops)
{
    file_ops = ops;
}

const AstraStartupInfo *
astra_posix_startup(void)
{
    return startup_block;
}

int
astra_posix_descriptor_file(uint32_t slot)
{
    /* Lowest free, because POSIX promises it and a redirect depends on it. */
    for (int fd = 0; fd < POSIX_DESCRIPTOR_MAX; ++fd) {
        if (descriptors[fd].kind != POSIX_DESCRIPTOR_FREE)
            continue;
        descriptors[fd].kind = POSIX_DESCRIPTOR_FILE;
        descriptors[fd].value = slot;
        return fd;
    }
    errno = EMFILE;
    return -1;
}

int
astra_posix_descriptor_slot(int fd)
{
    PosixDescriptor *slot = entry(fd);

    if (slot == NULL || slot->kind != POSIX_DESCRIPTOR_FILE)
        return -1;
    return (int)slot->value;
}

void
astra_posix_start(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;

    for (uint32_t index = 0u; index < POSIX_DESCRIPTOR_MAX; ++index) {
        descriptors[index].kind = POSIX_DESCRIPTOR_FREE;
        descriptors[index].value = 0u;
    }
    startup_block = startup;
    if (startup == NULL || startup->capabilities_address == 0u)
        return;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        const char *name = capabilities[index].name;
        int fd = -1;

        if (astra_capability_name_equal(name, "STDIN"))
            fd = POSIX_STDIN;
        else if (astra_capability_name_equal(name, "STDOUT"))
            fd = POSIX_STDOUT;
        else if (astra_capability_name_equal(name, "STDERR"))
            fd = POSIX_STDERR;
        if (fd < 0 || capabilities[index].handle == 0u)
            continue;
        descriptors[fd].kind = POSIX_DESCRIPTOR_STREAM;
        descriptors[fd].value = capabilities[index].handle;
    }
    /*
     * A program with STDOUT and no STDERR gets its diagnostics on STDOUT
     * rather than losing them. Unix would have given it both; the manifest
     * that granted one and not the other did not mean silence.
     */
    if (descriptors[POSIX_STDERR].kind == POSIX_DESCRIPTOR_FREE)
        descriptors[POSIX_STDERR] = descriptors[POSIX_STDOUT];
}

ssize_t
write(int fd, const void *bytes, size_t length)
{
    PosixDescriptor *slot = entry(fd);
    uint32_t written = 0u;
    uint32_t status;

    if (slot == NULL || bytes == NULL) {
        errno = EBADF;
        return -1;
    }
    if (length == 0u)
        return 0;
    if (length > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (slot->kind == POSIX_DESCRIPTOR_FILE) {
        if (file_ops == NULL) {
            errno = EBADF;
            return -1;
        }
        return file_ops->write(slot->value, bytes, length);
    }
    /*
     * Back pressure is a short write, not a failure: `written` says how much
     * arrived and stdio retries from there. A sink with no room at all is the
     * same fact with nothing to report, and answering EAGAIN to that is what
     * POSIX means by a *non-blocking* write -- which nobody here asked for.
     * stdio takes the error and stops, so an unbuffered stream loses the rest
     * of its line: `fprintf(stderr, "rm: %s: %s\n", ...)` came out as `rm: `
     * and nothing else, because the first segment filled the sink and the
     * second was refused. So this waits, for the same reason `read` below
     * does, and yields rather than spins so waiting on the terminal is not
     * the reason nothing else on the machine runs. A dead sink is still an
     * error; it does not answer WOULD_BLOCK.
     */
    for (;;) {
        status = astra_stream_write(slot->value, bytes, (uint32_t)length,
                                    &written);
        if (status == ASTRA_SYSCALL_OK || written != 0u)
            return (ssize_t)written;
        if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
            errno = EIO;
            return -1;
        }
        (void)astra_yield();
    }
}

ssize_t
read(int fd, void *bytes, size_t length)
{
    PosixDescriptor *slot = entry(fd);
    uint32_t taken = 0u;
    uint32_t status;

    if (slot == NULL || bytes == NULL) {
        errno = EBADF;
        return -1;
    }
    if (length == 0u)
        return 0;
    if (length > UINT32_MAX)
        length = UINT32_MAX;
    if (slot->kind == POSIX_DESCRIPTOR_FILE) {
        if (file_ops == NULL) {
            errno = EBADF;
            return -1;
        }
        return file_ops->read(slot->value, bytes, length);
    }
    /*
     * The stream returns what is ready and never waits, which is the right
     * rule for a source but the wrong answer for `read`: a POSIX reader takes
     * nothing to mean end of file and stops. So this waits, and a program that
     * wanted the other behaviour asks for it -- which is what O_NONBLOCK is
     * for, when there is an fcntl to set it.
     *
     * Yielding rather than spinning, because a shell waiting for a key must
     * not be the reason nothing else on the machine runs.
     */
    for (;;) {
        status = astra_stream_read(slot->value, bytes, (uint32_t)length,
                                   &taken);
        if (status != ASTRA_SYSCALL_OK) {
            errno = status == ASTRA_SYSCALL_PEER_DEAD ? EPIPE : EIO;
            return status == ASTRA_SYSCALL_PEER_DEAD ? 0 : -1;
        }
        if (taken != 0u)
            return (ssize_t)taken;
        (void)astra_yield();
    }
}

int
close(int fd)
{
    PosixDescriptor *slot = entry(fd);
    int result = 0;

    if (slot == NULL) {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == POSIX_DESCRIPTOR_FILE && file_ops != NULL)
        result = file_ops->close(slot->value);
    /*
     * A stream handle came from the startup block and belongs to the process,
     * not to stdio. Dropping the table entry ends this program's use of it
     * without revoking a capability something else may still hold.
     */
    slot->kind = POSIX_DESCRIPTOR_FREE;
    slot->value = 0u;
    return result;
}

int
isatty(int fd)
{
    PosixDescriptor *slot = entry(fd);

    if (slot == NULL) {
        errno = EBADF;
        return 0;
    }
    if (slot->kind == POSIX_DESCRIPTOR_FILE) {
        errno = ENOTTY;
        return 0;
    }
    /*
     * Every stream a program is granted today is a port to something rendering
     * text. When a pipe becomes a thing this has to ask the stream what it is.
     */
    return 1;
}

off_t
lseek(int fd, off_t offset, int whence)
{
    PosixDescriptor *slot = entry(fd);

    if (slot == NULL) {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == POSIX_DESCRIPTOR_FILE && file_ops != NULL)
        return file_ops->seek(slot->value, offset, whence);
    errno = ESPIPE;
    return -1;
}

void
_exit(int status)
{
    astra_process_exit((uint32_t)status);
    for (;;) {
    }
}
