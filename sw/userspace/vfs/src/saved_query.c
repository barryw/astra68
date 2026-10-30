#include <astra/saved_query.h>
#include <astra/runtime.h>
#include <astra/utf8.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define QUERY_HEADER_BYTES 16u
#define QUERY_BYTES_MAX (QUERY_HEADER_BYTES + 2u * (ASTRA_VFS_NAME_MAX - 1u) + \
    ASTRA_VFS_PATH_MAX - 1u)

static void put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint32_t get32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
        ((uint32_t)in[2] << 8) | in[3];
}

static uint32_t field_length(const char *text, uint32_t capacity)
{
    const char *end = memchr(text, '\0', capacity);
    return end == NULL ? UINT32_MAX : (uint32_t)(end - text);
}

static int query_valid(const AstraSavedQuery *query)
{
    uint32_t label, directory, text;
    if (query == NULL || query->size != sizeof(*query)) return 0;
    label = field_length(query->label, sizeof(query->label));
    directory = field_length(query->directory, sizeof(query->directory));
    text = field_length(query->text, sizeof(query->text));
    return label != 0u && label != UINT32_MAX &&
        directory != 0u && directory != UINT32_MAX &&
        text != 0u && text != UINT32_MAX &&
        query->directory[0] == '/' &&
        !astra_path_requires_walk(query->directory) &&
        astra_utf8_validate(query->label, label, 0u) &&
        astra_utf8_validate(query->directory, directory, 0u) &&
        astra_utf8_validate(query->text, text, 0u);
}

static uint32_t path_join(const char *directory, const char *name,
    char *out, uint32_t capacity)
{
    size_t base = strlen(directory), leaf = strlen(name);
    uint32_t slash = base != 0u && directory[base - 1u] != '/';
    if (base + slash + leaf >= capacity) return ASTRA_VFS_ERR_LIMIT;
    memcpy(out, directory, base);
    if (slash) out[base++] = '/';
    memcpy(out + base, name, leaf + 1u);
    return ASTRA_VFS_OK;
}

static void query_name(char *out, uint64_t id, int temporary)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t at = 0u;
    if (temporary) out[at++] = '.';
    memcpy(out + at, "query-", 6u);
    at += 6u;
    for (uint32_t shift = 0u; shift < 16u; ++shift)
        out[at++] = hex[(id >> (60u - 4u * shift)) & 15u];
    memcpy(out + at, temporary ? ".tmp" : ".asq", 5u);
}

static uint32_t write_complete(AstraFile *file, const uint8_t *data,
    uint32_t length)
{
    for (uint32_t at = 0u; at < length;) {
        uint32_t moved = 0u;
        uint32_t status = astra_filesystem_write(file, UINT64_MAX,
            data + at, length - at, &moved);
        if (status != ASTRA_VFS_OK) return status;
        if (moved == 0u) return ASTRA_VFS_ERR_IO;
        at += moved;
    }
    return astra_filesystem_sync(file);
}

uint32_t astra_saved_query_save(AstraFilesystem *filesystem,
    const char *store_directory, const AstraSavedQuery *query,
    char *filename, uint32_t filename_capacity)
{
    AstraFileInfo directory_info = ASTRA_FILE_INFO_INIT;
    AstraFile file = ASTRA_FILE_INIT;
    char temporary_name[32], final_name[32];
    char temporary_path[ASTRA_VFS_PATH_MAX], final_path[ASTRA_VFS_PATH_MAX];
    uint8_t *bytes;
    uint32_t label, path, text, length, status;
    uint64_t nonce;

    if (filename != NULL && filename_capacity != 0u) filename[0] = '\0';
    if (filesystem == NULL || store_directory == NULL ||
        store_directory[0] != '/' || astra_path_requires_walk(store_directory) ||
        !query_valid(query) || filename == NULL || filename_capacity < 27u)
        return ASTRA_VFS_ERR_INVALID;
    if (strlen(store_directory) > ASTRA_VFS_PATH_MAX - 32u)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_filesystem_lstat(filesystem, store_directory, &directory_info);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        status = astra_filesystem_mkdir_mode(filesystem, store_directory, 0700u);
        if (status == ASTRA_VFS_ERR_EXISTS)
            status = astra_filesystem_lstat(filesystem, store_directory,
                &directory_info);
        else if (status == ASTRA_VFS_OK)
            directory_info.kind = ASTRA_VFS_KIND_DIRECTORY;
    }
    if (status != ASTRA_VFS_OK) return status;
    if (directory_info.kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;

    label = (uint32_t)strlen(query->label);
    path = (uint32_t)strlen(query->directory);
    text = (uint32_t)strlen(query->text);
    length = QUERY_HEADER_BYTES + label + path + text;
    bytes = astra_runtime_allocate(length);
    if (bytes == NULL) return ASTRA_VFS_ERR_LIMIT;
    memcpy(bytes, "ASQ1", 4u);
    put32(bytes + 4u, label);
    put32(bytes + 8u, path);
    put32(bytes + 12u, text);
    memcpy(bytes + QUERY_HEADER_BYTES, query->label, label);
    memcpy(bytes + QUERY_HEADER_BYTES + label, query->directory, path);
    memcpy(bytes + QUERY_HEADER_BYTES + label + path, query->text, text);

    nonce = astra_clock_monotonic();
    status = ASTRA_VFS_ERR_EXISTS;
    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        query_name(temporary_name, nonce + attempt, 1);
        query_name(final_name, nonce + attempt, 0);
        if (path_join(store_directory, temporary_name, temporary_path,
                sizeof(temporary_path)) != ASTRA_VFS_OK ||
            path_join(store_directory, final_name, final_path,
                sizeof(final_path)) != ASTRA_VFS_OK) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        status = astra_filesystem_open_mode(filesystem, temporary_path,
            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_EXCLUSIVE, 0600u, &file);
        if (status != ASTRA_VFS_OK) {
            if (status == ASTRA_VFS_ERR_EXISTS) continue;
            break;
        }
        status = write_complete(&file, bytes, length);
        uint32_t close_status = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = close_status;
        if (status == ASTRA_VFS_OK)
            status = astra_filesystem_link(filesystem, temporary_path, final_path);
        (void)astra_filesystem_unlink(filesystem, temporary_path, 0u);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status == ASTRA_VFS_OK)
            memcpy(filename, final_name, strlen(final_name) + 1u);
        break;
    }
    astra_runtime_deallocate(bytes);
    return status;
}

