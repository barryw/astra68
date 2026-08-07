/*
 * `which` -- where a bare name would be found, and on which member.
 *
 * The question a search path cannot answer. A union is a binding rather than a
 * string: its members were joined by the startup sequence, they carry their own
 * rights, and no program can extend the list -- so "which one would run" has an
 * answer, and this prints it.
 *
 * It is also the proof that a union crosses a process boundary. This program
 * holds COMMANDS: as two grants with two roots, seeds them into its own
 * namespace, and loops them with the same Kit function the shell uses.
 */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("which", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define WHICH_PATH_MAX 128u
#define WHICH_CLIENT_MAX 4u

/* Statically allocated, because a user thread gets one 4 KiB stack. */
static AstraAssignTable assigns;
static uint32_t out;

/*
 * One client per distinct handle, made when a member first needs it. Two
 * members of one union on one volume share a handle and therefore a client;
 * two on different volumes would not, and nothing here has to change for that.
 */
static struct {
    uint32_t handle;
    AstraVfsClient client;
    int connected;
} clients[WHICH_CLIENT_MAX];

static void
say(const char *text)
{
    (void)astra_print(out, text);
}

static void
say_number(uint32_t value)
{
    char digits[12];
    char text[13];
    uint32_t index = 0u;
    uint32_t at = 0u;

    if (value == 0u) {
        digits[index++] = '0';
    }
    while (value != 0u && index < sizeof(digits)) {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (index != 0u) {
        text[at++] = digits[--index];
    }
    text[at] = '\0';
    (void)astra_print(out, text);
}

static AstraVfsClient *
client_for(const AstraAssign *assign, void *context)
{
    (void)context;
    for (uint32_t index = 0u; index < WHICH_CLIENT_MAX; ++index) {
        if (clients[index].connected &&
            clients[index].handle == assign->handle) {
            return &clients[index].client;
        }
    }
    for (uint32_t index = 0u; index < WHICH_CLIENT_MAX; ++index) {
        if (clients[index].connected) {
            continue;
        }
        clients[index].handle = assign->handle;
        if (astra_vfs_connect(&clients[index].client,
                              astra_vfs_port_transport,
                              &clients[index].handle) != ASTRA_VFS_OK) {
            return NULL;
        }
        clients[index].connected = 1;
        return &clients[index].client;
    }
    return NULL;
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const uint32_t *argv = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    AstraVfsClient *client = NULL;
    char typed[WHICH_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t member = 0u;
    uint32_t at = 0u;
    uint32_t status;

    if (startup == NULL || startup->capabilities_address == 0u) {
        return ASTRA_STATUS_INVALID;
    }
    capabilities =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;
    if (startup->argc != 0u && startup->argv_address != 0u) {
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    }
    if (astra_assign_seed(&assigns, capabilities,
                          startup->capability_count) != ASTRA_VFS_OK) {
        return ASTRA_STATUS_INVALID;
    }
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT")) {
            out = capabilities[index].handle;
        }
    }
    if (out == 0u) {
        return ASTRA_STATUS_ACCESS;
    }
    if (startup->argc < 2u || argv == NULL) {
        say("which: name it\n");
        return ASTRA_STATUS_INVALID;
    }

    at = 0u;
    {
        static const char prefix[] = "COMMANDS:";
        const char *word = (const char *)(uintptr_t)argv[1];

        while (prefix[at] != '\0' && at + 1u < sizeof(typed)) {
            typed[at] = prefix[at];
            ++at;
        }
        for (uint32_t index = 0u;
             word[index] != '\0' && at + 1u < sizeof(typed); ++index) {
            typed[at++] = word[index];
        }
        typed[at] = '\0';
    }

    status = astra_vfs_assign_open(&assigns, typed, ASTRA_RIGHT_READ,
                                   ASTRA_VFS_OPEN_READ, client_for, NULL,
                                   wire, sizeof(wire), &file, NULL, NULL,
                                   &client, &member);
    if (status != ASTRA_VFS_OK) {
        say("which: not on any member, status ");
        say_number(status);
        say("\n");
        return (int)status;
    }
    (void)astra_vfs_close(client, file);
    /* The path that answered, and the member index that is the answer. */
    say(wire);
    say(" [");
    say_number(member);
    say("]\n");
    return ASTRA_STATUS_OK;
}
