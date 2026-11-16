#define main hostfs_unused_main
#define astra_vfs_file_pager_pump hostfs_test_pager_pump
#define astra_vfs_file_pager_sync_pump hostfs_test_sync_pump
#include "../main.c"
#undef astra_vfs_file_pager_sync_pump
#undef astra_vfs_file_pager_pump
#undef main
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static char document[512];
static uint32_t read_status;
static int allocation_failed;
static uint32_t session_close_failure, handle_close_failure, released_grants;
static uint64_t usage_sequence = 91u;
static uint32_t usage_notification_failure, usage_notifications;
static int usage_file_mode;
static int usage_locked;
static uint64_t mapping_sequences[5];
static uint32_t mapping_processes[5][4], mapping_process_counts[5];
static uint32_t mapping_query_failure;
static int mutate_usage_mapping;
static uint32_t area_handles = 2u, area_mappings = 1u, area_children, next_area = 40u;
static uint32_t open_file_size = ASTRA_MEMORY_PAGE_SIZE * 2u;
static uint32_t open_file_identity_valid = 1u;
static uint32_t open_file_generation = 11u;
static uint32_t open_file_links = 1u;
static uint32_t file_closes, area_reclaims, area_binds, area_duplicates;
static uint32_t area_reference_queries;
static uint32_t area_extents[5], area_resizes, resize_result;
static void (*area_bind_observer)(void);
static uint32_t area_limits, area_close_status, file_close_status;
static uint32_t duplicate_limits, duplicate_status, duplicate_attempts;
static uint32_t expected_area_rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;
static uint32_t prepare_failure;
static uint32_t retain_status, release_wait_status, retained_releases;
static struct {
    AstraVfsService *service;
    AstraVfsFile file;
    uint32_t session;
} native_references[32];
static uint32_t session_open_limits, session_opens;
static uint32_t port_creates, port_pressure_status;
static uint32_t writes, truncates;
static uint32_t write_result, reclaim_result;
static uint32_t page_read_result, page_reads, page_publish_result;
static uint64_t file_position;
static uint8_t backing_bytes[3u * ASTRA_MEMORY_PAGE_SIZE];
static struct {
    uint32_t area, offset, dirty, token;
    uint8_t bytes[ASTRA_MEMORY_PAGE_SIZE];
} vm_pages[16];
static uint32_t open_file_flags = ASTRA_VFS_OPEN_READ;
static uint64_t node_sequence;
static uint64_t changed_inode = 7u;
static int change_gap;
static uint32_t changed_kind = ASTRA_HOST_FS_CHANGE_NODE;
static uint32_t snapshot_calls;
static uint32_t image_calls, image_status;
static uint32_t snapshot_failure;
static uint32_t change_next_failure;
static AstraResult filesystem_publish_result;
static uint32_t filesystem_publications;
static uint32_t shutdown_claims, shutdown_releases, transport_closes, pager_stops;
static uint32_t shutdown_release_status, shutdown_reply_status;
static uint32_t shutdown_decision, shutdown_reason, shutdown_closes;
static uint32_t shutdown_close_results[2], shutdown_close_result_count;
static int shutdown_fixture;
static HostfsFileArea *pump_page;
static int pager_pressure_fixture;
static uint32_t pager_pumps, pager_waits, pager_wakes;
static uint32_t sync_pumps, sync_pump_error;
uint32_t astra_log(const char *text) { (void)text; return ASTRA_SYSCALL_OK; }
uint64_t astra_clock_monotonic(void) { return 1u; }
uint32_t astra_clock_realtime(uint64_t *value)
{ *value = UINT64_C(1234567890); return ASTRA_SYSCALL_OK; }
void astra_volume_port_destroy(AstraVolumePort *port)
{ assert(port == &volume_port); memset(port, 0, sizeof(*port)); }
uint32_t astra_vfs_port_service_worker_pump(AstraVfsPortService *host,
    AstraVfsPortWorker *active, uint32_t budget)
{
    assert(host == &metrics_port && active == &metrics_worker && budget == 1u);
    if (pump_page != NULL) { file_areas = pump_page; pump_page = NULL; }
    return 0u;
}
uint32_t astra_vfs_port_service_grant_pump(AstraVfsPortService *host,
    AstraVfsPortWorker *active, const AstraVfsPortGrant *grant, uint32_t budget)
{ assert(host != NULL && active == &worker && grant != NULL && budget == 1u); return 0u; }
void astra_vfs_service_dispatch_grant(AstraVfsService *service, uint32_t owner,
    uint32_t grant, uint32_t features, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer, AstraVfsReply *reply)
{
    (void)service; (void)owner; (void)grant; (void)features; (void)operation;
    (void)request; (void)transfer; (void)reply;
    assert(0 && "shutdown fixture has no native sessions");
}
uint32_t astra_shutdown_respond(uint32_t reply, uint32_t transaction,
    uint32_t decision, uint32_t reason)
{
    assert(reply == 89u && transaction == 17u);
    shutdown_decision = decision; shutdown_reason = reason;
    return shutdown_reply_status;
}
uint32_t astra_vfs_host_claim(AstraVfsHostTransport *host, uint32_t generation,
    int release, uint32_t *epoch)
{
    assert(host == &transport && generation == 9u);
    if (release) ++shutdown_releases; else ++shutdown_claims;
    if (epoch != NULL) *epoch = 55u;
    return release ? shutdown_release_status : ASTRA_VFS_OK;
}
void astra_vfs_host_transport_destroy(AstraVfsHostTransport *host)
{ assert(host == &transport); ++transport_closes; }
uint32_t astra_rt_cancel_wait(uint32_t thread)
{ assert(thread == 88u); ++pager_stops; return ASTRA_SYSCALL_OK; }
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    if (pager_pressure_fixture) {
        assert(handle == 77u && deadline == UINT64_C(1000000001) && detail == NULL);
        return ASTRA_SYSCALL_OK;
    }
    assert(handle == 88u && deadline == ASTRA_DEADLINE_FOREVER && detail == NULL);
    return ASTRA_SYSCALL_OK;
}
uint32_t hostfs_test_pager_pump(AstraVfsFilePager *pager, uint32_t budget)
{
    assert(pager_pressure_fixture && pager == &file_pager && budget == 8u);
    if (++pager_pumps == 1u) pager->stalled = ASTRA_SYSCALL_RESOURCE_LIMIT;
    else { assert(pager->stalled == 0u); file_pager_stop = 1u; }
    return 0u;
}
uint32_t hostfs_test_sync_pump(const AstraVfsFilePager *pager,
    uint32_t receive, uint32_t budget, uint32_t *stalled)
{
    assert(pager == &file_pager && receive == 78u && budget == 8u);
    ++sync_pumps;
    *stalled = sync_pump_error;
    return 0u;
}
uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected, uint64_t deadline)
{
    assert(pager_pressure_fixture && address == &file_pager_failed &&
        expected == ASTRA_SYSCALL_RESOURCE_LIMIT && deadline == ASTRA_DEADLINE_FOREVER);
    ++pager_waits;
    __atomic_store_n(&file_pager_failed, 0u, __ATOMIC_RELEASE);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t maximum, uint32_t *woken)
{
    assert(address == &file_pager_failed && *address == 0u && maximum == 1u && woken == NULL);
    assert(file_areas == NULL);
    ++pager_wakes;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_unmap(void *address)
{ assert(address != NULL); return ASTRA_SYSCALL_OK; }
uint32_t astra_query_abi(uint32_t *abi, uint32_t *process, uint32_t *thread)
{ assert(abi == NULL && process != NULL && thread == NULL); *process = 0x5001u; return ASTRA_SYSCALL_OK; }
uint32_t astra_process_info(uint32_t process, AstraProcessInfo *info)
{ assert(process == 0x5001u && info->size == sizeof(*info)); info->id = 5u; return ASTRA_SYSCALL_OK; }
uint32_t astra_vfs_host_volume_control(AstraVfsHostTransport *host, uint32_t generation,
    uint32_t requester, const AstraVolumeControl *control, AstraVolumeReference *reference)
{
    assert(host == &transport && generation == 9u && requester == 5u &&
        astra_volume_control_valid(control) && control->operation == ASTRA_VOLUME_IMAGE &&
        control->image_share == 123u);
    ++image_calls;
    *reference = (AstraVolumeReference){55u, 88u, 1u};
    return image_status;
}
uint32_t astra_rt_thread_sleep(uint64_t deadline, uint32_t flags,
    uint32_t reserved, uint32_t *remaining)
{ (void)deadline; (void)flags; (void)reserved; (void)remaining;
  assert(0 && "unexpected reclaim wait"); return ASTRA_SYSCALL_IO_ERROR; }
uint32_t astra_vfs_host_change_snapshot(AstraVfsHostBackend *backend,
    uint64_t *nodes, uint64_t *entries)
{
    assert(backend != NULL && nodes != NULL && entries != NULL);
    if (snapshot_failure != 0u) return snapshot_failure;
    *nodes = node_sequence; *entries = 0u; ++snapshot_calls;
    return ASTRA_VFS_OK;
}
AstraResult astra_messaging_publish_reliable(AstraHandle broker, const char *topic,
    const void *payload, uint32_t size, uint32_t transaction,
    AstraMonotonicDeadline deadline, uint32_t *queued)
{
    const AstraFilesystemChange *change = payload;
    assert(broker == 81u && strcmp(topic, ASTRA_FILESYSTEM_CHANGE_TOPIC) == 0 &&
        size == sizeof(*change) && transaction != 0u && deadline > 1u && queued != NULL);
    ++filesystem_publications;
    *queued = filesystem_publish_result == ASTRA_OK ? 1u : 0u;
    return filesystem_publish_result;
}
uint32_t astra_vfs_host_change_next(AstraVfsHostBackend *backend,
    uint64_t *cursor, AstraHostFsChangeRecord *record, uint32_t *kind)
{
    assert(backend != NULL && cursor != NULL && record != NULL && kind != NULL);
    if (change_next_failure != 0u) return change_next_failure;
    if (*cursor < node_sequence) {
        ++*cursor;
        *kind = change_gap ? ASTRA_HOST_FS_CHANGE_GAP : changed_kind;
        if (change_gap) *cursor = node_sequence;
        *record = (AstraHostFsChangeRecord){.sequence = *cursor,
            .nodes = *cursor, .inode = changed_inode, .kind = *kind};
    } else {
        *kind = ASTRA_HOST_FS_CHANGE_NONE;
    }
    return ASTRA_VFS_OK;
}
int astra_vfs_state_lock_acquire(void *context)
{ assert(context == &state_lock && !usage_locked); usage_locked = 1; return 1; }
void astra_vfs_state_lock_release(void *context)
{ assert(context == &state_lock && usage_locked); usage_locked = 0; }
uint32_t astra_vfs_host_usage_holders_changed(AstraVfsHostTransport *host, uint32_t generation)
{
    assert(host == &transport && generation == 9u && usage_locked);
    if (usage_notification_failure != 0u) return usage_notification_failure;
    ++usage_sequence; ++usage_notifications;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_host_usage(AstraVfsHostTransport *host, uint32_t generation,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page)
{
    assert(host == &transport && generation == 9u && usage_locked && astra_volume_usage_query_valid(query));
    if (mutate_usage_mapping) {
        mutate_usage_mapping = 0;
        ++mapping_sequences[0];
        mapping_processes[0][1] = 13u;
    }
    if (query->sequence != 0u && query->sequence != usage_sequence) return ASTRA_VFS_ERR_BUSY;
    memset(page, 0, sizeof(*page));
    page->version = ASTRA_VOLUME_USAGE_VERSION; page->size = sizeof(*page);
    page->volume = (AstraVolumeReference){55u, query->volume.id, query->volume.generation};
    page->sequence = usage_sequence; page->catalog_sequence = 31u;
    const uint32_t kinds[] = {usage_file_mode ? ASTRA_VOLUME_USAGE_FILE : ASTRA_VOLUME_USAGE_ROOT,
        ASTRA_VOLUME_USAGE_SHARE};
    const uint64_t handles[] = {33u, 77u};
    for (uint32_t at = 0u; at < 2u; ++at) {
        if (astra_volume_usage_key_compare(kinds[at], handles[at], 1u,
                query->after_kind, query->after, query->after_process) <= 0) continue;
        page->records[page->count++] = (AstraVolumeUsageRecord){.volume = {55u, 5u, 1u},
            .handle = handles[at], .inode = 101u, .process = 1u, .kind = kinds[at],
            .rights = ASTRA_RIGHT_READ, .path_length = 9u, .path = "/ram0/dir"};
    }
    return ASTRA_VFS_OK;
}
uint32_t astra_host_lease_query(uint32_t device, AstraHostLeaseInfo *info)
{ assert(device == 30u); *info = (AstraHostLeaseInfo){0}; return ASTRA_SYSCALL_OK; }
uint32_t astra_vfs_host_status_from_syscall(uint32_t status)
{ return status == ASTRA_SYSCALL_WOULD_BLOCK ? ASTRA_VFS_ERR_BUSY :
    status == ASTRA_SYSCALL_RESOURCE_LIMIT || status == ASTRA_SYSCALL_OUT_OF_MEMORY ?
    ASTRA_VFS_ERR_LIMIT : ASTRA_VFS_ERR_IO; }
uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
    uint32_t *receive, uint32_t *send)
{
    assert(messages == queue_messages && bytes == messages * sizeof(AstraVfsRequestMessage));
    ++port_creates; *receive = *send = 0u;
    if (port_pressure_status != 0u && file_areas != NULL) return port_pressure_status;
    *receive = 32u; *send = 35u; return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle)
{ assert(handle == 31u || handle == 32u || handle == 35u || handle == 88u || (handle >= 40u && handle <= 48u &&
         (handle & 1u) == 0u));
  return handle >= 40u ? area_close_status : handle_close_failure; }
uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *host, uint32_t generation, uint32_t session)
{
    assert(host == &transport && generation == 9u && session == 33u);
    if (shutdown_fixture) {
        uint32_t at = shutdown_closes++;
        if (at < shutdown_close_result_count) return shutdown_close_results[at];
    }
    return session_close_failure;
}
uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *host,
    uint32_t generation, uint32_t parent, const AstraVolumeReference *volume,
    const char *root, uint16_t flags, uint32_t requester, uint32_t *session)
{
    assert(host == &transport && generation == 9u && parent == 0u &&
        (volume == NULL || shutdown_fixture) && strcmp(root, "/") == 0 && flags == 0u && requester == 0u);
    ++session_opens;
    *session = 0u;
    if (session_open_limits != 0u) { --session_open_limits; return ASTRA_VFS_ERR_LIMIT; }
    *session = 33u;
    return ASTRA_VFS_OK;
}
void astra_vfs_port_service_release_grant(AstraVfsPortService *port, uint32_t id)
{ assert(port != NULL && id == 34u); ++released_grants; }
uint32_t astra_log_failure(const char *name, uint32_t value)
{ assert(name != NULL && value != 0u); return ASTRA_SYSCALL_OK; }
void *astra_runtime_allocate(size_t size) { return allocation_failed ? NULL : malloc(size); }
void *astra_runtime_callocate(size_t count, size_t size)
{ return allocation_failed ? NULL : calloc(count, size); }
void *astra_runtime_reallocate(void *pointer, size_t size)
{ return allocation_failed ? NULL : realloc(pointer, size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
int astra_vfs_host_init(AstraVfsHostBackend *backend, AstraVfsHostTransport *host,
    uint32_t generation, uint32_t session)
{ assert(host == &transport && generation == 9u && session == 33u);
  *backend = (AstraVfsHostBackend){.transport = host, .generation = generation,
                                   .session = session}; return 1; }
uint32_t astra_rt_area_create_flagged(uint32_t size, uint32_t rights,
    uint32_t flags, uint32_t *area)
{ assert(size == ASTRA_AREA_SIZE_MAX &&
         rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER |
                    ASTRA_RIGHT_ADMINISTER) && flags == ASTRA_AREA_CREATE_RESERVED);
  if (area_limits != 0u) { --area_limits; return ASTRA_SYSCALL_RESOURCE_LIMIT; }
  *area = next_area; next_area += 2u;
  area_extents[(*area - 40u) / 2u] = size; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_bind_pager(uint32_t area, uint32_t port, uint32_t cookie)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u &&
         port == 99u && cookie != 0u);
  ++area_binds;
  if (area_bind_observer != NULL) {
      void (*observe)(void) = area_bind_observer;
      area_bind_observer = NULL;
      observe();
  }
  return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_handle_duplicate(uint32_t source, uint32_t rights,
    uint32_t *duplicate)
{ assert(source >= 40u && source <= 48u && (source & 1u) == 0u &&
         rights == expected_area_rights);
  if ((rights & ASTRA_RIGHT_WRITE) != 0u) {
      HostfsFileArea *entry = file_areas;
      while (entry != NULL && entry->area != source) entry = entry->next;
      assert(entry != NULL && entry->io->cache != NULL && entry->io->cache->writer != 0u &&
          entry->io->cache->external_writes);
  }
  ++duplicate_attempts; *duplicate = 0u;
  if (duplicate_limits != 0u) { --duplicate_limits; return duplicate_status; }
  *duplicate = source + 1u; ++area_duplicates; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_resize_backing(uint32_t area, uint32_t size)
{
    assert(area >= 40u && area <= 48u && (area & 1u) == 0u && size <= ASTRA_AREA_SIZE_MAX);
    ++area_resizes;
    if (resize_result != ASTRA_SYSCALL_OK) return resize_result;
    area_extents[(area - 40u) / 2u] = size;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_references(uint32_t area, uint32_t *handles,
    uint32_t *mappings, uint32_t *children)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u);
  ++area_reference_queries;
  *handles = area_handles; *mappings = area_mappings;
  *children = area_children;
  for (unsigned i = 0u; i < 16u; ++i)
      if (vm_pages[i].area == area && (vm_pages[i].dirty || vm_pages[i].token)) {
          ++*children; break;
      }
  return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_next_process(uint32_t area, uint32_t after,
    uint64_t *sequence, uint32_t *process)
{
    assert(area >= 40u && area <= 48u && (area & 1u) == 0u);
    uint32_t index = (area - 40u) / 2u;
    *process = 0u;
    if (mapping_query_failure != 0u) return mapping_query_failure;
    if (*sequence != 0u && *sequence != mapping_sequences[index])
        return ASTRA_SYSCALL_WOULD_BLOCK;
    *sequence = mapping_sequences[index];
    for (uint32_t at = 0u; at < mapping_process_counts[index]; ++at) {
        uint32_t candidate = mapping_processes[index][at];
        if (candidate > after && (*process == 0u || candidate < *process))
            *process = candidate;
    }
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_reclaim(uint32_t area, uint32_t offset,
    uint32_t size, uint32_t *released)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u &&
         offset == 0u && size == ASTRA_AREA_SIZE_MAX);
  *released = 0u;
  ++area_reclaims;
  if (reclaim_result != ASTRA_SYSCALL_OK) return reclaim_result;
  for (unsigned i = 0u; i < 16u; ++i)
      if (vm_pages[i].area == area && (vm_pages[i].dirty || vm_pages[i].token))
          return ASTRA_SYSCALL_WOULD_BLOCK;
  for (unsigned i = 0u; i < 16u; ++i)
      if (vm_pages[i].area == area) vm_pages[i].area = 0u;
  *released = 1u; return ASTRA_SYSCALL_OK; }

static unsigned vm_page(uint32_t area, uint32_t offset, int create)
{
    unsigned empty = 16u;
    offset -= offset % ASTRA_MEMORY_PAGE_SIZE;
    for (unsigned i = 0u; i < 16u; ++i) {
        if (vm_pages[i].area == area && vm_pages[i].offset == offset) return i;
        if (vm_pages[i].area == 0u) empty = i;
    }
    if (!create) return 16u;
    assert(empty != 16u);
    memset(&vm_pages[empty], 0, sizeof(vm_pages[empty]));
    vm_pages[empty].area = area; vm_pages[empty].offset = offset;
    return empty;
}
uint32_t astra_rt_area_read_resident(uint32_t area, uint32_t offset,
    void *bytes, uint32_t size)
{
    assert(offset % ASTRA_MEMORY_PAGE_SIZE + size <= ASTRA_MEMORY_PAGE_SIZE);
    if (offset - offset % ASTRA_MEMORY_PAGE_SIZE >= area_extents[(area - 40u) / 2u])
        return ASTRA_SYSCALL_WOULD_BLOCK;
    unsigned i = vm_page(area, offset, 0);
    if (i == 16u) return ASTRA_SYSCALL_WOULD_BLOCK;
    memcpy(bytes, vm_pages[i].bytes + offset % ASTRA_MEMORY_PAGE_SIZE, size);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_write_pager(uint32_t area, uint32_t offset,
    const void *bytes, uint32_t size)
{
    assert(offset % ASTRA_MEMORY_PAGE_SIZE + size <= ASTRA_MEMORY_PAGE_SIZE);
    unsigned i = vm_page(area, offset, 0);
    if (i == 16u) return ASTRA_SYSCALL_WOULD_BLOCK;
    memcpy(vm_pages[i].bytes + offset % ASTRA_MEMORY_PAGE_SIZE, bytes, size);
    vm_pages[i].dirty = 1u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_publish_clean_page(uint32_t area, uint32_t offset,
    const void *bytes, uint32_t size)
{
    assert(offset % ASTRA_MEMORY_PAGE_SIZE == 0u && size > 0u && size <= ASTRA_MEMORY_PAGE_SIZE);
    if (page_publish_result != ASTRA_SYSCALL_OK) return page_publish_result;
    if (vm_page(area, offset, 0) != 16u) return ASTRA_SYSCALL_WOULD_BLOCK;
    unsigned i = vm_page(area, offset, 1);
    memcpy(vm_pages[i].bytes, bytes, size);
    memset(vm_pages[i].bytes + size, 0, ASTRA_MEMORY_PAGE_SIZE - size);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_publish_dirty_page(uint32_t area, uint32_t offset, const void *bytes)
{
    uint32_t status = astra_rt_area_publish_clean_page(area, offset, bytes, ASTRA_MEMORY_PAGE_SIZE);
    if (status == ASTRA_SYSCALL_OK) vm_pages[vm_page(area, offset, 0)].dirty = 1u;
    return status;
}
uint32_t astra_rt_area_dirty_scan(uint32_t area, uint32_t start, uint32_t *offset, uint32_t *state)
{
    *offset = ASTRA_AREA_SIZE_MAX; *state = 0u;
    for (unsigned i = 0u; i < 16u; ++i)
        if (vm_pages[i].area == area && vm_pages[i].offset >= start &&
            vm_pages[i].offset < *offset && (vm_pages[i].dirty || vm_pages[i].token)) {
            *offset = vm_pages[i].offset;
            *state = (vm_pages[i].dirty ? ASTRA_AREA_DIRTY_MODIFIED : 0u) |
                (vm_pages[i].token ? ASTRA_AREA_DIRTY_WRITEBACK : 0u);
        }
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_writeback_begin(uint32_t area, uint32_t offset, void *bytes, uint32_t *token)
{
    unsigned i = vm_page(area, offset, 0);
    assert(i != 16u && vm_pages[i].dirty && !vm_pages[i].token);
    vm_pages[i].dirty = 0u; *token = vm_pages[i].token = i + 1u;
    memcpy(bytes, vm_pages[i].bytes, ASTRA_MEMORY_PAGE_SIZE);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_writeback_complete(uint32_t area, uint32_t offset, uint32_t token, bool success)
{
    unsigned i = vm_page(area, offset, 0);
    assert(i != 16u && vm_pages[i].token == token);
    vm_pages[i].token = 0u;
    if (!success) vm_pages[i].dirty = 1u;
    return ASTRA_SYSCALL_OK;
}
const AstraVfsServiceStats *astra_vfs_service_stats(const AstraVfsService *service)
{
    return &service->stats;
}
static uint32_t stat_node(void *context, const char *path, AstraVfsNodeInfo *info)
{
    (void)context;
    assert(strcmp(path, "/config/services/hostfs/settings.conf") == 0);
    info->size = strlen(document); info->kind = ASTRA_VFS_KIND_FILE;
    return ASTRA_VFS_OK;
}
static uint32_t adopt_file(void *context, uint64_t token, uint32_t flags,
    uintptr_t *node)
{ assert(((AstraVfsHostBackend *)context)->session == 33u &&
         (token == 1234u || token == 1235u) && flags == 0u);
  *node = 55u; return ASTRA_VFS_OK; }
static uint32_t stat_open_file(void *context, uintptr_t node,
    AstraVfsNodeInfo *info)
{ assert(((AstraVfsHostBackend *)context)->session == 33u && node == 55u);
  *info = (AstraVfsNodeInfo){.kind = ASTRA_VFS_KIND_FILE,
    .size = open_file_size,
    .meta = {.valid = ASTRA_VFS_META_NLINK | (open_file_identity_valid ?
                      ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                      ASTRA_VFS_META_GENERATION : 0u), .nlink = open_file_links,
             .volume = 5u, .inode = 7u,
             .generation = open_file_generation}};
  return ASTRA_VFS_OK; }
static uint32_t close_open_file(void *context, uintptr_t node)
{ assert(((AstraVfsHostBackend *)context)->session == 33u && node == 55u);
  ++file_closes; return file_close_status; }
int astra_vfs_service_session_bound(const AstraVfsService *service,
    uint32_t session, uint32_t owner, uint32_t grant)
{
    assert(service != NULL && !usage_locked);
    return owner >= 1u && owner <= 3u && grant == owner && session == 100u + owner;
}
static uint32_t retained_count(uint32_t session)
{
    uint32_t count = 0u;
    for (unsigned i = 0u; i < 32u; ++i)
        count += native_references[i].file != ASTRA_VFS_FILE_INVALID &&
                 native_references[i].session == session;
    return count;
}
uint32_t astra_vfs_service_retain_file(AstraVfsService *service,
    uint32_t session, AstraVfsFile file, uint32_t access, AstraVfsFile *retained)
{
    assert(service != NULL && !usage_locked && retained != NULL);
    assert((access & ~(ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u);
    assert(session >= 101u && session <= 103u);
    assert((session == 102u && file == 2234u) ||
           (session != 102u && (file == 1234u || file == 1235u)));
    *retained = ASTRA_VFS_FILE_INVALID;
    if (retain_status != ASTRA_VFS_OK) return retain_status;
    if ((open_file_flags & access) != access) return ASTRA_VFS_ERR_ACCESS;
    for (unsigned i = 0u; i < 32u; ++i) {
        if (native_references[i].file != ASTRA_VFS_FILE_INVALID) continue;
        native_references[i].service = service;
        native_references[i].session = session;
        *retained = native_references[i].file = 5000u + i;
        return ASTRA_VFS_OK;
    }
    assert(0 && "test reference leak");
    return ASTRA_VFS_ERR_LIMIT;
}
static uint32_t area_call_session, area_call_file;
static struct {
    HostfsVolume *owner;
    AstraVfsNodeMeta identity;
    uint32_t file;
    AstraVfsFileIO io;
} test_io[32];
uint32_t astra_vfs_service_retain_active(AstraVfsService *service,
    const AstraVfsOpenFile *active, AstraVfsFile *copy)
{
    assert(astra_vfs_open_file_active(active));
    return astra_vfs_service_retain_file(service, area_call_session,
        area_call_file, 0u, copy);
}
uint32_t astra_vfs_service_file_area(AstraVfsService *service,
    uint32_t session, AstraVfsFile file, uint64_t base, uint32_t span,
    AstraVfsFileAreaCreate create, void *context, uint32_t *area, uint64_t *size)
{
    /* Native core tests own admission/I/O/lifetime; this fixture owns areas. */
    *area = 0u; *size = 0u;
    if (span == 0u || span > ASTRA_AREA_SIZE_MAX ||
        ((base | span) & (ASTRA_MEMORY_PAGE_SIZE - 1u)) != 0u ||
        span > ASTRA_AREA_SIZE_MAX - base % ASTRA_AREA_SIZE_MAX || base > UINT64_MAX - span)
        return ASTRA_VFS_ERR_INVALID;
    if ((open_file_flags & ASTRA_VFS_OPEN_READ) == 0u) return ASTRA_VFS_ERR_ACCESS;
    HostfsGrant *grant = context;
    HostfsBackend backend = {.provider.session = 33u, .grant = grant, .owner = grant->owner};
    AstraVfsNodeInfo info;
    uint32_t status = stat_open_file(&backend, 55u, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (base >= info.size) return ASTRA_VFS_ERR_INVALID;
    AstraVfsOpenFile active = {.backend = {astra_vfs_host_ops(), &backend},
        .node = 55u, .flags = open_file_flags, .kind = ASTRA_VFS_KIND_FILE};
    if (file_areas == NULL) memset(test_io, 0, sizeof(test_io));
    for (unsigned i = 0u; i < 32u; ++i) {
        int same = test_io[i].owner == grant->owner &&
            (astra_vfs_node_identity_equal(&test_io[i].identity, &info.meta) ||
             (!astra_vfs_node_identity_equal(&info.meta, &info.meta) && test_io[i].file == file));
        if (test_io[i].owner != NULL && !same) continue;
        test_io[i].owner = grant->owner; test_io[i].identity = info.meta;
        test_io[i].file = file; test_io[i].io.identity = info.meta;
        active.io = &test_io[i].io;
        break;
    }
    assert(active.io != NULL);
    AstraVfsOpenFile *saved_active = active.io->active;
    active.io->active = &active;
    uint32_t saved_session = area_call_session, saved_file = area_call_file;
    area_call_session = session; area_call_file = file;
    status = hostfs_cache_ops.stat(NULL, &active, &info);
    if (status == ASTRA_VFS_OK) {
        if (active.io->cache != NULL) info.size = active.io->cache->size;
        AstraVfsFile owned = file;
        if ((open_file_flags & ASTRA_VFS_OPEN_WRITE) != 0u)
            status = astra_vfs_service_retain_file(service, session, file,
                ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &owned);
        if (status == ASTRA_VFS_OK)
            status = create(context, &active, &owned, &info, base, span, area);
        if (owned != file && owned != ASTRA_VFS_FILE_INVALID)
            assert(astra_vfs_service_release_retained(service, &owned) == ASTRA_VFS_OK);
    }
    active.io->active = saved_active;
    area_call_session = saved_session; area_call_file = saved_file;
    if (status == ASTRA_VFS_OK) *size = info.size;
    (void)service;
    return status;
}
uint32_t astra_vfs_service_stat_retained(AstraVfsService *service,
    AstraVfsFile retained, AstraVfsNodeInfo *info)
{
    assert(!usage_locked && retained >= 5000u && retained < 5032u &&
        native_references[retained - 5000u].file == retained &&
        native_references[retained - 5000u].service == service);
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next) {
        if (entry->source.service != service || entry->source.retained != retained ||
            entry->io == NULL) continue;
        HostfsBackend backend = {.provider.session = 33u, .owner = entry->owner,
            .grant = entry->grant};
        AstraVfsOpenFile active = {.backend = {astra_vfs_host_ops(), &backend},
            .node = 55u, .flags = open_file_flags, .kind = ASTRA_VFS_KIND_FILE,
            .io = entry->io};
        assert(active.io->active == NULL);
        active.io->active = &active;
        uint32_t status = hostfs_cache_ops.stat(NULL, &active, info);
        if (status == ASTRA_VFS_OK && active.io->cache != NULL)
            info->size = active.io->cache->size;
        active.io->active = NULL;
        ++active.io->sequence;
        return status;
    }
    AstraVfsHostBackend backend = {.session = 33u};
    return stat_open_file(&backend, 55u, info);
}
uint32_t astra_vfs_service_release_retained(AstraVfsService *service,
    AstraVfsFile *retained)
{
    assert(!usage_locked && retained != NULL && *retained >= 5000u &&
        *retained < 5032u && native_references[*retained - 5000u].file == *retained &&
        native_references[*retained - 5000u].service == service);
    if (release_wait_status != ASTRA_VFS_OK) return release_wait_status;
    native_references[*retained - 5000u].file = ASTRA_VFS_FILE_INVALID;
    *retained = ASTRA_VFS_FILE_INVALID;
    ++retained_releases;
    return file_close_status;
}

static HostfsFileArea *cache_entry(AstraVfsService *service, AstraVfsFile retained)
{
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        if (entry->source.service == service && entry->io != NULL &&
            (entry->source.retained == retained ||
             (entry->io->cache != NULL && entry->io->cache->writer == retained))) return entry;
    return NULL;
}
uint32_t astra_vfs_service_cache_prepare(AstraVfsService *service,
    const AstraVfsOpenFile *file, AstraVfsFile *owned, uint32_t flags, AstraVfsFileCache *storage,
    AstraVfsFileCache *snapshot)
{
    assert(!usage_locked && astra_vfs_open_file_active(file) && owned != NULL);
    assert((flags & ~ASTRA_VFS_CACHE_EXTERNAL_WRITES) == 0u);
    assert((file->flags & ASTRA_VFS_OPEN_WRITE) != 0u);
    if (prepare_failure != 0u) return prepare_failure;
    if (!astra_vfs_node_identity_equal(&file->io->identity, &file->io->identity))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (file->io->cache == NULL) {
        assert(storage != NULL && storage->writer == 0u && *owned != 0u);
        assert(stat_open_file(file->backend.context, file->node, &storage->backing) == ASTRA_VFS_OK);
        storage->writer = *owned; *owned = 0u;
        storage->size = storage->backing.size;
        file->io->cache = storage; ++service->cached_nodes;
    }
    if ((flags & ASTRA_VFS_CACHE_EXTERNAL_WRITES) != 0u)
        file->io->cache->external_writes = 1u;
    *snapshot = *file->io->cache;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_service_cache_publish(AstraVfsService *service,
    const AstraVfsOpenFile *file, uint64_t size, uint64_t modified_ns)
{
    (void)service;
    assert(!usage_locked && astra_vfs_open_file_active(file) && file->io->cache != NULL);
    file->io->cache->size = size; file->io->cache->modified_ns = modified_ns;
    file->io->cache->written = 1u;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_service_cache_snapshot(AstraVfsService *service,
    AstraVfsFile retained, AstraVfsFileCache *snapshot, uint32_t *sequence)
{
    assert(!usage_locked);
    HostfsFileArea *entry = cache_entry(service, retained);
    if (entry == NULL) return ASTRA_VFS_ERR_BAD_HANDLE;
    *snapshot = entry->io->cache == NULL ? (AstraVfsFileCache){0} : *entry->io->cache;
    *sequence = entry->io->sequence;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_service_cache_retire(AstraVfsService *service,
    AstraVfsFile retained, uint32_t sequence)
{
    assert(!usage_locked);
    HostfsFileArea *entry = cache_entry(service, retained);
    if (entry == NULL) return ASTRA_VFS_ERR_BAD_HANDLE;
    if (entry->io->active != NULL || entry->io->sequence != sequence) return ASTRA_VFS_ERR_BUSY;
    AstraVfsFileCache *cache = entry->io->cache;
    if (cache == NULL) return ASTRA_VFS_OK;
    if (cache->error != 0u) return cache->error;
    if (cache->size != cache->backing.size) return ASTRA_VFS_ERR_BUSY;
    AstraVfsFile writer = cache->writer;
    entry->io->cache = NULL; --service->cached_nodes;
    memset(cache, 0, sizeof(*cache));
    return astra_vfs_service_release_retained(service, &writer);
}
uint32_t astra_vfs_backend_read_page(const AstraVfsBackend *backend, uintptr_t node,
    uint32_t access, const AstraVfsNodeMeta *identity, const AstraVfsNodeInfo *expected,
    uint64_t offset, void *bytes, uint32_t *valid)
{
    AstraVfsNodeInfo current;
    assert(!usage_locked && (access == ASTRA_VFS_OPEN_READ || access == ASTRA_VFS_OPEN_WRITE));
    assert(stat_open_file(backend->context, node, &current) == ASTRA_VFS_OK);
    assert(astra_vfs_node_identity_equal(identity, &current.meta));
    assert(astra_vfs_node_revision_equal(expected, &current));
    ++page_reads; *valid = 0u;
    if (page_read_result != ASTRA_VFS_OK) return page_read_result;
    memset(bytes, 0, ASTRA_MEMORY_PAGE_SIZE);
    if (offset < current.size) {
        *valid = current.size - offset < ASTRA_MEMORY_PAGE_SIZE ?
            (uint32_t)(current.size - offset) : ASTRA_MEMORY_PAGE_SIZE;
        if (offset < sizeof(backing_bytes)) {
            uint32_t count = sizeof(backing_bytes) - offset < *valid ?
                (uint32_t)(sizeof(backing_bytes) - offset) : *valid;
            memcpy(bytes, backing_bytes + offset, count);
        }
    }
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_service_write_page_retained(AstraVfsService *service,
    AstraVfsFile writer, const AstraVfsOpenFile *active, const AstraVfsNodeInfo *expected,
    uint64_t offset, uint64_t size, const void *bytes, uint32_t *moved, AstraVfsNodeInfo *current)
{
    (void)service;
    assert(!usage_locked && astra_vfs_open_file_active(active) && active->io->cache->writer == writer);
    assert(astra_vfs_node_revision_equal(expected, &active->io->cache->backing));
    *moved = 0u; ++writes;
    if (write_result != ASTRA_VFS_OK) return write_result;
    *moved = size - offset < ASTRA_MEMORY_PAGE_SIZE ? (uint32_t)(size - offset) : ASTRA_MEMORY_PAGE_SIZE;
    assert(offset + *moved <= sizeof(backing_bytes));
    memcpy(backing_bytes + offset, bytes, *moved);
    if (offset + *moved > open_file_size) open_file_size = offset + *moved;
    assert(stat_open_file(active->backend.context, active->node, current) == ASTRA_VFS_OK);
    active->io->cache->backing = *current;
    return ASTRA_VFS_OK;
}
uint32_t astra_vfs_service_sync_retained(AstraVfsService *service, AstraVfsFile retained, uint32_t *remaining)
{
    HostfsFileArea *entry = cache_entry(service, retained);
    assert(entry != NULL && entry->io->active == NULL && entry->io->cache != NULL);
    AstraVfsOpenFile active = {.io = entry->io, .backend = {
        &entry->owner->ops, &entry->owner->backend}, .node = 55u};
    active.io->active = &active;
    uint32_t status = hostfs_sync(NULL, &active, remaining);
    if (status != ASTRA_VFS_ERR_BUSY) active.io->cache->error = status;
    active.io->active = NULL; ++active.io->sequence;
    return status;
}
uint32_t astra_vfs_backend_description_operation(const AstraVfsBackend *backend,
    uintptr_t node, uint32_t operation, const AstraVfsRequest *request,
    AstraVfsReply *reply, const uint64_t *eof)
{
    (void)backend; (void)eof;
    assert(node == 55u);
    if (operation == ASTRA_VFS_OP_FILE_FLAGS) reply->open_flags = open_file_flags;
    else {
        assert(operation == ASTRA_VFS_OP_SEEK && request->flags <= 1u);
        if (request->flags == 0u) file_position = request->offset;
        reply->position = file_position;
    }
    return ASTRA_VFS_OK;
}
static uint32_t read_open_file(void *context, uintptr_t node, uint64_t offset,
    void *data, uint32_t length, uint32_t *moved)
{ assert(context != NULL && node == 55u && offset == 0u && length == 3u);
  memcpy(data, "disk", length);
  *moved = length; return ASTRA_VFS_OK; }
static uint32_t flags_open_file(void *context, uintptr_t node,
    uint32_t mask, uint32_t flags, uint32_t *result)
{ assert(context != NULL && node == 55u && mask == 0u && flags == 0u);
  *result = open_file_flags; return ASTRA_VFS_OK; }
static uint32_t write_open_file(void *context, uintptr_t node, uint64_t offset,
    uint32_t flags, uint64_t max_end, const void *buffer, uint32_t length,
    uint32_t *moved, uint64_t *position)
{ (void)context; (void)flags; (void)max_end; (void)buffer;
  assert(node == 55u && length <= 1u); ++writes;
  *moved = length; *position = offset + length; return write_result; }
static uint32_t truncate_open_file(void *context, uintptr_t node, uint64_t size)
{ (void)context; assert(node == 55u && size <= 1u);
  ++truncates; open_file_size = size; return ASTRA_VFS_OK; }
const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    static const AstraVfsBackendOps ops = {.stat = stat_node,
        .adopt = adopt_file, .stat_node = stat_open_file, .close = close_open_file,
        .read = read_open_file, .write = write_open_file,
        .file_flags = flags_open_file,
        .truncate = truncate_open_file};
    return &ops;
}
uint32_t astra_vfs_backend_read_path(const AstraVfsBackend *source, const char *path,
    void *buffer, uint32_t capacity, uint32_t *moved, uint64_t *size)
{
    (void)source; (void)path;
    assert(capacity == strlen(document));
    memcpy(buffer, document, capacity);
    *moved = capacity; *size = capacity;
    return read_status;
}
static void settings(uint64_t files, uint64_t sessions, uint64_t messages, const char *extra)
{
    snprintf(document, sizeof(document), "astra-config 1\nschema 1\nowner_files %llu\nowner_sessions %llu\nqueue_messages %llu\n%s",
        (unsigned long long)files, (unsigned long long)sessions, (unsigned long long)messages, extra);
}
static void test_backend_bindings(void)
{
    HostfsVolume *volume = calloc(1u, sizeof(*volume));
    assert(volume != NULL);
    HostfsGrant grant = {.owner = volume, .session = 33u, .port.id = 34u};
    HostfsGrant *table[] = {&grant};
    grants = table; grant_count = 1u; transport.generation = 9u;
    allocation_failed = 1;
    assert(grant_backend_create(&grant, 9u) == ASTRA_VFS_ERR_LIMIT && grant.backend == NULL);
    allocation_failed = 0;
    grant.backing_references = UINT32_MAX;
    assert(grant_backend_create(&grant, 9u) == ASTRA_VFS_ERR_LIMIT && grant.backend == NULL);
    grant.backing_references = 0u;
    assert(grant_backend_create(&grant, 9u) == ASTRA_VFS_OK && grant.backing_references == 1u);
    HostfsBackend *old = grant.backend;
    assert(old->owner == volume && old->grant == &grant && old->references == 1u);
    assert(grant_backend_create(&grant, 9u) == ASTRA_VFS_ERR_BUSY && grant.backend == old &&
        grant.backing_references == 1u);
    AstraVfsBackend held = {0}, rejected = {0};
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_acquire(NULL, 7u, 34u, &rejected) == ASTRA_VFS_ERR_ACCESS);
    assert(hostfs_backend_acquire(volume, 7u, 35u, &rejected) == ASTRA_VFS_ERR_ACCESS);
    assert(hostfs_backend_acquire(volume, 7u, 34u, &held) == ASTRA_VFS_OK &&
        held.ops == &volume->ops && held.context == old && old->references == 2u);
    assert(hostfs_backend_incarnation(volume, &held) == 9u);
    old->references = UINT32_MAX;
    assert(hostfs_backend_retain(volume, &held) == ASTRA_VFS_ERR_LIMIT);
    old->references = 2u;
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_detach(&grant, 1) == ASTRA_VFS_OK && grant.backend == NULL &&
        old->provider.lost && old->references == 1u && grant.backing_references == 1u);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_retain(volume, &held) == ASTRA_VFS_ERR_LOST);
    assert(hostfs_backend_acquire(volume, 7u, 34u, &rejected) == ASTRA_VFS_ERR_LOST);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_create(&grant, 10u) == ASTRA_VFS_OK && grant.backend != old &&
        !grant.backend->provider.lost && grant.backing_references == 2u);
    assert(hostfs_backend_incarnation(volume, &held) == 9u);
    AstraVfsBackend fresh = {&volume->ops, grant.backend};
    assert(hostfs_backend_incarnation(volume, &fresh) == 10u);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    hostfs_backend_release(volume, &held);
    assert(grant.backing_references == 1u);
    grant.retiring = 1u;
    assert(hostfs_backend_acquire(volume, 7u, 34u, &rejected) == ASTRA_VFS_ERR_LOST);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_detach(&grant, 1) == ASTRA_VFS_OK && grant.backing_references == 0u);
    assert(grant_backend_create(&grant, 9u) == ASTRA_VFS_ERR_LOST && grant.backend == NULL &&
        grant.backing_references == 0u);
    assert(grant_backend_detach(&grant, 1) == ASTRA_VFS_OK && !usage_locked);
    grants = NULL; grant_count = 0u;
    free(volume);
}
static void test_grant_admission_pressure(void)
{
    static const struct {
        uint32_t port_status, handles, mappings, children, pins;
        uint32_t status, expected_ports;
    } cases[] = {
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 0u, 0u, ASTRA_VFS_OK, 2u},
        {ASTRA_SYSCALL_OUT_OF_MEMORY, 1u, 0u, 0u, 0u, ASTRA_VFS_OK, 2u},
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 2u, 0u, 0u, 0u, ASTRA_VFS_ERR_LIMIT, 2u},
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 1u, 0u, 0u, ASTRA_VFS_ERR_LIMIT, 2u},
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 1u, 0u, ASTRA_VFS_ERR_LIMIT, 2u},
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 0u, 1u, ASTRA_VFS_ERR_LIMIT, 2u},
        {ASTRA_SYSCALL_IO_ERROR, 1u, 0u, 0u, 0u, ASTRA_VFS_ERR_IO, 1u},
    };
    HostfsVolume *volume = calloc(1u, sizeof(*volume));
    assert(volume != NULL);
    const uint32_t saved_next = next_grant_id;
    device_handle = 30u; owner_files = 1024u; queue_messages = 16u;
    transport.generation = 9u;
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        HostfsGrant cached = {.owner = volume};
        HostfsGrant *table[4] = {0};
        grants = table; grant_capacity = 4u; grant_count = 0u; next_grant_id = 34u;
        file_areas = calloc(1u, sizeof(*file_areas));
        assert(file_areas != NULL);
        file_areas->owner = volume;
        file_areas->grant = &cached; file_areas->area = 40u;
        file_areas->source.service = &volume->service;
        assert(astra_vfs_service_retain_file(&volume->service, 101u, 1234u, ASTRA_VFS_OPEN_READ,
            &file_areas->source.retained) == ASTRA_VFS_OK);
        file_areas->source.cookie = 1u;
        file_areas->source.span = ASTRA_AREA_SIZE_MAX;
        file_areas->pins = cases[i].pins;
        port_pressure_status = cases[i].port_status;
        area_handles = cases[i].handles; area_mappings = cases[i].mappings;
        area_children = cases[i].children;
        port_creates = area_reference_queries = 0u;
        uint32_t send = 0u;
        assert(grant_add(volume, 33u, ASTRA_VFS_GRANT_READ, &volume->reference,
            "/", 7u, &send, NULL) == cases[i].status);
        assert(port_creates == cases[i].expected_ports);
        if (cases[i].status == ASTRA_VFS_OK) {
            assert(send == 35u && grant_count == 1u && file_areas == NULL &&
                retained_count(101u) == 0u && grant_retire(0u));
            assert(astra_close(send) == ASTRA_SYSCALL_OK);
        } else {
            assert(send == 0u && grant_count == 0u && file_areas != NULL &&
                retained_count(101u) == 1u && file_areas->source.retained != ASTRA_VFS_FILE_INVALID);
            assert(cases[i].port_status == ASTRA_SYSCALL_IO_ERROR ?
                area_reference_queries == 0u : area_reference_queries == 1u);
            area_handles = 1u; area_mappings = area_children = file_areas->pins = 0u;
            file_areas_maintain(1);
        }
        assert(file_areas == NULL && retained_count(101u) == 0u && !usage_locked);
    }
    grants = NULL; grant_capacity = grant_count = 0u; next_grant_id = saved_next;
    port_pressure_status = device_handle = owner_files = queue_messages = 0u;
    retained_releases = file_closes = released_grants = usage_holders_changed = area_reference_queries = 0u;
    area_handles = 2u; area_mappings = 1u;
    free(volume);
}