uint32_t astra_saved_query_load(AstraFilesystem *filesystem,
    const char *path, AstraSavedQuery *query)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint8_t *bytes = NULL;
    uint32_t status, length, label, directory, text;
    AstraSavedQuery next = ASTRA_SAVED_QUERY_INIT;

    if (filesystem == NULL || path == NULL || query == NULL ||
        query->size != sizeof(*query) || path[0] != '/' ||
        strlen(path) >= ASTRA_VFS_PATH_MAX || astra_path_requires_walk(path))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_file_info(&file, &info);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_FILE ||
        info.byte_size < QUERY_HEADER_BYTES ||
        info.byte_size > QUERY_BYTES_MAX) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    length = (uint32_t)info.byte_size;
    bytes = astra_runtime_allocate(length);
    if (bytes == NULL) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    for (uint32_t at = 0u; at < length;) {
        uint32_t moved = 0u;
        status = astra_filesystem_read(&file, bytes + at, length - at, &moved);
        if (status != ASTRA_VFS_OK) goto done;
        if (moved == 0u) {
            status = ASTRA_VFS_ERR_INVALID;
            goto done;
        }
        at += moved;
    }
    label = get32(bytes + 4u);
    directory = get32(bytes + 8u);
    text = get32(bytes + 12u);
    if (memcmp(bytes, "ASQ1", 4u) != 0 ||
        label == 0u || label >= sizeof(next.label) ||
        directory == 0u || directory >= sizeof(next.directory) ||
        text == 0u || text >= sizeof(next.text) ||
        (uint64_t)QUERY_HEADER_BYTES + label + directory + text != length) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    memcpy(next.label, bytes + QUERY_HEADER_BYTES, label);
    memcpy(next.directory, bytes + QUERY_HEADER_BYTES + label, directory);
    memcpy(next.text, bytes + QUERY_HEADER_BYTES + label + directory, text);
    const char *name = path;
    for (const char *at = path; *at != '\0'; ++at)
        if (*at == '/') name = at + 1u;
    if (name[0] == '\0' || strlen(name) >= ASTRA_VFS_NAME_MAX) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    memcpy(next.filename, name, strlen(name) + 1u);
    if (!query_valid(&next)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
done:
    astra_runtime_deallocate(bytes);
    uint32_t close_status = astra_filesystem_close(&file);
    if (status == ASTRA_VFS_OK) status = close_status;
    if (status == ASTRA_VFS_OK) *query = next;
    return status;
}

uint32_t astra_saved_query_snapshot_open(AstraFilesystem *filesystem,
    const char *store_directory, AstraSavedQuerySnapshot *snapshot)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraSavedQuerySnapshot next = ASTRA_SAVED_QUERY_SNAPSHOT_INIT;
    uint32_t status;
    if (filesystem == NULL || store_directory == NULL || snapshot == NULL ||
        store_directory[0] != '/' ||
        strlen(store_directory) >= ASTRA_VFS_PATH_MAX ||
        astra_path_requires_walk(store_directory))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_directory_open(filesystem, store_directory,
        &directory);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        *snapshot = next;
        return ASTRA_VFS_OK;
    }
    if (status != ASTRA_VFS_OK) return status;
    for (;;) {
        AstraDirectoryEntry entries[8];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 8u,
            &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            size_t length = strlen(entries[at].name);
            char path[ASTRA_VFS_PATH_MAX];
            AstraSavedQuery preset = ASTRA_SAVED_QUERY_INIT;
            if (length < 5u || strcmp(entries[at].name + length - 4u,
                    ".asq") != 0 ||
                path_join(store_directory, entries[at].name, path,
                    sizeof(path)) != ASTRA_VFS_OK)
                continue;
            uint32_t loaded = astra_saved_query_load(filesystem, path, &preset);
            if (loaded == ASTRA_VFS_ERR_INVALID ||
                loaded == ASTRA_VFS_ERR_NOT_FOUND) continue;
            if (loaded != ASTRA_VFS_OK) {
                status = loaded;
                break;
            }
            size_t allocation_bytes;
            if (next.count == UINT32_MAX ||
                __builtin_mul_overflow((size_t)next.count + 1u,
                    sizeof(*next.queries), &allocation_bytes)) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            AstraSavedQuery *grown = astra_runtime_reallocate(next.queries,
                allocation_bytes);
            if (grown == NULL) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            next.queries = grown;
            next.queries[next.count++] = preset;
        }
        if (status != ASTRA_VFS_OK) break;
    }
    astra_filesystem_directory_close(&directory);
    if (status != ASTRA_VFS_OK) {
        astra_saved_query_snapshot_close(&next);
        return status;
    }
    *snapshot = next;
    return ASTRA_VFS_OK;
}

void astra_saved_query_snapshot_close(AstraSavedQuerySnapshot *snapshot)
{
    if (snapshot == NULL) return;
    astra_runtime_deallocate(snapshot->queries);
    *snapshot = (AstraSavedQuerySnapshot)ASTRA_SAVED_QUERY_SNAPSHOT_INIT;
}
