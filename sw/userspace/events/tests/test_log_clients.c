#include <astra/log_clients.h>
#include <astra/log_ring.h>
#include <astra/event.h>
#include <astra/message_abi.h>

#include <astra/log_format.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(64) uint8_t ring_a[ASTRA_LOG_RING_AREA_SIZE];
static _Alignas(64) uint8_t ring_b[ASTRA_LOG_RING_AREA_SIZE];
static AstraLogClients clients;
static uint32_t consumed_calls;

static void
format(uint8_t *area)
{
    AstraBulkRingHeader *header = (void *)area;

    memset(area, 0, ASTRA_LOG_RING_AREA_SIZE);
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->element_size = ASTRA_LOG_RING_SLOT_SIZE;
    header->capacity = ASTRA_LOG_RING_CAPACITY;
    header->data_offset = ASTRA_LOG_RING_HEADER_SIZE;
}

static void
emit(uint8_t *area, AstraLogProducer *p, uint64_t monotonic, const char *text)
{
    AstraLogRecordFields f;

    memset(&f, 0, sizeof(f));
    f.monotonic_high = (uint32_t)(monotonic >> 32);
    f.monotonic_low = (uint32_t)monotonic;
    f.level = ASTRA_EVENT_LEVEL_NOTICE;
    f.stream = ASTRA_LOG_STREAM_STDOUT;
    (void)area;
    assert(astra_log_producer_write(p, &f, text, (uint32_t)strlen(text)) ==
           ASTRA_LOG_WRITE_OK);
}

static void
on_consumed(void *context, AstraLogClient *client, uint32_t position)
{
    (void)context;
    ++consumed_calls;
    __atomic_store_n(
        &((AstraBulkRingHeader *)(uintptr_t)client->ring)->consumer_position,
        position, __ATOMIC_RELEASE);
}

static AstraLogSetup setup(const char *ident);

static uint32_t
count_of(const char *haystack, const char *needle)
{
    uint32_t count = 0u;

    for (const char *at = strstr(haystack, needle); at != NULL;
         at = strstr(at + 1, needle))
        ++count;
    return count;
}

/* Publish records until the ring is full: a producer that never lets up. */
static uint32_t
fill(uint8_t *area, AstraLogProducer *p, uint64_t *monotonic,
     const char *text)
{
    const AstraBulkRingHeader *header = (const void *)area;
    uint32_t added = 0u;

    while (header->producer_position - header->consumer_position <
           ASTRA_LOG_RING_CAPACITY) {
        emit(area, p, (*monotonic)++, text);
        ++added;
    }
    return added;
}

static AstraLogProducer fa, fb;
static AstraLogClients fresh_clients;

/* A second, independent table for one scenario: doom on ring_a, display on
 * ring_b, both empty. */
static void
fresh(void)
{
    AstraLogSetup sa = setup("doom"), sb = setup("display");

    memset(&fresh_clients, 0, sizeof(fresh_clients));
    format(ring_a);
    format(ring_b);
    astra_log_producer_init(&fa, ring_a, NULL, NULL);
    astra_log_producer_init(&fb, ring_b, NULL, NULL);
    assert(astra_log_clients_add(&fresh_clients, 0x10000011u, &sa, sizeof(sa),
                                 ring_a, sizeof(ring_a), 5u, 6u) != NULL);
    assert(astra_log_clients_add(&fresh_clients, 0x10000012u, &sb, sizeof(sb),
                                 ring_b, sizeof(ring_b), 7u, 8u) != NULL);
}

static char big[ASTRA_LOG_DRAIN_OUT_BYTES + 1u];

/* One client refilling its ring before every pass must not starve another:
 * the quiet client's one line is out within two passes, and two flooding
 * clients drain in proportion. */
static void
test_fair(void)
{
    uint64_t at = 1000u;
    uint32_t day = 0u, pending = 0u, n;
    uint32_t seen = 0u, doom = 0u, display = 0u;

    fresh();
    emit(ring_b, &fb, 1u, "quiet");
    for (uint32_t pass = 0u; pass < 2u && !seen; ++pass) {
        (void)fill(ring_a, &fa, &at, "flood");
        n = astra_log_clients_drain(&fresh_clients, big,
                                    ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed,
                                    NULL, &day, &pending);
        big[n] = '\0';
        seen = strstr(big, "display[10000012] stdout: quiet\n") != NULL;
    }
    assert(seen);

    fresh();
    for (uint32_t pass = 0u; pass < 10u; ++pass) {
        (void)fill(ring_a, &fa, &at, "a");
        (void)fill(ring_b, &fb, &at, "b");
        n = astra_log_clients_drain(&fresh_clients, big,
                                    ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed,
                                    NULL, &day, &pending);
        big[n] = '\0';
        doom += count_of(big, " doom[");
        display += count_of(big, " display[");
    }
    assert(doom != 0u && display != 0u);
    assert(doom * 10u >= (doom + display) * 4u);
    assert(display * 10u >= (doom + display) * 4u);
}