static void test_native_file_usage(void)
{
    HostfsVolume volume = {.reference = {55u, 5u, 1u}};
    AstraVfsSessionSlot sessions[] = {{.id = 101u, .owner = 700u}, {.id = 102u, .owner = 900u}};
    AstraVfsOpenFile files[] = {
        {.node = 33u, .session = 101u, .state = ASTRA_VFS_FILE_OPEN},
        {.token = 77u, .session = 102u, .state = ASTRA_VFS_FILE_SHARED},
    };
    files[0].io = &files[0].io_storage;
    files[0].io->active = &files[0];
    volume.service.sessions = sessions; volume.service.session_capacity = 2u;
    volume.service.files = files; volume.service.file_high_water = 2u;
    volumes = &volume; usage_file_mode = 1;
    AstraVolumeUsagePage *page = calloc(1u, sizeof(*page));
    assert(page != NULL);
    AstraVolumeUsageQuery query = {.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query),
        .volume = {55u, 5u, 1u}, .after_kind = ASTRA_VOLUME_USAGE_FILE, .after = 33u,
        .after_process = 500u, .sequence = usage_sequence, .catalog_sequence = 31u};
    uint32_t status = hostfs_read_usage(&query, page);
    if (status != ASTRA_VFS_OK || page->count != 2u)
        fprintf(stderr, "native usage status %u count %u\n", status, page->count);
    assert(status == ASTRA_VFS_OK && page->count == 2u);
    assert(page->records[0].handle == 33u && page->records[0].process == 700u &&
        (page->records[0].flags & ASTRA_VOLUME_USAGE_ACTIVE) != 0u &&
        strcmp(page->records[0].path, "/ram0/dir") == 0);
    assert(page->records[1].handle == 77u && page->records[1].process == 900u);
    query.after_process = 700u;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && page->count == 1u &&
        page->records[0].kind == ASTRA_VOLUME_USAGE_SHARE && page->records[0].process == 900u);
    query.after = query.after_kind = query.after_process = 0u;
    files[0].session = 102u; sessions[1].closing = 1u;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && page->count == 2u &&
        page->records[0].process == 900u &&
        (page->records[0].flags & ASTRA_VOLUME_USAGE_CLEANUP_PENDING) != 0u);
    query.sequence = page->sequence;
    hostfs_holders_changed(NULL);
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_BUSY);
    query.sequence = query.catalog_sequence = 0u;
    files[0].session = 0u; files[0].state = ASTRA_VFS_FILE_DETACHED;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && page->records[0].process == 0u);
    HostfsFileArea mapped = {.owner = &volume, .io = files[0].io, .area = 40u,
        .pins = 1u, .source = {.cookie = 1u, .service = &volume.service}};
    file_areas = &mapped;
    mapping_sequences[0] = 1u;
    const uint32_t holders[] = {11u, 7u, 9u, 9u};
    memcpy(mapping_processes[0], holders, sizeof(holders));
    mapping_process_counts[0] = 4u;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK &&
        page->count == 2u && page->records[0].process == 7u && page->records[1].process == 9u &&
        page->next == 33u && page->next_process == 9u &&
        strcmp(page->records[0].path, "/ram0/dir") == 0);
    query.volume = page->volume; query.sequence = page->sequence; query.catalog_sequence = page->catalog_sequence;
    query.after = page->next; query.after_kind = page->next_kind; query.after_process = page->next_process;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK &&
        page->count == 2u && page->records[0].kind == ASTRA_VOLUME_USAGE_FILE &&
        page->records[0].process == 11u && page->records[1].kind == ASTRA_VOLUME_USAGE_SHARE &&
        page->records[1].process == 900u && page->next == 0u);
    mapping_process_counts[0] = 2u;
    mapping_processes[0][0] = 7u; mapping_processes[0][1] = 11u;
    ++mapping_sequences[0];
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_BUSY);
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query)};
    mutate_usage_mapping = 1;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_BUSY);
    assert(usage_holders_changed != 0u);
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && page->count == 2u &&
        page->records[0].process == 7u && page->records[1].process == 13u);
    HostfsFileArea sibling = {.owner = &volume, .io = files[0].io, .area = 42u,
        .pins = 1u, .source = {.cookie = 2u, .service = &volume.service}};
    mapped.next = &sibling;
    mapping_sequences[1] = 1u;
    mapping_process_counts[1] = 2u;
    mapping_processes[1][0] = 15u; mapping_processes[1][1] = 7u;
    files[0].session = 101u; files[0].state = ASTRA_VFS_FILE_OPEN;
    const uint32_t expected[] = {7u, 13u, 15u, 700u};
    uint32_t seen = 0u, shares = 0u;
    do {
        assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && astra_volume_usage_page_valid(&query, page));
        for (uint32_t at = 0u; at < page->count; ++at) {
            if (page->records[at].kind == ASTRA_VOLUME_USAGE_FILE) {
                assert(seen < 4u && page->records[at].process == expected[seen++]);
                assert(strcmp(page->records[at].path, "/ram0/dir") == 0);
            } else {
                assert(seen == 4u && page->records[at].process == 900u);
                ++shares;
            }
        }
        query.volume = page->volume; query.sequence = page->sequence; query.catalog_sequence = page->catalog_sequence;
        query.after = page->next; query.after_kind = page->next_kind; query.after_process = page->next_process;
    } while (page->next != 0u);
    assert(seen == 4u && shares == 1u);
    AstraVolumeUsageRecord *records = calloc(5u, sizeof(*records));
    AstraVolumeUsageSnapshotInfo info;
    assert(records != NULL);
    assert(hostfs_read_usage_snapshot(&volume.reference, records, 2u, &info) ==
        ASTRA_STATUS_BUFFER_TOO_SMALL && info.count == 5u && !usage_locked);
    assert(records[0].handle == 0u);
    assert(hostfs_read_usage_snapshot(&volume.reference, records, 5u, &info) ==
        ASTRA_STATUS_OK && info.count == 5u && !usage_locked);
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION,
        .size = sizeof(query), .volume = volume.reference};
    assert(astra_volume_usage_snapshot_info_valid(&volume.reference, &info) &&
        astra_volume_usage_records_valid(&query, &info.volume, records, info.count));
    for (uint32_t at = 0u; at < 4u; ++at)
        assert(records[at].process == expected[at] &&
            strcmp(records[at].path, "/ram0/dir") == 0);
    assert(records[4].kind == ASTRA_VOLUME_USAGE_SHARE && records[4].process == 900u);
    mutate_usage_mapping = 1;
    assert(hostfs_read_usage_snapshot(&volume.reference, records, 5u, &info) ==
        ASTRA_STATUS_BUSY && info.count == 0u && info.sequence == 0u && !usage_locked);
    assert(hostfs_read_usage_snapshot(&volume.reference, records, 5u, &info) == ASTRA_STATUS_OK);
    allocation_failed = 1;
    assert(hostfs_read_usage_snapshot(&volume.reference, records, 5u, &info) ==
        ASTRA_STATUS_LIMIT && info.count == 0u && !usage_locked);
    allocation_failed = 0;
    free(records);
    HostfsVolume foreign_volume = {.reference = {55u, 6u, 1u}};
    HostfsFileArea foreign = {.owner = &foreign_volume, .area = 44u,
        .pins = 1u, .source = {.cookie = 3u, .service = &foreign_volume.service}};
    sibling.next = &foreign;
    mapping_sequences[2] = 9u;
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION,
        .size = sizeof(query), .volume = volume.reference};
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK);
    assert(foreign.usage_mapping_sequence == 0u);
    query.volume = (AstraVolumeReference){0};
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK);
    assert(foreign.usage_mapping_sequence == 9u);
    sibling.next = NULL;
    mapping_sequences[2] = 0u;
    mapping_query_failure = ASTRA_SYSCALL_IO_ERROR;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_IO);
    mapping_query_failure = 0u;
    file_areas = NULL;
    mapping_process_counts[0] = mapping_process_counts[1] = 0u;
    mapping_sequences[0] = mapping_sequences[1] = 0u;
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query)};
    files[0].state = ASTRA_VFS_FILE_FREE;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && page->records[0].process == 1u);
    usage_file_mode = 0; volumes = NULL; usage_notifications = 0u;
    free(page);
}

