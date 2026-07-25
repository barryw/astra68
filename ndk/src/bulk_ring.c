#include <astra/bulk_ring.h>

#include "internal/syscall.h"

#include <stddef.h>

_Static_assert(sizeof(AstraBulkRingHeader) == ASTRA_BULK_RING_HEADER_SIZE,
               "AstraBulkRingHeader ABI size");
_Static_assert(offsetof(AstraBulkRingHeader, producer_position) == 0x20u,
               "AstraBulkRingHeader producer offset");
_Static_assert(offsetof(AstraBulkRingHeader, consumer_position) == 0x30u,
               "AstraBulkRingHeader consumer offset");

static int power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static int valid_role(uint8_t role)
{
    return role == ASTRA_BULK_RING_PRODUCER ||
           role == ASTRA_BULK_RING_CONSUMER;
}

static int empty_ring(const AstraBulkRing *ring)
{
    return ring != 0 && ring->endpoint == ASTRA_INVALID_HANDLE &&
           ring->header == 0 && ring->data == 0 && ring->area_size == 0u &&
           ring->offset == 0u && ring->element_size == 0u &&
           ring->capacity == 0u && ring->generation == 0u &&
           ring->local_position == 0u && ring->peer_position == 0u &&
           ring->reservation_position == 0u && ring->role == 0u &&
           ring->reservation_active == 0u && ring->failed == 0u &&
           ring->reserved == 0u;
}

static void clear_ring(AstraBulkRing *ring)
{
    ring->endpoint = ASTRA_INVALID_HANDLE;
    ring->header = 0;
    ring->data = 0;
    ring->area_size = 0u;
    ring->offset = 0u;
    ring->element_size = 0u;
    ring->capacity = 0u;
    ring->generation = 0u;
    ring->local_position = 0u;
    ring->peer_position = 0u;
    ring->reservation_position = 0u;
    ring->role = 0u;
    ring->reservation_active = 0u;
    ring->failed = 0u;
    ring->reserved = 0u;
}

static void acquire_fence(void)
{
    __asm__ volatile("" : : : "memory");
}

static void release_fence(void)
{
#if defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
#endif
}

static int reserved_zero(const volatile uint32_t reserved[3])
{
    return reserved[0] == 0u && reserved[1] == 0u && reserved[2] == 0u;
}

static int header_valid(const AstraBulkRing *ring)
{
    volatile AstraBulkRingHeader *header;
    uint32_t producer;
    uint32_t consumer;
    uint32_t total;

    if (ring == 0 || ring->header == 0 || ring->data == 0 ||
        ring->endpoint == ASTRA_INVALID_HANDLE || !valid_role(ring->role) ||
        ring->element_size < ASTRA_BULK_RING_ELEMENT_SIZE_MIN ||
        ring->element_size > ASTRA_BULK_RING_ELEMENT_SIZE_MAX ||
        (ring->element_size & 3u) != 0u ||
        ring->capacity < ASTRA_BULK_RING_CAPACITY_MIN ||
        ring->capacity > ASTRA_BULK_RING_CAPACITY_MAX ||
        !power_of_two(ring->capacity) || ring->generation == 0u)
        return 0;
    header = ring->header;
    total = ASTRA_BULK_RING_HEADER_SIZE +
            ring->element_size * ring->capacity;
    if (header->magic != ASTRA_BULK_RING_MAGIC ||
        header->version != ASTRA_BULK_RING_ABI_VERSION ||
        header->header_size != ASTRA_BULK_RING_HEADER_SIZE ||
        header->flags != 0u || header->element_size != ring->element_size ||
        header->capacity != ring->capacity ||
        header->data_offset != ASTRA_BULK_RING_HEADER_SIZE ||
        header->total_size != total || header->generation != ring->generation ||
        !reserved_zero(header->producer_reserved) ||
        !reserved_zero(header->consumer_reserved) ||
        ring->offset > ring->area_size || total > ring->area_size - ring->offset)
        return 0;
    producer = header->producer_position;
    consumer = header->consumer_position;
    if (producer - consumer > ring->capacity)
        return 0;
    return ring->role == ASTRA_BULK_RING_PRODUCER ?
        producer == ring->local_position :
        consumer == ring->local_position;
}

