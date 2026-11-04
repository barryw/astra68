#include <messaging.h>

#include <astra/messaging_abi.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <string.h>

#define MESSAGING_ENDPOINT_MAX 128u
#define MESSAGING_SUBSCRIPTION_MAX 128u

typedef struct MessagingEndpoint {
    struct MessagingEndpoint *next;
    uint32_t process_id;
    uint32_t send;
} MessagingEndpoint;

typedef struct MessagingSubscription {
    struct MessagingSubscription *next;
    uint32_t process_id;
    uint32_t send;
    uint32_t kind;
    uint32_t publisher_id;
    uint64_t id;
    uint64_t sequence;
    char name[ASTRA_MESSAGING_NAME_MAX];
    char topic[ASTRA_MESSAGING_TOPIC_MAX];
} MessagingSubscription;

static MessagingEndpoint *endpoints;
static MessagingSubscription *subscriptions;
static uint32_t endpoint_count;
static uint32_t subscription_count;
static uint64_t next_subscription = 1u;

static uint32_t live_process(uint32_t id, char *name)
{
    uint32_t count = 0u;
    uint32_t status = astra_process_snapshot(
        supervisor_loader_process_handle(), NULL, 0u, &count);
    if (status != ASTRA_SYSCALL_BUFFER_TOO_SMALL && status != ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_IO;
    for (uint32_t attempt = 0u; attempt < 2u; ++attempt) {
        if (count == 0u) return ASTRA_STATUS_PEER_DEAD;
#if SIZE_MAX < UINT32_MAX
        if (count > SIZE_MAX / sizeof(AstraProcSnapshot)) return ASTRA_STATUS_LIMIT;
#endif
        AstraProcSnapshot *records = astra_runtime_allocate(
            (size_t)count * sizeof(*records));
        if (records == NULL) return ASTRA_STATUS_LIMIT;
        uint32_t capacity = count;
        status = astra_process_snapshot(supervisor_loader_process_handle(),
            records, capacity, &count);
        if (status == ASTRA_SYSCALL_OK) {
            uint32_t found = ASTRA_STATUS_PEER_DEAD;
            for (uint32_t at = 0u; at < count; ++at)
                if (records[at].process.id == id) {
                    found = ASTRA_STATUS_OK;
                    if (name != NULL) {
                        const char *end = memchr(records[at].name, '\0',
                            sizeof(records[at].name));
                        if (end != NULL)
                            memcpy(name, records[at].name,
                                (size_t)(end - records[at].name) + 1u);
                    }
                    break;
                }
            astra_runtime_deallocate(records);
            return found;
        }
        astra_runtime_deallocate(records);
        if (status != ASTRA_SYSCALL_BUFFER_TOO_SMALL) return ASTRA_STATUS_IO;
    }
    return ASTRA_STATUS_BUSY;
}

static const SupervisorProcessRecord *process_record(
    const SupervisorProcessTable *processes, uint32_t id)
{
    for (uint32_t at = 0u; at < processes->count; ++at)
        if (processes->records[at].id == id)
            return &processes->records[at];
    return NULL;
}

static uint32_t identity(const SupervisorProcessRecord *record,
    uint32_t *kind, char name[ASTRA_MESSAGING_NAME_MAX])
{
    const char *source = NULL;
    if (record->service_name[0] != '\0') {
        *kind = ASTRA_MESSAGING_SERVICE;
        source = record->service_name;
    } else if (record->bundle[0] != '\0') {
        *kind = ASTRA_MESSAGING_APPLICATION;
        source = record->bundle;
        for (const char *at = source; *at != '\0'; ++at)
            if (*at == '/') source = at + 1u;
    } else {
        *kind = ASTRA_MESSAGING_COMMAND;
        name[0] = '\0';
        return ASTRA_STATUS_OK;
    }
    size_t length = strlen(source);
    if (length >= ASTRA_MESSAGING_NAME_MAX) return ASTRA_STATUS_NAME_TOO_LONG;
    memcpy(name, source, length + 1u);
    return ASTRA_STATUS_OK;
}

static int terminated(const char *text, uint32_t size)
{
    return memchr(text, '\0', size) != NULL;
}

static MessagingEndpoint *endpoint(uint32_t id)
{
    for (MessagingEndpoint *at = endpoints; at != NULL; at = at->next)
        if (at->process_id == id) return at;
    return NULL;
}

static uint32_t register_endpoint(uint32_t owner, uint32_t *send)
{
    if (*send == 0u) return ASTRA_STATUS_BAD_HANDLE;
    uint32_t state = astra_wait_one(*send, 0u, NULL);
    if (state == ASTRA_SYSCALL_PEER_DEAD) return ASTRA_STATUS_PEER_DEAD;
    if (state != ASTRA_SYSCALL_OK && state != ASTRA_SYSCALL_TIMED_OUT &&
        state != ASTRA_SYSCALL_WOULD_BLOCK) return ASTRA_STATUS_ACCESS;
    if (endpoint(owner) != NULL) return ASTRA_STATUS_EXISTS;
    if (endpoint_count == MESSAGING_ENDPOINT_MAX) return ASTRA_STATUS_LIMIT;
    MessagingEndpoint *entry = astra_runtime_callocate(1u, sizeof(*entry));
    if (entry == NULL) return ASTRA_STATUS_LIMIT;
    entry->process_id = owner;
    entry->send = *send;
    entry->next = endpoints;
    endpoints = entry;
    ++endpoint_count;
    *send = 0u;
    return ASTRA_STATUS_OK;
}

static uint32_t unregister_endpoint(uint32_t owner)
{
    MessagingEndpoint **at = &endpoints;
    while (*at != NULL) {
        MessagingEndpoint *entry = *at;
        if (entry->process_id == owner) {
            *at = entry->next;
            (void)astra_close(entry->send);
            astra_runtime_deallocate(entry);
            --endpoint_count;
            return ASTRA_STATUS_OK;
        }
        at = &entry->next;
    }
    return ASTRA_STATUS_NOT_FOUND;
}

static uint32_t subscribe(uint32_t owner, const AstraMessagingRequest *request,
    uint32_t *send, uint64_t *id)
{
    if (*send == 0u || request->kind < ASTRA_MESSAGING_SERVICE ||
        request->kind > ASTRA_MESSAGING_COMMAND || request->topic[0] == '\0' ||
        (request->kind == ASTRA_MESSAGING_COMMAND ?
            request->process_id == 0u : request->name[0] == '\0'))
        return ASTRA_STATUS_INVALID;
    if (subscription_count == MESSAGING_SUBSCRIPTION_MAX ||
        next_subscription == UINT64_MAX)
        return ASTRA_STATUS_LIMIT;
    MessagingSubscription *entry = astra_runtime_callocate(1u, sizeof(*entry));
    if (entry == NULL) return ASTRA_STATUS_LIMIT;
    entry->process_id = owner;
    entry->send = *send;
    entry->kind = request->kind;
    entry->publisher_id = request->process_id;
    entry->id = next_subscription++;
    memcpy(entry->name, request->name, sizeof(entry->name));
    memcpy(entry->topic, request->topic, sizeof(entry->topic));
    entry->next = subscriptions;
    subscriptions = entry;
    ++subscription_count;
    *send = 0u;
    *id = entry->id;
    return ASTRA_STATUS_OK;
}

static uint32_t unsubscribe(uint32_t owner, uint64_t id)
{
    MessagingSubscription **at = &subscriptions;
    while (*at != NULL) {
        MessagingSubscription *entry = *at;
        if (entry->id == id) {
            if (entry->process_id != owner) return ASTRA_STATUS_ACCESS;
            *at = entry->next;
            (void)astra_close(entry->send);
            astra_runtime_deallocate(entry);
            --subscription_count;
            return ASTRA_STATUS_OK;
        }
        at = &entry->next;
    }
    return ASTRA_STATUS_NOT_FOUND;
}

void supervisor_messaging_remove(uint32_t process_id)
{
    (void)unregister_endpoint(process_id);
    MessagingSubscription **at = &subscriptions;
    while (*at != NULL) {
        MessagingSubscription *entry = *at;
        if (entry->process_id == process_id) {
            *at = entry->next;
            (void)astra_close(entry->send);
            astra_runtime_deallocate(entry);
            --subscription_count;
        } else at = &entry->next;
    }
}

static void prune_commands(const SupervisorProcessTable *processes)
{
    for (MessagingEndpoint *at = endpoints; at != NULL;) {
        uint32_t id = at->process_id;
        uint32_t state = astra_wait_one(at->send, 0u, NULL);
        at = at->next;
        if (state == ASTRA_SYSCALL_PEER_DEAD ||
            (process_record(processes, id) == NULL &&
             live_process(id, NULL) == ASTRA_STATUS_PEER_DEAD))
            supervisor_messaging_remove(id);
    }
    for (MessagingSubscription *at = subscriptions; at != NULL;) {
        uint32_t id = at->process_id;
        if (process_record(processes, id) == NULL &&
            live_process(id, NULL) == ASTRA_STATUS_PEER_DEAD) {
            supervisor_messaging_remove(id);
            at = subscriptions;
        } else at = at->next;
    }
}

static uint32_t publish(uint32_t owner, uint32_t kind, const char *name,
    const AstraMessagingRequest *request, uint32_t response,
    AstraMessagingReply *reply)
{
    MessagingSubscription **at = &subscriptions;
    while (*at != NULL) {
        MessagingSubscription *entry = *at;
        if (entry->kind != kind ||
            (entry->publisher_id != 0u && entry->publisher_id != owner) ||
            ((kind != ASTRA_MESSAGING_COMMAND || entry->name[0] != '\0') &&
                strcmp(entry->name, name) != 0) ||
            strcmp(entry->topic, request->topic) != 0) {
            at = &entry->next;
            continue;
        }
        AstraMessagingEvent event = {0};
        uint32_t response_copy = 0u;
        ++entry->sequence;
        astra_message_header_set(&event.header, sizeof(event),
            ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION,
            ASTRA_MESSAGING_EVENT, request->header.transaction_id);
        event.kind = kind;
        event.process_id = owner;
        event.subscription = entry->id;
        event.sequence = entry->sequence;
        memcpy(event.name, name, strlen(name) + 1u);
        memcpy(event.topic, request->topic, strlen(request->topic) + 1u);
        event.payload_size = request->payload_size;
        memcpy(event.payload, request->payload, request->payload_size);
        if (response != 0u &&
            astra_rt_handle_duplicate(response,
                ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER,
                &response_copy) != ASTRA_SYSCALL_OK) {
            ++reply->dropped;
            at = &entry->next;
            continue;
        }
        uint32_t sent = astra_port_send(entry->send, &event, sizeof(event),
            response_copy != 0u ? &response_copy : NULL,
            response_copy != 0u ? 1u : 0u);
        if (response_copy != 0u) (void)astra_close(response_copy);
        if (sent == ASTRA_SYSCALL_OK) {
            ++reply->delivered;
            at = &entry->next;
        } else if (sent == ASTRA_SYSCALL_PEER_DEAD ||
                   sent == ASTRA_SYSCALL_INVALID_HANDLE) {
            *at = entry->next;
            (void)astra_close(entry->send);
            astra_runtime_deallocate(entry);
            --subscription_count;
            ++reply->dropped;
        } else {
            ++reply->dropped;
            at = &entry->next;
        }
    }
    return ASTRA_STATUS_OK;
}

uint32_t supervisor_messaging_start(uint32_t *receive, uint32_t *send)
{
    if (receive == NULL || send == NULL) return ASTRA_STATUS_INVALID;
    return astra_rt_port_create(8u, 8u * sizeof(AstraMessagingRequest),
        receive, send) == ASTRA_SYSCALL_OK ? ASTRA_STATUS_OK :
        ASTRA_STATUS_LIMIT;
}

void supervisor_messaging_pump(uint32_t receive,
    const SupervisorProcessTable *processes)
{
    AstraMessagingRequest request = {0};
    AstraMessagingReply reply = {0};
    uint32_t handles[3] = {0};
    uint32_t size = 0u, count = 0u, owner = 0u, target = 0u;
    uint32_t status = astra_port_receive_from(receive, &request,
        sizeof(request), handles, 3u, &size, &count, &owner);
    if (status != ASTRA_SYSCALL_OK) return;
    astra_message_header_set(&reply.header, sizeof(reply),
        ASTRA_MESSAGING_PROTOCOL, ASTRA_MESSAGING_VERSION,
        ASTRA_MESSAGING_REPLY, request.header.transaction_id);
    const SupervisorProcessRecord *self = process_record(processes, owner);
    char own_name[ASTRA_MESSAGING_NAME_MAX] = {0};
    uint32_t own_kind = ASTRA_MESSAGING_COMMAND;
    reply.status = self != NULL ? identity(self, &own_kind, own_name) :
        live_process(owner, own_name);
    if (size != sizeof(request) || count > 3u ||
        (count != 0u && handles[0] == 0u) ||
        request.header.total_size != sizeof(request) ||
        request.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request.header.protocol != ASTRA_MESSAGING_PROTOCOL ||
        request.header.protocol_version != ASTRA_MESSAGING_VERSION ||
        request.header.flags != 0u || request.header.reserved != 0u ||
        request.header.transaction_id == 0u || request.reserved != 0u ||
        request.header.operation < ASTRA_MESSAGING_IDENTIFY ||
        request.header.operation > ASTRA_MESSAGING_PUBLISH ||
        request.payload_size > ASTRA_MESSAGING_PAYLOAD_MAX ||
        !terminated(request.name, sizeof(request.name)) ||
        !terminated(request.topic, sizeof(request.topic)) ||
        (request.header.operation != ASTRA_MESSAGING_PUBLISH &&
         request.payload_size != 0u) ||
        ((request.header.operation == ASTRA_MESSAGING_REGISTER ||
          request.header.operation == ASTRA_MESSAGING_SUBSCRIBE) ?
            count != 2u :
          request.header.operation == ASTRA_MESSAGING_PUBLISH ?
            count > 2u : count != 1u))
        reply.status = ASTRA_STATUS_PROTOCOL;
    if (reply.status == ASTRA_STATUS_OK) {
        if (request.header.operation == ASTRA_MESSAGING_LIST ||
            request.header.operation == ASTRA_MESSAGING_LOOKUP ||
            request.header.operation == ASTRA_MESSAGING_REGISTER ||
            request.header.operation == ASTRA_MESSAGING_SUBSCRIBE)
            prune_commands(processes);
        switch (request.header.operation) {
        case ASTRA_MESSAGING_IDENTIFY:
            reply.kind = own_kind;
            reply.process_id = owner;
            memcpy(reply.name, own_name, sizeof(reply.name));
            break;
        case ASTRA_MESSAGING_LIST: {
            const SupervisorProcessRecord *found = NULL;
            uint32_t command = 0u;
            for (uint32_t at = 0u; at < processes->count; ++at) {
                const SupervisorProcessRecord *candidate = &processes->records[at];
                if (candidate->id > request.cursor &&
                    (found == NULL || candidate->id < found->id))
                    found = candidate;
            }
            for (MessagingEndpoint *at = endpoints; at != NULL; at = at->next)
                if (process_record(processes, at->process_id) == NULL &&
                    at->process_id > request.cursor &&
                    (found == NULL || at->process_id < found->id) &&
                    (command == 0u || at->process_id < command) &&
                    live_process(at->process_id, NULL) == ASTRA_STATUS_OK)
                    command = at->process_id;
            if (command != 0u) {
                reply.kind = ASTRA_MESSAGING_COMMAND;
                reply.process_id = command;
                reply.cursor = command;
                reply.has_endpoint = 1u;
                (void)live_process(command, reply.name);
            } else if (found == NULL) reply.status = ASTRA_STATUS_NOT_FOUND;
            else {
                reply.status = identity(found, &reply.kind, reply.name);
                reply.process_id = found->id;
                reply.cursor = found->id;
                reply.has_endpoint = endpoint(found->id) != NULL;
            }
            break;
        }
        case ASTRA_MESSAGING_LOOKUP:
            for (uint32_t at = 0u; at < processes->count; ++at) {
                const SupervisorProcessRecord *candidate = &processes->records[at];
                uint32_t kind = 0u;
                char name[ASTRA_MESSAGING_NAME_MAX] = {0};
                if (identity(candidate, &kind, name) != ASTRA_STATUS_OK ||
                    kind != request.kind || strcmp(name, request.name) != 0 ||
                    (request.process_id != 0u && candidate->id != request.process_id) ||
                    endpoint(candidate->id) == NULL)
                    continue;
                if (target == 0u || candidate->id < target) target = candidate->id;
            }
            if (request.kind == ASTRA_MESSAGING_COMMAND)
                for (MessagingEndpoint *at = endpoints; at != NULL; at = at->next)
                    if (process_record(processes, at->process_id) == NULL &&
                        (request.process_id == 0u ||
                         request.process_id == at->process_id) &&
                        (target == 0u || at->process_id < target) &&
                        live_process(at->process_id, own_name) == ASTRA_STATUS_OK &&
                        (request.name[0] == '\0' ||
                         strcmp(request.name, own_name) == 0))
                        target = at->process_id;
            if (target == 0u) reply.status = ASTRA_STATUS_NOT_FOUND;
            else {
                reply.kind = request.kind;
                reply.process_id = target;
                reply.has_endpoint = 1u;
                if (request.kind == ASTRA_MESSAGING_COMMAND)
                    (void)live_process(target, reply.name);
                else memcpy(reply.name, request.name, sizeof(reply.name));
            }
            break;
        case ASTRA_MESSAGING_REGISTER:
            reply.status = register_endpoint(owner, &handles[1]);
            break;
        case ASTRA_MESSAGING_UNREGISTER:
            reply.status = unregister_endpoint(owner);
            break;
        case ASTRA_MESSAGING_SUBSCRIBE:
            reply.status = subscribe(owner, &request, &handles[1],
                &reply.subscription);
            break;
        case ASTRA_MESSAGING_UNSUBSCRIBE:
            reply.status = unsubscribe(owner, request.subscription);
            break;
        case ASTRA_MESSAGING_PUBLISH:
            if (request.topic[0] == '\0') reply.status = ASTRA_STATUS_INVALID;
            else reply.status = publish(owner, own_kind, own_name,
                &request, count == 2u ? handles[1] : 0u, &reply);
            break;
        default:
            reply.status = ASTRA_STATUS_PROTOCOL;
        }
    }
    if (count > 0u && handles[0] != 0u) {
        uint32_t outgoing = 0u;
        if (reply.status == ASTRA_STATUS_OK && target != 0u) {
            MessagingEndpoint *entry = endpoint(target);
            if (entry == NULL || astra_rt_handle_duplicate(entry->send,
                ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                    ASTRA_RIGHT_TRANSFER, &outgoing) !=
                    ASTRA_SYSCALL_OK)
                reply.status = ASTRA_STATUS_PEER_DEAD;
        }
        if (outgoing != 0u) {
            const AstraHandleDisposition item = {
                outgoing, ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT};
            (void)astra_port_send_rights(handles[0], &reply, sizeof(reply),
                &item, 1u);
            (void)astra_close(outgoing);
        } else (void)astra_port_send(handles[0], &reply, sizeof(reply), NULL, 0u);
    }
    for (uint32_t at = 0u; at < count; ++at)
        if (handles[at] != 0u) (void)astra_close(handles[at]);
}
