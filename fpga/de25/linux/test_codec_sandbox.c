#include "astra_codec_sandbox.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    assert(astra_codec_sandbox_enter() == 0);
    errno = 0;
    assert(open("/etc/passwd", O_RDONLY) == -1 && errno == EPERM);
    errno = 0;
    assert(socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPERM);
    return 0;
}
