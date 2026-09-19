/* POSIX calls whose implementation is a small composition of core calls. */

#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "resource_internal.h"

#include <astra/limits.h>
#include <astra/vfs_service.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ASTRA_POSIX_CLOCK_TICKS_PER_SECOND 1000000L
#define ASTRA_POSIX_CLOSE_RESTART 1

int
usleep(useconds_t useconds)
{
    struct timespec request;

    request.tv_sec = (time_t)(useconds / 1000000u);
    request.tv_nsec = (long)(useconds % 1000000u) * 1000L;
    return nanosleep(&request, NULL);
}

int
truncate(const char *path, off_t length)
{
    int descriptor;
    int result;
    int saved;

    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    descriptor = open(path, O_WRONLY);
    if (descriptor < 0)
        return -1;
    result = ftruncate(descriptor, length);
    saved = errno;
    if (close(descriptor) != 0 && result == 0)
        return -1;
    if (result != 0)
        errno = saved;
    return result;
}

int
fdatasync(int descriptor)
{
    /* Astra's durability boundary currently flushes data and metadata. */
    return fsync(descriptor);
}

int
posix_close(int descriptor, int flags)
{
    if (flags != 0 && flags != ASTRA_POSIX_CLOSE_RESTART) {
        errno = EINVAL;
        return -1;
    }
    return close(descriptor);
}

int
getpagesize(void)
{
    return (int)ASTRA_MEMORY_PAGE_SIZE;
}

int
getdtablesize(void)
{
    uint32_t capacity = astra_posix_resource_nofile();

    return capacity > (uint32_t)INT_MAX ? INT_MAX : (int)capacity;
}

char *
get_current_dir_name(void)
{
    size_t capacity = 128u;

    for (;;) {
        char *path = malloc(capacity);

        if (path == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        if (getcwd(path, capacity) != NULL)
            return path;
        free(path);
        if (errno != ERANGE)
            return NULL;
        if (capacity > SIZE_MAX / 2u) {
            errno = ENOMEM;
            return NULL;
        }
        capacity *= 2u;
    }
}

char *
getwd(char *buffer)
{
    if (buffer == NULL) {
        errno = EFAULT;
        return NULL;
    }
    return getcwd(buffer, PATH_MAX);
}

static size_t
copy_confstr(const char *value, char *buffer, size_t length)
{
    size_t required = strlen(value) + 1u;

    if (buffer != NULL && length != 0u) {
        size_t copied = required < length ? required : length;

        (void)memcpy(buffer, value, copied);
        buffer[copied - 1u] = '\0';
    }
    return required;
}

size_t
confstr(int name, char *buffer, size_t length)
{
    switch (name) {
    case _CS_PATH:
        return copy_confstr("/commands", buffer, length);
    case _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS:
    case _CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS:
    case _CS_POSIX_V7_ILP32_OFFBIG_LIBS:
    case _CS_XBS5_ILP32_OFFBIG_LINTFLAGS:
#ifdef _CS_POSIX_V7_THREADS_CFLAGS
    case _CS_POSIX_V7_THREADS_CFLAGS:
#endif
#ifdef _CS_POSIX_V7_THREADS_LDFLAGS
    case _CS_POSIX_V7_THREADS_LDFLAGS:
#endif
    case _CS_V7_ENV:
    case _CS_LFS_CFLAGS:
    case _CS_LFS_LDFLAGS:
    case _CS_LFS_LIBS:
    case _CS_LFS_LINTFLAGS:
        /* Astra's only ABI already has 64-bit off_t and thread support. */
        return copy_confstr("", buffer, length);
    case _CS_POSIX_V7_WIDTH_RESTRICTED_ENVS:
        return copy_confstr("POSIX_V7_ILP32_OFFBIG", buffer, length);
    case _CS_POSIX_V7_ILP32_OFF32_CFLAGS:
    case _CS_POSIX_V7_ILP32_OFF32_LDFLAGS:
    case _CS_POSIX_V7_ILP32_OFF32_LIBS:
    case _CS_XBS5_ILP32_OFF32_LINTFLAGS:
    case _CS_POSIX_V7_LP64_OFF64_CFLAGS:
    case _CS_POSIX_V7_LP64_OFF64_LDFLAGS:
    case _CS_POSIX_V7_LP64_OFF64_LIBS:
    case _CS_XBS5_LP64_OFF64_LINTFLAGS:
    case _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS:
    case _CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS:
    case _CS_POSIX_V7_LPBIG_OFFBIG_LIBS:
    case _CS_XBS5_LPBIG_OFFBIG_LINTFLAGS:
        return 0u;
    default:
        errno = EINVAL;
        return 0u;
    }
}

#ifdef ASTRA_POSIX_TEST
char *
astra_test_getwd(char *buffer)
{
    return getwd(buffer);
}

size_t
astra_test_confstr(int name, char *buffer, size_t length)
{
    return confstr(name, buffer, length);
}
#endif

long
sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK:
        /* times() reports microseconds, so one clock tick is one microsecond. */
        return ASTRA_POSIX_CLOCK_TICKS_PER_SECOND;
    case _SC_OPEN_MAX:
        return (long)astra_posix_resource_nofile();
    case _SC_PAGESIZE:
        return (long)ASTRA_MEMORY_PAGE_SIZE;
    case _SC_VERSION:
        return _POSIX_VERSION;
#ifdef _SC_NPROCESSORS_CONF
    case _SC_NPROCESSORS_CONF:
#endif
#ifdef _SC_NPROCESSORS_ONLN
    case _SC_NPROCESSORS_ONLN:
#endif
        return 1L;
    case _SC_NGROUPS_MAX:
        return -1L;
    default:
#ifdef _SC_POSIX_26_VERSION
        if (name >= _SC_ARG_MAX && name <= _SC_POSIX_26_VERSION)
            return -1L;
#endif
        errno = EINVAL;
        return -1L;
    }
}