static void test_native_image_lease(void)
{
    HostfsBackend backend = {0};
    AstraVfsOpenFile file = {.kind = ASTRA_VFS_KIND_FILE, .flags = ASTRA_VFS_OPEN_READ,
        .token = 123u, .backend.context = &backend};
    AstraVolumeReference reference = {0};
    assert(hostfs_image_from_share(&reference, &file) == ASTRA_VFS_OK && image_calls == 1u &&
        reference.id == 88u);
    file.flags |= ASTRA_VFS_OPEN_WRITE;
    assert(hostfs_image_from_share(&reference, &file) == ASTRA_VFS_ERR_ACCESS && image_calls == 1u);
    file.flags = ASTRA_VFS_OPEN_READ; backend.provider.lost = 1u;
    assert(hostfs_image_from_share(&reference, &file) == ASTRA_VFS_ERR_LOST && image_calls == 1u);
    backend.provider.lost = 0u; image_status = ASTRA_VFS_ERR_IO;
    assert(hostfs_image_from_share(&reference, &file) == ASTRA_VFS_ERR_IO && image_calls == 2u);
}

static void test_shutdown_preserves_admission(void)
{
    HostfsVolume volume = {0};
    HostfsFileArea page = {.pins = 1u};

    transport.generation = 9u;
    for (uint32_t at = 0u; at < 4u; ++at) {
        volume_port.receive = 86u;
        file_sync_receive = at == 3u ? 78u : 0u;
        sync_pump_error = ASTRA_SYSCALL_IO_ERROR;
        file_pager_thread = 88u;
        file_pager_stop = 0u;
        metrics_port.stalled = at == 0u ? ASTRA_SYSCALL_IO_ERROR : 0u;
        volume.port.stalled = at == 1u ? ASTRA_SYSCALL_IO_ERROR : 0u;
        volumes = at == 1u ? &volume : NULL;
        pump_page = at == 2u ? &page : NULL;
        assert(finish_hostfs() == (at == 2u ? ASTRA_VFS_ERR_BUSY : ASTRA_VFS_ERR_IO));
        assert(volume_port.receive == 86u && file_pager_thread == 88u &&
               file_pager_stop == 0u && shutdown_releases == 0u &&
               transport_closes == 0u && pager_stops == 0u);
        file_areas = NULL;
    }
    assert(sync_pumps == 1u);
    file_sync_receive = 0u;
    sync_pump_error = 0u;
    volumes = NULL;
    volume_port = (AstraVolumePort){0};
    metrics_port = (AstraVfsPortService){0};
    file_pager_thread = 0u;
}

