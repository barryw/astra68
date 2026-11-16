#include <astra/pager_service.h>
#include <astra/runtime.h>
#include <astra/vfs_file_pager.h>
#include <astra/vfs_service_core.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct PageCall {
    union {
        AstraPagerPageRequest request;
        AstraPagerSyncRequest sync;
    };
    uint32_t handle;
    uint32_t source_status;
    uint32_t source_valid;
    uint32_t send_status;
} PageCall;

static PageCall calls[9];
static AstraPagerPageReply replies[9];
static uint32_t call_count, next_call, send_count, close_count, read_count;
static uint32_t receive_error;
static uint32_t forward_count, forward_status, sync_count;
static uint32_t async_count, sync_snapshot_status;
static uint32_t refresh_count, refresh_status;
static uint32_t pin_count, release_count, pinned;
static int expect_unknown_identity;
static AstraVfsService service;

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
    assert((receive == 77u || receive == 78u) && capacity == sizeof(AstraPagerPageRequest) &&
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
    if (handle == 88u) {
        assert(size == sizeof(AstraPagerSyncRequest) && handle_count == 1u &&
            handles != NULL && handles[0] == calls[next_call - 1u].handle &&
            memcmp(message, &calls[next_call - 1u].sync, size) == 0 && pinned == 0u);
        ++forward_count;
        return forward_status;
    }
    int syncing = calls[send_count].request.header.operation == ASTRA_PAGER_OPERATION_SYNC;
    assert(handle == calls[send_count].handle &&
           size == (syncing ? sizeof(AstraPagerSyncReply) : sizeof(AstraPagerPageReply)) && handles == NULL &&
           handle_count == 0u);
    if ((!syncing || pin_count != 0u) && (calls[send_count].request.cookie == 44u ||
        calls[send_count].request.cookie == 45u))
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

uint32_t astra_vfs_service_sync_retained(AstraVfsService *owner,
    AstraVfsFile retained, uint32_t *remaining)
{
    assert(owner == &service && retained == 55u && remaining == NULL && pinned == 44u);
    ++sync_count;
    return calls[next_call - 1u].source_status;
}

uint32_t astra_vfs_service_stat_retained(AstraVfsService *owner,
    AstraVfsFile retained, AstraVfsNodeInfo *info)
{
    assert(owner == &service && retained == 55u && pinned == 44u);
    ++refresh_count;
    memset(info, 0, sizeof(*info));
    return refresh_status;
}

uint32_t astra_vfs_service_read_page_retained(AstraVfsService *owner,
    AstraVfsFile retained, uint32_t access, const AstraVfsNodeMeta *identity, uint64_t offset,
    void *page, uint32_t *valid_bytes)
{
    const PageCall *call = &calls[next_call - 1u];
    assert((identity == NULL) == expect_unknown_identity);
    assert(owner == &service && access ==
               (call->request.cookie == 45u ? ASTRA_VFS_OPEN_WRITE : ASTRA_VFS_OPEN_READ) &&
           pinned == call->request.cookie &&
           retained == (call->request.cookie == 44u ? 55u : 56u) &&
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

static void test_sync(AstraVfsFilePager *pager)
{
    for (uint32_t test = 0u; test < 20u; ++test) {
        call_count = next_call = send_count = close_count = read_count = 0u;
        pin_count = release_count = pinned = 0u;
        forward_count = sync_count = receive_error = 0u;
        async_count = sync_snapshot_status = 0u;
        refresh_count = refresh_status = 0u;
        forward_status = ASTRA_SYSCALL_OK;
        pager->stalled = 0u;
        pager->sync_send = 88u;
        add_call(0u, ASTRA_STATUS_NO_SPACE, 0u, ASTRA_SYSCALL_OK);
        astra_message_header_set(&calls[0].sync.header, sizeof(calls[0].sync),
            ASTRA_PAGER_PROTOCOL, ASTRA_PAGER_PROTOCOL_VERSION, ASTRA_PAGER_OPERATION_SYNC, 1u);
        calls[0].sync.byte_size = ASTRA_MEMORY_PAGE_SIZE;
        calls[0].sync.flags = test >= 12u ? ASTRA_AREA_SYNC_ASYNC : 0u;
        if (test == 1u) forward_status = ASTRA_SYSCALL_WOULD_BLOCK;
        if (test == 2u) forward_status = ASTRA_SYSCALL_PEER_DEAD;
        if (test == 3u) pager->sync_send = 0u;
        if (test == 4u || test == 15u) calls[0].sync.flags = ~ASTRA_AREA_SYNC_ASYNC;
        if (test == 5u) calls[0].sync.offset = 1u;
        if (test == 6u) calls[0].sync.byte_size = 0u;
        if (test == 8u) calls[0].sync.cookie = 46u;
        if (test == 9u) calls[0].sync.offset = 2u * ASTRA_MEMORY_PAGE_SIZE;
        if (test == 10u) calls[0].send_status = ASTRA_SYSCALL_PEER_DEAD;
        if (test == 11u) calls[0].source_status = ASTRA_STATUS_OK;
        if (test == 12u) calls[0].source_status = ASTRA_STATUS_OK;
        if (test == 14u) calls[0].source_status = ASTRA_STATUS_BUSY;
        if (test == 16u) sync_snapshot_status = ASTRA_STATUS_LOST;
        if (test == 18u) refresh_status = ASTRA_STATUS_BUSY;
        if (test == 19u) refresh_status = ASTRA_STATUS_IO;
        uint32_t stalled = UINT32_MAX;
        if (test < 7u || test == 17u) assert(astra_vfs_file_pager_pump(pager, 1u) == 1u);
        else {
            assert(astra_vfs_file_pager_sync_pump(pager, 78u, 1u, &stalled) == 1u);
            assert(stalled == 0u && pager->stalled == 0u);
        }
        assert(read_count == 0u && pinned == 0u && pin_count == release_count);
        if (test == 0u || test == 17u) {
            assert(forward_count == 1u && send_count == 0u && close_count == 0u && sync_count == 0u);
            continue;
        }
        AstraPagerSyncReply reply;
        memcpy(&reply, &replies[0], sizeof(reply));
        uint32_t expected = test == 1u ? ASTRA_STATUS_BUSY :
            test == 2u || test == 8u || test == 16u ? ASTRA_STATUS_LOST :
            test == 3u ? ASTRA_STATUS_UNSUPPORTED :
            (test >= 4u && test <= 6u) || test == 9u || test == 15u ? ASTRA_STATUS_PROTOCOL :
            test == 11u || test == 12u || test == 14u ? ASTRA_STATUS_OK :
            test == 19u ? ASTRA_STATUS_IO : ASTRA_STATUS_NO_SPACE;
        assert(reply.header.operation == ASTRA_PAGER_OPERATION_SYNC_REPLY &&
            reply.cookie == calls[0].sync.cookie && reply.offset == calls[0].sync.offset &&
            reply.byte_size == calls[0].sync.byte_size && reply.status == expected);
        assert(send_count == 1u && close_count == 1u &&
            sync_count == (test == 7u || test == 10u || test == 11u));
        assert(async_count == ((test >= 12u && test <= 14u) || test == 16u || test == 18u));
        assert(refresh_count == ((test >= 12u && test <= 14u) || test == 16u || test >= 18u));
    }
}

static struct {
    AstraVfsFilePageSource source;
    AstraVfsFileCache cache;
    AstraVfsFileIO io;
    AstraVfsOpenFile active;
    uint32_t states[4], writes[4];
    uint32_t scans, begins, completes, successes, window;
    uint32_t snapshot_status, scan_status, begin_status, write_status, complete_status;
    int short_write, redirty, bad_scan;
} wb;

uint32_t astra_vfs_service_cache_snapshot(AstraVfsService *owner,
    AstraVfsFile retained, AstraVfsFileCache *snapshot, uint32_t *sequence)
{
    if (retained == 55u) {
        assert(owner == &service && pinned == 44u);
        ++async_count;
        *snapshot = (AstraVfsFileCache){.error = calls[next_call - 1u].source_status};
        *sequence = 1u;
        return sync_snapshot_status;
    }
    assert(owner == &service && retained == 57u &&
        astra_vfs_open_file_active(&wb.active));
    if (wb.snapshot_status != ASTRA_VFS_OK) return wb.snapshot_status;
    *snapshot = wb.cache;
    *sequence = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_rt_area_dirty_scan(uint32_t area, uint32_t start,
    uint32_t *offset, uint32_t *state)
{
    assert(area == 88u && start <= wb.source.span &&
        start % ASTRA_MEMORY_PAGE_SIZE == 0u && astra_vfs_open_file_active(&wb.active));
    ++wb.scans;
    if (wb.scan_status != ASTRA_SYSCALL_OK) return wb.scan_status;
    if (wb.bad_scan) { *offset = start + 1u; *state = 0u; return ASTRA_SYSCALL_OK; }
    uint32_t end = start + wb.window * ASTRA_MEMORY_PAGE_SIZE;
    if (end > wb.source.span) end = wb.source.span;
    for (uint32_t at = start; at < end; at += ASTRA_MEMORY_PAGE_SIZE)
        if (wb.states[at / ASTRA_MEMORY_PAGE_SIZE] != 0u) {
            *offset = at;
            *state = wb.states[at / ASTRA_MEMORY_PAGE_SIZE];
            return ASTRA_SYSCALL_OK;
        }
    *offset = end; *state = 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_writeback_begin(uint32_t area, uint32_t offset,
    void *page, uint32_t *token)
{
    uint32_t index = offset / ASTRA_MEMORY_PAGE_SIZE;
    assert(area == 88u && index < 4u && wb.states[index] == ASTRA_AREA_DIRTY_MODIFIED &&
        astra_vfs_open_file_active(&wb.active));
    ++wb.begins;
    if (wb.begin_status != ASTRA_SYSCALL_OK) return wb.begin_status;
    wb.states[index] = ASTRA_AREA_DIRTY_WRITEBACK;
    memset(page, (int)index + 1, ASTRA_MEMORY_PAGE_SIZE);
    *token = 100u + index;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_vfs_service_write_page_retained(AstraVfsService *owner,
    AstraVfsFile retained, const AstraVfsOpenFile *active,
    const AstraVfsNodeInfo *expected, uint64_t offset, uint64_t size,
    const void *page, uint32_t *moved, AstraVfsNodeInfo *current)
{
    uint32_t index = (uint32_t)((offset - wb.source.base) / ASTRA_MEMORY_PAGE_SIZE);
    assert(owner == &service && retained == 57u && active == &wb.active &&
        astra_vfs_open_file_active(active) && index < 4u &&
        offset == wb.source.base + index * ASTRA_MEMORY_PAGE_SIZE && size == wb.cache.size &&
        astra_vfs_node_revision_equal(expected, &wb.cache.backing));
    for (uint32_t at = 0u; at < ASTRA_MEMORY_PAGE_SIZE; ++at)
        assert(((const uint8_t *)page)[at] == index + 1u);
    ++wb.writes[index];
    *moved = size - offset < ASTRA_MEMORY_PAGE_SIZE ?
        (uint32_t)(size - offset) : ASTRA_MEMORY_PAGE_SIZE;
    if (wb.short_write) --*moved;
    if (wb.redirty) wb.states[index] |= ASTRA_AREA_DIRTY_MODIFIED;
    if (offset + *moved > wb.cache.backing.size) wb.cache.backing.size = offset + *moved;
    *current = wb.cache.backing;
    return wb.write_status;
}

uint32_t astra_rt_area_writeback_complete(uint32_t area, uint32_t offset,
    uint32_t token, bool success)
{
    uint32_t index = offset / ASTRA_MEMORY_PAGE_SIZE;
    assert(area == 88u && index < 4u && token == 100u + index &&
        (wb.states[index] & ASTRA_AREA_DIRTY_WRITEBACK) != 0u &&
        astra_vfs_open_file_active(&wb.active));
    ++wb.completes;
    if (wb.complete_status != ASTRA_SYSCALL_OK) return wb.complete_status;
    wb.states[index] &= ASTRA_AREA_DIRTY_MODIFIED;
    if (success) ++wb.successes;
    else wb.states[index] |= ASTRA_AREA_DIRTY_MODIFIED;
    return ASTRA_SYSCALL_OK;
}

static void writeback_reset(void)
{
    memset(&wb, 0, sizeof(wb));
    AstraVfsNodeMeta identity = {.valid = ASTRA_VFS_META_VOLUME |
        ASTRA_VFS_META_INODE | ASTRA_VFS_META_GENERATION,
        .volume = 3u, .inode = 5u, .generation = 7u};
    assert(astra_vfs_file_page_source_init(&wb.source, &service, 55u,
        ASTRA_VFS_OPEN_READ, &identity, UINT64_C(0x100000000),
        4u * ASTRA_MEMORY_PAGE_SIZE, 44u));
    wb.cache = (AstraVfsFileCache){.writer = 57u,
        .size = wb.source.base + 2u * ASTRA_MEMORY_PAGE_SIZE + 13u,
        .backing = {.kind = ASTRA_VFS_KIND_FILE,
            .size = wb.source.base + 23u, .meta = identity}};
    wb.io = (AstraVfsFileIO){.active = &wb.active, .cache = &wb.cache};
    wb.active.io = &wb.io;
    wb.window = 4u;
}

static void test_writeback(void)
{
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];
    uint32_t cursor = 0u, remaining = 1u;
    writeback_reset();
    wb.states[0] = wb.states[2] = ASTRA_AREA_DIRTY_MODIFIED;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_BUSY);
    assert(remaining == 0u && wb.scans == 1u && wb.writes[0] == 1u && wb.writes[2] == 0u &&
        cursor == ASTRA_MEMORY_PAGE_SIZE && wb.states[0] == 0u);
    remaining = UINT32_MAX;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_OK);
    assert(cursor == wb.source.span && wb.writes[2] == 1u && wb.completes == 2u &&
        wb.successes == 2u && wb.cache.backing.size == wb.cache.size);

    /* A continued scan cannot certify an earlier leaf stayed clean. */
    writeback_reset(); wb.window = 2u;
    cursor = 2u * ASTRA_MEMORY_PAGE_SIZE; remaining = 1u;
    wb.states[0] = ASTRA_AREA_DIRTY_MODIFIED;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_BUSY && cursor == 0u && wb.begins == 0u);
    remaining = UINT32_MAX;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_OK && wb.writes[0] == 1u);

    writeback_reset(); wb.window = 2u; cursor = 0u; remaining = 1u;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_BUSY && cursor == 2u * ASTRA_MEMORY_PAGE_SIZE);
    remaining = UINT32_MAX;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_OK && cursor == wb.source.span);

    writeback_reset(); cursor = 0u; remaining = UINT32_MAX;
    wb.redirty = 1; wb.states[0] = ASTRA_AREA_DIRTY_MODIFIED;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_BUSY && cursor == 0u &&
        wb.states[0] == ASTRA_AREA_DIRTY_MODIFIED && wb.writes[0] == 1u);
    wb.redirty = 0;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_OK && wb.writes[0] == 2u);

    for (uint32_t fault = 0u; fault < 9u; ++fault) {
        writeback_reset(); cursor = 0u; remaining = 8u;
        wb.states[0] = ASTRA_AREA_DIRTY_MODIFIED;
        switch (fault) {
        case 0u: wb.scan_status = ASTRA_SYSCALL_IO_ERROR; break;
        case 1u: wb.snapshot_status = ASTRA_VFS_ERR_LOST; break;
        case 2u: wb.begin_status = ASTRA_SYSCALL_WOULD_BLOCK; break;
        case 3u: wb.write_status = ASTRA_VFS_ERR_IO; break;
        case 4u: wb.short_write = 1; break;
        case 5u: wb.complete_status = ASTRA_SYSCALL_IO_ERROR; break;
        case 6u: wb.states[0] = ASTRA_AREA_DIRTY_WRITEBACK; break;
        case 7u: wb.bad_scan = 1; break;
        case 8u: wb.cache.size = wb.source.base; break;
        }
        uint32_t expected = fault == 1u ? ASTRA_VFS_ERR_LOST :
            fault == 2u || fault == 6u || fault == 8u ? ASTRA_VFS_ERR_BUSY : ASTRA_VFS_ERR_IO;
        assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
            page, &remaining, &cursor) == expected && cursor == 0u &&
            wb.states[0] != 0u && wb.successes == 0u);
        assert(wb.completes == (fault >= 3u && fault <= 5u ? 1u : 0u));
    }

    writeback_reset(); cursor = 0u; remaining = 0u;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_BUSY && wb.scans == 0u);
    wb.io.active = NULL;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_INVALID);
    wb.io.active = &wb.active; cursor = 1u;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_INVALID);
    cursor = 0u;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 58u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_INVALID);
    wb.source.identity.inode++;
    assert(astra_vfs_file_pager_writeback(&wb.source, &wb.active, 57u, 88u,
        page, &remaining, &cursor) == ASTRA_VFS_ERR_INVALID);
}

