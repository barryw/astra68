#include <astra/posix.h>
#include <astra/secret_memory.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int astra_posix_read_secret(const char *prompt, char *buffer, size_t capacity)
{
    struct termios original, hidden;
    size_t length = 0u;
    int overflow = 0, result = -1, error = 0;
    char byte;
    if (prompt == NULL || buffer == NULL || capacity < 2u ||
        capacity > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    buffer[0] = '\0';
    int fd = open("/dev/tty", O_RDWR);
    if (fd < 0) {
        astra_secret_wipe(buffer, (uint32_t)capacity);
        return -1;
    }
    if (tcgetattr(fd, &original) != 0) goto done;
    hidden = original;
    hidden.c_lflag &= (tcflag_t)~(ECHO | ISIG);
    if (tcsetattr(fd, TCSAFLUSH, &hidden) != 0) goto done;
    if (astra_posix_write_all(fd, prompt, strlen(prompt)) != 0)
        goto restore;
    for (;;) {
        ssize_t received = read(fd, &byte, 1u);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) {
            if (received == 0) errno = EIO;
            goto restore;
        }
        if (byte == '\n') break;
        if (byte == '\0' || length + 1u >= capacity) {
            overflow = 1;
            continue;
        }
        if (!overflow) buffer[length++] = byte;
    }
    if (overflow) {
        errno = ERANGE;
        goto restore;
    }
    buffer[length] = '\0';
    result = 0;
restore:
    error = errno;
    if (astra_posix_write_all(fd, "\n", 1u) != 0 && result == 0) {
        result = -1;
        error = errno;
    }
    if (tcsetattr(fd, TCSAFLUSH, &original) != 0 && result == 0) {
        result = -1;
        error = errno;
    }
    errno = error;
done:
    error = errno;
    if (close(fd) != 0 && result == 0) {
        result = -1;
        error = errno;
    }
    if (result != 0) astra_secret_wipe(buffer, (uint32_t)capacity);
    errno = error;
    return result;
}
