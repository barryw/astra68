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
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("rm", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static uint32_t error_handle;

static void
say(const char *text)
{
    (void)astra_print(error_handle, text);
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
        status = astra_vfs_assign_stat(
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
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capability;
    int result = 0;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    capability = astra_startup_capability(startup, "STDERR");
    if (capability == NULL)
        capability = astra_startup_capability(startup, "STDOUT");
    if (capability != NULL)
        error_handle = capability->handle;
    if (error_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (startup->argc < 2u) {
        say("rm: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        say("rm: filesystem unavailable\n");
        return (int)status;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = astra_startup_argument(startup, index);
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
