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

typedef struct CopyFrame {
    AstraDirectory directory;
    AstraDirectoryEntry *entries;
    char *source;
    char *destination;
    uint32_t batch_count;
    uint32_t batch_at;
    uint8_t opened;
    char paths[];
} CopyFrame;

static CopyFrame *copy_frame_new(const char *source, const char *destination)
{
    size_t source_bytes = strlen(source) + 1u;
    size_t destination_bytes = strlen(destination) + 1u;
    if (source_bytes > ASTRA_VFS_PATH_MAX ||
        destination_bytes > ASTRA_VFS_PATH_MAX)
        return NULL;
    CopyFrame *frame = astra_runtime_allocate(
        sizeof(*frame) + source_bytes + destination_bytes);
    if (frame == NULL) return NULL;
    frame->directory = (AstraDirectory)ASTRA_DIRECTORY_INIT;
    frame->source = frame->paths;
    frame->destination = frame->paths + source_bytes;
    frame->entries = NULL;
    frame->batch_count = 0u;
    frame->batch_at = 0u;
    frame->opened = 0u;
    memcpy(frame->source, source, source_bytes);
    memcpy(frame->destination, destination, destination_bytes);
    return frame;
}

static void copy_frame_free(CopyFrame *frame)
{
    if (frame->opened)
        astra_filesystem_directory_close(&frame->directory);
    astra_runtime_deallocate(frame->entries);
    astra_runtime_deallocate(frame);
}

static uint32_t copy_one(AstraFilesystem *source_filesystem,
                         const char *source,
                         AstraFilesystem *destination_filesystem,
                         const char *destination, int *created)
{
    CopyFrame *frames[COPY_DEPTH_MAX] = {0};
    char *paths = astra_runtime_allocate(2u * ASTRA_VFS_PATH_MAX);
    uint32_t active = 0u;
    uint32_t status = ASTRA_VFS_OK;
    AstraFileInfo info = {.size = sizeof(info)};
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    frames[active] = copy_frame_new(source, destination);
    if (frames[active] == NULL) {
        astra_runtime_deallocate(paths);
        return ASTRA_VFS_ERR_LIMIT;
    }
    ++active;
    while (active != 0u && status == ASTRA_VFS_OK) {
        CopyFrame *frame = frames[active - 1u];
        if (!frame->opened) {
            status = astra_filesystem_lstat(source_filesystem,
                frame->source, &info);
            if (status != ASTRA_VFS_OK) break;
            if (info.kind == ASTRA_VFS_KIND_FILE) {
                status = copy_file(source_filesystem, frame->source,
                    destination_filesystem, frame->destination, info.mode,
                    active == 1u ? created : NULL);
            } else if (info.kind == ASTRA_VFS_KIND_SYMLINK) {
                if (info.byte_size >= ASTRA_VFS_PATH_MAX) {
                    status = ASTRA_VFS_ERR_LIMIT;
                } else {
                    uint32_t length = 0u;
                    status = astra_filesystem_readlink(source_filesystem,
                        frame->source, paths, ASTRA_VFS_PATH_MAX - 1u, &length);
                    if (status == ASTRA_VFS_OK && length >= ASTRA_VFS_PATH_MAX)
                        status = ASTRA_VFS_ERR_LIMIT;
                    if (status == ASTRA_VFS_OK) {
                        paths[length] = '\0';
                        status = astra_filesystem_symlink(paths,
                            destination_filesystem, frame->destination);
                        if (status == ASTRA_VFS_OK && active == 1u &&
                            created != NULL) *created = 1;
                    }
                }
            } else if (info.kind == ASTRA_VFS_KIND_DIRECTORY) {
                frame->entries = astra_runtime_allocate(
                    8u * sizeof(*frame->entries));
                if (frame->entries == NULL) {
                    status = ASTRA_VFS_ERR_LIMIT;
                    break;
                }
                status = astra_filesystem_directory_open(source_filesystem,
                    frame->source, &frame->directory);
                if (status != ASTRA_VFS_OK) break;
                frame->opened = 1u;
                status = astra_filesystem_mkdir_mode(destination_filesystem,
                    frame->destination, (uint16_t)(info.mode & 0777u));
                if (status == ASTRA_VFS_OK && active == 1u && created != NULL)
                    *created = 1;
                continue;
            } else {
                status = ASTRA_VFS_ERR_UNSUPPORTED;
            }
            if (status != ASTRA_VFS_OK) break;
            copy_frame_free(frame);
            frames[--active] = NULL;
            continue;
        }
        if (frame->batch_at == frame->batch_count) {
            frame->batch_at = 0u;
            frame->batch_count = 0u;
            status = astra_filesystem_directory_read(&frame->directory,
                frame->entries, 8u, &frame->batch_count);
            if (status != ASTRA_VFS_OK) break;
            if (frame->batch_count == 0u) {
                copy_frame_free(frame);
                frames[--active] = NULL;
                continue;
            }
        }
        const AstraDirectoryEntry *entry =
            &frame->entries[frame->batch_at++];
        if (strcmp(entry->name, ".") == 0 ||
            strcmp(entry->name, "..") == 0) continue;
        if (active == COPY_DEPTH_MAX) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        paths[0] = '\0';
        paths[ASTRA_VFS_PATH_MAX] = '\0';
        status = copy_path(paths, frame->source, entry->name);
        if (status == ASTRA_VFS_OK)
            status = copy_path(paths + ASTRA_VFS_PATH_MAX,
                frame->destination, entry->name);
        if (status != ASTRA_VFS_OK) break;
        frames[active] = copy_frame_new(paths,
            paths + ASTRA_VFS_PATH_MAX);
        if (frames[active] == NULL) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        ++active;
    }
    while (active != 0u) {
        copy_frame_free(frames[--active]);
    }
    astra_runtime_deallocate(paths);
    return status;
}

