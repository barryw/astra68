#include "port.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "memory.h"

#include <stddef.h>
#include <stdint.h>
#if !defined(__m68k__)
#include <stdlib.h>
#endif

/*
 * Object tables live in their own region above the frame metadata, not beside
 * the kernel image. Raising a limit then costs address space there and moves
 * no address anything else reads -- the trace ring especially, which crash
 * tooling finds at a fixed address. kernel.ld names this region if it fills.
 */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif


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
    uint32_t sender;
    uint16_t next;
    uint16_t size;
    uint16_t port_slot;
    uint16_t slot;
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
    uint32_t maximum_bytes;
    uint16_t slot;
    uint8_t state;
    uint8_t releasing_messages;
    uint8_t reserved[2];
};

#define PORT_LEAF_BITS 6u
#define PORT_LEAF_ENTRIES (1u << PORT_LEAF_BITS)
#define PORT_LEAF_COUNT \
    ((KERNEL_PORT_MAX + PORT_LEAF_ENTRIES - 1u) / PORT_LEAF_ENTRIES)
#define PORT_LEAF_FRAMES \
    ((PORT_LEAF_ENTRIES * sizeof(KernelPort) + KERNEL_PAGE_SIZE - 1u) / \
     KERNEL_PAGE_SIZE)
#define MESSAGE_LEAF_BITS 3u
#define MESSAGE_LEAF_ENTRIES (1u << MESSAGE_LEAF_BITS)
#define MESSAGE_LEAF_COUNT \
    ((KERNEL_PORT_MESSAGE_MAX + MESSAGE_LEAF_ENTRIES - 1u) / \
     MESSAGE_LEAF_ENTRIES)
