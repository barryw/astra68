#define _GNU_SOURCE 1

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *astra_test_mkdtemp(char *path);
int astra_test_mkostemp(char *path, int flags);

int
main(void)
{
    char directory[] = "/tmp/astra-mkdtemp-XXXXXX";
    char file[] = "/tmp/astra-mkostemp-XXXXXX";
    char invalid[] = "/tmp/no-template";
    struct stat about;
    int descriptor;

    errno = 0;
    assert(astra_test_mkdtemp(invalid) == NULL && errno == EINVAL);
    assert(astra_test_mkdtemp(directory) == directory);
    assert(stat(directory, &about) == 0 && S_ISDIR(about.st_mode));
    assert(strstr(directory, "XXXXXX") == NULL);
    assert(rmdir(directory) == 0);

    descriptor = astra_test_mkostemp(file, O_CLOEXEC);
    assert(descriptor >= 0);
    assert((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0);
    assert(close(descriptor) == 0);
    assert(unlink(file) == 0);
    return 0;
}
