#include "port.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "object_cache.h"

#include <stddef.h>
#include <stdint.h>

#define KERNEL_PORT_SLOT_NONE UINT16_MAX

typedef enum KernelPortMessageState {
    KERNEL_PORT_MESSAGE_FREE = 0,
    KERNEL_PORT_MESSAGE_RESERVED,
    KERNEL_PORT_MESSAGE_QUEUED,
    KERNEL_PORT_MESSAGE_RECEIVING
} KernelPortMessageState;

typedef struct KernelPortMessage {
    uint8_t data[KERNEL_PORT_MESSAGE_SIZE_MAX];
    KernelDetachedHandle detached[KERNEL_PORT_MESSAGE_HANDLE_MAX];
    uint32_t generation;
    uint16_t next;
    uint16_t size;
    uint16_t port_slot;
    uint8_t handle_count;
    uint8_t state;
} KernelPortMessage;

struct KernelPort {
    KernelThreadWaitQueue readable;
    KernelThreadWaitQueue writable;
    uint32_t owner;
    uint32_t generation;
    uint32_t queued_bytes;
    uint32_t receive_terminal;
    uint32_t send_terminal;
    uint16_t references;
    uint16_t send_references;
    uint16_t receive_references;
    uint16_t head;
    uint16_t tail;
    uint16_t queued_messages;
    uint16_t maximum_messages;
    uint16_t maximum_bytes;
    uint8_t state;
    uint8_t capacity_reserved;
    uint8_t releasing_messages;
    uint8_t reserved;
};

static KernelPort ports[KERNEL_PORT_MAX];
static KernelPortMessage messages[KERNEL_PORT_MESSAGE_MAX];
static KernelObjectCache port_cache;
static KernelObjectCache message_cache;
static uint32_t port_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PORT_MAX)];
static uint32_t message_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PORT_MESSAGE_MAX)];
static KernelPortPoolStats pool_stats;
static uint8_t pool_corrupt;

_Static_assert(sizeof(KernelPort) == 64u,
               "message-port object memory budget changed");
_Static_assert(sizeof(KernelPortMessage) == 324u,
               "message record memory budget changed");
_Static_assert(sizeof(ports) + sizeof(messages) == 11392u,
               "message-port fixed pool memory budget changed");

static uint16_t port_slot(const KernelPort *port)
{
    return (uint16_t)(port - &ports[0]);
}

static bool valid_port_pointer(const KernelPort *port)
{
    uintptr_t address = (uintptr_t)port;
    uintptr_t first = (uintptr_t)&ports[0];
    uintptr_t limit = (uintptr_t)&ports[KERNEL_PORT_MAX];

    return port != NULL && address >= first && address < limit &&
           (address - first) % sizeof(ports[0]) == 0u;
}

static bool valid_endpoint(KernelPortEndpoint endpoint)
{
    return endpoint == KERNEL_PORT_ENDPOINT_SEND ||
           endpoint == KERNEL_PORT_ENDPOINT_RECEIVE;
}

static bool active_state(uint8_t state)
{
    return state == KERNEL_PORT_OPEN || state == KERNEL_PORT_PEER_CLOSED;
}

static uint32_t active_port_count(void)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
        if (active_state(ports[slot].state))
            ++count;
    }
    return count;
}

static uint32_t closing_port_count(void)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
        if (ports[slot].state == KERNEL_PORT_CLOSING)
            ++count;
    }
    return count;
}

static void owner_reservations(uint32_t owner, uint32_t *port_count,
                               uint32_t *message_capacity,
                               uint32_t *byte_capacity)
{
    uint32_t owner_ports = 0u;
    uint32_t owner_messages = 0u;
    uint32_t owner_bytes = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
        const KernelPort *port = &ports[slot];

        if (port->capacity_reserved == 0u || port->owner != owner)
            continue;
        ++owner_ports;
        owner_messages += port->maximum_messages;
        owner_bytes += port->maximum_bytes;
    }
    if (port_count != NULL)
        *port_count = owner_ports;
    if (message_capacity != NULL)
        *message_capacity = owner_messages;
    if (byte_capacity != NULL)
        *byte_capacity = owner_bytes;
}