#define MESSAGE_LEAF_FRAMES \
    ((MESSAGE_LEAF_ENTRIES * sizeof(KernelPortMessage) + \
      KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE)

static KernelPort *port_directory[PORT_LEAF_COUNT] KERNEL_TABLES;
static uint32_t port_directory_physical[PORT_LEAF_COUNT] KERNEL_TABLES;
static KernelPortMessage *message_directory[MESSAGE_LEAF_COUNT] KERNEL_TABLES;
static uint32_t message_directory_physical[MESSAGE_LEAF_COUNT] KERNEL_TABLES;
static uint16_t next_port_slot;
static uint16_t next_message_slot;
static uint32_t port_backed_limit;
static uint32_t message_backed_limit;
static uint32_t validation_seen_messages[
    (KERNEL_PORT_MESSAGE_MAX + 31u) / 32u] KERNEL_TABLES;
static uint32_t validation_seen_detached[
    (KERNEL_HANDLE_DETACHED_MAX + 31u) / 32u] KERNEL_TABLES;
static KernelPortPoolStats pool_stats;
static uint8_t pool_corrupt;

#if defined(__m68k__)
_Static_assert(sizeof(KernelPort) <= 80u,
               "message-port object memory budget changed");
#endif
/*
 * A record is the largest message it can hold plus its bookkeeping, so an exact
 * number here would have to be edited every time the inline limit moves and
 * would say nothing about whether the result still fits. These bound it
 * instead: the record may not grow slack beyond its payload, and the pool may
 * not take more than half the object-table region -- kernel.ld catches the
 * overflow, this catches it earlier and says which pool did it.
 */
_Static_assert(sizeof(KernelPortMessage) <=
                   KERNEL_PORT_MESSAGE_SIZE_MAX +
                       KERNEL_PORT_MESSAGE_HANDLE_MAX *
                           sizeof(KernelDetachedHandle) + 64u,
               "message record overhead grew beyond its payload");

static void reset_message_metadata(KernelPortMessage *message,
                                   uint32_t generation, uint8_t state)
{
    /*
     * Payload and detached slots are bounded by size and handle_count and are
     * overwritten before publication.  Clearing the entire 1,096-byte record
     * on both claim and release made a 36-byte send pay for 2,192 bytes while
     * exposing no additional state to a receiver.
     */
    message->generation = generation;
    message->sender = 0u;
    message->next = KERNEL_PORT_SLOT_NONE;
    message->size = 0u;
    message->port_slot = KERNEL_PORT_SLOT_NONE;
    message->handle_count = 0u;
    message->state = state;
}

static void *allocate_metadata(KernelAllocationSite site, uint32_t bytes,
                               uint32_t *physical)
{
    uint32_t frames = (bytes + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;

#if defined(__m68k__)
    if (kernel_memory_alloc_zeroed_tagged(
            site, frames, 1u, KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE,
            physical) != KERNEL_MEMORY_OK)
        return NULL;
    void *memory = kernel_memory_access(*physical,
                                        frames * KERNEL_PAGE_SIZE);
    if (memory == NULL) {
        (void)kernel_memory_release(*physical, frames, KERNEL_OWNER_CORE);
        return NULL;
    }
    return memory;
#else
    if (!kernel_allocation_attempt(site, KERNEL_OWNER_CORE))
        return NULL;
    void *memory = calloc(frames, KERNEL_PAGE_SIZE);

    *physical = 0u;
    if (memory == NULL) {
        kernel_allocation_fail(site, KERNEL_OWNER_CORE);
        return NULL;
    }
    if (!kernel_allocation_commit(site, frames,
                                  frames * KERNEL_PAGE_SIZE,
                                  KERNEL_OWNER_CORE)) {
        free(memory);
        return NULL;
    }
    return memory;
#endif
}

static KernelPort *port_at(uint32_t slot)
{
    KernelPort *leaf;

    if (slot >= KERNEL_PORT_MAX)
        return NULL;
    leaf = port_directory[slot >> PORT_LEAF_BITS];
    return leaf != NULL ? &leaf[slot & (PORT_LEAF_ENTRIES - 1u)] : NULL;
}

static KernelPortMessage *message_at(uint32_t slot)
{
    KernelPortMessage *leaf;

    if (slot >= KERNEL_PORT_MESSAGE_MAX)
        return NULL;
    leaf = message_directory[slot >> MESSAGE_LEAF_BITS];
    return leaf != NULL ? &leaf[slot & (MESSAGE_LEAF_ENTRIES - 1u)] : NULL;
}

static bool ensure_port_leaf(uint32_t slot)
{
    uint32_t leaf_index = slot >> PORT_LEAF_BITS;
    uint32_t bytes = PORT_LEAF_ENTRIES * sizeof(KernelPort);
    KernelPort *leaf;

    if (port_directory[leaf_index] != NULL)
        return true;
    leaf = allocate_metadata(KERNEL_ALLOCATION_SITE_PORT_METADATA, bytes,
                             &port_directory_physical[leaf_index]);
    if (leaf == NULL)
        return false;
    port_directory[leaf_index] = leaf;
    for (uint32_t index = 0u; index < PORT_LEAF_ENTRIES; ++index) {
        uint32_t record_slot = leaf_index * PORT_LEAF_ENTRIES + index;

        kernel_thread_wait_queue_init(&leaf[index].readable);
        kernel_thread_wait_queue_init(&leaf[index].writable);
        leaf[index].head = KERNEL_PORT_SLOT_NONE;
        leaf[index].tail = KERNEL_PORT_SLOT_NONE;
        leaf[index].slot = record_slot < KERNEL_PORT_MAX ?
            (uint16_t)record_slot : KERNEL_PORT_SLOT_NONE;
        leaf[index].state = KERNEL_PORT_FREE;
    }
    uint32_t limit = (leaf_index + 1u) * PORT_LEAF_ENTRIES;
    if (limit > KERNEL_PORT_MAX)
        limit = KERNEL_PORT_MAX;
    if (limit > port_backed_limit)
        port_backed_limit = limit;
    return true;
}

static bool release_record_allocations(KernelAllocationSite site,
                                       uint32_t record_size)
{
    KernelAllocationStats allocations;

    return kernel_allocation_site_stats(site, &allocations) &&
           allocations.current_bytes ==
               allocations.current_units * record_size &&
           (allocations.current_units == 0u ||
            kernel_allocation_release(
                site, allocations.current_units, allocations.current_bytes));
}

static bool discard_port_metadata(void)
{
    KernelAllocationStats message_metadata;
    KernelAllocationStats port_metadata;
    uint32_t message_leaves = 0u;
    uint32_t port_leaves = 0u;

    for (uint32_t leaf = 0u; leaf < PORT_LEAF_COUNT; ++leaf) {
        if (port_directory[leaf] == NULL)
            continue;
        if (leaf != port_leaves)
            return false;
        ++port_leaves;
    }
    for (uint32_t leaf = 0u; leaf < MESSAGE_LEAF_COUNT; ++leaf) {
        if (message_directory[leaf] == NULL)
            continue;
        if (leaf != message_leaves)
            return false;
        ++message_leaves;
    }
    if (!kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_METADATA, &port_metadata) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_MESSAGE_METADATA,
            &message_metadata) ||
        ((port_metadata.current_units != 0u ||
          port_metadata.current_bytes != 0u) &&
         (port_metadata.current_units !=
              port_leaves * PORT_LEAF_FRAMES ||
          port_metadata.current_bytes !=
              port_leaves * PORT_LEAF_FRAMES * KERNEL_PAGE_SIZE)) ||
        ((message_metadata.current_units != 0u ||
          message_metadata.current_bytes != 0u) &&
         (message_metadata.current_units !=
              message_leaves * MESSAGE_LEAF_FRAMES ||
          message_metadata.current_bytes !=
              message_leaves * MESSAGE_LEAF_FRAMES * KERNEL_PAGE_SIZE)))
        return false;

    for (uint32_t leaf = 0u; leaf < port_leaves; ++leaf) {
#if defined(__m68k__)
        if (port_metadata.current_units != 0u &&
            kernel_memory_release(
                port_directory_physical[leaf], PORT_LEAF_FRAMES,
                KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            return false;
#else
        free(port_directory[leaf]);
#endif
        port_directory[leaf] = NULL;
        port_directory_physical[leaf] = 0u;
    }
    for (uint32_t leaf = 0u; leaf < message_leaves; ++leaf) {
#if defined(__m68k__)
        if (message_metadata.current_units != 0u &&
            kernel_memory_release(
                message_directory_physical[leaf], MESSAGE_LEAF_FRAMES,
                KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            return false;
#else
        free(message_directory[leaf]);
#endif
        message_directory[leaf] = NULL;
        message_directory_physical[leaf] = 0u;
    }
#if !defined(__m68k__)
    if ((port_metadata.current_units != 0u &&
         !kernel_allocation_release(
             KERNEL_ALLOCATION_SITE_PORT_METADATA,
             port_metadata.current_units, port_metadata.current_bytes)) ||
        (message_metadata.current_units != 0u &&
         !kernel_allocation_release(
             KERNEL_ALLOCATION_SITE_PORT_MESSAGE_METADATA,
             message_metadata.current_units,
             message_metadata.current_bytes)))
        return false;
#endif
    port_backed_limit = 0u;
    message_backed_limit = 0u;
    return true;
}

static bool ensure_message_leaf(uint32_t slot)
{
    uint32_t leaf_index = slot >> MESSAGE_LEAF_BITS;
    uint32_t bytes = MESSAGE_LEAF_ENTRIES * sizeof(KernelPortMessage);
    KernelPortMessage *leaf;

    if (message_directory[leaf_index] != NULL)
        return true;
    leaf = allocate_metadata(
        KERNEL_ALLOCATION_SITE_PORT_MESSAGE_METADATA, bytes,
        &message_directory_physical[leaf_index]);
    if (leaf == NULL)
        return false;
    message_directory[leaf_index] = leaf;
    for (uint32_t index = 0u; index < MESSAGE_LEAF_ENTRIES; ++index) {
        uint32_t record_slot = leaf_index * MESSAGE_LEAF_ENTRIES + index;

        leaf[index].slot = record_slot < KERNEL_PORT_MESSAGE_MAX ?
            (uint16_t)record_slot : KERNEL_PORT_SLOT_NONE;
        reset_message_metadata(&leaf[index], 0u, KERNEL_PORT_MESSAGE_FREE);
    }
    uint32_t limit = (leaf_index + 1u) * MESSAGE_LEAF_ENTRIES;
    if (limit > KERNEL_PORT_MESSAGE_MAX)
        limit = KERNEL_PORT_MESSAGE_MAX;
    if (limit > message_backed_limit)
        message_backed_limit = limit;
    return true;
}

static uint16_t port_slot(const KernelPort *port)
{
    return port->slot;
}

static bool valid_port_pointer(const KernelPort *port)
{
    return port != NULL && port->slot < KERNEL_PORT_MAX &&
           port_at(port->slot) == port;
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

    for (uint32_t slot = 0u; slot < port_backed_limit; ++slot) {
        KernelPort *port = port_at(slot);
        if (port != NULL && active_state(port->state))
            ++count;
    }
    return count;
}

static uint32_t closing_port_count(void)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < port_backed_limit; ++slot) {
        KernelPort *port = port_at(slot);
        if (port != NULL && port->state == KERNEL_PORT_CLOSING)
            ++count;
    }
    return count;
}

static KernelPortStatus allocate_message(uint32_t owner,
                                         KernelPortMessage **result)
{
    if (result == NULL)
        return KERNEL_PORT_INVALID_ARGUMENT;
    *result = NULL;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_PORT_MESSAGE,
                                   owner)) {
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    KernelPortMessage *message = NULL;
    for (uint32_t offset = 0u; offset < KERNEL_PORT_MESSAGE_MAX; ++offset) {
        uint32_t slot = (uint32_t)next_message_slot + offset;
        if (slot >= KERNEL_PORT_MESSAGE_MAX)
            slot -= KERNEL_PORT_MESSAGE_MAX;
        if (!ensure_message_leaf(slot))
            break;
        message = message_at(slot);
        if (message->state == KERNEL_PORT_MESSAGE_FREE) {
            next_message_slot = slot + 1u == KERNEL_PORT_MESSAGE_MAX ?
                0u : (uint16_t)(slot + 1u);
            break;
        }
        message = NULL;
    }
    if (message == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_PORT_MESSAGE, owner);
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    uint32_t generation;

    if (message->state != KERNEL_PORT_MESSAGE_FREE) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    generation = kernel_generation_next(message->generation);
    reset_message_metadata(message, generation,
                           KERNEL_PORT_MESSAGE_RESERVED);
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_PORT_MESSAGE, 1u,
                                  sizeof(*message), owner)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    *result = message;
    return KERNEL_PORT_OK;
}

