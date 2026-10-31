#include <astra/trash.h>
#include <astra/document_store.h>
#include <astra/filesystem_tree_internal.h>
#include <astra/endian.h>
#include <astra/runtime.h>
#include <astra/string.h>
#include <astra/utf8.h>
#include <astra/vfs_path.h>

#include <string.h>

#define TRASH_RECORD_HEADER 8u
#define TRASH_RECORD_MAX (TRASH_RECORD_HEADER + ASTRA_VFS_PATH_MAX - 1u)

static int valid_root(const char *root)
{
    return root != NULL && root[0] == '/' && root[1] != '\0' &&
        root[1] != '/' && strlen(root) < ASTRA_VFS_PATH_MAX &&
        astra_path_leaf(root) == root + 1u &&
        !astra_path_requires_walk(root);
}

static uint32_t trash_paths(const char *root, char *trash, char *origins)
{
    uint32_t status = astra_document_path_join(root, ".Trash", trash,
        ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_document_path_join(trash, ".origins", origins,
            ASTRA_VFS_PATH_MAX);
    return status;
}

static uint32_t existing_store(AstraFilesystem *filesystem,
    const char *trash, const char *origins, int *has_origins)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status = astra_filesystem_lstat(filesystem, trash, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) return ASTRA_VFS_ERR_NOT_DIR;
    status = astra_filesystem_lstat(filesystem, origins, &info);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        *has_origins = 0;
        return ASTRA_VFS_OK;
    }
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) return ASTRA_VFS_ERR_NOT_DIR;
    *has_origins = 1;
    return ASTRA_VFS_OK;
}

static int valid_source(const char *root, const char *trash,
    const char *source)
{
    if (source == NULL || source[0] != '/' ||
        strlen(source) >= ASTRA_VFS_PATH_MAX ||
        !astra_path_beneath(source, root) ||
        source[strlen(root)] != '/' ||
        source[strlen(root) + 1u] == '\0' ||
        astra_path_beneath(source, trash) ||
        astra_path_requires_walk(source))
        return 0;
    uint32_t length = (uint32_t)strlen(source);
    const char *leaf = astra_path_leaf(source);
    uint32_t leaf_length = (uint32_t)strlen(leaf);
    return leaf_length != 0u && leaf_length < ASTRA_VFS_NAME_MAX &&
        astra_utf8_validate(source, length, 0u);
}

