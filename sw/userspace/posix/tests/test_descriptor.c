#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <astra/event.h>
#include <astra/log_ring.h>
#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/posix_process.h>
#include <astra/status.h>
#include <astra/stream.h>
#include <astra/syscall.h>

ssize_t pread(int fd, void *buffer, size_t length, off_t offset);
ssize_t pwrite(int fd, const void *buffer, size_t length, off_t offset);

static uint32_t file_closes;
static char pipe_bytes[128];
static uint32_t pipe_head;
static uint32_t pipe_tail;
static int pipe_producer_closed;
static int pipe_consumer_closed;
static uint32_t file_imports;
static uint32_t socket_imports;
static uint32_t restore_failures;
static uint32_t restore_status;
static uint32_t process_service;
static uint32_t stream_read_handle;
static uint32_t stream_write_handle;
static uint32_t stream_write_limit;
static uint32_t stream_write_calls;
static uint32_t stream_write_cancel_once;
static off_t positioned_offset;

/* Lines a program without streams writes to stdout/stderr go to the log. */
static struct { uint32_t level, stream; char text[512]; } logged[32];
static uint32_t logged_count;

uint32_t
astra_log_line(uint32_t level, uint32_t stream, const char *text,
               uint32_t length)
{
    assert(logged_count < 32u && length < 512u);
    logged[logged_count].level = level;
    logged[logged_count].stream = stream;
    memcpy(logged[logged_count].text, text, length);
    logged[logged_count].text[length] = '\0';
    ++logged_count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    assert(strcmp(operation, "POSIX descriptor restore") == 0);
    ++restore_failures;
    restore_status = status;
    return ASTRA_SYSCALL_OK;
}

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

static ssize_t
file_pread(uint32_t slot, void *bytes, size_t length, off_t offset)
{
    assert(slot == 99u && length == 1u);
    positioned_offset = offset;
    *(char *)bytes = 'R';
    return 1;
}

static ssize_t
file_pwrite(uint32_t slot, const void *bytes, size_t length, off_t offset)
{
    assert(slot == 99u && length == 1u && *(const char *)bytes == 'W');
    positioned_offset = offset;
    return 1;
}

