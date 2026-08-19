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

#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTRA_PROGRAM("ps", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    PS_BATCH = 8u,
    PS_STATUS_MAX = 512u,
};

/* The states AstraProcessInfo reports, in the order the kernel numbers them. */
static const char *
state_name(unsigned long value)
{
    static const char *const names[] = {
        "new", "ready", "run", "wait", "stop", "zombie"
    };

    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}

/*
 * `status` is `name value` a line at a time. Reading a field rather than
 * parsing a record on purpose: a tree that grows a field must not break a
 * reader that never asked for it.
 */
static int
field(const char *text, const char *name, char *out, size_t capacity)
{
    size_t length = strlen(name);
    const char *at = text;

    while (*at != '\0') {
        const char *line = at;
        const char *end = line;

        while (*end != '\0' && *end != '\n')
            ++end;
        if ((size_t)(end - line) > length && line[length] == ' ' &&
            strncmp(line, name, length) == 0) {
            size_t moved = (size_t)(end - line) - length - 1u;

            if (moved >= capacity)
                moved = capacity - 1u;
            memcpy(out, line + length + 1u, moved);
            out[moved] = '\0';
            return 1;
        }
        at = (*end == '\0') ? end : end + 1;
    }
    out[0] = '\0';
    return 0;
}

static unsigned long
number(const char *text, const char *name)
{
    char value[24];

    if (!field(text, name, value, sizeof(value)))
        return 0ul;
    return strtoul(value, NULL, 10);
}

static uint32_t
read_status(AstraFilesystem *filesystem,
            const AstraFilesystemLibraryV1 *library, const char *id,
            char *out, uint32_t capacity)
{
    AstraFile file = ASTRA_FILE_INIT;
    char path[8u + ASTRA_VFS_NAME_MAX];
    uint32_t total = 0u;
    uint32_t status;

    (void)snprintf(path, sizeof(path), "PROC:%.*s/status",
                   (int)(sizeof(path) - 14u), id);
    status = library->open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK)
        return status;
    for (;;) {
        uint32_t moved = 0u;

        status = library->read(&file, out + total, capacity - 1u - total,
                               &moved);
        if (status != ASTRA_VFS_OK || moved == 0u)
            break;
        total += moved;
        if (total >= capacity - 1u)
            break;
    }
    (void)library->close(&file);
    out[total] = '\0';
    return total != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

int
astra_main(const AstraStartupInfo *startup)
{
    AstraProcessFilesystem process_filesystem =
        ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[PS_BATCH];
    static char status_text[PS_STATUS_MAX];
    uint32_t listed = 0u;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "ps: no filesystem: status %u\n", status);
        return (int)status;
    }
    status = process_filesystem.library->directory_open(
        &process_filesystem.filesystem, "PROC:", &directory);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr,
                      "ps: PROC: not granted to this program (status %u)\n",
                      status);
        astra_process_filesystem_close(&process_filesystem);
        return (int)status;
    }
    printf("%6s %5s %-6s %3s %5s %8s %9s %s\n",
           "PID", "GEN", "STATE", "THR", "FRAME", "RUNS", "SYSCALLS",
           "COMMAND");
    for (;;) {
        uint32_t count = 0u;

        status = process_filesystem.library->directory_read(
            &directory, entries, PS_BATCH, &count);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        for (uint32_t index = 0u; index < count; ++index) {
            char name[40];

            if (read_status(&process_filesystem.filesystem,
                            process_filesystem.library, entries[index].name,
                            status_text, PS_STATUS_MAX) != ASTRA_VFS_OK)
                continue;
            (void)field(status_text, "name", name, sizeof(name));
            printf("%6lu %5lu %-6s %3lu %5lu %8lu %9lu %s\n",
                   number(status_text, "id"),
                   number(status_text, "generation"),
                   state_name(number(status_text, "state")),
                   number(status_text, "live"),
                   number(status_text, "frames"),
                   number(status_text, "runs"),
                   number(status_text, "syscalls"),
                   name);
            ++listed;
        }
    }
    process_filesystem.library->directory_close(&directory);
    astra_process_filesystem_close(&process_filesystem);
    (void)fflush(stdout);
    return listed != 0u ? 0 : (int)ASTRA_STATUS_NOT_FOUND;
}
