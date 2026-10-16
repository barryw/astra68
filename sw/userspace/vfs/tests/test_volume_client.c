#include <astra/volume_port.h>
#include <astra/area.h>
#include <astra/port.h>
#include <astra/result.h>
#include <astra/filesystem_library.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned owned[512], next_handle = 1u;
static AstraVolumeCatalogPage shared;
static AstraVolumeUsagePage shared_usage;
static uint32_t shared_size;
static AstraVolumeNotification notification;
static AstraResult transport_failure, map_failure;
static uint32_t service_failure, corrupt_reply, corrupt_page;
static uint32_t maps, closes, queued;
static uint64_t subscription;
static unsigned allocations, fail_allocation, snapshot_pages, fail_continuation;
static uint32_t shares, unshares;
static uint32_t image_releases;
uint32_t astra_vfs_share(AstraVfsClient *client, AstraVfsFile file, uint64_t *token)
{ assert(client != NULL && file == 9u); ++shares; *token = 99u; return ASTRA_STATUS_OK; }
uint32_t astra_vfs_unshare(AstraVfsClient *client, uint64_t token)
{ assert(client != NULL && token == 99u); ++unshares; return ASTRA_STATUS_OK; }

void *astra_runtime_allocate(size_t size)
{
    if (fail_allocation) return NULL;
    void *bytes = malloc(size);
    if (bytes != NULL) ++allocations;
    return bytes;
}
void *astra_runtime_reallocate(void *old, size_t size)
{
    if (fail_allocation) return NULL;
    void *bytes = realloc(old, size);
    if (bytes != NULL && old == NULL) ++allocations;
    return bytes;
}
void astra_runtime_deallocate(void *bytes)
{
    if (bytes != NULL) { assert(allocations != 0u); --allocations; free(bytes); }
}