uint32_t astra_trash_move(AstraFilesystem *filesystem,
    const char *volume_root, const char *source,
    char *trashed, uint32_t capacity)
{
    char *paths, *trash, *origins, *metadata, *payload;
    uint8_t *record = NULL;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status, relative_length;
    const char *relative, *leaf;
    char prefix[ASTRA_VFS_NAME_MAX];
    char name[ASTRA_VFS_NAME_MAX];

    if (trashed != NULL && capacity != 0u) trashed[0] = '\0';
    if (filesystem == NULL || !valid_root(volume_root) || source == NULL ||
        trashed == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    paths = astra_runtime_allocate(4u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    trash = paths;
    origins = trash + ASTRA_VFS_PATH_MAX;
    metadata = origins + ASTRA_VFS_PATH_MAX;
    payload = metadata + ASTRA_VFS_PATH_MAX;
    status = trash_paths(volume_root, trash, origins);
    if (status != ASTRA_VFS_OK) goto done;
    if (!valid_source(volume_root, trash, source)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    status = astra_filesystem_lstat(filesystem, source, &info);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_FILE &&
        info.kind != ASTRA_VFS_KIND_DIRECTORY &&
        info.kind != ASTRA_VFS_KIND_SYMLINK) {
        status = ASTRA_VFS_ERR_UNSUPPORTED;
        goto done;
    }
    status = astra_document_store_prepare(filesystem, trash);
    if (status != ASTRA_VFS_OK) goto done;
    status = astra_document_store_prepare(filesystem, origins);
    if (status != ASTRA_VFS_OK) goto done;

    relative = source + strlen(volume_root) + 1u;
    relative_length = (uint32_t)strlen(relative);
    record = astra_runtime_allocate(TRASH_RECORD_HEADER + relative_length);
    if (record == NULL) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    memcpy(record, "ATR1", 4u);
    astra_store_be32(record + 4u, relative_length);
    memcpy(record + TRASH_RECORD_HEADER, relative, relative_length);

    leaf = astra_path_leaf(source);
    AstraString text;
    astra_string_init(&text, prefix, sizeof(prefix));
    uint32_t bytes = astra_string_utf8_prefix(leaf,
        (uint32_t)strlen(leaf), 180u);
    if (!astra_string_append_bytes(&text, leaf, bytes) ||
        !astra_string_append_char(&text, '-')) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    status = ASTRA_VFS_ERR_LIMIT;
    for (uint32_t number = 1u; number != 0u; ++number) {
        AstraString candidate;
        astra_string_init(&candidate, name, sizeof(name));
        if (!astra_string_append(&candidate, prefix) ||
            !astra_string_append_u64(&candidate, number)) break;
        if (astra_document_path_join(trash, name, payload,
                ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK ||
            astra_document_path_join(origins, name, metadata,
                ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK ||
            strlen(payload) >= capacity) break;
        status = astra_document_stage(filesystem, metadata, record,
            TRASH_RECORD_HEADER + relative_length);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status != ASTRA_VFS_OK) break;
        status = astra_filesystem_rename_noreplace(filesystem, source,
            payload);
        if (status == ASTRA_VFS_OK) {
            memcpy(trashed, payload, strlen(payload) + 1u);
            break;
        }
        uint32_t cleanup = astra_filesystem_unlink(filesystem, metadata, 0u);
        if (cleanup != ASTRA_VFS_OK) {
            status = cleanup;
            break;
        }
        if (status != ASTRA_VFS_ERR_EXISTS) break;
    }
done:
    astra_runtime_deallocate(record);
    astra_runtime_deallocate(paths);
    return status;
}

uint32_t astra_trash_origin(AstraFilesystem *filesystem,
    const char *volume_root, const char *trashed,
    char *origin, uint32_t capacity)
{
    char *paths, *trash, *origins, *metadata;
    const char *name;
    uint8_t *record = NULL;
    uint32_t status, length = 0u, relative_length;

    if (origin != NULL && capacity != 0u) origin[0] = '\0';
    if (filesystem == NULL || !valid_root(volume_root) ||
        trashed == NULL || origin == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    paths = astra_runtime_allocate(3u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    trash = paths;
    origins = trash + ASTRA_VFS_PATH_MAX;
    metadata = origins + ASTRA_VFS_PATH_MAX;
    status = trash_paths(volume_root, trash, origins);
    if (status != ASTRA_VFS_OK) goto done;
    int has_origins = 0;
    status = existing_store(filesystem, trash, origins, &has_origins);
    if (status != ASTRA_VFS_OK) goto done;
    if (!has_origins) {
        status = ASTRA_VFS_ERR_NOT_FOUND;
        goto done;
    }
    size_t trash_length = strlen(trash);
    if (strlen(trashed) >= ASTRA_VFS_PATH_MAX ||
        strncmp(trashed, trash, trash_length) != 0 ||
        trashed[trash_length] != '/' ||
        trashed[trash_length + 1u] == '\0' ||
        astra_path_requires_walk(trashed)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    name = trashed + trash_length + 1u;
    size_t name_length = strlen(name);
    if (name_length >= ASTRA_VFS_NAME_MAX ||
        memchr(name, '/', name_length) != NULL ||
        strcmp(name, ".origins") == 0 ||
        !astra_utf8_validate(name, (uint32_t)name_length, 0u)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    status = astra_document_path_join(origins, name, metadata,
        ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK) goto done;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    status = astra_filesystem_lstat(filesystem, trashed, &info);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_FILE &&
        info.kind != ASTRA_VFS_KIND_DIRECTORY &&
        info.kind != ASTRA_VFS_KIND_SYMLINK) {
        status = ASTRA_VFS_ERR_UNSUPPORTED;
        goto done;
    }
    status = astra_document_read(filesystem, metadata,
        TRASH_RECORD_HEADER + 1u, TRASH_RECORD_MAX, &record, &length);
    if (status != ASTRA_VFS_OK) goto done;
    relative_length = astra_load_be32(record + 4u);
    if (memcmp(record, "ATR1", 4u) != 0 ||
        relative_length == 0u ||
        relative_length != length - TRASH_RECORD_HEADER ||
        relative_length >= ASTRA_VFS_PATH_MAX ||
        record[TRASH_RECORD_HEADER] == '/' ||
        memchr(record + TRASH_RECORD_HEADER, '\0', relative_length) != NULL ||
        !astra_utf8_validate((const char *)record + TRASH_RECORD_HEADER,
            relative_length, 0u)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    size_t root_length = strlen(volume_root);
    if (root_length + 1u + relative_length >= capacity ||
        root_length + 1u + relative_length >= ASTRA_VFS_PATH_MAX) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    memcpy(origin, volume_root, root_length);
    origin[root_length] = '/';
    memcpy(origin + root_length + 1u, record + TRASH_RECORD_HEADER,
        relative_length);
    origin[root_length + 1u + relative_length] = '\0';
    if (astra_path_requires_walk(origin) ||
        !astra_path_beneath(origin, volume_root) ||
        astra_path_beneath(origin, trash))
        status = ASTRA_VFS_ERR_INVALID;
done:
    if (status != ASTRA_VFS_OK) origin[0] = '\0';
    astra_runtime_deallocate(record);
    astra_runtime_deallocate(paths);
    return status;
}

uint32_t astra_trash_restore(AstraFilesystem *filesystem,
    const char *volume_root, const char *trashed,
    char *origin, uint32_t capacity, uint32_t *moved)
{
    char *paths, *trash, *origins, *metadata;
    uint32_t status;
    if (moved != NULL) *moved = 0u;
    if (moved == NULL) return ASTRA_VFS_ERR_INVALID;
    status = astra_trash_origin(filesystem, volume_root, trashed,
        origin, capacity);
    if (status != ASTRA_VFS_OK) return status;
    paths = astra_runtime_allocate(3u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    trash = paths;
    origins = trash + ASTRA_VFS_PATH_MAX;
    metadata = origins + ASTRA_VFS_PATH_MAX;
    status = trash_paths(volume_root, trash, origins);
    if (status == ASTRA_VFS_OK)
        status = astra_document_path_join(origins, astra_path_leaf(trashed),
            metadata, ASTRA_VFS_PATH_MAX);
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename_noreplace(filesystem, trashed,
            origin);
    if (status == ASTRA_VFS_OK) {
        *moved = 1u;
        status = astra_filesystem_unlink(filesystem, metadata, 0u);
    }
    astra_runtime_deallocate(paths);
    return status;
}

static uint32_t first_payload(AstraFilesystem *filesystem,
    const char *trash, char *name, int *found)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entry;
    uint32_t status = astra_filesystem_directory_open(filesystem, trash,
        &directory);
    *found = 0;
    if (status != ASTRA_VFS_OK) return status;
    for (;;) {
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, &entry, 1u,
            &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        if (memchr(entry.name, '\0', ASTRA_VFS_NAME_MAX) == NULL) {
            status = ASTRA_VFS_ERR_INVALID;
            break;
        }
        if (strcmp(entry.name, ".") == 0 ||
            strcmp(entry.name, "..") == 0 ||
            strcmp(entry.name, ".origins") == 0) continue;
        memcpy(name, entry.name, strlen(entry.name) + 1u);
        *found = 1;
        break;
    }
    astra_filesystem_directory_close(&directory);
    return status;
}

uint32_t astra_trash_empty(AstraFilesystem *filesystem,
    const char *volume_root, char *failed, uint32_t capacity,
    uint32_t *removed)
{
    char *paths, *trash, *origins, *payload, *metadata;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t status;
    int has_origins = 0;
    if (removed != NULL) *removed = 0u;
    if (failed != NULL && capacity != 0u) failed[0] = '\0';
    if (filesystem == NULL || !valid_root(volume_root) ||
        failed == NULL || capacity == 0u || removed == NULL)
        return ASTRA_VFS_ERR_INVALID;
    paths = astra_runtime_allocate(4u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) return ASTRA_VFS_ERR_LIMIT;
    trash = paths;
    origins = trash + ASTRA_VFS_PATH_MAX;
    payload = origins + ASTRA_VFS_PATH_MAX;
    metadata = payload + ASTRA_VFS_PATH_MAX;
    status = trash_paths(volume_root, trash, origins);
    if (status != ASTRA_VFS_OK) goto done;
    if (strlen(trash) >= capacity) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    memcpy(failed, trash, strlen(trash) + 1u);
    status = existing_store(filesystem, trash, origins, &has_origins);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        status = ASTRA_VFS_OK;
        goto done;
    }
    if (status != ASTRA_VFS_OK) goto done;
    for (;;) {
        int found = 0;
        status = first_payload(filesystem, trash, name, &found);
        if (status != ASTRA_VFS_OK || !found) break;
        status = astra_document_path_join(trash, name, payload,
            ASTRA_VFS_PATH_MAX);
        if (status != ASTRA_VFS_OK) break;
        if (strlen(payload) >= capacity) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        memcpy(failed, payload, strlen(payload) + 1u);
        status = astra_filesystem_remove_tree_internal(filesystem, payload);
        if (status != ASTRA_VFS_OK) break;
        ++*removed;
        if (has_origins) {
            status = astra_document_path_join(origins, name, metadata,
                ASTRA_VFS_PATH_MAX);
            if (status != ASTRA_VFS_OK) break;
            if (strlen(metadata) >= capacity) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            memcpy(failed, metadata, strlen(metadata) + 1u);
            status = astra_filesystem_unlink(filesystem, metadata, 0u);
            if (status != ASTRA_VFS_OK &&
                status != ASTRA_VFS_ERR_NOT_FOUND) break;
        }
        failed[0] = '\0';
    }
done:
    if (status == ASTRA_VFS_OK) failed[0] = '\0';
    astra_runtime_deallocate(paths);
    return status;
}
