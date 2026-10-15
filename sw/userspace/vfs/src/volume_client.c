#include <astra/volume_port.h>
#include <astra/area.h>
#include <astra/port.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/filesystem_library.h>
#include <astra/vfs_file_lifetime.h>
#include <string.h>

/* Both complete snapshot collectors use one bounded owned-array admission
 * contract. A failed growth preserves the old owner for rollback. */
static AstraResult snapshot_capacity(void *records, uint32_t count, uint32_t *capacity,
    uint32_t width, uint32_t initial, uint32_t added, void **grown_records)
{
    uint32_t maximum = UINT32_MAX / width;
    if (added > maximum - count) return ASTRA_ERROR_OUT_OF_MEMORY;
    *grown_records = records;
    if (count + added <= *capacity) return ASTRA_OK;
    uint32_t grown = *capacity == 0u ? initial : *capacity;
    while (grown < count + added) grown = grown > maximum / 2u ? maximum : grown * 2u;
    void *replacement = astra_runtime_reallocate(records, (size_t)grown * width);
    if (replacement == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    *grown_records = replacement; *capacity = grown;
    return ASTRA_OK;
}

static AstraResult exchange(AstraHandle service, const void *request, uint32_t size,
    AstraHandle *sent, AstraVolumeReply *reply, uint32_t reply_size, AstraHandle *root)
{
    AstraCall call = {0};
    const AstraMessageHeader *header = request;
    AstraHandle received = ASTRA_INVALID_HANDLE;
    call.request = request;
    call.request_size = size;
    call.handles = sent;
    call.handle_count = sent != NULL ? 1u : 0u;
    call.reply = reply;
    call.reply_capacity = reply_size;
    call.reply_handles = &received;
    call.reply_handle_capacity = 1u;
    AstraResult result = astra_port_call(service, &call, ASTRA_DEADLINE_INFINITE);
    if (result == ASTRA_OK && (call.reply_size != reply_size ||
        reply->header.total_size != reply_size ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u || reply->header.reserved != 0u ||
        reply->header.protocol != ASTRA_VOLUME_PROTOCOL ||
        reply->header.protocol_version != ASTRA_VOLUME_PROTOCOL_VERSION ||
        reply->header.operation != ASTRA_VOLUME_REPLY ||
        reply->header.transaction_id != header->transaction_id ||
        reply->reserved != 0u ||
        (header->operation != ASTRA_VOLUME_SUBSCRIBE && reply->subscription != 0u) ||
        (reply->status != ASTRA_STATUS_OK && reply->subscription != 0u) ||
        call.reply_handle_count != (root != NULL && reply->status == ASTRA_STATUS_OK ? 1u : 0u) ||
        ((call.reply_handle_count == 0u) != (received == ASTRA_INVALID_HANDLE))))
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK)
        result = astra_result_from_service(reply->status);
    if (result == ASTRA_OK && root != NULL) {
        *root = received;
        received = ASTRA_INVALID_HANDLE;
    }
    if (received != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&received);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

static void initialize(AstraVolumeRequest *request, uint32_t operation)
{
    memset(request, 0, sizeof(*request));
    /* Each call has its own one-shot reply capability: no shared rendezvous. */
    astra_message_header_set(&request->header, sizeof(*request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, operation, 1u);
}

AstraResult astra_volume_catalog(AstraHandle service,
    const AstraVolumeCatalogQuery *query, AstraVolumeCatalogPage *page)
{
    AstraVolumeRequest request;
    AstraVolumeReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle sent = ASTRA_INVALID_HANDLE;
    AstraResult result;
    if (page == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    if (service == ASTRA_INVALID_HANDLE || !astra_volume_query_valid(query))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    initialize(&request, ASTRA_VOLUME_QUERY);
    request.query = *query;
    result = astra_area_create(sizeof(*page), ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
        ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(area.handle, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &sent);
    if (result == ASTRA_OK)
        result = exchange(service, &request, sizeof(request), &sent, &reply, sizeof(reply), NULL);
    if (sent != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&sent);
        if (result == ASTRA_OK) result = closed;
    }
    if (result == ASTRA_OK)
        result = astra_area_map(&area, ASTRA_AREA_MAP_READ);
    if (result == ASTRA_OK) {
        if (area.size < sizeof(*page))
            result = ASTRA_ERROR_IO;
        else {
            memcpy(page, area.address, sizeof(*page));
            if (!astra_volume_page_valid(query, page))
                result = ASTRA_ERROR_IO;
        }
    }
    if (area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_area_close(&area);
        if (result == ASTRA_OK) result = closed;
    }
    if (result != ASTRA_OK) memset(page, 0, sizeof(*page));
    return result;
}

AstraResult astra_volume_broker_info(AstraHandle broker, AstraVolumeBrokerInfo *info)
{
    AstraMessageHeader request = {0};
    AstraVolumeBrokerInfoReply reply = {0};
    if (info == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));
    if (broker == ASTRA_INVALID_HANDLE) return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_message_header_set(&request, sizeof(request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_BROKER_INFO, 1u);
    AstraResult result = exchange(broker, &request, sizeof(request), NULL,
        &reply.reply, sizeof(reply), NULL);
    if (result == ASTRA_OK && (reply.info.reserved != 0u ||
        (reply.info.rights & ASTRA_RIGHT_READ) == 0u ||
        (reply.info.rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u ||
        ((reply.info.source.id == 0u || reply.info.source.epoch == 0u ||
            reply.info.source.generation == 0u) && (reply.info.source.id != 0u ||
            reply.info.source.epoch != 0u || reply.info.source.generation != 0u))))
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) *info = reply.info;
    return result;
}

AstraResult astra_volume_root(AstraHandle broker, const AstraVolumeReference *reference,
    uint32_t rights, AstraHandle *root)
{
    AstraVolumeRootRequest request = {0};
    AstraVolumeReply reply = {0};
    if (broker == ASTRA_INVALID_HANDLE || reference == NULL || root == NULL ||
        *root != ASTRA_INVALID_HANDLE || reference->epoch == 0u || reference->id == 0u ||
        reference->generation == 0u || (rights & ASTRA_RIGHT_READ) == 0u ||
        (rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_message_header_set(&request.header, sizeof(request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_ROOT, 1u);
    request.reference = *reference;
    request.rights = rights;
    return exchange(broker, &request, sizeof(request), NULL, &reply, sizeof(reply), root);
}

AstraResult astra_volume_usage(AstraHandle service,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page)
{
    AstraVolumeUsageRequest request = {0};
    AstraVolumeReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle sent = ASTRA_INVALID_HANDLE;
    if (page == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    if (service == ASTRA_INVALID_HANDLE || !astra_volume_usage_query_valid(query))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_message_header_set(&request.header, sizeof(request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_USAGE, 1u);
    request.query = *query;
    AstraResult result = astra_area_create(sizeof(*page), ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
        ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(area.handle, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &sent);
    if (result == ASTRA_OK)
        result = exchange(service, &request, sizeof(request), &sent, &reply, sizeof(reply), NULL);
    if (sent != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&sent);
        if (result == ASTRA_OK) result = closed;
    }
    if (result == ASTRA_OK) result = astra_area_map(&area, ASTRA_AREA_MAP_READ);
    if (result == ASTRA_OK) {
        if (area.size < sizeof(*page)) result = ASTRA_ERROR_IO;
        else {
            memcpy(page, area.address, sizeof(*page));
            if (!astra_volume_usage_page_valid(query, page)) result = ASTRA_ERROR_IO;
        }
    }
    if (area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_area_close(&area);
        if (result == ASTRA_OK) result = closed;
    }
    if (result != ASTRA_OK) memset(page, 0, sizeof(*page));
    return result;
}

void astra_volume_usage_snapshot_close(AstraVolumeUsageSnapshot *snapshot)
{
    if (snapshot == NULL) return;
    astra_runtime_deallocate(snapshot->records);
    *snapshot = (AstraVolumeUsageSnapshot)ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
}

AstraResult astra_volume_usage_snapshot_open(AstraHandle service,
    const AstraVolumeReference *volume, AstraVolumeUsageSnapshot *snapshot)
{
    if (service == ASTRA_INVALID_HANDLE || snapshot == NULL ||
        snapshot->volume.epoch != 0u || snapshot->volume.id != 0u ||
        snapshot->volume.generation != 0u || snapshot->sequence != 0u ||
        snapshot->catalog_sequence != 0u || snapshot->records != NULL || snapshot->count != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    const AstraVolumeReference all = {0};
    AstraVolumeUsageQuery query = {ASTRA_VOLUME_USAGE_VERSION, sizeof(query), volume == NULL ? all : *volume,
        0u, 0u, 0u, 0u, 0u};
    if (!astra_volume_usage_query_valid(&query)) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraVolumeUsagePage *page = astra_runtime_allocate(sizeof(*page));
    if (page == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    AstraVolumeUsageSnapshot collected = ASTRA_VOLUME_USAGE_SNAPSHOT_INIT;
    uint32_t capacity = 0u;
    AstraResult result;
    for (;;) {
        result = astra_volume_usage(service, &query, page);
        if (result != ASTRA_OK) break;
        void *grown_records = NULL;
        result = snapshot_capacity(collected.records, collected.count, &capacity,
            sizeof(*collected.records), ASTRA_VOLUME_USAGE_PAGE_RECORDS, page->count, &grown_records);
        if (result != ASTRA_OK) break;
        collected.records = grown_records;
        if (page->count != 0u) memcpy(collected.records + collected.count, page->records,
            (size_t)page->count * sizeof(*collected.records));
        collected.count += page->count; collected.volume = page->volume;
        collected.sequence = page->sequence; collected.catalog_sequence = page->catalog_sequence;
        if (page->next == 0u) break;
        query.volume = page->volume; query.sequence = page->sequence;
        query.catalog_sequence = page->catalog_sequence;
        query.after = page->next; query.after_kind = page->next_kind; query.after_process = page->next_process;
    }
    astra_runtime_deallocate(page);
    if (result == ASTRA_OK) *snapshot = collected;
    else astra_volume_usage_snapshot_close(&collected);
    return result;
}

static AstraResult control_call(AstraHandle administrator, const AstraVolumeControl *control,
    AstraVolumeReference *reference)
{
    AstraVolumeControlRequest request = {0};
    AstraVolumeControlReply reply = {0};
    if (reference == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(reference, 0, sizeof(*reference));
    if (administrator == ASTRA_INVALID_HANDLE || !astra_volume_control_valid(control))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_message_header_set(&request.header, sizeof(request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_CONTROL, 1u);
    request.control = *control;
    AstraResult result = exchange(administrator, &request, sizeof(request), NULL,
        &reply.reply, sizeof(reply), NULL);
    if (result == ASTRA_OK) {
        if (reply.reference.epoch == 0u || reply.reference.id == 0u || reply.reference.generation == 0u)
            result = ASTRA_ERROR_IO;
        else *reference = reply.reference;
    }
    return result;
}

AstraResult astra_volume_control(AstraHandle administrator, const AstraVolumeControl *control,
    AstraVolumeReference *reference)
{
    if (control == NULL || control->operation == ASTRA_VOLUME_IMAGE) {
        if (reference != NULL) memset(reference, 0, sizeof(*reference));
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return control_call(administrator, control, reference);
}

AstraResult astra_volume_image(AstraHandle administrator, AstraFile *file,
    AstraVolumeReference *reference)
{
    if (reference == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(reference, 0, sizeof(*reference));
    if (administrator == ASTRA_INVALID_HANDLE || file == NULL || file->_private_client == NULL ||
        file->_private_kind != ASTRA_VFS_KIND_FILE ||
        (file->_private_flags & ASTRA_VFS_OPEN_READ) == 0u ||
        (file->_private_flags & ASTRA_VFS_OPEN_WRITE) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t status = astra_vfs_file_owner_status(file->_private_client,
        file->_private_ownership_epoch);
    if (status != ASTRA_VFS_OK) return astra_result_from_service(status);
    AstraVolumeControl control = {.operation = ASTRA_VOLUME_IMAGE};
    status = astra_vfs_share(file->_private_client, file->_private_file, &control.image_share);
    if (status != ASTRA_STATUS_OK)
        return astra_result_from_service(status);
    AstraResult result = control_call(administrator, &control, reference);
    /* The share remains caller-owned. IMAGE borrows it under authenticated
     * ownership, retaining a separate provider lease before replying. */
    status = astra_vfs_unshare(file->_private_client, control.image_share);
    if (result == ASTRA_OK && status != ASTRA_STATUS_OK)
        result = astra_result_from_service(status);
    return result;
}

void astra_volume_snapshot_close(AstraVolumeSnapshot *snapshot)
{
    if (snapshot == NULL) return;
    astra_runtime_deallocate(snapshot->records);
    *snapshot = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
}

AstraResult astra_volume_snapshot_open(AstraHandle service, AstraVolumeSnapshot *snapshot)
{
    if (service == ASTRA_INVALID_HANDLE || snapshot == NULL || snapshot->epoch != 0u ||
        snapshot->sequence != 0u || snapshot->records != NULL || snapshot->count != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraVolumeCatalogPage *page = astra_runtime_allocate(sizeof(*page));
    if (page == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    AstraVolumeSnapshot collected = ASTRA_VOLUME_SNAPSHOT_INIT;
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    AstraResult result;
    uint32_t capacity = 0u;
    for (;;) {
        result = astra_volume_catalog(service, &query, page);
        if (result != ASTRA_OK) break;
        void *grown_records = NULL;
        result = snapshot_capacity(collected.records, collected.count, &capacity,
            sizeof(*collected.records), ASTRA_VOLUME_PAGE_RECORDS, page->count, &grown_records);
        if (result != ASTRA_OK) break;
        collected.records = grown_records;
        if (page->count != 0u)
            memcpy(collected.records + collected.count, page->records,
                (size_t)page->count * sizeof(*collected.records));
        collected.count += page->count;
        collected.epoch = page->epoch;
        collected.sequence = page->sequence;
        if (page->next == 0u) break;
        query.epoch = page->epoch;
        query.sequence = page->sequence;
        query.after = page->next;
    }
    astra_runtime_deallocate(page);
    if (result == ASTRA_OK) *snapshot = collected;
    else astra_volume_snapshot_close(&collected);
    return result;
}

AstraResult astra_volume_watch_open(AstraHandle service, AstraVolumeWatch *watch)
{
    AstraVolumeRequest request;
    AstraVolumeReply reply = {0};
    AstraPort port = ASTRA_PORT_INIT;
    AstraHandle sent = ASTRA_INVALID_HANDLE;
    if (service == ASTRA_INVALID_HANDLE || watch == NULL || watch->id != 0u ||
        watch->receive != ASTRA_INVALID_HANDLE || watch->reserved != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    initialize(&request, ASTRA_VOLUME_SUBSCRIBE);
    AstraResult result = astra_port_create(1u, sizeof(AstraVolumeNotification), &port);
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(port.send,
            ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER, &sent);
    if (result == ASTRA_OK)
        result = exchange(service, &request, sizeof(request), &sent, &reply, sizeof(reply), NULL);
    if (result == ASTRA_OK && reply.subscription == 0u) result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK) {
        watch->id = reply.subscription;
        watch->receive = port.receive;
        port.receive = ASTRA_INVALID_HANDLE;
    }
    if (sent != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&sent);
        if (result == ASTRA_OK) result = closed;
    }
    AstraResult closed = astra_port_close(&port);
    if (result == ASTRA_OK) result = closed;
    if (result != ASTRA_OK && watch->receive != ASTRA_INVALID_HANDLE) {
        closed = astra_handle_close(&watch->receive);
        (void)closed;
        *watch = (AstraVolumeWatch)ASTRA_VOLUME_WATCH_INIT;
    }
    return result;
}

AstraResult astra_volume_watch_receive(const AstraVolumeWatch *watch, AstraVolumeChange *change)
{
    AstraVolumeNotification notification = {0};
    uint32_t size = 0u, count = 0u;
    if (watch == NULL || change == NULL || watch->id == 0u ||
        watch->receive == ASTRA_INVALID_HANDLE || watch->reserved != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memset(change, 0, sizeof(*change));
    AstraResult result = astra_port_receive_try(watch->receive, &notification,
        sizeof(notification), NULL, 0u, &size, &count);
    if (result != ASTRA_OK) return result;
    if (size != sizeof(notification) || count != 0u ||
        notification.header.total_size != sizeof(notification) ||
        notification.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        notification.header.flags != 0u || notification.header.reserved != 0u ||
        notification.header.protocol != ASTRA_VOLUME_PROTOCOL ||
        notification.header.protocol_version != ASTRA_VOLUME_PROTOCOL_VERSION ||
        notification.header.operation != ASTRA_VOLUME_CHANGED ||
        notification.header.transaction_id != 0u || notification.subscription != watch->id ||
        notification.change.reserved != 0u ||
        (notification.change.status == ASTRA_STATUS_OK &&
            (notification.change.epoch == 0u || notification.change.sequence == 0u)))
        return ASTRA_ERROR_IO;
    *change = notification.change;
    return ASTRA_OK;
}

AstraResult astra_volume_watch_close(AstraHandle service, AstraVolumeWatch *watch)
{
    AstraVolumeRequest request;
    AstraVolumeReply reply = {0};
    if (watch == NULL || watch->reserved != 0u) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = ASTRA_OK;
    if (watch->id != 0u) {
        initialize(&request, ASTRA_VOLUME_UNSUBSCRIBE);
        request.subscription = watch->id;
        result = exchange(service, &request, sizeof(request), NULL, &reply, sizeof(reply), NULL);
    }
    if (watch->receive != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_handle_close(&watch->receive);
        if (result == ASTRA_OK) result = closed;
    }
    *watch = (AstraVolumeWatch)ASTRA_VOLUME_WATCH_INIT;
    return result;
}
