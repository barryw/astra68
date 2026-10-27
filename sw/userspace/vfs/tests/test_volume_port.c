#include <astra/volume_port.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstraVolumePort port;
static union { AstraVolumeRequest metadata; AstraVolumeRootRequest root;
    AstraVolumeControlRequest control; AstraVolumeFormatRequest format;
    AstraVolumePartitionRequest partition;
    AstraVolumeUsageRequest usage; } incoming_request;
#define request incoming_request.metadata
static AstraVolumeReply reply;
static AstraVolumeNotification notifications[8];
static uint32_t queued[8], dead[8], closed[64];
static uint32_t sent_handles[ASTRA_MESSAGE_HANDLES_MAX], sent_count, sender = 17u;
static uint32_t incoming_size;
static int pending, allocation_failure, malformed_page;
static uint32_t catalog_status;
static uint64_t epoch = 51u, sequence = 1u;
static union { AstraVolumeCatalogPage catalog; AstraVolumeUsagePage usage; } output_storage;
#define output output_storage.catalog
static uint32_t area_bytes = sizeof(output), area_rights = 1u;
static uint32_t root_calls, root_sent, root_send_failure;
static uint32_t control_calls;
static uint32_t format_calls, partition_calls, format_outcome;
static AstraVolumeReference admitted;
static AstraVolumeBrokerInfo broker_info;
static const AstraVolumeReference *expected_source;
static int expected_delegates;

