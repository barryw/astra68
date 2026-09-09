/*
 * `mkdir` -- a directory, or several.
 *
 * A builtin until now, and out for the same reason the others left: what the
 * shell carries cannot be replaced and cannot be run by anything but the
 * shell. This one is small enough that moving it looks like tidiness, and is
 * not: `mkdir` is the first thing anything scripted does, and a builtin is not
 * available to a script.
 *
 * A name with no assign is resolved against CWD:, the place the shell says the
 * prompt is standing.
 */

#include <astra/vfs_process.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_union.h>

#include <stdio.h>

ASTRA_PROGRAM("mkdir", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static void say(const char *text)
{
    (void)fputs(text, stderr);
}

static int make(const char *name)
{
    AstraVfsClient *client = NULL;
    char typed[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    const char *text;
    uint32_t status;

    status = astra_process_path(name, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_assign_primary(
            astra_process_vfs_assigns(), typed, ASTRA_RIGHT_WRITE,
            astra_process_vfs_assign_client, NULL, wire, sizeof(wire),
            &client, NULL);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_mkdir(client, wire);
    if (status == ASTRA_VFS_OK)
        return 0;
    text = astra_vfs_status_text(status);
    say("mkdir: ");
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
    int result = 0;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    if (argc < 2) {
        say("mkdir: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        say("mkdir: filesystem unavailable\n");
        return (int)status;
    }
    for (int index = 1; index < argc; ++index) {
        const char *word = argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        /* Every name is attempted; the first refusal is what is returned. */
        one = make(word);
        if (one != 0 && result == 0)
            result = one;
    }
    astra_process_vfs_close();
    return result;
}
