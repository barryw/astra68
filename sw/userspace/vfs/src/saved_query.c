#include <astra/saved_query.h>
#include <astra/document_store.h>
#include <astra/endian.h>
#include <astra/runtime.h>
#include <astra/utf8.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define QUERY_HEADER_BYTES 16u
#define QUERY_BYTES_MAX (QUERY_HEADER_BYTES + 2u * (ASTRA_VFS_NAME_MAX - 1u) + \
    ASTRA_VFS_PATH_MAX - 1u)

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

static int filename_valid(const char *filename)
{
    if (field_length(filename, ASTRA_VFS_NAME_MAX) != 26u ||
        memcmp(filename, "query-", 6u) != 0 ||
        memcmp(filename + 22u, ".asq", 4u) != 0)
        return 0;
    for (uint32_t at = 6u; at < 22u; ++at)
        if (!((filename[at] >= '0' && filename[at] <= '9') ||
              (filename[at] >= 'a' && filename[at] <= 'f')))
            return 0;
    return 1;
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

static uint32_t encode_query(const AstraSavedQuery *query, uint8_t **out,
    uint32_t *length)
{
    uint32_t label = (uint32_t)strlen(query->label);
    uint32_t path = (uint32_t)strlen(query->directory);
    uint32_t text = (uint32_t)strlen(query->text);
    uint32_t bytes_length = QUERY_HEADER_BYTES + label + path + text;
    uint8_t *bytes = astra_runtime_allocate(bytes_length);
    if (bytes == NULL) return ASTRA_VFS_ERR_LIMIT;
    memcpy(bytes, "ASQ1", 4u);
    astra_store_be32(bytes + 4u, label);
    astra_store_be32(bytes + 8u, path);
    astra_store_be32(bytes + 12u, text);
    memcpy(bytes + QUERY_HEADER_BYTES, query->label, label);
    memcpy(bytes + QUERY_HEADER_BYTES + label, query->directory, path);
    memcpy(bytes + QUERY_HEADER_BYTES + label + path, query->text, text);
    *out = bytes;
    *length = bytes_length;
    return ASTRA_VFS_OK;
}

uint32_t astra_saved_query_save(AstraFilesystem *filesystem,
    const char *store_directory, const AstraSavedQuery *query,
    char *filename, uint32_t filename_capacity)
{
    char temporary_name[32], final_name[32];
    char *paths, *temporary_path, *final_path;
    uint8_t *bytes = NULL;
    uint32_t length = 0u, status;
    uint64_t nonce;

    if (filename != NULL && filename_capacity != 0u) filename[0] = '\0';
    if (filesystem == NULL || store_directory == NULL ||
        store_directory[0] != '/' || astra_path_requires_walk(store_directory) ||
        !query_valid(query) || filename == NULL || filename_capacity < 27u)
        return ASTRA_VFS_ERR_INVALID;
    if (strlen(store_directory) > ASTRA_VFS_PATH_MAX - 32u)
        return ASTRA_VFS_ERR_LIMIT;
    status = astra_document_store_prepare(filesystem, store_directory);
    if (status != ASTRA_VFS_OK) return status;

    status = encode_query(query, &bytes, &length);
    if (status != ASTRA_VFS_OK) return status;
    paths = astra_runtime_allocate(2u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) {
        astra_runtime_deallocate(bytes);
        return ASTRA_VFS_ERR_LIMIT;
    }
    temporary_path = paths;
    final_path = paths + ASTRA_VFS_PATH_MAX;

    nonce = astra_clock_monotonic();
    status = ASTRA_VFS_ERR_EXISTS;
    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        query_name(temporary_name, nonce + attempt, 1);
        query_name(final_name, nonce + attempt, 0);
        if (astra_document_path_join(store_directory, temporary_name, temporary_path,
                ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK ||
            astra_document_path_join(store_directory, final_name, final_path,
                ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        status = astra_document_stage(filesystem, temporary_path, bytes, length);
        if (status != ASTRA_VFS_OK) {
            if (status == ASTRA_VFS_ERR_EXISTS) continue;
            break;
        }
        if (status == ASTRA_VFS_OK)
            status = astra_filesystem_link(filesystem, temporary_path, final_path);
        (void)astra_filesystem_unlink(filesystem, temporary_path, 0u);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status == ASTRA_VFS_OK)
            memcpy(filename, final_name, strlen(final_name) + 1u);
        break;
    }
    astra_runtime_deallocate(paths);
    astra_runtime_deallocate(bytes);
    return status;
}

uint32_t astra_saved_query_set_label(AstraFilesystem *filesystem,
    const char *store_directory, const AstraSavedQuery *query,
    const char *label)
{
    AstraSavedQuery *replacement, *current;
    char *paths, *final_path, *temporary_path;
    char temporary_name[32];
    uint8_t *scratch;
    uint8_t *bytes = NULL;
    uint32_t length = 0u, status;
    uint32_t label_length;

    if (filesystem == NULL || store_directory == NULL || query == NULL ||
        label == NULL || store_directory[0] != '/' ||
        strlen(store_directory) > ASTRA_VFS_PATH_MAX - 32u ||
        astra_path_requires_walk(store_directory) ||
        !query_valid(query) || !filename_valid(query->filename))
        return ASTRA_VFS_ERR_INVALID;
    label_length = field_length(label, ASTRA_VFS_NAME_MAX);
    if (label_length == 0u || label_length == UINT32_MAX ||
        !astra_utf8_validate(label, label_length, 0u))
        return ASTRA_VFS_ERR_INVALID;
    scratch = astra_runtime_allocate(2u * sizeof(*current) +
        2u * ASTRA_VFS_PATH_MAX);
    if (scratch == NULL) return ASTRA_VFS_ERR_LIMIT;
    current = (AstraSavedQuery *)scratch;
    replacement = current + 1u;
    paths = (char *)(replacement + 1u);
    final_path = paths;
    temporary_path = paths + ASTRA_VFS_PATH_MAX;
    *current = (AstraSavedQuery)ASTRA_SAVED_QUERY_INIT;
    status = astra_document_path_join(store_directory, query->filename,
        final_path, ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK) goto done;
    status = astra_saved_query_load(filesystem, final_path, current);
    if (status != ASTRA_VFS_OK) goto done;
    if (strcmp(current->label, query->label) != 0 ||
        strcmp(current->directory, query->directory) != 0 ||
        strcmp(current->text, query->text) != 0) {
        status = ASTRA_VFS_ERR_BUSY;
        goto done;
    }
    *replacement = *current;
    memcpy(replacement->label, label, label_length + 1u);
    status = encode_query(replacement, &bytes, &length);
    if (status != ASTRA_VFS_OK) goto done;
    uint64_t nonce = astra_clock_monotonic();
    status = ASTRA_VFS_ERR_EXISTS;
    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        query_name(temporary_name, nonce + attempt, 1);
        if (astra_document_path_join(store_directory, temporary_name, temporary_path,
                ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        status = astra_document_stage(filesystem, temporary_path, bytes, length);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status != ASTRA_VFS_OK) break;
        status = astra_filesystem_rename(filesystem, temporary_path,
            final_path);
        if (status != ASTRA_VFS_OK)
            (void)astra_filesystem_unlink(filesystem, temporary_path, 0u);
        break;
    }
done:
    astra_runtime_deallocate(bytes);
    astra_runtime_deallocate(scratch);
    return status;
}

uint32_t astra_saved_query_load(AstraFilesystem *filesystem,
    const char *path, AstraSavedQuery *query)
{
    uint8_t *bytes = NULL;
    uint32_t status, length, label, directory, text;
    AstraSavedQuery *next;

    if (filesystem == NULL || path == NULL || query == NULL ||
        query->size != sizeof(*query) || path[0] != '/' ||
        strlen(path) >= ASTRA_VFS_PATH_MAX || astra_path_requires_walk(path))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_document_read(filesystem, path, QUERY_HEADER_BYTES,
        QUERY_BYTES_MAX, &bytes, &length);
    if (status != ASTRA_VFS_OK) return status;
    next = astra_runtime_allocate(sizeof(*next));
    if (next == NULL) {
        astra_runtime_deallocate(bytes);
        return ASTRA_VFS_ERR_LIMIT;
    }
    *next = (AstraSavedQuery)ASTRA_SAVED_QUERY_INIT;
    label = astra_load_be32(bytes + 4u);
    directory = astra_load_be32(bytes + 8u);
    text = astra_load_be32(bytes + 12u);
    if (memcmp(bytes, "ASQ1", 4u) != 0 ||
        label == 0u || label >= sizeof(next->label) ||
        directory == 0u || directory >= sizeof(next->directory) ||
        text == 0u || text >= sizeof(next->text) ||
        (uint64_t)QUERY_HEADER_BYTES + label + directory + text != length) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    memcpy(next->label, bytes + QUERY_HEADER_BYTES, label);
    memcpy(next->directory, bytes + QUERY_HEADER_BYTES + label, directory);
    memcpy(next->text, bytes + QUERY_HEADER_BYTES + label + directory, text);
    const char *name = path;
    for (const char *at = path; *at != '\0'; ++at)
        if (*at == '/') name = at + 1u;
    if (name[0] == '\0' || strlen(name) >= ASTRA_VFS_NAME_MAX) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
    memcpy(next->filename, name, strlen(name) + 1u);
    if (!query_valid(next)) {
        status = ASTRA_VFS_ERR_INVALID;
        goto done;
    }
done:
    astra_runtime_deallocate(bytes);
    if (status == ASTRA_VFS_OK) *query = *next;
    astra_runtime_deallocate(next);
    return status;
}

uint32_t astra_saved_query_snapshot_open(AstraFilesystem *filesystem,
    const char *store_directory, AstraSavedQuerySnapshot *snapshot)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraSavedQuerySnapshot next = ASTRA_SAVED_QUERY_SNAPSHOT_INIT;
    AstraSavedQuery *preset;
    char *path;
    uint8_t *scratch;
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
    scratch = astra_runtime_allocate(sizeof(*preset) + ASTRA_VFS_PATH_MAX);
    if (scratch == NULL) {
        astra_filesystem_directory_close(&directory);
        return ASTRA_VFS_ERR_LIMIT;
    }
    preset = (AstraSavedQuery *)scratch;
    path = (char *)(preset + 1u);
    for (;;) {
        AstraDirectoryEntry entries[1];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 1u,
            &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            size_t length = strlen(entries[at].name);
            if (length < 5u || strcmp(entries[at].name + length - 4u,
                    ".asq") != 0 ||
                astra_document_path_join(store_directory, entries[at].name, path,
                    ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK)
                continue;
            *preset = (AstraSavedQuery)ASTRA_SAVED_QUERY_INIT;
            uint32_t loaded = astra_saved_query_load(filesystem, path, preset);
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
            next.queries[next.count++] = *preset;
        }
        if (status != ASTRA_VFS_OK) break;
    }
    astra_filesystem_directory_close(&directory);
    astra_runtime_deallocate(scratch);
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
