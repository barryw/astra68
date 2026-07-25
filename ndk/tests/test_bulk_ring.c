#include <astra/bulk_ring.h>

#include "syscall.h"
#include "syscall_script.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_STORAGE_SIZE = 4096,
    TEST_ELEMENT_SIZE = 16,
    TEST_CAPACITY = 4
};

static _Alignas(64) uint8_t storage[TEST_STORAGE_SIZE];

static AstraArea mapped_area(void)
{
    AstraArea area = {
        0x100u, storage, sizeof(storage),
        ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE
    };

    return area;
}

static AstraBulkRingHeader *format_ring(uint32_t offset,
                                        uint32_t element_size,
                                        uint32_t capacity,
                                        uint32_t generation,
                                        uint32_t producer,
                                        uint32_t consumer)
{
    AstraBulkRingHeader *header;

    assert(offset <= sizeof(storage) - sizeof(*header));
    header = (AstraBulkRingHeader *)(void *)&storage[offset];
    memset(header, 0, sizeof(*header));
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->version = ASTRA_BULK_RING_ABI_VERSION;
    header->header_size = ASTRA_BULK_RING_HEADER_SIZE;
    header->element_size = element_size;
    header->capacity = capacity;
    header->data_offset = ASTRA_BULK_RING_HEADER_SIZE;
    header->total_size = ASTRA_BULK_RING_HEADER_SIZE +
                         element_size * capacity;
    header->generation = generation;
    header->producer_position = producer;
    header->consumer_position = consumer;
    return header;
}

static void expect_notify(AstraHandle endpoint, uint32_t position,
                          uint32_t flags, uint32_t role, uint32_t status,
                          uint32_t producer, uint32_t consumer)
{
    astra_ndk_expect_syscall(ASTRA_SYSCALL_RING_NOTIFY, endpoint, position,
                             flags, role, 0, status, producer, consumer);
}

static void test_header_abi_and_endpoint_creation(void)
{
    AstraBulkRingEndpoints endpoints = ASTRA_BULK_RING_ENDPOINTS_INIT;

    _Static_assert(sizeof(AstraBulkRingHeader) == 64u,
                   "bulk-ring ABI size");
    _Static_assert(_Alignof(AstraBulkRingHeader) == 4u,
                   "bulk-ring ABI alignment");
    _Static_assert(offsetof(AstraBulkRingHeader, producer_position) == 0x20u,
                   "producer position offset");
    _Static_assert(offsetof(AstraBulkRingHeader, consumer_position) == 0x30u,
                   "consumer position offset");

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_RING_CREATE, 0x100u, 0u,
                             TEST_ELEMENT_SIZE, TEST_CAPACITY, 0,
                             ASTRA_SYSCALL_OK, 0x201u, 0x202u);
    assert(astra_bulk_ring_create(0x100u, 0u, TEST_ELEMENT_SIZE,
                                  TEST_CAPACITY, &endpoints) == ASTRA_OK);
    assert(endpoints.producer == 0x201u && endpoints.consumer == 0x202u);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x201u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x202u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_bulk_ring_endpoints_close(&endpoints) == ASTRA_OK);
    assert(endpoints.producer == ASTRA_INVALID_HANDLE);
    assert(endpoints.consumer == ASTRA_INVALID_HANDLE);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    assert(astra_bulk_ring_create(0u, 0u, TEST_ELEMENT_SIZE,
                                  TEST_CAPACITY, &endpoints) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_bulk_ring_create(0x100u, 1u, TEST_ELEMENT_SIZE,
                                  TEST_CAPACITY, &endpoints) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_bulk_ring_create(0x100u, 0u, 6u,
                                  TEST_CAPACITY, &endpoints) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_bulk_ring_create(0x100u, 0u, TEST_ELEMENT_SIZE,
                                  3u, &endpoints) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_ndk_syscall_script_done();
}