static void test_shutdown_failure_and_retry(void)
{
    HostfsVolume volume = {.reference = {55u, 5u, 1u}};
    AstraShutdownRequest request = {.header.transaction_id = 17u};
    shutdown_fixture = 1;
    transport.generation = 9u;
    volumes = &volume;
    volume_port.receive = 86u;
    file_pager_thread = 88u;
    for (uint32_t at = 0u; at < 4u; ++at) {
        volume.backend.provider.session = 33u;
        bound_provider_epoch = 55u;
        shutdown_claims = shutdown_releases = shutdown_closes = 0u;
        shutdown_close_result_count = at == 0u ? 2u : 0u;
        shutdown_close_results[0] = ASTRA_VFS_ERR_IO;
        shutdown_close_results[1] = ASTRA_VFS_ERR_BAD_HANDLE;
        shutdown_release_status = at == 1u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
        shutdown_reply_status = at == 2u ? ASTRA_SYSCALL_CLOSED : ASTRA_SYSCALL_OK;
        uint32_t status = hostfs_shutdown(&request, 89u);
        assert(status == (at < 2u ? ASTRA_VFS_ERR_IO :
                          at == 2u ? ASTRA_VFS_ERR_PEER : ASTRA_VFS_OK));
        assert(shutdown_decision == (at < 2u ? ASTRA_SHUTDOWN_CANCEL : ASTRA_SHUTDOWN_READY));
        assert(shutdown_reason == (at < 2u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK));
        assert(shutdown_releases == (at == 0u ? 0u : 1u) &&
               shutdown_closes == (at == 0u ? 2u : 1u) &&
               shutdown_claims == (at < 3u ? 1u : 0u));
        assert(bound_provider_epoch == (at < 3u ? 55u : 0u) &&
               volume.backend.provider.session == (at < 3u ? 33u : 0u));
        assert(volume_port.receive == 86u && file_pager_thread == 88u &&
               transport_closes == 0u && pager_stops == 0u);
    }
    shutdown_fixture = 0;
    shutdown_release_status = shutdown_reply_status = shutdown_claims = shutdown_releases = 0u;
    volumes = NULL;
    volume_port = (AstraVolumePort){0};
    file_pager_thread = 0u;
    session_opens = 0u;
}

