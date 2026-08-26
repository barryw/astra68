#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int
main(void)
{
    char hostname[8];

    assert(getuid() == (uid_t)0 && geteuid() == (uid_t)0);
    assert(getgid() == (gid_t)0 && getegid() == (gid_t)0);
    assert(gethostname(hostname, sizeof(hostname)) == 0);
    assert(strcmp(hostname, "astra68") == 0);
    errno = 0;
    assert(gethostname(hostname, sizeof(hostname) - 1u) == -1);
    assert(errno == ENAMETOOLONG);
    return 0;
}
