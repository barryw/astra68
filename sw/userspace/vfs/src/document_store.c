#include <astra/document_store.h>
#include <astra/runtime.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <string.h>

uint32_t astra_document_path_join(const char *directory, const char *name,
    char *out, uint32_t capacity)
{
    size_t base, leaf;
    uint32_t slash;
    if (directory == NULL || name == NULL || out == NULL || capacity == 0u ||
        directory[0] != '/' || name[0] == '\0' ||
        astra_path_requires_walk(directory))
        return ASTRA_VFS_ERR_INVALID;
    base = strlen(directory);
    leaf = strlen(name);
    for (size_t at = 0u; at < leaf; ++at)
        if (name[at] == '/') return ASTRA_VFS_ERR_INVALID;
    slash = directory[base - 1u] != '/';
    if (base + slash + leaf >= capacity) return ASTRA_VFS_ERR_LIMIT;
    memcpy(out, directory, base);
    if (slash) out[base++] = '/';
    memcpy(out + base, name, leaf + 1u);
    return ASTRA_VFS_OK;
}

uint32_t astra_document_store_prepare(AstraFilesystem *filesystem,
    const char *directory)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;
    if (filesystem == NULL || directory == NULL || directory[0] != '/' ||
        strlen(directory) >= ASTRA_VFS_PATH_MAX ||
        astra_path_requires_walk(directory))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_lstat(filesystem, directory, &info);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        status = astra_filesystem_mkdir_mode(filesystem, directory, 0700u);
        if (status == ASTRA_VFS_ERR_EXISTS)
            status = astra_filesystem_lstat(filesystem, directory, &info);
        else if (status == ASTRA_VFS_OK)
            info.kind = ASTRA_VFS_KIND_DIRECTORY;
    }
    if (status != ASTRA_VFS_OK) return status;
    return info.kind == ASTRA_VFS_KIND_DIRECTORY ? ASTRA_VFS_OK :
        ASTRA_VFS_ERR_NOT_DIR;
}

uint32_t astra_document_stage(AstraFilesystem *filesystem, const char *path,
    const uint8_t *bytes, uint32_t length)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t status;
    if (filesystem == NULL || path == NULL || path[0] != '/' ||
        bytes == NULL || length == 0u ||
        strlen(path) >= ASTRA_VFS_PATH_MAX || astra_path_requires_walk(path))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_open_mode(filesystem, path,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE, 0600u, &file);
    if (status != ASTRA_VFS_OK) return status;
    for (uint32_t at = 0u; at < length;) {
        uint32_t moved = 0u;
        status = astra_filesystem_write(&file, UINT64_MAX,
            bytes + at, length - at, &moved);
        if (status != ASTRA_VFS_OK) break;
        if (moved == 0u) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        at += moved;
    }
    if (status == ASTRA_VFS_OK) status = astra_filesystem_sync(&file);
    uint32_t close_status = astra_filesystem_close(&file);
    if (status == ASTRA_VFS_OK) status = close_status;
    if (status != ASTRA_VFS_OK)
        (void)astra_filesystem_unlink(filesystem, path, 0u);
    return status;
}

uint32_t astra_document_read(AstraFilesystem *filesystem, const char *path,
    uint32_t minimum, uint32_t maximum, uint8_t **bytes, uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint8_t *buffer = NULL;
    uint32_t status, close_status;
    if (bytes != NULL) *bytes = NULL;
    if (length != NULL) *length = 0u;
    if (filesystem == NULL || path == NULL || path[0] != '/' ||
        minimum == 0u || maximum < minimum || bytes == NULL || length == NULL ||
        strlen(path) >= ASTRA_VFS_PATH_MAX || astra_path_requires_walk(path))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_file_info(&file, &info);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_FILE || info.byte_size < minimum ||
        info.byte_size > maximum) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    buffer = astra_runtime_allocate((size_t)info.byte_size);
    if (buffer == NULL) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    for (uint32_t at = 0u; at < info.byte_size;) {
        uint32_t moved = 0u;
        status = astra_filesystem_read(&file, buffer + at,
            (uint32_t)info.byte_size - at, &moved);
        if (status != ASTRA_VFS_OK) goto done;
        if (moved == 0u) {
            status = ASTRA_VFS_ERR_INVALID;
            goto done;
        }
        at += moved;
    }
done:
    close_status = astra_filesystem_close(&file);
    if (status == ASTRA_VFS_OK) status = close_status;
    if (status == ASTRA_VFS_OK) {
        *bytes = buffer;
        *length = (uint32_t)info.byte_size;
    } else astra_runtime_deallocate(buffer);
    return status;
}
