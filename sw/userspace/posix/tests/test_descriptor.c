#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/syscall.h>

static uint32_t file_closes;
static char pipe_bytes[128];
static uint32_t pipe_head;
static uint32_t pipe_tail;
static int pipe_producer_closed;
static int pipe_consumer_closed;
static uint32_t file_imports;
static uint32_t socket_imports;

static uint32_t file_exec_size(void) { return sizeof(uint32_t); }
static int file_exec_export(void *state, uint32_t capacity, uint32_t *used)
{
    assert(capacity == sizeof(uint32_t));
    *(uint32_t *)state = 0x56465331u;
    *used = sizeof(uint32_t);
    return 0;
}
static int file_exec_import(const AstraStartupInfo *startup,
                            const void *state, uint32_t size)
{
    assert(startup != NULL && size == sizeof(uint32_t));
    assert(*(const uint32_t *)state == 0x56465331u);
    return 0;
}
static int file_state_export(uint32_t slot, void *state, uint32_t size)
{
    AstraPosixFileExecState *wire = state;

    assert(slot == 99u && size == sizeof(*wire));
    *wire = (AstraPosixFileExecState){ .file = slot };
    return 0;
}
static int file_state_import(const void *state, uint32_t size, uint32_t *slot)
{
    const AstraPosixFileExecState *wire = state;

    assert(size == sizeof(*wire) && wire->file == 99u);
    *slot = wire->file;
    ++file_imports;
    return 0;
}

static int
file_close(uint32_t slot)
{
    assert(slot == 99u);
    ++file_closes;
    return 0;
}

static const AstraPosixFileOps file_ops = {
    .close = file_close,
    .exec_size = file_exec_size,
    .exec_export = file_exec_export,
    .exec_import = file_exec_import,
    .file_export = file_state_export,
    .file_import = file_state_import
};

static ssize_t socket_read(uint32_t slot, void *bytes, size_t length, int flags)
{
    (void)slot; (void)bytes; (void)length; (void)flags;
    return 0;
}

static ssize_t socket_write(uint32_t slot, const void *bytes, size_t length,
                            int flags)
{
    (void)slot; (void)bytes; (void)flags;
    return (ssize_t)length;
}

static int socket_close(uint32_t slot) { (void)slot; return 0; }
static int socket_poll(uint32_t slot, short events, short *revents,
                       uint32_t handles[2], uint32_t *count)
{
    (void)slot; (void)events; (void)revents; (void)handles; (void)count;
    return 0;
}
static uint32_t socket_exec_size(void) { return sizeof(uint32_t); }
static uint32_t socket_state_size(void) { return sizeof(uint32_t); }
static int socket_exec_export(void *state, uint32_t capacity, uint32_t *used)
{
    assert(capacity == sizeof(uint32_t));
    *(uint32_t *)state = 0x534f434bu;
    *used = sizeof(uint32_t);
    return 0;
}
static int socket_exec_import(const void *state, uint32_t size)
{
    assert(size == sizeof(uint32_t));
    assert(*(const uint32_t *)state == 0x534f434bu);
    ++socket_imports;
    return 0;
}
static int socket_state_export(uint32_t slot, void *state, uint32_t size)
{
    assert(size == sizeof(uint32_t));
    *(uint32_t *)state = slot;
    return 0;
}
static int socket_state_import(const void *state, uint32_t size,
                               uint32_t *slot)
{
    assert(size == sizeof(uint32_t));
    *slot = *(const uint32_t *)state;
    return 0;
}
static const AstraPosixSocketOps socket_ops = {
    .read = socket_read,
    .write = socket_write,
    .close = socket_close,
    .poll = socket_poll,
    .exec_size = socket_exec_size,
    .socket_size = socket_state_size,
    .exec_export = socket_exec_export,
    .exec_import = socket_exec_import,
    .socket_export = socket_state_export,
    .socket_import = socket_state_import
};

