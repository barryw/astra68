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
uint32_t astra_close(uint32_t handle)
{ assert(handle == 31u || handle == 32u); return handle_close_failure; }
uint32_t astra_vfs_host_session_close(AstraVfsHostTransport *host, uint32_t generation, uint32_t session)
{ assert(host == &transport && generation == 9u && session == 33u); return session_close_failure; }
void astra_vfs_port_service_release_grant(AstraVfsPortService *port, uint32_t id)
{ assert(port != NULL && id == 34u); ++released_grants; }
uint32_t astra_log_failure(const char *name, uint32_t value)
{ assert(name != NULL && value != 0u); return ASTRA_SYSCALL_OK; }
void *astra_runtime_allocate(size_t size) { return allocation_failed ? NULL : malloc(size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }
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
const AstraVfsBackendOps *astra_vfs_host_ops(void)
{
    static const AstraVfsBackendOps ops = {.stat = stat_node};
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
int main(void)
{
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
    assert(grant_retire(0u) && grant_count == 0u && retiring_table[0] == NULL);
    grants = NULL; revoke_failure = session_close_failure = handle_close_failure = 0u;
    HostfsGrant recovered = {.root = "/", .root_meta = {
        .valid = ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE, .volume = 55u, .inode = 1u}};
    AstraVfsNodeMeta same = recovered.root_meta;
    assert(grant_root_matches(&recovered, &same));
    ++same.inode;
    assert(!grant_root_matches(&recovered, &same));
    same = recovered.root_meta;
    strcpy(recovered.root, "/folder");
    assert(!grant_root_matches(&recovered, &same));
    same.valid |= ASTRA_VFS_META_GENERATION;
    recovered.root_meta.valid |= ASTRA_VFS_META_GENERATION;
    same.generation = recovered.root_meta.generation = 123u;
    assert(grant_root_matches(&recovered, &same));
    ++same.generation;
    assert(!grant_root_matches(&recovered, &same));
    HostfsVolume first = {0}, second = {0};
    first.reference = (AstraVolumeReference){UINT64_MAX, UINT64_MAX, UINT64_MAX};
    assert(volume_metric_group(&first, "/ram0") && strcmp(first.metric_group, "hostfs.vfs.ram0") == 0);
    first.dynamic = second.dynamic = 1u;
    second.reference = first.reference;
    --second.reference.generation;
    assert(volume_metric_group(&first, "/usb0") && volume_metric_group(&second, "/usb0"));
    assert(strlen(first.metric_group) < ASTRA_METRIC_NAME_MAX && strcmp(first.metric_group, second.metric_group) != 0);
    AstraMetricSample samples[17];
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
    assert(sample_vfs(&first, samples, 17u) == 17u && samples[12].value == 100u &&
           samples[15].value == ASTRA_VFS_ERR_IO);
    assert(sample_vfs(&second, samples, 17u) == 17u && samples[12].value == 9u &&
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
