#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int issetugid(void);

int
main(void)
{
    char hostname[8];
    char login[5];
    gid_t groups[1];

    assert(issetugid() == 0);
    assert(getlogin_r(login, sizeof(login)) == 0);
    assert(strcmp(login, "root") == 0);
    assert(getlogin_r(login, sizeof(login) - 1u) == ERANGE);
    assert(setgroups(0u, NULL) == 0);
    groups[0] = (gid_t)0;
    assert(setgroups(1u, groups) == 0);
    groups[0] = (gid_t)1;
    errno = 0;
    assert(setgroups(1u, groups) == -1 && errno == EPERM);
    assert(initgroups("root", (gid_t)0) == 0);
    errno = 0;
    assert(initgroups("other", (gid_t)0) == -1 && errno == EPERM);

    assert(setenv("ASTRA_SECURE_ENV_TEST", "present", 1) == 0);
    assert(strcmp(secure_getenv("ASTRA_SECURE_ENV_TEST"), "present") == 0);

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
