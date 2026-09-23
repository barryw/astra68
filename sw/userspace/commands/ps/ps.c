/*
 * `ps` -- the process list, read out of PROC:.
 *
 * It looks like `ps` because that is what it is for, and it gets there without
 * the thing that makes a Unix `ps` work. There is no global process namespace
 * here: `PROCESS_INFO` is scoped to a handle the caller already holds, so a
 * process cannot enumerate its neighbours by counting upwards. What this reads
 * is a *view* -- PROC:, rendered by the supervisor, which can answer because it
 * holds the handles, and visible to this program only because the mount was
 * granted to it. See docs/OBSERVABILITY.md.
 *
 * So the familiar shape survives and the property underneath it changes: a
 * program with no PROC: mount prints nothing and says why, rather than seeing
 * an empty machine.
 *
 * GEN is not decoration. A number alone must never name a process here,
 * because numbers get reused; every identifier this prints carries the
 * generation observed with it, which is what a later control operation has to
 * present for the kernel to accept it.
 */

#include <astra/program.h>
#include <astra/posix.h>
#include <astra/proc.h>
#include <astra/divide.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>

#include <stdio.h>

#include "ps_support.h"

ASTRA_PROGRAM("ps", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    PS_OUTPUT_MAX = 2048u,
};

static char output[PS_OUTPUT_MAX];
static AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
static const char header[] =
    "       PID   GEN STATE  PRI  NI THR MEM(K)  CPU%        TIME    RUNS "
    "  CALLS HND COMMAND\n";
static uint32_t output_used;

static void
say_error(const char *text)
{
    (void)fputs(text, stderr);
}

static int
flush_output(void)
{
    int written = fwrite(output, 1u, output_used, stdout) == output_used;

    output_used = 0u;
    return written;
}

static int
append_output(const char *text, uint32_t length)
{
    if (length > sizeof(output) - output_used && !flush_output())
        return 0;
    for (uint32_t index = 0u; index < length; ++index)
        output[output_used++] = text[index];
    return 1;
}

static int append_row(const AstraProcSnapshot *record)
{
    uint32_t length = astra_ps_format_row(NULL, 0u, record);

    if (length == 0u || length > sizeof(output))
        return 0;
    if (length > sizeof(output) - output_used && !flush_output())
        return 0;
    if (astra_ps_format_row(output + output_used,
                            (uint32_t)sizeof(output) - output_used,
                            record) != length)
        return 0;
    output_used += length;
    return 1;
}

int
main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    AstraProcSnapshot *records = NULL;
    uint32_t listed = 0u;
    uint32_t moved = 0u;
    uint32_t status;

    (void)argc;
    (void)argv;
    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    status = astra_process_filesystem_open(&filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: filesystem unavailable\n");
        return (int)status;
    }
    status = astra_process_read_file_alloc(
        &filesystem, "/proc/snapshot", (void **)&records, &moved);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: PROC: not granted to this program\n");
        astra_process_filesystem_close(&filesystem);
        return (int)status;
    }
    if (moved % sizeof(records[0]) != 0u)
        status = ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK &&
        !append_output(header, sizeof(header) - 1u))
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK) {
        for (uint32_t index = 0u;
             status == ASTRA_VFS_OK && index < moved / sizeof(records[0]);
             ++index) {
            const AstraProcessInfo *process = &records[index].process;

            if (process->id == 0u)
                continue;
            if (process->size != sizeof(*process)) {
                status = ASTRA_VFS_ERR_PROTOCOL;
                break;
            }
            if (!append_row(&records[index]))
                status = ASTRA_VFS_ERR_IO;
            else
                ++listed;
        }
    }
    astra_process_filesystem_close(&filesystem);
    astra_runtime_deallocate(records);
    if (status == ASTRA_VFS_OK && !flush_output())
        status = ASTRA_VFS_ERR_IO;
    if (status != ASTRA_VFS_OK)
        return (int)status;
    return listed != 0u ? 0 : (int)ASTRA_STATUS_NOT_FOUND;
}
