#define main hostfs_unused_main
#include "../main.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static char document[512];
static uint32_t read_status;
static int allocation_failed;
static uint32_t revoke_failure, session_close_failure, handle_close_failure, released_grants;
static uint64_t usage_sequence = 91u;
static uint32_t usage_notification_failure, usage_notifications;
static int usage_locked;
static uint32_t area_handles = 2u, area_mappings = 1u, area_children, next_area = 40u;
static uint32_t open_file_size = ASTRA_MEMORY_PAGE_SIZE * 2u;
static uint32_t open_file_identity_valid = 1u;
static uint32_t open_file_generation = 11u;
static uint32_t open_file_links = 1u;
static uint32_t file_closes, area_reclaims, area_binds, area_duplicates;
static uint32_t area_reference_queries;
static uint32_t adopt_limits, area_limits, area_close_status, file_close_status;
static uint32_t session_open_limits, session_opens;
static uint32_t scope_create_limits, scope_creates, port_creates, port_pressure_status;
static uint32_t writes, truncates, opens;
static uint32_t open_file_flags = ASTRA_VFS_OPEN_READ;
static uint64_t node_sequence;
static uint64_t changed_inode = 7u;
static int change_gap;
static uint32_t changed_kind = ASTRA_HOST_FS_CHANGE_NODE;
static uint32_t snapshot_calls;
static uint32_t snapshot_failure;
static uint32_t change_next_failure;
static AstraResult filesystem_publish_result;
static uint32_t filesystem_publications;
uint32_t astra_log(const char *text) { (void)text; return ASTRA_SYSCALL_OK; }
uint64_t astra_clock_monotonic(void) { return 1u; }
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
    if (query->sequence != 0u && query->sequence != usage_sequence) return ASTRA_VFS_ERR_BUSY;
    memset(page, 0, sizeof(*page));
    page->version = ASTRA_VOLUME_USAGE_VERSION; page->size = sizeof(*page);
    page->volume = (AstraVolumeReference){55u, query->volume.id, query->volume.generation};
    page->sequence = usage_sequence; page->catalog_sequence = 31u;
    const uint32_t kinds[] = {ASTRA_VOLUME_USAGE_ROOT, ASTRA_VOLUME_USAGE_SHARE};
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
uint32_t astra_host_scope_revoke(uint32_t scope)
{ assert(scope == 31u); return revoke_failure; }
uint32_t astra_host_scope_create(uint32_t device, uint32_t word,
    uint32_t flags, uint32_t *scope)
{
    assert(device == 30u && word == 33u && flags == ASTRA_HOST_SCOPE_READ_ONLY);
    ++scope_creates; *scope = 0u;
    if (scope_create_limits != 0u) { --scope_create_limits; return ASTRA_SYSCALL_RESOURCE_LIMIT; }
    *scope = 31u; return ASTRA_SYSCALL_OK;
}
uint32_t astra_host_scope_limit(uint32_t scope, uint32_t files)
{ assert(scope == 31u && files == owner_files); return ASTRA_SYSCALL_OK; }
uint32_t astra_host_lease_query(uint32_t device, AstraHostLeaseInfo *info)
{ assert(device == 31u); *info = (AstraHostLeaseInfo){0}; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
    uint32_t *receive, uint32_t *send)
{
    assert(messages == queue_messages && bytes == messages * sizeof(AstraVfsRequestMessage));
    ++port_creates; *receive = *send = 0u;
    if (port_pressure_status != 0u && file_areas != NULL) return port_pressure_status;
    *receive = 32u; *send = 35u; return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle)
{ assert(handle == 31u || handle == 32u || handle == 35u || (handle >= 40u && handle <= 48u &&
         (handle & 1u) == 0u));
  return handle >= 40u ? area_close_status : handle_close_failure; }
uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *host, uint32_t generation, uint32_t session)
{ assert(host == &transport && generation == 9u && session == 33u); return session_close_failure; }
uint32_t astra_vfs_host_session_open(AstraVfsHostTransport *host,
    uint32_t generation, uint32_t parent, const AstraVolumeReference *volume,
    const char *root, uint16_t flags, uint32_t requester, uint32_t *session)
{
    assert(host == &transport && generation == 9u && parent == 0u &&
        volume == NULL && strcmp(root, "/") == 0 && flags == 0u && requester == 0u);
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
         rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER |
                    ASTRA_RIGHT_ADMINISTER) && flags == ASTRA_AREA_CREATE_RESERVED);
  if (area_limits != 0u) { --area_limits; return ASTRA_SYSCALL_RESOURCE_LIMIT; }
  *area = next_area; next_area += 2u; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_bind_pager(uint32_t area, uint32_t port, uint32_t cookie)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u &&
         port == 99u && cookie != 0u);
  ++area_binds; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_handle_duplicate(uint32_t source, uint32_t rights,
    uint32_t *duplicate)
{ assert(source >= 40u && source <= 48u && (source & 1u) == 0u &&
         rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                                    ASTRA_RIGHT_TRANSFER));
  *duplicate = source + 1u; ++area_duplicates; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_references(uint32_t area, uint32_t *handles,
    uint32_t *mappings, uint32_t *children)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u);
  ++area_reference_queries;
  *handles = area_handles; *mappings = area_mappings;
  *children = area_children; return ASTRA_SYSCALL_OK; }
uint32_t astra_rt_area_reclaim(uint32_t area, uint32_t offset,
    uint32_t size, uint32_t *released)
{ assert(area >= 40u && area <= 48u && (area & 1u) == 0u &&
         offset == 0u && size == ASTRA_AREA_SIZE_MAX);
  *released = 1u; ++area_reclaims; return ASTRA_SYSCALL_OK; }
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
  if (adopt_limits != 0u) { --adopt_limits; *node = 0u; return ASTRA_VFS_ERR_LIMIT; }
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
  assert(node == 55u && length == 1u); ++writes;
  *moved = length; *position = offset + length; return ASTRA_VFS_OK; }
static uint32_t truncate_open_file(void *context, uintptr_t node, uint64_t size)
{ (void)context; assert(node == 55u && size == 1u);
  ++truncates; return ASTRA_VFS_OK; }