static AstraHandle acquire(void)
{ assert(next_handle < 512u); owned[next_handle] = 1u; return next_handle++; }
static void balanced(void)
{ for (unsigned at = 1u; at < next_handle; ++at) assert(!owned[at]); }
AstraResult astra_handle_close(AstraHandle *handle)
{
    assert(*handle != 0u && owned[*handle]); owned[*handle] = 0u; *handle = 0u; ++closes;
    return ASTRA_OK;
}
AstraResult astra_handle_duplicate(AstraHandle source, uint32_t rights, AstraHandle *duplicate)
{
    assert(source != 0u && owned[source] && (rights & ASTRA_RIGHT_TRANSFER) != 0u);
    *duplicate = acquire(); return ASTRA_OK;
}
AstraResult astra_area_create(uint32_t size, uint32_t rights, AstraArea *area)
{
    assert((size == sizeof(shared) || size == sizeof(shared_usage)) && (rights & ASTRA_RIGHT_WRITE) != 0u);
    shared_size = size;
    area->handle = acquire(); memset(&shared, 0, sizeof(shared)); memset(&shared_usage, 0, sizeof(shared_usage)); return ASTRA_OK;
}
AstraResult astra_area_map(AstraArea *area, uint32_t flags)
{
    assert(owned[area->handle] && flags == ASTRA_AREA_MAP_READ); ++maps;
    if (map_failure != 0) return map_failure;
    area->address = shared_size == sizeof(shared) ? (void *)&shared : (void *)&shared_usage;
    area->size = shared_size; area->map_flags = flags;
    return ASTRA_OK;
}
AstraResult astra_area_close(AstraArea *area)
{
    AstraResult result = astra_handle_close(&area->handle);
    area->address = NULL; area->size = 0u; area->map_flags = 0u; return result;
}
AstraResult astra_port_create(uint32_t count, uint32_t bytes, AstraPort *port)
{
    assert(count == 1u && bytes == sizeof(notification));
    port->receive = acquire(); port->send = acquire(); return ASTRA_OK;
}
AstraResult astra_port_close(AstraPort *port)
{
    if (port->receive != 0u) assert(astra_handle_close(&port->receive) == ASTRA_OK);
    if (port->send != 0u) assert(astra_handle_close(&port->send) == ASTRA_OK);
    return ASTRA_OK;
}
AstraResult astra_port_call(AstraHandle endpoint, AstraCall *call, AstraMonotonicDeadline deadline)
{
    assert(endpoint == 77u && deadline == ASTRA_DEADLINE_INFINITE);
    const AstraVolumeRequest *request = call->request;
    int control = request->header.operation == ASTRA_VOLUME_CONTROL;
    int broker_info = request->header.operation == ASTRA_VOLUME_BROKER_INFO;
    uint32_t reply_size = control ? sizeof(AstraVolumeControlReply) : broker_info ?
        sizeof(AstraVolumeBrokerInfoReply) : sizeof(AstraVolumeReply);
    assert(call->request_size == (control ? sizeof(AstraVolumeControlRequest) :
        request->header.operation == ASTRA_VOLUME_ROOT ? sizeof(AstraVolumeRootRequest) :
        request->header.operation == ASTRA_VOLUME_USAGE ? sizeof(AstraVolumeUsageRequest) :
        broker_info ? sizeof(AstraMessageHeader) : sizeof(*request)) &&
        call->reply_capacity == reply_size);
    assert(request->header.protocol == ASTRA_VOLUME_PROTOCOL && request->header.flags == 0u);
    if (transport_failure != 0) return transport_failure; /* unsent handles retained */
    for (uint32_t at = 0u; at < call->handle_count; ++at)
        assert(astra_handle_close(&call->handles[at]) == ASTRA_OK);
    AstraVolumeReply *reply = call->reply;
    memset(reply, 0, reply_size);
    astra_message_header_set(&reply->header, reply_size, ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_REPLY, request->header.transaction_id);
    reply->status = service_failure;
    call->reply_handle_count = 0u;
    if (broker_info && reply->status == 0u) {
        AstraVolumeBrokerInfo *info = &((AstraVolumeBrokerInfoReply *)reply)->info;
        info->source = (AstraVolumeReference){25u, 3u, 1u}; info->rights = ASTRA_RIGHT_READ;
        if (corrupt_page) info->source.generation = 0u;
    } else if (control && reply->status == 0u) {
        const AstraVolumeControlRequest *wanted = call->request;
        assert(astra_volume_control_valid(&wanted->control) && call->handle_count == 0u);
        if (wanted->control.operation == ASTRA_VOLUME_RELEASE_IMAGE_SOURCE) {
            assert(wanted->control.reference.epoch == 25u &&
                wanted->control.reference.id == 3u && wanted->control.reference.generation == 1u);
            ++image_releases;
        }
        ((AstraVolumeControlReply *)reply)->reference = (AstraVolumeReference){25u, 8u, 1u};
    } else if (request->header.operation == ASTRA_VOLUME_ROOT && reply->status == 0u) {
        const AstraVolumeRootRequest *root_request = call->request;
        assert(call->handle_count == 0u && call->reply_handle_capacity == 1u);
        assert(root_request->reference.epoch == 25u && root_request->reference.id == 5u &&
            root_request->reference.generation == 1u && root_request->reserved == 0u);
        *call->reply_handles = acquire();
        call->reply_handle_count = 1u;
    } else if (request->header.operation == ASTRA_VOLUME_USAGE && reply->status == 0u) {
        const AstraVolumeUsageQuery *wanted = &((const AstraVolumeUsageRequest *)request)->query;
        assert(call->handle_count == 1u && astra_volume_usage_query_valid(wanted));
        shared_usage.version = ASTRA_VOLUME_USAGE_VERSION; shared_usage.size = sizeof(shared_usage);
        shared_usage.volume = (AstraVolumeReference){25u, wanted->volume.id, wanted->volume.generation};
        shared_usage.sequence = 11u; shared_usage.catalog_sequence = 3u; shared_usage.count = 1u;
        shared_usage.records[0] = (AstraVolumeUsageRecord){.volume = {25u, 5u, 1u},
            .handle = 1u, .inode = 51u, .process = 17u, .kind = ASTRA_VOLUME_USAGE_FILE,
            .rights = ASTRA_RIGHT_READ, .path_length = 15u, .path = "/ram0/held file"};
        if (corrupt_page) shared_usage.records[0].reserved = 1u;
        if (snapshot_pages) {
            if (snapshot_pages == 2) shared_usage.records[0].kind = ASTRA_VOLUME_USAGE_ROOT;
            if (wanted->after == 0u) {
                shared_usage.next = 1u; shared_usage.next_kind = shared_usage.records[0].kind;
                shared_usage.next_process = 17u;
            } else {
                assert(wanted->volume.epoch == 25u && wanted->sequence == 11u &&
                    wanted->catalog_sequence == 3u && wanted->after == 1u &&
                    wanted->after_kind == shared_usage.records[0].kind && wanted->after_process == 17u);
                if (snapshot_pages == 2) shared_usage.records[0].process = 19u;
                else shared_usage.records[0].handle = 2u;
                if (fail_continuation) reply->status = ASTRA_STATUS_BUSY;
            }
        }
    } else if (request->header.operation == ASTRA_VOLUME_QUERY && reply->status == 0u) {
        assert(call->handle_count == 1u);
        shared.version = ASTRA_VOLUME_CATALOG_VERSION; shared.size = sizeof(shared);
        shared.epoch = 25u; shared.sequence = 3u;
        shared.count = 1u;
        shared.records[0] = (AstraVolumeRecord){.id = 5u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED, .name = "/dh0"};
        if (corrupt_page) shared.records[0].generation = 0u;
        if (snapshot_pages) {
            if (request->query.after == 0u) shared.next = 5u;
            else {
                assert(request->query.epoch == 25u && request->query.sequence == 3u && request->query.after == 5u);
                shared.records[0].id = 6u;
                if (fail_continuation) reply->status = ASTRA_STATUS_BUSY;
            }
        }
    } else if (request->header.operation == ASTRA_VOLUME_SUBSCRIBE && reply->status == 0u) {
        assert(call->handle_count == 1u); reply->subscription = ++subscription;
        memset(&notification, 0, sizeof(notification));
        astra_message_header_set(&notification.header, sizeof(notification), ASTRA_VOLUME_PROTOCOL,
            ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_CHANGED, 0u);
        notification.subscription = subscription;
        notification.change = (AstraVolumeChange){25u, 3u, 0u, 0u}; queued = 1u;
    } else if (request->header.operation == ASTRA_VOLUME_UNSUBSCRIBE)
        assert(call->handle_count == 0u && request->subscription != 0u);
    call->reply_size = reply_size;
    if (corrupt_reply == 1u) ++reply->header.transaction_id;
    if (corrupt_reply == 2u) reply->reserved = 1u;
    if (corrupt_reply == 3u) reply->subscription = 9u;
    if (corrupt_reply == 4u) reply->header.flags = 1u;
    return ASTRA_OK;
}
AstraResult astra_port_receive_try(AstraHandle receive, void *out, uint32_t capacity,
    AstraHandle *handles, uint32_t handle_capacity, uint32_t *size, uint32_t *count)
{
    assert(owned[receive] && capacity == sizeof(notification) && handles == NULL && handle_capacity == 0u);
    if (!queued) return ASTRA_ERROR_WOULD_BLOCK;
    queued = 0u; memcpy(out, &notification, sizeof(notification)); *size = sizeof(notification); *count = 0u;
    return ASTRA_OK;
}
static void removal_lifecycle(void)
{
    AstraVolumeRecord records[2] = {
        {.id = 5u, .generation = 1u, .kind = ASTRA_VOLUME_MOUNT,
            .state = ASTRA_VOLUME_QUIESCING},
        {.id = 3u, .generation = 1u, .kind = ASTRA_VOLUME_SOURCE,
            .state = ASTRA_VOLUME_UNMOUNTED}
    };
    AstraVolumeSnapshot snapshot = {.epoch = 25u, .records = records, .count = 2u};
    AstraVolumeRemoval removal = {.mount = {25u, 5u, 1u},
        .image_source = {25u, 3u, 1u}};
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING && image_releases == 0u);
    records[0].state = ASTRA_VOLUME_UNMOUNTED;
    records[0].flags = ASTRA_VOLUME_CLEANUP_PENDING;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING && image_releases == 0u);
    records[0].flags = 0u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING && image_releases == 1u);
    assert(removal.source_admitted == 1u);
    records[1].state = ASTRA_VOLUME_LOST;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING && image_releases == 1u);
    snapshot.count = 1u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_COMPLETE);

    removal = (AstraVolumeRemoval){.mount = {25u, 5u, 1u},
        .image_source = {25u, 3u, 1u}};
    snapshot.count = 2u;
    records[1].generation = 2u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING &&
        removal.source_admitted == 1u && image_releases == 1u);
    records[1].generation = 1u;
    removal.source_admitted = 0u;
    service_failure = ASTRA_STATUS_BUSY;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PENDING && removal.source_admitted == 0u);
    service_failure = 0u;

    records[0].state = ASTRA_VOLUME_MOUNTED;
    records[0].status = ASTRA_STATUS_BUSY;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_REFUSED && removal.status == ASTRA_STATUS_BUSY);
    records[0].state = ASTRA_VOLUME_FAILED;
    records[0].status = ASTRA_STATUS_IO;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_RETAINED && removal.status == ASTRA_STATUS_IO);
    records[0].state = ASTRA_VOLUME_LOST;
    records[0].status = 0u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_RETAINED && removal.status == ASTRA_STATUS_LOST);
    snapshot.epoch = 26u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED && image_releases == 1u);
    snapshot.epoch = 25u;
    removal = (AstraVolumeRemoval){.mount = {25u, 5u, 1u}};
    snapshot.count = 0u;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_OK);
    assert(removal.state == ASTRA_VOLUME_REMOVAL_COMPLETE);
    removal.image_source = removal.mount;
    assert(astra_volume_removal_advance(77u, &snapshot, &removal) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(image_releases == 1u);
    balanced();
}

