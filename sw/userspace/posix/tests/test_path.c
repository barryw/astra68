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

    assert(astra_posix_path_is_absolute("/WORK/note"));
    assert(astra_posix_path_is_absolute("WORK:note"));
    assert(astra_posix_path_is_absolute("work:"));
    assert(!astra_posix_path_is_absolute("note"));
    assert(!astra_posix_path_is_absolute("directory/a:b"));
    assert(!astra_posix_path_is_absolute(":note"));
    assert(!astra_posix_path_is_absolute(NULL));

    expect("/WORK/project", "notes.txt", 1,
           "/WORK/project/notes.txt", "WORK:project/notes.txt");
    expect_invalid("/WORK", "");
    expect("/WORK/project", "../notes.txt", 1,
           "/WORK/notes.txt", "WORK:notes.txt");
    expect("/WORK", "/SYS//lib/./../vim", 1,
           "/SYS/vim", "SYS:vim");
    expect("/WORK", "commands:vim", 1,
           "/commands/vim", "COMMANDS:vim");
    expect("/WORK", "COMMANDS:bin/../hello", 1,
           "/COMMANDS/hello", "COMMANDS:hello");
    expect_invalid("/WORK", "COMMANDS:../hello");
    expect("/WORK", "HOME:/.zshrc", 1,
           "/HOME/.zshrc", "HOME:.zshrc");
    expect("/WORK", "../../..", 0, "/", "");
    expect("/", "WORK", 1, "/WORK", "WORK:");
    expect("/A/B/C", "../../D", 1, "/A/D", "A:D");
    expect_native("/WORK/project", "notes.txt", 1,
                  "WORK:project/notes.txt");
    /* exec PATH lookup: slash-absolute enters COMMANDS, relative stays in CWD. */
    expect_native("/WORK", "/commands/hello", 1, "COMMANDS:hello");
    expect_native("/WORK", "commands/hello", 1, "WORK:commands/hello");
    expect_native("/WORK", "commands:vim", 1, "COMMANDS:vim");
    expect_native("/WORK", "/SYS//lib/./../vim", 1, "SYS:vim");
    expect_native("/WORK", "../../..", 0, "");
    assert(astra_posix_link_target_to_native("../note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "../note") == 0);
    assert(astra_posix_link_target_to_native("/work/note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "WORK:note") == 0);
    assert(astra_posix_link_target_to_native("work:note", target,
                                             sizeof(target)) == 0);
    assert(strcmp(target, "WORK:note") == 0);
    assert(astra_posix_link_target_to_posix("WORK:note", target,
                                            sizeof(target)) == 0);
    assert(strcmp(target, "/WORK/note") == 0);
    assert(astra_posix_link_target_to_posix("../note", target,
                                            sizeof(target)) == 0);
    assert(strcmp(target, "../note") == 0);
    return 0;
}
