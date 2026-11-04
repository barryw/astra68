/* Move native files and complete application bundles through Filesystem Kit. */
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>
#include "../file_target.h"

#include <stdio.h>

ASTRA_PROGRAM("mv", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    AstraProcessFilesystem process = ASTRA_PROCESS_FILESYSTEM_INIT;
    char *paths;
    char *source;
    char *destination;
    const char *target = NULL;
    uint32_t published = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    if (argc != 3) {
        (void)fputs("mv: usage: mv source destination\n", stderr);
        return ASTRA_STATUS_INVALID;
    }
    paths = astra_runtime_allocate(3u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_STATUS_LIMIT;
    source = paths;
    destination = paths + ASTRA_VFS_PATH_MAX;
    status = astra_process_filesystem_open(&process, startup);
    if (status == ASTRA_VFS_OK)
        status = astra_process_path(argv[1], source, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_process_path(argv[2], destination, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_command_file_target(&process.filesystem, source,
            destination, paths + 2u * ASTRA_VFS_PATH_MAX, &target);
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_move_tree_between(&process.filesystem,
            source, &process.filesystem, target, &published);
    if (status != ASTRA_VFS_OK) {
        const char *message = astra_vfs_status_text(status);
        (void)fputs(published != 0u ?
            "mv: destination created; source removal failed: " : "mv: ", stderr);
        (void)fputs(message == NULL ? "move failed" : message, stderr);
        (void)fputs("\n", stderr);
    }
    astra_process_filesystem_close(&process);
    astra_runtime_deallocate(paths);
    return (int)status;
}
