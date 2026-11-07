#include <astra/pager_service.h>
#include <astra/runtime.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_service_core.h>

#include <string.h>

int
astra_vfs_file_page_source_init(AstraVfsFilePageSource *source,
    const AstraVfsBackend *backend, uintptr_t node,
    const AstraVfsNodeMeta *identity, uint32_t span, uint32_t cookie)
{
    if (source == NULL || backend == NULL || backend->ops == NULL ||
        backend->ops->read == NULL || backend->ops->stat_node == NULL ||
        node == 0u || !astra_vfs_node_identity_equal(identity, identity) ||
        span == 0u || (span & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        cookie == 0u)
        return 0;
    *source = (AstraVfsFilePageSource){
        .backend = *backend, .identity = *identity, .node = node,
        .span = span, .cookie = cookie,
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
        astra_message_header_set(&reply.header, sizeof(reply),
            ASTRA_PAGER_PROTOCOL, ASTRA_PAGER_PROTOCOL_VERSION,
            ASTRA_PAGER_OPERATION_PAGE_REPLY,
            request.header.transaction_id);
        reply.cookie = request.cookie;
        reply.offset = request.offset;
        reply.status = ASTRA_STATUS_PROTOCOL;
        if (size == sizeof(request) &&
            request.header.total_size == sizeof(request) &&
            request.header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
            request.header.flags == 0u && request.header.reserved == 0u &&
            request.header.protocol == ASTRA_PAGER_PROTOCOL &&
            request.header.protocol_version == ASTRA_PAGER_PROTOCOL_VERSION &&
            request.header.operation == ASTRA_PAGER_OPERATION_PAGE &&
            request.header.transaction_id != 0u &&
            request.cookie != 0u &&
            request.reserved[0] == 0u && request.reserved[1] == 0u &&
            (request.offset & (ASTRA_MEMORY_PAGE_SIZE - 1u)) == 0u) {
            source = pager->acquire(pager->context, request.cookie);
            reply.status = source == NULL ? ASTRA_STATUS_LOST :
                request.offset >= source->span ? ASTRA_STATUS_PROTOCOL :
                astra_vfs_backend_read_page(&source->backend, source->node,
                    &source->identity, request.offset, pager->page, &valid);
            if (reply.status == ASTRA_STATUS_OK) {
                if (valid == 0u || valid > ASTRA_MEMORY_PAGE_SIZE)
                    reply.status = ASTRA_STATUS_IO;
                else {
                    reply.data_area = pager->data_area;
                    reply.valid_bytes = valid;
                }
            }
        }
        status = astra_port_send(handle, &reply, sizeof(reply), NULL, 0u);
        if (status != ASTRA_SYSCALL_OK &&
            status != ASTRA_SYSCALL_PEER_DEAD)
            pager->stalled = status;
        if (source != NULL) pager->release(pager->context, source);
        (void)astra_close(handle);
        if (pager->stalled != 0u)
            break;
    }
    return consumed;
}
