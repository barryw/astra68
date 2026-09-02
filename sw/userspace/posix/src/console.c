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
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    POSIX_STDIN = 0,
    POSIX_STDOUT = 1,
    POSIX_STDERR = 2
};

typedef enum PosixDescriptorKind {
    POSIX_DESCRIPTOR_FREE = 0,
    POSIX_DESCRIPTOR_STREAM,
    POSIX_DESCRIPTOR_FILE,
    POSIX_DESCRIPTOR_PIPE_READ,
    POSIX_DESCRIPTOR_PIPE_WRITE,
    POSIX_DESCRIPTOR_SOCKET
} PosixDescriptorKind;

/* A default allocation, not a ceiling. The containing charged area is the
 * actual bound, and the kernel accepts any fitting power-of-two capacity. */
#define POSIX_PIPE_CAPACITY 65536u

typedef struct PosixOpenDescription {
    uint8_t kind;
    uint8_t reserved[3];
    /* A stream capability, or the file half's slot. Never both. */
    uint32_t value;
    uint32_t read_wait;
    int status_flags;
    uint32_t references;
} PosixOpenDescription;

typedef struct PosixDescriptor {
    PosixOpenDescription *description;
    uint8_t descriptor_flags;
} PosixDescriptor;

#define POSIX_EXEC_MAGIC 0x50584543u
#define POSIX_EXEC_VERSION 2u

typedef struct PosixExecHeader {
    uint32_t magic;
    uint32_t total_size;
    uint32_t version;
    uint32_t file_state_offset;
    uint32_t file_state_size;
    uint32_t socket_state_offset;
    uint32_t socket_state_size;
    uint32_t descriptor_offset;
    uint32_t descriptor_count;
    uint32_t description_offset;
    uint32_t description_count;
} PosixExecHeader;

typedef struct PosixExecDescriptor {
    uint32_t fd;
    uint32_t description;
    uint32_t flags;
} PosixExecDescriptor;

typedef struct PosixExecDescription {
    uint32_t kind;
    uint32_t status_flags;
    uint32_t value;
    uint32_t read_wait;
    uint32_t state_offset;
    uint32_t state_size;
} PosixExecDescription;

/*
 * A free entry means the program was not granted that stream, or has not
 * opened anything there. It is not an error to lack one -- `status` is granted
 * none -- so the failure surfaces at the write rather than at startup.
 */
static PosixDescriptor initial_descriptors[3];
static PosixOpenDescription initial_descriptions[3];
static PosixDescriptor *descriptors = initial_descriptors;
static uint32_t descriptor_capacity = 3u;
static const AstraPosixFileOps *file_ops;
static const AstraPosixSocketOps *socket_ops;
static const AstraStartupInfo *startup_block;
static char *empty_environment[] = { NULL };
extern char **environ;

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

static PosixOpenDescription *
entry(int fd)
{
    if (fd < 0 || (uint32_t)fd >= descriptor_capacity ||
        descriptors[fd].description == NULL)
        return NULL;
    return descriptors[fd].description;
}

static int
stream_errno(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_INVALID_HANDLE: return EBADF;
    case ASTRA_SYSCALL_INVALID_ARGUMENT: return EINVAL;
    case ASTRA_SYSCALL_ACCESS_DENIED: return EACCES;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ENOMEM;
    case ASTRA_SYSCALL_CANCELLED: return EINTR;
    case ASTRA_SYSCALL_PEER_DEAD:
    case ASTRA_SYSCALL_CLOSED: return EPIPE;
    default: return EIO;
    }
}