static KernelPortStatus allocate_message(uint32_t owner,
                                         KernelPortMessage **result)
{
    void *raw_message;
    uint16_t slot;
    KernelObjectCacheStatus status = kernel_object_cache_claim(
        &message_cache, owner, &raw_message, &slot);

    if (result == NULL)
        return KERNEL_PORT_INVALID_ARGUMENT;
    *result = NULL;
    if (status == KERNEL_OBJECT_CACHE_UNAVAILABLE) {
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    if (status != KERNEL_OBJECT_CACHE_OK ||
        slot >= KERNEL_PORT_MESSAGE_MAX) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    KernelPortMessage *message = raw_message;
    uint32_t generation;

    if (message->state != KERNEL_PORT_MESSAGE_FREE) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    generation = kernel_generation_next(message->generation);
    kernel_bytes_clear(message, sizeof(*message));
    message->generation = generation;
    message->next = KERNEL_PORT_SLOT_NONE;
    message->port_slot = KERNEL_PORT_SLOT_NONE;
    message->state = KERNEL_PORT_MESSAGE_RESERVED;
    *result = message;
    return KERNEL_PORT_OK;
}

static void free_message(KernelPortMessage *message)
{
    uint32_t generation;

    if (message == NULL || message < &messages[0] ||
        message >= &messages[KERNEL_PORT_MESSAGE_MAX] ||
        message->state == KERNEL_PORT_MESSAGE_FREE) {
        pool_corrupt = 1u;
        return;
    }
    generation = message->generation;
    kernel_bytes_clear(message, sizeof(*message));
    message->generation = generation;
    message->next = KERNEL_PORT_SLOT_NONE;
    message->port_slot = KERNEL_PORT_SLOT_NONE;
    message->state = KERNEL_PORT_MESSAGE_FREE;
    if (kernel_object_cache_release(&message_cache, message) !=
        KERNEL_OBJECT_CACHE_OK)
        pool_corrupt = 1u;
}

static bool wake_one(KernelThreadWaitQueue *queue, uint32_t result,
                     uint32_t *woken_threads)
{
    KernelThread *thread = NULL;
    KernelThreadStatus status = kernel_thread_wake_one(
        queue, result, &thread);

    if (status == KERNEL_THREAD_NO_RUNNABLE) {
        if (woken_threads != NULL)
            *woken_threads = 0u;
        return true;
    }
    if (status != KERNEL_THREAD_OK || thread == NULL)
        return false;
    if (woken_threads != NULL)
        *woken_threads = 1u;
    ++pool_stats.wait_wakeups;
    return true;
}

static bool wake_all(KernelThreadWaitQueue *queue, uint32_t result,
                     uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (kernel_thread_wake_all(queue, result, &woken) != KERNEL_THREAD_OK)
        return false;
    pool_stats.wait_wakeups += woken;
    if (woken_threads != NULL)
        *woken_threads = woken;
    return true;
}

static void update_queue_maximum(void)
{
    if (pool_stats.queued_messages > pool_stats.max_queued_messages)
        pool_stats.max_queued_messages = pool_stats.queued_messages;
    if (pool_stats.queued_bytes > pool_stats.max_queued_bytes)
        pool_stats.max_queued_bytes = pool_stats.queued_bytes;
    if (pool_stats.queued_handles > pool_stats.max_queued_handles)
        pool_stats.max_queued_handles = pool_stats.queued_handles;
}

static bool send_ready(const KernelPort *port)
{
    return port->state == KERNEL_PORT_OPEN &&
           port->queued_messages < port->maximum_messages &&
           port->queued_bytes <=
               (uint32_t)port->maximum_bytes -
                   KERNEL_PORT_MESSAGE_SIZE_MIN;
}

static void release_capacity(KernelPort *port)
{
    if (port->capacity_reserved == 0u)
        return;
    if (pool_stats.reserved_message_capacity < port->maximum_messages ||
        pool_stats.reserved_byte_capacity < port->maximum_bytes) {
        pool_corrupt = 1u;
        return;
    }
    pool_stats.reserved_message_capacity -= port->maximum_messages;
    pool_stats.reserved_byte_capacity -= port->maximum_bytes;
    port->capacity_reserved = 0u;
}

static void maybe_free_port(KernelPort *port)
{
    uint32_t generation;

    if (!valid_port_pointer(port) || port->state != KERNEL_PORT_CLOSING ||
        port->references != 0u || port->queued_messages != 0u ||
        port->queued_bytes != 0u || port->head != KERNEL_PORT_SLOT_NONE ||
        port->tail != KERNEL_PORT_SLOT_NONE ||
        port->releasing_messages != 0u)
        return;
    if (port->capacity_reserved != 0u ||
        kernel_thread_wait_queue_count(&port->readable) != 0u ||
        kernel_thread_wait_queue_count(&port->writable) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    generation = port->generation;
    kernel_bytes_clear(port, sizeof(*port));
    kernel_thread_wait_queue_init(&port->readable);
    kernel_thread_wait_queue_init(&port->writable);
    port->generation = generation;
    port->head = KERNEL_PORT_SLOT_NONE;
    port->tail = KERNEL_PORT_SLOT_NONE;
    port->state = KERNEL_PORT_FREE;
    if (kernel_object_cache_release(&port_cache, port) !=
        KERNEL_OBJECT_CACHE_OK)
        pool_corrupt = 1u;
}

static bool discard_messages(KernelPort *port)
{
    if (!valid_port_pointer(port) || port->releasing_messages != 0u)
        return false;
    port->releasing_messages = 1u;
    while (port->head != KERNEL_PORT_SLOT_NONE) {
        KernelDetachedHandle detached[KERNEL_PORT_MESSAGE_HANDLE_MAX];
        uint16_t slot = port->head;
        KernelPortMessage *message;
        uint32_t size;
        uint32_t handle_count;

        if (slot >= KERNEL_PORT_MESSAGE_MAX) {
            pool_corrupt = 1u;
            break;
        }
        message = &messages[slot];
        if (message->state != KERNEL_PORT_MESSAGE_QUEUED ||
            message->port_slot != port_slot(port) ||
            port->queued_messages == 0u ||
            port->queued_bytes < message->size ||
            pool_stats.queued_messages == 0u ||
            pool_stats.queued_bytes < message->size ||
            pool_stats.queued_handles < message->handle_count) {
            pool_corrupt = 1u;
            break;
        }
        size = message->size;
        handle_count = message->handle_count;
        for (uint32_t index = 0u; index < handle_count; ++index)
            detached[index] = message->detached[index];
        port->head = message->next;
        --port->queued_messages;
        port->queued_bytes -= size;
        --pool_stats.queued_messages;
        pool_stats.queued_bytes -= size;
        pool_stats.queued_handles -= handle_count;
        ++pool_stats.discarded_messages;
        pool_stats.discarded_handles += handle_count;
        free_message(message);
        if (handle_count != 0u &&
            kernel_handle_detached_release(detached, handle_count) !=
                KERNEL_HANDLE_OK) {
            pool_corrupt = 1u;
            break;
        }
    }
    if (port->head == KERNEL_PORT_SLOT_NONE)
        port->tail = KERNEL_PORT_SLOT_NONE;
    port->releasing_messages = 0u;
    return pool_corrupt == 0u;
}

static KernelPortStatus close_port(KernelPort *port,
                                   uint32_t receive_result,
                                   uint32_t send_result,
                                   uint32_t *woken_threads)
{
    uint32_t receive_woken = 0u;
    uint32_t send_woken = 0u;

    if (!valid_port_pointer(port) || receive_result == ASTRA_SYSCALL_OK ||
        send_result == ASTRA_SYSCALL_OK)
        return KERNEL_PORT_INVALID_ARGUMENT;
    if (port->state == KERNEL_PORT_CLOSING) {
        if (woken_threads != NULL)
            *woken_threads = 0u;
        return KERNEL_PORT_CLOSED;
    }
    if (!active_state(port->state) || port->owner == 0u ||
        port->generation == 0u)
        return KERNEL_PORT_CORRUPT;

    port->state = KERNEL_PORT_CLOSING;
    port->receive_terminal = receive_result;
    port->send_terminal = send_result;
    release_capacity(port);
    if (!wake_all(&port->readable, receive_result, &receive_woken) ||
        !wake_all(&port->writable, send_result, &send_woken) ||
        !discard_messages(port)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    ++pool_stats.close_operations;
    if (woken_threads != NULL)
        *woken_threads = receive_woken + send_woken;
    maybe_free_port(port);
    return pool_corrupt == 0u ? KERNEL_PORT_OK : KERNEL_PORT_CORRUPT;
}

static KernelPortStatus mark_sender_closed(KernelPort *port,
                                           uint32_t *woken_threads)
{
    uint32_t receive_woken = 0u;
    uint32_t send_woken = 0u;
    uint32_t receive_result;

    if (!valid_port_pointer(port) || port->state != KERNEL_PORT_OPEN ||
        port->send_references != 0u || port->receive_references == 0u)
        return KERNEL_PORT_CORRUPT;
    port->state = KERNEL_PORT_PEER_CLOSED;
    port->receive_terminal = ASTRA_SYSCALL_PEER_DEAD;
    port->send_terminal = ASTRA_SYSCALL_PEER_DEAD;
    receive_result = port->queued_messages == 0u ?
        ASTRA_SYSCALL_PEER_DEAD : ASTRA_SYSCALL_OK;
    if (!wake_all(&port->readable, receive_result, &receive_woken) ||
        !wake_all(&port->writable, ASTRA_SYSCALL_PEER_DEAD,
                  &send_woken)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    ++pool_stats.peer_closures;
    if (woken_threads != NULL)
        *woken_threads = receive_woken + send_woken;
    return KERNEL_PORT_OK;
}

static KernelPortStatus map_handle_status(KernelHandleStatus status)
{
    switch (status) {
    case KERNEL_HANDLE_INVALID_HANDLE:
    case KERNEL_HANDLE_TYPE_MISMATCH:
        return KERNEL_PORT_INVALID_HANDLE;
    case KERNEL_HANDLE_ACCESS_DENIED:
        return KERNEL_PORT_ACCESS_DENIED;
    case KERNEL_HANDLE_DUPLICATE:
        return KERNEL_PORT_DUPLICATE_HANDLE;
    case KERNEL_HANDLE_TRANSFER_POOL_FULL:
        return KERNEL_PORT_TRANSFER_POOL_FULL;
    case KERNEL_HANDLE_TABLE_FULL:
        return KERNEL_PORT_HANDLE_TABLE_FULL;
    case KERNEL_HANDLE_INVALID_ARGUMENT:
        return KERNEL_PORT_INVALID_ARGUMENT;
    default:
        return KERNEL_PORT_CORRUPT;
    }
}

void kernel_port_pool_init(void)
{
    if (!kernel_object_cache_init(
            &port_cache, ports, sizeof(ports[0]), KERNEL_PORT_MAX,
            port_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PORT_MAX),
            KERNEL_ALLOCATION_SITE_PORT_OBJECT) ||
        !kernel_object_cache_init(
            &message_cache, messages, sizeof(messages[0]),
            KERNEL_PORT_MESSAGE_MAX, message_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PORT_MESSAGE_MAX),
            KERNEL_ALLOCATION_SITE_PORT_MESSAGE)) {
        pool_corrupt = 1u;
        return;
    }
    for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
        uint32_t generation = ports[slot].generation;

        kernel_bytes_clear(&ports[slot], sizeof(ports[slot]));
        kernel_thread_wait_queue_init(&ports[slot].readable);
        kernel_thread_wait_queue_init(&ports[slot].writable);
        ports[slot].generation = generation;
        ports[slot].head = KERNEL_PORT_SLOT_NONE;
        ports[slot].tail = KERNEL_PORT_SLOT_NONE;
        ports[slot].state = KERNEL_PORT_FREE;
    }
    for (uint32_t slot = 0u; slot < KERNEL_PORT_MESSAGE_MAX; ++slot) {
        uint32_t generation = messages[slot].generation;

        kernel_bytes_clear(&messages[slot], sizeof(messages[slot]));
        messages[slot].generation = generation;
        messages[slot].next = KERNEL_PORT_SLOT_NONE;
        messages[slot].port_slot = KERNEL_PORT_SLOT_NONE;
        messages[slot].state = KERNEL_PORT_MESSAGE_FREE;
    }
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
}

KernelPortStatus kernel_port_create(uint32_t owner,
                                    uint32_t maximum_messages,
                                    uint32_t maximum_bytes,
                                    KernelPort **port)
{
    uint32_t owner_ports;
    uint32_t owner_messages;
    uint32_t owner_bytes;
    void *raw_port;
    uint16_t slot;
    KernelObjectCacheStatus cache_status;

    if (port == NULL || owner == 0u || maximum_messages == 0u ||
        maximum_messages > KERNEL_PORT_QUEUE_MESSAGES_MAX ||
        maximum_bytes < KERNEL_PORT_MESSAGE_SIZE_MIN ||
        maximum_bytes > KERNEL_PORT_QUEUE_BYTES_MAX ||
        maximum_messages > UINT16_MAX || maximum_bytes > UINT16_MAX)
        return KERNEL_PORT_INVALID_ARGUMENT;
    *port = NULL;
    owner_reservations(owner, &owner_ports, &owner_messages, &owner_bytes);
    if (owner_ports >= KERNEL_PORT_OWNER_MAX ||
        maximum_messages >
            KERNEL_PORT_OWNER_MESSAGE_MAX - owner_messages ||
        maximum_bytes > KERNEL_PORT_OWNER_BYTES_MAX - owner_bytes ||
        maximum_messages > KERNEL_PORT_MESSAGE_MAX -
                               pool_stats.reserved_message_capacity ||
        maximum_bytes > KERNEL_PORT_MESSAGE_BYTES_MAX -
                            pool_stats.reserved_byte_capacity) {
        ++pool_stats.quota_failures;
        return KERNEL_PORT_QUOTA_EXCEEDED;
    }
    cache_status = kernel_object_cache_claim(
        &port_cache, owner, &raw_port, &slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE) {
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    if (cache_status != KERNEL_OBJECT_CACHE_OK || slot >= KERNEL_PORT_MAX) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    KernelPort *candidate = raw_port;
    uint32_t generation;
    uint32_t active;

    if (candidate->state != KERNEL_PORT_FREE) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    generation = kernel_generation_next(candidate->generation);
    kernel_bytes_clear(candidate, sizeof(*candidate));
    kernel_thread_wait_queue_init(&candidate->readable);
    kernel_thread_wait_queue_init(&candidate->writable);
    candidate->owner = owner;
    candidate->generation = generation;
    candidate->references = 2u;
    candidate->send_references = 1u;
    candidate->receive_references = 1u;
    candidate->head = KERNEL_PORT_SLOT_NONE;
    candidate->tail = KERNEL_PORT_SLOT_NONE;
    candidate->maximum_messages = (uint16_t)maximum_messages;
    candidate->maximum_bytes = (uint16_t)maximum_bytes;
    candidate->state = KERNEL_PORT_OPEN;
    candidate->capacity_reserved = 1u;
    pool_stats.reserved_message_capacity += maximum_messages;
    pool_stats.reserved_byte_capacity += maximum_bytes;
    ++pool_stats.created_ports;
    active = active_port_count();
    if (active > pool_stats.max_active_ports)
        pool_stats.max_active_ports = active;
    *port = candidate;
    return KERNEL_PORT_OK;
}

void kernel_port_abandon_unpublished(KernelPort *port)
{
    if (!valid_port_pointer(port) || port->state != KERNEL_PORT_OPEN ||
        port->references != 2u || port->send_references != 1u ||
        port->receive_references != 1u || port->queued_messages != 0u) {
        pool_corrupt = 1u;
        return;
    }
    ++pool_stats.publication_rollbacks;
    port->references = 0u;
    port->send_references = 0u;
    port->receive_references = 0u;
    if (close_port(port, ASTRA_SYSCALL_CLOSED,
                   ASTRA_SYSCALL_PEER_DEAD, NULL) != KERNEL_PORT_OK)
        pool_corrupt = 1u;
}

/*
 * A second handle to the same endpoint.
 *
 * Ports had no retain at all until a launch needed one: they moved through the
 * transfer machinery, which hands an endpoint over rather than sharing it, and
 * that is still how a reply channel travels. What a launch needs is different
 * -- the launcher keeps its stream sink and the child gets one too -- and a
 * copy needs a reference the release will match.
 *
 * **Only the send endpoint may be copied.** A second receive handle is a second
 * service on one port, with messages going to whichever end asked first; that
 * is a worker pool, it is a real thing to want, and it is not something a
 * launch should be able to create by accident. Refusing it here means a grant
 * of a receive endpoint fails loudly instead of quietly splitting a service.
 */
bool kernel_port_handle_retain(void *object, void *context)
{
    KernelPort *port = object;
    KernelPortEndpoint endpoint =
        (KernelPortEndpoint)(uintptr_t)context;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        !active_state(port->state) || port->references == 0u)
        return false;
    if (endpoint != KERNEL_PORT_ENDPOINT_SEND)
        return false;
    /*
     * A sender count of zero means every sender has gone and the port has
     * already been told so. Reviving one from a handle nobody holds would
     * reopen a channel the receiver was told was finished with.
     */
    if (port->send_references == 0u || port->send_references == UINT16_MAX ||
        port->references == UINT16_MAX)
        return false;
    ++port->send_references;
    ++port->references;
    return true;
}

void kernel_port_handle_release(void *object, void *context)
{
    KernelPort *port = object;
    KernelPortEndpoint endpoint =
        (KernelPortEndpoint)(uintptr_t)context;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        port->state == KERNEL_PORT_FREE || port->references == 0u) {
        pool_corrupt = 1u;
        return;
    }
    if (endpoint == KERNEL_PORT_ENDPOINT_SEND) {
        if (port->send_references == 0u) {
            pool_corrupt = 1u;
            return;
        }
        --port->send_references;
    } else {
        if (port->receive_references == 0u) {
            pool_corrupt = 1u;
            return;
        }
        --port->receive_references;
    }
    --port->references;

    if (endpoint == KERNEL_PORT_ENDPOINT_RECEIVE &&
        port->receive_references == 0u &&
        active_state(port->state)) {
        if (close_port(port, ASTRA_SYSCALL_CLOSED,
                       ASTRA_SYSCALL_PEER_DEAD, NULL) != KERNEL_PORT_OK)
            pool_corrupt = 1u;
        return;
    }
    if (endpoint == KERNEL_PORT_ENDPOINT_SEND &&
        port->send_references == 0u && port->state == KERNEL_PORT_OPEN) {
        if (mark_sender_closed(port, NULL) != KERNEL_PORT_OK)
            pool_corrupt = 1u;
        return;
    }
    if (port->state == KERNEL_PORT_CLOSING)
        maybe_free_port(port);
}

