#include "file_target.h"

#include <astra/string.h>

#include <string.h>

uint32_t astra_command_file_target(AstraFilesystem *filesystem,
    const char *source, const char *destination, char *joined,
    const char **target)
{
    AstraFileInfo info = {.size = sizeof(info)};
    uint32_t status = astra_filesystem_stat(filesystem, destination, &info);

    *target = destination;
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        return ASTRA_VFS_OK;
    if (status != ASTRA_VFS_OK)
        return status;
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_EXISTS;

    const char *leaf = strrchr(source, '/');
    leaf = leaf == NULL ? source : leaf + 1u;
    joined[0] = '\0';
    if (leaf[0] == '\0' ||
        !astra_string_concat(joined, ASTRA_VFS_PATH_MAX, destination) ||
        !astra_string_concat(joined, ASTRA_VFS_PATH_MAX, "/") ||
        !astra_string_concat(joined, ASTRA_VFS_PATH_MAX, leaf))
        return ASTRA_VFS_ERR_LIMIT;
    *target = joined;
    return ASTRA_VFS_OK;
}
