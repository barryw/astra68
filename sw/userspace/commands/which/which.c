/* `which` -- where a bare command name resolves in the COMMANDS: union. */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_process.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("which", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static uint32_t out;

static void say(const char *text)
{
    (void)astra_print(out, text);
}

static void say_number(uint32_t value)
{
    char digits[12];
    char text[13];
    uint32_t count = 0u;
    uint32_t at = 0u;

    if (value == 0u)
        digits[count++] = '0';
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u)
        text[at++] = digits[--count];
    text[at] = '\0';
    say(text);
}

static uint32_t command_path(const char *name, char *path, uint32_t capacity)
{
    static const char prefix[] = "COMMANDS:";
    uint32_t at = 0u;

    while (prefix[at] != '\0') {
        if (at + 1u >= capacity)
            return ASTRA_STATUS_INVALID;
        path[at] = prefix[at];
        ++at;
    }
    for (uint32_t index = 0u; name[index] != '\0'; ++index) {
        if (at + 1u >= capacity)
            return ASTRA_STATUS_INVALID;
        path[at++] = name[index];
    }
    path[at] = '\0';
    return ASTRA_STATUS_OK;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const uint32_t *argv = NULL;
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    char typed[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 0u;
    uint32_t status;

    if (startup == NULL || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT"))
            out = capabilities[index].handle;
    if (out == 0u)
        return ASTRA_STATUS_ACCESS;
    if (startup->argc < 2u || argv == NULL) {
        say("which: name it\n");
        return ASTRA_STATUS_INVALID;
    }
    status = command_path((const char *)(uintptr_t)argv[1], typed,
                          sizeof(typed));
    if (status != ASTRA_STATUS_OK) {
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
    say_number(member);
    say("]\n");
    return ASTRA_STATUS_OK;
}