static void test_shutdown_grant_retry(void)
{
    HostfsVolume volume = {.reference = {55u, 5u, 1u}};
    HostfsGrant grant = {.owner = &volume, .session = 33u,
        .volume = {55u, 5u, 1u}, .root = "/",
        .port = {.id = 34u, .receive = 32u, .rights = ASTRA_VFS_GRANT_WRITE}};
    HostfsGrant *entries[] = {&grant};
    AstraShutdownRequest request = {.header.transaction_id = 17u};
    shutdown_fixture = 1;
    transport.generation = 9u;
    volumes = &volume; grants = entries; grant_count = 1u;
    bound_provider_epoch = 55u;
    shutdown_closes = 0u;
    shutdown_close_result_count = 2u;
    shutdown_close_results[0] = shutdown_close_results[1] = ASTRA_VFS_ERR_IO;
    handle_close_failure = ASTRA_SYSCALL_IO_ERROR;
    assert(hostfs_shutdown(&request, 89u) == ASTRA_VFS_ERR_IO);
    assert(grant_count == 1u && !grant.retiring && grant.port.receive == 32u &&
        grant.session == 33u && bound_provider_epoch == 0u);
    assert(hostfs_check_provider() == ASTRA_VFS_OK && bound_provider_epoch == 55u &&
        grant_count == 1u && !grant.retiring && grant.port.receive == 32u &&
        grant.session == 33u && grant.backend != NULL);
    assert(grant_backend_detach(&grant, 1) == ASTRA_VFS_OK);
    shutdown_fixture = 0; shutdown_close_result_count = handle_close_failure = 0u;
    shutdown_claims = shutdown_releases = shutdown_closes = session_opens = 0u;
    bound_provider_epoch = 0u; volumes = NULL; grants = NULL; grant_count = 0u;
}

static HostfsGrant *competing_grant;
static uint64_t competing_base;
static uint32_t competing_area;

static void publish_competing_area(void)
{
    uint64_t size = 0u;
    uint32_t owner = competing_grant->port.id;
    assert(!usage_locked);
    assert(hostfs_file_area(NULL, &competing_grant->port, owner, 100u + owner,
        owner == 2u ? 2234u : 1234u, competing_base, ASTRA_MEMORY_PAGE_SIZE,
        &competing_area, &size) == ASTRA_VFS_OK);
    assert(size == open_file_size);
}

static void test_file_area_extent_failure_keeps_source_unpublished(void)
{
    const uint32_t failures[] = {ASTRA_SYSCALL_IO_ERROR, ASTRA_SYSCALL_RESOURCE_LIMIT};
    for (unsigned i = 0u; i < 2u; ++i) {
        HostfsVolume volume = {0};
        HostfsGrant grant = {.owner = &volume, .port.id = 1u};
        uint32_t area = 0u, duplicates = area_duplicates, resizes = area_resizes;
        uint64_t size = 0u;
        assert(file_areas == NULL);
        file_pager_thread = 88u; file_pager_send = 99u;
        next_area = 40u; area_handles = 1u; area_mappings = area_children = 0u;
        open_file_size = ASTRA_MEMORY_PAGE_SIZE;
        resize_result = failures[i];
        assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
            ASTRA_MEMORY_PAGE_SIZE, &area, &size) ==
            (i == 0u ? ASTRA_VFS_ERR_IO : ASTRA_VFS_ERR_LIMIT));
        assert(area == 0u && size == 0u && file_areas == NULL &&
            retained_count(101u) == 0u && area_duplicates == duplicates &&
            area_resizes == resizes + 1u && volume.cache_stats[CACHE_CREATES] == 0u);
        resize_result = ASTRA_SYSCALL_OK;
    }
    file_pager_thread = file_pager_send = 0u;
}

