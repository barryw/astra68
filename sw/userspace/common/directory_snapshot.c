#include "directory_snapshot.h"

#include <astra/ascii.h>
#include <astra/runtime.h>

#include <stdint.h>
#include <string.h>

static uint32_t append(AstraDirectorySnapshot *model, const AstraDirectoryEntry *entry)
{
    if (model->count == model->capacity) {
        uint32_t capacity = model->capacity == 0u ? 32u : model->capacity * 2u;
        AstraDirectoryEntry *entries;

        if (capacity <= model->capacity ||
            capacity > UINT32_MAX / sizeof(*entries))
            return ASTRA_VFS_ERR_LIMIT;
        entries = astra_runtime_allocate((size_t)capacity * sizeof(*entries));
        if (entries == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        if (model->count != 0u)
            memcpy(entries, model->entries,
                   (size_t)model->count * sizeof(*entries));
        astra_runtime_deallocate(model->entries);
        model->entries = entries;
        model->capacity = capacity;
    }
    model->entries[model->count++] = *entry;
    return ASTRA_VFS_OK;
}

void astra_directory_snapshot_dispose(AstraDirectorySnapshot *model)
{
    if (model == NULL)
        return;
    astra_runtime_deallocate(model->entries);
    *model = (AstraDirectorySnapshot)ASTRA_DIRECTORY_SNAPSHOT_INIT;
}

void astra_directory_snapshot_filter_hidden(AstraDirectorySnapshot *model)
{
    uint32_t visible = 0u;
    if (model == NULL || model->status != ASTRA_VFS_OK) return;
    for (uint32_t at = 0u; at < model->count; ++at)
        if (model->entries[at].name[0] != '.')
            model->entries[visible++] = model->entries[at];
    model->count = visible;
}

static int name_compare(const char *left, const char *right)
{
    for (;;) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        unsigned char lower_a = a < 128u ? (unsigned char)astra_ascii_lower(a) : a;
        unsigned char lower_b = b < 128u ? (unsigned char)astra_ascii_lower(b) : b;
        if (lower_a != lower_b) return lower_a < lower_b ? -1 : 1;
        if (a == 0u) return 0;
    }
}

static const char *extension(const char *name)
{
    const char *dot = NULL;
    for (const char *at = name; *at != '\0'; ++at)
        if (*at == '.') dot = at;
    return dot != NULL && dot != name ? dot + 1u : "";
}

static int entry_compare(const AstraDirectoryEntry *left,
    const AstraDirectoryEntry *right, uint32_t key, uint32_t reverse)
{
    int left_directory = left->kind == ASTRA_VFS_KIND_DIRECTORY;
    int right_directory = right->kind == ASTRA_VFS_KIND_DIRECTORY;
    int compared = 0;
    if (left_directory != right_directory)
        return left_directory ? -1 : 1;
    if (key == ASTRA_DIRECTORY_SORT_SIZE && left->size != right->size)
        compared = left->size < right->size ? -1 : 1;
    else if (key == ASTRA_DIRECTORY_SORT_MODIFIED &&
             left->mtime != right->mtime)
        compared = left->mtime > right->mtime ? -1 : 1;
    else if (key == ASTRA_DIRECTORY_SORT_KIND)
        compared = name_compare(extension(left->name),
                                extension(right->name));
    if (compared == 0) compared = name_compare(left->name, right->name);
    return reverse != 0u ? -compared : compared;
}

uint32_t astra_directory_snapshot_sort(AstraDirectorySnapshot *model,
    uint32_t key, uint32_t reverse)
{
    AstraDirectoryEntry *scratch, *source, *target;
    if (model == NULL || model->status != ASTRA_VFS_OK ||
        key > ASTRA_DIRECTORY_SORT_MODIFIED || reverse > 1u)
        return ASTRA_VFS_ERR_INVALID;
    if (model->count < 2u) return ASTRA_VFS_OK;
    size_t bytes = (size_t)model->count * sizeof(*scratch);
    if (bytes / sizeof(*scratch) != model->count)
        return ASTRA_VFS_ERR_LIMIT;
    scratch = astra_runtime_allocate(bytes);
    if (scratch == NULL) return ASTRA_VFS_ERR_LIMIT;
    source = model->entries;
    target = scratch;
    for (uint64_t span = 1u; span < model->count; span *= 2u) {
        for (uint64_t base = 0u; base < model->count; base += span * 2u) {
            uint64_t middle = base + span < model->count ?
                base + span : model->count;
            uint64_t end = base + span * 2u < model->count ?
                base + span * 2u : model->count;
            uint64_t left = base, right = middle;
            for (uint64_t out = base; out < end; ++out) {
                if (left == middle)
                    target[out] = source[right++];
                else if (right == end ||
                         entry_compare(&source[left], &source[right], key,
                                       reverse) <= 0)
                    target[out] = source[left++];
                else
                    target[out] = source[right++];
            }
        }
        AstraDirectoryEntry *swap = source;
        source = target;
        target = swap;
    }
    if (source != model->entries)
        memcpy(model->entries, source,
            (size_t)model->count * sizeof(*source));
    astra_runtime_deallocate(scratch);
    return ASTRA_VFS_OK;
}

uint32_t astra_directory_snapshot_load(AstraDirectorySnapshot *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry *batch;
    uint32_t status;

    if (model == NULL || filesystem == NULL || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    astra_directory_snapshot_dispose(model);
    batch = astra_runtime_allocate(8u * sizeof(*batch));
    if (batch == NULL) { model->status = ASTRA_VFS_ERR_LIMIT; return model->status; }
    status = astra_filesystem_directory_open(filesystem, path, &directory);
    if (status == ASTRA_VFS_OK) {
        while (status == ASTRA_VFS_OK) {
            uint32_t count = 0u;

            if (cancelled != NULL &&
                __atomic_load_n(cancelled, __ATOMIC_ACQUIRE) != 0u) {
                status = ASTRA_DIRECTORY_SNAPSHOT_CANCELLED;
                break;
            }
            status = astra_filesystem_directory_read(&directory, batch, 8u,
                                                      &count);
            if (status != ASTRA_VFS_OK || count == 0u)
                break;
            if (count > 8u) {
                status = ASTRA_VFS_ERR_INVALID;
                break;
            }
            for (uint32_t at = 0u; at < count; ++at) {
                if (strcmp(batch[at].name, ".") == 0 ||
                    strcmp(batch[at].name, "..") == 0)
                    continue;
                status = append(model, &batch[at]);
                if (status != ASTRA_VFS_OK)
                    break;
            }
        }
        astra_filesystem_directory_close(&directory);
    }
    if (status != ASTRA_VFS_OK)
        astra_directory_snapshot_dispose(model);
    astra_runtime_deallocate(batch);
    model->status = status;
    return status;
}