static void free_message(KernelPortMessage *message)
{
    uint32_t generation;
    uint16_t slot;

    if (message == NULL || message->slot >= KERNEL_PORT_MESSAGE_MAX ||
        message_at(message->slot) != message ||
        message->state == KERNEL_PORT_MESSAGE_FREE) {
        pool_corrupt = 1u;
        return;
    }
    generation = message->generation;
    slot = message->slot;
    reset_message_metadata(message, generation, KERNEL_PORT_MESSAGE_FREE);
    if (slot < next_message_slot)
        next_message_slot = slot;
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_PORT_MESSAGE, 1u,
                                   sizeof(*message)))
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
               port->maximum_bytes -
                   KERNEL_PORT_MESSAGE_SIZE_MIN;
}

static void maybe_free_port(KernelPort *port)
{
    uint32_t generation;
    uint16_t slot;

    if (!valid_port_pointer(port) || port->state != KERNEL_PORT_CLOSING ||
        port->references != 0u || port->queued_messages != 0u ||
        port->queued_bytes != 0u || port->head != KERNEL_PORT_SLOT_NONE ||
        port->tail != KERNEL_PORT_SLOT_NONE ||
        port->releasing_messages != 0u)
        return;
    if (kernel_thread_wait_queue_count(&port->readable) != 0u ||
        kernel_thread_wait_queue_count(&port->writable) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    generation = port->generation;
    slot = port->slot;
    kernel_bytes_clear(port, sizeof(*port));
    kernel_thread_wait_queue_init(&port->readable);
    kernel_thread_wait_queue_init(&port->writable);
    port->generation = generation;
    port->slot = slot;
    port->head = KERNEL_PORT_SLOT_NONE;
    port->tail = KERNEL_PORT_SLOT_NONE;
    port->state = KERNEL_PORT_FREE;
    if (slot < next_port_slot)
        next_port_slot = slot;
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_PORT_OBJECT, 1u,
                                   sizeof(*port)))
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
        message = message_at(slot);
        if (message == NULL) {
            pool_corrupt = 1u;
            break;
        }
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
    if (!release_record_allocations(
            KERNEL_ALLOCATION_SITE_PORT_OBJECT, sizeof(KernelPort)) ||
        !release_record_allocations(
            KERNEL_ALLOCATION_SITE_PORT_MESSAGE,
            sizeof(KernelPortMessage)) ||
        !discard_port_metadata()) {
        pool_corrupt = 1u;
        return;
    }
    next_port_slot = 0u;
    next_message_slot = 0u;
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
}

