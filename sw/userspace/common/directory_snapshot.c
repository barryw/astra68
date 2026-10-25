#include "directory_snapshot.h"

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
