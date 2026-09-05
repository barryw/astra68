#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/syscall.h>

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    return status;
}

static int read_ready;
static int write_ready;
static int wake_read;
static int wake_write;
static int eof_next;
static AstraStartupCapability capabilities[3];

uint32_t astra_yield(void) { return ASTRA_SYSCALL_OK; }
uint64_t astra_clock_monotonic(void) { return 100u; }
uint32_t astra_close(uint32_t handle)
{
    (void)handle;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_event_create(uint32_t flags, uint32_t rights,
                               uint32_t *handle)
{
    assert(flags == ASTRA_EVENT_MANUAL_RESET && rights == ASTRA_RIGHT_WAIT);
    *handle = 99u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_create(uint32_t size, uint32_t rights,
                              uint32_t *handle)
{
    (void)size;
    (void)rights;
    (void)handle;
    assert(0 && "poll test must not create a pipe area");
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}
uint32_t astra_rt_ring_create(uint32_t area, uint32_t offset,
                              uint32_t element_size, uint32_t capacity,
                              uint32_t flags, uint32_t *producer,
                              uint32_t *consumer)
{
    (void)area;
    (void)offset;
    (void)element_size;
    (void)capacity;
    (void)flags;
    (void)producer;
    (void)consumer;
    assert(0 && "poll test must not create a byte ring");
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)deadline;
    (void)detail;
    if (handle == 110u) {
        if (wake_read) {
            wake_read = 0;
            read_ready = 1;
        }
        return read_ready ? ASTRA_SYSCALL_OK : ASTRA_SYSCALL_TIMED_OUT;
    }
    if (handle == 20u) {
        if (wake_write) {
            wake_write = 0;
            write_ready = 1;
        }
        return write_ready ? ASTRA_SYSCALL_OK : ASTRA_SYSCALL_TIMED_OUT;
    }
    assert(handle == 99u);
    return ASTRA_SYSCALL_TIMED_OUT;
}
uint32_t astra_wait_multiple(const uint32_t *handles, uint32_t count,
                             uint64_t deadline, uint32_t *index,
                             uint32_t *detail)
{
    (void)deadline;
    (void)index;
    (void)detail;
    assert(handles != NULL && count != 0u);
    read_ready = 1;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_stream_read_wait(uint32_t source, uint32_t *wait_handle,
                                uint32_t *events)
{
    assert(source == 10u);
    *wait_handle = 110u;
    *events = read_ready ? ASTRA_STREAM_READY_READ : 0u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_stream_read(uint32_t source, void *bytes, uint32_t capacity,
                           uint32_t *length)
{
    assert(source == 10u && bytes != NULL && capacity != 0u);
    if (!read_ready) {
        *length = 0u;
        return ASTRA_SYSCALL_OK;
    }
    *(char *)bytes = 'x';
    *length = 1u;
    read_ready = 0;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_stream_read_ex(uint32_t source, void *bytes,
                              uint32_t capacity, uint32_t *length,
                              uint32_t *flags)
{
    if (eof_next) {
        eof_next = 0;
        *length = 0u;
        *flags = ASTRA_STREAM_DATA_EOF;
        return ASTRA_SYSCALL_OK;
    }
    *flags = 0u;
    return astra_stream_read(source, bytes, capacity, length);
}
uint32_t astra_stream_write(uint32_t handle, const void *bytes,
                            uint32_t length, uint32_t *written)
{
    assert(handle == 20u && bytes != NULL);
    *written = write_ready ? length : 0u;
    return write_ready ? ASTRA_SYSCALL_OK : ASTRA_SYSCALL_WOULD_BLOCK;
}
uint32_t astra_rt_ring_read_try(uint32_t handle, void *bytes,
                                uint32_t capacity, uint32_t *copied)
{
    (void)handle;
    (void)bytes;
    (void)capacity;
    (void)copied;
    assert(0 && "stream descriptor must not use byte-ring read");
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}
uint32_t astra_rt_ring_write_try(uint32_t handle, const void *bytes,
                                 uint32_t length, uint32_t flags,
                                 uint32_t *written)
{
    (void)handle;
    (void)bytes;
    (void)length;
    (void)flags;
    (void)written;
    assert(0 && "stream descriptor must not use byte-ring write");
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}
void astra_process_exit(uint32_t status)
{
    (void)status;
    for (;;) { }
}

static void
capability(uint32_t index, const char *name, uint32_t handle)
{
    uint32_t at = 0u;

    while (name[at] != '\0') {
        capabilities[index].name[at] = name[at];
        ++at;
    }
    capabilities[index].name[at] = '\0';
    capabilities[index].handle = handle;
}

int
main(void)
{
    AstraStartupInfo startup = {
        .magic = ASTRA_STARTUP_MAGIC,
        .abi_version = ASTRA_STARTUP_ABI_VERSION,
        .header_size = ASTRA_STARTUP_INFO_SIZE,
        .total_size = ASTRA_STARTUP_INFO_SIZE,
        .syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION,
        .capability_count = 3u,
        .capabilities_address = (uint32_t)(uintptr_t)capabilities
    };
    struct pollfd descriptor = { .fd = 0, .events = POLLIN };
    struct timeval zero = {0};
    fd_set reads;
    char byte;

    capability(0u, "STDIN", 10u);
    capability(1u, "STDOUT", 20u);
    capability(2u, "STDERR", 20u);
    astra_posix_start(&startup);

    assert(poll(&descriptor, 1u, 0) == 0 && descriptor.revents == 0);
    assert(fcntl(0, F_SETFL, O_NONBLOCK) == 0);
    assert(read(0, &byte, 1u) == -1 && errno == EAGAIN);
    read_ready = 1;
    assert(poll(&descriptor, 1u, 0) == 1);
    assert((descriptor.revents & POLLIN) != 0);
    assert(read(0, &byte, 1u) == 1 && byte == 'x');

    assert(fcntl(0, F_SETFL, 0) == 0);
    wake_read = 1;
    assert(read(0, &byte, 1u) == 1 && byte == 'x');
    eof_next = 1;
    assert(read(0, &byte, 1u) == 0);

    assert(fcntl(1, F_SETFL, O_NONBLOCK) == 0);
    assert(write(1, "z", 1u) == -1 && errno == EAGAIN);
    assert(fcntl(1, F_SETFL, 0) == 0);
    wake_write = 1;
    assert(write(1, "z", 1u) == 1);

    read_ready = 1;
    FD_ZERO(&reads);
    FD_SET(0, &reads);
    assert(select(1, &reads, NULL, NULL, &zero) == 1);
    assert(FD_ISSET(0, &reads));

    descriptor.fd = 42;
    descriptor.events = POLLIN;
    assert(poll(&descriptor, 1u, 0) == 1);
    assert(descriptor.revents == POLLNVAL);
    assert(poll(NULL, 0u, 1) == 0);
    return 0;
}
