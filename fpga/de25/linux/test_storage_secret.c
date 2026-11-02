#define _GNU_SOURCE
#include "astra_storage_secret.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/var/tmp/astra-secret-test.XXXXXX";
    char path[sizeof(directory) + 16u];
    unsigned char first[32], second[32];
    assert(geteuid() == 0);
    assert(mkdtemp(directory) != NULL);
    assert(snprintf(path, sizeof(path), "%s/key", directory) > 0);
    int fd = astra_storage_secret_open(path);
    assert(fd >= 0 && pread(fd, first, sizeof(first), 0) == sizeof(first));
    close(fd);
    fd = astra_storage_secret_open(path);
    assert(fd >= 0 && pread(fd, second, sizeof(second), 0) == sizeof(second));
    assert(memcmp(first, second, sizeof(first)) == 0);
    close(fd);
    assert(chmod(path, 0644u) == 0);
    assert(astra_storage_secret_open(path) < 0 && errno == EACCES);
    assert(chmod(path, 0600u) == 0);
    assert(unlink(path) == 0);
    assert(symlink("/etc/shadow", path) == 0);
    assert(astra_storage_secret_open(path) < 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    puts("host secret key creation, reuse, mode and symlink fences PASS");
    return 0;
}