#if defined(__GNUC__) && !defined(__clang__)
/* GCC analyzer bug 113990 mistakes realloc ownership returned to persistent
 * state for a leak. Keep every other analyzer diagnostic enabled here. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#endif
static int
grow_descriptors(uint32_t wanted)
{
    PosixDescriptor *grown;
    const uint32_t maximum_capacity =
        UINT32_MAX / (uint32_t)sizeof(*grown);
    uint32_t old_capacity = descriptor_capacity;
    uint32_t capacity = descriptor_capacity;
    int using_initial = descriptors == initial_descriptors;

    if (wanted <= capacity)
        return 1;
    if (wanted > maximum_capacity) {
        errno = EMFILE;
        return 0;
    }

    while (capacity < wanted) {
        capacity = capacity > maximum_capacity / 2u ?
            wanted : capacity * 2u;
    }
    if (using_initial) {
        grown = calloc(capacity, sizeof(*grown));
        if (grown == NULL) {
            errno = ENOMEM;
            return 0;
        }
        (void)memcpy(grown, descriptors,
                     (size_t)old_capacity * sizeof(*grown));
    } else {
        grown = realloc(descriptors,
                        (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            errno = ENOMEM;
            return 0;
        }
    }
    (void)memset(&grown[old_capacity], 0,
                 (size_t)(capacity - old_capacity) * sizeof(*grown));
    descriptors = grown;
    descriptor_capacity = capacity;
    return 1;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void
astra_posix_file_bind(const AstraPosixFileOps *ops)
{
    file_ops = ops;
}

void
astra_posix_socket_bind(const AstraPosixSocketOps *ops)
{
    socket_ops = ops;
}

static int
claim_descriptor(uint32_t minimum)
{
    uint32_t fd;

    if (minimum > (uint32_t)INT_MAX) {
        errno = EINVAL;
        return -1;
    }
    for (fd = minimum; fd < descriptor_capacity; ++fd)
        if (descriptors[fd].description == NULL)
            return (int)fd;
    if (!grow_descriptors(fd + 1u))
        return -1;
    return (int)fd;
}

static int descriptor_native(PosixDescriptorKind kind, uint32_t handle,
                             int flags);

static int
release_description(PosixOpenDescription *description)
{
    int result = 0;
    int is_initial = 0;

    if (description == NULL)
        return 0;
    if (description->kind == POSIX_DESCRIPTOR_FILE && file_ops != NULL)
        result = file_ops->close(description->value);
    else if (description->kind == POSIX_DESCRIPTOR_SOCKET &&
             socket_ops != NULL)
        result = socket_ops->close(description->value);
    else if (description->value != 0u &&
             astra_close(description->value) != ASTRA_SYSCALL_OK) {
        errno = EIO;
        result = -1;
    }
    if (description->read_wait != 0u)
        (void)astra_close(description->read_wait);
    for (uint32_t index = 0u; index < 3u; ++index)
        if (description == &initial_descriptions[index])
            is_initial = 1;
    if (!is_initial)
        free(description);
    else
        (void)memset(description, 0, sizeof(*description));
    return result;
}

static int
release_descriptor(PosixDescriptor *descriptor)
{
    PosixOpenDescription *description = descriptor->description;

    if (description == NULL)
        return 0;
    descriptor->description = NULL;
    descriptor->descriptor_flags = 0u;
    if (--description->references != 0u)
        return 0;
    return release_description(description);
}

const AstraStartupInfo *
astra_posix_startup(void)
{
    return startup_block;
}

int
astra_posix_descriptor_file(uint32_t slot, int flags)
{
    PosixOpenDescription *description;
    int fd = claim_descriptor(0u);

    if (fd < 0)
        return -1;
    description = calloc(1u, sizeof(*description));
    if (description == NULL) {
        errno = ENOMEM;
        return -1;
    }
    description->kind = POSIX_DESCRIPTOR_FILE;
    description->value = slot;
    description->status_flags = flags;
    description->references = 1u;
    descriptors[fd].description = description;
    return fd;
}

int
astra_posix_descriptor_socket(uint32_t slot, int flags)
{
    int fd = descriptor_native(POSIX_DESCRIPTOR_SOCKET, slot, flags);

    if (fd < 0)
        return -1;
    return fd;
}

static int
descriptor_native(PosixDescriptorKind kind, uint32_t handle, int flags)
{
    PosixOpenDescription *description;
    int fd = claim_descriptor(0u);

    if (fd < 0)
        return -1;
    description = calloc(1u, sizeof(*description));
    if (description == NULL) {
        errno = ENOMEM;
        return -1;
    }
    description->kind = (uint8_t)kind;
    description->value = handle;
    description->status_flags = flags;
    description->references = 1u;
    descriptors[fd].description = description;
    return fd;
}

int
pipe(int fildes[2])
{
    const uint32_t area_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_ADMINISTER;
    uint32_t area = 0u;
    uint32_t producer = 0u;
    uint32_t consumer = 0u;
    uint32_t status;
    int read_fd;
    int write_fd;

    if (fildes == NULL) {
        errno = EFAULT;
        return -1;
    }
    status = astra_rt_area_create(
        ASTRA_BULK_RING_HEADER_SIZE + POSIX_PIPE_CAPACITY, area_rights, &area);
    if (status != ASTRA_SYSCALL_OK) {
        errno = stream_errno(status);
        return -1;
    }
    status = astra_rt_ring_create(
        area, 0u, 1u, POSIX_PIPE_CAPACITY,
        ASTRA_BULK_RING_CREATE_KERNEL_COPY, &producer, &consumer);
    if (astra_close(area) != ASTRA_SYSCALL_OK &&
        status == ASTRA_SYSCALL_OK)
        status = ASTRA_SYSCALL_IO_ERROR;
    if (status != ASTRA_SYSCALL_OK) {
        if (producer != 0u)
            (void)astra_close(producer);
        if (consumer != 0u)
            (void)astra_close(consumer);
        errno = stream_errno(status);
        return -1;
    }
    read_fd = descriptor_native(POSIX_DESCRIPTOR_PIPE_READ, consumer,
                                O_RDONLY);
    if (read_fd < 0) {
        (void)astra_close(producer);
        (void)astra_close(consumer);
        return -1;
    }
    write_fd = descriptor_native(POSIX_DESCRIPTOR_PIPE_WRITE, producer,
                                 O_WRONLY);
    if (write_fd < 0) {
        (void)close(read_fd);
        (void)astra_close(producer);
        return -1;
    }
    fildes[0] = read_fd;
    fildes[1] = write_fd;
    return 0;
}

int
astra_posix_descriptor_slot(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    if (slot == NULL || slot->kind != POSIX_DESCRIPTOR_FILE)
        return -1;
    return (int)slot->value;
}

int
astra_posix_descriptor_socket_slot(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    if (slot == NULL || slot->kind != POSIX_DESCRIPTOR_SOCKET)
        return -1;
    return (int)slot->value;
}

int
astra_posix_descriptor_flags(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    if (slot == NULL) {
        errno = EBADF;
        return -1;
    }
    return slot->status_flags;
}

uint32_t
astra_posix_descriptor_handle(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    return slot != NULL && slot->kind == POSIX_DESCRIPTOR_STREAM ?
        slot->value : 0u;
}

static int
align_offset(uint32_t value, uint32_t alignment, uint32_t *result)
{
    uint32_t mask = alignment - 1u;

    if (alignment == 0u || (alignment & mask) != 0u ||
        value > UINT32_MAX - mask)
        return 0;
    *result = (value + mask) & ~mask;
    return 1;
}

int
astra_posix_exec_export(void **state, uint32_t *size)
{
    PosixOpenDescription **unique = NULL;
    PosixExecHeader *header = NULL;
    PosixExecDescriptor *wire_descriptors = NULL;
    PosixExecDescription *wire_descriptions = NULL;
    uint32_t active = 0u;
    uint32_t unique_count = 0u;
    uint32_t file_size;
    uint32_t socket_size = 0u;
    uint32_t socket_description_size = 0u;
    uint32_t at;
    uint32_t total;
    uint32_t file_used = 0u;
    uint32_t socket_used = 0u;
    uint8_t *bytes = NULL;

    if (state == NULL || size == NULL || file_ops == NULL ||
        file_ops->exec_size == NULL || file_ops->exec_export == NULL ||
        file_ops->file_export == NULL) {
        errno = EINVAL;
        return -1;
    }
    *state = NULL;
    *size = 0u;
    for (uint32_t fd = 0u; fd < descriptor_capacity; ++fd)
        if (descriptors[fd].description != NULL &&
            (descriptors[fd].descriptor_flags & FD_CLOEXEC) == 0u)
            ++active;
    unique = calloc(active == 0u ? 1u : active, sizeof(*unique));
    if (unique == NULL) {
        errno = ENOMEM;
        return -1;
    }
    for (uint32_t fd = 0u; fd < descriptor_capacity; ++fd) {
        PosixOpenDescription *description = descriptors[fd].description;
        uint32_t index;

        if (description == NULL ||
            (descriptors[fd].descriptor_flags & FD_CLOEXEC) != 0u)
            continue;
        for (index = 0u; index < unique_count; ++index)
            if (unique[index] == description)
                break;
        if (index == unique_count)
            unique[unique_count++] = description;
    }
    file_size = file_ops->exec_size();
    if (socket_ops != NULL) {
        if (socket_ops->exec_size == NULL || socket_ops->socket_size == NULL ||
            socket_ops->exec_export == NULL ||
            socket_ops->socket_export == NULL) {
            errno = EINVAL;
            goto failed;
        }
        socket_size = socket_ops->exec_size();
        socket_description_size = socket_ops->socket_size();
    }
    if (file_size == 0u ||
        !align_offset((uint32_t)sizeof(*header) + file_size,
                      _Alignof(uint32_t), &at) ||
        socket_size > UINT32_MAX - at)
        goto overflow;
    at += socket_size;
    if (!align_offset(at, _Alignof(PosixExecDescriptor), &at) ||
        active > (UINT32_MAX - at) / sizeof(*wire_descriptors))
        goto overflow;
    at += active * (uint32_t)sizeof(*wire_descriptors);
    if (!align_offset(at, _Alignof(PosixExecDescription), &at) ||
        unique_count > (UINT32_MAX - at) / sizeof(*wire_descriptions))
        goto overflow;
    total = at + unique_count * (uint32_t)sizeof(*wire_descriptions);
    for (uint32_t index = 0u; index < unique_count; ++index) {
        uint32_t state_size = unique[index]->kind == POSIX_DESCRIPTOR_FILE ?
            (uint32_t)sizeof(AstraPosixFileExecState) :
            unique[index]->kind == POSIX_DESCRIPTOR_SOCKET ?
                socket_description_size : 0u;

        if (state_size > UINT32_MAX - total)
            goto overflow;
        total += state_size;
    }
    bytes = calloc(1u, total);
    if (bytes == NULL) {
        free(unique);
        errno = ENOMEM;
        return -1;
    }
    header = (PosixExecHeader *)(void *)bytes;
    header->magic = POSIX_EXEC_MAGIC;
    header->total_size = total;
    header->version = POSIX_EXEC_VERSION;
    header->file_state_offset = sizeof(*header);
    header->file_state_size = file_size;
    if (!align_offset((uint32_t)sizeof(*header) + file_size,
                      _Alignof(uint32_t), &header->socket_state_offset))
        goto failed;
    header->socket_state_size = socket_size;
    header->descriptor_offset =
        (header->socket_state_offset + socket_size +
         _Alignof(PosixExecDescriptor) - 1u) &
        ~((uint32_t)_Alignof(PosixExecDescriptor) - 1u);
    header->descriptor_count = active;
    header->description_offset = at;
    header->description_count = unique_count;
    wire_descriptors = (PosixExecDescriptor *)(void *)
        (bytes + header->descriptor_offset);
    wire_descriptions = (PosixExecDescription *)(void *)
        (bytes + header->description_offset);
    if (file_ops->exec_export(bytes + header->file_state_offset, file_size,
                              &file_used) < 0 || file_used != file_size)
        goto failed;
    if (socket_size != 0u &&
        (socket_ops->exec_export(bytes + header->socket_state_offset,
                                 socket_size, &socket_used) < 0 ||
         socket_used != socket_size))
        goto failed;

    at = header->description_offset +
         unique_count * (uint32_t)sizeof(*wire_descriptions);
    for (uint32_t index = 0u; index < unique_count; ++index) {
        PosixOpenDescription *description = unique[index];
        PosixExecDescription *wire = &wire_descriptions[index];

        wire->kind = description->kind;
        wire->status_flags = (uint32_t)description->status_flags;
        wire->value = description->value;
        wire->read_wait = description->read_wait;
        wire->state_offset = at;
        if (description->kind == POSIX_DESCRIPTOR_FILE) {
            wire->state_size = sizeof(AstraPosixFileExecState);
            if (file_ops->file_export(description->value, bytes + at,
                                      wire->state_size) < 0)
                goto failed;
            at += wire->state_size;
        } else if (description->kind == POSIX_DESCRIPTOR_SOCKET) {
            if (socket_ops == NULL || socket_description_size == 0u) {
                errno = EINVAL;
                goto failed;
            }
            wire->state_size = socket_description_size;
            if (socket_ops->socket_export(description->value, bytes + at,
                                          wire->state_size) < 0)
                goto failed;
            at += wire->state_size;
        }
    }
    active = 0u;
    for (uint32_t fd = 0u; fd < descriptor_capacity; ++fd) {
        PosixOpenDescription *description = descriptors[fd].description;
        uint32_t index;

        if (description == NULL)
            continue;
        if ((descriptors[fd].descriptor_flags & FD_CLOEXEC) != 0u)
            continue;
        /* ponytail: exec is cold; replace this scan with a pointer hash only
         * if descriptor-heavy exec is measured hot. */
        for (index = 0u; index < unique_count; ++index)
            if (unique[index] == description)
                break;
        if (index == unique_count)
            goto failed;
        wire_descriptors[active].fd = fd;
        wire_descriptors[active].description = index;
        wire_descriptors[active].flags = descriptors[fd].descriptor_flags;
        ++active;
    }
    free(unique);
    *state = bytes;
    *size = total;
    return 0;

