#include <astra/pager_service.h>
#include <astra/runtime.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_service_core.h>

#include <string.h>

int
astra_vfs_file_page_source_init(AstraVfsFilePageSource *source,
    AstraVfsService *service, AstraVfsFile retained, uint32_t access,
    const AstraVfsNodeMeta *identity, uint64_t base, uint32_t span,
    uint32_t cookie)
{
    if (source == NULL || service == NULL ||
        retained == ASTRA_VFS_FILE_INVALID || !astra_vfs_page_access_valid(access) || identity == NULL ||
        (base & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        span == 0u || (span & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        base > UINT64_MAX - span ||
        cookie == 0u)
        return 0;
    *source = (AstraVfsFilePageSource){
        .service = service, .retained = retained, .access = access, .identity = *identity,
        .base = base, .span = span, .cookie = cookie,
    };
    return 1;
}

int
astra_vfs_file_pager_init(AstraVfsFilePager *pager, uint32_t receive,
    uint32_t data_area, void *page, uint32_t page_size,
    AstraVfsFilePageAcquire acquire, AstraVfsFilePageRelease release,
    void *context)
{
    if (pager == NULL || receive == 0u || data_area == 0u || page == NULL ||
        page_size < ASTRA_MEMORY_PAGE_SIZE || acquire == NULL || release == NULL)
        return 0;
    *pager = (AstraVfsFilePager){
        .receive = receive, .data_area = data_area, .page = page,
        .acquire = acquire, .release = release, .context = context,
    };
    return 1;
}

static int pager_header_valid(const AstraMessageHeader *header, uint32_t size,
    uint32_t expected, uint32_t operation)
{
    return size == expected && header->total_size == expected &&
        header->header_size == ASTRA_MESSAGE_HEADER_SIZE &&
        header->flags == 0u && header->reserved == 0u &&
        header->protocol == ASTRA_PAGER_PROTOCOL &&
        header->protocol_version == ASTRA_PAGER_PROTOCOL_VERSION &&
        header->operation == operation && header->transaction_id != 0u;
}

static int pager_sync_valid(const AstraPagerSyncRequest *request, uint32_t size)
{
    return pager_header_valid(&request->header, size, sizeof(*request),
        ASTRA_PAGER_OPERATION_SYNC) && request->cookie != 0u &&
        (request->flags & ~ASTRA_AREA_SYNC_ASYNC) == 0u && request->byte_size != 0u &&
        ((request->offset | request->byte_size) & (ASTRA_MEMORY_PAGE_SIZE - 1u)) == 0u &&
        request->offset <= UINT32_MAX - request->byte_size;
}

static void pager_reply(uint32_t *stalled, uint32_t handle,
    const void *reply, uint32_t size)
{
    uint32_t status = astra_port_send(handle, reply, size, NULL, 0u);
    if (status != ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_PEER_DEAD)
        *stalled = status;
    (void)astra_close(handle);
}

static void pager_sync_reply(uint32_t *stalled, uint32_t handle,
    const AstraPagerSyncRequest *request, uint32_t status)
{
    AstraPagerSyncReply reply = {0};
    astra_message_header_set(&reply.header, sizeof(reply), ASTRA_PAGER_PROTOCOL,
        ASTRA_PAGER_PROTOCOL_VERSION, ASTRA_PAGER_OPERATION_SYNC_REPLY,
        request->header.transaction_id);
    reply.status = status;
    reply.cookie = request->cookie;
    reply.offset = request->offset;
    reply.byte_size = request->byte_size;
    pager_reply(stalled, handle, &reply, sizeof(reply));
}

uint32_t
astra_vfs_file_pager_pump(AstraVfsFilePager *pager, uint32_t budget)
{
    uint32_t consumed = 0u;

    if (pager == NULL || pager->receive == 0u || pager->page == NULL ||
        pager->acquire == NULL || pager->release == NULL)
        return 0u;
    while (consumed < budget) {
        AstraPagerPageRequest request = {0};
        AstraPagerPageReply reply = {0};
        const AstraVfsFilePageSource *source = NULL;
        uint32_t handle = 0u, size = 0u, count = 0u, valid = 0u;
        uint32_t status = astra_port_receive(pager->receive, &request,
            sizeof(request), &handle, 1u, &size, &count);

        if (status == ASTRA_SYSCALL_WOULD_BLOCK)
            break;
        if (status != ASTRA_SYSCALL_OK) {
            pager->stalled = status;
            break;
        }
        ++consumed;
        if (count != 1u) {
            if (count != 0u) (void)astra_close(handle);
            continue;
        }
        if (request.header.operation == ASTRA_PAGER_OPERATION_SYNC) {
            AstraPagerSyncRequest sync;
            memcpy(&sync, &request, sizeof(sync));
            uint32_t result = ASTRA_STATUS_PROTOCOL;
            if (pager_sync_valid(&sync, size)) {
                if (pager->sync_send == 0u) result = ASTRA_STATUS_UNSUPPORTED;
                else {
                    status = astra_port_send(pager->sync_send, &sync, sizeof(sync), &handle, 1u);
                    if (status == ASTRA_SYSCALL_OK) continue; /* Capability moved. */
                    result = status == ASTRA_SYSCALL_WOULD_BLOCK ? ASTRA_STATUS_BUSY :
                        status == ASTRA_SYSCALL_PEER_DEAD ? ASTRA_STATUS_LOST : ASTRA_STATUS_IO;
                }
            }
            pager_sync_reply(&pager->stalled, handle, &sync, result);
            if (pager->stalled != 0u) break;
            continue;
        }
        astra_message_header_set(&reply.header, sizeof(reply),
            ASTRA_PAGER_PROTOCOL, ASTRA_PAGER_PROTOCOL_VERSION,
            ASTRA_PAGER_OPERATION_PAGE_REPLY,
            request.header.transaction_id);
        reply.cookie = request.cookie;
        reply.offset = request.offset;
        reply.status = ASTRA_STATUS_PROTOCOL;
        if (pager_header_valid(&request.header, size, sizeof(request),
                ASTRA_PAGER_OPERATION_PAGE) &&
            request.cookie != 0u &&
            request.reserved[0] == 0u && request.reserved[1] == 0u &&
            (request.offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) == 0u) {
            source = pager->acquire(pager->context, request.cookie);
            reply.status = source == NULL ? ASTRA_STATUS_LOST :
                request.offset >= source->span ? ASTRA_STATUS_PROTOCOL :
                astra_vfs_service_read_page_retained(source->service, source->retained,
                    source->access,
                    astra_vfs_node_identity_equal(&source->identity,
                        &source->identity) ? &source->identity : NULL,
                    source->base + request.offset,
                    pager->page, &valid);
            if (reply.status == ASTRA_STATUS_OK) {
                if (valid == 0u || valid > ASTRA_MEMORY_PAGE_SIZE)
                    reply.status = ASTRA_STATUS_IO;
                else {
                    reply.data_area = pager->data_area;
                    reply.valid_bytes = valid;
                }
            }
        }
        pager_reply(&pager->stalled, handle, &reply, sizeof(reply));
        if (source != NULL) pager->release(pager->context, source);
        if (pager->stalled != 0u)
            break;
    }
    return consumed;
}

uint32_t astra_vfs_file_pager_sync_pump(const AstraVfsFilePager *pager,
    uint32_t receive, uint32_t budget, uint32_t *stalled)
{
    uint32_t consumed = 0u;
    if (stalled == NULL) return 0u;
    *stalled = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (pager == NULL || receive == 0u ||
        pager->acquire == NULL || pager->release == NULL)
        return 0u;
    *stalled = 0u;
    while (consumed < budget) {
        AstraPagerSyncRequest request = {0};
        const AstraVfsFilePageSource *source = NULL;
        uint32_t handle = 0u, size = 0u, count = 0u;
        uint32_t status = astra_port_receive(receive, &request, sizeof(request),
            &handle, 1u, &size, &count);
        if (status == ASTRA_SYSCALL_WOULD_BLOCK) break;
        if (status != ASTRA_SYSCALL_OK) { *stalled = status; break; }
        ++consumed;
        if (count != 1u) {
            if (count != 0u) (void)astra_close(handle);
            continue;
        }
        status = ASTRA_STATUS_PROTOCOL;
        if (pager_sync_valid(&request, size)) {
            source = pager->acquire(pager->context, request.cookie);
            if (source == NULL) status = ASTRA_STATUS_LOST;
            else if (request.offset >= source->span || request.byte_size > source->span - request.offset)
                status = ASTRA_STATUS_PROTOCOL;
            else if ((request.flags & ASTRA_AREA_SYNC_ASYNC) != 0u) {
                AstraVfsFileCache cache;
                AstraVfsNodeInfo info;
                uint32_t sequence;
                /* Dirty generations already retain the owner's bounded worker work. */
                status = astra_vfs_service_stat_retained(source->service, source->retained, &info);
                if (status == ASTRA_STATUS_OK || status == ASTRA_STATUS_BUSY) {
                    uint32_t saved = astra_vfs_service_cache_snapshot(source->service,
                        source->retained, &cache, &sequence);
                    if (saved != ASTRA_STATUS_OK) status = saved;
                    else if (cache.error != ASTRA_STATUS_OK && cache.error != ASTRA_STATUS_BUSY)
                        status = cache.error;
                }
            } else status = astra_vfs_service_sync_retained(source->service, source->retained, NULL);
        }
        pager_sync_reply(stalled, handle, &request, status);
        if (source != NULL) pager->release(pager->context, source);
        if (*stalled != 0u) break;
    }
    return consumed;
}

uint32_t
astra_vfs_file_pager_writeback(const AstraVfsFilePageSource *source,
    const AstraVfsOpenFile *active, AstraVfsFile writer, uint32_t area,
    void *page, uint32_t *remaining, uint32_t *cursor)
{
    uint32_t status = ASTRA_VFS_OK;
    if (source == NULL || source->service == NULL || active == NULL ||
        !astra_vfs_open_file_active(active) || active->io->cache == NULL ||
        writer == ASTRA_VFS_FILE_INVALID || active->io->cache->writer != writer ||
        area == 0u || page == NULL || remaining == NULL || cursor == NULL ||
        source->span == 0u ||
        ((source->base | source->span | *cursor) & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        source->base > UINT64_MAX - source->span || *cursor > source->span ||
        !astra_vfs_node_identity_equal(&source->identity,
                                      &active->io->cache->backing.meta))
        return ASTRA_VFS_ERR_INVALID;

    if (*cursor == source->span) *cursor = 0u;
    uint32_t start = *cursor;
    int wrote = 0, verifying = 0;
    while (*remaining != 0u) {
        uint32_t offset = 0u, state = 0u;
        --*remaining;
        if (astra_rt_area_dirty_scan(area, *cursor, &offset, &state) != ASTRA_SYSCALL_OK ||
            offset < *cursor || offset > source->span ||
            (offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
            (state & ~(ASTRA_AREA_DIRTY_MODIFIED | ASTRA_AREA_DIRTY_WRITEBACK)) != 0u ||
            (state != 0u && offset == source->span) ||
            (state == 0u && offset == *cursor && offset != source->span)) {
            status = ASTRA_VFS_ERR_IO;
            break;
        }
        if (state == 0u) {
            *cursor = offset;
            if (offset != source->span) continue;
            if (start == 0u && !wrote) return ASTRA_VFS_OK;
            /* Later writes may have redirtied an earlier completed snapshot. */
            *cursor = start = 0u;
            wrote = 0;
            verifying = 1;
            continue;
        }
        *cursor = offset;
        if (verifying || (state & ASTRA_AREA_DIRTY_WRITEBACK) != 0u) {
            status = ASTRA_VFS_ERR_BUSY;
            break;
        }

        AstraVfsFileCache cache;
        AstraVfsNodeInfo current;
        uint32_t sequence, token = 0u, moved = 0u;
        status = astra_vfs_service_cache_snapshot(source->service, writer, &cache, &sequence);
        if (status != ASTRA_VFS_OK) break;
        uint64_t absolute = source->base + offset;
        if (absolute >= cache.size) {
            status = ASTRA_VFS_ERR_BUSY;
            break;
        }
        uint32_t result = astra_rt_area_writeback_begin(area, offset, page, &token);
        if (result != ASTRA_SYSCALL_OK) {
            status = result == ASTRA_SYSCALL_WOULD_BLOCK ? ASTRA_VFS_ERR_BUSY : ASTRA_VFS_ERR_IO;
            break;
        }
        status = astra_vfs_service_write_page_retained(source->service, writer,
            active, &cache.backing, absolute, cache.size, page, &moved, &current);
        uint32_t expected = cache.size - absolute < ASTRA_MEMORY_PAGE_SIZE ?
            (uint32_t)(cache.size - absolute) : ASTRA_MEMORY_PAGE_SIZE;
        if (status == ASTRA_VFS_OK && moved != expected) status = ASTRA_VFS_ERR_IO;
        if (astra_rt_area_writeback_complete(area, offset, token,
                status == ASTRA_VFS_OK) != ASTRA_SYSCALL_OK)
            status = ASTRA_VFS_ERR_IO;
        if (status != ASTRA_VFS_OK) break;
        wrote = 1;
        *cursor = offset + ASTRA_MEMORY_PAGE_SIZE;
    }
    if (status != ASTRA_VFS_OK) *cursor = 0u;
    return status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_BUSY : status;
}
