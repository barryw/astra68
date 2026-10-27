/* Copy native files and complete application bundles through Filesystem Kit. */
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/string.h>
#include <astra/vfs_process.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("cp", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    AstraProcessFilesystem process = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFileInfo info = {.size = sizeof(info)};
    char *paths;
    char *source;
    char *destination;
    uint32_t status;
    int recursive = 0;
    int first = 1;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    if (argc > 1 && strcmp(argv[1], "-R") == 0) {
        recursive = 1;
        first = 2;
    }
    if (argc != first + 2) {
        (void)fputs("cp: usage: cp [-R] source destination\n", stderr);
        return ASTRA_STATUS_INVALID;
    }
    paths = astra_runtime_allocate(3u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_STATUS_LIMIT;
    source = paths;
    destination = paths + ASTRA_VFS_PATH_MAX;
    status = astra_process_filesystem_open(&process, startup);
    if (status == ASTRA_VFS_OK)
        status = astra_process_path(argv[first], source, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_process_path(argv[first + 1], destination,
                                    ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_lstat(&process.filesystem, source, &info);
    if (status == ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY &&
        !recursive) status = ASTRA_VFS_ERR_IS_DIR;
    if (status == ASTRA_VFS_OK) {
        info.size = sizeof(info);
        status = astra_filesystem_stat(&process.filesystem, destination, &info);
        if (status == ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_DIRECTORY) {
            const char *leaf = strrchr(source, '/');
            leaf = leaf == NULL ? source : leaf + 1u;
            paths[2u * ASTRA_VFS_PATH_MAX] = '\0';
            if (leaf[0] == '\0' ||
                !astra_string_concat(paths + 2u * ASTRA_VFS_PATH_MAX,
                                     ASTRA_VFS_PATH_MAX, destination) ||
                !astra_string_concat(paths + 2u * ASTRA_VFS_PATH_MAX,
                                     ASTRA_VFS_PATH_MAX, "/") ||
                !astra_string_concat(paths + 2u * ASTRA_VFS_PATH_MAX,
                                     ASTRA_VFS_PATH_MAX, leaf))
                status = ASTRA_VFS_ERR_LIMIT;
            else
                destination = paths + 2u * ASTRA_VFS_PATH_MAX;
        } else if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            status = ASTRA_VFS_OK;
        } else if (status == ASTRA_VFS_OK) {
            status = ASTRA_VFS_ERR_EXISTS;
        }
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_copy_tree(&process.filesystem, source,
                                            destination);
    if (status != ASTRA_VFS_OK) {
        const char *message = astra_vfs_status_text(status);
        (void)fputs("cp: ", stderr);
        (void)fputs(message == NULL ? "copy failed" : message, stderr);
        (void)fputs("\n", stderr);
    }
    astra_process_filesystem_close(&process);
    astra_runtime_deallocate(paths);
    return (int)status;
}
