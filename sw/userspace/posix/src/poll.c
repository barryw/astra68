#include <astra/posix_descriptor.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

static uint32_t idle_event;

static int
append_wait(uint32_t *waits, uint32_t *count, uint32_t handle)
{
    for (uint32_t index = 0u; index < *count; ++index)
        if (waits[index] == handle)
            return 0;
    if (*count == ASTRA_WAIT_MULTIPLE_MAX) {
        errno = EMFILE;
        return -1;
    }
    waits[(*count)++] = handle;
    return 0;
}

static uint64_t
poll_deadline(int timeout)
{
    return timeout < 0 ? ASTRA_DEADLINE_FOREVER :
        astra_clock_monotonic() + (uint64_t)(uint32_t)timeout * UINT64_C(1000000);
}

static int
wait_until(const uint32_t *waits, uint32_t count, uint64_t deadline)
{
    uint32_t status;

    if (count != 0u) {
        status = astra_wait_multiple(waits, count, deadline, NULL, NULL);
    } else {
        if (idle_event == 0u) {
            status = astra_rt_event_create(ASTRA_EVENT_MANUAL_RESET,
                                           ASTRA_RIGHT_WAIT, &idle_event);
            if (status != ASTRA_SYSCALL_OK) {
                errno = status == ASTRA_SYSCALL_RESOURCE_LIMIT ? ENOMEM : EIO;
                return -1;
            }
        }
        status = astra_wait_one(idle_event, deadline, NULL);
    }
    if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_PEER_DEAD ||
        status == ASTRA_SYSCALL_CLOSED)
        return 1;
    if (status == ASTRA_SYSCALL_TIMED_OUT)
        return 0;
    errno = status == ASTRA_SYSCALL_CANCELLED ? EINTR : EIO;
    return -1;
}

int
poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
    uint64_t deadline;

    if (timeout < -1) {
        errno = EINVAL;
        return -1;
    }
    if (nfds != 0u && fds == NULL) {
        errno = EFAULT;
        return -1;
    }
    deadline = poll_deadline(timeout);
    for (;;) {
        uint32_t wait_count = 0u;
        int ready_count = 0;

        for (nfds_t index = 0u; index < nfds; ++index) {
            uint32_t descriptor_waits[2];
            uint32_t descriptor_count = 0u;

            fds[index].revents = 0;
            if (fds[index].fd < 0)
                continue;
            if (astra_posix_descriptor_poll(
                    fds[index].fd, fds[index].events, &fds[index].revents,
                    descriptor_waits, &descriptor_count) < 0)
                return -1;
            if (fds[index].revents != 0)
                ++ready_count;
            for (uint32_t at = 0u; at < descriptor_count; ++at)
                if (append_wait(waits, &wait_count,
                                descriptor_waits[at]) < 0)
                    return -1;
        }
        if (ready_count != 0 || timeout == 0)
            return ready_count;
        {
            int result = wait_until(waits, wait_count, deadline);

            if (result <= 0)
                return result;
        }
    }
}

int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
       struct timeval *timeout)
{
    struct pollfd fds[FD_SETSIZE];
    int milliseconds = -1;
    nfds_t count = 0u;
    int result;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    if (timeout != NULL) {
        uint64_t value;

        if (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
            timeout->tv_usec >= 1000000) {
            errno = EINVAL;
            return -1;
        }
        if ((uint64_t)timeout->tv_sec > (uint64_t)INT32_MAX / 1000u) {
            milliseconds = INT32_MAX;
        } else {
            value = (uint64_t)timeout->tv_sec * UINT64_C(1000) +
                    ((uint64_t)timeout->tv_usec + 999u) / 1000u;
            milliseconds = value > INT32_MAX ? INT32_MAX : (int)value;
        }
    }
    for (int fd = 0; fd < nfds; ++fd) {
        short events = 0;

        if (readfds != NULL && FD_ISSET(fd, readfds))
            events |= POLLIN;
        if (writefds != NULL && FD_ISSET(fd, writefds))
            events |= POLLOUT;
        if (exceptfds != NULL && FD_ISSET(fd, exceptfds))
            events |= POLLPRI;
        if (events == 0)
            continue;
        fds[count].fd = fd;
        fds[count].events = events;
        fds[count].revents = 0;
        ++count;
    }
    result = poll(fds, count, milliseconds);
    if (result < 0)
        return -1;
    for (nfds_t index = 0u; index < count; ++index)
        if ((fds[index].revents & POLLNVAL) != 0) {
            errno = EBADF;
            return -1;
        }
    if (readfds != NULL)
        FD_ZERO(readfds);
    if (writefds != NULL)
        FD_ZERO(writefds);
    if (exceptfds != NULL)
        FD_ZERO(exceptfds);
    result = 0;
    for (nfds_t index = 0u; index < count; ++index) {
        int selected = 0;
        short returned = fds[index].revents;

        if (readfds != NULL &&
            (returned & (POLLIN | POLLERR | POLLHUP)) != 0) {
            FD_SET(fds[index].fd, readfds);
            selected = 1;
        }
        if (writefds != NULL &&
            (returned & (POLLOUT | POLLERR | POLLHUP)) != 0) {
            FD_SET(fds[index].fd, writefds);
            selected = 1;
        }
        if (exceptfds != NULL && (returned & POLLPRI) != 0) {
            FD_SET(fds[index].fd, exceptfds);
            selected = 1;
        }
        result += selected;
    }
    return result;
}
