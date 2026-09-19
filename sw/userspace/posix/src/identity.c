/* The POSIX identity of Astra's single local owner. */

#define _GNU_SOURCE 1

#include <errno.h>
#include <grp.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
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
issetugid(void)
{
    /* Astra has neither set-id executables nor a second local identity. */
    return 0;
}

char *
secure_getenv(const char *name)
{
    return issetugid() ? NULL : getenv(name);
}

#if defined(__GNUC__) && !defined(__clang__)
/* The public ABI is nonnull; the implementation still rejects bad callers. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
int
getlogin_r(char *name, size_t size)
{
    static const char owner[] = "root";

    if (name == NULL)
        return EFAULT;
    if (size < sizeof(owner))
        return ERANGE;
    (void)memcpy(name, owner, sizeof(owner));
    return 0;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int
getgroups(int size, gid_t list[])
{
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    if (size != 0 && list == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (size == 0)
        return 1;
    list[0] = getgid();
    return 1;
}

int
setgroups(size_t size, const gid_t *groups)
{
    if (size != 0u && groups == NULL) {
        errno = EFAULT;
        return -1;
    }
    for (size_t index = 0u; index < size; ++index)
        if (groups[index] != (gid_t)0) {
            errno = EPERM;
            return -1;
        }
    return 0;
}

int
initgroups(const char *user, gid_t group)
{
    if (user == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (strcmp(user, "root") != 0 || group != (gid_t)0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int
reject_foreign_identity(unsigned long identity)
{
    if (identity == 0ul)
        return 0;
    errno = EPERM;
    return -1;
}

int
setuid(uid_t uid)
{
    return reject_foreign_identity((unsigned long)uid);
}

int
seteuid(uid_t uid)
{
    return setuid(uid);
}

int
setreuid(uid_t real_uid, uid_t effective_uid)
{
    if ((real_uid == (uid_t)-1 || real_uid == (uid_t)0) &&
        (effective_uid == (uid_t)-1 || effective_uid == (uid_t)0))
        return 0;
    errno = EPERM;
    return -1;
}

int
setresuid(uid_t real_uid, uid_t effective_uid, uid_t saved_uid)
{
    if ((real_uid == (uid_t)-1 || real_uid == (uid_t)0) &&
        (effective_uid == (uid_t)-1 || effective_uid == (uid_t)0) &&
        (saved_uid == (uid_t)-1 || saved_uid == (uid_t)0))
        return 0;
    errno = EPERM;
    return -1;
}

int
setgid(gid_t gid)
{
    return reject_foreign_identity((unsigned long)gid);
}

int
setegid(gid_t gid)
{
    return setgid(gid);
}

int
setregid(gid_t real_gid, gid_t effective_gid)
{
    if ((real_gid == (gid_t)-1 || real_gid == (gid_t)0) &&
        (effective_gid == (gid_t)-1 || effective_gid == (gid_t)0))
        return 0;
    errno = EPERM;
    return -1;
}

int
setresgid(gid_t real_gid, gid_t effective_gid, gid_t saved_gid)
{
    if ((real_gid == (gid_t)-1 || real_gid == (gid_t)0) &&
        (effective_gid == (gid_t)-1 || effective_gid == (gid_t)0) &&
        (saved_gid == (gid_t)-1 || saved_gid == (gid_t)0))
        return 0;
    errno = EPERM;
    return -1;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
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
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
