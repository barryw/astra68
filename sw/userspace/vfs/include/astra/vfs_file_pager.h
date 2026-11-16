#ifndef ASTRA_VFS_FILE_PAGER_H
#define ASTRA_VFS_FILE_PAGER_H

#include <astra/vfs_service_core.h>

/* Cache owner retains the description while mappings or acquired faults live.
 * Incomplete identity keeps the area scoped to this description. */
typedef struct AstraVfsFilePageSource {
    AstraVfsService *service;
    AstraVfsFile retained;
    uint32_t access;
    AstraVfsNodeMeta identity;
    uint64_t base;
    uint32_t span;
    uint32_t cookie;
} AstraVfsFilePageSource;

/* Acquire pins the source until release; neither callback may hold its lock
 * through provider I/O or the reply send. */
typedef const AstraVfsFilePageSource *(*AstraVfsFilePageAcquire)(
    void *context, uint32_t cookie);
typedef void (*AstraVfsFilePageRelease)(void *context,
    const AstraVfsFilePageSource *source);

/* One receive port and scratch data area serve every file cache object. */
typedef struct AstraVfsFilePager {
    uint32_t receive;
    uint32_t sync_send; /* Optional control-worker handoff; zero refuses sync. */
    uint32_t data_area;
    uint8_t *page;
    AstraVfsFilePageAcquire acquire;
    AstraVfsFilePageRelease release;
    void *context;
    uint32_t stalled;
} AstraVfsFilePager;

int astra_vfs_file_page_source_init(AstraVfsFilePageSource *source,
    AstraVfsService *service, AstraVfsFile retained, uint32_t access,
    const AstraVfsNodeMeta *identity, uint64_t base, uint32_t span,
    uint32_t cookie);
int astra_vfs_file_pager_init(AstraVfsFilePager *pager, uint32_t receive,
    uint32_t data_area, void *page, uint32_t page_size,
    AstraVfsFilePageAcquire acquire, AstraVfsFilePageRelease release,
    void *context);

/* Consume at most budget queued page requests. WOULD_BLOCK is an empty port;
 * other receive failures remain in stalled for the service owner. */
uint32_t astra_vfs_file_pager_pump(AstraVfsFilePager *pager, uint32_t budget);
/* Run on the native cache's control worker, which must service dirty generations
 * with bounded writeback even after views close. ASYNC acknowledges that work;
 * synchronous requests also complete backing sync. The reply capability keeps
 * caller/area lifetime in the kernel; no userspace pending-request list exists. */
uint32_t astra_vfs_file_pager_sync_pump(const AstraVfsFilePager *pager,
    uint32_t receive, uint32_t budget, uint32_t *stalled);

/* Flush one canonical area using the cache's retained WRITE authority. Caller
 * pins source/area and holds active's node I/O throughout. Scratch is one page,
 * private to this call. Each dirty-scan call consumes one remaining step.
 * Cursor resumes bounded work; only a complete clean scan from zero succeeds.
 * BUSY preserves pending/redirtied/in-flight pages. Errors reset cursor for retry.
 * No byte store or dirty ledger is owned here. */
uint32_t astra_vfs_file_pager_writeback(const AstraVfsFilePageSource *source,
    const AstraVfsOpenFile *active, AstraVfsFile writer, uint32_t area,
    void *page, uint32_t *remaining, uint32_t *cursor);

#endif