static uint32_t open_truncated(void *context, const char *path, uint32_t flags,
    uint16_t mode, uintptr_t *node, AstraVfsNodeInfo *info)
{ (void)context; (void)path; (void)mode;
  assert((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u); ++opens;
  *node = 55u; return stat_open_file(context, *node, info); }
static uint32_t open_truncated_at(void *context, uintptr_t directory,
    const char *path, uint32_t flags, uint16_t mode, uintptr_t *node,
    AstraVfsNodeInfo *info)
{ assert(directory == 23u);
  return open_truncated(context, path, flags, mode, node, info); }
const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    static const AstraVfsBackendOps ops = {.stat = stat_node,
        .adopt = adopt_file, .stat_node = stat_open_file, .close = close_open_file,
        .read = read_open_file, .write = write_open_file,
        .file_flags = flags_open_file,
        .truncate = truncate_open_file, .open = open_truncated,
        .open_at = open_truncated_at};
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
    assert(grant_backend_create(&grant) == ASTRA_VFS_ERR_LIMIT && grant.backend == NULL);
    allocation_failed = 0;
    grant.backing_references = UINT32_MAX;
    assert(grant_backend_create(&grant) == ASTRA_VFS_ERR_LIMIT && grant.backend == NULL);
    grant.backing_references = 0u;
    assert(grant_backend_create(&grant) == ASTRA_VFS_OK && grant.backing_references == 1u);
    HostfsBackend *old = grant.backend;
    assert(old->owner == volume && old->grant == &grant && old->references == 1u);
    assert(grant_backend_create(&grant) == ASTRA_VFS_ERR_BUSY && grant.backend == old &&
        grant.backing_references == 1u);
    AstraVfsBackend held = {0}, rejected = {0};
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_acquire(NULL, 7u, 34u, &rejected) == ASTRA_VFS_ERR_ACCESS);
    assert(hostfs_backend_acquire(volume, 7u, 35u, &rejected) == ASTRA_VFS_ERR_ACCESS);
    assert(hostfs_backend_acquire(volume, 7u, 34u, &held) == ASTRA_VFS_OK &&
        held.ops == &volume->ops && held.context == old && old->references == 2u);
    old->references = UINT32_MAX;
    assert(hostfs_backend_retain(volume, &held) == ASTRA_VFS_ERR_LIMIT);
    old->references = 2u;
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_detach(&grant) == ASTRA_VFS_OK && grant.backend == NULL &&
        old->provider.lost && old->references == 1u && grant.backing_references == 1u);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_retain(volume, &held) == ASTRA_VFS_ERR_LOST);
    assert(hostfs_backend_acquire(volume, 7u, 34u, &rejected) == ASTRA_VFS_ERR_LOST);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_create(&grant) == ASTRA_VFS_OK && grant.backend != old &&
        !grant.backend->provider.lost && grant.backing_references == 2u);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    hostfs_backend_release(volume, &held);
    assert(grant.backing_references == 1u);
    grant.retiring = 1u;
    assert(hostfs_backend_acquire(volume, 7u, 34u, &rejected) == ASTRA_VFS_ERR_LOST);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_backend_detach(&grant) == ASTRA_VFS_OK && grant.backing_references == 0u);
    assert(grant_backend_create(&grant) == ASTRA_VFS_ERR_LOST && grant.backend == NULL &&
        grant.backing_references == 0u);
    assert(grant_backend_detach(&grant) == ASTRA_VFS_OK && !usage_locked);
    grants = NULL; grant_count = 0u;
    free(volume);
}
static void test_grant_admission_pressure(void)
{
    static const struct {
        uint32_t scope_limits, port_status, handles, mappings, children, pins;
        uint32_t status, expected_scopes, expected_ports;
    } cases[] = {
        {0u, ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 0u, 0u, ASTRA_VFS_OK, 1u, 2u},
        {0u, ASTRA_SYSCALL_OUT_OF_MEMORY, 1u, 0u, 0u, 0u, ASTRA_VFS_OK, 1u, 2u},
        {0u, ASTRA_SYSCALL_RESOURCE_LIMIT, 2u, 0u, 0u, 0u, ASTRA_VFS_ERR_LIMIT, 1u, 2u},
        {0u, ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 1u, 0u, 0u, ASTRA_VFS_ERR_LIMIT, 1u, 2u},
        {0u, ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 1u, 0u, ASTRA_VFS_ERR_LIMIT, 1u, 2u},
        {0u, ASTRA_SYSCALL_RESOURCE_LIMIT, 1u, 0u, 0u, 1u, ASTRA_VFS_ERR_LIMIT, 1u, 2u},
        {0u, ASTRA_SYSCALL_IO_ERROR, 1u, 0u, 0u, 0u, ASTRA_VFS_ERR_IO, 1u, 1u},
        {1u, 0u, 1u, 0u, 0u, 0u, ASTRA_VFS_OK, 2u, 1u},
        {2u, 0u, 1u, 1u, 0u, 0u, ASTRA_VFS_ERR_LIMIT, 2u, 0u},
    };
    HostfsVolume *volume = calloc(1u, sizeof(*volume));
    assert(volume != NULL);
    const uint32_t saved_next = next_grant_id;
    device_handle = 30u; owner_files = 1024u; queue_messages = 16u;
    transport.generation = 9u;
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        HostfsGrant cached = {.owner = volume, .backing_references = 1u};
        HostfsGrant *table[4] = {0};
        grants = table; grant_capacity = 4u; grant_count = 0u; next_grant_id = 34u;
        file_areas = calloc(1u, sizeof(*file_areas));
        assert(file_areas != NULL);
        file_areas->grant = &cached; file_areas->area = 40u;
        file_areas->backend.session = 33u; file_areas->source.node = 55u;
        file_areas->source.span = ASTRA_AREA_SIZE_MAX;
        file_areas->pins = cases[i].pins;
        scope_create_limits = cases[i].scope_limits;
        port_pressure_status = cases[i].port_status;
        area_handles = cases[i].handles; area_mappings = cases[i].mappings;
        area_children = cases[i].children;
        scope_creates = port_creates = area_reference_queries = 0u;
        uint32_t send = 0u;
        assert(grant_add(volume, 33u, ASTRA_VFS_GRANT_READ, &volume->reference,
            "/", 7u, &send, NULL) == cases[i].status);
        assert(scope_creates == cases[i].expected_scopes && port_creates == cases[i].expected_ports);
        if (cases[i].status == ASTRA_VFS_OK) {
            assert(send == 35u && grant_count == 1u && file_areas == NULL &&
                cached.backing_references == 0u && grant_retire(0u));
            assert(astra_close(send) == ASTRA_SYSCALL_OK);
        } else {
            assert(send == 0u && grant_count == 0u && file_areas != NULL &&
                cached.backing_references == 1u && file_areas->source.node == 55u);
            assert(cases[i].port_status == ASTRA_SYSCALL_IO_ERROR ?
                area_reference_queries == 0u : area_reference_queries == 1u);
            area_handles = 1u; area_mappings = area_children = file_areas->pins = 0u;
            file_areas_maintain(1);
        }
        assert(file_areas == NULL && cached.backing_references == 0u && !usage_locked);
    }
    grants = NULL; grant_capacity = grant_count = 0u; next_grant_id = saved_next;
    port_pressure_status = scope_create_limits = device_handle = owner_files = queue_messages = 0u;
    file_closes = released_grants = usage_holders_changed = area_reference_queries = 0u;
    area_handles = 2u; area_mappings = 1u;
    free(volume);
}