KernelPortStatus kernel_port_send(
    KernelPort *port, KernelHandleTable *source_table,
    const void *raw_message, uint32_t message_size,
    const KernelHandle *handles, uint32_t handle_count,
    uint32_t *woken_threads)
{
    KernelPortMessage *message;
    KernelHandleTransferBatch transfer;
    KernelHandleStatus handle_status;
    KernelPortStatus message_status;
    uint16_t message_slot;

    if (woken_threads != NULL)
        *woken_threads = 0u;
    if (!valid_port_pointer(port) || source_table == NULL ||
        raw_message == NULL ||
        message_size < KERNEL_PORT_MESSAGE_SIZE_MIN ||
        message_size > KERNEL_PORT_MESSAGE_SIZE_MAX ||
        handle_count > KERNEL_PORT_MESSAGE_HANDLE_MAX ||
        (handle_count != 0u && handles == NULL))
        return KERNEL_PORT_INVALID_ARGUMENT;
    if (port->state == KERNEL_PORT_CLOSING ||
        port->state == KERNEL_PORT_PEER_CLOSED)
        return KERNEL_PORT_PEER_DEAD;
    if (port->state != KERNEL_PORT_OPEN || port->send_references == 0u ||
        port->receive_references == 0u)
        return KERNEL_PORT_CORRUPT;
    if (port->queued_messages >= port->maximum_messages ||
        message_size > (uint32_t)port->maximum_bytes - port->queued_bytes) {
        ++pool_stats.send_would_block;
        return KERNEL_PORT_WOULD_BLOCK;
    }
    if ((port->queued_messages == 0u &&
         (port->head != KERNEL_PORT_SLOT_NONE ||
          port->tail != KERNEL_PORT_SLOT_NONE)) ||
        (port->queued_messages != 0u &&
         (port->head >= KERNEL_PORT_MESSAGE_MAX ||
          port->tail >= KERNEL_PORT_MESSAGE_MAX ||
          messages[port->tail].state != KERNEL_PORT_MESSAGE_QUEUED ||
          messages[port->tail].next != KERNEL_PORT_SLOT_NONE)))
        return KERNEL_PORT_CORRUPT;

    message_status = allocate_message(port->owner, &message);
    if (message_status != KERNEL_PORT_OK)
        return message_status;
    kernel_bytes_copy(message->data, raw_message, message_size);
    message->size = (uint16_t)message_size;
    message->port_slot = port_slot(port);
    message->handle_count = (uint8_t)handle_count;

    if (handle_count != 0u) {
        handle_status = kernel_handle_transfer_prepare(
            source_table, handles, handle_count, ASTRA_RIGHT_TRANSFER,
            &transfer);
        if (handle_status != KERNEL_HANDLE_OK) {
            free_message(message);
            return map_handle_status(handle_status);
        }
        handle_status = kernel_handle_transfer_commit_export(
            source_table, &transfer);
        if (handle_status != KERNEL_HANDLE_OK) {
            if (kernel_handle_transfer_rollback(&transfer) !=
                KERNEL_HANDLE_OK)
                pool_corrupt = 1u;
            free_message(message);
            return KERNEL_PORT_CORRUPT;
        }
        for (uint32_t index = 0u; index < handle_count; ++index)
            message->detached[index] = transfer.detached[index];
    }

    message_slot = (uint16_t)(message - &messages[0]);
    message->state = KERNEL_PORT_MESSAGE_QUEUED;
    if (port->tail == KERNEL_PORT_SLOT_NONE) {
        port->head = message_slot;
        port->tail = message_slot;
    } else {
        messages[port->tail].next = message_slot;
        port->tail = message_slot;
    }
    ++port->queued_messages;
    port->queued_bytes += message_size;
    ++pool_stats.queued_messages;
    pool_stats.queued_bytes += message_size;
    pool_stats.queued_handles += handle_count;
    ++pool_stats.sends;
    update_queue_maximum();
    if (!wake_one(&port->readable, ASTRA_SYSCALL_OK, woken_threads)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    return KERNEL_PORT_OK;
}

KernelPortStatus kernel_port_receive_prepare(
    KernelPort *port, KernelHandleTable *destination_table,
    uint32_t message_capacity, uint32_t handle_capacity,
    KernelPortReceipt *receipt, uint32_t *required_message_size,
    uint32_t *required_handle_count)
{
    KernelPortMessage *message;
    KernelHandleStatus handle_status;
    uint16_t slot;

    if (!valid_port_pointer(port) || destination_table == NULL ||
        receipt == NULL || required_message_size == NULL ||
        required_handle_count == NULL ||
        handle_capacity > KERNEL_PORT_MESSAGE_HANDLE_MAX)
        return KERNEL_PORT_INVALID_ARGUMENT;
    kernel_bytes_clear(receipt, sizeof(*receipt));
    receipt->message_slot = KERNEL_PORT_SLOT_NONE;
    *required_message_size = 0u;
    *required_handle_count = 0u;
    if (port->state == KERNEL_PORT_CLOSING)
        return port->receive_terminal == ASTRA_SYSCALL_PEER_DEAD ?
            KERNEL_PORT_PEER_DEAD : KERNEL_PORT_CLOSED;
    if (!active_state(port->state) || port->receive_references == 0u)
        return KERNEL_PORT_CORRUPT;
    if (port->queued_messages == 0u) {
        if (port->head != KERNEL_PORT_SLOT_NONE ||
            port->tail != KERNEL_PORT_SLOT_NONE)
            return KERNEL_PORT_CORRUPT;
        if (port->state == KERNEL_PORT_PEER_CLOSED)
            return KERNEL_PORT_PEER_DEAD;
        ++pool_stats.receive_would_block;
        return KERNEL_PORT_WOULD_BLOCK;
    }
    slot = port->head;
    if (slot >= KERNEL_PORT_MESSAGE_MAX)
        return KERNEL_PORT_CORRUPT;
    message = &messages[slot];
    if (message->state == KERNEL_PORT_MESSAGE_RECEIVING) {
        ++pool_stats.receive_would_block;
        return KERNEL_PORT_WOULD_BLOCK;
    }
    if (message->state != KERNEL_PORT_MESSAGE_QUEUED ||
        message->port_slot != port_slot(port))
        return KERNEL_PORT_CORRUPT;
    *required_message_size = message->size;
    *required_handle_count = message->handle_count;
    if (message_capacity < message->size ||
        handle_capacity < message->handle_count) {
        ++pool_stats.receive_buffer_too_small;
        return KERNEL_PORT_BUFFER_TOO_SMALL;
    }
    handle_status = kernel_handle_import_reserve(
        destination_table, message->detached, message->handle_count,
        &receipt->import);
    if (handle_status != KERNEL_HANDLE_OK)
        return map_handle_status(handle_status);
    message->state = KERNEL_PORT_MESSAGE_RECEIVING;
    receipt->port = port;
    receipt->destination_table = destination_table;
    receipt->message = message->data;
    receipt->message_size = message->size;
    receipt->message_generation = message->generation;
    receipt->message_slot = slot;
    receipt->handle_count = message->handle_count;
    receipt->active = 1u;
    return KERNEL_PORT_OK;
}

static bool valid_receipt(const KernelPortReceipt *receipt,
                          KernelPortMessage **message)
{
    if (receipt == NULL || message == NULL || receipt->active == 0u ||
        !valid_port_pointer(receipt->port) ||
        receipt->destination_table == NULL ||
        receipt->message_slot >= KERNEL_PORT_MESSAGE_MAX ||
        receipt->port->head != receipt->message_slot)
        return false;
    *message = &messages[receipt->message_slot];
    return (*message)->state == KERNEL_PORT_MESSAGE_RECEIVING &&
           (*message)->generation == receipt->message_generation &&
           (*message)->port_slot == port_slot(receipt->port) &&
           (*message)->size == receipt->message_size &&
           (*message)->handle_count == receipt->handle_count &&
           (*message)->data == receipt->message;
}

static void reset_receipt(KernelPortReceipt *receipt)
{
    kernel_bytes_clear(receipt, sizeof(*receipt));
    receipt->message_slot = KERNEL_PORT_SLOT_NONE;
}

KernelPortStatus kernel_port_receive_commit(KernelPortReceipt *receipt,
                                            uint32_t *woken_threads)
{
    KernelPortMessage *message;
    KernelPort *port;
    uint32_t writable_woken = 0u;
    uint32_t readable_woken = 0u;
    uint32_t size;
    uint32_t handle_count;

    if (woken_threads != NULL)
        *woken_threads = 0u;
    if (!valid_receipt(receipt, &message))
        return KERNEL_PORT_INVALID_STATE;
    port = receipt->port;
    size = message->size;
    handle_count = message->handle_count;
    if (port->state != KERNEL_PORT_OPEN &&
        port->state != KERNEL_PORT_PEER_CLOSED)
        return KERNEL_PORT_CORRUPT;
    if (port->queued_messages == 0u || port->queued_bytes < size ||
        pool_stats.queued_messages == 0u ||
        pool_stats.queued_bytes < size ||
        pool_stats.queued_handles < handle_count ||
        (message->next == KERNEL_PORT_SLOT_NONE &&
         port->tail != receipt->message_slot) ||
        (message->next != KERNEL_PORT_SLOT_NONE &&
         message->next >= KERNEL_PORT_MESSAGE_MAX))
        return KERNEL_PORT_CORRUPT;
    if (kernel_handle_import_commit(
            receipt->destination_table, &receipt->import,
            message->detached) != KERNEL_HANDLE_OK)
        return KERNEL_PORT_CORRUPT;
    port->head = message->next;
    --port->queued_messages;
    port->queued_bytes -= size;
    --pool_stats.queued_messages;
    pool_stats.queued_bytes -= size;
    pool_stats.queued_handles -= handle_count;
    if (port->head == KERNEL_PORT_SLOT_NONE)
        port->tail = KERNEL_PORT_SLOT_NONE;
    free_message(message);
    reset_receipt(receipt);
    ++pool_stats.receives;

    if (!wake_all(&port->writable, ASTRA_SYSCALL_OK,
                  &writable_woken)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    if (port->queued_messages != 0u) {
        if (!wake_one(&port->readable, ASTRA_SYSCALL_OK,
                      &readable_woken)) {
            pool_corrupt = 1u;
            return KERNEL_PORT_CORRUPT;
        }
    } else if (port->state == KERNEL_PORT_PEER_CLOSED) {
        if (!wake_all(&port->readable, ASTRA_SYSCALL_PEER_DEAD,
                      &readable_woken)) {
            pool_corrupt = 1u;
            return KERNEL_PORT_CORRUPT;
        }
    }
    if (woken_threads != NULL)
        *woken_threads = writable_woken + readable_woken;
    return KERNEL_PORT_OK;
}

KernelPortStatus kernel_port_receive_cancel(KernelPortReceipt *receipt,
                                            uint32_t *woken_threads)
{
    KernelPortMessage *message;
    KernelPort *port;

    if (woken_threads != NULL)
        *woken_threads = 0u;
    if (!valid_receipt(receipt, &message))
        return KERNEL_PORT_INVALID_STATE;
    port = receipt->port;
    if (kernel_handle_import_cancel(receipt->destination_table,
                                    &receipt->import) != KERNEL_HANDLE_OK)
        return KERNEL_PORT_CORRUPT;
    message->state = KERNEL_PORT_MESSAGE_QUEUED;
    reset_receipt(receipt);
    if (!wake_one(&port->readable, ASTRA_SYSCALL_OK, woken_threads)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    return KERNEL_PORT_OK;
}

KernelPortStatus kernel_port_prepare_wait(KernelPort *port,
                                          KernelPortEndpoint endpoint,
                                          KernelThreadWaitSpec *spec)
{
    KernelThreadWaitQueue *queue;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        spec == NULL)
        return KERNEL_PORT_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    if (port->state == KERNEL_PORT_CLOSING)
        return endpoint == KERNEL_PORT_ENDPOINT_SEND ?
            KERNEL_PORT_PEER_DEAD :
            (port->receive_terminal == ASTRA_SYSCALL_PEER_DEAD ?
                 KERNEL_PORT_PEER_DEAD : KERNEL_PORT_CLOSED);
    if (!active_state(port->state))
        return KERNEL_PORT_CORRUPT;
    if (endpoint == KERNEL_PORT_ENDPOINT_SEND) {
        if (send_ready(port))
            return KERNEL_PORT_OK;
        if (port->state == KERNEL_PORT_PEER_CLOSED)
            return KERNEL_PORT_PEER_DEAD;
        queue = &port->writable;
    } else {
        if (port->queued_messages != 0u &&
            port->head < KERNEL_PORT_MESSAGE_MAX &&
            messages[port->head].state == KERNEL_PORT_MESSAGE_QUEUED)
            return KERNEL_PORT_OK;
        if (port->state == KERNEL_PORT_PEER_CLOSED &&
            port->queued_messages == 0u)
            return KERNEL_PORT_PEER_DEAD;
        queue = &port->readable;
    }
    if (kernel_thread_wait_queue_count(queue) >= KERNEL_PORT_WAITER_MAX)
        return KERNEL_PORT_QUOTA_EXCEEDED;
    spec->queue = queue;
    spec->sequence = kernel_thread_wait_queue_sequence(queue);
    return spec->sequence == 0u ? KERNEL_PORT_CORRUPT :
                                 KERNEL_PORT_WOULD_BLOCK;
}

KernelPortStatus kernel_port_prepare_wait_after(
    KernelPort *port, KernelPortEndpoint endpoint,
    uint32_t expected_sequence, KernelThreadWaitSpec *spec)
{
    KernelThreadWaitQueue *queue;
    uint32_t sequence;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        expected_sequence == 0u || spec == NULL)
        return KERNEL_PORT_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    if (port->state == KERNEL_PORT_CLOSING)
        return endpoint == KERNEL_PORT_ENDPOINT_SEND ?
            KERNEL_PORT_PEER_DEAD :
            (port->receive_terminal == ASTRA_SYSCALL_PEER_DEAD ?
                 KERNEL_PORT_PEER_DEAD : KERNEL_PORT_CLOSED);
    if (!active_state(port->state))
        return KERNEL_PORT_CORRUPT;
    if (endpoint == KERNEL_PORT_ENDPOINT_SEND &&
        port->state == KERNEL_PORT_PEER_CLOSED)
        return KERNEL_PORT_PEER_DEAD;
    if (endpoint == KERNEL_PORT_ENDPOINT_RECEIVE &&
        port->state == KERNEL_PORT_PEER_CLOSED &&
        port->queued_messages == 0u)
        return KERNEL_PORT_PEER_DEAD;
    queue = endpoint == KERNEL_PORT_ENDPOINT_SEND ?
        &port->writable : &port->readable;
    sequence = kernel_thread_wait_queue_sequence(queue);
    if (sequence == 0u)
        return KERNEL_PORT_CORRUPT;
    if (sequence != expected_sequence)
        return KERNEL_PORT_OK;
    if (kernel_thread_wait_queue_count(queue) >= KERNEL_PORT_WAITER_MAX)
        return KERNEL_PORT_QUOTA_EXCEEDED;
    spec->queue = queue;
    spec->sequence = sequence;
    return KERNEL_PORT_WOULD_BLOCK;
}

KernelPortStatus kernel_port_wait_sequence(const KernelPort *port,
                                           KernelPortEndpoint endpoint,
                                           uint32_t *sequence)
{
    const KernelThreadWaitQueue *queue;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        sequence == NULL || !active_state(port->state))
        return KERNEL_PORT_INVALID_ARGUMENT;
    queue = endpoint == KERNEL_PORT_ENDPOINT_SEND ?
        &port->writable : &port->readable;
    *sequence = kernel_thread_wait_queue_sequence(queue);
    return *sequence == 0u ? KERNEL_PORT_CORRUPT : KERNEL_PORT_OK;
}

