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

#include <astra/posix.h>
#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>

ASTRA_PROGRAM("mkdir", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static int
make(AstraProcessFilesystem *process, const char *name)
{
    char typed[ASTRA_VFS_PATH_MAX];
    const char *text;
    uint32_t status;

    status = process->library->qualify("CWD", "", name, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = process->library->mkdir(&process->filesystem, typed);
    if (status == ASTRA_VFS_OK)
        return 0;
    text = astra_vfs_status_text(status);
    if (text != NULL)
        (void)fprintf(stderr, "mkdir: %s: %s\n", name, text);
    else
        (void)fprintf(stderr, "mkdir: %s: status %u\n", name, status);
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
        (void)fprintf(stderr, "mkdir: needs a name\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "mkdir: no filesystem: status %u\n", status);
        return (int)status;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        /* Every name is attempted; the first refusal is what is returned. */
        one = make(&process_filesystem, word);
        if (one != 0 && result == 0)
            result = one;
    }
    astra_process_filesystem_close(&process_filesystem);
    (void)fflush(stdout);
    return result;
}
