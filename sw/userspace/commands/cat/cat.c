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

#include <astra/posix.h>
#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("cat", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    CAT_CHUNK = 512u,
};

static void
complain(const char *path, uint32_t status)
{
    const char *text = astra_vfs_status_text(status);

    if (text != NULL)
        (void)fprintf(stderr, "cat: %s: %s\n", path, text);
    else
        (void)fprintf(stderr, "cat: %s: status %u\n", path, status);
}

static int
emit(AstraProcessFilesystem *process, const char *path)
{
    static uint8_t chunk[CAT_CHUNK];
    char typed[ASTRA_VFS_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t status;

    /*
     * "CWD" and an empty directory, not the shell's assign and directory: the
     * directory is already folded into the root the grant carries, so this
     * qualifies a bare name and leaves an ASSIGN:path one alone.
     */
    status = astra_process_path(process, path, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = process->library->open(&process->filesystem, typed,
                                        ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) {
        complain(path, status);
        return (int)status;
    }
    for (;;) {
        uint32_t moved = 0u;

        status = process->library->read(&file, chunk, sizeof(chunk), &moved);
        if (status != ASTRA_VFS_OK) {
            complain(path, status);
            break;
        }
        /* A short read is normal: one message carries a bounded payload. */
        if (moved == 0u)
            break;
        if (fwrite(chunk, 1u, moved, stdout) != moved) {
            (void)fprintf(stderr, "cat: %s: output stopped\n", path);
            status = ASTRA_VFS_ERR_IO;
            break;
        }
    }
    (void)process->library->close(&file);
    return status == ASTRA_VFS_OK ? 0 : (int)status;
}

int
astra_main(const AstraStartupInfo *startup)
{
    AstraProcessFilesystem process_filesystem =
        ASTRA_PROCESS_FILESYSTEM_INIT;
    const uint32_t *argv = NULL;
    int result = 0;
    int named = 0;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    if (argv == NULL || startup->argc < 2u) {
        /*
         * No standard input to fall back on yet, so this says what it needs
         * rather than waiting on a stream nothing will write.
         */
        (void)fprintf(stderr, "cat: needs a file\n");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "cat: no filesystem: status %u\n", status);
        return (int)status;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];
        int one;

        if (word == NULL || word[0] == '\0')
            continue;
        named = 1;
        /* Every file is attempted; the first refusal is what is returned. */
        one = emit(&process_filesystem, word);
        if (one != 0 && result == 0)
            result = one;
    }
    if (!named) {
        (void)fprintf(stderr, "cat: needs a file\n");
        result = ASTRA_STATUS_INVALID;
    }
    astra_process_filesystem_close(&process_filesystem);
    (void)fflush(stdout);
    return result;
}
