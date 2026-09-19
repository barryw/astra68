#define _GNU_SOURCE

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int astra_test_alphasort(const struct dirent **left,
                         const struct dirent **right);
int astra_test_versionsort(const struct dirent **left,
                           const struct dirent **right);
int astra_test_readdir_r(DIR *restrict directory,
                         struct dirent *restrict entry,
                         struct dirent **restrict result);
int astra_test_scandir(const char *path, struct dirent ***namelist,
                       int (*select_entry)(const struct dirent *),
                       int (*compare)(const struct dirent **,
                                      const struct dirent **));
int astra_test_scandirat(int directory_fd, const char *path,
                         struct dirent ***namelist,
                         int (*select_entry)(const struct dirent *),
                         int (*compare)(const struct dirent **,
                                        const struct dirent **));

static int
not_dot(const struct dirent *entry)
{
    return entry->d_name[0] != '.';
}

static void
free_list(struct dirent **entries, int count)
{
    while (count > 0)
        free(entries[--count]);
    free(entries);
}

static void
touch_at(int directory, const char *name)
{
    int fd = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL, 0600);

    assert(fd >= 0);
    assert(close(fd) == 0);
}

int
main(void)
{
    char template[] = "/tmp/astra-dirent-XXXXXX";
    struct dirent **entries = NULL;
    struct dirent current;
    struct dirent *result;
    DIR *stream;
    int count;
    int directory;

    assert(mkdtemp(template) == template);
    directory = open(template, O_RDONLY | O_DIRECTORY);
    assert(directory >= 0);
    touch_at(directory, "file10");
    touch_at(directory, "file2");
    touch_at(directory, "alpha");

    count = astra_test_scandir(template, &entries, not_dot,
                               astra_test_versionsort);
    assert(count == 3);
    assert(strcmp(entries[0]->d_name, "alpha") == 0);
    assert(strcmp(entries[1]->d_name, "file2") == 0);
    assert(strcmp(entries[2]->d_name, "file10") == 0);
    free_list(entries, count);

    count = astra_test_scandirat(directory, ".", &entries, not_dot,
                                 astra_test_alphasort);
    assert(count == 3);
    assert(strcmp(entries[0]->d_name, "alpha") == 0);
    assert(strcmp(entries[1]->d_name, "file10") == 0);
    assert(strcmp(entries[2]->d_name, "file2") == 0);
    free_list(entries, count);

    stream = opendir(template);
    assert(stream != NULL);
    errno = EBUSY;
    assert(astra_test_readdir_r(stream, &current, &result) == 0);
    assert(result == &current);
    assert(errno == EBUSY);
    assert(closedir(stream) == 0);

    assert(unlinkat(directory, "file10", 0) == 0);
    assert(unlinkat(directory, "file2", 0) == 0);
    assert(unlinkat(directory, "alpha", 0) == 0);
    assert(close(directory) == 0);
    assert(rmdir(template) == 0);
    return 0;
}
