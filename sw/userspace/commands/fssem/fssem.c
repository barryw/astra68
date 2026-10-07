/*
 * fssem -- the filesystem semantic suite (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * Phase 1 exit evidence).
 *
 *   fssem CASE...
 *
 * Each case runs in a fresh directory /work/fssem-<pid>-<case> and prints
 * "FSSEM PASS <case>" or "FSSEM FAIL <case>: <detail>". The last line is
 * "FSSEM DONE passed P failed F"; the exit status is 0 only when F is 0.
 * Cases use the POSIX layer unless their name says otherwise; the raw-*
 * cases speak the storage protocol on a port directly.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

ASTRA_PROGRAM("fssem", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

typedef int (*FssemCase)(const char *directory);

static char failure[192];

static int fail(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(failure, sizeof(failure), format, arguments);
    va_end(arguments);
    return -1;
}

#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition))                                                  \
            return fail(__VA_ARGS__);                                      \
    } while (0)

static void join(char *out, size_t capacity, const char *directory,
                 const char *leaf)
{
    (void)snprintf(out, capacity, "%s/%s", directory, leaf);
}

static int write_text(const char *path, const char *text, int flags)
{
    size_t length = strlen(text);
    int fd = open(path, flags, 0644);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = write(fd, text, length);
    if (close(fd) != 0 || moved != (ssize_t)length)
        return -1;
    return 0;
}

static ssize_t read_text(const char *path, char *out, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    ssize_t moved;

    if (fd < 0)
        return -1;
    moved = read(fd, out, capacity - 1u);
    (void)close(fd);
    if (moved >= 0)
        out[moved] = '\0';
    return moved;
}

/* Create, read back, rename, unlink: the floor every later case stands on. */
static int case_basic(const char *directory)
{
    char a[160];
    char b[160];
    char text[32];
    struct stat st;

    join(a, sizeof(a), directory, "a");
    join(b, sizeof(b), directory, "b");
    CHECK(write_text(a, "basic", O_WRONLY | O_CREAT | O_EXCL) == 0,
          "create %s: %s", a, strerror(errno));
    CHECK(read_text(a, text, sizeof(text)) == 5 &&
          strcmp(text, "basic") == 0, "read back %s", a);
    CHECK(rename(a, b) == 0, "rename: %s", strerror(errno));
    CHECK(stat(a, &st) != 0 && errno == ENOENT, "old name survived rename");
    CHECK(stat(b, &st) == 0 && st.st_size == 5, "new name: %s",
          strerror(errno));
    CHECK(unlink(b) == 0, "unlink: %s", strerror(errno));
    return 0;
}

static const struct {
    const char *name;
    FssemCase run;
} cases[] = {
    {"basic", case_basic},
};

int main(int argc, char **argv)
{
    uint32_t passed = 0u;
    uint32_t failed = 0u;

    if (argc < 2) {
        fprintf(stderr, "usage: fssem CASE...\n");
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        char directory[128];
        size_t at = 0u;

        while (at < sizeof(cases) / sizeof(cases[0]) &&
               strcmp(cases[at].name, argv[index]) != 0)
            ++at;
        if (at == sizeof(cases) / sizeof(cases[0])) {
            printf("FSSEM FAIL %s: no such case\n", argv[index]);
            ++failed;
            continue;
        }
        (void)snprintf(directory, sizeof(directory), "/work/fssem-%ld-%s",
                       (long)getpid(), cases[at].name);
        failure[0] = '\0';
        if (mkdir(directory, 0755) != 0) {
            printf("FSSEM FAIL %s: mkdir %s: %s\n", cases[at].name,
                   directory, strerror(errno));
            ++failed;
            continue;
        }
        if (cases[at].run(directory) == 0) {
            printf("FSSEM PASS %s\n", cases[at].name);
            ++passed;
        } else {
            printf("FSSEM FAIL %s: %s\n", cases[at].name, failure);
            ++failed;
        }
        fflush(stdout);
    }
    printf("FSSEM DONE passed %u failed %u\n", (unsigned)passed,
           (unsigned)failed);
    return failed == 0u ? 0 : 1;
}