static const AstraPosixFileOps file_ops = {
    .pread = file_pread,
    .pwrite = file_pwrite,
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

/* signal.c's restart rule is its own test's subject. */
uint32_t posix_wait_one(uint32_t handle, uint64_t deadline)
{
    return astra_wait_one(handle, deadline, NULL);
}

uint32_t
astra_stream_write(uint32_t handle, const void *bytes, uint32_t length,
                   uint32_t *written)
{
    stream_write_handle = handle;
    (void)bytes;
    ++stream_write_calls;
    if (stream_write_cancel_once != 0u) {
        --stream_write_cancel_once;
        *written = 0u;
        return ASTRA_SYSCALL_CANCELLED;
    }
    if (stream_write_limit != 0u && length > stream_write_limit)
        length = stream_write_limit;
    *written = length;
    return 0u;
}

uint32_t
astra_stream_read(uint32_t handle, void *bytes, uint32_t capacity,
                  uint32_t *length)
{
    stream_read_handle = handle;
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

uint32_t
astra_stream_identity(uint32_t handle, AstraStreamIdentity *identity)
{
    *identity = (AstraStreamIdentity){
        .kind = ASTRA_STREAM_KIND_TERMINAL,
        .directions = handle == 10u ? ASTRA_STREAM_DIRECTION_READ :
                                      ASTRA_STREAM_DIRECTION_WRITE,
        .object_id = 100u,
    };
    return handle >= 10u && handle <= 12u ? ASTRA_SYSCALL_OK :
                                            ASTRA_SYSCALL_INVALID_ARGUMENT;
}

uint32_t astra_posix_process_service(void) { return process_service; }

uint32_t
astra_posix_process_tty_foreground(uint32_t service,
                                   AstraPosixProcessReply *reply)
{
    assert(service == 9u && reply != NULL);
    *reply = (AstraPosixProcessReply){.session = 100, .group = 100};
    return ASTRA_STATUS_OK;
}

void
astra_process_exit(uint32_t status)
{
    (void)status;
    for (;;) {
    }
}

static void
test_log_descriptors_without_streams(void)
{
    AstraStartupInfo startup = {
        .magic = ASTRA_STARTUP_MAGIC,
        .abi_version = ASTRA_STARTUP_ABI_VERSION,
        .program_entry = 0x00100000u,
        .header_size = ASTRA_STARTUP_INFO_SIZE,
        .total_size = ASTRA_STARTUP_INFO_SIZE,
        .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
        .capability_count = 0u,
    };

    logged_count = 0u;
    astra_posix_start(&startup);
    assert(!isatty(1) && !isatty(2));
    /* stderr arrives a byte at a time from picolibc; one record per line. */
    assert(write(2, "e", 1) == 1 && write(2, "rr\n", 3) == 3);
    assert(write(1, "out one\nout two\n", 16) == 16);
    assert(logged_count == 3u);
    assert(strcmp(logged[0].text, "err") == 0 &&
           logged[0].stream == ASTRA_LOG_STREAM_STDERR &&
           logged[0].level == ASTRA_EVENT_LEVEL_WARNING);
    assert(strcmp(logged[1].text, "out one") == 0 &&
           logged[1].stream == ASTRA_LOG_STREAM_STDOUT &&
           logged[1].level == ASTRA_EVENT_LEVEL_NOTICE);
    assert(strcmp(logged[2].text, "out two") == 0);
    /* Interleaved partial lines on the two streams stay separate. */
    assert(write(1, "par", 3) == 3 && write(2, "tial", 4) == 4);
    assert(logged_count == 3u);
    assert(write(1, "t\n", 2) == 2);
    assert(strcmp(logged[3].text, "part") == 0);
    /* Blank lines are kept: a program's blank line is part of its output. */
    assert(write(1, "\n", 1) == 1);
    assert(logged_count == 5u && logged[4].text[0] == '\0');
    /* A 300-byte line is emitted in 240-byte pieces. */
    {
        char line[300];
        memset(line, 'z', sizeof(line));
        assert(write(1, line, sizeof(line)) == 300);
        assert(logged_count == 6u && strlen(logged[5].text) == 240u);
    }
    /* _exit's flush emits what is pending on both streams. */
    astra_posix_log_flush();
    assert(logged_count == 8u);
    assert((strcmp(logged[6].text, "tial") == 0 &&
            strlen(logged[7].text) == 60u) ||
           (strcmp(logged[7].text, "tial") == 0 &&
            strlen(logged[6].text) == 60u));

    /* dup/dup2 of a LOG fd shares the description: the stream is preserved,
     * not reset to a default. */
    logged_count = 0u;
    assert(dup2(1, 5) == 5);
    assert(write(5, "dup\n", 4) == 4);
    assert(logged_count == 1u && strcmp(logged[0].text, "dup") == 0 &&
           logged[0].stream == ASTRA_LOG_STREAM_STDOUT &&
           logged[0].level == ASTRA_EVENT_LEVEL_NOTICE);
    assert(close(5) == 0);
    logged_count = 0u;
    assert(dup2(2, 6) == 6);
    assert(write(6, "dup2\n", 5) == 5);
    assert(logged_count == 1u && strcmp(logged[0].text, "dup2") == 0 &&
           logged[0].stream == ASTRA_LOG_STREAM_STDERR &&
           logged[0].level == ASTRA_EVENT_LEVEL_WARNING);
    assert(close(6) == 0);
}

static void
test_log_descriptors_survive_exec(void)
{
    AstraStartupInfo first = {
        .magic = ASTRA_STARTUP_MAGIC,
        .abi_version = ASTRA_STARTUP_ABI_VERSION,
        .program_entry = 0x00100000u,
        .header_size = ASTRA_STARTUP_INFO_SIZE,
        .total_size = ASTRA_STARTUP_INFO_SIZE,
        .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
        .capability_count = 0u,
    };
    void *handoff;
    uint32_t handoff_size;

    astra_posix_start(&first);
    astra_posix_file_bind(&file_ops);
    assert(!isatty(1) && !isatty(2));
    /* A partial line written before exec was written: exec replaces the
     * thread's buffer, so the export emits it first. */
    logged_count = 0u;
    assert(write(1, "before exec", 11) == 11);
    assert(logged_count == 0u);
    assert(astra_posix_exec_export(&handoff, &handoff_size) == 0);
    assert(logged_count == 1u && strcmp(logged[0].text, "before exec") == 0 &&
           logged[0].stream == ASTRA_LOG_STREAM_STDOUT);
    /* ASan deliberately allocates above the target's 32-bit address space.
     * The ordinary non-PIE host gate exercises import; the sanitizer still
     * checks the complete exporter and owns/frees its result. */
    if ((uintptr_t)handoff <= UINT32_MAX) {
        AstraStartupInfo startup = {
            .handoff_address = (uint32_t)(uintptr_t)handoff,
            .handoff_size = handoff_size
        };

        astra_posix_start(&startup);
        assert(!isatty(1) && !isatty(2));
        logged_count = 0u;
        assert(write(1, "o\n", 2) == 2);
        assert(write(2, "e\n", 2) == 2);
        assert(logged_count == 2u);
        assert(strcmp(logged[0].text, "o") == 0 &&
               logged[0].stream == ASTRA_LOG_STREAM_STDOUT &&
               logged[0].level == ASTRA_EVENT_LEVEL_NOTICE);
        assert(strcmp(logged[1].text, "e") == 0 &&
               logged[1].stream == ASTRA_LOG_STREAM_STDERR &&
               logged[1].level == ASTRA_EVENT_LEVEL_WARNING);
    }
    free(handoff);
}

int
main(void)
{
    char bytes[16];
    int fildes[2];
    int duplicate;
    void *handoff;
    uint32_t handoff_size;
    /* Exercise Astra validation without glibc folding deliberate bad inputs. */
    ssize_t (*volatile read_call)(int, void *, size_t) = read;
    ssize_t (*volatile write_call)(int, const void *, size_t) = write;

    {
        static const AstraStartupCapability capabilities[] = {
            {.name = "STDIN", .handle = 10u},
            {.name = "STDOUT", .handle = 11u},
            {.name = "STDERR", .handle = 12u},
        };
        AstraStartupInfo startup = {
            .magic = ASTRA_STARTUP_MAGIC,
            .abi_version = ASTRA_STARTUP_ABI_VERSION,
            .program_entry = 0x00100000u,
            .header_size = ASTRA_STARTUP_INFO_SIZE,
            .total_size = ASTRA_STARTUP_INFO_SIZE,
            .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
            .capability_count = 3u,
            .capabilities_address = (uint32_t)(uintptr_t)capabilities,
        };
        int tty;

        if ((uintptr_t)capabilities <= UINT32_MAX) {
            process_service = 9u;
            astra_posix_start(&startup);
            assert(isatty(0) && isatty(1) && isatty(2));
            tty = astra_posix_descriptor_controlling_terminal(
                O_RDWR | O_NONBLOCK);
            assert(tty == 3 &&
                   astra_posix_descriptor_terminal_id(tty) == 100u);
            assert(write_call(tty, "x", 1u) == 1 &&
                   stream_write_handle == 11u);
            stream_write_limit = 2u;
            stream_write_cancel_once = 1u;
            stream_write_calls = 0u;
            assert(astra_posix_write_all(1, "hello", 5u) == 0);
            assert(stream_write_calls == 4u);
            stream_write_limit = 0u;
            errno = 0;
            assert(read_call(tty, bytes, 1u) == -1 && errno == EAGAIN &&
                   stream_read_handle == 10u);
            assert(close(0) == 0);
            tty = astra_posix_descriptor_controlling_terminal(O_RDONLY);
            assert(tty == 0);
            assert(close(tty) == 0);
        }
        process_service = 0u;
        astra_posix_start(NULL);
    }

    astra_posix_start(NULL);
    for (uint32_t slot = 0u; slot < 64u; ++slot) {
        int fd = astra_posix_descriptor_file(slot, 0);

        assert(fd == (int)slot);
        assert(astra_posix_descriptor_slot(fd) == (int)slot);
    }
    astra_posix_start(NULL);
    astra_posix_file_bind(&file_ops);
    assert(astra_posix_descriptor_file(99u,
           O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC) == 0);
    assert(fcntl(0, F_GETFL) == O_RDWR);
    assert(fcntl(0, F_GETFD) == FD_CLOEXEC);
    assert(fcntl(0, F_SETFD, 0) == 0);
    assert(pread(0, bytes, 1u, 37) == 1 && bytes[0] == 'R' &&
           positioned_offset == 37);
    assert(pwrite(0, "W", 1u, 41) == 1 && positioned_offset == 41);
    errno = 0;
    assert(dup2(0, INT_MAX) == -1 && errno == EMFILE);
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
    assert(read_call(fildes[0], NULL, 0u) == 0);
    assert(write_call(fildes[1], NULL, 0u) == 0);
    errno = 0;
    assert(read_call(fildes[0], NULL, 1u) == -1 && errno == EFAULT);
    errno = 0;
    assert(write_call(fildes[1], NULL, 1u) == -1 && errno == EFAULT);
    errno = 0;
    assert(read_call(fildes[0], bytes, SIZE_MAX) == -1 && errno == EINVAL);
    errno = 0;
    assert(write_call(fildes[1], bytes, SIZE_MAX) == -1 && errno == EINVAL);
    errno = 0;
    assert(write_call(fildes[0], NULL, 0u) == -1 && errno == EBADF);
    errno = 0;
    assert(read_call(fildes[1], NULL, 0u) == -1 && errno == EBADF);
    errno = 0;
    assert(pwrite(fildes[0], "x", 1u, 0) == -1 && errno == EBADF);
    errno = 0;
    assert(pread(fildes[1], bytes, 1u, 0) == -1 && errno == EBADF);
    errno = 0;
    assert(!isatty(fildes[0]) && errno == ENOTTY);
    errno = 0;
    assert(write_call(fildes[0], "x", 1u) == -1 && errno == EBADF);
    errno = 0;
    assert(pread(fildes[0], bytes, 1u, 0) == -1 && errno == ESPIPE);
    assert(write_call(fildes[1], "/work/notes.txt", 15u) == 15);
    assert(read_call(fildes[0], bytes, 6u) == 6);
    assert(memcmp(bytes, "/work/", 6u) == 0);
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
    assert((fcntl(0, F_GETFL) & O_ACCMODE) == O_RDWR);
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

        *(uint32_t *)handoff = 0u;
        errno = 0;
        astra_posix_start(&startup);
        assert(restore_failures == 1u && restore_status == EINVAL);
    }
    free(handoff);

    test_log_descriptors_without_streams();
    test_log_descriptors_survive_exec();
    return 0;
}