static int
pathconf_value(int name, long *value)
{
    switch (name) {
    case _PC_FILESIZEBITS:
        *value = (long)(sizeof(off_t) * CHAR_BIT);
        break;
    case _PC_NAME_MAX:
        *value = (long)ASTRA_VFS_NAME_MAX - 1L;
        break;
    case _PC_PATH_MAX:
        *value = (long)ASTRA_VFS_PATH_MAX - 1L;
        break;
    case _PC_2_SYMLINKS:
    case _PC_CHOWN_RESTRICTED:
    case _PC_NO_TRUNC:
        *value = 1L;
        break;
    case _PC_SYMLINK_MAX:
        *value = (long)ASTRA_VFS_PATH_MAX - 1L;
        break;
#ifdef _PC_TIMESTAMP_RESOLUTION
    case _PC_TIMESTAMP_RESOLUTION:
        *value = 1000000000L; /* The VFS metadata contract carries seconds. */
        break;
#endif
    case _PC_LINK_MAX:
    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
    case _PC_PIPE_BUF:
    case _PC_ALLOC_SIZE_MIN:
    case _PC_REC_INCR_XFER_SIZE:
    case _PC_REC_MAX_XFER_SIZE:
    case _PC_REC_MIN_XFER_SIZE:
    case _PC_REC_XFER_ALIGN:
#ifdef _PC_TEXTDOMAIN_MAX
    case _PC_TEXTDOMAIN_MAX:
#endif
    case _PC_VDISABLE:
    case _PC_ASYNC_IO:
#ifdef _PC_FALLOC
    case _PC_FALLOC:
#endif
    case _PC_PRIO_IO:
    case _PC_SYNC_IO:
        *value = -1L; /* Valid question; this filesystem makes no promise. */
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return 0;
}

long
pathconf(const char *path, int name)
{
    struct stat about;
    long value;

    if (pathconf_value(name, &value) != 0)
        return -1L;
    if (stat(path, &about) != 0)
        return -1L;
    return value;
}

long
fpathconf(int descriptor, int name)
{
    struct stat about;
    long value;

    if (pathconf_value(name, &value) != 0)
        return -1L;
    if (fstat(descriptor, &about) != 0)
        return -1L;
    return value;
}
