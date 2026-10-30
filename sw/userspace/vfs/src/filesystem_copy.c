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

static uint32_t copy_file(AstraFilesystem *source_filesystem,
                          const char *source,
                          AstraFilesystem *destination_filesystem,
                          const char *destination, uint16_t mode, int *created)
{
    AstraFile input = ASTRA_FILE_INIT;
    AstraFile output = ASTRA_FILE_INIT;
    uint8_t *buffer = astra_runtime_allocate(COPY_BUFFER_SIZE);
    uint32_t status;
    uint32_t close_status;

    if (buffer == NULL) return ASTRA_VFS_ERR_LIMIT;
    status = astra_filesystem_open(source_filesystem, source, ASTRA_VFS_OPEN_READ,
                                   &input);
    if (status != ASTRA_VFS_OK) goto done;
    status = astra_filesystem_open_mode(destination_filesystem, destination,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE, (uint16_t)(mode & 0777u), &output);
    if (status != ASTRA_VFS_OK) goto done;
    if (created != NULL) *created = 1;
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

static uint32_t copy_one(AstraFilesystem *source_filesystem,
                         const char *source,
                         AstraFilesystem *destination_filesystem,
                         const char *destination, uint32_t depth, int *created)
{
    AstraFileInfo info = {.size = sizeof(info)};
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    char *paths;
    uint32_t status;

    if (depth >= COPY_DEPTH_MAX) return ASTRA_VFS_ERR_LIMIT;
    status = astra_filesystem_lstat(source_filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind == ASTRA_VFS_KIND_FILE)
        return copy_file(source_filesystem, source, destination_filesystem,
            destination, info.mode, created);
    if (info.kind == ASTRA_VFS_KIND_SYMLINK) {
        if (info.byte_size >= ASTRA_VFS_PATH_MAX)
            return ASTRA_VFS_ERR_LIMIT;
        char *target = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
        if (target == NULL) return ASTRA_VFS_ERR_LIMIT;
        uint32_t length = 0u;
        status = astra_filesystem_readlink(source_filesystem, source,
            target, ASTRA_VFS_PATH_MAX - 1u, &length);
        if (status == ASTRA_VFS_OK) {
            if (length >= ASTRA_VFS_PATH_MAX)
                status = ASTRA_VFS_ERR_LIMIT;
            else {
                target[length] = '\0';
                status = astra_filesystem_symlink(target,
                    destination_filesystem, destination);
                if (status == ASTRA_VFS_OK && created != NULL)
                    *created = 1;
            }
        }
        astra_runtime_deallocate(target);
        return status;
    }
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_UNSUPPORTED;

    status = astra_filesystem_directory_open(source_filesystem, source, &directory);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir_mode(destination_filesystem, destination,
                                         (uint16_t)(info.mode & 0777u));
    if (status != ASTRA_VFS_OK) goto done;
    if (created != NULL) *created = 1;
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
                status = copy_one(source_filesystem, paths,
                                  destination_filesystem,
                                  paths + ASTRA_VFS_PATH_MAX, depth + 1u, NULL);
            if (status != ASTRA_VFS_OK) break;
        }
        if (status != ASTRA_VFS_OK) break;
    }
    astra_runtime_deallocate(paths);
done:
    astra_filesystem_directory_close(&directory);
    return status;
}

static uint32_t remove_partial(AstraFilesystem *filesystem, const char *path,
                               uint32_t depth)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status = astra_filesystem_lstat(filesystem, path, &info);
    if (status != ASTRA_VFS_OK || info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return status == ASTRA_VFS_OK ?
            astra_filesystem_unlink(filesystem, path, 0u) : status;
    if (depth >= COPY_DEPTH_MAX) return ASTRA_VFS_ERR_LIMIT;
    char *child = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (child == NULL) return ASTRA_VFS_ERR_LIMIT;
    for (;;) {
        AstraDirectory directory = ASTRA_DIRECTORY_INIT;
        AstraDirectoryEntry entries[8];
        uint32_t count = 0u, child_at = 0u;
        int found = 0;
        status = astra_filesystem_directory_open(filesystem, path, &directory);
        if (status != ASTRA_VFS_OK) break;
        do {
            status = astra_filesystem_directory_read(&directory, entries, 8u,
                                                      &count);
            if (status != ASTRA_VFS_OK) break;
            for (child_at = 0u; child_at < count; ++child_at)
                if (strcmp(entries[child_at].name, ".") != 0 &&
                    strcmp(entries[child_at].name, "..") != 0) {
                    found = 1;
                    break;
                }
        } while (count != 0u && !found);
        astra_filesystem_directory_close(&directory);
        if (status != ASTRA_VFS_OK) break;
        if (!found) break;
        child[0] = '\0';
        status = copy_path(child, path, entries[child_at].name);
        if (status == ASTRA_VFS_OK)
            status = remove_partial(filesystem, child, depth + 1u);
        if (status != ASTRA_VFS_OK) break;
    }
    astra_runtime_deallocate(child);
    return status == ASTRA_VFS_OK ?
        astra_filesystem_unlink(filesystem, path, ASTRA_VFS_UNLINK_DIRECTORY) :
        status;
}