overflow:
    free(unique);
    errno = EOVERFLOW;
    return -1;

failed:
    free(unique);
    free(bytes);
    return -1;
}

static int
restore_exec_descriptors(const AstraStartupInfo *startup)
{
    const uint8_t *bytes;
    const PosixExecHeader *header;
    const PosixExecDescriptor *wire_descriptors;
    const PosixExecDescription *wire_descriptions;
    PosixOpenDescription **restored = NULL;
    uint32_t maximum_fd = 0u;
    uint32_t expected;
    uint32_t state_at;

    if (startup == NULL || startup->handoff_size == 0u)
        return 0;
    if (startup->handoff_address == 0u || file_ops == NULL ||
        file_ops->exec_import == NULL || file_ops->file_import == NULL) {
        errno = EINVAL;
        return -1;
    }
    bytes = (const uint8_t *)(uintptr_t)startup->handoff_address;
    header = (const PosixExecHeader *)(const void *)bytes;
    if (startup->handoff_size < sizeof(*header) ||
        header->magic != POSIX_EXEC_MAGIC ||
        header->version != POSIX_EXEC_VERSION ||
        header->total_size != startup->handoff_size ||
        header->file_state_offset < sizeof(*header) ||
        header->file_state_offset > header->total_size ||
        header->file_state_size > header->total_size -
                                      header->file_state_offset ||
        header->socket_state_offset > header->total_size ||
        header->socket_state_size > header->total_size -
                                        header->socket_state_offset ||
        header->descriptor_offset > header->total_size ||
        header->descriptor_count >
            (header->total_size - header->descriptor_offset) /
                sizeof(PosixExecDescriptor) ||
        header->description_offset > header->total_size ||
        header->description_count >
            (header->total_size - header->description_offset) /
                sizeof(PosixExecDescription) ||
        (header->descriptor_offset &
         (_Alignof(PosixExecDescriptor) - 1u)) != 0u ||
        (header->description_offset &
         (_Alignof(PosixExecDescription) - 1u)) != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (header->file_state_offset != sizeof(*header) ||
        !align_offset((uint32_t)sizeof(*header) + header->file_state_size,
                      _Alignof(uint32_t), &expected) ||
        header->socket_state_offset != expected ||
        header->socket_state_size > UINT32_MAX - expected ||
        !align_offset(expected + header->socket_state_size,
                      _Alignof(PosixExecDescriptor), &expected) ||
        header->descriptor_offset != expected ||
        header->descriptor_count >
            (UINT32_MAX - expected) / sizeof(PosixExecDescriptor)) {
        errno = EINVAL;
        return -1;
    }
    expected += header->descriptor_count *
                (uint32_t)sizeof(PosixExecDescriptor);
    if (!align_offset(expected, _Alignof(PosixExecDescription), &expected) ||
        header->description_offset != expected ||
        header->description_count >
            (UINT32_MAX - expected) / sizeof(PosixExecDescription)) {
        errno = EINVAL;
        return -1;
    }
    state_at = expected + header->description_count *
                          (uint32_t)sizeof(PosixExecDescription);
    wire_descriptors = (const PosixExecDescriptor *)(const void *)
        (bytes + header->descriptor_offset);
    wire_descriptions = (const PosixExecDescription *)(const void *)
        (bytes + header->description_offset);
    if (file_ops->exec_import(
            startup, bytes + header->file_state_offset,
            header->file_state_size) < 0)
        return -1;
    if (header->socket_state_size != 0u) {
        if (socket_ops == NULL || socket_ops->exec_import == NULL ||
            socket_ops->socket_size == NULL ||
            socket_ops->socket_import == NULL ||
            socket_ops->exec_import(bytes + header->socket_state_offset,
                                    header->socket_state_size) < 0)
            return -1;
    }
    if (header->description_count != 0u) {
        restored = calloc(header->description_count, sizeof(*restored));
        if (restored == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }
    for (uint32_t index = 0u; index < header->description_count; ++index) {
        const PosixExecDescription *wire = &wire_descriptions[index];
        PosixOpenDescription *description;

        if (wire->kind < POSIX_DESCRIPTOR_STREAM ||
            wire->kind > POSIX_DESCRIPTOR_SOCKET ||
            wire->state_offset != state_at ||
            wire->state_offset > header->total_size ||
            wire->state_size > header->total_size - wire->state_offset) {
            errno = EINVAL;
            goto failed;
        }
        state_at += wire->state_size;
        description = calloc(1u, sizeof(*description));
        if (description == NULL) {
            errno = ENOMEM;
            goto failed;
        }
        description->kind = (uint8_t)wire->kind;
        description->status_flags = (int)wire->status_flags;
        description->value = wire->value;
        description->read_wait = wire->read_wait;
        if (description->kind == POSIX_DESCRIPTOR_FILE) {
            if (wire->state_size != sizeof(AstraPosixFileExecState) ||
                file_ops->file_import(bytes + wire->state_offset,
                                      wire->state_size,
                                      &description->value) < 0) {
                free(description);
                goto failed;
            }
        } else if (description->kind == POSIX_DESCRIPTOR_SOCKET) {
            if (socket_ops == NULL ||
                wire->state_size != socket_ops->socket_size() ||
                socket_ops->socket_import(bytes + wire->state_offset,
                                          wire->state_size,
                                          &description->value) < 0) {
                free(description);
                goto failed;
            }
        } else if (wire->state_size != 0u) {
            errno = EINVAL;
            free(description);
            goto failed;
        }
        restored[index] = description;
    }
    if (state_at != header->total_size) {
        errno = EINVAL;
        goto failed;
    }
    for (uint32_t index = 0u; index < header->descriptor_count; ++index) {
        const PosixExecDescriptor *wire = &wire_descriptors[index];

        if (wire->fd > (uint32_t)INT_MAX ||
            wire->description >= header->description_count ||
            (wire->flags & ~FD_CLOEXEC) != 0u) {
            errno = EINVAL;
            goto failed;
        }
        if (wire->fd > maximum_fd)
            maximum_fd = wire->fd;
    }
    if (header->descriptor_count != 0u &&
        !grow_descriptors(maximum_fd + 1u))
        goto failed;
    for (uint32_t index = 0u; index < header->descriptor_count; ++index) {
        const PosixExecDescriptor *wire = &wire_descriptors[index];
        PosixOpenDescription *description = restored[wire->description];

        if (descriptors[wire->fd].description != NULL) {
            errno = EINVAL;
            goto failed;
        }
        if ((wire->flags & FD_CLOEXEC) != 0u)
            continue;
        descriptors[wire->fd].description = description;
        descriptors[wire->fd].descriptor_flags = (uint8_t)wire->flags;
        ++description->references;
    }
    for (uint32_t index = 0u; index < header->description_count; ++index)
        if (restored[index]->references == 0u)
            (void)release_description(restored[index]);
    free(restored);
    return 1;

failed:
    for (uint32_t fd = 0u; fd < descriptor_capacity; ++fd) {
        descriptors[fd].description = NULL;
        descriptors[fd].descriptor_flags = 0u;
    }
    for (uint32_t index = 0u; index < header->description_count; ++index)
        if (restored[index] != NULL)
            (void)release_description(restored[index]);
    free(restored);
    return -1;
}

void
astra_posix_start(const AstraStartupInfo *startup)
{
    static const char *const standard_names[] = {
        "STDIN", "STDOUT", "STDERR"
    };

    for (uint32_t fd = 0u; fd < descriptor_capacity; ++fd)
        (void)release_descriptor(&descriptors[fd]);
    if (descriptors != initial_descriptors)
        free(descriptors);
    descriptors = initial_descriptors;
    descriptor_capacity = 3u;
    (void)memset(descriptors, 0, sizeof(initial_descriptors));
    (void)memset(initial_descriptions, 0, sizeof(initial_descriptions));
    startup_block = startup;
    environ = empty_environment;
    if (startup != NULL && startup->environment_count != 0u &&
        startup->environment_address != 0u)
        environ = (char **)(uintptr_t)startup->environment_address;
    if (restore_exec_descriptors(startup) != 0)
        return;
    if (!astra_startup_validate(startup))
        return;
    for (uint32_t fd = 0u; fd < 3u; ++fd) {
        const AstraStartupCapability *capability =
            astra_startup_capability(startup, standard_names[fd]);

        if (capability == NULL || capability->handle == 0u)
            continue;
        initial_descriptions[fd].kind = POSIX_DESCRIPTOR_STREAM;
        initial_descriptions[fd].value = capability->handle;
        initial_descriptions[fd].status_flags = fd == POSIX_STDIN ?
            O_RDONLY : O_WRONLY;
        initial_descriptions[fd].references = 1u;
        descriptors[fd].description = &initial_descriptions[fd];
    }
    /*
     * A program with STDOUT and no STDERR gets its diagnostics on STDOUT
     * rather than losing them. Unix would have given it both; the manifest
     * that granted one and not the other did not mean silence.
     */
    if (descriptors[POSIX_STDERR].description == NULL &&
        descriptors[POSIX_STDOUT].description != NULL) {
        descriptors[POSIX_STDERR].description =
            descriptors[POSIX_STDOUT].description;
        ++descriptors[POSIX_STDERR].description->references;
    }
}

ssize_t
write(int fd, const void *bytes, size_t length)
{
    PosixOpenDescription *slot = entry(fd);
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
    if (slot->kind == POSIX_DESCRIPTOR_SOCKET) {
        if (socket_ops == NULL) {
            errno = EBADF;
            return -1;
        }
        return socket_ops->write(slot->value, bytes, length,
                                 slot->status_flags);
    }
    if (slot->kind == POSIX_DESCRIPTOR_PIPE_READ) {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == POSIX_DESCRIPTOR_PIPE_WRITE) {
        uint32_t request = length > ASTRA_BULK_RING_TRANSFER_MAX ?
            ASTRA_BULK_RING_TRANSFER_MAX : (uint32_t)length;
        uint32_t flags = length <= ASTRA_BULK_RING_TRANSFER_MAX ?
            ASTRA_BULK_RING_WRITE_ATOMIC : 0u;

        for (;;) {
            status = astra_rt_ring_write_try(slot->value, bytes, request,
                                             flags, &written);
            if (status == ASTRA_SYSCALL_OK)
                return (ssize_t)written;
            if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                errno = stream_errno(status);
                return -1;
            }
            if ((slot->status_flags & O_NONBLOCK) != 0) {
                errno = EAGAIN;
                return -1;
            }
            status = astra_wait_one(slot->value, ASTRA_DEADLINE_FOREVER,
                                    NULL);
            if (status != ASTRA_SYSCALL_OK) {
                errno = stream_errno(status);
                return -1;
            }
        }
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
            errno = stream_errno(status);
            return -1;
        }
        if ((slot->status_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        status = astra_wait_one(slot->value, ASTRA_DEADLINE_FOREVER, NULL);
        if (status != ASTRA_SYSCALL_OK) {
            errno = stream_errno(status);
            return -1;
        }
    }
}

ssize_t
read(int fd, void *bytes, size_t length)
{
    PosixOpenDescription *slot = entry(fd);
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
    if (slot->kind == POSIX_DESCRIPTOR_SOCKET) {
        if (socket_ops == NULL) {
            errno = EBADF;
            return -1;
        }
        return socket_ops->read(slot->value, bytes, length,
                                slot->status_flags);
    }
    if (slot->kind == POSIX_DESCRIPTOR_PIPE_WRITE) {
        errno = EBADF;
        return -1;
    }
    if (slot->kind == POSIX_DESCRIPTOR_PIPE_READ) {
        uint32_t request = length > ASTRA_BULK_RING_TRANSFER_MAX ?
            ASTRA_BULK_RING_TRANSFER_MAX : (uint32_t)length;

        for (;;) {
            status = astra_rt_ring_read_try(slot->value, bytes, request,
                                            &taken);
            if (status == ASTRA_SYSCALL_OK)
                return (ssize_t)taken;
            if (status == ASTRA_SYSCALL_PEER_DEAD ||
                status == ASTRA_SYSCALL_CLOSED)
                return 0;
            if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                errno = stream_errno(status);
                return -1;
            }
            if ((slot->status_flags & O_NONBLOCK) != 0) {
                errno = EAGAIN;
                return -1;
            }
            status = astra_wait_one(slot->value, ASTRA_DEADLINE_FOREVER,
                                    NULL);
            if (status == ASTRA_SYSCALL_PEER_DEAD ||
                status == ASTRA_SYSCALL_CLOSED)
                continue;
            if (status != ASTRA_SYSCALL_OK) {
                errno = stream_errno(status);
                return -1;
            }
        }
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
        uint32_t flags = 0u;

        status = astra_stream_read_ex(slot->value, bytes, (uint32_t)length,
                                      &taken, &flags);
        if (status != ASTRA_SYSCALL_OK) {
            errno = stream_errno(status);
            return status == ASTRA_SYSCALL_PEER_DEAD ||
                           status == ASTRA_SYSCALL_CLOSED ? 0 : -1;
        }
        if (taken != 0u)
            return (ssize_t)taken;
        if ((flags & ASTRA_STREAM_DATA_EOF) != 0u)
            return 0;
        if ((slot->status_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        if (slot->read_wait == 0u) {
            uint32_t events = 0u;

            status = astra_stream_read_wait(slot->value, &slot->read_wait,
                                            &events);
            if (status != ASTRA_SYSCALL_OK || slot->read_wait == 0u) {
                errno = stream_errno(status);
                return status == ASTRA_SYSCALL_PEER_DEAD ||
                               status == ASTRA_SYSCALL_CLOSED ? 0 : -1;
            }
            if ((events & ASTRA_STREAM_READY_READ) != 0u)
                continue;
        }
        status = astra_wait_one(slot->read_wait, ASTRA_DEADLINE_FOREVER,
                                NULL);
        if (status != ASTRA_SYSCALL_OK) {
            errno = stream_errno(status);
            return status == ASTRA_SYSCALL_PEER_DEAD ||
                           status == ASTRA_SYSCALL_CLOSED ? 0 : -1;
        }
    }
}

int
close(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    if (slot == NULL) {
        errno = EBADF;
        return -1;
    }
    (void)slot;
    return release_descriptor(&descriptors[fd]);
}

static int
duplicate_descriptor(int oldfd, int minimum, int exact)
{
    PosixOpenDescription *source = entry(oldfd);
    int newfd;

    if (source == NULL) {
        errno = EBADF;
        return -1;
    }
    if (minimum < 0) {
        errno = EINVAL;
        return -1;
    }
    if (exact != 0 && oldfd == minimum)
        return oldfd;
    if (exact != 0) {
        if (!grow_descriptors((uint32_t)minimum + 1u))
            return -1;
        newfd = minimum;
        (void)release_descriptor(&descriptors[newfd]);
    } else {
        newfd = claim_descriptor((uint32_t)minimum);
        if (newfd < 0)
            return -1;
    }
    descriptors[newfd].description = source;
    descriptors[newfd].descriptor_flags = 0u;
    ++source->references;
    return newfd;
}

int
dup(int oldfd)
{
    return duplicate_descriptor(oldfd, 0, 0);
}

int
dup2(int oldfd, int newfd)
{
    return duplicate_descriptor(oldfd, newfd, 1);
}

int
fcntl(int fd, int command, ...)
{
    PosixOpenDescription *slot = entry(fd);
    va_list arguments;
    int value;

    if (slot == NULL) {
        errno = EBADF;
        return -1;
    }
    switch (command) {
    case F_DUPFD:
        va_start(arguments, command);
        value = va_arg(arguments, int);
        va_end(arguments);
        return duplicate_descriptor(fd, value, 0);
    case F_GETFD:
        return descriptors[fd].descriptor_flags;
    case F_SETFD:
        va_start(arguments, command);
        value = va_arg(arguments, int);
        va_end(arguments);
        if ((value & ~FD_CLOEXEC) != 0) {
            errno = EINVAL;
            return -1;
        }
        descriptors[fd].descriptor_flags = (uint8_t)value;
        return 0;
    case F_GETFL:
        return slot->status_flags;
    case F_SETFL:
        va_start(arguments, command);
        value = va_arg(arguments, int);
        va_end(arguments);
        slot->status_flags = (slot->status_flags & ~O_NONBLOCK) |
                             (value & O_NONBLOCK);
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int
astra_posix_descriptor_poll(int fd, short events, short *revents,
                            uint32_t handles[2], uint32_t *count)
{
    PosixOpenDescription *slot = entry(fd);
    int access;

    if (revents == NULL || handles == NULL || count == NULL) {
        errno = EINVAL;
        return -1;
    }
    *revents = 0;
    *count = 0u;
    if (slot == NULL) {
        *revents = POLLNVAL;
        return 0;
    }
    if (slot->kind == POSIX_DESCRIPTOR_PIPE_READ ||
        slot->kind == POSIX_DESCRIPTOR_PIPE_WRITE) {
        short requested = slot->kind == POSIX_DESCRIPTOR_PIPE_READ ?
            POLLIN : POLLOUT;

        if ((events & requested) != 0) {
            uint32_t status = astra_wait_one(
                slot->value, astra_clock_monotonic(), NULL);

            if (status == ASTRA_SYSCALL_OK)
                *revents |= requested;
            else if (status == ASTRA_SYSCALL_TIMED_OUT)
                handles[(*count)++] = slot->value;
            else if (status == ASTRA_SYSCALL_PEER_DEAD ||
                     status == ASTRA_SYSCALL_CLOSED)
                *revents |= slot->kind == POSIX_DESCRIPTOR_PIPE_READ ?
                    POLLHUP : (POLLERR | POLLHUP);
            else
                *revents |= POLLERR;
        }
        return 0;
    }
    access = slot->status_flags & O_ACCMODE;
    if (slot->kind == POSIX_DESCRIPTOR_FILE) {
        if ((events & POLLIN) != 0 && access != O_WRONLY)
            *revents |= POLLIN;
        if ((events & POLLOUT) != 0 && access != O_RDONLY)
            *revents |= POLLOUT;
        return 0;
    }
    if (slot->kind == POSIX_DESCRIPTOR_SOCKET) {
        if (socket_ops == NULL) {
            *revents = POLLNVAL;
            return 0;
        }
        return socket_ops->poll(slot->value, events, revents, handles, count);
    }
    if ((events & POLLIN) != 0 && access != O_WRONLY) {
        uint32_t status;

        if (slot->read_wait == 0u) {
            uint32_t ready = 0u;

            status = astra_stream_read_wait(slot->value, &slot->read_wait,
                                            &ready);
            if (status == ASTRA_SYSCALL_PEER_DEAD ||
                status == ASTRA_SYSCALL_CLOSED) {
                *revents |= POLLHUP;
            } else if (status != ASTRA_SYSCALL_OK ||
                       slot->read_wait == 0u) {
                errno = status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ENOMEM : EIO;
                return -1;
            } else if ((ready & ASTRA_STREAM_READY_READ) != 0u) {
                *revents |= POLLIN;
            }
        }
        if (slot->read_wait != 0u && (*revents & POLLIN) == 0) {
            status = astra_wait_one(slot->read_wait,
                                    astra_clock_monotonic(), NULL);
            if (status == ASTRA_SYSCALL_OK)
                *revents |= POLLIN;
            else if (status == ASTRA_SYSCALL_TIMED_OUT)
                handles[(*count)++] = slot->read_wait;
            else if (status == ASTRA_SYSCALL_PEER_DEAD ||
                     status == ASTRA_SYSCALL_CLOSED)
                *revents |= POLLHUP;
            else
                *revents |= POLLERR;
        }
    }
    if ((events & POLLOUT) != 0 && access != O_RDONLY) {
        uint32_t status = astra_wait_one(slot->value,
                                         astra_clock_monotonic(), NULL);

        if (status == ASTRA_SYSCALL_OK)
            *revents |= POLLOUT;
        else if (status == ASTRA_SYSCALL_TIMED_OUT)
            handles[(*count)++] = slot->value;
        else if (status == ASTRA_SYSCALL_PEER_DEAD ||
                 status == ASTRA_SYSCALL_CLOSED)
            *revents |= POLLERR | POLLHUP;
        else
            *revents |= POLLERR;
    }
    return 0;
}

int
isatty(int fd)
{
    PosixOpenDescription *slot = entry(fd);

    if (slot == NULL) {
        errno = EBADF;
        return 0;
    }
    if (slot->kind != POSIX_DESCRIPTOR_STREAM) {
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
    PosixOpenDescription *slot = entry(fd);

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
