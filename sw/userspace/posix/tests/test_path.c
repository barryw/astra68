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

int main(void)
{
    expect("/WORK/project", "notes.txt", 1,
           "/WORK/project/notes.txt", "WORK:project/notes.txt");
    expect("/WORK/project", "../notes.txt", 1,
           "/WORK/notes.txt", "WORK:notes.txt");
    expect("/WORK", "/SYS//lib/./../vim", 1,
           "/SYS/vim", "SYS:vim");
    expect("/WORK", "commands:vim", 1,
           "/commands/vim", "COMMANDS:vim");
    expect("/WORK", "../../..", 0, "/", "");
    expect("/", "WORK", 1, "/WORK", "WORK:");
    expect("/A/B/C", "../../D", 1, "/A/D", "A:D");
    return 0;
}