static void test_batched_publication_and_backpressure(void)
{
    AstraArea area = mapped_area();
    AstraBulkRingHeader *header = format_ring(
        0u, TEST_ELEMENT_SIZE, TEST_CAPACITY, 7u, 0u, 0u);
    AstraBulkRing producer = ASTRA_BULK_RING_INIT;
    AstraBulkRing consumer = ASTRA_BULK_RING_INIT;
    AstraHandle producer_handle = 0x301u;
    AstraHandle consumer_handle = 0x302u;

    astra_ndk_syscall_script_reset();
    expect_notify(producer_handle, 0u, 0u, ASTRA_BULK_RING_PRODUCER,
                  ASTRA_SYSCALL_OK, 0u, 0u);
    assert(astra_bulk_ring_attach(&producer, &producer_handle, &area, 0u,
                                  ASTRA_BULK_RING_PRODUCER) == ASTRA_OK);
    expect_notify(consumer_handle, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                  ASTRA_SYSCALL_OK, 0u, 0u);
    assert(astra_bulk_ring_attach(&consumer, &consumer_handle, &area, 0u,
                                  ASTRA_BULK_RING_CONSUMER) == ASTRA_OK);
    assert(producer_handle == ASTRA_INVALID_HANDLE);
    assert(consumer_handle == ASTRA_INVALID_HANDLE);
    astra_ndk_syscall_script_done();

    for (uint32_t value = 1u; value <= 3u; ++value) {
        void *slot;
        uint32_t *element;

        assert(astra_bulk_ring_write_reserve(&producer, &slot) == ASTRA_OK);
        element = slot;
        element[0] = value;
        element[1] = value ^ UINT32_C(0xffffffff);
        assert(astra_bulk_ring_write_commit(&producer) == ASTRA_OK);
    }
    assert(header->producer_position == 3u);
    {
        const void *element;
        assert(astra_bulk_ring_read_reserve(&consumer, &element) ==
               ASTRA_ERROR_WOULD_BLOCK);
    }

    astra_ndk_syscall_script_reset();
    expect_notify(0x301u, 3u, 0u, ASTRA_BULK_RING_PRODUCER,
                  ASTRA_SYSCALL_OK, 3u, 0u);
    assert(astra_bulk_ring_notify(&producer) == ASTRA_OK);
    expect_notify(0x302u, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                  ASTRA_SYSCALL_OK, 3u, 0u);
    assert(astra_bulk_ring_notify(&consumer) == ASTRA_OK);
    astra_ndk_syscall_script_done();

    for (uint32_t value = 1u; value <= 3u; ++value) {
        const void *slot;
        const uint32_t *element;

        assert(astra_bulk_ring_read_reserve(&consumer, &slot) == ASTRA_OK);
        element = slot;
        assert(element[0] == value);
        assert(element[1] == (value ^ UINT32_C(0xffffffff)));
        assert(astra_bulk_ring_read_commit(&consumer) == ASTRA_OK);
    }
    assert(header->consumer_position == 3u);

    /* Until the consumer rings its doorbell, the producer stays bounded by
       the last canonical consumer position. */
    {
        void *element;
        assert(astra_bulk_ring_write_reserve(&producer, &element) ==
               ASTRA_OK);
        assert(astra_bulk_ring_write_commit(&producer) == ASTRA_OK);
        assert(astra_bulk_ring_write_reserve(&producer, &element) ==
               ASTRA_ERROR_WOULD_BLOCK);
    }

    astra_ndk_syscall_script_reset();
    expect_notify(0x302u, 3u, 0u, ASTRA_BULK_RING_CONSUMER,
                  ASTRA_SYSCALL_OK, 3u, 3u);
    assert(astra_bulk_ring_notify(&consumer) == ASTRA_OK);
    expect_notify(0x301u, 4u, 0u, ASTRA_BULK_RING_PRODUCER,
                  ASTRA_SYSCALL_OK, 4u, 3u);
    assert(astra_bulk_ring_notify(&producer) == ASTRA_OK);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x301u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_bulk_ring_close(&producer) == ASTRA_OK);
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x302u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_bulk_ring_close(&consumer) == ASTRA_OK);
    astra_ndk_syscall_script_done();
}

