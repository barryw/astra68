#include "../src/path.h"

#include <astra/vfs_service.h>

#include <assert.h>
#include <string.h>

static void expect(const char *cwd, const char *path, int result,
                   const char *normal, const char *native)
{
    char got_normal[ASTRA_VFS_PATH_MAX];
    char got_native[ASTRA_VFS_PATH_MAX];

    assert(astra_posix_path_resolve(cwd, path, got_normal,
                                    sizeof(got_normal), got_native,
                                    sizeof(got_native)) == result);
    assert(strcmp(got_normal, normal) == 0);
    assert(strcmp(got_native, native) == 0);
}

static void expect_native(const char *cwd, const char *path, int result,
                          const char *native)
{
    char got[ASTRA_VFS_PATH_MAX];

    assert(astra_posix_path_resolve_native(cwd, path, got, sizeof(got)) ==
           result);
    assert(strcmp(got, native) == 0);
}

static void expect_invalid(const char *cwd, const char *path)
{
    char normal[ASTRA_VFS_PATH_MAX];
    char native[ASTRA_VFS_PATH_MAX];

    assert(astra_posix_path_resolve(cwd, path, normal, sizeof(normal), native,
                                    sizeof(native)) == -1);
    assert(astra_posix_path_resolve_native(cwd, path, native,
                                           sizeof(native)) == -1);
}

int main(void)
{
    char target[ASTRA_VFS_PATH_MAX + 2u];

    assert(astra_posix_path_is_absolute("/work/note"));
    assert(astra_posix_path_is_absolute("/dh0/docs/readme"));
    assert(!astra_posix_path_is_absolute("WORK:note"));
    assert(!astra_posix_path_is_absolute("work:"));
    assert(!astra_posix_path_is_absolute("note"));
    assert(!astra_posix_path_is_absolute("directory/a:b"));
    assert(!astra_posix_path_is_absolute(":note"));
    assert(!astra_posix_path_is_absolute(NULL));

    expect("/work/project", "notes.txt", 1,
           "/work/project/notes.txt", "/work/project/notes.txt");
    expect_invalid("/work", "");
    expect("/work/project", "../notes.txt", 1,
           "/work/notes.txt", "/work/notes.txt");
    expect("/work", "/sys//lib/./../vim", 1,
           "/sys/vim", "/sys/vim");
    expect("/work", "/dh0/docs/readme", 1,
           "/dh0/docs/readme", "/dh0/docs/readme");
    expect("/work", "commands:vim", 1,
           "/work/commands:vim", "/work/commands:vim");
    expect("/work", "commands/bin/../hello", 1,
           "/work/commands/hello", "/work/commands/hello");
    expect("/work", "../../..", 0, "/", "/");
    expect("/", "work", 1, "/work", "/work");
    expect("/a/b/c", "../../d", 1, "/a/d", "/a/d");
    expect_native("/work/project", "notes.txt", 1,
                  "/work/project/notes.txt");
    /* PATH lookup and ordinary relative names use the same slash grammar. */
    expect_native("/work", "/commands/hello", 1, "/commands/hello");
    expect_native("/work", "commands/hello", 1, "/work/commands/hello");
    expect_native("/work", "commands:vim", 1, "/work/commands:vim");
    expect_native("/work", "../../..", 0, "/");
    assert(astra_posix_link_target_to_native("../note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "../note") == 0);
    assert(astra_posix_link_target_to_native("/work/note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "/work/note") == 0);
    assert(astra_posix_link_target_to_native("work:note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "work:note") == 0);
    assert(astra_posix_link_target_to_posix("work:note", target,
                                            sizeof(target)) == 0);
    assert(strcmp(target, "work:note") == 0);
    assert(astra_posix_link_target_to_posix("/sys/apps", target,
                                            sizeof(target)) == 0);
    assert(strcmp(target, "/sys/apps") == 0);
    assert(astra_posix_link_target_to_posix("../note", target,
                                            sizeof(target)) == 0);
    assert(strcmp(target, "../note") == 0);
    return 0;
}
