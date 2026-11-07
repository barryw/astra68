#include <astra/pager_service.h>
#include <astra/runtime.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_service_core.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct PageCall {
    AstraPagerPageRequest request;
    uint32_t handle;
    uint32_t source_status;
    uint32_t source_valid;
    uint32_t send_status;
} PageCall;

static PageCall calls[7];
static AstraPagerPageReply replies[7];
static uint32_t call_count, next_call, send_count, close_count, read_count;
static uint32_t receive_error;

uint32_t astra_port_receive(uint32_t receive, void *message, uint32_t capacity,
    uint32_t *handles, uint32_t handle_capacity, uint32_t *size,
    uint32_t *handle_count)
{
    assert(receive == 77u && capacity == sizeof(AstraPagerPageRequest) &&
           handle_capacity == 1u);
    if (receive_error != 0u) return receive_error;
    if (next_call == call_count) return ASTRA_SYSCALL_WOULD_BLOCK;
    PageCall *call = &calls[next_call++];
    memcpy(message, &call->request, sizeof(call->request));
    *size = sizeof(call->request);
    *handle_count = call->handle != 0u ? 1u : 0u;
    if (call->handle != 0u) handles[0] = call->handle;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
    const uint32_t *handles, uint32_t handle_count)
{
    assert(handle == calls[send_count].handle &&
           size == sizeof(AstraPagerPageReply) && handles == NULL &&
           handle_count == 0u);
    memcpy(&replies[send_count], message, size);
    return calls[send_count++].send_status;
}

uint32_t astra_close(uint32_t handle)
{
    assert(handle != 0u);
    ++close_count;
    return ASTRA_SYSCALL_OK;
}

static uint32_t unused_read(void *context, uintptr_t node, uint64_t offset,
    void *buffer, uint32_t length, uint32_t *moved)
{
    (void)context; (void)node; (void)offset; (void)buffer;
    (void)length; (void)moved;
    assert(0);
    return ASTRA_VFS_ERR_IO;
}

static uint32_t unused_stat(void *context, uintptr_t node, AstraVfsNodeInfo *info)
{
    (void)context; (void)node; (void)info;
    assert(0);
    return ASTRA_VFS_ERR_IO;
}

uint32_t astra_vfs_backend_read_page(const AstraVfsBackend *backend,
    uintptr_t node, const AstraVfsNodeMeta *identity, uint64_t offset,
    void *page, uint32_t *valid_bytes)
{
    const PageCall *call = &calls[next_call - 1u];
    assert(backend->context == &calls && node == 55u &&
           identity->volume == 3u && identity->inode == 5u &&
           identity->generation == 7u &&
           offset == call->request.offset);
    ++read_count;
    *valid_bytes = call->source_valid;
    memcpy(page, "story", 5u);
    return call->source_status;
}

static void add_call(uint32_t offset, uint32_t source_status,
    uint32_t source_valid, uint32_t send_status)
{
    PageCall *call = &calls[call_count];
    astra_message_header_set(&call->request.header,
        sizeof(call->request), ASTRA_PAGER_PROTOCOL,
        ASTRA_PAGER_PROTOCOL_VERSION, ASTRA_PAGER_OPERATION_PAGE,
        call_count + 1u);
    call->request.cookie = 44u;
    call->request.offset = offset;
    call->handle = 100u + call_count;
    call->source_status = source_status;
    call->source_valid = source_valid;
    call->send_status = send_status;
    ++call_count;
}

int main(void)
{
    const AstraVfsBackendOps ops = {.read = unused_read,
                                     .stat_node = unused_stat};
    const AstraVfsBackend backend = {&ops, &calls};
    const AstraVfsNodeMeta identity = {
        .valid = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                 ASTRA_VFS_META_GENERATION,
        .volume = 3u, .inode = 5u, .generation = 7u,
    };
    AstraVfsFilePager pager;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];

    assert(!astra_vfs_file_pager_init(&pager, &backend, 55u, NULL,
        2u * ASTRA_MEMORY_PAGE_SIZE, 44u, 77u, 99u, page, sizeof(page)));
    assert(!astra_vfs_file_pager_init(&pager, &backend, 55u, &identity,
        ASTRA_MEMORY_PAGE_SIZE + 1u, 44u, 77u, 99u, page, sizeof(page)));
    assert(astra_vfs_file_pager_init(&pager, &backend, 55u, &identity,
        2u * ASTRA_MEMORY_PAGE_SIZE, 44u, 77u, 99u, page, sizeof(page)));

    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    add_call(ASTRA_MEMORY_PAGE_SIZE, ASTRA_VFS_ERR_BUSY, 0u, ASTRA_SYSCALL_OK);
    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    calls[2].request.cookie = 45u;
    add_call(0u, ASTRA_VFS_ERR_LOST, 0u, ASTRA_SYSCALL_OK);
    add_call(2u * ASTRA_MEMORY_PAGE_SIZE, ASTRA_VFS_OK, 5u,
             ASTRA_SYSCALL_OK);
    add_call(0u, ASTRA_VFS_OK, ASTRA_MEMORY_PAGE_SIZE + 1u,
             ASTRA_SYSCALL_PEER_DEAD);
    assert(astra_vfs_file_pager_pump(&pager, 6u) == 6u);
    assert(next_call == 6u && send_count == 6u && close_count == 6u &&
           read_count == 4u && pager.stalled == 0u);
    assert(replies[0].status == ASTRA_STATUS_OK &&
           replies[0].cookie == 44u && replies[0].offset == 0u &&
           replies[0].data_area == 99u && replies[0].valid_bytes == 5u &&
           memcmp(page, "story", 5u) == 0);
    assert(replies[1].status == ASTRA_STATUS_BUSY &&
           replies[1].data_area == 0u && replies[1].valid_bytes == 0u);
    assert(replies[2].status == ASTRA_STATUS_PROTOCOL &&
           replies[3].status == ASTRA_STATUS_LOST &&
           replies[4].status == ASTRA_STATUS_PROTOCOL &&
           replies[5].status == ASTRA_STATUS_IO);
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 0u);
    receive_error = ASTRA_SYSCALL_IO_ERROR;
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 0u &&
           pager.stalled == ASTRA_SYSCALL_IO_ERROR);
    puts("VFS file pager PASS");
    return 0;
}