static AstraResult report_corruption(AstraBulkRing *ring)
{
    uint32_t ignored1;
    uint32_t ignored2;
    AstraResult result;

    if (ring == 0 || ring->endpoint == ASTRA_INVALID_HANDLE ||
        !valid_role(ring->role))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    ring->failed = 1u;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_RING_NOTIFY, ring->endpoint, ring->local_position,
        ASTRA_BULK_RING_NOTIFY_CORRUPT, ring->role, 0,
        &ignored1, &ignored2));
    return result == ASTRA_ERROR_INVALID_HANDLE ? result : ASTRA_ERROR_IO;
}

static AstraResult refresh(AstraBulkRing *ring)
{
    AstraResult result;
    uint32_t producer;
    uint32_t consumer;

    if (ring == 0 || ring->failed != 0u)
        return ring == 0 ? ASTRA_ERROR_INVALID_ARGUMENT : ASTRA_ERROR_IO;
    if (ring->reservation_active != 0u)
        return ASTRA_ERROR_BUSY;
    if (!header_valid(ring))
        return report_corruption(ring);
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_RING_NOTIFY, ring->endpoint, ring->local_position,
        0, ring->role, 0, &producer, &consumer));
    if (result != ASTRA_OK)
        return result;
    if (producer - consumer > ring->capacity ||
        (ring->role == ASTRA_BULK_RING_PRODUCER &&
         producer != ring->local_position) ||
        (ring->role == ASTRA_BULK_RING_CONSUMER &&
         consumer != ring->local_position))
        return report_corruption(ring);
    ring->peer_position = ring->role == ASTRA_BULK_RING_PRODUCER ?
        consumer : producer;
    if (!header_valid(ring))
        return report_corruption(ring);
    return ASTRA_OK;
}

static int endpoint_ready(const AstraBulkRing *ring)
{
    return ring->role == ASTRA_BULK_RING_PRODUCER ?
        ring->local_position - ring->peer_position < ring->capacity :
        ring->peer_position - ring->local_position != 0u;
}

AstraResult astra_bulk_ring_create(AstraHandle area, uint32_t offset,
                                   uint32_t element_size, uint32_t capacity,
                                   AstraBulkRingEndpoints *endpoints)
{
    AstraResult result;
    uint32_t producer;
    uint32_t consumer;

    if (area == ASTRA_INVALID_HANDLE || endpoints == 0 ||
        endpoints->producer != ASTRA_INVALID_HANDLE ||
        endpoints->consumer != ASTRA_INVALID_HANDLE ||
        (offset & (ASTRA_BULK_RING_OFFSET_ALIGNMENT - 1u)) != 0u ||
        element_size < ASTRA_BULK_RING_ELEMENT_SIZE_MIN ||
        element_size > ASTRA_BULK_RING_ELEMENT_SIZE_MAX ||
        (element_size & 3u) != 0u ||
        capacity < ASTRA_BULK_RING_CAPACITY_MIN ||
        capacity > ASTRA_BULK_RING_CAPACITY_MAX || !power_of_two(capacity))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_RING_CREATE, area, offset, element_size, capacity, 0,
        &producer, &consumer));
    if (result == ASTRA_OK) {
        if (producer == ASTRA_INVALID_HANDLE ||
            consumer == ASTRA_INVALID_HANDLE || producer == consumer)
            return ASTRA_ERROR_IO;
        endpoints->producer = producer;
        endpoints->consumer = consumer;
    }
    return result;
}

