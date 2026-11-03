#include <astra/posix.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static const char *input;
static size_t position;
static int opened, closed, hidden, restored, open_error;

int __wrap_open(const char *path, int flags, ...)
{
    assert(strcmp(path, "/dev/tty") == 0 && flags == O_RDWR);
    if (open_error) { errno = ENXIO; return -1; }
    ++opened;
    return 55;
}

ssize_t __wrap_read(int fd, void *buffer, size_t count)
{
    assert(fd == 55 && count == 1u);
    if (input[position] == '\0') return 0;
    *(char *)buffer = input[position++];
    return 1;
}

int __wrap_close(int fd)
{
    assert(fd == 55);
    ++closed;
    return 0;
}

int __wrap_tcgetattr(int fd, struct termios *attributes)
{
    assert(fd == 55);
    memset(attributes, 0, sizeof(*attributes));
    attributes->c_lflag = ECHO | ISIG | ICANON;
    return 0;
}

int __wrap_tcsetattr(int fd, int action, const struct termios *attributes)
{
    assert(fd == 55 && action == TCSAFLUSH);
    if ((attributes->c_lflag & (ECHO | ISIG)) == 0u) ++hidden;
    else { assert((attributes->c_lflag & (ECHO | ISIG)) == (ECHO | ISIG)); ++restored; }
    return 0;
}

int astra_posix_write_all(int fd, const void *bytes, size_t length)
{
    assert(fd == 55 && bytes != NULL && length != 0u);
    return 0;
}

int main(void)
{
    char secret[8];
    input = "p,a=s\n";
    memset(secret, 0x5a, sizeof(secret));
    assert(astra_posix_read_secret("Password: ", secret, sizeof(secret)) == 0);
    assert(strcmp(secret, "p,a=s") == 0);
    assert(opened == 1 && closed == 1 && hidden == 1 && restored == 1);

    input = "abcdef\n"; position = 0u;
    memset(secret, 0x5a, sizeof(secret));
    assert(astra_posix_read_secret("Password: ", secret, 4u) == -1 &&
        errno == ERANGE && position == 7u);
    for (size_t at = 0u; at < 4u; ++at) assert(secret[at] == 0);
    assert(opened == 2 && closed == 2 && hidden == 2 && restored == 2);

    open_error = 1;
    memset(secret, 0x5a, sizeof(secret));
    assert(astra_posix_read_secret("Password: ", secret, sizeof(secret)) == -1 &&
        errno == ENXIO);
    for (size_t at = 0u; at < sizeof(secret); ++at) assert(secret[at] == 0);
    return 0;
}
