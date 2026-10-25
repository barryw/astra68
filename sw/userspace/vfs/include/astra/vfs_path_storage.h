#ifndef ASTRA_VFS_PATH_STORAGE_H
#define ASTRA_VFS_PATH_STORAGE_H

/* Native threads have small stacks. Path work belongs to this call's heap,
 * including during nested loader and namespace lookups; it is never shared
 * process/TLS scratch. Reuse the NDK's existing scoped ownership idiom. */
#include <astra/attributes.h>
#include <astra/runtime.h>
#include <astra/vfs_service.h>

typedef struct AstraVfsPathStorage {
    char *bytes;
    uint32_t count;
} AstraVfsPathStorage;

static inline AstraVfsPathStorage astra_vfs_path_storage_open(uint32_t count)
{
    AstraVfsPathStorage storage = {0};

    if (count == 0u || count > UINT32_MAX / ASTRA_VFS_PATH_MAX)
        return storage;
    storage.bytes = astra_runtime_allocate((size_t)count * ASTRA_VFS_PATH_MAX);
    if (storage.bytes == NULL)
        return storage;
    storage.count = count;
    for (uint32_t at = 0u; at < count; ++at)
        storage.bytes[at * ASTRA_VFS_PATH_MAX] = '\0';
    return storage;
}

static inline char *astra_vfs_path_storage_at(AstraVfsPathStorage *storage,
                                              uint32_t index)
{
    return storage != NULL && index < storage->count ?
        storage->bytes + (size_t)index * ASTRA_VFS_PATH_MAX : NULL;
}

static inline void astra_vfs_path_storage_close(AstraVfsPathStorage *storage)
{
    if (storage == NULL)
        return;
    astra_runtime_deallocate(storage->bytes);
    *storage = (AstraVfsPathStorage){0};
}

#define ASTRA_AUTO_VFS_PATH_STORAGE \
    ASTRA_CLEANUP(astra_vfs_path_storage_close)

#endif