static uint32_t remove_partial(AstraFilesystem *filesystem, const char *path)
{
    char *paths[COPY_DEPTH_MAX] = {0};
    char *child = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status = ASTRA_VFS_OK;
    uint32_t active = 0u;
    if (child == NULL) return ASTRA_VFS_ERR_LIMIT;
    size_t length = strlen(path) + 1u;
    if (length > ASTRA_VFS_PATH_MAX ||
        (paths[0] = astra_runtime_allocate(length)) == NULL) {
        astra_runtime_deallocate(child);
        return ASTRA_VFS_ERR_LIMIT;
    }
    memcpy(paths[active++], path, length);
    while (active != 0u && status == ASTRA_VFS_OK) {
        const char *current = paths[active - 1u];
        status = astra_filesystem_lstat(filesystem, current, &info);
        if (status != ASTRA_VFS_OK) break;
        if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
            status = astra_filesystem_unlink(filesystem, current, 0u);
            if (status != ASTRA_VFS_OK) break;
            astra_runtime_deallocate(paths[--active]);
            continue;
        }
        AstraDirectory directory = ASTRA_DIRECTORY_INIT;
        AstraDirectoryEntry entry;
        uint32_t count = 0u;
        status = astra_filesystem_directory_open(filesystem, current,
            &directory);
        if (status != ASTRA_VFS_OK) break;
        do {
            status = astra_filesystem_directory_read(&directory, &entry, 1u,
                &count);
            if (status != ASTRA_VFS_OK) break;
        } while (count != 0u &&
            (strcmp(entry.name, ".") == 0 ||
             strcmp(entry.name, "..") == 0));
        astra_filesystem_directory_close(&directory);
        if (status != ASTRA_VFS_OK) break;
        if (count == 0u) {
            status = astra_filesystem_unlink(filesystem, current,
                ASTRA_VFS_UNLINK_DIRECTORY);
            if (status != ASTRA_VFS_OK) break;
            astra_runtime_deallocate(paths[--active]);
            continue;
        }
        if (active == COPY_DEPTH_MAX) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        child[0] = '\0';
        status = copy_path(child, current, entry.name);
        if (status != ASTRA_VFS_OK) break;
        length = strlen(child) + 1u;
        paths[active] = astra_runtime_allocate(length);
        if (paths[active] == NULL) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        memcpy(paths[active++], child, length);
    }
    while (active != 0u)
        astra_runtime_deallocate(paths[--active]);
    astra_runtime_deallocate(child);
    return status;
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
            staging, &created);
        if (status != ASTRA_VFS_ERR_EXISTS || created) break;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename(destination_filesystem, staging, destination);
    if (status != ASTRA_VFS_OK && created)
        (void)remove_partial(destination_filesystem, staging);
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
    return remove_partial(source_filesystem, source);
}
