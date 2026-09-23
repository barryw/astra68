/*
 * `rm` -- removes a name, or several.
 *
 * Use the Filesystem Kit so the protected root namespace cannot be bypassed
 * by resolving a command argument directly to a backend path.
 */

#include <astra/vfs_process.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>

ASTRA_PROGRAM("rm", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static void
say(const char *text)
{
    (void)fputs(text, stderr);
}

static int remove_name(AstraProcessFilesystem *filesystem, const char *name)
{
    char typed[ASTRA_VFS_PATH_MAX];
    const char *text;
    uint32_t status;

    status = astra_process_path(name, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_unlink(&filesystem->filesystem, typed);
    if (status == ASTRA_VFS_OK)
        return 0;
    text = astra_vfs_status_text(status);
    say("rm: ");
    say(name);
    say(": ");
    if (text != NULL) {
        say(text);
    } else {
        say("operation failed");
    }
    say("\n");
    return (int)status;
}

int
main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    int result = 0;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    if (argc < 2) {
        say("rm: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_filesystem_open(&filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        say("rm: filesystem unavailable\n");
        return (int)status;
    }
    for (int index = 1; index < argc; ++index) {
        const char *word = argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        /* Every name is attempted; the first refusal is what is returned. */
        one = remove_name(&filesystem, word);
        if (one != 0 && result == 0)
            result = one;
    }
    astra_process_filesystem_close(&filesystem);
    return result;
}