static void test_wait_wrap_role_and_corruption(void)
{
    AstraArea area = mapped_area();
    AstraBulkRingHeader *header = format_ring(
        0u, TEST_ELEMENT_SIZE, TEST_CAPACITY, 9u,
        UINT32_MAX - 1u, UINT32_MAX - 1u);
    AstraBulkRing producer = ASTRA_BULK_RING_INIT;
    AstraHandle handle = 0x401u;
    const uint32_t initial = UINT32_MAX - 1u;

    astra_ndk_syscall_script_reset();
    expect_notify(handle, initial, 0u, ASTRA_BULK_RING_PRODUCER,
                  ASTRA_SYSCALL_OK, initial, initial);
    assert(astra_bulk_ring_attach(&producer, &handle, &area, 0u,
                                  ASTRA_BULK_RING_PRODUCER) == ASTRA_OK);
    astra_ndk_syscall_script_done();
    for (uint32_t index = 0u; index < 3u; ++index) {
        void *slot;
        uint32_t *element;

        assert(astra_bulk_ring_write_reserve(&producer, &slot) == ASTRA_OK);
        element = slot;
        element[0] = index;
        assert(astra_bulk_ring_write_commit(&producer) == ASTRA_OK);
    }
    assert(header->producer_position == 1u);
    astra_ndk_syscall_script_reset();
    expect_notify(0x401u, 1u, 0u, ASTRA_BULK_RING_PRODUCER,
                  ASTRA_SYSCALL_OK, 1u, initial);
    assert(astra_bulk_ring_notify(&producer) == ASTRA_OK);
    astra_ndk_syscall_script_done();

    header->magic ^= 1u;
    astra_ndk_syscall_script_reset();
    expect_notify(0x401u, 1u, ASTRA_BULK_RING_NOTIFY_CORRUPT,
                  ASTRA_BULK_RING_PRODUCER, ASTRA_SYSCALL_IO_ERROR, 0, 0);
    {
        void *element;
        assert(astra_bulk_ring_write_reserve(&producer, &element) ==
               ASTRA_ERROR_IO);
        assert(astra_bulk_ring_write_reserve(&producer, &element) ==
               ASTRA_ERROR_IO);
    }
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x401u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_bulk_ring_close(&producer) == ASTRA_OK);
    astra_ndk_syscall_script_done();

    header = format_ring(0u, TEST_ELEMENT_SIZE, TEST_CAPACITY, 10u, 1u, 0u);
    {
        AstraBulkRing consumer = ASTRA_BULK_RING_INIT;
        AstraHandle consumer_handle = 0x402u;
        const uint64_t deadline = UINT64_C(0x0000000123456789);

        astra_ndk_syscall_script_reset();
        expect_notify(consumer_handle, 0u, 0u,
                      ASTRA_BULK_RING_CONSUMER,
                      ASTRA_SYSCALL_OK, 0u, 0u);
        assert(astra_bulk_ring_attach(&consumer, &consumer_handle, &area,
                                      0u, ASTRA_BULK_RING_CONSUMER) ==
               ASTRA_OK);
        expect_notify(0x402u, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                      ASTRA_SYSCALL_OK, 0u, 0u);
        astra_ndk_expect_syscall(ASTRA_SYSCALL_WAIT_ONE, 0x402u,
                                 1u, 0x23456789u, 0, 0,
                                 ASTRA_SYSCALL_OK, 0, 0);
        expect_notify(0x402u, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                      ASTRA_SYSCALL_OK, 1u, 0u);
        assert(astra_bulk_ring_wait_until(
                   &consumer, (AstraMonotonicDeadline)deadline) == ASTRA_OK);
        astra_ndk_syscall_script_done();

        astra_ndk_syscall_script_reset();
        expect_notify(0x402u, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                      ASTRA_SYSCALL_PEER_DEAD, 0, 0);
        assert(astra_bulk_ring_notify(&consumer) ==
               ASTRA_ERROR_PEER_DEAD);
        astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x402u,
                                 0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
        assert(astra_bulk_ring_close(&consumer) == ASTRA_OK);
        astra_ndk_syscall_script_done();
    }

    header = format_ring(0u, TEST_ELEMENT_SIZE, TEST_CAPACITY, 11u, 0u, 0u);
    (void)header;
    {
        AstraBulkRing wrong = ASTRA_BULK_RING_INIT;
        AstraHandle wrong_handle = 0x403u;

        astra_ndk_syscall_script_reset();
        expect_notify(wrong_handle, 0u, 0u, ASTRA_BULK_RING_CONSUMER,
                      ASTRA_SYSCALL_INVALID_ARGUMENT, 0, 0);
        assert(astra_bulk_ring_attach(&wrong, &wrong_handle, &area, 0u,
                                      ASTRA_BULK_RING_CONSUMER) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        assert(wrong_handle == 0x403u);
        assert(wrong.endpoint == ASTRA_INVALID_HANDLE);
        astra_ndk_syscall_script_done();
    }
}

int main(void)
{
    test_header_abi_and_endpoint_creation();
    test_batched_publication_and_backpressure();
    test_wait_wrap_role_and_corruption();
    puts("NDK bulk-ring tests passed");
    return 0;
}
