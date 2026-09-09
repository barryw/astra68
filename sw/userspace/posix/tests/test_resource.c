#define _XOPEN_SOURCE 700

#include "../src/resource_internal.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/resource.h>

int
main(void)
{
    AstraPosixResourceState saved;
    struct rlimit limit;

    _Static_assert(RLIM_NLIMITS > RLIMIT_AS,
                   "resource limit count must cover every public limit");

    astra_posix_resource_reset();
    assert(getrlimit(RLIMIT_NOFILE, &limit) == 0 &&
           limit.rlim_cur == RLIM_INFINITY);
    limit.rlim_cur = 12u;
    assert(setrlimit(RLIMIT_NOFILE, &limit) == 0 &&
           astra_posix_resource_nofile() == 12u);
    limit.rlim_max = 12u;
    assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
    limit.rlim_max = RLIM_INFINITY;
    errno = 0;
    assert(setrlimit(RLIMIT_NOFILE, &limit) == -1 && errno == EPERM);

    assert(getrlimit(RLIMIT_DATA, &limit) == 0);
    limit.rlim_cur = 100u;
    assert(setrlimit(RLIMIT_DATA, &limit) == 0);
    assert(astra_posix_resource_heap_allows(20u, 70u, 10u));
    assert(!astra_posix_resource_heap_allows(20u, 70u, 11u));

    assert(getrlimit(RLIMIT_FSIZE, &limit) == 0);
    limit.rlim_cur = 4096u;
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0 &&
           astra_posix_resource_file_size() == 4096u);

    assert(getrlimit(RLIMIT_STACK, &limit) == 0 &&
           limit.rlim_cur == 4096u && limit.rlim_max == 4096u);
    limit.rlim_cur = 2048u;
    errno = 0;
    assert(setrlimit(RLIMIT_STACK, &limit) == -1 && errno == ENOTSUP);

    astra_posix_resource_export(&saved);
    assert(astra_posix_resource_validate(&saved));
    astra_posix_resource_reset();
    astra_posix_resource_import(&saved);
    assert(astra_posix_resource_nofile() == 12u);
    return 0;
}