static void test_file_area_publication(void)
{
    for (unsigned mode = 0u; mode < 5u; ++mode) {
        HostfsVolume *volume = calloc(1u, sizeof(*volume));
        HostfsVolume *other = calloc(1u, sizeof(*other));
        assert(volume != NULL && other != NULL && file_areas == NULL);
        HostfsGrant first = {.owner = volume, .port.id = 1u};
        HostfsGrant second = {.owner = mode == 4u ? other : volume, .port.id = 2u};
        competing_grant = mode == 0u ? &first : &second;
        competing_base = mode == 3u ? ASTRA_AREA_SIZE_MAX : 0u;
        open_file_identity_valid = mode != 2u;
        open_file_size = ASTRA_AREA_SIZE_MAX + ASTRA_MEMORY_PAGE_SIZE;
        next_area = 40u; area_handles = 2u; area_mappings = 1u;
        file_pager_thread = 88u; file_pager_send = 99u;
        area_bind_observer = publish_competing_area;
        uint32_t area = 0u;
        uint64_t size = 0u;
        assert(hostfs_file_area(NULL, &first.port, 1u, 101u, 1234u, 0u,
            ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK);
        assert(area_bind_observer == NULL && size == open_file_size);
        if (mode < 2u) {
            assert(area == competing_area && file_areas->next == NULL &&
                volume->cache_stats[CACHE_CREATES] == 1u &&
                retained_count(101u) + retained_count(102u) == 1u);
        } else {
            assert(area != competing_area && file_areas->next != NULL &&
                file_areas->next->next == NULL &&
                retained_count(101u) == 1u && retained_count(102u) == 1u);
        }
        area_handles = 1u; area_mappings = 0u;
        file_areas_maintain(1);
        assert(file_areas == NULL && retained_count(101u) == 0u &&
            retained_count(102u) == 0u && !usage_locked);
        free(other); free(volume);
    }
    competing_grant = NULL;
    file_pager_thread = file_pager_send = 0u;
    open_file_identity_valid = 1u;
}

static void test_write_boundary(void)
{
    HostfsVolume volume = {0};
    volume.backend.owner = &volume;
    volume.backend.provider.session = 33u;
    HostfsGrant grant = {.owner = &volume, .backend = &volume.backend, .port.id = 1u};
    volume.backend.grant = &grant;
    hostfs_backend_ops_init(&volume.ops);
    AstraVfsNodeInfo info = {0};
    assert(file_areas == NULL);
    memset(vm_pages, 0, sizeof(vm_pages));
    memset(backing_bytes, 0, sizeof(backing_bytes));
    memcpy(backing_bytes, "old", 3u); open_file_size = 3u;
    open_file_flags = ASTRA_VFS_OPEN_WRITE;
    assert(stat_open_file(&volume.backend, 55u, &info) == ASTRA_VFS_OK);
    AstraVfsOpenFile file = {.backend = {.ops = &volume.ops,
        .context = &volume.backend}, .node = 55u, .flags = ASTRA_VFS_OPEN_WRITE};
    file.io = &file.io_storage;
    file.io_storage.active = &file; file.io_storage.identity = info.meta;
    file_pager_thread = 88u; file_pager_send = 99u;
    next_area = 40u; area_handles = 1u; area_mappings = area_children = 0u;
    area_call_session = 101u; area_call_file = 1234u;
    writes = page_reads = 0u;
    uint32_t moved, reclaims = area_reclaims;
    uint64_t position = 0u;
    AstraVfsFile owned;
    assert(astra_vfs_service_retain_active(&volume.service, &file, &owned) == ASTRA_VFS_OK);
    assert(hostfs_write(NULL, &file, &owned, 7u, 0u, 99u, "x", 1u,
        &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 1u && position == 8u && owned == 0u && writes == 0u &&
        page_reads == 1u && open_file_size == 3u && area_reclaims == reclaims);
    assert(area_extents[0] == 8u);
    uint8_t bytes[8];
    assert(hostfs_read(NULL, &file, 0u, 0u, bytes, sizeof(bytes), &moved, &position) == ASTRA_VFS_OK &&
        moved == sizeof(bytes) && memcmp(bytes, "old\0\0\0\0x", sizeof(bytes)) == 0 && writes == 0u);
    page_publish_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
    assert(hostfs_write(NULL, &file, &owned, ASTRA_MEMORY_PAGE_SIZE - 1u, 0u, UINT64_MAX,
        "yz", 2u, &moved, &position) == ASTRA_VFS_ERR_LIMIT && moved == 1u &&
        position == ASTRA_MEMORY_PAGE_SIZE && file.io->cache->size == ASTRA_MEMORY_PAGE_SIZE);
    page_publish_result = 0u;
    assert(hostfs_write(NULL, &file, &owned, 7u, 0u, 99u, "", 0u,
        &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 0u && position == 7u && owned == 0u &&
        area_reclaims == reclaims);
    file.io->active = NULL;
    write_result = ASTRA_VFS_ERR_IO;
    file_areas_writeback(16u);
    assert(file.io->cache->error == ASTRA_VFS_ERR_IO && open_file_size == 3u &&
        vm_pages[vm_page(40u, 0u, 0)].dirty);
    file.io->active = &file;
    open_file_size = 4u;
    assert(hostfs_read(NULL, &file, 0u, 0u, bytes, sizeof(bytes), &moved, &position) ==
        ASTRA_VFS_ERR_BUSY && moved == 0u && file.io->cache->error == ASTRA_VFS_ERR_IO &&
        vm_pages[vm_page(40u, 0u, 0)].dirty);
    open_file_size = 3u;
    file.io->active = NULL;
    file_areas_maintain(1);
    assert(file_areas != NULL && file.io->cache != NULL);
    assert(finish_hostfs() == ASTRA_VFS_ERR_IO && file_areas != NULL && file.io->cache != NULL);
    write_result = 0u;
    file_pager_failed = ASTRA_SYSCALL_RESOURCE_LIMIT;
    uint32_t wakes = pager_wakes;
    assert(file_pager_check() == ASTRA_SYSCALL_OK &&
        file_pager_failed == 0u && pager_wakes == wakes + 1u);
    assert(file_areas == NULL && file.io->cache == NULL && volume.service.cached_nodes == 0u &&
        retained_count(101u) == 0u && open_file_size == ASTRA_MEMORY_PAGE_SIZE &&
        memcmp(backing_bytes, "old\0\0\0\0x", sizeof(bytes)) == 0 &&
        backing_bytes[ASTRA_MEMORY_PAGE_SIZE - 1u] == 'y');
    file.io->active = &file;
    assert(astra_vfs_service_retain_active(&volume.service, &file, &owned) == ASTRA_VFS_OK);
    assert(hostfs_write(NULL, &file, &owned, 1u, 0u, 99u, "x", 1u, &moved, &position) == ASTRA_VFS_OK);
    file.io->active = NULL;
    file_areas_maintain(1);
    assert(file_areas != NULL && file.io->cache != NULL && backing_bytes[1u] == 'l');
    transport.generation = 9u;
    assert(finish_hostfs() == ASTRA_VFS_OK && file_areas == NULL && file.io->cache == NULL &&
        retained_count(101u) == 0u && backing_bytes[1u] == 'x');
    file.io->active = &file;
    assert(astra_vfs_service_retain_active(&volume.service, &file, &owned) == ASTRA_VFS_OK);
    assert(hostfs_write(NULL, &file, &owned, 1u, 0u, UINT64_MAX, "y", 1u,
        &moved, &position) == ASTRA_VFS_OK);
    ++open_file_size;
    assert(hostfs_read(NULL, &file, 0u, 0u, bytes, sizeof(bytes), &moved, &position) ==
        ASTRA_VFS_ERR_BUSY && moved == 0u && file.io->cache->error == ASTRA_VFS_ERR_BUSY &&
        vm_pages[vm_page(file_areas->area, 0u, 0)].dirty);
    --open_file_size;
    file.io->active = NULL;
    assert(finish_hostfs() == ASTRA_VFS_OK && file_areas == NULL && file.io->cache == NULL &&
        backing_bytes[1u] == 'y');
    memset(vm_pages, 0, sizeof(vm_pages));
    open_file_flags = ASTRA_VFS_OPEN_READ;
    file_pager_thread = file_pager_send = 0u;
}

static void test_reclaim_keeps_active_node(void)
{
    HostfsVolume *volume = calloc(1u, sizeof(*volume));
    assert(volume != NULL && file_areas == NULL);
    HostfsGrant grant = {.owner = volume, .port.id = 1u};
    uint32_t area = 0u;
    uint64_t size;
    next_area = 40u; area_handles = 2u; area_mappings = 1u;
    file_pager_thread = 88u; file_pager_send = 99u;
    open_file_identity_valid = 1u;
    assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK);
    HostfsFileArea *entry = file_areas;
    AstraVfsOpenFile active = {0};
    AstraVfsFileIO io = {.active = &active};
    entry->io = &io;
    uint32_t queried = area_reference_queries, released = retained_releases;
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == entry && retained_count(101u) == 1u &&
        retained_releases == released && area_reference_queries == queried);
    io.active = NULL;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u &&
        retained_releases == released + 1u);
    file_pager_thread = file_pager_send = 0u;
    free(volume);
}

static void test_shared_write_authority_precedes_area_publication(void)
{
    HostfsVolume volume = {0};
    HostfsGrant grant = {.owner = &volume, .port.id = 1u};
    uint32_t area = 0u;
    uint64_t size = 0u;
    assert(file_areas == NULL && retained_count(101u) == 0u);
    next_area = 40u; area_handles = 2u; area_mappings = 1u; area_children = 0u;
    file_pager_thread = 88u; file_pager_send = 99u;
    open_file_size = ASTRA_MEMORY_PAGE_SIZE;
    open_file_flags = ASTRA_VFS_OPEN_READ;
    assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK);
    assert(file_areas->io->cache == NULL && retained_count(101u) == 1u);
    open_file_flags |= ASTRA_VFS_OPEN_WRITE;
    expected_area_rights |= ASTRA_RIGHT_WRITE;
    prepare_failure = ASTRA_VFS_ERR_BUSY;
    uint32_t duplicates = area_duplicates;
    assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_ERR_BUSY && area == 0u &&
        area_duplicates == duplicates && retained_count(101u) == 1u);
    prepare_failure = 0u;
    assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK);
    assert(file_areas->io->cache != NULL && file_areas->io->cache->writer != 0u &&
        retained_count(101u) == 2u);
    open_file_flags = ASTRA_VFS_OPEN_READ;
    expected_area_rights &= ~ASTRA_RIGHT_WRITE;
    ++open_file_size;
    assert(hostfs_file_area(NULL, &grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &area, &size) == ASTRA_VFS_OK);
    assert(file_areas->io->cache->external_writes && size == open_file_size);
    file_areas_maintain(1);
    assert(file_areas != NULL && retained_count(101u) == 2u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    file_pager_thread = file_pager_send = 0u;
}

