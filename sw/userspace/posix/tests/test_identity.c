#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int
main(void)
{
    char hostname[8];
    gid_t groups[1];

    assert(getuid() == (uid_t)0 && geteuid() == (uid_t)0);
    assert(getgid() == (gid_t)0 && getegid() == (gid_t)0);
    assert(getgroups(0, NULL) == 1);
    assert(getgroups(1, groups) == 1 && groups[0] == (gid_t)0);
    assert(setuid((uid_t)0) == 0 && seteuid((uid_t)0) == 0);
    assert(setreuid((uid_t)-1, (uid_t)0) == 0);
    assert(setresuid((uid_t)0, (uid_t)-1, (uid_t)0) == 0);
    assert(setgid((gid_t)0) == 0 && setegid((gid_t)0) == 0);
    assert(setregid((gid_t)-1, (gid_t)0) == 0);
    assert(setresgid((gid_t)0, (gid_t)-1, (gid_t)0) == 0);

    errno = 0;
    assert(setuid((uid_t)1) == -1 && errno == EPERM);
    errno = 0;
    assert(setresuid((uid_t)-1, (uid_t)1, (uid_t)-1) == -1 &&
           errno == EPERM);
    errno = 0;
    assert(setgid((gid_t)1) == -1 && errno == EPERM);
    errno = 0;
    assert(setresgid((gid_t)-1, (gid_t)-1, (gid_t)1) == -1 &&
           errno == EPERM);

    assert(gethostname(hostname, sizeof(hostname)) == 0);
    assert(strcmp(hostname, "astra68") == 0);
    errno = 0;
    assert(gethostname(hostname, sizeof(hostname) - 1u) == -1);
    assert(errno == ENAMETOOLONG);
    return 0;
}
