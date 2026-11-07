#ifndef ASTRA_VFS_FILE_PAGER_H
#define ASTRA_VFS_FILE_PAGER_H

#include <astra/vfs_backend.h>

/* Borrowed description: the cache owner keeps it alive until all mappings
 * and in-flight faults for this cookie have retired. */
typedef struct AstraVfsFilePageSource {
    AstraVfsBackend backend;
    AstraVfsNodeMeta identity;
    uintptr_t node;
    uint32_t span;
    uint32_t cookie;
} AstraVfsFilePageSource;

typedef const AstraVfsFilePageSource *(*AstraVfsFilePageLookup)(
    void *context, uint32_t cookie);

/* One receive port and scratch data area serve every file cache object. */
typedef struct AstraVfsFilePager {
    uint32_t receive;
    uint32_t data_area;
    uint8_t *page;
    AstraVfsFilePageLookup lookup;
    void *context;
    uint32_t stalled;
} AstraVfsFilePager;

int astra_vfs_file_page_source_init(AstraVfsFilePageSource *source,
    const AstraVfsBackend *backend, uintptr_t node,
    const AstraVfsNodeMeta *identity, uint32_t span, uint32_t cookie);
int astra_vfs_file_pager_init(AstraVfsFilePager *pager, uint32_t receive,
    uint32_t data_area, void *page, uint32_t page_size,
    AstraVfsFilePageLookup lookup, void *context);

/* Consume at most budget queued page requests. WOULD_BLOCK is an empty port;
 * other receive failures remain in stalled for the service owner. */
uint32_t astra_vfs_file_pager_pump(AstraVfsFilePager *pager, uint32_t budget);

#endif
