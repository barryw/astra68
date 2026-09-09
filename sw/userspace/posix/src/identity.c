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
