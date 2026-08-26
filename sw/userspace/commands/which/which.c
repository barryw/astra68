/* `which` -- where a bare command name resolves in the COMMANDS: union. */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_path.h>
#include <astra/vfs_process.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("which", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static uint32_t out;

static void say(const char *text)
{
    (void)astra_print(out, text);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *standard_output;
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
    standard_output = astra_startup_capability(startup, "STDOUT");
    if (standard_output != NULL)
        out = standard_output->handle;
    if (out == 0u)
        return ASTRA_STATUS_ACCESS;
    name = astra_startup_argument(startup, 1u);
    if (name == NULL) {
        say("which: name it\n");
        return ASTRA_STATUS_INVALID;
    }
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
    (void)astra_print_u32(out, member);
    say("]\n");
    return ASTRA_STATUS_OK;
}
