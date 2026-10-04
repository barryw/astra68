#include <astra/ndk.h>

enum {
    EXAMPLE_RING_ELEMENT_SIZE = 16u,
    EXAMPLE_RING_CAPACITY = 64u,
    EXAMPLE_RING_AREA_SIZE = 4096u
};

typedef struct ExampleBulkRecord {
    uint32_t sequence;
    uint32_t operation;
    uint32_t value;
    uint32_t reserved;
} ExampleBulkRecord;

typedef struct ExampleBulkChannel {
    AstraArea area;
    AstraBulkRing producer;
    AstraBulkRing consumer;
} ExampleBulkChannel;

static void example_bulk_channel_reset(ExampleBulkChannel *channel)
{
    AstraArea empty_area = ASTRA_AREA_INIT;
    AstraBulkRing empty_ring = ASTRA_BULK_RING_INIT;

    channel->area = empty_area;
    channel->producer = empty_ring;
    channel->consumer = empty_ring;
}

AstraResult example_bulk_channel_create(ExampleBulkChannel *channel)
{
    AstraBulkRingEndpoints endpoints = ASTRA_BULK_RING_ENDPOINTS_INIT;
    AstraResult result;
    const uint32_t rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;

    if (channel == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    example_bulk_channel_reset(channel);
    result = astra_area_create(EXAMPLE_RING_AREA_SIZE, rights,
                               &channel->area);
    if (result != ASTRA_OK)
        return result;
    result = astra_area_map(&channel->area,
                            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result != ASTRA_OK)
        goto fail;
    result = astra_bulk_ring_create(
        channel->area.handle, 0u, EXAMPLE_RING_ELEMENT_SIZE,
        EXAMPLE_RING_CAPACITY, &endpoints);
    if (result != ASTRA_OK)
        goto fail;
    result = astra_bulk_ring_attach(
        &channel->producer, &endpoints.producer, &channel->area, 0u,
        ASTRA_BULK_RING_PRODUCER);
    if (result != ASTRA_OK)
        goto fail;
    result = astra_bulk_ring_attach(
        &channel->consumer, &endpoints.consumer, &channel->area, 0u,
        ASTRA_BULK_RING_CONSUMER);
    if (result != ASTRA_OK)
        goto fail;
    return ASTRA_OK;

fail:
    if (endpoints.producer != ASTRA_INVALID_HANDLE ||
        endpoints.consumer != ASTRA_INVALID_HANDLE) {
        AstraResult cleanup = astra_bulk_ring_endpoints_close(&endpoints);

        (void)cleanup;
    }
    if (channel->producer.endpoint != ASTRA_INVALID_HANDLE) {
        AstraResult cleanup = astra_bulk_ring_close(&channel->producer);

        (void)cleanup;
    }
    if (channel->consumer.endpoint != ASTRA_INVALID_HANDLE) {
        AstraResult cleanup = astra_bulk_ring_close(&channel->consumer);

        (void)cleanup;
    }
    {
        AstraResult cleanup = astra_area_close(&channel->area);

        (void)cleanup;
    }
    return result;
}

AstraResult example_bulk_channel_send(ExampleBulkChannel *channel,
                                      uint32_t first_sequence,
                                      uint32_t count)
{
    if (channel == 0 || count > EXAMPLE_RING_CAPACITY)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < count; ++index) {
        void *slot;
        ExampleBulkRecord *record;
        AstraResult result = astra_bulk_ring_write_reserve(
            &channel->producer, &slot);

        if (result != ASTRA_OK)
            return result;
        record = slot;
        record->sequence = first_sequence + index;
        record->operation = 1u;
        record->value = index;
        record->reserved = 0u;
        result = astra_bulk_ring_write_commit(&channel->producer);
        if (result != ASTRA_OK)
            return result;
    }
    return astra_bulk_ring_notify(&channel->producer);
}

AstraResult example_bulk_channel_receive(ExampleBulkChannel *channel,
                                         ExampleBulkRecord *record,
                                         AstraMonotonicDeadline deadline)
{
    const void *slot;
    const ExampleBulkRecord *shared_record;
    AstraResult result;

    if (channel == 0 || record == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_bulk_ring_wait_until(&channel->consumer, deadline);
    if (result != ASTRA_OK)
        return result;
    result = astra_bulk_ring_read_reserve(&channel->consumer, &slot);
    if (result != ASTRA_OK)
        return result;
    shared_record = slot;
    *record = *shared_record;
    result = astra_bulk_ring_read_commit(&channel->consumer);
    if (result != ASTRA_OK)
        return result;
    return astra_bulk_ring_notify(&channel->consumer);
}

AstraResult example_bulk_channel_close(ExampleBulkChannel *channel)
{
    AstraResult first = ASTRA_OK;
    AstraResult result;

    if (channel == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (channel->producer.endpoint != ASTRA_INVALID_HANDLE) {
        result = astra_bulk_ring_close(&channel->producer);
        if (result != ASTRA_OK)
            first = result;
    }
    if (channel->consumer.endpoint != ASTRA_INVALID_HANDLE) {
        result = astra_bulk_ring_close(&channel->consumer);
        if (result != ASTRA_OK && first == ASTRA_OK)
            first = result;
    }
    if (channel->area.handle != ASTRA_INVALID_HANDLE ||
        channel->area.address != 0) {
        result = astra_area_close(&channel->area);
        if (result != ASTRA_OK && first == ASTRA_OK)
            first = result;
    }
    return first;
}

ASTRA_PROGRAM("bulk_ring", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

/* Fill the ring from its producer end and drain it from its consumer end. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    ExampleBulkChannel channel;
    AstraMonotonicDeadline deadline;
    AstraResult result;
    uint32_t received = 0u;

    if (output == 0 || example_bulk_channel_create(&channel) != ASTRA_OK)
        return 1;
    deadline = (AstraMonotonicDeadline)(astra_clock_monotonic() +
                                        UINT64_C(1000000000));
    result = example_bulk_channel_send(&channel, 3u, EXAMPLE_RING_CAPACITY);
    while (result == ASTRA_OK && received < EXAMPLE_RING_CAPACITY) {
        ExampleBulkRecord record;

        result = example_bulk_channel_receive(&channel, &record, deadline);
        if (result == ASTRA_OK && (record.sequence != 3u + received ||
                                   record.value != received))
            result = ASTRA_ERROR_INVALID_ARGUMENT;
        if (result == ASTRA_OK)
            ++received;
    }
    if (example_bulk_channel_close(&channel) != ASTRA_OK || result != ASTRA_OK)
        return 2;
    (void)astra_print(output->handle, "bulk_ring: 64 records in order, "
                                      "3 to 66\n");
    return 0;
}
