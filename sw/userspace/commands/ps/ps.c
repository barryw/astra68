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
static const char header[] =
    "       PID   GEN STATE  PRI  NI THR MEM(K)  CPU%        TIME    RUNS "
    "  CALLS HND COMMAND\n";
static uint32_t output_used;

static uint32_t
read_snapshot(AstraProcSnapshot **records, uint32_t *moved)
{
    const AstraAssign *assign = NULL;
    AstraVfsClient *client;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint64_t offset = 0u;
    uint16_t kind = 0u;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    *moved = 0u;
    *records = NULL;
    status = astra_assign_resolve(astra_process_vfs_assigns(),
                                  "PROC:snapshot", ASTRA_RIGHT_READ, 0u,
                                  path, sizeof(path), &assign);
    if (status != ASTRA_VFS_OK)
        return status;
    client = astra_process_vfs_client_for(assign);
    if (client == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    status = astra_vfs_open(client, path, ASTRA_VFS_OPEN_READ, &file, &size,
                            &kind);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_ps_snapshot_allocate(size, astra_runtime_reallocate,
                                        records);
    if (status != ASTRA_VFS_OK) {
        (void)astra_vfs_close(client, file);
        return status;
    }
    while (offset < size) {
        uint32_t chunk = (uint32_t)(size - offset);
        uint32_t got = 0u;

        if (chunk > ASTRA_VFS_IO_MAX)
            chunk = ASTRA_VFS_IO_MAX;
        status = astra_vfs_read(client, file, offset,
                                (uint8_t *)*records + (uint32_t)offset,
                                chunk, &got);
        if (status != ASTRA_VFS_OK || got == 0u) {
            status = status != ASTRA_VFS_OK ? status : ASTRA_VFS_ERR_PROTOCOL;
            break;
        }
        offset += got;
    }
    if (astra_vfs_close(client, file) != ASTRA_VFS_OK &&
        status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_PROTOCOL;
    *moved = (uint32_t)offset;
    return status == ASTRA_VFS_OK && offset == size
               ? ASTRA_VFS_OK : status;
}

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
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: filesystem unavailable\n");
        return (int)status;
    }
    status = read_snapshot(&records, &moved);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: PROC: not granted to this program\n");
        (void)astra_runtime_reallocate(records, 0u);
        astra_process_vfs_close();
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
    astra_process_vfs_close();
    (void)astra_runtime_reallocate(records, 0u);
    if (status == ASTRA_VFS_OK && !flush_output())
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_ERR_IO)
        return (int)status;
    return listed != 0u ? 0 : (int)ASTRA_STATUS_NOT_FOUND;
}