/* A long record behind seven flooding clients is refused for want of room in
 * the first pass; the cursor then starts the next pass at it, where the whole
 * budget is open, instead of leaving it behind the flood forever. */
static void
test_cursor(void)
{
    static _Alignas(64) uint8_t rings[8][ASTRA_LOG_RING_AREA_SIZE];
    static AstraLogProducer producers[8];
    static AstraLogClients table;
    static char text[ASTRA_LOG_TEXT_MAX + 1u];
    uint64_t at = 1u;
    uint32_t day = 0u, pending = 0u, n, seen = 0u;

    memset(&table, 0, sizeof(table));
    for (uint32_t i = 0u; i < 8u; ++i) {
        AstraLogSetup s = setup(i == 7u ? "long" : "flood");

        format(rings[i]);
        astra_log_producer_init(&producers[i], rings[i], NULL, NULL);
        assert(astra_log_clients_add(&table, i + 1u, &s, sizeof(s), rings[i],
                                     sizeof(rings[i]), 0u, 0u) != NULL);
    }
    memset(text, 'L', ASTRA_LOG_TEXT_MAX);
    emit(rings[7], &producers[7], at++, text);
    for (uint32_t pass = 0u; pass < 2u && !seen; ++pass) {
        for (uint32_t i = 0u; i < 7u; ++i)
            (void)fill(rings[i], &producers[i], &at, "f");
        n = astra_log_clients_drain(&table, big, ASTRA_LOG_DRAIN_OUT_BYTES,
                                    on_consumed, NULL, &day, &pending);
        big[n] = '\0';
        seen = strstr(big, " long[00000008] stdout: LLLL") != NULL;
    }
    assert(seen);
}

/* The smallest `out` a pass accepts: the longest record plus its marker. */
#define MIN_OUT (ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX) + \
                 ASTRA_LOG_LINE_BYTES(26u))

/* `out` smaller than the backlog: nothing is consumed that does not fit, so
 * successive drains deliver every record exactly once, whole. */
static void
test_budget(void)
{
    static char all[16384];
    static char out[MIN_OUT];
    char text[8];
    uint32_t used = 0u, day = 0u, pending = 1u, n, passes = 0u;

    fresh();
    for (uint32_t i = 0u; i < 100u; ++i) {
        if (i == 7u)
            fa.dropped = 2u;
        snprintf(text, sizeof(text), "r%03u", (unsigned)i);
        emit(ring_a, &fa, 100u + i, text);
    }
    do {
        n = astra_log_clients_drain(&fresh_clients, out, sizeof(out),
                                    on_consumed, NULL, &day, &pending);
        assert(n == 0u || out[n - 1u] == '\n');
        assert(used + n < sizeof(all));
        memcpy(all + used, out, n);
        used += n;
        assert(++passes < 100u);
    } while (n != 0u || pending != 0u);
    assert(passes > 2u); /* the backlog really was more than one pass */
    all[used] = '\0';
    for (uint32_t i = 0u; i < 100u; ++i) {
        char needle[24];

        snprintf(needle, sizeof(needle), "stdout: r%03u\n", (unsigned)i);
        assert(count_of(all, needle) == 1u);
    }
    assert(count_of(all, "[2 lines dropped]\n") == 1u);
    assert(strstr(all, "[2 lines dropped]") < strstr(all, "r007"));
    assert(strstr(all, "truncated") == NULL);
    assert(count_of(all, "\n") == 101u);
}

/* An `out` that could not hold the longest record and its marker is refused
 * outright: nothing written, nothing consumed, nothing reported pending. */
