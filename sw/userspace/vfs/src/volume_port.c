#include <astra/volume_port.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <string.h>

struct AstraVolumeSubscriber {
    AstraVolumeSubscriber *next;
    uint64_t id;
    uint32_t owner;
    uint32_t send;
    AstraVolumeChange delivered;
};

static int header_valid(const AstraMessageHeader *header, uint32_t size)
{
    return header->total_size == size &&
        header->header_size == ASTRA_MESSAGE_HEADER_SIZE && header->flags == 0u &&
        header->reserved == 0u && header->protocol == ASTRA_VOLUME_PROTOCOL &&
        header->protocol_version == ASTRA_VOLUME_PROTOCOL_VERSION &&
        header->transaction_id != 0u;
}

static int query_empty(const AstraVolumeCatalogQuery *query)
{
    return query->version == 0u && query->size == 0u && query->epoch == 0u &&
        query->sequence == 0u && query->after == 0u;
}

int astra_volume_port_init(AstraVolumePort *port, uint32_t receive,
    uint32_t owner_limit, AstraVolumeReadCatalog read, void *context)
{
    if (port == NULL || receive == 0u || owner_limit == 0u || read == NULL) return 0;
    memset(port, 0, sizeof(*port));
    port->receive = receive;
    port->owner_limit = owner_limit;
    port->next_subscription = 1u;
    port->read = read;
    port->context = context;
    port->observed.status = ASTRA_STATUS_LOST;
    return 1;
}

static uint32_t notify(AstraVolumePort *port, AstraVolumeSubscriber *subscriber)
{
    AstraVolumeNotification message = {0};
    astra_message_header_set(&message.header, sizeof(message), ASTRA_VOLUME_PROTOCOL,
        ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_CHANGED, 0u);
    message.subscription = subscriber->id;
    message.change = port->observed;
    uint32_t status = astra_port_send(subscriber->send, &message, sizeof(message), NULL, 0u);
    if (status == ASTRA_SYSCALL_OK) subscriber->delivered = port->observed;
    return status;
}

void astra_volume_port_poll(AstraVolumePort *port)
{
    const AstraVolumeCatalogQuery query = {ASTRA_VOLUME_CATALOG_VERSION,
        sizeof(AstraVolumeCatalogQuery), 0u, 0u, 0u};
    memset(&port->page, 0, sizeof(port->page));
    port->observed = (AstraVolumeChange){0};
    port->observed.status = port->read(port->context, &query, &port->page);
    if (port->observed.status == ASTRA_STATUS_OK) {
        if (!astra_volume_page_valid(&query, &port->page))
            port->observed.status = ASTRA_STATUS_PROTOCOL;
        else {
            port->observed.epoch = port->page.epoch;
            port->observed.sequence = port->page.sequence;
        }
    }
    AstraVolumeSubscriber **at = &port->subscribers;
    while (*at != NULL) {
        AstraVolumeSubscriber *subscriber = *at;
        uint32_t status = astra_wait_one(subscriber->send, 0u, NULL);
        if (status == ASTRA_SYSCALL_OK || status == ASTRA_SYSCALL_TIMED_OUT ||
            status == ASTRA_SYSCALL_WOULD_BLOCK) {
            if (memcmp(&subscriber->delivered, &port->observed, sizeof(port->observed)) != 0)
                status = notify(port, subscriber);
            else status = ASTRA_SYSCALL_OK;
        }
        if (status != ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_WOULD_BLOCK &&
            status != ASTRA_SYSCALL_TIMED_OUT) {
            *at = subscriber->next;
            (void)astra_close(subscriber->send);
            astra_runtime_deallocate(subscriber);
        } else at = &subscriber->next;
    }
}

static uint32_t subscribe(AstraVolumePort *port, uint32_t owner, uint32_t *send, uint64_t *id)
{
    uint32_t held = 0u;
    for (AstraVolumeSubscriber *at = port->subscribers; at != NULL; at = at->next)
        if (at->owner == owner) ++held;
    if (held >= port->owner_limit || port->next_subscription == UINT64_MAX)
        return ASTRA_STATUS_LIMIT;
    AstraVolumeSubscriber *subscriber = astra_runtime_callocate(1u, sizeof(*subscriber));
    if (subscriber == NULL) return ASTRA_STATUS_LIMIT;
    subscriber->id = port->next_subscription++;
    subscriber->owner = owner;
    subscriber->send = *send;
    uint32_t status = notify(port, subscriber);
    if (status != ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_WOULD_BLOCK) {
        astra_runtime_deallocate(subscriber);
        return status == ASTRA_SYSCALL_PEER_DEAD ? ASTRA_STATUS_PEER_DEAD : ASTRA_STATUS_BAD_HANDLE;
    }
    subscriber->next = port->subscribers;
    port->subscribers = subscriber;
    *send = 0u;
    *id = subscriber->id;
    return ASTRA_STATUS_OK;
}