uint32_t astra_yield(void) { return 0u; }
uint32_t astra_close(uint32_t handle)
{
    if (handle == 81u)
        pipe_producer_closed = 1;
    else if (handle == 82u)
        pipe_consumer_closed = 1;
    return 0u;
}
uint32_t
astra_rt_area_create(uint32_t size, uint32_t rights, uint32_t *handle)
{
    assert(size == ASTRA_BULK_RING_HEADER_SIZE + 65536u);
    assert(rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                      ASTRA_RIGHT_ADMINISTER));
    *handle = 80u;
    return ASTRA_SYSCALL_OK;
}
uint32_t
astra_rt_ring_create(uint32_t area, uint32_t offset, uint32_t element_size,
                     uint32_t capacity, uint32_t flags, uint32_t *producer,
                     uint32_t *consumer)
{
    assert(area == 80u && offset == 0u && element_size == 1u);
    assert(capacity == 65536u);
    assert(flags == ASTRA_BULK_RING_CREATE_KERNEL_COPY);
    *producer = 81u;
    *consumer = 82u;
    pipe_head = 0u;
    pipe_tail = 0u;
    pipe_producer_closed = 0;
    pipe_consumer_closed = 0;
    return ASTRA_SYSCALL_OK;
}
uint32_t
astra_rt_ring_write_try(uint32_t producer, const void *bytes, uint32_t length,
                        uint32_t flags, uint32_t *written)
{
    assert(producer == 81u && !pipe_consumer_closed);
    assert((flags & ~ASTRA_BULK_RING_WRITE_ATOMIC) == 0u);
    assert(pipe_tail + length <= sizeof(pipe_bytes));
    memcpy(&pipe_bytes[pipe_tail], bytes, length);
    pipe_tail += length;
    *written = length;
    return ASTRA_SYSCALL_OK;
}
uint32_t
astra_rt_ring_read_try(uint32_t consumer, void *bytes, uint32_t capacity,
                       uint32_t *copied)
{
    uint32_t available;

    assert(consumer == 82u);
    available = pipe_tail - pipe_head;
    if (available == 0u)
        return pipe_producer_closed ? ASTRA_SYSCALL_PEER_DEAD :
                                      ASTRA_SYSCALL_WOULD_BLOCK;
    if (capacity > available)
        capacity = available;
    memcpy(bytes, &pipe_bytes[pipe_head], capacity);
    pipe_head += capacity;
    *copied = capacity;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)handle;
    (void)deadline;
    (void)detail;
    return ASTRA_SYSCALL_TIMED_OUT;
}

uint32_t
astra_stream_write(uint32_t handle, const void *bytes, uint32_t length,
                   uint32_t *written)
{
    (void)handle;
    (void)bytes;
    *written = length;
    return 0u;
}

uint32_t
astra_stream_read(uint32_t handle, void *bytes, uint32_t capacity,
                  uint32_t *length)
{
    (void)handle;
    (void)bytes;
    (void)capacity;
    *length = 0u;
    return 0u;
}

uint32_t
astra_stream_read_ex(uint32_t handle, void *bytes, uint32_t capacity,
                     uint32_t *length, uint32_t *flags)
{
    *flags = 0u;
    return astra_stream_read(handle, bytes, capacity, length);
}

uint32_t
astra_stream_read_wait(uint32_t handle, uint32_t *wait_handle,
                       uint32_t *events)
{
    (void)handle;
    *wait_handle = 1u;
    *events = 0u;
    return ASTRA_SYSCALL_OK;
}

void
astra_process_exit(uint32_t status)
{
    (void)status;
    for (;;) {
    }
}