static void
test_small_out_refused(void)
{
    static char out[MIN_OUT];
    uint32_t day = 0u, pending = 1u;
    uint32_t before;

    fresh();
    emit(ring_a, &fa, 100u, "kept");
    before = fresh_clients.client[0].position;
    consumed_calls = 0u;
    assert(astra_log_clients_drain(&fresh_clients, out, MIN_OUT - 1u,
                                   on_consumed, NULL, &day, &pending) == 0u);
    assert(pending == 0u && consumed_calls == 0u);
    assert(fresh_clients.client[0].position == before);
    assert(astra_log_clients_drain(&fresh_clients, out, MIN_OUT, on_consumed,
                                   NULL, &day, &pending) != 0u);
    assert(fresh_clients.client[0].position != before);
}

/* A pass stopped by its record cap -- many empty lines from one client --
 * starts the next pass at that client, like any other refusal for room. */
static void
test_record_cap_moves_cursor(void)
{
    uint32_t day = 0u, pending = 0u;

    fresh();
    emit(ring_a, &fa, 1u, "one");
    for (uint32_t i = 0u; i < ASTRA_LOG_RING_CAPACITY; ++i)
        emit(ring_b, &fb, 10u + i, "");
    assert(fresh_clients.cursor == 0u);
    (void)astra_log_clients_drain(&fresh_clients, big,
                                  ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed,
                                  NULL, &day, &pending);
    assert(pending == 1u);
    assert(fresh_clients.cursor == 1u);
}

/* Drop counts are announced in ring order: a later record (in time or in
 * the ring) carrying a smaller count, as racing producer threads can,
 * announces nothing -- never a wrapped count, never the same drop twice. */
static void
test_drop_order(void)
{
    uint32_t day = 0u, pending = 0u, n;

    fresh();
    fa.dropped = 1u;
    emit(ring_a, &fa, 100u, "x1");
    fa.dropped = 0u;
    emit(ring_a, &fa, 200u, "y0");
    fa.dropped = 1u;
    emit(ring_a, &fa, 300u, "z1");
    n = astra_log_clients_drain(&fresh_clients, big,
                                ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed, NULL,
                                &day, &pending);
    big[n] = '\0';
    assert(count_of(big, "lines dropped]") == 1u);
    assert(strstr(big, "[1 lines dropped]\n") < strstr(big, "x1"));
    assert(strstr(big, "4294967295") == NULL);

    /* Ring order 1 then 0, monotonic reversed. */
    fresh();
    fa.dropped = 1u;
    emit(ring_a, &fa, 200u, "first");
    fa.dropped = 0u;
    emit(ring_a, &fa, 100u, "second");
    n = astra_log_clients_drain(&fresh_clients, big,
                                ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed, NULL,
                                &day, &pending);
    big[n] = '\0';
    assert(count_of(big, "[1 lines dropped]\n") == 1u);
    assert(count_of(big, "lines dropped]") == 1u);
}

/* Same helper the runtime uses to build a SETUP message (log_setup() in
 * sw/userspace/runtime/src/log.c): astra_message_header_set, then the
 * payload fields. */
static AstraLogSetup
setup(const char *ident)
{
    AstraLogSetup s;

    memset(&s, 0, sizeof(s));
    astra_message_header_set(&s.header, sizeof(s), ASTRA_LOG_SETUP_MAGIC,
                             ASTRA_LOG_SETUP_VERSION, ASTRA_LOG_OPERATION_SETUP,
                             0u);
    s.magic = ASTRA_LOG_SETUP_MAGIC;
    s.version = ASTRA_LOG_SETUP_VERSION;
    s.area_size = ASTRA_LOG_RING_AREA_SIZE;
    s.ring_offset = ASTRA_LOG_RING_OFFSET;
    s.slot_size = ASTRA_LOG_RING_SLOT_SIZE;
    s.capacity = ASTRA_LOG_RING_CAPACITY;
    strcpy(s.ident, ident);
    return s;
}

