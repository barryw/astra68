/* `which` -- where a bare command name resolves in the COMMANDS: union. */

#include <astra/program.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/vfs_path.h>
#include <astra/vfs_process.h>
#include <astra/vfs_union.h>

#include <stdio.h>

ASTRA_PROGRAM("which", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static void say(const char *text)
{
    (void)fputs(text, stdout);
}

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const char *name;
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    char typed[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    if (argc < 2) {
        say("which: name it\n");
        return ASTRA_STATUS_INVALID;
    }
    name = argv[1];
    status = astra_path_qualify("COMMANDS", "", name, typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        say("which: name too long, refused rather than cut\n");
        return (int)status;
    }
    status = astra_process_vfs_init(startup);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_assign_open(
            astra_process_vfs_assigns(), typed, ASTRA_RIGHT_READ,
            ASTRA_VFS_OPEN_READ, astra_process_vfs_assign_client, NULL,
            wire, sizeof(wire), &file, &size, &kind, &client, &member);
    if (status == ASTRA_VFS_OK)
        (void)astra_vfs_close(client, file);
    astra_process_vfs_close();
    if (status != ASTRA_VFS_OK) {
        say("which: not on any member\n");
        return (int)status;
    }
    say(wire);
    say(" [");
    (void)printf("%lu", (unsigned long)member);
    say("]\n");
    return ASTRA_STATUS_OK;
}