int main(void)
{
    const AstraVfsNodeMeta identity = {
        .valid = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                 ASTRA_VFS_META_GENERATION,
        .volume = 3u, .inode = 5u, .generation = 7u,
    };
    AstraVfsNodeMeta second_identity = identity;
    AstraVfsFilePageSource sources[2];
    AstraVfsFilePager pager;
    uint8_t page[ASTRA_MEMORY_PAGE_SIZE];

    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u, 0u,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], NULL, 55u, ASTRA_VFS_OPEN_READ,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, ASTRA_VFS_FILE_INVALID, ASTRA_VFS_OPEN_READ,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        NULL, 0u, 2u * ASTRA_MEMORY_PAGE_SIZE, 44u));
    AstraVfsNodeMeta unknown_identity = {0};
    assert(astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        &unknown_identity, 0u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        &identity, 0u, ASTRA_MEMORY_PAGE_SIZE + 1u, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        &identity, 1u, ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(!astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        &identity, UINT64_MAX - ASTRA_MEMORY_PAGE_SIZE + 1u,
        ASTRA_MEMORY_PAGE_SIZE, 44u));
    assert(astra_vfs_file_page_source_init(&sources[0], &service, 55u, ASTRA_VFS_OPEN_READ,
        &identity, 0u, 2u * ASTRA_MEMORY_PAGE_SIZE, 44u));
    second_identity.inode = 6u;
    assert(astra_vfs_file_page_source_init(&sources[1], &service, 56u, ASTRA_VFS_OPEN_WRITE,
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
    test_sync(&pager);
    test_writeback();
    puts("VFS file pager PASS");
    return 0;
}
