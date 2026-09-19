#define _GNU_SOURCE 1

#include <astra/limits.h>
#include <astra/vfs_service.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int usleep(unsigned int useconds);
char *astra_test_getwd(char *buffer);
size_t astra_test_confstr(int name, char *buffer, size_t length);
int astra_test_posix_close(int descriptor, int flags);

static struct timespec slept;
static const char *opened_path;
static int opened_flags;
static off_t truncated_to;
static int close_result;
static int stat_result;
static int fstat_result;
static int fsync_result;
static uint32_t nofile_limit = 4096u;

uint32_t
astra_posix_resource_nofile(void)
{
    return nofile_limit;
}

int
__wrap_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    assert(remaining == NULL);
    slept = *request;
    return 0;
}

int
__wrap_open(const char *path, int flags, ...)
{
    opened_path = path;
    opened_flags = flags;
    return 7;
}

int
__wrap_ftruncate(int descriptor, off_t length)
{
    assert(descriptor == 7);
    truncated_to = length;
    return 0;
}

int __wrap_close(int descriptor) { assert(descriptor == 7); return close_result; }

int
__wrap_stat(const char *path, struct stat *about)
{
    assert(strcmp(path, "WORK:file") == 0);
    if (stat_result != 0) {
        errno = ENOENT;
        return -1;
    }
    memset(about, 0, sizeof(*about));
    return 0;
}

int
__wrap_fstat(int descriptor, struct stat *about)
{
    assert(descriptor == 9);
    if (fstat_result != 0) {
        errno = EBADF;
        return -1;
    }
    memset(about, 0, sizeof(*about));
    return 0;
}

int
__wrap_fsync(int descriptor)
{
    assert(descriptor == 9);
    return fsync_result;
}

char *
__wrap_getcwd(char *buffer, size_t size)
{
    static const char path[] = "/WORK/src";

    if (size < sizeof(path)) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buffer, path, sizeof(path));
    return buffer;
}

int
main(void)
{
    assert(usleep(1000500u) == 0);
    assert(slept.tv_sec == 1 && slept.tv_nsec == 500000L);

    assert(truncate("WORK:file", 1234) == 0);
    assert(strcmp(opened_path, "WORK:file") == 0);
    assert((opened_flags & O_WRONLY) != 0);
    assert(truncated_to == 1234);
    close_result = -1;
    errno = 0;
    assert(truncate("WORK:file", 99) == -1);

    assert(sysconf(_SC_CLK_TCK) == 1000000L);
    assert(sysconf(_SC_OPEN_MAX) == (long)nofile_limit);
    assert(sysconf(_SC_PAGESIZE) == (long)ASTRA_MEMORY_PAGE_SIZE);
    assert(sysconf(_SC_VERSION) == _POSIX_VERSION);
    assert(getpagesize() == (int)ASTRA_MEMORY_PAGE_SIZE);
    assert(getdtablesize() == (int)nofile_limit);
    nofile_limit = UINT32_C(0x80000000);
    assert(getdtablesize() == INT_MAX);
    nofile_limit = 4096u;
    errno = EBUSY;
    assert(sysconf(_SC_NGROUPS_MAX) == -1 && errno == EBUSY);
    errno = 0;
    assert(sysconf(INT32_MAX) == -1 && errno == EINVAL);

    assert(pathconf("WORK:file", _PC_PATH_MAX) ==
           (long)ASTRA_VFS_PATH_MAX - 1L);
    assert(pathconf("WORK:file", _PC_NAME_MAX) ==
           (long)ASTRA_VFS_NAME_MAX - 1L);
    errno = 0;
    assert(pathconf("WORK:file", INT32_MAX) == -1 && errno == EINVAL);
    assert(fpathconf(9, _PC_PATH_MAX) == (long)ASTRA_VFS_PATH_MAX - 1L);
    fstat_result = -1;
    errno = 0;
    assert(fpathconf(9, _PC_PATH_MAX) == -1 && errno == EBADF);
    fstat_result = 0;
    stat_result = -1;
    errno = 0;
    assert(pathconf("WORK:file", _PC_PATH_MAX) == -1 && errno == ENOENT);

    fsync_result = 0;
    assert(fdatasync(9) == 0);
    fsync_result = -1;
    assert(fdatasync(9) == -1);

    close_result = 0;
    assert(astra_test_posix_close(7, 0) == 0);
    assert(astra_test_posix_close(7, 1) == 0);
    errno = 0;
    assert(astra_test_posix_close(7, 2) == -1 && errno == EINVAL);

    {
        char value[32];

        assert(astra_test_confstr(_CS_PATH, NULL, 0u) == 10u);
        assert(astra_test_confstr(_CS_PATH, value, sizeof(value)) == 10u);
        assert(strcmp(value, "/commands") == 0);
        assert(astra_test_confstr(_CS_PATH, value, 5u) == 10u);
        assert(strcmp(value, "/com") == 0);
        assert(astra_test_confstr(_CS_POSIX_V7_ILP32_OFFBIG_CFLAGS,
                                  value, sizeof(value)) == 1u);
        assert(value[0] == '\0');
        assert(astra_test_confstr(_CS_POSIX_V7_WIDTH_RESTRICTED_ENVS,
                                  value, sizeof(value)) == 22u);
        assert(strcmp(value, "POSIX_V7_ILP32_OFFBIG") == 0);
        errno = EBUSY;
        assert(astra_test_confstr(_CS_POSIX_V7_LP64_OFF64_CFLAGS,
                                  value, sizeof(value)) == 0u);
        assert(errno == EBUSY);
        errno = 0;
        assert(astra_test_confstr(INT_MAX, value, sizeof(value)) == 0u &&
               errno == EINVAL);
    }

    {
        char *cwd = get_current_dir_name();

        assert(cwd != NULL && strcmp(cwd, "/WORK/src") == 0);
        free(cwd);
    }
    {
        char cwd[PATH_MAX];

        assert(astra_test_getwd(cwd) == cwd);
        assert(strcmp(cwd, "/WORK/src") == 0);
    }

    puts("ASTRA POSIX UNISTD COMPAT PASS");
    return 0;
}
