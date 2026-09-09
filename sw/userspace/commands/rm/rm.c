/*
 * `rm` -- removes a name, or several.
 *
 * A builtin until now. Moving it out is worth more here than anywhere else in
 * this set: a builtin runs with everything the shell holds, so `rm` refusing
 * to touch a read-only member was the shell being careful. As a program it
 * holds only what it was granted, and the refusal comes from the member --
 * which is the difference between a rule and a guarantee.
 *
 * It also has to keep a distinction the shell's builtin already made and which
 * is easy to lose: a name on no member at all is "not found", while a member
 * that refused on rights has said nothing about whether the name is there.
 * Reporting the second as the first is how a machine tells a person a file
 * does not exist when it does.
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

ASTRA_PROGRAM("rm", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static void
say(const char *text)
{
    (void)fputs(text, stderr);
}

static int remove_name(const char *name)
{
    AstraVfsClient *client = NULL;
    char typed[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    const char *text;
    uint32_t status;

    status = astra_process_path(name, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_assign_lstat(
            astra_process_vfs_assigns(), typed, ASTRA_RIGHT_WRITE,
            astra_process_vfs_assign_client, NULL, wire, sizeof(wire), NULL,
            &client, NULL, NULL);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_unlink(client, wire);
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
    int result = 0;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    if (argc < 2) {
        say("rm: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_vfs_init(startup);
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
        one = remove_name(word);
        if (one != 0 && result == 0)
            result = one;
    }
    astra_process_vfs_close();
    return result;
}