static uint32_t unsubscribe(AstraVolumePort *port, uint32_t owner, uint64_t id)
{
    AstraVolumeSubscriber **at = &port->subscribers;
    while (*at != NULL) {
        AstraVolumeSubscriber *subscriber = *at;
        if (subscriber->id == id) {
            if (subscriber->owner != owner) return ASTRA_STATUS_ACCESS;
            *at = subscriber->next;
            (void)astra_close(subscriber->send);
            astra_runtime_deallocate(subscriber);
            return ASTRA_STATUS_OK;
        }
        at = &subscriber->next;
    }
    return ASTRA_STATUS_BAD_HANDLE;
}

static uint32_t pump(AstraVolumePort *port, uint32_t receive, uint32_t rights,
    const AstraVolumeReference *source, int role, uint32_t budget)
{
    union { AstraMessageHeader header; AstraVolumeRequest request;
        AstraVolumeRootRequest root;
        AstraVolumeControlRequest control;
        AstraVolumeUsageRequest usage;
        uint8_t bytes[ASTRA_MESSAGE_SIZE_MAX]; } incoming;
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX];
    uint32_t served = 0u;
    while (served < budget) {
        uint32_t size = 0u, count = 0u, owner = 0u;
        uint32_t result = astra_port_receive_from(receive, &incoming, sizeof(incoming),
            handles, ASTRA_MESSAGE_HANDLES_MAX, &size, &count, &owner);
        if (result != ASTRA_SYSCALL_OK) break;
        ++served;
        union { AstraVolumeControlReply control; AstraVolumeBrokerInfoReply broker; } response = {0};
        AstraVolumeReply reply = {0};
        uint32_t reply_size = incoming.header.operation == ASTRA_VOLUME_CONTROL ?
            sizeof(response.control) : incoming.header.operation == ASTRA_VOLUME_BROKER_INFO ?
            sizeof(response.broker) : sizeof(reply);
        astra_message_header_set(&reply.header, sizeof(reply), ASTRA_VOLUME_PROTOCOL,
            ASTRA_VOLUME_PROTOCOL_VERSION, ASTRA_VOLUME_REPLY, incoming.header.transaction_id);
        reply.status = ASTRA_STATUS_PROTOCOL;
        uint32_t root = 0u;
        if (owner != 0u && header_valid(&incoming.header, size)) {
            const AstraVolumeRequest *request = &incoming.request;
            if (incoming.header.operation == ASTRA_VOLUME_CONTROL && size == sizeof(incoming.control) && count == 1u) {
                if (!astra_volume_control_valid(&incoming.control.control))
                    reply.status = ASTRA_STATUS_INVALID;
                else if (role != 1 || port->control == NULL)
                    reply.status = ASTRA_STATUS_ACCESS;
                else
                    reply.status = port->control(port->context, owner, &incoming.control.control, &response.control.reference);
                if (reply.status != ASTRA_STATUS_OK)
                    memset(&response.control.reference, 0, sizeof(response.control.reference));
                else if (response.control.reference.epoch == 0u || response.control.reference.id == 0u ||
                         response.control.reference.generation == 0u)
                    reply.status = ASTRA_STATUS_PROTOCOL;
            } else if (incoming.header.operation == ASTRA_VOLUME_USAGE &&
                size == sizeof(incoming.usage) && count == 2u) {
                if (!astra_volume_usage_query_valid(&incoming.usage.query))
                    reply.status = ASTRA_STATUS_INVALID;
                else if (role != 2 || port->usage == NULL)
                    reply.status = ASTRA_STATUS_ACCESS;
                else {
                    reply.status = port->usage(port->context, &incoming.usage.query, &port->usage_page);
                    if (reply.status == ASTRA_STATUS_OK) {
                        void *mapped = NULL;
                        uint32_t bytes = 0u;
                        if (!astra_volume_usage_page_valid(&incoming.usage.query, &port->usage_page))
                            reply.status = ASTRA_STATUS_PROTOCOL;
                        else if (astra_rt_area_map(handles[1], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                            &mapped, &bytes) != ASTRA_SYSCALL_OK) reply.status = ASTRA_STATUS_ACCESS;
                        else if (bytes < sizeof(port->usage_page)) reply.status = ASTRA_STATUS_BUFFER_TOO_SMALL;
                        else memcpy(mapped, &port->usage_page, sizeof(port->usage_page));
                        if (mapped != NULL && astra_rt_area_unmap(mapped) != ASTRA_SYSCALL_OK &&
                            reply.status == ASTRA_STATUS_OK) reply.status = ASTRA_STATUS_IO;
                    }
                }
            } else if (incoming.header.operation == ASTRA_VOLUME_BROKER_INFO &&
                size == sizeof(AstraMessageHeader) && count == 1u) {
                if (role != 0 || rights == 0u) reply.status = ASTRA_STATUS_ACCESS;
                else {
                    reply.status = ASTRA_STATUS_OK;
                    response.broker.info.rights = rights;
                    if (source != NULL) response.broker.info.source = *source;
                }
            } else if (role != 0)
                reply.status = ASTRA_STATUS_ACCESS;
            else if (incoming.header.operation == ASTRA_VOLUME_ROOT && size == sizeof(incoming.root) && count == 1u) {
                const AstraVolumeRootRequest *wanted = &incoming.root;
                if (wanted->reserved != 0u || wanted->reference.epoch == 0u ||
                    wanted->reference.id == 0u || wanted->reference.generation == 0u ||
                    (wanted->rights & ASTRA_RIGHT_READ) == 0u ||
                    (wanted->rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u)
                    reply.status = ASTRA_STATUS_INVALID;
                else if (port->root == NULL || (wanted->rights & rights) != wanted->rights)
                    reply.status = ASTRA_STATUS_ACCESS;
                else
                    reply.status = port->root(port->context, owner, &wanted->reference, wanted->rights, source, &root);
                if (reply.status == ASTRA_STATUS_OK && root == 0u)
                    reply.status = ASTRA_STATUS_PROTOCOL;
            } else if (rights != 0u)
                reply.status = ASTRA_STATUS_ACCESS;
            else if (size != sizeof(*request))
                reply.status = ASTRA_STATUS_PROTOCOL;
            else if (request->header.operation == ASTRA_VOLUME_QUERY && count == 2u &&
                request->subscription == 0u && astra_volume_query_valid(&request->query)) {
                reply.status = port->read(port->context, &request->query, &port->page);
                if (reply.status == ASTRA_STATUS_OK) {
                    void *mapped = NULL;
                    uint32_t bytes = 0u;
                    if (!astra_volume_page_valid(&request->query, &port->page))
                        reply.status = ASTRA_STATUS_PROTOCOL;
                    else if (astra_rt_area_map(handles[1], ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                        &mapped, &bytes) != ASTRA_SYSCALL_OK) reply.status = ASTRA_STATUS_ACCESS;
                    else if (bytes < sizeof(port->page)) reply.status = ASTRA_STATUS_BUFFER_TOO_SMALL;
                    else memcpy(mapped, &port->page, sizeof(port->page));
                    if (mapped != NULL && astra_rt_area_unmap(mapped) != ASTRA_SYSCALL_OK &&
                        reply.status == ASTRA_STATUS_OK) reply.status = ASTRA_STATUS_IO;
                }
            } else if (request->header.operation == ASTRA_VOLUME_SUBSCRIBE && count == 2u &&
                request->subscription == 0u && query_empty(&request->query)) {
                astra_volume_port_poll(port);
                reply.status = subscribe(port, owner, &handles[1], &reply.subscription);
            } else if (request->header.operation == ASTRA_VOLUME_UNSUBSCRIBE && count == 1u &&
                request->subscription != 0u && query_empty(&request->query))
                reply.status = unsubscribe(port, owner, request->subscription);
        }
        if (count != 0u) {
            reply.header.total_size = reply_size;
            if (reply.status == ASTRA_STATUS_OK && root != 0u) {
                const AstraHandleDisposition disposition = {root, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                    (source != NULL ? ASTRA_RIGHT_TRANSFER : 0u)};
                if (astra_port_send_rights(handles[0], &reply, sizeof(reply), &disposition, 1u) == ASTRA_SYSCALL_OK)
                    root = 0u;
            } else {
                response.control.reply = reply;
                (void)astra_port_send(handles[0], &response, reply_size, NULL, 0u);
            }
        }
        if (root != 0u) (void)astra_close(root);
        for (uint32_t at = 0u; at < count; ++at)
            if (handles[at] != 0u) (void)astra_close(handles[at]);
    }
    return served;
}

