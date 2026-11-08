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

static PageCall calls[9];
static AstraPagerPageReply replies[9];
static uint32_t call_count, next_call, send_count, close_count, read_count;
static uint32_t receive_error;
static uint32_t pin_count, release_count, pinned;
static int expect_unknown_identity;

static const AstraVfsFilePageSource *acquire(void *context, uint32_t cookie)
{
    const AstraVfsFilePageSource *sources = context;
    if (cookie == 44u || cookie == 45u) {
        assert(!pinned);
        pinned = cookie;
        ++pin_count;
        return &sources[cookie - 44u];
    }
    return NULL;
}

static void release(void *context, const AstraVfsFilePageSource *source)
{
    const AstraVfsFilePageSource *sources = context;
    assert(pinned != 0u && source == &sources[pinned - 44u]);
    pinned = 0u;
    ++release_count;
}

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
    if (calls[send_count].request.cookie == 44u ||
        calls[send_count].request.cookie == 45u)
        assert(pinned == calls[send_count].request.cookie);
    else assert(pinned == 0u);
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
    assert((identity == NULL) == expect_unknown_identity);
    assert(backend->context == &calls &&
           pinned == call->request.cookie &&
           node == (call->request.cookie == 44u ? 55u : 56u) &&
           (identity == NULL ||
            (identity->volume == 3u &&
             identity->inode == (call->request.cookie == 44u ? 5u : 6u) &&
             identity->generation == 7u)) &&
           offset == (call->request.cookie == 45u ?
                       UINT64_C(0x100000000) : 0u) +
                     call->request.offset);
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
    AstraVfsNodeMeta second_identity = identity;
    AstraVfsFilePageSource sources[2];
    AstraVfsFilePager pager;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];

    assert(!astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        NULL, 0u, 2u * ASTRA_MEMORY_PAGE_SIZE, 44u));
    AstraVfsNodeMeta unknown_identity = {0};
    assert(astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        &unknown_identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE + 1u, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        &identity, 1u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        &identity, UINT64_MAX - ASTRA_MEMORY_PAGE_SIZE + 1u,
        ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(astra_vfs_file_page_source_init(&sources[0], &backend, 55u,
        &identity, 0u, 2u * ASTRA_MEMORY_PAGE_SIZE, 44u));
    second_identity.inode = 6u;
    assert(astra_vfs_file_page_source_init(&sources[1], &backend, 56u,
        &second_identity, UINT64_C(0x100000000),
        ASTRA_MEMORY_PAGE_SIZE, 45u));
    assert(!astra_vfs_file_pager_init(&pager, 77u, 99u, page, 1u,
        acquire, release, sources));
    assert(!astra_vfs_file_pager_init(&pager, 77u, 99u, page,
        sizeof(page), acquire, NULL, sources));
    assert(astra_vfs_file_pager_init(&pager, 77u, 99u, page,
        sizeof(page), acquire, release, sources));

    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    add_call(ASTRA_MEMORY_PAGE_SIZE, ASTRA_VFS_ERR_BUSY, 0u, ASTRA_SYSCALL_OK);
    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    calls[2].request.cookie = 45u;
    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    calls[3].request.cookie = 46u;
    add_call(2u * ASTRA_MEMORY_PAGE_SIZE, ASTRA_VFS_OK, 5u,
             ASTRA_SYSCALL_OK);
    add_call(0u, ASTRA_VFS_OK, ASTRA_MEMORY_PAGE_SIZE + 1u,
             ASTRA_SYSCALL_PEER_DEAD);
    add_call(0u, ASTRA_VFS_ERR_LOST, 0u, ASTRA_SYSCALL_OK);
    assert(astra_vfs_file_pager_pump(&pager, 7u) == 7u);
    assert(next_call == 7u && send_count == 7u && close_count == 7u &&
           read_count == 5u && pin_count == 6u && release_count == 6u &&
           pinned == 0u && pager.stalled == 0u);
    assert(replies[0].status == ASTRA_STATUS_OK &&
           replies[0].cookie == 44u && replies[0].offset == 0u &&
           replies[0].data_area == 99u && replies[0].valid_bytes == 5u &&
           memcmp(page, "story", 5u) == 0);
    assert(replies[1].status == ASTRA_STATUS_BUSY &&
           replies[1].data_area == 0u && replies[1].valid_bytes == 0u);
    assert(replies[2].status == ASTRA_STATUS_OK &&
           replies[2].cookie == 45u && replies[2].data_area == 99u &&
           replies[2].valid_bytes == 5u &&
           replies[3].status == ASTRA_STATUS_LOST &&
           replies[4].status == ASTRA_STATUS_PROTOCOL &&
           replies[5].status == ASTRA_STATUS_IO &&
           replies[6].status == ASTRA_STATUS_LOST);
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 0u);
    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_IO_ERROR);
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 1u &&
           pager.stalled == ASTRA_SYSCALL_IO_ERROR &&
           pin_count == 7u && release_count == 7u && pinned == 0u &&
           close_count == 8u);
    pager.stalled = 0u;
    sources[0].identity = unknown_identity;
    expect_unknown_identity = 1;
    add_call(0u, ASTRA_VFS_OK, 5u, ASTRA_SYSCALL_OK);
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 1u &&
           replies[8].status == ASTRA_STATUS_OK &&
           read_count == 7u && close_count == 9u);
    receive_error = ASTRA_SYSCALL_IO_ERROR;
    assert(astra_vfs_file_pager_pump(&pager, 1u) == 0u &&
           pager.stalled == ASTRA_SYSCALL_IO_ERROR);
    puts("VFS file pager PASS");
    return 0;
}
