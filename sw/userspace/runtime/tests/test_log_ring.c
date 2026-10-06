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
    AstraLogRecordFields *stored;

    format_ring();
    astra_log_producer_init(&producer, area, record_notify, NULL);
    assert(astra_log_producer_write(&producer, &value, "ok", 2u) ==
           ASTRA_LOG_WRITE_OK);
    /* Self-consistent claim: text_length=209 and slots=2 match each other
     * (astra_log_record_slots(209) == 2) and both pass every other bound
     * (slots <= ASTRA_LOG_RECORD_SLOTS_MAX, text_length <= ASTRA_LOG_TEXT_MAX),
     * so only "fields->slots > produced - at" -- 2 claimed vs. 1 actually
     * published -- can catch this. This isolates that check: a 9-slot claim
     * (as before) is also independently caught by the SLOTS_MAX bound and
     * proves nothing about this line. */
    stored = (AstraLogRecordFields *)(void *)
        (area + ASTRA_LOG_RING_HEADER_SIZE + 4u);
    stored->text_length = 209u;
    stored->slots = 2u;
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
/* Every 7th record per thread is a 600-byte, 3-slot record instead of the
 * usual 9-byte, 1-slot one: exercises multi-slot commit ordering under
 * contention, not just the single-slot fast path. */
#define CONCURRENCY_LONG_EVERY   7u
#define CONCURRENCY_LONG_LENGTH  600u

/* Deterministic content for a long record, reproducible from (thread, index)
 * alone so the consumer can verify every byte without extra bookkeeping. */
static void
fill_long_pattern(char *text, uint32_t thread_id, uint32_t index,
                  uint32_t total_length)
{
    int prefix_length = snprintf(text, total_length, "%u:%07u:", thread_id,
                                 index);

    for (uint32_t k = (uint32_t)prefix_length; k < total_length; ++k)
        text[k] = (char)('a' + (thread_id + index + k) % 26u);
}

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
        char text[CONCURRENCY_LONG_LENGTH];
        uint32_t length;
        uint32_t status;

        if (index % CONCURRENCY_LONG_EVERY == 0u) {
            fill_long_pattern(text, args->thread_id, index,
                              CONCURRENCY_LONG_LENGTH);
            length = CONCURRENCY_LONG_LENGTH;
        } else {
            length = (uint32_t)snprintf(text, sizeof(text), "%u:%07u",
                                        args->thread_id, index);
        }
        status = astra_log_producer_write(args->producer, &value, text,
                                          length);

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
        char text[CONCURRENCY_LONG_LENGTH];
        char expected[CONCURRENCY_LONG_LENGTH];
        uint32_t length = 0u;
        uint32_t status = astra_log_consumer_next(area, &position, &read,
                                                  text, sizeof(text),
                                                  &length);

        if (status == ASTRA_LOG_READ_OK) {
            ConcurrencyRecord *slot;
            unsigned thread_id = 0u, seq = 0u;
            char head[16];
            uint32_t head_length =
                length < sizeof(head) - 1u ? length : sizeof(head) - 1u;

            /* Parse from a bounded, NUL-terminated copy of the prefix, not
             * from `text` itself: `text` must stay untouched so the memcmp
             * below checks every one of its bytes, and without a bound a
             * short record's "%u" could run past its 9 bytes into another
             * record's leftover bytes from a previous iteration. */
            memcpy(head, text, head_length);
            head[head_length] = '\0';
            sscanf(head, "%u:%u", &thread_id, &seq);
            if (length == 9u) {
                int expected_length = snprintf(expected, sizeof(expected),
                                               "%u:%07u", thread_id, seq);
                assert((uint32_t)expected_length == length);
            } else {
                assert(length == CONCURRENCY_LONG_LENGTH);
                fill_long_pattern(expected, thread_id, seq,
                                  CONCURRENCY_LONG_LENGTH);
            }
            assert(memcmp(text, expected, length) == 0);
            assert(concurrency_result_count < CONCURRENCY_TOTAL);
            slot = &concurrency_results[concurrency_result_count++];
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
    AstraLogProducer producer;
    pthread_t producers[CONCURRENCY_THREADS];
    pthread_t consumer;
    ConcurrencyProducerArgs args[CONCURRENCY_THREADS];
    int64_t last_seen[CONCURRENCY_THREADS];
    uint32_t delivered_count[CONCURRENCY_THREADS];
    int64_t last_sequence = -1;
    uint32_t total_written = 0u;
    uint32_t total_dropped = 0u;
    uint32_t thread_index;
    uint32_t result_index;

    format_ring();
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
        /* `sequence` is the record's ring position (the reserve CAS's
         * winning `first`), so it strictly increases across every record in
         * delivery order regardless of which thread produced it -- the
         * consumer only ever advances forward through the ring. This proves
         * both uniqueness (no two records share a ring position) and that
         * sequence tracks real publish order; it would not reliably hold if
         * sequence were still an independent counter CAS'd apart from the
         * reserve, since that counter's own races need not land in ring
         * order. Density (0..total_written-1) no longer holds now that
         * multi-slot records skip ring positions, so that is not asserted. */
        assert((int64_t)record->sequence > last_sequence);
        last_sequence = (int64_t)record->sequence;
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