void *astra_runtime_callocate(size_t count, size_t size)
{ return allocation_failure ? NULL : calloc(count, size); }
void astra_runtime_deallocate(void *memory) { free(memory); }
uint32_t astra_close(uint32_t handle) { assert(handle < 64u); ++closed[handle]; return ASTRA_SYSCALL_OK; }
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)detail; assert(deadline == 0u && handle >= 10u && handle < 18u);
    return dead[handle - 10u] ? ASTRA_SYSCALL_PEER_DEAD :
        queued[handle - 10u] ? ASTRA_SYSCALL_TIMED_OUT : ASTRA_SYSCALL_OK;
}
uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
    const uint32_t *handles, uint32_t count)
{
    assert(handles == NULL && count == 0u);
    if (handle == 2u) {
        assert(size == sizeof(reply) || size == sizeof(AstraVolumeControlReply) ||
            size == sizeof(AstraVolumeBrokerInfoReply) || size == sizeof(AstraVolumeFormatReply));
        memcpy(&reply, message, sizeof(reply));
        if (size == sizeof(AstraVolumeControlReply))
            admitted = ((const AstraVolumeControlReply *)message)->reference;
        if (size == sizeof(AstraVolumeBrokerInfoReply))
            broker_info = ((const AstraVolumeBrokerInfoReply *)message)->info;
        if (size == sizeof(AstraVolumeFormatReply))
            format_outcome = ((const AstraVolumeFormatReply *)message)->outcome;
        return ASTRA_SYSCALL_OK;
    }
    if (handle < 10u || handle >= 18u) return ASTRA_SYSCALL_INVALID_HANDLE;
    uint32_t at = handle - 10u;
    if (dead[at]) return ASTRA_SYSCALL_PEER_DEAD;
    if (queued[at]) return ASTRA_SYSCALL_WOULD_BLOCK;
    assert(size == sizeof(notifications[at]));
    memcpy(&notifications[at], message, size);
    queued[at] = 1u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_send_rights(uint32_t handle, const void *message, uint32_t size,
    const AstraHandleDisposition *dispositions, uint32_t count)
{
    assert(handle == 2u && size == sizeof(reply) && count == 1u);
    assert(dispositions[0].handle == 31u && dispositions[0].rights == (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
        (expected_source != NULL ? ASTRA_RIGHT_TRANSFER : 0u)));
    memcpy(&reply, message, size);
    if (root_send_failure) return ASTRA_SYSCALL_PEER_DEAD;
    root_sent = 31u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_receive_from(uint32_t handle, void *message, uint32_t capacity,
    uint32_t *handles, uint32_t handle_capacity, uint32_t *size, uint32_t *count, uint32_t *owner)
{
    assert((handle == 1u || handle == 5u) && capacity >= incoming_size && handle_capacity >= sent_count);
    if (!pending) return ASTRA_SYSCALL_WOULD_BLOCK;
    memcpy(message, &request, incoming_size);
    memcpy(handles, sent_handles, sent_count * sizeof(*handles));
    *size = incoming_size; *count = sent_count; *owner = sender; pending = 0;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_map(uint32_t handle, uint32_t flags, void **mapping, uint32_t *size)
{
    assert(handle == 3u && flags == (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
    if (!area_rights) return ASTRA_SYSCALL_ACCESS_DENIED;
    *mapping = &output_storage; *size = area_bytes; return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_unmap(void *mapping)
{ assert(mapping == &output_storage); return ASTRA_SYSCALL_OK; }

static uint32_t catalog(void *context, const AstraVolumeCatalogQuery *query, AstraVolumeCatalogPage *page)
{
    assert(context == &epoch);
    if (catalog_status != 0u) return catalog_status;
    if (query->epoch != 0u && query->epoch != epoch) return ASTRA_STATUS_LOST;
    if (query->sequence != 0u && query->sequence != sequence) return ASTRA_STATUS_BUSY;
    memset(page, 0, sizeof(*page));
    page->version = ASTRA_VOLUME_CATALOG_VERSION; page->size = sizeof(*page);
    page->epoch = epoch; page->sequence = sequence;
    if (query->after == 0u) {
        page->count = 1u; page->next = 5u;
        page->records[0] = (AstraVolumeRecord){.id = 5u, .generation = 1u,
            .kind = ASTRA_VOLUME_MOUNT, .state = ASTRA_VOLUME_MOUNTED,
            .name = "/dh0", .label = "Astra"};
    }
    if (malformed_page) page->reserved = 1u;
    return ASTRA_STATUS_OK;
}
static uint32_t usage(void *context, const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page)
{
    assert(context == &epoch && astra_volume_usage_query_valid(query));
    if (catalog_status != 0u) return catalog_status;
    if (query->sequence != 0u && query->sequence != sequence) return ASTRA_STATUS_BUSY;
    memset(page, 0, sizeof(*page));
    page->version = ASTRA_VOLUME_USAGE_VERSION; page->size = sizeof(*page);
    page->volume = (AstraVolumeReference){epoch, query->volume.id, query->volume.generation};
    page->sequence = sequence; page->catalog_sequence = 5u; page->count = 1u;
    page->records[0] = (AstraVolumeUsageRecord){.volume = {epoch, 5u, 1u},
        .handle = 27u, .process = 17u, .inode = 99u, .kind = ASTRA_VOLUME_USAGE_FILE,
        .rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, .path = "/usb0/file", .path_length = 10u};
    if (malformed_page) page->records[0].path_length = ASTRA_VOLUME_USAGE_PATH_MAX;
    return ASTRA_STATUS_OK;
}
static void enqueue(uint32_t operation, uint32_t second)
{
    memset(&incoming_request, 0, sizeof(incoming_request));
    astra_message_header_set(&request.header, sizeof(request), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, operation, 21u);
    sent_handles[0] = 2u; sent_count = 1u;
    if (second != 0u) sent_handles[sent_count++] = second;
    incoming_size = sizeof(request); pending = 1;
    memset(&reply, 0, sizeof(reply));
}
static void pump(uint32_t expected)
{
    assert(astra_volume_port_pump(&port, 1u) == 1u && !pending);
    assert(reply.status == expected && reply.header.transaction_id == 21u);
}
static uint64_t watch(uint32_t second)
{
    enqueue(ASTRA_VOLUME_SUBSCRIBE, second); pump(ASTRA_STATUS_OK);
    assert(reply.subscription != 0u); return reply.subscription;
}
static uint32_t root_grant(void *context, uint32_t owner, const AstraVolumeReference *reference,
    uint32_t rights, const AstraVolumeReference *source, int delegates,
    uint32_t *root)
{
    assert(source == expected_source);
    assert(delegates == expected_delegates);
    assert(context == &epoch && owner == sender && rights == ASTRA_RIGHT_READ);
    ++root_calls;
    if (reference->epoch != epoch || reference->id != 5u || reference->generation != 1u)
        return ASTRA_STATUS_LOST;
    *root = 31u;
    return ASTRA_STATUS_OK;
}
static void root_request(uint32_t rights)
{
    enqueue(ASTRA_VOLUME_ROOT, 0u);
    request.header.total_size = sizeof(AstraVolumeRootRequest);
    incoming_size = sizeof(AstraVolumeRootRequest);
    incoming_request.root.reference = (AstraVolumeReference){epoch, 5u, 1u};
    incoming_request.root.rights = rights;
}
static uint32_t apply_control(void *context, uint32_t owner, const AstraVolumeControl *control,
    AstraVolumeReference *reference)
{
    assert(context == &epoch && owner == sender && astra_volume_control_valid(control));
    ++control_calls;
    if (control->reference.generation != 1u) return ASTRA_STATUS_LOST;
    *reference = control->reference;
    return ASTRA_STATUS_OK;
}
static void control_request(void)
{
    enqueue(ASTRA_VOLUME_CONTROL, 0u);
    incoming_size = sizeof(AstraVolumeControlRequest); request.header.total_size = incoming_size;
    incoming_request.control.control = (AstraVolumeControl){.reference = {epoch, 5u, 1u},
        .operation = ASTRA_VOLUME_UNMOUNT};
}
static uint32_t apply_format(void *context, uint32_t owner,
    const AstraVolumeFormatRequest *format, uint32_t *outcome)
{
    assert(context == &epoch && owner == sender && astra_volume_format_valid(format));
    ++format_calls;
    *outcome = format->header.operation == ASTRA_VOLUME_FORMAT_STATUS ?
        ASTRA_STATUS_BUSY : ASTRA_STATUS_OK;
    return ASTRA_STATUS_OK;
}
static uint32_t apply_partition(void *context, uint32_t owner,
    const AstraVolumePartitionRequest *partition, uint32_t *outcome)
{
    assert(context == &epoch && owner == sender &&
        astra_volume_partition_valid(partition));
    ++partition_calls;
    *outcome = partition->header.operation == ASTRA_VOLUME_PARTITION_STATUS ?
        ASTRA_STATUS_BUSY : ASTRA_STATUS_OK;
    return ASTRA_STATUS_OK;
}
static void format_request(uint32_t operation)
{
    enqueue(operation, 0u);
    incoming_size = sizeof(AstraVolumeFormatRequest);
    request.header.total_size = incoming_size;
    incoming_request.format.source = (AstraVolumeReference){epoch, 5u, 1u};
    if (operation == ASTRA_VOLUME_FORMAT) {
        incoming_request.format.format = ASTRA_VOLUME_FAT32;
        strcpy(incoming_request.format.label, "TEST");
    }
}
static void partition_request(uint32_t operation)
{
    enqueue(operation, 0u);
    incoming_size = sizeof(AstraVolumePartitionRequest);
    request.header.total_size = incoming_size;
    incoming_request.partition.device = (AstraVolumeReference){epoch, 5u, 1u};
    if (operation == ASTRA_VOLUME_PARTITION)
        incoming_request.partition.edit = (AstraVolumePartitionEdit){
            .operation = ASTRA_VOLUME_PARTITION_INITIALIZE,
            .map = ASTRA_VOLUME_PARTITION_MAP_MBR};
}
static void usage_request(void)
{
    enqueue(ASTRA_VOLUME_USAGE, 3u);
    incoming_size = sizeof(AstraVolumeUsageRequest); request.header.total_size = incoming_size;
    incoming_request.usage.query = (AstraVolumeUsageQuery){ASTRA_VOLUME_USAGE_VERSION,
        sizeof(AstraVolumeUsageQuery), {0}, 0u, 0u, 0u, 0u, 0u};
}
static void usage_validation(void)
{
    AstraVolumeUsageQuery query = {ASTRA_VOLUME_USAGE_VERSION, sizeof(query), {0}, 0u, 0u, 0u, 0u, 0u};
    AstraVolumeUsagePage page;
    assert(usage(&epoch, &query, &page) == ASTRA_STATUS_OK && astra_volume_usage_page_valid(&query, &page));
    page.records[0].path_length = sizeof(page.records[0].path);
    assert(!astra_volume_usage_page_valid(&query, &page));
    page.records[0].path_length = 10u; page.records[0].flags = ASTRA_VOLUME_USAGE_UNNAMED;
    assert(!astra_volume_usage_page_valid(&query, &page));
    page.records[0].path_length = 0u; page.records[0].path[0] = '\0';
    assert(astra_volume_usage_page_valid(&query, &page));
    page.records[0].volume.epoch = epoch + 1u;
    assert(!astra_volume_usage_page_valid(&query, &page));
    page.records[0].volume.epoch = epoch; page.next = 27u; page.next_kind = ASTRA_VOLUME_USAGE_ROOT;
    assert(!astra_volume_usage_page_valid(&query, &page));
    query.after = 27u;
    assert(!astra_volume_usage_query_valid(&query));
    query.after_kind = ASTRA_VOLUME_USAGE_FILE;
    assert(!astra_volume_usage_query_valid(&query));
    query.sequence = sequence; query.catalog_sequence = 5u; query.volume.epoch = epoch;
    assert(astra_volume_usage_query_valid(&query));
    query.after = query.after_kind = query.after_process = 0u;
    assert(usage(&epoch, &query, &page) == ASTRA_STATUS_OK);
    page.records[0].kind = ASTRA_VOLUME_USAGE_ROOT; page.records[0].process = 7u;
    page.records[1] = page.records[0]; page.records[1].process = 9u; page.count = 2u;
    page.next = page.records[1].handle; page.next_kind = ASTRA_VOLUME_USAGE_ROOT; page.next_process = 9u;
    assert(astra_volume_usage_page_valid(&query, &page));
    page.next_process = 7u;
    assert(!astra_volume_usage_page_valid(&query, &page));
    page.next_process = 9u; page.records[1].process = 7u;
    assert(!astra_volume_usage_page_valid(&query, &page));
    page.records[1].process = 9u; page.reserved = 1u;
    assert(!astra_volume_usage_page_valid(&query, &page));
    query.after_process = 7u;
    assert(!astra_volume_usage_query_valid(&query));
}
static void query_validation(void)
{
    AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION, sizeof(query), 0u, 0u, 0u};
    AstraVolumeCatalogPage page;
    assert(catalog(&epoch, &query, &page) == 0u && astra_volume_page_valid(&query, &page));
    query.after = 1u; assert(!astra_volume_query_valid(&query)); query.after = 0u;
    page.records[0].id = 0u; assert(!astra_volume_page_valid(&query, &page));
    page.records[0].id = 5u; page.next = 4u; assert(!astra_volume_page_valid(&query, &page));
    page.next = 5u; memset(page.records[0].label, 'x', sizeof(page.records[0].label));
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].label[0] = '\0'; page.count = ASTRA_VOLUME_PAGE_RECORDS + 1u;
    assert(!astra_volume_page_valid(&query, &page));
    page.count = 1u;
    strcpy(page.records[0].label, "\xc0\xaf"); /* An overlong slash is not UTF-8. */
    assert(!astra_volume_page_valid(&query, &page));
    strcpy(page.records[0].label, "\xf0\x9f\xaa\x90");
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].parent = 1u;
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].parent_generation = 2u;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].format = ASTRA_VOLUME_FAT12;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].format = ASTRA_VOLUME_NTFS;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].format = ASTRA_VOLUME_UNSUPPORTED;
    assert(!astra_volume_page_valid(&query, &page));
    strcpy(page.records[0].detected_type, "btrfs");
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].format = ASTRA_VOLUME_UNSUPPORTED + 1u;
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].format = ASTRA_VOLUME_FAT32;
    page.records[0].kind = ASTRA_VOLUME_DEVICE;
    page.records[0].class_id = ASTRA_VOLUME_USB;
    page.records[0].state = ASTRA_VOLUME_DISCOVERED;
    page.records[0].origin = ASTRA_VOLUME_ORIGIN_DEVICE;
    page.records[0].flags = ASTRA_VOLUME_PHYSICAL_READ_ONLY | ASTRA_VOLUME_READ_ONLY;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].flags = ASTRA_VOLUME_PHYSICAL_READ_ONLY;
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].flags = 0u;
    page.records[0].state = ASTRA_VOLUME_PARTITIONING;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].class_id = ASTRA_VOLUME_OPTICAL;
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].class_id = ASTRA_VOLUME_USB;
    page.records[0].kind = ASTRA_VOLUME_SOURCE;
    assert(!astra_volume_page_valid(&query, &page));
    page.records[0].state = ASTRA_VOLUME_FORMATTING;
    assert(astra_volume_page_valid(&query, &page));
    page.records[0].origin = ASTRA_VOLUME_ORIGIN_RAM;
    assert(!astra_volume_page_valid(&query, &page));
}
int main(void)
{
    query_validation(); usage_validation();
    assert(astra_volume_port_init(&port, 1u, 2u, catalog, &epoch));
    port.root = root_grant;
    port.control = apply_control;
    port.format = apply_format;
    port.partition = apply_partition;
    port.usage = usage;
    usage_request(); pump(ASTRA_STATUS_ACCESS);
    usage_request();
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, NULL, 0, 1u) == 1u && reply.status == ASTRA_STATUS_ACCESS);
    usage_request();
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_ACCESS);
    area_bytes = sizeof(output_storage.usage);
    usage_request();
    assert(astra_volume_port_usage_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_OK &&
        output_storage.usage.records[0].process == 17u && output_storage.usage.records[0].handle == 27u);
    usage_request(); area_bytes = 1u;
    assert(astra_volume_port_usage_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_BUFFER_TOO_SMALL);
    usage_request(); area_bytes = sizeof(output_storage.usage); malformed_page = 1;
    assert(astra_volume_port_usage_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_PROTOCOL);
    malformed_page = 0; area_bytes = sizeof(output);
    root_request(ASTRA_RIGHT_READ);
    assert(astra_volume_port_usage_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_ACCESS);
    control_request(); pump(ASTRA_STATUS_ACCESS); assert(control_calls == 0u && admitted.id == 0u);
    control_request();
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_OK && admitted.id == 5u);
    control_request(); incoming_request.control.control.reference.generation = 2u;
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_LOST && admitted.id == 0u);
    control_request(); incoming_request.control.control.flags = 1u;
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_INVALID);
    format_request(ASTRA_VOLUME_FORMAT);
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_ACCESS && format_calls == 0u);
    format_request(ASTRA_VOLUME_FORMAT);
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_OK && format_calls == 1u &&
        format_outcome == ASTRA_STATUS_OK);
    format_request(ASTRA_VOLUME_FORMAT_STATUS);
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_OK && format_calls == 2u &&
        format_outcome == ASTRA_STATUS_BUSY);
    format_request(ASTRA_VOLUME_FORMAT); incoming_request.format.label[5] = 'X';
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_INVALID && format_calls == 2u);
    partition_request(ASTRA_VOLUME_PARTITION);
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_ACCESS && partition_calls == 0u);
    partition_request(ASTRA_VOLUME_PARTITION);
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_OK && partition_calls == 1u &&
        format_outcome == ASTRA_STATUS_OK);
    partition_request(ASTRA_VOLUME_PARTITION_STATUS);
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_OK && partition_calls == 2u &&
        format_outcome == ASTRA_STATUS_BUSY);
    partition_request(ASTRA_VOLUME_PARTITION);
    incoming_request.partition.edit.number = 1u;
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_INVALID && partition_calls == 2u);
    control_request();
    assert(astra_volume_port_format_pump(&port, 5u, 1u) == 1u &&
        reply.status == ASTRA_STATUS_ACCESS && control_calls == 2u);
    root_request(ASTRA_RIGHT_READ);
    assert(astra_volume_port_control_pump(&port, 5u, 1u) == 1u && reply.status == ASTRA_STATUS_ACCESS);
    root_request(ASTRA_RIGHT_READ); pump(ASTRA_STATUS_ACCESS); assert(root_calls == 0u);
    root_request(ASTRA_RIGHT_READ);
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 0, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_OK && root_sent == 31u && root_calls == 1u);
    root_request(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE);
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 0, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_ACCESS && root_calls == 1u);
    expected_delegates = 1;
    root_request(ASTRA_RIGHT_READ);
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 1, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_OK && root_calls == 2u);
    expected_delegates = 0;
    root_request(ASTRA_RIGHT_READ); incoming_request.root.reference.generation = 2u;
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 0, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_LOST);
    root_request(ASTRA_RIGHT_READ); root_send_failure = 1u;
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 0, 1u) == 1u && closed[31] == 1u);
    root_send_failure = 0u;
    const AstraVolumeReference ram_source = {epoch, 4u, 2u};
    expected_source = &ram_source;
    expected_delegates = 1;
    root_request(ASTRA_RIGHT_READ);
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, &ram_source, 1, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_OK && root_sent == 31u);
    expected_source = NULL;
    expected_delegates = 0;
    enqueue(ASTRA_VOLUME_BROKER_INFO, 0u);
    incoming_size = sizeof(AstraMessageHeader); request.header.total_size = incoming_size;
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, &ram_source, 1, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_OK && broker_info.source.id == 4u && broker_info.source.generation == 2u);
    pending = 1;
    assert(astra_volume_port_root_pump(&port, 5u, ASTRA_RIGHT_READ, NULL, 0, 1u) == 1u);
    assert(reply.status == ASTRA_STATUS_OK && broker_info.source.id == 0u && broker_info.rights == ASTRA_RIGHT_READ);
    pending = 1; pump(ASTRA_STATUS_ACCESS);
    enqueue(ASTRA_VOLUME_QUERY, 3u);
    request.query = (AstraVolumeCatalogQuery){ASTRA_VOLUME_CATALOG_VERSION, sizeof(request.query), 0u, 0u, 0u};
    pump(ASTRA_STATUS_OK);
    assert(output.records[0].id == 5u && closed[3] >= 1u);
    request.query.epoch = epoch; request.query.sequence = sequence; request.query.after = output.next;
    pending = 1; pump(ASTRA_STATUS_OK); assert(output.count == 0u);
    ++sequence; pending = 1; pump(ASTRA_STATUS_BUSY);
    ++epoch; pending = 1; pump(ASTRA_STATUS_LOST);
    enqueue(ASTRA_VOLUME_QUERY, 3u);
    request.query = (AstraVolumeCatalogQuery){ASTRA_VOLUME_CATALOG_VERSION, sizeof(request.query), 0u, 0u, 0u};
    area_bytes = 1u; pump(ASTRA_STATUS_BUFFER_TOO_SMALL); area_bytes = sizeof(output);
    pending = 1; area_rights = 0u; pump(ASTRA_STATUS_ACCESS); area_rights = 1u;
    pending = 1; malformed_page = 1; pump(ASTRA_STATUS_PROTOCOL); malformed_page = 0;
    enqueue(ASTRA_VOLUME_QUERY, 3u); request.header.flags = 1u; pump(ASTRA_STATUS_PROTOCOL);
    enqueue(ASTRA_VOLUME_QUERY, 3u); sent_handles[2] = 4u; sent_count = 3u;
    pump(ASTRA_STATUS_PROTOCOL); assert(closed[4] == 1u);
    uint64_t first = watch(10u), second = watch(11u);
    assert(first != second && queued[0] && notifications[0].change.sequence == sequence);
    enqueue(ASTRA_VOLUME_SUBSCRIBE, 12u); pump(ASTRA_STATUS_LIMIT);
    sender = 18u; uint64_t other = watch(12u);
    enqueue(ASTRA_VOLUME_UNSUBSCRIBE, 0u); request.subscription = first; pump(ASTRA_STATUS_ACCESS);
    sender = 17u;
    ++sequence; astra_volume_port_poll(&port); /* Full queues retain pending latest state. */
    ++sequence; astra_volume_port_poll(&port);
    queued[0] = 0u; astra_volume_port_poll(&port);
    assert(notifications[0].change.sequence == sequence);
    catalog_status = ASTRA_STATUS_LOST; queued[0] = 0u; astra_volume_port_poll(&port);
    assert(notifications[0].change.status == ASTRA_STATUS_LOST && notifications[0].change.epoch == 0u);
    catalog_status = 0u; queued[0] = 0u; astra_volume_port_poll(&port);
    assert(notifications[0].change.status == 0u && notifications[0].change.epoch == epoch);
    dead[1] = 1u; astra_volume_port_poll(&port); assert(closed[11] == 1u);
    enqueue(ASTRA_VOLUME_UNSUBSCRIBE, 0u); request.subscription = second; pump(ASTRA_STATUS_BAD_HANDLE);
    enqueue(ASTRA_VOLUME_UNSUBSCRIBE, 0u); request.subscription = first; pump(ASTRA_STATUS_OK);
    assert(closed[10] == 1u);
    enqueue(ASTRA_VOLUME_SUBSCRIBE, 13u); allocation_failure = 1;
    pump(ASTRA_STATUS_LIMIT); allocation_failure = 0;
    sender = 18u; enqueue(ASTRA_VOLUME_UNSUBSCRIBE, 0u); request.subscription = other; pump(ASTRA_STATUS_OK);
    assert(port.subscribers == NULL);
    sender = 17u;
    for (uint32_t at = 0u; at < 1000u; ++at) {
        queued[0] = 0u; first = watch(10u);
        enqueue(ASTRA_VOLUME_UNSUBSCRIBE, 0u); request.subscription = first; pump(ASTRA_STATUS_OK);
        assert(port.subscribers == NULL);
    }
    port.next_subscription = UINT64_MAX;
    enqueue(ASTRA_VOLUME_SUBSCRIBE, 13u); pump(ASTRA_STATUS_LIMIT);
    astra_volume_port_destroy(&port);
    puts("Native volume catalog/subscriptions: PASS");
    return 0;
}