AstraResult astra_bulk_ring_endpoints_close(
    AstraBulkRingEndpoints *endpoints)
{
    AstraResult first = ASTRA_OK;
    AstraResult result;
    uint32_t attempted = 0u;

    if (endpoints == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (endpoints->producer != ASTRA_INVALID_HANDLE) {
        ++attempted;
        result = astra_handle_close(&endpoints->producer);
        if (result != ASTRA_OK)
            first = result;
    }
    if (endpoints->consumer != ASTRA_INVALID_HANDLE) {
        ++attempted;
        result = astra_handle_close(&endpoints->consumer);
        if (result != ASTRA_OK && first == ASTRA_OK)
            first = result;
    }
    return attempted == 0u ? ASTRA_ERROR_INVALID_HANDLE : first;
}

void astra_bulk_ring_endpoints_cleanup(AstraBulkRingEndpoints *endpoints)
{
    AstraResult ignored = astra_bulk_ring_endpoints_close(endpoints);

    (void)ignored;
}

AstraResult astra_bulk_ring_attach(AstraBulkRing *ring,
                                   AstraHandle *endpoint,
                                   const AstraArea *area, uint32_t offset,
                                   uint8_t role)
{
    AstraBulkRing prepared = ASTRA_BULK_RING_INIT;
    volatile AstraBulkRingHeader *header;
    uint32_t total;
    AstraResult result;

    if (!empty_ring(ring) || endpoint == 0 ||
        *endpoint == ASTRA_INVALID_HANDLE || area == 0 || area->address == 0 ||
        area->size < ASTRA_BULK_RING_HEADER_SIZE ||
        (area->map_flags & (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE)) !=
            (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE) ||
        (offset & (ASTRA_BULK_RING_OFFSET_ALIGNMENT - 1u)) != 0u ||
        offset > area->size - ASTRA_BULK_RING_HEADER_SIZE || !valid_role(role))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    header = (volatile AstraBulkRingHeader *)(void *)
        ((uint8_t *)area->address + offset);
    if (header->element_size < ASTRA_BULK_RING_ELEMENT_SIZE_MIN ||
        header->element_size > ASTRA_BULK_RING_ELEMENT_SIZE_MAX ||
        (header->element_size & 3u) != 0u ||
        header->capacity < ASTRA_BULK_RING_CAPACITY_MIN ||
        header->capacity > ASTRA_BULK_RING_CAPACITY_MAX ||
        !power_of_two(header->capacity)) {
        prepared.endpoint = *endpoint;
        prepared.role = role;
        return report_corruption(&prepared);
    }
    total = ASTRA_BULK_RING_HEADER_SIZE +
            header->element_size * header->capacity;
    prepared.endpoint = *endpoint;
    prepared.header = header;
    prepared.data = (volatile uint8_t *)(void *)header +
                    ASTRA_BULK_RING_HEADER_SIZE;
    prepared.area_size = area->size;
    prepared.offset = offset;
    prepared.element_size = header->element_size;
    prepared.capacity = header->capacity;
    prepared.generation = header->generation;
    prepared.local_position = role == ASTRA_BULK_RING_PRODUCER ?
        header->producer_position : header->consumer_position;
    prepared.peer_position = prepared.local_position;
    prepared.role = role;
    if (total > area->size - offset || !header_valid(&prepared))
        return report_corruption(&prepared);
    result = refresh(&prepared);
    if (result != ASTRA_OK)
        return result;
    *ring = prepared;
    *endpoint = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}

AstraResult astra_bulk_ring_notify(AstraBulkRing *ring)
{
    return refresh(ring);
}

AstraResult astra_bulk_ring_wait_until(AstraBulkRing *ring,
                                       AstraMonotonicDeadline deadline_ns)
{
    uint64_t deadline;

    if (ring == 0 || deadline_ns < ASTRA_DEADLINE_POLL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    deadline = (uint64_t)deadline_ns;
    for (;;) {
        AstraResult result = refresh(ring);
        uint32_t ignored1;
        uint32_t ignored2;

        if (result != ASTRA_OK)
            return result;
        if (endpoint_ready(ring))
            return ASTRA_OK;
        if (deadline_ns == ASTRA_DEADLINE_POLL)
            return ASTRA_ERROR_WOULD_BLOCK;
        result = astra_internal_result(astra_internal_syscall(
            ASTRA_SYSCALL_WAIT_ONE, ring->endpoint,
            (uint32_t)(deadline >> 32), (uint32_t)deadline, 0, 0,
            &ignored1, &ignored2));
        if (result != ASTRA_OK)
            return result;
    }
}

AstraResult astra_bulk_ring_write_reserve(AstraBulkRing *ring,
                                           void **element)
{
    uint32_t slot;

    if (ring == 0 || element == 0 || ring->role != ASTRA_BULK_RING_PRODUCER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *element = 0;
    if (ring->failed != 0u)
        return ASTRA_ERROR_IO;
    if (ring->reservation_active != 0u)
        return ASTRA_ERROR_BUSY;
    if (!header_valid(ring))
        return report_corruption(ring);
    if (!endpoint_ready(ring))
        return ASTRA_ERROR_WOULD_BLOCK;
    slot = ring->local_position & (ring->capacity - 1u);
    ring->reservation_position = ring->local_position;
    ring->reservation_active = 1u;
    *element = (void *)(uintptr_t)(ring->data + slot * ring->element_size);
    return ASTRA_OK;
}

AstraResult astra_bulk_ring_write_commit(AstraBulkRing *ring)
{
    uint32_t next;

    if (ring == 0 || ring->role != ASTRA_BULK_RING_PRODUCER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (ring->failed != 0u)
        return ASTRA_ERROR_IO;
    if (ring->reservation_active == 0u ||
        ring->reservation_position != ring->local_position)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!header_valid(ring))
        return report_corruption(ring);
    next = ring->local_position + 1u;
    release_fence();
    ring->header->producer_position = next;
    ring->local_position = next;
    ring->reservation_position = 0u;
    ring->reservation_active = 0u;
    return ASTRA_OK;
}

AstraResult astra_bulk_ring_read_reserve(AstraBulkRing *ring,
                                          const void **element)
{
    uint32_t slot;

    if (ring == 0 || element == 0 || ring->role != ASTRA_BULK_RING_CONSUMER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *element = 0;
    if (ring->failed != 0u)
        return ASTRA_ERROR_IO;
    if (ring->reservation_active != 0u)
        return ASTRA_ERROR_BUSY;
    if (!header_valid(ring))
        return report_corruption(ring);
    if (!endpoint_ready(ring))
        return ASTRA_ERROR_WOULD_BLOCK;
    slot = ring->local_position & (ring->capacity - 1u);
    acquire_fence();
    ring->reservation_position = ring->local_position;
    ring->reservation_active = 1u;
    *element = (const void *)(uintptr_t)
        (ring->data + slot * ring->element_size);
    return ASTRA_OK;
}

AstraResult astra_bulk_ring_read_commit(AstraBulkRing *ring)
{
    uint32_t next;

    if (ring == 0 || ring->role != ASTRA_BULK_RING_CONSUMER)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (ring->failed != 0u)
        return ASTRA_ERROR_IO;
    if (ring->reservation_active == 0u ||
        ring->reservation_position != ring->local_position)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!header_valid(ring))
        return report_corruption(ring);
    next = ring->local_position + 1u;
    release_fence();
    ring->header->consumer_position = next;
    ring->local_position = next;
    ring->reservation_position = 0u;
    ring->reservation_active = 0u;
    return ASTRA_OK;
}

AstraResult astra_bulk_ring_close(AstraBulkRing *ring)
{
    AstraResult result;

    if (ring == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (ring->endpoint == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_HANDLE;
    result = astra_handle_close(&ring->endpoint);
    if (result == ASTRA_OK)
        clear_ring(ring);
    return result;
}

void astra_bulk_ring_cleanup(AstraBulkRing *ring)
{
    AstraResult ignored = astra_bulk_ring_close(ring);

    (void)ignored;
}