KernelPortStatus kernel_port_commit_wait(KernelPort *port,
                                         KernelPortEndpoint endpoint)
{
    KernelThreadWaitQueue *queue;
    uint32_t waiters;

    if (!valid_port_pointer(port) || !valid_endpoint(endpoint) ||
        port->state == KERNEL_PORT_FREE)
        return KERNEL_PORT_INVALID_ARGUMENT;
    queue = endpoint == KERNEL_PORT_ENDPOINT_SEND ?
        &port->writable : &port->readable;
    waiters = kernel_thread_wait_queue_count(queue);
    return waiters != 0u && waiters <= KERNEL_PORT_WAITER_MAX ?
        KERNEL_PORT_OK : KERNEL_PORT_INVALID_STATE;
}

KernelPortStatus kernel_port_owner_died(uint32_t owner,
                                        uint32_t *closed_ports,
                                        uint32_t *woken_threads)
{
    uint32_t closed = 0u;
    uint32_t woken = 0u;

    if (owner == 0u || closed_ports == NULL || woken_threads == NULL)
        return KERNEL_PORT_INVALID_ARGUMENT;
    *closed_ports = 0u;
    *woken_threads = 0u;
    for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
        KernelPort *port = &ports[slot];
        uint32_t port_woken = 0u;

        if (!active_state(port->state) || port->owner != owner)
            continue;
        if (close_port(port, ASTRA_SYSCALL_PEER_DEAD,
                       ASTRA_SYSCALL_PEER_DEAD,
                       &port_woken) != KERNEL_PORT_OK)
            return KERNEL_PORT_CORRUPT;
        ++closed;
        woken += port_woken;
    }
    if (closed != 0u)
        ++pool_stats.owner_deaths;
    *closed_ports = closed;
    *woken_threads = woken;
    return KERNEL_PORT_OK;
}

