#include "directory.h"

#include <astra/runtime.h>
#include <astra/status.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t append(AtlasDirectory *model, const AstraDirectoryEntry *entry)
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

void atlas_directory_dispose(AtlasDirectory *model)
{
    if (model == NULL)
        return;
    astra_runtime_deallocate(model->entries);
    *model = (AtlasDirectory)ATLAS_DIRECTORY_INIT;
}

uint32_t atlas_directory_load(AtlasDirectory *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry batch[8];
    uint32_t status;

    if (model == NULL || filesystem == NULL || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    atlas_directory_dispose(model);
    status = astra_filesystem_directory_open(filesystem, path, &directory);
    if (status == ASTRA_VFS_OK) {
        while (status == ASTRA_VFS_OK) {
            uint32_t count = 0u;

            if (cancelled != NULL &&
                __atomic_load_n(cancelled, __ATOMIC_ACQUIRE) != 0u) {
                status = ATLAS_DIRECTORY_CANCELLED;
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
        atlas_directory_dispose(model);
    model->status = status;
    return status;
}