int main(void)
{
    removal_lifecycle();
    AstraVolumeControl control = {.reference = {25u, 5u, 1u}, .operation = ASTRA_VOLUME_UNMOUNT};
    AstraVolumeReference admitted;
    assert(astra_volume_control(77u, &control, &admitted) == ASTRA_OK && admitted.id == 8u);
    control.flags = ASTRA_VOLUME_READ_ONLY;
    assert(astra_volume_control(77u, &control, &admitted) == ASTRA_ERROR_INVALID_ARGUMENT && admitted.id == 0u);
    control.flags = 0u;
    service_failure = ASTRA_STATUS_BUSY;
    assert(astra_volume_control(77u, &control, &admitted) == ASTRA_ERROR_BUSY && admitted.id == 0u);
    service_failure = 0u;
    AstraVfsClient client = {0};
    AstraFile image = ASTRA_FILE_INIT;
    image._private_client = &client; image._private_file = 9u;
    image._private_flags = ASTRA_VFS_OPEN_READ; image._private_kind = ASTRA_VFS_KIND_FILE;
    assert(astra_volume_image(77u, &image, &admitted) == ASTRA_OK && admitted.id == 8u && shares == 1u && unshares == 1u);
    transport_failure = ASTRA_ERROR_PEER_DEAD;
    assert(astra_volume_image(77u, &image, &admitted) == transport_failure && unshares == 2u && admitted.id == 0u);
    transport_failure = 0;
    client.ownership_epoch = 1u;
    assert(astra_volume_image(77u, &image, &admitted) == ASTRA_ERROR_INVALID_HANDLE &&
           admitted.id == 0u && shares == 2u && unshares == 2u);
    /* A freshly admitted owner may reuse the same token; the inherited file
     * above must never share it. */
    image._private_ownership_epoch = 1u;
    assert(astra_volume_image(77u, &image, &admitted) == ASTRA_OK &&
           admitted.id == 8u && shares == 3u && unshares == 3u);
    image._private_flags |= ASTRA_VFS_OPEN_WRITE;
    assert(astra_volume_image(77u, &image, &admitted) == ASTRA_ERROR_INVALID_ARGUMENT && shares == 3u);
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    AstraVolumeCatalogPage page;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_OK && page.count == 1u);
    balanced();
    for (corrupt_reply = 1u; corrupt_reply < 5u; ++corrupt_reply) {
        assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_IO);
        assert(page.count == 0u && page.epoch == 0u); balanced();
    }
    corrupt_reply = 0u; corrupt_page = 1u;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_IO); balanced(); corrupt_page = 0u;
    service_failure = ASTRA_STATUS_BUSY;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_BUSY); balanced();
    service_failure = ASTRA_STATUS_LOST;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_INVALID_HANDLE); balanced(); service_failure = 0u;
    transport_failure = ASTRA_ERROR_PEER_DEAD;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_PEER_DEAD); balanced(); transport_failure = 0;
    map_failure = ASTRA_ERROR_OUT_OF_MEMORY;
    assert(astra_volume_catalog(77u, &query, &page) == map_failure); balanced(); map_failure = 0;
    query.after = 1u;
    assert(astra_volume_catalog(77u, &query, &page) == ASTRA_ERROR_INVALID_ARGUMENT && page.count == 0u);
    query.after = 0u;
    AstraVolumeWatch watch = ASTRA_VOLUME_WATCH_INIT;
    AstraVolumeChange change;
    assert(astra_volume_watch_open(77u, &watch) == ASTRA_OK && watch.id != 0u);
    assert(astra_volume_watch_open(77u, &watch) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_volume_watch_receive(&watch, &change) == ASTRA_OK && change.sequence == 3u);
    assert(astra_volume_watch_receive(&watch, &change) == ASTRA_ERROR_WOULD_BLOCK);
    queued = 1u; ++notification.subscription;
    assert(astra_volume_watch_receive(&watch, &change) == ASTRA_ERROR_IO);
    assert(astra_volume_watch_close(77u, &watch) == ASTRA_OK && watch.id == 0u); balanced();
    transport_failure = ASTRA_ERROR_PEER_DEAD;
    assert(astra_volume_watch_open(77u, &watch) == transport_failure && watch.id == 0u); balanced();
    transport_failure = 0u;
    assert(astra_volume_watch_open(77u, &watch) == ASTRA_OK);
    transport_failure = ASTRA_ERROR_PEER_DEAD;
    assert(astra_volume_watch_close(77u, &watch) == transport_failure && watch.id == 0u); balanced();
    transport_failure = 0;
    AstraVolumeReference reference = {25u, 5u, 1u};
    AstraHandle root = ASTRA_INVALID_HANDLE;
    assert(astra_volume_root(77u, &reference, ASTRA_RIGHT_READ, &root) == ASTRA_OK && root != 0u);
    assert(astra_volume_root(77u, &reference, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_handle_close(&root) == ASTRA_OK); balanced();
    corrupt_reply = 1u;
    assert(astra_volume_root(77u, &reference, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_IO && root == 0u);
    balanced(); corrupt_reply = 0u;
    service_failure = ASTRA_STATUS_ACCESS;
    assert(astra_volume_root(77u, &reference, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_PERMISSION && root == 0u);
    balanced(); service_failure = 0u;
    reference.generation = 0u;
    assert(astra_volume_root(77u, &reference, ASTRA_RIGHT_READ, &root) == ASTRA_ERROR_INVALID_ARGUMENT);
    AstraVolumeSnapshot snapshot = ASTRA_VOLUME_SNAPSHOT_INIT;
    snapshot_pages = 1u;
    assert(astra_volume_snapshot_open(77u, &snapshot) == ASTRA_OK);
    assert(snapshot.epoch == 25u && snapshot.sequence == 3u && snapshot.count == 2u &&
        snapshot.records[0].id == 5u && snapshot.records[1].id == 6u && allocations == 1u);
    assert(astra_volume_snapshot_open(77u, &snapshot) == ASTRA_ERROR_INVALID_ARGUMENT);
    astra_volume_snapshot_close(&snapshot);
    assert(snapshot.epoch == 0u && snapshot.records == NULL && allocations == 0u); balanced();
    fail_continuation = 1u;
    assert(astra_volume_snapshot_open(77u, &snapshot) == ASTRA_ERROR_BUSY);
    assert(snapshot.epoch == 0u && snapshot.records == NULL && snapshot.count == 0u && allocations == 0u); balanced();
    fail_continuation = 0u; fail_allocation = 1u;
    assert(astra_volume_snapshot_open(77u, &snapshot) == ASTRA_ERROR_OUT_OF_MEMORY);
    assert(snapshot.records == NULL && allocations == 0u); balanced();
    fail_allocation = 0u;
    AstraVolumeUsageQuery usage_query = {ASTRA_VOLUME_USAGE_VERSION, sizeof(usage_query), {0}, 0u, 0u, 0u, 0u, 0u};
    AstraVolumeUsagePage usage_page;
    snapshot_pages = 0u;
    assert(astra_volume_usage(77u, &usage_query, &usage_page) == ASTRA_OK &&
        usage_page.records[0].process == 17u && usage_page.records[0].handle == 1u);
    balanced();
    corrupt_page = 1u;
    assert(astra_volume_usage(77u, &usage_query, &usage_page) == ASTRA_ERROR_IO && usage_page.count == 0u);
    corrupt_page = 0u; balanced();
    service_failure = ASTRA_STATUS_ACCESS;
    assert(astra_volume_usage(77u, &usage_query, &usage_page) == ASTRA_ERROR_PERMISSION && usage_page.count == 0u);
    service_failure = 0u; balanced();
    transport_failure = ASTRA_ERROR_PEER_DEAD;
    assert(astra_volume_usage(77u, &usage_query, &usage_page) == transport_failure && usage_page.sequence == 0u);
    transport_failure = 0u; balanced();
    map_failure = ASTRA_ERROR_OUT_OF_MEMORY;
    assert(astra_volume_usage(77u, &usage_query, &usage_page) == map_failure);
    map_failure = 0u; balanced();
    AstraVolumeUsageSnapshot usage_snapshot = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    snapshot_pages = 1u;
    assert(astra_volume_usage_snapshot_open(77u, NULL, &usage_snapshot) == ASTRA_OK &&
        usage_snapshot.count == 2u && usage_snapshot.sequence == 11u && allocations == 1u);
    assert(astra_volume_usage_snapshot_open(77u, NULL, &usage_snapshot) == ASTRA_ERROR_INVALID_ARGUMENT);
    astra_volume_usage_snapshot_close(&usage_snapshot); balanced();
    assert(allocations == 0u && usage_snapshot.records == NULL);
    fail_continuation = 1u;
    assert(astra_volume_usage_snapshot_open(77u, NULL, &usage_snapshot) == ASTRA_ERROR_BUSY &&
        usage_snapshot.records == NULL && usage_snapshot.count == 0u && allocations == 0u);
    fail_continuation = 0u; fail_allocation = 1u;
    assert(astra_volume_usage_snapshot_open(77u, NULL, &usage_snapshot) == ASTRA_ERROR_OUT_OF_MEMORY);
    fail_allocation = 0u; balanced();
    snapshot_pages = 2u;
    assert(astra_volume_usage_snapshot_open(77u, NULL, &usage_snapshot) == ASTRA_OK && usage_snapshot.count == 2u &&
        usage_snapshot.records[0].kind == ASTRA_VOLUME_USAGE_ROOT &&
        usage_snapshot.records[0].handle == usage_snapshot.records[1].handle &&
        usage_snapshot.records[0].process == 17u && usage_snapshot.records[1].process == 19u);
    astra_volume_usage_snapshot_close(&usage_snapshot); balanced();
    snapshot_pages = 0u;
    AstraVolumeBrokerInfo info = {0};
    assert(astra_volume_broker_info(77u, &info) == ASTRA_OK && info.source.id == 3u && info.rights == ASTRA_RIGHT_READ);
    corrupt_page = 1u;
    assert(astra_volume_broker_info(77u, &info) == ASTRA_ERROR_IO && info.source.id == 0u);
    corrupt_page = 0u; service_failure = ASTRA_STATUS_ACCESS;
    assert(astra_volume_broker_info(77u, &info) == ASTRA_ERROR_PERMISSION);
    service_failure = 0u;
    balanced();
    assert(maps != 0u && closes != 0u);
    puts("Native volume client validation/cleanup: PASS");
    return 0;
}