int main(void)
{
    test_backend_bindings();
    test_grant_admission_pressure();
    HostfsVolume change_owner = {0};
    HostfsGrant change_grant = {.owner = &change_owner};
    HostfsFileArea other = {.grant = &change_grant,
        .source.identity = {.valid = ASTRA_VFS_META_VOLUME |
            ASTRA_VFS_META_INODE | ASTRA_VFS_META_GENERATION,
            .volume = 1u, .inode = 999u}};
    HostfsFileArea changed = {.next = &other, .grant = &change_grant,
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
    hostfs_sessions_changed(NULL); usage_notification_failure = ASTRA_VFS_ERR_IO;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_IO && usage_holders_changed != 0u);
    usage_notification_failure = 0u;
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_ERR_BUSY && usage_holders_changed == 0u && usage_notifications == 1u);
    memset(shared_owner->sessions, 0, sizeof(shared_owner->sessions));
    hostfs_sessions_changed(NULL);
    query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(query)};
    assert(hostfs_read_usage(&query, page) == ASTRA_VFS_OK && astra_volume_usage_page_valid(&query, page) &&
        page->count == 2u && page->records[0].kind == ASTRA_VOLUME_USAGE_ROOT && page->records[0].process == 0u);
    shared_grant->retiring = 1u; hostfs_sessions_changed(NULL);
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
    retiring_grant->port.scope = 31u; retiring_grant->port.receive = 32u;
    retiring_grant->port.id = 34u; retiring_grant->session = 33u;
    HostfsGrant *retiring_table[1] = {retiring_grant};
    grants = retiring_table; grant_count = 1u; transport.generation = 9u;
    assert(grant_backend_create(retiring_grant) == ASTRA_VFS_OK);
    AstraVfsBackend retiring_backend = {0};
    assert(astra_vfs_state_lock_acquire(&state_lock));
    assert(hostfs_backend_acquire(&retiring_owner, 7u, 34u, &retiring_backend) == ASTRA_VFS_OK);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_live(34u));
    revoke_failure = ASTRA_SYSCALL_IO_ERROR;
    assert(!grant_retire(0u) && grant_count == 1u && !grant_live(34u) &&
        retiring_grant->port.receive == 0u && retiring_grant->session == 33u && released_grants == 1u);
    revoke_failure = ASTRA_SYSCALL_OK; session_close_failure = ASTRA_VFS_ERR_IO;
    handle_close_failure = ASTRA_SYSCALL_IO_ERROR;
    assert(!grant_retire(0u) && grant_count == 1u && retiring_grant->scope_revoked &&
        retiring_grant->port.scope == 31u && retiring_grant->session == 33u && released_grants == 1u);
    handle_close_failure = ASTRA_SYSCALL_INVALID_HANDLE;
    assert(!grant_retire(0u) && retiring_grant->port.scope == 0u && retiring_grant->session == 33u);
    session_close_failure = ASTRA_VFS_ERR_LOST;
    assert(!grant_retire(0u) && retiring_grant->session == 33u &&
        retiring_grant->backing_references == 1u &&
        ((HostfsBackend *)retiring_backend.context)->provider.lost);
    assert(astra_vfs_state_lock_acquire(&state_lock));
    hostfs_backend_release(&retiring_owner, &retiring_backend);
    astra_vfs_state_lock_release(&state_lock);
    assert(grant_retire(0u) && grant_count == 0u && retiring_table[0] == NULL);
    grants = NULL; revoke_failure = session_close_failure = handle_close_failure = 0u;
    HostfsVolume mapped_owner = {0};
    mapped_owner.backend.owner = &mapped_owner;
    HostfsGrant mapped_grant = {.owner = &mapped_owner, .session = 33u,
        .backend = &mapped_owner.backend};
    HostfsGrant another_grant = {.owner = &mapped_owner, .session = 33u};
    uint32_t mapped_area = 0u;
    uint64_t mapped_size = 0u;
    file_pager_thread = 88u; file_pager_send = 99u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == 2u * ASTRA_MEMORY_PAGE_SIZE &&
           mapped_grant.backing_references == 1u && area_binds == 1u);
    assert(file_areas->source.base == 0u &&
           file_areas->source.span == ASTRA_AREA_SIZE_MAX);
    mapped_owner.backend.provider.session = 33u;
    uint32_t references_before_hit = area_reference_queries;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u,
        ASTRA_MEMORY_PAGE_SIZE, ASTRA_MEMORY_PAGE_SIZE, &mapped_area,
        &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_grant.backing_references == 1u &&
           area_binds == 1u && area_reference_queries == references_before_hit);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u,
        ASTRA_AREA_SIZE_MAX - ASTRA_MEMORY_PAGE_SIZE,
        2u * ASTRA_MEMORY_PAGE_SIZE, &mapped_area,
        &mapped_size) == ASTRA_VFS_ERR_INVALID);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1235u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_grant.backing_references == 1u && area_duplicates == 3u &&
           file_closes == 2u);
    assert(hostfs_file_area(NULL, &another_grant.port, 2u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == 2u * ASTRA_MEMORY_PAGE_SIZE &&
           mapped_grant.backing_references == 1u && another_grant.backing_references == 0u &&
           area_binds == 1u && area_duplicates == 4u && file_closes == 3u);
    open_file_size = ASTRA_MEMORY_PAGE_SIZE;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1235u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && mapped_size == ASTRA_MEMORY_PAGE_SIZE &&
           mapped_grant.backing_references == 1u && area_binds == 1u &&
           area_duplicates == 5u && area_reclaims == 1u &&
           file_areas->snapshot.size == ASTRA_MEMORY_PAGE_SIZE);
    open_file_size = ASTRA_AREA_SIZE_MAX + ASTRA_MEMORY_PAGE_SIZE;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1235u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && mapped_size == open_file_size &&
           mapped_grant.backing_references == 2u && area_binds == 2u &&
           area_duplicates == 6u && file_areas->source.base == ASTRA_AREA_SIZE_MAX);
    HostfsVolume other_owner = {0};
    HostfsGrant other_grant = {.owner = &other_owner, .session = 33u};
    assert(hostfs_file_area(NULL, &other_grant.port, 3u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 45u && other_grant.backing_references == 1u &&
           area_binds == 3u && area_duplicates == 7u);
    mapped_owner.backend.provider.session = 33u;
    uint32_t moved = 0u;
    uint64_t position = 0u;
    assert(hostfs_write(&mapped_owner.backend, 55u, 0u, 0u, 0u,
        "x", 1u, &moved, &position) == ASTRA_VFS_OK);
    assert(writes == 1u && moved == 1u && position == 1u && area_reclaims == 3u);
    assert(hostfs_truncate(&mapped_owner.backend, 55u, 1u) == ASTRA_VFS_OK);
    assert(truncates == 1u && area_reclaims == 5u);
    uintptr_t opened = 0u;
    AstraVfsNodeInfo opened_info = {0};
    assert(hostfs_open(&mapped_owner.backend, "/mapped", ASTRA_VFS_OPEN_TRUNCATE,
        ASTRA_VFS_MODE_DEFAULT, &opened, &opened_info) == ASTRA_VFS_OK);
    assert(opens == 1u && area_reclaims == 7u);
    assert(hostfs_open_at(&mapped_owner.backend, 23u, "mapped",
        ASTRA_VFS_OPEN_TRUNCATE, ASTRA_VFS_MODE_DEFAULT,
        &opened, &opened_info) == ASTRA_VFS_OK);
    assert(opens == 2u && area_reclaims == 9u && !file_areas->stale);
    file_areas->next->source.identity.generation++;
    assert(hostfs_write(&mapped_owner.backend, 55u, 0u, 0u, 0u,
        "x", 1u, &moved, &position) == ASTRA_VFS_OK);
    assert(area_reclaims == 10u && !file_areas->next->stale);
    file_areas->next->source.identity.generation--;
    file_areas_observe_nodes(&mapped_owner, 0u);
    assert(!file_areas->stale && !file_areas->next->stale &&
           !file_areas->next->next->stale);
    file_areas_observe_nodes(&mapped_owner, 1u);
    assert(mapped_owner.observed_cache_nodes == 1u &&
           !file_areas->stale && file_areas->next->stale &&
           file_areas->next->next->stale);
    file_areas_maintain(1);
    assert(area_reclaims == 12u && !file_areas->stale &&
           !file_areas->next->stale && !file_areas->next->next->stale);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u &&
           other_grant.backing_references == 0u && file_closes == 7u);
    node_sequence = 1u;
    next_area = 40u;
    area_handles = 2u; area_mappings = 1u;
    provider_epoch = 50u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == 4u);
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_GAP, 0u));
    file_areas_maintain(1);
    assert(!file_areas->stale && area_reclaims == 13u);
    provider_epoch = 51u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && area_binds == 5u &&
           mapped_grant.backing_references == 2u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u &&
           file_closes == 9u);
    provider_epoch = 0u;
    next_area = 40u;
    area_handles = 2u; area_mappings = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == 6u);
    node_sequence = 2u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(snapshot_calls >= 2u && area_reclaims == 14u &&
           mapped_owner.observed_cache_nodes == 2u && mapped_area == 41u &&
           area_binds == 6u);
    assert(hostfs_file_mutated(NULL, &mapped_grant.port, 1234u) ==
           ASTRA_VFS_OK && area_reclaims == 15u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u);
    open_file_identity_valid = 0u;
    next_area = 46u;
    area_handles = 2u; area_mappings = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 47u && mapped_grant.backing_references == 1u);
    assert(hostfs_file_area(NULL, &another_grant.port, 2u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 49u && another_grant.backing_references == 1u);
    assert(file_areas_invalidate(&mapped_owner, ASTRA_HOST_FS_CHANGE_NODE,
        999u) && file_areas->stale && file_areas->next->stale);
    file_areas_maintain(1);
    assert(!file_areas->stale && !file_areas->next->stale);
    uint32_t reclaimed_before = area_reclaims;
    assert(hostfs_write(&mapped_owner.backend, 55u, 0u, 0u, 0u,
        "x", 1u, &moved, &position) == ASTRA_VFS_OK);
    assert(area_reclaims == reclaimed_before + 2u);
    area_handles = 1u; area_mappings = 0u;
    file_areas_maintain(1);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u &&
           another_grant.backing_references == 0u);
    open_file_identity_valid = 1u;
    open_file_flags = ASTRA_VFS_OPEN_WRITE;
    uint32_t closes_before_denied = file_closes;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_ACCESS);
    assert(mapped_area == 0u && file_closes == closes_before_denied + 1u &&
           file_areas == NULL && mapped_grant.backing_references == 0u);
    open_file_flags = ASTRA_VFS_OPEN_READ;
    next_area = 40u;
    uint32_t retained_binds = area_binds;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas != NULL && mapped_grant.backing_references == 1u);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 41u && area_binds == retained_binds + 1u);
    file_areas->pins = 1u;
    file_areas_maintain(1);
    assert(file_areas != NULL);
    file_areas->pins = 0u;
    mapped_grant.retiring = 1u;
    file_areas_maintain(0);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u);
    mapped_grant.retiring = 0u;
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    adopt_limits = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 43u && mapped_grant.backing_references == 1u);
    area_limits = 1u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 45u && mapped_grant.backing_references == 1u);
    file_areas->pins = 1u;
    area_limits = 2u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_LIMIT);
    assert(mapped_area == 0u && mapped_grant.backing_references == 1u && file_areas->pins == 1u);
    file_areas->pins = 0u;
    file_close_status = ASTRA_VFS_ERR_IO;
    file_areas_maintain(1);
    assert(file_areas != NULL && file_areas->source.node == 55u);
    file_close_status = ASTRA_VFS_OK;
    area_close_status = ASTRA_SYSCALL_IO_ERROR;
    file_areas_maintain(1);
    assert(file_areas != NULL && file_areas->source.node == 0u);
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, ASTRA_AREA_SIZE_MAX,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    assert(mapped_area == 47u && mapped_grant.backing_references == 2u);
    area_close_status = ASTRA_SYSCALL_OK;
    file_areas_maintain(0);
    assert(file_areas != NULL && file_areas->next == NULL && mapped_grant.backing_references == 1u);
    adopt_limits = 2u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_ERR_LIMIT);
    assert(file_areas == NULL && mapped_area == 0u && mapped_grant.backing_references == 0u);
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    session_open_limits = 1u;
    uint32_t opened_session = 0u;
    assert(hostfs_session_open(0u, NULL, "/", 0u, 0u, &opened_session) == ASTRA_VFS_OK);
    assert(opened_session == 33u && session_opens == 2u && file_areas == NULL &&
        mapped_grant.backing_references == 0u);
    session_open_limits = 2u;
    assert(hostfs_session_open(0u, NULL, "/", 0u, 0u, &opened_session) == ASTRA_VFS_ERR_LIMIT);
    assert(opened_session == 0u && session_opens == 4u);
    next_area = 40u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
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
    assert(file_areas == NULL && mapped_grant.backing_references == 0u);
    open_file_links = 1u;
    next_area = 40u;
    open_file_identity_valid = 0u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u);
    open_file_identity_valid = 1u;
    open_file_links = 0u;
    assert(hostfs_file_area(NULL, &mapped_grant.port, 1u, 1234u, 0u,
        ASTRA_MEMORY_PAGE_SIZE, &mapped_area, &mapped_size) == ASTRA_VFS_OK);
    file_areas_maintain(0);
    assert(file_areas == NULL && mapped_grant.backing_references == 0u);
    open_file_links = 1u;
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
    return 0;
}