int
main(void)
{
    AstraLogProducer a, b;
    AstraLogSetup sa = setup("doom"), sb = setup("display");
    char out[ASTRA_LOG_DRAIN_OUT_BYTES + 1u];
    uint32_t day = 0u;
    uint32_t pending = 1u;
    uint32_t n;

    format(ring_a);
    format(ring_b);
    astra_log_producer_init(&a, ring_a, NULL, NULL);
    astra_log_producer_init(&b, ring_b, NULL, NULL);
    assert(astra_log_clients_add(&clients, 0x10000011u, &sa, sizeof(sa),
                                 ring_a, sizeof(ring_a), 5u, 6u) != NULL);
    assert(astra_log_clients_add(&clients, 0x10000012u, &sb, sizeof(sb),
                                 ring_b, sizeof(ring_b), 7u, 8u) != NULL);

    /* Bad setups are refused. */
    {
        AstraLogSetup bad = sa;
        bad.slot_size = 256u;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad) - 1u,
                                     ring_a, sizeof(ring_a), 9u, 10u) == NULL);
        /* No NUL anywhere in `ident`: genuinely unterminated (setting only
         * the last byte would leave the earlier NUL from "doom" intact, and
         * the scan -- like event_catalog.c's terminated() and every other
         * bounded-string check in this codebase -- accepts a NUL anywhere in
         * the field, not just at the end). */
        bad = sa;
        for (uint32_t at = 0u; at < ASTRA_LOG_IDENT_MAX; ++at)
            bad.ident[at] = 'x';
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        assert(astra_log_clients_add(&clients, 1u, &sa, sizeof(sa), ring_a,
                                     4096u, 9u, 10u) == NULL); /* too small */
        /* A payload that still claims the right magic/version but whose
         * message header lies about the operation (or anything else the
         * kernel does not check) is refused, same as any other service's
         * request validator. */
        bad = sa;
        bad.header.operation = ASTRA_LOG_OPERATION_DOORBELL;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        bad.header.protocol_version = ASTRA_LOG_SETUP_VERSION + 1u;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        bad.header.total_size = sizeof(bad) - 1u; /* header disagrees with wire size */
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
    }

    emit(ring_a, &a, 300u, "a-late");
    emit(ring_b, &b, 100u, "b-early");
    emit(ring_a, &a, 200u, "a-middle");
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed, NULL,
                                &day, &pending);
    out[n] = '\0';
    /* Merged by monotonic time across clients, each line once. */
    {
        const char *early = strstr(out, "display[10000012] stdout: b-early");
        const char *middle = strstr(out, "doom[10000011] stdout: a-middle");
        const char *late = strstr(out, "doom[10000011] stdout: a-late");

        assert(early && middle && late && early < middle && middle < late);
    }
    assert(consumed_calls == 2u);
    assert(pending == 0u);

    /* Nothing new: no output, no consumed calls, nothing pending. */
    pending = 1u;
    assert(astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed,
                                   NULL, &day, &pending) == 0u);
    assert(consumed_calls == 2u);
    assert(pending == 0u);

    /* Drops are announced once, before the next line. */
    a.dropped = 3u;
    emit(ring_a, &a, 400u, "after-drop");
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES, on_consumed, NULL,
                                &day, &pending);
    out[n] = '\0';
    assert(strstr(out, "warning doom[10000011] log: [3 lines dropped]\n"));
    assert(strstr(out, "[3 lines dropped]") < strstr(out, "after-drop"));
    assert(pending == 0u);

    /* A full ring is more than one pass's output budget
     * (ASTRA_LOG_DRAIN_OUT_BYTES) holds: `pending` must say so, and the
     * caller must drain again -- "call again until 0 bytes AND pending == 0"
     * -- to reach the rest. */
    {
        uint64_t at = 1000u;

        (void)fill(ring_a, &a, &at, "x");
    }
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES,
                                on_consumed, NULL, &day, &pending);
    assert(n > 0u);
    assert(pending == 1u);
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES,
                                on_consumed, NULL, &day, &pending);
    assert(n > 0u);
    assert(pending == 0u);
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES,
                                on_consumed, NULL, &day, &pending);
    assert(n == 0u && pending == 0u); /* now actually caught up */

    /* A corrupt ring marks the client dead and stops reading it; the other
     * client still drains, and the dead one no longer counts as pending. */
    ((AstraBulkRingHeader *)ring_b)->producer_position = 100000u;
    (void)astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES,
                                  on_consumed, NULL, &day, &pending);
    assert(clients.client[1].dead);
    emit(ring_a, &a, 5000u, "survivor");
    n = astra_log_clients_drain(&clients, out, ASTRA_LOG_DRAIN_OUT_BYTES,
                                on_consumed, NULL, &day, &pending);
    out[n] = '\0';
    assert(strstr(out, "doom[10000011] stdout: survivor\n"));
    assert(pending == 0u);

    test_fair();
    test_cursor();
    test_budget();
    test_small_out_refused();
    test_record_cap_moves_cursor();
    test_drop_order();
    puts("LOG CLIENTS PASS");
    return 0;
}
