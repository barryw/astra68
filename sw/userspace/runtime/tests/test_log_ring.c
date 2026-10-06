#include <astra/log_ring.h>

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static uint8_t area[ASTRA_LOG_RING_AREA_SIZE];
static uint32_t notifications[16];
static uint32_t notification_count;

static uint32_t
record_notify(void *context)
{
    (void)context;
    notifications[notification_count % 16u] =
        ((AstraBulkRingHeader *)(void *)area)->producer_position;
    ++notification_count;
    return 0u; /* the service is alive */
}

static void
format_ring(void)
{
    volatile AstraBulkRingHeader *header = (void *)area;

    memset(area, 0, sizeof(area));
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->element_size = ASTRA_LOG_RING_SLOT_SIZE;
    header->capacity = ASTRA_LOG_RING_CAPACITY;
    header->data_offset = ASTRA_LOG_RING_HEADER_SIZE;
    notification_count = 0u;
}

static AstraLogRecordFields
fields(uint8_t level, uint8_t stream)
{
    AstraLogRecordFields value;

    memset(&value, 0, sizeof(value));
    value.level = level;
    value.stream = stream;
    value.monotonic_low = 42u;
    return value;
}

static void
drain_expect(uint32_t *position, const char *expected, uint32_t dropped)
{
    AstraLogRecordFields read;
    char text[ASTRA_LOG_TEXT_MAX];
    uint32_t length = 0u;

    assert(astra_log_consumer_next(area, position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_OK);
    assert(length == strlen(expected));
    assert(memcmp(text, expected, length) == 0);
    assert(read.dropped == dropped);
    assert(read.monotonic_low == 42u);
}

static void
test_slots(void)
{
    assert(astra_log_record_slots(0u) == 1u);
    assert(astra_log_record_slots(208u) == 1u);
    assert(astra_log_record_slots(209u) == 2u);
    assert(astra_log_record_slots(208u + 248u) == 2u);
    assert(astra_log_record_slots(ASTRA_LOG_TEXT_MAX) == 8u);
    assert(astra_log_record_slots(ASTRA_LOG_TEXT_MAX + 1u) == 8u);
}

static void
test_round_trip_and_notify_once_per_burst(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_STDOUT);
    uint32_t position = 0u;
    volatile AstraBulkRingHeader *header = (void *)area;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    assert(astra_log_producer_write(&producer, &value, "first", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(header->producer_position == 1u);
    assert(notification_count == 1u && notifications[0] == 1u);
    /* Consumer has not moved: the second line in the burst does not notify. */
    assert(astra_log_producer_write(&producer, &value, "second", 6u) ==
           ASTRA_LOG_WRITE_OK);
    assert(notification_count == 1u);
    drain_expect(&position, "first", 0u);
    drain_expect(&position, "second", 0u);
    header->consumer_position = position;
    /* The consumer caught up to what the kernel was told: notify again. */
    assert(astra_log_producer_write(&producer, &value, "third", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(notification_count == 2u);
}

static void
test_long_record_wraps_the_ring(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(4u, ASTRA_LOG_STREAM_STDERR);
    char text[1000];
    uint32_t position = 0u;
    volatile AstraBulkRingHeader *header = (void *)area;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    /* Fill to two slots before the end, so a 5-slot record wraps. */
    for (uint32_t index = 0u; index < ASTRA_LOG_RING_CAPACITY - 2u; ++index) {
        assert(astra_log_producer_write(&producer, &value, "x", 1u) ==
               ASTRA_LOG_WRITE_OK);
        drain_expect(&position, "x", 0u);
        header->consumer_position = position;
    }
    for (uint32_t index = 0u; index < sizeof(text); ++index)
        text[index] = (char)('a' + index % 26u);
    assert(astra_log_producer_write(&producer, &value, text, sizeof(text)) ==
           ASTRA_LOG_WRITE_OK);
    {
        AstraLogRecordFields read;
        char out[ASTRA_LOG_TEXT_MAX];
        uint32_t length = 0u;

        assert(astra_log_consumer_next(area, &position, &read, out,
                                       sizeof(out), &length) ==
               ASTRA_LOG_READ_OK);
        assert(length == sizeof(text) && memcmp(out, text, length) == 0);
        assert(read.slots == astra_log_record_slots(sizeof(text)));
    }
}

static void
test_full_ring_drops_and_counts(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    uint32_t position = 0u;
    uint32_t written = 0u;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    while (astra_log_producer_write(&producer, &value, "line", 4u) ==
           ASTRA_LOG_WRITE_OK)
        ++written;
    assert(written == ASTRA_LOG_RING_CAPACITY);
    assert(astra_log_producer_write(&producer, &value, "line", 4u) ==
           ASTRA_LOG_WRITE_DROPPED);
    assert(producer.dropped == 2u);
    for (uint32_t index = 0u; index < written; ++index)
        drain_expect(&position, "line", 0u);
    ((volatile AstraBulkRingHeader *)area)->consumer_position = position;
    assert(astra_log_producer_write(&producer, &value, "after", 5u) ==
           ASTRA_LOG_WRITE_OK);
    drain_expect(&position, "after", 2u);
}

static void
test_uncommitted_slot_holds_back_publication(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    volatile AstraBulkRingHeader *header = (void *)area;
    uint32_t position = 0u;
    AstraLogRecordFields read;
    char text[64];
    uint32_t length;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    /* Simulate a writer preempted after reserving slot 0. */
    producer.reserved = 1u;
    assert(astra_log_producer_write(&producer, &value, "later", 5u) ==
           ASTRA_LOG_WRITE_OK);
    assert(header->producer_position == 0u); /* slot 0 not committed */
    assert(astra_log_consumer_next(area, &position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_EMPTY);
}

static void
test_corrupt_slot_count_is_rejected(void)
{
    AstraLogProducer producer;
    AstraLogRecordFields value = fields(2u, ASTRA_LOG_STREAM_LOG);
    uint32_t position = 0u;
    AstraLogRecordFields read;
    char text[64];
    uint32_t length;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    assert(astra_log_producer_write(&producer, &value, "ok", 2u) ==
           ASTRA_LOG_WRITE_OK);
    /* Claim 9 slots in the first slot's header: more than published. */
    area[ASTRA_LOG_RING_HEADER_SIZE + 4u +
         __builtin_offsetof(AstraLogRecordFields, slots)] = 9u;
    assert(astra_log_consumer_next(area, &position, &read, text, sizeof(text),
                                   &length) == ASTRA_LOG_READ_CORRUPT);
}

/*
 * The real proof of the lock-free claim: several threads writing the SAME
 * AstraLogProducer concurrently, coordinated only by CAS, with a consumer
 * draining concurrently. Every record must arrive exactly once, intact, with
 * each thread's own records in its own order; the process-wide 'dropped' and
 * 'sequence' counters must stay internally consistent under the race.
 */
#define CONCURRENCY_THREADS      4u
#define CONCURRENCY_PER_THREAD   10000u
#define CONCURRENCY_TOTAL        (CONCURRENCY_THREADS * CONCURRENCY_PER_THREAD)

typedef struct {
    uint32_t thread_id;
    uint32_t seq;
    uint32_t dropped;
    uint32_t sequence;
} ConcurrencyRecord;

static ConcurrencyRecord concurrency_results[CONCURRENCY_TOTAL];
static uint32_t concurrency_result_count;
static int concurrency_producers_done;
static int concurrency_corrupt;

typedef struct {
    AstraLogProducer *producer;
    uint32_t thread_id;
    uint32_t written;
    uint32_t dropped;
} ConcurrencyProducerArgs;

static void *
concurrency_producer_main(void *argument)
{
    ConcurrencyProducerArgs *args = argument;
    AstraLogRecordFields value = fields(1u, ASTRA_LOG_STREAM_LOG);
    uint32_t index;

    for (index = 0u; index < CONCURRENCY_PER_THREAD; ++index) {
        char text[32];
        int length = snprintf(text, sizeof(text), "%u:%07u",
                              args->thread_id, index);
        uint32_t status = astra_log_producer_write(args->producer, &value,
                                                   text, (uint32_t)length);

        if (status == ASTRA_LOG_WRITE_OK)
            ++args->written;
        else if (status == ASTRA_LOG_WRITE_DROPPED)
            ++args->dropped;
        else
            assert(0 && "unexpected DEAD: no doorbell is armed here");
    }
    return NULL;
}

static void *
concurrency_consumer_main(void *argument)
{
    uint32_t position = 0u;

    (void)argument;
    for (;;) {
        AstraLogRecordFields read;
        char text[64];
        uint32_t length = 0u;
        uint32_t status = astra_log_consumer_next(area, &position, &read,
                                                  text, sizeof(text),
                                                  &length);

        if (status == ASTRA_LOG_READ_OK) {
            ConcurrencyRecord *slot =
                &concurrency_results[concurrency_result_count++];
            unsigned thread_id = 0u, seq = 0u;

            text[length < sizeof(text) ? length : sizeof(text) - 1u] = '\0';
            sscanf(text, "%u:%u", &thread_id, &seq);
            slot->thread_id = thread_id;
            slot->seq = seq;
            slot->dropped = read.dropped;
            slot->sequence = read.sequence;
            /* Atomic release store: a plain write here races, under the
             * formal C11 model, with the producer threads' atomic-acquire
             * reads of this same field (log_ring.c's load32). A real
             * consumer (the events service, a separate process) must
             * publish consumer_position into the shared header the same
             * way -- see the report's concern for Task 6. */
            __atomic_store_n(
                (uint32_t *)(uintptr_t)&((volatile AstraBulkRingHeader *)
                                         area)->consumer_position,
                position, __ATOMIC_RELEASE);
            continue;
        }
        if (status == ASTRA_LOG_READ_CORRUPT) {
            __atomic_store_n(&concurrency_corrupt, 1, __ATOMIC_RELEASE);
            break;
        }
        /* EMPTY: stop only once every producer has returned, so the final
         * burst is never missed. */
        if (__atomic_load_n(&concurrency_producers_done, __ATOMIC_ACQUIRE))
            break;
    }
    return NULL;
}

static void
test_concurrent_producers(void)
{
    static uint8_t sequence_seen[CONCURRENCY_TOTAL];
    AstraLogProducer producer;
    pthread_t producers[CONCURRENCY_THREADS];
    pthread_t consumer;
    ConcurrencyProducerArgs args[CONCURRENCY_THREADS];
    int64_t last_seen[CONCURRENCY_THREADS];
    uint32_t delivered_count[CONCURRENCY_THREADS];
    uint32_t total_written = 0u;
    uint32_t total_dropped = 0u;
    uint32_t thread_index;
    uint32_t result_index;

    format_ring();
    memset(sequence_seen, 0, sizeof(sequence_seen));
    concurrency_result_count = 0u;
    concurrency_producers_done = 0;
    concurrency_corrupt = 0;
    astra_log_producer_init(&producer, area, NULL, NULL);

    assert(pthread_create(&consumer, NULL, concurrency_consumer_main,
                          NULL) == 0);
    for (thread_index = 0u; thread_index < CONCURRENCY_THREADS;
         ++thread_index) {
        args[thread_index].producer = &producer;
        args[thread_index].thread_id = thread_index;
        args[thread_index].written = 0u;
        args[thread_index].dropped = 0u;
        assert(pthread_create(&producers[thread_index], NULL,
                              concurrency_producer_main,
                              &args[thread_index]) == 0);
    }
    for (thread_index = 0u; thread_index < CONCURRENCY_THREADS;
         ++thread_index)
        assert(pthread_join(producers[thread_index], NULL) == 0);
    __atomic_store_n(&concurrency_producers_done, 1, __ATOMIC_RELEASE);
    assert(pthread_join(consumer, NULL) == 0);

    assert(!concurrency_corrupt);

    for (thread_index = 0u; thread_index < CONCURRENCY_THREADS;
         ++thread_index) {
        total_written += args[thread_index].written;
        total_dropped += args[thread_index].dropped;
        last_seen[thread_index] = -1;
        delivered_count[thread_index] = 0u;
    }
    assert(total_written + total_dropped == CONCURRENCY_TOTAL);
    assert(producer.dropped == total_dropped);
    assert(concurrency_result_count == total_written);

    for (result_index = 0u; result_index < concurrency_result_count;
         ++result_index) {
        ConcurrencyRecord *record = &concurrency_results[result_index];
        uint32_t thread = record->thread_id;

        /* Per-thread order preserved: a thread's own records arrive
         * strictly increasing by its loop index. Gaps are expected and
         * harmless -- this same thread can have dropped the lines in
         * between under contention, which only skips their index. */
        assert(thread < CONCURRENCY_THREADS);
        assert((int64_t)record->seq > last_seen[thread]);
        last_seen[thread] = (int64_t)record->seq;
        ++delivered_count[thread];
        /* The process-wide sequence counter hands out each value once:
         * exactly-once delivery, proven independently of the text payload. */
        assert(record->sequence < total_written);
        assert(sequence_seen[record->sequence] == 0u);
        sequence_seen[record->sequence] = 1u;
        /* The dropped count a record carries can never exceed the final
         * total: it is read once, at or before the end of the run. */
        assert(record->dropped <= total_dropped);
    }
    for (thread_index = 0u; thread_index < CONCURRENCY_THREADS;
         ++thread_index)
        assert(delivered_count[thread_index] == args[thread_index].written);

    printf("LOG RING CONCURRENCY: %u threads x %u, %u written, %u dropped\n",
          CONCURRENCY_THREADS, CONCURRENCY_PER_THREAD, total_written,
          total_dropped);
}

int
main(void)
{
    test_slots();
    test_round_trip_and_notify_once_per_burst();
    test_long_record_wraps_the_ring();
    test_full_ring_drops_and_counts();
    test_uncommitted_slot_holds_back_publication();
    test_corrupt_slot_count_is_rejected();
    test_concurrent_producers();
    puts("LOG RING PASS");
    return 0;
}
