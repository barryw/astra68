#ifndef ASTRA_VFS_FILE_PAGER_H
#define ASTRA_VFS_FILE_PAGER_H

#include <astra/vfs_backend.h>

/* Borrowed resources: the cache owner retains the description, port, data
 * area, and page buffer until every mapping and in-flight fault has retired. */
typedef struct AstraVfsFilePager {
    AstraVfsBackend backend;
    AstraVfsNodeMeta identity;
    uintptr_t node;
    uint8_t *page;
    uint32_t span;
    uint32_t cookie;
    uint32_t receive;
    uint32_t data_area;
    uint32_t stalled;
} AstraVfsFilePager;

int astra_vfs_file_pager_init(AstraVfsFilePager *pager,
    const AstraVfsBackend *backend, uintptr_t node,
    const AstraVfsNodeMeta *identity, uint32_t span, uint32_t cookie,
    uint32_t receive, uint32_t data_area, void *page, uint32_t page_size);

/* Consume at most budget queued page requests. WOULD_BLOCK is an empty port;
 * other receive failures remain in stalled for the service owner. */
uint32_t astra_vfs_file_pager_pump(AstraVfsFilePager *pager, uint32_t budget);

#endif
