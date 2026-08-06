#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <astra/vfs_path.h>

static void
test_splitting(void)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char rest[64];

    assert(astra_path_split("WORK:src/main.c", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_OK);
    assert(strcmp(name, "WORK") == 0);
    assert(strcmp(rest, "src/main.c") == 0);

    /* An assign alone names its root. */
    assert(astra_path_split("SYS:", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_OK);
    assert(strcmp(name, "SYS") == 0);
    assert(strcmp(rest, "") == 0);

    /* The name is canonicalised; the rest is byte-exact. */
    assert(astra_path_split("work:Makefile", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_OK);
    assert(strcmp(name, "WORK") == 0);
    assert(strcmp(rest, "Makefile") == 0);
}

static void
test_refusals(void)
{
    char name[ASTRA_CAPABILITY_NAME_MAX];
    char rest[64];
    char tiny[4];

    /* There is no root: a leading slash is not a path on this machine. */
    assert(astra_path_split("/etc/passwd", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_ERR_INVALID);
    /* A relative path is not absolute and is not this function's business. */
    assert(astra_path_split("src/main.c", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split(":rest", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split("", name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_split(NULL, name, sizeof(name), rest, sizeof(rest)) ==
           ASTRA_VFS_ERR_INVALID);
    /* A name longer than the field would be a different name if truncated. */
    assert(astra_path_split("AVERYLONGASSIGNNAME:x", name, sizeof(name), rest,
                            sizeof(rest)) == ASTRA_VFS_ERR_INVALID);
    /* Truncation would name a different file, so it is refused. */
    assert(astra_path_split("WORK:src/main.c", name, sizeof(name), tiny,
                            sizeof(tiny)) == ASTRA_VFS_ERR_INVALID);
}

static void
test_normalising(void)
{
    char out[64];

    assert(astra_path_normalise("src/./main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("src/lib/../main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("src//main.c", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/main.c") == 0);

    assert(astra_path_normalise("", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "") == 0);

    /* A trailing separator names the same directory. */
    assert(astra_path_normalise("src/lib/", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "src/lib") == 0);

    /* Descending and coming back names the root, which is the empty rest. */
    assert(astra_path_normalise("src/..", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "") == 0);

    /* The output that does not fit is refused rather than truncated. */
    assert(astra_path_normalise("src/main.c", out, 4u) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_path_normalise("src", out, 4u) == ASTRA_VFS_OK);
    assert(strcmp(out, "src") == 0);
}

static void
test_dotdot_cannot_climb_out(void)
{
    char out[64];

    /*
     * The whole security property of an assign-rooted namespace: there is no
     * string a program can build that names something above what it holds.
     */
    assert(astra_path_normalise("..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("src/../..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("../etc", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("src/../../etc", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("./..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(astra_path_normalise("/..", out, sizeof(out)) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /* A file legitimately named ".." is not a climb, and neither is "..." */
    assert(astra_path_normalise("...", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "...") == 0);
    assert(astra_path_normalise("..x", out, sizeof(out)) == ASTRA_VFS_OK);
    assert(strcmp(out, "..x") == 0);
    assert(astra_path_normalise("src/..x/../y", out, sizeof(out)) ==
           ASTRA_VFS_OK);
    assert(strcmp(out, "src/y") == 0);
}

int
main(void)
{
    test_splitting();
    test_refusals();
    test_normalising();
    test_dotdot_cannot_climb_out();
    puts("ASTRA VFS PATH PASS");
    return 0;
}
