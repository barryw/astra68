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

#include <astra/posix.h>
#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>

ASTRA_PROGRAM("rm", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static int
remove_name(AstraProcessFilesystem *process, const char *name)
{
    char typed[ASTRA_VFS_PATH_MAX];
    const char *text;
    uint32_t status;

    status = astra_process_path(process, name, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = process->library->unlink(&process->filesystem, typed);
    if (status == ASTRA_VFS_OK)
        return 0;
    text = astra_vfs_status_text(status);
    if (text != NULL)
        (void)fprintf(stderr, "rm: %s: %s\n", name, text);
    else
        (void)fprintf(stderr, "rm: %s: status %u\n", name, status);
    return (int)status;
}

int
astra_main(const AstraStartupInfo *startup)
{
    AstraProcessFilesystem process_filesystem =
        ASTRA_PROCESS_FILESYSTEM_INIT;
    const uint32_t *argv = NULL;
    int result = 0;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    if (argv == NULL || startup->argc < 2u) {
        (void)fprintf(stderr, "rm: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "rm: no filesystem: status %u\n", status);
        return (int)status;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        /* Every name is attempted; the first refusal is what is returned. */
        one = remove_name(&process_filesystem, word);
        if (one != 0 && result == 0)
            result = one;
    }
    astra_process_filesystem_close(&process_filesystem);
    (void)fflush(stdout);
    return result;
}