uint32_t astra_volume_port_pump(AstraVolumePort *port, uint32_t budget)
{ return pump(port, port->receive, 0u, NULL, 0, budget); }

uint32_t astra_volume_port_root_pump(AstraVolumePort *port, uint32_t receive,
    uint32_t rights, const AstraVolumeReference *source, uint32_t budget)
{
    if ((rights & ASTRA_RIGHT_READ) == 0u ||
        (rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u ||
        (source != NULL && (source->epoch == 0u || source->id == 0u || source->generation == 0u))) return 0u;
    return pump(port, receive, rights, source, 0, budget);
}

uint32_t astra_volume_port_control_pump(AstraVolumePort *port, uint32_t receive, uint32_t budget)
{ return pump(port, receive, 0u, NULL, 1, budget); }

uint32_t astra_volume_port_usage_pump(AstraVolumePort *port, uint32_t receive, uint32_t budget)
{ return pump(port, receive, 0u, NULL, 2, budget); }

void astra_volume_port_destroy(AstraVolumePort *port)
{
    while (port->subscribers != NULL) {
        AstraVolumeSubscriber *subscriber = port->subscribers;
        port->subscribers = subscriber->next;
        (void)astra_close(subscriber->send);
        astra_runtime_deallocate(subscriber);
    }
    memset(port, 0, sizeof(*port));
}