KernelPortStatus kernel_port_create(uint32_t owner,
                                    uint32_t maximum_messages,
                                    uint32_t maximum_bytes,
                                    KernelPort **port)
{
    if (port == NULL || owner == 0u || maximum_messages == 0u ||
        maximum_messages > KERNEL_PORT_QUEUE_MESSAGES_MAX ||
        maximum_bytes < KERNEL_PORT_MESSAGE_SIZE_MIN ||
        maximum_bytes > KERNEL_PORT_QUEUE_BYTES_MAX ||
        maximum_messages > UINT16_MAX)
        return KERNEL_PORT_INVALID_ARGUMENT;
    *port = NULL;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_PORT_OBJECT,
                                   owner)) {
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    KernelPort *candidate = NULL;
    for (uint32_t offset = 0u; offset < KERNEL_PORT_MAX; ++offset) {
        uint32_t slot = (uint32_t)next_port_slot + offset;
        if (slot >= KERNEL_PORT_MAX)
            slot -= KERNEL_PORT_MAX;
        if (!ensure_port_leaf(slot))
            break;
        candidate = port_at(slot);
        if (candidate->state == KERNEL_PORT_FREE) {
            next_port_slot = slot + 1u == KERNEL_PORT_MAX ?
                0u : (uint16_t)(slot + 1u);
            break;
        }
        candidate = NULL;
    }
    if (candidate == NULL) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_PORT_OBJECT, owner);
        ++pool_stats.allocation_failures;
        return KERNEL_PORT_NO_SLOT;
    }
    uint32_t generation;
    uint32_t active;

    if (candidate->state != KERNEL_PORT_FREE) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
    generation = kernel_generation_next(candidate->generation);
    uint16_t slot = candidate->slot;
    kernel_bytes_clear(candidate, sizeof(*candidate));
    kernel_thread_wait_queue_init(&candidate->readable);
    kernel_thread_wait_queue_init(&candidate->writable);
    candidate->owner = owner;
    candidate->generation = generation;
    candidate->slot = slot;
    candidate->references = 2u;
    candidate->send_references = 1u;
    candidate->receive_references = 1u;
    candidate->head = KERNEL_PORT_SLOT_NONE;
    candidate->tail = KERNEL_PORT_SLOT_NONE;
    candidate->maximum_messages = (uint16_t)maximum_messages;
    candidate->maximum_bytes = maximum_bytes;
    candidate->state = KERNEL_PORT_OPEN;
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_PORT_OBJECT, 1u,
                                  sizeof(*candidate), owner)) {
        pool_corrupt = 1u;
        return KERNEL_PORT_CORRUPT;
    }
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
        message_size > port->maximum_bytes - port->queued_bytes) {
        ++pool_stats.send_would_block;
        return KERNEL_PORT_WOULD_BLOCK;
    }
    if ((port->queued_messages == 0u &&
         (port->head != KERNEL_PORT_SLOT_NONE ||
          port->tail != KERNEL_PORT_SLOT_NONE)) ||
        (port->queued_messages != 0u &&
         (port->head >= KERNEL_PORT_MESSAGE_MAX ||
          port->tail >= KERNEL_PORT_MESSAGE_MAX ||
          message_at(port->tail) == NULL ||
          message_at(port->tail)->state != KERNEL_PORT_MESSAGE_QUEUED ||
          message_at(port->tail)->next != KERNEL_PORT_SLOT_NONE)))
        return KERNEL_PORT_CORRUPT;

    if (handle_count != 0u) {
        handle_status = kernel_handle_transfer_validate(
            source_table, handles, handle_count, ASTRA_RIGHT_TRANSFER);
        if (handle_status != KERNEL_HANDLE_OK)
            return map_handle_status(handle_status);
    }

    message_status = allocate_message(port->owner, &message);
    if (message_status != KERNEL_PORT_OK)
        return message_status;
    kernel_bytes_copy(message->data, raw_message, message_size);
    message->size = (uint16_t)message_size;
    message->sender = source_table->process_id;
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

    message_slot = message->slot;
    message->state = KERNEL_PORT_MESSAGE_QUEUED;
    if (port->tail == KERNEL_PORT_SLOT_NONE) {
        port->head = message_slot;
        port->tail = message_slot;
    } else {
        message_at(port->tail)->next = message_slot;
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
    message = message_at(slot);
    if (message == NULL)
        return KERNEL_PORT_CORRUPT;
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
    receipt->sender = message->sender;
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
    *message = message_at(receipt->message_slot);
    if (*message == NULL)
        return false;
    return (*message)->state == KERNEL_PORT_MESSAGE_RECEIVING &&
           (*message)->generation == receipt->message_generation &&
           (*message)->sender == receipt->sender &&
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
         port->tail != receipt->message_slot))
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
            message_at(port->head) != NULL &&
            message_at(port->head)->state == KERNEL_PORT_MESSAGE_QUEUED)
            return KERNEL_PORT_OK;
        if (port->state == KERNEL_PORT_PEER_CLOSED &&
            port->queued_messages == 0u)
            return KERNEL_PORT_PEER_DEAD;
        queue = &port->readable;
    }
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
    return waiters != 0u && waiters != UINT32_MAX ?
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
    for (uint32_t slot = 0u; slot < port_backed_limit; ++slot) {
        KernelPort *port = port_at(slot);
        uint32_t port_woken = 0u;

        if (port == NULL || !active_state(port->state) ||
            port->owner != owner)
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
    port = port_at(slot);
    if (port == NULL)
        return false;
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
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    snapshot->reserved[2] = 0u;
    return true;
}

