#define _GNU_SOURCE
#include "astra68-resolve.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char top[] = "/tmp/astra68-resolve-XXXXXX";
static int root;

static void make_directory(const char *path)
{
    assert(mkdirat(root, path, 0755) == 0);
}

static void make_file(const char *path)
{
    int fd = openat(root, path, O_WRONLY | O_CREAT | O_EXCL, 0644);

    assert(fd >= 0);
    assert(write(fd, "sentinel", 8) == 8);
    assert(close(fd) == 0);
}

static void make_link(const char *target, const char *path)
{
    assert(symlinkat(target, root, path) == 0);
}

typedef struct Case {
    const char *base;
    const char *path;
    unsigned flags;
    int status;
    uint32_t escape;
    uint32_t consumed;
    const char *names;
    const char *missing;
    const char *continuation;
} Case;

static const Case cases[] = {
    {"", "a/f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "/a/f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "//a///f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "a/./f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "a/b/../f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "a/..", 0, 0, 0, 0, ".", NULL, NULL},
    {"", "..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 2, NULL, NULL, "/"},
    {"", "a/../../x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 7, NULL, NULL, "/x"},
    {"", "/..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 3, NULL, NULL, "/"},
    {"", "l/f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "l", 0, 0, 0, 0, "l", NULL, NULL},
    {"", "l", ASTRA_RESOLVE_FOLLOW_FINAL, 0, 0, 0, "a", NULL, NULL},
    {"", "l/", 0, 0, 0, 0, "a", NULL, NULL},
    {"", "deep/../f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "abs/x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 3, NULL, NULL, "/etc/x"},
    {"", "esc/sentinel", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 3, NULL, NULL, "/outside/sentinel"},
    {"", "up/a/f", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 2, NULL, NULL, "/a/f"},
    {"", "nested/x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 6, NULL, NULL, "/outside/x"},
    {"", "within/f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "within/../../x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 12, NULL, NULL, "/x"},
    {"", "loop1", ASTRA_RESOLVE_FOLLOW_FINAL, -ELOOP, 0, 0, NULL, NULL, NULL},
    {"", "chain0", ASTRA_RESOLVE_FOLLOW_FINAL, 0, 0, 0, "a/f", NULL, NULL},
    {"", "too-many", ASTRA_RESOLVE_FOLLOW_FINAL, -ELOOP, 0, 0, NULL, NULL, NULL},
    {"", "a/f/", 0, -ENOTDIR, 0, 0, NULL, NULL, NULL},
    {"", "a/f/../f", 0, -ENOTDIR, 0, 0, NULL, NULL, NULL},
    {"", "missing/../a/f", 0, -ENOENT, 0, 0, NULL, NULL, NULL},
    {"", "a/missing", 0, 0, 0, 0, NULL, "missing", NULL},
    {"", "a/new/", 0, 0, 0, 0, NULL, "new", NULL},
    {"a/b", "../f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"a/b", "../../..", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_PARENT, 8, NULL, NULL, "/"},
    {"a/b", "/a/f", 0, 0, 0, 0, "a/f", NULL, NULL},
    {"", "a/", ASTRA_RESOLVE_ENTRY, 0, 0, 0, "a", NULL, NULL},
    {"", "l/", ASTRA_RESOLVE_ENTRY, -ENOTDIR, 0, 0, NULL, NULL, NULL},
    {"", "nested-abs/x", 0, -EXDEV, ASTRA_RESOLVE_ESCAPE_SYMLINK, 10, NULL, NULL, "/system/x"},
};

static void check_cases(void)
{
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const Case *test = &cases[index];
        int base = test->base[0] ? openat(root, test->base, O_RDONLY | O_DIRECTORY) : root;
        AstraResolution out;
        int status;

        assert(base >= 0);
        status = astra_resolve(root, base, test->path, test->flags, ASTRA_RESOLVE_LINK_MAX, &out);
        if (status != test->status) {
            fprintf(stderr, "case %zu %s: got %d, expected %d\n", index,
                    test->path, status, test->status);
            abort();
        }
        if (status == -EXDEV) {
            assert(out.escape == test->escape);
            assert(out.consumed == test->consumed);
            assert(test->continuation != NULL);
            assert(strcmp(out.continuation, test->continuation) == 0);
        }
        if (status == 0) {
            if (test->names != NULL) {
                struct stat expected, actual;

                assert(fstatat(root, test->names, &expected, AT_SYMLINK_NOFOLLOW) == 0);
                assert(fstatat(out.parent, out.leaf[0] ? out.leaf : ".", &actual,
                               AT_SYMLINK_NOFOLLOW) == 0);
                assert(actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino);
            }
            if (test->missing != NULL)
                assert(strcmp(out.leaf, test->missing) == 0);
            assert(close(out.parent) == 0);
        } else {
            assert(out.parent == -1);
        }
        if (base != root)
            assert(close(base) == 0);
    }
}

static void check_moved_base_and_open(void)
{
    char outside[512];
    AstraResolution out;
    int base = openat(root, "a/b", O_RDONLY | O_DIRECTORY);

    assert(base >= 0);
    {
        char path[32];
        uint32_t length;
        assert(astra_resolve_directory_path(root, base, path, sizeof(path),
                                             &length) == 0);
        assert(length == 4u && memcmp(path, "/a/b", length) == 0);
        assert(astra_resolve_directory_path(root, base, path, 2u, &length) ==
               -ERANGE);
    }
    assert(astra_resolve(root, base, "f", ASTRA_RESOLVE_FOLLOW_FINAL, ASTRA_RESOLVE_LINK_MAX, &out) == 0);
    assert(close(out.parent) == 0);
    make_directory("new-parent");
    make_file("new-parent/f");
    assert(renameat(root, "a/b", root, "new-parent/b") == 0);
    {
        char path[32];
        uint32_t length;
        assert(astra_resolve_directory_path(root, base, path, sizeof(path),
                                             &length) == 0);
        assert(length == 13u && memcmp(path, "/new-parent/b", length) == 0);
    }
    assert(astra_resolve(root, base, "../f", ASTRA_RESOLVE_FOLLOW_FINAL,
                         ASTRA_RESOLVE_LINK_MAX, &out) == 0);
    {
        struct stat found, expected;
        assert(fstatat(out.parent, out.leaf, &found, 0) == 0);
        assert(fstatat(root, "new-parent/f", &expected, 0) == 0);
        assert(found.st_dev == expected.st_dev && found.st_ino == expected.st_ino);
    }
    assert(close(out.parent) == 0);
    assert(renameat(root, "new-parent/b", root, "a/b") == 0);
    snprintf(outside, sizeof(outside), "%s/outside", top);
    assert(renameat(root, "a/b", AT_FDCWD, outside) == 0);
    {
        char path[32];
        uint32_t length;
        assert(astra_resolve_directory_path(root, base, path, sizeof(path),
                                             &length) == -ENOENT);
    }
    assert(astra_resolve(root, base, "f", 0, ASTRA_RESOLVE_LINK_MAX, &out) == -ESTALE);
    assert(astra_resolve_open_path(root, base, "f", O_RDONLY, 0,
                                   ASTRA_RESOLVE_FOLLOW_FINAL,
                                   ASTRA_RESOLVE_LINK_MAX, &out) == -ESTALE);
    assert(astra_resolve(root, base, "", 0, ASTRA_RESOLVE_LINK_MAX, &out) == -ESTALE);
    assert(astra_resolve(root, base, "..", 0, ASTRA_RESOLVE_LINK_MAX, &out) == -ESTALE);
    assert(close(base) == 0);
    assert(renameat(AT_FDCWD, outside, root, "a/b") == 0);

    assert(astra_resolve(root, root, "a/b/f", 0, ASTRA_RESOLVE_LINK_MAX, &out) == 0);
    assert(renameat(root, "a/b", AT_FDCWD, outside) == 0);
    assert(astra_resolve_open(root, &out, O_WRONLY | O_TRUNC, 0) == -ESTALE);
    {
        struct stat st;
        int fd = openat(out.parent, out.leaf, O_RDONLY);

        assert(fd >= 0 && fstat(fd, &st) == 0 && st.st_size == 8);
        assert(close(fd) == 0);
    }
    assert(close(out.parent) == 0);
    assert(renameat(AT_FDCWD, outside, root, "a/b") == 0);
}

static void check_open_paths(void)
{
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const Case *test = &cases[index];
        AstraResolution expected, actual;
        int base = test->base[0] ? openat(root, test->base, O_RDONLY | O_DIRECTORY) : root;
        int old_fd = astra_resolve(root, base, test->path, test->flags,
                                   ASTRA_RESOLVE_LINK_MAX, &expected);
        int new_fd;

        assert(base >= 0);
        if (old_fd == 0) {
            old_fd = astra_resolve_open(root, &expected, O_PATH, 0);
            assert(close(expected.parent) == 0);
            if (old_fd == -ENOENT)
                expected.missing = 1u;
        }
        new_fd = astra_resolve_open_path(root, base, test->path, O_PATH, 0,
                                         test->flags, ASTRA_RESOLVE_LINK_MAX, &actual);
        if ((old_fd < 0 && new_fd != old_fd) || (old_fd >= 0 && new_fd < 0)) {
            fprintf(stderr, "open case %zu %s: got %d, expected %d\n",
                    index, test->path, new_fd, old_fd);
            abort();
        }
        assert(actual.parent == -1);
        assert(actual.links == expected.links);
        if (old_fd >= 0) {
            struct stat old_st, new_st;

            assert(fstat(old_fd, &old_st) == 0 && fstat(new_fd, &new_st) == 0);
            assert(old_st.st_dev == new_st.st_dev && old_st.st_ino == new_st.st_ino);
            assert(close(old_fd) == 0 && close(new_fd) == 0);
        } else {
            assert(actual.missing == expected.missing);
            assert(actual.escape == expected.escape);
            assert(actual.consumed == expected.consumed);
            assert(strcmp(actual.continuation, expected.continuation) == 0);
        }
        if (base != root)
            assert(close(base) == 0);
    }
}

static int remove_entry(const char *path, const struct stat *st, int type,
                        struct FTW *walk)
{
    (void)st;
    (void)type;
    (void)walk;
    return remove(path);
}

int main(void)
{
    char directory[512];

    assert(mkdtemp(top) != NULL);
    snprintf(directory, sizeof(directory), "%s/root", top);
    assert(mkdir(directory, 0755) == 0);
    root = open(directory, O_RDONLY | O_DIRECTORY);
    assert(root >= 0);
    make_directory("a");
    make_directory("a/b");
    make_file("a/f");
    make_file("a/b/f");
    make_link("a", "l");
    make_link("a/b", "deep");
    make_link("/etc", "abs");
    make_link("../outside", "esc");
    make_link("..", "up");
    make_link("a/b/..", "within");
    make_link("a/escape", "nested");
    make_link("a/absolute", "nested-abs");
    make_link("/system", "a/absolute");
    make_link("../../outside", "a/escape");
    make_link("loop2", "loop1");
    make_link("loop1", "loop2");
    for (unsigned index = 0; index < ASTRA_RESOLVE_LINK_MAX; ++index) {
        char name[32], target[32];

        snprintf(name, sizeof(name), "chain%u", index);
        snprintf(target, sizeof(target), "chain%u", index + 1);
        make_link(index + 1 == ASTRA_RESOLVE_LINK_MAX ? "a/f" : target, name);
    }
    make_link("chain0", "too-many");
    check_cases();
    check_open_paths();
    check_moved_base_and_open();
    assert(close(root) == 0);
    assert(nftw(top, remove_entry, 32, FTW_DEPTH | FTW_PHYS) == 0);
    puts("ASTRA68 RESOLVE PASS");
    return 0;
}
