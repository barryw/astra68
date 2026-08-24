/*
 * `cat` -- a file, or several, onto standard output.
 *
 * It was a builtin, and it moved out for the reason `ls` did: a builtin cannot
 * be replaced, cannot be run by anything but the shell carrying it, and is a
 * second implementation of reading a file to keep in step with the first. What
 * it gains by being a program is the thing it will be needed for -- once a
 * stream can be pointed somewhere other than the terminal, `cat` writing to
 * standard output is how a file gets written without an editor, and a builtin
 * writing straight into the terminal model could never have been redirected.
 *
 * A path with no assign is resolved against CWD:, which is where the shell
 * says the prompt is standing. That grant is the whole reason this can be a
 * program: the machine has no root and no current directory, so `cat foo`
 * would otherwise name nothing at all.
 */

#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("cat", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    CAT_CHUNK = 512u,
};

static uint32_t stdout_handle;
static uint32_t error_handle;

static void
say(const char *text)
{
    (void)astra_print(error_handle, text);
}

static void
complain(const char *path, uint32_t status)
{
    const char *text = astra_vfs_status_text(status);

    say("cat: ");
    say(path);
    say(": ");
    if (text != NULL) {
        say(text);
    } else {
        say("operation failed");
    }
    say("\n");
}

static int
emit(const char *path)
{
    static uint8_t chunk[CAT_CHUNK];
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t offset = 0u;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    char typed[ASTRA_VFS_PATH_MAX];
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    /*
     * "CWD" and an empty directory, not the shell's assign and directory: the
     * directory is already folded into the root the grant carries, so this
     * qualifies a bare name and leaves an ASSIGN:path one alone.
     */
    status = astra_process_path(path, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_assign_open(
            astra_process_vfs_assigns(), typed, ASTRA_RIGHT_READ,
            ASTRA_VFS_OPEN_READ, astra_process_vfs_assign_client, NULL,
            wire, sizeof(wire), &file, &size, &kind, &client, NULL);
    if (status != ASTRA_VFS_OK) {
        complain(path, status);
        return (int)status;
    }
    for (;;) {
        uint32_t moved = 0u;

        status = astra_vfs_port_read_bulk(client, file, offset, chunk,
                                          sizeof(chunk), &moved);
        if (status != ASTRA_VFS_OK) {
            complain(path, status);
            break;
        }
        /* A short read is normal: one message carries a bounded payload. */
        if (moved == 0u)
            break;
        offset += moved;
        if (astra_stream_write_all(stdout_handle, chunk, moved) !=
            ASTRA_SYSCALL_OK) {
            say("cat: ");
            say(path);
            say(": output stopped\n");
            status = ASTRA_VFS_ERR_IO;
            break;
        }
    }
    (void)astra_vfs_close(client, file);
    return status == ASTRA_VFS_OK ? 0 : (int)status;
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const uint32_t *argv = NULL;
    int result = 0;
    int named = 0;
    uint32_t status;

    if (startup == NULL || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT"))
            stdout_handle = capabilities[index].handle;
        else if (astra_capability_name_equal(capabilities[index].name,
                                             "STDERR"))
            error_handle = capabilities[index].handle;
    }
    if (stdout_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (error_handle == 0u)
        error_handle = stdout_handle;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    if (argv == NULL || startup->argc < 2u) {
        /*
         * No standard input to fall back on yet, so this says what it needs
         * rather than waiting on a stream nothing will write.
         */
        say("cat: needs a file\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        say("cat: filesystem unavailable\n");
        return (int)status;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        named = 1;
        /* Every file is attempted; the first refusal is what is returned. */
        one = emit(word);
        if (one != 0 && result == 0)
            result = one;
    }
    if (!named) {
        say("cat: needs a file\n");
        result = ASTRA_STATUS_INVALID;
    }
    astra_process_vfs_close();
    return result;
}
