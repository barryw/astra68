#ifndef ASTRA_VFS_RAM_BACKEND_H
#define ASTRA_VFS_RAM_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include <astra/vfs_backend.h>

typedef void *(*AstraVfsRamAllocate)(size_t bytes);
typedef void (*AstraVfsRamFree)(void *pointer);
typedef int (*AstraVfsRamLock)(void *context);
typedef void (*AstraVfsRamUnlock)(void *context);

typedef struct AstraVfsRamNode AstraVfsRamNode;

typedef struct AstraVfsRamBackend {
    AstraVfsRamNode *root;
    uint64_t max_bytes;
    uint64_t used_bytes;
    uint64_t node_count;
    AstraVfsRamAllocate allocate;
    AstraVfsRamFree deallocate;
    AstraVfsRamLock lock;
    AstraVfsRamUnlock unlock;
    void *lock_context;
} AstraVfsRamBackend;

int astra_vfs_ram_init(AstraVfsRamBackend *backend, uint64_t max_bytes,
                       AstraVfsRamAllocate allocate,
                       AstraVfsRamFree deallocate,
                       AstraVfsRamLock lock, AstraVfsRamUnlock unlock,
                       void *lock_context);
void astra_vfs_ram_destroy(AstraVfsRamBackend *backend);
const AstraVfsBackendOps *astra_vfs_ram_ops(void);

#endif
