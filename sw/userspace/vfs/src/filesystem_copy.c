#include <astra/filesystem_library.h>
#include <astra/runtime.h>
#include <astra/string.h>

#include <string.h>

#define COPY_DEPTH_MAX 32u
#define COPY_BUFFER_SIZE 16384u

static uint32_t copy_path(char *out, const char *parent, const char *name)
{
    size_t length = strlen(parent);
    if (!astra_string_concat(out, ASTRA_VFS_PATH_MAX, parent) ||
        (length != 0u && parent[length - 1u] != '/' &&
         !astra_string_concat(out, ASTRA_VFS_PATH_MAX, "/")) ||
        !astra_string_concat(out, ASTRA_VFS_PATH_MAX, name))
        return ASTRA_VFS_ERR_LIMIT;
    return ASTRA_VFS_OK;
}

static uint32_t copy_file(AstraFilesystem *filesystem, const char *source,
                          const char *destination, uint16_t mode)
{
    AstraFile input = ASTRA_FILE_INIT;
    AstraFile output = ASTRA_FILE_INIT;
    uint8_t *buffer = astra_runtime_allocate(COPY_BUFFER_SIZE);
    uint32_t status;
    uint32_t close_status;

    if (buffer == NULL) return ASTRA_VFS_ERR_LIMIT;
    status = astra_filesystem_open(filesystem, source, ASTRA_VFS_OPEN_READ,
                                   &input);
    if (status != ASTRA_VFS_OK) goto done;
    status = astra_filesystem_open_mode(filesystem, destination,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE, (uint16_t)(mode & 0777u), &output);
    if (status != ASTRA_VFS_OK) goto done;
    for (;;) {
        uint32_t count = 0u;
        status = astra_filesystem_read(&input, buffer, COPY_BUFFER_SIZE, &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t offset = 0u; offset < count;) {
            uint32_t written = 0u;
            status = astra_filesystem_write(&output, UINT64_MAX,
                buffer + offset, count - offset, &written);
            if (status != ASTRA_VFS_OK || written == 0u) {
                if (status == ASTRA_VFS_OK) status = ASTRA_VFS_ERR_IO;
                break;
            }
            offset += written;
        }
        if (status != ASTRA_VFS_OK) break;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_sync(&output);
done:
    if (output._private_file != ASTRA_VFS_FILE_INVALID) {
        close_status = astra_filesystem_close(&output);
        if (status == ASTRA_VFS_OK) status = close_status;
    }
    if (input._private_file != ASTRA_VFS_FILE_INVALID) {
        close_status = astra_filesystem_close(&input);
        if (status == ASTRA_VFS_OK) status = close_status;
    }
    astra_runtime_deallocate(buffer);
    return status;
}

static uint32_t copy_one(AstraFilesystem *filesystem, const char *source,
                         const char *destination, uint32_t depth)
{
    AstraFileInfo info = {.size = sizeof(info)};
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    char *paths;
    uint32_t status;

    if (depth >= COPY_DEPTH_MAX) return ASTRA_VFS_ERR_LIMIT;
    status = astra_filesystem_lstat(filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind == ASTRA_VFS_KIND_FILE)
        return copy_file(filesystem, source, destination, info.mode);
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_UNSUPPORTED;

    status = astra_filesystem_directory_open(filesystem, source, &directory);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir_mode(filesystem, destination,
                                         (uint16_t)(info.mode & 0777u));
    if (status != ASTRA_VFS_OK) goto done;
    paths = astra_runtime_allocate(2u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    for (;;) {
        AstraDirectoryEntry entries[8];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 8u,
                                                  &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            if (strcmp(entries[at].name, ".") == 0 ||
                strcmp(entries[at].name, "..") == 0) continue;
            paths[0] = '\0';
            paths[ASTRA_VFS_PATH_MAX] = '\0';
            status = copy_path(paths, source, entries[at].name);
            if (status == ASTRA_VFS_OK)
                status = copy_path(paths + ASTRA_VFS_PATH_MAX,
                                   destination, entries[at].name);
            if (status == ASTRA_VFS_OK)
                status = copy_one(filesystem, paths,
                                  paths + ASTRA_VFS_PATH_MAX, depth + 1u);
            if (status != ASTRA_VFS_OK) break;
        }
        if (status != ASTRA_VFS_OK) break;
    }
    astra_runtime_deallocate(paths);
done:
    astra_filesystem_directory_close(&directory);
    return status;
}

uint32_t astra_filesystem_copy_tree(AstraFilesystem *filesystem,
                                   const char *source,
                                   const char *destination)
{
    AstraFileInfo info = {.size = sizeof(info)};
    char *staging;
    uint32_t status;
    size_t length;

    if (filesystem == NULL || source == NULL || destination == NULL ||
        source[0] == '\0' || destination[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    length = strlen(source);
    if (strcmp(source, destination) == 0 ||
        (strncmp(source, destination, length) == 0 &&
         destination[length] == '/'))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_lstat(filesystem, destination, &info);
    if (status == ASTRA_VFS_OK) return ASTRA_VFS_ERR_EXISTS;
    if (status != ASTRA_VFS_ERR_NOT_FOUND) return status;
    staging = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (staging == NULL) return ASTRA_VFS_ERR_LIMIT;
    staging[0] = '\0';
    if (!astra_string_concat(staging, ASTRA_VFS_PATH_MAX, destination) ||
        !astra_string_concat(staging, ASTRA_VFS_PATH_MAX, ".astra-copying")) {
        astra_runtime_deallocate(staging);
        return ASTRA_VFS_ERR_LIMIT;
    }
    status = astra_filesystem_lstat(filesystem, staging, &info);
    if (status == ASTRA_VFS_ERR_NOT_FOUND)
        status = copy_one(filesystem, source, staging, 0u);
    else if (status == ASTRA_VFS_OK)
        status = ASTRA_VFS_ERR_EXISTS;
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename(filesystem, staging, destination);
    astra_runtime_deallocate(staging);
    return status;
}
