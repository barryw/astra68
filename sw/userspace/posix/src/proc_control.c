#include "proc_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
proc_open(pid_t process, const char *leaf, int flags)
{
    char path[32];
    int fd;

    if (process <= 0) {
        errno = ENOENT;
        return -1;
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/%s", (long)process, leaf);
    fd = open(path, flags);
    if (fd < 0 && errno == ENOENT && access("/proc", F_OK) != 0)
        errno = ENOSYS;
    return fd;
}

int
posix_proc_control(pid_t process, const char *command)
{
    size_t length = strlen(command);
    ssize_t written;
    int saved;
    int fd = proc_open(process, "ctl", O_WRONLY);

    if (fd < 0)
        return -1;
    written = write(fd, command, length);
    saved = errno;
    (void)close(fd);
    if (written == (ssize_t)length)
        return 0;
    errno = written < 0 ? saved : EIO;
    return -1;
}

int
posix_proc_priority(pid_t process, uint32_t *priority)
{
    char status[512];
    const char *field;
    ssize_t length;
    uint32_t value = 0u;
    int fd = proc_open(process, "status", O_RDONLY);

    if (fd < 0)
        return -1;
    length = read(fd, status, sizeof(status) - 1u);
    (void)close(fd);
    if (length <= 0) {
        errno = length == 0 ? ENOENT : errno;
        return -1;
    }
    status[length] = '\0';
    field = strstr(status, "\npriority ");
    if (field == NULL) {
        errno = EIO;
        return -1;
    }
    for (field += sizeof("\npriority ") - 1u; *field >= '0' && *field <= '9';
         ++field)
        value = value * 10u + (uint32_t)(*field - '0');
    *priority = value;
    return 0;
}