bool kernel_port_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_port_pool_valid(void)
{
    KernelAllocationStats port_allocations;
    KernelAllocationStats port_metadata;
    KernelAllocationStats message_allocations;
    KernelAllocationStats message_metadata;
    uint32_t active = 0u;
    uint32_t live_ports = 0u;
    uint32_t port_leaves = 0u;
    uint32_t message_leaves = 0u;
    uint32_t queued_messages = 0u;
    uint32_t queued_bytes = 0u;
    uint32_t queued_handles = 0u;
    KernelHandleTransferStats transfer_stats;

    kernel_bytes_clear(validation_seen_messages,
                       sizeof(validation_seen_messages));
    kernel_bytes_clear(validation_seen_detached,
                       sizeof(validation_seen_detached));
    if (!kernel_port_pool_healthy() ||
        !kernel_handle_transfer_pool_valid())
        return false;
    while (port_leaves < PORT_LEAF_COUNT &&
           port_directory[port_leaves] != NULL)
        ++port_leaves;
    for (uint32_t leaf = port_leaves; leaf < PORT_LEAF_COUNT; ++leaf)
        if (port_directory[leaf] != NULL)
            return false;
    while (message_leaves < MESSAGE_LEAF_COUNT &&
           message_directory[message_leaves] != NULL)
        ++message_leaves;
    for (uint32_t leaf = message_leaves; leaf < MESSAGE_LEAF_COUNT; ++leaf)
        if (message_directory[leaf] != NULL)
            return false;
    uint32_t expected_port_limit = port_leaves * PORT_LEAF_ENTRIES;
    uint32_t expected_message_limit = message_leaves * MESSAGE_LEAF_ENTRIES;
    if (expected_port_limit > KERNEL_PORT_MAX)
        expected_port_limit = KERNEL_PORT_MAX;
    if (expected_message_limit > KERNEL_PORT_MESSAGE_MAX)
        expected_message_limit = KERNEL_PORT_MESSAGE_MAX;
    if (port_backed_limit != expected_port_limit ||
        message_backed_limit != expected_message_limit)
        return false;
    for (uint32_t port_index = 0u; port_index < port_backed_limit;
         ++port_index) {
        const KernelPort *port = port_at(port_index);
        uint32_t readable = kernel_thread_wait_queue_count(&port->readable);
        uint32_t writable = kernel_thread_wait_queue_count(&port->writable);
        uint32_t port_messages = 0u;
        uint32_t port_bytes = 0u;
        uint16_t message_slot = port->head;
        uint16_t last = KERNEL_PORT_SLOT_NONE;
        bool claimed;

        if (port == NULL || port->slot != port_index)
            return false;
        claimed = port->state != KERNEL_PORT_FREE;
        if (claimed)
            ++live_ports;

        if (readable == UINT32_MAX || writable == UINT32_MAX)
            return false;
        if (port->state == KERNEL_PORT_FREE) {
            if (claimed || port->owner != 0u || port->references != 0u ||
                port->send_references != 0u ||
                port->receive_references != 0u ||
                port->queued_messages != 0u || port->queued_bytes != 0u ||
                port->head != KERNEL_PORT_SLOT_NONE ||
                port->tail != KERNEL_PORT_SLOT_NONE || readable != 0u ||
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
                port->send_terminal != ASTRA_SYSCALL_OK)
                return false;
        } else if (port->state == KERNEL_PORT_PEER_CLOSED) {
            if (port->send_references != 0u ||
                port->receive_references == 0u ||
                port->receive_terminal != ASTRA_SYSCALL_PEER_DEAD ||
                port->send_terminal != ASTRA_SYSCALL_PEER_DEAD)
                return false;
        } else if (port->state == KERNEL_PORT_CLOSING) {
            if (port->queued_messages != 0u || port->queued_bytes != 0u ||
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
            port->maximum_bytes < KERNEL_PORT_MESSAGE_SIZE_MIN ||
            port->maximum_bytes > KERNEL_PORT_QUEUE_BYTES_MAX ||
            port->queued_messages > port->maximum_messages ||
            port->queued_bytes > port->maximum_bytes)
            return false;
        ++active;

        while (message_slot != KERNEL_PORT_SLOT_NONE) {
            const KernelPortMessage *message;

            uint32_t seen_mask = 1u << (message_slot & 31u);
            if (message_slot >= KERNEL_PORT_MESSAGE_MAX ||
                (validation_seen_messages[message_slot >> 5] &
                 seen_mask) != 0u ||
                port_messages >= KERNEL_PORT_MESSAGE_MAX)
                return false;
            message = message_at(message_slot);
            if (message == NULL)
                return false;
            if ((message->state != KERNEL_PORT_MESSAGE_QUEUED &&
                 message->state != KERNEL_PORT_MESSAGE_RECEIVING) ||
                message->generation == 0u ||
                message->port_slot != port_index ||
                message->size < KERNEL_PORT_MESSAGE_SIZE_MIN ||
                message->size > KERNEL_PORT_MESSAGE_SIZE_MAX)
                return false;
            if (message->state == KERNEL_PORT_MESSAGE_RECEIVING &&
                message_slot != port->head)
                return false;
            validation_seen_messages[message_slot >> 5] |= seen_mask;
            ++port_messages;
            port_bytes += message->size;
            queued_handles += message->handle_count;
            for (uint32_t handle = 0u; handle < message->handle_count;
                 ++handle) {
                uint16_t detached_slot;
                uint32_t detached_mask;

                if (!kernel_handle_detached_slot(
                        message->detached[handle], &detached_slot) ||
                    detached_slot >= KERNEL_HANDLE_DETACHED_MAX)
                    return false;
                detached_mask = 1u << (detached_slot & 31u);
                if ((validation_seen_detached[detached_slot >> 5] &
                     detached_mask) != 0u)
                    return false;
                validation_seen_detached[detached_slot >> 5] |= detached_mask;
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
    for (uint32_t slot = 0u; slot < message_backed_limit; ++slot) {
        KernelPortMessage *message = message_at(slot);
        bool seen = (validation_seen_messages[slot >> 5] &
                     (1u << (slot & 31u))) != 0u;

        if (message == NULL || message->slot != slot ||
            (seen != (message->state != KERNEL_PORT_MESSAGE_FREE)))
            return false;
    }
    if (active > KERNEL_PORT_MAX ||
        queued_messages != pool_stats.queued_messages ||
        queued_bytes != pool_stats.queued_bytes ||
        queued_handles != pool_stats.queued_handles ||
        queued_messages > KERNEL_PORT_MESSAGE_MAX ||
        queued_bytes > KERNEL_PORT_MESSAGE_BYTES_MAX ||
        queued_handles > KERNEL_HANDLE_DETACHED_MAX ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_OBJECT, &port_allocations) ||
        port_allocations.current_units != live_ports ||
        port_allocations.current_bytes != live_ports * sizeof(KernelPort) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_MESSAGE, &message_allocations) ||
        message_allocations.current_units != queued_messages ||
        message_allocations.current_bytes !=
            queued_messages * sizeof(KernelPortMessage) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_METADATA, &port_metadata) ||
        port_metadata.current_units != port_leaves * PORT_LEAF_FRAMES ||
        port_metadata.current_bytes !=
            port_leaves * PORT_LEAF_FRAMES * KERNEL_PAGE_SIZE ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_PORT_MESSAGE_METADATA,
            &message_metadata) ||
        message_metadata.current_units !=
            message_leaves * MESSAGE_LEAF_FRAMES ||
        message_metadata.current_bytes !=
            message_leaves * MESSAGE_LEAF_FRAMES * KERNEL_PAGE_SIZE ||
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