bool kernel_port_snapshot(uint32_t slot, KernelPortSnapshot *snapshot)
{
    const KernelPort *port;
    uint32_t readable;
    uint32_t writable;

    if (slot >= KERNEL_PORT_MAX || snapshot == NULL)
        return false;
    port = &ports[slot];
    readable = kernel_thread_wait_queue_count(&port->readable);
    writable = kernel_thread_wait_queue_count(&port->writable);
    if (readable == UINT32_MAX || writable == UINT32_MAX)
        return false;
    snapshot->owner = port->owner;
    snapshot->generation = port->generation;
    snapshot->queued_bytes = port->queued_bytes;
    snapshot->receive_terminal = port->receive_terminal;
    snapshot->send_terminal = port->send_terminal;
    snapshot->references = port->references;
    snapshot->send_references = port->send_references;
    snapshot->receive_references = port->receive_references;
    snapshot->queued_messages = port->queued_messages;
    snapshot->maximum_messages = port->maximum_messages;
    snapshot->maximum_bytes = port->maximum_bytes;
    snapshot->readable_waiters = (uint16_t)readable;
    snapshot->writable_waiters = (uint16_t)writable;
    snapshot->state = port->state;
    snapshot->capacity_reserved = port->capacity_reserved;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    return true;
}

bool kernel_port_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_port_pool_valid(void)
{
    uint8_t seen_messages[KERNEL_PORT_MESSAGE_MAX];
    uint8_t seen_detached[KERNEL_HANDLE_DETACHED_MAX];
    uint32_t active = 0u;
    uint32_t reserved_messages = 0u;
    uint32_t reserved_bytes = 0u;
    uint32_t queued_messages = 0u;
    uint32_t queued_bytes = 0u;
    uint32_t queued_handles = 0u;
    KernelHandleTransferStats transfer_stats;

    kernel_bytes_clear(seen_messages, sizeof(seen_messages));
    kernel_bytes_clear(seen_detached, sizeof(seen_detached));
    if (!kernel_port_pool_healthy() ||
        !kernel_object_cache_valid(&port_cache) ||
        !kernel_object_cache_valid(&message_cache) ||
        !kernel_handle_transfer_pool_valid())
        return false;
    for (uint32_t port_index = 0u; port_index < KERNEL_PORT_MAX;
         ++port_index) {
        const KernelPort *port = &ports[port_index];
        uint32_t readable = kernel_thread_wait_queue_count(&port->readable);
        uint32_t writable = kernel_thread_wait_queue_count(&port->writable);
        uint32_t port_messages = 0u;
        uint32_t port_bytes = 0u;
        uint16_t message_slot = port->head;
        uint16_t last = KERNEL_PORT_SLOT_NONE;
        bool claimed = kernel_object_cache_slot_claimed(
            &port_cache, (uint16_t)port_index);

        if (readable > KERNEL_PORT_WAITER_MAX ||
            writable > KERNEL_PORT_WAITER_MAX)
            return false;
        if (port->state == KERNEL_PORT_FREE) {
            if (claimed || port->owner != 0u || port->references != 0u ||
                port->send_references != 0u ||
                port->receive_references != 0u ||
                port->queued_messages != 0u || port->queued_bytes != 0u ||
                port->head != KERNEL_PORT_SLOT_NONE ||
                port->tail != KERNEL_PORT_SLOT_NONE ||
                port->capacity_reserved != 0u || readable != 0u ||
                writable != 0u)
                return false;
            continue;
        }
        if (!claimed)
            return false;
        if (port->generation == 0u || port->owner == 0u ||
            port->references !=
                port->send_references + port->receive_references ||
            port->releasing_messages != 0u)
            return false;
        if (port->state == KERNEL_PORT_OPEN) {
            if (port->send_references == 0u ||
                port->receive_references == 0u ||
                port->receive_terminal != ASTRA_SYSCALL_OK ||
                port->send_terminal != ASTRA_SYSCALL_OK ||
                port->capacity_reserved == 0u)
                return false;
        } else if (port->state == KERNEL_PORT_PEER_CLOSED) {
            if (port->send_references != 0u ||
                port->receive_references == 0u ||
                port->receive_terminal != ASTRA_SYSCALL_PEER_DEAD ||
                port->send_terminal != ASTRA_SYSCALL_PEER_DEAD ||
                port->capacity_reserved == 0u)
                return false;
        } else if (port->state == KERNEL_PORT_CLOSING) {
            if (port->capacity_reserved != 0u ||
                port->queued_messages != 0u || port->queued_bytes != 0u ||
                port->head != KERNEL_PORT_SLOT_NONE ||
                port->tail != KERNEL_PORT_SLOT_NONE ||
                port->receive_terminal == ASTRA_SYSCALL_OK ||
                port->send_terminal == ASTRA_SYSCALL_OK ||
                readable != 0u || writable != 0u)
                return false;
            continue;
        } else {
            return false;
        }
        if (port->maximum_messages == 0u ||
            port->maximum_messages > KERNEL_PORT_QUEUE_MESSAGES_MAX ||
            port->maximum_bytes < KERNEL_PORT_MESSAGE_SIZE_MIN ||
            port->maximum_bytes > KERNEL_PORT_QUEUE_BYTES_MAX ||
            port->queued_messages > port->maximum_messages ||
            port->queued_bytes > port->maximum_bytes)
            return false;
        ++active;
        reserved_messages += port->maximum_messages;
        reserved_bytes += port->maximum_bytes;

        while (message_slot != KERNEL_PORT_SLOT_NONE) {
            const KernelPortMessage *message;

            if (message_slot >= KERNEL_PORT_MESSAGE_MAX ||
                seen_messages[message_slot] != 0u ||
                port_messages >= KERNEL_PORT_MESSAGE_MAX)
                return false;
            message = &messages[message_slot];
            if ((message->state != KERNEL_PORT_MESSAGE_QUEUED &&
                 message->state != KERNEL_PORT_MESSAGE_RECEIVING) ||
                message->generation == 0u ||
                message->port_slot != port_index ||
                message->size < KERNEL_PORT_MESSAGE_SIZE_MIN ||
                message->size > KERNEL_PORT_MESSAGE_SIZE_MAX ||
                message->handle_count > KERNEL_PORT_MESSAGE_HANDLE_MAX)
                return false;
            if (message->state == KERNEL_PORT_MESSAGE_RECEIVING &&
                message_slot != port->head)
                return false;
            seen_messages[message_slot] = 1u;
            ++port_messages;
            port_bytes += message->size;
            queued_handles += message->handle_count;
            for (uint32_t handle = 0u; handle < message->handle_count;
                 ++handle) {
                uint16_t detached_slot;

                if (!kernel_handle_detached_slot(
                        message->detached[handle], &detached_slot) ||
                    detached_slot >= KERNEL_HANDLE_DETACHED_MAX ||
                    seen_detached[detached_slot] != 0u)
                    return false;
                seen_detached[detached_slot] = 1u;
            }
            last = message_slot;
            message_slot = message->next;
        }
        if (port_messages != port->queued_messages ||
            port_bytes != port->queued_bytes ||
            (port_messages == 0u &&
             port->tail != KERNEL_PORT_SLOT_NONE) ||
            (port_messages != 0u && port->tail != last))
            return false;
        queued_messages += port_messages;
        queued_bytes += port_bytes;
    }
    for (uint32_t slot = 0u; slot < KERNEL_PORT_MESSAGE_MAX; ++slot) {
        bool claimed = kernel_object_cache_slot_claimed(
            &message_cache, (uint16_t)slot);

        if (seen_messages[slot] == 0u &&
            messages[slot].state != KERNEL_PORT_MESSAGE_FREE)
            return false;
        if (claimed !=
            (messages[slot].state != KERNEL_PORT_MESSAGE_FREE))
            return false;
    }
    for (uint32_t port_index = 0u; port_index < KERNEL_PORT_MAX;
         ++port_index) {
        const KernelPort *owner_port = &ports[port_index];
        uint32_t owner_ports;
        uint32_t owner_message_capacity;
        uint32_t owner_byte_capacity;
        uint32_t owner_queued_messages = 0u;
        uint32_t owner_queued_handles = 0u;

        if (owner_port->state == KERNEL_PORT_FREE)
            continue;
        owner_reservations(owner_port->owner, &owner_ports,
                           &owner_message_capacity,
                           &owner_byte_capacity);
        if (owner_ports > KERNEL_PORT_OWNER_MAX ||
            owner_message_capacity > KERNEL_PORT_OWNER_MESSAGE_MAX ||
            owner_byte_capacity > KERNEL_PORT_OWNER_BYTES_MAX)
            return false;
        for (uint32_t slot = 0u; slot < KERNEL_PORT_MAX; ++slot) {
            const KernelPort *port = &ports[slot];
            uint16_t current;

            if (!active_state(port->state) ||
                port->owner != owner_port->owner)
                continue;
            owner_queued_messages += port->queued_messages;
            current = port->head;
            while (current != KERNEL_PORT_SLOT_NONE) {
                if (current >= KERNEL_PORT_MESSAGE_MAX)
                    return false;
                owner_queued_handles += messages[current].handle_count;
                current = messages[current].next;
            }
        }
        if (owner_queued_messages > KERNEL_PORT_OWNER_MESSAGE_MAX ||
            owner_queued_handles > 128u)
            return false;
    }
    if (active > KERNEL_PORT_MAX ||
        reserved_messages != pool_stats.reserved_message_capacity ||
        reserved_bytes != pool_stats.reserved_byte_capacity ||
        queued_messages != pool_stats.queued_messages ||
        queued_bytes != pool_stats.queued_bytes ||
        queued_handles != pool_stats.queued_handles ||
        reserved_messages > KERNEL_PORT_MESSAGE_MAX ||
        reserved_bytes > KERNEL_PORT_MESSAGE_BYTES_MAX ||
        queued_messages > KERNEL_PORT_MESSAGE_MAX ||
        queued_bytes > KERNEL_PORT_MESSAGE_BYTES_MAX ||
        queued_handles > KERNEL_HANDLE_DETACHED_MAX ||
        !kernel_handle_transfer_stats(&transfer_stats) ||
        transfer_stats.reserved_detached != 0u ||
        transfer_stats.live_detached != queued_handles)
        return false;
    return true;
}

bool kernel_port_pool_stats(KernelPortPoolStats *stats)
{
    if (stats == NULL || !kernel_port_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    stats->active_ports = active_port_count();
    stats->closing_ports = closing_port_count();
    return true;
}