static uint32_t transfer_preflight(AstraFilesystem *source_filesystem,
    const char *source, AstraFilesystem *destination_filesystem,
    const char *destination)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;
    size_t length;

    if (source_filesystem == NULL || destination_filesystem == NULL ||
        source == NULL || destination == NULL ||
        source[0] == '\0' || destination[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    length = strlen(source);
    if (strcmp(source, destination) == 0 ||
        (strncmp(source, destination, length) == 0 &&
         destination[length] == '/'))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_lstat(source_filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_FILE &&
        info.kind != ASTRA_VFS_KIND_DIRECTORY &&
        info.kind != ASTRA_VFS_KIND_SYMLINK)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    status = astra_filesystem_lstat(destination_filesystem, destination, &info);
    if (status == ASTRA_VFS_OK) return ASTRA_VFS_ERR_EXISTS;
    return status == ASTRA_VFS_ERR_NOT_FOUND ? ASTRA_VFS_OK : status;
}

uint32_t astra_filesystem_copy_tree_between(AstraFilesystem *source_filesystem,
    const char *source, AstraFilesystem *destination_filesystem,
    const char *destination)
{
    char *staging;
    uint32_t status = transfer_preflight(source_filesystem, source,
        destination_filesystem, destination);
    int created = 0;
    uint64_t nonce;

    if (status != ASTRA_VFS_OK) return status;
    staging = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (staging == NULL) return ASTRA_VFS_ERR_LIMIT;
    nonce = astra_clock_monotonic();
    for (uint32_t attempt = 0u; attempt < 16u; ++attempt) {
        AstraString path;
        astra_string_init(&path, staging, ASTRA_VFS_PATH_MAX);
        if (!astra_string_append(&path, destination) ||
            !astra_string_append(&path, ".astra-copying-") ||
            !astra_string_append_hex(&path, nonce + attempt, 16u)) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        created = 0;
        status = copy_one(source_filesystem, source, destination_filesystem,
            staging, 0u, &created);
        if (status != ASTRA_VFS_ERR_EXISTS || created) break;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename(destination_filesystem, staging, destination);
    if (status != ASTRA_VFS_OK && created)
        (void)remove_partial(destination_filesystem, staging, 0u);
    astra_runtime_deallocate(staging);
    return status;
}

uint32_t astra_filesystem_copy_tree(AstraFilesystem *filesystem,
    const char *source, const char *destination)
{
    return astra_filesystem_copy_tree_between(filesystem, source,
        filesystem, destination);
}

uint32_t astra_filesystem_move_tree_between(AstraFilesystem *source_filesystem,
    const char *source, AstraFilesystem *destination_filesystem,
    const char *destination, uint32_t *published)
{
    uint32_t status;

    if (published != NULL) *published = 0u;
    if (published == NULL) return ASTRA_VFS_ERR_INVALID;
    status = transfer_preflight(source_filesystem, source,
        destination_filesystem, destination);
    if (status != ASTRA_VFS_OK) return status;

    /* Source view can name destination on same volume. It may intentionally
     * expose only the exact removable source mount, in which case a different
     * destination assign returns NOT_FOUND and copy handles that case. */
    status = astra_filesystem_rename(source_filesystem, source, destination);
    if (status == ASTRA_VFS_OK) {
        *published = 1u;
        return ASTRA_VFS_OK;
    }
    if (status != ASTRA_VFS_ERR_CROSS_DEVICE &&
        status != ASTRA_VFS_ERR_NOT_FOUND)
        return status;
    status = astra_filesystem_copy_tree_between(source_filesystem, source,
        destination_filesystem, destination);
    if (status != ASTRA_VFS_OK) return status;
    *published = 1u;
    return remove_partial(source_filesystem, source, 0u);
}