int
main(void)
{
    char bytes[16];
    int fildes[2];
    int duplicate;
    void *handoff;
    uint32_t handoff_size;
    ssize_t (*read_call)(int, void *, size_t) = read;
    ssize_t (*write_call)(int, const void *, size_t) = write;

    astra_posix_start(NULL);
    for (uint32_t slot = 0u; slot < 64u; ++slot) {
        int fd = astra_posix_descriptor_file(slot, 0);

        assert(fd == (int)slot);
        assert(astra_posix_descriptor_slot(fd) == (int)slot);
    }
    astra_posix_start(NULL);
    astra_posix_file_bind(&file_ops);
    assert(astra_posix_descriptor_file(99u, 0) == 0);
    assert(dup(0) == 1);
    assert(astra_posix_descriptor_slot(1) == 99);
    assert(fcntl(0, F_SETFL, O_NONBLOCK) == 0);
    assert((fcntl(1, F_GETFL) & O_NONBLOCK) != 0);
    assert(fcntl(1, F_SETFD, FD_CLOEXEC) == 0);
    assert(fcntl(0, F_GETFD) == 0);
    assert(fcntl(1, F_GETFD) == FD_CLOEXEC);
    assert(dup2(0, 4) == 4);
    assert(astra_posix_descriptor_slot(4) == 99);
    assert(dup2(4, 4) == 4);
    assert(fcntl(0, F_DUPFD, 6) == 6);
    assert(close(0) == 0);
    assert(file_closes == 0u);
    assert(astra_posix_descriptor_slot(1) == 99);
    errno = 0;
    assert(dup(0) == -1 && errno == EBADF);
    assert(close(1) == 0 && close(4) == 0 && file_closes == 0u);
    assert(close(6) == 0 && file_closes == 1u);
    assert(pipe(fildes) == 0);
    assert(fildes[0] == 0 && fildes[1] == 1);
    errno = 0;
    assert(!isatty(fildes[0]) && errno == ENOTTY);
    errno = 0;
    assert(write_call(fildes[0], "x", 1u) == -1 && errno == EBADF);
    assert(write_call(fildes[1], "WORK:notes.txt", 14u) == 14);
    assert(read_call(fildes[0], bytes, 5u) == 5);
    assert(memcmp(bytes, "WORK:", 5u) == 0);
    duplicate = dup(fildes[1]);
    assert(duplicate == 2);
    assert(close(fildes[1]) == 0 && !pipe_producer_closed);
    assert(write_call(duplicate, "!", 1u) == 1);
    assert(close(duplicate) == 0 && pipe_producer_closed);
    assert(read_call(fildes[0], bytes, sizeof(bytes)) == 10);
    assert(memcmp(bytes, "notes.txt!", 10u) == 0);
    assert(read_call(fildes[0], bytes, sizeof(bytes)) == 0);
    assert(close(fildes[0]) == 0 && pipe_consumer_closed);

    astra_posix_start(NULL);
    astra_posix_file_bind(&file_ops);
    assert(astra_posix_descriptor_file(99u, O_NONBLOCK) == 0);
    assert(dup(0) == 1 && dup(0) == 2);
    assert(fcntl(1, F_SETFD, FD_CLOEXEC) == 0);
    assert(astra_posix_exec_export(&handoff, &handoff_size) == 0);
    /* ASan deliberately allocates above the target's 32-bit address space.
     * The ordinary non-PIE host gate exercises import; the sanitizer still
     * checks the complete exporter and owns/frees its result. */
    if ((uintptr_t)handoff <= UINT32_MAX) {
        AstraStartupInfo startup = {
            .handoff_address = (uint32_t)(uintptr_t)handoff,
            .handoff_size = handoff_size
        };

        astra_posix_start(&startup);
        assert(file_imports == 1u);
        assert(astra_posix_descriptor_slot(0) == 99);
        assert(astra_posix_descriptor_slot(2) == 99);
        errno = 0;
        assert(fcntl(1, F_GETFD) == -1 && errno == EBADF);
        assert((fcntl(0, F_GETFL) & O_NONBLOCK) != 0);
        file_closes = 0u;
        assert(close(0) == 0 && file_closes == 0u);
        assert(close(2) == 0 && file_closes == 1u);
    }
    free(handoff);

    astra_posix_start(NULL);
    astra_posix_file_bind(&file_ops);
    astra_posix_socket_bind(&socket_ops);
    assert(astra_posix_descriptor_socket(77u, O_NONBLOCK) == 0);
    assert(dup(0) == 1);
    assert(fcntl(1, F_SETFD, FD_CLOEXEC) == 0);
    assert(astra_posix_exec_export(&handoff, &handoff_size) == 0);
    if ((uintptr_t)handoff <= UINT32_MAX) {
        AstraStartupInfo startup = {
            .handoff_address = (uint32_t)(uintptr_t)handoff,
            .handoff_size = handoff_size
        };

        astra_posix_start(&startup);
        assert(socket_imports == 1u);
        assert(astra_posix_descriptor_socket_slot(0) == 77);
        assert(astra_posix_descriptor_socket_slot(1) == -1);
    }
    free(handoff);
    return 0;
}
