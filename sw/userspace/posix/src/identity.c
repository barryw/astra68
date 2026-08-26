/* The POSIX identity of Astra's single local owner. */

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

uid_t
getuid(void)
{
    return (uid_t)0;
}

uid_t
geteuid(void)
{
    return getuid();
}

gid_t
getgid(void)
{
    return (gid_t)0;
}

gid_t
getegid(void)
{
    return getgid();
}

int
gethostname(char *name, size_t length)
{
    static const char hostname[] = "astra68";
    size_t index;

    if (name == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (length < sizeof(hostname)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (index = 0u; index < sizeof(hostname); ++index)
        name[index] = hostname[index];
    return 0;
}