int main(void)
{
    pager_pressure_fixture = 1;
    file_pager.receive = 77u;
    file_pager_run(0u);
    assert(pager_pumps == 2u && pager_waits == 1u && file_pager_failed == 0u);
    assert(file_pager_check() == ASTRA_SYSCALL_OK && pager_wakes == 0u);
    file_pager_failed = ASTRA_SYSCALL_IO_ERROR;
    assert(file_pager_check() == ASTRA_SYSCALL_IO_ERROR && pager_wakes == 0u);
    file_pager_failed = 0u;
    pager_pressure_fixture = 0;
    file_pager = (AstraVfsFilePager){0};
    file_pager_stop = 0u;
    assert(hostfs_cache_ops.read != NULL && hostfs_cache_ops.sync != NULL);
    test_shutdown_preserves_admission();
    test_shutdown_failure_and_retry();
    test_shutdown_grant_retry();
    test_backend_bindings();
    test_grant_admission_pressure();
    test_native_file_usage();
    test_native_image_lease();
    HostfsVolume change_owner = {0};
    HostfsGrant change_grant = {.owner = &change_owner};
    HostfsFileArea other = {.owner = &change_owner, .grant = &change_grant, .source.cookie = 1u,
        .source.identity = {.valid = ASTRA_VFS_META_VOLUME |
            ASTRA_VFS_META_INODE | ASTRA_VFS_META_GENERATION,
            .volume = 1u, .inode = 999u}};
    HostfsFileArea changed = {.owner = &change_owner, .next = &other, .grant = &change_grant, .source.cookie = 2u,
        .source.identity = {.valid = ASTRA_VFS_META_VOLUME |
            ASTRA_VFS_META_INODE | ASTRA_VFS_META_GENERATION,
            .volume = 1u, .inode = 1234u}};
    file_areas = &changed;
    changed_inode = 1234u;
    node_sequence = 1u;
    assert(file_areas_observe_nodes(&change_owner, 1u));
    assert(changed.stale && !other.stale);
    changed.stale = 0u;
    changed_kind = ASTRA_HOST_FS_CHANGE_ENTRY_CREATED;
    node_sequence = 2u;
    assert(file_areas_observe_nodes(&change_owner, 2u));
    assert(!changed.stale && !other.stale);
    change_gap = 1;
    node_sequence = 3u;
    assert(file_areas_observe_nodes(&change_owner, 3u));
    assert(changed.stale && other.stale);
    changed.stale = other.stale = 0u;
    assert(file_areas_observe_nodes(&change_owner, 4u));
    assert(changed.stale && other.stale);
    change_gap = 0;
    changed_kind = ASTRA_HOST_FS_CHANGE_NODE;
    node_sequence = 32u;
    assert(file_areas_observe_nodes(&change_owner, node_sequence));
    assert(changed.stale && other.stale &&
           change_owner.observed_cache_nodes == node_sequence &&
           change_owner.observed_cache_sequence < node_sequence);
    /* Newly filled pages must survive queued events already reconciled by
       the full invalidation at the bounded-drain boundary. */
    changed.stale = other.stale = 0u;
    changed.check_links = other.check_links = 0u;
    change_gap = 1;
    assert(file_areas_observe_nodes(&change_owner, node_sequence));
    assert(!changed.stale && !other.stale && changed.check_links && other.check_links);
    change_gap = 0;
    /* Exercise the same covered snapshot with exact records still queued. */
    change_owner.observed_cache_sequence = 8u;
    while (change_owner.observed_cache_sequence < node_sequence) {
        uint64_t previous = change_owner.observed_cache_sequence;
        assert(file_areas_observe_nodes(&change_owner, node_sequence));
        assert(!changed.stale && !other.stale &&
               change_owner.observed_cache_nodes == node_sequence &&
               change_owner.observed_cache_sequence > previous);
    }
    node_sequence = 33u;
    change_gap = 1;
    assert(file_areas_observe_nodes(&change_owner, 32u));
    assert(changed.stale && other.stale && change_owner.observed_cache_nodes == 33u);
    changed.stale = other.stale = 0u;
    change_gap = 0;
    node_sequence = 34u;
    assert(file_areas_observe_nodes(&change_owner, 34u));
    assert(changed.stale && !other.stale &&
           change_owner.observed_cache_nodes == 34u);
    changed.stale = other.stale = 0u;
    change_next_failure = ASTRA_VFS_ERR_IO;
    assert(file_areas_observe_nodes(&change_owner, node_sequence));
    assert(changed.stale && other.stale);
    change_next_failure = 0u;
    /* A provider restart or generation rollover begins a fresh stream. */
    node_sequence = 0u;
    assert(file_areas_observe_nodes(&change_owner, 0u));
    assert(changed.stale && other.stale &&
           change_owner.observed_cache_nodes == 0u &&
           change_owner.observed_cache_sequence == 0u);
    changed.stale = other.stale = 0u;
    node_sequence = 1u;
    assert(file_areas_observe_nodes(&change_owner, 1u));
    assert(changed.stale && !other.stale &&
           change_owner.observed_cache_nodes == 1u);
    /* Publishing an old notification, including a broker retry, cannot
       invalidate pages filled after cache reconciliation covered it. */
    changed.stale = other.stale = 0u;
    change_owner.service_ready = 1u;
    volumes = &change_owner;
    filesystem_publish_result = ASTRA_ERROR_BUSY;
    publish_filesystem_changes(81u);
    assert(filesystem_publications == 1u &&
        change_owner.published_change_sequence == 0u && !changed.stale && !other.stale);
    filesystem_publish_result = ASTRA_OK;
    publish_filesystem_changes(81u);
    assert(filesystem_publications == 2u &&
        change_owner.published_change_sequence == 1u && !changed.stale && !other.stale);
    change_gap = 1;
    node_sequence = 2u;
    assert(file_areas_observe_nodes(&change_owner, node_sequence));
    assert(changed.stale && other.stale);
    changed.stale = other.stale = 0u;
    publish_filesystem_changes(81u);
    assert(change_owner.published_change_sequence == 2u && !changed.stale && !other.stale);
    change_gap = 0;
    node_sequence = 3u;
    publish_filesystem_changes(81u);
    assert(changed.stale && !other.stale);
    changed.stale = other.stale = 0u;
    snapshot_failure = ASTRA_VFS_ERR_IO;
    uint32_t publications = filesystem_publications;
    publish_filesystem_changes(81u);
    assert(changed.stale && other.stale && filesystem_publications == publications);
    snapshot_failure = 0u;
    volumes = NULL;
    file_areas = NULL;
    node_sequence = 0u;
    change_gap = 0;
    changed_kind = ASTRA_HOST_FS_CHANGE_NODE;
    changed_inode = 7u;
    /* A shared delegated root reports all actual HELLO owners, never the
       Supervisor creation requester. Duplicate sessions do not duplicate a
       process, and a page can stop inside the shared root's holder list. */
    HostfsVolume *shared_owner = calloc(1u, sizeof(*shared_owner));
    HostfsGrant *shared_grant = calloc(1u, sizeof(*shared_grant));
    assert(shared_owner != NULL && shared_grant != NULL);
    shared_grant->owner = shared_owner; shared_grant->session = 33u;
    shared_grant->port.id = 34u; shared_grant->requester = 1u;
    shared_owner->service.sessions = shared_owner->sessions;
    shared_owner->service.session_capacity = 5u;
    const uint32_t owners[] = {11u, 7u, 9u, 9u, 13u};
    for (uint32_t at = 0u; at < 5u; ++at)
        shared_owner->sessions[at] = (AstraVfsSessionSlot){.id = at + 1u,
            .owner = owners[at], .grant = at == 4u ? 99u : 34u};
    HostfsGrant *shared_table[] = {shared_grant};
    grants = shared_table; grant_count = 1u; transport.generation = 9u;
    AstraVolumeUsageQuery query = {.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query)};
    AstraVolumeUsagePage *page = malloc(sizeof(*page));
    assert(page != NULL && hostfs_read_usage(&query, page) == ASTRA_VFS_OK);
    assert(astra_volume_usage_page_valid(&query, page) && page->count == 2u && page->next_process == 9u &&
        page->records[0].process == 7u && page->records[1].process == 9u &&
        page->records[0].handle == 33u && page->records[1].handle == 33u);
    query.volume = page->volume; query.sequence = page->sequence; query.catalog_sequence = page->catalog_sequence;
    query.after = page->next; query.after_kind = page->next_kind; query.after_process = page->next_process;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && astra_volume_usage_page_valid(&query, page));
    assert(page->count == 2u && page->next == 0u && page->records[0].process == 11u &&
        page->records[0].kind == ASTRA_VOLUME_USAGE_ROOT && page->records[1].kind == ASTRA_VOLUME_USAGE_SHARE);
    hostfs_holders_changed(NULL); usage_notification_failure = ASTRA_VFS_ERR_IO;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_IO && usage_holders_changed != 0u);
    usage_notification_failure = 0u;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_BUSY && usage_holders_changed == 0u && usage_notifications == 1u);
    memset(shared_owner->sessions, 0, sizeof(shared_owner->sessions));
    hostfs_holders_changed(NULL);
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query)};
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && astra_volume_usage_page_valid(&query, page) &&
        page->count == 2u && page->records[0].kind == ASTRA_VOLUME_USAGE_ROOT && page->records[0].process == 0u);
    shared_grant->retiring = 1u; hostfs_holders_changed(NULL);
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && astra_volume_usage_page_valid(&query, page) &&
        (page->records[0].flags & (ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_CLEANUP_PENDING)) ==
            (ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_CLEANUP_PENDING));
    allocation_failed = 1;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_LIMIT);
    allocation_failed = 0;
    free(page); free(shared_grant); free(shared_owner); grants = NULL; grant_count = 0u;
    /* Every failing retirement keeps the canonical owner charged. A lost
       scope/session is already retired; no numeric handle is reused. */
    HostfsVolume retiring_owner = {0};
    HostfsGrant *retiring_grant = calloc(1u, sizeof(*retiring_grant));
    assert(retiring_grant != NULL);
    retiring_grant->owner = &retiring_owner;
    retiring_grant->port.receive = 32u;
    retiring_grant->port.id = 34u; retiring_grant->session = 33u;
    HostfsGrant *retiring_table[1] = {retiring_grant};
    grants = retiring_table; grant_count = 1u; transport.generation = 9u;
    assert(grant_backend_create(retiring_grant, 9u) == ASTRA_VFS_OK);
    AstraVfsBackend retiring_backend = {0};
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_acquire(&retiring_owner, 7u, 34u, &retiring_backend) == ASTRA_VFS_OK);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_live(34u));
    handle_close_failure = ASTRA_SYSCALL_IO_ERROR;
    assert(!grant_retire(0u) && grant_count == 1u && !grant_live(34u) &&
        retiring_grant->port.receive == 32u && released_grants == 1u);
    handle_close_failure = ASTRA_SYSCALL_INVALID_HANDLE;
    assert(!grant_retire(0u) && retiring_grant->port.receive == 0u &&
        retiring_grant->session == 33u && retiring_grant->backing_references == 1u &&
        !((HostfsBackend *)retiring_backend.context)->provider.lost);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    hostfs_backend_release(&retiring_owner, &retiring_backend);
    astra_vfs_state_lock_release(&state_lock);
    session_close_failure = ASTRA_VFS_ERR_IO;
    assert(!grant_retire(0u) && retiring_grant->session == 33u && released_grants == 1u);
    session_close_failure = ASTRA_VFS_ERR_LOST;
    assert(grant_retire(0u) && grant_count == 0u && retiring_table[0] == NULL);
    grants = NULL; session_close_failure = handle_close_failure = 0u;
    HostfsVolume mapped_owner = {0};
    mapped_owner.backend.owner = &mapped_owner;
    HostfsGrant mapped_grant = {.owner = &mapped_owner, .session = 33u, .port.id = 1u,
        .backend = &mapped_owner.backend};
    HostfsGrant another_grant = {.owner = &mapped_owner, .session = 33u, .port.id = 2u};
    uint32_t mapped_area = 0u;
    uint64_t mapped_size = 0u;
    uint32_t releases_before_mapping = retained_releases;
    file_pager_thread = 88u; file_pager_send = 99u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 2u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_ACCESS);
    assert(file_areas == NULL && retained_count(101u) == 0u && mapped_area == 0u);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == 2u * ASTRA_MEMORY_PAGE_SIZE &&
           retained_count(101u) == 1u && area_binds == 1u);
    assert(area_extents[0] == mapped_size);
    assert(file_areas->source.base == 0u &&
           file_areas->source.span == ASTRA_AREA_SIZE_MAX);
    mapped_owner.backend.provider.session = 33u;
    uint32_t references_before_hit = area_reference_queries;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u,
        ASTRA_MEMORY_PAGE_SIZE, ASTRA_MEMORY_PAGE_SIZE, &mapped_area,
        &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && retained_count(101u) == 1u &&
           area_binds == 1u && area_reference_queries == references_before_hit);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u,
        ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE,
        2u * ASTRA_MEMORY_PAGE_SIZE, &mapped_area,
        &mapped_size) == ASTRA_VFS_ERR_INVALID);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1235u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(retained_count(101u) == 1u && area_duplicates == 3u &&
           retained_releases == releases_before_mapping);
    assert(hostfs_file_area(NULL, &another_grant.port, 2u, 102u, 2234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == 2u * ASTRA_MEMORY_PAGE_SIZE &&
           retained_count(101u) == 1u && retained_count(102u) == 0u &&
           area_binds == 1u && area_duplicates == 4u && retained_releases == releases_before_mapping);
    open_file_size = ASTRA_MEMORY_PAGE_SIZE;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1235u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == ASTRA_MEMORY_PAGE_SIZE &&
           retained_count(101u) == 1u && area_binds == 1u &&
           area_duplicates == 5u && area_reclaims == 1u &&
           file_areas->cache.backing.size == ASTRA_MEMORY_PAGE_SIZE);
    open_file_size = ASTRA_AREA_SIZE_MAX + ASTRA_MEMORY_PAGE_SIZE;
    uint32_t reclaims_before_growth = area_reclaims;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1235u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && mapped_size == open_file_size &&
           retained_count(101u) == 2u && area_binds == 2u &&
           area_duplicates == 6u && file_areas->next->source.base == ASTRA_AREA_SIZE_MAX &&
           area_reclaims == reclaims_before_growth + 1u);
    HostfsVolume other_owner = {0};
    HostfsGrant other_grant = {.owner = &other_owner, .session = 33u, .port.id = 3u};
    assert(hostfs_file_area(NULL, &other_grant.port, 3u, 103u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 45u && retained_count(103u) == 1u &&
           area_binds == 3u && area_duplicates == 7u);
    {
        AstraVfsNodeInfo old;
        assert(stat_open_file(&mapped_owner.backend, 55u, &old) == ASTRA_VFS_OK);
        AstraVfsFileCache conflict = {.backing = old, .size = old.size, .writer = 1234u};
        unsigned page = vm_page(42u, 0u, 1);
        vm_pages[page].dirty = 1u;
        file_areas->io->cache = &conflict;
        open_file_size = 1u;
        uint32_t resizes = area_resizes;
        assert(astra_vfs_service_stat_retained(&mapped_owner.service,
            file_areas->source.retained, &old) == ASTRA_VFS_ERR_BUSY);
        /* A dirty later chunk must not truncate private copies in an earlier one. */
        assert(area_resizes == resizes && area_extents[0] == ASTRA_AREA_SIZE_MAX &&
            area_extents[1] == ASTRA_MEMORY_PAGE_SIZE && vm_pages[page].dirty &&
            conflict.error == ASTRA_VFS_ERR_BUSY);
        vm_pages[page].dirty = 0u;
        file_areas->io->cache = NULL;
        open_file_size = ASTRA_AREA_SIZE_MAX + ASTRA_MEMORY_PAGE_SIZE;
    }
    mapped_owner.backend.provider.session = 33u;
    uint32_t moved = 0u;
    uint64_t position = 0u;
    hostfs_backend_ops_init(&mapped_owner.ops);
    uint32_t reclaims_before_backing = area_reclaims;
    assert(mapped_owner.ops.write(&mapped_owner.backend, 55u, 0u, 0u, 0u,
        "x", 1u, &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 1u && position == 1u &&
           area_reclaims == reclaims_before_backing);
    uint32_t before_truncate = area_reclaims;
    assert(mapped_owner.ops.truncate(&mapped_owner.backend, 55u, 1u) == ASTRA_VFS_OK);
    /* The native core owns truncate admission and invokes this same stat hook. */
    AstraVfsNodeInfo resized;
    assert(astra_vfs_service_stat_retained(&mapped_owner.service,
        file_areas->source.retained, &resized) == ASTRA_VFS_OK);
    assert(truncates == 1u && area_reclaims == before_truncate + 2u);
    assert(area_extents[0] == 1u && area_extents[1] == 0u &&
        area_extents[2] == ASTRA_AREA_SIZE_MAX);
    assert(mapped_owner.ops.truncate(&mapped_owner.backend, 55u, 1u) == ASTRA_VFS_OK);
    assert(astra_vfs_service_stat_retained(&mapped_owner.service,
        file_areas->source.retained, &resized) == ASTRA_VFS_OK);
    assert(area_reclaims == before_truncate + 2u && !file_areas->next->stale);
    file_areas_observe_nodes(&mapped_owner, 0u);
    assert(!file_areas->stale && !file_areas->next->stale &&
           !file_areas->next->next->stale);
    file_areas_observe_nodes(&mapped_owner, 1u);
    assert(mapped_owner.observed_cache_nodes == 1u &&
           file_areas->stale && file_areas->next->stale &&
           !file_areas->next->next->stale);
    file_areas_maintain(1);
    assert(area_reclaims == before_truncate + 4u && !file_areas->stale &&
           !file_areas->next->stale && !file_areas->next->next->stale);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u &&
           retained_count(103u) == 0u && retained_releases == releases_before_mapping + 3u);
    node_sequence = 1u;
    next_area = 40u;
    area_handles = 2u; area_mappings = 1u;
    bound_provider_epoch = 50u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == 4u);
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_GAP, 0u));
    uint32_t before_gap = area_reclaims;
    file_areas_maintain(1);
    assert(!file_areas->stale && area_reclaims == before_gap + 1u);
    bound_provider_epoch = 51u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && area_binds == 5u &&
           retained_count(101u) == 2u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u &&
           retained_releases == releases_before_mapping + 5u);
    bound_provider_epoch = 0u;
    next_area = 40u;
    area_handles = 2u; area_mappings = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == 6u);
    node_sequence = 2u;
    uint32_t before_observed_change = area_reclaims;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(snapshot_calls >= 2u && area_reclaims == before_observed_change + 1u &&
           mapped_owner.observed_cache_nodes == 2u && mapped_area == 41u &&
           area_binds == 6u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    open_file_identity_valid = 0u;
    next_area = 46u;
    area_handles = 2u; area_mappings = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 47u && retained_count(101u) == 1u);
    assert(hostfs_file_area(NULL, &another_grant.port, 2u, 102u, 2234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 49u && retained_count(102u) == 1u);
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_NODE,
        999u) && file_areas->stale && file_areas->next->stale);
    file_areas_maintain(1);
    assert(!file_areas->stale && !file_areas->next->stale);
    uint32_t reclaimed_before = area_reclaims;
    assert(mapped_owner.ops.truncate(&mapped_owner.backend, 55u, 0u) == ASTRA_VFS_OK);
    for (HostfsFileArea *entry = file_areas; entry != NULL; entry = entry->next)
        assert(astra_vfs_service_stat_retained(&mapped_owner.service,
            entry->source.retained, &resized) == ASTRA_VFS_OK);
    assert(area_reclaims == reclaimed_before + 2u);
    assert(area_extents[3] == 0u && area_extents[4] == 0u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && retained_count(101u) == 0u &&
           retained_count(102u) == 0u);
    /* The following admission fixture needs two source windows again. */
    open_file_size = ASTRA_AREA_SIZE_MAX + ASTRA_MEMORY_PAGE_SIZE;
    open_file_identity_valid = 1u;
    open_file_flags = ASTRA_VFS_OPEN_WRITE;
    uint32_t closes_before_denied = retained_releases;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_ACCESS);
    assert(mapped_area == 0u && retained_releases == closes_before_denied &&
           file_areas == NULL && retained_count(101u) == 0u);
    open_file_flags = ASTRA_VFS_OPEN_READ;
    next_area = 40u;
    uint32_t retained_binds = area_binds;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas != NULL && retained_count(101u) == 1u);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == retained_binds + 1u);
    file_areas->pins = 1u;
    file_areas_maintain(1);
    assert(file_areas != NULL);
    file_areas->pins = 0u;
    mapped_grant.retiring = 1u;
    file_areas_maintain(0);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    mapped_grant.retiring = 0u;
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    retain_status = ASTRA_VFS_ERR_LIMIT;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && retained_count(101u) == 1u);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_LIMIT);
    assert(mapped_area == 0u && retained_count(101u) == 1u);
    retain_status = ASTRA_VFS_OK;
    area_limits = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && retained_count(101u) == 2u);
    file_areas->next->pins = 1u;
    file_areas_maintain(1);
    assert(file_areas != NULL && file_areas->next == NULL && retained_count(101u) == 1u);
    file_areas->pins = 1u;
    area_limits = 2u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_LIMIT);
    file_areas_maintain(1);
    assert(mapped_area == 0u && retained_count(101u) == 1u && file_areas->pins == 1u);
    file_areas->pins = 0u;
    release_wait_status = ASTRA_VFS_ERR_IO;
    file_areas_maintain(1);
    assert(file_areas != NULL && file_areas->source.retained != ASTRA_VFS_FILE_INVALID &&
        retained_count(101u) == 1u);
    release_wait_status = ASTRA_VFS_OK;
    file_close_status = ASTRA_VFS_ERR_IO;
    area_close_status = ASTRA_SYSCALL_IO_ERROR;
    file_areas_maintain(1);
    assert(file_areas != NULL && file_areas->source.retained == ASTRA_VFS_FILE_INVALID &&
        file_areas->grant == NULL && retained_count(101u) == 0u &&
        volume_referenced(&mapped_owner));
    file_close_status = ASTRA_VFS_OK;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 45u && retained_count(101u) == 1u);
    area_close_status = ASTRA_SYSCALL_OK;
    file_areas_maintain(0);
    assert(file_areas != NULL && file_areas->next == NULL && retained_count(101u) == 1u);
    file_areas_maintain(1);
    retain_status = ASTRA_VFS_ERR_LIMIT;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_LIMIT);
    assert(file_areas == NULL && mapped_area == 0u && retained_count(101u) == 0u);
    retain_status = ASTRA_VFS_OK;
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    session_open_limits = 1u;
    uint32_t opened_session = 0u;
    assert(hostfs_session_open(0u, NULL, "/", 0u, 0u, &opened_session) == ASTRA_VFS_OK);
    assert(opened_session == 33u && session_opens == 2u && file_areas == NULL &&
        retained_count(101u) == 0u);
    session_open_limits = 2u;
    assert(hostfs_session_open(0u, NULL, "/", 0u, 0u, &opened_session) == ASTRA_VFS_ERR_LIMIT);
    assert(opened_session == 0u && session_opens == 4u);
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED, 0u));
    open_file_links = 2u;
    file_areas_maintain(0);
    assert(file_areas != NULL && !file_areas->check_links);
    area_handles = 2u; area_mappings = 1u;
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_ENTRY_REMOVED, 0u));
    open_file_links = 0u;
    file_areas_maintain(0);
    assert(file_areas != NULL && file_areas->check_links);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(0);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    open_file_links = 1u;
    next_area = 40u;
    open_file_identity_valid = 0u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    open_file_identity_valid = 1u;
    open_file_links = 0u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas == NULL && retained_count(101u) == 0u);
    open_file_links = 1u;
    static const struct {
        uint32_t failure, failures, result, attempts;
    } duplicate_cases[] = {
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, ASTRA_VFS_OK, 2u},
        {ASTRA_SYSCALL_OUT_OF_MEMORY, 1u, ASTRA_VFS_OK, 2u},
        {ASTRA_SYSCALL_RESOURCE_LIMIT, 2u, ASTRA_VFS_ERR_LIMIT, 2u},
        {ASTRA_SYSCALL_IO_ERROR, 1u, ASTRA_VFS_ERR_IO, 1u},
    };
    for (uint32_t cached = 0u; cached < 2u; ++cached) {
        for (unsigned i = 0u; i < sizeof(duplicate_cases) / sizeof(duplicate_cases[0]); ++i) {
            next_area = 40u;
            assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u, 0u,
                ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
            if (cached)
                assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u,
                    ASTRA_AREA_SIZE_MAX, ASTRA_MEMORY_PAGE_SIZE, &mapped_area,
                    &mapped_size) == ASTRA_VFS_OK);
            duplicate_limits = duplicate_cases[i].failures;
            duplicate_status = duplicate_cases[i].failure;
            duplicate_attempts = 0u;
            assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 101u, 1234u,
                cached ? 0u : ASTRA_AREA_SIZE_MAX, ASTRA_MEMORY_PAGE_SIZE,
                &mapped_area, &mapped_size) == duplicate_cases[i].result);
            assert(duplicate_attempts == duplicate_cases[i].attempts && !usage_locked);
            if (duplicate_cases[i].result == ASTRA_VFS_OK) {
                assert(mapped_area == (cached ? 41u : 43u));
            } else {
                assert(mapped_area == 0u);
            }
            /* The active node keeps both canonical chunks through publication. */
            assert(file_areas != NULL && file_areas->next != NULL &&
                file_areas->next->next == NULL && file_areas->pins == 0u &&
                file_areas->next->pins == 0u && retained_count(101u) == 2u);
            duplicate_limits = 0u;
            file_areas_maintain(1);
            assert(file_areas == NULL && retained_count(101u) == 0u);
        }
    }
    file_pager_thread = file_pager_send = 0u;
    AstraMetricSample cache_samples[17u + CACHE_STAT_COUNT];
    assert(sample_vfs(&mapped_owner, NULL, 0u) == 17u + CACHE_STAT_COUNT);
    assert(sample_vfs(&mapped_owner, cache_samples, 17u + CACHE_STAT_COUNT) ==
        17u + CACHE_STAT_COUNT);
    assert(strcmp(cache_samples[17u + CACHE_HITS].name, "cache_hits") == 0 &&
        cache_samples[17u + CACHE_HITS].value != 0u &&
        cache_samples[17u + CACHE_CLOSES].value != 0u);
    HostfsGrant recovered = {.root = "/folder", .root_meta = {
        .valid = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                 ASTRA_VFS_META_GENERATION,
        .volume = 55u, .inode = 1u, .generation = 123u}};
    AstraVfsNodeMeta same = recovered.root_meta;
    assert(grant_root_matches(&recovered, &same));
    ++same.inode;
    assert(!grant_root_matches(&recovered, &same));
    same = recovered.root_meta;
    same.valid &= ~ASTRA_VFS_META_GENERATION;
    assert(!grant_root_matches(&recovered, &same));
    same = recovered.root_meta;
    assert(grant_root_matches(&recovered, &same));
    ++same.generation;
    assert(!grant_root_matches(&recovered, &same));
    same = recovered.root_meta;
    ++same.volume;
    assert(!grant_root_matches(&recovered, &same));
    same = recovered.root_meta;
    same.volume = 0u;
    recovered.root_meta.volume = 0u;
    assert(!grant_root_matches(&recovered, &same));
    recovered.root_meta.volume = 55u;
    HostfsVolume first = {0}, second = {0};
    first.reference = (AstraVolumeReference){UINT64_MAX, UINT64_MAX, UINT64_MAX};
    assert(volume_metric_group(&first, "/ram0") && strcmp(first.metric_group, "hostfs.vfs.ram0") == 0);
    first.dynamic = second.dynamic = 1u;
    second.reference = first.reference;
    --second.reference.generation;
    assert(volume_metric_group(&first, "/usb0") && volume_metric_group(&second, "/usb0"));
    assert(strlen(first.metric_group) < ASTRA_METRIC_NAME_MAX && strcmp(first.metric_group, second.metric_group) != 0);
    AstraMetricSample samples[17u + CACHE_STAT_COUNT];
    first.next = &second;
    first.service.stats.requests = 3u;
    second.service.stats.requests = 5u;
    first.service.stats.peak_open_files = 100u;
    second.service.stats.peak_open_files = 9u;
    first.service.stats.last_failed_status = ASTRA_VFS_ERR_IO;
    second.service.stats.last_failed_status = ASTRA_VFS_ERR_ACCESS;
    volumes = &first;
    assert(sample_vfs_totals(NULL, NULL, 0u) == 12u);
    assert(sample_vfs_totals(NULL, samples, 17u) == 12u && samples[0].value == 8u);
    assert(sample_vfs(&first, samples, 17u + CACHE_STAT_COUNT) == 17u + CACHE_STAT_COUNT && samples[12].value == 100u &&
           samples[15].value == ASTRA_VFS_ERR_IO);
    assert(sample_vfs(&second, samples, 17u + CACHE_STAT_COUNT) == 17u + CACHE_STAT_COUNT && samples[12].value == 9u &&
           samples[15].value == ASTRA_VFS_ERR_ACCESS);
    volumes = NULL;
    AstraVfsHostBackend root = {0};
    settings(1024u, 64u, 16u, "");
    assert(configured_limits(&root) == ASTRA_STATUS_OK);
    assert(owner_files == 1024u && owner_sessions == 64u && queue_messages == 16u);
    for (unsigned index = 0u; index < 3u; ++index) {
        uint64_t values[] = {1024u, 64u, 16u};
        values[index] = 0u; settings(values[0], values[1], values[2], "");
        assert(configured_limits(&root) == ASTRA_STATUS_INVALID);
        values[index] = UINT64_C(1) << 32; settings(values[0], values[1], values[2], "");
        assert(configured_limits(&root) == ASTRA_STATUS_INVALID);
        assert(owner_files == 1024u && owner_sessions == 64u && queue_messages == 16u);
    }
    settings(1024u, 64u, ASTRA_PORT_MESSAGES_MAX, "");
    assert(configured_limits(&root) == ASTRA_STATUS_OK);
    settings(1024u, 64u, ASTRA_PORT_MESSAGES_MAX + 1u, "");
    assert(configured_limits(&root) == ASTRA_STATUS_INVALID);
    settings(1024u, 64u, 16u, "owner_files 2\n");
    assert(configured_limits(&root) == ASTRA_STATUS_INVALID);
    strcpy(document, "astra-config 1\nschema 1\nowner_files 2\nqueue_messages 16\n");
    assert(configured_limits(&root) == ASTRA_STATUS_INVALID);
    settings(1024u, 64u, 16u, "");
    read_status = ASTRA_VFS_ERR_LOST;
    assert(configured_limits(&root) == ASTRA_VFS_ERR_LOST);
    read_status = ASTRA_VFS_OK; allocation_failed = 1;
    assert(configured_limits(&root) == ASTRA_STATUS_LIMIT);
    allocation_failed = 0;
    test_file_area_extent_failure_keeps_source_unpublished();
    test_file_area_publication();
    test_reclaim_keeps_active_node();
    test_write_boundary();
    test_shared_write_authority_precedes_area_publication();
    return 0;
}
